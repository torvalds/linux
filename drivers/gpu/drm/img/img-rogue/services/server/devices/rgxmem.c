/*************************************************************************/ /*!
@File
@Title          RGX memory context management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX memory context management
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

#include "pvr_debug.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_server_utils.h"
#include "devicemem_pdump.h"
#include "rgxdevice.h"
#include "rgx_fwif_km.h"
#include "rgxfwutils.h"
#include "pdump_km.h"
#include "pdump_physmem.h"
#include "pvr_notifier.h"
#include "pvrsrv.h"
#include "sync_internal.h"
#include "rgx_memallocflags.h"
#include "rgx_bvnc_defs_km.h"
#include "info_page.h"

#if defined(PDUMP)
#include "sync.h"
#endif

struct SERVER_MMU_CONTEXT_TAG
{
	DEVMEM_MEMDESC *psFWMemContextMemDesc;
	PRGXFWIF_FWMEMCONTEXT sFWMemContextDevVirtAddr;
	MMU_CONTEXT *psMMUContext;
	IMG_PID uiPID;
	IMG_CHAR szProcessName[RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME];
	IMG_UINT64 ui64FBSCEntryMask;
	DLLIST_NODE sNode;
	PVRSRV_RGXDEV_INFO *psDevInfo;
}; /* SERVER_MMU_CONTEXT is typedef-ed in rgxmem.h */

PVRSRV_ERROR RGXSLCFlushRange(PVRSRV_DEVICE_NODE *psDeviceNode,
							  MMU_CONTEXT *psMMUContext,
							  IMG_DEV_VIRTADDR sDevVAddr,
							  IMG_DEVMEM_SIZE_T uiSize,
							  IMG_BOOL bInvalidate)
{
	PVRSRV_ERROR eError;
	DLLIST_NODE *psNode, *psNext;
	RGXFWIF_KCCB_CMD sFlushInvalCmd;
	SERVER_MMU_CONTEXT *psServerMMUContext = NULL;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32 ui32kCCBCommandSlot;

	OSWRLockAcquireRead(psDevInfo->hMemoryCtxListLock);

	dllist_foreach_node(&psDevInfo->sMemoryContextList, psNode, psNext)
	{
		SERVER_MMU_CONTEXT *psIter = IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);
		if (psIter->psMMUContext == psMMUContext)
		{
			psServerMMUContext = psIter;
		}
	}

	OSWRLockReleaseRead(psDevInfo->hMemoryCtxListLock);

	if (! psServerMMUContext)
	{
		return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
	}

	/* Schedule the SLC flush command */
#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Submit SLC flush and invalidate");
#endif
	sFlushInvalCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCFLUSHINVAL;
	sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bInval = bInvalidate;
	sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bDMContext = IMG_FALSE;
	sFlushInvalCmd.uCmdData.sSLCFlushInvalData.ui64Size = uiSize;
	sFlushInvalCmd.uCmdData.sSLCFlushInvalData.ui64Address = sDevVAddr.uiAddr;
	eError = RGXGetFWCommonContextAddrFromServerMMUCtx(psDevInfo,
													   psServerMMUContext,
													   &sFlushInvalCmd.uCmdData.sSLCFlushInvalData.psContext);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = RGXSendCommandWithPowLockAndGetKCCBSlot(psDevInfo,
									   &sFlushInvalCmd,
									   PDUMP_FLAGS_CONTINUOUS,
									   &ui32kCCBCommandSlot);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "RGXSLCFlush: Failed to schedule SLC flush command with error (%u)",
		         eError));
	}
	else
	{
		/* Wait for the SLC flush to complete */
		eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "RGXSLCFlush: SLC flush and invalidate aborted with error (%u)",
			         eError));
		}
	}

	return eError;
}

