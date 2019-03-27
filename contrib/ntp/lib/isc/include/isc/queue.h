/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

/*
 * This is a generic implementation of a two-lock concurrent queue.
 * There are built-in mutex locks for the head and tail of the queue,
 * allowing elements to be safely added and removed at the same time.
 */

#ifndef ISC_QUEUE_H
#define ISC_QUEUE_H 1
#include <isc/assertions.h>
#include <isc/boolean.h>
#include <isc/mutex.h>

#ifdef ISC_QUEUE_CHECKINIT
#define ISC_QLINK_INSIST(x) ISC_INSIST(x)
#else
#define ISC_QLINK_INSIST(x) (void)0
#endif

#define ISC_QLINK(type) struct { void *next; isc_boolean_t linked; }
#define ISC_QLINK_INIT(elt, link) \
	do { \
		(elt)->link.next = (void *)(-1); \
		(elt)->link.linked = ISC_FALSE; \
	} while (0)
#define ISC_QLINK_LINKED(elt, link) ((elt)->link.linked)

#define ISC_QUEUE(type) struct { \
	type headnode; \
	type *head, *tail; \
	isc_mutex_t headlock, taillock; \
}

#define ISC_QUEUE_INIT(queue, link) \
	do { \
		isc_mutex_init(&(queue).headlock); \
		isc_mutex_init(&(queue).taillock); \
		(queue).head = (void *) &((queue).headnode); \
		(queue).tail = (void *) &((queue).headnode); \
		ISC_QLINK_INIT((queue).head, link); \
	} while (0)

#define ISC_QUEUE_EMPTY(queue) ISC_TF((queue).head == (queue).tail)

#define ISC_QUEUE_DESTROY(queue) \
	do { \
		ISC_QLINK_INSIST(ISC_QUEUE_EMPTY(queue)); \
		isc_mutex_destroy(&(queue).headlock); \
		isc_mutex_destroy(&(queue).taillock); \
	} while (0)

#define ISC_QUEUE_PUSH(queue, elt, link) \
	do { \
		ISC_QLINK_INSIST(!ISC_QLINK_LINKED(elt, link)); \
		(elt)->link.next = (void *)(-1); \
		LOCK(&(queue).taillock); \
		(queue).tail->link.next = elt; \
		(queue).tail = elt; \
		UNLOCK(&(queue).taillock); \
		(elt)->link.linked = ISC_TRUE; \
	} while (0)

#define ISC_QUEUE_POP(queue, link, ret) \
	do { \
		LOCK(&(queue).headlock); \
		ret = (queue).head->link.next; \
		if (ret == (void *)(-1)) { \
			UNLOCK(&(queue).headlock); \
			ret = NULL; \
		} else { \
			(queue).head->link.next = ret->link.next; \
			if (ret->link.next == (void *)(-1)) { \
				LOCK(&(queue).taillock); \
				(queue).tail = (queue).head; \
				UNLOCK(&(queue).taillock); \
			} \
			UNLOCK(&(queue).headlock); \
			ret->link.next = (void *)(-1); \
			ret->link.linked = ISC_FALSE; \
		} \
	} while (0)

#endif /* ISC_QUEUE_H */
