/*	$NetBSD: queue.h,v 1.38 2004/04/18 14:12:05 lukem Exp $	*/
/*	$Id$ */

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

#ifndef	_ASN1_QUEUE_H_
#define	_ASN1_QUEUE_H_

/*
 * Tail queue definitions.
 */
#define	ASN1_TAILQ_HEAD(name, type)					\
struct name {								\
	struct type *tqh_first;	/* first element */			\
	struct type **tqh_last;	/* addr of last next element */		\
}

#define	ASN1_TAILQ_HEAD_INITIALIZER(head)				\
	{ NULL, &(head).tqh_first }
#define	ASN1_TAILQ_ENTRY(type)						\
struct {								\
	struct type *tqe_next;	/* next element */			\
	struct type **tqe_prev;	/* address of previous next element */	\
}

/*
 * Tail queue functions.
 */
#if defined(_KERNEL) && defined(QUEUEDEBUG)
#define	QUEUEDEBUG_ASN1_TAILQ_INSERT_HEAD(head, elm, field)		\
	if ((head)->tqh_first &&					\
	    (head)->tqh_first->field.tqe_prev != &(head)->tqh_first)	\
		panic("ASN1_TAILQ_INSERT_HEAD %p %s:%d", (head), __FILE__, __LINE__);
#define	QUEUEDEBUG_ASN1_TAILQ_INSERT_TAIL(head, elm, field)		\
	if (*(head)->tqh_last != NULL)					\
		panic("ASN1_TAILQ_INSERT_TAIL %p %s:%d", (head), __FILE__, __LINE__);
#define	QUEUEDEBUG_ASN1_TAILQ_OP(elm, field)				\
	if ((elm)->field.tqe_next &&					\
	    (elm)->field.tqe_next->field.tqe_prev !=			\
	    &(elm)->field.tqe_next)					\
		panic("ASN1_TAILQ_* forw %p %s:%d", (elm), __FILE__, __LINE__);\
	if (*(elm)->field.tqe_prev != (elm))				\
		panic("ASN1_TAILQ_* back %p %s:%d", (elm), __FILE__, __LINE__);
#define	QUEUEDEBUG_ASN1_TAILQ_PREREMOVE(head, elm, field)		\
	if ((elm)->field.tqe_next == NULL &&				\
	    (head)->tqh_last != &(elm)->field.tqe_next)			\
		panic("ASN1_TAILQ_PREREMOVE head %p elm %p %s:%d",	\
		      (head), (elm), __FILE__, __LINE__);
#define	QUEUEDEBUG_ASN1_TAILQ_POSTREMOVE(elm, field)			\
	(elm)->field.tqe_next = (void *)1L;				\
	(elm)->field.tqe_prev = (void *)1L;
#else
#define	QUEUEDEBUG_ASN1_TAILQ_INSERT_HEAD(head, elm, field)
#define	QUEUEDEBUG_ASN1_TAILQ_INSERT_TAIL(head, elm, field)
#define	QUEUEDEBUG_ASN1_TAILQ_OP(elm, field)
#define	QUEUEDEBUG_ASN1_TAILQ_PREREMOVE(head, elm, field)
#define	QUEUEDEBUG_ASN1_TAILQ_POSTREMOVE(elm, field)
#endif

#define	ASN1_TAILQ_INIT(head) do {					\
	(head)->tqh_first = NULL;					\
	(head)->tqh_last = &(head)->tqh_first;				\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_INSERT_HEAD(head, elm, field) do {			\
	QUEUEDEBUG_ASN1_TAILQ_INSERT_HEAD((head), (elm), field)		\
	if (((elm)->field.tqe_next = (head)->tqh_first) != NULL)	\
		(head)->tqh_first->field.tqe_prev =			\
		    &(elm)->field.tqe_next;				\
	else								\
		(head)->tqh_last = &(elm)->field.tqe_next;		\
	(head)->tqh_first = (elm);					\
	(elm)->field.tqe_prev = &(head)->tqh_first;			\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_INSERT_TAIL(head, elm, field) do {			\
	QUEUEDEBUG_ASN1_TAILQ_INSERT_TAIL((head), (elm), field)		\
	(elm)->field.tqe_next = NULL;					\
	(elm)->field.tqe_prev = (head)->tqh_last;			\
	*(head)->tqh_last = (elm);					\
	(head)->tqh_last = &(elm)->field.tqe_next;			\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_INSERT_AFTER(head, listelm, elm, field) do {		\
	QUEUEDEBUG_ASN1_TAILQ_OP((listelm), field)			\
	if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL)\
		(elm)->field.tqe_next->field.tqe_prev = 		\
		    &(elm)->field.tqe_next;				\
	else								\
		(head)->tqh_last = &(elm)->field.tqe_next;		\
	(listelm)->field.tqe_next = (elm);				\
	(elm)->field.tqe_prev = &(listelm)->field.tqe_next;		\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_INSERT_BEFORE(listelm, elm, field) do {		\
	QUEUEDEBUG_ASN1_TAILQ_OP((listelm), field)			\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_REMOVE(head, elm, field) do {			\
	QUEUEDEBUG_ASN1_TAILQ_PREREMOVE((head), (elm), field)		\
	QUEUEDEBUG_ASN1_TAILQ_OP((elm), field)				\
	if (((elm)->field.tqe_next) != NULL)				\
		(elm)->field.tqe_next->field.tqe_prev = 		\
		    (elm)->field.tqe_prev;				\
	else								\
		(head)->tqh_last = (elm)->field.tqe_prev;		\
	*(elm)->field.tqe_prev = (elm)->field.tqe_next;			\
	QUEUEDEBUG_ASN1_TAILQ_POSTREMOVE((elm), field);			\
} while (/*CONSTCOND*/0)

#define	ASN1_TAILQ_FOREACH(var, head, field)				\
	for ((var) = ((head)->tqh_first);				\
		(var);							\
		(var) = ((var)->field.tqe_next))

#define	ASN1_TAILQ_FOREACH_REVERSE(var, head, headname, field)		\
	for ((var) = (*(((struct headname *)((head)->tqh_last))->tqh_last)); \
		(var);							\
		(var) = (*(((struct headname *)((var)->field.tqe_prev))->tqh_last)))

/*
 * Tail queue access methods.
 */
#define	ASN1_TAILQ_EMPTY(head)		((head)->tqh_first == NULL)
#define	ASN1_TAILQ_FIRST(head)		((head)->tqh_first)
#define	ASN1_TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)

#define	ASN1_TAILQ_LAST(head, headname) \
	(*(((struct headname *)((head)->tqh_last))->tqh_last))
#define	ASN1_TAILQ_PREV(elm, headname, field) \
	(*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))


#endif	/* !_ASN1_QUEUE_H_ */
