/*************************************************************************/ /*!
@File
@Title          Device specific initialisation routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific MMU initialisation
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
#include "rgxmipsmmuinit.h"

#include "device.h"
#include "img_types.h"
#include "img_defs.h"
#include "mmu_common.h"
#include "pdump_mmu.h"
#include "rgxheapconfig.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "rgx_memallocflags.h"
#include "pdump_km.h"
#include "rgxdevice.h"
#include "log2.h"

/*
 * Bits of PT, PD and PC not involving addresses
 */

/* Currently there is no page directory for MIPS MMU */
#define RGX_MIPS_MMUCTRL_PDE_PROTMASK        0
/* Currently there is no page catalog for MIPS MMU */
#define RGX_MIPS_MMUCTRL_PCE_PROTMASK	     0


static MMU_PxE_CONFIG sRGXMMUPCEConfig;
static MMU_DEVVADDR_CONFIG sRGXMMUTopLevelDevVAddrConfig;


/*
 *
 *  Configuration for heaps with 4kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_4KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_4KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_4KBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig4KB;


/*
 *
 *  Configuration for heaps with 16kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_16KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_16KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_16KBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig16KB;


/*
 *
 *  Configuration for heaps with 64kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_64KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_64KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_64KBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig64KB;


/*
 *
 *  Configuration for heaps with 256kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_256KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_256KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_256KBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig256KB;


/*
 *
 *  Configuration for heaps with 1MB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_1MBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_1MBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_1MBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig1MB;


/*
 *
 *  Configuration for heaps with 2MB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_2MBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_2MBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_2MBDP;
static MMU_PAGESIZECONFIG gsPageSizeConfig2MB;


/* Forward declaration of protection bits derivation functions, for
   the following structure */
static IMG_UINT64 RGXDerivePCEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize);
static IMG_UINT32 RGXDerivePCEProt4(IMG_UINT32 uiProtFlags);
static IMG_UINT64 RGXDerivePDEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize);
static IMG_UINT32 RGXDerivePDEProt4(IMG_UINT32 uiProtFlags);
static IMG_UINT64 RGXDerivePTEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize);
static IMG_UINT32 RGXDerivePTEProt4(IMG_UINT32 uiProtFlags);

static PVRSRV_ERROR RGXGetPageSizeConfigCB(IMG_UINT32 uiLog2DataPageSize,
										   const MMU_PxE_CONFIG **ppsMMUPDEConfig,
										   const MMU_PxE_CONFIG **ppsMMUPTEConfig,
										   const MMU_DEVVADDR_CONFIG **ppsMMUDevVAddrConfig,
										   IMG_HANDLE *phPriv);

static PVRSRV_ERROR RGXPutPageSizeConfigCB(IMG_HANDLE hPriv);

static PVRSRV_ERROR RGXGetPageSizeFromPDE4(IMG_UINT32 ui32PDE, IMG_UINT32 *pui32Log2PageSize);
static PVRSRV_ERROR RGXGetPageSizeFromPDE8(IMG_UINT64 ui64PDE, IMG_UINT32 *pui32Log2PageSize);

static MMU_DEVICEATTRIBS sRGXMMUDeviceAttributes;

/* Cached policy */
static IMG_UINT32 gui32CachedPolicy;

static PVRSRV_ERROR RGXCheckTrampolineAddrs(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
                                            MMU_DEVICEATTRIBS *psDevAttrs,
                                            IMG_UINT64 *pui64Addr);

PVRSRV_ERROR RGXMipsMMUInit_Register(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	IMG_BOOL bPhysBusAbove32Bit = 0;

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, PHYS_BUS_WIDTH))
	{
		bPhysBusAbove32Bit = RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH) > 32;
	}

	sRGXMMUDeviceAttributes.pszMMUPxPDumpMemSpaceName =
		PhysHeapPDumpMemspaceName(psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_MAIN]);

	/*
	 * Setup sRGXMMUPCEConfig, no PC in MIPS MMU currently
	 */
	sRGXMMUPCEConfig.uiBytesPerEntry = 0; /* 32 bit entries */
	sRGXMMUPCEConfig.uiAddrMask = 0; /* Mask to get significant address bits of PC entry */

	sRGXMMUPCEConfig.uiAddrShift = 0; /* Shift this many bits to get PD address in PC entry */
	sRGXMMUPCEConfig.uiAddrLog2Align = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_4K; /* Alignment of PD AND PC */

	sRGXMMUPCEConfig.uiProtMask = RGX_MIPS_MMUCTRL_PCE_PROTMASK; /* Mask to get the status bits of the PC */
	sRGXMMUPCEConfig.uiProtShift = 0; /* Shift this many bits to have status bits starting with bit 0 */

	sRGXMMUPCEConfig.uiValidEnMask = RGX_MIPS_MMUCTRL_PC_DATA_VALID_EN; /* Mask to get entry valid bit of the PC */
	sRGXMMUPCEConfig.uiValidEnShift = RGX_MIPS_MMUCTRL_PC_DATA_VALID_SHIFT; /* Shift this many bits to have entry valid bit starting with bit 0 */

	/*
	 *  Setup sRGXMMUTopLevelDevVAddrConfig
	 */
	sRGXMMUTopLevelDevVAddrConfig.uiPCIndexMask = 0; /* Get the PC address bits from a 40 bit virt. address (in a 64bit UINT) */
	sRGXMMUTopLevelDevVAddrConfig.uiPCIndexShift = 0;
	sRGXMMUTopLevelDevVAddrConfig.uiNumEntriesPC = 0;

	sRGXMMUTopLevelDevVAddrConfig.uiPDIndexMask = 0; /* Get the PD address bits from a 40 bit virt. address (in a 64bit UINT) */
	sRGXMMUTopLevelDevVAddrConfig.uiPDIndexShift = 0;
	sRGXMMUTopLevelDevVAddrConfig.uiNumEntriesPD = 0;

	sRGXMMUTopLevelDevVAddrConfig.uiPTIndexMask = IMG_UINT64_C(0xfffffff000); /* Get the PT address bits from a 40 bit virt. address (in a 64bit UINT) */
	sRGXMMUTopLevelDevVAddrConfig.uiPTIndexShift = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_4K;
	sRGXMMUTopLevelDevVAddrConfig.uiNumEntriesPT = (RGX_NUM_OS_SUPPORTED << RGXMIPSFW_LOG2_PAGETABLE_SIZE_4K) >> RGXMIPSFW_LOG2_PTE_ENTRY_SIZE;

