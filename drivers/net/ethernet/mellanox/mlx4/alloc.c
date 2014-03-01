/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/bitmap.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include "mlx4.h"

u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap)
{
	u32 obj;

	spin_lock(&bitmap->lock);

	obj = find_next_zero_bit(bitmap->table, bitmap->max, bitmap->last);
	if (obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
				& bitmap->mask;
		obj = find_first_zero_bit(bitmap->table, bitmap->max);
	}

	if (obj < bitmap->max) {
		set_bit(obj, bitmap->table);
		bitmap->last = (obj + 1);
		if (bitmap->last == bitmap->max)
			bitmap->last = 0;
		obj |= bitmap->top;
	} else
		obj = -1;

	if (obj != -1)
		--bitmap->avail;

	spin_unlock(&bitmap->lock);

	return obj;
}

void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj, int use_rr)
{
	mlx4_bitmap_free_range(bitmap, obj, 1, use_rr);
}

u32 mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt, int align)
{
	u32 obj;

	if (likely(cnt == 1 && align == 1))
		return mlx4_bitmap_alloc(bitmap);

	spin_lock(&bitmap->lock);

	obj = bitmap_find_next_zero_area(bitmap->table, bitmap->max,
				bitmap->last, cnt, align - 1);
	if (obj >= bitmap->max) {
		bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
				& bitmap->mask;
		obj = bitmap_find_next_zero_area(bitmap->table, bitmap->max,
						0, cnt, align - 1);
	}

	if (obj < bitmap->max) {
		bitmap_set(bitmap->table, obj, cnt);
		if (obj == bitmap->last) {
			bitmap->last = (obj + cnt);
			if (bitmap->last >= bitmap->max)
				bitmap->last = 0;
		}
		obj |= bitmap->top;
	} else
		obj = -1;

	if (obj != -1)
		bitmap->avail -= cnt;

	spin_unlock(&bitmap->lock);

	return obj;
}

u32 mlx4_bitmap_avail(struct mlx4_bitmap *bitmap)
{
	return bitmap->avail;
}

void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, u32 obj, int cnt,
			    int use_rr)
{
	obj &= bitmap->max + bitmap->reserved_top - 1;

	spin_lock(&bitmap->lock);
	if (!use_rr) {
		bitmap->last = min(bitmap->last, obj);
		bitmap->top = (bitmap->top + bitmap->max + bitmap->reserved_top)
				& bitmap->mask;
	}
	bitmap_clear(bitmap->table, obj, cnt);
	bitmap->avail += cnt;
	spin_unlock(&bitmap->lock);
}

int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, u32 num, u32 mask,
		     u32 reserved_bot, u32 reserved_top)
{
	/* num must be a power of 2 */
	if (num != roundup_pow_of_two(num))
		return -EINVAL;

	bitmap->last = 0;
	bitmap->top  = 0;
	bitmap->max  = num - reserved_top;
	bitmap->mask = mask;
	bitmap->reserved_top = reserved_top;
	bitmap->avail = num - reserved_top - reserved_bot;
	spin_lock_init(&bitmap->lock);
	bitmap->table = kzalloc(BITS_TO_LONGS(bitmap->max) *
				sizeof (long), GFP_KERNEL);
	if (!bitmap->table)
		return -ENOMEM;

	bitmap_set(bitmap->table, 0, reserved_bot);

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
		buf->direct.buf   = dma_alloc_coherent(&dev->pdev->dev,
						       size, &t, GFP_KERNEL);
		if (!buf->direct.buf)
			return -ENOMEM;

		buf->direct.map = t;

		while (t & ((1 << buf->page_shift) - 1)) {
			--buf->page_shift;
			buf->npages *= 2;
		}

		memset(buf->direct.buf, 0, size);
	} else {
		int i;

		buf->direct.buf  = NULL;
		buf->nbufs       = (size + PAGE_SIZE - 1) / PAGE_SIZE;
		buf->npages      = buf->nbufs;
		buf->page_shift  = PAGE_SHIFT;
		buf->page_list   = kcalloc(buf->nbufs, sizeof(*buf->page_list),
					   GFP_KERNEL);
		if (!buf->page_list)
			return -ENOMEM;

		for (i = 0; i < buf->nbufs; ++i) {
			buf->page_list[i].buf =
				dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
						   &t, GFP_KERNEL);
			if (!buf->page_list[i].buf)
				goto err_free;

			buf->page_list[i].map = t;

			memset(buf->page_list[i].buf, 0, PAGE_SIZE);
		}

		if (BITS_PER_LONG == 64) {
			struct page **pages;
			pages = kmalloc(sizeof *pages * buf->nbufs, GFP_KERNEL);
			if (!pages)
				goto err_free;
			for (i = 0; i < buf->nbufs; ++i)
				pages[i] = virt_to_page(buf->page_list[i].buf);
			buf->direct.buf = vmap(pages, buf->nbufs, VM_MAP, PAGE_KERNEL);
			kfree(pages);
			if (!buf->direct.buf)
				goto err_free;
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
		dma_free_coherent(&dev->pdev->dev, size, buf->direct.buf,
				  buf->direct.map);
	else {
		if (BITS_PER_LONG == 64 && buf->direct.buf)
			vunmap(buf->direct.buf);

		for (i = 0; i < buf->nbufs; ++i)
			if (buf->page_list[i].buf)
				dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
						  buf->page_list[i].buf,
						  buf->page_list[i].map);
		kfree(buf->page_list);
	}
}
EXPORT_SYMBOL_GPL(mlx4_buf_free);

