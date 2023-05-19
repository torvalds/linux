/*************************************************************************/ /*!
@File           rgx_mips.h
@Title
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Platform       RGX
@Description    RGX MIPS definitions, kernel/user space
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(RGX_MIPS_H)
#define RGX_MIPS_H

/*
 * Utility defines for memory management
 */
#define RGXMIPSFW_LOG2_PAGE_SIZE_4K              (12)
#define RGXMIPSFW_PAGE_SIZE_4K                   (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE_4K)
#define RGXMIPSFW_PAGE_MASK_4K                   (RGXMIPSFW_PAGE_SIZE_4K - 1)
#define RGXMIPSFW_LOG2_PAGE_SIZE_64K             (16)
#define RGXMIPSFW_PAGE_SIZE_64K                  (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE_64K)
#define RGXMIPSFW_PAGE_MASK_64K                  (RGXMIPSFW_PAGE_SIZE_64K - 1)
#define RGXMIPSFW_LOG2_PAGE_SIZE_256K            (18)
#define RGXMIPSFW_PAGE_SIZE_256K                 (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE_256K)
#define RGXMIPSFW_PAGE_MASK_256K                 (RGXMIPSFW_PAGE_SIZE_256K - 1)
#define RGXMIPSFW_LOG2_PAGE_SIZE_1MB             (20)
#define RGXMIPSFW_PAGE_SIZE_1MB                  (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE_1MB)
#define RGXMIPSFW_PAGE_MASK_1MB                  (RGXMIPSFW_PAGE_SIZE_1MB - 1)
#define RGXMIPSFW_LOG2_PAGE_SIZE_4MB             (22)
#define RGXMIPSFW_PAGE_SIZE_4MB                  (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE_4MB)
#define RGXMIPSFW_PAGE_MASK_4MB                  (RGXMIPSFW_PAGE_SIZE_4MB - 1)
#define RGXMIPSFW_LOG2_PTE_ENTRY_SIZE            (2)
/* log2 page table sizes dependent on FW heap size and page size (for each OS) */
#define RGXMIPSFW_LOG2_PAGETABLE_SIZE_4K         (RGX_FIRMWARE_HEAP_SHIFT - RGXMIPSFW_LOG2_PAGE_SIZE_4K + RGXMIPSFW_LOG2_PTE_ENTRY_SIZE)
#define RGXMIPSFW_LOG2_PAGETABLE_SIZE_64K        (RGX_FIRMWARE_HEAP_SHIFT - RGXMIPSFW_LOG2_PAGE_SIZE_64K + RGXMIPSFW_LOG2_PTE_ENTRY_SIZE)
/* Maximum number of page table pages (both Host and MIPS pages) */
#define RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES        (4)
/* Total number of TLB entries */
#define RGXMIPSFW_NUMBER_OF_TLB_ENTRIES          (16)
/* "Uncached" caching policy */
#define RGXMIPSFW_UNCACHED_CACHE_POLICY          (0X00000002U)
/* "Write-back write-allocate" caching policy */
#define RGXMIPSFW_WRITEBACK_CACHE_POLICY         (0X00000003)
/* "Write-through no write-allocate" caching policy */
#define RGXMIPSFW_WRITETHROUGH_CACHE_POLICY      (0X00000001)
/* Cached policy used by MIPS in case of physical bus on 32 bit */
#define RGXMIPSFW_CACHED_POLICY                  (RGXMIPSFW_WRITEBACK_CACHE_POLICY)
/* Cached policy used by MIPS in case of physical bus on more than 32 bit */
#define RGXMIPSFW_CACHED_POLICY_ABOVE_32BIT      (RGXMIPSFW_WRITETHROUGH_CACHE_POLICY)
/* Total number of Remap entries */
#define RGXMIPSFW_NUMBER_OF_REMAP_ENTRIES        (2 * RGXMIPSFW_NUMBER_OF_TLB_ENTRIES)


/*
 * MIPS EntryLo/PTE format
 */

#define RGXMIPSFW_ENTRYLO_READ_INHIBIT_SHIFT     (31U)
#define RGXMIPSFW_ENTRYLO_READ_INHIBIT_CLRMSK    (0X7FFFFFFF)
#define RGXMIPSFW_ENTRYLO_READ_INHIBIT_EN        (0X80000000U)