/*
 *
 *  Configuration for heaps with 4kB Data-Page size
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_4KBDP. No PD in MIPS MMU currently
	 */
	sRGXMMUPDEConfig_4KBDP.uiBytesPerEntry = 0;

	/* No PD used for MIPS */
	sRGXMMUPDEConfig_4KBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_4KBDP.uiAddrShift = 0;
	sRGXMMUPDEConfig_4KBDP.uiAddrLog2Align = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_4K;

	sRGXMMUPDEConfig_4KBDP.uiVarCtrlMask = IMG_UINT64_C(0x0);
	sRGXMMUPDEConfig_4KBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_4KBDP.uiProtMask = RGX_MIPS_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_4KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_4KBDP.uiValidEnMask = RGX_MIPS_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_4KBDP.uiValidEnShift = RGX_MIPS_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_4KBDP.
	 */
	sRGXMMUPTEConfig_4KBDP.uiBytesPerEntry = 1 << RGXMIPSFW_LOG2_PTE_ENTRY_SIZE;


	if (bPhysBusAbove32Bit)
	{
		sRGXMMUPTEConfig_4KBDP.uiAddrMask = RGXMIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT;
		gui32CachedPolicy = RGXMIPSFW_CACHED_POLICY_ABOVE_32BIT;
	}
	else
	{
		sRGXMMUPTEConfig_4KBDP.uiAddrMask = RGXMIPSFW_ENTRYLO_PFN_MASK;
		gui32CachedPolicy = RGXMIPSFW_CACHED_POLICY;
	}

	sRGXMMUPTEConfig_4KBDP.uiAddrShift = RGXMIPSFW_ENTRYLO_PFN_SHIFT;
	sRGXMMUPTEConfig_4KBDP.uiAddrLog2Align = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_4K;

	sRGXMMUPTEConfig_4KBDP.uiProtMask = RGXMIPSFW_ENTRYLO_DVG | ~RGXMIPSFW_ENTRYLO_CACHE_POLICY_CLRMSK |
	                                    RGXMIPSFW_ENTRYLO_READ_INHIBIT_EN | RGXMIPSFW_ENTRYLO_EXEC_INHIBIT_EN;
	sRGXMMUPTEConfig_4KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_4KBDP.uiValidEnMask = RGXMIPSFW_ENTRYLO_VALID_EN;
	sRGXMMUPTEConfig_4KBDP.uiValidEnShift = RGXMIPSFW_ENTRYLO_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_4KBDP
	 */
	sRGXMMUDevVAddrConfig_4KBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_4KBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_4KBDP.uiNumEntriesPC = 0;


	sRGXMMUDevVAddrConfig_4KBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_4KBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_4KBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_4KBDP.uiPTIndexMask = ~RGX_MIPS_MMUCTRL_VADDR_PT_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_4KBDP.uiPTIndexShift = RGX_MIPS_MMUCTRL_VADDR_PT_INDEX_SHIFT;
	sRGXMMUDevVAddrConfig_4KBDP.uiNumEntriesPT = (RGX_NUM_OS_SUPPORTED << RGXMIPSFW_LOG2_PAGETABLE_SIZE_4K) >> RGXMIPSFW_LOG2_PTE_ENTRY_SIZE;


	sRGXMMUDevVAddrConfig_4KBDP.uiPageOffsetMask = IMG_UINT64_C(0x0000000fff);
	sRGXMMUDevVAddrConfig_4KBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_4KBDP.uiOffsetInBytes = RGX_FIRMWARE_RAW_HEAP_BASE & IMG_UINT64_C(0x00ffffffff);

	/*
	 * Setup gsPageSizeConfig4KB
	 */
	gsPageSizeConfig4KB.psPDEConfig = &sRGXMMUPDEConfig_4KBDP;
	gsPageSizeConfig4KB.psPTEConfig = &sRGXMMUPTEConfig_4KBDP;
	gsPageSizeConfig4KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_4KBDP;
	gsPageSizeConfig4KB.uiRefCount = 0;
	gsPageSizeConfig4KB.uiMaxRefCount = 0;


