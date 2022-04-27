/*************************************************************************/ /*!
@File           rgxkicksync.c
@Title          Server side of the sync only kick API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description
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

#include "img_defs.h"
#include "rgxkicksync.h"

#include "rgxdevice.h"
#include "rgxmem.h"
#include "rgxfwutils.h"
#include "allocmem.h"
#include "sync.h"
#include "rgxhwperf.h"
#include "ospvr_gputrace.h"

#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"

/* Enable this to dump the compiled list of UFOs prior to kick call */
#define ENABLE_KICKSYNC_UFO_DUMP	0

//#define KICKSYNC_CHECKPOINT_DEBUG 1

#if defined(KICKSYNC_CHECKPOINT_DEBUG)
#define CHKPT_DBG(X) PVR_DPF(X)
#else
#define CHKPT_DBG(X)
#endif

struct _RGX_SERVER_KICKSYNC_CONTEXT_
{
	PVRSRV_DEVICE_NODE        * psDeviceNode;
	RGX_SERVER_COMMON_CONTEXT * psServerCommonContext;
	DLLIST_NODE                 sListNode;
	SYNC_ADDR_LIST              sSyncAddrListFence;
	SYNC_ADDR_LIST              sSyncAddrListUpdate;
	POS_LOCK                    hLock;
};


PVRSRV_ERROR PVRSRVRGXCreateKickSyncContextKM(CONNECTION_DATA              *psConnection,
                                              PVRSRV_DEVICE_NODE           *psDeviceNode,
                                              IMG_HANDLE                    hMemCtxPrivData,
                                              IMG_UINT32                    ui32PackedCCBSizeU88,
                                              IMG_UINT32                    ui32ContextFlags,
                                              RGX_SERVER_KICKSYNC_CONTEXT **ppsKickSyncContext)
{
	PVRSRV_RGXDEV_INFO          * psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC              * psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext;
	RGX_COMMON_CONTEXT_INFO      sInfo;
	PVRSRV_ERROR                 eError;
	IMG_UINT32                   ui32CCBAllocSizeLog2, ui32CCBMaxAllocSizeLog2;

	memset(&sInfo, 0, sizeof(sInfo));

	/* Prepare cleanup struct */
	* ppsKickSyncContext = NULL;
	psKickSyncContext = OSAllocZMem(sizeof(*psKickSyncContext));
	if (psKickSyncContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = OSLockCreate(&psKickSyncContext->hLock);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create lock (%s)",
									__func__,
									PVRSRVGetErrorString(eError)));
		goto err_lockcreate;
	}

	psKickSyncContext->psDeviceNode = psDeviceNode;

	ui32CCBAllocSizeLog2 = U32toU8_Unpack1(ui32PackedCCBSizeU88);
	ui32CCBMaxAllocSizeLog2 = U32toU8_Unpack2(ui32PackedCCBSizeU88);
	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_KICKSYNC,
									 RGXFWIF_DM_GP,
									 hMemCtxPrivData,
									 NULL,
									 0,
									 psFWMemContextMemDesc,
									 NULL,
									 ui32CCBAllocSizeLog2 ? ui32CCBAllocSizeLog2 : RGX_KICKSYNC_CCB_SIZE_LOG2,
									 ui32CCBMaxAllocSizeLog2 ? ui32CCBMaxAllocSizeLog2 : RGX_KICKSYNC_CCB_MAX_SIZE_LOG2,
									 ui32ContextFlags,
									 0, /* priority */
									 0, /* max deadline MS */
									 0, /* robustness address */
									 & sInfo,
									 & psKickSyncContext->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextalloc;
	}

	OSWRLockAcquireWrite(psDevInfo->hKickSyncCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sKickSyncCtxtListHead), &(psKickSyncContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hKickSyncCtxListLock);

	SyncAddrListInit(&psKickSyncContext->sSyncAddrListFence);
	SyncAddrListInit(&psKickSyncContext->sSyncAddrListUpdate);

	* ppsKickSyncContext = psKickSyncContext;
	return PVRSRV_OK;

fail_contextalloc:
	OSLockDestroy(psKickSyncContext->hLock);