PVRSRV_ERROR RGXInvalidateFBSCTable(PVRSRV_DEVICE_NODE *psDeviceNode,
									MMU_CONTEXT *psMMUContext,
									IMG_UINT64 ui64FBSCEntryMask)
{
	DLLIST_NODE *psNode, *psNext;
	SERVER_MMU_CONTEXT *psServerMMUContext = NULL;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	OSWRLockAcquireRead(psDevInfo->hMemoryCtxListLock);

	dllist_foreach_node(&psDevInfo->sMemoryContextList, psNode, psNext)
	{
		SERVER_MMU_CONTEXT *psIter = IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);
		if (psIter->psMMUContext == psMMUContext)
		{
			psServerMMUContext = psIter;
		}
	}

	OSWRLockReleaseRead(psDevInfo->hMemoryCtxListLock);

	if (! psServerMMUContext)
	{
		return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
	}

	/* Accumulate the FBSC invalidate request */
	psServerMMUContext->ui64FBSCEntryMask |= ui64FBSCEntryMask;

	return PVRSRV_OK;
}

/*
 * RGXExtractFBSCEntryMaskFromMMUContext
 *
 */
PVRSRV_ERROR RGXExtractFBSCEntryMaskFromMMUContext(PVRSRV_DEVICE_NODE *psDeviceNode,
												   SERVER_MMU_CONTEXT *psServerMMUContext,
												   IMG_UINT64 *pui64FBSCEntryMask)
{
	if (!psServerMMUContext)
	{
		return PVRSRV_ERROR_MMU_CONTEXT_NOT_FOUND;
	}

	*pui64FBSCEntryMask = psServerMMUContext->ui64FBSCEntryMask;
	psServerMMUContext->ui64FBSCEntryMask = 0;

	return PVRSRV_OK;
}

void RGXMMUCacheInvalidate(PVRSRV_DEVICE_NODE *psDeviceNode,
						   MMU_CONTEXT *psMMUContext,
						   MMU_LEVEL eMMULevel,
						   IMG_BOOL bUnmap)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	IMG_UINT32 ui32NewCacheFlags;

	PVR_UNREFERENCED_PARAMETER(bUnmap);

	switch (eMMULevel)
	{
		case MMU_LEVEL_3:
			ui32NewCacheFlags = RGXFWIF_MMUCACHEDATA_FLAGS_PC;

			break;
		case MMU_LEVEL_2:
			ui32NewCacheFlags = RGXFWIF_MMUCACHEDATA_FLAGS_PD;

			break;
		case MMU_LEVEL_1:
			ui32NewCacheFlags = RGXFWIF_MMUCACHEDATA_FLAGS_PT;

#if defined(RGX_FEATURE_SLC_VIVT_BIT_MASK)
			if (!(RGX_IS_FEATURE_SUPPORTED(psDevInfo, SLC_VIVT)))
#endif
			{
				ui32NewCacheFlags |= RGXFWIF_MMUCACHEDATA_FLAGS_TLB;
			}

			break;
		default:
			ui32NewCacheFlags = 0;
			PVR_ASSERT(0);

			break;
	}

#if defined(RGX_FEATURE_SLC_VIVT_BIT_MASK)
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, SLC_VIVT))
	{
		MMU_AppendCacheFlags(psMMUContext, ui32NewCacheFlags);
	}
	else
#endif
	{
		MMU_AppendCacheFlags(psDevInfo->psKernelMMUCtx, ui32NewCacheFlags);
	}
}

static inline void _GetAndResetCacheOpsPending(PVRSRV_RGXDEV_INFO *psDevInfo,
                                               IMG_UINT32 *pui32FWCacheFlags)
{
	/*
	 * Atomically exchange flags and 0 to ensure we never accidentally read
	 * state inconsistently or overwrite valid cache flags with 0.
	 */
	*pui32FWCacheFlags = MMU_ExchangeCacheFlags(psDevInfo->psKernelMMUCtx, 0);
}

