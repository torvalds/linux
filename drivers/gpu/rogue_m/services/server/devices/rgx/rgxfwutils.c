 /*************************************************************************/ /*!
@File
@Title          Rogue firmware utility routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Rogue firmware utility routines
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

#include <stddef.h>

#include "lists.h"

#include "rgxdefs_km.h"
#include "rgx_fwif_km.h"
#include "pdump_km.h"
#include "osfunc.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "devicemem_server.h"
#include "pvr_debug.h"
#include "rgxfwutils.h"
#include "rgx_fwif.h"
#include "rgx_fwif_alignchecks_km.h"
#include "rgx_fwif_resetframework.h"
#include "rgx_pdump_panics.h"
#include "rgxheapconfig.h"
#include "pvrsrv.h"
#include "rgxdebug.h"
#include "rgxhwperf.h"
#include "rgxccb.h"
#include "rgxcompute.h"
#include "rgxtransfer.h"
#if defined(RGX_FEATURE_RAY_TRACING)
#include "rgxray.h"
#endif
#if defined(SUPPORT_DISPLAY_CLASS)
#include "dc_server.h"
#endif
#include "rgxmem.h"
#include "rgxta3d.h"
#include "rgxutils.h"
#include "sync_internal.h"
#include "tlstream.h"
#include "devicemem_server_utils.h"

#if defined(TDMETACODE)
#include "physmem_osmem.h"
#endif

#ifdef __linux__
#include <linux/kernel.h>	// sprintf
#include <linux/string.h>	// strncpy, strlen
#include "trace_events.h"
#else
#include <stdio.h>
#endif

#include "process_stats.h"
/* Kernel CCB length */
#define RGXFWIF_KCCB_TA_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_3D_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_2D_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_CDM_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_GP_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_RTU_NUMCMDS_LOG2	(6)
#define RGXFWIF_KCCB_SHG_NUMCMDS_LOG2	(6)

/* Firmware CCB length */
#define RGXFWIF_FWCCB_TA_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_3D_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_2D_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_CDM_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_GP_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_RTU_NUMCMDS_LOG2	(4)
#define RGXFWIF_FWCCB_SHG_NUMCMDS_LOG2	(4)

#if defined(RGX_FEATURE_SLC_VIVT)
static PVRSRV_ERROR _AllocateSLC3Fence(PVRSRV_RGXDEV_INFO* psDevInfo, RGXFWIF_INIT* psRGXFWInit)
{
	PVRSRV_ERROR eError;
	DEVMEM_MEMDESC** ppsSLC3FenceMemDesc = &psDevInfo->psSLC3FenceMemDesc;

	PVR_DPF_ENTERED;

	eError = DevmemAllocate(psDevInfo->psFirmwareHeap,
							1,
							ROGUE_CACHE_LINE_SIZE,
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
                            PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE | 
							PVRSRV_MEMALLOCFLAG_UNCACHED,
							"SLC3 Fence WA",
							ppsSLC3FenceMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF_RETURN_RC(eError);
	}

	/*
		We need to map it so the heap for this allocation
		is set
	*/
	eError = DevmemMapToDevice(*ppsSLC3FenceMemDesc,
							   psDevInfo->psFirmwareHeap,
							   &psRGXFWInit->sSLC3FenceDevVAddr);
	if (eError != PVRSRV_OK)
	{
		DevmemFwFree(*ppsSLC3FenceMemDesc);
	}

	PVR_DPF_RETURN_RC1(eError, *ppsSLC3FenceMemDesc);
}

static IMG_VOID _FreeSLC3Fence(PVRSRV_RGXDEV_INFO* psDevInfo)
{
	DEVMEM_MEMDESC* psSLC3FenceMemDesc = psDevInfo->psSLC3FenceMemDesc;

	if (psSLC3FenceMemDesc)
	{
		DevmemReleaseDevVirtAddr(psSLC3FenceMemDesc);
		DevmemFree(psSLC3FenceMemDesc);
	}
}
#endif

static IMG_VOID __MTSScheduleWrite(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32Value)
{
	/* ensure memory is flushed before kicking MTS */
	OSWriteMemoryBarrier();

	OSWriteHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MTS_SCHEDULE, ui32Value);

	/* ensure the MTS kick goes through before continuing */
	OSMemoryBarrier();
}


/*!
*******************************************************************************
 @Function		RGXFWSetupSignatureChecks
 @Description	
 @Input			psDevInfo
 
 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXFWSetupSignatureChecks(PVRSRV_RGXDEV_INFO* psDevInfo,
                                              DEVMEM_MEMDESC**    ppsSigChecksMemDesc, 
                                              IMG_UINT32          ui32SigChecksBufSize,
                                              RGXFWIF_SIGBUF_CTL* psSigBufCtl,
                                              const IMG_CHAR*     pszBufferName)
{
	PVRSRV_ERROR	eError;
	DEVMEM_FLAGS_T	uiMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
									  PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
					                  PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
									  PVRSRV_MEMALLOCFLAG_CPU_READABLE |
									  PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | 
									  PVRSRV_MEMALLOCFLAG_UNCACHED |
									  PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocate memory for the checks */
	PDUMPCOMMENT("Allocate memory for %s signature checks", pszBufferName);
	eError = DevmemFwAllocate(psDevInfo,
							ui32SigChecksBufSize,
							uiMemAllocFlags,
							"SignatureChecks",
							ppsSigChecksMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for signature checks (%u)",
					ui32SigChecksBufSize,
					eError));
		return eError;
	}

	/* Prepare the pointer for the fw to access that memory */
	RGXSetFirmwareAddress(&psSigBufCtl->psBuffer,
						  *ppsSigChecksMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	DevmemPDumpLoadMem(	*ppsSigChecksMemDesc,
						0,
						ui32SigChecksBufSize,
						PDUMP_FLAGS_CONTINUOUS);

	psSigBufCtl->ui32LeftSizeInRegs = ui32SigChecksBufSize / sizeof(IMG_UINT32);

	return PVRSRV_OK;
}

#if defined(RGXFW_ALIGNCHECKS)
/*!
*******************************************************************************
 @Function		RGXFWSetupAlignChecks
 @Description	
 @Input			psDevInfo
 
 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXFWSetupAlignChecks(PVRSRV_RGXDEV_INFO* psDevInfo, 
								RGXFWIF_DEV_VIRTADDR	*psAlignChecksDevFW,
								IMG_UINT32				*pui32RGXFWAlignChecks,
								IMG_UINT32				ui32RGXFWAlignChecksSize)
{
	IMG_UINT32		aui32RGXFWAlignChecksKM[] = { RGXFW_ALIGN_CHECKS_INIT_KM };
	IMG_UINT32		ui32RGXFWAlingChecksTotal = sizeof(aui32RGXFWAlignChecksKM) + ui32RGXFWAlignChecksSize;
	IMG_UINT32*		paui32AlignChecks;
	PVRSRV_ERROR	eError;

	/* Allocate memory for the checks */
	PDUMPCOMMENT("Allocate memory for alignment checks");
	eError = DevmemFwAllocate(psDevInfo,
							ui32RGXFWAlingChecksTotal,
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | PVRSRV_MEMALLOCFLAG_UNCACHED,
							"AlignmentChecks",
							&psDevInfo->psRGXFWAlignChecksMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for alignment checks (%u)",
					ui32RGXFWAlingChecksTotal,
					eError));
		goto failAlloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc,
									(IMG_VOID **)&paui32AlignChecks);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel addr for alignment checks (%u)",
					eError));
		goto failAqCpuAddr;
	}

	/* Copy the values */
	OSMemCopy(paui32AlignChecks, &aui32RGXFWAlignChecksKM[0], sizeof(aui32RGXFWAlignChecksKM));
	paui32AlignChecks += sizeof(aui32RGXFWAlignChecksKM)/sizeof(IMG_UINT32);

	OSMemCopy(paui32AlignChecks, pui32RGXFWAlignChecks, ui32RGXFWAlignChecksSize);

	DevmemPDumpLoadMem(	psDevInfo->psRGXFWAlignChecksMemDesc,
						0,
						ui32RGXFWAlingChecksTotal,
						PDUMP_FLAGS_CONTINUOUS);

	/* Prepare the pointer for the fw to access that memory */
	RGXSetFirmwareAddress(psAlignChecksDevFW,
						  psDevInfo->psRGXFWAlignChecksMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	return PVRSRV_OK;




failAqCpuAddr:
	DevmemFwFree(psDevInfo->psRGXFWAlignChecksMemDesc);
failAlloc:

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static IMG_VOID RGXFWFreeAlignChecks(PVRSRV_RGXDEV_INFO* psDevInfo)
{
	if (psDevInfo->psRGXFWAlignChecksMemDesc != IMG_NULL)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc);
		DevmemFwFree(psDevInfo->psRGXFWAlignChecksMemDesc);
		psDevInfo->psRGXFWAlignChecksMemDesc = IMG_NULL;
	}
}
#endif


IMG_VOID RGXSetFirmwareAddress(RGXFWIF_DEV_VIRTADDR	*ppDest,
							   DEVMEM_MEMDESC		*psSrc,
							   IMG_UINT32			uiExtraOffset,
							   IMG_UINT32			ui32Flags)
{
	PVRSRV_ERROR		eError;
	IMG_DEV_VIRTADDR	psDevVirtAddr;
	IMG_UINT64			ui64Offset;
	IMG_BOOL            bCachedInMETA;
	DEVMEM_FLAGS_T      uiDevFlags;

	eError = DevmemAcquireDevVirtAddr(psSrc, &psDevVirtAddr);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Convert to an address in META memmap */
	ui64Offset = psDevVirtAddr.uiAddr + uiExtraOffset - RGX_FIRMWARE_HEAP_BASE;

	/* The biggest offset for the Shared region that can be addressed */
	PVR_ASSERT(ui64Offset < 3*RGXFW_SEGMMU_DMAP_SIZE);

	/* Check in the devmem flags whether this memory is cached/uncached */
	DevmemGetFlags(psSrc, &uiDevFlags);

	/* Honour the META cache flags */	
	bCachedInMETA = (PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) & uiDevFlags) != 0;
	
#if defined(HW_ERN_45914)
	/* We only cache in META if it's also cached in the SLC */
	{
		IMG_BOOL bCachedInSLC = (DevmemDeviceCacheMode(uiDevFlags) == PVRSRV_MEMALLOCFLAG_GPU_CACHED);

		bCachedInMETA = bCachedInMETA && bCachedInSLC;
	}
#endif

	if (bCachedInMETA)
	{
		ppDest->ui32Addr = ((IMG_UINT32) ui64Offset) | RGXFW_BOOTLDR_META_ADDR;
	}
	else
	{
		ppDest->ui32Addr = ((IMG_UINT32) ui64Offset) | RGXFW_SEGMMU_DMAP_ADDR_START;
	}

	if (ui32Flags & RFW_FWADDR_NOREF_FLAG)
	{
		DevmemReleaseDevVirtAddr(psSrc);
	}
}

#if defined(RGX_FEATURE_META_DMA)
IMG_VOID RGXSetMetaDMAAddress(RGXFWIF_DMA_ADDR		*psDest,
							  DEVMEM_MEMDESC		*psSrcMemDesc,
							  RGXFWIF_DEV_VIRTADDR	*psSrcFWDevVAddr,
							  IMG_UINT32			uiOffset)
{
	PVRSRV_ERROR		eError;
	IMG_DEV_VIRTADDR	sDevVirtAddr;

	eError = DevmemAcquireDevVirtAddr(psSrcMemDesc, &sDevVirtAddr);
	PVR_ASSERT(eError == PVRSRV_OK);

	psDest->psDevVirtAddr.uiAddr = sDevVirtAddr.uiAddr;
	psDest->psDevVirtAddr.uiAddr += uiOffset;
	psDest->pbyFWAddr.ui32Addr = psSrcFWDevVAddr->ui32Addr;

	DevmemReleaseDevVirtAddr(psSrcMemDesc);
}
#endif

IMG_VOID RGXUnsetFirmwareAddress(DEVMEM_MEMDESC *psSrc)
{
	DevmemReleaseDevVirtAddr(psSrc);
}

struct _RGX_SERVER_COMMON_CONTEXT_ {
	DEVMEM_MEMDESC *psFWCommonContextMemDesc;
	PRGXFWIF_FWCOMMONCONTEXT sFWCommonContextFWAddr;
	DEVMEM_MEMDESC *psFWMemContextMemDesc;
	DEVMEM_MEMDESC *psFWFrameworkMemDesc;
	DEVMEM_MEMDESC *psContextStateMemDesc;
	RGX_CLIENT_CCB *psClientCCB;
	DEVMEM_MEMDESC *psClientCCBMemDesc;
	DEVMEM_MEMDESC *psClientCCBCtrlMemDesc;
	IMG_BOOL bCommonContextMemProvided;
	IMG_UINT32 ui32ContextID;
	DLLIST_NODE sListNode;
	RGXFWIF_CONTEXT_RESET_REASON eLastResetReason;
};

PVRSRV_ERROR FWCommonContextAllocate(CONNECTION_DATA *psConnection,
									 PVRSRV_DEVICE_NODE *psDeviceNode,
									 const IMG_CHAR *pszContextName,
									 DEVMEM_MEMDESC *psAllocatedMemDesc,
									 IMG_UINT32 ui32AllocatedOffset,
									 DEVMEM_MEMDESC *psFWMemContextMemDesc,
									 DEVMEM_MEMDESC *psContextStateMemDesc,
									 IMG_UINT32 ui32CCBAllocSize,
									 IMG_UINT32 ui32Priority,
									 RGX_COMMON_CONTEXT_INFO *psInfo,
									 RGX_SERVER_COMMON_CONTEXT **ppsServerCommonContext)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_COMMON_CONTEXT *psServerCommonContext;
	RGXFWIF_FWCOMMONCONTEXT *psFWCommonContext;
	IMG_UINT32 ui32FWCommonContextOffset;
	IMG_UINT8 *pui8Ptr;
	PVRSRV_ERROR eError;

	/*
		Allocate all the resources that are required
	*/
	psServerCommonContext = OSAllocMem(sizeof(*psServerCommonContext));
	if (psServerCommonContext == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc;
	}

	if (psAllocatedMemDesc)
	{
		PDUMPCOMMENT("Using existing MemDesc for Rogue firmware %s context (offset = %d)",
					 pszContextName,
					 ui32AllocatedOffset);
		ui32FWCommonContextOffset = ui32AllocatedOffset;
		psServerCommonContext->psFWCommonContextMemDesc = psAllocatedMemDesc;
		psServerCommonContext->bCommonContextMemProvided = IMG_TRUE;
	}
	else
	{
		/* Allocate device memory for the firmware context */
		PDUMPCOMMENT("Allocate Rogue firmware %s context", pszContextName);
		eError = DevmemFwAllocate(psDevInfo,
								sizeof(*psFWCommonContext),
								RGX_FWCOMCTX_ALLOCFLAGS,
								"FirmwareContext",
								&psServerCommonContext->psFWCommonContextMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s : Failed to allocate firmware %s context (%s)",
									__FUNCTION__,
									pszContextName,
									PVRSRVGetErrorStringKM(eError)));
			goto fail_contextalloc;
		}
		ui32FWCommonContextOffset = 0;
		psServerCommonContext->bCommonContextMemProvided = IMG_FALSE;
	}

	/* Record this context so we can refer to it if the FW needs to tell us it was reset. */
	psServerCommonContext->eLastResetReason    = RGXFWIF_CONTEXT_RESET_REASON_NONE;
	psServerCommonContext->ui32ContextID       = psDevInfo->ui32CommonCtxtCurrentID++;

	/* Allocate the client CCB */
	eError = RGXCreateCCB(psDeviceNode,
						  ui32CCBAllocSize,
						  psConnection,
						  pszContextName,
						  psServerCommonContext,
						  &psServerCommonContext->psClientCCB,
						  &psServerCommonContext->psClientCCBMemDesc,
						  &psServerCommonContext->psClientCCBCtrlMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: failed to create CCB for %s context(%s)",
								__FUNCTION__,
								pszContextName,
								PVRSRVGetErrorStringKM(eError)));
		goto fail_allocateccb;
	}

	/*
		Temporarily map the firmware context to the kernel and init it
	*/
	eError = DevmemAcquireCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc,
                                      (IMG_VOID **)&pui8Ptr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware %s context (%s)to CPU",
								__FUNCTION__,
								pszContextName,
								PVRSRVGetErrorStringKM(eError)));
		goto fail_cpuvirtacquire;
	}

	psFWCommonContext = (RGXFWIF_FWCOMMONCONTEXT *) (pui8Ptr + ui32FWCommonContextOffset);

	/* Set the firmware CCB device addresses in the firmware common context */
	RGXSetFirmwareAddress(&psFWCommonContext->psCCB,
						  psServerCommonContext->psClientCCBMemDesc,
						  0, RFW_FWADDR_FLAG_NONE);
	RGXSetFirmwareAddress(&psFWCommonContext->psCCBCtl,
						  psServerCommonContext->psClientCCBCtrlMemDesc,
						  0, RFW_FWADDR_FLAG_NONE);

	/* Set the memory context device address */
	psServerCommonContext->psFWMemContextMemDesc = psFWMemContextMemDesc;
	RGXSetFirmwareAddress(&psFWCommonContext->psFWMemContext,
						  psFWMemContextMemDesc,
						  0, RFW_FWADDR_FLAG_NONE);

	/* Set the framework register updates address */
	psServerCommonContext->psFWFrameworkMemDesc = psInfo->psFWFrameworkMemDesc;
	RGXSetFirmwareAddress(&psFWCommonContext->psRFCmd,
						  psInfo->psFWFrameworkMemDesc,
						  0, RFW_FWADDR_FLAG_NONE);

	psFWCommonContext->ui32Priority = ui32Priority;
	psFWCommonContext->ui32PrioritySeqNum = 0;

	if(psInfo->psMCUFenceAddr != IMG_NULL)
	{
		psFWCommonContext->ui64MCUFenceAddr = psInfo->psMCUFenceAddr->uiAddr;
	}

	/* Store a references to Server Common Context and PID for notifications back from the FW. */
	psFWCommonContext->ui32ServerCommonContextID = psServerCommonContext->ui32ContextID;
	psFWCommonContext->ui32PID                   = OSGetCurrentProcessID();

	/* Set the firmware GPU context state buffer */
	psServerCommonContext->psContextStateMemDesc = psContextStateMemDesc;
	if (psContextStateMemDesc)
	{
		RGXSetFirmwareAddress(&psFWCommonContext->psContextState,
							  psContextStateMemDesc,
							  0,
							  RFW_FWADDR_FLAG_NONE);
	}

	/*
	 * Dump the created context
	 */
	PDUMPCOMMENT("Dump %s context", pszContextName);
	DevmemPDumpLoadMem(psServerCommonContext->psFWCommonContextMemDesc,
					   ui32FWCommonContextOffset,
					   sizeof(*psFWCommonContext),
					   PDUMP_FLAGS_CONTINUOUS);

	/* We've finished the setup so release the CPU mapping */
	DevmemReleaseCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc);

	/* Map this allocation into the FW */
	RGXSetFirmwareAddress(&psServerCommonContext->sFWCommonContextFWAddr,
						  psServerCommonContext->psFWCommonContextMemDesc,
						  ui32FWCommonContextOffset,
						  RFW_FWADDR_FLAG_NONE);

