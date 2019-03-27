/*
 * Submitted by David Pacheco (dp.spambait@gmail.com)
 *
 * Copyright 2006-2007 Niels Provos
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SUN MICROSYSTEMS, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SUN MICROSYSTEMS, INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2007 Sun Microsystems. All rights reserved.
 * Use is subject to license terms.
 */

/*
 * evport.c: event backend using Solaris 10 event ports. See port_create(3C).
 * This implementation is loosely modeled after the one used for select(2) (in
 * select.c).
 *
 * The outstanding events are tracked in a data structure called evport_data.
 * Each entry in the ed_fds array corresponds to a file descriptor, and contains
 * pointers to the read and write events that correspond to that fd. (That is,
 * when the file is readable, the "read" event should handle it, etc.)
 *
 * evport_add and evport_del update this data structure. evport_dispatch uses it
 * to determine where to callback when an event occurs (which it gets from
 * port_getn).
 *
 * Helper functions are used: grow() grows the file descriptor array as
 * necessary when large fd's come in. reassociate() takes care of maintaining
 * the proper file-descriptor/event-port associations.
 *
 * As in the select(2) implementation, signals are handled by evsignal.
 */

#include "event2/event-config.h"
#include "evconfig-private.h"

#ifdef EVENT__HAVE_EVENT_PORTS

#include <sys/time.h>
#include <sys/queue.h>
#include <errno.h>
#include <poll.h>
#include <port.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "event2/thread.h"

#include "evthread-internal.h"
#include "event-internal.h"
#include "log-internal.h"
#include "evsignal-internal.h"
#include "evmap-internal.h"

#define INITIAL_EVENTS_PER_GETN 8
#define MAX_EVENTS_PER_GETN 4096

/*
 * Per-file-descriptor information about what events we're subscribed to. These
 * fields are NULL if no event is subscribed to either of them.
 */

struct fd_info {
	/* combinations of EV_READ and EV_WRITE */
	short fdi_what;
	/* Index of this fd within ed_pending, plus 1.  Zero if this fd is
	 * not in ed_pending.  (The +1 is a hack so that memset(0) will set
	 * it to a nil index. */
	int pending_idx_plus_1;
};

#define FDI_HAS_READ(fdi)  ((fdi)->fdi_what & EV_READ)
#define FDI_HAS_WRITE(fdi) ((fdi)->fdi_what & EV_WRITE)
#define FDI_HAS_EVENTS(fdi) (FDI_HAS_READ(fdi) || FDI_HAS_WRITE(fdi))
#define FDI_TO_SYSEVENTS(fdi) (FDI_HAS_READ(fdi) ? POLLIN : 0) | \
    (FDI_HAS_WRITE(fdi) ? POLLOUT : 0)

struct evport_data {
	int		ed_port;	/* event port for system events  */
	/* How many elements of ed_pending should we look at? */
	int ed_npending;
	/* How many elements are allocated in ed_pending and pevtlist? */
	int ed_maxevents;
	/* fdi's that we need to reassoc */
	int *ed_pending;
	/* storage space for incoming events. */ 
	port_event_t *ed_pevtlist;
	
};

static void*	evport_init(struct event_base *);
static int evport_add(struct event_base *, int fd, short old, short events, void *);
static int evport_del(struct event_base *, int fd, short old, short events, void *);
static int	evport_dispatch(struct event_base *, struct timeval *);
static void	evport_dealloc(struct event_base *);
static int	grow(struct evport_data *, int min_events);

const struct eventop evportops = {
	"evport",
	evport_init,
	evport_add,
	evport_del,
	evport_dispatch,
	evport_dealloc,
	1, /* need reinit */
	0, /* features */
	sizeof(struct fd_info), /* fdinfo length */
};

/*
 * Initialize the event port implementation.
 */

static void*
evport_init(struct event_base *base)
{
	struct evport_data *evpd;

	if (!(evpd = mm_calloc(1, sizeof(struct evport_data))))
		return (NULL);

	if ((evpd->ed_port = port_create()) == -1) {
		mm_free(evpd);
		return (NULL);
	}

	if (grow(evpd, INITIAL_EVENTS_PER_GETN) < 0) {
		close(evpd->ed_port);
		mm_free(evpd);
		return NULL;
	}
		
	evpd->ed_npending = 0;

	evsig_init_(base);

	return (evpd);
}