#define RGXMIPSFW_ENTRYLO_EXEC_INHIBIT_SHIFT     (30U)
#define RGXMIPSFW_ENTRYLO_EXEC_INHIBIT_CLRMSK    (0XBFFFFFFF)
#define RGXMIPSFW_ENTRYLO_EXEC_INHIBIT_EN        (0X40000000U)

/* Page Frame Number */
#define RGXMIPSFW_ENTRYLO_PFN_SHIFT              (6)
#define RGXMIPSFW_ENTRYLO_PFN_ALIGNSHIFT         (12)
/* Mask used for the MIPS Page Table in case of physical bus on 32 bit */
#define RGXMIPSFW_ENTRYLO_PFN_MASK               (0x03FFFFC0)
#define RGXMIPSFW_ENTRYLO_PFN_SIZE               (20)
/* Mask used for the MIPS Page Table in case of physical bus on more than 32 bit */
#define RGXMIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT   (0x3FFFFFC0U)
#define RGXMIPSFW_ENTRYLO_PFN_SIZE_ABOVE_32BIT   (24)
#define RGXMIPSFW_ADDR_TO_ENTRYLO_PFN_RSHIFT     (RGXMIPSFW_ENTRYLO_PFN_ALIGNSHIFT - \
                                                  RGXMIPSFW_ENTRYLO_PFN_SHIFT)

#define RGXMIPSFW_ENTRYLO_CACHE_POLICY_SHIFT     (3U)
#define RGXMIPSFW_ENTRYLO_CACHE_POLICY_CLRMSK    (0XFFFFFFC7U)

#define RGXMIPSFW_ENTRYLO_DIRTY_SHIFT            (2U)
#define RGXMIPSFW_ENTRYLO_DIRTY_CLRMSK           (0XFFFFFFFB)
#define RGXMIPSFW_ENTRYLO_DIRTY_EN               (0X00000004U)

#define RGXMIPSFW_ENTRYLO_VALID_SHIFT            (1U)
#define RGXMIPSFW_ENTRYLO_VALID_CLRMSK           (0XFFFFFFFD)
#define RGXMIPSFW_ENTRYLO_VALID_EN               (0X00000002U)

#define RGXMIPSFW_ENTRYLO_GLOBAL_SHIFT           (0U)
#define RGXMIPSFW_ENTRYLO_GLOBAL_CLRMSK          (0XFFFFFFFE)
#define RGXMIPSFW_ENTRYLO_GLOBAL_EN              (0X00000001U)

#define RGXMIPSFW_ENTRYLO_DVG                    (RGXMIPSFW_ENTRYLO_DIRTY_EN | \
                                                  RGXMIPSFW_ENTRYLO_VALID_EN | \
                                                  RGXMIPSFW_ENTRYLO_GLOBAL_EN)
#define RGXMIPSFW_ENTRYLO_UNCACHED               (RGXMIPSFW_UNCACHED_CACHE_POLICY << \
                                                  RGXMIPSFW_ENTRYLO_CACHE_POLICY_SHIFT)
#define RGXMIPSFW_ENTRYLO_DVG_UNCACHED           (RGXMIPSFW_ENTRYLO_DVG | RGXMIPSFW_ENTRYLO_UNCACHED)


/* Remap Range Config Addr Out */
/* These defines refer to the upper half of the Remap Range Config register */
#define RGXMIPSFW_REMAP_RANGE_ADDR_OUT_MASK      (0x0FFFFFF0)
#define RGXMIPSFW_REMAP_RANGE_ADDR_OUT_SHIFT     (4)  /* wrt upper half of the register */
#define RGXMIPSFW_REMAP_RANGE_ADDR_OUT_ALIGNSHIFT (12)
#define RGXMIPSFW_ADDR_TO_RR_ADDR_OUT_RSHIFT     (RGXMIPSFW_REMAP_RANGE_ADDR_OUT_ALIGNSHIFT - \
                                                  RGXMIPSFW_REMAP_RANGE_ADDR_OUT_SHIFT)
/*
 * Pages to trampoline problematic physical addresses:
 *   - RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN : 0x1FC0_0000
 *   - RGXMIPSFW_DATA_REMAP_PHYS_ADDR_IN : 0x1FC0_1000
 *   - RGXMIPSFW_CODE_REMAP_PHYS_ADDR_IN : 0x1FC0_2000
 *   - (benign trampoline)               : 0x1FC0_3000
 * that would otherwise be erroneously remapped by the MIPS wrapper
 * (see "Firmware virtual layout and remap configuration" section below)
 */

