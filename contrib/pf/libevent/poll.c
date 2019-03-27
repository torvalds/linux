/*	$OpenBSD: poll.c,v 1.2 2002/06/25 15:50:15 mickey Exp $	*/

/*
 * Copyright 2000-2003 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <sys/_time.h>
#endif
#include <sys/queue.h>
#include <sys/tree.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef CHECK_INVARIANTS
#include <assert.h>
#endif

#include "event.h"
#include "event-internal.h"
#include "evsignal.h"
#include "log.h"

extern volatile sig_atomic_t evsignal_caught;

struct pollop {
	int event_count;		/* Highest number alloc */
	int nfds;                       /* Size of event_* */
	int fd_count;                   /* Size of idxplus1_by_fd */
	struct pollfd *event_set;
	struct event **event_r_back;
	struct event **event_w_back;
	int *idxplus1_by_fd; /* Index into event_set by fd; we add 1 so
			      * that 0 (which is easy to memset) can mean
			      * "no entry." */
};

void *poll_init	(void);
int poll_add		(void *, struct event *);
int poll_del		(void *, struct event *);
int poll_recalc		(struct event_base *, void *, int);
int poll_dispatch	(struct event_base *, void *, struct timeval *);
void poll_dealloc	(void *);

const struct eventop pollops = {
	"poll",
	poll_init,
	poll_add,
	poll_del,
	poll_recalc,
	poll_dispatch,
	poll_dealloc
};

void *
poll_init(void)
{
	struct pollop *pollop;

	/* Disable poll when this environment variable is set */
	if (getenv("EVENT_NOPOLL"))
		return (NULL);

	if (!(pollop = calloc(1, sizeof(struct pollop))))
		return (NULL);

	evsignal_init();

	return (pollop);
}

/*
 * Called with the highest fd that we know about.  If it is 0, completely
 * recalculate everything.
 */

int
poll_recalc(struct event_base *base, void *arg, int max)
{
	return (0);
}

#ifdef CHECK_INVARIANTS
static void
poll_check_ok(struct pollop *pop)
{
	int i, idx;
	struct event *ev;

	for (i = 0; i < pop->fd_count; ++i) {
		idx = pop->idxplus1_by_fd[i]-1;
		if (idx < 0)
			continue;
		assert(pop->event_set[idx].fd == i);
		if (pop->event_set[idx].events & POLLIN) {
			ev = pop->event_r_back[idx];
			assert(ev);
			assert(ev->ev_events & EV_READ);
			assert(ev->ev_fd == i);
		}
		if (pop->event_set[idx].events & POLLOUT) {
			ev = pop->event_w_back[idx];
			assert(ev);
			assert(ev->ev_events & EV_WRITE);
			assert(ev->ev_fd == i);
		}
	}
	for (i = 0; i < pop->nfds; ++i) {
		struct pollfd *pfd = &pop->event_set[i];
		assert(pop->idxplus1_by_fd[pfd->fd] == i+1);
	}
}
#else
#define poll_check_ok(pop)
#endif

int
poll_dispatch(struct event_base *base, void *arg, struct timeval *tv)
{
	int res, i, sec, nfds;
	struct pollop *pop = arg;

	poll_check_ok(pop);
	sec = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
	nfds = pop->nfds;
	res = poll(pop->event_set, nfds, sec);

	if (res == -1) {
		if (errno != EINTR) {
                        event_warn("poll");
			return (-1);
		}

		evsignal_process();
		return (0);
	} else if (evsignal_caught)
		evsignal_process();

	event_debug(("%s: poll reports %d", __func__, res));

	if (res == 0)
		return (0);

	for (i = 0; i < nfds; i++) {
		int what = pop->event_set[i].revents;
		struct event *r_ev = NULL, *w_ev = NULL;
		if (!what)
			continue;

		res = 0;

		/* If the file gets closed notify */
		if (what & (POLLHUP|POLLERR))
			what |= POLLIN|POLLOUT;
		if (what & POLLIN) {
			res |= EV_READ;
			r_ev = pop->event_r_back[i];
		}
		if (what & POLLOUT) {
			res |= EV_WRITE;
			w_ev = pop->event_w_back[i];
		}
		if (res == 0)
			continue;

		if (r_ev && (res & r_ev->ev_events)) {
			if (!(r_ev->ev_events & EV_PERSIST))
				event_del(r_ev);
			event_active(r_ev, res & r_ev->ev_events, 1);
		}
		if (w_ev && w_ev != r_ev && (res & w_ev->ev_events)) {
			if (!(w_ev->ev_events & EV_PERSIST))
				event_del(w_ev);
			event_active(w_ev, res & w_ev->ev_events, 1);
		}
	}

	return (0);
}

