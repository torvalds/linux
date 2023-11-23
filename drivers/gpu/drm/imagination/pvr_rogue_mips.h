/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_ROGUE_MIPS_H
#define PVR_ROGUE_MIPS_H

#include <linux/bits.h>
#include <linux/types.h>

/* Utility defines for memory management. */
#define ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K (12)
#define ROGUE_MIPSFW_PAGE_SIZE_4K (0x1 << ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K)
#define ROGUE_MIPSFW_PAGE_MASK_4K (ROGUE_MIPSFW_PAGE_SIZE_4K - 1)
#define ROGUE_MIPSFW_LOG2_PAGE_SIZE_64K (16)
#define ROGUE_MIPSFW_PAGE_SIZE_64K (0x1 << ROGUE_MIPSFW_LOG2_PAGE_SIZE_64K)
#define ROGUE_MIPSFW_PAGE_MASK_64K (ROGUE_MIPSFW_PAGE_SIZE_64K - 1)
#define ROGUE_MIPSFW_LOG2_PAGE_SIZE_256K (18)
#define ROGUE_MIPSFW_PAGE_SIZE_256K (0x1 << ROGUE_MIPSFW_LOG2_PAGE_SIZE_256K)
#define ROGUE_MIPSFW_PAGE_MASK_256K (ROGUE_MIPSFW_PAGE_SIZE_256K - 1)
#define ROGUE_MIPSFW_LOG2_PAGE_SIZE_1MB (20)
#define ROGUE_MIPSFW_PAGE_SIZE_1MB (0x1 << ROGUE_MIPSFW_LOG2_PAGE_SIZE_1MB)
#define ROGUE_MIPSFW_PAGE_MASK_1MB (ROGUE_MIPSFW_PAGE_SIZE_1MB - 1)
#define ROGUE_MIPSFW_LOG2_PAGE_SIZE_4MB (22)
#define ROGUE_MIPSFW_PAGE_SIZE_4MB (0x1 << ROGUE_MIPSFW_LOG2_PAGE_SIZE_4MB)
#define ROGUE_MIPSFW_PAGE_MASK_4MB (ROGUE_MIPSFW_PAGE_SIZE_4MB - 1)
#define ROGUE_MIPSFW_LOG2_PTE_ENTRY_SIZE (2)
/* log2 page table sizes dependent on FW heap size and page size (for each OS). */
#define ROGUE_MIPSFW_LOG2_PAGETABLE_SIZE_4K(pvr_dev) ((pvr_dev)->fw_dev.fw_heap_info.log2_size - \
						      ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K +    \
						      ROGUE_MIPSFW_LOG2_PTE_ENTRY_SIZE)
#define ROGUE_MIPSFW_LOG2_PAGETABLE_SIZE_64K(pvr_dev) ((pvr_dev)->fw_dev.fw_heap_info.log2_size - \
						       ROGUE_MIPSFW_LOG2_PAGE_SIZE_64K +   \
						       ROGUE_MIPSFW_LOG2_PTE_ENTRY_SIZE)
/* Maximum number of page table pages (both Host and MIPS pages). */
#define ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES (4)
/* Total number of TLB entries. */
#define ROGUE_MIPSFW_NUMBER_OF_TLB_ENTRIES (16)
/* "Uncached" caching policy. */
#define ROGUE_MIPSFW_UNCACHED_CACHE_POLICY (2)
/* "Write-back write-allocate" caching policy. */
#define ROGUE_MIPSFW_WRITEBACK_CACHE_POLICY (3)
/* "Write-through no write-allocate" caching policy. */
#define ROGUE_MIPSFW_WRITETHROUGH_CACHE_POLICY (1)
/* Cached policy used by MIPS in case of physical bus on 32 bit. */
#define ROGUE_MIPSFW_CACHED_POLICY (ROGUE_MIPSFW_WRITEBACK_CACHE_POLICY)
/* Cached policy used by MIPS in case of physical bus on more than 32 bit. */
#define ROGUE_MIPSFW_CACHED_POLICY_ABOVE_32BIT (ROGUE_MIPSFW_WRITETHROUGH_CACHE_POLICY)
/* Total number of Remap entries. */
#define ROGUE_MIPSFW_NUMBER_OF_REMAP_ENTRIES (2 * ROGUE_MIPSFW_NUMBER_OF_TLB_ENTRIES)

/* MIPS EntryLo/PTE format. */

#define ROGUE_MIPSFW_ENTRYLO_READ_INHIBIT_SHIFT (31U)
#define ROGUE_MIPSFW_ENTRYLO_READ_INHIBIT_CLRMSK (0X7FFFFFFF)
#define ROGUE_MIPSFW_ENTRYLO_READ_INHIBIT_EN (0X80000000)