/*
 *
 *  Configuration for heaps with 16kB Data-Page size
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_16KBDP
	 */
	sRGXMMUPDEConfig_16KBDP.uiBytesPerEntry = 0;

	sRGXMMUPDEConfig_16KBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_16KBDP.uiAddrShift = 0; /* These are for a page directory ENTRY, meaning the address of a PT cropped to suit the PD */
	sRGXMMUPDEConfig_16KBDP.uiAddrLog2Align = 0; /* Alignment of the page tables NOT directories */

	sRGXMMUPDEConfig_16KBDP.uiVarCtrlMask = 0;
	sRGXMMUPDEConfig_16KBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_16KBDP.uiProtMask = 0;
	sRGXMMUPDEConfig_16KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_16KBDP.uiValidEnMask = 0;
	sRGXMMUPDEConfig_16KBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUPTEConfig_16KBDP. Not supported yet
	 */
	sRGXMMUPTEConfig_16KBDP.uiBytesPerEntry = 0;

	sRGXMMUPTEConfig_16KBDP.uiAddrMask = 0;
	sRGXMMUPTEConfig_16KBDP.uiAddrShift = 0; /* These are for a page table ENTRY, meaning the address of a PAGE cropped to suit the PD */
	sRGXMMUPTEConfig_16KBDP.uiAddrLog2Align = 0; /* Alignment of the pages NOT tables */

	sRGXMMUPTEConfig_16KBDP.uiProtMask = 0;
	sRGXMMUPTEConfig_16KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_16KBDP.uiValidEnMask = 0;
	sRGXMMUPTEConfig_16KBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUDevVAddrConfig_16KBDP
	 */
	sRGXMMUDevVAddrConfig_16KBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiNumEntriesPC = 0;

	sRGXMMUDevVAddrConfig_16KBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_16KBDP.uiPTIndexMask = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiPTIndexShift = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiNumEntriesPT = 0;

	sRGXMMUDevVAddrConfig_16KBDP.uiPageOffsetMask = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_16KBDP.uiOffsetInBytes = 0;

	/*
	 * Setup gsPageSizeConfig16KB
	 */
	gsPageSizeConfig16KB.psPDEConfig = &sRGXMMUPDEConfig_16KBDP;
	gsPageSizeConfig16KB.psPTEConfig = &sRGXMMUPTEConfig_16KBDP;
	gsPageSizeConfig16KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_16KBDP;
	gsPageSizeConfig16KB.uiRefCount = 0;
	gsPageSizeConfig16KB.uiMaxRefCount = 0;


