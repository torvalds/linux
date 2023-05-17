/*************************************************************************/ /*!
@File
@Title          RGX Ray routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Ray routines
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
#include "srvkm.h"
#include "pdump_km.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxray.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "osfunc.h"
#include "rgxccb.h"
#include "rgxhwperf.h"
#include "ospvr_gputrace.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"
#include "rgx_memallocflags.h"

#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"

#include "rgxtimerquery.h"

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
#include "rgxworkest.h"
#include "rgxworkest_ray.h"
#endif

/* Enable this to dump the compiled list of UFOs prior to kick call */
#define ENABLE_CMP_UFO_DUMP	0

//#define CMP_CHECKPOINT_DEBUG 1
//#define CMP_CHECKPOINT_DEBUG 1

#if defined(CMP_CHECKPOINT_DEBUG)
#define CHKPT_DBG(X) PVR_DPF(X)
#else
#define CHKPT_DBG(X)
#endif

struct _RGX_SERVER_RAY_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWRayContextMemDesc;
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	POS_LOCK					hLock;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	WORKEST_HOST_DATA			sWorkEstData;
#endif
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	SYNC_ADDR_LIST				sSyncAddrListFence;
	SYNC_ADDR_LIST				sSyncAddrListUpdate;
};

PVRSRV_ERROR PVRSRVRGXCreateRayContextKM(CONNECTION_DATA			*psConnection,
											 PVRSRV_DEVICE_NODE		*psDeviceNode,
											 IMG_INT32				i32Priority,
											 IMG_HANDLE				hMemCtxPrivData,
											 IMG_UINT32				ui32ContextFlags,
											 IMG_UINT32				ui32StaticRayContextStateSize,
											 IMG_PBYTE				pStaticRayContextState,
											 IMG_UINT64				ui64RobustnessAddress,
											 IMG_UINT32				ui32MaxDeadlineMS,
											 RGX_SERVER_RAY_CONTEXT	**ppsRayContext)
{
	DEVMEM_MEMDESC				*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_RAY_CONTEXT		*psRayContext;
	RGXFWIF_FWRAYCONTEXT		*psFWRayContext;
	RGX_COMMON_CONTEXT_INFO		sInfo = {NULL};
	PVRSRV_ERROR				eError;

	*ppsRayContext = NULL;

	psRayContext = OSAllocZMem(sizeof(*psRayContext));
	if (psRayContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRayContext->psDeviceNode = psDeviceNode;
	/*
		Create the FW ray context, this has the RDM common
		context embedded within it
	 */
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(RGXFWIF_FWRAYCONTEXT),
			RGX_FWCOMCTX_ALLOCFLAGS,
			"FwRayContext",
			&psRayContext->psFWRayContextMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_fwraycontext;
	}

	eError = OSLockCreate(&psRayContext->hLock);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Failed to create lock (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto fail_createlock;
	}

	PDUMPCOMMENT(psDeviceNode, "Allocate RGX firmware ray context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_COMPUTECTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FwRayContextState",
							  &psRayContext->psContextStateMemDesc);

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_RAY,
									 RGXFWIF_DM_RAY,
									 hMemCtxPrivData,
									 psRayContext->psFWRayContextMemDesc,
									 offsetof(RGXFWIF_FWRAYCONTEXT, sRDMContext),
									 psFWMemContextMemDesc,
									 psRayContext->psContextStateMemDesc,
									 RGX_RDM_CCB_SIZE_LOG2,
									 RGX_RDM_CCB_MAX_SIZE_LOG2,
									 ui32ContextFlags,
									 i32Priority,
									 ui32MaxDeadlineMS,
									 ui64RobustnessAddress,
									 &sInfo,
									 &psRayContext->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to init Ray fw common context (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto fail_raycommoncontext;
	}

	eError = DevmemAcquireCpuVirtAddr(psRayContext->psFWRayContextMemDesc,
			(void **)&psFWRayContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_acquire_cpu_mapping;
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		WorkEstInitRay(psDevInfo, &psRayContext->sWorkEstData);
	}
#endif

	OSDeviceMemCopy(&psFWRayContext->sStaticRayContextState, pStaticRayContextState, ui32StaticRayContextStateSize);
	DevmemPDumpLoadMem(psRayContext->psFWRayContextMemDesc, 0, sizeof(RGXFWIF_FWCOMPUTECONTEXT), PDUMP_FLAGS_CONTINUOUS);
	DevmemReleaseCpuVirtAddr(psRayContext->psFWRayContextMemDesc);

	SyncAddrListInit(&psRayContext->sSyncAddrListFence);
	SyncAddrListInit(&psRayContext->sSyncAddrListUpdate);

	*ppsRayContext = psRayContext;

	return PVRSRV_OK;
fail_acquire_cpu_mapping:
fail_raycommoncontext:
	OSLockDestroy(psRayContext->hLock);
fail_createlock:
	DevmemFwUnmapAndFree(psDevInfo, psRayContext->psFWRayContextMemDesc);
fail_fwraycontext:
	OSFreeMem(psRayContext);

	return eError;
}