static
PVRSRV_ERROR _PrepareAndSubmitCacheCommand(PVRSRV_DEVICE_NODE *psDeviceNode,
                                           RGXFWIF_DM eDM,
                                           IMG_UINT32 ui32CacheFlags,
                                           IMG_BOOL bInterrupt,
                                           IMG_UINT32 *pui32MMUInvalidateUpdate)
{
	PVRSRV_ERROR eError;
	RGXFWIF_KCCB_CMD sFlushCmd;
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;

	*pui32MMUInvalidateUpdate = psDeviceNode->ui32NextMMUInvalidateUpdate++;

	/* Setup cmd and add the device nodes sync object */
	sFlushCmd.eCmdType = RGXFWIF_KCCB_CMD_MMUCACHE;
	sFlushCmd.uCmdData.sMMUCacheData.ui32MMUCacheSyncUpdateValue = *pui32MMUInvalidateUpdate;
	SyncPrimGetFirmwareAddr(psDeviceNode->psMMUCacheSyncPrim,
	                        &sFlushCmd.uCmdData.sMMUCacheData.sMMUCacheSync.ui32Addr);

	/* Indicate the firmware should signal command completion to the host */
	if (bInterrupt)
	{
		ui32CacheFlags |= RGXFWIF_MMUCACHEDATA_FLAGS_INTERRUPT;
	}

	sFlushCmd.uCmdData.sMMUCacheData.ui32CacheFlags = ui32CacheFlags;

#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
	                      "Submit MMU flush and invalidate (flags = 0x%08x)",
	                      ui32CacheFlags);
#endif

	/* Schedule MMU cache command */
	eError = RGXSendCommand(psDevInfo,
							&sFlushCmd,
							PDUMP_FLAGS_CONTINUOUS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to schedule MMU cache command to "
		         "DM=%d with error (%u)",
		         __func__, eDM, eError));
		psDeviceNode->ui32NextMMUInvalidateUpdate--;
	}

	return eError;
}

PVRSRV_ERROR RGXMMUCacheInvalidateKick(PVRSRV_DEVICE_NODE *psDeviceNode,
                                       IMG_UINT32 *pui32MMUInvalidateUpdate)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32FWCacheFlags;
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	eError = PVRSRVPowerLock(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: failed to acquire powerlock (%s)",
					__func__, PVRSRVGetErrorString(eError)));
		goto RGXMMUCacheInvalidateKick_exit;
	}

	_GetAndResetCacheOpsPending(psDeviceNode->pvDevice, &ui32FWCacheFlags);
	if (ui32FWCacheFlags == 0)
	{
		/* Nothing to do if no cache ops pending */
		eError = PVRSRV_OK;
		goto _PowerUnlockAndReturnErr;
	}

	/* Ensure device is powered up before sending cache command */
	PDUMPPOWCMDSTART(psDeviceNode);
	eError = PVRSRVSetDevicePowerStateKM(psDeviceNode,
										 PVRSRV_DEV_POWER_STATE_ON,
										 PVRSRV_POWER_FLAGS_NONE);
	PDUMPPOWCMDEND(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: failed to transition RGX to ON (%s)",
					__func__, PVRSRVGetErrorString(eError)));
		MMU_AppendCacheFlags(psDevInfo->psKernelMMUCtx, ui32FWCacheFlags);
		goto _PowerUnlockAndReturnErr;
	}

	eError = _PrepareAndSubmitCacheCommand(psDeviceNode, RGXFWIF_DM_GP, ui32FWCacheFlags,
										   IMG_TRUE, pui32MMUInvalidateUpdate);
	if (eError != PVRSRV_OK)
	{
		/* failed to submit cache operations, return failure */
		PVR_DPF((PVR_DBG_WARNING, "%s: failed to submit cache command (%s)",
					__func__, PVRSRVGetErrorString(eError)));
		MMU_AppendCacheFlags(psDevInfo->psKernelMMUCtx, ui32FWCacheFlags);
		goto _PowerUnlockAndReturnErr;
	}

_PowerUnlockAndReturnErr:
	PVRSRVPowerUnlock(psDeviceNode);

RGXMMUCacheInvalidateKick_exit:
	return eError;
}

PVRSRV_ERROR RGXPreKickCacheCommand(PVRSRV_RGXDEV_INFO *psDevInfo,
									RGXFWIF_DM eDM,
									IMG_UINT32 *pui32MMUInvalidateUpdate)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_UINT32 ui32FWCacheFlags;

	/* Caller should ensure that power lock is held before calling this function */
	PVR_ASSERT(OSLockIsLocked(psDeviceNode->hPowerLock));

	_GetAndResetCacheOpsPending(psDeviceNode->pvDevice, &ui32FWCacheFlags);
	if (ui32FWCacheFlags == 0)
	{
		/* Nothing to do if no cache ops pending */
		return PVRSRV_OK;
	}

	return _PrepareAndSubmitCacheCommand(psDeviceNode, eDM, ui32FWCacheFlags,
	                                     IMG_FALSE, pui32MMUInvalidateUpdate);
}

