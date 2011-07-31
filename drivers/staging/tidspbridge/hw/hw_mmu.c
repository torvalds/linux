/*
 * hw_mmu.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * API definitions to setup MMU TLB and PTE
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

#include <linux/io.h>
#include "MMURegAcM.h"
#include <hw_defs.h>
#include <hw_mmu.h>
#include <linux/types.h>
#include <linux/err.h>

#define MMU_BASE_VAL_MASK	0xFC00
#define MMU_PAGE_MAX	     3
#define MMU_ELEMENTSIZE_MAX      3
#define MMU_ADDR_MASK	    0xFFFFF000
#define MMU_TTB_MASK	     0xFFFFC000
#define MMU_SECTION_ADDR_MASK    0xFFF00000
#define MMU_SSECTION_ADDR_MASK   0xFF000000
#define MMU_PAGE_TABLE_MASK      0xFFFFFC00
#define MMU_LARGE_PAGE_MASK      0xFFFF0000
#define MMU_SMALL_PAGE_MASK      0xFFFFF000

#define MMU_LOAD_TLB	0x00000001
#define MMU_GFLUSH	0x60

/*
 * hw_mmu_page_size_t: Enumerated Type used to specify the MMU Page Size(SLSS)
 */
enum hw_mmu_page_size_t {
	HW_MMU_SECTION,
	HW_MMU_LARGE_PAGE,
	HW_MMU_SMALL_PAGE,
	HW_MMU_SUPERSECTION
};

/*
 * FUNCTION	      : mmu_flush_entry
 *
 * INPUTS:
 *
 *       Identifier      : base_address
 *       Type		: const u32
 *       Description     : Base Address of instance of MMU module
 *
 * RETURNS:
 *
 *       Type		: hw_status
 *       Description     : 0		 -- No errors occured
 *			 RET_BAD_NULL_PARAM     -- A Pointer
 *						Paramater was set to NULL
 *
 * PURPOSE:	      : Flush the TLB entry pointed by the
 *			lock counter register
 *			even if this entry is set protected
 *
 * METHOD:	       : Check the Input parameter and Flush a
 *			 single entry in the TLB.
 */
static hw_status mmu_flush_entry(const void __iomem *base_address);

/*
 * FUNCTION	      : mmu_set_cam_entry
 *
 * INPUTS:
 *
 *       Identifier      : base_address
 *       TypE		: const u32
 *       Description     : Base Address of instance of MMU module
 *
 *       Identifier      : page_sz
 *       TypE		: const u32
 *       Description     : It indicates the page size
 *
 *       Identifier      : preserved_bit
 *       Type		: const u32
 *       Description     : It indicates the TLB entry is preserved entry
 *							or not
 *
 *       Identifier      : valid_bit
 *       Type		: const u32
 *       Description     : It indicates the TLB entry is valid entry or not
 *
 *
 *       Identifier      : virtual_addr_tag
 *       Type	    	: const u32
 *       Description     : virtual Address
 *
 * RETURNS:
 *
 *       Type	    	: hw_status
 *       Description     : 0		 -- No errors occured
 *			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
 *						   was set to NULL
 *			 RET_PARAM_OUT_OF_RANGE -- Input Parameter out
 *						   of Range
 *
 * PURPOSE:	      	: Set MMU_CAM reg
 *
 * METHOD:	       	: Check the Input parameters and set the CAM entry.
 */
static hw_status mmu_set_cam_entry(const void __iomem *base_address,
				   const u32 page_sz,
				   const u32 preserved_bit,
				   const u32 valid_bit,
				   const u32 virtual_addr_tag);

/*
 * FUNCTION	      : mmu_set_ram_entry
 *
 * INPUTS:
 *
 *       Identifier      : base_address
 *       Type	    	: const u32
 *       Description     : Base Address of instance of MMU module
 *
 *       Identifier      : physical_addr
 *       Type	    	: const u32
 *       Description     : Physical Address to which the corresponding
 *			 virtual   Address shouldpoint
 *
 *       Identifier      : endianism
 *       Type	    	: hw_endianism_t
 *       Description     : endianism for the given page
 *
 *       Identifier      : element_size
 *       Type	    	: hw_element_size_t
 *       Description     : The element size ( 8,16, 32 or 64 bit)
 *
 *       Identifier      : mixed_size
 *       Type	    	: hw_mmu_mixed_size_t
 *       Description     : Element Size to follow CPU or TLB
 *
 * RETURNS:
 *
 *       Type	    	: hw_status
 *       Description     : 0		 -- No errors occured
 *			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
 *							was set to NULL
 *			 RET_PARAM_OUT_OF_RANGE -- Input Parameter
 *							out of Range
 *
 * PURPOSE:	      : Set MMU_CAM reg
 *
 * METHOD:	       : Check the Input parameters and set the RAM entry.
 */
