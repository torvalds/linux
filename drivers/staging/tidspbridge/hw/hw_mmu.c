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

#include <GlobalTypes.h>
#include <linux/io.h>
#include "MMURegAcM.h"
#include <hw_defs.h>
#include <hw_mmu.h>
#include <linux/types.h>

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
 *       Identifier      : baseAddress
 *       Type		: const u32
 *       Description     : Base Address of instance of MMU module
 *
 * RETURNS:
 *
 *       Type		: hw_status
 *       Description     : RET_OK		 -- No errors occured
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
static hw_status mmu_flush_entry(const void __iomem *baseAddress);

/*
 * FUNCTION	      : mmu_set_cam_entry
 *
 * INPUTS:
 *
 *       Identifier      : baseAddress
 *       TypE		: const u32
 *       Description     : Base Address of instance of MMU module
 *
 *       Identifier      : pageSize
 *       TypE		: const u32
 *       Description     : It indicates the page size
 *
 *       Identifier      : preservedBit
 *       Type		: const u32
 *       Description     : It indicates the TLB entry is preserved entry
 *							or not
 *
 *       Identifier      : validBit
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
 *       Description     : RET_OK		 -- No errors occured
 *			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
 *						   was set to NULL
 *			 RET_PARAM_OUT_OF_RANGE -- Input Parameter out
 *						   of Range
 *
 * PURPOSE:	      	: Set MMU_CAM reg
 *
 * METHOD:	       	: Check the Input parameters and set the CAM entry.
 */
static hw_status mmu_set_cam_entry(const void __iomem *baseAddress,
				   const u32 pageSize,
				   const u32 preservedBit,
				   const u32 validBit,
				   const u32 virtual_addr_tag);

/*
 * FUNCTION	      : mmu_set_ram_entry
 *
 * INPUTS:
 *
 *       Identifier      : baseAddress
 *       Type	    	: const u32
 *       Description     : Base Address of instance of MMU module
 *
 *       Identifier      : physicalAddr
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
 *       Description     : RET_OK		 -- No errors occured
 *			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
 *							was set to NULL
 *			 RET_PARAM_OUT_OF_RANGE -- Input Parameter
 *							out of Range
 *
 * PURPOSE:	      : Set MMU_CAM reg
 *
 * METHOD:	       : Check the Input parameters and set the RAM entry.
 */
static hw_status mmu_set_ram_entry(const void __iomem *baseAddress,
				   const u32 physicalAddr,
				   enum hw_endianism_t endianism,
				   enum hw_element_size_t element_size,
				   enum hw_mmu_mixed_size_t mixed_size);

/* HW FUNCTIONS */

hw_status hw_mmu_enable(const void __iomem *baseAddress)
{
	hw_status status = RET_OK;

	MMUMMU_CNTLMMU_ENABLE_WRITE32(baseAddress, HW_SET);

	return status;
}

hw_status hw_mmu_disable(const void __iomem *baseAddress)
{
	hw_status status = RET_OK;

	MMUMMU_CNTLMMU_ENABLE_WRITE32(baseAddress, HW_CLEAR);

	return status;
}

hw_status hw_mmu_num_locked_set(const void __iomem *baseAddress,
				u32 numLockedEntries)
{
	hw_status status = RET_OK;

	MMUMMU_LOCK_BASE_VALUE_WRITE32(baseAddress, numLockedEntries);

	return status;
}

hw_status hw_mmu_victim_num_set(const void __iomem *baseAddress,
				u32 victimEntryNum)
{
	hw_status status = RET_OK;

	MMUMMU_LOCK_CURRENT_VICTIM_WRITE32(baseAddress, victimEntryNum);

	return status;
}

hw_status hw_mmu_event_ack(const void __iomem *baseAddress, u32 irqMask)
{
	hw_status status = RET_OK;

	MMUMMU_IRQSTATUS_WRITE_REGISTER32(baseAddress, irqMask);

	return status;
}

hw_status hw_mmu_event_disable(const void __iomem *baseAddress, u32 irqMask)
{
	hw_status status = RET_OK;
	u32 irq_reg;

	irq_reg = MMUMMU_IRQENABLE_READ_REGISTER32(baseAddress);

	MMUMMU_IRQENABLE_WRITE_REGISTER32(baseAddress, irq_reg & ~irqMask);

	return status;
}

hw_status hw_mmu_event_enable(const void __iomem *baseAddress, u32 irqMask)
{
	hw_status status = RET_OK;
	u32 irq_reg;

	irq_reg = MMUMMU_IRQENABLE_READ_REGISTER32(baseAddress);

	MMUMMU_IRQENABLE_WRITE_REGISTER32(baseAddress, irq_reg | irqMask);

	return status;
}

hw_status hw_mmu_event_status(const void __iomem *baseAddress, u32 *irqMask)
{
	hw_status status = RET_OK;

	*irqMask = MMUMMU_IRQSTATUS_READ_REGISTER32(baseAddress);

	return status;
}

