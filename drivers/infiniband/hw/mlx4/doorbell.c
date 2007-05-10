/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"

struct mlx4_ib_db_pgdir {
	struct list_head	list;
	DECLARE_BITMAP(order0, MLX4_IB_DB_PER_PAGE);
	DECLARE_BITMAP(order1, MLX4_IB_DB_PER_PAGE / 2);
	unsigned long	       *bits[2];
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

static struct mlx4_ib_db_pgdir *mlx4_ib_alloc_db_pgdir(struct mlx4_ib_dev *dev)
{
	struct mlx4_ib_db_pgdir *pgdir;

	pgdir = kzalloc(sizeof *pgdir, GFP_KERNEL);
	if (!pgdir)
		return NULL;

	bitmap_fill(pgdir->order1, MLX4_IB_DB_PER_PAGE / 2);
	pgdir->bits[0] = pgdir->order0;
	pgdir->bits[1] = pgdir->order1;
	pgdir->db_page = dma_alloc_coherent(dev->ib_dev.dma_device,
					    PAGE_SIZE, &pgdir->db_dma,
					    GFP_KERNEL);
	if (!pgdir->db_page) {
		kfree(pgdir);
		return NULL;
	}

	return pgdir;
}

static int mlx4_ib_alloc_db_from_pgdir(struct mlx4_ib_db_pgdir *pgdir,
				       struct mlx4_ib_db *db, int order)
{
	int o;
	int i;

	for (o = order; o <= 1; ++o) {
		i = find_first_bit(pgdir->bits[o], MLX4_IB_DB_PER_PAGE >> o);
		if (i < MLX4_IB_DB_PER_PAGE >> o)
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

int mlx4_ib_db_alloc(struct mlx4_ib_dev *dev, struct mlx4_ib_db *db, int order)
{
	struct mlx4_ib_db_pgdir *pgdir;
	int ret = 0;

	mutex_lock(&dev->pgdir_mutex);

	list_for_each_entry(pgdir, &dev->pgdir_list, list)
		if (!mlx4_ib_alloc_db_from_pgdir(pgdir, db, order))
			goto out;

	pgdir = mlx4_ib_alloc_db_pgdir(dev);
	if (!pgdir) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&pgdir->list, &dev->pgdir_list);

	/* This should never fail -- we just allocated an empty page: */
	WARN_ON(mlx4_ib_alloc_db_from_pgdir(pgdir, db, order));

out:
	mutex_unlock(&dev->pgdir_mutex);

	return ret;
}

void mlx4_ib_db_free(struct mlx4_ib_dev *dev, struct mlx4_ib_db *db)
{
	int o;
	int i;

	mutex_lock(&dev->pgdir_mutex);

	o = db->order;
	i = db->index;

	if (db->order == 0 && test_bit(i ^ 1, db->u.pgdir->order0)) {
		clear_bit(i ^ 1, db->u.pgdir->order0);
		++o;
	}

	i >>= o;
	set_bit(i, db->u.pgdir->bits[o]);

	if (bitmap_full(db->u.pgdir->order1, MLX4_IB_DB_PER_PAGE / 2)) {
		dma_free_coherent(dev->ib_dev.dma_device, PAGE_SIZE,
				  db->u.pgdir->db_page, db->u.pgdir->db_dma);
		list_del(&db->u.pgdir->list);
		kfree(db->u.pgdir);
	}

	mutex_unlock(&dev->pgdir_mutex);
}

struct mlx4_ib_user_db_page {
	struct list_head	list;
	struct ib_umem	       *umem;
	unsigned long		user_virt;
	int			refcnt;
};

int mlx4_ib_db_map_user(struct mlx4_ib_ucontext *context, unsigned long virt,
			struct mlx4_ib_db *db)
{
	struct mlx4_ib_user_db_page *page;
	struct ib_umem_chunk *chunk;
	int err = 0;

	mutex_lock(&context->db_page_mutex);

	list_for_each_entry(page, &context->db_page_list, list)
		if (page->user_virt == (virt & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof *page, GFP_KERNEL);
	if (!page) {
		err = -ENOMEM;
		goto out;
	}

	page->user_virt = (virt & PAGE_MASK);
	page->refcnt    = 0;
	page->umem      = ib_umem_get(&context->ibucontext, virt & PAGE_MASK,
				      PAGE_SIZE, 0);
	if (IS_ERR(page->umem)) {
		err = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &context->db_page_list);

found:
	chunk = list_entry(page->umem->chunk_list.next, struct ib_umem_chunk, list);
	db->dma		= sg_dma_address(chunk->page_list) + (virt & ~PAGE_MASK);
	db->u.user_page = page;
	++page->refcnt;

out:
	mutex_unlock(&context->db_page_mutex);

	return err;
}

void mlx4_ib_db_unmap_user(struct mlx4_ib_ucontext *context, struct mlx4_ib_db *db)
{
	mutex_lock(&context->db_page_mutex);

	if (!--db->u.user_page->refcnt) {
		list_del(&db->u.user_page->list);
		ib_umem_release(db->u.user_page->umem);
		kfree(db->u.user_page);
	}

	mutex_unlock(&context->db_page_mutex);
}
