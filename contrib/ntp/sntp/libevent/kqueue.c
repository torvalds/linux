/*	$OpenBSD: kqueue.c,v 1.5 2002/07/10 14:41:31 art Exp $	*/

/*
 * Copyright 2000-2007 Niels Provos <provos@citi.umich.edu>
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

#ifdef EVENT__HAVE_KQUEUE

#include <sys/types.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#include <sys/event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef EVENT__HAVE_INTTYPES_H
#include <inttypes.h>
#endif

/* Some platforms apparently define the udata field of struct kevent as
 * intptr_t, whereas others define it as void*.  There doesn't seem to be an
 * easy way to tell them apart via autoconf, so we need to use OS macros. */
#if defined(EVENT__HAVE_INTTYPES_H) && !defined(__OpenBSD__) && !defined(__FreeBSD__) && !defined(__darwin__) && !defined(__APPLE__)
#define PTR_TO_UDATA(x)	((intptr_t)(x))
#define INT_TO_UDATA(x) ((intptr_t)(x))
#else
#define PTR_TO_UDATA(x)	(x)
#define INT_TO_UDATA(x) ((void*)(x))
#endif

#include "event-internal.h"
#include "log-internal.h"
#include "evmap-internal.h"
#include "event2/thread.h"
#include "evthread-internal.h"
#include "changelist-internal.h"

#include "kqueue-internal.h"

#define NEVENT		64

struct kqop {
	struct kevent *changes;
	int changes_size;

	struct kevent *events;
	int events_size;
	int kq;
	int notify_event_added;
	pid_t pid;
};

static void kqop_free(struct kqop *kqop);

static void *kq_init(struct event_base *);
static int kq_sig_add(struct event_base *, int, short, short, void *);
static int kq_sig_del(struct event_base *, int, short, short, void *);
static int kq_dispatch(struct event_base *, struct timeval *);
static void kq_dealloc(struct event_base *);

const struct eventop kqops = {
	"kqueue",
	kq_init,
	event_changelist_add_,
	event_changelist_del_,
	kq_dispatch,
	kq_dealloc,
	1 /* need reinit */,
    EV_FEATURE_ET|EV_FEATURE_O1|EV_FEATURE_FDS,
	EVENT_CHANGELIST_FDINFO_SIZE
};

static const struct eventop kqsigops = {
	"kqueue_signal",
	NULL,
	kq_sig_add,
	kq_sig_del,
	NULL,
	NULL,
	1 /* need reinit */,
	0,
	0
};

static void *
kq_init(struct event_base *base)
{
	int kq = -1;
	struct kqop *kqueueop = NULL;

	if (!(kqueueop = mm_calloc(1, sizeof(struct kqop))))
		return (NULL);

/* Initialize the kernel queue */

	if ((kq = kqueue()) == -1) {
		event_warn("kqueue");
		goto err;
	}

	kqueueop->kq = kq;

	kqueueop->pid = getpid();

	/* Initialize fields */
	kqueueop->changes = mm_calloc(NEVENT, sizeof(struct kevent));
	if (kqueueop->changes == NULL)
		goto err;
	kqueueop->events = mm_calloc(NEVENT, sizeof(struct kevent));
	if (kqueueop->events == NULL)
		goto err;
	kqueueop->events_size = kqueueop->changes_size = NEVENT;

	/* Check for Mac OS X kqueue bug. */
	memset(&kqueueop->changes[0], 0, sizeof kqueueop->changes[0]);
	kqueueop->changes[0].ident = -1;
	kqueueop->changes[0].filter = EVFILT_READ;
	kqueueop->changes[0].flags = EV_ADD;
	/*
	 * If kqueue works, then kevent will succeed, and it will
	 * stick an error in events[0].  If kqueue is broken, then
	 * kevent will fail.
	 */
	if (kevent(kq,
		kqueueop->changes, 1, kqueueop->events, NEVENT, NULL) != 1 ||
	    (int)kqueueop->events[0].ident != -1 ||
	    kqueueop->events[0].flags != EV_ERROR) {
		event_warn("%s: detected broken kqueue; not using.", __func__);
		goto err;
	}

	base->evsigsel = &kqsigops;

	return (kqueueop);
err:
	if (kqueueop)
		kqop_free(kqueueop);

	return (NULL);
}

