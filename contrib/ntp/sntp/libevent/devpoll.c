/*
 * Copyright 2000-2009 Niels Provos <provos@citi.umich.edu>
 * Copyright 2009-2012 Niels Provos and Nick Mathewson
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
#include "evconfig-private.h"

#ifdef EVENT__HAVE_DEVPOLL

#include <sys/types.h>
#include <sys/resource.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#include <sys/devpoll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/thread.h"
#include "event-internal.h"
#include "evsignal-internal.h"
#include "log-internal.h"
#include "evmap-internal.h"
#include "evthread-internal.h"

struct devpollop {
	struct pollfd *events;
	int nevents;
	int dpfd;
	struct pollfd *changes;
	int nchanges;
};

static void *devpoll_init(struct event_base *);
static int devpoll_add(struct event_base *, int fd, short old, short events, void *);
static int devpoll_del(struct event_base *, int fd, short old, short events, void *);
static int devpoll_dispatch(struct event_base *, struct timeval *);
static void devpoll_dealloc(struct event_base *);

const struct eventop devpollops = {
	"devpoll",
	devpoll_init,
	devpoll_add,
	devpoll_del,
	devpoll_dispatch,
	devpoll_dealloc,
	1, /* need reinit */
	EV_FEATURE_FDS|EV_FEATURE_O1,
	0
};

#define NEVENT	32000

static int
devpoll_commit(struct devpollop *devpollop)
{
	/*
	 * Due to a bug in Solaris, we have to use pwrite with an offset of 0.
	 * Write is limited to 2GB of data, until it will fail.
	 */
	if (pwrite(devpollop->dpfd, devpollop->changes,
		sizeof(struct pollfd) * devpollop->nchanges, 0) == -1)
		return (-1);

	devpollop->nchanges = 0;
	return (0);
}

static int
devpoll_queue(struct devpollop *devpollop, int fd, int events) {
	struct pollfd *pfd;

	if (devpollop->nchanges >= devpollop->nevents) {
		/*
		 * Change buffer is full, must commit it to /dev/poll before
		 * adding more
		 */
		if (devpoll_commit(devpollop) != 0)
			return (-1);
	}

	pfd = &devpollop->changes[devpollop->nchanges++];
	pfd->fd = fd;
	pfd->events = events;
	pfd->revents = 0;

	return (0);
}

static void *
devpoll_init(struct event_base *base)
{
	int dpfd, nfiles = NEVENT;
	struct rlimit rl;
	struct devpollop *devpollop;

	if (!(devpollop = mm_calloc(1, sizeof(struct devpollop))))
		return (NULL);

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 &&
	    rl.rlim_cur != RLIM_INFINITY)
		nfiles = rl.rlim_cur;

	/* Initialize the kernel queue */
	if ((dpfd = evutil_open_closeonexec_("/dev/poll", O_RDWR, 0)) == -1) {
		event_warn("open: /dev/poll");
		mm_free(devpollop);
		return (NULL);
	}

	devpollop->dpfd = dpfd;

	/* Initialize fields */
	/* FIXME: allocating 'nfiles' worth of space here can be
	 * expensive and unnecessary.  See how epoll.c does it instead. */
	devpollop->events = mm_calloc(nfiles, sizeof(struct pollfd));
	if (devpollop->events == NULL) {
		mm_free(devpollop);
		close(dpfd);
		return (NULL);
	}
	devpollop->nevents = nfiles;

	devpollop->changes = mm_calloc(nfiles, sizeof(struct pollfd));
	if (devpollop->changes == NULL) {
		mm_free(devpollop->events);
		mm_free(devpollop);
		close(dpfd);
		return (NULL);
	}

	evsig_init_(base);

	return (devpollop);
}

static int
devpoll_dispatch(struct event_base *base, struct timeval *tv)
{
	struct devpollop *devpollop = base->evbase;
	struct pollfd *events = devpollop->events;
	struct dvpoll dvp;
	int i, res, timeout = -1;

	if (devpollop->nchanges)
		devpoll_commit(devpollop);

	if (tv != NULL)
		timeout = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;

	dvp.dp_fds = devpollop->events;
	dvp.dp_nfds = devpollop->nevents;
	dvp.dp_timeout = timeout;

	EVBASE_RELEASE_LOCK(base, th_base_lock);

	res = ioctl(devpollop->dpfd, DP_POLL, &dvp);

	EVBASE_ACQUIRE_LOCK(base, th_base_lock);

	if (res == -1) {
		if (errno != EINTR) {
			event_warn("ioctl: DP_POLL");
			return (-1);
		}

		return (0);
	}

	event_debug(("%s: devpoll_wait reports %d", __func__, res));

	for (i = 0; i < res; i++) {
		int which = 0;
		int what = events[i].revents;

		if (what & POLLHUP)
			what |= POLLIN | POLLOUT;
		else if (what & POLLERR)
			what |= POLLIN | POLLOUT;

		if (what & POLLIN)
			which |= EV_READ;
		if (what & POLLOUT)
			which |= EV_WRITE;

		if (!which)
			continue;

		/* XXX(niels): not sure if this works for devpoll */
		evmap_io_active_(base, events[i].fd, which);
	}

	return (0);
}


static int
devpoll_add(struct event_base *base, int fd, short old, short events, void *p)
{
	struct devpollop *devpollop = base->evbase;
	int res;
	(void)p;

	/*
	 * It's not necessary to OR the existing read/write events that we
	 * are currently interested in with the new event we are adding.
	 * The /dev/poll driver ORs any new events with the existing events
	 * that it has cached for the fd.
	 */

	res = 0;
	if (events & EV_READ)
		res |= POLLIN;
	if (events & EV_WRITE)
		res |= POLLOUT;

	if (devpoll_queue(devpollop, fd, res) != 0)
		return (-1);

	return (0);
}

static int
devpoll_del(struct event_base *base, int fd, short old, short events, void *p)
{
	struct devpollop *devpollop = base->evbase;
	int res;
	(void)p;

	res = 0;
	if (events & EV_READ)
		res |= POLLIN;
	if (events & EV_WRITE)
		res |= POLLOUT;

	/*
	 * The only way to remove an fd from the /dev/poll monitored set is
	 * to use POLLREMOVE by itself.  This removes ALL events for the fd
	 * provided so if we care about two events and are only removing one
	 * we must re-add the other event after POLLREMOVE.
	 */

	if (devpoll_queue(devpollop, fd, POLLREMOVE) != 0)
		return (-1);

	if ((res & (POLLIN|POLLOUT)) != (POLLIN|POLLOUT)) {
		/*
		 * We're not deleting all events, so we must resubmit the
		 * event that we are still interested in if one exists.
		 */

		if ((res & POLLIN) && (old & EV_WRITE)) {
			/* Deleting read, still care about write */
			devpoll_queue(devpollop, fd, POLLOUT);
		} else if ((res & POLLOUT) && (old & EV_READ)) {
			/* Deleting write, still care about read */
			devpoll_queue(devpollop, fd, POLLIN);
		}
	}

	return (0);
}

static void
devpoll_dealloc(struct event_base *base)
{
	struct devpollop *devpollop = base->evbase;

	evsig_dealloc_(base);
	if (devpollop->events)
		mm_free(devpollop->events);
	if (devpollop->changes)
		mm_free(devpollop->changes);
	if (devpollop->dpfd >= 0)
		close(devpollop->dpfd);

	memset(devpollop, 0, sizeof(struct devpollop));
	mm_free(devpollop);
}

#endif /* EVENT__HAVE_DEVPOLL */
