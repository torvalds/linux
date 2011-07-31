/*
 * hw_mmu.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * MMU types and API declarations
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _HW_MMU_H
#define _HW_MMU_H

#include <linux/types.h>

/* Bitmasks for interrupt sources */
#define HW_MMU_TRANSLATION_FAULT   0x2
#define HW_MMU_ALL_INTERRUPTS      0x1F

#define HW_MMU_COARSE_PAGE_SIZE 0x400

/* hw_mmu_mixed_size_t:  Enumerated Type used to specify whether to follow
			CPU/TLB Element size */
enum hw_mmu_mixed_size_t {
	HW_MMU_TLBES,
	HW_MMU_CPUES
};

/* hw_mmu_map_attrs_t:  Struct containing MMU mapping attributes */
struct hw_mmu_map_attrs_t {
	enum hw_endianism_t endianism;
	enum hw_element_size_t element_size;
	enum hw_mmu_mixed_size_t mixed_size;
	bool donotlockmpupage;
};

extern hw_status hw_mmu_enable(const void __iomem *base_address);

extern hw_status hw_mmu_disable(const void __iomem *base_address);

extern hw_status hw_mmu_num_locked_set(const void __iomem *base_address,
				       u32 num_locked_entries);

extern hw_status hw_mmu_victim_num_set(const void __iomem *base_address,
				       u32 victim_entry_num);

/* For MMU faults */
extern hw_status hw_mmu_event_ack(const void __iomem *base_address,
				  u32 irq_mask);

extern hw_status hw_mmu_event_disable(const void __iomem *base_address,
				      u32 irq_mask);

extern hw_status hw_mmu_event_enable(const void __iomem *base_address,
				     u32 irq_mask);

extern hw_status hw_mmu_event_status(const void __iomem *base_address,
				     u32 *irq_mask);

extern hw_status hw_mmu_fault_addr_read(const void __iomem *base_address,
					u32 *addr);

/* Set the TT base address */
extern hw_status hw_mmu_ttb_set(const void __iomem *base_address,
				u32 ttb_phys_addr);

extern hw_status hw_mmu_twl_enable(const void __iomem *base_address);

extern hw_status hw_mmu_twl_disable(const void __iomem *base_address);

extern hw_status hw_mmu_tlb_flush(const void __iomem *base_address,
				  u32 virtual_addr, u32 page_sz);

extern hw_status hw_mmu_tlb_add(const void __iomem *base_address,
				u32 physical_addr,
				u32 virtual_addr,
				u32 page_sz,
				u32 entry_num,
				struct hw_mmu_map_attrs_t *map_attrs,
				s8 preserved_bit, s8 valid_bit);

/* For PTEs */
extern hw_status hw_mmu_pte_set(const u32 pg_tbl_va,
				u32 physical_addr,
				u32 virtual_addr,
				u32 page_sz,
				struct hw_mmu_map_attrs_t *map_attrs);

extern hw_status hw_mmu_pte_clear(const u32 pg_tbl_va,
				  u32 virtual_addr, u32 page_size);

void hw_mmu_tlb_flush_all(const void __iomem *base);

static inline u32 hw_mmu_pte_addr_l1(u32 l1_base, u32 va)
{
	u32 pte_addr;
	u32 va31_to20;

	va31_to20 = va >> (20 - 2);	/* Left-shift by 2 here itself */
	va31_to20 &= 0xFFFFFFFCUL;
	pte_addr = l1_base + va31_to20;

	return pte_addr;
}

static inline u32 hw_mmu_pte_addr_l2(u32 l2_base, u32 va)
{
	u32 pte_addr;

	pte_addr = (l2_base & 0xFFFFFC00) | ((va >> 10) & 0x3FC);

	return pte_addr;
}

static inline u32 hw_mmu_pte_coarse_l1(u32 pte_val)
{
	u32 pte_coarse;

	pte_coarse = pte_val & 0xFFFFFC00;

	return pte_coarse;
}

static inline u32 hw_mmu_pte_size_l1(u32 pte_val)
{
	u32 pte_size = 0;

	if ((pte_val & 0x3) == 0x1) {
		/* Points to L2 PT */
		pte_size = HW_MMU_COARSE_PAGE_SIZE;
	}

	if ((pte_val & 0x3) == 0x2) {
		if (pte_val & (1 << 18))
			pte_size = HW_PAGE_SIZE16MB;
		else
			pte_size = HW_PAGE_SIZE1MB;
	}

	return pte_size;
}

static inline u32 hw_mmu_pte_size_l2(u32 pte_val)
{
	u32 pte_size = 0;

	if (pte_val & 0x2)
		pte_size = HW_PAGE_SIZE4KB;
	else if (pte_val & 0x1)
		pte_size = HW_PAGE_SIZE64KB;

	return pte_size;
}

#endif /* _HW_MMU_H */
