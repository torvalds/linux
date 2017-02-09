/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/slab.h>
#include <asm/byteorder.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>

#include "cxio_hal.h"
#include "cxio_resource.h"
#include "iwch.h"
#include "iwch_provider.h"

static int iwch_finish_mem_reg(struct iwch_mr *mhp, u32 stag)
{
	u32 mmid;

	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	pr_debug("%s mmid 0x%x mhp %p\n", __func__, mmid, mhp);
	return insert_handle(mhp->rhp, &mhp->rhp->mmidr, mhp, mmid);
}

int iwch_register_mem(struct iwch_dev *rhp, struct iwch_pd *php,
		      struct iwch_mr *mhp, int shift)
{
	u32 stag;
	int ret;

	if (cxio_register_phys_mem(&rhp->rdev,
				   &stag, mhp->attr.pdid,
				   mhp->attr.perms,
				   mhp->attr.zbva,
				   mhp->attr.va_fbo,
				   mhp->attr.len,
				   shift - 12,
				   mhp->attr.pbl_size, mhp->attr.pbl_addr))
		return -ENOMEM;

	ret = iwch_finish_mem_reg(mhp, stag);
	if (ret)
		cxio_dereg_mem(&rhp->rdev, mhp->attr.stag, mhp->attr.pbl_size,
		       mhp->attr.pbl_addr);
	return ret;
}

int iwch_alloc_pbl(struct iwch_mr *mhp, int npages)
{
	mhp->attr.pbl_addr = cxio_hal_pblpool_alloc(&mhp->rhp->rdev,
						    npages << 3);

	if (!mhp->attr.pbl_addr)
		return -ENOMEM;

	mhp->attr.pbl_size = npages;

	return 0;
}

void iwch_free_pbl(struct iwch_mr *mhp)
{
	cxio_hal_pblpool_free(&mhp->rhp->rdev, mhp->attr.pbl_addr,
			      mhp->attr.pbl_size << 3);
}

int iwch_write_pbl(struct iwch_mr *mhp, __be64 *pages, int npages, int offset)
{
	return cxio_write_pbl(&mhp->rhp->rdev, pages,
			      mhp->attr.pbl_addr + (offset << 3), npages);
}