err_lockcreate:
	OSFreeMem(psKickSyncContext);
	return eError;
}


PVRSRV_ERROR PVRSRVRGXDestroyKickSyncContextKM(RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext)
{
	PVRSRV_RGXDEV_INFO * psDevInfo = psKickSyncContext->psDeviceNode->pvDevice;
	PVRSRV_ERROR         eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psKickSyncContext->psDeviceNode,
	                                          psKickSyncContext->psServerCommonContext,
	                                          RGXFWIF_DM_GP,
	                                          PDUMP_FLAGS_NONE);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */

	OSWRLockAcquireWrite(psDevInfo->hKickSyncCtxListLock);
	dllist_remove_node(&(psKickSyncContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hKickSyncCtxListLock);

	FWCommonContextFree(psKickSyncContext->psServerCommonContext);

	SyncAddrListDeinit(&psKickSyncContext->sSyncAddrListFence);
	SyncAddrListDeinit(&psKickSyncContext->sSyncAddrListUpdate);

	OSLockDestroy(psKickSyncContext->hLock);

	OSFreeMem(psKickSyncContext);

	return PVRSRV_OK;
}

PVRSRV_ERROR PVRSRVRGXSetKickSyncContextPropertyKM(RGX_SERVER_KICKSYNC_CONTEXT *psKickSyncContext,
                                                   RGX_CONTEXT_PROPERTY eContextProperty,
                                                   IMG_UINT64 ui64Input,
                                                   IMG_UINT64 *pui64Output)
{
	PVRSRV_ERROR eError;

	switch (eContextProperty)
	{
		case RGX_CONTEXT_PROPERTY_FLAGS:
		{
			OSLockAcquire(psKickSyncContext->hLock);
			eError = FWCommonContextSetFlags(psKickSyncContext->psServerCommonContext,
			                                 (IMG_UINT32)ui64Input);

			OSLockRelease(psKickSyncContext->hLock);
			PVR_LOG_IF_ERROR(eError, "FWCommonContextSetFlags");
			break;
		}

		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_ERROR_NOT_SUPPORTED - asked to set unknown property (%d)", __func__, eContextProperty));
			eError = PVRSRV_ERROR_NOT_SUPPORTED;
		}
	}

	return eError;
}

void DumpKickSyncCtxtsInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                           void *pvDumpDebugFile,
                           IMG_UINT32 ui32VerbLevel)
{
	DLLIST_NODE *psNode, *psNext;
	OSWRLockAcquireRead(psDevInfo->hKickSyncCtxListLock);
	dllist_foreach_node(&psDevInfo->sKickSyncCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_KICKSYNC_CONTEXT *psCurrentServerKickSyncCtx =
				IMG_CONTAINER_OF(psNode, RGX_SERVER_KICKSYNC_CONTEXT, sListNode);

		if (NULL != psCurrentServerKickSyncCtx->psServerCommonContext)
		{
			DumpFWCommonContextInfo(psCurrentServerKickSyncCtx->psServerCommonContext,
			                        pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
		}
	}
	OSWRLockReleaseRead(psDevInfo->hKickSyncCtxListLock);
}

IMG_UINT32 CheckForStalledClientKickSyncCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_UINT32 ui32ContextBitMask = 0;

	OSWRLockAcquireRead(psDevInfo->hKickSyncCtxListLock);

	dllist_foreach_node(&psDevInfo->sKickSyncCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_KICKSYNC_CONTEXT *psCurrentServerKickSyncCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_KICKSYNC_CONTEXT, sListNode);

		if (NULL != psCurrentServerKickSyncCtx->psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerKickSyncCtx->psServerCommonContext, RGX_KICK_TYPE_DM_GP) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_GP;
			}
		}
	}

	OSWRLockReleaseRead(psDevInfo->hKickSyncCtxListLock);
	return ui32ContextBitMask;
}

