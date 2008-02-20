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
#include <asm/byteorder.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>

#include "cxio_hal.h"
#include "iwch.h"
#include "iwch_provider.h"

int iwch_register_mem(struct iwch_dev *rhp, struct iwch_pd *php,
					struct iwch_mr *mhp,
					int shift,
					__be64 *page_list)
{
	u32 stag;
	u32 mmid;


	if (cxio_register_phys_mem(&rhp->rdev,
				   &stag, mhp->attr.pdid,
				   mhp->attr.perms,
				   mhp->attr.zbva,
				   mhp->attr.va_fbo,
				   mhp->attr.len,
				   shift-12,
				   page_list,
				   &mhp->attr.pbl_size, &mhp->attr.pbl_addr))
		return -ENOMEM;
	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	insert_handle(rhp, &rhp->mmidr, mhp, mmid);
	PDBG("%s mmid 0x%x mhp %p\n", __FUNCTION__, mmid, mhp);
	return 0;
}

int iwch_reregister_mem(struct iwch_dev *rhp, struct iwch_pd *php,
					struct iwch_mr *mhp,
					int shift,
					__be64 *page_list,
					int npages)
{
	u32 stag;
	u32 mmid;


	/* We could support this... */
	if (npages > mhp->attr.pbl_size)
		return -ENOMEM;

	stag = mhp->attr.stag;
	if (cxio_reregister_phys_mem(&rhp->rdev,
				   &stag, mhp->attr.pdid,
				   mhp->attr.perms,
				   mhp->attr.zbva,
				   mhp->attr.va_fbo,
				   mhp->attr.len,
				   shift-12,
				   page_list,
				   &mhp->attr.pbl_size, &mhp->attr.pbl_addr))
		return -ENOMEM;
	mhp->attr.state = 1;
	mhp->attr.stag = stag;
	mmid = stag >> 8;
	mhp->ibmr.rkey = mhp->ibmr.lkey = stag;
	insert_handle(rhp, &rhp->mmidr, mhp, mmid);
	PDBG("%s mmid 0x%x mhp %p\n", __FUNCTION__, mmid, mhp);
	return 0;
}

int build_phys_page_list(struct ib_phys_buf *buffer_list,
					int num_phys_buf,
					u64 *iova_start,
					u64 *total_size,
					int *npages,
					int *shift,
					__be64 **page_list)
{
	u64 mask;
	int i, j, n;

	mask = 0;
	*total_size = 0;
	for (i = 0; i < num_phys_buf; ++i) {
		if (i != 0 && buffer_list[i].addr & ~PAGE_MASK)
			return -EINVAL;
		if (i != 0 && i != num_phys_buf - 1 &&
		    (buffer_list[i].size & ~PAGE_MASK))
			return -EINVAL;
		*total_size += buffer_list[i].size;
		if (i > 0)
			mask |= buffer_list[i].addr;
		else
			mask |= buffer_list[i].addr & PAGE_MASK;
		if (i != num_phys_buf - 1)
			mask |= buffer_list[i].addr + buffer_list[i].size;
		else
			mask |= (buffer_list[i].addr + buffer_list[i].size +
				PAGE_SIZE - 1) & PAGE_MASK;
	}

	if (*total_size > 0xFFFFFFFFULL)
		return -ENOMEM;

	/* Find largest page shift we can use to cover buffers */
	for (*shift = PAGE_SHIFT; *shift < 27; ++(*shift))
		if ((1ULL << *shift) & mask)
			break;

	buffer_list[0].size += buffer_list[0].addr & ((1ULL << *shift) - 1);
	buffer_list[0].addr &= ~0ull << *shift;

	*npages = 0;
	for (i = 0; i < num_phys_buf; ++i)
		*npages += (buffer_list[i].size +
			(1ULL << *shift) - 1) >> *shift;

	if (!*npages)
		return -EINVAL;

	*page_list = kmalloc(sizeof(u64) * *npages, GFP_KERNEL);
	if (!*page_list)
		return -ENOMEM;

	n = 0;
	for (i = 0; i < num_phys_buf; ++i)
		for (j = 0;
		     j < (buffer_list[i].size + (1ULL << *shift) - 1) >> *shift;
		     ++j)
			(*page_list)[n++] = cpu_to_be64(buffer_list[i].addr +
			    ((u64) j << *shift));

	PDBG("%s va 0x%llx mask 0x%llx shift %d len %lld pbl_size %d\n",
	     __FUNCTION__, (unsigned long long) *iova_start,
	     (unsigned long long) mask, *shift, (unsigned long long) *total_size,
	     *npages);

	return 0;

}
