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
#include "pvrsrv.h"
#include "sync_internal.h"
#include "rgx_memallocflags.h"

/*
	FIXME:
	For now just get global state, but what we really want is to do
	this per memory context
*/
static IMG_UINT32 ui32CacheOpps = 0;
static IMG_UINT32 ui32CacheOpSequence = 0;
/* FIXME: End */

typedef struct _SERVER_MMU_CONTEXT_ {
	DEVMEM_MEMDESC *psFWMemContextMemDesc;
	MMU_CONTEXT *psMMUContext;
	IMG_PID uiPID;
	IMG_CHAR szProcessName[RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME];
	DLLIST_NODE sNode;
	PVRSRV_RGXDEV_INFO *psDevInfo;
} SERVER_MMU_CONTEXT;

IMG_VOID RGXMMUCacheInvalidate(PVRSRV_DEVICE_NODE *psDeviceNode,
							   IMG_HANDLE hDeviceData,
							   MMU_LEVEL eMMULevel,
							   IMG_BOOL bUnmap)
{
	PVR_UNREFERENCED_PARAMETER(bUnmap);

	switch (eMMULevel)
	{
		case MMU_LEVEL_3:	ui32CacheOpps |= RGXFWIF_MMUCACHEDATA_FLAGS_PC;
							break;
		case MMU_LEVEL_2:	ui32CacheOpps |= RGXFWIF_MMUCACHEDATA_FLAGS_PD;
							break;
		case MMU_LEVEL_1:	ui32CacheOpps |= RGXFWIF_MMUCACHEDATA_FLAGS_PT;
							ui32CacheOpps |= RGXFWIF_MMUCACHEDATA_FLAGS_TLB;
							break;
		default:
							PVR_ASSERT(0);
							break;
	}
}

PVRSRV_ERROR RGXSLCCacheInvalidateRequest(PVRSRV_DEVICE_NODE *psDeviceNode,
									PMR *psPmr)
{
	RGXFWIF_KCCB_CMD sFlushInvalCmd;
	IMG_UINT32 ulPMRFlags;
	IMG_UINT32 ui32DeviceCacheFlags;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(psDeviceNode);

	/* In DEINIT state, we stop scheduling SLC flush commands, because we don't know in what state the firmware is.
	 * Anyway, if we are in DEINIT state, we don't care anymore about FW memory consistency
	 */
	if (psDeviceNode->eDevState != PVRSRV_DEVICE_STATE_DEINIT)
	{

		/* get the PMR's caching flags */
		eError = PMR_Flags(psPmr, &ulPMRFlags);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "RGXSLCCacheInvalidateRequest: Unable to get the caching attributes of PMR %p",psPmr));
		}

		ui32DeviceCacheFlags = DevmemDeviceCacheMode(ulPMRFlags);

		/* Schedule a SLC flush and invalidate if
		 * - the memory is cached.
		 * - we can't get the caching attributes (by precaution).
		 */
		if ((ui32DeviceCacheFlags == PVRSRV_MEMALLOCFLAG_GPU_CACHED) || (eError != PVRSRV_OK))
		{
			/* Schedule the SLC flush command ... */
#if defined(PDUMP)
			PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Submit SLC flush and invalidate");
#endif
			sFlushInvalCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCFLUSHINVAL;
			sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bInval = IMG_TRUE;
			sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bDMContext = IMG_FALSE;
			sFlushInvalCmd.uCmdData.sSLCFlushInvalData.eDM = 0;
			sFlushInvalCmd.uCmdData.sSLCFlushInvalData.psContext.ui32Addr = 0;

			eError = RGXSendCommandWithPowLock(psDeviceNode->pvDevice,
												RGXFWIF_DM_GP,
												&sFlushInvalCmd,
												sizeof(sFlushInvalCmd),
												IMG_TRUE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXSLCCacheInvalidateRequest: Failed to schedule SLC flush command with error (%u)", eError));
			}
			else
			{
				/* Wait for the SLC flush to complete */
				eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, IMG_TRUE);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"RGXSLCCacheInvalidateRequest: SLC flush and invalidate aborted with error (%u)", eError));
				}
			}
		}
	}

	return eError;
}


