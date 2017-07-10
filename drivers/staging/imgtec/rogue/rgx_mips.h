/*************************************************************************/ /*!
@File           rgx_mips.h
@Title
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Platform       RGX
@Description    RGX MIPS definitions, user space
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

#if !defined (__RGX_MIPS_H__)
#define __RGX_MIPS_H__

/*
 * Utility defines for memory management
 */
#define RGXMIPSFW_LOG2_PAGE_SIZE                 (12)
#define RGXMIPSFW_LOG2_PAGE_SIZE_64K             (16)
/* Page size */
#define RGXMIPSFW_PAGE_SIZE                      (0x1 << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_PAGE_MASK                      (RGXMIPSFW_PAGE_SIZE - 1)
#define RGXMIPSFW_LOG2_PAGETABLE_PAGE_SIZE       (15)
#define RGXMIPSFW_LOG2_PTE_ENTRY_SIZE            (2)
/* Page mask MIPS register setting for bigger pages */
#define RGXMIPSFW_PAGE_MASK_16K                  (0x00007800)
#define RGXMIPSFW_PAGE_MASK_64K                  (0x0001F800)
/* Page Frame Number of the entry lo */
#define RGXMIPSFW_ENTRYLO_PFN_MASK               (0x03FFFFC0)
#define RGXMIPSFW_ENTRYLO_PFN_SHIFT              (6)
/* Dirty Valid And Global bits in entry lo */
#define RGXMIPSFW_ENTRYLO_DVG_MASK               (0x00000007)
/* Dirty Valid And Global bits + caching policy in entry lo */
#define RGXMIPSFW_ENTRYLO_DVGC_MASK              (0x0000003F)
/* Total number of TLB entries */
#define RGXMIPSFW_NUMBER_OF_TLB_ENTRIES          (16)


/*
 * Firmware physical layout
 */
#define RGXMIPSFW_CODE_BASE_PAGE                 (0x0)
#define RGXMIPSFW_CODE_OFFSET                    (RGXMIPSFW_CODE_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#if defined(SUPPORT_TRUSTED_DEVICE)
/* Clean way of getting a 256K allocation (62 + 1 + 1 pages) without using too many ifdefs */
/* This will need to be changed if the non-secure builds reach this amount of pages */
#define RGXMIPSFW_CODE_NUMPAGES                  (62)
#else
#define RGXMIPSFW_CODE_NUMPAGES                  (38)
#endif
#define RGXMIPSFW_CODE_SIZE                      (RGXMIPSFW_CODE_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)

#define RGXMIPSFW_EXCEPTIONSVECTORS_BASE_PAGE    (RGXMIPSFW_CODE_BASE_PAGE + RGXMIPSFW_CODE_NUMPAGES)
#define RGXMIPSFW_EXCEPTIONSVECTORS_OFFSET       (RGXMIPSFW_EXCEPTIONSVECTORS_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_EXCEPTIONSVECTORS_NUMPAGES     (1)
#define RGXMIPSFW_EXCEPTIONSVECTORS_SIZE         (RGXMIPSFW_EXCEPTIONSVECTORS_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)

#define RGXMIPSFW_BOOT_NMI_CODE_BASE_PAGE        (RGXMIPSFW_EXCEPTIONSVECTORS_BASE_PAGE + RGXMIPSFW_EXCEPTIONSVECTORS_NUMPAGES)
#define RGXMIPSFW_BOOT_NMI_CODE_OFFSET           (RGXMIPSFW_BOOT_NMI_CODE_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_BOOT_NMI_CODE_NUMPAGES         (1)
#define RGXMIPSFW_BOOT_NMI_CODE_SIZE             (RGXMIPSFW_BOOT_NMI_CODE_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)


#define RGXMIPSFW_DATA_BASE_PAGE                 (0x0)
#define RGXMIPSFW_DATA_OFFSET                    (RGXMIPSFW_DATA_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_DATA_NUMPAGES                  (22)
#define RGXMIPSFW_DATA_SIZE                      (RGXMIPSFW_DATA_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)

