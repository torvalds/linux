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

#include "rgxlayer_impl.h"
#include "osfunc.h"
#include "pdump_km.h"
#include "rgxfwutils.h"
#include "rgxfwimageutils.h"
#include "devicemem.h"
#include "cache_km.h"
#include "pmr.h"

#if defined(PDUMP)
#if defined(__linux__)
 #include <linux/version.h>

 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
  #include <linux/stdarg.h>
 #else
  #include <stdarg.h>
 #endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */
#else
 #include <stdarg.h>
#endif /* __linux__ */
#endif

void RGXMemCopy(const void *hPrivate,
		void *pvDst,
		void *pvSrc,
		size_t uiSize)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	OSDeviceMemCopy(pvDst, pvSrc, uiSize);
}

void RGXMemSet(const void *hPrivate,
		void *pvDst,
		IMG_UINT8 ui8Value,
		size_t uiSize)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	OSDeviceMemSet(pvDst, ui8Value, uiSize);
}

void RGXCommentLog(const void *hPrivate,
		const IMG_CHAR *pszString,
		...)
{
#if defined(PDUMP)
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	va_list argList;
	va_start(argList, pszString);

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	PDumpCommentWithFlagsVA(psDevInfo->psDeviceNode, PDUMP_FLAGS_CONTINUOUS, pszString, argList);
	va_end(argList);
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	PVR_UNREFERENCED_PARAMETER(pszString);
#endif
}

void RGXErrorLog(const void *hPrivate,
		const IMG_CHAR *pszString,
		...)
{
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	va_list argList;

	PVR_UNREFERENCED_PARAMETER(hPrivate);

	va_start(argList, pszString);
	vsnprintf(szBuffer, sizeof(szBuffer), pszString, argList);
	va_end(argList);

	PVR_DPF((PVR_DBG_ERROR, "%s", szBuffer));
}

IMG_UINT32 RGXGetOSPageSize(const void *hPrivate)
{
	PVR_UNREFERENCED_PARAMETER(hPrivate);
	return OSGetPageSize();
}

IMG_UINT32 RGXGetFWCorememSize(const void *hPrivate)
{
#if defined(RGX_FEATURE_META_COREMEM_SIZE_MAX_VALUE_IDX)
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32CorememSize = 0;

	PVR_ASSERT(hPrivate != NULL);

	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META_COREMEM_SIZE))
	{
		ui32CorememSize = RGX_GET_FEATURE_VALUE(psDevInfo, META_COREMEM_SIZE);
	}

	return ui32CorememSize;
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);

	return 0U;
#endif
}

void RGXWriteReg32(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		OSWriteHWReg32(pvRegsBase, ui32RegAddr, ui32RegValue);
	}

	PDUMPREG32(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	           ui32RegAddr, ui32RegValue, psParams->ui32PdumpFlags);
}

void RGXWriteReg64(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT64 ui64RegValue)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		OSWriteHWReg64(pvRegsBase, ui32RegAddr, ui64RegValue);
	}

	PDUMPREG64(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	           ui32RegAddr, ui64RegValue, psParams->ui32PdumpFlags);
}

IMG_UINT32 RGXReadReg32(const void *hPrivate, IMG_UINT32 ui32RegAddr)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;
	IMG_UINT32 ui32RegValue;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui32RegValue = IMG_UINT32_MAX;
	}
	else
#endif
	{
		ui32RegValue = OSReadHWReg32(pvRegsBase, ui32RegAddr);
	}

	PDUMPREGREAD32(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	               ui32RegAddr, psParams->ui32PdumpFlags);

	return ui32RegValue;
}

IMG_UINT64 RGXReadReg64(const void *hPrivate, IMG_UINT32 ui32RegAddr)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;
	IMG_UINT64 ui64RegValue;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui64RegValue = IMG_UINT64_MAX;
	}
	else
#endif
	{
		ui64RegValue = OSReadHWReg64(pvRegsBase, ui32RegAddr);
	}

	PDUMPREGREAD64(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	               ui32RegAddr, PDUMP_FLAGS_CONTINUOUS);

	return ui64RegValue;
}

IMG_UINT32 RGXReadModifyWriteReg64(const void *hPrivate,
		IMG_UINT32 ui32RegAddr,
		IMG_UINT64 uiRegValueNew,
		IMG_UINT64 uiRegKeepMask)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;
#if defined(PDUMP)
	PDUMP_FLAGS_T ui32PDumpFlags = PDUMP_FLAGS_CONTINUOUS;