#if defined(LINUX)
	trace_rogue_create_fw_context(OSGetCurrentProcessName(),
								  pszContextName,
								  psServerCommonContext->sFWCommonContextFWAddr.ui32Addr);
#endif
	/*Add the node to the list when finalised */
	dllist_add_to_tail(&(psDevInfo->sCommonCtxtListHead), &(psServerCommonContext->sListNode));

	*ppsServerCommonContext = psServerCommonContext;
	return PVRSRV_OK;

fail_allocateccb:
	DevmemReleaseCpuVirtAddr(psServerCommonContext->psFWCommonContextMemDesc);
fail_cpuvirtacquire:
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWCommonContextMemDesc);
	if (!psServerCommonContext->bCommonContextMemProvided)
	{
		DevmemFwFree(psServerCommonContext->psFWCommonContextMemDesc);
	}
fail_contextalloc:
	OSFreeMem(psServerCommonContext);
fail_alloc:
	return eError;
}

IMG_VOID FWCommonContextFree(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{

	/* Remove the context from the list of all contexts. */
	dllist_remove_node(&psServerCommonContext->sListNode);

	/*
		Unmap the context itself and then all it's resources
	*/

	/* Unmap the FW common context */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWCommonContextMemDesc);
	/* Umap context state buffer (if there was one) */
	if (psServerCommonContext->psContextStateMemDesc)
	{
		RGXUnsetFirmwareAddress(psServerCommonContext->psContextStateMemDesc);
	}
	/* Unmap the framework buffer */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWFrameworkMemDesc);
	/* Unmap client CCB and CCB control */
	RGXUnsetFirmwareAddress(psServerCommonContext->psClientCCBCtrlMemDesc);
	RGXUnsetFirmwareAddress(psServerCommonContext->psClientCCBMemDesc);
	/* Unmap the memory context */
	RGXUnsetFirmwareAddress(psServerCommonContext->psFWMemContextMemDesc);

	/* Destroy the client CCB */
	RGXDestroyCCB(psServerCommonContext->psClientCCB);
	

	/* Free the FW common context (if there was one) */
	if (!psServerCommonContext->bCommonContextMemProvided)
	{
		DevmemFwFree(psServerCommonContext->psFWCommonContextMemDesc);
	}
	/* Free the hosts representation of the common context */
	OSFreeMem(psServerCommonContext);
}

PRGXFWIF_FWCOMMONCONTEXT FWCommonContextGetFWAddress(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	return psServerCommonContext->sFWCommonContextFWAddr;
}

RGX_CLIENT_CCB *FWCommonContextGetClientCCB(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	return psServerCommonContext->psClientCCB;
}

RGXFWIF_CONTEXT_RESET_REASON FWCommonContextGetLastResetReason(RGX_SERVER_COMMON_CONTEXT *psServerCommonContext)
{
	RGXFWIF_CONTEXT_RESET_REASON  eLastResetReason;
	
	PVR_ASSERT(psServerCommonContext != IMG_NULL);
	
	/* Take the most recent reason and reset for next time... */
	eLastResetReason = psServerCommonContext->eLastResetReason;
	psServerCommonContext->eLastResetReason = RGXFWIF_CONTEXT_RESET_REASON_NONE;

	return eLastResetReason;
}

/*!
*******************************************************************************
 @Function		RGXFreeKernelCCB
 @Description	Free a kernel CCB
 @Input			psDevInfo
 @Input			eKCCBType
 
 @Return		PVRSRV_ERROR
******************************************************************************/
static IMG_VOID RGXFreeKernelCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM				eKCCBType)
{
	if (psDevInfo->apsKernelCCBMemDesc[eKCCBType] != IMG_NULL)
	{
		if (psDevInfo->apsKernelCCB[eKCCBType] != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->apsKernelCCBMemDesc[eKCCBType]);
			psDevInfo->apsKernelCCB[eKCCBType] = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->apsKernelCCBMemDesc[eKCCBType]);
		psDevInfo->apsKernelCCBMemDesc[eKCCBType] = IMG_NULL;
	}
	if (psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType] != IMG_NULL)
	{
		if (psDevInfo->apsKernelCCBCtl[eKCCBType] != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
			psDevInfo->apsKernelCCBCtl[eKCCBType] = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);
		psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType] = IMG_NULL;
	}
}

/*!
*******************************************************************************
 @Function		RGXSetupKernelCCB
 @Description	Allocate and initialise a kernel CCB
 @Input			psDevInfo
 
 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXSetupKernelCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo, 
									  RGXFWIF_INIT			*psRGXFWInit,
									  RGXFWIF_DM			eKCCBType,
									  IMG_UINT32			ui32NumCmdsLog2,
									  IMG_UINT32			ui32CmdSize)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psKCCBCtl;
	DEVMEM_FLAGS_T		uiCCBCtlMemAllocFlags, uiCCBMemAllocFlags;
	IMG_UINT32			ui32kCCBSize = (1U << ui32NumCmdsLog2);


	/*
	 * FIXME: the write offset need not be writeable by the firmware, indeed may
	 * not even be needed for reading. Consider moving it to its own data
	 * structure.
	 */
	uiCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_UNCACHED | 
							 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocation flags for Kernel CCB */
	uiCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						 PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) |
						 PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						 PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						 PVRSRV_MEMALLOCFLAG_UNCACHED | 
						 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/*
		Allocate memory for the kernel CCB control.
	*/
	PDUMPCOMMENT("Allocate memory for kernel CCB control %u", eKCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_CCB_CTL),
							uiCCBCtlMemAllocFlags,
							"KernelCCBControl",
                            &psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to allocate kernel CCB ctl %u (%u)",
				eKCCBType, eError));
		goto fail;
	}

	/*
		Allocate memory for the kernel CCB.
		(this will reference further command data in non-shared CCBs)
	*/
	PDUMPCOMMENT("Allocate memory for kernel CCB %u", eKCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							ui32kCCBSize * ui32CmdSize,
							uiCCBMemAllocFlags,
							"KernelCCB",
                            &psDevInfo->apsKernelCCBMemDesc[eKCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to allocate kernel CCB %u (%u)",
				eKCCBType, eError));
		goto fail;
	}

	/*
		Map the kernel CCB control to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
                                      (IMG_VOID **)&psDevInfo->apsKernelCCBCtl[eKCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to acquire cpu kernel CCB Ctl %u (%u)",
				eKCCBType, eError));
		goto fail;
	}

	/*
		Map the kernel CCB to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsKernelCCBMemDesc[eKCCBType],
                                      (IMG_VOID **)&psDevInfo->apsKernelCCB[eKCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupKernelCCB: Failed to acquire cpu kernel CCB %u (%u)",
				eKCCBType, eError));
		goto fail;
	}

	/*
	 * Initialise the kernel CCB control.
	 */
	psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
	psKCCBCtl->ui32WriteOffset = 0;
	psKCCBCtl->ui32ReadOffset = 0;
	psKCCBCtl->ui32WrapMask = ui32kCCBSize - 1;
	psKCCBCtl->ui32CmdSize = ui32CmdSize;

	/*
	 * Set-up RGXFWIfCtl pointers to access the kCCBs
	 */
	RGXSetFirmwareAddress(&psRGXFWInit->psKernelCCBCtl[eKCCBType],
						  psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	RGXSetFirmwareAddress(&psRGXFWInit->psKernelCCB[eKCCBType],
						  psDevInfo->apsKernelCCBMemDesc[eKCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	psRGXFWInit->eDM[eKCCBType] = eKCCBType;

	/*
	 * Pdump the kernel CCB control.
	 */
	PDUMPCOMMENT("Initialise kernel CCB ctl %d", eKCCBType);
	DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
					   0,
					   sizeof(RGXFWIF_CCB_CTL),
					   0);

	return PVRSRV_OK;

fail:
	RGXFreeKernelCCB(psDevInfo, eKCCBType);

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************
 @Function		RGXFreeFirmwareCCB
 @Description	Free a firmware CCB
 @Input			psDevInfo
 @Input			eFWCCBType

 @Return		PVRSRV_ERROR
******************************************************************************/
static IMG_VOID RGXFreeFirmwareCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM				eFWCCBType)
{
	if (psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType] != IMG_NULL)
	{
		if (psDevInfo->apsFirmwareCCB[eFWCCBType] != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);
			psDevInfo->apsFirmwareCCB[eFWCCBType] = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);
		psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType] = IMG_NULL;
	}
	if (psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType] != IMG_NULL)
	{
		if (psDevInfo->apsFirmwareCCBCtl[eFWCCBType] != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
			psDevInfo->apsFirmwareCCBCtl[eFWCCBType] = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);
		psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType] = IMG_NULL;
	}
}

/*!
*******************************************************************************
 @Function		RGXSetupFirmwareCCB
 @Description	Allocate and initialise a Firmware CCB
 @Input			psDevInfo

 @Return		PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXSetupFirmwareCCB(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									  RGXFWIF_INIT			*psRGXFWInit,
									  RGXFWIF_DM			eFWCCBType,
									  IMG_UINT32			ui32NumCmdsLog2,
									  IMG_UINT32			ui32CmdSize)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psFWCCBCtl;
	DEVMEM_FLAGS_T		uiCCBCtlMemAllocFlags, uiCCBMemAllocFlags;
	IMG_UINT32			ui32FWCCBSize = (1U << ui32NumCmdsLog2);

	/*
	 * FIXME: the write offset need not be writeable by the host, indeed may
	 * not even be needed for reading. Consider moving it to its own data
	 * structure.
	 */
	uiCCBCtlMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_UNCACHED |
							 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocation flags for Firmware CCB */
	uiCCBMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						 PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						 PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						 PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						 PVRSRV_MEMALLOCFLAG_UNCACHED |
						 PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/*
		Allocate memory for the Firmware CCB control.
	*/
	PDUMPCOMMENT("Allocate memory for firmware CCB control %u", eFWCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_CCB_CTL),
							uiCCBCtlMemAllocFlags,
							"FirmwareCCBControl",
                            &psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to allocate Firmware CCB ctl %u (%u)",
				eFWCCBType, eError));
		goto fail;
	}

	/*
		Allocate memory for the Firmware CCB.
		(this will reference further command data in non-shared CCBs)
	*/
	PDUMPCOMMENT("Allocate memory for firmware CCB %u", eFWCCBType);
	eError = DevmemFwAllocate(psDevInfo,
							ui32FWCCBSize * ui32CmdSize,
							uiCCBMemAllocFlags,
							"FirmwareCCB",
                            &psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType]);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to allocate Firmware CCB %u (%u)",
				eFWCCBType, eError));
		goto fail;
	}

	/*
		Map the Firmware CCB control to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
                                      (IMG_VOID **)&psDevInfo->apsFirmwareCCBCtl[eFWCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to acquire cpu firmware CCB Ctl %u (%u)",
				eFWCCBType, eError));
		goto fail;
	}

	/*
		Map the firmware CCB to the kernel.
	*/
	eError = DevmemAcquireCpuVirtAddr(psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType],
                                      (IMG_VOID **)&psDevInfo->apsFirmwareCCB[eFWCCBType]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmwareCCB: Failed to acquire cpu firmware CCB %u (%u)",
				eFWCCBType, eError));
		goto fail;
	}

	/*
	 * Initialise the firmware CCB control.
	 */
	psFWCCBCtl = psDevInfo->apsFirmwareCCBCtl[eFWCCBType];
	psFWCCBCtl->ui32WriteOffset = 0;
	psFWCCBCtl->ui32ReadOffset = 0;
	psFWCCBCtl->ui32WrapMask = ui32FWCCBSize - 1;
	psFWCCBCtl->ui32CmdSize = ui32CmdSize;

	/*
	 * Set-up RGXFWIfCtl pointers to access the kCCBs
	 */
	RGXSetFirmwareAddress(&psRGXFWInit->psFirmwareCCBCtl[eFWCCBType],
						  psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	RGXSetFirmwareAddress(&psRGXFWInit->psFirmwareCCB[eFWCCBType],
						  psDevInfo->apsFirmwareCCBMemDesc[eFWCCBType],
						  0, RFW_FWADDR_NOREF_FLAG);

	psRGXFWInit->eDM[eFWCCBType] = eFWCCBType;

	/*
	 * Pdump the kernel CCB control.
	 */
	PDUMPCOMMENT("Initialise firmware CCB ctl %d", eFWCCBType);
	DevmemPDumpLoadMem(psDevInfo->apsFirmwareCCBCtlMemDesc[eFWCCBType],
					   0,
					   sizeof(RGXFWIF_CCB_CTL),
					   0);

	return PVRSRV_OK;

fail:
	RGXFreeFirmwareCCB(psDevInfo, eFWCCBType);

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static IMG_VOID RGXSetupFaultReadRegisterRollback(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PMR *psPMR;
	
	if (psDevInfo->psRGXFaultAddressMemDesc)
	{
		if (DevmemServerGetImportHandle(psDevInfo->psRGXFaultAddressMemDesc,(IMG_VOID **)&psPMR) == PVRSRV_OK)
		{
			PMRUnlockSysPhysAddresses(psPMR);
		}
		DevmemFwFree(psDevInfo->psRGXFaultAddressMemDesc);
		psDevInfo->psRGXFaultAddressMemDesc = IMG_NULL;
	}
}

static PVRSRV_ERROR RGXSetupFaultReadRegister(PVRSRV_DEVICE_NODE	*psDeviceNode, RGXFWIF_INIT *psRGXFWInit)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			*pui32MemoryVirtAddr;
	IMG_UINT32			i;
	IMG_SIZE_T			ui32PageSize;
	DEVMEM_FLAGS_T		uiMemAllocFlags;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psDeviceNode->pvDevice;
	PMR					*psPMR;

	ui32PageSize = OSGetPageSize();

	/* Allocate page of memory to use for page faults on non-blocking memory transactions */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED;
	
	psDevInfo->psRGXFaultAddressMemDesc = IMG_NULL;
	eError = DevmemFwAllocateExportable(psDeviceNode,
										ui32PageSize,
										uiMemAllocFlags,
										"FaultAddress",
										&psDevInfo->psRGXFaultAddressMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to allocate mem for fault address (%u)",
				eError));
		goto failFaultAddressDescAlloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFaultAddressMemDesc,
									  (IMG_VOID **)&pui32MemoryVirtAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire mem for fault adress (%u)",
				eError));
		goto failFaultAddressDescAqCpuVirt;
	}

	for (i = 0; i < ui32PageSize/sizeof(IMG_UINT32); i++)
	{
		*(pui32MemoryVirtAddr + i) = 0xDEADBEEF;
	}

	eError = DevmemServerGetImportHandle(psDevInfo->psRGXFaultAddressMemDesc,(IMG_VOID **)&psPMR);
		
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error getting PMR for fault adress (%u)",
				eError));
		
		goto failFaultAddressDescGetPMR;
	}
	else
	{
		IMG_BOOL bValid;
		IMG_UINT32 ui32Log2PageSize = OSGetPageShift();
		
		eError = PMRLockSysPhysAddresses(psPMR,ui32Log2PageSize);
			
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error locking physical address for fault address MemDesc (%u)",
					eError));
			
			goto failFaultAddressDescLockPhys;
		}
			
		eError = PMR_DevPhysAddr(psPMR,ui32Log2PageSize,1,0,&(psRGXFWInit->sFaultPhysAddr),&bValid);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Error getting physical address for fault address MemDesc (%u)",
					eError));
			
			goto failFaultAddressDescGetPhys;
		}

		if (!bValid)
		{
			psRGXFWInit->sFaultPhysAddr.uiAddr = 0;
			PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed getting physical address for fault address MemDesc - invalid page (0x%llX)",
					psRGXFWInit->sFaultPhysAddr.uiAddr));

			goto failFaultAddressDescGetPhys;
		}
	}

	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFaultAddressMemDesc);
	
	return PVRSRV_OK;

