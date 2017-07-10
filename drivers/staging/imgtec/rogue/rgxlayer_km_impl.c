/*************************************************************************/ /*!
@File
@Title          DDK implementation of the Services abstraction layer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    DDK implementation of the Services abstraction layer
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

#if defined (PDUMP)
#include <stdarg.h>
#endif

#include "rgxlayer_km_impl.h"
#include "pdump_km.h"
#include "devicemem_utils.h"
#include "pvrsrv.h"
#include "rgxdevice.h"
#include "rgxfwutils.h"

void RGXWriteReg32(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if( !(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
#endif
	{
		OSWriteHWReg32(pvRegsBase, ui32RegAddr, ui32RegValue);
	}

	PDUMPREG32(RGX_PDUMPREG_NAME, ui32RegAddr, ui32RegValue, psPowerParams->ui32PdumpFlags);
}

void RGXWriteReg64(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT64 ui64RegValue)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if( !(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
#endif
	{
		OSWriteHWReg64(pvRegsBase, ui32RegAddr, ui64RegValue);
	}

	PDUMPREG64(RGX_PDUMPREG_NAME, ui32RegAddr, ui64RegValue, psPowerParams->ui32PdumpFlags);
}

IMG_UINT32 RGXReadReg32(const void *hPrivate, IMG_UINT32 ui32RegAddr)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;
	IMG_UINT32 ui32RegValue;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui32RegValue = IMG_UINT32_MAX;
	}
	else
#endif
	{
		ui32RegValue = OSReadHWReg32(pvRegsBase, ui32RegAddr);
	}

	PDUMPREGREAD32(RGX_PDUMPREG_NAME, ui32RegAddr, psPowerParams->ui32PdumpFlags);

	return ui32RegValue;
}

IMG_UINT64 RGXReadReg64(const void *hPrivate, IMG_UINT32 ui32RegAddr)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;
	IMG_UINT64 ui64RegValue;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui64RegValue = IMG_UINT64_MAX;
	}
	else
#endif
	{
		ui64RegValue = OSReadHWReg64(pvRegsBase, ui32RegAddr);
	}

	PDUMPREGREAD64(RGX_PDUMPREG_NAME, ui32RegAddr, PDUMP_FLAGS_CONTINUOUS);

	return ui64RegValue;
}

PVRSRV_ERROR RGXPollReg32(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT32 ui32RegValue,
                          IMG_UINT32 ui32RegMask)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if( !(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
#endif
	{
		if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)pvRegsBase + ui32RegAddr),
		                         ui32RegValue,
		                         ui32RegMask) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg32: Poll for Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}
	}

	PDUMPREGPOL(RGX_PDUMPREG_NAME,
	            ui32RegAddr,
	            ui32RegValue,
	            ui32RegMask,
	            psPowerParams->ui32PdumpFlags,
	            PDUMP_POLL_OPERATOR_EQUAL);

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXPollReg64(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT64 ui64RegValue,
                          IMG_UINT64 ui64RegMask)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void *pvRegsBase;

	/* Split lower and upper words */
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64RegValue >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64RegValue);
	IMG_UINT32 ui32UpperMask = (IMG_UINT32) (ui64RegMask >> 32);
	IMG_UINT32 ui32LowerMask = (IMG_UINT32) (ui64RegMask);

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if( !(psPowerParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW) )
#endif
	{
		if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)pvRegsBase + ui32RegAddr + 4),
		                         ui32UpperValue,
		                         ui32UpperMask) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg64: Poll for upper part of Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}

		if (PVRSRVPollForValueKM((IMG_UINT32 *)((IMG_UINT8*)pvRegsBase + ui32RegAddr),
		                         ui32LowerValue,
		                         ui32LowerMask) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg64: Poll for upper part of Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}
	}

	PDUMPREGPOL(RGX_PDUMPREG_NAME,
	            ui32RegAddr + 4,
	            ui32UpperValue,
	            ui32UpperMask,
	            psPowerParams->ui32PdumpFlags,
	            PDUMP_POLL_OPERATOR_EQUAL);


	PDUMPREGPOL(RGX_PDUMPREG_NAME,
	            ui32RegAddr,
	            ui32LowerValue,
	            ui32LowerMask,
	            psPowerParams->ui32PdumpFlags,
	            PDUMP_POLL_OPERATOR_EQUAL);

	return PVRSRV_OK;
}