#endif

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

	/* only use the new values for bits we update according to the keep mask */
	uiRegValueNew &= ~uiRegKeepMask;

#if defined(PDUMP)

	PDUMP_BLKSTART(ui32PDumpFlags);

	/* Store register offset to temp PDump variable */
	PDumpRegRead64ToInternalVar(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                            ":SYSMEM:$1", ui32RegAddr, ui32PDumpFlags);

	/* Keep the bits set in the mask */
	PDumpWriteVarANDValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                        uiRegKeepMask, ui32PDumpFlags);

	/* OR the new values */
	PDumpWriteVarORValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                       uiRegValueNew, ui32PDumpFlags);

	/* Do the actual register write */
	PDumpInternalVarToReg64(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                        ui32RegAddr, ":SYSMEM:$1", ui32PDumpFlags);

	PDUMP_BLKEND(ui32PDumpFlags);

	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif

	{
		IMG_UINT64 uiRegValue = OSReadHWReg64(pvRegsBase, ui32RegAddr);
		uiRegValue &= uiRegKeepMask;
		OSWriteHWReg64(pvRegsBase, ui32RegAddr, uiRegValue | uiRegValueNew);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXPollReg32(const void *hPrivate,
		IMG_UINT32 ui32RegAddr,
		IMG_UINT32 ui32RegValue,
		IMG_UINT32 ui32RegMask)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		if (PVRSRVPollForValueKM(psDevInfo->psDeviceNode,
				(IMG_UINT32 __iomem *)((IMG_UINT8 __iomem *)pvRegsBase + ui32RegAddr),
				ui32RegValue,
				ui32RegMask,
				POLL_FLAG_LOG_ERROR) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg32: Poll for Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}
	}

	PDUMPREGPOL(psDevInfo->psDeviceNode,
			RGX_PDUMPREG_NAME,
			ui32RegAddr,
			ui32RegValue,
			ui32RegMask,
			psParams->ui32PdumpFlags,
			PDUMP_POLL_OPERATOR_EQUAL);

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXPollReg64(const void *hPrivate,
		IMG_UINT32 ui32RegAddr,
		IMG_UINT64 ui64RegValue,
		IMG_UINT64 ui64RegMask)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;

	/* Split lower and upper words */
	IMG_UINT32 ui32UpperValue = (IMG_UINT32) (ui64RegValue >> 32);
	IMG_UINT32 ui32LowerValue = (IMG_UINT32) (ui64RegValue);
	IMG_UINT32 ui32UpperMask = (IMG_UINT32) (ui64RegMask >> 32);
	IMG_UINT32 ui32LowerMask = (IMG_UINT32) (ui64RegMask);

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = psDevInfo->pvRegsBaseKM;

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		if (PVRSRVPollForValueKM(psDevInfo->psDeviceNode,
				(IMG_UINT32 __iomem *)((IMG_UINT8 __iomem *)pvRegsBase + ui32RegAddr + 4),
				ui32UpperValue,
				ui32UpperMask,
				POLL_FLAG_LOG_ERROR) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg64: Poll for upper part of Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}

		if (PVRSRVPollForValueKM(psDevInfo->psDeviceNode,
				(IMG_UINT32 __iomem *)((IMG_UINT8 __iomem *)pvRegsBase + ui32RegAddr),
				ui32LowerValue,
				ui32LowerMask,
				POLL_FLAG_LOG_ERROR) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPollReg64: Poll for lower part of Reg (0x%x) failed", ui32RegAddr));
			return PVRSRV_ERROR_TIMEOUT;
		}
	}

	PDUMPREGPOL(psDevInfo->psDeviceNode,
			RGX_PDUMPREG_NAME,
			ui32RegAddr + 4,
			ui32UpperValue,
			ui32UpperMask,
			psParams->ui32PdumpFlags,
			PDUMP_POLL_OPERATOR_EQUAL);


	PDUMPREGPOL(psDevInfo->psDeviceNode,
			RGX_PDUMPREG_NAME,
			ui32RegAddr,
			ui32LowerValue,
			ui32LowerMask,
			psParams->ui32PdumpFlags,
			PDUMP_POLL_OPERATOR_EQUAL);

	return PVRSRV_OK;
}

void RGXWaitCycles(const void *hPrivate, IMG_UINT32 ui32Cycles, IMG_UINT32 ui32TimeUs)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;
	OSWaitus(ui32TimeUs);
	PDUMPIDLWITHFLAGS(psDevInfo->psDeviceNode, ui32Cycles, PDUMP_FLAGS_CONTINUOUS);
}

