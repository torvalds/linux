/*
 * Copyright (C) 2004-2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: timer.h,v 1.43 2009/09/02 23:48:03 tbox Exp $ */

#ifndef ISC_TIMER_H
#define ISC_TIMER_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/timer.h
 * \brief Provides timers which are event sources in the task system.
 *
 * Three types of timers are supported:
 *
 *\li	'ticker' timers generate a periodic tick event.
 *
 *\li	'once' timers generate an idle timeout event if they are idle for too
 *	long, and generate a life timeout event if their lifetime expires.
 *	They are used to implement both (possibly expiring) idle timers and
 *	'one-shot' timers.
 *
 *\li	'limited' timers generate a periodic tick event until they reach
 *	their lifetime when they generate a life timeout event.
 *
 *\li	'inactive' timers generate no events.
 *
 * Timers can change type.  It is typical to create a timer as
 * an 'inactive' timer and then change it into a 'ticker' or
 * 'once' timer.
 *
 *\li MP:
 *	The module ensures appropriate synchronization of data structures it
 *	creates and manipulates.
 *	Clients of this module must not be holding a timer's task's lock when
 *	making a call that affects that timer.  Failure to follow this rule
 *	can result in deadlock.
 *	The caller must ensure that isc_timermgr_destroy() is called only
 *	once for a given manager.
 *
 * \li Reliability:
 *	No anticipated impact.
 *
 * \li Resources:
 *	TBS
 *
 * \li Security:
 *	No anticipated impact.
 *
 * \li Standards:
 *	None.
 */


/***
 *** Imports
 ***/

#include <isc/types.h>
#include <isc/event.h>
#include <isc/eventclass.h>
#include <isc/lang.h>
#include <isc/time.h>

ISC_LANG_BEGINDECLS

/***
 *** Types
 ***/

/*% Timer Type */
typedef enum {
	isc_timertype_ticker = 0, 	/*%< Ticker */
	isc_timertype_once = 1, 	/*%< Once */
	isc_timertype_limited = 2, 	/*%< Limited */
	isc_timertype_inactive = 3 	/*%< Inactive */
} isc_timertype_t;

typedef struct isc_timerevent {
	struct isc_event	common;
	isc_time_t		due;
} isc_timerevent_t;

#define ISC_TIMEREVENT_FIRSTEVENT	(ISC_EVENTCLASS_TIMER + 0)
#define ISC_TIMEREVENT_TICK		(ISC_EVENTCLASS_TIMER + 1)
#define ISC_TIMEREVENT_IDLE		(ISC_EVENTCLASS_TIMER + 2)
#define ISC_TIMEREVENT_LIFE		(ISC_EVENTCLASS_TIMER + 3)
#define ISC_TIMEREVENT_LASTEVENT	(ISC_EVENTCLASS_TIMER + 65535)

/*% Timer and timer manager methods */
typedef struct {
	void		(*destroy)(isc_timermgr_t **managerp);
	isc_result_t	(*timercreate)(isc_timermgr_t *manager,
				       isc_timertype_t type,
				       isc_time_t *expires,
				       isc_interval_t *interval,
				       isc_task_t *task,
				       isc_taskaction_t action,
				       const void *arg,
				       isc_timer_t **timerp);
} isc_timermgrmethods_t;

typedef struct {
	void		(*attach)(isc_timer_t *timer, isc_timer_t **timerp);
	void		(*detach)(isc_timer_t **timerp);
	isc_result_t	(*reset)(isc_timer_t *timer, isc_timertype_t type,
				 isc_time_t *expires, isc_interval_t *interval,
				 isc_boolean_t purge);
	isc_result_t	(*touch)(isc_timer_t *timer);
} isc_timermethods_t;

/*%
 * This structure is actually just the common prefix of a timer manager
 * object implementation's version of an isc_timermgr_t.
 * \brief
 * Direct use of this structure by clients is forbidden.  timer implementations
 * may change the structure.  'magic' must be ISCAPI_TIMERMGR_MAGIC for any
 * of the isc_timer_ routines to work.  timer implementations must maintain
 * all timer invariants.
 */
struct isc_timermgr {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_timermgrmethods_t	*methods;
};