/* page fault debug is the only current use case for needing to find process info
 * after that process device memory context has been destroyed
 */

typedef struct _UNREGISTERED_MEMORY_CONTEXT_
{
	IMG_PID uiPID;
	IMG_CHAR szProcessName[RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME];
	IMG_DEV_PHYADDR sPCDevPAddr;
} UNREGISTERED_MEMORY_CONTEXT;

/* must be a power of two */
#define UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE (1 << 3)

static UNREGISTERED_MEMORY_CONTEXT gasUnregisteredMemCtxs[UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE];
static IMG_UINT32 gui32UnregisteredMemCtxsHead;

/* record a device memory context being unregistered.
 * the list of unregistered contexts can be used to find the PID and process name
 * belonging to a memory context which has been destroyed
 */
static void _RecordUnregisteredMemoryContext(PVRSRV_RGXDEV_INFO *psDevInfo, SERVER_MMU_CONTEXT *psServerMMUContext)
{
	UNREGISTERED_MEMORY_CONTEXT *psRecord;

	OSLockAcquire(psDevInfo->hMMUCtxUnregLock);

	psRecord = &gasUnregisteredMemCtxs[gui32UnregisteredMemCtxsHead];

	gui32UnregisteredMemCtxsHead = (gui32UnregisteredMemCtxsHead + 1)
					& (UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE - 1);

	OSLockRelease(psDevInfo->hMMUCtxUnregLock);

	psRecord->uiPID = psServerMMUContext->uiPID;
	if (MMU_AcquireBaseAddr(psServerMMUContext->psMMUContext, &psRecord->sPCDevPAddr) != PVRSRV_OK)
	{
		PVR_LOG(("_RecordUnregisteredMemoryContext: Failed to get PC address for memory context"));
	}
	OSStringLCopy(psRecord->szProcessName, psServerMMUContext->szProcessName, sizeof(psRecord->szProcessName));
}


void RGXUnregisterMemoryContext(IMG_HANDLE hPrivData)
{
	SERVER_MMU_CONTEXT *psServerMMUContext = hPrivData;
	PVRSRV_RGXDEV_INFO *psDevInfo = psServerMMUContext->psDevInfo;

#if defined(PDUMP)
	{
		RGXFWIF_DEV_VIRTADDR sFWAddr;

		RGXSetFirmwareAddress(&sFWAddr,
		                      psServerMMUContext->psFWMemContextMemDesc,
		                      0,
		                      RFW_FWADDR_NOREF_FLAG);

		/*
		 * MMU cache commands (always dumped) might have a pointer to this FW
		 * memory context, wait until the FW has caught-up to the latest command.
		 */
		PDUMPCOMMENT(psDevInfo->psDeviceNode,
		             "Ensure FW has executed all MMU invalidations on FW memory "
		             "context 0x%x before freeing it", sFWAddr.ui32Addr);
		SyncPrimPDumpPol(psDevInfo->psDeviceNode->psMMUCacheSyncPrim,
		                 psDevInfo->psDeviceNode->ui32NextMMUInvalidateUpdate - 1,
		                 0xFFFFFFFF,
		                 PDUMP_POLL_OPERATOR_GREATEREQUAL,
		                 PDUMP_FLAGS_CONTINUOUS);
	}
#endif

	OSWRLockAcquireWrite(psDevInfo->hMemoryCtxListLock);
	dllist_remove_node(&psServerMMUContext->sNode);
	OSWRLockReleaseWrite(psDevInfo->hMemoryCtxListLock);

	if (GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED)
	{
		_RecordUnregisteredMemoryContext(psDevInfo, psServerMMUContext);
	}

	/*
	 * Release the page catalogue address acquired in RGXRegisterMemoryContext().
	 */
	MMU_ReleaseBaseAddr(NULL);

	/*
	 * Free the firmware memory context.
	 */
	PDUMPCOMMENT(psDevInfo->psDeviceNode, "Free FW memory context");
	DevmemFwUnmapAndFree(psDevInfo, psServerMMUContext->psFWMemContextMemDesc);

	OSFreeMem(psServerMMUContext);
}