void RGXAcquireKernelMMUPC(const void *hPrivate, IMG_DEV_PHYADDR *psPCAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psPCAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sPCAddr;
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
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

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
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

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
	*psGPURegsAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sGPURegAddr;
}

#if defined(PDUMP)
void RGXMIPSWrapperConfig(const void *hPrivate,
		IMG_UINT32 ui32RegAddr,
		IMG_UINT64 ui64GPURegsAddr,
		IMG_UINT32 ui32GPURegsAlign,
		IMG_UINT32 ui32BootMode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PDUMP_FLAGS_T ui32PDumpFlags = PDUMP_FLAGS_CONTINUOUS;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	OSWriteHWReg64(psDevInfo->pvRegsBaseKM,
			ui32RegAddr,
			(ui64GPURegsAddr >> ui32GPURegsAlign) | ui32BootMode);

	PDUMP_BLKSTART(ui32PDumpFlags);

	/* Store register offset to temp PDump variable */
	PDumpRegLabelToInternalVar(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                           ui32RegAddr, ":SYSMEM:$1", ui32PDumpFlags);

	/* Align register transactions identifier */
	PDumpWriteVarSHRValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                        ui32GPURegsAlign, ui32PDumpFlags);

	/* Enable micromips instruction encoding */
	PDumpWriteVarORValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                       ui32BootMode, ui32PDumpFlags);

	/* Do the actual register write */
	PDumpInternalVarToReg64(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                        ui32RegAddr, ":SYSMEM:$1", ui32PDumpFlags);

	PDUMP_BLKEND(ui32PDumpFlags);
}
#endif

void RGXAcquireBootRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psBootRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psBootRemapAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sBootRemapAddr;
}

void RGXAcquireCodeRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psCodeRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psCodeRemapAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sCodeRemapAddr;
}

void RGXAcquireDataRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psDataRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psDataRemapAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sDataRemapAddr;
}

void RGXAcquireTrampolineRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psTrampolineRemapAddr)
{
	PVR_ASSERT(hPrivate != NULL);
	*psTrampolineRemapAddr = ((RGX_LAYER_PARAMS*)hPrivate)->sTrampolineRemapAddr;
}

#if defined(PDUMP)
static inline
void RGXWriteRemapConfig2Reg(void __iomem *pvRegs,
		PMR *psPMR,
		IMG_DEVMEM_OFFSET_T uiLogicalOffset,
		IMG_UINT32 ui32RegAddr,
		IMG_UINT64 ui64PhyAddr,
		IMG_UINT64 ui64PhyMask,
		IMG_UINT64 ui64Settings)
{
	PDUMP_FLAGS_T ui32PDumpFlags = PDUMP_FLAGS_CONTINUOUS;
	PVRSRV_DEVICE_NODE *psDevNode;

	PVR_ASSERT(psPMR != NULL);
	psDevNode = PMR_DeviceNode(psPMR);

	OSWriteHWReg64(pvRegs, ui32RegAddr, (ui64PhyAddr & ui64PhyMask) | ui64Settings);

	PDUMP_BLKSTART(ui32PDumpFlags);

	/* Store memory offset to temp PDump variable */
	PDumpMemLabelToInternalVar64(":SYSMEM:$1", psPMR,
	                             uiLogicalOffset, ui32PDumpFlags);

	/* Keep only the relevant bits of the output physical address */
	PDumpWriteVarANDValueOp(psDevNode, ":SYSMEM:$1", ui64PhyMask, ui32PDumpFlags);

	/* Extra settings for this remapped region */
	PDumpWriteVarORValueOp(psDevNode, ":SYSMEM:$1", ui64Settings, ui32PDumpFlags);

	/* Do the actual register write */
	PDumpInternalVarToReg64(psDevNode, RGX_PDUMPREG_NAME, ui32RegAddr,
	                        ":SYSMEM:$1", ui32PDumpFlags);

	PDUMP_BLKEND(ui32PDumpFlags);
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
	IMG_UINT32 ui32BootRemapMemOffset = RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_CODE);

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

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
	IMG_UINT32 ui32CodeRemapMemOffset = RGXGetFWImageSectionOffset(NULL, MIPS_EXCEPTIONS_CODE);

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

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
	IMG_UINT32 ui32DataRemapMemOffset = RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_DATA);

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

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

