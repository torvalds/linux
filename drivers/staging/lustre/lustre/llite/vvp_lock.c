/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_lock for VVP layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE


#include "../include/obd.h"
#include "../include/lustre_lite.h"

#include "vvp_internal.h"

/*****************************************************************************
 *
 * Vvp lock functions.
 *
 */

/**
 * Estimates lock value for the purpose of managing the lock cache during
 * memory shortages.
 *
 * Locks for memory mapped files are almost infinitely precious, others are
 * junk. "Mapped locks" are heavy, but not infinitely heavy, so that they are
 * ordered within themselves by weights assigned from other layers.
 */
static unsigned long vvp_lock_weigh(const struct lu_env *env,
				    const struct cl_lock_slice *slice)
{
	struct ccc_object *cob = cl2ccc(slice->cls_obj);

	return atomic_read(&cob->cob_mmap_cnt) > 0 ? ~0UL >> 2 : 0;
}

static const struct cl_lock_operations vvp_lock_ops = {
	.clo_delete    = ccc_lock_delete,
	.clo_fini      = ccc_lock_fini,
	.clo_enqueue   = ccc_lock_enqueue,
	.clo_wait      = ccc_lock_wait,
	.clo_unuse     = ccc_lock_unuse,
	.clo_fits_into = ccc_lock_fits_into,
	.clo_state     = ccc_lock_state,
	.clo_weigh     = vvp_lock_weigh
};

int vvp_lock_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_lock *lock, const struct cl_io *io)
{
	return ccc_lock_init(env, obj, lock, io, &vvp_lock_ops);
}