static int
grow(struct evport_data *data, int min_events)
{
	int newsize;
	int *new_pending;
	port_event_t *new_pevtlist;
	if (data->ed_maxevents) {
		newsize = data->ed_maxevents;
		do {
			newsize *= 2;
		} while (newsize < min_events);
	} else {
		newsize = min_events;
	}

	new_pending = mm_realloc(data->ed_pending, sizeof(int)*newsize);
	if (new_pending == NULL)
		return -1;
	data->ed_pending = new_pending;
	new_pevtlist = mm_realloc(data->ed_pevtlist, sizeof(port_event_t)*newsize);
	if (new_pevtlist == NULL)
		return -1;
	data->ed_pevtlist = new_pevtlist; 

	data->ed_maxevents = newsize;
	return 0;
}

#ifdef CHECK_INVARIANTS
/*
 * Checks some basic properties about the evport_data structure. Because it
 * checks all file descriptors, this function can be expensive when the maximum
 * file descriptor ever used is rather large.
 */

static void
check_evportop(struct evport_data *evpd)
{
	EVUTIL_ASSERT(evpd);
	EVUTIL_ASSERT(evpd->ed_port > 0);
}

/*
 * Verifies very basic integrity of a given port_event.
 */
static void
check_event(port_event_t* pevt)
{
	/*
	 * We've only registered for PORT_SOURCE_FD events. The only
	 * other thing we can legitimately receive is PORT_SOURCE_ALERT,
	 * but since we're not using port_alert either, we can assume
	 * PORT_SOURCE_FD.
	 */
	EVUTIL_ASSERT(pevt->portev_source == PORT_SOURCE_FD);
}

#else
#define check_evportop(epop)
#define check_event(pevt)
#endif /* CHECK_INVARIANTS */

/*
 * (Re)associates the given file descriptor with the event port. The OS events
 * are specified (implicitly) from the fd_info struct.
 */
static int
reassociate(struct evport_data *epdp, struct fd_info *fdip, int fd)
{
	int sysevents = FDI_TO_SYSEVENTS(fdip);

	if (sysevents != 0) {
		if (port_associate(epdp->ed_port, PORT_SOURCE_FD,
				   fd, sysevents, fdip) == -1) {
			event_warn("port_associate");
			return (-1);
		}
	}

	check_evportop(epdp);

	return (0);
}

/*
 * Main event loop - polls port_getn for some number of events, and processes
 * them.
 */