#define RGXMIPSFW_BOOT_NMI_DATA_BASE_PAGE        (RGXMIPSFW_DATA_BASE_PAGE + RGXMIPSFW_DATA_NUMPAGES)
#define RGXMIPSFW_BOOT_NMI_DATA_OFFSET           (RGXMIPSFW_BOOT_NMI_DATA_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_BOOT_NMI_DATA_NUMPAGES         (1)
#define RGXMIPSFW_BOOT_NMI_DATA_SIZE             (RGXMIPSFW_BOOT_NMI_DATA_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)

#define RGXMIPSFW_STACK_BASE_PAGE                (RGXMIPSFW_BOOT_NMI_DATA_BASE_PAGE + RGXMIPSFW_BOOT_NMI_DATA_NUMPAGES)
#define RGXMIPSFW_STACK_OFFSET                   (RGXMIPSFW_STACK_BASE_PAGE << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_STACK_NUMPAGES                 (1)
#define RGXMIPSFW_STACK_SIZE                     (RGXMIPSFW_STACK_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)

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
#define RGXMIPSFW_TRAMPOLINE_NUMPAGES            (1 << RGXMIPSFW_TRAMPOLINE_LOG2_NUMPAGES)
#define RGXMIPSFW_TRAMPOLINE_SIZE                (RGXMIPSFW_TRAMPOLINE_NUMPAGES << RGXMIPSFW_LOG2_PAGE_SIZE)
#define RGXMIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE   (RGXMIPSFW_TRAMPOLINE_LOG2_NUMPAGES + RGXMIPSFW_LOG2_PAGE_SIZE)

#define RGXMIPSFW_TRAMPOLINE_TARGET_PHYS_ADDR    (RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN)
#define RGXMIPSFW_TRAMPOLINE_OFFSET(a)           (a - RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN)

#define RGXMIPSFW_SENSITIVE_ADDR(a)              (RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN == (~((1<<RGXMIPSFW_TRAMPOLINE_LOG2_SEGMENT_SIZE)-1) & a))

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
#define RGXMIPSFW_BOOT_REMAP_PHYS_ADDR_IN        (0x1FC00000)
#define RGXMIPSFW_BOOT_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_BOOT_NMI_CODE_VIRTUAL_BASE     (RGXMIPSFW_BOOT_REMAP_VIRTUAL_BASE)

/* Data remap setup */
#define RGXMIPSFW_DATA_REMAP_VIRTUAL_BASE        (0xBFC01000)
#define RGXMIPSFW_DATA_REMAP_PHYS_ADDR_IN        (0x1FC01000)
#define RGXMIPSFW_DATA_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_BOOT_NMI_DATA_VIRTUAL_BASE     (RGXMIPSFW_DATA_REMAP_VIRTUAL_BASE)

/* Code remap setup */
#define RGXMIPSFW_CODE_REMAP_VIRTUAL_BASE        (0x9FC02000)
#define RGXMIPSFW_CODE_REMAP_PHYS_ADDR_IN        (0x1FC02000)
#define RGXMIPSFW_CODE_REMAP_LOG2_SEGMENT_SIZE   (12)
#define RGXMIPSFW_EXCEPTIONS_VIRTUAL_BASE        (RGXMIPSFW_CODE_REMAP_VIRTUAL_BASE)

/* Fixed TLB setup */
#define RGXMIPSFW_PT_VIRTUAL_BASE                (0xCF000000)
#define RGXMIPSFW_REGISTERS_VIRTUAL_BASE         (0xCF400000)
#define RGXMIPSFW_STACK_VIRTUAL_BASE             (0xCF600000)

#if defined(SUPPORT_TRUSTED_DEVICE)
/* The extra fixed TLB entries are used in security builds for the FW code */
#define RGXMIPSFW_NUMBER_OF_RESERVED_TLB         (5)
#else
#define RGXMIPSFW_NUMBER_OF_RESERVED_TLB         (3)
#endif

