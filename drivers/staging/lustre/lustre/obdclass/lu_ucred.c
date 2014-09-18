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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/lu_ucred.c
 *
 * Lustre user credentials context infrastructure.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Fan Yong <fan.yong@intel.com>
 *   Author: Vitaly Fertman <vitaly_fertman@xyratex.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "../../include/linux/libcfs/libcfs.h"
#include "../include/obd_support.h"
#include "../include/lu_object.h"
#include "../include/md_object.h"

/* context key constructor/destructor: lu_ucred_key_init, lu_ucred_key_fini */
LU_KEY_INIT_FINI(lu_ucred, struct lu_ucred);

static struct lu_context_key lu_ucred_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = lu_ucred_key_init,
	.lct_fini = lu_ucred_key_fini
};

/**
 * Get ucred key if session exists and ucred key is allocated on it.
 * Return NULL otherwise.
 */
struct lu_ucred *lu_ucred(const struct lu_env *env)
{
	if (!env->le_ses)
		return NULL;
	return lu_context_key_get(env->le_ses, &lu_ucred_key);
}
EXPORT_SYMBOL(lu_ucred);

/**
 * Get ucred key and check if it is properly initialized.
 * Return NULL otherwise.
 */
struct lu_ucred *lu_ucred_check(const struct lu_env *env)
{
	struct lu_ucred *uc = lu_ucred(env);
	if (uc && uc->uc_valid != UCRED_OLD && uc->uc_valid != UCRED_NEW)
		return NULL;
	return uc;
}
EXPORT_SYMBOL(lu_ucred_check);

/**
 * Get ucred key, which must exist and must be properly initialized.
 * Assert otherwise.
 */
struct lu_ucred *lu_ucred_assert(const struct lu_env *env)
{
	struct lu_ucred *uc = lu_ucred_check(env);
	LASSERT(uc != NULL);
	return uc;
}
EXPORT_SYMBOL(lu_ucred_assert);

int lu_ucred_global_init(void)
{
	LU_CONTEXT_KEY_INIT(&lu_ucred_key);
	return lu_context_key_register(&lu_ucred_key);
}

void lu_ucred_global_fini(void)
{
	lu_context_key_degister(&lu_ucred_key);
}
