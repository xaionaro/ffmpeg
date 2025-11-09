/*
 * Android Binder handler
 *
 * Copyright (c) 2025 Dmitrii Okunev
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#if defined(__ANDROID__)

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include "config.h"
#include "libavutil/log.h"
#include "android_binder.h"

#define DEFAULT_THREAD_POOL_SIZE 4
#define MAX_THREAD_POOL_SIZE     256

static unsigned get_thread_pool_size(void)
{
    const char *tps_str = getenv("FFMPEG_ANDROID_BINDER_THREAD_POOL_SIZE");
    if (tps_str == NULL || !*tps_str) {
        av_log(NULL, AV_LOG_DEBUG,
               "android/binder: FFMPEG_ANDROID_BINDER_THREAD_POOL_SIZE not set, using default %u\n",
               DEFAULT_THREAD_POOL_SIZE);
        return DEFAULT_THREAD_POOL_SIZE;
    }

    errno = 0;
    unsigned long thread_pool_size = strtoul(tps_str, NULL, 10);
    if (errno != 0 || thread_pool_size <= 0
        || thread_pool_size > UINT32_MAX) {
        av_log(NULL, AV_LOG_ERROR,
               "android/binder: invalid value of FFMPEG_ANDROID_BINDER_THREAD_POOL_SIZE: '%s' (errno: %d), using the default one, instead: %u\n",
               tps_str, errno, DEFAULT_THREAD_POOL_SIZE);
        return DEFAULT_THREAD_POOL_SIZE;
    }

    if (thread_pool_size > MAX_THREAD_POOL_SIZE) {
        av_log(NULL, AV_LOG_WARNING,
               "android/binder: too large FFMPEG_ANDROID_BINDER_THREAD_POOL_SIZE: '%s', clamping to %d\n",
               tps_str, MAX_THREAD_POOL_SIZE);
        thread_pool_size = MAX_THREAD_POOL_SIZE;
    }

    av_log(NULL, AV_LOG_DEBUG,
           "android/binder: thread pool size: %lu\n", thread_pool_size);
    return (unsigned) thread_pool_size;
}

static void *dlopen_libbinder_ndk(void)
{
    /*
     * To make ffmpeg builds reusable at different Android versions we intentionally
     * avoid including linking with libbinder_ndk.so at the link time. Instead, we
     * resolve the symbols at runtime using dlopen()/dlsym().
     *
     * See also: https://source.android.com/docs/core/architecture/aidl/aidl-backends
     */

    void *h = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
    if (h != NULL)
        return h;

    av_log(NULL, AV_LOG_VERBOSE,
           "android/binder: libbinder_ndk.so not found; skipping binder threadpool init\n");
    return NULL;
}

void android_binder_threadpool_init(void)
{
    typedef int (*set_thread_pool_max_fn)(uint32_t);
    typedef void (*start_thread_pool_fn)(void);

    set_thread_pool_max_fn set_thread_pool_max = NULL;
    start_thread_pool_fn start_thread_pool = NULL;

    void *h = dlopen_libbinder_ndk();
    if (h == NULL) {
        return;
    }

    unsigned thead_pool_size = get_thread_pool_size();

    set_thread_pool_max =
        (set_thread_pool_max_fn) dlsym(h,
                                       "ABinderProcess_setThreadPoolMaxThreadCount");
    start_thread_pool =
        (start_thread_pool_fn) dlsym(h, "ABinderProcess_startThreadPool");

    if (start_thread_pool == NULL) {
        av_log(NULL, AV_LOG_VERBOSE,
               "android/binder: ABinderProcess_startThreadPool not found; skipping threadpool init\n");
        return;
    }

    if (set_thread_pool_max != NULL) {
        int ok = set_thread_pool_max(thead_pool_size);
        av_log(NULL, AV_LOG_DEBUG,
               "android/binder: ABinderProcess_setThreadPoolMaxThreadCount(%u) => %s\n",
               thead_pool_size, ok ? "ok" : "fail");
    } else {
        av_log(NULL, AV_LOG_DEBUG,
               "android/binder: ABinderProcess_setThreadPoolMaxThreadCount is unavailable; using the library default\n");
    }

    start_thread_pool();
    av_log(NULL, AV_LOG_DEBUG,
           "android/binder: ABinderProcess_startThreadPool() called\n");
}

#endif                          /* __ANDROID__ */