PVRSRV_ERROR RGXPreKickCacheCommand(PVRSRV_RGXDEV_INFO 	*psDevInfo)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	RGXFWIF_KCCB_CMD sFlushCmd;
	PVRSRV_ERROR eError = PVRSRV_OK;
	RGXFWIF_DM eDMcount = RGXFWIF_DM_MAX;

	if (!ui32CacheOpps)
	{
		goto _PVRSRVPowerLock_Exit;
	}

	sFlushCmd.eCmdType = RGXFWIF_KCCB_CMD_MMUCACHE;
	/* Set which memory context this command is for (all ctxs for now) */
	ui32CacheOpps |= RGXFWIF_MMUCACHEDATA_FLAGS_CTX_ALL;
#if 0
	sFlushCmd.uCmdData.sMMUCacheData.psMemoryContext = ???
#endif

	/* PVRSRVPowerLock guarantees atomicity between commands and global variables consistency.
	 * This is helpful in a scenario with several applications allocating resources. */
	eError = PVRSRVPowerLock();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_WARNING, "RGXPreKickCacheCommand: failed to acquire powerlock (%s)",
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
		PVR_DPF((PVR_DBG_WARNING, "RGXPreKickCacheCommand: failed to transition RGX to ON (%s)",
					PVRSRVGetErrorStringKM(eError)));

		goto _PVRSRVSetDevicePowerStateKM_Exit;
	}

	sFlushCmd.uCmdData.sMMUCacheData.ui32Flags = ui32CacheOpps;
	sFlushCmd.uCmdData.sMMUCacheData.ui32CacheSequenceNum = ++ui32CacheOpSequence;

#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS,
							"Submit MMU flush and invalidate (flags = 0x%08x, cache operation sequence = %u)",
							ui32CacheOpps, ui32CacheOpSequence);
#endif

	ui32CacheOpps = 0;

	/* Schedule MMU cache command */
	do
	{
		eDMcount--;
		eError = RGXSendCommandRaw(psDevInfo, eDMcount, &sFlushCmd, sizeof(RGXFWIF_KCCB_CMD), PDUMP_FLAGS_CONTINUOUS);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXPreKickCacheCommand: Failed to schedule MMU cache command \
									to DM=%d with error (%u)", eDMcount, eError));
			break;
		}
	}
	while(eDMcount > 0);

_PVRSRVSetDevicePowerStateKM_Exit:
	PVRSRVPowerUnlock();

_PVRSRVPowerLock_Exit:
	return eError;
}

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
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
static IMG_UINT32 gui32UnregisteredMemCtxsHead = 0;

/* record a device memory context being unregistered.
 * the list of unregistered contexts can be used to find the PID and process name
 * belonging to a memory context which has been destroyed
 */
static IMG_VOID _RecordUnregisteredMemoryContext(PVRSRV_RGXDEV_INFO *psDevInfo, SERVER_MMU_CONTEXT *psServerMMUContext)
{
	UNREGISTERED_MEMORY_CONTEXT *psRecord;

	OSLockAcquire(psDevInfo->hMMUCtxUnregLock);

	psRecord = &gasUnregisteredMemCtxs[gui32UnregisteredMemCtxsHead];

	gui32UnregisteredMemCtxsHead = (gui32UnregisteredMemCtxsHead + 1)
					& (UNREGISTERED_MEMORY_CONTEXTS_HISTORY_SIZE - 1);

	OSLockRelease(psDevInfo->hMMUCtxUnregLock);

	psRecord->uiPID = psServerMMUContext->uiPID;
	MMU_AcquireBaseAddr(psServerMMUContext->psMMUContext, &psRecord->sPCDevPAddr);
	OSStringNCopy(psRecord->szProcessName, psServerMMUContext->szProcessName, sizeof(psRecord->szProcessName));
	psRecord->szProcessName[sizeof(psRecord->szProcessName) - 1] = '\0';
}

