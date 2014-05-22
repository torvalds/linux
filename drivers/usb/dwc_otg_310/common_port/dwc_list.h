/*	$OpenBSD: queue.h,v 1.26 2004/05/04 16:59:32 grange Exp $	*/
/*	$NetBSD: queue.h,v 1.11 1996/05/16 05:17:14 mycroft Exp $	*/

/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)queue.h	8.5 (Berkeley) 8/20/94
 */

#ifndef _DWC_LIST_H_
#define _DWC_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 *
 * This file defines linked list operations.  It is derived from BSD with
 * only the MACRO names being prefixed with DWC_.  This is because a few of
 * these names conflict with those on Linux.  For documentation on use, see the
 * inline comments in the source code.  The original license for this source
 * code applies and is preserved in the dwc_list.h source file.
 */

/*
 * This file defines five types of data structures: singly-linked lists,
 * lists, simple queues, tail queues, and circular queues.
 *
 *
 * A singly-linked list is headed by a single forward pointer. The elements
 * are singly linked for minimum space and pointer manipulation overhead at
 * the expense of O(n) removal for arbitrary elements. New elements can be
 * added to the list after an existing element or at the head of the list.
 * Elements being removed from the head of the list should use the explicit
 * macro for this purpose for optimum efficiency. A singly-linked list may
 * only be traversed in the forward direction.  Singly-linked lists are ideal
 * for applications with large datasets and few or no removals or for
 * implementing a LIFO queue.
 *
 * A list is headed by a single forward pointer (or an array of forward
 * pointers for a hash table header). The elements are doubly linked
 * so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before
 * or after an existing element or at the head of the list. A list
 * may only be traversed in the forward direction.
 *
 * A simple queue is headed by a pair of pointers, one the head of the
 * list and the other to the tail of the list. The elements are singly
 * linked to save space, so elements can only be removed from the
 * head of the list. New elements can be added to the list before or after
 * an existing element, at the head of the list, or at the end of the
 * list. A simple queue may only be traversed in the forward direction.
 *
 * A tail queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or
 * after an existing element, at the head of the list, or at the end of
 * the list. A tail queue may be traversed in either direction.
 *
 * A circle queue is headed by a pair of pointers, one to the head of the
 * list and the other to the tail of the list. The elements are doubly
 * linked so that an arbitrary element can be removed without a need to
 * traverse the list. New elements can be added to the list before or after
 * an existing element, at the head of the list, or at the end of the list.
 * A circle queue may be traversed in either direction, but has a more
 * complex end of list detection.
 *
 * For details on the use of these macros, see the queue(3) manual page.
 */

/*
 * Double-linked List.
 */

	typedef struct dwc_list_link {
		struct dwc_list_link *next;
		struct dwc_list_link *prev;
	} dwc_list_link_t;

#define DWC_LIST_INIT(link) do {	\
	(link)->next = (link);		\
	(link)->prev = (link);		\
} while (0)

#define DWC_LIST_FIRST(link)	((link)->next)
#define DWC_LIST_LAST(link)	((link)->prev)
#define DWC_LIST_END(link)	(link)
#define DWC_LIST_NEXT(link)	((link)->next)
#define DWC_LIST_PREV(link)	((link)->prev)
#define DWC_LIST_EMPTY(link)	\
	(DWC_LIST_FIRST(link) == DWC_LIST_END(link))
#define DWC_LIST_ENTRY(link, type, field)			\
	(type *)((uint8_t *)(link) - (size_t)(&((type *)0)->field))

#if 0
#define DWC_LIST_INSERT_HEAD(list, link) do {			\
	(link)->next = (list)->next;				\
	(link)->prev = (list);					\
	(list)->next->prev = (link);				\
	(list)->next = (link);					\
} while (0)

