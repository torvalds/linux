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
#include <linux/nodemask.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/mlx5/driver.h>

#include "mlx5_core.h"

#define MLX5_FRAG_BUF_POOL_MIN_BLOCK_SHIFT	MLX5_ADAPTER_PAGE_SHIFT
#define MLX5_FRAG_BUF_POOLS_NUM \
	(PAGE_SHIFT - MLX5_FRAG_BUF_POOL_MIN_BLOCK_SHIFT + 1)

struct mlx5_db_pgdir {
	struct list_head	list;
	unsigned long	       *bitmap;
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

struct mlx5_dma_pool {
	/* Protects page_list and per-page allocation bitmaps. */
	struct mutex lock;
	struct list_head page_list;
	struct mlx5_core_dev *dev;
	int node;
	u8 block_shift;
};

struct mlx5_dma_pool_page {
	struct mlx5_dma_pool *pool;
	struct list_head pool_link;
	unsigned long *bitmap;
	void *buf;
	dma_addr_t dma;
};

struct mlx5_frag_buf_node_pools {
	struct mlx5_dma_pool *pools[MLX5_FRAG_BUF_POOLS_NUM];
};

struct mlx5_dma_pool_stats {
	int node;
	size_t block_size;
	size_t used_blocks;
	size_t allocated_blocks;
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

static void mlx5_dma_pool_destroy(struct mlx5_dma_pool *pool)
{
	mutex_destroy(&pool->lock);
	kfree(pool);
}

static struct mlx5_dma_pool *mlx5_dma_pool_create(struct mlx5_core_dev *dev,
						  int node, u8 block_shift)
{
	struct mlx5_dma_pool *pool;

	pool = kzalloc_obj(*pool);
	if (!pool)
		return NULL;

	INIT_LIST_HEAD(&pool->page_list);
	mutex_init(&pool->lock);
	pool->dev = dev;
	pool->node = node;
	pool->block_shift = block_shift;
	return pool;
}

static struct mlx5_dma_pool_page *
mlx5_dma_pool_page_alloc(struct mlx5_dma_pool *pool)
{
	int blocks_per_page = BIT(PAGE_SHIFT - pool->block_shift);
	struct mlx5_dma_pool_page *page;

	page = kzalloc_obj(*page);
	if (!page)
		goto err_out;

	page->pool = pool;
	page->bitmap = bitmap_zalloc(blocks_per_page, GFP_KERNEL);
	if (!page->bitmap)
		goto err_free_page;

	bitmap_fill(page->bitmap, blocks_per_page);
	page->buf = mlx5_dma_zalloc_coherent_node(pool->dev, PAGE_SIZE,
						  &page->dma, pool->node);
	if (!page->buf)
		goto err_free_bitmap;

	return page;

err_free_bitmap:
	bitmap_free(page->bitmap);
err_free_page:
	kfree(page);
err_out:
	return NULL;
}

static void mlx5_dma_pool_page_free(struct mlx5_core_dev *dev,
				    struct mlx5_dma_pool_page *page)
{
	dma_free_coherent(mlx5_core_dma_dev(dev), PAGE_SIZE, page->buf,
			  page->dma);
	bitmap_free(page->bitmap);
	kfree(page);
}

static int mlx5_dma_pool_alloc_from_page(struct mlx5_dma_pool *pool,
					 struct mlx5_dma_pool_page *page,
					 unsigned long *idx_out)
{
	int blocks_per_page = BIT(PAGE_SHIFT - pool->block_shift);

	*idx_out = find_first_bit(page->bitmap, blocks_per_page);
	if (*idx_out >= blocks_per_page)
		return -ENOMEM;

	__clear_bit(*idx_out, page->bitmap);

	if (bitmap_empty(page->bitmap, blocks_per_page))
		list_move_tail(&page->pool_link, &pool->page_list);

	return 0;
}

static struct mlx5_dma_pool_page *
mlx5_dma_pool_alloc(struct mlx5_dma_pool *pool, unsigned long *idx_out)
{
	struct mlx5_dma_pool_page *page;

	mutex_lock(&pool->lock);

	page = list_first_entry_or_null(&pool->page_list,
					struct mlx5_dma_pool_page, pool_link);
	if (page && !mlx5_dma_pool_alloc_from_page(pool, page, idx_out))
		goto unlock; /* successfully allocated from existing page */

	page = mlx5_dma_pool_page_alloc(pool);
	if (!page)
		goto unlock;

	list_add(&page->pool_link, &pool->page_list);
	mlx5_dma_pool_alloc_from_page(pool, page, idx_out);

unlock:
	mutex_unlock(&pool->lock);
	return page;
}

static void mlx5_dma_pool_free(struct mlx5_dma_pool *pool,
			       struct mlx5_dma_pool_page *page,
			       unsigned long idx)
{
	int blocks_per_page = BIT(PAGE_SHIFT - pool->block_shift);
	bool was_full;

	mutex_lock(&pool->lock);
	was_full = bitmap_empty(page->bitmap, blocks_per_page);
	__set_bit(idx, page->bitmap);

	if (bitmap_full(page->bitmap, blocks_per_page)) {
		list_del(&page->pool_link);
		mlx5_dma_pool_page_free(pool->dev, page);
	} else {
		memset((u8 *)page->buf + (idx << pool->block_shift), 0,
		       BIT(pool->block_shift));
		if (was_full)
			list_move(&page->pool_link, &pool->page_list);
	}
	mutex_unlock(&pool->lock);
}

static void mlx5_dma_pool_debugfs_get_stats(struct mlx5_dma_pool *pool,
					    struct mlx5_dma_pool_stats *stats)
{
	int blocks_per_page = BIT(PAGE_SHIFT - pool->block_shift);
	struct mlx5_dma_pool_page *page;
	size_t free_blocks = 0;
	size_t pages = 0;