void RGXWaitCycles(const void *hPrivate, IMG_UINT32 ui32Cycles, IMG_UINT32 ui32TimeUs)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	OSWaitus(ui32TimeUs);
	PDUMPIDLWITHFLAGS(ui32Cycles, PDUMP_FLAGS_CONTINUOUS);
}

void RGXCommentLogPower(const void *hPrivate, const IMG_CHAR *pszString, ...)
{
#if defined(PDUMP)
	va_list argList;
	va_start(argList, pszString);
	PDumpCommentWithFlagsVA(PDUMP_FLAGS_CONTINUOUS, pszString, argList);
	va_end(argList);
	PVR_UNREFERENCED_PARAMETER(hPrivate);
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	PVR_UNREFERENCED_PARAMETER(pszString);
#endif
}


void RGXAcquireKernelMMUPC(const void *hPrivate, IMG_DEV_PHYADDR *psPCAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psPCAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sPCAddr;
}

#if defined(PDUMP)

void RGXWriteKernelMMUPC64(const void *hPrivate,
                           IMG_UINT32 ui32PCReg,
                           IMG_UINT32 ui32PCRegAlignShift,
                           IMG_UINT32 ui32PCRegShift,
                           IMG_UINT64 ui64PCVal)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Write the cat-base address */
	OSWriteHWReg64(psDevInfo->pvRegsBaseKM, ui32PCReg, ui64PCVal);

	/* Pdump catbase address */
	MMU_PDumpWritePageCatBase(psDevInfo->psKernelMMUCtx,
	                          RGX_PDUMPREG_NAME,
	                          ui32PCReg,
	                          8,
	                          ui32PCRegAlignShift,
	                          ui32PCRegShift,
	                          PDUMP_FLAGS_CONTINUOUS);
}

void RGXWriteKernelMMUPC32(const void *hPrivate,
                           IMG_UINT32 ui32PCReg,
                           IMG_UINT32 ui32PCRegAlignShift,
                           IMG_UINT32 ui32PCRegShift,
                           IMG_UINT32 ui32PCVal)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Write the cat-base address */
	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32PCReg, ui32PCVal);

	/* Pdump catbase address */
	MMU_PDumpWritePageCatBase(psDevInfo->psKernelMMUCtx,
	                          RGX_PDUMPREG_NAME,
	                          ui32PCReg,
	                          4,
	                          ui32PCRegAlignShift,
	                          ui32PCRegShift,
	                          PDUMP_FLAGS_CONTINUOUS);
}

#endif /* defined(PDUMP) */


void RGXAcquireGPURegsAddr(const void *hPrivate, IMG_DEV_PHYADDR *psGPURegsAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psGPURegsAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sGPURegAddr;
}

#if defined(PDUMP)
void RGXMIPSWrapperConfig(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT64 ui64GPURegsAddr,
                          IMG_UINT32 ui32GPURegsAlign,
                          IMG_UINT32 ui32BootMode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	OSWriteHWReg64(psDevInfo->pvRegsBaseKM,
	               ui32RegAddr,
	               (ui64GPURegsAddr >> ui32GPURegsAlign) | ui32BootMode);

	/* Store register offset to temp PDump variable */
	PDumpRegLabelToInternalVar(RGX_PDUMPREG_NAME, ui32RegAddr, ":SYSMEM:$1", PDUMP_FLAGS_CONTINUOUS);

	/* Align register transactions identifier */
	PDumpWriteVarSHRValueOp(":SYSMEM:$1", ui32GPURegsAlign, PDUMP_FLAGS_CONTINUOUS);

	/* Enable micromips instruction encoding */
	PDumpWriteVarORValueOp(":SYSMEM:$1", ui32BootMode, PDUMP_FLAGS_CONTINUOUS);

	/* Do the actual register write */
	PDumpInternalVarToReg64(RGX_PDUMPREG_NAME, ui32RegAddr, ":SYSMEM:$1", 0);
}
#endif

void RGXAcquireBootRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psBootRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psBootRemapAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sBootRemapAddr;
}

void RGXAcquireCodeRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psCodeRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psCodeRemapAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sCodeRemapAddr;
}

void RGXAcquireDataRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psDataRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psDataRemapAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sDataRemapAddr;
}

void RGXAcquireTrampolineRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psTrampolineRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psTrampolineRemapAddr = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->sTrampolineRemapAddr;
}

#if defined(PDUMP)
static inline
void RGXWriteRemapConfig2Reg(void *pvRegs,
                             PMR *psPMR,
                             IMG_DEVMEM_OFFSET_T uiLogicalOffset,
                             IMG_UINT32 ui32RegAddr,
                             IMG_UINT64 ui64PhyAddr,
                             IMG_UINT64 ui64PhyMask,
                             IMG_UINT64 ui64Settings)
{
	OSWriteHWReg64(pvRegs, ui32RegAddr, (ui64PhyAddr & ui64PhyMask) | ui64Settings);

	/* Store memory offset to temp PDump variable */
	PDumpMemLabelToInternalVar(":SYSMEM:$1", psPMR, uiLogicalOffset, PDUMP_FLAGS_CONTINUOUS);

	/* Keep only the relevant bits of the output physical address */
	PDumpWriteVarANDValueOp(":SYSMEM:$1", ui64PhyMask, PDUMP_FLAGS_CONTINUOUS);

	/* Extra settings for this remapped region */
	PDumpWriteVarORValueOp(":SYSMEM:$1", ui64Settings, PDUMP_FLAGS_CONTINUOUS);

	/* Do the actual register write */
	PDumpInternalVarToReg32(RGX_PDUMPREG_NAME, ui32RegAddr, ":SYSMEM:$1", PDUMP_FLAGS_CONTINUOUS);
}

void RGXBootRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32BootRemapMemOffset = RGXMIPSFW_BOOT_NMI_CODE_BASE_PAGE * (IMG_UINT32)RGXMIPSFW_PAGE_SIZE;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Write remap config1 register */
	RGXWriteReg64(hPrivate,
	              ui32Config1RegAddr,
	              ui64Config1RegValue);

	/* Write remap config2 register */
	RGXWriteRemapConfig2Reg(psDevInfo->pvRegsBaseKM,
	                        psDevInfo->psRGXFWCodeMemDesc->psImport->hPMR,
	                        psDevInfo->psRGXFWCodeMemDesc->uiOffset + ui32BootRemapMemOffset,
	                        ui32Config2RegAddr,
	                        ui64Config2PhyAddr,
	                        ui64Config2PhyMask,
	                        ui64Config2Settings);
}

void RGXCodeRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32CodeRemapMemOffset = RGXMIPSFW_EXCEPTIONSVECTORS_BASE_PAGE * (IMG_UINT32)RGXMIPSFW_PAGE_SIZE;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Write remap config1 register */
	RGXWriteReg64(hPrivate,
	              ui32Config1RegAddr,
	              ui64Config1RegValue);

	/* Write remap config2 register */
	RGXWriteRemapConfig2Reg(psDevInfo->pvRegsBaseKM,
	                        psDevInfo->psRGXFWCodeMemDesc->psImport->hPMR,
	                        psDevInfo->psRGXFWCodeMemDesc->uiOffset + ui32CodeRemapMemOffset,
	                        ui32Config2RegAddr,
	                        ui64Config2PhyAddr,
	                        ui64Config2PhyMask,
	                        ui64Config2Settings);
}

void RGXDataRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32DataRemapMemOffset = RGXMIPSFW_BOOT_NMI_DATA_BASE_PAGE * (IMG_UINT32)RGXMIPSFW_PAGE_SIZE;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Write remap config1 register */
	RGXWriteReg64(hPrivate,
	              ui32Config1RegAddr,
	              ui64Config1RegValue);

	/* Write remap config2 register */
	RGXWriteRemapConfig2Reg(psDevInfo->pvRegsBaseKM,
	                        psDevInfo->psRGXFWDataMemDesc->psImport->hPMR,
	                        psDevInfo->psRGXFWDataMemDesc->uiOffset + ui32DataRemapMemOffset,
	                        ui32Config2RegAddr,
	                        ui64Config2PhyAddr,
	                        ui64Config2PhyMask,
	                        ui64Config2Settings);
}
#endif



#define MAX_NUM_COHERENCY_TESTS  (10)
IMG_BOOL RGXDoFWSlaveBoot(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_CONFIG *psDevConfig;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	if (psDevInfo->ui32CoherencyTestsDone >= MAX_NUM_COHERENCY_TESTS)
	{
		return IMG_FALSE;
	}

	psDevConfig = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevConfig;

	return PVRSRVSystemSnoopingOfCPUCache(psDevConfig);
}