#define DWC_LIST_INSERT_TAIL(list, link) do {			\
	(link)->next = (list);					\
	(link)->prev = (list)->prev;				\
	(list)->prev->next = (link);				\
	(list)->prev = (link);					\
} while (0)
#else
#define DWC_LIST_INSERT_HEAD(list, link) do {			\
	dwc_list_link_t *__next__ = (list)->next;		\
	__next__->prev = (link);				\
	(link)->next = __next__;				\
	(link)->prev = (list);					\
	(list)->next = (link);					\
} while (0)

#define DWC_LIST_INSERT_TAIL(list, link) do {			\
	dwc_list_link_t *__prev__ = (list)->prev;		\
	(list)->prev = (link);					\
	(link)->next = (list);					\
	(link)->prev = __prev__;				\
	__prev__->next = (link);				\
} while (0)
#endif

#if 0
	static inline void __list_add(struct list_head *new,
				      struct list_head *prev,
				      struct list_head *next)
	{
		next->prev = new;
		new->next = next;
		new->prev = prev;
		prev->next = new;
	}

	static inline void list_add(struct list_head *new,
				      struct list_head *head)
	{
		__list_add(new, head, head->next);
	}

	static inline void list_add_tail(struct list_head *new,
					 struct list_head *head)
	{
		__list_add(new, head->prev, head);
	}

	static inline void __list_del(struct list_head *prev,
				      struct list_head *next)
	{
		next->prev = prev;
		prev->next = next;
	}

	static inline void list_del(struct list_head *entry)
	{
		__list_del(entry->prev, entry->next);
		entry->next = LIST_POISON1;
		entry->prev = LIST_POISON2;
	}
#endif

#define DWC_LIST_REMOVE(link) do {				\
	(link)->next->prev = (link)->prev;			\
	(link)->prev->next = (link)->next;			\
} while (0)

#define DWC_LIST_REMOVE_INIT(link) do {				\
	DWC_LIST_REMOVE(link);					\
	DWC_LIST_INIT(link);					\
} while (0)

#define DWC_LIST_MOVE_HEAD(list, link) do {			\
	DWC_LIST_REMOVE(link);					\
	DWC_LIST_INSERT_HEAD(list, link);			\
} while (0)

#define DWC_LIST_MOVE_TAIL(list, link) do {			\
	DWC_LIST_REMOVE(link);					\
	DWC_LIST_INSERT_TAIL(list, link);			\
} while (0)

#define DWC_LIST_FOREACH(var, list)				\
	for ((var) = DWC_LIST_FIRST(list);			\
	    (var) != DWC_LIST_END(list);			\
	    (var) = DWC_LIST_NEXT(var))

#define DWC_LIST_FOREACH_SAFE(var, var2, list)			\
	for ((var) = DWC_LIST_FIRST(list), (var2) = DWC_LIST_NEXT(var);	\
	    (var) != DWC_LIST_END(list);			\
	    (var) = (var2), (var2) = DWC_LIST_NEXT(var2))

#define DWC_LIST_FOREACH_REVERSE(var, list)			\
	for ((var) = DWC_LIST_LAST(list);			\
	    (var) != DWC_LIST_END(list);			\
	    (var) = DWC_LIST_PREV(var))

/*
 * Singly-linked List definitions.
 */
#define DWC_SLIST_HEAD(name, type)					\
struct name {								\
	struct type *slh_first;	/* first element */			\
}

#define DWC_SLIST_HEAD_INITIALIZER(head)				\
	{ NULL }

#define DWC_SLIST_ENTRY(type)						\
struct {								\
	struct type *sle_next;	/* next element */			\
}

/*
 * Singly-linked List access methods.
 */
#define DWC_SLIST_FIRST(head)	((head)->slh_first)
#define DWC_SLIST_END(head)		NULL
#define DWC_SLIST_EMPTY(head)	(SLIST_FIRST(head) == SLIST_END(head))
#define DWC_SLIST_NEXT(elm, field)	((elm)->field.sle_next)