/*
 * RGXRegisterMemoryContext
 */
PVRSRV_ERROR RGXRegisterMemoryContext(PVRSRV_DEVICE_NODE	*psDeviceNode,
									  MMU_CONTEXT			*psMMUContext,
									  IMG_HANDLE			*hPrivData)
{
	PVRSRV_ERROR			eError;
	PVRSRV_RGXDEV_INFO		*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_MEMALLOCFLAGS_T	uiFWMemContextMemAllocFlags;
	RGXFWIF_FWMEMCONTEXT	*psFWMemContext;
	DEVMEM_MEMDESC			*psFWMemContextMemDesc;
	SERVER_MMU_CONTEXT *psServerMMUContext;

	if (psDevInfo->psKernelMMUCtx == NULL)
	{
		/*
		 * This must be the creation of the Kernel memory context. Take a copy
		 * of the MMU context for use when programming the BIF.
		 */
		psDevInfo->psKernelMMUCtx = psMMUContext;

#if defined(RGX_BRN71422_TARGET_HARDWARE_PHYSICAL_ADDR)
		/* Setup the BRN71422 mapping in the FW memory context. */
		if (RGX_IS_BRN_SUPPORTED(psDevInfo, 71422))
		{
			RGXMapBRN71422TargetPhysicalAddress(psMMUContext);
		}
#endif
	}
	else
	{
		psServerMMUContext = OSAllocMem(sizeof(*psServerMMUContext));
		if (psServerMMUContext == NULL)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto fail_alloc_server_ctx;
		}

		psServerMMUContext->psDevInfo = psDevInfo;
		psServerMMUContext->ui64FBSCEntryMask = 0;
		psServerMMUContext->sFWMemContextDevVirtAddr.ui32Addr = 0;

		/*
		 * This FW MemContext is only mapped into kernel for initialisation purposes.
		 * Otherwise this allocation is only used by the FW.
		 * Therefore the GPU cache doesn't need coherency, and write-combine
		 * will suffice on the CPU side (WC buffer will be flushed at any kick)
		 */
		uiFWMemContextMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
										PVRSRV_MEMALLOCFLAG_CPU_READABLE |
										PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC |
										PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
										PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN);

		/*
			Allocate device memory for the firmware memory context for the new
			application.
		*/
		PDUMPCOMMENT(psDevInfo->psDeviceNode, "Allocate RGX firmware memory context");
		eError = DevmemFwAllocate(psDevInfo,
								sizeof(*psFWMemContext),
								uiFWMemContextMemAllocFlags | PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
								"FwMemoryContext",
								&psFWMemContextMemDesc);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed to allocate firmware memory context (%u)",
			         __func__,
			         eError));
			goto fail_alloc_fw_ctx;
		}

		/*
			Temporarily map the firmware memory context to the kernel.
		*/
		eError = DevmemAcquireCpuVirtAddr(psFWMemContextMemDesc,
										  (void **)&psFWMemContext);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed to map firmware memory context (%u)",
			         __func__,
			         eError));
			goto fail_acquire_cpu_addr;
		}

		/*
		 * Write the new memory context's page catalogue into the firmware memory
		 * context for the client.
		 */
		eError = MMU_AcquireBaseAddr(psMMUContext, &psFWMemContext->sPCDevPAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed to acquire Page Catalogue address (%u)",
			         __func__,
			         eError));
			DevmemReleaseCpuVirtAddr(psFWMemContextMemDesc);
			goto fail_acquire_base_addr;
		}

		/*
		 * Set default values for the rest of the structure.
		 */
		psFWMemContext->uiPageCatBaseRegSet = RGXFW_BIF_INVALID_PCSET;
		psFWMemContext->uiBreakpointAddr = 0;
		psFWMemContext->uiBPHandlerAddr = 0;
		psFWMemContext->uiBreakpointCtl = 0;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		IMG_UINT32 ui32OSid = 0, ui32OSidReg = 0;
		IMG_BOOL   bOSidAxiProt;

		MMU_GetOSids(psMMUContext, &ui32OSid, &ui32OSidReg, &bOSidAxiProt);

		psFWMemContext->ui32OSid     = ui32OSidReg;
		psFWMemContext->bOSidAxiProt = bOSidAxiProt;
}
#endif

