/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef __MMU_H
#define __MMU_H

struct psb_mmu_driver {
	/* protects driver- and pd structures. Always take in read mode
	 * before taking the page table spinlock.
	 */
	struct rw_semaphore sem;

	/* protects page tables, directory tables and pt tables.
	 * and pt structures.
	 */
	spinlock_t lock;

	atomic_t needs_tlbflush;
	atomic_t *msvdx_mmu_invaldc;
	struct psb_mmu_pd *default_pd;
	uint32_t bif_ctrl;
	int has_clflush;
	int clflush_add;
	unsigned long clflush_mask;

	struct drm_device *dev;
};

struct psb_mmu_pd;

struct psb_mmu_pt {
	struct psb_mmu_pd *pd;
	uint32_t index;
	uint32_t count;
	struct page *p;
	uint32_t *v;
};

struct psb_mmu_pd {
	struct psb_mmu_driver *driver;
	int hw_context;
	struct psb_mmu_pt **tables;
	struct page *p;
	struct page *dummy_pt;
	struct page *dummy_page;
	uint32_t pd_mask;
	uint32_t invalid_pde;
	uint32_t invalid_pte;
};

extern struct psb_mmu_driver *psb_mmu_driver_init(struct drm_device *dev,
						  int trap_pagefaults,
						  int invalid_type,
						  atomic_t *msvdx_mmu_invaldc);
extern void psb_mmu_driver_takedown(struct psb_mmu_driver *driver);
extern struct psb_mmu_pd *psb_mmu_get_default_pd(struct psb_mmu_driver
						 *driver);
extern struct psb_mmu_pd *psb_mmu_alloc_pd(struct psb_mmu_driver *driver,
					   int trap_pagefaults,
					   int invalid_type);
extern void psb_mmu_free_pagedir(struct psb_mmu_pd *pd);
extern void psb_mmu_flush(struct psb_mmu_driver *driver);
extern void psb_mmu_remove_pfn_sequence(struct psb_mmu_pd *pd,
					unsigned long address,
					uint32_t num_pages);
extern int psb_mmu_insert_pfn_sequence(struct psb_mmu_pd *pd,
				       uint32_t start_pfn,
				       unsigned long address,
				       uint32_t num_pages, int type);
extern void psb_mmu_set_pd_context(struct psb_mmu_pd *pd, int hw_context);
extern int psb_mmu_insert_pages(struct psb_mmu_pd *pd, struct page **pages,
				unsigned long address, uint32_t num_pages,
				uint32_t desired_tile_stride,
				uint32_t hw_tile_stride, int type);
extern void psb_mmu_remove_pages(struct psb_mmu_pd *pd,
				 unsigned long address, uint32_t num_pages,
				 uint32_t desired_tile_stride,
				 uint32_t hw_tile_stride);

#endif
