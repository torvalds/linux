/*
 * Copyright (c) 2012 Niels Provos and Nick Mathewson
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
#include "util-internal.h"
#include "event2/event-config.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <event2/event.h>
#include <signal.h>

static void
sock_perror(const char *s)
{
#ifdef _WIN32
	const char *err = evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
	fprintf(stderr, "%s: %s\n", s, err);
#else
	perror(s);
#endif
}

static void
callback1(evutil_socket_t fd, short events, void *arg)
{
}
static void
callback2(evutil_socket_t fd, short events, void *arg)
{
}

/* Testing code for event_base_dump_events().

   Notes that just because we have code to exercise this function,
   doesn't mean that *ANYTHING* about the output format is guaranteed to
   remain in the future.
 */
int
main(int argc, char **argv)
{
#define N_EVENTS 13
	int i;
	struct event *ev[N_EVENTS];
	evutil_socket_t pair1[2];
	evutil_socket_t pair2[2];
	struct timeval tv_onesec = {1,0};
	struct timeval tv_two5sec = {2,500*1000};
	const struct timeval *tv_onesec_common;
	const struct timeval *tv_two5sec_common;
	struct event_base *base;
	struct timeval now;

#ifdef _WIN32
	WORD wVersionRequested;
	WSADATA wsaData;

	wVersionRequested = MAKEWORD(2, 2);

	WSAStartup(wVersionRequested, &wsaData);
#endif

#ifdef _WIN32
#define LOCAL_SOCKETPAIR_AF AF_INET
#else
#define LOCAL_SOCKETPAIR_AF AF_UNIX
#endif

	if (evutil_make_internal_pipe_(pair1) < 0 ||
	    evutil_make_internal_pipe_(pair2) < 0) {
		sock_perror("evutil_make_internal_pipe_");
		return 1;
	}

	if (!(base = event_base_new())) {
		fprintf(stderr,"Couldn't make event_base\n");
		return 2;
	}

	tv_onesec_common = event_base_init_common_timeout(base, &tv_onesec);
	tv_two5sec_common = event_base_init_common_timeout(base, &tv_two5sec);

	ev[0] = event_new(base, pair1[0], EV_WRITE, callback1, NULL);
	ev[1] = event_new(base, pair1[1], EV_READ|EV_PERSIST, callback1, NULL);
	ev[2] = event_new(base, pair2[0], EV_WRITE|EV_PERSIST, callback2, NULL);
	ev[3] = event_new(base, pair2[1], EV_READ, callback2, NULL);

	/* For timers */
	ev[4] = evtimer_new(base, callback1, NULL);
	ev[5] = evtimer_new(base, callback1, NULL);
	ev[6] = evtimer_new(base, callback1, NULL);
	ev[7] = event_new(base, -1, EV_PERSIST, callback2, NULL);
	ev[8] = event_new(base, -1, EV_PERSIST, callback2, NULL);
	ev[9] = event_new(base, -1, EV_PERSIST, callback2, NULL);

	/* To activate */
	ev[10] = event_new(base, -1, 0, callback1, NULL);
	ev[11] = event_new(base, -1, 0, callback2, NULL);

	/* Signals */
	ev[12] = evsignal_new(base, SIGINT, callback2, NULL);

	event_add(ev[0], NULL);
	event_add(ev[1], &tv_onesec);
	event_add(ev[2], tv_onesec_common);
	event_add(ev[3], tv_two5sec_common);

	event_add(ev[4], tv_onesec_common);
	event_add(ev[5], tv_onesec_common);
	event_add(ev[6], &tv_onesec);
	event_add(ev[7], tv_two5sec_common);
	event_add(ev[8], tv_onesec_common);
	event_add(ev[9], &tv_two5sec);

	event_active(ev[10], EV_READ, 1);
	event_active(ev[11], EV_READ|EV_WRITE|EV_TIMEOUT, 1);
	event_active(ev[1], EV_READ, 1);

	event_add(ev[12], NULL);

	evutil_gettimeofday(&now,NULL);
	puts("=====expected");
	printf("Now= %ld.%06d\n",(long)now.tv_sec,(int)now.tv_usec);
	puts("Inserted:");
	printf("  %p [fd  %ld] Write\n",ev[0],(long)pair1[0]);
	printf("  %p [fd  %ld] Read Persist Timeout=T+1\n",ev[1],(long)pair1[1]);
	printf("  %p [fd  %ld] Write Persist Timeout=T+1\n",ev[2],(long)pair2[0]);
	printf("  %p [fd  %ld] Read Timeout=T+2.5\n",ev[3],(long)pair2[1]);
	printf("  %p [fd  -1] Timeout=T+1\n",ev[4]);
	printf("  %p [fd  -1] Timeout=T+1\n",ev[5]);
	printf("  %p [fd  -1] Timeout=T+1\n",ev[6]);
	printf("  %p [fd  -1] Persist Timeout=T+2.5\n",ev[7]);
	printf("  %p [fd  -1] Persist Timeout=T+1\n",ev[8]);
	printf("  %p [fd  -1] Persist Timeout=T+2.5\n",ev[9]);
	printf("  %p [sig %d] Signal Persist\n", ev[12], (int)SIGINT);

	puts("Active:");
	printf("  %p [fd  -1, priority=0] Read active\n", ev[10]);
	printf("  %p [fd  -1, priority=0] Read Write Timeout active\n", ev[11]);
	printf("  %p [fd  %ld, priority=0] Read active\n", ev[1], (long)pair1[1]);

	puts("======received");
	event_base_dump_events(base, stdout);

	for (i = 0; i < N_EVENTS; ++i) {
		event_free(ev[i]);
	}
	event_base_free(base);

	return 0;
}