PVRSRV_ERROR PVRSRVRGXDestroyRayContextKM(RGX_SERVER_RAY_CONTEXT *psRayContext)
{

	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO *psDevInfo = psRayContext->psDeviceNode->pvDevice;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		RGXFWIF_FWRAYCONTEXT	*psFWRayContext;
		IMG_UINT32 ui32WorkEstCCBSubmitted;

		eError = DevmemAcquireCpuVirtAddr(psRayContext->psFWRayContextMemDesc,
				(void **)&psFWRayContext);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to map firmware ray context (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
			return eError;
		}

		ui32WorkEstCCBSubmitted = psFWRayContext->ui32WorkEstCCBSubmitted;

		DevmemReleaseCpuVirtAddr(psRayContext->psFWRayContextMemDesc);

		/* Check if all of the workload estimation CCB commands for this workload are read */
		if (ui32WorkEstCCBSubmitted != psRayContext->sWorkEstData.ui32WorkEstCCBReceived)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"%s: WorkEst # cmds submitted (%u) and received (%u) mismatch",
					__func__, ui32WorkEstCCBSubmitted,
					psRayContext->sWorkEstData.ui32WorkEstCCBReceived));

			return PVRSRV_ERROR_RETRY;
		}
	}
#endif

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psRayContext->psDeviceNode,
											  psRayContext->psServerCommonContext,
											  RGXFWIF_DM_RAY,
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
	FWCommonContextFree(psRayContext->psServerCommonContext);
	DevmemFwUnmapAndFree(psDevInfo, psRayContext->psContextStateMemDesc);
	psRayContext->psServerCommonContext = NULL;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		WorkEstDeInitRay(psDevInfo, &psRayContext->sWorkEstData);
	}
