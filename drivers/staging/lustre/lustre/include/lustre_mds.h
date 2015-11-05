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
 * lustre/include/lustre_mds.h
 *
 * MDS data structures.
 * See also lustre_idl.h for wire formats of requests.
 */

#ifndef _LUSTRE_MDS_H
#define _LUSTRE_MDS_H

/** \defgroup mds mds
 *
 * @{
 */

#include "lustre_handles.h"
#include "../../include/linux/libcfs/libcfs.h"
#include "lustre/lustre_idl.h"
#include "lustre_lib.h"
#include "lustre_dlm.h"
#include "lustre_export.h"

struct mds_group_info {
	struct obd_uuid *uuid;
	int group;
};

#define MDD_OBD_NAME     "mdd_obd"
#define MDD_OBD_UUID     "mdd_obd_uuid"

static inline int md_should_create(__u64 flags)
{
       return !(flags & MDS_OPEN_DELAY_CREATE ||
	       !(flags & FMODE_WRITE));
}

/* these are local flags, used only on the client, private */
#define M_CHECK_STALE	   0200000000

/** @} mds */

#endif
