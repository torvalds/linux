/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitmap.h>

#include "mlx4.h"

u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap)
{
	u32 obj;

	spin_lock(&bitmap->lock);

	obj = find_next_zero_bit(bitmap->table, bitmap->max, bitmap->last);
	if (obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
		obj = find_first_zero_bit(bitmap->table, bitmap->max);
	}

	if (obj < bitmap->max) {
		set_bit(obj, bitmap->table);
		obj |= bitmap->top;
		bitmap->last = obj + 1;
	} else
		obj = -1;

	spin_unlock(&bitmap->lock);

	return obj;
}

void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj)
{
	obj &= bitmap->max - 1;

	spin_lock(&bitmap->lock);
	clear_bit(obj, bitmap->table);
	bitmap->last = min(bitmap->last, obj);
	bitmap->top = (bitmap->top + bitmap->max) & bitmap->mask;
	spin_unlock(&bitmap->lock);
}

int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, u32 num, u32 mask, u32 reserved)
{
	int i;

	/* num must be a power of 2 */
	if (num != roundup_pow_of_two(num))
		return -EINVAL;

	bitmap->last = 0;
	bitmap->top  = 0;
	bitmap->max  = num;
	bitmap->mask = mask;
	spin_lock_init(&bitmap->lock);
	bitmap->table = kzalloc(BITS_TO_LONGS(num) * sizeof (long), GFP_KERNEL);
	if (!bitmap->table)
		return -ENOMEM;

	for (i = 0; i < reserved; ++i)
		set_bit(i, bitmap->table);

	return 0;
}

void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap)
{
	kfree(bitmap->table);
}

/*
 * Handling for queue buffers -- we allocate a bunch of memory and
 * register it in a memory region at HCA virtual address 0.  If the
 * requested size is > max_direct, we split the allocation into
 * multiple pages, so we don't require too much contiguous memory.
 */

int mlx4_buf_alloc(struct mlx4_dev *dev, int size, int max_direct,
		   struct mlx4_buf *buf)
{
	dma_addr_t t;

	if (size <= max_direct) {
		buf->nbufs        = 1;
		buf->npages       = 1;
		buf->page_shift   = get_order(size) + PAGE_SHIFT;
		buf->u.direct.buf = dma_alloc_coherent(&dev->pdev->dev,
						       size, &t, GFP_KERNEL);
		if (!buf->u.direct.buf)
			return -ENOMEM;

		buf->u.direct.map = t;

		while (t & ((1 << buf->page_shift) - 1)) {
			--buf->page_shift;
			buf->npages *= 2;
		}

		memset(buf->u.direct.buf, 0, size);
	} else {
		int i;

		buf->nbufs       = (size + PAGE_SIZE - 1) / PAGE_SIZE;
		buf->npages      = buf->nbufs;
		buf->page_shift  = PAGE_SHIFT;
		buf->u.page_list = kzalloc(buf->nbufs * sizeof *buf->u.page_list,
					   GFP_KERNEL);
		if (!buf->u.page_list)
			return -ENOMEM;

		for (i = 0; i < buf->nbufs; ++i) {
			buf->u.page_list[i].buf =
				dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
						   &t, GFP_KERNEL);
			if (!buf->u.page_list[i].buf)
				goto err_free;

			buf->u.page_list[i].map = t;

			memset(buf->u.page_list[i].buf, 0, PAGE_SIZE);
		}
	}

	return 0;

err_free:
	mlx4_buf_free(dev, size, buf);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(mlx4_buf_alloc);

void mlx4_buf_free(struct mlx4_dev *dev, int size, struct mlx4_buf *buf)
{
	int i;

	if (buf->nbufs == 1)
		dma_free_coherent(&dev->pdev->dev, size, buf->u.direct.buf,
				  buf->u.direct.map);
	else {
		for (i = 0; i < buf->nbufs; ++i)
			dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  buf->u.page_list[i].buf,
					  buf->u.page_list[i].map);
		kfree(buf->u.page_list);
	}
}
EXPORT_SYMBOL_GPL(mlx4_buf_free);