failFaultAddressDescGetPhys:
	PMRUnlockSysPhysAddresses(psPMR);

failFaultAddressDescLockPhys:

failFaultAddressDescGetPMR:
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFaultAddressMemDesc);

failFaultAddressDescAqCpuVirt:
	DevmemFwFree(psDevInfo->psRGXFaultAddressMemDesc);
	psDevInfo->psRGXFaultAddressMemDesc = IMG_NULL;

failFaultAddressDescAlloc:

	return eError;
}

static PVRSRV_ERROR RGXHwBrn37200(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;

#if defined(FIX_HW_BRN_37200)
	struct _DEVMEM_HEAP_	*psBRNHeap;
	DEVMEM_FLAGS_T			uiFlags;
	IMG_DEV_VIRTADDR		sTmpDevVAddr;
	IMG_SIZE_T				uiPageSize;

	uiPageSize = OSGetPageSize();
	
	uiFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
				PVRSRV_MEMALLOCFLAG_GPU_READABLE | 
				PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
				PVRSRV_MEMALLOCFLAG_CPU_READABLE |
				PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
				PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | 
				PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
				PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
				PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx,
							  "HWBRN37200", /* FIXME: We need to create an IDENT macro for this string.
							                 Make sure the IDENT macro is not accessible to userland */
							  &psBRNHeap);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: HWBRN37200 Failed DevmemFindHeapByName (%u)", eError));
		goto failFWHWBRN37200FindHeapByName;
	}

	psDevInfo->psRGXFWHWBRN37200MemDesc = IMG_NULL;
	eError = DevmemAllocate(psBRNHeap,
						uiPageSize,
						ROGUE_CACHE_LINE_SIZE,
						uiFlags,
						"HWBRN37200",
						&psDevInfo->psRGXFWHWBRN37200MemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: Failed to allocate %u bytes for HWBRN37200 (%u)",
				(IMG_UINT32)uiPageSize,
				eError));
		goto failFWHWBRN37200MemDescAlloc;
	}
		
	/*
		We need to map it so the heap for this allocation
		is set
	*/
	eError = DevmemMapToDevice(psDevInfo->psRGXFWHWBRN37200MemDesc,
						   psBRNHeap,
						   &sTmpDevVAddr);
		
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXHwBrn37200: Failed to allocate %u bytes for HWBRN37200 (%u)",
				(IMG_UINT32)uiPageSize,
				eError));
		goto failFWHWBRN37200DevmemMapToDevice;
	}

	return PVRSRV_OK;

failFWHWBRN37200DevmemMapToDevice:

failFWHWBRN37200MemDescAlloc:
	DevmemFwFree(psDevInfo->psRGXFWHWBRN37200MemDesc);
	psDevInfo->psRGXFWHWBRN37200MemDesc = IMG_NULL;

failFWHWBRN37200FindHeapByName:
#endif

	return eError;
}

/*!
*******************************************************************************

 @Function	RGXSetupFirmware

 @Description

 Setups all the firmware related data

 @Input psDevInfo

 @Return PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR RGXSetupFirmware(PVRSRV_DEVICE_NODE	*psDeviceNode, 
							     IMG_BOOL			bEnableSignatureChecks,
							     IMG_UINT32			ui32SignatureChecksBufSize,
							     IMG_UINT32			ui32HWPerfFWBufSizeKB,
							     IMG_UINT64		 	ui64HWPerfFilter,
							     IMG_UINT32			ui32RGXFWAlignChecksSize,
							     IMG_UINT32			*pui32RGXFWAlignChecks,
							     IMG_UINT32			ui32ConfigFlags,
							     IMG_UINT32			ui32LogType,
							     IMG_UINT32            ui32NumTilingCfgs,
							     IMG_UINT32            *pui32BIFTilingXStrides,
							     IMG_UINT32			ui32FilterFlags,
							     IMG_UINT32			ui32JonesDisableMask,
							     IMG_UINT32			ui32HWRDebugDumpLimit,
								 IMG_UINT32			ui32HWPerfCountersDataSize,
							     RGXFWIF_DEV_VIRTADDR	*psRGXFWInitFWAddr,
							     RGX_RD_POWER_ISLAND_CONF eRGXRDPowerIslandConf)

{
	PVRSRV_ERROR		eError;
	DEVMEM_FLAGS_T		uiMemAllocFlags;
	RGXFWIF_INIT		*psRGXFWInit;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32			dm;
#if defined(RGX_FEATURE_META_DMA)
	RGXFWIF_DEV_VIRTADDR sRGXTmpCorememDataStoreFWAddr;
#endif

	/* Fw init data */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;
						/* FIXME: Change to Cached */

	PDUMPCOMMENT("Allocate RGXFWIF_INIT structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_INIT),
							uiMemAllocFlags,
							"FirmwareInitStructure",
							&psDevInfo->psRGXFWIfInitMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for fw if ctl (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_INIT),
				eError));
		goto fail;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc,
									  (IMG_VOID **)&psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel fw if ctl (%u)",
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(psRGXFWInitFWAddr,
						psDevInfo->psRGXFWIfInitMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	/* FW Trace buffer */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate rgxfw trace structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_TRACEBUF),
							uiMemAllocFlags,
							"FirmwareTraceStructure",
							&psDevInfo->psRGXFWIfTraceBufCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for fw trace (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_TRACEBUF),
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psTraceBufCtl,
						psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfTraceBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel tracebuf ctl (%u)",
				eError));
		goto fail;
	}

	/* Determine the size of the HWPerf FW buffer */
	if (ui32HWPerfFWBufSizeKB > (RGXFW_HWPERF_L1_SIZE_MAX>>10))
	{
		/* Size specified as a AppHint but it is too big */
		PVR_DPF((PVR_DBG_WARNING,"RGXSetupFirmware: HWPerfFWBufSizeInKB value (%u) too big, using maximum (%u)",
				ui32HWPerfFWBufSizeKB, RGXFW_HWPERF_L1_SIZE_MAX>>10));
		psDevInfo->ui32RGXFWIfHWPerfBufSize = RGXFW_HWPERF_L1_SIZE_MAX;
		
	}
	else if (ui32HWPerfFWBufSizeKB > (RGXFW_HWPERF_L1_SIZE_MIN>>10))
	{
		/* Size specified as in AppHint HWPerfFWBufSizeInKB */
		PVR_DPF((PVR_DBG_WARNING,"RGXSetupFirmware: Using HWPerf FW buffer size of %u KB",
				ui32HWPerfFWBufSizeKB));
		psDevInfo->ui32RGXFWIfHWPerfBufSize = ui32HWPerfFWBufSizeKB<<10;
	}
	else if (ui32HWPerfFWBufSizeKB > 0)
	{
		/* Size specified as a AppHint but it is too small */
		PVR_DPF((PVR_DBG_WARNING,"RGXSetupFirmware: HWPerfFWBufSizeInKB value (%u) too small, using minimum (%u)",
				ui32HWPerfFWBufSizeKB, RGXFW_HWPERF_L1_SIZE_MIN>>10));
		psDevInfo->ui32RGXFWIfHWPerfBufSize = RGXFW_HWPERF_L1_SIZE_MIN;
	}
	else
	{
		/* 0 size implies AppHint not set or is set to zero,
		 * use default size from driver constant. */
		psDevInfo->ui32RGXFWIfHWPerfBufSize = RGXFW_HWPERF_L1_SIZE_DEFAULT;
	}

	/* Allocate HWPerf FW L1 buffer */
	eError = DevmemFwAllocate(psDevInfo,
							  psDevInfo->ui32RGXFWIfHWPerfBufSize+RGXFW_HWPERF_L1_PADDING_DEFAULT,
							  uiMemAllocFlags,
							  "FirmwareHWPerfBuffer",
							  &psDevInfo->psRGXFWIfHWPerfBufMemDesc);
	
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to allocate kernel fw hwperf buffer (%u)",
				 eError));
		goto fail;
	}

	/* Meta cached flag removed from this allocation as it was found
	 * FW performance was better without it. */
	RGXSetFirmwareAddress(&psRGXFWInit->psHWPerfInfoCtl,
						  psDevInfo->psRGXFWIfHWPerfBufMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfBufMemDesc,
									  (IMG_VOID**)&psDevInfo->psRGXFWIfHWPerfBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to acquire kernel hwperf buffer (%u)",
				 eError));
		goto fail;
	}


	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE | 
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	/* Allocate buffer to store FW data */
	eError = DevmemFwAllocate(psDevInfo,
							  RGX_META_COREMEM_DATA_SIZE,
							  uiMemAllocFlags,
							  "FirmwareCorememDataStore",
							  &psDevInfo->psRGXFWIfCorememDataStoreMemDesc);
	
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSetupFirmware: Failed to allocate coremem data store (%u)",
				 eError));
		goto fail;
	}

#if defined(RGX_FEATURE_META_DMA)
	RGXSetFirmwareAddress(&sRGXTmpCorememDataStoreFWAddr,
						  psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);

	RGXSetMetaDMAAddress(&psRGXFWInit->sCorememDataStore,
						 psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						 &sRGXTmpCorememDataStoreFWAddr,
						 0);
#else
	RGXSetFirmwareAddress(&psRGXFWInit->sCorememDataStore.pbyFWAddr,
						  psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						  0, RFW_FWADDR_NOREF_FLAG);
#endif

	/* init HW frame info */
	PDUMPCOMMENT("Allocate rgxfw HW info buffer");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_HWRINFOBUF),
							uiMemAllocFlags,
							"FirmwareHWInfoBuffer",
							&psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %d bytes for HW info (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_HWRINFOBUF),
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psRGXFWIfHWRInfoBufCtl,
						psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfHWRInfoBuf);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel tracebuf ctl (%u)",
				eError));
		goto fail;
	}
	OSMemSet(psDevInfo->psRGXFWIfHWRInfoBuf, 0, sizeof(RGXFWIF_HWRINFOBUF));

	/* init HWPERF data */
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfRIdx = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfWIdx = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfWrapCount = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfSize = psDevInfo->ui32RGXFWIfHWPerfBufSize;
	psRGXFWInit->ui64HWPerfFilter = ui64HWPerfFilter;
	psRGXFWInit->bDisableFilterHWPerfCustomCounter = (ui32ConfigFlags & RGXFWIF_INICFG_HWP_DISABLE_FILTER) ? IMG_TRUE : IMG_FALSE;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfUt = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32HWPerfDropCount = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32FirstDropOrdinal = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32LastDropOrdinal = 0;
	psDevInfo->psRGXFWIfTraceBuf->ui32PowMonEnergy = 0;

	
	/* Initialise the HWPerf module in the Rogue device driver.
	 * May allocate host buffer if HWPerf enabled at driver load time.
	 */
	eError = RGXHWPerfInit(psDeviceNode, (ui32ConfigFlags & RGXFWIF_INICFG_HWPERF_EN));
	PVR_LOGG_IF_ERROR(eError, "RGXHWPerfInit", fail);

	/* Set initial log type */
	if (ui32LogType & ~RGXFWIF_LOG_TYPE_MASK)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Invalid initial log type (0x%X)",ui32LogType));
		goto fail;
	}
	psDevInfo->psRGXFWIfTraceBuf->ui32LogType = ui32LogType;

	/* Allocate shared buffer for GPU utilisation */
	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate shared buffer for GPU utilisation");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_GPU_UTIL_FWCB),
							uiMemAllocFlags,
							"FirmwareGPUUtilisationBuffer",
							&psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for GPU utilisation buffer ctl (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_GPU_UTIL_FWCB),
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psGpuUtilFWCbCtl,
						psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc,
									  (IMG_VOID **)&psDevInfo->psRGXFWIfGpuUtilFWCb);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel GPU utilization FW CB ctl (%u)",
				eError));
		goto fail;
	}

	/* Initialise GPU utilisation buffer */
	psDevInfo->psRGXFWIfGpuUtilFWCb->ui64LastWord =
	    RGXFWIF_GPU_UTIL_MAKE_WORD(OSClockns64(),RGXFWIF_GPU_UTIL_STATE_IDLE);

	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate rgxfw FW runtime configuration (FW)");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_RUNTIME_CFG),
							uiMemAllocFlags,
							"FirmwareFWRuntimeCfg",
							&psDevInfo->psRGXFWIfRuntimeCfgMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for FW runtime configuration (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_RUNTIME_CFG),
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psRuntimeCfg,
						psDevInfo->psRGXFWIfRuntimeCfgMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWIfRuntimeCfgMemDesc,
									(IMG_VOID **)&psDevInfo->psRGXFWIfRuntimeCfg);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to acquire kernel FW runtime configuration (%u)",
				eError));
		goto fail;
	}

#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("Allocate rgxfw register configuration structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_REG_CFG),
							uiMemAllocFlags,
							"Firmware register configuration structure",
							&psDevInfo->psRGXFWIfRegCfgMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate %u bytes for fw register configurations (%u)",
				(IMG_UINT32)sizeof(RGXFWIF_REG_CFG),
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psRegCfg,
						psDevInfo->psRGXFWIfRegCfgMemDesc,
						0, RFW_FWADDR_NOREF_FLAG);
#endif

	uiMemAllocFlags =	PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
						PVRSRV_MEMALLOCFLAG_GPU_READABLE |
						PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
						PVRSRV_MEMALLOCFLAG_CPU_READABLE |
						PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
						PVRSRV_MEMALLOCFLAG_UNCACHED |
						PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;

	PDUMPCOMMENT("Allocate rgxfw hwperfctl structure");
	eError = DevmemFwAllocateExportable(psDeviceNode,
							ui32HWPerfCountersDataSize,
							uiMemAllocFlags,
							"Firmware hwperf control structure",
							&psDevInfo->psRGXFWIfHWPerfCountersMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXInitHWPerfCounters: Failed to allocate %u bytes for fw hwperf control (%u)",
				ui32HWPerfCountersDataSize,
				eError));
		goto fail;
	}

	eError = DevmemExport(psDevInfo->psRGXFWIfHWPerfCountersMemDesc,
	                      &psDevInfo->sRGXFWHWPerfCountersExportCookie);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Failed to export fw hwperf ctl (%u)",
				eError));
		goto fail;
	}

	RGXSetFirmwareAddress(&psRGXFWInit->psHWPerfCtl,
						psDevInfo->psRGXFWIfHWPerfCountersMemDesc,
						0, 0);
	
	/* Allocate a sync for power management */
	eError = SyncPrimContextCreate(IMG_NULL,
									psDevInfo->psDeviceNode,
						  			&psDevInfo->hSyncPrimContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate sync primitive context with error (%u)", eError));
		goto fail;
	}

	eError = SyncPrimAlloc(psDevInfo->hSyncPrimContext, &psDevInfo->psPowSyncPrim, "fw power ack");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate sync primitive with error (%u)", eError));
		goto fail;
	}

	psRGXFWInit->uiPowerSync = SyncPrimGetFirmwareAddr(psDevInfo->psPowSyncPrim);

	/* Required info by FW to calculate the ActivePM idle timer latency */
	{
		RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;
		RGXFWIF_RUNTIME_CFG *psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;

		psRGXFWInit->ui32InitialCoreClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;
		psRGXFWInit->ui32ActivePMLatencyms = psRGXData->psRGXTimingInfo->ui32ActivePMLatencyms;

		/* Initialise variable runtime configuration to the system defaults */
		psRuntimeCfg->ui32CoreClockSpeed = psRGXFWInit->ui32InitialCoreClockSpeed;
		psRuntimeCfg->ui32ActivePMLatencyms = psRGXFWInit->ui32ActivePMLatencyms;
		psRuntimeCfg->bActivePMLatencyPersistant = IMG_TRUE;
	}

	/* Setup Fault read register */
	eError = RGXSetupFaultReadRegister(psDeviceNode, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup fault read register"));
		goto fail;
	}

	/* Apply FIX_HW_BRN_37200 */
	eError = RGXHwBrn37200(psDevInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to apply HWBRN37200"));
		goto fail;
	}

	/*
	 * Set up kernel TA CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_TA, RGXFWIF_KCCB_TA_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel TA CCB"));
		goto fail;
	}

	/*
	 * Set up firmware TA CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_TA, RGXFWIF_FWCCB_TA_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware TA CCB"));
		goto fail;
	}

	/*
	 * Set up kernel 3D CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_3D, RGXFWIF_KCCB_3D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel 3D CCB"));
		goto fail;
	}

	/*
	 * Set up Firmware 3D CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_3D, RGXFWIF_FWCCB_3D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware 3D CCB"));
		goto fail;
	}

	/*
	 * Set up kernel 2D CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_2D, RGXFWIF_KCCB_2D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel 2D CCB"));
		goto fail;
	}
	/*
	 * Set up Firmware 2D CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_2D, RGXFWIF_FWCCB_2D_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware 2D CCB"));
		goto fail;
	}

	/*
	 * Set up kernel compute CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_CDM, RGXFWIF_KCCB_CDM_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel Compute CCB"));
		goto fail;
	}

	/*
	 * Set up Firmware Compute CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_CDM, RGXFWIF_FWCCB_CDM_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware Compute CCB"));
		goto fail;
	}

	/*
	 * Set up kernel general purpose CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_GP, RGXFWIF_KCCB_GP_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel General Purpose CCB"));
		goto fail;
	}
	
	/*
	 * Set up Firmware general purpose CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_GP, RGXFWIF_FWCCB_GP_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware General Purpose CCB"));
		goto fail;
	}
#if defined(RGX_FEATURE_RAY_TRACING)	
	/*
	 * Set up kernel SHG CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_SHG, RGXFWIF_KCCB_SHG_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel SHG CCB"));
		goto fail;
	}

	/*
	 * Set up Firmware SHG CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_SHG, RGXFWIF_FWCCB_SHG_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware SHG CCB"));
		goto fail;
	}
	
	/*
	 * Set up kernel RTU CCB.
	 */
	eError = RGXSetupKernelCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_RTU, RGXFWIF_KCCB_RTU_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_KCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate kernel RTU CCB"));
		goto fail;
	}

	/*
	 * Set up Firmware RTU CCB.
	 */
	eError = RGXSetupFirmwareCCB(psDevInfo,
							   psRGXFWInit,
							   RGXFWIF_DM_RTU, RGXFWIF_FWCCB_RTU_NUMCMDS_LOG2,
							   sizeof(RGXFWIF_FWCCB_CMD));
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate Firmware SHG CCB"));
		goto fail;
	}
