/*	$NetBSD: queue.h,v 1.39 2004/04/18 14:25:34 lukem Exp $	*/

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

#ifndef	_MECHQUEUE_H_
#define	_MECHQUEUE_H_

/*
 * Singly-linked List definitions.
 */
#define	HEIM_SLIST_HEAD(name, type)						\
struct name {								\
	struct type *slh_first;	/* first element */			\
}

#define	HEIM_SLIST_HEAD_INITIALIZER(head)					\
	{ NULL }

#define	HEIM_SLIST_ENTRY(type)						\
struct {								\
	struct type *sle_next;	/* next element */			\
}

/*
 * Singly-linked List functions.
 */
#define	HEIM_SLIST_INIT(head) do {						\
	(head)->slh_first = NULL;					\
} while (/*CONSTCOND*/0)

#define	HEIM_SLIST_INSERT_AFTER(slistelm, elm, field) do {			\
	(elm)->field.sle_next = (slistelm)->field.sle_next;		\
	(slistelm)->field.sle_next = (elm);				\
} while (/*CONSTCOND*/0)

#define	HEIM_SLIST_INSERT_HEAD(head, elm, field) do {			\
	(elm)->field.sle_next = (head)->slh_first;			\
	(head)->slh_first = (elm);					\
} while (/*CONSTCOND*/0)

#define	HEIM_SLIST_REMOVE_HEAD(head, field) do {				\
	(head)->slh_first = (head)->slh_first->field.sle_next;		\
} while (/*CONSTCOND*/0)

#define	HEIM_SLIST_REMOVE(head, elm, type, field) do {			\
	if ((head)->slh_first == (elm)) {				\
		HEIM_SLIST_REMOVE_HEAD((head), field);			\
	}								\
	else {								\
		struct type *curelm = (head)->slh_first;		\
		while(curelm->field.sle_next != (elm))			\
			curelm = curelm->field.sle_next;		\
		curelm->field.sle_next =				\
		    curelm->field.sle_next->field.sle_next;		\
	}								\
} while (/*CONSTCOND*/0)

#define	HEIM_SLIST_FOREACH(var, head, field)					\
	for((var) = (head)->slh_first; (var); (var) = (var)->field.sle_next)

/*
 * Singly-linked List access methods.
 */
#define	HEIM_SLIST_EMPTY(head)	((head)->slh_first == NULL)
#define	HEIM_SLIST_FIRST(head)	((head)->slh_first)
#define	HEIM_SLIST_NEXT(elm, field)	((elm)->field.sle_next)

#endif	/* !_MECHQUEUE_H_ */