#endif

IMG_VOID RGXUnregisterMemoryContext(IMG_HANDLE hPrivData)
{
	SERVER_MMU_CONTEXT *psServerMMUContext = hPrivData;
	PVRSRV_RGXDEV_INFO *psDevInfo = psServerMMUContext->psDevInfo;

	OSWRLockAcquireWrite(psDevInfo->hMemoryCtxListLock);
	dllist_remove_node(&psServerMMUContext->sNode);
	OSWRLockReleaseWrite(psDevInfo->hMemoryCtxListLock);

#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	_RecordUnregisteredMemoryContext(psDevInfo, psServerMMUContext);
#endif

	/*
	 * Release the page catalogue address acquired in RGXRegisterMemoryContext().
	 */
	MMU_ReleaseBaseAddr(IMG_NULL /* FIXME */);
	
	/*
	 * Free the firmware memory context.
	 */
	DevmemFwFree(psServerMMUContext->psFWMemContextMemDesc);

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
	PVRSRV_RGXDEV_INFO 		*psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_FLAGS_T			uiFWMemContextMemAllocFlags;
	RGXFWIF_FWMEMCONTEXT	*psFWMemContext;
	DEVMEM_MEMDESC			*psFWMemContextMemDesc;
	SERVER_MMU_CONTEXT *psServerMMUContext;

	if (psDevInfo->psKernelMMUCtx == IMG_NULL)
	{
		/*
		 * This must be the creation of the Kernel memory context. Take a copy
		 * of the MMU context for use when programming the BIF.
		 */ 
		psDevInfo->psKernelMMUCtx = psMMUContext;
	}
	else
	{
		psServerMMUContext = OSAllocMem(sizeof(*psServerMMUContext));
		if (psServerMMUContext == IMG_NULL)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto fail_alloc_server_ctx;
		}

		psServerMMUContext->psDevInfo = psDevInfo;

		/*
		 * This FW MemContext is only mapped into kernel for initialisation purposes.
		 * Otherwise this allocation is only used by the FW.
		 * Therefore the GPU cache doesn't need coherency,
		 * and write-combine is suffice on the CPU side (WC buffer will be flushed at any kick)
		 */
		uiFWMemContextMemAllocFlags = PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(META_CACHED) |
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
										PVRSRV_MEMALLOCFLAG_CPU_READABLE |
										PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
										PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE;

		/*
			Allocate device memory for the firmware memory context for the new
			application.
		*/
		PDUMPCOMMENT("Allocate RGX firmware memory context");
		/* FIXME: why cache-consistent? */
		eError = DevmemFwAllocate(psDevInfo,
								sizeof(*psFWMemContext),
								uiFWMemContextMemAllocFlags,
								"FirmwareMemoryContext",
								&psFWMemContextMemDesc);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXRegisterMemoryContext: Failed to allocate firmware memory context (%u)",
					eError));
			goto fail_alloc_fw_ctx;
		}
		
		/*
			Temporarily map the firmware memory context to the kernel.
		*/
		eError = DevmemAcquireCpuVirtAddr(psFWMemContextMemDesc,
										  (IMG_VOID **)&psFWMemContext);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXRegisterMemoryContext: Failed to map firmware memory context (%u)",
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
			PVR_DPF((PVR_DBG_ERROR,"RGXRegisterMemoryContext: Failed to acquire Page Catalogue address (%u)",
					eError));
			DevmemReleaseCpuVirtAddr(psFWMemContextMemDesc);
			goto fail_acquire_base_addr;
		}

		/*
		 * Set default values for the rest of the structure.
		 */
		psFWMemContext->uiPageCatBaseRegID = -1;
		psFWMemContext->uiBreakpointAddr = 0;
		psFWMemContext->uiBPHandlerAddr = 0;
		psFWMemContext->uiBreakpointCtl = 0;

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		IMG_UINT32 ui32OSid = 0, ui32OSidReg = 0;

		MMU_GetOSids(psMMUContext, &ui32OSid, &ui32OSidReg);

		psFWMemContext->ui32OSid = ui32OSidReg;
}
#endif

