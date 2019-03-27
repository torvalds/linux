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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _TRAVERSE_H
#define	_TRAVERSE_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines used to traverse tdesc trees, invoking user-supplied callbacks
 * as the tree is traversed.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "ctftools.h"

typedef int (*tdtrav_cb_f)(tdesc_t *, tdesc_t **, void *);

typedef struct tdtrav_data {
	int vgen;

	tdtrav_cb_f *firstops;
	tdtrav_cb_f *preops;
	tdtrav_cb_f *postops;

	void *private;
} tdtrav_data_t;

void tdtrav_init(tdtrav_data_t *, int *, tdtrav_cb_f *, tdtrav_cb_f *,
    tdtrav_cb_f *, void *);
int tdtraverse(tdesc_t *, tdesc_t **, tdtrav_data_t *);

int iitraverse(iidesc_t *, int *, tdtrav_cb_f *, tdtrav_cb_f *, tdtrav_cb_f *,
    void *);
int iitraverse_hash(hash_t *, int *, tdtrav_cb_f *, tdtrav_cb_f *,
    tdtrav_cb_f *, void *);
int iitraverse_td(void *, void *);

int tdtrav_assert(tdesc_t *, tdesc_t **, void *);

#ifdef __cplusplus
}
#endif

#endif /* _TRAVERSE_H */