#define DWC_SLIST_FOREACH(var, head, field)				\
	for ((var) = SLIST_FIRST(head);					\
	    (var) != SLIST_END(head);					\
	    (var) = SLIST_NEXT(var, field))

#define DWC_SLIST_FOREACH_PREVPTR(var, varp, head, field)		\
	for ((varp) = &SLIST_FIRST((head));				\
	    ((var) = *(varp)) != SLIST_END(head);			\
	    (varp) = &SLIST_NEXT((var), field))

/*
 * Singly-linked List functions.
 */
#define DWC_SLIST_INIT(head) {						\
	SLIST_FIRST(head) = SLIST_END(head);				\
}

#define DWC_SLIST_INSERT_AFTER(slistelm, elm, field) do {		\
	(elm)->field.sle_next = (slistelm)->field.sle_next;		\
	(slistelm)->field.sle_next = (elm);				\
} while (0)

#define DWC_SLIST_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.sle_next = (head)->slh_first;			\
	(head)->slh_first = (elm);					\
} while (0)

#define DWC_SLIST_REMOVE_NEXT(head, elm, field) do {			\
	(elm)->field.sle_next = (elm)->field.sle_next->field.sle_next;	\
} while (0)

#define DWC_SLIST_REMOVE_HEAD(head, field) do {				\
	(head)->slh_first = (head)->slh_first->field.sle_next;		\
} while (0)

#define DWC_SLIST_REMOVE(head, elm, type, field) do {			\
	if ((head)->slh_first == (elm)) {				\
		SLIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = (head)->slh_first;		\
		while (curelm->field.sle_next != (elm))			\
			curelm = curelm->field.sle_next;		\
		curelm->field.sle_next =				\
		    curelm->field.sle_next->field.sle_next;		\
	}								\
} while (0)

/*
 * Simple queue definitions.
 */
#define DWC_SIMPLEQ_HEAD(name, type)					\
struct name {								\
	struct type *sqh_first;	/* first element */			\
	struct type **sqh_last;	/* addr of last next element */		\
}

#define DWC_SIMPLEQ_HEAD_INITIALIZER(head)				\
	{ NULL, &(head).sqh_first }

#define DWC_SIMPLEQ_ENTRY(type)						\
struct {								\
	struct type *sqe_next;	/* next element */			\
}

/*
 * Simple queue access methods.
 */
#define DWC_SIMPLEQ_FIRST(head)	    ((head)->sqh_first)
#define DWC_SIMPLEQ_END(head)	    NULL
#define DWC_SIMPLEQ_EMPTY(head)	    (SIMPLEQ_FIRST(head) == SIMPLEQ_END(head))
#define DWC_SIMPLEQ_NEXT(elm, field)    ((elm)->field.sqe_next)

#define DWC_SIMPLEQ_FOREACH(var, head, field)				\
	for ((var) = SIMPLEQ_FIRST(head);				\
	    (var) != SIMPLEQ_END(head);					\
	    (var) = SIMPLEQ_NEXT(var, field))

/*
 * Simple queue functions.
 */
#define DWC_SIMPLEQ_INIT(head) do {					\
	(head)->sqh_first = NULL;					\
	(head)->sqh_last = &(head)->sqh_first;				\
} while (0)

#define DWC_SIMPLEQ_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.sqe_next = (head)->sqh_first;			\
	if ((elm)->field.sqe_next == NULL)				\
		(head)->sqh_last = &(elm)->field.sqe_next;		\
	(head)->sqh_first = (elm);					\
} while (0)

#define DWC_SIMPLEQ_INSERT_TAIL(head, elm, field) do {			\
	(elm)->field.sqe_next = NULL;					\
	*(head)->sqh_last = (elm);					\
	(head)->sqh_last = &(elm)->field.sqe_next;			\
} while (0)

