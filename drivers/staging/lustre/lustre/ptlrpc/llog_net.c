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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/llog_net.c
 *
 * OST<->MDS recovery logging infrastructure.
 *
 * Invariants in implementation:
 * - we do not share logs among different OST<->MDS connections, so that
 *   if an OST or MDS fails it need only look at log(s) relevant to itself
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOG

#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_class.h"
#include "../include/lustre_log.h"
#include <linux/list.h>

int llog_initiator_connect(struct llog_ctxt *ctxt)
{
	struct obd_import *new_imp;

	LASSERT(ctxt);
	new_imp = ctxt->loc_obd->u.cli.cl_import;
	LASSERTF(!ctxt->loc_imp || ctxt->loc_imp == new_imp,
		 "%p - %p\n", ctxt->loc_imp, new_imp);
	mutex_lock(&ctxt->loc_mutex);
	if (ctxt->loc_imp != new_imp) {
		if (ctxt->loc_imp)
			class_import_put(ctxt->loc_imp);
		ctxt->loc_imp = class_import_get(new_imp);
	}
	mutex_unlock(&ctxt->loc_mutex);
	return 0;
}
EXPORT_SYMBOL(llog_initiator_connect);