#endif

	DevmemFwUnmapAndFree(psDevInfo, psRayContext->psFWRayContextMemDesc);

	OSLockDestroy(psRayContext->hLock);
	OSFreeMem(psRayContext);

	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVRGXKickRDMKM(RGX_SERVER_RAY_CONTEXT	*psRayContext,
								IMG_UINT32				ui32ClientUpdateCount,
								SYNC_PRIMITIVE_BLOCK	**pauiClientUpdateUFODevVarBlock,
								IMG_UINT32				*paui32ClientUpdateSyncOffset,
								IMG_UINT32				*paui32ClientUpdateValue,
								PVRSRV_FENCE			iCheckFence,
								PVRSRV_TIMELINE			iUpdateTimeline,
								PVRSRV_FENCE			*piUpdateFence,
								IMG_CHAR				pcszUpdateFenceName[PVRSRV_SYNC_NAME_LENGTH],
								IMG_UINT32				ui32CmdSize,
								IMG_PBYTE				pui8DMCmd,
								IMG_UINT32				ui32PDumpFlags,
								IMG_UINT32				ui32ExtJobRef,
								IMG_UINT32				ui32AccStructSizeInBytes,
								IMG_UINT32				ui32DispatchSize,
								IMG_UINT64				ui64DeadlineInus)
{

	RGXFWIF_KCCB_CMD		sRayKCCBCmd;
	RGX_CCB_CMD_HELPER_DATA	asCmdHelperData[1];
	PVRSRV_ERROR			eError, eError2;
	IMG_UINT32				ui32FWCtx;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;
	PVRSRV_RGXDEV_INFO      *psDevInfo;
	RGX_CLIENT_CCB          *psClientCCB;
	IMG_UINT32              ui32IntJobRef;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	RGXFWIF_WORKEST_KICK_DATA sWorkloadKickDataRay = {0};
	IMG_UINT32 ui32RDMWorkloadDataRO = 0;
	IMG_UINT32 ui32RDMCmdHeaderOffset = 0;
	IMG_UINT32 ui32RDMCmdOffsetWrapCheck = 0;
	IMG_UINT32 ui32RDMCmdOffset = 0;
	RGX_WORKLOAD sWorkloadCharacteristics = {0};
#endif

	IMG_BOOL				bCCBStateOpen = IMG_FALSE;
	IMG_UINT64 ui64FBSCEntryMask;
	IMG_UINT32 ui32IntClientFenceCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntFenceUFOAddress = NULL;
	IMG_UINT32 ui32IntClientUpdateCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntUpdateUFOAddress = NULL;
	IMG_UINT32 *paui32IntUpdateValue = NULL;
	PVRSRV_FENCE  iUpdateFence = PVRSRV_NO_FENCE;
	IMG_UINT64               uiCheckFenceUID = 0;
	IMG_UINT64               uiUpdateFenceUID = 0;
	PSYNC_CHECKPOINT psUpdateSyncCheckpoint = NULL;
	PSYNC_CHECKPOINT *apsFenceSyncCheckpoints = NULL;
	IMG_UINT32 ui32FenceSyncCheckpointCount = 0;
	IMG_UINT32 *pui32IntAllocatedUpdateValues = NULL;
	PVRSRV_CLIENT_SYNC_PRIM *psFenceTimelineUpdateSync = NULL;
	IMG_UINT32 ui32FenceTimelineUpdateValue = 0;
	void *pvUpdateFenceFinaliseData = NULL;

	psDevInfo = FWCommonContextGetRGXDevInfo(psRayContext->psServerCommonContext);
	psClientCCB = FWCommonContextGetClientCCB(psRayContext->psServerCommonContext);
	ui32IntJobRef = OSAtomicIncrement(&psDevInfo->iCCBSubmissionOrdinal);

	if (iUpdateTimeline >= 0 && !piUpdateFence)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Ensure we haven't been given a null ptr to
	 * update values if we have been told we
	 * have updates
	 */
	if (ui32ClientUpdateCount > 0)
	{
		PVR_LOG_RETURN_IF_FALSE(paui32ClientUpdateValue != NULL,
		                        "paui32ClientUpdateValue NULL but "
		                        "ui32ClientUpdateCount > 0",
		                        PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Ensure the string is null-terminated (Required for safety) */
	pcszUpdateFenceName[PVRSRV_SYNC_NAME_LENGTH-1] = '\0';

	OSLockAcquire(psRayContext->hLock);

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListFence,
									0,
									NULL,
									NULL);
	if (eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	ui32IntClientUpdateCount = ui32ClientUpdateCount;

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListUpdate,
									ui32ClientUpdateCount,
									pauiClientUpdateUFODevVarBlock,
									paui32ClientUpdateSyncOffset);
	if (eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}
	if (ui32IntClientUpdateCount && !pauiIntUpdateUFOAddress)
	{
		pauiIntUpdateUFOAddress = psRayContext->sSyncAddrListUpdate.pasFWAddrs;
	}
	paui32IntUpdateValue = paui32ClientUpdateValue;

	CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointResolveFence (iCheckFence=%d), psRayContext->psDeviceNode->hSyncCheckpointContext=<%p>...", __func__, iCheckFence, (void*)psRayContext->psDeviceNode->hSyncCheckpointContext));
	/* Resolve the sync checkpoints that make up the input fence */
	eError = SyncCheckpointResolveFence(psRayContext->psDeviceNode->hSyncCheckpointContext,
										iCheckFence,
										&ui32FenceSyncCheckpointCount,
										&apsFenceSyncCheckpoints,
	                                    &uiCheckFenceUID, ui32PDumpFlags);
	if (eError != PVRSRV_OK)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, returned ERROR (eError=%d)", __func__, eError));
		goto fail_resolve_input_fence;
	}
	CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, fence %d contained %d checkpoints (apsFenceSyncCheckpoints=<%p>)", __func__, iCheckFence, ui32FenceSyncCheckpointCount, (void*)apsFenceSyncCheckpoints));
#if defined(CMP_CHECKPOINT_DEBUG)
	if (ui32FenceSyncCheckpointCount > 0)
	{
		IMG_UINT32 ii;
		for (ii=0; ii<ui32FenceSyncCheckpointCount; ii++)
		{
			PSYNC_CHECKPOINT psNextCheckpoint = *(apsFenceSyncCheckpoints + ii);
			CHKPT_DBG((PVR_DBG_ERROR, "%s:    apsFenceSyncCheckpoints[%d]=<%p>", __func__, ii, (void*)psNextCheckpoint));
		}
	}
#endif
	/* Create the output fence (if required) */
	if (iUpdateTimeline != PVRSRV_NO_TIMELINE)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointCreateFence (iUpdateFence=%d, iUpdateTimeline=%d,  psRayContext->psDeviceNode->hSyncCheckpointContext=<%p>)...", __func__, iUpdateFence, iUpdateTimeline, (void*)psRayContext->psDeviceNode->hSyncCheckpointContext));
		eError = SyncCheckpointCreateFence(psRayContext->psDeviceNode,
		                                   pcszUpdateFenceName,
										   iUpdateTimeline,
										   psRayContext->psDeviceNode->hSyncCheckpointContext,
										   &iUpdateFence,
										   &uiUpdateFenceUID,
										   &pvUpdateFenceFinaliseData,
										   &psUpdateSyncCheckpoint,
										   (void*)&psFenceTimelineUpdateSync,
										   &ui32FenceTimelineUpdateValue,
										   ui32PDumpFlags);
		if (eError != PVRSRV_OK)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: ...returned error (%d)", __func__, eError));
			goto fail_create_output_fence;
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s: ...returned from SyncCheckpointCreateFence (iUpdateFence=%d, psFenceTimelineUpdateSync=<%p>, ui32FenceTimelineUpdateValue=%u)", __func__, iUpdateFence, psFenceTimelineUpdateSync, ui32FenceTimelineUpdateValue));

		CHKPT_DBG((PVR_DBG_ERROR, "%s: ui32IntClientUpdateCount=%u, psFenceTimelineUpdateSync=<%p>", __func__, ui32IntClientUpdateCount, (void*)psFenceTimelineUpdateSync));
		/* Append the sync prim update for the timeline (if required) */
		if (psFenceTimelineUpdateSync)
		{
			IMG_UINT32 *pui32TimelineUpdateWp = NULL;

			/* Allocate memory to hold the list of update values (including our timeline update) */
			pui32IntAllocatedUpdateValues = OSAllocMem(sizeof(*pui32IntAllocatedUpdateValues) * (ui32IntClientUpdateCount+1));
			if (!pui32IntAllocatedUpdateValues)
			{
				/* Failed to allocate memory */
				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto fail_alloc_update_values_mem;
			}
			OSCachedMemSet(pui32IntAllocatedUpdateValues, 0xbb, sizeof(*pui32IntAllocatedUpdateValues) * (ui32IntClientUpdateCount+1));
			/* Copy the update values into the new memory, then append our timeline update value */
			OSCachedMemCopy(pui32IntAllocatedUpdateValues, paui32IntUpdateValue, sizeof(*pui32IntAllocatedUpdateValues) * ui32IntClientUpdateCount);
#if defined(CMP_CHECKPOINT_DEBUG)
			if (ui32IntClientUpdateCount > 0)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pui32IntAllocatedUpdateValues;

				CHKPT_DBG((PVR_DBG_ERROR, "%s: ui32IntClientUpdateCount=%u:", __func__, ui32IntClientUpdateCount));
				for (iii=0; iii<ui32IntClientUpdateCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR, "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
			/* Now set the additional update value */
			pui32TimelineUpdateWp = pui32IntAllocatedUpdateValues + ui32IntClientUpdateCount;
			*pui32TimelineUpdateWp = ui32FenceTimelineUpdateValue;
			ui32IntClientUpdateCount++;
			/* Now make sure paui32ClientUpdateValue points to pui32IntAllocatedUpdateValues */
			paui32ClientUpdateValue = pui32IntAllocatedUpdateValues;

			CHKPT_DBG((PVR_DBG_ERROR, "%s: append the timeline sync prim addr <%p> to the compute context update list", __func__,  (void*)psFenceTimelineUpdateSync));
			/* Now append the timeline sync prim addr to the compute context update list */
			SyncAddrListAppendSyncPrim(&psRayContext->sSyncAddrListUpdate,
			                           psFenceTimelineUpdateSync);
#if defined(CMP_CHECKPOINT_DEBUG)
			if (ui32IntClientUpdateCount > 0)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pui32IntAllocatedUpdateValues;

				CHKPT_DBG((PVR_DBG_ERROR, "%s: ui32IntClientUpdateCount=%u:", __func__, ui32IntClientUpdateCount));
				for (iii=0; iii<ui32IntClientUpdateCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR, "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
			/* Ensure paui32IntUpdateValue is now pointing to our new array of update values */
			paui32IntUpdateValue = pui32IntAllocatedUpdateValues;
		}
	}

	/* Append the checks (from input fence) */
	if (ui32FenceSyncCheckpointCount > 0)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append %d sync checkpoints to Ray RDM Fence (&psRayContext->sSyncAddrListFence=<%p>)...", __func__, ui32FenceSyncCheckpointCount, (void*)&psRayContext->sSyncAddrListFence));
#if defined(CMP_CHECKPOINT_DEBUG)
		if (ui32IntClientUpdateCount > 0)
		{
			IMG_UINT32 iii;
			IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiIntFenceUFOAddress;

			for (iii=0; iii<ui32IntClientUpdateCount; iii++)
			{
				CHKPT_DBG((PVR_DBG_ERROR, "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
				pui32Tmp++;
			}
		}
#endif
		SyncAddrListAppendCheckpoints(&psRayContext->sSyncAddrListFence,
									  ui32FenceSyncCheckpointCount,
									  apsFenceSyncCheckpoints);
		if (!pauiIntFenceUFOAddress)
		{
			pauiIntFenceUFOAddress = psRayContext->sSyncAddrListFence.pasFWAddrs;
		}
		ui32IntClientFenceCount += ui32FenceSyncCheckpointCount;
	}
#if defined(CMP_CHECKPOINT_DEBUG)
	if (ui32IntClientUpdateCount > 0)
	{
		IMG_UINT32 iii;
		IMG_UINT32 *pui32Tmp = (IMG_UINT32*)paui32IntUpdateValue;

		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Dumping %d update values (paui32IntUpdateValue=<%p>)...", __func__, ui32IntClientUpdateCount, (void*)paui32IntUpdateValue));
		for (iii=0; iii<ui32IntClientUpdateCount; iii++)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: paui32IntUpdateValue[%d] = <%p>", __func__, iii, (void*)pui32Tmp));
			CHKPT_DBG((PVR_DBG_ERROR, "%s: *paui32IntUpdateValue[%d] = 0x%x", __func__, iii, *pui32Tmp));
			pui32Tmp++;
		}
	}
#endif

	if (psUpdateSyncCheckpoint)
	{
		/* Append the update (from output fence) */
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append 1 sync checkpoint to Ray RDM Update (&psRayContext->sSyncAddrListUpdate=<%p>, psUpdateSyncCheckpoint=<%p>)...", __func__, (void*)&psRayContext->sSyncAddrListUpdate , (void*)psUpdateSyncCheckpoint));
		SyncAddrListAppendCheckpoints(&psRayContext->sSyncAddrListUpdate,
									  1,
									  &psUpdateSyncCheckpoint);
		if (!pauiIntUpdateUFOAddress)
		{
			pauiIntUpdateUFOAddress = psRayContext->sSyncAddrListUpdate.pasFWAddrs;
		}
		ui32IntClientUpdateCount++;
#if defined(CMP_CHECKPOINT_DEBUG)
		if (ui32IntClientUpdateCount > 0)
		{
			IMG_UINT32 iii;
			IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiIntUpdateUFOAddress;

			CHKPT_DBG((PVR_DBG_ERROR, "%s: pauiIntUpdateUFOAddress=<%p>, pui32Tmp=<%p>, ui32IntClientUpdateCount=%u", __func__, (void*)pauiIntUpdateUFOAddress, (void*)pui32Tmp, ui32IntClientUpdateCount));
			for (iii=0; iii<ui32IntClientUpdateCount; iii++)
			{
				CHKPT_DBG((PVR_DBG_ERROR, "%s: pauiIntUpdateUFOAddress[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
				pui32Tmp++;
			}
		}
#endif
	}
	CHKPT_DBG((PVR_DBG_ERROR, "%s:   (after pvr_sync) ui32IntClientFenceCount=%d, ui32IntClientUpdateCount=%d", __func__, ui32IntClientFenceCount, ui32IntClientUpdateCount));

#if (ENABLE_CMP_UFO_DUMP == 1)
		PVR_DPF((PVR_DBG_ERROR, "%s: dumping Ray (RDM) fence/updates syncs...", __func__));
		{
			IMG_UINT32 ii;
			PRGXFWIF_UFO_ADDR *psTmpIntFenceUFOAddress = pauiIntFenceUFOAddress;
			PRGXFWIF_UFO_ADDR *psTmpIntUpdateUFOAddress = pauiIntUpdateUFOAddress;
			IMG_UINT32 *pui32TmpIntUpdateValue = paui32IntUpdateValue;

			/* Dump Fence syncs and Update syncs */
			PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d Ray (RDM) fence syncs (&psRayContext->sSyncAddrListFence=<%p>, pauiIntFenceUFOAddress=<%p>):", __func__, ui32IntClientFenceCount, (void*)&psRayContext->sSyncAddrListFence, (void*)pauiIntFenceUFOAddress));
			for (ii=0; ii<ui32IntClientFenceCount; ii++)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s:   %d/%d<%p>. FWAddr=0x%x, CheckValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED", __func__, ii+1, ui32IntClientFenceCount, (void*)psTmpIntFenceUFOAddress, psTmpIntFenceUFOAddress->ui32Addr));
				psTmpIntFenceUFOAddress++;
			}
			PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d Ray (RDM) update syncs (&psRayContext->sSyncAddrListUpdate=<%p>, pauiIntUpdateUFOAddress=<%p>):", __func__, ui32IntClientUpdateCount, (void*)&psRayContext->sSyncAddrListUpdate, (void*)pauiIntUpdateUFOAddress));
			for (ii=0; ii<ui32IntClientUpdateCount; ii++)
			{
				if (psTmpIntUpdateUFOAddress->ui32Addr & 0x1)
				{
					PVR_DPF((PVR_DBG_ERROR, "%s:   %d/%d<%p>. FWAddr=0x%x, UpdateValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED", __func__, ii+1, ui32IntClientUpdateCount, (void*)psTmpIntUpdateUFOAddress, psTmpIntUpdateUFOAddress->ui32Addr));
				}
				else
				{
					PVR_DPF((PVR_DBG_ERROR, "%s:   %d/%d<%p>. FWAddr=0x%x, UpdateValue=%d", __func__, ii+1, ui32IntClientUpdateCount, (void*)psTmpIntUpdateUFOAddress, psTmpIntUpdateUFOAddress->ui32Addr, *pui32TmpIntUpdateValue));
					pui32TmpIntUpdateValue++;
				}
				psTmpIntUpdateUFOAddress++;
			}
		}
#endif

	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psRayContext->psDeviceNode->pvDevice,
	                          &pPreAddr,
	                          &pPostAddr,
	                          &pRMWUFOAddr);

	/*
	* Extract the FBSC entries from MMU Context for the deferred FBSC invalidate command,
	* in other words, take the value and set it to zero afterwards.
	* FBSC Entry Mask must be extracted from MMU ctx and updated just before the kick starts
	* as it must be ready at the time of context activation.
	*/
	{
		eError = RGXExtractFBSCEntryMaskFromMMUContext(psRayContext->psDeviceNode,
														FWCommonContextGetServerMMUCtx(psRayContext->psServerCommonContext),
														&ui64FBSCEntryMask);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Failed to extract FBSC Entry Mask (%d)", eError));
			goto fail_invalfbsc;
		}
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		sWorkloadCharacteristics.sRay.ui32AccStructSize = ui32AccStructSizeInBytes;
		sWorkloadCharacteristics.sRay.ui32DispatchSize = ui32DispatchSize;

		/* Prepare workload estimation */
		WorkEstPrepare(psRayContext->psDeviceNode->pvDevice,
				&psRayContext->sWorkEstData,
				&psRayContext->sWorkEstData.uWorkloadMatchingData.sCompute.sDataCDM,
				RGXFWIF_CCB_CMD_TYPE_RAY,
				&sWorkloadCharacteristics,
				ui64DeadlineInus,
				&sWorkloadKickDataRay);

		if (sWorkloadKickDataRay.ui32CyclesPrediction != 0)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "%s: Dispatch size = %u, Acc struct size = %u, prediction = %u",
					__func__,
					sWorkloadCharacteristics.sRay.ui32DispatchSize,
					sWorkloadCharacteristics.sRay.ui32AccStructSize,
					sWorkloadKickDataRay.ui32CyclesPrediction));
		}
	}
