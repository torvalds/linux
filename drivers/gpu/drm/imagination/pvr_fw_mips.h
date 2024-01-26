/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FW_MIPS_H
#define PVR_FW_MIPS_H

#include "pvr_rogue_mips.h"

#include <asm/page.h>
#include <linux/types.h>

/* Forward declaration from pvr_gem.h. */
struct pvr_gem_object;

#define PVR_MIPS_PT_PAGE_COUNT ((ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES * ROGUE_MIPSFW_PAGE_SIZE_4K) \
				>> PAGE_SHIFT)
/**
 * struct pvr_fw_mips_data - MIPS-specific data
 */
struct pvr_fw_mips_data {
	/**
	 * @pt_pages: Pages containing MIPS pagetable.
	 */
	struct page *pt_pages[PVR_MIPS_PT_PAGE_COUNT];

	/** @pt: Pointer to CPU mapping of MIPS pagetable. */
	u32 *pt;

	/** @pt_dma_addr: DMA mappings of MIPS pagetable. */
	dma_addr_t pt_dma_addr[PVR_MIPS_PT_PAGE_COUNT];

	/** @boot_code_dma_addr: DMA address of MIPS boot code. */
	dma_addr_t boot_code_dma_addr;

	/** @boot_data_dma_addr: DMA address of MIPS boot data. */
	dma_addr_t boot_data_dma_addr;

	/** @exception_code_dma_addr: DMA address of MIPS exception code. */
	dma_addr_t exception_code_dma_addr;

	/** @cache_policy: Cache policy for this processor. */
	u32 cache_policy;

	/** @pfn_mask: PFN mask for MIPS pagetable. */
	u32 pfn_mask;
};

#endif /* PVR_FW_MIPS_H */