#endif

	/* Setup Signature and Checksum Buffers for TA and 3D */
	eError = RGXFWSetupSignatureChecks(psDevInfo,
	                                   &psDevInfo->psRGXFWSigTAChecksMemDesc, 
	                                   ui32SignatureChecksBufSize,
	                                   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_TA],
	                                   "TA");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup TA signature checks"));
		goto fail;
	}
	psDevInfo->ui32SigTAChecksSize = ui32SignatureChecksBufSize;

	eError = RGXFWSetupSignatureChecks(psDevInfo,
	                                   &psDevInfo->psRGXFWSig3DChecksMemDesc, 
	                                   ui32SignatureChecksBufSize,
	                                   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_3D],
	                                   "3D");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup 3D signature checks"));
		goto fail;
	}
	psDevInfo->ui32Sig3DChecksSize = ui32SignatureChecksBufSize;

#if defined(RGX_FEATURE_RAY_TRACING)
	eError = RGXFWSetupSignatureChecks(psDevInfo,
	                                   &psDevInfo->psRGXFWSigRTChecksMemDesc, 
	                                   ui32SignatureChecksBufSize,
	                                   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_RTU],
	                                   "RTU");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup RTU signature checks"));
		goto fail;
	}
	psDevInfo->ui32SigRTChecksSize = ui32SignatureChecksBufSize;
	
	eError = RGXFWSetupSignatureChecks(psDevInfo,
	                                   &psDevInfo->psRGXFWSigSHChecksMemDesc, 
	                                   ui32SignatureChecksBufSize,
	                                   &psRGXFWInit->asSigBufCtl[RGXFWIF_DM_SHG],
	                                   "SHG");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup SHG signature checks"));
		goto fail;
	}
	psDevInfo->ui32SigSHChecksSize = ui32SignatureChecksBufSize;
#endif

#if defined(RGXFW_ALIGNCHECKS)
	eError = RGXFWSetupAlignChecks(psDevInfo, 
								&psRGXFWInit->paui32AlignChecks, 
								pui32RGXFWAlignChecks, 
								ui32RGXFWAlignChecksSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to setup alignment checks"));
		goto fail;
	}
#endif

	/* Fill the remaining bits of fw the init data */
	psRGXFWInit->sPDSExecBase.uiAddr = RGX_PDSCODEDATA_HEAP_BASE;
	psRGXFWInit->sUSCExecBase.uiAddr = RGX_USCCODE_HEAP_BASE;
	psRGXFWInit->sDPXControlStreamBase.uiAddr = RGX_DOPPLER_HEAP_BASE;
	psRGXFWInit->sResultDumpBase.uiAddr = RGX_DOPPLER_OVERFLOW_HEAP_BASE;
	psRGXFWInit->sRTUHeapBase.uiAddr = RGX_DOPPLER_HEAP_BASE;

	/* RD Power Island */
	{
		RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;
		IMG_BOOL bSysEnableRDPowIsland = psRGXData->psRGXTimingInfo->bEnableRDPowIsland;
		IMG_BOOL bEnableRDPowIsland = ((eRGXRDPowerIslandConf == RGX_RD_POWER_ISLAND_DEFAULT) && bSysEnableRDPowIsland) ||
						(eRGXRDPowerIslandConf == RGX_RD_POWER_ISLAND_FORCE_ON);

		ui32ConfigFlags |= bEnableRDPowIsland? RGXFWIF_INICFG_POW_RASCALDUST : 0;
	}

	psRGXFWInit->ui32ConfigFlags = ui32ConfigFlags & RGXFWIF_INICFG_ALL;
	psRGXFWInit->ui32FilterFlags = ui32FilterFlags;
#if defined(RGX_FEATURE_S7_TOP_INFRASTRUCTURE)
	psRGXFWInit->ui32JonesDisableMask = ui32JonesDisableMask;
#endif
	psDevInfo->bPDPEnabled = (ui32ConfigFlags & RGXFWIF_SRVCFG_DISABLE_PDP_EN)
			? IMG_FALSE : IMG_TRUE;
	psRGXFWInit->ui32HWRDebugDumpLimit = ui32HWRDebugDumpLimit;

#if defined(RGX_FEATURE_SLC_VIVT)
	eError = _AllocateSLC3Fence(psDevInfo, psRGXFWInit);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to allocate memory for SLC3Fence"));
		goto fail;
	}
#endif

	/* Timestamps */
	uiMemAllocFlags =
		PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
		PVRSRV_MEMALLOCFLAG_GPU_READABLE | /* XXX ?? */
		PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
		PVRSRV_MEMALLOCFLAG_CPU_READABLE |
		PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
		PVRSRV_MEMALLOCFLAG_UNCACHED |
		PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC;
	
	/*
	  the timer query arrays
	*/
	PDUMPCOMMENT("Allocate timer query arrays (FW)");
	eError = DevmemFwAllocate(psDevInfo,
	                          sizeof(RGXFWIF_TIMESTAMP) * RGX_MAX_TIMER_QUERIES,
	                          uiMemAllocFlags,
	                          "Start times array",
	                          & psDevInfo->psStartTimeMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to map start times array"));
		goto fail;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psStartTimeMemDesc,
	                                  (IMG_VOID **)& psDevInfo->pasStartTimeById);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to map start times array"));
		goto fail;
	}

	eError = DevmemFwAllocate(psDevInfo,
	                          sizeof(RGXFWIF_TIMESTAMP) * RGX_MAX_TIMER_QUERIES,
	                          uiMemAllocFlags,
	                          "End times array",
	                          & psDevInfo->psEndTimeMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to map end times array"));
		goto fail;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psEndTimeMemDesc,
	                                  (IMG_VOID **)& psDevInfo->pasEndTimeById);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to map end times array"));
		goto fail;
	}

	eError = DevmemFwAllocate(psDevInfo,
	                          sizeof(IMG_UINT32) * RGX_MAX_TIMER_QUERIES,
	                          uiMemAllocFlags,
	                          "Completed ops array",
	                          & psDevInfo->psCompletedMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to completed ops array"));
		goto fail;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psCompletedMemDesc,
	                                  (IMG_VOID **)& psDevInfo->pui32CompletedById);
	
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXSetupFirmware: Failed to map completed ops array"));
		goto fail;
	}

		/* Initialize FW started flag */
	psRGXFWInit->bFirmwareStarted = IMG_FALSE;
	
	/* Initialise the compatibility check data */
	RGXFWIF_COMPCHECKS_BVNC_INIT(psRGXFWInit->sRGXCompChecks.sFWBVNC);
	RGXFWIF_COMPCHECKS_BVNC_INIT(psRGXFWInit->sRGXCompChecks.sHWBVNC);
	
	{
		/* Below line is to make sure (compilation time check) that 
				RGX_BVNC_KM_V_ST fits into RGXFWIF_COMPCHECKS_BVNC structure */
		IMG_CHAR _tmp_[RGXFWIF_COMPCHECKS_BVNC_V_LEN_MAX] = RGX_BVNC_KM_V_ST;
		_tmp_[0] = '\0';
	}
	
	PDUMPCOMMENT("Dump RGXFW Init data");
	if (!bEnableSignatureChecks)
	{
#if defined(PDUMP)
		PDUMPCOMMENT("(to enable rgxfw signatures place the following line after the RTCONF line)");
		DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfInitMemDesc,
							offsetof(RGXFWIF_INIT, asSigBufCtl),
							sizeof(RGXFWIF_SIGBUF_CTL)*RGXFWIF_DM_MAX,
							PDUMP_FLAGS_CONTINUOUS);
#endif
		psRGXFWInit->asSigBufCtl[RGXFWIF_DM_3D].psBuffer.ui32Addr = 0x0;
		psRGXFWInit->asSigBufCtl[RGXFWIF_DM_TA].psBuffer.ui32Addr = 0x0;
	}
	
	for (dm = 0; dm < RGXFWIF_DM_MAX; dm++)
	{
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmLockedUpCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmOverranCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmRecoveredCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->aui16HwrDmFalseDetectCount[dm] = 0;
		psDevInfo->psRGXFWIfTraceBuf->apsHwrDmFWCommonContext[dm].ui32Addr = 0;
	}
	
	/*
	 * BIF Tiling configuration
	 */

	psRGXFWInit->sBifTilingCfg[0].uiBase = RGX_BIF_TILING_HEAP_1_BASE;
	psRGXFWInit->sBifTilingCfg[0].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[0].uiXStride = pui32BIFTilingXStrides[0];
	psRGXFWInit->sBifTilingCfg[1].uiBase = RGX_BIF_TILING_HEAP_2_BASE;
	psRGXFWInit->sBifTilingCfg[1].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[1].uiXStride = pui32BIFTilingXStrides[1];
	psRGXFWInit->sBifTilingCfg[2].uiBase = RGX_BIF_TILING_HEAP_3_BASE;
	psRGXFWInit->sBifTilingCfg[2].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[2].uiXStride = pui32BIFTilingXStrides[2];
	psRGXFWInit->sBifTilingCfg[3].uiBase = RGX_BIF_TILING_HEAP_4_BASE;
	psRGXFWInit->sBifTilingCfg[3].uiLen = RGX_BIF_TILING_HEAP_SIZE;
	psRGXFWInit->sBifTilingCfg[3].uiXStride = pui32BIFTilingXStrides[3];

#if defined(PDUMP)
	PDUMPCOMMENT("Dump rgxfw hwperfctl structure");
	DevmemPDumpLoadZeroMem (psDevInfo->psRGXFWIfHWPerfCountersMemDesc,
	                        0,
							ui32HWPerfCountersDataSize,
	                        PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("Dump rgxfw HW Perf Info structure");
	DevmemPDumpLoadMem (psDevInfo->psRGXFWIfHWPerfBufMemDesc,
						0,
						psDevInfo->ui32RGXFWIfHWPerfBufSize,
						PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("Dump rgxfw trace structure");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
						0,
						sizeof(RGXFWIF_TRACEBUF),
						PDUMP_FLAGS_CONTINUOUS);
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("Dump rgxfw register configuration buffer");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfRegCfgMemDesc,
						0,
						sizeof(RGXFWIF_REG_CFG),
						PDUMP_FLAGS_CONTINUOUS);