PVRSRV_ERROR RGXIOCoherencyTest(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	DEVMEM_MEMDESC *psIOCoherencyTestMemDesc;
	IMG_UINT32 *pui32CpuVirtAddr;
	RGXFWIF_DEV_VIRTADDR sCoherencyTestBuffer;
	IMG_DEVMEM_SIZE_T uiCoherencyBlockSize = sizeof(IMG_UINT64);
	IMG_DEVMEM_ALIGN_T uiCoherencyBlockAlign = sizeof(IMG_UINT64);
	IMG_UINT32 ui32SLCCTRL;
	IMG_UINT32 ui32TestNum;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_POWER_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* Size and align are 'expanded' because we request an export align allocation */
	DevmemExportalignAdjustSizeAndAlign(DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareHeap),
	                                    &uiCoherencyBlockSize,
	                                    &uiCoherencyBlockAlign);

	/* Allocate, acquire cpu address and set firmware address */
	eError = DevmemFwAllocateExportable(psDevInfo->psDeviceNode,
	                                    uiCoherencyBlockSize,
	                                    uiCoherencyBlockAlign,
	                                    PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
	                                    PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
	                                    PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
	                                    PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT |
	                                    PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT |
	                                    PVRSRV_MEMALLOCFLAG_GPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE,
	                                    "FwExIoCoherencyTestBuffer",
	                                    &psIOCoherencyTestMemDesc);
	PVR_ASSERT(eError == PVRSRV_OK);

	eError = DevmemAcquireCpuVirtAddr(psIOCoherencyTestMemDesc,
	                                  (void **) &pui32CpuVirtAddr);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Create a FW address which is uncached in the Meta DCache and in the SLC
	 * using the Meta bootloader segment.
	 * This segment is the only one configured correctly out of reset
	 * (when this test is meant to be executed).
	 */
	{
		RGXSetFirmwareAddress(&sCoherencyTestBuffer,
		                      psIOCoherencyTestMemDesc,
		                      0,
		                      RFW_FWADDR_FLAG_NONE);

		/* Undo most of the FW mappings done by RGXSetFirmwareAddress */
		sCoherencyTestBuffer.ui32Addr &= ~RGXFW_SEGMMU_DATA_META_CACHE_MASK;
		sCoherencyTestBuffer.ui32Addr &= ~RGXFW_SEGMMU_DATA_VIVT_SLC_CACHE_MASK;
		sCoherencyTestBuffer.ui32Addr -= RGXFW_SEGMMU_DATA_BASE_ADDRESS;

		/* Map the buffer in the bootloader segment as uncached */
		sCoherencyTestBuffer.ui32Addr |= RGXFW_BOOTLDR_META_ADDR;
		sCoherencyTestBuffer.ui32Addr |= RGXFW_SEGMMU_DATA_META_UNCACHED;
	}

	/* Bypass the SLC when IO coherency is enabled */
	ui32SLCCTRL = RGXReadReg32(hPrivate, RGX_CR_SLC_CTRL_BYPASS);
	RGXWriteReg32(hPrivate,
	              RGX_CR_SLC_CTRL_BYPASS,
	              ui32SLCCTRL | RGX_CR_SLC_CTRL_BYPASS_BYP_CC_EN);

	for (ui32TestNum = 1; ui32TestNum < 3; ui32TestNum++)
	{
		IMG_UINT32 i;
		IMG_BOOL bPassed = IMG_TRUE;

		PVR_LOG(("Startup I/O Coherency Test [pass #%u]", ui32TestNum));

		for (i = 0; i < uiCoherencyBlockSize/sizeof(IMG_UINT32); i++)
		{
			IMG_UINT32 ui32FWAddr, ui32FWValue;
			PVRSRV_ERROR eError2;

			/* Ensures line is in dcache */
			ui32FWValue = pui32CpuVirtAddr[i];

			/* Dirty allocation in dcache */
			pui32CpuVirtAddr[i] = i + ui32TestNum;

			/* Flush possible cpu store-buffer(ing) */
			OSWriteMemoryBarrier();

			/* Read back value using RGX slave-port interface */
			ui32FWAddr = sCoherencyTestBuffer.ui32Addr + (i * sizeof(IMG_UINT32));

			eError2 = RGXReadMETAAddr(psDevInfo, ui32FWAddr, &ui32FWValue);

			if (eError2 != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXReadWithSP error: %s",
				         PVRSRVGetErrorStringKM(eError2)));
			}

			/* Compare to see if I/O coherency worked */
			if (pui32CpuVirtAddr[i] != ui32FWValue)
			{
				PVR_DPF((PVR_DBG_ERROR, "Expected: %x, Got: %x",
				         pui32CpuVirtAddr[i], ui32FWValue));

				bPassed = IMG_FALSE;
				eError = PVRSRV_ERROR_INIT_FAILURE;
			}
		}

		PVR_LOG(("I/O Coherency Test [pass #%u] completed, Passed? %d",
		         ui32TestNum, bPassed));
	}

	/* Restore SLC bypass settings */
	RGXWriteReg32(hPrivate, RGX_CR_SLC_CTRL_BYPASS, ui32SLCCTRL);

	RGXUnsetFirmwareAddress(psIOCoherencyTestMemDesc);
	DevmemReleaseCpuVirtAddr(psIOCoherencyTestMemDesc);
	DevmemFwFree(psDevInfo, psIOCoherencyTestMemDesc);

	if (eError == PVRSRV_OK)
	{
		PVR_LOG(("I/O Coherency Test succeeded"));
		psDevInfo->ui32CoherencyTestsDone = MAX_NUM_COHERENCY_TESTS + 1;
	}
	else
	{
		PVR_LOG(("I/O Coherency Test FAILED"));
		psDevInfo->ui32CoherencyTestsDone++;
	}

	return eError;
}

IMG_BOOL RGXDeviceHasFeaturePower(const void *hPrivate, IMG_UINT64 ui64Feature)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;

	return (psDevInfo->sDevFeatureCfg.ui64Features & ui64Feature) != 0;
}

IMG_BOOL RGXDeviceHasErnBrnPower(const void *hPrivate, IMG_UINT64 ui64ErnsBrns)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;

	return (psDevInfo->sDevFeatureCfg.ui64ErnsBrns & ui64ErnsBrns) != 0;
}

IMG_UINT32 RGXGetDeviceSLCBanks(const void *hPrivate)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;

	return psDevInfo->sDevFeatureCfg.ui32SLCBanks;
}

IMG_UINT32 RGXGetDeviceSLCSize(const void *hPrivate)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;

	return psDevInfo->sDevFeatureCfg.ui32SLCSize;
}

IMG_UINT32 RGXGetDeviceCacheLineSize(const void *hPrivate)
{
	RGX_POWER_LAYER_PARAMS *psPowerParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psPowerParams = (RGX_POWER_LAYER_PARAMS*)hPrivate;
	psDevInfo = psPowerParams->psDevInfo;

	return psDevInfo->sDevFeatureCfg.ui32CacheLineSize;
}