PVRSRV_ERROR PVRSRVRGXKickSyncKM(RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext,
                                 IMG_UINT32                    ui32ClientCacheOpSeqNum,
                                 IMG_UINT32                    ui32ClientUpdateCount,
                                 SYNC_PRIMITIVE_BLOCK       ** pauiClientUpdateUFODevVarBlock,
                                 IMG_UINT32                  * paui32ClientUpdateOffset,
                                 IMG_UINT32                  * paui32ClientUpdateValue,
                                 PVRSRV_FENCE                  iCheckFence,
                                 PVRSRV_TIMELINE               iUpdateTimeline,
                                 PVRSRV_FENCE                * piUpdateFence,
                                 IMG_CHAR                      szUpdateFenceName[PVRSRV_SYNC_NAME_LENGTH],
                                 IMG_UINT32                    ui32ExtJobRef)
{
	RGXFWIF_KCCB_CMD         sKickSyncKCCBCmd;
	RGX_CCB_CMD_HELPER_DATA  asCmdHelperData[1];
	PVRSRV_ERROR             eError;
	PVRSRV_ERROR             eError2;
	IMG_BOOL                 bCCBStateOpen = IMG_FALSE;
	PRGXFWIF_UFO_ADDR        *pauiClientFenceUFOAddress = NULL;
	PRGXFWIF_UFO_ADDR        *pauiClientUpdateUFOAddress = NULL;
	IMG_UINT32               ui32ClientFenceCount = 0;
	IMG_UINT32               *paui32ClientFenceValue = NULL;
	PVRSRV_FENCE             iUpdateFence = PVRSRV_NO_FENCE;
	IMG_UINT32               ui32FWCtx = FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext).ui32Addr;
	PVRSRV_RGXDEV_INFO       *psDevInfo = FWCommonContextGetRGXDevInfo(psKickSyncContext->psServerCommonContext);
	RGX_CLIENT_CCB           *psClientCCB = FWCommonContextGetClientCCB(psKickSyncContext->psServerCommonContext);
	IMG_UINT32               ui32IntJobRef = OSAtomicIncrement(&psDevInfo->iCCBSubmissionOrdinal);
	IMG_UINT64               uiCheckFenceUID = 0;
	IMG_UINT64               uiUpdateFenceUID = 0;
	PSYNC_CHECKPOINT psUpdateSyncCheckpoint = NULL;
	PSYNC_CHECKPOINT *apsFenceSyncCheckpoints = NULL;
	IMG_UINT32 ui32FenceSyncCheckpointCount = 0;
	IMG_UINT32 ui32FenceTimelineUpdateValue = 0;
	IMG_UINT32 *pui32IntAllocatedUpdateValues = NULL;
	PVRSRV_CLIENT_SYNC_PRIM *psFenceTimelineUpdateSync = NULL;
	void *pvUpdateFenceFinaliseData = NULL;

	/* Ensure we haven't been given a null ptr to
	 * update values if we have been told we
	 * have dev var updates
	 */
	if (ui32ClientUpdateCount > 0)
	{
		PVR_LOG_RETURN_IF_FALSE(paui32ClientUpdateValue != NULL,
		                        "paui32ClientUpdateValue NULL but ui32ClientUpdateCount > 0",
		                        PVRSRV_ERROR_INVALID_PARAMS);
	}

	OSLockAcquire(psKickSyncContext->hLock);
	eError = SyncAddrListPopulate(&psKickSyncContext->sSyncAddrListUpdate,
							ui32ClientUpdateCount,
							pauiClientUpdateUFODevVarBlock,
							paui32ClientUpdateOffset);

	if (eError != PVRSRV_OK)
	{
		goto fail_syncaddrlist;
	}

	if (ui32ClientUpdateCount > 0)
	{
		pauiClientUpdateUFOAddress = psKickSyncContext->sSyncAddrListUpdate.pasFWAddrs;
	}
	/* Ensure the string is null-terminated (Required for safety) */
	szUpdateFenceName[31] = '\0';

	/* This will never be true if called from the bridge since piUpdateFence will always be valid */
	if (iUpdateTimeline >= 0 && !piUpdateFence)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto out_unlock;
	}

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: calling SyncCheckpointResolveFence (iCheckFence=%d), "
			   "psKickSyncContext->psDeviceNode->hSyncCheckpointContext=<%p>...",
			   __func__, iCheckFence,
			   (void*)psKickSyncContext->psDeviceNode->hSyncCheckpointContext));
	/* Resolve the sync checkpoints that make up the input fence */
	eError = SyncCheckpointResolveFence(psKickSyncContext->psDeviceNode->hSyncCheckpointContext,
	                                    iCheckFence,
	                                    &ui32FenceSyncCheckpointCount,
	                                    &apsFenceSyncCheckpoints,
	                                    &uiCheckFenceUID,
	                                    PDUMP_FLAGS_NONE);
	if (eError != PVRSRV_OK)
	{
		goto fail_resolve_fence;
	}

	/* Create the output fence (if required) */
	if (iUpdateTimeline != PVRSRV_NO_TIMELINE)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s: calling SyncCheckpointCreateFence (iUpdateTimeline=%d)...",
				   __func__, iUpdateTimeline));
		eError = SyncCheckpointCreateFence(psKickSyncContext->psDeviceNode,
		                                   szUpdateFenceName,
		                                   iUpdateTimeline,
		                                   psKickSyncContext->psDeviceNode->hSyncCheckpointContext,
		                                   &iUpdateFence,
		                                   &uiUpdateFenceUID,
		                                   &pvUpdateFenceFinaliseData,
		                                   &psUpdateSyncCheckpoint,
		                                   (void*)&psFenceTimelineUpdateSync,
		                                   &ui32FenceTimelineUpdateValue,
		                                   PDUMP_FLAGS_NONE);
		if (eError != PVRSRV_OK)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: ...returned error (%d)",
					   __func__, eError));
			goto fail_create_output_fence;
		}
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s: ...returned from SyncCheckpointCreateFence "
				   "(iUpdateFence=%d, psFenceTimelineUpdateSync=<%p>, "
				   "ui32FenceTimelineUpdateValue=%u)",
				   __func__, iUpdateFence, psFenceTimelineUpdateSync,
				   ui32FenceTimelineUpdateValue));

		/* Append the sync prim update for the timeline (if required) */
		if (psFenceTimelineUpdateSync)
		{
			IMG_UINT32 *pui32TimelineUpdateWp = NULL;

			/* Allocate memory to hold the list of update values (including our timeline update) */
			pui32IntAllocatedUpdateValues = OSAllocMem(sizeof(*paui32ClientUpdateValue) * (ui32ClientUpdateCount+1));
			if (!pui32IntAllocatedUpdateValues)
			{
				/* Failed to allocate memory */
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_alloc_update_values_mem;
			}
			OSCachedMemSet(pui32IntAllocatedUpdateValues, 0xbb, sizeof(*pui32IntAllocatedUpdateValues) * (ui32ClientUpdateCount+1));
			/* Copy the update values into the new memory, then append our timeline update value */
			OSCachedMemCopy(pui32IntAllocatedUpdateValues, paui32ClientUpdateValue, sizeof(*pui32IntAllocatedUpdateValues) * ui32ClientUpdateCount);
			/* Now set the additional update value */
			pui32TimelineUpdateWp = pui32IntAllocatedUpdateValues + ui32ClientUpdateCount;
			*pui32TimelineUpdateWp = ui32FenceTimelineUpdateValue;
			ui32ClientUpdateCount++;
			/* Now make sure paui32ClientUpdateValue points to pui32IntAllocatedUpdateValues */
			paui32ClientUpdateValue = pui32IntAllocatedUpdateValues;
#if defined(KICKSYNC_CHECKPOINT_DEBUG)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pui32IntAllocatedUpdateValues;

				for (iii=0; iii<ui32ClientUpdateCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x",
							   __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
			/* Now append the timeline sync prim addr to the kicksync context update list */
			SyncAddrListAppendSyncPrim(&psKickSyncContext->sSyncAddrListUpdate,
			                           psFenceTimelineUpdateSync);
		}
	}

	/* Reset number of fence syncs in kicksync context fence list to 0 */
	SyncAddrListPopulate(&psKickSyncContext->sSyncAddrListFence,
	                     0, NULL, NULL);

	if (ui32FenceSyncCheckpointCount > 0)
	{
		/* Append the checks (from input fence) */
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Append %d sync checkpoints to KickSync Fence "
				   "(&psKickSyncContext->sSyncAddrListFence=<%p>)...",
				   __func__, ui32FenceSyncCheckpointCount,
				   (void*)&psKickSyncContext->sSyncAddrListFence));
		SyncAddrListAppendCheckpoints(&psKickSyncContext->sSyncAddrListFence,
									  ui32FenceSyncCheckpointCount,
									  apsFenceSyncCheckpoints);
		if (!pauiClientFenceUFOAddress)
		{
			pauiClientFenceUFOAddress = psKickSyncContext->sSyncAddrListFence.pasFWAddrs;
		}
		ui32ClientFenceCount += ui32FenceSyncCheckpointCount;
