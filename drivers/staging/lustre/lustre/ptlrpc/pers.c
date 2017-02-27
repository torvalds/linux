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
 * Copyright (c) 2014, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC

#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_import.h"

#include "ptlrpc_internal.h"

void ptlrpc_fill_bulk_md(struct lnet_md *md, struct ptlrpc_bulk_desc *desc,
			 int mdidx)
{
	int offset = mdidx * LNET_MAX_IOV;

	BUILD_BUG_ON(PTLRPC_MAX_BRW_PAGES >= LI_POISON);

	LASSERT(mdidx < desc->bd_md_max_brw);
	LASSERT(desc->bd_iov_count <= PTLRPC_MAX_BRW_PAGES);
	LASSERT(!(md->options & (LNET_MD_IOVEC | LNET_MD_KIOV |
				 LNET_MD_PHYS)));

	md->length = max(0, desc->bd_iov_count - mdidx * LNET_MAX_IOV);
	md->length = min_t(unsigned int, LNET_MAX_IOV, md->length);

	if (ptlrpc_is_bulk_desc_kiov(desc->bd_type)) {
		md->options |= LNET_MD_KIOV;
		if (GET_ENC_KIOV(desc))
			md->start = &BD_GET_ENC_KIOV(desc, offset);
		else
			md->start = &BD_GET_KIOV(desc, offset);
	} else {
		md->options |= LNET_MD_IOVEC;
		if (GET_ENC_KVEC(desc))
			md->start = &BD_GET_ENC_KVEC(desc, offset);
		else
			md->start = &BD_GET_KVEC(desc, offset);
	}
}