hw_status hw_mmu_fault_addr_read(const void __iomem *baseAddress, u32 *addr)
{
	hw_status status = RET_OK;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

	/* read values from register */
	*addr = MMUMMU_FAULT_AD_READ_REGISTER32(baseAddress);

	return status;
}

hw_status hw_mmu_ttb_set(const void __iomem *baseAddress, u32 TTBPhysAddr)
{
	hw_status status = RET_OK;
	u32 load_ttb;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

	load_ttb = TTBPhysAddr & ~0x7FUL;
	/* write values to register */
	MMUMMU_TTB_WRITE_REGISTER32(baseAddress, load_ttb);

	return status;
}

hw_status hw_mmu_twl_enable(const void __iomem *baseAddress)
{
	hw_status status = RET_OK;

	MMUMMU_CNTLTWL_ENABLE_WRITE32(baseAddress, HW_SET);

	return status;
}

hw_status hw_mmu_twl_disable(const void __iomem *baseAddress)
{
	hw_status status = RET_OK;

	MMUMMU_CNTLTWL_ENABLE_WRITE32(baseAddress, HW_CLEAR);

	return status;
}

hw_status hw_mmu_tlb_flush(const void __iomem *baseAddress, u32 virtualAddr,
			   u32 pageSize)
{
	hw_status status = RET_OK;
	u32 virtual_addr_tag;
	enum hw_mmu_page_size_t pg_size_bits;

	switch (pageSize) {
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
		return RET_FAIL;
	}

	/* Generate the 20-bit tag from virtual address */
	virtual_addr_tag = ((virtualAddr & MMU_ADDR_MASK) >> 12);

	mmu_set_cam_entry(baseAddress, pg_size_bits, 0, 0, virtual_addr_tag);

	mmu_flush_entry(baseAddress);

	return status;
}

hw_status hw_mmu_tlb_add(const void __iomem *baseAddress,
			 u32 physicalAddr,
			 u32 virtualAddr,
			 u32 pageSize,
			 u32 entryNum,
			 struct hw_mmu_map_attrs_t *map_attrs,
			 s8 preservedBit, s8 validBit)
{
	hw_status status = RET_OK;
	u32 lock_reg;
	u32 virtual_addr_tag;
	enum hw_mmu_page_size_t mmu_pg_size;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(pageSize, MMU_PAGE_MAX, RET_PARAM_OUT_OF_RANGE,
			       RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(map_attrs->element_size, MMU_ELEMENTSIZE_MAX,
			       RET_PARAM_OUT_OF_RANGE, RES_MMU_BASE +
			       RES_INVALID_INPUT_PARAM);

	switch (pageSize) {
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
		return RET_FAIL;
	}

	lock_reg = MMUMMU_LOCK_READ_REGISTER32(baseAddress);

	/* Generate the 20-bit tag from virtual address */
	virtual_addr_tag = ((virtualAddr & MMU_ADDR_MASK) >> 12);

	/* Write the fields in the CAM Entry Register */
	mmu_set_cam_entry(baseAddress, mmu_pg_size, preservedBit, validBit,
			  virtual_addr_tag);

	/* Write the different fields of the RAM Entry Register */
	/* endianism of the page,Element Size of the page (8, 16, 32, 64 bit) */
	mmu_set_ram_entry(baseAddress, physicalAddr, map_attrs->endianism,
			  map_attrs->element_size, map_attrs->mixed_size);

	/* Update the MMU Lock Register */
	/* currentVictim between lockedBaseValue and (MMU_Entries_Number - 1) */
	MMUMMU_LOCK_CURRENT_VICTIM_WRITE32(baseAddress, entryNum);

	/* Enable loading of an entry in TLB by writing 1
	   into LD_TLB_REG register */
	MMUMMU_LD_TLB_WRITE_REGISTER32(baseAddress, MMU_LOAD_TLB);

	MMUMMU_LOCK_WRITE_REGISTER32(baseAddress, lock_reg);

	return status;
}

hw_status hw_mmu_pte_set(const u32 pg_tbl_va,
			 u32 physicalAddr,
			 u32 virtualAddr,
			 u32 pageSize, struct hw_mmu_map_attrs_t *map_attrs)
{
	hw_status status = RET_OK;
	u32 pte_addr, pte_val;
	s32 num_entries = 1;

	switch (pageSize) {
	case HW_PAGE_SIZE4KB:
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtualAddr &
					      MMU_SMALL_PAGE_MASK);
		pte_val =
		    ((physicalAddr & MMU_SMALL_PAGE_MASK) |
		     (map_attrs->endianism << 9) | (map_attrs->
						    element_size << 4) |
		     (map_attrs->mixed_size << 11) | 2);
		break;

	case HW_PAGE_SIZE64KB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtualAddr &
					      MMU_LARGE_PAGE_MASK);
		pte_val =
		    ((physicalAddr & MMU_LARGE_PAGE_MASK) |
		     (map_attrs->endianism << 9) | (map_attrs->
						    element_size << 4) |
		     (map_attrs->mixed_size << 11) | 1);
		break;

	case HW_PAGE_SIZE1MB:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtualAddr &
					      MMU_SECTION_ADDR_MASK);
		pte_val =
		    ((((physicalAddr & MMU_SECTION_ADDR_MASK) |
		       (map_attrs->endianism << 15) | (map_attrs->
						       element_size << 10) |
		       (map_attrs->mixed_size << 17)) & ~0x40000) | 0x2);
		break;

	case HW_PAGE_SIZE16MB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtualAddr &
					      MMU_SSECTION_ADDR_MASK);
		pte_val =
		    (((physicalAddr & MMU_SSECTION_ADDR_MASK) |
		      (map_attrs->endianism << 15) | (map_attrs->
						      element_size << 10) |
		      (map_attrs->mixed_size << 17)
		     ) | 0x40000 | 0x2);
		break;

	case HW_MMU_COARSE_PAGE_SIZE:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtualAddr &
					      MMU_SECTION_ADDR_MASK);
		pte_val = (physicalAddr & MMU_PAGE_TABLE_MASK) | 1;
		break;

	default:
		return RET_FAIL;
	}

	while (--num_entries >= 0)
		((u32 *) pte_addr)[num_entries] = pte_val;

	return status;
}