#if defined(PDUMP)
		{
			IMG_CHAR			aszName[PMR_MAX_MEMSPNAME_SYMB_ADDR_LENGTH_DEFAULT];
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
												   PMR_MAX_MEMSPNAME_SYMB_ADDR_LENGTH_DEFAULT);

			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXRegisterMemoryContext: Failed to generate a Dump Page Catalogue address (%u)",
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
				PVR_DPF((PVR_DBG_ERROR,"RGXRegisterMemoryContext: Failed to acquire Page Catalogue address (%u)",
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
		psServerMMUContext->uiPID = OSGetCurrentProcessID();
		psServerMMUContext->psMMUContext = psMMUContext;
		psServerMMUContext->psFWMemContextMemDesc = psFWMemContextMemDesc;
		if (OSSNPrintf(psServerMMUContext->szProcessName,
						RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME,
						"%s",
						OSGetCurrentProcessName()) == RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME)
		{
			psServerMMUContext->szProcessName[RGXMEM_SERVER_MMU_CONTEXT_MAX_NAME-1] = '\0';
		}

		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "New memory context: Process Name: %s PID: %u (0x%08X)",
										psServerMMUContext->szProcessName,
										psServerMMUContext->uiPID,
										psServerMMUContext->uiPID);

		OSWRLockAcquireWrite(psDevInfo->hMemoryCtxListLock);
		dllist_add_to_tail(&psDevInfo->sMemoryContextList, &psServerMMUContext->sNode);
		OSWRLockReleaseWrite(psDevInfo->hMemoryCtxListLock);

		MMU_SetDeviceData(psMMUContext, psFWMemContextMemDesc);
		*hPrivData = psServerMMUContext;
	}
			
	return PVRSRV_OK;

#if defined(PDUMP)
fail_pdump_cat_base:
fail_pdump_cat_base_addr:
	MMU_ReleaseBaseAddr(IMG_NULL);
#endif
fail_acquire_base_addr:
	/* Done before jumping to the fail point as the release is done before exit */
fail_acquire_cpu_addr:
	DevmemFwFree(psServerMMUContext->psFWMemContextMemDesc);
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

typedef struct _RGX_FAULT_DATA_ {
	IMG_DEV_VIRTADDR *psDevVAddr;
	IMG_DEV_PHYADDR *psDevPAddr;
} RGX_FAULT_DATA;

static IMG_BOOL _RGXCheckFaultAddress(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	SERVER_MMU_CONTEXT *psServerMMUContext = IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);
	RGX_FAULT_DATA *psFaultData = (RGX_FAULT_DATA *) pvCallbackData;
	IMG_DEV_PHYADDR sPCDevPAddr;
	
	if (MMU_AcquireBaseAddr(psServerMMUContext->psMMUContext, &sPCDevPAddr) != PVRSRV_OK)
	{
		PVR_LOG(("Failed to get PC address for memory context"));
		return IMG_TRUE;
	}

	if (psFaultData->psDevPAddr->uiAddr == sPCDevPAddr.uiAddr)
	{
		PVR_LOG(("Found memory context (PID = %d, %s)",
				 psServerMMUContext->uiPID,
				 psServerMMUContext->szProcessName));

		MMU_CheckFaultAddress(psServerMMUContext->psMMUContext, psFaultData->psDevVAddr);
		return IMG_FALSE;
	}
	return IMG_TRUE;
}

