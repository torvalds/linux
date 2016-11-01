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

void ptlrpc_fill_bulk_md(lnet_md_t *md, struct ptlrpc_bulk_desc *desc,
			 int mdidx)
{
	CLASSERT(PTLRPC_MAX_BRW_PAGES < LI_POISON);

	LASSERT(mdidx < desc->bd_md_max_brw);
	LASSERT(desc->bd_iov_count <= PTLRPC_MAX_BRW_PAGES);
	LASSERT(!(md->options & (LNET_MD_IOVEC | LNET_MD_KIOV |
				 LNET_MD_PHYS)));

	md->options |= LNET_MD_KIOV;
	md->length = max(0, desc->bd_iov_count - mdidx * LNET_MAX_IOV);
	md->length = min_t(unsigned int, LNET_MAX_IOV, md->length);
	if (desc->bd_enc_iov)
		md->start = &desc->bd_enc_iov[mdidx * LNET_MAX_IOV];
	else
		md->start = &desc->bd_iov[mdidx * LNET_MAX_IOV];
}

void ptlrpc_add_bulk_page(struct ptlrpc_bulk_desc *desc, struct page *page,
			  int pageoffset, int len)
{
	lnet_kiov_t *kiov = &desc->bd_iov[desc->bd_iov_count];

	kiov->kiov_page = page;
	kiov->kiov_offset = pageoffset;
	kiov->kiov_len = len;

	desc->bd_iov_count++;
}
