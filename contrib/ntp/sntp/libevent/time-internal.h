/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef TIME_INTERNAL_H_INCLUDED_
#define TIME_INTERNAL_H_INCLUDED_

#include "event2/event-config.h"
#include "evconfig-private.h"

#ifdef EVENT__HAVE_MACH_MACH_TIME_H
/* For mach_timebase_info */
#include <mach/mach_time.h>
#endif

#include <time.h>

#include "event2/util.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(EVENT__HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
#define HAVE_POSIX_MONOTONIC
#elif defined(EVENT__HAVE_MACH_ABSOLUTE_TIME)
#define HAVE_MACH_MONOTONIC
#elif defined(_WIN32)
#define HAVE_WIN32_MONOTONIC
#else
#define HAVE_FALLBACK_MONOTONIC
#endif

long evutil_tv_to_msec_(const struct timeval *tv);
void evutil_usleep_(const struct timeval *tv);

#ifdef _WIN32
typedef ULONGLONG (WINAPI *ev_GetTickCount_func)(void);
#endif

struct evutil_monotonic_timer {

#ifdef HAVE_MACH_MONOTONIC
	struct mach_timebase_info mach_timebase_units;
#endif

#ifdef HAVE_POSIX_MONOTONIC
	int monotonic_clock;
#endif

#ifdef HAVE_WIN32_MONOTONIC
	ev_GetTickCount_func GetTickCount64_fn;
	ev_GetTickCount_func GetTickCount_fn;
	ev_uint64_t last_tick_count;
	ev_uint64_t adjust_tick_count;

	ev_uint64_t first_tick;
	ev_uint64_t first_counter;
	double usec_per_count;
	int use_performance_counter;
#endif

	struct timeval adjust_monotonic_clock;
	struct timeval last_time;
};

int evutil_configure_monotonic_time_(struct evutil_monotonic_timer *mt,
    int flags);
int evutil_gettime_monotonic_(struct evutil_monotonic_timer *mt, struct timeval *tv);


#ifdef __cplusplus
}
#endif

#endif /* EVENT_INTERNAL_H_INCLUDED_ */