	mutex_lock(&pool->lock);
	list_for_each_entry(page, &pool->page_list, pool_link) {
		pages++;
		free_blocks += bitmap_weight(page->bitmap, blocks_per_page);
	}
	mutex_unlock(&pool->lock);

	stats->node = pool->node;
	stats->block_size = BIT(pool->block_shift);
	stats->allocated_blocks = pages * blocks_per_page;
	stats->used_blocks = stats->allocated_blocks - free_blocks;
}

static void mlx5_dma_pool_debugfs_stats_print(struct seq_file *file,
					      struct mlx5_dma_pool *pool)
{
	struct mlx5_dma_pool_stats stats = {};

	mlx5_dma_pool_debugfs_get_stats(pool, &stats);
	seq_printf(file, "%4d       %5zu      %7zu           %7zu\n",
		   stats.node, stats.block_size, stats.used_blocks,
		   stats.allocated_blocks);
}

static void mlx5_dma_pools_debugfs_print_header(struct seq_file *file)
{
	seq_puts(file, "node  block_size  used_blocks  allocated_blocks\n");
}

static void
mlx5_frag_buf_node_pools_destroy(struct mlx5_frag_buf_node_pools *node_pools)
{
	for (int i = 0; i < MLX5_FRAG_BUF_POOLS_NUM; i++)
		if (node_pools->pools[i])
			mlx5_dma_pool_destroy(node_pools->pools[i]);
	kfree(node_pools);
}

static struct mlx5_frag_buf_node_pools *
mlx5_frag_buf_node_pools_create(struct mlx5_core_dev *dev, int node)
{
	struct mlx5_frag_buf_node_pools *node_pools;

	node_pools = kzalloc_obj(*node_pools);
	if (!node_pools)
		return NULL;

	for (int i = 0; i < MLX5_FRAG_BUF_POOLS_NUM; i++) {
		u8 block_shift = MLX5_FRAG_BUF_POOL_MIN_BLOCK_SHIFT + i;

		node_pools->pools[i] = mlx5_dma_pool_create(dev, node,
							    block_shift);
		if (!node_pools->pools[i]) {
			mlx5_frag_buf_node_pools_destroy(node_pools);
			return NULL;
		}
	}

	return node_pools;
}

static int
mlx5_frag_buf_dma_pools_debugfs_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	int node;

	mlx5_dma_pools_debugfs_print_header(file);

	if (!dev->priv.frag_buf_node_pools)
		return 0;

