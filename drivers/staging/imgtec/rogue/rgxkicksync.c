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

#include "rgxkicksync.h"

#include "rgxdevice.h"
#include "rgxmem.h"
#include "rgxfwutils.h"
#include "allocmem.h"
#include "sync.h"
#include "rgxhwperf.h"

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif

struct _RGX_SERVER_KICKSYNC_CONTEXT_
{
	PVRSRV_DEVICE_NODE        * psDeviceNode;
	RGX_SERVER_COMMON_CONTEXT * psServerCommonContext;
	PVRSRV_CLIENT_SYNC_PRIM   * psSync;
	DLLIST_NODE                 sListNode;
	SYNC_ADDR_LIST              sSyncAddrListFence;
	SYNC_ADDR_LIST              sSyncAddrListUpdate;
	ATOMIC_T                    hJobId;
};


IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateKickSyncContextKM(CONNECTION_DATA             * psConnection,
                                              PVRSRV_DEVICE_NODE          * psDeviceNode,
                                              IMG_HANDLE					hMemCtxPrivData,
                                              RGX_SERVER_KICKSYNC_CONTEXT ** ppsKickSyncContext)
{
	PVRSRV_RGXDEV_INFO          * psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC              * psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext;
	RGX_COMMON_CONTEXT_INFO      sInfo;
	PVRSRV_ERROR                 eError = PVRSRV_OK;

	/* Prepare cleanup struct */
	* ppsKickSyncContext = NULL;
	psKickSyncContext = OSAllocZMem(sizeof(*psKickSyncContext));
	if (psKickSyncContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psKickSyncContext->psDeviceNode = psDeviceNode;

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
	                       & psKickSyncContext->psSync,
	                       "kick sync cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "PVRSRVRGXCreateKickSyncContextKM: Failed to allocate cleanup sync (0x%x)",
		         eError));
		goto fail_syncalloc;
	}

	sInfo.psFWFrameworkMemDesc = NULL;
	sInfo.psMCUFenceAddr = NULL;

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_KICKSYNC,
									 RGXFWIF_DM_GP,
									 NULL,
									 0,
									 psFWMemContextMemDesc,
									 NULL,
									 RGX_KICKSYNC_CCB_SIZE_LOG2,
	                                 0, /* priority */
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
fail_syncalloc:
	OSFreeMem(psKickSyncContext);
	return eError;
}	


IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyKickSyncContextKM(RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext)
{
	PVRSRV_ERROR         eError    = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO * psDevInfo = psKickSyncContext->psDeviceNode->pvDevice;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psKickSyncContext->psDeviceNode,
	                                          psKickSyncContext->psServerCommonContext,
	                                          psKickSyncContext->psSync,
	                                          RGXFWIF_DM_3D,
	                                          PDUMP_FLAGS_NONE);

	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}	
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__FUNCTION__,
				PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */

	OSWRLockAcquireWrite(psDevInfo->hKickSyncCtxListLock);
	dllist_remove_node(&(psKickSyncContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hKickSyncCtxListLock);

	FWCommonContextFree(psKickSyncContext->psServerCommonContext);
	SyncPrimFree(psKickSyncContext->psSync);

	SyncAddrListDeinit(&psKickSyncContext->sSyncAddrListFence);
	SyncAddrListDeinit(&psKickSyncContext->sSyncAddrListUpdate);
	
	OSFreeMem(psKickSyncContext);

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickSyncKM(RGX_SERVER_KICKSYNC_CONTEXT * psKickSyncContext,

                                 IMG_UINT32                    ui32ClientCacheOpSeqNum,

                                 IMG_UINT32                    ui32ClientFenceCount,
                                 SYNC_PRIMITIVE_BLOCK           ** pauiClientFenceUFOSyncPrimBlock,
                                 IMG_UINT32                  * paui32ClientFenceOffset,
                                 IMG_UINT32                  * paui32ClientFenceValue,

                                 IMG_UINT32                    ui32ClientUpdateCount,
                                 SYNC_PRIMITIVE_BLOCK           ** pauiClientUpdateUFOSyncPrimBlock,
                                 IMG_UINT32                  * paui32ClientUpdateOffset,
                                 IMG_UINT32                  * paui32ClientUpdateValue,

                                 IMG_UINT32                    ui32ServerSyncPrims,
                                 IMG_UINT32                  * paui32ServerSyncFlags,
                                 SERVER_SYNC_PRIMITIVE      ** pasServerSyncs,

                                 IMG_INT32                     i32CheckFenceFD,
                                 IMG_INT32                     i32UpdateTimelineFD,
                                 IMG_INT32                   * pi32UpdateFenceFD,
                                 IMG_CHAR                      szFenceName[32],

                                 IMG_UINT32                    ui32ExtJobRef)
{
	RGXFWIF_KCCB_CMD         sKickSyncKCCBCmd;
	RGX_CCB_CMD_HELPER_DATA  asCmdHelperData[1];
	PVRSRV_ERROR             eError;
	PVRSRV_ERROR             eError2;
	IMG_UINT32               i;
	PRGXFWIF_UFO_ADDR        *pauiClientFenceUFOAddress;
	PRGXFWIF_UFO_ADDR        *pauiClientUpdateUFOAddress;
	IMG_INT32                i32UpdateFenceFD = -1;
	IMG_UINT32               ui32JobId;
	IMG_UINT32               ui32FWCtx = FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext).ui32Addr;

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* Android fd sync update info */
	struct pvr_sync_append_data *psFDFenceData = NULL;
#endif
	
	ui32JobId = OSAtomicIncrement(&psKickSyncContext->hJobId);

	eError = SyncAddrListPopulate(&psKickSyncContext->sSyncAddrListFence,
							ui32ClientFenceCount,
							pauiClientFenceUFOSyncPrimBlock,
							paui32ClientFenceOffset);

	if(eError != PVRSRV_OK)
	{
		goto fail_syncaddrlist;
	}

	pauiClientFenceUFOAddress = psKickSyncContext->sSyncAddrListFence.pasFWAddrs;

	eError = SyncAddrListPopulate(&psKickSyncContext->sSyncAddrListUpdate,
							ui32ClientUpdateCount,
							pauiClientUpdateUFOSyncPrimBlock,
							paui32ClientUpdateOffset);

	if(eError != PVRSRV_OK)
	{
		goto fail_syncaddrlist;
	}

	pauiClientUpdateUFOAddress = psKickSyncContext->sSyncAddrListUpdate.pasFWAddrs;

	/* Sanity check the server fences */
	for (i = 0; i < ui32ServerSyncPrims; i++)
	{
		if (0 == (paui32ServerSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on Kick Sync) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	/* Ensure the string is null-terminated (Required for safety) */
	szFenceName[31] = '\0';

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* Android FD fences are hardcoded to updates (IMG_TRUE below), Fences go to the TA and updates to the 3D */
	if (i32UpdateTimelineFD >= 0 && !pi32UpdateFenceFD)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (i32CheckFenceFD >= 0 || i32UpdateTimelineFD >= 0)
	{
		eError =
		  pvr_sync_append_fences(szFenceName,
								 i32CheckFenceFD,
								 i32UpdateTimelineFD,
								 ui32ClientUpdateCount,
								 pauiClientUpdateUFOAddress,
								 paui32ClientUpdateValue,
								 ui32ClientFenceCount,
								 pauiClientFenceUFOAddress,
								 paui32ClientFenceValue,
								 &psFDFenceData);
		if (eError != PVRSRV_OK)
		{
			goto fail_fdsync;
		}
		pvr_sync_get_updates(psFDFenceData, &ui32ClientUpdateCount,
			&pauiClientUpdateUFOAddress, &paui32ClientUpdateValue);
		pvr_sync_get_checks(psFDFenceData, &ui32ClientFenceCount,
			&pauiClientFenceUFOAddress, &paui32ClientFenceValue);
	}
#endif /* defined(SUPPORT_NATIVE_FENCE_SYNC) */

	eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(psKickSyncContext->psServerCommonContext),
	                                ui32ClientFenceCount,
	                                pauiClientFenceUFOAddress,
	                                paui32ClientFenceValue,
	                                ui32ClientUpdateCount,
	                                pauiClientUpdateUFOAddress,
	                                paui32ClientUpdateValue,
	                                ui32ServerSyncPrims,
	                                paui32ServerSyncFlags,
	                                SYNC_FLAG_MASK_ALL,
	                                pasServerSyncs,
	                                0,
	                                NULL,
	                                NULL,
	                                NULL,
	                                NULL,
	                                RGXFWIF_CCB_CMD_TYPE_NULL,
	                                ui32ExtJobRef,
	                                ui32JobId,
	                                PDUMP_FLAGS_NONE,
	                                NULL,
	                                "KickSync",
	                                asCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto fail_cmdinit;
	}

	eError = RGXCmdHelperAcquireCmdCCB(IMG_ARR_NUM_ELEMS(asCmdHelperData), asCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto fail_cmdaquire;
	}

	/*
	 *  We should reserved space in the kernel CCB here and fill in the command
	 *  directly.
	 *  This is so if there isn't space in the kernel CCB we can return with
	 *  retry back to services client before we take any operations
	 */

	/*
	 * We might only be kicking for flush out a padding packet so only submit
	 * the command if the create was successful
	 */

	/*
	 * All the required resources are ready at this point, we can't fail so
	 * take the required server sync operations and commit all the resources
	 */
	RGXCmdHelperReleaseCmdCCB(1,
	                          asCmdHelperData,
	                          "KickSync",
	                          FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext).ui32Addr);

	/* Construct the kernel kicksync CCB command. */
	sKickSyncKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psKickSyncContext->psServerCommonContext);
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psKickSyncContext->psServerCommonContext));
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.sWorkloadDataFWAddress.ui32Addr = 0;
	sKickSyncKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;
	
	/*
	 * Submit the kicksync command to the firmware.
	 */
	RGX_HWPERF_HOST_ENQ(psKickSyncContext, OSGetCurrentClientProcessIDKM(),
	                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
	                    RGX_HWPERF_KICK_TYPE_SYNC);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError2 = RGXScheduleCommand(psKickSyncContext->psDeviceNode->pvDevice,
		                             RGXFWIF_DM_3D,
		                             & sKickSyncKCCBCmd,
		                             sizeof(sKickSyncKCCBCmd),
		                             ui32ClientCacheOpSeqNum,
		                             PDUMP_FLAGS_NONE);
		if (eError2 != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUEnqueueEvent(psKickSyncContext->psDeviceNode->pvDevice,
					ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_SYNC);
#endif

	if (eError2 != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "PVRSRVRGXKickSync failed to schedule kernel CCB command. (0x%x)",
		         eError2));
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	if (i32UpdateTimelineFD >= 0)
	{
		/* If we get here, this should never fail. Hitting that likely implies
		 * a code error above */
		i32UpdateFenceFD = pvr_sync_get_update_fd(psFDFenceData);
		if (i32UpdateFenceFD < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get install update sync fd",
				__FUNCTION__));
			/* If we fail here, we cannot rollback the syncs as the hw already
			 * has references to resources they may be protecting in the kick
			 * so fallthrough */

			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_free_append_data;
		}
	}

#if defined(NO_HARDWARE)
	pvr_sync_nohw_complete_fences(psFDFenceData);
#endif
	pvr_sync_free_append_fences_data(psFDFenceData);
#endif

	*pi32UpdateFenceFD = i32UpdateFenceFD;

	return PVRSRV_OK;

fail_cmdaquire:
fail_cmdinit:
#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_rollback_append_fences(psFDFenceData);
fail_free_append_data:
	pvr_sync_free_append_fences_data(psFDFenceData);
fail_fdsync:
#endif
fail_syncaddrlist:
	return eError;
}	


/**************************************************************************//**
 End of file (rgxkicksync.c)
******************************************************************************/
