/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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
#ifndef	SH_MMU_H_
#define	SH_MMU_H_


#include <sh_css.h>

#include "mmu/isp_mmu.h"


/*
 * include SH header file here
 */

/*
 * set page directory base address (physical address).
 *
 * must be provided.
 */
static int sh_set_pd_base(struct isp_mmu *mmu,
		unsigned int phys)
{
	sh_css_mmu_set_page_table_base_address((void *)phys);
	return 0;
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
	sh_css_mmu_invalidate_cache();
}

static struct isp_mmu_driver sh_mmu_driver = {
	.name = "Silicon Hive ISP3000 MMU",
	.pte_valid_mask = 0x1,
	.set_pd_base = sh_set_pd_base,
	.tlb_flush_all = sh_tlb_flush,
};

#define	ISP_VM_START	0x0
#define	ISP_VM_SIZE	(1 << 30)	/* 1G address space */
#define	ISP_PTR_NULL	NULL

#endif /* SH_MMU_H_ */

