/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: event.h,v 1.34 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_EVENT_H
#define ISC_EVENT_H 1

/*! \file isc/event.h */

#include <isc/lang.h>
#include <isc/types.h>

/*****
 ***** Events.
 *****/

typedef void (*isc_eventdestructor_t)(isc_event_t *);

#define ISC_EVENT_COMMON(ltype)		\
	size_t				ev_size; \
	unsigned int			ev_attributes; \
	void *				ev_tag; \
	isc_eventtype_t			ev_type; \
	isc_taskaction_t		ev_action; \
	void *				ev_arg; \
	void *				ev_sender; \
	isc_eventdestructor_t		ev_destroy; \
	void *				ev_destroy_arg; \
	ISC_LINK(ltype)			ev_link

/*%
 * Attributes matching a mask of 0x000000ff are reserved for the task library's
 * definition.  Attributes of 0xffffff00 may be used by the application
 * or non-ISC libraries.
 */
#define ISC_EVENTATTR_NOPURGE		0x00000001

/*%
 * The ISC_EVENTATTR_CANCELED attribute is intended to indicate
 * that an event is delivered as a result of a canceled operation
 * rather than successful completion, by mutual agreement
 * between the sender and receiver.  It is not set or used by
 * the task system.
 */
#define ISC_EVENTATTR_CANCELED		0x00000002

#define ISC_EVENT_INIT(event, sz, at, ta, ty, ac, ar, sn, df, da) \
do { \
	(event)->ev_size = (sz); \
	(event)->ev_attributes = (at); \
	(event)->ev_tag = (ta); \
	(event)->ev_type = (ty); \
	(event)->ev_action = (ac); \
	(event)->ev_arg = (ar); \
	(event)->ev_sender = (sn); \
	(event)->ev_destroy = (df); \
	(event)->ev_destroy_arg = (da); \
	ISC_LINK_INIT((event), ev_link); \
} while (0)

/*%
 * This structure is public because "subclassing" it may be useful when
 * defining new event types.
 */
struct isc_event {
	ISC_EVENT_COMMON(struct isc_event);
};

#define ISC_EVENTTYPE_FIRSTEVENT	0x00000000
#define ISC_EVENTTYPE_LASTEVENT		0xffffffff

#define ISC_EVENT_PTR(p) ((isc_event_t **)(void *)(p))

ISC_LANG_BEGINDECLS

isc_event_t *
isc_event_allocate(isc_mem_t *mctx, void *sender, isc_eventtype_t type,
		   isc_taskaction_t action, const void *arg, size_t size);
/*%<
 * Allocate an event structure. 
 *
 * Allocate and initialize in a structure with initial elements
 * defined by:
 *
 * \code
 *	struct {
 *		ISC_EVENT_COMMON(struct isc_event);
 *		...
 *	};
 * \endcode
 *	
 * Requires:
 *\li	'size' >= sizeof(struct isc_event)
 *\li	'action' to be non NULL
 *
 * Returns:
 *\li	a pointer to a initialized structure of the requested size.
 *\li	NULL if unable to allocate memory.
 */

void
isc_event_free(isc_event_t **);

ISC_LANG_ENDDECLS

#endif /* ISC_EVENT_H */
