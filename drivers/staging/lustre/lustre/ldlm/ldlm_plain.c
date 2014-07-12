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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ldlm/ldlm_plain.c
 *
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Phil Schwan <phil@clusterfs.com>
 */

/**
 * This file contains implementation of PLAIN lock type.
 *
 * PLAIN locks are the simplest form of LDLM locking, and are used when
 * there only needs to be a single lock on a resource. This avoids some
 * of the complexity of EXTENT and IBITS lock types, but doesn't allow
 * different "parts" of a resource to be locked concurrently.  Example
 * use cases for PLAIN locks include locking of MGS configuration logs
 * and (as of Lustre 2.4) quota records.
 */

#define DEBUG_SUBSYSTEM S_LDLM

#include "../include/lustre_dlm.h"
#include "../include/obd_support.h"
#include "../include/lustre_lib.h"

#include "ldlm_internal.h"


void ldlm_plain_policy_wire_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy)
{
	/* No policy for plain locks */
}

void ldlm_plain_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy)
{
	/* No policy for plain locks */
}