/*
 *
 *  Configuration for heaps with 64kB Data-Page size. Not supported yet
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_64KBDP
	 */
	sRGXMMUPDEConfig_64KBDP.uiBytesPerEntry = 0;

	sRGXMMUPDEConfig_64KBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_64KBDP.uiAddrShift = 0;
	sRGXMMUPDEConfig_64KBDP.uiAddrLog2Align = 0;

	sRGXMMUPDEConfig_64KBDP.uiVarCtrlMask = 0;
	sRGXMMUPDEConfig_64KBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_64KBDP.uiProtMask = 0;
	sRGXMMUPDEConfig_64KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_64KBDP.uiValidEnMask = 0;
	sRGXMMUPDEConfig_64KBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUPTEConfig_64KBDP.
	 *
	 */
	sRGXMMUPTEConfig_64KBDP.uiBytesPerEntry = 1 << RGXMIPSFW_LOG2_PTE_ENTRY_SIZE;

	if (bPhysBusAbove32Bit)
	{
		sRGXMMUPTEConfig_64KBDP.uiAddrMask = RGXMIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT;
		gui32CachedPolicy = RGXMIPSFW_CACHED_POLICY_ABOVE_32BIT;
	}
	else
	{
		sRGXMMUPTEConfig_64KBDP.uiAddrMask = RGXMIPSFW_ENTRYLO_PFN_MASK;
		gui32CachedPolicy = RGXMIPSFW_CACHED_POLICY;
	}

	/* Even while using 64K pages, MIPS still aligns addresses to 4K */
	sRGXMMUPTEConfig_64KBDP.uiAddrShift = RGXMIPSFW_ENTRYLO_PFN_SHIFT;
	sRGXMMUPTEConfig_64KBDP.uiAddrLog2Align = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_4K;

	sRGXMMUPTEConfig_64KBDP.uiProtMask = RGXMIPSFW_ENTRYLO_DVG | ~RGXMIPSFW_ENTRYLO_CACHE_POLICY_CLRMSK |
	                                     RGXMIPSFW_ENTRYLO_READ_INHIBIT_EN | RGXMIPSFW_ENTRYLO_EXEC_INHIBIT_EN;
	sRGXMMUPTEConfig_64KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_64KBDP.uiValidEnMask = RGXMIPSFW_ENTRYLO_VALID_EN;
	sRGXMMUPTEConfig_64KBDP.uiValidEnShift = RGXMIPSFW_ENTRYLO_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_64KBDP.
	 */
	sRGXMMUDevVAddrConfig_64KBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_64KBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_64KBDP.uiNumEntriesPC = 0;

	sRGXMMUDevVAddrConfig_64KBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_64KBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_64KBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_64KBDP.uiPTIndexMask = IMG_UINT64_C(0x00ffff0000);
	sRGXMMUDevVAddrConfig_64KBDP.uiPTIndexShift = (IMG_UINT32)RGXMIPSFW_LOG2_PAGE_SIZE_64K;
	sRGXMMUDevVAddrConfig_64KBDP.uiNumEntriesPT = (RGX_NUM_OS_SUPPORTED << RGXMIPSFW_LOG2_PAGETABLE_SIZE_64K) >> RGXMIPSFW_LOG2_PTE_ENTRY_SIZE;

	sRGXMMUDevVAddrConfig_64KBDP.uiPageOffsetMask = IMG_UINT64_C(0x000000ffff);
	sRGXMMUDevVAddrConfig_64KBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_64KBDP.uiOffsetInBytes = RGX_FIRMWARE_RAW_HEAP_BASE & IMG_UINT64_C(0x00ffffffff);

	/*
	 * Setup gsPageSizeConfig64KB.
	 */
	gsPageSizeConfig64KB.psPDEConfig = &sRGXMMUPDEConfig_64KBDP;
	gsPageSizeConfig64KB.psPTEConfig = &sRGXMMUPTEConfig_64KBDP;
	gsPageSizeConfig64KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_64KBDP;
	gsPageSizeConfig64KB.uiRefCount = 0;
	gsPageSizeConfig64KB.uiMaxRefCount = 0;


