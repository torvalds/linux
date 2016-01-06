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
#include "rgxmmuinit.h"
#include "rgxmmudefs_km.h"

#include "device.h"
#include "img_types.h"
#include "mmu_common.h"
#include "pdump_mmu.h"

#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "rgx_memallocflags.h"
#include "pdump_km.h"

/*
 * Bits of PT, PD and PC not involving addresses 
 */

#define RGX_MMUCTRL_PTE_PROTMASK	(RGX_MMUCTRL_PT_DATA_PM_META_PROTECT_EN | \
									 RGX_MMUCTRL_PT_DATA_ENTRY_PENDING_EN | \
									 RGX_MMUCTRL_PT_DATA_PM_SRC_EN | \
									 RGX_MMUCTRL_PT_DATA_SLC_BYPASS_CTRL_EN | \
									 RGX_MMUCTRL_PT_DATA_CC_EN | \
									 RGX_MMUCTRL_PT_DATA_READ_ONLY_EN | \
									 RGX_MMUCTRL_PT_DATA_VALID_EN)

#define RGX_MMUCTRL_PDE_PROTMASK	(RGX_MMUCTRL_PD_DATA_ENTRY_PENDING_EN | \
									 ~RGX_MMUCTRL_PD_DATA_PAGE_SIZE_CLRMSK | \
									 RGX_MMUCTRL_PD_DATA_VALID_EN)

#define RGX_MMUCTRL_PCE_PROTMASK	(RGX_MMUCTRL_PC_DATA_ENTRY_PENDING_EN | \
									 RGX_MMUCTRL_PC_DATA_VALID_EN)





static MMU_PxE_CONFIG sRGXMMUPCEConfig;
static MMU_DEVVADDR_CONFIG sRGXMMUTopLevelDevVAddrConfig;


typedef struct _RGX_PAGESIZECONFIG_
{
	const MMU_PxE_CONFIG *psPDEConfig;
	const MMU_PxE_CONFIG *psPTEConfig;
	const MMU_DEVVADDR_CONFIG *psDevVAddrConfig;
	IMG_UINT32 uiRefCount;
	IMG_UINT32 uiMaxRefCount;
} RGX_PAGESIZECONFIG;

/*
 *
 *  Configuration for heaps with 4kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_4KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_4KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_4KBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig4KB;


/*
 *
 *  Configuration for heaps with 16kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_16KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_16KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_16KBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig16KB;


/*
 *
 *  Configuration for heaps with 64kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_64KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_64KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_64KBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig64KB;


/*
 *
 *  Configuration for heaps with 256kB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_256KBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_256KBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_256KBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig256KB;


/*
 *
 *  Configuration for heaps with 1MB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_1MBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_1MBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_1MBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig1MB;


/*
 *
 *  Configuration for heaps with 2MB Data-Page size
 *
 */

static MMU_PxE_CONFIG sRGXMMUPDEConfig_2MBDP;
static MMU_PxE_CONFIG sRGXMMUPTEConfig_2MBDP;
static MMU_DEVVADDR_CONFIG sRGXMMUDevVAddrConfig_2MBDP;
static RGX_PAGESIZECONFIG gsPageSizeConfig2MB;


/* Forward declaration of protection bits derivation functions, for
   the following structure */
static IMG_UINT64 RGXDerivePCEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize);
static IMG_UINT32 RGXDerivePCEProt4(IMG_UINT32 uiProtFlags);
static IMG_UINT64 RGXDerivePDEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize);
static IMG_UINT32 RGXDerivePDEProt4(IMG_UINT32 uiProtFlags);
static IMG_UINT64 RGXDerivePTEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize);
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