#if defined(PDUMP)
		{
			IMG_CHAR			aszName[PHYSMEM_PDUMP_MEMSPNAME_SYMB_ADDR_MAX_LENGTH];
			IMG_DEVMEM_OFFSET_T uiOffset = 0;

			/*
			 * Dump the Mem context allocation
			 */
			DevmemPDumpLoadMem(psFWMemContextMemDesc, 0, sizeof(*psFWMemContext), PDUMP_FLAGS_CONTINUOUS);


			/*
			 * Obtain a symbolic addr of the mem context structure
			 */
			eError = DevmemPDumpPageCatBaseToSAddr(psFWMemContextMemDesc,
												   &uiOffset,
												   aszName,
												   PHYSMEM_PDUMP_MEMSPNAME_SYMB_ADDR_MAX_LENGTH);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "%s: Failed to generate a Dump Page Catalogue address (%u)",
				         __func__,
				         eError));
				DevmemReleaseCpuVirtAddr(psFWMemContextMemDesc);
				goto fail_pdump_cat_base_addr;
			}

			/*
			 * Dump the Page Cat tag in the mem context (symbolic address)
			 */
			eError = MMU_PDumpWritePageCatBase(psMMUContext,
												aszName,
												uiOffset,
												8, /* 64-bit register write */
												0,
												0,
												0);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "%s: Failed to acquire Page Catalogue address (%u)",
				         __func__,
				         eError));
				DevmemReleaseCpuVirtAddr(psFWMemContextMemDesc);
				goto fail_pdump_cat_base;
			}
		}
#endif

		/*
		 * Release kernel address acquired above.
		 */
		DevmemReleaseCpuVirtAddr(psFWMemContextMemDesc);

		/*
		 * Store the process information for this device memory context
		 * for use with the host page-fault analysis.
		 */
		psServerMMUContext->uiPID = OSGetCurrentClientProcessIDKM();
		psServerMMUContext->psMMUContext = psMMUContext;
		psServerMMUContext->psFWMemContextMemDesc = psFWMemContextMemDesc;
		OSStringLCopy(psServerMMUContext->szProcessName,
		              OSGetCurrentClientProcessNameKM(),
		              sizeof(psServerMMUContext->szProcessName));

		PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS,
		                      "New memory context: Process Name: %s PID: %u (0x%08X)",
		                      psServerMMUContext->szProcessName,
		                      psServerMMUContext->uiPID,
		                      psServerMMUContext->uiPID);

		OSWRLockAcquireWrite(psDevInfo->hMemoryCtxListLock);
		dllist_add_to_tail(&psDevInfo->sMemoryContextList, &psServerMMUContext->sNode);
		OSWRLockReleaseWrite(psDevInfo->hMemoryCtxListLock);

		*hPrivData = psServerMMUContext;
	}

	return PVRSRV_OK;

#if defined(PDUMP)
fail_pdump_cat_base:
fail_pdump_cat_base_addr:
	MMU_ReleaseBaseAddr(NULL);
#endif
fail_acquire_base_addr:
	/* Done before jumping to the fail point as the release is done before exit */
fail_acquire_cpu_addr:
	DevmemFwUnmapAndFree(psDevInfo, psServerMMUContext->psFWMemContextMemDesc);
fail_alloc_fw_ctx:
	OSFreeMem(psServerMMUContext);
fail_alloc_server_ctx:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

DEVMEM_MEMDESC *RGXGetFWMemDescFromMemoryContextHandle(IMG_HANDLE hPriv)
{
	SERVER_MMU_CONTEXT *psMMUContext = (SERVER_MMU_CONTEXT *) hPriv;

	return psMMUContext->psFWMemContextMemDesc;
}

void RGXSetFWMemContextDevVirtAddr(SERVER_MMU_CONTEXT *psServerMMUContext,
						RGXFWIF_DEV_VIRTADDR	sFWMemContextAddr)
{
	psServerMMUContext->sFWMemContextDevVirtAddr.ui32Addr = sFWMemContextAddr.ui32Addr;
}