/*
 *
 *  Configuration for heaps with 256kB Data-Page size. Not supported yet
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_256KBDP
	 */
	sRGXMMUPDEConfig_256KBDP.uiBytesPerEntry = 0;

	sRGXMMUPDEConfig_256KBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_256KBDP.uiAddrShift = 0;
	sRGXMMUPDEConfig_256KBDP.uiAddrLog2Align = 0;

	sRGXMMUPDEConfig_256KBDP.uiVarCtrlMask = 0;
	sRGXMMUPDEConfig_256KBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_256KBDP.uiProtMask = 0;
	sRGXMMUPDEConfig_256KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_256KBDP.uiValidEnMask = 0;
	sRGXMMUPDEConfig_256KBDP.uiValidEnShift = 0;

	/*
	 * Setup MMU_PxE_CONFIG sRGXMMUPTEConfig_256KBDP
	 */
	sRGXMMUPTEConfig_256KBDP.uiBytesPerEntry = 0;

	sRGXMMUPTEConfig_256KBDP.uiAddrMask = 0;
	sRGXMMUPTEConfig_256KBDP.uiAddrShift = 0;
	sRGXMMUPTEConfig_256KBDP.uiAddrLog2Align = 0;

	sRGXMMUPTEConfig_256KBDP.uiProtMask = 0;
	sRGXMMUPTEConfig_256KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_256KBDP.uiValidEnMask = 0;
	sRGXMMUPTEConfig_256KBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUDevVAddrConfig_256KBDP
	 */
	sRGXMMUDevVAddrConfig_256KBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiNumEntriesPC = 0;

	sRGXMMUDevVAddrConfig_256KBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_256KBDP.uiPTIndexMask = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiPTIndexShift = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiNumEntriesPT = 0;

	sRGXMMUDevVAddrConfig_256KBDP.uiPageOffsetMask = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_256KBDP.uiOffsetInBytes = 0;

	/*
	 * Setup gsPageSizeConfig256KB
	 */
	gsPageSizeConfig256KB.psPDEConfig = &sRGXMMUPDEConfig_256KBDP;
	gsPageSizeConfig256KB.psPTEConfig = &sRGXMMUPTEConfig_256KBDP;
	gsPageSizeConfig256KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_256KBDP;
	gsPageSizeConfig256KB.uiRefCount = 0;
	gsPageSizeConfig256KB.uiMaxRefCount = 0;

	/*
	 * Setup sRGXMMUPDEConfig_1MBDP.  Not supported yet
	 */
	sRGXMMUPDEConfig_1MBDP.uiBytesPerEntry = 0;

	sRGXMMUPDEConfig_1MBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_1MBDP.uiAddrShift = 0;
	sRGXMMUPDEConfig_1MBDP.uiAddrLog2Align = 0;

	sRGXMMUPDEConfig_1MBDP.uiVarCtrlMask = 0;
	sRGXMMUPDEConfig_1MBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_1MBDP.uiProtMask = 0;
	sRGXMMUPDEConfig_1MBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_1MBDP.uiValidEnMask = 0;
	sRGXMMUPDEConfig_1MBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUPTEConfig_1MBDP
	 */
	sRGXMMUPTEConfig_1MBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_1MBDP.uiAddrMask = 0;
	sRGXMMUPTEConfig_1MBDP.uiAddrShift = 0;
	sRGXMMUPTEConfig_1MBDP.uiAddrLog2Align = 0;

	sRGXMMUPTEConfig_1MBDP.uiProtMask = 0;
	sRGXMMUPTEConfig_1MBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_1MBDP.uiValidEnMask = 0;
	sRGXMMUPTEConfig_1MBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUDevVAddrConfig_1MBDP
	 */
	sRGXMMUDevVAddrConfig_1MBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiNumEntriesPC = 0;

	sRGXMMUDevVAddrConfig_1MBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_1MBDP.uiPTIndexMask = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiPTIndexShift = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiNumEntriesPT = 0;

	sRGXMMUDevVAddrConfig_1MBDP.uiPageOffsetMask = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_1MBDP.uiOffsetInBytes = 0;

	/*
	 * Setup gsPageSizeConfig1MB
	 */
	gsPageSizeConfig1MB.psPDEConfig = &sRGXMMUPDEConfig_1MBDP;
	gsPageSizeConfig1MB.psPTEConfig = &sRGXMMUPTEConfig_1MBDP;
	gsPageSizeConfig1MB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_1MBDP;
	gsPageSizeConfig1MB.uiRefCount = 0;
	gsPageSizeConfig1MB.uiMaxRefCount = 0;

	/*
	 * Setup sRGXMMUPDEConfig_2MBDP. Not supported yet
	 */
	sRGXMMUPDEConfig_2MBDP.uiBytesPerEntry = 0;

	sRGXMMUPDEConfig_2MBDP.uiAddrMask = 0;
	sRGXMMUPDEConfig_2MBDP.uiAddrShift = 0;
	sRGXMMUPDEConfig_2MBDP.uiAddrLog2Align = 0;

	sRGXMMUPDEConfig_2MBDP.uiVarCtrlMask = 0;
	sRGXMMUPDEConfig_2MBDP.uiVarCtrlShift = 0;

	sRGXMMUPDEConfig_2MBDP.uiProtMask = 0;
	sRGXMMUPDEConfig_2MBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_2MBDP.uiValidEnMask = 0;
	sRGXMMUPDEConfig_2MBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUPTEConfig_2MBDP
	 */
	sRGXMMUPTEConfig_2MBDP.uiBytesPerEntry = 0;

	sRGXMMUPTEConfig_2MBDP.uiAddrMask = 0;
	sRGXMMUPTEConfig_2MBDP.uiAddrShift = 0;
	sRGXMMUPTEConfig_2MBDP.uiAddrLog2Align = 0;

	sRGXMMUPTEConfig_2MBDP.uiProtMask = 0;
	sRGXMMUPTEConfig_2MBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_2MBDP.uiValidEnMask = 0;
	sRGXMMUPTEConfig_2MBDP.uiValidEnShift = 0;

	/*
	 * Setup sRGXMMUDevVAddrConfig_2MBDP
	 */
	sRGXMMUDevVAddrConfig_2MBDP.uiPCIndexMask = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiPCIndexShift = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiNumEntriesPC = 0;

	sRGXMMUDevVAddrConfig_2MBDP.uiPDIndexMask = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiPDIndexShift = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiNumEntriesPD = 0;

	sRGXMMUDevVAddrConfig_2MBDP.uiPTIndexMask = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiPTIndexShift = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiNumEntriesPT = 0;

	sRGXMMUDevVAddrConfig_2MBDP.uiPageOffsetMask = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiPageOffsetShift = 0;
	sRGXMMUDevVAddrConfig_2MBDP.uiOffsetInBytes = 0;

	/*
	 * Setup gsPageSizeConfig2MB
	 */
	gsPageSizeConfig2MB.psPDEConfig = &sRGXMMUPDEConfig_2MBDP;
	gsPageSizeConfig2MB.psPTEConfig = &sRGXMMUPTEConfig_2MBDP;
	gsPageSizeConfig2MB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_2MBDP;
	gsPageSizeConfig2MB.uiRefCount = 0;
	gsPageSizeConfig2MB.uiMaxRefCount = 0;

	/*
	 * Setup sRGXMMUDeviceAttributes
	 */
	sRGXMMUDeviceAttributes.eMMUType = PDUMP_MMU_TYPE_MIPS_MICROAPTIV;
	sRGXMMUDeviceAttributes.eTopLevel = MMU_LEVEL_1;

	/*
	 * The page table fits in one or more big physically adjacent pages,
	 * at most as big as the page table itself.
	 * To calculate its alignment/page size, calculate the log2 size of the page
	 * table taking into account all OSes, then round that down to a valid MIPS
	 * log2 page size (12, 14, 16 for a 4K, 16K, 64K page size).
	 */
	sRGXMMUDeviceAttributes.ui32BaseAlign =
		(CeilLog2(RGX_NUM_OS_SUPPORTED) + RGXMIPSFW_LOG2_PAGETABLE_SIZE_4K) & ~1U;

	/* 256K alignment might be too hard to achieve, fall back to 64K */
	sRGXMMUDeviceAttributes.ui32BaseAlign =
		MIN(sRGXMMUDeviceAttributes.ui32BaseAlign, RGXMIPSFW_LOG2_PAGE_SIZE_64K);



	/* The base configuration is set to 4kB pages*/
	sRGXMMUDeviceAttributes.psBaseConfig = &sRGXMMUPTEConfig_4KBDP;
	sRGXMMUDeviceAttributes.psTopLevelDevVAddrConfig = &sRGXMMUTopLevelDevVAddrConfig;

	/* Functions for deriving page table/dir/cat protection bits */
	sRGXMMUDeviceAttributes.pfnDerivePCEProt8 = RGXDerivePCEProt8;
	sRGXMMUDeviceAttributes.pfnDerivePCEProt4 = RGXDerivePCEProt4;
	sRGXMMUDeviceAttributes.pfnDerivePDEProt8 = RGXDerivePDEProt8;
	sRGXMMUDeviceAttributes.pfnDerivePDEProt4 = RGXDerivePDEProt4;
	sRGXMMUDeviceAttributes.pfnDerivePTEProt8 = RGXDerivePTEProt8;
	sRGXMMUDeviceAttributes.pfnDerivePTEProt4 = RGXDerivePTEProt4;

	/* Functions for establishing configurations for PDE/PTE/DEVVADDR
	   on per-heap basis */
	sRGXMMUDeviceAttributes.pfnGetPageSizeConfiguration = RGXGetPageSizeConfigCB;
	sRGXMMUDeviceAttributes.pfnPutPageSizeConfiguration = RGXPutPageSizeConfigCB;

	sRGXMMUDeviceAttributes.pfnGetPageSizeFromPDE4 = RGXGetPageSizeFromPDE4;
	sRGXMMUDeviceAttributes.pfnGetPageSizeFromPDE8 = RGXGetPageSizeFromPDE8;

	psDeviceNode->psFirmwareMMUDevAttrs = &sRGXMMUDeviceAttributes;

	psDeviceNode->pfnValidateOrTweakPhysAddrs = RGXCheckTrampolineAddrs;

	return PVRSRV_OK;
}

