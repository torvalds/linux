/*
 * Copyright (c) 2009-2012 Niels Provos and Nick Mathewson
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
#ifndef CHANGELIST_INTERNAL_H_INCLUDED_
#define CHANGELIST_INTERNAL_H_INCLUDED_

/*
  A "changelist" is a list of all the fd status changes that should be made
  between calls to the backend's dispatch function.  There are a few reasons
  that a backend would want to queue changes like this rather than processing
  them immediately.

    1) Sometimes applications will add and delete the same event more than
       once between calls to dispatch.  Processing these changes immediately
       is needless, and potentially expensive (especially if we're on a system
       that makes one syscall per changed event).

    2) Sometimes we can coalesce multiple changes on the same fd into a single
       syscall if we know about them in advance.  For example, epoll can do an
       add and a delete at the same time, but only if we have found out about
       both of them before we tell epoll.

    3) Sometimes adding an event that we immediately delete can cause
       unintended consequences: in kqueue, this makes pending events get
       reported spuriously.
 */

#include "event2/util.h"

/** Represents a */
struct event_change {
	/** The fd or signal whose events are to be changed */
	evutil_socket_t fd;
	/* The events that were enabled on the fd before any of these changes
	   were made.  May include EV_READ or EV_WRITE. */
	short old_events;

	/* The changes that we want to make in reading and writing on this fd.
	 * If this is a signal, then read_change has EV_CHANGE_SIGNAL set,
	 * and write_change is unused. */
	ev_uint8_t read_change;
	ev_uint8_t write_change;
	ev_uint8_t close_change;
};

/* Flags for read_change and write_change. */

/* If set, add the event. */
#define EV_CHANGE_ADD     0x01
/* If set, delete the event.  Exclusive with EV_CHANGE_ADD */
#define EV_CHANGE_DEL     0x02
/* If set, this event refers a signal, not an fd. */
#define EV_CHANGE_SIGNAL  EV_SIGNAL
/* Set for persistent events.  Currently not used. */
#define EV_CHANGE_PERSIST EV_PERSIST
/* Set for adding edge-triggered events. */
#define EV_CHANGE_ET      EV_ET

/* The value of fdinfo_size that a backend should use if it is letting
 * changelist handle its add and delete functions. */
#define EVENT_CHANGELIST_FDINFO_SIZE sizeof(int)

/** Set up the data fields in a changelist. */
void event_changelist_init_(struct event_changelist *changelist);
/** Remove every change in the changelist, and make corresponding changes
 * in the event maps in the base.  This function is generally used right
 * after making all the changes in the changelist. */
void event_changelist_remove_all_(struct event_changelist *changelist,
    struct event_base *base);
/** Free all memory held in a changelist. */
void event_changelist_freemem_(struct event_changelist *changelist);

/** Implementation of eventop_add that queues the event in a changelist. */
int event_changelist_add_(struct event_base *base, evutil_socket_t fd, short old, short events,
    void *p);
/** Implementation of eventop_del that queues the event in a changelist. */
int event_changelist_del_(struct event_base *base, evutil_socket_t fd, short old, short events,
    void *p);

#endif