static struct mlx4_db_pgdir *mlx4_alloc_db_pgdir(struct device *dma_device)
{
	struct mlx4_db_pgdir *pgdir;

	pgdir = kzalloc(sizeof *pgdir, GFP_KERNEL);
	if (!pgdir)
		return NULL;

	bitmap_fill(pgdir->order1, MLX4_DB_PER_PAGE / 2);
	pgdir->bits[0] = pgdir->order0;
	pgdir->bits[1] = pgdir->order1;
	pgdir->db_page = dma_alloc_coherent(dma_device, PAGE_SIZE,
					    &pgdir->db_dma, GFP_KERNEL);
	if (!pgdir->db_page) {
		kfree(pgdir);
		return NULL;
	}

	return pgdir;
}

static int mlx4_alloc_db_from_pgdir(struct mlx4_db_pgdir *pgdir,
				    struct mlx4_db *db, int order)
{
	int o;
	int i;

	for (o = order; o <= 1; ++o) {
		i = find_first_bit(pgdir->bits[o], MLX4_DB_PER_PAGE >> o);
		if (i < MLX4_DB_PER_PAGE >> o)
			goto found;
	}

	return -ENOMEM;

found:
	clear_bit(i, pgdir->bits[o]);

	i <<= o;

	if (o > order)
		set_bit(i ^ 1, pgdir->bits[order]);

	db->u.pgdir = pgdir;
	db->index   = i;
	db->db      = pgdir->db_page + db->index;
	db->dma     = pgdir->db_dma  + db->index * 4;
	db->order   = order;

	return 0;
}

int mlx4_db_alloc(struct mlx4_dev *dev, struct mlx4_db *db, int order)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_db_pgdir *pgdir;
	int ret = 0;

	mutex_lock(&priv->pgdir_mutex);

	list_for_each_entry(pgdir, &priv->pgdir_list, list)
		if (!mlx4_alloc_db_from_pgdir(pgdir, db, order))
			goto out;

	pgdir = mlx4_alloc_db_pgdir(&(dev->pdev->dev));
	if (!pgdir) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&pgdir->list, &priv->pgdir_list);

	/* This should never fail -- we just allocated an empty page: */
	WARN_ON(mlx4_alloc_db_from_pgdir(pgdir, db, order));

out:
	mutex_unlock(&priv->pgdir_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mlx4_db_alloc);

void mlx4_db_free(struct mlx4_dev *dev, struct mlx4_db *db)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int o;
	int i;

	mutex_lock(&priv->pgdir_mutex);

	o = db->order;
	i = db->index;

	if (db->order == 0 && test_bit(i ^ 1, db->u.pgdir->order0)) {
		clear_bit(i ^ 1, db->u.pgdir->order0);
		++o;
	}
	i >>= o;
	set_bit(i, db->u.pgdir->bits[o]);

	if (bitmap_full(db->u.pgdir->order1, MLX4_DB_PER_PAGE / 2)) {
		dma_free_coherent(&(dev->pdev->dev), PAGE_SIZE,
				  db->u.pgdir->db_page, db->u.pgdir->db_dma);
		list_del(&db->u.pgdir->list);
		kfree(db->u.pgdir);
	}

	mutex_unlock(&priv->pgdir_mutex);
}
EXPORT_SYMBOL_GPL(mlx4_db_free);

int mlx4_alloc_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		       int size, int max_direct)
{
	int err;

	err = mlx4_db_alloc(dev, &wqres->db, 1);
	if (err)
		return err;

	*wqres->db.db = 0;

	err = mlx4_buf_alloc(dev, size, max_direct, &wqres->buf);
	if (err)
		goto err_db;

	err = mlx4_mtt_init(dev, wqres->buf.npages, wqres->buf.page_shift,
			    &wqres->mtt);
	if (err)
		goto err_buf;

	err = mlx4_buf_write_mtt(dev, &wqres->mtt, &wqres->buf);
	if (err)
		goto err_mtt;

	return 0;

err_mtt:
	mlx4_mtt_cleanup(dev, &wqres->mtt);
err_buf:
	mlx4_buf_free(dev, size, &wqres->buf);
err_db:
	mlx4_db_free(dev, &wqres->db);

	return err;
}
EXPORT_SYMBOL_GPL(mlx4_alloc_hwq_res);

void mlx4_free_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		       int size)
{
	mlx4_mtt_cleanup(dev, &wqres->mtt);
	mlx4_buf_free(dev, size, &wqres->buf);
	mlx4_db_free(dev, &wqres->db);
}
EXPORT_SYMBOL_GPL(mlx4_free_hwq_res);
