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

#include <linux/vfs.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include "mgc_internal.h"

LPROC_SEQ_FOPS_RO_TYPE(mgc, uuid);
LPROC_SEQ_FOPS_RO_TYPE(mgc, connect_flags);
LPROC_SEQ_FOPS_RO_TYPE(mgc, server_uuid);
LPROC_SEQ_FOPS_RO_TYPE(mgc, conn_uuid);
LPROC_SEQ_FOPS_RO_TYPE(mgc, import);
LPROC_SEQ_FOPS_RO_TYPE(mgc, state);

LPROC_SEQ_FOPS_WR_ONLY(mgc, ping);

static int mgc_ir_state_seq_show(struct seq_file *m, void *v)
{
	return lprocfs_mgc_rd_ir_state(m, m->private);
}
LPROC_SEQ_FOPS_RO(mgc_ir_state);

static struct lprocfs_vars lprocfs_mgc_obd_vars[] = {
	{ "uuid",	     &mgc_uuid_fops,	  NULL, 0 },
	{ "ping",	     &mgc_ping_fops,      NULL, 0222 },
	{ "connect_flags",   &mgc_connect_flags_fops, NULL, 0 },
	{ "mgs_server_uuid", &mgc_server_uuid_fops,   NULL, 0 },
	{ "mgs_conn_uuid",   &mgc_conn_uuid_fops,     NULL, 0 },
	{ "import",	     &mgc_import_fops,	NULL, 0 },
	{ "state",	     &mgc_state_fops,	 NULL, 0 },
	{ "ir_state",	     &mgc_ir_state_fops,  NULL, 0 },
	{ NULL }
};

LPROC_SEQ_FOPS_RO_TYPE(mgc, numrefs);
static struct lprocfs_vars lprocfs_mgc_module_vars[] = {
	{ "num_refs",	&mgc_numrefs_fops,       NULL, 0 },
	{ NULL }
};

void lprocfs_mgc_init_vars(struct lprocfs_static_vars *lvars)
{
	lvars->module_vars = lprocfs_mgc_module_vars;
	lvars->obd_vars    = lprocfs_mgc_obd_vars;
}