void RGXTrampolineRemapConfig(const void *hPrivate,
		IMG_UINT32 ui32Config1RegAddr,
		IMG_UINT64 ui64Config1RegValue,
		IMG_UINT32 ui32Config2RegAddr,
		IMG_UINT64 ui64Config2PhyAddr,
		IMG_UINT64 ui64Config2PhyMask,
		IMG_UINT64 ui64Config2Settings)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PDUMP_FLAGS_T ui32PDumpFlags = PDUMP_FLAGS_CONTINUOUS;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	/* write the register for real, without PDump */
	OSWriteHWReg64(psDevInfo->pvRegsBaseKM,
			ui32Config1RegAddr,
			ui64Config1RegValue);

	PDUMP_BLKSTART(ui32PDumpFlags);

	/* Store the memory address in a PDump variable */
	PDumpPhysHandleToInternalVar64(psDevInfo->psDeviceNode, ":SYSMEM:$1",
			psDevInfo->psTrampoline->hPdumpPages,
			ui32PDumpFlags);

	/* Keep only the relevant bits of the input physical address */
	PDumpWriteVarANDValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
			~RGX_CR_MIPS_ADDR_REMAP4_CONFIG1_BASE_ADDR_IN_CLRMSK,
			ui32PDumpFlags);

	/* Enable bit */
	PDumpWriteVarORValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
			RGX_CR_MIPS_ADDR_REMAP4_CONFIG1_MODE_ENABLE_EN,
			ui32PDumpFlags);

	/* Do the PDump register write */
	PDumpInternalVarToReg64(psDevInfo->psDeviceNode,
			RGX_PDUMPREG_NAME,
			ui32Config1RegAddr,
			":SYSMEM:$1",
			ui32PDumpFlags);

	PDUMP_BLKEND(ui32PDumpFlags);

	/* this can be written directly */
	RGXWriteReg64(hPrivate,
			ui32Config2RegAddr,
			(ui64Config2PhyAddr & ui64Config2PhyMask) | ui64Config2Settings);
}
#endif

#define MAX_NUM_COHERENCY_TESTS  (10)
IMG_BOOL RGXDoFWSlaveBoot(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_CONFIG *psDevConfig;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	if (psDevInfo->ui32CoherencyTestsDone >= MAX_NUM_COHERENCY_TESTS)
	{
		return IMG_FALSE;
	}

	psDevConfig = ((RGX_LAYER_PARAMS*)hPrivate)->psDevConfig;

	return PVRSRVSystemSnoopingOfCPUCache(psDevConfig);
}

#if defined(RGX_FEATURE_META_MAX_VALUE_IDX)
static PVRSRV_ERROR RGXWriteMetaRegThroughSP(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Wait for Slave Port to be Ready */
	eError = RGXPollReg32(hPrivate,
			RGX_CR_META_SP_MSLVCTRL1,
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
			RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN);
	if (eError != PVRSRV_OK) return eError;

	/* Issue a Write */
	RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVCTRL0, ui32RegAddr);
	RGXWriteReg32(hPrivate, RGX_CR_META_SP_MSLVDATAT, ui32RegValue);

	return eError;
}
#endif

extern void do_invalid_range(unsigned long start, unsigned long len);
/*
 * The fabric coherency test is performed when platform supports fabric coherency
 * either in the form of ACE-lite or Full-ACE. This test is done quite early
 * with the firmware processor quiescent and makes exclusive use of the slave
 * port interface for reading/writing through the device memory hierarchy. The
 * rationale for the test is to ensure that what the CPU writes to its dcache
 * is visible to the GPU via coherency snoop miss/hit and vice-versa without
 * any intervening cache maintenance by the writing agent.
 */
PVRSRV_ERROR RGXFabricCoherencyTest(const void *hPrivate)
{
#if defined(RGX_FEATURE_META_MAX_VALUE_IDX)
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 *pui32FabricCohTestBufferCpuVA;
	DEVMEM_MEMDESC *psFabricCohTestBufferMemDesc;
	RGXFWIF_DEV_VIRTADDR sFabricCohTestBufferDevVA;
	IMG_DEVMEM_SIZE_T uiFabricCohTestBlockSize = sizeof(IMG_UINT64);
	IMG_DEVMEM_ALIGN_T uiFabricCohTestBlockAlign = sizeof(IMG_UINT64);
	IMG_UINT32 ui32SLCCTRL = 0;
	IMG_UINT32 ui32OddEven;
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK)
	IMG_BOOL   bFeatureS7 = RGX_DEVICE_HAS_FEATURE(hPrivate, S7_TOP_INFRASTRUCTURE);