#define DWC_SIMPLEQ_INSERT_AFTER(head, listelm, elm, field) do {	\
	(elm)->field.sqe_next = (listelm)->field.sqe_next;		\
	if ((elm)->field.sqe_next == NULL)				\
		(head)->sqh_last = &(elm)->field.sqe_next;		\
	(listelm)->field.sqe_next = (elm);				\
} while (0)

#define DWC_SIMPLEQ_REMOVE_HEAD(head, field) do {			\
	(head)->sqh_first = (head)->sqh_first->field.sqe_next;		\
	if ((head)->sqh_first == NULL)					\
		(head)->sqh_last = &(head)->sqh_first;			\
} while (0)

/*
 * Tail queue definitions.
 */
#define DWC_TAILQ_HEAD(name, type)					\
struct name {								\
	struct type *tqh_first;	/* first element */			\
	struct type **tqh_last;	/* addr of last next element */		\
}

#define DWC_TAILQ_HEAD_INITIALIZER(head)				\
	{ NULL, &(head).tqh_first }

#define DWC_TAILQ_ENTRY(type)						\
struct {								\
	struct type *tqe_next;	/* next element */			\
	struct type **tqe_prev;	/* address of previous next element */	\
}

/*
 * tail queue access methods
 */
#define DWC_TAILQ_FIRST(head)		((head)->tqh_first)
#define DWC_TAILQ_END(head)		NULL
#define DWC_TAILQ_NEXT(elm, field)	((elm)->field.tqe_next)
#define DWC_TAILQ_LAST(head, headname)					\
	(*(((struct headname *)((head)->tqh_last))->tqh_last))
/* XXX */
#define DWC_TAILQ_PREV(elm, headname, field)				\
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))
#define DWC_TAILQ_EMPTY(head)						\
	(TAILQ_FIRST(head) == TAILQ_END(head))

#define DWC_TAILQ_FOREACH(var, head, field)				\
	for ((var) = TAILQ_FIRST(head);					\
	    (var) != TAILQ_END(head);					\
	    (var) = TAILQ_NEXT(var, field))

#define DWC_TAILQ_FOREACH_REVERSE(var, head, headname, field)		\
	for ((var) = TAILQ_LAST(head, headname);				\
	    (var) != TAILQ_END(head);					\
	    (var) = TAILQ_PREV(var, headname, field))

/*
 * Tail queue functions.
 */
#define DWC_TAILQ_INIT(head) do {					\
	(head)->tqh_first = NULL;					\
	(head)->tqh_last = &(head)->tqh_first;				\
} while (0)

#define DWC_TAILQ_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.tqe_next = (head)->tqh_first;			\
	if ((elm)->field.tqe_next != NULL)	\
		(head)->tqh_first->field.tqe_prev =			\
		    &(elm)->field.tqe_next;				\
	else								\
		(head)->tqh_last = &(elm)->field.tqe_next;		\
	(head)->tqh_first = (elm);					\
	(elm)->field.tqe_prev = &(head)->tqh_first;			\
} while (0)

#define DWC_TAILQ_INSERT_TAIL(head, elm, field) do {			\
	(elm)->field.tqe_next = NULL;					\
	(elm)->field.tqe_prev = (head)->tqh_last;			\
	*(head)->tqh_last = (elm);					\
	(head)->tqh_last = &(elm)->field.tqe_next;			\
} while (0)

#define DWC_TAILQ_INSERT_AFTER(head, listelm, elm, field) do {		\
	(elm)->field.tqe_next = (listelm)->field.tqe_next;		\
	if ((elm)->field.tqe_next != NULL)				\
		(elm)->field.tqe_next->field.tqe_prev =			\
		    &(elm)->field.tqe_next;				\
	else								\
		(head)->tqh_last = &(elm)->field.tqe_next;		\
	(listelm)->field.tqe_next = (elm);				\
	(elm)->field.tqe_prev = &(listelm)->field.tqe_next;		\
} while (0)