PVRSRV_ERROR RGXMMUInit_Register(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	/*
	 * Setup sRGXMMUDeviceAttributes
	 */
	sRGXMMUDeviceAttributes.eMMUType = PDUMP_MMU_TYPE_VARPAGE_40BIT;
	sRGXMMUDeviceAttributes.eTopLevel = MMU_LEVEL_3;
	sRGXMMUDeviceAttributes.ui32BaseAlign = RGX_MMUCTRL_PC_DATA_PD_BASE_ALIGNSHIFT;
	sRGXMMUDeviceAttributes.psBaseConfig = &sRGXMMUPCEConfig;
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

	/*
	 * Setup sRGXMMUPCEConfig
	 */
	sRGXMMUPCEConfig.uiBytesPerEntry = 4; /* 32 bit entries */
	sRGXMMUPCEConfig.uiAddrMask = 0xfffffff0; /* Mask to get significant address bits of PC entry */

	sRGXMMUPCEConfig.uiAddrShift = 4; /* Shift this many bits to get PD address in PC entry */
	sRGXMMUPCEConfig.uiLog2Align = 12; /* Alignment of PD AND PC */

	sRGXMMUPCEConfig.uiProtMask = RGX_MMUCTRL_PCE_PROTMASK; //Mask to get the status bits of the PC */
	sRGXMMUPCEConfig.uiProtShift = 0; /* Shift this many bits to have status bits starting with bit 0 */

	sRGXMMUPCEConfig.uiValidEnMask = RGX_MMUCTRL_PC_DATA_VALID_EN; /* Mask to get entry valid bit of the PC */
	sRGXMMUPCEConfig.uiValidEnShift = RGX_MMUCTRL_PC_DATA_VALID_SHIFT; /* Shift this many bits to have entry valid bit starting with bit 0 */

	/*
	 *  Setup sRGXMMUTopLevelDevVAddrConfig
	 */
	sRGXMMUTopLevelDevVAddrConfig.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK; /* Get the PC address bits from a 40 bit virt. address (in a 64bit UINT) */
	sRGXMMUTopLevelDevVAddrConfig.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUTopLevelDevVAddrConfig.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK; /* Get the PD address bits from a 40 bit virt. address (in a 64bit UINT) */
	sRGXMMUTopLevelDevVAddrConfig.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

/*
 *
 *  Configuration for heaps with 4kB Data-Page size
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_4KBDP
	 */
	sRGXMMUPDEConfig_4KBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_4KBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_4KBDP.uiAddrShift = 12;
	sRGXMMUPDEConfig_4KBDP.uiLog2Align = 12;

	sRGXMMUPDEConfig_4KBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_4KBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_4KBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_4KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_4KBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_4KBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_4KBDP
	 */
	sRGXMMUPTEConfig_4KBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_4KBDP.uiAddrMask = IMG_UINT64_C(0xfffffff000);
	sRGXMMUPTEConfig_4KBDP.uiAddrShift = 12;
	sRGXMMUPTEConfig_4KBDP.uiLog2Align = 12;

	sRGXMMUPTEConfig_4KBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_4KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_4KBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_4KBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_4KBDP
	 */
	sRGXMMUDevVAddrConfig_4KBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_4KBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_4KBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_4KBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_4KBDP.uiPTIndexMask = ~RGX_MMUCTRL_VADDR_PT_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_4KBDP.uiPTIndexShift = RGX_MMUCTRL_VADDR_PT_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_4KBDP.uiPageOffsetMask = IMG_UINT64_C(0x0000000fff);
	sRGXMMUDevVAddrConfig_4KBDP.uiPageOffsetShift = 0;

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
	sRGXMMUPDEConfig_16KBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_16KBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_16KBDP.uiAddrShift = 10; /* These are for a page directory ENTRY, meaning the address of a PT cropped to suit the PD */
	sRGXMMUPDEConfig_16KBDP.uiLog2Align = 10; /* Alignment of the page tables NOT directories */

	sRGXMMUPDEConfig_16KBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_16KBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_16KBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_16KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_16KBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_16KBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_16KBDP
	 */
	sRGXMMUPTEConfig_16KBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_16KBDP.uiAddrMask = IMG_UINT64_C(0xffffffc000);
	sRGXMMUPTEConfig_16KBDP.uiAddrShift = 14; /* These are for a page table ENTRY, meaning the address of a PAGE cropped to suit the PD */
	sRGXMMUPTEConfig_16KBDP.uiLog2Align = 14; /* Alignment of the pages NOT tables */

	sRGXMMUPTEConfig_16KBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_16KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_16KBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_16KBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_16KBDP
	 */
	sRGXMMUDevVAddrConfig_16KBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_16KBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_16KBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_16KBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_16KBDP.uiPTIndexMask = IMG_UINT64_C(0x00001fc000);
	sRGXMMUDevVAddrConfig_16KBDP.uiPTIndexShift = 14;

	sRGXMMUDevVAddrConfig_16KBDP.uiPageOffsetMask = IMG_UINT64_C(0x0000003fff);
	sRGXMMUDevVAddrConfig_16KBDP.uiPageOffsetShift = 0;

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
 *  Configuration for heaps with 64kB Data-Page size
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_64KBDP
	 */
	sRGXMMUPDEConfig_64KBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_64KBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_64KBDP.uiAddrShift = 8;
	sRGXMMUPDEConfig_64KBDP.uiLog2Align = 8;

	sRGXMMUPDEConfig_64KBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_64KBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_64KBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_64KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_64KBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_64KBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_64KBDP
	 */
	sRGXMMUPTEConfig_64KBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_64KBDP.uiAddrMask = IMG_UINT64_C(0xffffff0000);
	sRGXMMUPTEConfig_64KBDP.uiAddrShift =16;
	sRGXMMUPTEConfig_64KBDP.uiLog2Align = 16;

	sRGXMMUPTEConfig_64KBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_64KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_64KBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_64KBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_64KBDP
	 */
	sRGXMMUDevVAddrConfig_64KBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_64KBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_64KBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_64KBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_64KBDP.uiPTIndexMask = IMG_UINT64_C(0x00001f0000);
	sRGXMMUDevVAddrConfig_64KBDP.uiPTIndexShift = 16;

	sRGXMMUDevVAddrConfig_64KBDP.uiPageOffsetMask = IMG_UINT64_C(0x000000ffff);
	sRGXMMUDevVAddrConfig_64KBDP.uiPageOffsetShift = 0;

	/*
	 * Setup gsPageSizeConfig64KB
	 */
	gsPageSizeConfig64KB.psPDEConfig = &sRGXMMUPDEConfig_64KBDP;
	gsPageSizeConfig64KB.psPTEConfig = &sRGXMMUPTEConfig_64KBDP;
	gsPageSizeConfig64KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_64KBDP;
	gsPageSizeConfig64KB.uiRefCount = 0;
	gsPageSizeConfig64KB.uiMaxRefCount = 0;


/*
 *
 *  Configuration for heaps with 256kB Data-Page size
 *
 */

	/*
	 * Setup sRGXMMUPDEConfig_256KBDP
	 */
	sRGXMMUPDEConfig_256KBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_256KBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_256KBDP.uiAddrShift = 6;
	sRGXMMUPDEConfig_256KBDP.uiLog2Align = 6;

	sRGXMMUPDEConfig_256KBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_256KBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_256KBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_256KBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_256KBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_256KBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup MMU_PxE_CONFIG sRGXMMUPTEConfig_256KBDP
	 */
	sRGXMMUPTEConfig_256KBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_256KBDP.uiAddrMask = IMG_UINT64_C(0xfffffc0000);
	sRGXMMUPTEConfig_256KBDP.uiAddrShift = 18;
	sRGXMMUPTEConfig_256KBDP.uiLog2Align = 18;

	sRGXMMUPTEConfig_256KBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_256KBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_256KBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_256KBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_256KBDP
	 */
	sRGXMMUDevVAddrConfig_256KBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_256KBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_256KBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_256KBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_256KBDP.uiPTIndexMask = IMG_UINT64_C(0x00001c0000);
	sRGXMMUDevVAddrConfig_256KBDP.uiPTIndexShift = 18;

	sRGXMMUDevVAddrConfig_256KBDP.uiPageOffsetMask = IMG_UINT64_C(0x000003ffff);
	sRGXMMUDevVAddrConfig_256KBDP.uiPageOffsetShift = 0;

	/*
	 * Setup gsPageSizeConfig256KB
	 */
	gsPageSizeConfig256KB.psPDEConfig = &sRGXMMUPDEConfig_256KBDP;
	gsPageSizeConfig256KB.psPTEConfig = &sRGXMMUPTEConfig_256KBDP;
	gsPageSizeConfig256KB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_256KBDP;
	gsPageSizeConfig256KB.uiRefCount = 0;
	gsPageSizeConfig256KB.uiMaxRefCount = 0;

	/*
	 * Setup sRGXMMUPDEConfig_1MBDP
	 */
	sRGXMMUPDEConfig_1MBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_1MBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_1MBDP.uiAddrShift = 4;
	sRGXMMUPDEConfig_1MBDP.uiLog2Align = 4;

	sRGXMMUPDEConfig_1MBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_1MBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_1MBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_1MBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_1MBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_1MBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_1MBDP
	 */
	sRGXMMUPTEConfig_1MBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_1MBDP.uiAddrMask = IMG_UINT64_C(0xfffff00000);
	sRGXMMUPTEConfig_1MBDP.uiAddrShift = 20;
	sRGXMMUPTEConfig_1MBDP.uiLog2Align = 20;

	sRGXMMUPTEConfig_1MBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_1MBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_1MBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_1MBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_1MBDP
	 */
	sRGXMMUDevVAddrConfig_1MBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_1MBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_1MBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_1MBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_1MBDP.uiPTIndexMask = IMG_UINT64_C(0x0000100000);
	sRGXMMUDevVAddrConfig_1MBDP.uiPTIndexShift = 20;

	sRGXMMUDevVAddrConfig_1MBDP.uiPageOffsetMask = IMG_UINT64_C(0x00000fffff);
	sRGXMMUDevVAddrConfig_1MBDP.uiPageOffsetShift = 0;

	/*
	 * Setup gsPageSizeConfig1MB
	 */
	gsPageSizeConfig1MB.psPDEConfig = &sRGXMMUPDEConfig_1MBDP;
	gsPageSizeConfig1MB.psPTEConfig = &sRGXMMUPTEConfig_1MBDP;
	gsPageSizeConfig1MB.psDevVAddrConfig = &sRGXMMUDevVAddrConfig_1MBDP;
	gsPageSizeConfig1MB.uiRefCount = 0;
	gsPageSizeConfig1MB.uiMaxRefCount = 0;

	/*
	 * Setup sRGXMMUPDEConfig_2MBDP
	 */
	sRGXMMUPDEConfig_2MBDP.uiBytesPerEntry = 8;

	sRGXMMUPDEConfig_2MBDP.uiAddrMask = IMG_UINT64_C(0xfffffffff0);
	sRGXMMUPDEConfig_2MBDP.uiAddrShift = 4;
	sRGXMMUPDEConfig_2MBDP.uiLog2Align = 4;

	sRGXMMUPDEConfig_2MBDP.uiVarCtrlMask = IMG_UINT64_C(0x000000000e);
	sRGXMMUPDEConfig_2MBDP.uiVarCtrlShift = 1;

	sRGXMMUPDEConfig_2MBDP.uiProtMask = RGX_MMUCTRL_PDE_PROTMASK;
	sRGXMMUPDEConfig_2MBDP.uiProtShift = 0;

	sRGXMMUPDEConfig_2MBDP.uiValidEnMask = RGX_MMUCTRL_PD_DATA_VALID_EN;
	sRGXMMUPDEConfig_2MBDP.uiValidEnShift = RGX_MMUCTRL_PD_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUPTEConfig_2MBDP
	 */
	sRGXMMUPTEConfig_2MBDP.uiBytesPerEntry = 8;

	sRGXMMUPTEConfig_2MBDP.uiAddrMask = IMG_UINT64_C(0xffffe00000);
	sRGXMMUPTEConfig_2MBDP.uiAddrShift = 21;
	sRGXMMUPTEConfig_2MBDP.uiLog2Align = 21;

	sRGXMMUPTEConfig_2MBDP.uiProtMask = RGX_MMUCTRL_PTE_PROTMASK;
	sRGXMMUPTEConfig_2MBDP.uiProtShift = 0;

	sRGXMMUPTEConfig_2MBDP.uiValidEnMask = RGX_MMUCTRL_PT_DATA_VALID_EN;
	sRGXMMUPTEConfig_2MBDP.uiValidEnShift = RGX_MMUCTRL_PT_DATA_VALID_SHIFT;

	/*
	 * Setup sRGXMMUDevVAddrConfig_2MBDP
	 */
	sRGXMMUDevVAddrConfig_2MBDP.uiPCIndexMask = ~RGX_MMUCTRL_VADDR_PC_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_2MBDP.uiPCIndexShift = RGX_MMUCTRL_VADDR_PC_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_2MBDP.uiPDIndexMask = ~RGX_MMUCTRL_VADDR_PD_INDEX_CLRMSK;
	sRGXMMUDevVAddrConfig_2MBDP.uiPDIndexShift = RGX_MMUCTRL_VADDR_PD_INDEX_SHIFT;

	sRGXMMUDevVAddrConfig_2MBDP.uiPTIndexMask = IMG_UINT64_C(0x0000000000);
	sRGXMMUDevVAddrConfig_2MBDP.uiPTIndexShift = 21;

	sRGXMMUDevVAddrConfig_2MBDP.uiPageOffsetMask = IMG_UINT64_C(0x00001fffff);
	sRGXMMUDevVAddrConfig_2MBDP.uiPageOffsetShift = 0;

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
	sRGXMMUDeviceAttributes.eTopLevel = MMU_LEVEL_3;
	sRGXMMUDeviceAttributes.ui32BaseAlign = RGX_MMUCTRL_PC_DATA_PD_BASE_ALIGNSHIFT;
	sRGXMMUDeviceAttributes.psBaseConfig = &sRGXMMUPCEConfig;
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

	psDeviceNode->psMMUDevAttrs = &sRGXMMUDeviceAttributes;

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXMMUInit_Unregister(PVRSRV_DEVICE_NODE *psDeviceNode)
{
    PVRSRV_ERROR eError;

    eError = PVRSRV_OK;

#if defined(PDUMP)
    psDeviceNode->pfnMMUGetContextID = IMG_NULL;
#endif

    psDeviceNode->psMMUDevAttrs = IMG_NULL;

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
    return (uiProtFlags & MMU_PROTFLAGS_INVALID)?0:RGX_MMUCTRL_PC_DATA_VALID_EN;
}


/*************************************************************************/ /*!
@Function       RGXDerivePCEProt8
@Description    calculate the PCE protection flags based on an 8 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePCEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize)
{
	PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_UNREFERENCED_PARAMETER(ui8Log2PageSize);

	PVR_DPF((PVR_DBG_ERROR, "8-byte PCE not supported on this device"));
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
	PVR_DPF((PVR_DBG_ERROR, "4-byte PDE not supported on this device"));
	return 0;
}


/*************************************************************************/ /*!
@Function       RGXDerivePDEProt8
@Description    derive the PDE protection flags based on an 8 byte entry

@Input          ui8Log2PageSize The log2 of the required page size.
                E.g, for 4KiB pages, this parameter must be 12.
                For 2MiB pages, it must be set to 21.

@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePDEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize)
{
	IMG_UINT64 ret_value = 0; // 0 means invalid

    if (! (uiProtFlags & MMU_PROTFLAGS_INVALID)) // if not invalid
	{
		switch (ui8Log2PageSize)
		{
			case 12:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_4KB;
				break;
			case 14:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_16KB;
				break;
			case 16:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_64KB;
				break;
			case 18:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_256KB;
				break;
			case 20:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_1MB;
				break;
			case 21:
				ret_value = RGX_MMUCTRL_PD_DATA_VALID_EN | RGX_MMUCTRL_PD_DATA_PAGE_SIZE_2MB;
				break;
			default:
				PVR_DPF((PVR_DBG_ERROR,
						 "%s:%d: in function<%s>: Invalid parameter log2_page_size. Expected {12, 14, 16, 18, 20, 21}. Got [%u]",
						 __FILE__, __LINE__, __FUNCTION__, ui8Log2PageSize));
		}
	}
	return ret_value;
}


/*************************************************************************/ /*!
@Function       RGXDerivePTEProt4
@Description    calculate the PTE protection flags based on a 4 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT32 RGXDerivePTEProt4(IMG_UINT32 uiProtFlags)
{
    PVR_UNREFERENCED_PARAMETER(uiProtFlags);
	PVR_DPF((PVR_DBG_ERROR, "4-byte PTE not supported on this device"));

	return 0;
}

/*************************************************************************/ /*!
@Function       RGXDerivePTEProt8
@Description    calculate the PTE protection flags based on an 8 byte entry
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static IMG_UINT64 RGXDerivePTEProt8(IMG_UINT32 uiProtFlags, IMG_UINT8 ui8Log2PageSize)
{
	IMG_UINT64 ui64MMUFlags=0;

	PVR_UNREFERENCED_PARAMETER(ui8Log2PageSize);

	if(((MMU_PROTFLAGS_READABLE|MMU_PROTFLAGS_WRITEABLE) & uiProtFlags) == (MMU_PROTFLAGS_READABLE|MMU_PROTFLAGS_WRITEABLE))
	{
		/* read/write */
	}
	else if(MMU_PROTFLAGS_READABLE & uiProtFlags)
	{
		/* read only */
		ui64MMUFlags |= RGX_MMUCTRL_PT_DATA_READ_ONLY_EN;
	}
	else if(MMU_PROTFLAGS_WRITEABLE & uiProtFlags)
	{
		/* write only */
        PVR_DPF((PVR_DBG_ERROR, "RGXDerivePTEProt8: write-only is not possible on this device"));
	}
    else if ((MMU_PROTFLAGS_INVALID & uiProtFlags) == 0)
    {
        PVR_DPF((PVR_DBG_ERROR, "RGXDerivePTEProt8: neither read nor write specified..."));
    }

	/* cache coherency */
	if(MMU_PROTFLAGS_CACHE_COHERENT & uiProtFlags)
	{
		ui64MMUFlags |= RGX_MMUCTRL_PT_DATA_CC_EN;
	}

	/* cache setup */
	if ((MMU_PROTFLAGS_CACHED & uiProtFlags) == 0)
	{
		ui64MMUFlags |= RGX_MMUCTRL_PT_DATA_SLC_BYPASS_CTRL_EN;
	}

    if ((uiProtFlags & MMU_PROTFLAGS_INVALID) == 0)
    {
        ui64MMUFlags |= RGX_MMUCTRL_PT_DATA_VALID_EN;
    }

	if (MMU_PROTFLAGS_DEVICE(PMMETA_PROTECT) & uiProtFlags)
	{
		ui64MMUFlags |= RGX_MMUCTRL_PT_DATA_PM_META_PROTECT_EN;
	}

	return ui64MMUFlags;
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
    RGX_PAGESIZECONFIG *psPageSizeConfig;

    switch (uiLog2DataPageSize)
    {
    case 12:
        psPageSizeConfig = &gsPageSizeConfig4KB;
        break;
    case 14:
        psPageSizeConfig = &gsPageSizeConfig16KB;
        break;
    case 16:
        psPageSizeConfig = &gsPageSizeConfig64KB;
        break;
    case 18:
        psPageSizeConfig = &gsPageSizeConfig256KB;
        break;
    case 20:
        psPageSizeConfig = &gsPageSizeConfig1MB;
        break;
    case 21:
        psPageSizeConfig = &gsPageSizeConfig2MB;
        break;
    default:
        PVR_DPF((PVR_DBG_ERROR,
                 "RGXGetPageSizeConfigCB: Invalid Data Page Size 1<<0x%x",
                 uiLog2DataPageSize));
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
    psPageSizeConfig->uiRefCount ++;

    /* This is purely for debug statistics */
    psPageSizeConfig->uiMaxRefCount = MAX(psPageSizeConfig->uiMaxRefCount,
                                          psPageSizeConfig->uiRefCount);
#endif

    *phPriv = (IMG_HANDLE)(IMG_UINTPTR_T)uiLog2DataPageSize;
	PVR_ASSERT (uiLog2DataPageSize == (IMG_UINT32)(IMG_UINTPTR_T)*phPriv);

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
    RGX_PAGESIZECONFIG *psPageSizeConfig;
    IMG_UINT32 uiLog2DataPageSize;

    uiLog2DataPageSize = (IMG_UINT32)(IMG_UINTPTR_T) hPriv;

    switch (uiLog2DataPageSize)
    {
    case 12:
        psPageSizeConfig = &gsPageSizeConfig4KB;
        break;
    case 14:
        psPageSizeConfig = &gsPageSizeConfig16KB;
        break;
    case 16:
        psPageSizeConfig = &gsPageSizeConfig64KB;
        break;
    case 18:
        psPageSizeConfig = &gsPageSizeConfig256KB;
        break;
    case 20:
        psPageSizeConfig = &gsPageSizeConfig1MB;
        break;
    case 21:
        psPageSizeConfig = &gsPageSizeConfig2MB;
        break;
    default:
        PVR_DPF((PVR_DBG_ERROR,
                 "RGXPutPageSizeConfigCB: Invalid Data Page Size 1<<0x%x",
                 uiLog2DataPageSize));
        return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
    }

    /* Ref-count here is not especially useful, but it's an extra
       check that the API is being used correctly */
    psPageSizeConfig->uiRefCount --;
#else
    PVR_UNREFERENCED_PARAMETER(hPriv);
#endif
    return PVRSRV_OK;
}

