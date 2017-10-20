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
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _MGC_INTERNAL_H
#define _MGC_INTERNAL_H

#include <linux/libcfs/libcfs.h>
#include <lustre_lib.h>
#include <lustre_dlm.h>
#include <lustre_log.h>
#include <lustre_export.h>

void lprocfs_mgc_init_vars(struct lprocfs_static_vars *lvars);
int lprocfs_mgc_rd_ir_state(struct seq_file *m, void *data);

int mgc_process_log(struct obd_device *mgc, struct config_llog_data *cld);

static inline int cld_is_sptlrpc(struct config_llog_data *cld)
{
	return cld->cld_type == CONFIG_T_SPTLRPC;
}

static inline int cld_is_recover(struct config_llog_data *cld)
{
	return cld->cld_type == CONFIG_T_RECOVER;
}

#endif  /* _MGC_INTERNAL_H */
