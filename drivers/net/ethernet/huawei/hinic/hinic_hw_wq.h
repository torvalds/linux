/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_WQ_H
#define HINIC_HW_WQ_H

#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/atomic.h>

#include "hinic_hw_if.h"
#include "hinic_hw_wqe.h"

struct hinic_free_block {
	int     page_idx;
	int     block_idx;
};

struct hinic_wq {
	struct hinic_hwif       *hwif;

	int             page_idx;
	int             block_idx;

	u16             wqebb_size;
	u16             wq_page_size;
	u16             q_depth;
	u16             max_wqe_size;
	u16             num_wqebbs_per_page;
	u16		wqebbs_per_page_shift;
	u16		wqebb_size_shift;
	/* The addresses are 64 bit in the HW */
	u64             block_paddr;
	void            **shadow_block_vaddr;
	u64             *block_vaddr;

	int             num_q_pages;
	u8              *shadow_wqe;
	u16             *shadow_idx;

	atomic_t        cons_idx;
	atomic_t        prod_idx;
	atomic_t        delta;
	u16             mask;
};

struct hinic_wqs {
	struct hinic_hwif       *hwif;
	int                     num_pages;

	/* The addresses are 64 bit in the HW */
	u64                     *page_paddr;
	u64                     **page_vaddr;
	void                    ***shadow_page_vaddr;

	struct hinic_free_block *free_blocks;
	int                     alloc_blk_pos;
	int                     return_blk_pos;
	int                     num_free_blks;

	/* Lock for getting a free block from the WQ set */
	struct semaphore        alloc_blocks_lock;
};

struct hinic_cmdq_pages {
	/* The addresses are 64 bit in the HW */
	u64                     page_paddr;
	u64                     *page_vaddr;
	void                    **shadow_page_vaddr;

	struct hinic_hwif       *hwif;
};

int hinic_wqs_cmdq_alloc(struct hinic_cmdq_pages *cmdq_pages,
			 struct hinic_wq *wq, struct hinic_hwif *hwif,
			 int cmdq_blocks, u16 wqebb_size, u16 wq_page_size,
			 u16 q_depth, u16 max_wqe_size);

void hinic_wqs_cmdq_free(struct hinic_cmdq_pages *cmdq_pages,
			 struct hinic_wq *wq, int cmdq_blocks);

int hinic_wqs_alloc(struct hinic_wqs *wqs, int num_wqs,
		    struct hinic_hwif *hwif);

void hinic_wqs_free(struct hinic_wqs *wqs);

int hinic_wq_allocate(struct hinic_wqs *wqs, struct hinic_wq *wq,
		      u16 wqebb_size, u16 wq_page_size, u16 q_depth,
		      u16 max_wqe_size);

void hinic_wq_free(struct hinic_wqs *wqs, struct hinic_wq *wq);

struct hinic_hw_wqe *hinic_get_wqe(struct hinic_wq *wq, unsigned int wqe_size,
				   u16 *prod_idx);

void hinic_return_wqe(struct hinic_wq *wq, unsigned int wqe_size);

void hinic_put_wqe(struct hinic_wq *wq, unsigned int wqe_size);

struct hinic_hw_wqe *hinic_read_wqe(struct hinic_wq *wq, unsigned int wqe_size,
				    u16 *cons_idx);

struct hinic_hw_wqe *hinic_read_wqe_direct(struct hinic_wq *wq, u16 cons_idx);

void hinic_write_wqe(struct hinic_wq *wq, struct hinic_hw_wqe *wqe,
		     unsigned int wqe_size);

#endif
