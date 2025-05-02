/*
 * Copyright (c) 2016 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/vmalloc.h>
#include <rdma/ib_umem.h>
#include "hns_roce_device.h"

void hns_roce_buf_free(struct hns_roce_dev *hr_dev, struct hns_roce_buf *buf)
{
	struct hns_roce_buf_list *trunks;
	u32 i;

	if (!buf)
		return;

	trunks = buf->trunk_list;
	if (trunks) {
		buf->trunk_list = NULL;
		for (i = 0; i < buf->ntrunks; i++)
			dma_free_coherent(hr_dev->dev, 1 << buf->trunk_shift,
					  trunks[i].buf, trunks[i].map);

		kfree(trunks);
	}

	kfree(buf);
}

/*
 * Allocate the dma buffer for storing ROCEE table entries
 *
 * @size: required size
 * @page_shift: the unit size in a continuous dma address range
 * @flags: HNS_ROCE_BUF_ flags to control the allocation flow.
 */
struct hns_roce_buf *hns_roce_buf_alloc(struct hns_roce_dev *hr_dev, u32 size,
					u32 page_shift, u32 flags)
{
	u32 trunk_size, page_size, alloced_size;
	struct hns_roce_buf_list *trunks;
	struct hns_roce_buf *buf;
	gfp_t gfp_flags;
	u32 ntrunk, i;

	/* The minimum shift of the page accessed by hw is HNS_HW_PAGE_SHIFT */
	if (WARN_ON(page_shift < HNS_HW_PAGE_SHIFT))
		return ERR_PTR(-EINVAL);

	gfp_flags = (flags & HNS_ROCE_BUF_NOSLEEP) ? GFP_ATOMIC : GFP_KERNEL;
	buf = kzalloc(sizeof(*buf), gfp_flags);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->page_shift = page_shift;
	page_size = 1 << buf->page_shift;

	/* Calc the trunk size and num by required size and page_shift */
	if (flags & HNS_ROCE_BUF_DIRECT) {
		buf->trunk_shift = order_base_2(ALIGN(size, PAGE_SIZE));
		ntrunk = 1;
	} else {
		buf->trunk_shift = order_base_2(ALIGN(page_size, PAGE_SIZE));
		ntrunk = DIV_ROUND_UP(size, 1 << buf->trunk_shift);
	}

	trunks = kcalloc(ntrunk, sizeof(*trunks), gfp_flags);
	if (!trunks) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	trunk_size = 1 << buf->trunk_shift;
	alloced_size = 0;
	for (i = 0; i < ntrunk; i++) {
		trunks[i].buf = dma_alloc_coherent(hr_dev->dev, trunk_size,
						   &trunks[i].map, gfp_flags);
		if (!trunks[i].buf)
			break;

		alloced_size += trunk_size;
	}

	buf->ntrunks = i;

	/* In nofail mode, it's only failed when the alloced size is 0 */
	if ((flags & HNS_ROCE_BUF_NOFAIL) ? i == 0 : i != ntrunk) {
		for (i = 0; i < buf->ntrunks; i++)
			dma_free_coherent(hr_dev->dev, trunk_size,
					  trunks[i].buf, trunks[i].map);

		kfree(trunks);
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	buf->npages = DIV_ROUND_UP(alloced_size, page_size);
	buf->trunk_list = trunks;

	return buf;
}

int hns_roce_get_kmem_bufs(struct hns_roce_dev *hr_dev, dma_addr_t *bufs,
			   int buf_cnt, struct hns_roce_buf *buf,
			   unsigned int page_shift)
{
	unsigned int offset, max_size;
	int total = 0;
	int i;

	if (page_shift > buf->trunk_shift) {
		dev_err(hr_dev->dev, "failed to check kmem buf shift %u > %u\n",
			page_shift, buf->trunk_shift);
		return -EINVAL;
	}

	offset = 0;
	max_size = buf->ntrunks << buf->trunk_shift;
	for (i = 0; i < buf_cnt && offset < max_size; i++) {
		bufs[total++] = hns_roce_buf_dma_addr(buf, offset);
		offset += (1 << page_shift);
	}

	return total;
}

int hns_roce_get_umem_bufs(dma_addr_t *bufs, int buf_cnt, struct ib_umem *umem,
			   unsigned int page_shift)
{
	struct ib_block_iter biter;
	int total = 0;

	/* convert system page cnt to hw page cnt */
	rdma_umem_for_each_dma_block(umem, &biter, 1 << page_shift) {
		bufs[total++] = rdma_block_iter_dma_address(&biter);
		if (total >= buf_cnt)
			goto done;
	}

done:
	return total;
}

void hns_roce_cleanup_bitmap(struct hns_roce_dev *hr_dev)
{
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC)
		ida_destroy(&hr_dev->xrcd_ida.ida);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		ida_destroy(&hr_dev->srq_table.srq_ida.ida);
		xa_destroy(&hr_dev->srq_table.xa);
	}
	hns_roce_cleanup_qp_table(hr_dev);
	hns_roce_cleanup_cq_table(hr_dev);
	ida_destroy(&hr_dev->mr_table.mtpt_ida.ida);
	ida_destroy(&hr_dev->pd_ida.ida);
	ida_destroy(&hr_dev->uar_ida.ida);
}