#define DWC_TAILQ_INSERT_BEFORE(listelm, elm, field) do {		\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (0)

#define DWC_TAILQ_REMOVE(head, elm, field) do {				\
	if (((elm)->field.tqe_next) != NULL)				\
		(elm)->field.tqe_next->field.tqe_prev =			\
		    (elm)->field.tqe_prev;				\
	else								\
		(head)->tqh_last = (elm)->field.tqe_prev;		\
	*(elm)->field.tqe_prev = (elm)->field.tqe_next;			\
} while (0)

#define DWC_TAILQ_REPLACE(head, elm, elm2, field) do {			\
	(elm2)->field.tqe_next = (elm)->field.tqe_next;			\
	if ((elm2)->field.tqe_next != NULL)	\
		(elm2)->field.tqe_next->field.tqe_prev =		\
		    &(elm2)->field.tqe_next;				\
	else								\
		(head)->tqh_last = &(elm2)->field.tqe_next;		\
	(elm2)->field.tqe_prev = (elm)->field.tqe_prev;			\
	*(elm2)->field.tqe_prev = (elm2);				\
} while (0)

/*
 * Circular queue definitions.
 */
#define DWC_CIRCLEQ_HEAD(name, type)					\
struct name {								\
	struct type *cqh_first;		/* first element */		\
	struct type *cqh_last;		/* last element */		\
}

#define DWC_CIRCLEQ_HEAD_INITIALIZER(head)				\
	{ DWC_CIRCLEQ_END(&head), DWC_CIRCLEQ_END(&head) }

#define DWC_CIRCLEQ_ENTRY(type)						\
struct {								\
	struct type *cqe_next;		/* next element */		\
	struct type *cqe_prev;		/* previous element */		\
}

/*
 * Circular queue access methods
 */
#define DWC_CIRCLEQ_FIRST(head)		((head)->cqh_first)
#define DWC_CIRCLEQ_LAST(head)		((head)->cqh_last)
#define DWC_CIRCLEQ_END(head)		((void *)(head))
#define DWC_CIRCLEQ_NEXT(elm, field)	((elm)->field.cqe_next)
#define DWC_CIRCLEQ_PREV(elm, field)	((elm)->field.cqe_prev)
#define DWC_CIRCLEQ_EMPTY(head)						\
	(DWC_CIRCLEQ_FIRST(head) == DWC_CIRCLEQ_END(head))

#define DWC_CIRCLEQ_EMPTY_ENTRY(elm, field) (((elm)->field.cqe_next == NULL) && ((elm)->field.cqe_prev == NULL))

#define DWC_CIRCLEQ_FOREACH(var, head, field)				\
	for ((var) = DWC_CIRCLEQ_FIRST(head);				\
	    (var) != DWC_CIRCLEQ_END(head);				\
	    (var) = DWC_CIRCLEQ_NEXT(var, field))

#define DWC_CIRCLEQ_FOREACH_SAFE(var, var2, head, field)			\
	for ((var) = DWC_CIRCLEQ_FIRST(head), var2 = DWC_CIRCLEQ_NEXT(var, field); \
	    (var) != DWC_CIRCLEQ_END(head);					\
	    (var) = var2, var2 = DWC_CIRCLEQ_NEXT(var, field))

#define DWC_CIRCLEQ_FOREACH_REVERSE(var, head, field)			\
	for ((var) = DWC_CIRCLEQ_LAST(head);				\
	    (var) != DWC_CIRCLEQ_END(head);				\
	    (var) = DWC_CIRCLEQ_PREV(var, field))

/*
 * Circular queue functions.
 */
#define DWC_CIRCLEQ_INIT(head) do {					\
	(head)->cqh_first = DWC_CIRCLEQ_END(head);			\
	(head)->cqh_last = DWC_CIRCLEQ_END(head);			\
} while (0)

