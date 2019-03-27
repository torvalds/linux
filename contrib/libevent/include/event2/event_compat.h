/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
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
#ifndef EVENT2_EVENT_COMPAT_H_INCLUDED_
#define EVENT2_EVENT_COMPAT_H_INCLUDED_

/** @file event2/event_compat.h

  Potentially non-threadsafe versions of the functions in event.h: provided
  only for backwards compatibility.

  In the oldest versions of Libevent, event_base was not a first-class
  structure.  Instead, there was a single event base that every function
  manipulated.  Later, when separate event bases were added, the old functions
  that didn't take an event_base argument needed to work by manipulating the
  "current" event base.  This could lead to thread-safety issues, and obscure,
  hard-to-diagnose bugs.

  @deprecated All functions in this file are by definition deprecated.
 */
#include <event2/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>

/**
  Initialize the event API.

  The event API needs to be initialized with event_init() before it can be
  used.  Sets the global current base that gets used for events that have no
  base associated with them.

  @deprecated This function is deprecated because it replaces the "current"
    event_base, and is totally unsafe for multithreaded use.  The replacement
    is event_base_new().

  @see event_base_set(), event_base_new()
 */
EVENT2_EXPORT_SYMBOL
struct event_base *event_init(void);

/**
  Loop to process events.

  Like event_base_dispatch(), but uses the "current" base.

  @deprecated This function is deprecated because it is easily confused by
    multiple calls to event_init(), and because it is not safe for
    multithreaded use.  The replacement is event_base_dispatch().

  @see event_base_dispatch(), event_init()
 */
EVENT2_EXPORT_SYMBOL
int event_dispatch(void);

/**
  Handle events.

  This function behaves like event_base_loop(), but uses the "current" base

  @deprecated This function is deprecated because it uses the event base from
    the last call to event_init, and is therefore not safe for multithreaded
    use.  The replacement is event_base_loop().

  @see event_base_loop(), event_init()
*/
EVENT2_EXPORT_SYMBOL
int event_loop(int);


/**
  Exit the event loop after the specified time.

  This function behaves like event_base_loopexit(), except that it uses the
  "current" base.

  @deprecated This function is deprecated because it uses the event base from
    the last call to event_init, and is therefore not safe for multithreaded
    use.  The replacement is event_base_loopexit().

  @see event_init, event_base_loopexit()
  */
EVENT2_EXPORT_SYMBOL
int event_loopexit(const struct timeval *);


/**
  Abort the active event_loop() immediately.

  This function behaves like event_base_loopbreakt(), except that it uses the
  "current" base.

  @deprecated This function is deprecated because it uses the event base from
    the last call to event_init, and is therefore not safe for multithreaded
    use.  The replacement is event_base_loopbreak().

  @see event_base_loopbreak(), event_init()
 */
EVENT2_EXPORT_SYMBOL
int event_loopbreak(void);

/**
  Schedule a one-time event to occur.

  @deprecated This function is obsolete, and has been replaced by
    event_base_once(). Its use is deprecated because it relies on the
    "current" base configured by event_init().

  @see event_base_once()
 */
EVENT2_EXPORT_SYMBOL
int event_once(evutil_socket_t , short,
    void (*)(evutil_socket_t, short, void *), void *, const struct timeval *);


/**
  Get the kernel event notification mechanism used by Libevent.

  @deprecated This function is obsolete, and has been replaced by
    event_base_get_method(). Its use is deprecated because it relies on the
    "current" base configured by event_init().

  @see event_base_get_method()
 */
EVENT2_EXPORT_SYMBOL
const char *event_get_method(void);


/**
  Set the number of different event priorities.

  @deprecated This function is deprecated because it is easily confused by
    multiple calls to event_init(), and because it is not safe for
    multithreaded use.  The replacement is event_base_priority_init().

  @see event_base_priority_init()
 */
EVENT2_EXPORT_SYMBOL
int	event_priority_init(int);

/**
  Prepare an event structure to be added.

  @deprecated event_set() is not recommended for new code, because it requires
     a subsequent call to event_base_set() to be safe under most circumstances.
     Use event_assign() or event_new() instead.
 */
EVENT2_EXPORT_SYMBOL
void event_set(struct event *, evutil_socket_t, short, void (*)(evutil_socket_t, short, void *), void *);

#define evtimer_set(ev, cb, arg)	event_set((ev), -1, 0, (cb), (arg))
#define evsignal_set(ev, x, cb, arg)	\
	event_set((ev), (x), EV_SIGNAL|EV_PERSIST, (cb), (arg))


/**
   @name timeout_* macros

   @deprecated These macros are deprecated because their naming is inconsistent
     with the rest of Libevent.  Use the evtimer_* macros instead.
   @{
 */
#define timeout_add(ev, tv)		event_add((ev), (tv))
#define timeout_set(ev, cb, arg)	event_set((ev), -1, 0, (cb), (arg))
#define timeout_del(ev)			event_del(ev)
#define timeout_pending(ev, tv)		event_pending((ev), EV_TIMEOUT, (tv))
#define timeout_initialized(ev)		event_initialized(ev)
/**@}*/

/**
   @name signal_* macros

   @deprecated These macros are deprecated because their naming is inconsistent
     with the rest of Libevent.  Use the evsignal_* macros instead.
   @{
 */
#define signal_add(ev, tv)		event_add((ev), (tv))
#define signal_set(ev, x, cb, arg)				\
	event_set((ev), (x), EV_SIGNAL|EV_PERSIST, (cb), (arg))
#define signal_del(ev)			event_del(ev)
#define signal_pending(ev, tv)		event_pending((ev), EV_SIGNAL, (tv))
#define signal_initialized(ev)		event_initialized(ev)
/**@}*/

#ifndef EVENT_FD
/* These macros are obsolete; use event_get_fd and event_get_signal instead. */
#define EVENT_FD(ev)		((int)event_get_fd(ev))
#define EVENT_SIGNAL(ev)	event_get_signal(ev)
#endif

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_EVENT_COMPAT_H_INCLUDED_ */