#if defined(KICKSYNC_CHECKPOINT_DEBUG)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiClientFenceUFOAddress;

				for (iii=0; iii<ui32ClientFenceCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s: pauiClientFenceUFOAddress[%d](<%p>) = 0x%x",
							   __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
	}

	if (psUpdateSyncCheckpoint)
	{
		PVRSRV_ERROR eErr;

		/* Append the update (from output fence) */
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Append 1 sync checkpoint to KickSync Update "
				   "(&psKickSyncContext->sSyncAddrListUpdate=<%p>)...",
				   __func__, (void*)&psKickSyncContext->sSyncAddrListUpdate));
		eErr = SyncAddrListAppendCheckpoints(&psKickSyncContext->sSyncAddrListUpdate,
											 1,
											 &psUpdateSyncCheckpoint);
		if (eErr != PVRSRV_OK)
		{
			CHKPT_DBG((PVR_DBG_ERROR,
					   "%s:  ...done. SyncAddrListAppendCheckpoints() returned error (%d)",
					   __func__, eErr));
		}
		else
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s:  ...done.", __func__));
		}
		if (!pauiClientUpdateUFOAddress)
		{
			pauiClientUpdateUFOAddress = psKickSyncContext->sSyncAddrListUpdate.pasFWAddrs;
		}
		ui32ClientUpdateCount++;
