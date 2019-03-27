/*
 * Copyright 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
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
 *
 */

#include "event2/event-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/resource.h>
#endif
#include <signal.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef EVENT__HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <getopt.h>
#include <event.h>
#include <evutil.h>

/*
 * This benchmark tests how quickly we can propagate a write down a chain
 * of socket pairs.  We start by writing to the first socket pair and all
 * events will fire subsequently until the last socket pair has been reached
 * and the benchmark terminates.
 */

static int fired;
static evutil_socket_t *pipes;
static struct event *events;

static void
read_cb(evutil_socket_t fd, short which, void *arg)
{
	char ch;
	evutil_socket_t sock = (evutil_socket_t)(ev_intptr_t)arg;

	(void) recv(fd, &ch, sizeof(ch), 0);
	if (sock >= 0) {
		if (send(sock, "e", 1, 0) < 0)
			perror("send");
	}
	fired++;
}

static struct timeval *
run_once(int num_pipes)
{
	int i;
	evutil_socket_t *cp;
	static struct timeval ts, te, tv_timeout;

	events = (struct event *)calloc(num_pipes, sizeof(struct event));
	pipes = (evutil_socket_t *)calloc(num_pipes * 2, sizeof(evutil_socket_t));

	if (events == NULL || pipes == NULL) {
		perror("malloc");
		exit(1);
	}

	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
		if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, cp) == -1) {
			perror("socketpair");
			exit(1);
		}
	}

	/* measurements includes event setup */
	evutil_gettimeofday(&ts, NULL);

	/* provide a default timeout for events */
	evutil_timerclear(&tv_timeout);
	tv_timeout.tv_sec = 60;

	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
		evutil_socket_t fd = i < num_pipes - 1 ? cp[3] : -1;
		event_set(&events[i], cp[0], EV_READ, read_cb,
		    (void *)(ev_intptr_t)fd);
		event_add(&events[i], &tv_timeout);
	}

	fired = 0;

	/* kick everything off with a single write */
	if (send(pipes[1], "e", 1, 0) < 0)
		perror("send");

	event_dispatch();

	evutil_gettimeofday(&te, NULL);
	evutil_timersub(&te, &ts, &te);

	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
		event_del(&events[i]);
		evutil_closesocket(cp[0]);
		evutil_closesocket(cp[1]);
	}

	free(pipes);
	free(events);

	return (&te);
}

int
main(int argc, char **argv)
{
#ifdef HAVE_SETRLIMIT
	struct rlimit rl;
#endif
	int i, c;
	struct timeval *tv;

	int num_pipes = 100;
#ifdef _WIN32
	WSADATA WSAData;
	WSAStartup(0x101, &WSAData);
#endif

	while ((c = getopt(argc, argv, "n:")) != -1) {
		switch (c) {
		case 'n':
			num_pipes = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Illegal argument \"%c\"\n", c);
			exit(1);
		}
	}

#ifdef HAVE_SETRLIMIT 
	rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
	if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
		perror("setrlimit");
		exit(1);
	}
#endif

	event_init();

	for (i = 0; i < 25; i++) {
		tv = run_once(num_pipes);
		if (tv == NULL)
			exit(1);
		fprintf(stdout, "%ld\n",
			tv->tv_sec * 1000000L + tv->tv_usec);
	}

#ifdef _WIN32
	WSACleanup();
#endif

	exit(0);
}