static PVRSRV_ERROR RGXGetPageSizeFromPDE4(IMG_UINT32 ui32PDE, IMG_UINT32 *pui32Log2PageSize)
{
    PVR_UNREFERENCED_PARAMETER(ui32PDE);
    PVR_UNREFERENCED_PARAMETER(pui32Log2PageSize);
	PVR_DPF((PVR_DBG_ERROR, "4-byte PDE not supported on this device"));
	return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
}

static PVRSRV_ERROR RGXGetPageSizeFromPDE8(IMG_UINT64 ui64PDE, IMG_UINT32 *pui32Log2PageSize)
{
	switch (ui64PDE & (~RGX_MMUCTRL_PD_DATA_PAGE_SIZE_CLRMSK))
	{
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_4KB:
			*pui32Log2PageSize = 12;
			break;
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_16KB:
			*pui32Log2PageSize = 14;
			break;
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_64KB:
			*pui32Log2PageSize = 16;
			break;
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_256KB:
			*pui32Log2PageSize = 18;
			break;
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_1MB:
			*pui32Log2PageSize = 20;
			break;
		case RGX_MMUCTRL_PD_DATA_PAGE_SIZE_2MB:
			*pui32Log2PageSize = 21;
			break;
		default:
			return PVRSRV_ERROR_MMU_INVALID_PAGE_SIZE_FOR_DEVICE;
			break;
	}
	return PVRSRV_OK;
}