static hw_status mmu_set_ram_entry(const void __iomem *base_address,
				   const u32 physical_addr,
				   enum hw_endianism_t endianism,
				   enum hw_element_size_t element_size,
				   enum hw_mmu_mixed_size_t mixed_size);

/* HW FUNCTIONS */

hw_status hw_mmu_enable(const void __iomem *base_address)
{
	hw_status status = 0;

	MMUMMU_CNTLMMU_ENABLE_WRITE32(base_address, HW_SET);

	return status;
}

hw_status hw_mmu_disable(const void __iomem *base_address)
{
	hw_status status = 0;

	MMUMMU_CNTLMMU_ENABLE_WRITE32(base_address, HW_CLEAR);

	return status;
}

hw_status hw_mmu_num_locked_set(const void __iomem *base_address,
				u32 num_locked_entries)
{
	hw_status status = 0;

	MMUMMU_LOCK_BASE_VALUE_WRITE32(base_address, num_locked_entries);

	return status;
}

hw_status hw_mmu_victim_num_set(const void __iomem *base_address,
				u32 victim_entry_num)
{
	hw_status status = 0;

	MMUMMU_LOCK_CURRENT_VICTIM_WRITE32(base_address, victim_entry_num);

	return status;
}

hw_status hw_mmu_event_ack(const void __iomem *base_address, u32 irq_mask)
{
	hw_status status = 0;

	MMUMMU_IRQSTATUS_WRITE_REGISTER32(base_address, irq_mask);

	return status;
}

hw_status hw_mmu_event_disable(const void __iomem *base_address, u32 irq_mask)
{
	hw_status status = 0;
	u32 irq_reg;

	irq_reg = MMUMMU_IRQENABLE_READ_REGISTER32(base_address);

	MMUMMU_IRQENABLE_WRITE_REGISTER32(base_address, irq_reg & ~irq_mask);

	return status;
}

hw_status hw_mmu_event_enable(const void __iomem *base_address, u32 irq_mask)
{
	hw_status status = 0;
	u32 irq_reg;

	irq_reg = MMUMMU_IRQENABLE_READ_REGISTER32(base_address);

	MMUMMU_IRQENABLE_WRITE_REGISTER32(base_address, irq_reg | irq_mask);

	return status;
}

hw_status hw_mmu_event_status(const void __iomem *base_address, u32 *irq_mask)
{
	hw_status status = 0;

	*irq_mask = MMUMMU_IRQSTATUS_READ_REGISTER32(base_address);

	return status;
}

hw_status hw_mmu_fault_addr_read(const void __iomem *base_address, u32 *addr)
{
	hw_status status = 0;

	/* read values from register */
	*addr = MMUMMU_FAULT_AD_READ_REGISTER32(base_address);

	return status;
}

hw_status hw_mmu_ttb_set(const void __iomem *base_address, u32 ttb_phys_addr)
{
	hw_status status = 0;
	u32 load_ttb;

	load_ttb = ttb_phys_addr & ~0x7FUL;
	/* write values to register */
	MMUMMU_TTB_WRITE_REGISTER32(base_address, load_ttb);

	return status;
}

hw_status hw_mmu_twl_enable(const void __iomem *base_address)
{
	hw_status status = 0;

	MMUMMU_CNTLTWL_ENABLE_WRITE32(base_address, HW_SET);

	return status;
}

hw_status hw_mmu_twl_disable(const void __iomem *base_address)
{
	hw_status status = 0;

	MMUMMU_CNTLTWL_ENABLE_WRITE32(base_address, HW_CLEAR);

	return status;
}