#endif
	PDUMPCOMMENT("Dump rgxfw init structure");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfInitMemDesc,
						0,
						sizeof(RGXFWIF_INIT),
						PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("Dump rgxfw coremem data store");
	DevmemPDumpLoadMem(	psDevInfo->psRGXFWIfCorememDataStoreMemDesc,
						0,
						RGX_META_COREMEM_DATA_SIZE,
						PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("RTCONF: run-time configuration");

	
	/* Dump the config options so they can be edited.
	 * 
	 * FIXME: Need new DevmemPDumpWRW API which writes a WRW to load ui32ConfigFlags
	 */
	PDUMPCOMMENT("(Set the FW config options here)");
	PDUMPCOMMENT("( Ctx Switch TA Enable: 0x%08x)", RGXFWIF_INICFG_CTXSWITCH_TA_EN);
	PDUMPCOMMENT("( Ctx Switch 3D Enable: 0x%08x)", RGXFWIF_INICFG_CTXSWITCH_3D_EN);
	PDUMPCOMMENT("( Ctx Switch CDM Enable: 0x%08x)", RGXFWIF_INICFG_CTXSWITCH_CDM_EN);
	PDUMPCOMMENT("( Ctx Switch Rand mode: 0x%08x)", RGXFWIF_INICFG_CTXSWITCH_MODE_RAND);
	PDUMPCOMMENT("( Ctx Switch Soft Reset Enable: 0x%08x)", RGXFWIF_INICFG_CTXSWITCH_SRESET_EN);
	PDUMPCOMMENT("( Reserved (do not set): 0x%08x)", RGXFWIF_INICFG_RSVD);
	PDUMPCOMMENT("( Rascal+Dust Power Island: 0x%08x)", RGXFWIF_INICFG_POW_RASCALDUST);
	PDUMPCOMMENT("( Enable HWPerf: 0x%08x)", RGXFWIF_INICFG_HWPERF_EN);
	PDUMPCOMMENT("( Enable HWR: 0x%08x)", RGXFWIF_INICFG_HWR_EN);
	PDUMPCOMMENT("( Check MList: 0x%08x)", RGXFWIF_INICFG_CHECK_MLIST_EN);
	PDUMPCOMMENT("( Disable Auto Clock Gating: 0x%08x)", RGXFWIF_INICFG_DISABLE_CLKGATING_EN);
	PDUMPCOMMENT("( Enable HWPerf Polling Perf Counter: 0x%08x)", RGXFWIF_INICFG_POLL_COUNTERS_EN);
#if defined(RGX_FEATURE_VDM_OBJECT_LEVEL_LLS)
	PDUMPCOMMENT("( Ctx Switch Object mode Index: 0x%08x)", RGXFWIF_INICFG_VDM_CTX_STORE_MODE_INDEX);
	PDUMPCOMMENT("( Ctx Switch Object mode Instance: 0x%08x)", RGX_CR_VDM_CONTEXT_STORE_MODE_MODE_INSTANCE);
	PDUMPCOMMENT("( Ctx Switch Object mode List: 0x%08x)", RGXFWIF_INICFG_VDM_CTX_STORE_MODE_LIST);
#endif
	PDUMPCOMMENT("( Enable SHG Bypass mode: 0x%08x)", RGXFWIF_INICFG_SHG_BYPASS_EN);
	PDUMPCOMMENT("( Enable RTU Bypass mode: 0x%08x)", RGXFWIF_INICFG_RTU_BYPASS_EN);
	PDUMPCOMMENT("( Enable register configuration: 0x%08x)", RGXFWIF_INICFG_REGCONFIG_EN);
	PDUMPCOMMENT("( Assert on TA Out-of-Memory: 0x%08x)", RGXFWIF_INICFG_ASSERT_ON_OUTOFMEMORY);
	PDUMPCOMMENT("( Disable HWPerf custom counter filter: 0x%08x)", RGXFWIF_INICFG_HWP_DISABLE_FILTER);
	PDUMPCOMMENT("( Enable HWPerf custom performance timer: 0x%08x)", RGXFWIF_INICFG_CUSTOM_PERF_TIMER_EN);
	PDUMPCOMMENT("( Enable CDM Killing Rand mode: 0x%08x)", RGXFWIF_INICFG_CDM_KILL_MODE_RAND_EN);
	PDUMPCOMMENT("( Disable DM overlap (except TA during SPM): 0x%08x)", RGXFWIF_INICFG_DISABLE_DM_OVERLAP);

	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfInitMemDesc,
							offsetof(RGXFWIF_INIT, ui32ConfigFlags),
							psRGXFWInit->ui32ConfigFlags,
							PDUMP_FLAGS_CONTINUOUS);

	/* default: no filter */
	psRGXFWInit->sPIDFilter.eMode = RGXFW_PID_FILTER_INCLUDE_ALL_EXCEPT;
	psRGXFWInit->sPIDFilter.asItems[0].uiPID = 0;

	PDUMPCOMMENT("( PID filter type: %X=INCLUDE_ALL_EXCEPT, %X=EXCLUDE_ALL_EXCEPT)",
							RGXFW_PID_FILTER_INCLUDE_ALL_EXCEPT,
							RGXFW_PID_FILTER_EXCLUDE_ALL_EXCEPT);

	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfInitMemDesc,
							offsetof(RGXFWIF_INIT, sPIDFilter.eMode),
							psRGXFWInit->sPIDFilter.eMode,
							PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("( PID filter PID/OSID list (Up to %u entries. Terminate with a zero PID))",
									RGXFWIF_PID_FILTER_MAX_NUM_PIDS);
	{
		IMG_UINT32 i;

		/* generate a few WRWs in the pdump stream as an example */
		for(i = 0; i < MIN(RGXFWIF_PID_FILTER_MAX_NUM_PIDS, 8); i++)
		{
			PDUMPCOMMENT("(PID and OSID pair %u)", i);

			PDUMPCOMMENT("(PID)");
			DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfInitMemDesc,
						offsetof(RGXFWIF_INIT, sPIDFilter.asItems[i].uiPID),
						0,
						PDUMP_FLAGS_CONTINUOUS);

			PDUMPCOMMENT("(OSID)");
			DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfInitMemDesc,
						offsetof(RGXFWIF_INIT, sPIDFilter.asItems[i].ui32OSID),
						0,
						PDUMP_FLAGS_CONTINUOUS);
		}
	}

	/* 
	 * Dump the log config so it can be edited.
	 */
	PDUMPCOMMENT("(Set the log config here)");
	PDUMPCOMMENT("( Log Type: set bit 0 for TRACE, reset for TBI)");
	PDUMPCOMMENT("( MAIN Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_MAIN);
	PDUMPCOMMENT("( MTS Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_MTS);
	PDUMPCOMMENT("( CLEANUP Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_CLEANUP);
	PDUMPCOMMENT("( CSW Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_CSW);
	PDUMPCOMMENT("( BIF Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_BIF);
	PDUMPCOMMENT("( PM Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_PM);
	PDUMPCOMMENT("( RTD Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_RTD);
	PDUMPCOMMENT("( SPM Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_SPM);
	PDUMPCOMMENT("( POW Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_POW);
	PDUMPCOMMENT("( HWR Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_HWR);
	PDUMPCOMMENT("( HWP Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_HWP);
#if defined(RGX_FEATURE_RAY_TRACING)
	PDUMPCOMMENT("( RPM Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_RPM);
#endif
#if defined(RGX_FEATURE_META_DMA)
	PDUMPCOMMENT("( DMA Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_DMA);
#endif
	PDUMPCOMMENT("( DEBUG Group Enable: 0x%08x)", RGXFWIF_LOG_TYPE_GROUP_DEBUG);
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfTraceBufCtlMemDesc,
							offsetof(RGXFWIF_TRACEBUF, ui32LogType),
							psDevInfo->psRGXFWIfTraceBuf->ui32LogType,
							PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("Set the HWPerf Filter config here");
	DevmemPDumpLoadMemValue64(psDevInfo->psRGXFWIfInitMemDesc,
						offsetof(RGXFWIF_INIT, ui64HWPerfFilter),
						psRGXFWInit->ui64HWPerfFilter,
						PDUMP_FLAGS_CONTINUOUS);						
							
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	PDUMPCOMMENT("(Number of registers configurations in sidekick)");
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, ui32NumRegsSidekick),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("(Number of registers configurations in rascal/dust)");
	DevmemPDumpLoadMemValue32(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, ui32NumRegsRascalDust),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("(Set registers here, address, value)");
	DevmemPDumpLoadMemValue64(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, asRegConfigs[0].ui64Addr),
							0,
							PDUMP_FLAGS_CONTINUOUS);
	DevmemPDumpLoadMemValue64(psDevInfo->psRGXFWIfRegCfgMemDesc,
							offsetof(RGXFWIF_REG_CFG, asRegConfigs[0].ui64Value),
							0,
							PDUMP_FLAGS_CONTINUOUS);
#endif /* SUPPORT_USER_REGISTER_CONFIGURATION */
#endif

	/* We don't need access to the fw init data structure anymore */
	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
	psRGXFWInit = IMG_NULL;

	psDevInfo->bFirmwareInitialised = IMG_TRUE;

	return PVRSRV_OK;

fail:
	if (psDevInfo->psRGXFWIfInitMemDesc != IMG_NULL && psRGXFWInit != IMG_NULL)
	{
		DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfInitMemDesc);
	}
	RGXFreeFirmware(psDevInfo);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
*******************************************************************************

 @Function	RGXFreeFirmware

 @Description

 Frees all the firmware-related allocations

 @Input psDevInfo

 @Return PVRSRV_ERROR

******************************************************************************/
IMG_VOID RGXFreeFirmware(PVRSRV_RGXDEV_INFO 	*psDevInfo)
{
	RGXFWIF_DM	eCCBType;
	
	psDevInfo->bFirmwareInitialised = IMG_FALSE;

	for (eCCBType = 0; eCCBType < RGXFWIF_DM_MAX; eCCBType++)
	{
		RGXFreeKernelCCB(psDevInfo, eCCBType);
		RGXFreeFirmwareCCB(psDevInfo, eCCBType);
	}

#if defined(RGXFW_ALIGNCHECKS)
	if (psDevInfo->psRGXFWAlignChecksMemDesc)
	{
		RGXFWFreeAlignChecks(psDevInfo);
	}
#endif

	if (psDevInfo->psRGXFWSigTAChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSigTAChecksMemDesc);
		psDevInfo->psRGXFWSigTAChecksMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWSig3DChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSig3DChecksMemDesc);
		psDevInfo->psRGXFWSig3DChecksMemDesc = IMG_NULL;
	}

#if defined(RGX_FEATURE_RAY_TRACING)
	if (psDevInfo->psRGXFWSigRTChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSigRTChecksMemDesc);
		psDevInfo->psRGXFWSigRTChecksMemDesc = IMG_NULL;
	}
	
	if (psDevInfo->psRGXFWSigSHChecksMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWSigSHChecksMemDesc);
		psDevInfo->psRGXFWSigSHChecksMemDesc = IMG_NULL;
	}
#endif

#if defined(FIX_HW_BRN_37200)
	if (psDevInfo->psRGXFWHWBRN37200MemDesc)
	{
		DevmemReleaseDevVirtAddr(psDevInfo->psRGXFWHWBRN37200MemDesc);
		DevmemFree(psDevInfo->psRGXFWHWBRN37200MemDesc);
		psDevInfo->psRGXFWHWBRN37200MemDesc = IMG_NULL;
	}
#endif

	RGXSetupFaultReadRegisterRollback(psDevInfo);

	if (psDevInfo->psPowSyncPrim != IMG_NULL)
	{
		SyncPrimFree(psDevInfo->psPowSyncPrim);
		psDevInfo->psPowSyncPrim = IMG_NULL;
	}
	
	if (psDevInfo->hSyncPrimContext != 0)
	{
		SyncPrimContextDestroy(psDevInfo->hSyncPrimContext);
		psDevInfo->hSyncPrimContext = 0;
	}

	if (psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc)
	{
		if (psDevInfo->psRGXFWIfGpuUtilFWCb != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);
			psDevInfo->psRGXFWIfGpuUtilFWCb = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc);
		psDevInfo->psRGXFWIfGpuUtilFWCbCtlMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfRuntimeCfgMemDesc)
	{
		if (psDevInfo->psRGXFWIfRuntimeCfg != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfRuntimeCfgMemDesc);
			psDevInfo->psRGXFWIfRuntimeCfg = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psRGXFWIfRuntimeCfgMemDesc);
		psDevInfo->psRGXFWIfRuntimeCfgMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc)
	{
		if (psDevInfo->psRGXFWIfHWRInfoBuf != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);
			psDevInfo->psRGXFWIfHWRInfoBuf = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc);
		psDevInfo->psRGXFWIfHWRInfoBufCtlMemDesc = IMG_NULL;
	}

	RGXHWPerfDeinit();
	
	if (psDevInfo->psRGXFWIfHWPerfBufMemDesc)
	{
		if (psDevInfo->psRGXFWIfHWPerfBuf != IMG_NULL)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfHWPerfBufMemDesc);
			psDevInfo->psRGXFWIfHWPerfBuf = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psRGXFWIfHWPerfBufMemDesc);
		psDevInfo->psRGXFWIfHWPerfBufMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfCorememDataStoreMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfCorememDataStoreMemDesc);
		psDevInfo->psRGXFWIfCorememDataStoreMemDesc = IMG_NULL;
	}

	if (psDevInfo->psRGXFWIfTraceBufCtlMemDesc)
	{
		if (psDevInfo->psRGXFWIfTraceBuf != IMG_NULL)
		{    
			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);
			psDevInfo->psRGXFWIfTraceBuf = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psRGXFWIfTraceBufCtlMemDesc);
		psDevInfo->psRGXFWIfTraceBufCtlMemDesc = IMG_NULL;
	}
#if defined(SUPPORT_USER_REGISTER_CONFIGURATION)
	if (psDevInfo->psRGXFWIfRegCfgMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfRegCfgMemDesc);
		psDevInfo->psRGXFWIfRegCfgMemDesc = IMG_NULL;
	}
#endif
	if (psDevInfo->psRGXFWIfHWPerfCountersMemDesc)
	{
		RGXUnsetFirmwareAddress(psDevInfo->psRGXFWIfHWPerfCountersMemDesc);
		if (DevmemIsValidExportCookie(&psDevInfo->sRGXFWHWPerfCountersExportCookie))
		{
			/* if the export cookie is valid, the init sequence failed */
			PVR_DPF((PVR_DBG_ERROR, "RGXFreeFirmware: FW HWPerf Export cookie"
			         "still valid (should have been unexported at init time)"));
			DevmemUnexport(psDevInfo->psRGXFWIfHWPerfCountersMemDesc,
			               &psDevInfo->sRGXFWHWPerfCountersExportCookie);
		}
		DevmemFwFree(psDevInfo->psRGXFWIfHWPerfCountersMemDesc);
		psDevInfo->psRGXFWIfHWPerfCountersMemDesc = IMG_NULL;
	}
#if defined(RGX_FEATURE_SLC_VIVT)
	_FreeSLC3Fence(psDevInfo);
#endif

	if (psDevInfo->psRGXFWIfInitMemDesc)
	{
		DevmemFwFree(psDevInfo->psRGXFWIfInitMemDesc);
		psDevInfo->psRGXFWIfInitMemDesc = IMG_NULL;
	}

	if (psDevInfo->psCompletedMemDesc)
	{
		if (psDevInfo->pui32CompletedById)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psCompletedMemDesc);
			psDevInfo->pui32CompletedById = IMG_NULL;
		}
		DevmemFwFree(psDevInfo->psCompletedMemDesc);
		psDevInfo->psCompletedMemDesc = IMG_NULL;
	}
	if (psDevInfo->psEndTimeMemDesc)
	{
		if (psDevInfo->pasEndTimeById)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psEndTimeMemDesc);
			psDevInfo->pasEndTimeById = IMG_NULL;
		}

		DevmemFwFree(psDevInfo->psEndTimeMemDesc);
		psDevInfo->psEndTimeMemDesc = IMG_NULL;
	}
	if (psDevInfo->psStartTimeMemDesc)
	{
		if (psDevInfo->pasStartTimeById)
		{
			DevmemReleaseCpuVirtAddr(psDevInfo->psStartTimeMemDesc);
			psDevInfo->pasStartTimeById = IMG_NULL;
		}

		DevmemFwFree(psDevInfo->psStartTimeMemDesc);
		psDevInfo->psStartTimeMemDesc = IMG_NULL;
	}
}


/******************************************************************************
 FUNCTION	: RGXStartFirmware

 PURPOSE	: Attempts to obtain a slot in the Kernel CCB

 PARAMETERS	: psDevInfo

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR RGXStartFirmware(PVRSRV_RGXDEV_INFO 	*psDevInfo)
{
	PVRSRV_ERROR	eError = PVRSRV_OK;

	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "RGXStart: Rogue Firmware Slave boot Start");
	/*
	 * Run init script.
	 */
	eError = RGXRunScript(psDevInfo, psDevInfo->psScripts->asInitCommands, RGX_MAX_INIT_COMMANDS, PDUMP_FLAGS_CONTINUOUS, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXStart: RGXRunScript failed (%d)", eError));
		return eError;
	}
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "RGXStart: Rogue Firmware startup complete\n");
	
	return eError;
}


/******************************************************************************
 FUNCTION	: RGXAcquireKernelCCBSlot

 PURPOSE	: Attempts to obtain a slot in the Kernel CCB

 PARAMETERS	: psCCB - the CCB
			: Address of space if available, IMG_NULL otherwise

 RETURNS	: PVRSRV_ERROR
******************************************************************************/
static PVRSRV_ERROR RGXAcquireKernelCCBSlot(DEVMEM_MEMDESC *psKCCBCtrlMemDesc,
											RGXFWIF_CCB_CTL	*psKCCBCtl,
											IMG_UINT32			*pui32Offset)
{
	IMG_UINT32	ui32OldWriteOffset, ui32NextWriteOffset;

	ui32OldWriteOffset = psKCCBCtl->ui32WriteOffset;
	ui32NextWriteOffset = (ui32OldWriteOffset + 1) & psKCCBCtl->ui32WrapMask;

	/* Note: The MTS can queue up to 255 kicks (254 pending kicks and 1 executing kick)
	 * Hence the kernel CCB should not queue more 254 commands
	 */
	PVR_ASSERT(psKCCBCtl->ui32WrapMask < 255);
	
#if defined(PDUMP)
	/* Wait for sufficient CCB space to become available */
	PDUMPCOMMENTWITHFLAGS(0, "Wait for kCCB woff=%u", ui32NextWriteOffset);
	DevmemPDumpCBP(psKCCBCtrlMemDesc,
	               offsetof(RGXFWIF_CCB_CTL, ui32ReadOffset),
	               ui32NextWriteOffset,
	               1,
	               (psKCCBCtl->ui32WrapMask + 1));
#endif

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{

		if (ui32NextWriteOffset != psKCCBCtl->ui32ReadOffset)
		{
			*pui32Offset = ui32NextWriteOffset;
			return PVRSRV_OK;
		}
		{
			/* 
			 * The following sanity check doesn't impact performance,
			 * since the CPU has to wait for the GPU anyway (full kernel CCB).
			 */
			if (PVRSRVGetPVRSRVData()->eServicesState != PVRSRV_SERVICES_STATE_OK)
			{
				return PVRSRV_ERROR_KERNEL_CCB_FULL;
			}
		}

		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	/* Time out on waiting for CCB space */
	return PVRSRV_ERROR_KERNEL_CCB_FULL;
}


PVRSRV_ERROR RGXSendCommandWithPowLock(PVRSRV_RGXDEV_INFO 	*psDevInfo,
										 RGXFWIF_DM			eKCCBType,
										 RGXFWIF_KCCB_CMD	*psKCCBCmd,
										 IMG_UINT32			ui32CmdSize,
										 IMG_BOOL			bPDumpContinuous)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;

	/* Ensure Rogue is powered up before kicking MTS */
	eError = PVRSRVPowerLock();

	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandWithPowLock: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVPowerLock_Exit;
	}

	PDUMPPOWCMDSTART();
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	PDUMPPOWCMDEND();

	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandWithPowLock: failed to transition Rogue to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVSetDevicePowerStateKM_Exit;
	}

	eError = RGXSendCommandRaw(psDevInfo, eKCCBType,  psKCCBCmd, ui32CmdSize, bPDumpContinuous?PDUMP_FLAGS_CONTINUOUS:0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSendCommandWithPowLock: failed to schedule command (%s)",
					PVRSRVGetErrorStringKM(eError)));
#if defined(DEBUG)
		/* PVRSRVDebugRequest must be called without powerlock */
		PVRSRVPowerUnlock();
		PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX, IMG_NULL);
		goto _PVRSRVPowerLock_Exit;
#endif
	}

_PVRSRVSetDevicePowerStateKM_Exit:
	PVRSRVPowerUnlock();

_PVRSRVPowerLock_Exit:
	return eError;
}

PVRSRV_ERROR RGXSendCommandRaw(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								 RGXFWIF_DM			eKCCBType,
								 RGXFWIF_KCCB_CMD	*psKCCBCmd,
								 IMG_UINT32			ui32CmdSize,
								 PDUMP_FLAGS_T		uiPdumpFlags)
{
	PVRSRV_ERROR		eError;
	RGXFWIF_CCB_CTL		*psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
	IMG_UINT8			*pui8KCCB = psDevInfo->apsKernelCCB[eKCCBType];
	IMG_UINT32			ui32NewWriteOffset;
	IMG_UINT32			ui32OldWriteOffset = psKCCBCtl->ui32WriteOffset;
#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(uiPdumpFlags);
#endif
#if defined(PDUMP)
	IMG_BOOL bIsInCaptureRange;
	IMG_BOOL bPdumpEnabled;
	IMG_BOOL bPDumpContinuous = (uiPdumpFlags & PDUMP_FLAGS_CONTINUOUS) != 0;
	IMG_BOOL bPDumpPowTrans = PDUMPPOWCMDINTRANS();

	PDumpIsCaptureFrameKM(&bIsInCaptureRange);
	bPdumpEnabled = (bIsInCaptureRange || bPDumpContinuous) && !bPDumpPowTrans;

	/* in capture range */
	if (bPdumpEnabled)
	{
		if (!psDevInfo->abDumpedKCCBCtlAlready[eKCCBType])
		{
			/* entering capture range */
			psDevInfo->abDumpedKCCBCtlAlready[eKCCBType] = IMG_TRUE;

			/* wait for firmware to catch up */
			PVR_DPF((PVR_DBG_MESSAGE, "RGXSendCommandRaw: waiting on fw to catch-up. DM: %d, roff: %d, woff: %d",
						eKCCBType, psKCCBCtl->ui32ReadOffset, ui32OldWriteOffset));
			PVRSRVPollForValueKM(&psKCCBCtl->ui32ReadOffset, ui32OldWriteOffset, 0xFFFFFFFF);

			/* Dump Init state of Kernel CCB control (read and write offset) */
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Initial state of kernel CCB Control(%d), roff: %d, woff: %d", eKCCBType, psKCCBCtl->ui32ReadOffset, psKCCBCtl->ui32WriteOffset);
			DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
					0,
					sizeof(RGXFWIF_CCB_CTL),
					PDUMP_FLAGS_CONTINUOUS);
		}
	}