#define ROGUE_MIPSFW_ENTRYLO_EXEC_INHIBIT_SHIFT (30U)
#define ROGUE_MIPSFW_ENTRYLO_EXEC_INHIBIT_CLRMSK (0XBFFFFFFF)
#define ROGUE_MIPSFW_ENTRYLO_EXEC_INHIBIT_EN (0X40000000)

/* Page Frame Number */
#define ROGUE_MIPSFW_ENTRYLO_PFN_SHIFT (6)
#define ROGUE_MIPSFW_ENTRYLO_PFN_ALIGNSHIFT (12)
/* Mask used for the MIPS Page Table in case of physical bus on 32 bit. */
#define ROGUE_MIPSFW_ENTRYLO_PFN_MASK (0x03FFFFC0)
#define ROGUE_MIPSFW_ENTRYLO_PFN_SIZE (20)
/* Mask used for the MIPS Page Table in case of physical bus on more than 32 bit. */
#define ROGUE_MIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT (0x3FFFFFC0)
#define ROGUE_MIPSFW_ENTRYLO_PFN_SIZE_ABOVE_32BIT (24)
#define ROGUE_MIPSFW_ADDR_TO_ENTRYLO_PFN_RSHIFT (ROGUE_MIPSFW_ENTRYLO_PFN_ALIGNSHIFT - \
						 ROGUE_MIPSFW_ENTRYLO_PFN_SHIFT)

#define ROGUE_MIPSFW_ENTRYLO_CACHE_POLICY_SHIFT (3U)
#define ROGUE_MIPSFW_ENTRYLO_CACHE_POLICY_CLRMSK (0XFFFFFFC7)

#define ROGUE_MIPSFW_ENTRYLO_DIRTY_SHIFT (2U)
#define ROGUE_MIPSFW_ENTRYLO_DIRTY_CLRMSK (0XFFFFFFFB)
#define ROGUE_MIPSFW_ENTRYLO_DIRTY_EN (0X00000004)

#define ROGUE_MIPSFW_ENTRYLO_VALID_SHIFT (1U)
#define ROGUE_MIPSFW_ENTRYLO_VALID_CLRMSK (0XFFFFFFFD)
#define ROGUE_MIPSFW_ENTRYLO_VALID_EN (0X00000002)

#define ROGUE_MIPSFW_ENTRYLO_GLOBAL_SHIFT (0U)
#define ROGUE_MIPSFW_ENTRYLO_GLOBAL_CLRMSK (0XFFFFFFFE)
#define ROGUE_MIPSFW_ENTRYLO_GLOBAL_EN (0X00000001)

#define ROGUE_MIPSFW_ENTRYLO_DVG (ROGUE_MIPSFW_ENTRYLO_DIRTY_EN | \
				  ROGUE_MIPSFW_ENTRYLO_VALID_EN | \
				  ROGUE_MIPSFW_ENTRYLO_GLOBAL_EN)
#define ROGUE_MIPSFW_ENTRYLO_UNCACHED (ROGUE_MIPSFW_UNCACHED_CACHE_POLICY << \
				       ROGUE_MIPSFW_ENTRYLO_CACHE_POLICY_SHIFT)
#define ROGUE_MIPSFW_ENTRYLO_DVG_UNCACHED (ROGUE_MIPSFW_ENTRYLO_DVG | \
					   ROGUE_MIPSFW_ENTRYLO_UNCACHED)

/* Remap Range Config Addr Out. */
/* These defines refer to the upper half of the Remap Range Config register. */
#define ROGUE_MIPSFW_REMAP_RANGE_ADDR_OUT_MASK (0x0FFFFFF0)
#define ROGUE_MIPSFW_REMAP_RANGE_ADDR_OUT_SHIFT (4) /* wrt upper half of the register. */
#define ROGUE_MIPSFW_REMAP_RANGE_ADDR_OUT_ALIGNSHIFT (12)
#define ROGUE_MIPSFW_ADDR_TO_RR_ADDR_OUT_RSHIFT (ROGUE_MIPSFW_REMAP_RANGE_ADDR_OUT_ALIGNSHIFT - \
						 ROGUE_MIPSFW_REMAP_RANGE_ADDR_OUT_SHIFT)

