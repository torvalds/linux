/*
 * Copyright (c) 2010-2012 Niels Provos and Nick Mathewson
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

#include "event2/event-config.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef EVENT__HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "event2/event.h"
#include "event2/util.h"
#include <time.h>

struct cpu_usage_timer {
#ifdef _WIN32
	HANDLE thread;
	FILETIME usertimeBegin;
	FILETIME kerneltimeBegin;
#else
	clock_t ticksBegin;
#endif
	struct timeval timeBegin;
};
static void
start_cpu_usage_timer(struct cpu_usage_timer *timer)
{
#ifdef _WIN32
	int r;
	FILETIME createtime, exittime;
	timer->thread = GetCurrentThread();
	r = GetThreadTimes(timer->thread, &createtime, &exittime,
	    &timer->usertimeBegin, &timer->kerneltimeBegin);
	if (r==0) printf("GetThreadTimes failed.");
#else
	timer->ticksBegin = clock();
#endif

	evutil_gettimeofday(&timer->timeBegin, NULL);
}
#ifdef _WIN32
static ev_int64_t
filetime_to_100nsec(const FILETIME *ft)
{
	/* Number of 100-nanosecond units */
	ev_int64_t n = ft->dwHighDateTime;
	n <<= 32;
	n += ft->dwLowDateTime;
	return n;
}
static double
filetime_diff(const FILETIME *ftStart, const FILETIME *ftEnd)
{
	ev_int64_t s, e, diff;
	double r;
	s = filetime_to_100nsec(ftStart);
	e = filetime_to_100nsec(ftEnd);
	diff = e - s;
	r = (double) diff;
	return r / 1.0e7;
}
#endif

static void
get_cpu_usage(struct cpu_usage_timer *timer, double *secElapsedOut,
    double *secUsedOut, double *usageOut)
{
#ifdef _WIN32
	double usertime_seconds, kerneltime_seconds;
	FILETIME createtime, exittime, usertimeEnd, kerneltimeEnd;
	int r;
#else
	clock_t ticksEnd;
#endif
	struct timeval timeEnd, timeDiff;
	double secondsPassed, secondsUsed;

#ifdef _WIN32
	r = GetThreadTimes(timer->thread, &createtime, &exittime,
	    &usertimeEnd, &kerneltimeEnd);
	if (r==0) printf("GetThreadTimes failed.");
	usertime_seconds = filetime_diff(&timer->usertimeBegin, &usertimeEnd);
	kerneltime_seconds = filetime_diff(&timer->kerneltimeBegin, &kerneltimeEnd);
	secondsUsed = kerneltime_seconds + usertime_seconds;
#else
	ticksEnd = clock();
	secondsUsed = (ticksEnd - timer->ticksBegin) / (double)CLOCKS_PER_SEC;
#endif
	evutil_gettimeofday(&timeEnd, NULL);
	evutil_timersub(&timeEnd, &timer->timeBegin, &timeDiff);
	secondsPassed = timeDiff.tv_sec + (timeDiff.tv_usec / 1.0e6);

	*secElapsedOut = secondsPassed;
	*secUsedOut = secondsUsed;
	*usageOut = secondsUsed / secondsPassed;
}

static void
write_cb(evutil_socket_t fd, short event, void *arg)
{
	printf("write callback. should only see this once\n");

	/* got what we want remove the event */
	event_del(*(struct event**)arg);

	/* opps changed my mind add it back again */
	event_add(*(struct event**)arg,NULL);

	/* not a good day for decisiveness, I really didn't want it after all */
	event_del(*(struct event**)arg);

}

static void
timeout_cb(evutil_socket_t fd, short event, void *arg)
{
	printf("timeout fired, time to end test\n");
	event_del(*(struct event**)arg);
	return;
}

int
main(int argc, char **argv)
{
	struct event* ev;
	struct event* timeout;
	struct event_base* base;

	evutil_socket_t pair[2];
	struct timeval tv;
	struct cpu_usage_timer timer;

	double usage, secPassed, secUsed;

#ifdef _WIN32
	WORD wVersionRequested;
	WSADATA wsaData;

	wVersionRequested = MAKEWORD(2, 2);

	(void) WSAStartup(wVersionRequested, &wsaData);
#endif
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		return (1);

	/* Initalize the event library */
	if (!(base = event_base_new()))
		return (1);

	/* Initalize a timeout to terminate the test */
	timeout = evtimer_new(base,timeout_cb,&timeout);
	/* and watch for writability on one end of the pipe */
	ev = event_new(base,pair[1],EV_WRITE | EV_PERSIST, write_cb, &ev);

	tv.tv_sec  = 1;
	tv.tv_usec = 500*1000;

	evtimer_add(timeout, &tv);

	event_add(ev, NULL);

	start_cpu_usage_timer(&timer);

	event_base_dispatch(base);

	event_free(ev);
	event_free(timeout);
	event_base_free(base);

	get_cpu_usage(&timer, &secPassed, &secUsed, &usage);

	/* attempt to calculate our cpu usage over the test should be
	   virtually nil */

	printf("usec used=%d, usec passed=%d, cpu usage=%.2f%%\n",
	    (int)(secUsed*1e6),
	    (int)(secPassed*1e6),
	    usage*100);

	if (usage > 50.0) /* way too high */
	  return 1;

	return 0;
}

