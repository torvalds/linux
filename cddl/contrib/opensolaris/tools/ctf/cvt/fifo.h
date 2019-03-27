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
 * Copyright 2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _FIFO_H
#define	_FIFO_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating a FIFO queue
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fifo fifo_t;

extern fifo_t *fifo_new(void);
extern void fifo_add(fifo_t *, void *);
extern void *fifo_remove(fifo_t *);
extern void fifo_free(fifo_t *, void (*)(void *));
extern int fifo_len(fifo_t *);
extern int fifo_empty(fifo_t *);
extern int fifo_iter(fifo_t *, int (*)(void *, void *), void *);

#ifdef __cplusplus
}
#endif

#endif /* _FIFO_H */
