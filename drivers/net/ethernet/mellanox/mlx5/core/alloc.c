/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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
#include <linux/mlx5/driver.h>

#include "mlx5_core.h"

struct mlx5_db_pgdir {
	struct list_head	list;
	unsigned long	       *bitmap;
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

/* Handling for queue buffers -- we allocate a bunch of memory and
 * register it in a memory region at HCA virtual address 0.
 */

static void *mlx5_dma_zalloc_coherent_node(struct mlx5_core_dev *dev,
					   size_t size, dma_addr_t *dma_handle,
					   int node)
{
	struct device *device = mlx5_core_dma_dev(dev);
	struct mlx5_priv *priv = &dev->priv;
	int original_node;
	void *cpu_handle;

	mutex_lock(&priv->alloc_mutex);
	original_node = dev_to_node(device);
	set_dev_node(device, node);
	cpu_handle = dma_alloc_coherent(device, size, dma_handle,
					GFP_KERNEL);
	set_dev_node(device, original_node);
	mutex_unlock(&priv->alloc_mutex);
	return cpu_handle;
}

int mlx5_frag_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			     struct mlx5_frag_buf *buf, int node)
{
	int i;

	buf->size = size;
	buf->npages = DIV_ROUND_UP(size, PAGE_SIZE);
	buf->page_shift = PAGE_SHIFT;
	buf->frags = kcalloc(buf->npages, sizeof(struct mlx5_buf_list),
			     GFP_KERNEL);
	if (!buf->frags)
		goto err_out;

	for (i = 0; i < buf->npages; i++) {
		struct mlx5_buf_list *frag = &buf->frags[i];
		int frag_sz = min_t(int, size, PAGE_SIZE);

		frag->buf = mlx5_dma_zalloc_coherent_node(dev, frag_sz,
							  &frag->map, node);
		if (!frag->buf)
			goto err_free_buf;
		if (frag->map & ((1 << buf->page_shift) - 1)) {
			dma_free_coherent(mlx5_core_dma_dev(dev), frag_sz,
					  buf->frags[i].buf, buf->frags[i].map);
			mlx5_core_warn(dev, "unexpected map alignment: %pad, page_shift=%d\n",
				       &frag->map, buf->page_shift);
			goto err_free_buf;
		}
		size -= frag_sz;
	}

	return 0;

err_free_buf:
	while (i--)
		dma_free_coherent(mlx5_core_dma_dev(dev), PAGE_SIZE, buf->frags[i].buf,
				  buf->frags[i].map);
	kfree(buf->frags);
err_out:
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(mlx5_frag_buf_alloc_node);

void mlx5_frag_buf_free(struct mlx5_core_dev *dev, struct mlx5_frag_buf *buf)
{
	int size = buf->size;
	int i;

	for (i = 0; i < buf->npages; i++) {
		int frag_sz = min_t(int, size, PAGE_SIZE);

		dma_free_coherent(mlx5_core_dma_dev(dev), frag_sz, buf->frags[i].buf,
				  buf->frags[i].map);
		size -= frag_sz;
	}
	kfree(buf->frags);
}
EXPORT_SYMBOL_GPL(mlx5_frag_buf_free);

static struct mlx5_db_pgdir *mlx5_alloc_db_pgdir(struct mlx5_core_dev *dev,
						 int node)
{
	u32 db_per_page = PAGE_SIZE / cache_line_size();
	struct mlx5_db_pgdir *pgdir;

	pgdir = kzalloc_node(sizeof(*pgdir), GFP_KERNEL, node);
	if (!pgdir)
		return NULL;

	pgdir->bitmap = bitmap_zalloc_node(db_per_page, GFP_KERNEL, node);
	if (!pgdir->bitmap) {
		kfree(pgdir);
		return NULL;
	}

	bitmap_fill(pgdir->bitmap, db_per_page);

	pgdir->db_page = mlx5_dma_zalloc_coherent_node(dev, PAGE_SIZE,
						       &pgdir->db_dma, node);
	if (!pgdir->db_page) {
		bitmap_free(pgdir->bitmap);
		kfree(pgdir);
		return NULL;
	}

	return pgdir;
}

static int mlx5_alloc_db_from_pgdir(struct mlx5_db_pgdir *pgdir,
				    struct mlx5_db *db)
{
	u32 db_per_page = PAGE_SIZE / cache_line_size();
	int offset;
	int i;

	i = find_first_bit(pgdir->bitmap, db_per_page);
	if (i >= db_per_page)
		return -ENOMEM;

	__clear_bit(i, pgdir->bitmap);

	db->u.pgdir = pgdir;
	db->index   = i;
	offset = db->index * cache_line_size();
	db->db      = pgdir->db_page + offset / sizeof(*pgdir->db_page);
	db->dma     = pgdir->db_dma  + offset;

	db->db[0] = 0;
	db->db[1] = 0;

	return 0;
}

int mlx5_db_alloc_node(struct mlx5_core_dev *dev, struct mlx5_db *db, int node)
{
	struct mlx5_db_pgdir *pgdir;
	int ret = 0;

	mutex_lock(&dev->priv.pgdir_mutex);

	list_for_each_entry(pgdir, &dev->priv.pgdir_list, list)
		if (!mlx5_alloc_db_from_pgdir(pgdir, db))
			goto out;

	pgdir = mlx5_alloc_db_pgdir(dev, node);
	if (!pgdir) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&pgdir->list, &dev->priv.pgdir_list);

	/* This should never fail -- we just allocated an empty page: */
	WARN_ON(mlx5_alloc_db_from_pgdir(pgdir, db));

out:
	mutex_unlock(&dev->priv.pgdir_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mlx5_db_alloc_node);

int mlx5_db_alloc(struct mlx5_core_dev *dev, struct mlx5_db *db)
{
	return mlx5_db_alloc_node(dev, db, dev->priv.numa_node);
}
EXPORT_SYMBOL_GPL(mlx5_db_alloc);

void mlx5_db_free(struct mlx5_core_dev *dev, struct mlx5_db *db)
{
	u32 db_per_page = PAGE_SIZE / cache_line_size();

	mutex_lock(&dev->priv.pgdir_mutex);

	__set_bit(db->index, db->u.pgdir->bitmap);

	if (bitmap_full(db->u.pgdir->bitmap, db_per_page)) {
		dma_free_coherent(mlx5_core_dma_dev(dev), PAGE_SIZE,
				  db->u.pgdir->db_page, db->u.pgdir->db_dma);
		list_del(&db->u.pgdir->list);
		bitmap_free(db->u.pgdir->bitmap);
		kfree(db->u.pgdir);
	}

	mutex_unlock(&dev->priv.pgdir_mutex);
}
EXPORT_SYMBOL_GPL(mlx5_db_free);

void mlx5_fill_page_frag_array_perm(struct mlx5_frag_buf *buf, __be64 *pas, u8 perm)
{
	int i;

	WARN_ON(perm & 0xfc);
	for (i = 0; i < buf->npages; i++)
		pas[i] = cpu_to_be64(buf->frags[i].map | perm);
}
EXPORT_SYMBOL_GPL(mlx5_fill_page_frag_array_perm);

void mlx5_fill_page_frag_array(struct mlx5_frag_buf *buf, __be64 *pas)
{
	mlx5_fill_page_frag_array_perm(buf, pas, 0);
}
EXPORT_SYMBOL_GPL(mlx5_fill_page_frag_array);