#if defined(KICKSYNC_CHECKPOINT_DEBUG)
		{
			IMG_UINT32 iii;
			IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiClientUpdateUFOAddress;

			for (iii=0; iii<ui32ClientUpdateCount; iii++)
			{
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s: pauiClientUpdateUFOAddress[%d](<%p>) = 0x%x",
						   __func__, iii, (void*)pui32Tmp, *pui32Tmp));
				pui32Tmp++;
			}
		}
#endif
	}

#if (ENABLE_KICKSYNC_UFO_DUMP == 1)
		PVR_DPF((PVR_DBG_ERROR, "%s: dumping KICKSYNC fence/updates syncs...",
				 __func__));
		{
			IMG_UINT32 ii;
			PRGXFWIF_UFO_ADDR *psTmpIntFenceUFOAddress = pauiClientFenceUFOAddress;
			IMG_UINT32 *pui32TmpIntFenceValue = paui32ClientFenceValue;
			PRGXFWIF_UFO_ADDR *psTmpIntUpdateUFOAddress = pauiClientUpdateUFOAddress;
			IMG_UINT32 *pui32TmpIntUpdateValue = paui32ClientUpdateValue;

			/* Dump Fence syncs and Update syncs */
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Prepared %d KickSync fence syncs "
					 "(&psKickSyncContext->sSyncAddrListFence=<%p>, "
					 "pauiClientFenceUFOAddress=<%p>):",
					 __func__, ui32ClientFenceCount,
					 (void*)&psKickSyncContext->sSyncAddrListFence,
					 (void*)pauiClientFenceUFOAddress));
			for (ii=0; ii<ui32ClientFenceCount; ii++)
			{
				if (psTmpIntFenceUFOAddress->ui32Addr & 0x1)
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "%s:   %d/%d<%p>. FWAddr=0x%x, "
							 "CheckValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
							 __func__, ii + 1, ui32ClientFenceCount,
							 (void*)psTmpIntFenceUFOAddress,
							 psTmpIntFenceUFOAddress->ui32Addr));
				}
				else
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "%s:   %d/%d<%p>. FWAddr=0x%x, CheckValue=%d(0x%x)",
							 __func__, ii + 1, ui32ClientFenceCount,
							 (void*)psTmpIntFenceUFOAddress,
							 psTmpIntFenceUFOAddress->ui32Addr,
							 *pui32TmpIntFenceValue,
							 *pui32TmpIntFenceValue));
					pui32TmpIntFenceValue++;
				}
				psTmpIntFenceUFOAddress++;
			}
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Prepared %d KickSync update syncs "
					 "(&psKickSyncContext->sSyncAddrListUpdate=<%p>, "
					 "pauiClientUpdateUFOAddress=<%p>):",
					 __func__, ui32ClientUpdateCount,
					 (void*)&psKickSyncContext->sSyncAddrListUpdate,
					 (void*)pauiClientUpdateUFOAddress));
			for (ii=0; ii<ui32ClientUpdateCount; ii++)
			{
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s:  Line %d, psTmpIntUpdateUFOAddress=<%p>",
						   __func__, __LINE__,
						   (void*)psTmpIntUpdateUFOAddress));
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s:  Line %d, pui32TmpIntUpdateValue=<%p>",
						   __func__, __LINE__,
						   (void*)pui32TmpIntUpdateValue));
				if (psTmpIntUpdateUFOAddress->ui32Addr & 0x1)
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "%s:   %d/%d<%p>. FWAddr=0x%x, "
							 "UpdateValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
							 __func__, ii + 1, ui32ClientUpdateCount,
							 (void*)psTmpIntUpdateUFOAddress,
							 psTmpIntUpdateUFOAddress->ui32Addr));
				}
				else
				{
					PVR_DPF((PVR_DBG_ERROR,
							 "%s:   %d/%d<%p>. FWAddr=0x%x, UpdateValue=%d",
							 __func__, ii + 1, ui32ClientUpdateCount,
							 (void*)psTmpIntUpdateUFOAddress,
							 psTmpIntUpdateUFOAddress->ui32Addr,
							 *pui32TmpIntUpdateValue));
					pui32TmpIntUpdateValue++;
				}
				psTmpIntUpdateUFOAddress++;
			}
		}