static int
evport_dispatch(struct event_base *base, struct timeval *tv)
{
	int i, res;
	struct evport_data *epdp = base->evbase;
	port_event_t *pevtlist = epdp->ed_pevtlist;

	/*
	 * port_getn will block until it has at least nevents events. It will
	 * also return how many it's given us (which may be more than we asked
	 * for, as long as it's less than our maximum (ed_maxevents)) in
	 * nevents.
	 */
	int nevents = 1;

	/*
	 * We have to convert a struct timeval to a struct timespec
	 * (only difference is nanoseconds vs. microseconds). If no time-based
	 * events are active, we should wait for I/O (and tv == NULL).
	 */
	struct timespec ts;
	struct timespec *ts_p = NULL;
	if (tv != NULL) {
		ts.tv_sec = tv->tv_sec;
		ts.tv_nsec = tv->tv_usec * 1000;
		ts_p = &ts;
	}

	/*
	 * Before doing anything else, we need to reassociate the events we hit
	 * last time which need reassociation. See comment at the end of the
	 * loop below.
	 */
	for (i = 0; i < epdp->ed_npending; ++i) {
		struct fd_info *fdi = NULL;
		const int fd = epdp->ed_pending[i];
		if (fd != -1) {
			/* We might have cleared out this event; we need
			 * to be sure that it's still set. */
			fdi = evmap_io_get_fdinfo_(&base->io, fd);
		}

		if (fdi != NULL && FDI_HAS_EVENTS(fdi)) {
			reassociate(epdp, fdi, fd);
			/* epdp->ed_pending[i] = -1; */
			fdi->pending_idx_plus_1 = 0;
		}
	}

	EVBASE_RELEASE_LOCK(base, th_base_lock);

	res = port_getn(epdp->ed_port, pevtlist, epdp->ed_maxevents,
	    (unsigned int *) &nevents, ts_p);

	EVBASE_ACQUIRE_LOCK(base, th_base_lock);

	if (res == -1) {
		if (errno == EINTR || errno == EAGAIN) {
			return (0);
		} else if (errno == ETIME) {
			if (nevents == 0)
				return (0);
		} else {
			event_warn("port_getn");
			return (-1);
		}
	}

	event_debug(("%s: port_getn reports %d events", __func__, nevents));

	for (i = 0; i < nevents; ++i) {
		port_event_t *pevt = &pevtlist[i];
		int fd = (int) pevt->portev_object;
		struct fd_info *fdi = pevt->portev_user;
		/*EVUTIL_ASSERT(evmap_io_get_fdinfo_(&base->io, fd) == fdi);*/

		check_evportop(epdp);
		check_event(pevt);
		epdp->ed_pending[i] = fd;
		fdi->pending_idx_plus_1 = i + 1;

		/*
		 * Figure out what kind of event it was
		 * (because we have to pass this to the callback)
		 */
		res = 0;
		if (pevt->portev_events & (POLLERR|POLLHUP)) {
			res = EV_READ | EV_WRITE;
		} else {
			if (pevt->portev_events & POLLIN)
				res |= EV_READ;
			if (pevt->portev_events & POLLOUT)
				res |= EV_WRITE;
		}

		/*
		 * Check for the error situations or a hangup situation
		 */
		if (pevt->portev_events & (POLLERR|POLLHUP|POLLNVAL))
			res |= EV_READ|EV_WRITE;

		evmap_io_active_(base, fd, res);
	} /* end of all events gotten */
	epdp->ed_npending = nevents;

	if (nevents == epdp->ed_maxevents &&
	    epdp->ed_maxevents < MAX_EVENTS_PER_GETN) {
		/* we used all the space this time.  We should be ready
		 * for more events next time around. */
		grow(epdp, epdp->ed_maxevents * 2);
	}

	check_evportop(epdp);

	return (0);
}


/*
 * Adds the given event (so that you will be notified when it happens via
 * the callback function).
 */

static int
evport_add(struct event_base *base, int fd, short old, short events, void *p)
{
	struct evport_data *evpd = base->evbase;
	struct fd_info *fdi = p;

	check_evportop(evpd);

	fdi->fdi_what |= events;

	return reassociate(evpd, fdi, fd);
}

/*
 * Removes the given event from the list of events to wait for.
 */

static int
evport_del(struct event_base *base, int fd, short old, short events, void *p)
{
	struct evport_data *evpd = base->evbase;
	struct fd_info *fdi = p;
	int associated = ! fdi->pending_idx_plus_1;

	check_evportop(evpd);

	fdi->fdi_what &= ~(events &(EV_READ|EV_WRITE));

	if (associated) {
		if (!FDI_HAS_EVENTS(fdi) &&
		    port_dissociate(evpd->ed_port, PORT_SOURCE_FD, fd) == -1) {
			/*
			 * Ignore EBADFD error the fd could have been closed
			 * before event_del() was called.
			 */
			if (errno != EBADFD) {
				event_warn("port_dissociate");
				return (-1);
			}
		} else {
			if (FDI_HAS_EVENTS(fdi)) {
				return (reassociate(evpd, fdi, fd));
			}
		}
	} else {
		if ((fdi->fdi_what & (EV_READ|EV_WRITE)) == 0) {
			const int i = fdi->pending_idx_plus_1 - 1;
			EVUTIL_ASSERT(evpd->ed_pending[i] == fd);
			evpd->ed_pending[i] = -1;
			fdi->pending_idx_plus_1 = 0;
		}
	}
	return 0;
}


static void
evport_dealloc(struct event_base *base)
{
	struct evport_data *evpd = base->evbase;

	evsig_dealloc_(base);

	close(evpd->ed_port);

	if (evpd->ed_pending)
		mm_free(evpd->ed_pending);
	if (evpd->ed_pevtlist)
		mm_free(evpd->ed_pevtlist);

	mm_free(evpd);
}

#endif /* EVENT__HAVE_EVENT_PORTS */
