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

#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include "hns_roce_device.h"
#include <rdma/ib_umem.h>

int hns_roce_bitmap_alloc(struct hns_roce_bitmap *bitmap, unsigned long *obj)
{
	int ret = 0;

	spin_lock(&bitmap->lock);
	*obj = find_next_zero_bit(bitmap->table, bitmap->max, bitmap->last);
	if (*obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
			       & bitmap->mask;
		*obj = find_first_zero_bit(bitmap->table, bitmap->max);
	}

	if (*obj < bitmap->max) {
		set_bit(*obj, bitmap->table);
		bitmap->last = (*obj + 1);
		if (bitmap->last == bitmap->max)
			bitmap->last = 0;
		*obj |= bitmap->top;
	} else {
		ret = -1;
	}

	spin_unlock(&bitmap->lock);

	return ret;
}

void hns_roce_bitmap_free(struct hns_roce_bitmap *bitmap, unsigned long obj,
			  int rr)
{
	hns_roce_bitmap_free_range(bitmap, obj, 1, rr);
}
EXPORT_SYMBOL_GPL(hns_roce_bitmap_free);

int hns_roce_bitmap_alloc_range(struct hns_roce_bitmap *bitmap, int cnt,
				int align, unsigned long *obj)
{
	int ret = 0;
	int i;

	if (likely(cnt == 1 && align == 1))
		return hns_roce_bitmap_alloc(bitmap, obj);

	spin_lock(&bitmap->lock);

	*obj = bitmap_find_next_zero_area(bitmap->table, bitmap->max,
					  bitmap->last, cnt, align - 1);
	if (*obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
			       & bitmap->mask;
		*obj = bitmap_find_next_zero_area(bitmap->table, bitmap->max, 0,
						  cnt, align - 1);
	}

	if (*obj < bitmap->max) {
		for (i = 0; i < cnt; i++)
			set_bit(*obj + i, bitmap->table);

		if (*obj == bitmap->last) {
			bitmap->last = (*obj + cnt);
			if (bitmap->last >= bitmap->max)
				bitmap->last = 0;
		}
		*obj |= bitmap->top;
	} else {
		ret = -1;
	}

	spin_unlock(&bitmap->lock);

	return ret;
}

void hns_roce_bitmap_free_range(struct hns_roce_bitmap *bitmap,
				unsigned long obj, int cnt,
				int rr)
{
	int i;

	obj &= bitmap->max + bitmap->reserved_top - 1;

	spin_lock(&bitmap->lock);
	for (i = 0; i < cnt; i++)
		clear_bit(obj + i, bitmap->table);

	if (!rr)
		bitmap->last = min(bitmap->last, obj);
	bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
		       & bitmap->mask;
	spin_unlock(&bitmap->lock);
}

int hns_roce_bitmap_init(struct hns_roce_bitmap *bitmap, u32 num, u32 mask,
			 u32 reserved_bot, u32 reserved_top)
{
	u32 i;

	if (num != roundup_pow_of_two(num))
		return -EINVAL;

	bitmap->last = 0;
	bitmap->top = 0;
	bitmap->max = num - reserved_top;
	bitmap->mask = mask;
	bitmap->reserved_top = reserved_top;
	spin_lock_init(&bitmap->lock);
	bitmap->table = kcalloc(BITS_TO_LONGS(bitmap->max), sizeof(long),
				GFP_KERNEL);
	if (!bitmap->table)
		return -ENOMEM;

	for (i = 0; i < reserved_bot; ++i)
		set_bit(i, bitmap->table);

	return 0;
}

void hns_roce_bitmap_cleanup(struct hns_roce_bitmap *bitmap)
{
	kfree(bitmap->table);
}

void hns_roce_buf_free(struct hns_roce_dev *hr_dev, u32 size,
		       struct hns_roce_buf *buf)
{
	int i;
	struct device *dev = hr_dev->dev;

	if (buf->nbufs == 1) {
		dma_free_coherent(dev, size, buf->direct.buf, buf->direct.map);
	} else {
		for (i = 0; i < buf->nbufs; ++i)
			if (buf->page_list[i].buf)
				dma_free_coherent(dev, 1 << buf->page_shift,
						  buf->page_list[i].buf,
						  buf->page_list[i].map);
		kfree(buf->page_list);
	}
}
EXPORT_SYMBOL_GPL(hns_roce_buf_free);

int hns_roce_buf_alloc(struct hns_roce_dev *hr_dev, u32 size, u32 max_direct,
		       struct hns_roce_buf *buf, u32 page_shift)
{
	int i = 0;
	dma_addr_t t;
	struct device *dev = hr_dev->dev;
	u32 page_size = 1 << page_shift;
	u32 order;