hw_status hw_mmu_tlb_flush(const void __iomem *base_address, u32 virtual_addr,
			   u32 page_sz)
{
	hw_status status = 0;
	u32 virtual_addr_tag;
	enum hw_mmu_page_size_t pg_size_bits;

	switch (page_sz) {
	case HW_PAGE_SIZE4KB:
		pg_size_bits = HW_MMU_SMALL_PAGE;
		break;

	case HW_PAGE_SIZE64KB:
		pg_size_bits = HW_MMU_LARGE_PAGE;
		break;

	case HW_PAGE_SIZE1MB:
		pg_size_bits = HW_MMU_SECTION;
		break;

	case HW_PAGE_SIZE16MB:
		pg_size_bits = HW_MMU_SUPERSECTION;
		break;

	default:
		return -EINVAL;
	}

	/* Generate the 20-bit tag from virtual address */
	virtual_addr_tag = ((virtual_addr & MMU_ADDR_MASK) >> 12);

	mmu_set_cam_entry(base_address, pg_size_bits, 0, 0, virtual_addr_tag);

	mmu_flush_entry(base_address);

	return status;
}

hw_status hw_mmu_tlb_add(const void __iomem *base_address,
			 u32 physical_addr,
			 u32 virtual_addr,
			 u32 page_sz,
			 u32 entry_num,
			 struct hw_mmu_map_attrs_t *map_attrs,
			 s8 preserved_bit, s8 valid_bit)
{
	hw_status status = 0;
	u32 lock_reg;
	u32 virtual_addr_tag;
	enum hw_mmu_page_size_t mmu_pg_size;

	/*Check the input Parameters */
	switch (page_sz) {
	case HW_PAGE_SIZE4KB:
		mmu_pg_size = HW_MMU_SMALL_PAGE;
		break;

	case HW_PAGE_SIZE64KB:
		mmu_pg_size = HW_MMU_LARGE_PAGE;
		break;

	case HW_PAGE_SIZE1MB:
		mmu_pg_size = HW_MMU_SECTION;
		break;

	case HW_PAGE_SIZE16MB:
		mmu_pg_size = HW_MMU_SUPERSECTION;
		break;

	default:
		return -EINVAL;
	}

	lock_reg = MMUMMU_LOCK_READ_REGISTER32(base_address);

	/* Generate the 20-bit tag from virtual address */
	virtual_addr_tag = ((virtual_addr & MMU_ADDR_MASK) >> 12);

	/* Write the fields in the CAM Entry Register */
	mmu_set_cam_entry(base_address, mmu_pg_size, preserved_bit, valid_bit,
			  virtual_addr_tag);

	/* Write the different fields of the RAM Entry Register */
	/* endianism of the page,Element Size of the page (8, 16, 32, 64 bit) */
	mmu_set_ram_entry(base_address, physical_addr, map_attrs->endianism,
			  map_attrs->element_size, map_attrs->mixed_size);

	/* Update the MMU Lock Register */
	/* currentVictim between lockedBaseValue and (MMU_Entries_Number - 1) */
	MMUMMU_LOCK_CURRENT_VICTIM_WRITE32(base_address, entry_num);

	/* Enable loading of an entry in TLB by writing 1
	   into LD_TLB_REG register */
	MMUMMU_LD_TLB_WRITE_REGISTER32(base_address, MMU_LOAD_TLB);

	MMUMMU_LOCK_WRITE_REGISTER32(base_address, lock_reg);

	return status;
}