void RGXCheckFaultAddress(PVRSRV_RGXDEV_INFO *psDevInfo,
				IMG_DEV_VIRTADDR *psDevVAddr,
				IMG_DEV_PHYADDR *psDevPAddr,
				MMU_FAULT_DATA *psOutFaultData)
{
	IMG_DEV_PHYADDR sPCDevPAddr;
	DLLIST_NODE *psNode, *psNext;

	OSWRLockAcquireRead(psDevInfo->hMemoryCtxListLock);

	dllist_foreach_node(&psDevInfo->sMemoryContextList, psNode, psNext)
	{
		SERVER_MMU_CONTEXT *psServerMMUContext =
			IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);

		if (MMU_AcquireBaseAddr(psServerMMUContext->psMMUContext, &sPCDevPAddr) != PVRSRV_OK)
		{
			PVR_LOG(("Failed to get PC address for memory context"));
			continue;
		}

		if (psDevPAddr->uiAddr == sPCDevPAddr.uiAddr)
		{
			MMU_CheckFaultAddress(psServerMMUContext->psMMUContext, psDevVAddr, psOutFaultData);
			goto out_unlock;
		}
	}

	/* Lastly check for fault in the kernel allocated memory */
	if (MMU_AcquireBaseAddr(psDevInfo->psKernelMMUCtx, &sPCDevPAddr) != PVRSRV_OK)
	{
		PVR_LOG(("Failed to get PC address for kernel memory context"));
	}

	if (psDevPAddr->uiAddr == sPCDevPAddr.uiAddr)
	{
		MMU_CheckFaultAddress(psDevInfo->psKernelMMUCtx, psDevVAddr, psOutFaultData);
	}

out_unlock:
	OSWRLockReleaseRead(psDevInfo->hMemoryCtxListLock);
}

/* given the physical address of a page catalogue, searches for a corresponding
 * MMU context and if found, provides the caller details of the process.
 * Returns IMG_TRUE if a process is found.
 */
IMG_BOOL RGXPCAddrToProcessInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_PHYADDR sPCAddress,
								RGXMEM_PROCESS_INFO *psInfo)
{
	IMG_BOOL bRet = IMG_FALSE;
	DLLIST_NODE *psNode, *psNext;
	SERVER_MMU_CONTEXT *psServerMMUContext = NULL;

	/* check if the input PC addr corresponds to an active memory context */
	dllist_foreach_node(&psDevInfo->sMemoryContextList, psNode, psNext)
	{
		SERVER_MMU_CONTEXT *psThisMMUContext =
			IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);
		IMG_DEV_PHYADDR sPCDevPAddr;

		if (MMU_AcquireBaseAddr(psThisMMUContext->psMMUContext, &sPCDevPAddr) != PVRSRV_OK)
		{
			PVR_LOG(("Failed to get PC address for memory context"));
			continue;
		}

		if (sPCAddress.uiAddr == sPCDevPAddr.uiAddr)
		{
			psServerMMUContext = psThisMMUContext;
			break;
		}
	}

	if (psServerMMUContext != NULL)
	{
		psInfo->uiPID = psServerMMUContext->uiPID;
		OSStringLCopy(psInfo->szProcessName, psServerMMUContext->szProcessName, sizeof(psInfo->szProcessName));
		psInfo->bUnregistered = IMG_FALSE;
		bRet = IMG_TRUE;
	}
	/* else check if the input PC addr corresponds to the firmware */
	else
	{
		IMG_DEV_PHYADDR sKernelPCDevPAddr;
		PVRSRV_ERROR eError;

		eError = MMU_AcquireBaseAddr(psDevInfo->psKernelMMUCtx, &sKernelPCDevPAddr);

		if (eError != PVRSRV_OK)
		{
			PVR_LOG(("Failed to get PC address for kernel memory context"));
		}
		else
		{
			if (sPCAddress.uiAddr == sKernelPCDevPAddr.uiAddr)
			{
				psInfo->uiPID = RGXMEM_SERVER_PID_FIRMWARE;
				OSStringLCopy(psInfo->szProcessName, "Firmware", sizeof(psInfo->szProcessName));
				psInfo->bUnregistered = IMG_FALSE;
				bRet = IMG_TRUE;
			}
		}
	}

	if ((GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED) &&
		(bRet == IMG_FALSE))
	{
		/* no active memory context found with the given PC address.
		 * Check the list of most recently freed memory contexts.
		 */
		IMG_UINT32 i;

		OSLockAcquire(psDevInfo->hMMUCtxUnregLock);

		/* iterate through the list of unregistered memory contexts
		 * from newest (one before the head) to the oldest (the current head)
		 */
		i = gui32UnregisteredMemCtxsHead;

		do
		{
			UNREGISTERED_MEMORY_CONTEXT *psRecord;

			i ? i-- : (i = (UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE - 1));

			psRecord = &gasUnregisteredMemCtxs[i];

			if (psRecord->sPCDevPAddr.uiAddr == sPCAddress.uiAddr)
			{
				psInfo->uiPID = psRecord->uiPID;
				OSStringLCopy(psInfo->szProcessName, psRecord->szProcessName, sizeof(psInfo->szProcessName));
				psInfo->bUnregistered = IMG_TRUE;
				bRet = IMG_TRUE;
				break;
			}
		} while (i != gui32UnregisteredMemCtxsHead);

		OSLockRelease(psDevInfo->hMMUCtxUnregLock);

	}

	return bRet;
}

IMG_BOOL RGXPCPIDToProcessInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_PID uiPID,
								RGXMEM_PROCESS_INFO *psInfo)
{
	IMG_BOOL bRet = IMG_FALSE;
	DLLIST_NODE *psNode, *psNext;
	SERVER_MMU_CONTEXT *psServerMMUContext = NULL;

	/* check if the input PID corresponds to an active memory context */
	dllist_foreach_node(&psDevInfo->sMemoryContextList, psNode, psNext)
	{
		SERVER_MMU_CONTEXT *psThisMMUContext =
			IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);

		if (psThisMMUContext->uiPID == uiPID)
		{
			psServerMMUContext = psThisMMUContext;
			break;
		}
	}

	if (psServerMMUContext != NULL)
	{
		psInfo->uiPID = psServerMMUContext->uiPID;
		OSStringLCopy(psInfo->szProcessName, psServerMMUContext->szProcessName, sizeof(psInfo->szProcessName));
		psInfo->bUnregistered = IMG_FALSE;
		bRet = IMG_TRUE;
	}
	/* else check if the input PID corresponds to the firmware */
	else if (uiPID == RGXMEM_SERVER_PID_FIRMWARE)
	{
		psInfo->uiPID = RGXMEM_SERVER_PID_FIRMWARE;
		OSStringLCopy(psInfo->szProcessName, "Firmware", sizeof(psInfo->szProcessName));
		psInfo->bUnregistered = IMG_FALSE;
		bRet = IMG_TRUE;
	}

	if ((GetInfoPageDebugFlagsKM() & DEBUG_FEATURE_PAGE_FAULT_DEBUG_ENABLED) &&
		(bRet == IMG_FALSE))
	{
		/* if the PID didn't correspond to an active context or the
		 * FW address then see if it matches a recently unregistered context
		 */
		const IMG_UINT32 ui32Mask = UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE - 1;
		IMG_UINT32 i, j;

		OSLockAcquire(psDevInfo->hMMUCtxUnregLock);

		for (i = (gui32UnregisteredMemCtxsHead - 1) & ui32Mask, j = 0;
		     j < UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE;
		     i = (gui32UnregisteredMemCtxsHead - 1) & ui32Mask, j++)
		{
			UNREGISTERED_MEMORY_CONTEXT *psRecord = &gasUnregisteredMemCtxs[i];

			if (psRecord->uiPID == uiPID)
			{
				psInfo->uiPID = psRecord->uiPID;
				OSStringLCopy(psInfo->szProcessName, psRecord->szProcessName, sizeof(psInfo->szProcessName));
				psInfo->bUnregistered = IMG_TRUE;
				bRet = IMG_TRUE;
				break;
			}
		}

		OSLockRelease(psDevInfo->hMMUCtxUnregLock);
	}

	return bRet;
}

IMG_PID RGXGetPIDFromServerMMUContext(SERVER_MMU_CONTEXT *psServerMMUContext)
{
	if (psServerMMUContext)
	{
		return psServerMMUContext->uiPID;
	}
	return 0;
}

/******************************************************************************
 End of file (rgxmem.c)
******************************************************************************/