#define RGXMIPSFW_TRAMPOLINE_LOG2_NUMPAGES       (2)
#define RGXMIPSFW_TRAMPOLINE_NUMPAGES            (1U << RGXMIPSFW_TRAMPOLINE_LOG2_NUMPAGES)
#define RGXMIPSFW_TRAMPOLINE_SIZE                (RGXMIPSFW_TRAMPOLINE_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE_4K)
#define RGXMIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE   (RGXMIPSFW_TRAMPOLINE_LOG2_NUMPAGES + RGXMIPSFW_LOG2_PAGE_SIZE_4K)

#define RGXMIPSFW_TRAMPOLINE_TARGET_PHYS_ADDR    (RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN)
#define RGXMIPSFW_TRAMPOLINE_OFFSET(a)           (a - RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN)

#define RGXMIPSFW_SENSITIVE_ADDR(a)              (RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN == (~((1UL << RGXMIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE)-1U) & a))

#define RGXMIPSFW_C0_PAGEMASK_4K                 (0x00001800)
#define RGXMIPSFW_C0_PAGEMASK_16K                (0x00007800)
#define RGXMIPSFW_C0_PAGEMASK_64K                (0x0001F800)
#define RGXMIPSFW_C0_PAGEMASK_256K               (0x0007F800)
#define RGXMIPSFW_C0_PAGEMASK_1MB                (0x001FF800)
#define RGXMIPSFW_C0_PAGEMASK_4MB                (0x007FF800)

#if defined(RGX_FEATURE_GPU_MULTICORE_SUPPORT)
/* GPU_COUNT: number of physical cores in the system
 * NUM_OF_REGBANKS = GPU_COUNT + 1 //XPU BROADCAST BANK
 * RGXMIPSFW_REGISTERS_PAGE_SIZE = NUM_OF_REGBANKS * REGBANK_SIZE(64KB) * NUM_OF_OSID(8)
 * For RGXMIPSFW_REGISTERS_PAGE_SIZE = 4MB, NUM_OF_REGBANKS = 8 so supports upto GPU_COUNT = 7 cores
 */
#define RGXMIPSFW_C0_PAGEMASK_REGISTERS                    (RGXMIPSFW_C0_PAGEMASK_4MB)
#define RGXMIPSFW_REGISTERS_PAGE_SIZE                      (RGXMIPSFW_PAGE_SIZE_4MB)
#define RGXMIPSFW_REGISTERS_REMAP_RANGE_CONFIG_REGION_SIZE (RGX_CR_MIPS_ADDR_REMAP_RANGE_CONFIG_REGION_SIZE_4MB)
#elif (RGX_NUM_DRIVERS_SUPPORTED == 1)
#define RGXMIPSFW_C0_PAGEMASK_REGISTERS                    (RGXMIPSFW_C0_PAGEMASK_64K)
#define RGXMIPSFW_REGISTERS_PAGE_SIZE                      (RGXMIPSFW_PAGE_SIZE_64K)
#define RGXMIPSFW_REGISTERS_REMAP_RANGE_CONFIG_REGION_SIZE (RGX_CR_MIPS_ADDR_REMAP_RANGE_CONFIG_REGION_SIZE_64KB)
#elif (RGX_NUM_DRIVERS_SUPPORTED <= 4)
#define RGXMIPSFW_C0_PAGEMASK_REGISTERS                    (RGXMIPSFW_C0_PAGEMASK_256K)
#define RGXMIPSFW_REGISTERS_PAGE_SIZE                      (RGXMIPSFW_PAGE_SIZE_256K)
#define RGXMIPSFW_REGISTERS_REMAP_RANGE_CONFIG_REGION_SIZE (RGX_CR_MIPS_ADDR_REMAP_RANGE_CONFIG_REGION_SIZE_256KB)
#elif (RGX_NUM_DRIVERS_SUPPORTED <= 8)
#define RGXMIPSFW_C0_PAGEMASK_REGISTERS                    (RGXMIPSFW_C0_PAGEMASK_1MB)
#define RGXMIPSFW_REGISTERS_PAGE_SIZE                      (RGXMIPSFW_PAGE_SIZE_1MB)
#define RGXMIPSFW_REGISTERS_REMAP_RANGE_CONFIG_REGION_SIZE (RGX_CR_MIPS_ADDR_REMAP_RANGE_CONFIG_REGION_SIZE_1MB)
#else
#error "MIPS TLB invalid params"
#endif