#endif
	IMG_UINT32 ui32TestType;
	IMG_UINT32 ui32OddEvenSeed = 1;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bFullTestPassed = IMG_TRUE;
	IMG_BOOL bExit = IMG_FALSE;
#if defined(DEBUG)
	IMG_BOOL bSubTestPassed = IMG_FALSE;
#endif

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	PVR_LOG(("Starting fabric coherency test ....."));

#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK)
	if (bFeatureS7)
	{
		IMG_UINT64 ui64SegOutAddrTopUncached = RGXFW_SEGMMU_OUTADDR_TOP_VIVT_SLC_UNCACHED(MMU_CONTEXT_MAPPING_FWIF);

		/* Configure META to use SLC force-linefill for the bootloader segment */
		RGXWriteMetaRegThroughSP(hPrivate, META_CR_MMCU_SEGMENTn_OUTA1(6),
				(ui64SegOutAddrTopUncached | RGXFW_BOOTLDR_DEVV_ADDR) >> 32);
	}
	else
#endif
	{
		/* Bypass the SLC when IO coherency is enabled */
		ui32SLCCTRL = RGXReadReg32(hPrivate, RGX_CR_SLC_CTRL_BYPASS);
		RGXWriteReg32(hPrivate,
				RGX_CR_SLC_CTRL_BYPASS,
				ui32SLCCTRL | RGX_CR_SLC_CTRL_BYPASS_BYP_CC_EN);
	}

	/* Size and align are 'expanded' because we request an export align allocation */
	eError = DevmemExportalignAdjustSizeAndAlign(DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareMainHeap),
			&uiFabricCohTestBlockSize,
			&uiFabricCohTestBlockAlign);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemExportalignAdjustSizeAndAlign() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e0;
	}

	/* Allocate, acquire cpu address and set firmware address */
	eError = DevmemFwAllocateExportable(psDevInfo->psDeviceNode,
			uiFabricCohTestBlockSize,
			uiFabricCohTestBlockAlign,
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
			PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
			PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
			PVRSRV_MEMALLOCFLAG_GPU_CACHE_COHERENT |
			PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT |
			PVRSRV_MEMALLOCFLAG_GPU_READABLE |
			PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_CPU_READABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
			"FwExFabricCoherencyTestBuffer",
			&psFabricCohTestBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemFwAllocateExportable() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e0;
	}

	eError = DevmemAcquireCpuVirtAddr(psFabricCohTestBufferMemDesc, (void **) &pui32FabricCohTestBufferCpuVA);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemAcquireCpuVirtAddr() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e1;
	}

	/* Create a FW address which is uncached in the Meta DCache and in the SLC
	 * using the Meta bootloader segment.
	 * This segment is the only one configured correctly out of reset
	 * (when this test is meant to be executed).
	 */
	eError = RGXSetFirmwareAddress(&sFabricCohTestBufferDevVA,
			psFabricCohTestBufferMemDesc,
			0,
			RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", e2);

	/* Undo most of the FW mappings done by RGXSetFirmwareAddress */
	sFabricCohTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_META_CACHE_MASK;
	sFabricCohTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_VIVT_SLC_CACHE_MASK;
	sFabricCohTestBufferDevVA.ui32Addr -= RGXFW_SEGMMU_DATA_BASE_ADDRESS;

	/* Map the buffer in the bootloader segment as uncached */
	sFabricCohTestBufferDevVA.ui32Addr |= RGXFW_BOOTLDR_META_ADDR;
	sFabricCohTestBufferDevVA.ui32Addr |= RGXFW_SEGMMU_DATA_META_UNCACHED;

	for (ui32TestType = 0; ui32TestType < 4 && bExit == IMG_FALSE; ui32TestType++)
	{
		IMG_CPU_PHYADDR sCpuPhyAddr;
		IMG_BOOL bValid;
		PMR *psPMR;

		/* Acquire underlying PMR CpuPA in preparation for cache maintenance */
		(void) DevmemLocalGetImportHandle(psFabricCohTestBufferMemDesc, (void**)&psPMR);
		eError = PMR_CpuPhysAddr(psPMR, OSGetPageShift(), 1, 0, &sCpuPhyAddr, &bValid);
		if (eError != PVRSRV_OK || bValid == IMG_FALSE)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"PMR_CpuPhysAddr error: %s, exiting",
					PVRSRVGetErrorString(eError)));
			bExit = IMG_TRUE;
			continue;
		}

		/* Here we do two passes [runs] mostly to account for the effects of using
		   the different seed (i.e. ui32OddEvenSeed) value to read and write */
		for (ui32OddEven = 1; ui32OddEven < 3 && bExit == IMG_FALSE; ui32OddEven++)
		{
			IMG_UINT32 i;

#if defined(DEBUG)
			switch (ui32TestType)
			{
			case 0:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Miss Test: starting [run #%u]", ui32OddEven));
				break;
			case 1:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Miss Test: starting [run #%u]", ui32OddEven));
				break;
			case 2:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Hit  Test: starting [run #%u]", ui32OddEven));
				break;
			case 3:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Hit  Test: starting [run #%u]", ui32OddEven));
				break;
			default:
				PVR_LOG(("Internal error, exiting test"));
				eError = PVRSRV_ERROR_INIT_FAILURE;
				bExit = IMG_TRUE;
				continue;
			}
#endif

			for (i = 0; i < 2 && bExit == IMG_FALSE; i++)
			{
				IMG_UINT32 ui32FWAddr;
				IMG_UINT32 ui32FWValue;
				IMG_UINT32 ui32FWValue2;
				IMG_CPU_PHYADDR sCpuPhyAddrStart;
				IMG_CPU_PHYADDR sCpuPhyAddrEnd;
				IMG_UINT32 ui32LastFWValue = ~0;
				IMG_UINT32 ui32Offset = i * sizeof(IMG_UINT32);

				/* Calculate next address and seed value to write/read from slave-port */
				ui32FWAddr = sFabricCohTestBufferDevVA.ui32Addr + ui32Offset;
				sCpuPhyAddrStart.uiAddr = sCpuPhyAddr.uiAddr + ui32Offset;
				sCpuPhyAddrEnd.uiAddr = sCpuPhyAddrStart.uiAddr;
				ui32OddEvenSeed += 1;

				if (ui32TestType & 0x1)
				{
					ui32FWValue = i + ui32OddEvenSeed;

					switch (ui32TestType)
					{
					case 1:
					case 3:
						/* Clean dcache to ensure there is no stale data in dcache that might over-write
						   what we are about to write via slave-port here because if it drains from the CPU
						   dcache before we read it, it would corrupt what we are going to read back via
						   the CPU */
						sCpuPhyAddrEnd.uiAddr += sizeof(IMG_UINT32);
						CacheOpExec(psDevInfo->psDeviceNode,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset + sizeof(IMG_UINT32),
								sCpuPhyAddrStart,
								sCpuPhyAddrEnd,
								PVRSRV_CACHE_OP_CLEAN);
						break;
					}

					/* Write the value using the RGX slave-port interface */
					eError = RGXWriteFWModuleAddr(psDevInfo, ui32FWAddr, ui32FWValue);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								"RGXWriteFWModuleAddr error: %s, exiting",
								PVRSRVGetErrorString(eError)));
						bExit = IMG_TRUE;
						continue;
					}

					/* Read back value using RGX slave-port interface, this is used
					   as a sort of memory barrier for the above write */
					eError = RGXReadFWModuleAddr(psDevInfo, ui32FWAddr, &ui32FWValue2);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								"RGXReadFWModuleAddr error: %s, exiting",
								PVRSRVGetErrorString(eError)));
						bExit = IMG_TRUE;
						continue;
					}
					else if (ui32FWValue != ui32FWValue2)
					{
						/* Fatal error, we should abort */
						PVR_DPF((PVR_DBG_ERROR,
								"At Offset: %d, RAW via SlavePort failed: expected: %x, got: %x",
								i,
								ui32FWValue,
								ui32FWValue2));
						eError = PVRSRV_ERROR_INIT_FAILURE;
						bExit = IMG_TRUE;
						continue;
					}

					if (! PVRSRVSystemSnoopingOfDeviceCache(psDevInfo->psDeviceNode->psDevConfig))
					{
						/* Invalidate dcache to ensure that any prefetched data by the CPU from this memory
						   region is discarded before we read (i.e. next read must trigger a cache miss).
						   If there is snooping of device cache, then any prefetching done by the CPU
						   will reflect the most up to date datum writing by GPU into said location,
						   that is to say prefetching must be coherent so CPU d-flush is not needed */
						sCpuPhyAddrEnd.uiAddr += sizeof(IMG_UINT32);
						CacheOpExec(psDevInfo->psDeviceNode,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset + sizeof(IMG_UINT32),
								sCpuPhyAddrStart,
								sCpuPhyAddrEnd,
								PVRSRV_CACHE_OP_INVALIDATE);
                        do_invalid_range(0x0, 0x200000);
					}
				}
				else
				{
					IMG_UINT32 ui32RAWCpuValue;

					/* Ensures line is in dcache */
					ui32FWValue = IMG_UINT32_MAX;

					/* Dirty allocation in dcache */
					ui32RAWCpuValue = i + ui32OddEvenSeed;
					pui32FabricCohTestBufferCpuVA[i] = i + ui32OddEvenSeed;

					/* Flush possible cpu store-buffer(ing) on LMA */
					OSWriteMemoryBarrier(&pui32FabricCohTestBufferCpuVA[i]);

					switch (ui32TestType)
					{
					case 0:
						/* Flush dcache to force subsequent incoming CPU-bound snoop to miss so
						   memory is coherent before the SlavePort reads */
						sCpuPhyAddrEnd.uiAddr += sizeof(IMG_UINT32);
						CacheOpExec(psDevInfo->psDeviceNode,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset,
								(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset + sizeof(IMG_UINT32),
								sCpuPhyAddrStart,
								sCpuPhyAddrEnd,
								PVRSRV_CACHE_OP_FLUSH);
						break;
					}

					/* Read back value using RGX slave-port interface */
					eError = RGXReadFWModuleAddr(psDevInfo, ui32FWAddr, &ui32FWValue);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								"RGXReadFWModuleAddr error: %s, exiting",
								PVRSRVGetErrorString(eError)));
						bExit = IMG_TRUE;
						continue;
					}

					/* We are being mostly paranoid here, just to account for CPU RAW operations */
					sCpuPhyAddrEnd.uiAddr += sizeof(IMG_UINT32);
					CacheOpExec(psDevInfo->psDeviceNode,
							(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset,
							(IMG_CHAR *)pui32FabricCohTestBufferCpuVA + ui32Offset + sizeof(IMG_UINT32),
							sCpuPhyAddrStart,
							sCpuPhyAddrEnd,
							PVRSRV_CACHE_OP_FLUSH);
					if (pui32FabricCohTestBufferCpuVA[i] != ui32RAWCpuValue)
					{
						/* Fatal error, we should abort */
						PVR_DPF((PVR_DBG_ERROR,
								"At Offset: %d, RAW by CPU failed: expected: %x, got: %x",
								i,
								ui32RAWCpuValue,
								pui32FabricCohTestBufferCpuVA[i]));
						eError = PVRSRV_ERROR_INIT_FAILURE;
						bExit = IMG_TRUE;
						continue;
					}
				}

				/* Compare to see if sub-test passed */
				if (pui32FabricCohTestBufferCpuVA[i] == ui32FWValue)
				{
#if defined(DEBUG)
					bSubTestPassed = IMG_TRUE;
#endif
				}
				else
				{
					bFullTestPassed = IMG_FALSE;
					eError = PVRSRV_ERROR_INIT_FAILURE;
#if defined(DEBUG)
					bSubTestPassed = IMG_FALSE;
#endif
					if (ui32LastFWValue != ui32FWValue)
					{
#if defined(DEBUG)
						PVR_LOG(("At Offset: %d, Expected: %x, Got: %x",
								i,
								(ui32TestType & 0x1) ? ui32FWValue : pui32FabricCohTestBufferCpuVA[i],
										(ui32TestType & 0x1) ? pui32FabricCohTestBufferCpuVA[i] : ui32FWValue));
#endif
					}
					else
					{
						PVR_DPF((PVR_DBG_ERROR,
								"test encountered unexpected error, exiting"));
						eError = PVRSRV_ERROR_INIT_FAILURE;
						bExit = IMG_TRUE;
						continue;
					}
				}

				ui32LastFWValue = (ui32TestType & 0x1) ? ui32FWValue : pui32FabricCohTestBufferCpuVA[i];
			}

