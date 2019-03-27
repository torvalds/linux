/*
 * Copyright (c) 2002-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2013 Niels Provos and Nick Mathewson
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
#include "../util-internal.h"
#include "event2/event-config.h"

#ifdef _WIN32
#include <winsock2.h>
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

#include <event.h>
#include <evutil.h>

struct timeval timeout = {3, 0};

static void
closed_cb(evutil_socket_t fd, short event, void *arg)
{
	if (EV_TIMEOUT & event) {
		printf("%s: Timeout!\n", __func__);
		exit(1);
	}

	if (EV_CLOSED & event) {
		printf("%s: detected socket close with success\n", __func__);
		return;
	}

	printf("%s: unable to detect socket close\n", __func__);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct event_base *base;
	struct event_config *cfg;
	struct event *ev;
	const char *test = "test string";
	evutil_socket_t pair[2];

	/* Initialize the library and check if the backend
	   supports EV_FEATURE_EARLY_CLOSE
	*/
	cfg = event_config_new();
	event_config_require_features(cfg, EV_FEATURE_EARLY_CLOSE);
	base = event_base_new_with_config(cfg);
	event_config_free(cfg);
	if (!base) {
		/* Backend doesn't support EV_FEATURE_EARLY_CLOSE */
		return 0;
	}

	/* Create a pair of sockets */
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		return (1);

	/* Send some data on socket 0 and immediately close it */
	if (send(pair[0], test, (int)strlen(test)+1, 0) < 0)
		return (1);
	shutdown(pair[0], EVUTIL_SHUT_WR);

	/* Dispatch */
	ev = event_new(base, pair[1], EV_CLOSED | EV_TIMEOUT, closed_cb, event_self_cbarg());
	event_add(ev, &timeout);
	event_base_dispatch(base);

	/* Finalize library */
	event_base_free(base);
	return 0;
}

