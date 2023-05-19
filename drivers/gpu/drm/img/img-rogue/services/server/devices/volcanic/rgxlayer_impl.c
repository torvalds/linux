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
#include "cache_km.h"

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

IMG_UINT32 RGXGetFWCorememSize(const void *hPrivate)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;

	PVR_ASSERT(hPrivate != NULL);

	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;

	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META_COREMEM_SIZE))
	{
		return RGX_GET_FEATURE_VALUE(psDevInfo, META_COREMEM_SIZE);
	}
	return 0;
}

void RGXWriteReg32(const void *hPrivate, IMG_UINT32 ui32RegAddr, IMG_UINT32 ui32RegValue)
{
	RGX_LAYER_PARAMS *psParams;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	void __iomem *pvRegsBase;

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		OSWriteUncheckedHWReg32(pvRegsBase, ui32RegAddr, ui32RegValue);
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
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

#if defined(PDUMP)
	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif
	{
		OSWriteUncheckedHWReg64(pvRegsBase, ui32RegAddr, ui64RegValue);
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
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

#if defined(PDUMP)
	if (psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui32RegValue = IMG_UINT32_MAX;
	}
	else
#endif
	{
		ui32RegValue = OSReadUncheckedHWReg32(pvRegsBase, ui32RegAddr);
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
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

#if defined(PDUMP)
	if (psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW)
	{
		ui64RegValue = IMG_UINT64_MAX;
	}
	else
#endif
	{
		ui64RegValue = OSReadUncheckedHWReg64(pvRegsBase, ui32RegAddr);
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

	PVR_ASSERT(hPrivate != NULL);
	psParams = (RGX_LAYER_PARAMS*)hPrivate;
	psDevInfo = psParams->psDevInfo;
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

	/* only use the new values for bits we update according to the keep mask */
	uiRegValueNew &= ~uiRegKeepMask;

#if defined(PDUMP)
	/* Store register offset to temp PDump variable */
	PDumpRegRead64ToInternalVar(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                            ":SYSMEM:$1", ui32RegAddr, PDUMP_FLAGS_CONTINUOUS);

	/* Keep the bits set in the mask */
	PDumpWriteVarANDValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                        uiRegKeepMask, PDUMP_FLAGS_CONTINUOUS);

	/* OR the new values */
	PDumpWriteVarORValueOp(psDevInfo->psDeviceNode, ":SYSMEM:$1",
	                       uiRegValueNew, PDUMP_FLAGS_CONTINUOUS);

	/* Do the actual register write */
	PDumpInternalVarToReg64(psDevInfo->psDeviceNode, RGX_PDUMPREG_NAME,
	                        ui32RegAddr, ":SYSMEM:$1", 0);

	if (!(psParams->ui32PdumpFlags & PDUMP_FLAGS_NOHW))
#endif

	{
		IMG_UINT64 uiRegValue = OSReadUncheckedHWReg64(pvRegsBase, ui32RegAddr);
		uiRegValue &= uiRegKeepMask;
		OSWriteUncheckedHWReg64(pvRegsBase, ui32RegAddr, uiRegValue | uiRegValueNew);
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
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

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
	pvRegsBase = (ui32RegAddr < RGX_HOST_SECURE_REGBANK_OFFSET) ?
	             (psDevInfo->pvRegsBaseKM) : (psDevInfo->pvSecureRegsBaseKM);

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
	OSWriteUncheckedHWReg32(psDevInfo->pvSecureRegsBaseKM, ui32PCReg, ui32PCVal);

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

#define MAX_NUM_COHERENCY_TESTS  (10)
IMG_BOOL RGXDoFWSlaveBoot(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	if (psDevInfo->ui32CoherencyTestsDone >= MAX_NUM_COHERENCY_TESTS)
	{
		return IMG_FALSE;
	}

	psDeviceNode = psDevInfo->psDeviceNode;
#if !defined(NO_HARDWARE)
	return (PVRSRVSystemSnoopingOfCPUCache(psDeviceNode->psDevConfig) &&
	        PVRSRVSystemSnoopingOfDeviceCache(psDeviceNode->psDevConfig));
#else
	return IMG_FALSE;
#endif
}

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
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 *pui32FabricCohTestBufferCpuVA = NULL;
	IMG_UINT32 *pui32FabricCohCcTestBufferCpuVA = NULL;
	IMG_UINT32 *pui32FabricCohNcTestBufferCpuVA = NULL;
	DEVMEM_MEMDESC *psFabricCohTestBufferMemDesc = NULL;
	DEVMEM_MEMDESC *psFabricCohCcTestBufferMemDesc = NULL;
	DEVMEM_MEMDESC *psFabricCohNcTestBufferMemDesc = NULL;
	RGXFWIF_DEV_VIRTADDR sFabricCohCcTestBufferDevVA;
	RGXFWIF_DEV_VIRTADDR sFabricCohNcTestBufferDevVA;
	RGXFWIF_DEV_VIRTADDR *psFabricCohTestBufferDevVA = NULL;
	IMG_DEVMEM_SIZE_T uiFabricCohTestBlockSize = sizeof(IMG_UINT64);
	IMG_DEVMEM_ALIGN_T uiFabricCohTestBlockAlign = sizeof(IMG_UINT64);
	IMG_UINT64 ui64SegOutAddrTopCached = 0;
	IMG_UINT64 ui64SegOutAddrTopUncached = 0;
	IMG_UINT32 ui32OddEven;
	IMG_UINT32 ui32OddEvenSeed = 1;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bFullTestPassed = IMG_TRUE;
	IMG_BOOL bExit = IMG_FALSE;
#if defined(DEBUG)
	IMG_BOOL bSubTestPassed = IMG_FALSE;
#endif
	enum TEST_TYPE {
		CPU_WRITE_GPU_READ_SM=0, GPU_WRITE_CPU_READ_SM,
		CPU_WRITE_GPU_READ_SH,   GPU_WRITE_CPU_READ_SH
	} eTestType;

	PVR_ASSERT(hPrivate != NULL);
	psDevInfo = ((RGX_LAYER_PARAMS*)hPrivate)->psDevInfo;

	PVR_LOG(("Starting fabric coherency test ....."));

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

	/* Allocate, acquire cpu address and set firmware address for cc=1 buffer */
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
										"FwExFabricCoherencyCcTestBuffer",
										&psFabricCohCcTestBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemFwAllocateExportable() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e0;
	}

	eError = DevmemAcquireCpuVirtAddr(psFabricCohCcTestBufferMemDesc, (void **) &pui32FabricCohCcTestBufferCpuVA);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemAcquireCpuVirtAddr() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e1;
	}

	/* Create a FW address which is uncached in the Meta DCache and in the SLC using the Meta bootloader segment.
	   This segment is the only one configured correctly out of reset (when this test is meant to be executed) */
	eError = RGXSetFirmwareAddress(&sFabricCohCcTestBufferDevVA,
						  psFabricCohCcTestBufferMemDesc,
						  0,
						  RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress:1", e2);

	/* Undo most of the FW mappings done by RGXSetFirmwareAddress */
	sFabricCohCcTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_META_CACHE_MASK;
	sFabricCohCcTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_VIVT_SLC_CACHE_MASK;
	sFabricCohCcTestBufferDevVA.ui32Addr -= RGXFW_SEGMMU_DATA_BASE_ADDRESS;

	/* Map the buffer in the bootloader segment as uncached */
	sFabricCohCcTestBufferDevVA.ui32Addr |= RGXFW_BOOTLDR_META_ADDR;
	sFabricCohCcTestBufferDevVA.ui32Addr |= RGXFW_SEGMMU_DATA_META_UNCACHED;

	/* Allocate, acquire cpu address and set firmware address for cc=0 buffer  */
	eError = DevmemFwAllocateExportable(psDevInfo->psDeviceNode,
										uiFabricCohTestBlockSize,
										uiFabricCohTestBlockAlign,
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
										PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
										PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
										PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT |
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_CPU_READABLE |
										PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
										"FwExFabricCoherencyNcTestBuffer",
										&psFabricCohNcTestBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemFwAllocateExportable() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e3;
	}

	eError = DevmemAcquireCpuVirtAddr(psFabricCohNcTestBufferMemDesc, (void **) &pui32FabricCohNcTestBufferCpuVA);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"DevmemAcquireCpuVirtAddr() error: %s, exiting",
				PVRSRVGetErrorString(eError)));
		goto e4;
	}

	eError = RGXSetFirmwareAddress(&sFabricCohNcTestBufferDevVA,
						  psFabricCohNcTestBufferMemDesc,
						  0,
						  RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress:2", e5);

	/* Undo most of the FW mappings done by RGXSetFirmwareAddress */
	sFabricCohNcTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_META_CACHE_MASK;
	sFabricCohNcTestBufferDevVA.ui32Addr &= ~RGXFW_SEGMMU_DATA_VIVT_SLC_CACHE_MASK;
	sFabricCohNcTestBufferDevVA.ui32Addr -= RGXFW_SEGMMU_DATA_BASE_ADDRESS;

	/* Map the buffer in the bootloader segment as uncached */
	sFabricCohNcTestBufferDevVA.ui32Addr |= RGXFW_BOOTLDR_META_ADDR;
	sFabricCohNcTestBufferDevVA.ui32Addr |= RGXFW_SEGMMU_DATA_META_UNCACHED;

	/* Obtain the META segment addresses corresponding to cached and uncached windows into SLC */
	ui64SegOutAddrTopCached   = RGXFW_SEGMMU_OUTADDR_TOP_VIVT_SLC_CACHED(MMU_CONTEXT_MAPPING_FWIF);
	ui64SegOutAddrTopUncached = RGXFW_SEGMMU_OUTADDR_TOP_VIVT_SLC_UNCACHED(MMU_CONTEXT_MAPPING_FWIF);

	/* At the top level, we perform snoop-miss (i.e. to verify slave port) & snoop-hit (i.e. to verify ACE) test.
	   NOTE: For now, skip snoop-miss test as Services currently forces all firmware allocations to be coherent */
	for (eTestType = CPU_WRITE_GPU_READ_SH; eTestType <= GPU_WRITE_CPU_READ_SH && bExit == IMG_FALSE; eTestType++)
	{
		IMG_CPU_PHYADDR sCpuPhyAddr;
		IMG_BOOL bValid;
		PMR *psPMR;

		if (eTestType == CPU_WRITE_GPU_READ_SM)
		{
			/* All snoop miss test must bypass the SLC, here memory is region of coherence so
			   configure META to use SLC bypass cache policy for the bootloader segment. Note
			   this cannot be done on a cache-coherent (i.e. CC=1) VA, as this violates ACE
			   standard as one cannot issue a non-coherent request into the bus fabric for
			   an allocation's VA that is cache-coherent in SLC, so use non-coherent buffer */
			RGXWriteMetaRegThroughSP(hPrivate, META_CR_MMCU_SEGMENTn_OUTA1(6),
									(ui64SegOutAddrTopUncached |  RGXFW_BOOTLDR_DEVV_ADDR) >> 32);
			pui32FabricCohTestBufferCpuVA = pui32FabricCohNcTestBufferCpuVA;
			psFabricCohTestBufferMemDesc = psFabricCohNcTestBufferMemDesc;
			psFabricCohTestBufferDevVA = &sFabricCohNcTestBufferDevVA;
		}
		else if (eTestType == CPU_WRITE_GPU_READ_SH)
		{
			/* All snoop hit test must obviously use SLC, here SLC is region of coherence so
			   configure META not to bypass the SLC for the bootloader segment */
			RGXWriteMetaRegThroughSP(hPrivate, META_CR_MMCU_SEGMENTn_OUTA1(6),
									(ui64SegOutAddrTopCached |  RGXFW_BOOTLDR_DEVV_ADDR) >> 32);
			pui32FabricCohTestBufferCpuVA = pui32FabricCohCcTestBufferCpuVA;
			psFabricCohTestBufferMemDesc = psFabricCohCcTestBufferMemDesc;
			psFabricCohTestBufferDevVA = &sFabricCohCcTestBufferDevVA;
		}

		if (eTestType == GPU_WRITE_CPU_READ_SH &&
			!PVRSRVSystemSnoopingOfDeviceCache(psDevInfo->psDeviceNode->psDevConfig))
		{
			/* Cannot perform this test if there is no snooping of device cache */
			continue;
		}

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

		/* Here we do two passes mostly to account for the effects of using a different
		   seed (i.e. ui32OddEvenSeed) value to read and write */
		for (ui32OddEven = 1; ui32OddEven < 3 && bExit == IMG_FALSE; ui32OddEven++)
		{
			IMG_UINT32 i;

			/* Do multiple sub-dword cache line tests */
			for (i = 0; i < 2 && bExit == IMG_FALSE; i++)
			{
				IMG_UINT32 ui32FWAddr;
				IMG_UINT32 ui32FWValue;
				IMG_UINT32 ui32FWValue2;
				IMG_UINT32 ui32LastFWValue = ~0;
				IMG_UINT32 ui32Offset = i * sizeof(IMG_UINT32);

				/* Calculate next address and seed value to write/read from slave-port */
				ui32FWAddr = psFabricCohTestBufferDevVA->ui32Addr + ui32Offset;
				ui32OddEvenSeed += 1;

				if (eTestType == GPU_WRITE_CPU_READ_SM || eTestType == GPU_WRITE_CPU_READ_SH)
				{
					/* Clean dcache to ensure there is no stale data in dcache that might over-write
					   what we are about to write via slave-port here because if it drains from the CPU
					   dcache before we read it, it would corrupt what we are going to read back via
					   the CPU */
					CacheOpValExec(psPMR, 0, ui32Offset, sizeof(IMG_UINT32), PVRSRV_CACHE_OP_CLEAN);

					/* Calculate a new value to write */
					ui32FWValue = i + ui32OddEvenSeed;

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
						//IMG_UINT32 ui32FWValue3;
						//RGXReadFWModuleAddr(psDevInfo, 0xC1F00000, &ui32FWValue3);

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

					if (!PVRSRVSystemSnoopingOfDeviceCache(psDevInfo->psDeviceNode->psDevConfig))
					{
						/* Invalidate dcache to ensure that any prefetched data by the CPU from this memory
						   region is discarded before we read (i.e. next read must trigger a cache miss).
						   If there is snooping of device cache, then any prefetching done by the CPU
						   will reflect the most up to date datum writing by GPU into said location,
						   that is to say prefetching must be coherent so CPU d-flush is not needed */
						CacheOpValExec(psPMR, 0, ui32Offset, sizeof(IMG_UINT32), PVRSRV_CACHE_OP_INVALIDATE);
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

					switch (eTestType)
					{
					case CPU_WRITE_GPU_READ_SM:
						/* Flush dcache to force subsequent incoming CPU-bound snoop to miss so
						   memory is coherent before the SlavePort reads */
						CacheOpValExec(psPMR, 0, ui32Offset, sizeof(IMG_UINT32), PVRSRV_CACHE_OP_FLUSH);
						break;
					default:
						break;
					}

					/* Read back value using RGX slave-port interface */
					eError = RGXReadFWModuleAddr(psDevInfo, ui32FWAddr, &ui32FWValue);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
								"RGXReadWithSP error: %s, exiting",
								PVRSRVGetErrorString(eError)));
						bExit = IMG_TRUE;
						continue;
					}

					/* Being mostly paranoid here, verify that CPU RAW operation is valid
					   after the above slave port read */
					CacheOpValExec(psPMR, 0, ui32Offset, sizeof(IMG_UINT32), PVRSRV_CACHE_OP_INVALIDATE);
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
								 (eTestType & 0x1) ? ui32FWValue : pui32FabricCohTestBufferCpuVA[i],
								 (eTestType & 0x1) ? pui32FabricCohTestBufferCpuVA[i] : ui32FWValue));
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

				ui32LastFWValue = (eTestType & 0x1) ? ui32FWValue : pui32FabricCohTestBufferCpuVA[i];
			}