/*
 * Pages to trampoline problematic physical addresses:
 *   - ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN : 0x1FC0_0000
 *   - ROGUE_MIPSFW_DATA_REMAP_PHYS_ADDR_IN : 0x1FC0_1000
 *   - ROGUE_MIPSFW_CODE_REMAP_PHYS_ADDR_IN : 0x1FC0_2000
 *   - (benign trampoline)               : 0x1FC0_3000
 * that would otherwise be erroneously remapped by the MIPS wrapper.
 * (see "Firmware virtual layout and remap configuration" section below)
 */

#define ROGUE_MIPSFW_TRAMPOLINE_LOG2_NUMPAGES (2)
#define ROGUE_MIPSFW_TRAMPOLINE_NUMPAGES BIT(ROGUE_MIPSFW_TRAMPOLINE_LOG2_NUMPAGES)
#define ROGUE_MIPSFW_TRAMPOLINE_SIZE (ROGUE_MIPSFW_TRAMPOLINE_NUMPAGES << \
				      ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K)
#define ROGUE_MIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE (ROGUE_MIPSFW_TRAMPOLINE_LOG2_NUMPAGES + \
						   ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K)

#define ROGUE_MIPSFW_TRAMPOLINE_TARGET_PHYS_ADDR (ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN)
#define ROGUE_MIPSFW_TRAMPOLINE_OFFSET(a) ((a) - ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN)

#define ROGUE_MIPSFW_SENSITIVE_ADDR(a) (ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN == \
					(~((1 << ROGUE_MIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE) - 1) \
					 & (a)))

/* Firmware virtual layout and remap configuration. */
/*
 * For each remap region we define:
 * - the virtual base used by the Firmware to access code/data through that region
 * - the microAptivAP physical address correspondent to the virtual base address,
 *   used as input address and remapped to the actual physical address
 * - log2 of size of the region remapped by the MIPS wrapper, i.e. number of bits from
 *   the bottom of the base input address that survive onto the output address
 *   (this defines both the alignment and the maximum size of the remapped region)
 * - one or more code/data segments within the remapped region.
 */

/* Boot remap setup. */
#define ROGUE_MIPSFW_BOOT_REMAP_VIRTUAL_BASE (0xBFC00000)
#define ROGUE_MIPSFW_BOOT_REMAP_PHYS_ADDR_IN (0x1FC00000)
#define ROGUE_MIPSFW_BOOT_REMAP_LOG2_SEGMENT_SIZE (12)
#define ROGUE_MIPSFW_BOOT_NMI_CODE_VIRTUAL_BASE (ROGUE_MIPSFW_BOOT_REMAP_VIRTUAL_BASE)

/* Data remap setup. */
#define ROGUE_MIPSFW_DATA_REMAP_VIRTUAL_BASE (0xBFC01000)
#define ROGUE_MIPSFW_DATA_CACHED_REMAP_VIRTUAL_BASE (0x9FC01000)
#define ROGUE_MIPSFW_DATA_REMAP_PHYS_ADDR_IN (0x1FC01000)
#define ROGUE_MIPSFW_DATA_REMAP_LOG2_SEGMENT_SIZE (12)
#define ROGUE_MIPSFW_BOOT_NMI_DATA_VIRTUAL_BASE (ROGUE_MIPSFW_DATA_REMAP_VIRTUAL_BASE)

/* Code remap setup. */
#define ROGUE_MIPSFW_CODE_REMAP_VIRTUAL_BASE (0x9FC02000)
#define ROGUE_MIPSFW_CODE_REMAP_PHYS_ADDR_IN (0x1FC02000)
#define ROGUE_MIPSFW_CODE_REMAP_LOG2_SEGMENT_SIZE (12)
#define ROGUE_MIPSFW_EXCEPTIONS_VIRTUAL_BASE (ROGUE_MIPSFW_CODE_REMAP_VIRTUAL_BASE)

/* Permanent mappings setup. */
#define ROGUE_MIPSFW_PT_VIRTUAL_BASE (0xCF000000)
#define ROGUE_MIPSFW_REGISTERS_VIRTUAL_BASE (0xCF800000)
#define ROGUE_MIPSFW_STACK_VIRTUAL_BASE (0xCF600000)

/* Bootloader configuration data. */
/*
 * Bootloader configuration offset (where ROGUE_MIPSFW_BOOT_DATA lives)
 * within the bootloader/NMI data page.
 */
#define ROGUE_MIPSFW_BOOTLDR_CONF_OFFSET (0x0)

/* NMI shared data. */
/* Base address of the shared data within the bootloader/NMI data page. */
#define ROGUE_MIPSFW_NMI_SHARED_DATA_BASE (0x100)
/* Size used by Debug dump data. */
#define ROGUE_MIPSFW_NMI_SHARED_SIZE (0x2B0)
/* Offsets in the NMI shared area in 32-bit words. */
#define ROGUE_MIPSFW_NMI_SYNC_FLAG_OFFSET (0x0)
#define ROGUE_MIPSFW_NMI_STATE_OFFSET (0x1)
#define ROGUE_MIPSFW_NMI_ERROR_STATE_SET (0x1)

