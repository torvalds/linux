/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

/* $Id: ondestroy.h,v 1.14 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_ONDESTROY_H
#define ISC_ONDESTROY_H 1

#include <isc/lang.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/*! \file isc/ondestroy.h
 * ondestroy handling.
 *
 * Any class ``X'' of objects that wants to send out notifications
 * on its destruction should declare a field of type isc_ondestroy_t
 * (call it 'ondest').
 *
 * \code
 * 	typedef struct {
 * 		...
 * 		isc_ondestroy_t	ondest;
 * 		...
 * 	} X;
 * \endcode
 *
 * When an object ``A'' of type X is created
 * it must initialize the field ondest with a call to
 *
 * \code
 * 	isc_ondestroy_init(&A->ondest).
 * \endcode
 *
 * X should also provide a registration function for third-party
 * objects to call to register their interest in being told about
 * the destruction of a particular instance of X.
 *
 * \code
 *	isc_result_t
 * 	X_ondestroy(X *instance, isc_task_t *task,
 * 		     isc_event_t **eventp) {
 * 		return(isc_ondestroy_register(&instance->ondest, task,eventp));
 * 	}
 * \endcode
 *
 *	Note: locking of the ondestory structure embedded inside of X, is
 * 	X's responsibility.
 *
 * When an instance of X is destroyed, a call to  isc_ondestroy_notify()
 * sends the notifications:
 *
 * \code
 *	X *instance;
 *	isc_ondestroy_t ondest = instance->ondest;
 *
 *	... completely cleanup 'instance' here...
 *
 * 	isc_ondestroy_notify(&ondest, instance);
 * \endcode
 *
 *
 * see lib/dns/zone.c for an ifdef'd-out example.
 */

struct isc_ondestroy {
	unsigned int magic;
	isc_eventlist_t events;
};

void
isc_ondestroy_init(isc_ondestroy_t *ondest);
/*%<
 * Initialize the on ondest structure. *must* be called before first call
 * to isc_ondestroy_register().
 */

isc_result_t
isc_ondestroy_register(isc_ondestroy_t *ondest, isc_task_t *task,
		       isc_event_t **eventp);

/*%<
 * Stores task and *eventp away inside *ondest.  Ownership of **event is
 * taken from the caller (and *eventp is set to NULL). The task is attached
 * to.
 */

void
isc_ondestroy_notify(isc_ondestroy_t *ondest, void *sender);
/*%<
 * Dispatches the event(s) to the task(s) that were given in
 * isc_ondestroy_register call(s) (done via calls to
 * isc_task_sendanddetach()).  Before dispatch, the sender value of each
 * event structure is set to the value of the sender paramater. The
 * internal structures of the ondest parameter are cleaned out, so no other
 * cleanup is needed.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_ONDESTROY_H */
