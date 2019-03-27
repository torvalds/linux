/*
 * Copyright (C) 2004, 2006, 2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1997-2002  Internet Software Consortium.
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

#ifndef ISC_LIST_H
#define ISC_LIST_H 1
#include <isc/boolean.h>
#include <isc/assertions.h>

#ifdef ISC_LIST_CHECKINIT
#define ISC_LINK_INSIST(x) ISC_INSIST(x)
#else
#define ISC_LINK_INSIST(x)
#endif

#define ISC_LIST(type) struct { type *head, *tail; }
#define ISC_LIST_INIT(list) \
	do { (list).head = NULL; (list).tail = NULL; } while (0)

#define ISC_LINK(type) struct { type *prev, *next; }
#define ISC_LINK_INIT_TYPE(elt, link, type) \
	do { \
		(elt)->link.prev = (type *)(-1); \
		(elt)->link.next = (type *)(-1); \
	} while (0)
#define ISC_LINK_INIT(elt, link) \
	ISC_LINK_INIT_TYPE(elt, link, void)
#define ISC_LINK_LINKED(elt, link) ((void *)((elt)->link.prev) != (void *)(-1))

#define ISC_LIST_HEAD(list) ((list).head)
#define ISC_LIST_TAIL(list) ((list).tail)
#define ISC_LIST_EMPTY(list) ISC_TF((list).head == NULL)

#define __ISC_LIST_PREPENDUNSAFE(list, elt, link) \
	do { \
		if ((list).head != NULL) \
			(list).head->link.prev = (elt); \
		else \
			(list).tail = (elt); \
		(elt)->link.prev = NULL; \
		(elt)->link.next = (list).head; \
		(list).head = (elt); \
	} while (0)

#define ISC_LIST_PREPEND(list, elt, link) \
	do { \
		ISC_LINK_INSIST(!ISC_LINK_LINKED(elt, link)); \
		__ISC_LIST_PREPENDUNSAFE(list, elt, link); \
	} while (0)

#define ISC_LIST_INITANDPREPEND(list, elt, link) \
		__ISC_LIST_PREPENDUNSAFE(list, elt, link)

#define __ISC_LIST_APPENDUNSAFE(list, elt, link) \
	do { \
		if ((list).tail != NULL) \
			(list).tail->link.next = (elt); \
		else \
			(list).head = (elt); \
		(elt)->link.prev = (list).tail; \
		(elt)->link.next = NULL; \
		(list).tail = (elt); \
	} while (0)

#define ISC_LIST_APPEND(list, elt, link) \
	do { \
		ISC_LINK_INSIST(!ISC_LINK_LINKED(elt, link)); \
		__ISC_LIST_APPENDUNSAFE(list, elt, link); \
	} while (0)

#define ISC_LIST_INITANDAPPEND(list, elt, link) \
		__ISC_LIST_APPENDUNSAFE(list, elt, link)

#define __ISC_LIST_UNLINKUNSAFE_TYPE(list, elt, link, type) \
	do { \
		if ((elt)->link.next != NULL) \
			(elt)->link.next->link.prev = (elt)->link.prev; \
		else { \
			ISC_INSIST((list).tail == (elt)); \
			(list).tail = (elt)->link.prev; \
		} \
		if ((elt)->link.prev != NULL) \
			(elt)->link.prev->link.next = (elt)->link.next; \
		else { \
			ISC_INSIST((list).head == (elt)); \
			(list).head = (elt)->link.next; \
		} \
		(elt)->link.prev = (type *)(-1); \
		(elt)->link.next = (type *)(-1); \
	} while (0)

#define __ISC_LIST_UNLINKUNSAFE(list, elt, link) \
	__ISC_LIST_UNLINKUNSAFE_TYPE(list, elt, link, void)

#define ISC_LIST_UNLINK_TYPE(list, elt, link, type) \
	do { \
		ISC_LINK_INSIST(ISC_LINK_LINKED(elt, link)); \
		__ISC_LIST_UNLINKUNSAFE_TYPE(list, elt, link, type); \
	} while (0)
#define ISC_LIST_UNLINK(list, elt, link) \
	ISC_LIST_UNLINK_TYPE(list, elt, link, void)

#define ISC_LIST_PREV(elt, link) ((elt)->link.prev)
#define ISC_LIST_NEXT(elt, link) ((elt)->link.next)

#define __ISC_LIST_INSERTBEFOREUNSAFE(list, before, elt, link) \
	do { \
		if ((before)->link.prev == NULL) \
			ISC_LIST_PREPEND(list, elt, link); \
		else { \
			(elt)->link.prev = (before)->link.prev; \
			(before)->link.prev = (elt); \
			(elt)->link.prev->link.next = (elt); \
			(elt)->link.next = (before); \
		} \
	} while (0)

#define ISC_LIST_INSERTBEFORE(list, before, elt, link) \
	do { \
		ISC_LINK_INSIST(ISC_LINK_LINKED(before, link)); \
		ISC_LINK_INSIST(!ISC_LINK_LINKED(elt, link)); \
		__ISC_LIST_INSERTBEFOREUNSAFE(list, before, elt, link); \
	} while (0)

#define __ISC_LIST_INSERTAFTERUNSAFE(list, after, elt, link) \
	do { \
		if ((after)->link.next == NULL) \
			ISC_LIST_APPEND(list, elt, link); \
		else { \
			(elt)->link.next = (after)->link.next; \
			(after)->link.next = (elt); \
			(elt)->link.next->link.prev = (elt); \
			(elt)->link.prev = (after); \
		} \
	} while (0)

#define ISC_LIST_INSERTAFTER(list, after, elt, link) \
	do { \
		ISC_LINK_INSIST(ISC_LINK_LINKED(after, link)); \
		ISC_LINK_INSIST(!ISC_LINK_LINKED(elt, link)); \
		__ISC_LIST_INSERTAFTERUNSAFE(list, after, elt, link); \
	} while (0)

#define ISC_LIST_APPENDLIST(list1, list2, link) \
	do { \
		if (ISC_LIST_EMPTY(list1)) \
			(list1) = (list2); \
		else if (!ISC_LIST_EMPTY(list2)) { \
			(list1).tail->link.next = (list2).head; \
			(list2).head->link.prev = (list1).tail; \
			(list1).tail = (list2).tail; \
		} \
		(list2).head = NULL; \
		(list2).tail = NULL; \
	} while (0)

#define ISC_LIST_PREPENDLIST(list1, list2, link) \
	do { \
		if (ISC_LIST_EMPTY(list1)) \
			(list1) = (list2); \
		else if (!ISC_LIST_EMPTY(list2)) { \
			(list2).tail->link.next = (list1).head; \
			(list1).head->link.prev = (list2).tail; \
			(list1).head = (list2).head; \
		} \
		(list2).head = NULL; \
		(list2).tail = NULL; \
	} while (0)

#define ISC_LIST_ENQUEUE(list, elt, link) ISC_LIST_APPEND(list, elt, link)
#define __ISC_LIST_ENQUEUEUNSAFE(list, elt, link) \
	__ISC_LIST_APPENDUNSAFE(list, elt, link)
#define ISC_LIST_DEQUEUE(list, elt, link) \
	 ISC_LIST_UNLINK_TYPE(list, elt, link, void)
#define ISC_LIST_DEQUEUE_TYPE(list, elt, link, type) \
	 ISC_LIST_UNLINK_TYPE(list, elt, link, type)
#define __ISC_LIST_DEQUEUEUNSAFE(list, elt, link) \
	__ISC_LIST_UNLINKUNSAFE_TYPE(list, elt, link, void)
#define __ISC_LIST_DEQUEUEUNSAFE_TYPE(list, elt, link, type) \
	__ISC_LIST_UNLINKUNSAFE_TYPE(list, elt, link, type)

#endif /* ISC_LIST_H */