IMG_VOID RGXCheckFaultAddress(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_VIRTADDR *psDevVAddr, IMG_DEV_PHYADDR *psDevPAddr)
{
	RGX_FAULT_DATA sFaultData;
	IMG_DEV_PHYADDR sPCDevPAddr;

	sFaultData.psDevVAddr = psDevVAddr;
	sFaultData.psDevPAddr = psDevPAddr;

	OSWRLockAcquireRead(psDevInfo->hMemoryCtxListLock);

	dllist_foreach_node(&psDevInfo->sMemoryContextList,
						_RGXCheckFaultAddress,
						&sFaultData);

	/* Lastly check for fault in the kernel allocated memory */
	if (MMU_AcquireBaseAddr(psDevInfo->psKernelMMUCtx, &sPCDevPAddr) != PVRSRV_OK)
	{
		PVR_LOG(("Failed to get PC address for kernel memory context"));
	}

	if (sFaultData.psDevPAddr->uiAddr == sPCDevPAddr.uiAddr)
	{
		MMU_CheckFaultAddress(psDevInfo->psKernelMMUCtx, psDevVAddr);
	}

	OSWRLockReleaseRead(psDevInfo->hMemoryCtxListLock);
}

/* input for query to find the MMU context corresponding to a
 * page catalogue address
 */
typedef struct _RGX_FIND_MMU_CONTEXT_
{
	IMG_DEV_PHYADDR sPCAddress;
	SERVER_MMU_CONTEXT *psServerMMUContext;
	MMU_CONTEXT *psMMUContext;
} RGX_FIND_MMU_CONTEXT;

static IMG_BOOL _RGXFindMMUContext(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	SERVER_MMU_CONTEXT *psServerMMUContext = IMG_CONTAINER_OF(psNode, SERVER_MMU_CONTEXT, sNode);
	RGX_FIND_MMU_CONTEXT *psData = pvCallbackData;
	IMG_DEV_PHYADDR sPCDevPAddr;

	if (MMU_AcquireBaseAddr(psServerMMUContext->psMMUContext, &sPCDevPAddr) != PVRSRV_OK)
	{
		PVR_LOG(("Failed to get PC address for memory context"));
		return IMG_TRUE;
	}

	if (psData->sPCAddress.uiAddr == sPCDevPAddr.uiAddr)
	{
		psData->psServerMMUContext = psServerMMUContext;

		return IMG_FALSE;
	}
	return IMG_TRUE;
}

/* given the physical address of a page catalogue, searches for a corresponding
 * MMU context and if found, provides the caller details of the process.
 * Returns IMG_TRUE if a process is found.
 */
IMG_BOOL RGXPCAddrToProcessInfo(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_DEV_PHYADDR sPCAddress,
								RGXMEM_PROCESS_INFO *psInfo)
{
	RGX_FIND_MMU_CONTEXT sData;
	IMG_BOOL bRet = IMG_FALSE;

	sData.sPCAddress = sPCAddress;
	sData.psServerMMUContext = IMG_NULL;

	dllist_foreach_node(&psDevInfo->sMemoryContextList, _RGXFindMMUContext, &sData);

	if(sData.psServerMMUContext != IMG_NULL)
	{
		psInfo->uiPID = sData.psServerMMUContext->uiPID;
		OSStringNCopy(psInfo->szProcessName, sData.psServerMMUContext->szProcessName, sizeof(psInfo->szProcessName));
		psInfo->szProcessName[sizeof(psInfo->szProcessName) - 1] = '\0';
		psInfo->bUnregistered = IMG_FALSE;
		bRet = IMG_TRUE;
	}
#if defined(SUPPORT_PAGE_FAULT_DEBUG)
	else
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

			if(psRecord->sPCDevPAddr.uiAddr == sPCAddress.uiAddr)
			{
				psInfo->uiPID = psRecord->uiPID;
				OSStringNCopy(psInfo->szProcessName, psRecord->szProcessName, sizeof(psInfo->szProcessName)-1);
				psInfo->szProcessName[sizeof(psInfo->szProcessName) - 1] = '\0';
				psInfo->bUnregistered = IMG_TRUE;
				bRet = IMG_TRUE;
				break;
			}
		} while(i != gui32UnregisteredMemCtxsHead);

		OSLockRelease(psDevInfo->hMMUCtxUnregLock);

	}
#endif
	return bRet;
}

/******************************************************************************
 End of file (rgxmem.c)
******************************************************************************/