#define ADD_UDATA 0x30303

static void
kq_setup_kevent(struct kevent *out, evutil_socket_t fd, int filter, short change)
{
	memset(out, 0, sizeof(struct kevent));
	out->ident = fd;
	out->filter = filter;

	if (change & EV_CHANGE_ADD) {
		out->flags = EV_ADD;
		/* We set a magic number here so that we can tell 'add'
		 * errors from 'del' errors. */
		out->udata = INT_TO_UDATA(ADD_UDATA);
		if (change & EV_ET)
			out->flags |= EV_CLEAR;
#ifdef NOTE_EOF
		/* Make it behave like select() and poll() */
		if (filter == EVFILT_READ)
			out->fflags = NOTE_EOF;
#endif
	} else {
		EVUTIL_ASSERT(change & EV_CHANGE_DEL);
		out->flags = EV_DELETE;
	}
}

static int
kq_build_changes_list(const struct event_changelist *changelist,
    struct kqop *kqop)
{
	int i;
	int n_changes = 0;

	for (i = 0; i < changelist->n_changes; ++i) {
		struct event_change *in_ch = &changelist->changes[i];
		struct kevent *out_ch;
		if (n_changes >= kqop->changes_size - 1) {
			int newsize = kqop->changes_size * 2;
			struct kevent *newchanges;

			newchanges = mm_realloc(kqop->changes,
			    newsize * sizeof(struct kevent));
			if (newchanges == NULL) {
				event_warn("%s: realloc", __func__);
				return (-1);
			}
			kqop->changes = newchanges;
			kqop->changes_size = newsize;
		}
		if (in_ch->read_change) {
			out_ch = &kqop->changes[n_changes++];
			kq_setup_kevent(out_ch, in_ch->fd, EVFILT_READ,
			    in_ch->read_change);
		}
		if (in_ch->write_change) {
			out_ch = &kqop->changes[n_changes++];
			kq_setup_kevent(out_ch, in_ch->fd, EVFILT_WRITE,
			    in_ch->write_change);
		}
	}
	return n_changes;
}

static int
kq_grow_events(struct kqop *kqop, size_t new_size)
{
	struct kevent *newresult;

	newresult = mm_realloc(kqop->events,
	    new_size * sizeof(struct kevent));

	if (newresult) {
		kqop->events = newresult;
		kqop->events_size = new_size;
		return 0;
	} else {
		return -1;
	}
}

