/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DC_MMU_H_
#define _VS_DC_MMU_H_

#include "vs_type.h"

#define DC_INFINITE ((u32)(~0U))

#define DC_ENTRY_TYPE(x) (x & 0xF0)
#define DC_SINGLE_PAGE_NODE_INITIALIZE (~((1U << 8) - 1))

#define DC_INVALID_PHYSICAL_ADDRESS ~0ULL
#define DC_INVALID_ADDRESS ~0U

/* 1k mode */
#define MMU_MTLB_SHIFT			 24
#define MMU_STLB_4K_SHIFT		 12

#define MMU_MTLB_BITS			 (32 - MMU_MTLB_SHIFT)
#define MMU_PAGE_4K_BITS		 MMU_STLB_4K_SHIFT
#define MMU_STLB_4K_BITS		 (32 - MMU_MTLB_BITS - MMU_PAGE_4K_BITS)

#define MMU_MTLB_ENTRY_NUM		 (1 << MMU_MTLB_BITS)
#define MMU_MTLB_SIZE			 (MMU_MTLB_ENTRY_NUM << 2)
#define MMU_STLB_4K_ENTRY_NUM	 (1 << MMU_STLB_4K_BITS)
#define MMU_STLB_4K_SIZE		 (MMU_STLB_4K_ENTRY_NUM << 2)
#define MMU_PAGE_4K_SIZE		 (1 << MMU_STLB_4K_SHIFT)

#define MMU_MTLB_MASK			 (~((1U << MMU_MTLB_SHIFT)-1))
#define MMU_STLB_4K_MASK		 ((~0U << MMU_STLB_4K_SHIFT) ^ MMU_MTLB_MASK)
#define MMU_PAGE_4K_MASK		 (MMU_PAGE_4K_SIZE - 1)

/* page offset definitions. */
#define MMU_OFFSET_4K_BITS		 (32 - MMU_MTLB_BITS - MMU_STLB_4K_BITS)
#define MMU_OFFSET_4K_MASK		 ((1U << MMU_OFFSET_4K_BITS) - 1)

#define MMU_MTLB_PRESENT		 0x00000001
#define MMU_MTLB_EXCEPTION		 0x00000002
#define MMU_MTLB_SECURITY		 0x00000010
#define MMU_MTLB_4K_PAGE		 0x00000000

typedef enum _dc_mmu_type {
	DC_MMU_USED   = (0 << 4),
	DC_MMU_SINGLE = (1 << 4),
	DC_MMU_FREE   = (2 << 4),
} dc_mmu_type;

typedef enum _dc_mmu_mode {
	MMU_MODE_1K,
	MMU_MODE_4K,
} dc_mmu_mode;

typedef struct _dc_mmu_stlb {
	u32  *logical;
	void *physical;
	u32  size;
	u64  physBase;
	u32  pageCount;
} dc_mmu_stlb, *dc_mmu_stlb_pt;

typedef struct _dc_mmu {
	u32    mtlb_bytes;
	u64    mtlb_physical;
	u32    *mtlb_logical;

	void   *safe_page_logical;
	u64    safe_page_physical;

	u32    dynamic_mapping_start;

	void   *stlbs;

	u64    stlb_physicals[MMU_MTLB_ENTRY_NUM];

	u32    page_table_entries;
	u32    page_table_size;
	u32    heap_list;

	u32    *map_logical;
	bool   free_nodes;

	void   *page_table_mutex;

	dc_mmu_mode mode;

	void   *static_stlb;
} dc_mmu, *dc_mmu_pt;

int dc_mmu_construct(struct device *dev, dc_mmu_pt *mmu);
int dc_mmu_map_memory(dc_mmu_pt mmu, u64 physical, u32 page_count,
			  u32 *address, bool continuous, bool security);
int dc_mmu_unmap_memory(dc_mmu_pt mmu, u32 gpu_address, u32 page_count);

#endif /* _VS_DC_MMU_H_ */