/* Firmware heap setup */
#define RGXMIPSFW_FIRMWARE_HEAP_BASE             (0xC0000000)
#define RGXMIPSFW_CODE_VIRTUAL_BASE              (RGXMIPSFW_FIRMWARE_HEAP_BASE)
/* The data virtual base takes into account the exception vectors page
 * and the boot code page mapped in the FW heap together with the FW code
 * (we can only map Firmware code allocation as a whole) */
#define RGXMIPSFW_DATA_VIRTUAL_BASE              (RGXMIPSFW_CODE_VIRTUAL_BASE + RGXMIPSFW_CODE_SIZE + \
                                                  RGXMIPSFW_EXCEPTIONSVECTORS_SIZE + RGXMIPSFW_BOOT_NMI_CODE_SIZE)


/*
 * Bootloader configuration data
 */
/* Bootloader configuration offset within the bootloader/NMI data page */
#define RGXMIPSFW_BOOTLDR_CONF_OFFSET                         (0x0)
/* Offsets of bootloader configuration parameters in 64-bit words */
#define RGXMIPSFW_ROGUE_REGS_BASE_PHYADDR_OFFSET              (0x0)
#define RGXMIPSFW_PAGE_TABLE_BASE_PHYADDR_OFFSET              (0x1)
#define RGXMIPSFW_STACKPOINTER_PHYADDR_OFFSET                 (0x2)
#define RGXMIPSFW_RESERVED_FUTURE_OFFSET                      (0x3)
#define RGXMIPSFW_FWINIT_VIRTADDR_OFFSET                      (0x4)

/*
 * MIPS Fence offset in the bootloader/NMI data page
 */
#define RGXMIPSFW_FENCE_OFFSET                                (0x80)

/*
 * NMI shared data
 */
/* Base address of the shared data within the bootloader/NMI data page */
#define RGXMIPSFW_NMI_SHARED_DATA_BASE                        (0x100)
/* Size used by Debug dump data */
#define RGXMIPSFW_NMI_SHARED_SIZE                             (0x128)
/* Offsets in the NMI shared area in 32-bit words */
#define RGXMIPSFW_NMI_SYNC_FLAG_OFFSET                        (0x0)
#define RGXMIPSFW_NMI_STATE_OFFSET                            (0x1)

/*
 * MIPS fault data
 */
/* Base address of the fault data within the bootloader/NMI data page */
#define RGXMIPSFW_FAULT_DATA_BASE                             (0x280)

/* The things that follow are excluded when compiling assembly sources*/
#if !defined (RGXMIPSFW_ASSEMBLY_CODE)
#include "img_types.h"
#include "km/rgxdefs_km.h"

#define RGXMIPSFW_GET_OFFSET_IN_DWORDS(offset)                (offset / sizeof(IMG_UINT32))
#define RGXMIPSFW_GET_OFFSET_IN_QWORDS(offset)                (offset / sizeof(IMG_UINT64))

/* Used for compatibility checks */
#define RGXMIPSFW_ARCHTYPE_VER_CLRMSK                         (0xFFFFE3FFU)
#define RGXMIPSFW_ARCHTYPE_VER_SHIFT                          (10U)
#define RGXMIPSFW_CORE_ID_VALUE                               (0x001U)
#define RGXFW_PROCESSOR_MIPS		                          "MIPS"

/* microAptivAP cache line size */
#define RGXMIPSFW_MICROAPTIVEAP_CACHELINE_SIZE                (16U)

/* The SOCIF transactions are identified with the top 16 bits of the physical address emitted by the MIPS */
#define RGXMIPSFW_WRAPPER_CONFIG_REGBANK_ADDR_ALIGN           (16U)

