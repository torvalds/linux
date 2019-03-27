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
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef	_DT_REGSET_H
#define	_DT_REGSET_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dt_regset {
	ulong_t dr_size;		/* number of registers in set */
	ulong_t *dr_bitmap;		/* bitmap of active registers */
} dt_regset_t;

extern dt_regset_t *dt_regset_create(ulong_t);
extern void dt_regset_destroy(dt_regset_t *);
extern void dt_regset_reset(dt_regset_t *);
extern int dt_regset_alloc(dt_regset_t *);
extern void dt_regset_free(dt_regset_t *, int);
extern void dt_regset_assert_free(dt_regset_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_REGSET_H */