#if defined(DEBUG)
			bSubTestPassed = bExit ? IMG_FALSE : bSubTestPassed;
			switch (eTestType)
			{
			case CPU_WRITE_GPU_READ_SM:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Miss Test: completed [run #%u]: %s",
						 ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case GPU_WRITE_CPU_READ_SM:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Miss Test: completed [run #%u]: %s",
						 ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case CPU_WRITE_GPU_READ_SH:
				PVR_LOG(("CPU:Write/GPU:Read Snoop Hit Test: completed [run #%u]: %s",
						 ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			case GPU_WRITE_CPU_READ_SH:
				PVR_LOG(("GPU:Write/CPU:Read Snoop Hit Test: completed [run #%u]: %s",
						 ui32OddEven, bSubTestPassed ? "PASSED" : "FAILED"));
				break;
			default:
				PVR_LOG(("Internal error, exiting test"));
				eError = PVRSRV_ERROR_INIT_FAILURE;
				bExit = IMG_TRUE;
				continue;
			}
#endif
		}
	}

	/* Release and free NC/CC test buffers */
	RGXUnsetFirmwareAddress(psFabricCohCcTestBufferMemDesc);
e5:
	DevmemReleaseCpuVirtAddr(psFabricCohCcTestBufferMemDesc);
e4:
	DevmemFwUnmapAndFree(psDevInfo, psFabricCohCcTestBufferMemDesc);

e3:
	RGXUnsetFirmwareAddress(psFabricCohNcTestBufferMemDesc);
e2:
	DevmemReleaseCpuVirtAddr(psFabricCohNcTestBufferMemDesc);
e1:
	DevmemFwUnmapAndFree(psDevInfo, psFabricCohNcTestBufferMemDesc);

e0:
	/* Restore bootloader segment settings */
	RGXWriteMetaRegThroughSP(hPrivate, META_CR_MMCU_SEGMENTn_OUTA1(6),
	                         (ui64SegOutAddrTopCached | RGXFW_BOOTLDR_DEVV_ADDR) >> 32);

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
