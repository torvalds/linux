/*
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
#ifndef EVMAP_INTERNAL_H_INCLUDED_
#define EVMAP_INTERNAL_H_INCLUDED_

/** @file evmap-internal.h
 *
 * An event_map is a utility structure to map each fd or signal to zero or
 * more events.  Functions to manipulate event_maps should only be used from
 * inside libevent.  They generally need to hold the lock on the corresponding
 * event_base.
 **/

struct event_base;
struct event;

/** Initialize an event_map for use.
 */
void evmap_io_initmap_(struct event_io_map* ctx);
void evmap_signal_initmap_(struct event_signal_map* ctx);

/** Remove all entries from an event_map.

	@param ctx the map to clear.
 */
void evmap_io_clear_(struct event_io_map* ctx);
void evmap_signal_clear_(struct event_signal_map* ctx);

/** Add an IO event (some combination of EV_READ or EV_WRITE) to an
    event_base's list of events on a given file descriptor, and tell the
    underlying eventops about the fd if its state has changed.

    Requires that ev is not already added.

    @param base the event_base to operate on.
    @param fd the file descriptor corresponding to ev.
    @param ev the event to add.
*/
int evmap_io_add_(struct event_base *base, evutil_socket_t fd, struct event *ev);
/** Remove an IO event (some combination of EV_READ or EV_WRITE) to an
    event_base's list of events on a given file descriptor, and tell the
    underlying eventops about the fd if its state has changed.

    @param base the event_base to operate on.
    @param fd the file descriptor corresponding to ev.
    @param ev the event to remove.
 */
int evmap_io_del_(struct event_base *base, evutil_socket_t fd, struct event *ev);
/** Active the set of events waiting on an event_base for a given fd.

    @param base the event_base to operate on.
    @param fd the file descriptor that has become active.
    @param events a bitmask of EV_READ|EV_WRITE|EV_ET.
*/
void evmap_io_active_(struct event_base *base, evutil_socket_t fd, short events);


/* These functions behave in the same way as evmap_io_*, except they work on
 * signals rather than fds.  signals use a linear map everywhere; fds use
 * either a linear map or a hashtable. */
int evmap_signal_add_(struct event_base *base, int signum, struct event *ev);
int evmap_signal_del_(struct event_base *base, int signum, struct event *ev);
void evmap_signal_active_(struct event_base *base, evutil_socket_t signum, int ncalls);

/* Return the fdinfo object associated with a given fd.  If the fd has no
 * events associated with it, the result may be NULL.
 */
void *evmap_io_get_fdinfo_(struct event_io_map *ctx, evutil_socket_t fd);

/* Helper for event_reinit(): Tell the backend to re-add every fd and signal
 * for which we have a pending event.
 */
int evmap_reinit_(struct event_base *base);

/* Helper for event_base_free(): Call event_del() on every pending fd and
 * signal event.
 */
void evmap_delete_all_(struct event_base *base);

/* Helper for event_base_assert_ok_(): Check referential integrity of the
 * evmaps.
 */
void evmap_check_integrity_(struct event_base *base);

/* Helper: Call fn on every fd or signal event, passing as its arguments the
 * provided event_base, the event, and arg.  If fn returns 0, process the next
 * event.  If it returns any other value, return that value and process no
 * more events.
 */
int evmap_foreach_event_(struct event_base *base,
    event_base_foreach_event_cb fn,
    void *arg);

#endif /* EVMAP_INTERNAL_H_INCLUDED_ */