/* MIPS boot stage. */
#define ROGUE_MIPSFW_BOOT_STAGE_OFFSET (0x400)

/*
 * MIPS private data in the bootloader data page.
 * Memory below this offset is used by the FW only, no interface data allowed.
 */
#define ROGUE_MIPSFW_PRIVATE_DATA_OFFSET (0x800)

struct rogue_mipsfw_boot_data {
	u64 stack_phys_addr;
	u64 reg_base;
	u64 pt_phys_addr[ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES];
	u32 pt_log2_page_size;
	u32 pt_num_pages;
	u32 reserved1;
	u32 reserved2;
};

#define ROGUE_MIPSFW_GET_OFFSET_IN_DWORDS(offset) ((offset) / sizeof(u32))
#define ROGUE_MIPSFW_GET_OFFSET_IN_QWORDS(offset) ((offset) / sizeof(u64))

/* Used for compatibility checks. */
#define ROGUE_MIPSFW_ARCHTYPE_VER_CLRMSK (0xFFFFE3FFU)
#define ROGUE_MIPSFW_ARCHTYPE_VER_SHIFT (10U)
#define ROGUE_MIPSFW_CORE_ID_VALUE (0x001U)
#define ROGUE_FW_PROCESSOR_MIPS "MIPS"

/* microAptivAP cache line size. */
#define ROGUE_MIPSFW_MICROAPTIVEAP_CACHELINE_SIZE (16U)

/*
 * The SOCIF transactions are identified with the top 16 bits of the physical address emitted by
 * the MIPS.
 */
#define ROGUE_MIPSFW_WRAPPER_CONFIG_REGBANK_ADDR_ALIGN (16U)

/* Values to put in the MIPS selectors for performance counters. */
/* Icache accesses in COUNTER0. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_ICACHE_ACCESSES_C0 (9U)
/* Icache misses in COUNTER1. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_ICACHE_MISSES_C1 (9U)

/* Dcache accesses in COUNTER0. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_DCACHE_ACCESSES_C0 (10U)
/* Dcache misses in COUNTER1. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_DCACHE_MISSES_C1 (11U)

/* ITLB instruction accesses in COUNTER0. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_ITLB_INSTR_ACCESSES_C0 (5U)
/* JTLB instruction accesses misses in COUNTER1. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_JTLB_INSTR_MISSES_C1 (7U)

  /* Instructions completed in COUNTER0. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_INSTR_COMPLETED_C0 (1U)
/* JTLB data misses in COUNTER1. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_JTLB_DATA_MISSES_C1 (8U)

/* Shift for the Event field in the MIPS perf ctrl registers. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_EVENT_SHIFT (5U)

/* Additional flags for performance counters. See MIPS manual for further reference. */
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_COUNT_USER_MODE (8U)
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_COUNT_KERNEL_MODE (2U)
#define ROGUE_MIPSFW_PERF_COUNT_CTRL_COUNT_EXL (1U)

#define ROGUE_MIPSFW_C0_NBHWIRQ	8

/* Macros to decode C0_Cause register. */
#define ROGUE_MIPSFW_C0_CAUSE_EXCCODE(cause) (((cause) & 0x7c) >> 2)
#define ROGUE_MIPSFW_C0_CAUSE_EXCCODE_FWERROR 9
/* Use only when Coprocessor Unusable exception. */
#define ROGUE_MIPSFW_C0_CAUSE_UNUSABLE_UNIT(cause) (((cause) >> 28) & 0x3)
#define ROGUE_MIPSFW_C0_CAUSE_PENDING_HWIRQ(cause) (((cause) & 0x3fc00) >> 10)
#define ROGUE_MIPSFW_C0_CAUSE_FDCIPENDING BIT(21)
#define ROGUE_MIPSFW_C0_CAUSE_IV BIT(23)
#define ROGUE_MIPSFW_C0_CAUSE_IC BIT(25)
#define ROGUE_MIPSFW_C0_CAUSE_PCIPENDING BIT(26)
#define ROGUE_MIPSFW_C0_CAUSE_TIPENDING BIT(30)
#define ROGUE_MIPSFW_C0_CAUSE_BRANCH_DELAY BIT(31)