#endif

	PVR_ASSERT(ui32CmdSize == psKCCBCtl->ui32CmdSize);
	if (!OSLockIsLocked(PVRSRVGetPVRSRVData()->hPowerLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSendCommandRaw called without power lock held!"));
		PVR_ASSERT(OSLockIsLocked(PVRSRVGetPVRSRVData()->hPowerLock));
	}

	/*
	 * Acquire a slot in the CCB.
	 */ 
	eError = RGXAcquireKernelCCBSlot(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType], psKCCBCtl, &ui32NewWriteOffset);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXSendCommandRaw failed to acquire CCB slot. Type:%u Error:%u",
				eKCCBType, eError));
		goto _RGXSendCommandRaw_Exit;
	}
	
	/*
	 * Copy the command into the CCB.
	 */
	OSMemCopy(&pui8KCCB[ui32OldWriteOffset * psKCCBCtl->ui32CmdSize],
			  psKCCBCmd, psKCCBCtl->ui32CmdSize);

	/* ensure kCCB data is written before the offsets */
	OSWriteMemoryBarrier();

	/* Move past the current command */
	psKCCBCtl->ui32WriteOffset = ui32NewWriteOffset;


#if defined(PDUMP)
	/* in capture range */
	if (bPdumpEnabled)
	{
		/* Dump new Kernel CCB content */
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Dump kCCB(%d) cmd, woff = %d", eKCCBType, ui32OldWriteOffset);
		DevmemPDumpLoadMem(psDevInfo->apsKernelCCBMemDesc[eKCCBType],
				ui32OldWriteOffset * psKCCBCtl->ui32CmdSize,
				psKCCBCtl->ui32CmdSize,
				PDUMP_FLAGS_CONTINUOUS);

		/* Dump new kernel CCB write offset */
		PDUMPCOMMENTWITHFLAGS(uiPdumpFlags, "Dump kCCBCtl(%d) woff: %d", eKCCBType, ui32NewWriteOffset);
		DevmemPDumpLoadMem(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
							   offsetof(RGXFWIF_CCB_CTL, ui32WriteOffset),
							   sizeof(IMG_UINT32),
							   uiPdumpFlags);
	}

	/* out of capture range */
	if (!bPdumpEnabled)
	{
		eError = RGXPdumpDrainKCCB(psDevInfo, ui32OldWriteOffset, eKCCBType);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXSendCommandRaw: problem draining kCCB (%d)", eError));
			goto _RGXSendCommandRaw_Exit;
		}
	}
#endif


	PDUMPCOMMENTWITHFLAGS(uiPdumpFlags, "MTS kick for kernel CCB %d", eKCCBType);
	/*
	 * Kick the MTS to schedule the firmware.
	 */
	{
		IMG_UINT32	ui32MTSRegVal = (eKCCBType & ~RGX_CR_MTS_SCHEDULE_DM_CLRMSK) | RGX_CR_MTS_SCHEDULE_TASK_COUNTED;
		
		__MTSScheduleWrite(psDevInfo, ui32MTSRegVal);

		PDUMPREG32(RGX_PDUMPREG_NAME, RGX_CR_MTS_SCHEDULE, ui32MTSRegVal, uiPdumpFlags);
	}
	
#if defined (NO_HARDWARE)
	/* keep the roff updated because fw isn't there to update it */
	psKCCBCtl->ui32ReadOffset = psKCCBCtl->ui32WriteOffset;
#endif

_RGXSendCommandRaw_Exit:
	return eError;
}

IMG_VOID RGXScheduleProcessQueuesKM(PVRSRV_CMDCOMP_HANDLE hCmdCompHandle)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = (PVRSRV_DEVICE_NODE*) hCmdCompHandle;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	OSScheduleMISR(psDevInfo->hProcessQueuesMISR);
}

/*!
******************************************************************************

 @Function	_RGXScheduleProcessQueuesMISR

 @Description - Sends uncounted kick to all the DMs (the FW will process all
				the queue for all the DMs)
******************************************************************************/
static IMG_VOID _RGXScheduleProcessQueuesMISR(IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE     *psDeviceNode = pvData;
	PVRSRV_RGXDEV_INFO     *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_DM			   eDM;
	PVRSRV_ERROR		   eError;
	PVRSRV_DEV_POWER_STATE ePowerState;

	/* We don't need to acquire the BridgeLock as this power transition won't
	   send a command to the FW */
	eError = PVRSRVPowerLock();
	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXScheduleProcessQueuesKM: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		return;
	}

	/* Check whether it's worth waking up the GPU */
	eError = PVRSRVGetDevicePowerState(psDeviceNode->sDevId.ui32DeviceIndex, &ePowerState);

	if ((eError == PVRSRV_OK) && (ePowerState == PVRSRV_DEV_POWER_STATE_OFF))
	{
		RGXFWIF_GPU_UTIL_FWCB  *psUtilFWCb = psDevInfo->psRGXFWIfGpuUtilFWCb;
		IMG_BOOL               bGPUHasWorkWaiting;

		bGPUHasWorkWaiting =
		    (RGXFWIF_GPU_UTIL_GET_STATE(psUtilFWCb->ui64LastWord) == RGXFWIF_GPU_UTIL_STATE_BLOCKED);

		if (!bGPUHasWorkWaiting)
		{
			/* all queues are empty, don't wake up the GPU */
			PVRSRVPowerUnlock();
			return;
		}
	}

	PDUMPPOWCMDSTART();
	/* wake up the GPU */
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	PDUMPPOWCMDEND();

	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXScheduleProcessQueuesKM: failed to transition Rogue to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		PVRSRVPowerUnlock();
		return;
	}

	/* uncounted kick for all DMs */
	for (eDM = RGXFWIF_HWDM_MIN; eDM < RGXFWIF_HWDM_MAX; eDM++)
	{
		IMG_UINT32	ui32MTSRegVal = (eDM & ~RGX_CR_MTS_SCHEDULE_DM_CLRMSK) | RGX_CR_MTS_SCHEDULE_TASK_NON_COUNTED;

		__MTSScheduleWrite(psDevInfo, ui32MTSRegVal);
	}

	PVRSRVPowerUnlock();
}

PVRSRV_ERROR RGXInstallProcessQueuesMISR(IMG_HANDLE *phMISR, PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return OSInstallMISR(phMISR,
	                     _RGXScheduleProcessQueuesMISR,
	                     psDeviceNode);
}

typedef struct _DEVMEM_COMMON_CONTEXT_LOOKUP_
{
	IMG_UINT32                 ui32ContextID;
	RGX_SERVER_COMMON_CONTEXT  *psServerCommonContext;
} DEVMEM_COMMON_CONTEXT_LOOKUP;


static IMG_BOOL _FindServerCommonContext(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	DEVMEM_COMMON_CONTEXT_LOOKUP  *psRefLookUp = (DEVMEM_COMMON_CONTEXT_LOOKUP *)pvCallbackData;
	RGX_SERVER_COMMON_CONTEXT     *psServerCommonContext;

	psServerCommonContext = IMG_CONTAINER_OF(psNode, RGX_SERVER_COMMON_CONTEXT, sListNode);

	if (psServerCommonContext->ui32ContextID == psRefLookUp->ui32ContextID)
	{
		psRefLookUp->psServerCommonContext = psServerCommonContext;
		return IMG_FALSE;
	}
	else
	{
		return IMG_TRUE;
	}
}


/*!
******************************************************************************

 @Function	RGXScheduleCommand

 @Description - Submits a CCB command and kicks the firmware but first schedules
                any commands which have to happen before handle  

 @Input psDevInfo - pointer to device info
 @Input eKCCBType - see RGXFWIF_CMD_*
 @Input pvKCCBCmd - kernel CCB command
 @Input ui32CmdSize -
 @Input bPDumpContinuous - TRUE if the pdump flags should be continuous


 @Return ui32Error - success or failure

******************************************************************************/
PVRSRV_ERROR RGXScheduleCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
								RGXFWIF_DM			eKCCBType,
								RGXFWIF_KCCB_CMD	*psKCCBCmd,
								IMG_UINT32			ui32CmdSize,
								IMG_BOOL			bPDumpContinuous)
{
	PVRSRV_DATA *psData = PVRSRVGetPVRSRVData();
	PVRSRV_ERROR eError;

	if ((eKCCBType == RGXFWIF_DM_3D) || (eKCCBType == RGXFWIF_DM_2D) || (eKCCBType == RGXFWIF_DM_CDM))
	{
		/* This handles the no operation case */
		OSCPUOperation(psData->uiCacheOp);
		psData->uiCacheOp = PVRSRV_CACHE_OP_NONE;
	}

	eError = RGXPreKickCacheCommand(psDevInfo);
	if (eError != PVRSRV_OK) goto RGXScheduleCommand_exit;

	eError = RGXSendCommandWithPowLock(psDevInfo, eKCCBType, psKCCBCmd, ui32CmdSize, bPDumpContinuous);
	if (eError != PVRSRV_OK) goto RGXScheduleCommand_exit;


RGXScheduleCommand_exit:
	return eError;
}

/*
 * RGXCheckFirmwareCCBs
 */
IMG_VOID RGXCheckFirmwareCCBs(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_FWCCB_CMD 	*psFwCCBCmd;
	IMG_UINT32 			ui32DMCount;

	for (ui32DMCount = 0; ui32DMCount < RGXFWIF_DM_MAX; ui32DMCount++)
	{
		RGXFWIF_CCB_CTL *psFWCCBCtl = psDevInfo->apsFirmwareCCBCtl[ui32DMCount];
		IMG_UINT8 		*psFWCCB = psDevInfo->apsFirmwareCCB[ui32DMCount];

		while (psFWCCBCtl->ui32ReadOffset != psFWCCBCtl->ui32WriteOffset)
		{
			/* Point to the next command */
			psFwCCBCmd = ((RGXFWIF_FWCCB_CMD *)psFWCCB) + psFWCCBCtl->ui32ReadOffset;

			switch(psFwCCBCmd->eCmdType)
			{
				case RGXFWIF_FWCCB_CMD_ZSBUFFER_BACKING:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, ZSBUFFER_BACKING, "Request to add backing to ZSBuffer");
					}
					RGXProcessRequestZSBufferBacking(psDevInfo,
													psFwCCBCmd->uCmdData.sCmdZSBufferBacking.ui32ZSBufferID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_ZSBUFFER_UNBACKING:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, ZSBUFFER_UNBACKING, "Request to remove backing from ZSBuffer");
					}
					RGXProcessRequestZSBufferUnbacking(psDevInfo,
													psFwCCBCmd->uCmdData.sCmdZSBufferBacking.ui32ZSBufferID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_FREELIST_GROW:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, FREELIST_GROW, "Request to grow the free list");
					}
					RGXProcessRequestGrow(psDevInfo,
										psFwCCBCmd->uCmdData.sCmdFreeListGS.ui32FreelistID);
					break;
				}

				case RGXFWIF_FWCCB_CMD_FREELISTS_RECONSTRUCTION:
				{
					if (psDevInfo->bPDPEnabled)
					{
						PDUMP_PANIC(RGX, FREELISTS_RECONSTRUCTION, "Request to reconstruct free lists");
					}

					PVR_DPF((PVR_DBG_MESSAGE, "RGXCheckFirmwareCCBs: Freelist reconstruction request (%d/%d) for %d freelists",
					        psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.ui32HwrCounter+1,
					        psDevInfo->psRGXFWIfTraceBuf->ui32HwrCounter+1,
					        psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.ui32FreelistsCount));

					RGXProcessRequestFreelistsReconstruction(psDevInfo, ui32DMCount,
										psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.ui32FreelistsCount,
										psFwCCBCmd->uCmdData.sCmdFreeListsReconstruction.aui32FreelistIDs);
					break;
				}

				case RGXFWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION:
				{
					DEVMEM_COMMON_CONTEXT_LOOKUP  sLookUp;

					sLookUp.ui32ContextID         = psFwCCBCmd->uCmdData.sCmdContextResetNotification.ui32ServerCommonContextID;
					sLookUp.psServerCommonContext = IMG_NULL;
					
					dllist_foreach_node(&psDevInfo->sCommonCtxtListHead, _FindServerCommonContext, (IMG_PVOID)&sLookUp);

					PVR_DPF((PVR_DBG_MESSAGE, "RGXCheckFirmwareCCBs: Context 0x%p reset (ID=0x%08x, Reason=%d)",
					        sLookUp.psServerCommonContext,
					        (IMG_UINT32)(psFwCCBCmd->uCmdData.sCmdContextResetNotification.ui32ServerCommonContextID),
					        (IMG_UINT32)(psFwCCBCmd->uCmdData.sCmdContextResetNotification.eResetReason)));

					if (sLookUp.psServerCommonContext != IMG_NULL)
					{
						sLookUp.psServerCommonContext->eLastResetReason = psFwCCBCmd->uCmdData.sCmdContextResetNotification.eResetReason;
					}
					break;
				}

				case RGXFWIF_FWCCB_CMD_DEBUG_DUMP:
				{
					RGXDumpDebugInfo(IMG_NULL,psDevInfo);
					break;
				}

				case RGXFWIF_FWCCB_CMD_UPDATE_STATS:
				{
                    IMG_PID	   pidTmp = psFwCCBCmd->uCmdData.sCmdUpdateStatsData.pidOwner;
                    IMG_INT32  i32AdjustmentValue = psFwCCBCmd->uCmdData.sCmdUpdateStatsData.i32AdjustmentValue;

                    switch (psFwCCBCmd->uCmdData.sCmdUpdateStatsData.eElementToUpdate)
					{
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_PARTIAL_RENDERS:
						{
                            PVRSRVStatsUpdateRenderContextStats(i32AdjustmentValue,0,0,0,0,0,pidTmp);
							break;
						}
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_OUT_OF_MEMORY:
						{
							PVRSRVStatsUpdateRenderContextStats(0,i32AdjustmentValue,0,0,0,0,pidTmp);
							break;
						}
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_TA_STORES:
						{
							PVRSRVStatsUpdateRenderContextStats(0,0,i32AdjustmentValue,0,0,0,pidTmp);
							break;
						}
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_3D_STORES:
						{
							PVRSRVStatsUpdateRenderContextStats(0,0,0,i32AdjustmentValue,0,0,pidTmp);
							break;
						}
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_SH_STORES:
						{
							PVRSRVStatsUpdateRenderContextStats(0,0,0,0,i32AdjustmentValue,0,pidTmp);
							break;
						}
						case RGXFWIF_FWCCB_CMD_UPDATE_NUM_CDM_STORES:
						{
							PVRSRVStatsUpdateRenderContextStats(0,0,0,0,0,i32AdjustmentValue,pidTmp);
							break;
						}
					}
					break;
				}
				default:
					PVR_ASSERT(IMG_FALSE);
			}

			/* Update read offset */
			psFWCCBCtl->ui32ReadOffset = (psFWCCBCtl->ui32ReadOffset + 1) & psFWCCBCtl->ui32WrapMask;
		}
	}
}

/*
 * PVRSRVRGXFrameworkCopyCommand
 */ 
PVRSRV_ERROR PVRSRVRGXFrameworkCopyCommand(DEVMEM_MEMDESC	*psFWFrameworkMemDesc,
										   IMG_PBYTE		pbyGPUFRegisterList,
										   IMG_UINT32		ui32FrameworkRegisterSize)
{
	PVRSRV_ERROR	eError;
	RGXFWIF_RF_REGISTERS	*psRFReg;

	eError = DevmemAcquireCpuVirtAddr(psFWFrameworkMemDesc,
                                      (IMG_VOID **)&psRFReg);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFrameworkCopyCommand: Failed to map firmware render context state (%u)",
				eError));
		return eError;
	}

	OSMemCopy(psRFReg, pbyGPUFRegisterList, ui32FrameworkRegisterSize);
	
	/* Release the CPU mapping */
	DevmemReleaseCpuVirtAddr(psFWFrameworkMemDesc);

	/*
	 * Dump the FW framework buffer
	 */
	PDUMPCOMMENT("Dump FWFramework buffer");
	DevmemPDumpLoadMem(psFWFrameworkMemDesc, 0, ui32FrameworkRegisterSize, PDUMP_FLAGS_CONTINUOUS);

	return PVRSRV_OK;
}

/*
 * PVRSRVRGXFrameworkCreateKM
 */
