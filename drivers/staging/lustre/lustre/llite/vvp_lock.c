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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2014, 2015, Intel Corporation.
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

#include "../include/obd_support.h"

#include "vvp_internal.h"

/*****************************************************************************
 *
 * Vvp lock functions.
 *
 */

static void vvp_lock_fini(const struct lu_env *env, struct cl_lock_slice *slice)
{
	struct vvp_lock *vlk = cl2vvp_lock(slice);

	kmem_cache_free(vvp_lock_kmem, vlk);
}

static int vvp_lock_enqueue(const struct lu_env *env,
			    const struct cl_lock_slice *slice,
			    struct cl_io *unused, struct cl_sync_io *anchor)
{
	CLOBINVRNT(env, slice->cls_obj, vvp_object_invariant(slice->cls_obj));

	return 0;
}

static const struct cl_lock_operations vvp_lock_ops = {
	.clo_fini	= vvp_lock_fini,
	.clo_enqueue	= vvp_lock_enqueue,
};

int vvp_lock_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_lock *lock, const struct cl_io *unused)
{
	struct vvp_lock *vlk;
	int result;

	CLOBINVRNT(env, obj, vvp_object_invariant(obj));

	vlk = kmem_cache_zalloc(vvp_lock_kmem, GFP_NOFS);
	if (vlk) {
		cl_lock_slice_add(lock, &vlk->vlk_cl, obj, &vvp_lock_ops);
		result = 0;
	} else {
		result = -ENOMEM;
	}
	return result;
}