static int
kq_dispatch(struct event_base *base, struct timeval *tv)
{
	struct kqop *kqop = base->evbase;
	struct kevent *events = kqop->events;
	struct kevent *changes;
	struct timespec ts, *ts_p = NULL;
	int i, n_changes, res;

	if (tv != NULL) {
		TIMEVAL_TO_TIMESPEC(tv, &ts);
		ts_p = &ts;
	}

	/* Build "changes" from "base->changes" */
	EVUTIL_ASSERT(kqop->changes);
	n_changes = kq_build_changes_list(&base->changelist, kqop);
	if (n_changes < 0)
		return -1;

	event_changelist_remove_all_(&base->changelist, base);

	/* steal the changes array in case some broken code tries to call
	 * dispatch twice at once. */
	changes = kqop->changes;
	kqop->changes = NULL;

	/* Make sure that 'events' is at least as long as the list of changes:
	 * otherwise errors in the changes can get reported as a -1 return
	 * value from kevent() rather than as EV_ERROR events in the events
	 * array.
	 *
	 * (We could instead handle -1 return values from kevent() by
	 * retrying with a smaller changes array or a larger events array,
	 * but this approach seems less risky for now.)
	 */
	if (kqop->events_size < n_changes) {
		int new_size = kqop->events_size;
		do {
			new_size *= 2;
		} while (new_size < n_changes);

		kq_grow_events(kqop, new_size);
		events = kqop->events;
	}

	EVBASE_RELEASE_LOCK(base, th_base_lock);

	res = kevent(kqop->kq, changes, n_changes,
	    events, kqop->events_size, ts_p);

	EVBASE_ACQUIRE_LOCK(base, th_base_lock);

	EVUTIL_ASSERT(kqop->changes == NULL);
	kqop->changes = changes;

	if (res == -1) {
		if (errno != EINTR) {
			event_warn("kevent");
			return (-1);
		}

		return (0);
	}

	event_debug(("%s: kevent reports %d", __func__, res));

	for (i = 0; i < res; i++) {
		int which = 0;

		if (events[i].flags & EV_ERROR) {
			switch (events[i].data) {

			/* Can occur on delete if we are not currently
			 * watching any events on this fd.  That can
			 * happen when the fd was closed and another
			 * file was opened with that fd. */
			case ENOENT:
			/* Can occur for reasons not fully understood
			 * on FreeBSD. */
			case EINVAL:
				continue;
#if defined(__FreeBSD__) && defined(ENOTCAPABLE)
			/*
			 * This currently occurs if an FD is closed
			 * before the EV_DELETE makes it out via kevent().
			 * The FreeBSD capabilities code sees the blank
			 * capability set and rejects the request to
			 * modify an event.
			 *
			 * To be strictly correct - when an FD is closed,
			 * all the registered events are also removed.
			 * Queuing EV_DELETE to a closed FD is wrong.
			 * The event(s) should just be deleted from
			 * the pending changelist.
			 */
			case ENOTCAPABLE:
				continue;
#endif

			/* Can occur on a delete if the fd is closed. */
			case EBADF:
				/* XXXX On NetBSD, we can also get EBADF if we
				 * try to add the write side of a pipe, but
				 * the read side has already been closed.
				 * Other BSDs call this situation 'EPIPE'. It
				 * would be good if we had a way to report
				 * this situation. */
				continue;
			/* These two can occur on an add if the fd was one side
			 * of a pipe, and the other side was closed. */
			case EPERM:
			case EPIPE:
				/* Report read events, if we're listening for
				 * them, so that the user can learn about any
				 * add errors.  (If the operation was a
				 * delete, then udata should be cleared.) */
				if (events[i].udata) {
					/* The operation was an add:
					 * report the error as a read. */
					which |= EV_READ;
					break;
				} else {
					/* The operation was a del:
					 * report nothing. */
					continue;
				}

			/* Other errors shouldn't occur. */
			default:
				errno = events[i].data;
				return (-1);
			}
		} else if (events[i].filter == EVFILT_READ) {
			which |= EV_READ;
		} else if (events[i].filter == EVFILT_WRITE) {
			which |= EV_WRITE;
		} else if (events[i].filter == EVFILT_SIGNAL) {
			which |= EV_SIGNAL;
#ifdef EVFILT_USER
		} else if (events[i].filter == EVFILT_USER) {
			base->is_notify_pending = 0;
#endif
		}

		if (!which)
			continue;

		if (events[i].filter == EVFILT_SIGNAL) {
			evmap_signal_active_(base, events[i].ident, 1);
		} else {
			evmap_io_active_(base, events[i].ident, which | EV_ET);
		}
	}

	if (res == kqop->events_size) {
		/* We used all the events space that we have. Maybe we should
		   make it bigger. */
		kq_grow_events(kqop, kqop->events_size * 2);
	}

	return (0);
}

static void
kqop_free(struct kqop *kqop)
{
	if (kqop->changes)
		mm_free(kqop->changes);
	if (kqop->events)
		mm_free(kqop->events);
	if (kqop->kq >= 0 && kqop->pid == getpid())
		close(kqop->kq);
	memset(kqop, 0, sizeof(struct kqop));
	mm_free(kqop);
}