	for_each_node_state(node, N_POSSIBLE) {
		struct mlx5_frag_buf_node_pools *node_pools;

		node_pools = dev->priv.frag_buf_node_pools[node];
		if (!node_pools)
			continue;

		for (int i = 0; i < MLX5_FRAG_BUF_POOLS_NUM; i++) {
			struct mlx5_dma_pool *pool = node_pools->pools[i];

			if (!pool)
				continue;

			mlx5_dma_pool_debugfs_stats_print(file, pool);
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mlx5_frag_buf_dma_pools_debugfs);

void mlx5_frag_buf_pools_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	int node;

	debugfs_remove(priv->dbg.frag_buf_dma_pools_debugfs);
	priv->dbg.frag_buf_dma_pools_debugfs = NULL;

	for_each_node_state(node, N_POSSIBLE) {
		struct mlx5_frag_buf_node_pools *node_pools;

		node_pools = priv->frag_buf_node_pools[node];
		if (!node_pools)
			continue;
		mlx5_frag_buf_node_pools_destroy(node_pools);
	}

	kfree(priv->frag_buf_node_pools);
	priv->frag_buf_node_pools = NULL;
}

int mlx5_frag_buf_pools_init(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	int node;

	priv->frag_buf_node_pools = kzalloc_objs(*priv->frag_buf_node_pools,
						 nr_node_ids);
	if (!priv->frag_buf_node_pools)
		return -ENOMEM;

	for_each_node_state(node, N_POSSIBLE) {
		struct mlx5_frag_buf_node_pools *node_pools;

		node_pools = mlx5_frag_buf_node_pools_create(dev, node);
		if (!node_pools) {
			mlx5_frag_buf_pools_cleanup(dev);
			return -ENOMEM;
		}
		priv->frag_buf_node_pools[node] = node_pools;
	}

	priv->dbg.frag_buf_dma_pools_debugfs =
		debugfs_create_file("frag_buf_dma_pools", 0444,
				    priv->dbg.dbg_root, dev,
				    &mlx5_frag_buf_dma_pools_debugfs_fops);

	return 0;
}

int mlx5_frag_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			     struct mlx5_frag_buf *buf, int node)
{
	struct mlx5_dma_pool *pool;
	int pool_idx;

	node = node == NUMA_NO_NODE ? numa_mem_id() : node;

	buf->size = size;
	buf->npages = DIV_ROUND_UP(size, PAGE_SIZE);
	buf->page_shift = clamp_t(int, order_base_2(size),
				  MLX5_FRAG_BUF_POOL_MIN_BLOCK_SHIFT,
				  PAGE_SHIFT);
	buf->frags = kcalloc_node(buf->npages, sizeof(*buf->frags),
				  GFP_KERNEL, node);
	if (!buf->frags)
		return -ENOMEM;

	pool_idx = buf->page_shift - MLX5_FRAG_BUF_POOL_MIN_BLOCK_SHIFT;
	pool = dev->priv.frag_buf_node_pools[node]->pools[pool_idx];
	for (int i = 0; i < buf->npages; i++) {
		struct mlx5_buf_list *frag = &buf->frags[i];
		struct mlx5_dma_pool_page *page;
		unsigned long idx;

		page = mlx5_dma_pool_alloc(pool, &idx);
		if (!page) {
			mlx5_frag_buf_free(dev, buf);
			return -ENOMEM;
		}
		frag->buf = (u8 *)page->buf + (idx << pool->block_shift);
		frag->map = page->dma + (idx << pool->block_shift);
		frag->frag_page = page;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_frag_buf_alloc_node);

void mlx5_frag_buf_free(struct mlx5_core_dev *dev, struct mlx5_frag_buf *buf)
{
	for (int i = 0; i < buf->npages; i++) {
		struct mlx5_buf_list *frag = &buf->frags[i];
		struct mlx5_dma_pool_page *page;
		struct mlx5_dma_pool *pool;
		unsigned long idx;

		if (!frag->buf)
			continue;

		page = frag->frag_page;
		pool = page->pool;
		idx = (frag->map - page->dma) >> pool->block_shift;
		mlx5_dma_pool_free(pool, page, idx);
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
