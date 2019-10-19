/*
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
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

#ifndef MLX4_ICM_H
#define MLX4_ICM_H

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#define MLX4_ICM_CHUNK_LEN						\
	((256 - sizeof(struct list_head) - 2 * sizeof(int)) /		\
	 (sizeof(struct scatterlist)))

enum {
	MLX4_ICM_PAGE_SHIFT	= 12,
	MLX4_ICM_PAGE_SIZE	= 1 << MLX4_ICM_PAGE_SHIFT,
};

struct mlx4_icm_buf {
	void			*addr;
	size_t			size;
	dma_addr_t		dma_addr;
};

struct mlx4_icm_chunk {
	struct list_head	list;
	int			npages;
	int			nsg;
	bool			coherent;
	union {
		struct scatterlist	sg[MLX4_ICM_CHUNK_LEN];
		struct mlx4_icm_buf	buf[MLX4_ICM_CHUNK_LEN];
	};
};

struct mlx4_icm {
	struct list_head	chunk_list;
	int			refcount;
};

struct mlx4_icm_iter {
	struct mlx4_icm	       *icm;
	struct mlx4_icm_chunk  *chunk;
	int			page_idx;
};

struct mlx4_dev;

struct mlx4_icm *mlx4_alloc_icm(struct mlx4_dev *dev, int npages,
				gfp_t gfp_mask, int coherent);
void mlx4_free_icm(struct mlx4_dev *dev, struct mlx4_icm *icm, int coherent);

int mlx4_table_get(struct mlx4_dev *dev, struct mlx4_icm_table *table, u32 obj);
void mlx4_table_put(struct mlx4_dev *dev, struct mlx4_icm_table *table, u32 obj);
int mlx4_table_get_range(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			 u32 start, u32 end);
void mlx4_table_put_range(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			  u32 start, u32 end);
int mlx4_init_icm_table(struct mlx4_dev *dev, struct mlx4_icm_table *table,
			u64 virt, int obj_size,	u32 nobj, int reserved,
			int use_lowmem, int use_coherent);
void mlx4_cleanup_icm_table(struct mlx4_dev *dev, struct mlx4_icm_table *table);
void *mlx4_table_find(struct mlx4_icm_table *table, u32 obj, dma_addr_t *dma_handle);

static inline void mlx4_icm_first(struct mlx4_icm *icm,
				  struct mlx4_icm_iter *iter)
{
	iter->icm      = icm;
	iter->chunk    = list_empty(&icm->chunk_list) ?
		NULL : list_entry(icm->chunk_list.next,
				  struct mlx4_icm_chunk, list);
	iter->page_idx = 0;
}

static inline int mlx4_icm_last(struct mlx4_icm_iter *iter)
{
	return !iter->chunk;
}

static inline void mlx4_icm_next(struct mlx4_icm_iter *iter)
{
	if (++iter->page_idx >= iter->chunk->nsg) {
		if (iter->chunk->list.next == &iter->icm->chunk_list) {
			iter->chunk = NULL;
			return;
		}

		iter->chunk = list_entry(iter->chunk->list.next,
					 struct mlx4_icm_chunk, list);
		iter->page_idx = 0;
	}
}

static inline dma_addr_t mlx4_icm_addr(struct mlx4_icm_iter *iter)
{
	if (iter->chunk->coherent)
		return iter->chunk->buf[iter->page_idx].dma_addr;
	else
		return sg_dma_address(&iter->chunk->sg[iter->page_idx]);
}

static inline unsigned long mlx4_icm_size(struct mlx4_icm_iter *iter)
{
	if (iter->chunk->coherent)
		return iter->chunk->buf[iter->page_idx].size;
	else
		return sg_dma_len(&iter->chunk->sg[iter->page_idx]);
}

int mlx4_MAP_ICM_AUX(struct mlx4_dev *dev, struct mlx4_icm *icm);
int mlx4_UNMAP_ICM_AUX(struct mlx4_dev *dev);

#endif /* MLX4_ICM_H */