/* Macros to decode C0_Debug register. */
#define ROGUE_MIPSFW_C0_DEBUG_EXCCODE(debug) (((debug) >> 10) & 0x1f)
#define ROGUE_MIPSFW_C0_DEBUG_DSS BIT(0)
#define ROGUE_MIPSFW_C0_DEBUG_DBP BIT(1)
#define ROGUE_MIPSFW_C0_DEBUG_DDBL BIT(2)
#define ROGUE_MIPSFW_C0_DEBUG_DDBS BIT(3)
#define ROGUE_MIPSFW_C0_DEBUG_DIB BIT(4)
#define ROGUE_MIPSFW_C0_DEBUG_DINT BIT(5)
#define ROGUE_MIPSFW_C0_DEBUG_DIBIMPR BIT(6)
#define ROGUE_MIPSFW_C0_DEBUG_DDBLIMPR BIT(18)
#define ROGUE_MIPSFW_C0_DEBUG_DDBSIMPR BIT(19)
#define ROGUE_MIPSFW_C0_DEBUG_IEXI BIT(20)
#define ROGUE_MIPSFW_C0_DEBUG_DBUSEP BIT(21)
#define ROGUE_MIPSFW_C0_DEBUG_CACHEEP BIT(22)
#define ROGUE_MIPSFW_C0_DEBUG_MCHECKP BIT(23)
#define ROGUE_MIPSFW_C0_DEBUG_IBUSEP BIT(24)
#define ROGUE_MIPSFW_C0_DEBUG_DM BIT(30)
#define ROGUE_MIPSFW_C0_DEBUG_DBD BIT(31)

/* Macros to decode TLB entries. */
#define ROGUE_MIPSFW_TLB_GET_MASK(page_mask) (((page_mask) >> 13) & 0XFFFFU)
/* Page size in KB. */
#define ROGUE_MIPSFW_TLB_GET_PAGE_SIZE(page_mask) ((((page_mask) | 0x1FFF) + 1) >> 11)
/* Page size in KB. */
#define ROGUE_MIPSFW_TLB_GET_PAGE_MASK(page_size) ((((page_size) << 11) - 1) & ~0x7FF)
#define ROGUE_MIPSFW_TLB_GET_VPN2(entry_hi) ((entry_hi) >> 13)
#define ROGUE_MIPSFW_TLB_GET_COHERENCY(entry_lo) (((entry_lo) >> 3) & 0x7U)
#define ROGUE_MIPSFW_TLB_GET_PFN(entry_lo) (((entry_lo) >> 6) & 0XFFFFFU)
/* GET_PA uses a non-standard PFN mask for 36 bit addresses. */
#define ROGUE_MIPSFW_TLB_GET_PA(entry_lo) (((u64)(entry_lo) & \
					    ROGUE_MIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT) << 6)
#define ROGUE_MIPSFW_TLB_GET_INHIBIT(entry_lo) (((entry_lo) >> 30) & 0x3U)
#define ROGUE_MIPSFW_TLB_GET_DGV(entry_lo) ((entry_lo) & 0x7U)
#define ROGUE_MIPSFW_TLB_GLOBAL BIT(0)
#define ROGUE_MIPSFW_TLB_VALID BIT(1)
#define ROGUE_MIPSFW_TLB_DIRTY BIT(2)
#define ROGUE_MIPSFW_TLB_XI BIT(30)
#define ROGUE_MIPSFW_TLB_RI BIT(31)

#define ROGUE_MIPSFW_REMAP_GET_REGION_SIZE(region_size_encoding) (1 << (((region_size_encoding) \
									+ 1) << 1))

struct rogue_mips_tlb_entry {
	u32 tlb_page_mask;
	u32 tlb_hi;
	u32 tlb_lo0;
	u32 tlb_lo1;
};

struct rogue_mips_remap_entry {
	u32 remap_addr_in;  /* Always 4k aligned. */
	u32 remap_addr_out; /* Always 4k aligned. */
	u32 remap_region_size;
};

struct rogue_mips_state {
	u32 error_state; /* This must come first in the structure. */
	u32 error_epc;
	u32 status_register;
	u32 cause_register;
	u32 bad_register;
	u32 epc;
	u32 sp;
	u32 debug;
	u32 depc;
	u32 bad_instr;
	u32 unmapped_address;
	struct rogue_mips_tlb_entry tlb[ROGUE_MIPSFW_NUMBER_OF_TLB_ENTRIES];
	struct rogue_mips_remap_entry remap[ROGUE_MIPSFW_NUMBER_OF_REMAP_ENTRIES];
};

#include "pvr_rogue_mips_check.h"

#endif /* PVR_ROGUE_MIPS_H */