static PVRSRV_ERROR RGXCheckTrampolineAddrs(struct _PVRSRV_DEVICE_NODE_ *psDevNode,
                                            MMU_DEVICEATTRIBS *psDevAttrs,
                                            IMG_UINT64 *pui64Addr)
{
	if (PVRSRV_IS_FEATURE_SUPPORTED(psDevNode, MIPS))
	{
		/*
		 * If mapping for the MIPS FW context, check for sensitive PAs
		 */
		if (psDevAttrs == psDevNode->psFirmwareMMUDevAttrs)
		{
			PVRSRV_RGXDEV_INFO *psDevice = (PVRSRV_RGXDEV_INFO *)psDevNode->pvDevice;

			if (RGXMIPSFW_SENSITIVE_ADDR(*pui64Addr))
			{
				*pui64Addr = psDevice->psTrampoline->sPhysAddr.uiAddr + RGXMIPSFW_TRAMPOLINE_OFFSET(*pui64Addr);
			}
			/* FIX_HW_BRN_63553 is mainlined for all MIPS cores */
			else if (*pui64Addr == 0x0 && !psDevice->sLayerParams.bDevicePA0IsValid)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s attempt to map addr 0x0 in the FW but 0x0 is not considered valid.", __func__));
				return PVRSRV_ERROR_MMU_FAILED_TO_MAP_PAGE_TABLE;
			}
		}
	}
	return PVRSRV_OK;
}

PVRSRV_ERROR RGXMipsMMUInit_Unregister(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	eError = PVRSRV_OK;

#if defined(PDUMP)
	psDeviceNode->pfnMMUGetContextID = NULL;
#endif

	psDeviceNode->psFirmwareMMUDevAttrs = NULL;

#if defined(DEBUG)
	PVR_DPF((PVR_DBG_MESSAGE, "Variable Page Size Heap Stats:"));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 4K page heaps: %d",
			 gsPageSizeConfig4KB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 4K page heaps (should be 0): %d",
			 gsPageSizeConfig4KB.uiRefCount));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 16K page heaps: %d",
			 gsPageSizeConfig16KB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 16K page heaps (should be 0): %d",
			 gsPageSizeConfig16KB.uiRefCount));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 64K page heaps: %d",
			 gsPageSizeConfig64KB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 64K page heaps (should be 0): %d",
			 gsPageSizeConfig64KB.uiRefCount));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 256K page heaps: %d",
			 gsPageSizeConfig256KB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 256K page heaps (should be 0): %d",
			 gsPageSizeConfig256KB.uiRefCount));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 1M page heaps: %d",
			 gsPageSizeConfig1MB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 1M page heaps (should be 0): %d",
			 gsPageSizeConfig1MB.uiRefCount));
	PVR_DPF((PVR_DBG_MESSAGE, "Max 2M page heaps: %d",
			 gsPageSizeConfig2MB.uiMaxRefCount));
	PVR_DPF((PVR_DBG_VERBOSE, "Current 2M page heaps (should be 0): %d",
			 gsPageSizeConfig2MB.uiRefCount));