#if defined(DEBUG)
			if (bExit)
			{
				continue;
			}

			switch (ui32TestType)
			{
			case 0:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Miss Test: completed [run #%u]: %s", ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case 1:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Miss Test: completed [run #%u]: %s", ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case 2:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Hit  Test: completed [run #%u]: %s", ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case 3:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Hit  Test: completed [run #%u]: %s", ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			default:
				PVR_LOG(("Internal error, exiting test"));
				bExit = IMG_TRUE;
				continue;
			}
#endif
		}
	}

	RGXUnsetFirmwareAddress(psFabricCohTestBufferMemDesc);
e2:
	DevmemReleaseCpuVirtAddr(psFabricCohTestBufferMemDesc);
e1:
	DevmemFwUnmapAndFree(psDevInfo, psFabricCohTestBufferMemDesc);

e0:
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE_BIT_MASK)
	if (bFeatureS7)
	{
		/* Restore bootloader segment settings */
		IMG_UINT64 ui64SegOutAddrTopCached   = RGXFW_SEGMMU_OUTADDR_TOP_VIVT_SLC_CACHED(MMU_CONTEXT_MAPPING_FWIF);
		RGXWriteMetaRegThroughSP(hPrivate, META_CR_MMCU_SEGMENTn_OUTA1(6),
				(ui64SegOutAddrTopCached | RGXFW_BOOTLDR_DEVV_ADDR) >> 32);
	}
	else