hw_status hw_mmu_pte_clear(const u32 pg_tbl_va, u32 virtualAddr, u32 page_size)
{
	hw_status status = RET_OK;
	u32 pte_addr;
	s32 num_entries = 1;

	switch (page_size) {
	case HW_PAGE_SIZE4KB:
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtualAddr &
					      MMU_SMALL_PAGE_MASK);
		break;

	case HW_PAGE_SIZE64KB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l2(pg_tbl_va,
					      virtualAddr &
					      MMU_LARGE_PAGE_MASK);
		break;

	case HW_PAGE_SIZE1MB:
	case HW_MMU_COARSE_PAGE_SIZE:
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtualAddr &
					      MMU_SECTION_ADDR_MASK);
		break;

	case HW_PAGE_SIZE16MB:
		num_entries = 16;
		pte_addr = hw_mmu_pte_addr_l1(pg_tbl_va,
					      virtualAddr &
					      MMU_SSECTION_ADDR_MASK);
		break;

	default:
		return RET_FAIL;
	}

	while (--num_entries >= 0)
		((u32 *) pte_addr)[num_entries] = 0;

	return status;
}

/* mmu_flush_entry */
static hw_status mmu_flush_entry(const void __iomem *baseAddress)
{
	hw_status status = RET_OK;
	u32 flush_entry_data = 0x1;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

	/* write values to register */
	MMUMMU_FLUSH_ENTRY_WRITE_REGISTER32(baseAddress, flush_entry_data);

	return status;
}

/* mmu_set_cam_entry */
static hw_status mmu_set_cam_entry(const void __iomem *baseAddress,
				   const u32 pageSize,
				   const u32 preservedBit,
				   const u32 validBit,
				   const u32 virtual_addr_tag)
{
	hw_status status = RET_OK;
	u32 mmu_cam_reg;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

	mmu_cam_reg = (virtual_addr_tag << 12);
	mmu_cam_reg = (mmu_cam_reg) | (pageSize) | (validBit << 2) |
	    (preservedBit << 3);

	/* write values to register */
	MMUMMU_CAM_WRITE_REGISTER32(baseAddress, mmu_cam_reg);

	return status;
}

/* mmu_set_ram_entry */
static hw_status mmu_set_ram_entry(const void __iomem *baseAddress,
				   const u32 physicalAddr,
				   enum hw_endianism_t endianism,
				   enum hw_element_size_t element_size,
				   enum hw_mmu_mixed_size_t mixed_size)
{
	hw_status status = RET_OK;
	u32 mmu_ram_reg;

	/*Check the input Parameters */
	CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
			  RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
	CHECK_INPUT_RANGE_MIN0(element_size, MMU_ELEMENTSIZE_MAX,
			       RET_PARAM_OUT_OF_RANGE, RES_MMU_BASE +
			       RES_INVALID_INPUT_PARAM);

	mmu_ram_reg = (physicalAddr & MMU_ADDR_MASK);
	mmu_ram_reg = (mmu_ram_reg) | ((endianism << 9) | (element_size << 7) |
				       (mixed_size << 6));

	/* write values to register */
	MMUMMU_RAM_WRITE_REGISTER32(baseAddress, mmu_ram_reg);

	return status;

}

void hw_mmu_tlb_flush_all(const void __iomem *base)
{
	__raw_writeb(1, base + MMU_GFLUSH);
}