#endif
	if (gsPageSizeConfig4KB.uiRefCount > 0 ||
		gsPageSizeConfig16KB.uiRefCount > 0 ||
		gsPageSizeConfig64KB.uiRefCount > 0 ||
		gsPageSizeConfig256KB.uiRefCount > 0 ||
		gsPageSizeConfig1MB.uiRefCount > 0 ||
		gsPageSizeConfig2MB.uiRefCount > 0
		)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXMMUInit_Unregister: Unbalanced MMU API Usage (Internal error)"));
	}

	return eError;
}

/*************************************************************************/ /*!
@Function       RGXDerivePCEProt4
@Description    calculate the PCE protection flags based on a 4 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT32 RGXDerivePCEProt4(IMG_UINT32 uiProtFlags)
{
	PVR_DPF((PVR_DBG_ERROR, "Page Catalog not supported on MIPS MMU"));
	return 0;
}


/*************************************************************************/ /*!
@Function       RGXDerivePCEProt8
@Description    calculate the PCE protection flags based on an 8 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePCEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize)
{
	PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_UNREFERENCED_PARAMETER(uiLog2DataPageSize);

	PVR_DPF((PVR_DBG_ERROR, "Page Catalog not supported on MIPS MMU"));
	return 0;
}


/*************************************************************************/ /*!
@Function       RGXDerivePDEProt4
@Description    derive the PDE protection flags based on a 4 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT32 RGXDerivePDEProt4(IMG_UINT32 uiProtFlags)
{
	PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_DPF((PVR_DBG_ERROR, "Page Directory not supported on MIPS MMU"));
	return 0;
}


/*************************************************************************/ /*!
@Function       RGXDerivePDEProt8
@Description    derive the PDE protection flags based on an 8 byte entry

@Input          uiLog2DataPageSize The log2 of the required page size.
				E.g, for 4KiB pages, this parameter must be 12.
				For 2MiB pages, it must be set to 21.

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePDEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize)
{
	PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_DPF((PVR_DBG_ERROR, "Page Directory not supported on MIPS MMU"));
	return 0;
}


/*************************************************************************/ /*!
@Function       RGXDerivePTEProt4
@Description    calculate the PTE protection flags based on a 4 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT32 RGXDerivePTEProt4(IMG_UINT32 uiProtFlags)
{
	IMG_UINT32 ui32MMUFlags = 0;

	if (((MMU_PROTFLAGS_READABLE|MMU_PROTFLAGS_WRITEABLE) & uiProtFlags) == (MMU_PROTFLAGS_READABLE|MMU_PROTFLAGS_WRITEABLE))
	{
		/* read/write */
		ui32MMUFlags |= RGXMIPSFW_ENTRYLO_DIRTY_EN;
	}
	else if (MMU_PROTFLAGS_READABLE & uiProtFlags)
	{
		/* read only */
	}
	else if (MMU_PROTFLAGS_WRITEABLE & uiProtFlags)
	{
		/* write only */
		ui32MMUFlags |= RGXMIPSFW_ENTRYLO_READ_INHIBIT_EN;
	}
	else if ((MMU_PROTFLAGS_INVALID & uiProtFlags) == 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDerivePTEProt4: neither read nor write specified..."));
	}

	/* cache coherency */
	if (MMU_PROTFLAGS_CACHE_COHERENT & uiProtFlags)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXDerivePTEProt4: cache coherency not supported for MIPS caches"));
	}

	/* cache setup */
	if ((MMU_PROTFLAGS_CACHED & uiProtFlags) == 0)
	{
		ui32MMUFlags |= RGXMIPSFW_ENTRYLO_UNCACHED;
	}
	else
	{
		ui32MMUFlags |= gui32CachedPolicy <<
		                RGXMIPSFW_ENTRYLO_CACHE_POLICY_SHIFT;
	}

	if ((uiProtFlags & MMU_PROTFLAGS_INVALID) == 0)
	{
		ui32MMUFlags |= RGXMIPSFW_ENTRYLO_VALID_EN;
		ui32MMUFlags |= RGXMIPSFW_ENTRYLO_GLOBAL_EN;
	}

	if (MMU_PROTFLAGS_DEVICE(PMMETA_PROTECT) & uiProtFlags)
	{
		/* PVR_DPF((PVR_DBG_WARNING, "RGXDerivePTEProt4: PMMETA Protect not existent for MIPS, option discarded")); */
	}

	return ui32MMUFlags;
}