/* Values to put in the MIPS selectors for performance counters*/
#define RGXMIPSFW_PERF_COUNT_CTRL_ICACHE_ACCESSES_C0          (9U)   /* Icache accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_ICACHE_MISSES_C1            (9U)   /* Icache misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_DCACHE_ACCESSES_C0          (10U)  /* Dcache accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_DCACHE_MISSES_C1            (11U) /* Dcache misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_ITLB_INSTR_ACCESSES_C0      (5U)  /* ITLB instruction accesses in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_JTLB_INSTR_MISSES_C1        (7U)  /* JTLB instruction accesses misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_INSTR_COMPLETED_C0          (1U)  /* Instructions completed in COUNTER0 */
#define RGXMIPSFW_PERF_COUNT_CTRL_JTLB_DATA_MISSES_C1         (8U)  /* JTLB data misses in COUNTER1 */

#define RGXMIPSFW_PERF_COUNT_CTRL_EVENT_SHIFT                 (5U)  /* Shift for the Event field in the MIPS perf ctrl registers */
/* Additional flags for performance counters. See MIPS manual for further reference*/
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_USER_MODE             (8U)
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_KERNEL_MODE           (2U)
#define RGXMIPSFW_PERF_COUNT_CTRL_COUNT_EXL                   (1U)


#define RGXMIPSFW_C0_NBHWIRQ	8

/* Macros to decode C0_Cause register */
#define RGXMIPSFW_C0_CAUSE_EXCCODE(CAUSE)       (((CAUSE) & 0x7c) >> 2)
/* Use only when Coprocessor Unusable exception */
#define RGXMIPSFW_C0_CAUSE_UNUSABLE_UNIT(CAUSE) (((CAUSE) >> 28) & 0x3)
#define RGXMIPSFW_C0_CAUSE_PENDING_HWIRQ(CAUSE) (((CAUSE) & 0x3fc00) >> 10)
#define RGXMIPSFW_C0_CAUSE_FDCIPENDING          (1 << 21)
#define RGXMIPSFW_C0_CAUSE_IV                   (1 << 23)
#define RGXMIPSFW_C0_CAUSE_IC                   (1 << 25)
#define RGXMIPSFW_C0_CAUSE_PCIPENDING           (1 << 26)
#define RGXMIPSFW_C0_CAUSE_TIPENDING            (1 << 30)

/* Macros to decode C0_Debug register */
#define RGXMIPSFW_C0_DEBUG_EXCCODE(DEBUG) (((DEBUG) >> 10) & 0x1f)
#define RGXMIPSFW_C0_DEBUG_DSS            (1 << 0)
#define RGXMIPSFW_C0_DEBUG_DBP            (1 << 1)
#define RGXMIPSFW_C0_DEBUG_DDBL           (1 << 2)
#define RGXMIPSFW_C0_DEBUG_DDBS           (1 << 3)
#define RGXMIPSFW_C0_DEBUG_DIB            (1 << 4)
#define RGXMIPSFW_C0_DEBUG_DINT           (1 << 5)
#define RGXMIPSFW_C0_DEBUG_DIBIMPR        (1 << 6)
#define RGXMIPSFW_C0_DEBUG_DDBLIMPR       (1 << 18)
#define RGXMIPSFW_C0_DEBUG_DDBSIMPR       (1 << 19)
#define RGXMIPSFW_C0_DEBUG_IEXI           (1 << 20)
#define RGXMIPSFW_C0_DEBUG_DBUSEP         (1 << 21)
#define RGXMIPSFW_C0_DEBUG_CACHEEP        (1 << 22)
#define RGXMIPSFW_C0_DEBUG_MCHECKP        (1 << 23)
#define RGXMIPSFW_C0_DEBUG_IBUSEP         (1 << 24)
#define RGXMIPSFW_C0_DEBUG_DM             (1 << 30)
#define RGXMIPSFW_C0_DEBUG_DBD            (1 << 31)

/* ELF format defines */
#define ELF_PT_LOAD     (0x1U)   /* Program header identifier as Load */
#define ELF_SHT_SYMTAB  (0x2U)   /* Section identifier as Symbol Table */
#define ELF_SHT_STRTAB  (0x3U)   /* Section identifier as String Table */
#define MAX_STRTAB_NUM  (0x8U)   /* Maximum number of string table in the firmware ELF file */