#define RGXMIPSFW_DECODE_REMAP_CONFIG_REGION_SIZE(r)       ((1U << (((r >> 7) + 1U) << 1U))*0x400)

/*
 * Firmware virtual layout and remap configuration
 */
/*
 * For each remap region we define:
 * - the virtual base used by the Firmware to access code/data through that region
 * - the microAptivAP physical address correspondent to the virtual base address,
 *   used as input address and remapped to the actual physical address
 * - log2 of size of the region remapped by the MIPS wrapper, i.e. number of bits from
 *   the bottom of the base input address that survive onto the output address
 *   (this defines both the alignment and the maximum size of the remapped region)
 * - one or more code/data segments within the remapped region
 */

/* Boot remap setup */
#define RGXMIPSFW_BOOT_REMAP_VIRTUAL_BASE        (0xBFC00000)
#define RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN        (0x1FC00000U)
#define RGXMIPSFW_BOOT_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_BOOT_NMI_CODE_VIRTUAL_BASE     (RGXMIPSFW_BOOT_REMAP_VIRTUAL_BASE)

/* Data remap setup */
#define RGXMIPSFW_DATA_REMAP_VIRTUAL_BASE        (0xBFC01000)
#define RGXMIPSFW_DATA_CACHED_REMAP_VIRTUAL_BASE (0x9FC01000)
#define RGXMIPSFW_DATA_REMAP_PHYS_ADDR_IN        (0x1FC01000U)
#define RGXMIPSFW_DATA_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_BOOT_NMI_DATA_VIRTUAL_BASE     (RGXMIPSFW_DATA_REMAP_VIRTUAL_BASE)

/* Code remap setup */
#define RGXMIPSFW_CODE_REMAP_VIRTUAL_BASE        (0x9FC02000)
#define RGXMIPSFW_CODE_REMAP_PHYS_ADDR_IN        (0x1FC02000U)
#define RGXMIPSFW_CODE_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_EXCEPTIONS_VIRTUAL_BASE        (RGXMIPSFW_CODE_REMAP_VIRTUAL_BASE)

/* Permanent mappings setup */
#define RGXMIPSFW_PT_VIRTUAL_BASE                (0xCF000000)
#define RGXMIPSFW_REGISTERS_VIRTUAL_BASE         (0xCF800000)
#define RGXMIPSFW_STACK_VIRTUAL_BASE             (0xCF600000)
#define RGXMIPSFW_MIPS_STATE_VIRTUAL_BASE        (RGXMIPSFW_REGISTERS_VIRTUAL_BASE + RGXMIPSFW_REGISTERS_PAGE_SIZE)

/* Offset inside the bootloader data page where the general_exception handler saves the error state.
 * The error value is then copied by the NMI handler to the MipsState struct in shared memory.
 * This is done because it's difficult to obtain the address of MipsState inside the general
 * exception handler. */
#define RGXMIPSFW_ERROR_STATE_BASE                        (0x100)

/*
 * Bootloader configuration data
 */
/* Bootloader configuration offset (where RGXMIPSFW_BOOT_DATA lives)
 * within the bootloader/NMI data page */
#define RGXMIPSFW_BOOTLDR_CONF_OFFSET                         (0x0U)

/*
 * MIPS boot stage
 */
#define RGXMIPSFW_BOOT_STAGE_OFFSET                           (0x400)

/*
 * MIPS private data in the bootloader data page.
 * Memory below this offset is used by the FW only, no interface data allowed.
 */
#define RGXMIPSFW_PRIVATE_DATA_OFFSET                         (0x800)


/* The things that follow are excluded when compiling assembly sources */
#if !defined(RGXMIPSFW_ASSEMBLY_CODE)
#include "img_types.h"
#include "km/rgxdefs_km.h"

typedef struct
{
	IMG_UINT64 ui64StackPhyAddr;
	IMG_UINT64 ui64RegBase;
	IMG_UINT64 aui64PTPhyAddr[RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES];
	IMG_UINT32 ui32PTLog2PageSize;
	IMG_UINT32 ui32PTNumPages;
	IMG_UINT32 ui32Reserved1;
	IMG_UINT32 ui32Reserved2;
} RGXMIPSFW_BOOT_DATA;

#define RGXMIPSFW_GET_OFFSET_IN_DWORDS(offset)                (offset / sizeof(IMG_UINT32))
#define RGXMIPSFW_GET_OFFSET_IN_QWORDS(offset)                (offset / sizeof(IMG_UINT64))

