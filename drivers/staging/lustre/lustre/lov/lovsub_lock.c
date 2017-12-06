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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_lock for LOVSUB layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "lov_cl_internal.h"

/** \addtogroup lov
 *  @{
 */

/*****************************************************************************
 *
 * Lovsub lock operations.
 *
 */

static void lovsub_lock_fini(const struct lu_env *env,
			     struct cl_lock_slice *slice)
{
	struct lovsub_lock   *lsl;

	lsl = cl2lovsub_lock(slice);
	kmem_cache_free(lovsub_lock_kmem, lsl);
}

static const struct cl_lock_operations lovsub_lock_ops = {
	.clo_fini    = lovsub_lock_fini,
};

int lovsub_lock_init(const struct lu_env *env, struct cl_object *obj,
		     struct cl_lock *lock, const struct cl_io *io)
{
	struct lovsub_lock *lsk;
	int result;

	lsk = kmem_cache_zalloc(lovsub_lock_kmem, GFP_NOFS);
	if (lsk) {
		cl_lock_slice_add(lock, &lsk->lss_cl, obj, &lovsub_lock_ops);
		result = 0;
	} else {
		result = -ENOMEM;
	}
	return result;
}

/** @} lov */