#endif
	{
		/* Restore SLC bypass settings */
		RGXWriteReg32(hPrivate, RGX_CR_SLC_CTRL_BYPASS, ui32SLCCTRL);
	}

	bFullTestPassed = bExit ? IMG_FALSE: bFullTestPassed;
	if (bFullTestPassed)
	{
		PVR_LOG(("fabric coherency test: PASSED"));
		psDevInfo->ui32CoherencyTestsDone = MAX_NUM_COHERENCY_TESTS + 1;
	}
	else
	{
		PVR_LOG(("fabric coherency test: FAILED"));
		psDevInfo->ui32CoherencyTestsDone++;
	}

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hPrivate);

	return PVRSRV_OK;
#endif
}

IMG_INT32 RGXDeviceGetFeatureValue(const void *hPrivate, IMG_UINT64 ui64Feature)
{
	IMG_INT32 i32Ret = -1;
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_ASSERT(hPrivate != NULL);

	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	psDeviceNode = psDevInfo->psDeviceNode;

	if ((psDeviceNode->pfnGetDeviceFeatureValue))
	{
		i32Ret = psDeviceNode->pfnGetDeviceFeatureValue(psDeviceNode, ui64Feature);
	}

	return i32Ret;
}

IMG_BOOL RGXDeviceHasFeature(const void *hPrivate, IMG_UINT64 ui64Feature)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);

	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	return (psDevInfo->sDevFeatureCfg.ui64Features & ui64Feature) != 0;
}

