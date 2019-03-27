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
 * Copyright (c) 2001 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _STACK_H
#define	_STACK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating stacks
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stk stk_t;

stk_t *stack_new(void (*)(void *));
void stack_free(stk_t *);
void *stack_pop(stk_t *);
void *stack_peek(stk_t *);
void stack_push(stk_t *, void *);
int stack_level(stk_t *);

#ifdef __cplusplus
}
#endif

#endif /* _STACK_H */
