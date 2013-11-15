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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_ECHO

#include <lprocfs_status.h>
#include <obd_class.h>

#ifdef LPROCFS
LPROC_SEQ_FOPS_RO_TYPE(echo, uuid);
static struct lprocfs_vars lprocfs_echo_obd_vars[] = {
	{ "uuid",	 &echo_uuid_fops,	0, 0 },
	{ 0 }
};

LPROC_SEQ_FOPS_RO_TYPE(echo, numrefs);
static struct lprocfs_vars lprocfs_echo_module_vars[] = {
	{ "num_refs",     &echo_numrefs_fops,     0, 0 },
	{ 0 }
};

void lprocfs_echo_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_echo_module_vars;
    lvars->obd_vars     = lprocfs_echo_obd_vars;
}
#endif /* LPROCFS */
