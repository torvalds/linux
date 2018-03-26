/*
 * Support for Merrifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2012 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#include "type_support.h"
#include "mmu/isp_mmu.h"
#include "memory_access/memory_access.h"
#include "atomisp_compat.h"

#define MERR_VALID_PTE_MASK	0x80000000

/*
 * include SH header file here
 */

static unsigned int sh_phys_to_pte(struct isp_mmu *mmu,
				   phys_addr_t phys)
{
	return phys >> ISP_PAGE_OFFSET;
}

static phys_addr_t sh_pte_to_phys(struct isp_mmu *mmu,
				  unsigned int pte)
{
	unsigned int mask = mmu->driver->pte_valid_mask;
	return (phys_addr_t)((pte & ~mask) << ISP_PAGE_OFFSET);
}

static unsigned int sh_get_pd_base(struct isp_mmu *mmu,
				   phys_addr_t phys)
{
	unsigned int pte = sh_phys_to_pte(mmu, phys);
	return HOST_ADDRESS(pte);
}

/*
 * callback to flush tlb.
 *
 * tlb_flush_range will at least flush TLBs containing
 * address mapping from addr to addr + size.
 *
 * tlb_flush_all will flush all TLBs.
 *
 * tlb_flush_all is must be provided. if tlb_flush_range is
 * not valid, it will set to tlb_flush_all by default.
 */
static void sh_tlb_flush(struct isp_mmu *mmu)
{
	atomisp_css_mmu_invalidate_cache();
}

struct isp_mmu_client sh_mmu_mrfld = {
	.name = "Silicon Hive ISP3000 MMU",
	.pte_valid_mask = MERR_VALID_PTE_MASK,
	.null_pte = ~MERR_VALID_PTE_MASK,
	.get_pd_base = sh_get_pd_base,
	.tlb_flush_all = sh_tlb_flush,
	.phys_to_pte = sh_phys_to_pte,
	.pte_to_phys = sh_pte_to_phys,
};