#endif

	RGXCmdHelperInitCmdCCB(psClientCCB,
	                       0, /* empty ui64FBSCEntryMask */
	                       ui32ClientFenceCount,
	                       pauiClientFenceUFOAddress,
	                       paui32ClientFenceValue,
	                       ui32ClientUpdateCount,
	                       pauiClientUpdateUFOAddress,
	                       paui32ClientUpdateValue,
	                       0,
	                       NULL,
	                       NULL,
	                       NULL,
	                       NULL,
	                       RGXFWIF_CCB_CMD_TYPE_NULL,
	                       ui32ExtJobRef,
	                       ui32IntJobRef,
	                       PDUMP_FLAGS_NONE,
	                       NULL,
	                       "KickSync",
	                       bCCBStateOpen,
	                       asCmdHelperData);

	eError = RGXCmdHelperAcquireCmdCCB(ARRAY_SIZE(asCmdHelperData), asCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto fail_cmdaquire;
	}

	/*
	 *  We should reserve space in the kernel CCB here and fill in the command
	 *  directly.
	 *  This is so if there isn't space in the kernel CCB we can return with
	 *  retry back to services client before we take any operations
	 */

	/*
	 * We might only be kicking for flush out a padding packet so only submit
	 * the command if the create was successful
	 */
	if (eError == PVRSRV_OK)
	{
		/*
		 * All the required resources are ready at this point, we can't fail so
		 * take the required server sync operations and commit all the resources
		 */
		RGXCmdHelperReleaseCmdCCB(1,
		                          asCmdHelperData,
		                          "KickSync",
		                          FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext).ui32Addr);
	}

	/* Construct the kernel kicksync CCB command. */
	sKickSyncKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext);
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(psClientCCB);
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(psClientCCB);

	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;

	/*
	 * Submit the kicksync command to the firmware.
	 */
	RGXSRV_HWPERF_ENQ(psKickSyncContext,
	                  OSGetCurrentClientProcessIDKM(),
	                  ui32FWCtx,
	                  ui32ExtJobRef,
	                  ui32IntJobRef,
	                  RGX_HWPERF_KICK_TYPE_SYNC,
	                  iCheckFence,
	                  iUpdateFence,
	                  iUpdateTimeline,
	                  uiCheckFenceUID,
	                  uiUpdateFenceUID,
	                  NO_DEADLINE,
	                  NO_CYCEST);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError2 = RGXScheduleCommand(psKickSyncContext->psDeviceNode->pvDevice,
		                             RGXFWIF_DM_GP,
		                             & sKickSyncKCCBCmd,
		                             ui32ClientCacheOpSeqNum,
		                             PDUMP_FLAGS_NONE);
		if (eError2 != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	PVRGpuTraceEnqueueEvent(psKickSyncContext->psDeviceNode->pvDevice,
	                        ui32FWCtx, ui32ExtJobRef, ui32IntJobRef,
	                        RGX_HWPERF_KICK_TYPE_SYNC);

	if (eError2 != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "PVRSRVRGXKickSync failed to schedule kernel CCB command. (0x%x)",
		         eError));
	}

	/*
	 * Now check eError (which may have returned an error from our earlier call
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (eError != PVRSRV_OK )
	{
		goto fail_cmdaquire;
	}

#if defined(NO_HARDWARE)
	/* If NO_HARDWARE, signal the output fence's sync checkpoint and sync prim */
	if (psUpdateSyncCheckpoint)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Signalling NOHW sync checkpoint<%p>, ID:%d, FwAddr=0x%x",
				   __func__, (void*)psUpdateSyncCheckpoint,
				   SyncCheckpointGetId(psUpdateSyncCheckpoint),
				   SyncCheckpointGetFirmwareAddr(psUpdateSyncCheckpoint)));
		SyncCheckpointSignalNoHW(psUpdateSyncCheckpoint);
	}
	if (psFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Updating NOHW sync prim<%p> to %d",
				   __func__, (void*)psFenceTimelineUpdateSync,
				   ui32FenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(psFenceTimelineUpdateSync, ui32FenceTimelineUpdateValue);
	}
	SyncCheckpointNoHWUpdateTimelines(NULL);
