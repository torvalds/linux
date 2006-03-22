/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
 *
 * $Id$
 */

#ifndef MTHCA_MEMFREE_H
#define MTHCA_MEMFREE_H

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#define MTHCA_ICM_CHUNK_LEN \
	((256 - sizeof (struct list_head) - 2 * sizeof (int)) /		\
	 (sizeof (struct scatterlist)))

enum {
	MTHCA_ICM_PAGE_SHIFT	= 12,
	MTHCA_ICM_PAGE_SIZE	= 1 << MTHCA_ICM_PAGE_SHIFT,
	MTHCA_DB_REC_PER_PAGE	= MTHCA_ICM_PAGE_SIZE / 8
};

struct mthca_icm_chunk {
	struct list_head   list;
	int                npages;
	int                nsg;
	struct scatterlist mem[MTHCA_ICM_CHUNK_LEN];
};

struct mthca_icm {
	struct list_head chunk_list;
	int              refcount;
};

struct mthca_icm_table {
	u64               virt;
	int               num_icm;
	int               num_obj;
	int               obj_size;
	int               lowmem;
	struct mutex      mutex;
	struct mthca_icm *icm[0];
};

struct mthca_icm_iter {
	struct mthca_icm       *icm;
	struct mthca_icm_chunk *chunk;
	int                     page_idx;
};

struct mthca_dev;

struct mthca_icm *mthca_alloc_icm(struct mthca_dev *dev, int npages,
				  gfp_t gfp_mask);
void mthca_free_icm(struct mthca_dev *dev, struct mthca_icm *icm);

struct mthca_icm_table *mthca_alloc_icm_table(struct mthca_dev *dev,
					      u64 virt, int obj_size,
					      int nobj, int reserved,
					      int use_lowmem);
void mthca_free_icm_table(struct mthca_dev *dev, struct mthca_icm_table *table);
int mthca_table_get(struct mthca_dev *dev, struct mthca_icm_table *table, int obj);
void mthca_table_put(struct mthca_dev *dev, struct mthca_icm_table *table, int obj);
void *mthca_table_find(struct mthca_icm_table *table, int obj);
int mthca_table_get_range(struct mthca_dev *dev, struct mthca_icm_table *table,
			  int start, int end);
void mthca_table_put_range(struct mthca_dev *dev, struct mthca_icm_table *table,
			   int start, int end);

static inline void mthca_icm_first(struct mthca_icm *icm,
				   struct mthca_icm_iter *iter)
{
	iter->icm      = icm;
	iter->chunk    = list_empty(&icm->chunk_list) ?
		NULL : list_entry(icm->chunk_list.next,
				  struct mthca_icm_chunk, list);
	iter->page_idx = 0;
}

static inline int mthca_icm_last(struct mthca_icm_iter *iter)
{
	return !iter->chunk;
}

static inline void mthca_icm_next(struct mthca_icm_iter *iter)
{
	if (++iter->page_idx >= iter->chunk->nsg) {
		if (iter->chunk->list.next == &iter->icm->chunk_list) {
			iter->chunk = NULL;
			return;
		}

		iter->chunk = list_entry(iter->chunk->list.next,
					 struct mthca_icm_chunk, list);
		iter->page_idx = 0;
	}
}

static inline dma_addr_t mthca_icm_addr(struct mthca_icm_iter *iter)
{
	return sg_dma_address(&iter->chunk->mem[iter->page_idx]);
}

static inline unsigned long mthca_icm_size(struct mthca_icm_iter *iter)
{
	return sg_dma_len(&iter->chunk->mem[iter->page_idx]);
}

struct mthca_db_page {
	DECLARE_BITMAP(used, MTHCA_DB_REC_PER_PAGE);
	__be64    *db_rec;
	dma_addr_t mapping;
};

struct mthca_db_table {
	int 	       	      npages;
	int 	       	      max_group1;
	int 	       	      min_group2;
	struct mthca_db_page *page;
	struct mutex          mutex;
};

enum mthca_db_type {
	MTHCA_DB_TYPE_INVALID   = 0x0,
	MTHCA_DB_TYPE_CQ_SET_CI = 0x1,
	MTHCA_DB_TYPE_CQ_ARM    = 0x2,
	MTHCA_DB_TYPE_SQ        = 0x3,
	MTHCA_DB_TYPE_RQ        = 0x4,
	MTHCA_DB_TYPE_SRQ       = 0x5,
	MTHCA_DB_TYPE_GROUP_SEP = 0x7
};

struct mthca_user_db_table;
struct mthca_uar;

int mthca_map_user_db(struct mthca_dev *dev, struct mthca_uar *uar,
		      struct mthca_user_db_table *db_tab, int index, u64 uaddr);
void mthca_unmap_user_db(struct mthca_dev *dev, struct mthca_uar *uar,
			 struct mthca_user_db_table *db_tab, int index);
struct mthca_user_db_table *mthca_init_user_db_tab(struct mthca_dev *dev);
void mthca_cleanup_user_db_tab(struct mthca_dev *dev, struct mthca_uar *uar,
			       struct mthca_user_db_table *db_tab);

int mthca_init_db_tab(struct mthca_dev *dev);
void mthca_cleanup_db_tab(struct mthca_dev *dev);
int mthca_alloc_db(struct mthca_dev *dev, enum mthca_db_type type,
		   u32 qn, __be32 **db);
void mthca_free_db(struct mthca_dev *dev, int type, int db_index);

#endif /* MTHCA_MEMFREE_H */