#endif

	RGXCmdHelperInitCmdCCB(psDevInfo,
	                       psClientCCB,
	                       ui64FBSCEntryMask,
	                       ui32IntClientFenceCount,
	                       pauiIntFenceUFOAddress,
	                       NULL,
	                       ui32IntClientUpdateCount,
	                       pauiIntUpdateUFOAddress,
	                       paui32IntUpdateValue,
	                       ui32CmdSize,
	                       pui8DMCmd,
                           &pPreAddr,
                           &pPostAddr,
                           &pRMWUFOAddr,
	                       RGXFWIF_CCB_CMD_TYPE_RAY,
	                       ui32ExtJobRef,
	                       ui32IntJobRef,
	                       ui32PDumpFlags,
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	                       &sWorkloadKickDataRay,
#else
	                       NULL,
#endif
	                       "Ray",
	                       bCCBStateOpen,
	                       asCmdHelperData);
	eError = RGXCmdHelperAcquireCmdCCB(ARRAY_SIZE(asCmdHelperData), asCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto fail_cmdaquire;
	}

	if (eError == PVRSRV_OK)
	{
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			ui32RDMCmdOffset = RGXGetHostWriteOffsetCCB(psClientCCB);
		}
#endif
		/*
			All the required resources are ready at this point, we can't fail so
			take the required server sync operations and commit all the resources
		*/
		RGXCmdHelperReleaseCmdCCB(1, asCmdHelperData, "RDM", FWCommonContextGetFWAddress(psRayContext->psServerCommonContext).ui32Addr);
	}