PVRSRV_ERROR PVRSRVRGXFrameworkCreateKM(PVRSRV_DEVICE_NODE	*psDeviceNode,
										DEVMEM_MEMDESC		**ppsFWFrameworkMemDesc,
										IMG_UINT32			ui32FrameworkCommandSize)
{
	PVRSRV_ERROR			eError;
	PVRSRV_RGXDEV_INFO 		*psDevInfo = psDeviceNode->pvDevice;
	
	/*
		Allocate device memory for the firmware GPU framework state.
		Sufficient info to kick one or more DMs should be contained in this buffer
	*/
	PDUMPCOMMENT("Allocate Rogue firmware framework state");

	eError = DevmemFwAllocate(psDevInfo,
							  ui32FrameworkCommandSize,
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FirmwareGPUFrameworkState",
							  ppsFWFrameworkMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFrameworkContextKM: Failed to allocate firmware framework state (%u)",
				eError));
		return eError;
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXWaitForFWOp(PVRSRV_RGXDEV_INFO	*psDevInfo,
							RGXFWIF_DM eDM,
							PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
							IMG_BOOL bPDumpContinuous)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	RGXFWIF_KCCB_CMD	sCmdSyncPrim;

	/* Ensure Rogue is powered up before kicking MTS */
	eError = PVRSRVPowerLock();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXWaitForFWOp: failed to acquire powerlock (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVPowerLock_Exit;
	}

	PDUMPPOWCMDSTART();
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
										 PVRSRV_DEV_POWER_STATE_ON,
										 IMG_FALSE);
	PDUMPPOWCMDEND();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXWaitForFWOp: failed to transition Rogue to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVSetDevicePowerStateKM_Exit;
	}




	/* Setup sync primitive */
	SyncPrimSet(psSyncPrim, 0);

	/* prepare a sync command */
	sCmdSyncPrim.eCmdType = RGXFWIF_KCCB_CMD_SYNC;
	sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr = SyncPrimGetFirmwareAddr(psSyncPrim);
	sCmdSyncPrim.uCmdData.sSyncData.uiUpdateVal = 1;

	PDUMPCOMMENT("RGXWaitForFWOp: Submit Kernel SyncPrim [0x%08x] to DM %d ", sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr, eDM);

	/* submit the sync primitive to the kernel CCB */
	eError = RGXSendCommandRaw(psDevInfo,
								eDM,
								&sCmdSyncPrim,
								sizeof(RGXFWIF_KCCB_CMD),
								bPDumpContinuous  ? PDUMP_FLAGS_CONTINUOUS:0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXScheduleCommandAndWait: Failed to schedule Kernel SyncPrim with error (%u)", eError));
		goto _RGXSendCommandRaw_Exit;
	}

	/* Wait for sync primitive to be updated */
#if defined(PDUMP)
	PDUMPCOMMENT("RGXScheduleCommandAndWait: Poll for Kernel SyncPrim [0x%08x] on DM %d ", sCmdSyncPrim.uCmdData.sSyncData.uiSyncObjDevVAddr, eDM);

	SyncPrimPDumpPol(psSyncPrim,
					1,
					0xffffffff,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);
#endif

	{
		RGXFWIF_CCB_CTL  *psKCCBCtl = psDevInfo->apsKernelCCBCtl[eDM];
		IMG_UINT32       ui32CurrentQueueLength = (psKCCBCtl->ui32WrapMask+1 +
												   psKCCBCtl->ui32WriteOffset -
												   psKCCBCtl->ui32ReadOffset) & psKCCBCtl->ui32WrapMask;
		IMG_UINT32       ui32MaxRetries;

		for (ui32MaxRetries = (ui32CurrentQueueLength + 1) * 3;
			 ui32MaxRetries > 0;
			 ui32MaxRetries--)
		{
			eError = PVRSRVWaitForValueKMAndHoldBridgeLockKM(psSyncPrim->pui32LinAddr, 1, 0xffffffff);

			if (eError != PVRSRV_ERROR_TIMEOUT)
			{
				break;
			}
		}

		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXScheduleCommandAndWait: PVRSRVWaitForValueKMAndHoldBridgeLock timed out. Dump debug information."));
			PVRSRVPowerUnlock();

			PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX,IMG_NULL);
			PVR_ASSERT(eError != PVRSRV_ERROR_TIMEOUT);
			goto _PVRSRVDebugRequest_Exit;
		}
	}

_RGXSendCommandRaw_Exit:
_PVRSRVSetDevicePowerStateKM_Exit:

	PVRSRVPowerUnlock();

_PVRSRVDebugRequest_Exit:
_PVRSRVPowerLock_Exit:
	return eError;
}

static
PVRSRV_ERROR RGXScheduleCleanupCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo,
									   RGXFWIF_DM			eDM,
									   RGXFWIF_KCCB_CMD		*psKCCBCmd,
									   IMG_UINT32			ui32CmdSize,
									   RGXFWIF_CLEANUP_TYPE	eCleanupType,
									   PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
									   IMG_BOOL				bPDumpContinuous)
{
	PVRSRV_ERROR eError;

	psKCCBCmd->eCmdType = RGXFWIF_KCCB_CMD_CLEANUP;

	psKCCBCmd->uCmdData.sCleanupData.eCleanupType = eCleanupType;
	psKCCBCmd->uCmdData.sCleanupData.uiSyncObjDevVAddr = SyncPrimGetFirmwareAddr(psSyncPrim);

	SyncPrimSet(psSyncPrim, 0);

	/*
		Send the cleanup request to the firmware. If the resource is still busy
		the firmware will tell us and we'll drop out with a retry.
	*/
	eError = RGXScheduleCommand(psDevInfo,
								eDM,
								psKCCBCmd,
								ui32CmdSize,
								bPDumpContinuous);
	if (eError != PVRSRV_OK)
	{
		goto fail_command;
	}

	/* Wait for sync primitive to be updated */
#if defined(PDUMP)
	PDUMPCOMMENT("Wait for the firmware to reply to the cleanup command");
	SyncPrimPDumpPol(psSyncPrim,
					RGXFWIF_CLEANUP_RUN,
					RGXFWIF_CLEANUP_RUN,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);

	/*
	 * The cleanup request to the firmware will tell us if a given resource is busy or not.
	 * If the RGXFWIF_CLEANUP_BUSY flag is set, this means that the resource is still in use.
	 * In this case we return a PVRSRV_ERROR_RETRY error to the client drivers and they will
	 * re-issue the cleanup request until it succeed.
	 *
	 * Since this retry mechanism doesn't work for pdumps, client drivers should ensure
	 * that cleanup requests are only submitted if the resource is unused.
	 * If this is not the case, the following poll will block infinitely, making sure
	 * the issue doesn't go unnoticed.
	 */
	PDUMPCOMMENT("Cleanup: If this poll fails, the following resource is still in use (DM=%u, type=%u, address=0x%08x), which is incorrect in pdumps",
					eDM,
					psKCCBCmd->uCmdData.sCleanupData.eCleanupType,
					psKCCBCmd->uCmdData.sCleanupData.uCleanupData.psContext.ui32Addr);
	SyncPrimPDumpPol(psSyncPrim,
					0,
					RGXFWIF_CLEANUP_BUSY,
					PDUMP_POLL_OPERATOR_EQUAL,
					bPDumpContinuous ? PDUMP_FLAGS_CONTINUOUS:0);
#endif

	{
		RGXFWIF_CCB_CTL  *psKCCBCtl = psDevInfo->apsKernelCCBCtl[eDM];
		IMG_UINT32       ui32CurrentQueueLength = (psKCCBCtl->ui32WrapMask+1 +
		                                           psKCCBCtl->ui32WriteOffset -
		                                           psKCCBCtl->ui32ReadOffset) & psKCCBCtl->ui32WrapMask;
		IMG_UINT32       ui32MaxRetries;

		for (ui32MaxRetries = ui32CurrentQueueLength + 1;
			 ui32MaxRetries > 0;
			 ui32MaxRetries--)
		{
			eError = PVRSRVWaitForValueKMAndHoldBridgeLockKM(psSyncPrim->pui32LinAddr, RGXFWIF_CLEANUP_RUN, RGXFWIF_CLEANUP_RUN);

			if (eError != PVRSRV_ERROR_TIMEOUT)
			{
				break;
			}
		}

		/*
			If the firmware hasn't got back to us in a timely manner
			then bail and let the caller retry the command.
		*/
		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((PVR_DBG_WARNING,"RGXScheduleCleanupCommand: PVRSRVWaitForValueKMAndHoldBridgeLock timed out. Dump debug information."));

			eError = PVRSRV_ERROR_RETRY;
#if defined(DEBUG)
			    PVRSRVDebugRequest(DEBUG_REQUEST_VERBOSITY_MAX,IMG_NULL);
#endif
			goto fail_poll;
		}
		else if (eError != PVRSRV_OK)
		{
			goto fail_poll;
		}
	}

	/*
		If the command has was run but a resource was busy, then the request
		will need to be retried.
	*/
	if (*psSyncPrim->pui32LinAddr & RGXFWIF_CLEANUP_BUSY)
	{
		eError = PVRSRV_ERROR_RETRY;
		goto fail_requestbusy;
	}

	return PVRSRV_OK;

fail_requestbusy:
fail_poll:
fail_command:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
	RGXRequestCommonContextCleanUp
*/
PVRSRV_ERROR RGXFWRequestCommonContextCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
											  RGX_SERVER_COMMON_CONTEXT *psServerCommonContext,
											  PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim,
											  RGXFWIF_DM eDM)
{
	RGXFWIF_KCCB_CMD			sRCCleanUpCmd = {0};
	PVRSRV_ERROR				eError;
	PRGXFWIF_FWCOMMONCONTEXT	psFWCommonContextFWAddr;

	psFWCommonContextFWAddr = FWCommonContextGetFWAddress(psServerCommonContext);

	PDUMPCOMMENT("Common ctx cleanup Request DM%d [context = 0x%08x]",
					eDM, psFWCommonContextFWAddr.ui32Addr);
	PDUMPCOMMENT("Wait for CCB to be empty before common ctx cleanup");

	RGXCCBPDumpDrainCCB(FWCommonContextGetClientCCB(psServerCommonContext), IMG_FALSE);


	/* Setup our command data, the cleanup call will fill in the rest */
	sRCCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psContext = psFWCommonContextFWAddr;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDeviceNode->pvDevice,
									   eDM,
									   &sRCCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_FWCOMMONCONTEXT,
									   psSyncPrim,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRequestCommonContextCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}

/*
 * RGXRequestHWRTDataCleanUp
 */

PVRSRV_ERROR RGXFWRequestHWRTDataCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
										 PRGXFWIF_HWRTDATA psHWRTData,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync,
										 RGXFWIF_DM eDM)
{
	RGXFWIF_KCCB_CMD			sHWRTDataCleanUpCmd = {0};
	PVRSRV_ERROR				eError;

	PDUMPCOMMENT("HW RTData cleanup Request DM%d [HWRTData = 0x%08x]", eDM, psHWRTData.ui32Addr);

	sHWRTDataCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psHWRTData = psHWRTData;

	eError = RGXScheduleCleanupCommand(psDeviceNode->pvDevice,
									   eDM,
									   &sHWRTDataCleanUpCmd,
									   sizeof(sHWRTDataCleanUpCmd),
									   RGXFWIF_CLEANUP_HWRTDATA,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXRequestHWRTDataCleanUp: Failed to schedule a HWRTData cleanup with error (%u)", eError));
	}

	return eError;
}

/*
	RGXFWRequestFreeListCleanUp
*/
PVRSRV_ERROR RGXFWRequestFreeListCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
										 PRGXFWIF_FREELIST psFWFreeList,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	RGXFWIF_KCCB_CMD			sFLCleanUpCmd = {0};
	PVRSRV_ERROR 				eError;

	PDUMPCOMMENT("Free list cleanup Request [FreeList = 0x%08x]", psFWFreeList.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sFLCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psFreelist = psFWFreeList;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDevInfo,
									   RGXFWIF_DM_GP,
									   &sFLCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_FREELIST,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestFreeListCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}

/*
	RGXFWRequestZSBufferCleanUp
*/
PVRSRV_ERROR RGXFWRequestZSBufferCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
										 PRGXFWIF_ZSBUFFER psFWZSBuffer,
										 PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	RGXFWIF_KCCB_CMD			sZSBufferCleanUpCmd = {0};
	PVRSRV_ERROR 				eError;

	PDUMPCOMMENT("ZS Buffer cleanup Request [ZS Buffer = 0x%08x]", psFWZSBuffer.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sZSBufferCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psZSBuffer = psFWZSBuffer;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDevInfo,
									   RGXFWIF_DM_3D,
									   &sZSBufferCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_ZSBUFFER,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestZSBufferCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}


#if defined(RGX_FEATURE_RAY_TRACING)
PVRSRV_ERROR RGXFWRequestRayFrameDataCleanUp(PVRSRV_DEVICE_NODE *psDeviceNode,
											 PRGXFWIF_RAY_FRAME_DATA psHWFrameData,
											 PVRSRV_CLIENT_SYNC_PRIM *psSync,
											 RGXFWIF_DM eDM)
{
	RGXFWIF_KCCB_CMD			sHWFrameDataCleanUpCmd = {0};
	PVRSRV_ERROR				eError;

	PDUMPCOMMENT("HW FrameData cleanup Request DM%d [HWFrameData = 0x%08x]", eDM, psHWFrameData.ui32Addr);

	sHWFrameDataCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psHWFrameData = psHWFrameData;

	eError = RGXScheduleCleanupCommand(psDeviceNode->pvDevice,
									   eDM,
									   &sHWFrameDataCleanUpCmd,
									   sizeof(sHWFrameDataCleanUpCmd),
									   RGXFWIF_CLEANUP_HWFRAMEDATA,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestRayFrameDataCleanUp: Failed to schedule a HWFrameData cleanup with error (%u)", eError));
	}

	return eError;
}

/*
	RGXFWRequestRPMFreeListCleanUp
*/
PVRSRV_ERROR RGXFWRequestRPMFreeListCleanUp(PVRSRV_RGXDEV_INFO *psDevInfo,
											PRGXFWIF_RPM_FREELIST psFWRPMFreeList,
											PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	RGXFWIF_KCCB_CMD			sFLCleanUpCmd = {0};
	PVRSRV_ERROR 				eError;

	PDUMPCOMMENT("RPM Free list cleanup Request [RPM FreeList = 0x%08x]", psFWRPMFreeList.ui32Addr);

	/* Setup our command data, the cleanup call will fill in the rest */
	sFLCleanUpCmd.uCmdData.sCleanupData.uCleanupData.psRPMFreelist = psFWRPMFreeList;

	/* Request cleanup of the firmware resource */
	eError = RGXScheduleCleanupCommand(psDevInfo,
									   RGXFWIF_DM_GP,
									   &sFLCleanUpCmd,
									   sizeof(RGXFWIF_KCCB_CMD),
									   RGXFWIF_CLEANUP_RPM_FREELIST,
									   psSync,
									   IMG_FALSE);

	if ((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXFWRequestRPMFreeListCleanUp: Failed to schedule a memory context cleanup with error (%u)", eError));
	}

	return eError;
}
#endif

PVRSRV_ERROR ContextSetPriority(RGX_SERVER_COMMON_CONTEXT *psContext,
								CONNECTION_DATA *psConnection,
								PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32Priority,
								RGXFWIF_DM eDM)
{
	IMG_UINT32				ui32CmdSize;
	IMG_UINT8				*pui8CmdPtr;
	RGXFWIF_KCCB_CMD		sPriorityCmd;
	RGXFWIF_CCB_CMD_HEADER	*psCmdHeader;	
	RGXFWIF_CMD_PRIORITY	*psCmd;
	IMG_UINT32				ui32BeforeWOff = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext));
	IMG_BOOL				bKickCMD = IMG_TRUE;
	PVRSRV_ERROR			eError;

	/*
		Get space for command
	*/
	ui32CmdSize = RGX_CCB_FWALLOC_ALIGN(sizeof(RGXFWIF_CCB_CMD_HEADER) + sizeof(RGXFWIF_CMD_PRIORITY));

	eError = RGXAcquireCCB(FWCommonContextGetClientCCB(psContext),
						   ui32CmdSize,
						   (IMG_PVOID *) &pui8CmdPtr,
						   IMG_TRUE);
	if (eError != PVRSRV_OK)
	{
		if (ui32BeforeWOff != RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext)))
		{
			bKickCMD = IMG_FALSE;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire client CCB", __FUNCTION__));
			goto fail_ccbacquire;
		}
	}

	if (bKickCMD)
	{
		/*
			Write the command header and command
		*/
		psCmdHeader = (RGXFWIF_CCB_CMD_HEADER *) pui8CmdPtr;
		psCmdHeader->eCmdType = RGXFWIF_CCB_CMD_TYPE_PRIORITY;
		psCmdHeader->ui32CmdSize = RGX_CCB_FWALLOC_ALIGN(sizeof(RGXFWIF_CMD_PRIORITY));
		pui8CmdPtr += sizeof(*psCmdHeader);
		
		psCmd = (RGXFWIF_CMD_PRIORITY *) pui8CmdPtr;
		psCmd->ui32Priority = ui32Priority;
		pui8CmdPtr += sizeof(*psCmd);
	}

	/*
		We should reserved space in the kernel CCB here and fill in the command
		directly.
		This is so if there isn't space in the kernel CCB we can return with
		retry back to services client before we take any operations
	*/

	if (bKickCMD)
	{
		/*
			Submit the command
		*/
		RGXReleaseCCB(FWCommonContextGetClientCCB(psContext),
					  ui32CmdSize,
					  IMG_TRUE);
	
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to release space in client CCB", __FUNCTION__));
			return eError;
		}
	}

	/* Construct the priority command. */
	sPriorityCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sPriorityCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psContext);
	sPriorityCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psContext));
	sPriorityCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
									eDM,
									&sPriorityCmd,
									sizeof(sPriorityCmd),
									IMG_TRUE);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXFlushComputeDataKM: Failed to schedule SLC flush command with error (%u)", eError));
	}

	return PVRSRV_OK;

fail_ccbacquire:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
	RGXReadMETAAddr