/* Redefined structs of ELF format */
typedef struct
{
	IMG_UINT8    ui32Eident[16];
	IMG_UINT16   ui32Etype;
	IMG_UINT16   ui32Emachine;
	IMG_UINT32   ui32Eversion;
	IMG_UINT32   ui32Eentry;
	IMG_UINT32   ui32Ephoff;
	IMG_UINT32   ui32Eshoff;
	IMG_UINT32   ui32Eflags;
	IMG_UINT16   ui32Eehsize;
	IMG_UINT16   ui32Ephentsize;
	IMG_UINT16   ui32Ephnum;
	IMG_UINT16   ui32Eshentsize;
	IMG_UINT16   ui32Eshnum;
	IMG_UINT16   ui32Eshtrndx;
} RGX_MIPS_ELF_HDR;


typedef struct
{
	IMG_UINT32   ui32Stname;
	IMG_UINT32   ui32Stvalue;
	IMG_UINT32   ui32Stsize;
	IMG_UINT8    ui32Stinfo;
	IMG_UINT8    ui32Stother;
	IMG_UINT16   ui32Stshndx;
} RGX_MIPS_ELF_SYM;


typedef struct
{
	IMG_UINT32   ui32Shname;
	IMG_UINT32   ui32Shtype;
	IMG_UINT32   ui32Shflags;
	IMG_UINT32   ui32Shaddr;
	IMG_UINT32   ui32Shoffset;
	IMG_UINT32   ui32Shsize;
	IMG_UINT32   ui32Shlink;
	IMG_UINT32   ui32Shinfo;
	IMG_UINT32   ui32Shaddralign;
	IMG_UINT32   ui32Shentsize;
} RGX_MIPS_ELF_SHDR;

typedef struct
{
	IMG_UINT32   ui32Ptype;
	IMG_UINT32   ui32Poffset;
	IMG_UINT32   ui32Pvaddr;
	IMG_UINT32   ui32Ppaddr;
	IMG_UINT32   ui32Pfilesz;
	IMG_UINT32   ui32Pmemsz;
	IMG_UINT32   ui32Pflags;
	IMG_UINT32   ui32Palign;
 } RGX_MIPS_ELF_PROGRAM_HDR;

#define RGXMIPSFW_TLB_GET_MASK(ENTRY_PAGE_MASK) (((ENTRY_PAGE_MASK) >> 13) & 0xffffU)
#define RGXMIPSFW_TLB_GET_VPN2(ENTRY_HI)        ((ENTRY_HI) >> 13)
#define RGXMIPSFW_TLB_GET_COHERENCY(ENTRY_LO)   (((ENTRY_LO) >> 3) & 0x7U)
#define RGXMIPSFW_TLB_GET_PFN(ENTRY_LO)         (((ENTRY_LO) >> 6) & 0xfffffU)
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
	IMG_UINT32 ui32ErrorEPC;
	IMG_UINT32 ui32StatusRegister;
	IMG_UINT32 ui32CauseRegister;
	IMG_UINT32 ui32BadRegister;
	IMG_UINT32 ui32EPC;
	IMG_UINT32 ui32SP;
	IMG_UINT32 ui32Debug;
	IMG_UINT32 ui32DEPC;
	IMG_UINT32 ui32BadInstr;
	RGX_MIPS_TLB_ENTRY asTLB[RGXMIPSFW_NUMBER_OF_TLB_ENTRIES];
} RGX_MIPS_STATE;

typedef struct {
	IMG_UINT32 ui32FaultPageEntryLo;
	IMG_UINT32 ui32BadVAddr;
	IMG_UINT32 ui32EntryLo0;
	IMG_UINT32 ui32EntryLo1;
} RGX_MIPS_FAULT_DATA;

#endif  /* RGXMIPSFW_ASSEMBLY_CODE */


#endif /*__RGX_MIPS_H__*/