#define ISCAPI_TIMERMGR_MAGIC		ISC_MAGIC('A','t','m','g')
#define ISCAPI_TIMERMGR_VALID(m)	((m) != NULL && \
					 (m)->magic == ISCAPI_TIMERMGR_MAGIC)

/*%
 * This is the common prefix of a timer object.  The same note as
 * that for the timermgr structure applies.
 */
struct isc_timer {
	unsigned int		impmagic;
	unsigned int		magic;
	isc_timermethods_t	*methods;
};

#define ISCAPI_TIMER_MAGIC	ISC_MAGIC('A','t','m','r')
#define ISCAPI_TIMER_VALID(s)	((s) != NULL && \
				 (s)->magic == ISCAPI_TIMER_MAGIC)

/***
 *** Timer and Timer Manager Functions
 ***
 *** Note: all Ensures conditions apply only if the result is success for
 *** those functions which return an isc_result_t.
 ***/

isc_result_t
isc_timer_create(isc_timermgr_t *manager,
		 isc_timertype_t type,
		 isc_time_t *expires,
		 isc_interval_t *interval,
		 isc_task_t *task,
		 isc_taskaction_t action,
		 const void *arg,
		 isc_timer_t **timerp);
/*%<
 * Create a new 'type' timer managed by 'manager'.  The timers parameters
 * are specified by 'expires' and 'interval'.  Events will be posted to
 * 'task' and when dispatched 'action' will be called with 'arg' as the
 * arg value.  The new timer is returned in 'timerp'.
 *
 * Notes:
 *
 *\li	For ticker timers, the timer will generate a 'tick' event every
 *	'interval' seconds.  The value of 'expires' is ignored.
 *
 *\li	For once timers, 'expires' specifies the time when a life timeout
 *	event should be generated.  If 'expires' is 0 (the epoch), then no life
 *	timeout will be generated.  'interval' specifies how long the timer
 *	can be idle before it generates an idle timeout.  If 0, then no
 *	idle timeout will be generated.
 *
 *\li	If 'expires' is NULL, the epoch will be used.
 *
 *	If 'interval' is NULL, the zero interval will be used.
 *
 * Requires:
 *
 *\li	'manager' is a valid manager
 *
 *\li	'task' is a valid task
 *
 *\li	'action' is a valid action
 *
 *\li	'expires' points to a valid time, or is NULL.
 *
 *\li	'interval' points to a valid interval, or is NULL.
 *
 *\li	type == isc_timertype_inactive ||
 *	('expires' and 'interval' are not both 0)
 *
 *\li	'timerp' is a valid pointer, and *timerp == NULL
 *
 * Ensures:
 *
 *\li	'*timerp' is attached to the newly created timer
 *
 *\li	The timer is attached to the task
 *
 *\li	An idle timeout will not be generated until at least Now + the
 *	timer's interval if 'timer' is a once timer with a non-zero
 *	interval.
 *
 * Returns:
 *
 *\li	Success
 *\li	No memory
 *\li	Unexpected error
 */

isc_result_t
isc_timer_reset(isc_timer_t *timer,
		isc_timertype_t type,
		isc_time_t *expires,
		isc_interval_t *interval,
		isc_boolean_t purge);
/*%<
 * Change the timer's type, expires, and interval values to the given
 * values.  If 'purge' is TRUE, any pending events from this timer
 * are purged from its task's event queue.
 *
 * Notes:
 *
 *\li	If 'expires' is NULL, the epoch will be used.
 *
 *\li	If 'interval' is NULL, the zero interval will be used.
 *
 * Requires:
 *
 *\li	'timer' is a valid timer
 *
 *\li	The same requirements that isc_timer_create() imposes on 'type',
 *	'expires' and 'interval' apply.
 *
 * Ensures:
 *
 *\li	An idle timeout will not be generated until at least Now + the
 *	timer's interval if 'timer' is a once timer with a non-zero
 *	interval.
 *
 * Returns:
 *
 *\li	Success
 *\li	No memory
 *\li	Unexpected error
 */