	/* SQ/RQ buf lease than one page, SQ + RQ = 8K */
	if (size <= max_direct) {
		buf->nbufs = 1;
		/* Npages calculated by page_size */
		order = get_order(size);
		if (order <= page_shift - PAGE_SHIFT)
			order = 0;
		else
			order -= page_shift - PAGE_SHIFT;
		buf->npages = 1 << order;
		buf->page_shift = page_shift;
		/* MTT PA must be recorded in 4k alignment, t is 4k aligned */
		buf->direct.buf = dma_alloc_coherent(dev, size, &t,
						     GFP_KERNEL);
		if (!buf->direct.buf)
			return -ENOMEM;

		buf->direct.map = t;

		while (t & ((1 << buf->page_shift) - 1)) {
			--buf->page_shift;
			buf->npages *= 2;
		}
	} else {
		buf->nbufs = (size + page_size - 1) / page_size;
		buf->npages = buf->nbufs;
		buf->page_shift = page_shift;
		buf->page_list = kcalloc(buf->nbufs, sizeof(*buf->page_list),
					 GFP_KERNEL);

		if (!buf->page_list)
			return -ENOMEM;

		for (i = 0; i < buf->nbufs; ++i) {
			buf->page_list[i].buf = dma_alloc_coherent(dev,
								   page_size,
								   &t,
								   GFP_KERNEL);

			if (!buf->page_list[i].buf)
				goto err_free;

			buf->page_list[i].map = t;
		}
	}

	return 0;

err_free:
	hns_roce_buf_free(hr_dev, size, buf);
	return -ENOMEM;
}

int hns_roce_get_kmem_bufs(struct hns_roce_dev *hr_dev, dma_addr_t *bufs,
			   int buf_cnt, int start, struct hns_roce_buf *buf)
{
	int i, end;
	int total;

	end = start + buf_cnt;
	if (end > buf->npages) {
		dev_err(hr_dev->dev,
			"invalid kmem region,offset %d,buf_cnt %d,total %d!\n",
			start, buf_cnt, buf->npages);
		return -EINVAL;
	}

	total = 0;
	for (i = start; i < end; i++)
		if (buf->nbufs == 1)
			bufs[total++] = buf->direct.map +
					((dma_addr_t)i << buf->page_shift);
		else
			bufs[total++] = buf->page_list[i].map;

	return total;
}

int hns_roce_get_umem_bufs(struct hns_roce_dev *hr_dev, dma_addr_t *bufs,
			   int buf_cnt, int start, struct ib_umem *umem,
			   int page_shift)
{
	struct ib_block_iter biter;
	int total = 0;
	int idx = 0;
	u64 addr;

	if (page_shift < PAGE_SHIFT) {
		dev_err(hr_dev->dev, "invalid page shift %d!\n", page_shift);
		return -EINVAL;
	}

	/* convert system page cnt to hw page cnt */
	rdma_for_each_block(umem->sg_head.sgl, &biter, umem->nmap,
			    1 << page_shift) {
		addr = rdma_block_iter_dma_address(&biter);
		if (idx >= start) {
			bufs[total++] = addr;
			if (total >= buf_cnt)
				goto done;
		}
		idx++;
	}

done:
	return total;
}

void hns_roce_init_buf_region(struct hns_roce_buf_region *region, int hopnum,
			      int offset, int buf_cnt)
{
	if (hopnum == HNS_ROCE_HOP_NUM_0)
		region->hopnum = 0;
	else
		region->hopnum = hopnum;

	region->offset = offset;
	region->count = buf_cnt;
}

void hns_roce_free_buf_list(dma_addr_t **bufs, int region_cnt)
{
	int i;

	for (i = 0; i < region_cnt; i++) {
		kfree(bufs[i]);
		bufs[i] = NULL;
	}
}

int hns_roce_alloc_buf_list(struct hns_roce_buf_region *regions,
			    dma_addr_t **bufs, int region_cnt)
{
	struct hns_roce_buf_region *r;
	int i;

	for (i = 0; i < region_cnt; i++) {
		r = &regions[i];
		bufs[i] = kcalloc(r->count, sizeof(dma_addr_t), GFP_KERNEL);
		if (!bufs[i])
			goto err_alloc;
	}

	return 0;

err_alloc:
	hns_roce_free_buf_list(bufs, i);

	return -ENOMEM;
}

void hns_roce_cleanup_bitmap(struct hns_roce_dev *hr_dev)
{
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ)
		hns_roce_cleanup_srq_table(hr_dev);
	hns_roce_cleanup_qp_table(hr_dev);
	hns_roce_cleanup_cq_table(hr_dev);
	hns_roce_cleanup_mr_table(hr_dev);
	hns_roce_cleanup_pd_table(hr_dev);
	hns_roce_cleanup_uar_table(hr_dev);
}
