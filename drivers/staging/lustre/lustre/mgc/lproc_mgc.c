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
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <linux/vfs.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include "mgc_internal.h"

#ifdef LPROCFS

static struct lprocfs_vars lprocfs_mgc_obd_vars[] = {
	{ "uuid",	    lprocfs_rd_uuid,	  0, 0 },
	{ "ping",	    0, lprocfs_wr_ping,       0, 0, 0222 },
	{ "connect_flags",   lprocfs_rd_connect_flags, 0, 0 },
	{ "mgs_server_uuid", lprocfs_rd_server_uuid,   0, 0 },
	{ "mgs_conn_uuid",   lprocfs_rd_conn_uuid,     0, 0 },
	{ "import",	  lprocfs_rd_import,	0, 0 },
	{ "state",	   lprocfs_rd_state,	 0, 0 },
	{ "ir_state",	lprocfs_mgc_rd_ir_state,  0, 0 },
	{ 0 }
};

static struct lprocfs_vars lprocfs_mgc_module_vars[] = {
	{ "num_refs",	lprocfs_rd_numrefs,       0, 0 },
	{ 0 }
};

void lprocfs_mgc_init_vars(struct lprocfs_static_vars *lvars)
{
	lvars->module_vars = lprocfs_mgc_module_vars;
	lvars->obd_vars    = lprocfs_mgc_obd_vars;
}
#endif /* LPROCFS */