static void
kq_dealloc(struct event_base *base)
{
	struct kqop *kqop = base->evbase;
	evsig_dealloc_(base);
	kqop_free(kqop);
}

/* signal handling */
static int
kq_sig_add(struct event_base *base, int nsignal, short old, short events, void *p)
{
	struct kqop *kqop = base->evbase;
	struct kevent kev;
	struct timespec timeout = { 0, 0 };
	(void)p;

	EVUTIL_ASSERT(nsignal >= 0 && nsignal < NSIG);

	memset(&kev, 0, sizeof(kev));
	kev.ident = nsignal;
	kev.filter = EVFILT_SIGNAL;
	kev.flags = EV_ADD;

	/* Be ready for the signal if it is sent any
	 * time between now and the next call to
	 * kq_dispatch. */
	if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1)
		return (-1);

        /* We can set the handler for most signals to SIG_IGN and
         * still have them reported to us in the queue.  However,
         * if the handler for SIGCHLD is SIG_IGN, the system reaps
         * zombie processes for us, and we don't get any notification.
         * This appears to be the only signal with this quirk. */
	if (evsig_set_handler_(base, nsignal,
                               nsignal == SIGCHLD ? SIG_DFL : SIG_IGN) == -1)
		return (-1);

	return (0);
}

static int
kq_sig_del(struct event_base *base, int nsignal, short old, short events, void *p)
{
	struct kqop *kqop = base->evbase;
	struct kevent kev;

	struct timespec timeout = { 0, 0 };
	(void)p;

	EVUTIL_ASSERT(nsignal >= 0 && nsignal < NSIG);

	memset(&kev, 0, sizeof(kev));
	kev.ident = nsignal;
	kev.filter = EVFILT_SIGNAL;
	kev.flags = EV_DELETE;

	/* Because we insert signal events
	 * immediately, we need to delete them
	 * immediately, too */
	if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1)
		return (-1);

	if (evsig_restore_handler_(base, nsignal) == -1)
		return (-1);

	return (0);
}


/* OSX 10.6 and FreeBSD 8.1 add support for EVFILT_USER, which we can use
 * to wake up the event loop from another thread. */

/* Magic number we use for our filter ID. */
#define NOTIFY_IDENT 42

int
event_kq_add_notify_event_(struct event_base *base)
{
	struct kqop *kqop = base->evbase;
#if defined(EVFILT_USER) && defined(NOTE_TRIGGER)
	struct kevent kev;
	struct timespec timeout = { 0, 0 };
#endif

	if (kqop->notify_event_added)
		return 0;

#if defined(EVFILT_USER) && defined(NOTE_TRIGGER)
	memset(&kev, 0, sizeof(kev));
	kev.ident = NOTIFY_IDENT;
	kev.filter = EVFILT_USER;
	kev.flags = EV_ADD | EV_CLEAR;

	if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1) {
		event_warn("kevent: adding EVFILT_USER event");
		return -1;
	}

	kqop->notify_event_added = 1;

	return 0;
#else
	return -1;
#endif
}

int
event_kq_notify_base_(struct event_base *base)
{
	struct kqop *kqop = base->evbase;
#if defined(EVFILT_USER) && defined(NOTE_TRIGGER)
	struct kevent kev;
	struct timespec timeout = { 0, 0 };
#endif
	if (! kqop->notify_event_added)
		return -1;

#if defined(EVFILT_USER) && defined(NOTE_TRIGGER)
	memset(&kev, 0, sizeof(kev));
	kev.ident = NOTIFY_IDENT;
	kev.filter = EVFILT_USER;
	kev.fflags = NOTE_TRIGGER;

	if (kevent(kqop->kq, &kev, 1, NULL, 0, &timeout) == -1) {
		event_warn("kevent: triggering EVFILT_USER event");
		return -1;
	}

	return 0;
#else
	return -1;
#endif
}

#endif /* EVENT__HAVE_KQUEUE */