#endif
	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence */
	SyncAddrListDeRefCheckpoints(ui32FenceSyncCheckpointCount,
								 apsFenceSyncCheckpoints);
	/* Free the memory that was allocated for the sync checkpoint list returned by ResolveFence() */
	if (apsFenceSyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFenceSyncCheckpoints);
	}
	/* Free memory allocated to hold the internal list of update values */
	if (pui32IntAllocatedUpdateValues)
	{
		OSFreeMem(pui32IntAllocatedUpdateValues);
		pui32IntAllocatedUpdateValues = NULL;
	}

	*piUpdateFence = iUpdateFence;
	if (pvUpdateFenceFinaliseData && (iUpdateFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psKickSyncContext->psDeviceNode, iUpdateFence,
									pvUpdateFenceFinaliseData,
									psUpdateSyncCheckpoint, szUpdateFenceName);
	}

	OSLockRelease(psKickSyncContext->hLock);
	return PVRSRV_OK;

fail_cmdaquire:
	SyncAddrListRollbackCheckpoints(psKickSyncContext->psDeviceNode, &psKickSyncContext->sSyncAddrListFence);
	SyncAddrListRollbackCheckpoints(psKickSyncContext->psDeviceNode, &psKickSyncContext->sSyncAddrListUpdate);
	if (iUpdateFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(iUpdateFence, pvUpdateFenceFinaliseData);
	}

	/* Free memory allocated to hold update values */
	if (pui32IntAllocatedUpdateValues)
	{
		OSFreeMem(pui32IntAllocatedUpdateValues);
	}
fail_alloc_update_values_mem:
fail_create_output_fence:
	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence */
	SyncAddrListDeRefCheckpoints(ui32FenceSyncCheckpointCount,
								 apsFenceSyncCheckpoints);
	/* Free memory allocated to hold the resolved fence's checkpoints */
	if (apsFenceSyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFenceSyncCheckpoints);
	}
fail_resolve_fence:
fail_syncaddrlist:
out_unlock:
	OSLockRelease(psKickSyncContext->hLock);
	return eError;
}

/**************************************************************************//**
 End of file (rgxkicksync.c)
******************************************************************************/