hw_status hw_mmu_pte_set(const u32 pg_tbl_va,
			 u32 physical_addr,
			 u32 virtual_addr,
			 u32 page_sz, struct hw_mmu_map_attrs_t *map_attrs)
{
	hw_status status = 0;
	u32 pte_addr, pte_val;
	s32 num_entries = 1;

	switch (page_sz) {
	case HW_PAGE_SIZE4KB:
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtual_addr &
					      MMU_SMALL_PAGE_MASK);
		pte_val =
		    ((physical_addr & MMU_SMALL_PAGE_MASK) |
		     (map_attrs->endianism << 9) | (map_attrs->
						    element_size << 4) |
		     (map_attrs->mixed_size << 11) | 2);
		break;

	case HW_PAGE_SIZE64KB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtual_addr &
					      MMU_LARGE_PAGE_MASK);
		pte_val =
		    ((physical_addr & MMU_LARGE_PAGE_MASK) |
		     (map_attrs->endianism << 9) | (map_attrs->
						    element_size << 4) |
		     (map_attrs->mixed_size << 11) | 1);
		break;

	case HW_PAGE_SIZE1MB:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtual_addr &
					      MMU_SECTION_ADDR_MASK);
		pte_val =
		    ((((physical_addr & MMU_SECTION_ADDR_MASK) |
		       (map_attrs->endianism << 15) | (map_attrs->
						       element_size << 10) |
		       (map_attrs->mixed_size << 17)) & ~0x40000) | 0x2);
		break;

	case HW_PAGE_SIZE16MB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtual_addr &
					      MMU_SSECTION_ADDR_MASK);
		pte_val =
		    (((physical_addr & MMU_SSECTION_ADDR_MASK) |
		      (map_attrs->endianism << 15) | (map_attrs->
						      element_size << 10) |
		      (map_attrs->mixed_size << 17)
		     ) | 0x40000 | 0x2);
		break;

	case HW_MMU_COARSE_PAGE_SIZE:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtual_addr &
					      MMU_SECTION_ADDR_MASK);
		pte_val = (physical_addr & MMU_PAGE_TABLE_MASK) | 1;
		break;

	default:
		return -EINVAL;
	}

	while (--num_entries >= 0)
		((u32 *) pte_addr)[num_entries] = pte_val;

	return status;
}

hw_status hw_mmu_pte_clear(const u32 pg_tbl_va, u32 virtual_addr, u32 page_size)
{
	hw_status status = 0;
	u32 pte_addr;
	s32 num_entries = 1;

	switch (page_size) {
	case HW_PAGE_SIZE4KB:
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtual_addr &
					      MMU_SMALL_PAGE_MASK);
		break;

	case HW_PAGE_SIZE64KB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtual_addr &
					      MMU_LARGE_PAGE_MASK);
		break;

	case HW_PAGE_SIZE1MB:
	case HW_MMU_COARSE_PAGE_SIZE:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtual_addr &
					      MMU_SECTION_ADDR_MASK);
		break;

	case HW_PAGE_SIZE16MB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtual_addr &
					      MMU_SSECTION_ADDR_MASK);
		break;

	default:
		return -EINVAL;
	}

	while (--num_entries >= 0)
		((u32 *) pte_addr)[num_entries] = 0;

	return status;
}

/* mmu_flush_entry */
static hw_status mmu_flush_entry(const void __iomem *base_address)
{
	hw_status status = 0;
	u32 flush_entry_data = 0x1;

	/* write values to register */
	MMUMMU_FLUSH_ENTRY_WRITE_REGISTER32(base_address, flush_entry_data);

	return status;
}

/* mmu_set_cam_entry */
static hw_status mmu_set_cam_entry(const void __iomem *base_address,
				   const u32 page_sz,
				   const u32 preserved_bit,
				   const u32 valid_bit,
				   const u32 virtual_addr_tag)
{
	hw_status status = 0;
	u32 mmu_cam_reg;

	mmu_cam_reg = (virtual_addr_tag << 12);
	mmu_cam_reg = (mmu_cam_reg) | (page_sz) | (valid_bit << 2) |
	    (preserved_bit << 3);

	/* write values to register */
	MMUMMU_CAM_WRITE_REGISTER32(base_address, mmu_cam_reg);

	return status;
}

/* mmu_set_ram_entry */
static hw_status mmu_set_ram_entry(const void __iomem *base_address,
				   const u32 physical_addr,
				   enum hw_endianism_t endianism,
				   enum hw_element_size_t element_size,
				   enum hw_mmu_mixed_size_t mixed_size)
{
	hw_status status = 0;
	u32 mmu_ram_reg;

	mmu_ram_reg = (physical_addr & MMU_ADDR_MASK);
	mmu_ram_reg = (mmu_ram_reg) | ((endianism << 9) | (element_size << 7) |
				       (mixed_size << 6));

	/* write values to register */
	MMUMMU_RAM_WRITE_REGISTER32(base_address, mmu_ram_reg);

	return status;

}

void hw_mmu_tlb_flush_all(const void __iomem *base)
{
	__raw_writeb(1, base + MMU_GFLUSH);
}
