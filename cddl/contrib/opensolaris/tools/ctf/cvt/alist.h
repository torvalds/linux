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
 * Copyright 2001-2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _ASSOC_H
#define	_ASSOC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Create, manage, and destroy association lists.  alists are arrays with
 * arbitrary index types.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct alist alist_t;

alist_t *alist_new(void (*)(void *), void (*)(void *));
alist_t *alist_xnew(int, void (*)(void *), void (*)(void *),
    int (*)(int, void *), int (*)(void *, void *));
void alist_free(alist_t *);
void alist_add(alist_t *, void *, void *);
int alist_find(alist_t *, void *, void **);
int alist_iter(alist_t *, int (*)(void *, void *, void *), void *);
void alist_stats(alist_t *, int);
int alist_dump(alist_t *, int (*)(void *, void *));

#ifdef __cplusplus
}
#endif

#endif /* _ASSOC_H */