/* Used for compatibility checks */
#define RGXMIPSFW_ARCHTYPE_VER_CLRMSK                         (0xFFFFE3FFU)
#define RGXMIPSFW_ARCHTYPE_VER_SHIFT                          (10U)
#define RGXMIPSFW_CORE_ID_VALUE                               (0x001U)
#define RGXFW_PROCESSOR_MIPS                                  "MIPS"

/* microAptivAP cache line size */
#define RGXMIPSFW_MICROAPTIVEAP_CACHELINE_SIZE                (16U)

/* The SOCIF transactions are identified with the top 16 bits of the physical address emitted by the MIPS */
#define RGXMIPSFW_WRAPPER_CONFIG_REGBANK_ADDR_ALIGN           (16U)

/* Values to put in the MIPS selectors for performance counters */
#define RGXMIPSFW_PERF_COUNT_CTRL_ICACHE_ACCESSES_C0          (9U)   /* Icache accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_ICACHE_MISSES_C1            (9U)   /* Icache misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_DCACHE_ACCESSES_C0          (10U)  /* Dcache accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_DCACHE_MISSES_C1            (11U) /* Dcache misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_ITLB_INSTR_ACCESSES_C0      (5U)  /* ITLB instruction accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_JTLB_INSTR_MISSES_C1        (7U)  /* JTLB instruction accesses misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_INSTR_COMPLETED_C0          (1U)  /* Instructions completed in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_JTLB_DATA_MISSES_C1         (8U)  /* JTLB data misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_EVENT_SHIFT                 (5U)  /* Shift for the Event field in the MIPS perf ctrl registers */
/* Additional flags for performance counters. See MIPS manual for further reference */
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_USER_MODE             (8U)
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_KERNEL_MODE           (2U)
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_EXL                   (1U)


#define RGXMIPSFW_C0_NBHWIRQ	8

/* Macros to decode C0_Cause register */
#define RGXMIPSFW_C0_CAUSE_EXCCODE(CAUSE)       (((CAUSE) & 0x7cU) >> 2U)
#define RGXMIPSFW_C0_CAUSE_EXCCODE_FWERROR      9
/* Use only when Coprocessor Unusable exception */
#define RGXMIPSFW_C0_CAUSE_UNUSABLE_UNIT(CAUSE) (((CAUSE) >> 28U) & 0x3U)
#define RGXMIPSFW_C0_CAUSE_PENDING_HWIRQ(CAUSE) (((CAUSE) & 0x3fc00) >> 10)
#define RGXMIPSFW_C0_CAUSE_FDCIPENDING          (1UL << 21)
#define RGXMIPSFW_C0_CAUSE_IV                   (1UL << 23)
#define RGXMIPSFW_C0_CAUSE_IC                   (1UL << 25)
#define RGXMIPSFW_C0_CAUSE_PCIPENDING           (1UL << 26)
#define RGXMIPSFW_C0_CAUSE_TIPENDING            (1UL << 30)
#define RGXMIPSFW_C0_CAUSE_BRANCH_DELAY         (1UL << 31)

/* Macros to decode C0_Debug register */
#define RGXMIPSFW_C0_DEBUG_EXCCODE(DEBUG) (((DEBUG) >> 10U) & 0x1fU)
#define RGXMIPSFW_C0_DEBUG_DSS            (1UL << 0)
#define RGXMIPSFW_C0_DEBUG_DBP            (1UL << 1)
#define RGXMIPSFW_C0_DEBUG_DDBL           (1UL << 2)
#define RGXMIPSFW_C0_DEBUG_DDBS           (1UL << 3)
#define RGXMIPSFW_C0_DEBUG_DIB            (1UL << 4)
#define RGXMIPSFW_C0_DEBUG_DINT           (1UL << 5)
#define RGXMIPSFW_C0_DEBUG_DIBIMPR        (1UL << 6)
#define RGXMIPSFW_C0_DEBUG_DDBLIMPR       (1UL << 18)
#define RGXMIPSFW_C0_DEBUG_DDBSIMPR       (1UL << 19)
#define RGXMIPSFW_C0_DEBUG_IEXI           (1UL << 20)
#define RGXMIPSFW_C0_DEBUG_DBUSEP         (1UL << 21)
#define RGXMIPSFW_C0_DEBUG_CACHEEP        (1UL << 22)
#define RGXMIPSFW_C0_DEBUG_MCHECKP        (1UL << 23)
#define RGXMIPSFW_C0_DEBUG_IBUSEP         (1UL << 24)
#define RGXMIPSFW_C0_DEBUG_DM             (1UL << 30)
#define RGXMIPSFW_C0_DEBUG_DBD            (1UL << 31)

