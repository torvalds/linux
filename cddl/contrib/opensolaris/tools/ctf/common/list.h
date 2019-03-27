/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2001-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIST_H
#define	_LIST_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating linked lists
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct list list_t;

void list_add(list_t **, void *);
void slist_add(list_t **, void *, int (*)(void *, void *));
void *list_remove(list_t **, void *, int (*)(void *, void *, void *), void *);
void list_free(list_t *, void (*)(void *, void *), void *);
void *list_find(list_t *, void *, int (*)(void *, void *));
void *list_first(list_t *);
int list_iter(list_t *, int (*)(void *, void *), void *);
int list_count(list_t *);
int list_empty(list_t *);
void list_concat(list_t **, list_t *);
void slist_merge(list_t **, list_t *, int (*)(void *, void *));

#ifdef __cplusplus
}
#endif

#endif /* _LIST_H */