IMG_BOOL RGXDeviceHasErnBrn(const void *hPrivate, IMG_UINT64 ui64ErnsBrns)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	return (psDevInfo->sDevFeatureCfg.ui64ErnsBrns & ui64ErnsBrns) != 0;
}

IMG_UINT32 RGXGetDeviceSLCBanks(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	if (!RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, SLC_BANKS))
	{
		return 0;
	}
	return RGX_GET_FEATURE_VALUE(psDevInfo, SLC_BANKS);
}

IMG_UINT32 RGXGetDeviceCacheLineSize(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	if (!RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, SLC_CACHE_LINE_SIZE_BITS))
	{
		return 0;
	}
	return RGX_GET_FEATURE_VALUE(psDevInfo, SLC_CACHE_LINE_SIZE_BITS);
}

IMG_UINT32 RGXGetDevicePhysBusWidth(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	if (!RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, PHYS_BUS_WIDTH))
	{
		return 0;
	}
	return RGX_GET_FEATURE_VALUE(psDevInfo, PHYS_BUS_WIDTH);
}

IMG_BOOL RGXDevicePA0IsValid(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	return psDevInfo->sLayerParams.bDevicePA0IsValid;
}

void RGXAcquireBootCodeAddr(const void *hPrivate, IMG_DEV_VIRTADDR *psBootCodeAddr)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	*psBootCodeAddr = psDevInfo->sFWCodeDevVAddrBase;
}

void RGXAcquireBootDataAddr(const void *hPrivate, IMG_DEV_VIRTADDR *psBootDataAddr)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	*psBootDataAddr = psDevInfo->sFWDataDevVAddrBase;
}

IMG_BOOL RGXDeviceAckIrq(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	return (psDevInfo->pfnRGXAckIrq != NULL) ?
			psDevInfo->pfnRGXAckIrq(psDevInfo) : IMG_TRUE;
}