#define DWC_CIRCLEQ_INIT_ENTRY(elm, field) do {				\
	(elm)->field.cqe_next = NULL;					\
	(elm)->field.cqe_prev = NULL;					\
} while (0)

#define DWC_CIRCLEQ_INSERT_AFTER(head, listelm, elm, field) do {	\
	(elm)->field.cqe_next = (listelm)->field.cqe_next;		\
	(elm)->field.cqe_prev = (listelm);				\
	if ((listelm)->field.cqe_next == DWC_CIRCLEQ_END(head))		\
		(head)->cqh_last = (elm);				\
	else								\
		(listelm)->field.cqe_next->field.cqe_prev = (elm);	\
	(listelm)->field.cqe_next = (elm);				\
} while (0)

#define DWC_CIRCLEQ_INSERT_BEFORE(head, listelm, elm, field) do {	\
	(elm)->field.cqe_next = (listelm);				\
	(elm)->field.cqe_prev = (listelm)->field.cqe_prev;		\
	if ((listelm)->field.cqe_prev == DWC_CIRCLEQ_END(head))		\
		(head)->cqh_first = (elm);				\
	else								\
		(listelm)->field.cqe_prev->field.cqe_next = (elm);	\
	(listelm)->field.cqe_prev = (elm);				\
} while (0)

#define DWC_CIRCLEQ_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.cqe_next = (head)->cqh_first;			\
	(elm)->field.cqe_prev = DWC_CIRCLEQ_END(head);			\
	if ((head)->cqh_last == DWC_CIRCLEQ_END(head))			\
		(head)->cqh_last = (elm);				\
	else								\
		(head)->cqh_first->field.cqe_prev = (elm);		\
	(head)->cqh_first = (elm);					\
} while (0)

#define DWC_CIRCLEQ_INSERT_TAIL(head, elm, field) do {			\
	(elm)->field.cqe_next = DWC_CIRCLEQ_END(head);			\
	(elm)->field.cqe_prev = (head)->cqh_last;			\
	if ((head)->cqh_first == DWC_CIRCLEQ_END(head))			\
		(head)->cqh_first = (elm);				\
	else								\
		(head)->cqh_last->field.cqe_next = (elm);		\
	(head)->cqh_last = (elm);					\
} while (0)

#define DWC_CIRCLEQ_REMOVE(head, elm, field) do {			\
	if ((elm)->field.cqe_next == DWC_CIRCLEQ_END(head))		\
		(head)->cqh_last = (elm)->field.cqe_prev;		\
	else								\
		(elm)->field.cqe_next->field.cqe_prev =			\
		    (elm)->field.cqe_prev;				\
	if ((elm)->field.cqe_prev == DWC_CIRCLEQ_END(head))		\
		(head)->cqh_first = (elm)->field.cqe_next;		\
	else								\
		(elm)->field.cqe_prev->field.cqe_next =			\
		    (elm)->field.cqe_next;				\
} while (0)

#define DWC_CIRCLEQ_REMOVE_INIT(head, elm, field) do {			\
	DWC_CIRCLEQ_REMOVE(head, elm, field);				\
	DWC_CIRCLEQ_INIT_ENTRY(elm, field);				\
} while (0)

#define DWC_CIRCLEQ_REPLACE(head, elm, elm2, field) do {		\
	(elm2)->field.cqe_next = (elm)->field.cqe_next;			\
	if ((elm2)->field.cqe_next ==					\
	    DWC_CIRCLEQ_END(head))					\
		(head).cqh_last = (elm2);				\
	else								\
		(elm2)->field.cqe_next->field.cqe_prev = (elm2);	\
	(elm2)->field.cqe_prev = (elm)->field.cqe_prev;			\
	if ((elm2)->field.cqe_prev ==					\
	    DWC_CIRCLEQ_END(head))					\
		(head).cqh_first = (elm2);				\
	else								\
		(elm2)->field.cqe_prev->field.cqe_next = (elm2);	\
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* _DWC_LIST_H_ */