int
poll_add(void *arg, struct event *ev)
{
	struct pollop *pop = arg;
	struct pollfd *pfd = NULL;
	int i;

	if (ev->ev_events & EV_SIGNAL)
		return (evsignal_add(ev));
	if (!(ev->ev_events & (EV_READ|EV_WRITE)))
		return (0);

	poll_check_ok(pop);
	if (pop->nfds + 1 >= pop->event_count) {
		struct pollfd *tmp_event_set;
		struct event **tmp_event_r_back;
		struct event **tmp_event_w_back;
		int tmp_event_count;

		if (pop->event_count < 32)
			tmp_event_count = 32;
		else
			tmp_event_count = pop->event_count * 2;

		/* We need more file descriptors */
		tmp_event_set = realloc(pop->event_set,
				 tmp_event_count * sizeof(struct pollfd));
		if (tmp_event_set == NULL) {
			event_warn("realloc");
			return (-1);
		}
		pop->event_set = tmp_event_set;

		tmp_event_r_back = realloc(pop->event_r_back,
			    tmp_event_count * sizeof(struct event *));
		if (tmp_event_r_back == NULL) {
			/* event_set overallocated; that's okay. */
			event_warn("realloc");
			return (-1);
		}
		pop->event_r_back = tmp_event_r_back;

		tmp_event_w_back = realloc(pop->event_w_back,
			    tmp_event_count * sizeof(struct event *));
		if (tmp_event_w_back == NULL) {
			/* event_set and event_r_back overallocated; that's
			 * okay. */
			event_warn("realloc");
			return (-1);
		}
		pop->event_w_back = tmp_event_w_back;

		pop->event_count = tmp_event_count;
	}
	if (ev->ev_fd >= pop->fd_count) {
		int *tmp_idxplus1_by_fd;
		int new_count;
		if (pop->fd_count < 32)
			new_count = 32;
		else
			new_count = pop->fd_count * 2;
		while (new_count <= ev->ev_fd)
			new_count *= 2;
		tmp_idxplus1_by_fd =
			realloc(pop->idxplus1_by_fd, new_count * sizeof(int));
		if (tmp_idxplus1_by_fd == NULL) {
			event_warn("realloc");
			return (-1);
		}
		pop->idxplus1_by_fd = tmp_idxplus1_by_fd;
		memset(pop->idxplus1_by_fd + pop->fd_count,
		       0, sizeof(int)*(new_count - pop->fd_count));
		pop->fd_count = new_count;
	}

	i = pop->idxplus1_by_fd[ev->ev_fd] - 1;
	if (i >= 0) {
		pfd = &pop->event_set[i];
	} else {
		i = pop->nfds++;
		pfd = &pop->event_set[i];
		pfd->events = 0;
		pfd->fd = ev->ev_fd;
		pop->event_w_back[i] = pop->event_r_back[i] = NULL;
		pop->idxplus1_by_fd[ev->ev_fd] = i + 1;
	}

	pfd->revents = 0;
	if (ev->ev_events & EV_WRITE) {
		pfd->events |= POLLOUT;
		pop->event_w_back[i] = ev;
	}
	if (ev->ev_events & EV_READ) {
		pfd->events |= POLLIN;
		pop->event_r_back[i] = ev;
	}
	poll_check_ok(pop);

	return (0);
}

/*
 * Nothing to be done here.
 */

int
poll_del(void *arg, struct event *ev)
{
	struct pollop *pop = arg;
	struct pollfd *pfd = NULL;
	int i;

	if (ev->ev_events & EV_SIGNAL)
		return (evsignal_del(ev));

	if (!(ev->ev_events & (EV_READ|EV_WRITE)))
		return (0);

	poll_check_ok(pop);
	i = pop->idxplus1_by_fd[ev->ev_fd] - 1;
	if (i < 0)
		return (-1);

	/* Do we still want to read or write? */
	pfd = &pop->event_set[i];
	if (ev->ev_events & EV_READ) {
		pfd->events &= ~POLLIN;
		pop->event_r_back[i] = NULL;
	}
	if (ev->ev_events & EV_WRITE) {
		pfd->events &= ~POLLOUT;
		pop->event_w_back[i] = NULL;
	}
	poll_check_ok(pop);
	if (pfd->events)
		/* Another event cares about that fd. */
		return (0);

	/* Okay, so we aren't interested in that fd anymore. */
	pop->idxplus1_by_fd[ev->ev_fd] = 0;

	--pop->nfds;
	if (i != pop->nfds) {
		/* 
		 * Shift the last pollfd down into the now-unoccupied
		 * position.
		 */
		memcpy(&pop->event_set[i], &pop->event_set[pop->nfds],
		       sizeof(struct pollfd));
		pop->event_r_back[i] = pop->event_r_back[pop->nfds];
		pop->event_w_back[i] = pop->event_w_back[pop->nfds];
		pop->idxplus1_by_fd[pop->event_set[i].fd] = i + 1;
	}

	poll_check_ok(pop);
	return (0);
}

void
poll_dealloc(void *arg)
{
	struct pollop *pop = arg;

	if (pop->event_set)
		free(pop->event_set);
	if (pop->event_r_back)
		free(pop->event_r_back);
	if (pop->event_w_back)
		free(pop->event_w_back);
	if (pop->idxplus1_by_fd)
		free(pop->idxplus1_by_fd);

	memset(pop, 0, sizeof(struct pollop));
	free(pop);
}