#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* The following is used to determine the offset of the command header containing
		 * the workload estimation data so that can be accessed when the KCCB is read */
		ui32RDMCmdHeaderOffset = RGXCmdHelperGetDMCommandHeaderOffset(asCmdHelperData);

		ui32RDMCmdOffsetWrapCheck = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRayContext->psServerCommonContext));

		/* This checks if the command would wrap around at the end of the CCB and
		 * therefore would start at an offset of 0 rather than the current command
		 * offset */
		if (ui32RDMCmdOffset < ui32RDMCmdOffsetWrapCheck)
		{
			ui32RDMWorkloadDataRO = ui32RDMCmdOffset;
		}
		else
		{
			ui32RDMWorkloadDataRO = 0;
		}
	}
#endif

	/* Construct the kernel ray CCB command. */
	sRayKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sRayKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRayContext->psServerCommonContext);
	sRayKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(psClientCCB);
	sRayKCCBCmd.uCmdData.sCmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(psClientCCB);
	sRayKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

	/* Add the Workload data into the KCCB kick */
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		/* Store the offset to the CCCB command header so that it can be referenced
		 * when the KCCB command reaches the FW */
		sRayKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = ui32RDMWorkloadDataRO + ui32RDMCmdHeaderOffset;
	}
#endif
	ui32FWCtx = FWCommonContextGetFWAddress(psRayContext->psServerCommonContext).ui32Addr;

	RGXSRV_HWPERF_ENQ(psRayContext,
	                  OSGetCurrentClientProcessIDKM(),
	                  ui32FWCtx,
	                  ui32ExtJobRef,
	                  ui32IntJobRef,
	                  RGX_HWPERF_KICK_TYPE2_RS,
	                  iCheckFence,
	                  iUpdateFence,
	                  iUpdateTimeline,
	                  uiCheckFenceUID,
	                  uiUpdateFenceUID,
	                  NO_DEADLINE,
	                  NO_CYCEST);

	/*
	 * Submit the compute command to the firmware.
	 */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError2 = RGXScheduleCommand(psRayContext->psDeviceNode->pvDevice,
									RGXFWIF_DM_RAY,
									&sRayKCCBCmd,
									ui32PDumpFlags);
		if (eError2 != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError2 != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s failed to schedule kernel CCB command (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError2)));
		if (eError == PVRSRV_OK)
		{
			eError = eError2;
		}
	}
	else
	{
		PVRGpuTraceEnqueueEvent(psRayContext->psDeviceNode,
		                        ui32FWCtx, ui32ExtJobRef, ui32IntJobRef,
		                        RGX_HWPERF_KICK_TYPE2_RS);
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
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Signalling NOHW sync checkpoint<%p>, ID:%d, FwAddr=0x%x", __func__, (void*)psUpdateSyncCheckpoint, SyncCheckpointGetId(psUpdateSyncCheckpoint), SyncCheckpointGetFirmwareAddr(psUpdateSyncCheckpoint)));
		SyncCheckpointSignalNoHW(psUpdateSyncCheckpoint);
	}
	if (psFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Updating NOHW sync prim<%p> to %d", __func__, (void*)psFenceTimelineUpdateSync, ui32FenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(psFenceTimelineUpdateSync, ui32FenceTimelineUpdateValue);
	}
	SyncCheckpointNoHWUpdateTimelines(NULL);