isc_result_t
isc_timer_touch(isc_timer_t *timer);
/*%<
 * Set the last-touched time of 'timer' to the current time.
 *
 * Requires:
 *
 *\li	'timer' is a valid once timer.
 *
 * Ensures:
 *
 *\li	An idle timeout will not be generated until at least Now + the
 *	timer's interval if 'timer' is a once timer with a non-zero
 *	interval.
 *
 * Returns:
 *
 *\li	Success
 *\li	Unexpected error
 */

void
isc_timer_attach(isc_timer_t *timer, isc_timer_t **timerp);
/*%<
 * Attach *timerp to timer.
 *
 * Requires:
 *
 *\li	'timer' is a valid timer.
 *
 *\li	'timerp' points to a NULL timer.
 *
 * Ensures:
 *
 *\li	*timerp is attached to timer.
 */

void
isc_timer_detach(isc_timer_t **timerp);
/*%<
 * Detach *timerp from its timer.
 *
 * Requires:
 *
 *\li	'timerp' points to a valid timer.
 *
 * Ensures:
 *
 *\li	*timerp is NULL.
 *
 *\li	If '*timerp' is the last reference to the timer,
 *	then:
 *
 *\code
 *		The timer will be shutdown
 *
 *		The timer will detach from its task
 *
 *		All resources used by the timer have been freed
 *
 *		Any events already posted by the timer will be purged.
 *		Therefore, if isc_timer_detach() is called in the context
 *		of the timer's task, it is guaranteed that no more
 *		timer event callbacks will run after the call.
 *\endcode
 */

isc_timertype_t
isc_timer_gettype(isc_timer_t *timer);
/*%<
 * Return the timer type.
 *
 * Requires:
 *
 *\li	'timer' to be a valid timer.
 */

isc_result_t
isc_timermgr_createinctx(isc_mem_t *mctx, isc_appctx_t *actx,
			 isc_timermgr_t **managerp);

isc_result_t
isc_timermgr_create(isc_mem_t *mctx, isc_timermgr_t **managerp);
/*%<
 * Create a timer manager.  isc_timermgr_createinctx() also associates
 * the new manager with the specified application context.
 *
 * Notes:
 *
 *\li	All memory will be allocated in memory context 'mctx'.
 *
 * Requires:
 *
 *\li	'mctx' is a valid memory context.
 *
 *\li	'managerp' points to a NULL isc_timermgr_t.
 *
 *\li	'actx' is a valid application context (for createinctx()).
 *
 * Ensures:
 *
 *\li	'*managerp' is a valid isc_timermgr_t.
 *
 * Returns:
 *
 *\li	Success
 *\li	No memory
 *\li	Unexpected error
 */

void
isc_timermgr_destroy(isc_timermgr_t **managerp);
/*%<
 * Destroy a timer manager.
 *
 * Notes:
 *
 *\li	This routine blocks until there are no timers left in the manager,
 *	so if the caller holds any timer references using the manager, it
 *	must detach them before calling isc_timermgr_destroy() or it will
 *	block forever.
 *
 * Requires:
 *
 *\li	'*managerp' is a valid isc_timermgr_t.
 *
 * Ensures:
 *
 *\li	*managerp == NULL
 *
 *\li	All resources used by the manager have been freed.
 */

void isc_timermgr_poke(isc_timermgr_t *m);

#ifdef USE_TIMERIMPREGISTER
/*%<
 * See isc_timermgr_create() above.
 */
typedef isc_result_t
(*isc_timermgrcreatefunc_t)(isc_mem_t *mctx, isc_timermgr_t **managerp);

isc_result_t
isc__timer_register(void);
/*%<
 * Register a new timer management implementation and add it to the list of
 * supported implementations.  This function must be called when a different
 * event library is used than the one contained in the ISC library.
 */

isc_result_t
isc_timer_register(isc_timermgrcreatefunc_t createfunc);
/*%<
 * A short cut function that specifies the timer management module in the ISC
 * library for isc_timer_register().  An application that uses the ISC library
 * usually do not have to care about this function: it would call
 * isc_lib_register(), which internally calls this function.
 */
#endif /* USE_TIMERIMPREGISTER */

ISC_LANG_ENDDECLS

#endif /* ISC_TIMER_H */