/* Macros to decode TLB entries */
#define RGXMIPSFW_TLB_GET_MASK(PAGE_MASK)       (((PAGE_MASK) >> 13) & 0XFFFFU)
#define RGXMIPSFW_TLB_GET_PAGE_SIZE(PAGE_MASK)  ((((PAGE_MASK) | 0x1FFFU) + 1U) >> 11U) /* page size in KB */
#define RGXMIPSFW_TLB_GET_PAGE_MASK(PAGE_SIZE)  ((((PAGE_SIZE) << 11) - 1) & ~0x7FF) /* page size in KB */
#define RGXMIPSFW_TLB_GET_VPN2(ENTRY_HI)        ((ENTRY_HI) >> 13)
#define RGXMIPSFW_TLB_GET_COHERENCY(ENTRY_LO)   (((ENTRY_LO) >> 3) & 0x7U)
#define RGXMIPSFW_TLB_GET_PFN(ENTRY_LO)         (((ENTRY_LO) >> 6) & 0XFFFFFU)
/* GET_PA uses a non-standard PFN mask for 36 bit addresses */
#define RGXMIPSFW_TLB_GET_PA(ENTRY_LO)          (((IMG_UINT64)(ENTRY_LO) & RGXMIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT) << 6)
#define RGXMIPSFW_TLB_GET_INHIBIT(ENTRY_LO)     (((ENTRY_LO) >> 30) & 0x3U)
#define RGXMIPSFW_TLB_GET_DGV(ENTRY_LO)         ((ENTRY_LO) & 0x7U)
#define RGXMIPSFW_TLB_GLOBAL                    (1U)
#define RGXMIPSFW_TLB_VALID                     (1U << 1)
#define RGXMIPSFW_TLB_DIRTY                     (1U << 2)
#define RGXMIPSFW_TLB_XI                        (1U << 30)
#define RGXMIPSFW_TLB_RI                        (1U << 31)

typedef struct {
	IMG_UINT32 ui32TLBPageMask;
	IMG_UINT32 ui32TLBHi;
	IMG_UINT32 ui32TLBLo0;
	IMG_UINT32 ui32TLBLo1;
} RGX_MIPS_TLB_ENTRY;

typedef struct {
	IMG_UINT32 ui32RemapAddrIn;     /* always 4k aligned */
	IMG_UINT32 ui32RemapAddrOut;    /* always 4k aligned */
	IMG_UINT32 ui32RemapRegionSize;
} RGX_MIPS_REMAP_ENTRY;

typedef struct {
	IMG_UINT32 ui32ErrorState; /* This must come first in the structure */
	IMG_UINT32 ui32Sync;
	IMG_UINT32 ui32ErrorEPC;
	IMG_UINT32 ui32StatusRegister;
	IMG_UINT32 ui32CauseRegister;
	IMG_UINT32 ui32BadRegister;
	IMG_UINT32 ui32EPC;
	IMG_UINT32 ui32SP;
	IMG_UINT32 ui32Debug;
	IMG_UINT32 ui32DEPC;
	IMG_UINT32 ui32BadInstr;
	IMG_UINT32 ui32UnmappedAddress;
	RGX_MIPS_TLB_ENTRY asTLB[RGXMIPSFW_NUMBER_OF_TLB_ENTRIES];
	IMG_UINT64 aui64Remap[RGXMIPSFW_NUMBER_OF_REMAP_ENTRIES];
} RGX_MIPS_STATE;

static_assert(offsetof(RGX_MIPS_STATE, ui32ErrorState) == 0,
				"ui32ErrorState is not the first member of the RGX_MIPS_STATE struct");

#if defined(SUPPORT_MIPS_64K_PAGE_SIZE)
static_assert(RGXMIPSFW_REGISTERS_PAGE_SIZE >= RGXMIPSFW_PAGE_SIZE_64K,
				"Register page size must be greater or equal to MIPS page size");
#else
static_assert(RGXMIPSFW_REGISTERS_PAGE_SIZE >= RGXMIPSFW_PAGE_SIZE_4K,
				"Register page size must be greater or equal to MIPS page size");
#endif


#endif /* RGXMIPSFW_ASSEMBLY_CODE */

#endif /* RGX_MIPS_H */