#endif /* defined(NO_HARDWARE) */

	*piUpdateFence = iUpdateFence;

	if (pvUpdateFenceFinaliseData && (iUpdateFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psRayContext->psDeviceNode, iUpdateFence,
		                            pvUpdateFenceFinaliseData,
									psUpdateSyncCheckpoint, pcszUpdateFenceName);
	}
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
	OSLockRelease(psRayContext->hLock);

	return PVRSRV_OK;

fail_cmdaquire:
fail_invalfbsc:
	SyncAddrListRollbackCheckpoints(psRayContext->psDeviceNode, &psRayContext->sSyncAddrListFence);
	SyncAddrListRollbackCheckpoints(psRayContext->psDeviceNode, &psRayContext->sSyncAddrListUpdate);
fail_alloc_update_values_mem:
	if (iUpdateFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(iUpdateFence, pvUpdateFenceFinaliseData);
	}
fail_create_output_fence:
	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence */
	SyncAddrListDeRefCheckpoints(ui32FenceSyncCheckpointCount,
								 apsFenceSyncCheckpoints);
fail_resolve_input_fence:

err_populate_sync_addr_list:
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

	OSLockRelease(psRayContext->hLock);
	return eError;
}

/******************************************************************************
 End of file (rgxray.c)
******************************************************************************/
