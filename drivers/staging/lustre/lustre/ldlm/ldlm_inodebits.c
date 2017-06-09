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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ldlm/ldlm_inodebits.c
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

/**
 * This file contains implementation of IBITS lock type
 *
 * IBITS lock type contains a bit mask determining various properties of an
 * object. The meanings of specific bits are specific to the caller and are
 * opaque to LDLM code.
 *
 * Locks with intersecting bitmasks and conflicting lock modes (e.g.  LCK_PW)
 * are considered conflicting.  See the lock mode compatibility matrix
 * in lustre_dlm.h.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include "../include/lustre_dlm.h"
#include "../include/obd_support.h"
#include "../include/lustre_lib.h"
#include "ldlm_internal.h"

void ldlm_ibits_policy_wire_to_local(const union ldlm_wire_policy_data *wpolicy,
				     union ldlm_policy_data *lpolicy)
{
	lpolicy->l_inodebits.bits = wpolicy->l_inodebits.bits;
}

void ldlm_ibits_policy_local_to_wire(const union ldlm_policy_data *lpolicy,
				     union ldlm_wire_policy_data *wpolicy)
{
	memset(wpolicy, 0, sizeof(*wpolicy));
	wpolicy->l_inodebits.bits = lpolicy->l_inodebits.bits;
}