/*************************************************************************/ /*!
@Function       RGXDerivePTEProt8
@Description    calculate the PTE protection flags based on an 8 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePTEProt8(IMG_UINT32 uiProtFlags, IMG_UINT32 uiLog2DataPageSize)
{
	PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_UNREFERENCED_PARAMETER(uiLog2DataPageSize);

	PVR_DPF((PVR_DBG_ERROR, "8-byte PTE not supported on this device"));

	return 0;
}


/*************************************************************************/ /*!
@Function       RGXGetPageSizeConfig
@Description    Set up configuration for variable sized data pages.
				RGXPutPageSizeConfigCB has to be called to ensure correct
				refcounting.
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXGetPageSizeConfigCB(IMG_UINT32 uiLog2DataPageSize,
										   const MMU_PxE_CONFIG **ppsMMUPDEConfig,
										   const MMU_PxE_CONFIG **ppsMMUPTEConfig,
										   const MMU_DEVVADDR_CONFIG **ppsMMUDevVAddrConfig,
										   IMG_HANDLE *phPriv)
{
	MMU_PAGESIZECONFIG *psPageSizeConfig;

	switch (uiLog2DataPageSize)
	{
	case RGXMIPSFW_LOG2_PAGE_SIZE_64K:
		psPageSizeConfig = &gsPageSizeConfig64KB;
		break;
	case RGXMIPSFW_LOG2_PAGE_SIZE_4K:
		psPageSizeConfig = &gsPageSizeConfig4KB;
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXGetPageSizeConfigCB: Invalid Data Page Size 1<<0x%x",
				 uiLog2DataPageSize));
		*phPriv = NULL;
		return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
	}

	/* Refer caller's pointers to the data */
	*ppsMMUPDEConfig = psPageSizeConfig->psPDEConfig;
	*ppsMMUPTEConfig = psPageSizeConfig->psPTEConfig;
	*ppsMMUDevVAddrConfig = psPageSizeConfig->psDevVAddrConfig;

#if defined(SUPPORT_MMU_PAGESIZECONFIG_REFCOUNT)
	/* Increment ref-count - not that we're allocating anything here
	   (I'm using static structs), but one day we might, so we want
	   the Get/Put code to be balanced properly */
	psPageSizeConfig->uiRefCount++;

	/* This is purely for debug statistics */
	psPageSizeConfig->uiMaxRefCount = MAX(psPageSizeConfig->uiMaxRefCount,
										  psPageSizeConfig->uiRefCount);
#endif

	*phPriv = (IMG_HANDLE)(uintptr_t)uiLog2DataPageSize;
	PVR_ASSERT (uiLog2DataPageSize == (IMG_UINT32)(uintptr_t)*phPriv);

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       RGXPutPageSizeConfig
@Description    Tells this code that the mmu module is done with the
				configurations set in RGXGetPageSizeConfig.  This can
				be a no-op.
				Called after RGXGetPageSizeConfigCB.
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXPutPageSizeConfigCB(IMG_HANDLE hPriv)
{
#if defined(SUPPORT_MMU_PAGESIZECONFIG_REFCOUNT)
	MMU_PAGESIZECONFIG *psPageSizeConfig;
	IMG_UINT32 uiLog2DataPageSize;

	uiLog2DataPageSize = (IMG_UINT32)(uintptr_t) hPriv;

	switch (uiLog2DataPageSize)
	{
	case RGXMIPSFW_LOG2_PAGE_SIZE_64K:
		psPageSizeConfig = &gsPageSizeConfig64KB;
		break;
	case RGXMIPSFW_LOG2_PAGE_SIZE_4K:
		psPageSizeConfig = &gsPageSizeConfig4KB;
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXPutPageSizeConfigCB: Invalid Data Page Size 1<<0x%x",
				 uiLog2DataPageSize));
		return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
	}

	/* Ref-count here is not especially useful, but it's an extra
	   check that the API is being used correctly */
	psPageSizeConfig->uiRefCount--;
#else
	PVR_UNREFERENCED_PARAMETER(hPriv);
#endif
	return PVRSRV_OK;
}

static PVRSRV_ERROR RGXGetPageSizeFromPDE4(IMG_UINT32 ui32PDE, IMG_UINT32 *pui32Log2PageSize)
{
	PVR_UNREFERENCED_PARAMETER(ui32PDE);
	PVR_UNREFERENCED_PARAMETER(pui32Log2PageSize);
	PVR_DPF((PVR_DBG_ERROR, "PDE not supported on MIPS"));
	return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
}

static PVRSRV_ERROR RGXGetPageSizeFromPDE8(IMG_UINT64 ui64PDE, IMG_UINT32 *pui32Log2PageSize)
{
	PVR_UNREFERENCED_PARAMETER(ui64PDE);
	PVR_UNREFERENCED_PARAMETER(pui32Log2PageSize);
	PVR_DPF((PVR_DBG_ERROR, "PDE not supported on MIPS"));
	return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
}