*/
PVRSRV_ERROR RGXReadMETAAddr(PVRSRV_RGXDEV_INFO	*psDevInfo, IMG_UINT32 ui32METAAddr, IMG_UINT32 *pui32Value)
{
	IMG_UINT8 *pui8RegBase = (IMG_UINT8*)psDevInfo->pvRegsBaseKM;
	IMG_UINT32 ui32Value;

	/* Wait for Slave Port to be Ready */
	if (PVRSRVPollForValueKM(
	        (IMG_UINT32*) (pui8RegBase + RGX_CR_META_SP_MSLVCTRL1),
	        RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	        RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN) != PVRSRV_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	/* Issue the Read */
	OSWriteHWReg32(
	    psDevInfo->pvRegsBaseKM,
	    RGX_CR_META_SP_MSLVCTRL0,
	    ui32METAAddr | RGX_CR_META_SP_MSLVCTRL0_RD_EN);

	/* Wait for Slave Port to be Ready: read complete */
	if (PVRSRVPollForValueKM(
	        (IMG_UINT32*) (pui8RegBase + RGX_CR_META_SP_MSLVCTRL1),
	        RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN,
	        RGX_CR_META_SP_MSLVCTRL1_READY_EN|RGX_CR_META_SP_MSLVCTRL1_GBLPORT_IDLE_EN) != PVRSRV_OK)
	{
		return PVRSRV_ERROR_TIMEOUT;
	}

	/* Read the value */
	ui32Value = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_META_SP_MSLVDATAX);

	*pui32Value = ui32Value;

	return PVRSRV_OK;
}


/*
	RGXUpdateHealthStatus
*/
PVRSRV_ERROR RGXUpdateHealthStatus(PVRSRV_DEVICE_NODE* psDevNode,
                                   IMG_BOOL bCheckAfterTimePassed)
{
	PVRSRV_DATA*                 psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_DEVICE_HEALTH_STATUS  eNewStatus   = PVRSRV_DEVICE_HEALTH_STATUS_OK;
	PVRSRV_DEVICE_HEALTH_REASON  eNewReason   = PVRSRV_DEVICE_HEALTH_REASON_NONE;
	PVRSRV_RGXDEV_INFO*  psDevInfo;
	RGXFWIF_TRACEBUF*  psRGXFWIfTraceBufCtl;
	IMG_UINT32  ui32DMCount, ui32ThreadCount;
	IMG_BOOL  bKCCBCmdsWaiting;
	
	PVR_ASSERT(psDevNode != NULL);
	psDevInfo = psDevNode->pvDevice;
	psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;
	
	/* If the firmware is not initialised, there is not much point continuing! */
	if (!psDevInfo->bFirmwareInitialised  ||  psDevInfo->pvRegsBaseKM == IMG_NULL  ||
	    psDevInfo->psDeviceNode == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	/* If Rogue is not powered on, don't continue 
	   (there is a race condition where PVRSRVIsDevicePowered returns TRUE when the GPU is actually powering down. 
	   That's not a problem as this function does not touch the HW except for the RGXScheduleCommand function,
	   which is already powerlock safe. The worst thing that could happen is that Rogue might power back up
	   but the chances of that are very low */
	if (!PVRSRVIsDevicePowered(psDevNode->sDevId.ui32DeviceIndex))
	{
		return PVRSRV_OK;
	}
	
	/* If this is a quick update, then include the last current value... */
	if (!bCheckAfterTimePassed)
	{
		eNewStatus = psDevNode->eHealthStatus;
		eNewReason = psDevNode->eHealthReason;
	}
	
	/*
	   Firmware thread checks...
	*/
	for (ui32ThreadCount = 0;  ui32ThreadCount < RGXFW_THREAD_NUM;  ui32ThreadCount++)
	{
		if (psRGXFWIfTraceBufCtl != IMG_NULL)
		{
			IMG_CHAR*  pszTraceAssertInfo = psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.szInfo;

			/*
			Check if the FW has hit an assert...
			*/
			if (*pszTraceAssertInfo != '\0')
			{
				PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Firmware thread %d has asserted: %s (%s:%d)",
				        ui32ThreadCount, pszTraceAssertInfo,
						psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.szPath,
						psRGXFWIfTraceBufCtl->sTraceBuf[ui32ThreadCount].sAssertBuf.ui32LineNum));
				eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
				eNewReason = PVRSRV_DEVICE_HEALTH_REASON_ASSERTED;
				goto _RGXUpdateHealthStatus_Exit;
			}

			/*
			   Check the threads to see if they are in the same poll locations as last time...
			*/
			if (bCheckAfterTimePassed)
			{	
				if (psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] != 0  &&
					psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] == psDevInfo->aui32CrLastPollAddr[ui32ThreadCount])
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Firmware stuck on CR poll: T%u polling %s (reg:0x%08X mask:0x%08X)",
							ui32ThreadCount,
							((psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] & RGXFW_POLL_TYPE_SET)?("set"):("unset")), 
							psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount] & ~RGXFW_POLL_TYPE_SET, 
							psRGXFWIfTraceBufCtl->aui32CrPollMask[ui32ThreadCount]));
					eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
					eNewReason = PVRSRV_DEVICE_HEALTH_REASON_POLL_FAILING;
					goto _RGXUpdateHealthStatus_Exit;
				}
				psDevInfo->aui32CrLastPollAddr[ui32ThreadCount] = psRGXFWIfTraceBufCtl->aui32CrPollAddr[ui32ThreadCount];
			}
		}
	}

	/*
	   Event Object Timeouts check...
	*/
	if (psDevInfo->ui32GEOTimeoutsLastTime > 1  &&  psPVRSRVData->ui32GEOConsecutiveTimeouts > psDevInfo->ui32GEOTimeoutsLastTime)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Global Event Object Timeouts have risen (from %d to %d)",
				psDevInfo->ui32GEOTimeoutsLastTime, psPVRSRVData->ui32GEOConsecutiveTimeouts));
		eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING;
		eNewReason = PVRSRV_DEVICE_HEALTH_REASON_TIMEOUTS;
	}
	psDevInfo->ui32GEOTimeoutsLastTime = psPVRSRVData->ui32GEOConsecutiveTimeouts;
	
	/*
	   Check the Kernel CCB pointers are valid. If any commands were waiting last time, then check
	   that some have executed since then.
	*/
	bKCCBCmdsWaiting = IMG_FALSE;
	
	for (ui32DMCount = 0; ui32DMCount < RGXFWIF_DM_MAX; ui32DMCount++)
	{
		RGXFWIF_CCB_CTL *psKCCBCtl = ((PVRSRV_RGXDEV_INFO*)psDevNode->pvDevice)->apsKernelCCBCtl[ui32DMCount];

		if (psKCCBCtl != IMG_NULL)
		{
			if (psKCCBCtl->ui32ReadOffset > psKCCBCtl->ui32WrapMask  ||
				psKCCBCtl->ui32WriteOffset > psKCCBCtl->ui32WrapMask)
			{
				PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: KCCB for DM%d has invalid offset (ROFF=%d WOFF=%d)",
				        ui32DMCount, psKCCBCtl->ui32ReadOffset, psKCCBCtl->ui32WriteOffset));
				eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_DEAD;
				eNewReason = PVRSRV_DEVICE_HEALTH_REASON_QUEUE_CORRUPT;
			}

			if (psKCCBCtl->ui32ReadOffset != psKCCBCtl->ui32WriteOffset)
			{
				bKCCBCmdsWaiting = IMG_TRUE;
			}
		}
	}

	if (bCheckAfterTimePassed && psDevInfo->psRGXFWIfTraceBuf != IMG_NULL)
	{
		IMG_UINT32  ui32KCCBCmdsExecuted = psDevInfo->psRGXFWIfTraceBuf->ui32KCCBCmdsExecuted;
		
		if (psDevInfo->ui32KCCBCmdsExecutedLastTime == ui32KCCBCmdsExecuted)
		{
			/*
			   If something was waiting last time then the Firmware has stopped processing commands.
			*/
			if (psDevInfo->bKCCBCmdsWaitingLastTime)
			{
				PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: No KCCB commands executed since check!"));
				eNewStatus = PVRSRV_DEVICE_HEALTH_STATUS_NOT_RESPONDING;
				eNewReason = PVRSRV_DEVICE_HEALTH_REASON_QUEUE_STALLED;
			}
		
			/*
			   If no commands are currently pending and nothing happened since the last poll, then
			   schedule a dummy command to ping the firmware so we know it is alive and processing.
			*/
			if (!bKCCBCmdsWaiting)
			{
				RGXFWIF_KCCB_CMD  sCmpKCCBCmd;
				PVRSRV_ERROR      eError;

				sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_HEALTH_CHECK;

				eError = RGXScheduleCommand(psDevNode->pvDevice,
											RGXFWIF_DM_GP,
											&sCmpKCCBCmd,
											sizeof(sCmpKCCBCmd),
											IMG_TRUE);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Cannot schedule Health Check command! (0x%x)", eError));
				}
				else
				{
					bKCCBCmdsWaiting = IMG_TRUE;
				}
			}
		}

		psDevInfo->bKCCBCmdsWaitingLastTime     = bKCCBCmdsWaiting;
		psDevInfo->ui32KCCBCmdsExecutedLastTime = ui32KCCBCmdsExecuted;
	}

	if (bCheckAfterTimePassed && (PVRSRV_DEVICE_HEALTH_STATUS_OK==eNewStatus))
	{
		/* Attempt to detect and deal with any stalled client contexts */
		IMG_BOOL bStalledClient = IMG_FALSE;
		if (CheckForStalledClientTransferCtxt(psDevInfo))
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Detected stalled client transfer context"));
			bStalledClient = IMG_TRUE;
		}
		if (CheckForStalledClientRenderCtxt(psDevInfo))
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Detected stalled client render context"));
			bStalledClient = IMG_TRUE;
		}
#if !defined(UNDER_WDDM)
		if (CheckForStalledClientComputeCtxt(psDevInfo))
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Detected stalled client compute context"));
			bStalledClient = IMG_TRUE;
		}
#endif
#if defined(RGX_FEATURE_RAY_TRACING)
		if (CheckForStalledClientRayCtxt(psDevInfo))
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXGetDeviceHealthStatus: Detected stalled client raytrace context"));
			bStalledClient = IMG_TRUE;
		}
#endif
		/* try the unblock routines only on the transition from OK to stalled */
		if (!psDevInfo->bStalledClient && bStalledClient)
		{
#if defined(SUPPORT_DISPLAY_CLASS)
			//DCDisplayContextFlush();
#endif
		}
		psDevInfo->bStalledClient = bStalledClient;
	}

	/*
	   Finished, save the new status...
	*/
_RGXUpdateHealthStatus_Exit:
	psDevNode->eHealthStatus = eNewStatus;
	psDevNode->eHealthReason = eNewReason;

	return PVRSRV_OK;
} /* RGXUpdateHealthStatus */

PVRSRV_ERROR CheckStalledClientCommonContext(RGX_SERVER_COMMON_CONTEXT *psCurrentServerCommonContext)
{
	RGX_CLIENT_CCB 	*psCurrentClientCCB = psCurrentServerCommonContext->psClientCCB;

	return CheckForStalledCCB(psCurrentClientCCB);
}

IMG_VOID DumpStalledFWCommonContext(RGX_SERVER_COMMON_CONTEXT *psCurrentServerCommonContext,
									DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	RGX_CLIENT_CCB 	*psCurrentClientCCB = psCurrentServerCommonContext->psClientCCB;
	PRGXFWIF_FWCOMMONCONTEXT sFWCommonContext = psCurrentServerCommonContext->sFWCommonContextFWAddr;

#if defined(PVRSRV_ENABLE_FULL_SYNC_TRACKING) || defined(PVRSRV_ENABLE_FULL_CCB_DUMP)
	DumpCCB(sFWCommonContext, psCurrentClientCCB, pfnDumpDebugPrintf);
#else
	DumpStalledCCBCommand(sFWCommonContext, psCurrentClientCCB, pfnDumpDebugPrintf);
#endif
}

IMG_VOID AttachKickResourcesCleanupCtls(PRGXFWIF_CLEANUP_CTL *apsCleanupCtl,
									IMG_UINT32 *pui32NumCleanupCtl,
									RGXFWIF_DM eDM,
									IMG_BOOL bKick,
									RGX_RTDATA_CLEANUP_DATA        *psRTDataCleanup,
									RGX_ZSBUFFER_DATA              *psZBuffer,
									RGX_ZSBUFFER_DATA              *psSBuffer)
{
	PRGXFWIF_CLEANUP_CTL *psCleanupCtlWrite = apsCleanupCtl;

	PVR_ASSERT((eDM == RGXFWIF_DM_TA) || (eDM == RGXFWIF_DM_3D));

	if(bKick)
	{
		if(eDM == RGXFWIF_DM_TA)
		{
			if(psRTDataCleanup)
			{
				PRGXFWIF_CLEANUP_CTL psCleanupCtl;

				RGXSetFirmwareAddress(&psCleanupCtl, psRTDataCleanup->psFWHWRTDataMemDesc,
									offsetof(RGXFWIF_HWRTDATA, sTACleanupState),
								RFW_FWADDR_NOREF_FLAG);

				*(psCleanupCtlWrite++) = psCleanupCtl;
			}
		}
		else
		{
			if(psRTDataCleanup)
			{
				PRGXFWIF_CLEANUP_CTL psCleanupCtl;

				RGXSetFirmwareAddress(&psCleanupCtl, psRTDataCleanup->psFWHWRTDataMemDesc,
									offsetof(RGXFWIF_HWRTDATA, s3DCleanupState),
								RFW_FWADDR_NOREF_FLAG);

				*(psCleanupCtlWrite++) = psCleanupCtl;
			}

			if(psZBuffer)
			{
				(psCleanupCtlWrite++)->ui32Addr = psZBuffer->sZSBufferFWDevVAddr.ui32Addr +
								offsetof(RGXFWIF_FWZSBUFFER, sCleanupState);
			}

			if(psSBuffer)
			{
				(psCleanupCtlWrite++)->ui32Addr = psSBuffer->sZSBufferFWDevVAddr.ui32Addr +
								offsetof(RGXFWIF_FWZSBUFFER, sCleanupState);
			}
		}
	}

	*pui32NumCleanupCtl = psCleanupCtlWrite - apsCleanupCtl;

	PVR_ASSERT(*pui32NumCleanupCtl <= RGXFWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS);
}

PVRSRV_ERROR RGXResetHWRLogs(PVRSRV_DEVICE_NODE *psDevNode)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo;
	RGXFWIF_HWRINFOBUF	*psHWRInfoBuf;
	RGXFWIF_TRACEBUF 	*psRGXFWIfTraceBufCtl;
	IMG_UINT32 			i;

	if(psDevNode->pvDevice == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVINFO;
	}
	psDevInfo = psDevNode->pvDevice;

	psHWRInfoBuf = psDevInfo->psRGXFWIfHWRInfoBuf;
	psRGXFWIfTraceBufCtl = psDevInfo->psRGXFWIfTraceBuf;

	for(i = 0 ; i < RGXFWIF_DM_MAX ; i++)
	{
		/* Reset the HWR numbers */
		psRGXFWIfTraceBufCtl->aui16HwrDmLockedUpCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmFalseDetectCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmRecoveredCount[i] = 0;
		psRGXFWIfTraceBufCtl->aui16HwrDmOverranCount[i] = 0;
	}

	for(i = 0 ; i < RGXFWIF_HWINFO_MAX ; i++)
	{
		psHWRInfoBuf->sHWRInfo[i].ui32HWRNumber = 0;
	}

	for(i = 0 ; i < RGXFW_THREAD_NUM ; i++)
	{
		psHWRInfoBuf->ui32FirstCrPollAddr[i] = 0;
		psHWRInfoBuf->ui32FirstCrPollMask[i] = 0;
	}

	psHWRInfoBuf->ui32WriteIndex = 0;
	psHWRInfoBuf->ui32DDReqCount = 0;

	return PVRSRV_OK;
}

#if defined(PDUMP)
PVRSRV_ERROR RGXPdumpDrainKCCB(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32WriteOffset, RGXFWIF_DM eKCCBType)
{
	RGXFWIF_CCB_CTL *psKCCBCtl = psDevInfo->apsKernelCCBCtl[eKCCBType];
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psDevInfo->abDumpedKCCBCtlAlready[eKCCBType])
	{
		/* exiting capture range */
		psDevInfo->abDumpedKCCBCtlAlready[eKCCBType] = IMG_FALSE;

		/* make sure previous cmd is drained in pdump in case we will 'jump' over some future cmds */
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS | PDUMP_FLAGS_POWER,
                                      "kCCB(%p): Draining rgxfw_roff (0x%x) == woff (0x%x)",
                                      psKCCBCtl,
                                      ui32WriteOffset,
                                      ui32WriteOffset);
		eError = DevmemPDumpDevmemPol32(psDevInfo->apsKernelCCBCtlMemDesc[eKCCBType],
                                                offsetof(RGXFWIF_CCB_CTL, ui32ReadOffset),
                                                ui32WriteOffset,
                                                0xffffffff,
                                                PDUMP_POLL_OPERATOR_EQUAL,
                                                PDUMP_FLAGS_CONTINUOUS | PDUMP_FLAGS_POWER);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXPdumpDrainKCCB: problem pdumping POL for kCCBCtl (%d)", eError));
		}
	}
	return eError;
}
#endif

/******************************************************************************
 End of file (rgxfwutils.c)
******************************************************************************/
