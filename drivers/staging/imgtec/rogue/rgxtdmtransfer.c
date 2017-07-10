/*************************************************************************/ /*!
@File           rgxtdmtransfer.c
@Title          Device specific TDM transfer queue routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#include "pdump_km.h"
#include "rgxdevice.h"
#include "rgxccb.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxtdmtransfer.h"
#include "rgx_tq_shared.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "pvrsrv.h"
#include "rgx_fwif_resetframework.h"
#include "rgx_memallocflags.h"
#include "rgxtimerquery.h"
#include "rgxhwperf.h"
#include "htbuffer.h"

#include "pdump_km.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif


typedef struct {
	RGX_SERVER_COMMON_CONTEXT * psServerCommonContext;
	IMG_UINT32                  ui32Priority;
} RGX_SERVER_TQ_TDM_DATA;


struct _RGX_SERVER_TQ_TDM_CONTEXT_ {
	PVRSRV_DEVICE_NODE      *psDeviceNode;
	DEVMEM_MEMDESC          *psFWFrameworkMemDesc;
	IMG_UINT32              ui32Flags;
	RGX_SERVER_TQ_TDM_DATA  sTDMData;
	PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync;
	DLLIST_NODE             sListNode;
	SYNC_ADDR_LIST          sSyncAddrListFence;
	SYNC_ADDR_LIST          sSyncAddrListUpdate;
	ATOMIC_T                hJobId;
};

static PVRSRV_ERROR _CreateTDMTransferContext(
	CONNECTION_DATA         * psConnection,
	PVRSRV_DEVICE_NODE      * psDeviceNode,
	DEVMEM_MEMDESC          * psFWMemContextMemDesc,
	IMG_UINT32                ui32Priority,
	RGX_COMMON_CONTEXT_INFO * psInfo,
	RGX_SERVER_TQ_TDM_DATA  * psTDMData)
{
	PVRSRV_ERROR eError;

	eError = FWCommonContextAllocate(
		psConnection,
		psDeviceNode,
		REQ_TYPE_TQ_TDM,
		RGXFWIF_DM_TDM,
		NULL,
		0,
		psFWMemContextMemDesc,
		NULL,
		RGX_TQ2D_CCB_SIZE_LOG2,
		ui32Priority,
		psInfo,
		&psTDMData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextalloc;
	}

	psTDMData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_contextalloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR _DestroyTDMTransferContext(
	RGX_SERVER_TQ_TDM_DATA  * psTDMData,
	PVRSRV_DEVICE_NODE      * psDeviceNode,
	PVRSRV_CLIENT_SYNC_PRIM * psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(
		psDeviceNode,
		psTDMData->psServerCommonContext,
		psCleanupSync,
		RGXFWIF_DM_TDM,
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

	/* ... it has so we can free it's resources */
	FWCommonContextFree(psTDMData->psServerCommonContext);
	return PVRSRV_OK;
}

/*
 * PVRSRVCreateTransferContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXTDMCreateTransferContextKM(
	CONNECTION_DATA            * psConnection,
	PVRSRV_DEVICE_NODE         * psDeviceNode,
	IMG_UINT32                   ui32Priority,
	IMG_DEV_VIRTADDR             sMCUFenceAddr,
	IMG_UINT32                   ui32FrameworkCommandSize,
	IMG_PBYTE                    pabyFrameworkCommand,
	IMG_HANDLE                   hMemCtxPrivData,
	RGX_SERVER_TQ_TDM_CONTEXT ** ppsTransferContext)
{
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContext;

	DEVMEM_MEMDESC          * psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO   sInfo;
	PVRSRV_ERROR              eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO        *psDevInfo = psDeviceNode->pvDevice;

	/* Allocate the server side structure */
	*ppsTransferContext = NULL;
	psTransferContext = OSAllocZMem(sizeof(*psTransferContext));
	if (psTransferContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psTransferContext->psDeviceNode = psDeviceNode;

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psTransferContext->psCleanupSync,
						   "transfer context cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateTransferContextKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto fail_syncalloc;
	}

	/* 
	 * Create the FW framework buffer
	 */
	eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode,
										&psTransferContext->psFWFrameworkMemDesc,
										ui32FrameworkCommandSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateTransferContextKM: Failed to allocate firmware GPU framework state (%u)",
				eError));
		goto fail_frameworkcreate;
	}

	/* Copy the Framework client data into the framework buffer */
	eError = PVRSRVRGXFrameworkCopyCommand(psTransferContext->psFWFrameworkMemDesc,
										   pabyFrameworkCommand,
										   ui32FrameworkCommandSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateTransferContextKM: Failed to populate the framework buffer (%u)",
				eError));
		goto fail_frameworkcopy;
	}

	sInfo.psFWFrameworkMemDesc = psTransferContext->psFWFrameworkMemDesc;
	sInfo.psMCUFenceAddr = &sMCUFenceAddr;

	eError = _CreateTDMTransferContext(psConnection,
									  psDeviceNode,
									  psFWMemContextMemDesc,
									  ui32Priority,
									  &sInfo,
									  &psTransferContext->sTDMData);
	if (eError != PVRSRV_OK)
	{
		goto fail_tdmtransfercontext;
	}

	SyncAddrListInit(&psTransferContext->sSyncAddrListFence);
	SyncAddrListInit(&psTransferContext->sSyncAddrListUpdate);

	{
		OSWRLockAcquireWrite(psDevInfo->hTDMCtxListLock);
		dllist_add_to_tail(&(psDevInfo->sTDMCtxtListHead), &(psTransferContext->sListNode));
		OSWRLockReleaseWrite(psDevInfo->hTDMCtxListLock);
		*ppsTransferContext = psTransferContext;
	}

	*ppsTransferContext = psTransferContext;
	
	return PVRSRV_OK;
	
fail_tdmtransfercontext:
fail_frameworkcopy:
	DevmemFwFree(psDevInfo, psTransferContext->psFWFrameworkMemDesc);
fail_frameworkcreate:
	SyncPrimFree(psTransferContext->psCleanupSync);
fail_syncalloc:
	OSFreeMem(psTransferContext);
	PVR_ASSERT(eError != PVRSRV_OK);
	*ppsTransferContext = NULL;
	return eError;
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXTDMDestroyTransferContextKM(RGX_SERVER_TQ_TDM_CONTEXT *psTransferContext)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psTransferContext->psDeviceNode->pvDevice;

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hTDMCtxListLock);
	dllist_remove_node(&(psTransferContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hTDMCtxListLock);


	eError = _DestroyTDMTransferContext(&psTransferContext->sTDMData,
	                                    psTransferContext->psDeviceNode,
	                                    psTransferContext->psCleanupSync);
	if (eError != PVRSRV_OK)
	{
		goto fail_destroyTDM;
	}

	DevmemFwFree(psDevInfo, psTransferContext->psFWFrameworkMemDesc);
	SyncPrimFree(psTransferContext->psCleanupSync);

	SyncAddrListDeinit(&psTransferContext->sSyncAddrListFence);
	SyncAddrListDeinit(&psTransferContext->sSyncAddrListUpdate);

	OSFreeMem(psTransferContext);

	return PVRSRV_OK;

  fail_destroyTDM:

	OSWRLockAcquireWrite(psDevInfo->hTDMCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sTDMCtxtListHead), &(psTransferContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hTDMCtxListLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
 * PVRSRVSubmitTQ3DKickKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXTDMSubmitTransferKM(
	RGX_SERVER_TQ_TDM_CONTEXT * psTransferContext,
	IMG_UINT32                  ui32PDumpFlags,
	IMG_UINT32                  ui32ClientCacheOpSeqNum,
	IMG_UINT32                  ui32ClientFenceCount,
	SYNC_PRIMITIVE_BLOCK     ** pauiClientFenceUFOSyncPrimBlock,
	IMG_UINT32                * paui32ClientFenceSyncOffset,
	IMG_UINT32                * paui32ClientFenceValue,
	IMG_UINT32                  ui32ClientUpdateCount,
	SYNC_PRIMITIVE_BLOCK     ** pauiClientUpdateUFOSyncPrimBlock,
	IMG_UINT32                * paui32ClientUpdateSyncOffset,
	IMG_UINT32                * paui32ClientUpdateValue,
	IMG_UINT32                  ui32ServerSyncCount,
	IMG_UINT32                * paui32ServerSyncFlags,
	SERVER_SYNC_PRIMITIVE    ** papsServerSyncs,
	IMG_INT32                   i32CheckFenceFD,
	IMG_INT32                   i32UpdateTimelineFD,
	IMG_INT32                 * pi32UpdateFenceFD,
	IMG_CHAR                    szFenceName[32],
	IMG_UINT32                  ui32FWCommandSize,
	IMG_UINT8                 * pui8FWCommand,
	IMG_UINT32                  ui32ExtJobRef,
	IMG_UINT32                  ui32SyncPMRCount,
	IMG_UINT32                * paui32SyncPMRFlags,
	PMR                      ** ppsSyncPMRs)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psTransferContext->psDeviceNode;
	RGX_CCB_CMD_HELPER_DATA *psCmdHelper;
	PRGXFWIF_UFO_ADDR * pauiIntFenceUFOAddress   = NULL;
	PRGXFWIF_UFO_ADDR * pauiIntUpdateUFOAddress  = NULL;
	IMG_UINT32        * paui32IntFenceValue      = paui32ClientFenceValue;
	IMG_UINT32          ui32IntClientFenceCount  = ui32ClientFenceCount;
	IMG_UINT32        * paui32IntUpdateValue     = paui32ClientUpdateValue;
	IMG_UINT32          ui32IntClientUpdateCount = ui32ClientUpdateCount;
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	IMG_INT32 i32UpdateFenceFD = -1;
	IMG_UINT32 ui32JobId;

	IMG_UINT32 ui32CmdOffset = 0;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;

#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_append_data *psAppendData = NULL;
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	struct pvr_sync_append_data *psFDFenceData = NULL;

	if (i32UpdateTimelineFD >= 0 && !pi32UpdateFenceFD)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#else
	if (i32UpdateTimelineFD >= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Providing native sync timeline (%d) in non native sync enabled driver",
			__func__, i32UpdateTimelineFD));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	if (i32CheckFenceFD >= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Providing native check sync (%d) in non native sync enabled driver",
			__func__, i32CheckFenceFD));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#endif

	ui32JobId = OSAtomicIncrement(&psTransferContext->hJobId);

	/* Ensure the string is null-terminated (Required for safety) */
	szFenceName[31] = '\0';

	if (ui32SyncPMRCount != 0)
	{
		if (!ppsSyncPMRs)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	/* We can't allocate the required amount of stack space on all consumer architectures */
	psCmdHelper = OSAllocMem(sizeof(RGX_CCB_CMD_HELPER_DATA));
	if (psCmdHelper == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_allochelper;
	}


	/*
		Init the command helper commands for all the prepares
	*/
	{
		RGX_CLIENT_CCB *psClientCCB;
		RGX_SERVER_COMMON_CONTEXT *psServerCommonCtx;
		IMG_CHAR *pszCommandName;
		RGXFWIF_CCB_CMD_TYPE eType;

		psServerCommonCtx = psTransferContext->sTDMData.psServerCommonContext;
		psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
		pszCommandName = "TQ-TDM";
		eType = (ui32FWCommandSize == 0) ? RGXFWIF_CCB_CMD_TYPE_NULL : RGXFWIF_CCB_CMD_TYPE_TQ_TDM;

		eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListFence,
		                              ui32ClientFenceCount,
		                              pauiClientFenceUFOSyncPrimBlock,
		                              paui32ClientFenceSyncOffset);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntFenceUFOAddress = psTransferContext->sSyncAddrListFence.pasFWAddrs;

		eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListUpdate,
										ui32ClientUpdateCount,
										pauiClientUpdateUFOSyncPrimBlock,
										paui32ClientUpdateSyncOffset);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntUpdateUFOAddress = psTransferContext->sSyncAddrListUpdate.pasFWAddrs;


#if defined(SUPPORT_BUFFER_SYNC)
		if (ui32SyncPMRCount)
		{
			int err;

			err = pvr_buffer_sync_append_start(psDeviceNode->psBufferSyncContext,
											   ui32SyncPMRCount,
											   ppsSyncPMRs,
											   paui32SyncPMRFlags,
											   ui32IntClientFenceCount,
											   pauiIntFenceUFOAddress,
											   paui32IntFenceValue,
											   ui32IntClientUpdateCount,
											   pauiIntUpdateUFOAddress,
											   paui32IntUpdateValue,
											   &psAppendData);
			if (err)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to append buffer syncs (errno=%d)", __FUNCTION__, err));
				eError = (err == -ENOMEM) ? PVRSRV_ERROR_OUT_OF_MEMORY : PVRSRV_ERROR_INVALID_PARAMS;
				goto fail_sync_append;
			}

			pvr_buffer_sync_append_checks_get(psAppendData,
											  &ui32IntClientFenceCount,
											  &pauiIntFenceUFOAddress,
											  &paui32IntFenceValue);

			pvr_buffer_sync_append_updates_get(psAppendData,
											   &ui32IntClientUpdateCount,
											   &pauiIntUpdateUFOAddress,
											   &paui32IntUpdateValue);
		}
#endif /* defined(SUPPORT_BUFFER_SYNC) */

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	if (i32CheckFenceFD >= 0 || i32UpdateTimelineFD >= 0)
	{
		eError =
		  pvr_sync_append_fences(szFenceName,
		                               i32CheckFenceFD,
		                               i32UpdateTimelineFD,
		                               ui32IntClientUpdateCount,
		                               pauiIntUpdateUFOAddress,
		                               paui32IntUpdateValue,
		                               ui32IntClientFenceCount,
		                               pauiIntFenceUFOAddress,
		                               paui32IntFenceValue,
		                               &psFDFenceData);
		if (eError != PVRSRV_OK)
		{
			goto fail_syncinit;
		}
		pvr_sync_get_updates(psFDFenceData, &ui32IntClientUpdateCount,
			&pauiIntUpdateUFOAddress, &paui32IntUpdateValue);
		pvr_sync_get_checks(psFDFenceData, &ui32IntClientFenceCount,
			&pauiIntFenceUFOAddress, &paui32IntFenceValue);
	}
#endif

		RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psTransferContext->psDeviceNode->pvDevice,
		                          & pPreAddr,
		                          & pPostAddr,
		                          & pRMWUFOAddr);

		/*
			Create the command helper data for this command
		*/
		eError = RGXCmdHelperInitCmdCCB(psClientCCB,
		                                ui32IntClientFenceCount,
		                                pauiIntFenceUFOAddress,
		                                paui32IntFenceValue,
		                                ui32IntClientUpdateCount,
		                                pauiIntUpdateUFOAddress,
		                                paui32IntUpdateValue,
		                                ui32ServerSyncCount,
		                                paui32ServerSyncFlags,
		                                SYNC_FLAG_MASK_ALL,
		                                papsServerSyncs,
		                                ui32FWCommandSize,
		                                pui8FWCommand,
		                                & pPreAddr,
		                                & pPostAddr,
		                                & pRMWUFOAddr,
		                                eType,
		                                ui32ExtJobRef,
		                                ui32JobId,
		                                ui32PDumpFlags,
		                                NULL,
		                                pszCommandName,
		                                psCmdHelper);
		if (eError != PVRSRV_OK)
		{
			goto fail_initcmd;
		}
	}

	/*
		Acquire space for all the commands in one go
	*/
	
	eError = RGXCmdHelperAcquireCmdCCB(1, psCmdHelper);
	if (eError != PVRSRV_OK)
	{
			goto fail_3dcmdacquire;
	}


	/*
		We should acquire the kernel CCB(s) space here as the schedule could fail
		and we would have to roll back all the syncs
	*/

	/*
		Only do the command helper release (which takes the server sync
		operations if the acquire succeeded
	*/
	ui32CmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->sTDMData.psServerCommonContext));
	RGXCmdHelperReleaseCmdCCB(1,
	                          psCmdHelper,
	                          "TQ_TDM",
	                          FWCommonContextGetFWAddress(psTransferContext->sTDMData.psServerCommonContext).ui32Addr);


	/*
		Even if we failed to acquire the client CCB space we might still need
		to kick the HW to process a padding packet to release space for us next
		time round
	*/
	{
		RGXFWIF_KCCB_CMD sTDMKCCBCmd;

		/* Construct the kernel 3D CCB command. */
		sTDMKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		sTDMKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->sTDMData.psServerCommonContext);
		sTDMKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->sTDMData.psServerCommonContext));
		sTDMKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

		/* HTBLOGK(HTB_SF_MAIN_KICK_TDM, */
		/* 		s3DKCCBCmd.uCmdData.sCmdKickData.psContext, */
		/* 		ui323DCmdOffset); */
		RGX_HWPERF_HOST_ENQ(psTransferContext, OSGetCurrentClientProcessIDKM(),
		                    FWCommonContextGetFWAddress(psTransferContext->
		                    sTDMData.psServerCommonContext).ui32Addr,
		                    ui32ExtJobRef, ui32JobId, RGX_HWPERF_KICK_TYPE_TQTDM);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDeviceNode->pvDevice,
										RGXFWIF_DM_TDM,
			                            & sTDMKCCBCmd,
										sizeof(sTDMKCCBCmd),
										ui32ClientCacheOpSeqNum,
										ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUEnqueueEvent(psDeviceNode->pvDevice,
 			FWCommonContextGetFWAddress(psTransferContext->
 				sTDMData.psServerCommonContext).ui32Addr,
			ui32JobId, RGX_HWPERF_KICK_TYPE_TQTDM);
#endif
	}

	/*
	 * Now check eError (which may have returned an error from our earlier calls
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (eError != PVRSRV_OK )
	{
		goto fail_2dcmdacquire;
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
	/*
		Free the merged sync memory if required
	*/
	pvr_sync_free_append_fences_data(psFDFenceData);
#endif

#if defined(SUPPORT_BUFFER_SYNC)
	if (psAppendData)
	{
		pvr_buffer_sync_append_finish(psAppendData);
	}
#endif

	* pi32UpdateFenceFD = i32UpdateFenceFD;

	OSFreeMem(psCmdHelper);

	return PVRSRV_OK;

/*
	No resources are created in this function so there is nothing to free
	unless we had to merge syncs.
	If we fail after the client CCB acquire there is still nothing to do
	as only the client CCB release will modify the client CCB
*/
fail_2dcmdacquire:
fail_3dcmdacquire:

fail_initcmd:

/* fail_pdumpcheck: */
/* fail_cmdtype: */

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
fail_syncinit:
	/* Relocated cleanup here as the loop could fail after the first iteration
	 * at the above goto tags at which point the psFDCheckData memory would
	 * have been allocated.
	 */
	pvr_sync_rollback_append_fences(psFDFenceData);
fail_free_append_data:
	pvr_sync_free_append_fences_data(psFDFenceData);
#endif
#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_append_abort(psAppendData);
fail_sync_append:
#endif
fail_populate_sync_addr_list:
	PVR_ASSERT(eError != PVRSRV_OK);
	OSFreeMem(psCmdHelper);
fail_allochelper:
	return eError;
	

}


IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXTDMNotifyWriteOffsetUpdateKM(
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContext,
	IMG_UINT32                 ui32PDumpFlags)
{
	RGXFWIF_KCCB_CMD  sKCCBCmd;
	PVRSRV_ERROR      eError;

	/* Schedule the firmware command */
	sKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_NOTIFY_WRITE_OFFSET_UPDATE;
	sKCCBCmd.uCmdData.sWriteOffsetUpdateData.psContext = FWCommonContextGetFWAddress(psTransferContext->sTDMData.psServerCommonContext);

	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psTransferContext->psDeviceNode->pvDevice,
		                            RGXFWIF_DM_TDM,
		                            &sKCCBCmd,
		                            sizeof(sKCCBCmd),
		                            0,
		                            ui32PDumpFlags);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXTDMNotifyWriteOffsetUpdateKM: Failed to schedule the FW command %d (%s)",
				eError, PVRSRVGETERRORSTRING(eError)));
	}

	return eError;
}


PVRSRV_ERROR PVRSRVRGXTDMSetTransferContextPriorityKM(CONNECTION_DATA *psConnection,
                                                   PVRSRV_DEVICE_NODE * psDeviceNode,
												   RGX_SERVER_TQ_TDM_CONTEXT *psTransferContext,
												   IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;
	
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	
	if (psTransferContext->sTDMData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psTransferContext->sTDMData.psServerCommonContext,
									psConnection,
									psTransferContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_TDM);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			return eError;
		}
	}

	return PVRSRV_OK;
}

void CheckForStalledTDMTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	DLLIST_NODE *psNode, *psNext;

	OSWRLockAcquireRead(psDevInfo->hTDMCtxListLock);

	dllist_foreach_node(&psDevInfo->sTDMCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_TQ_TDM_CONTEXT *psCurrentServerTransferCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_TDM_CONTEXT, sListNode);

		DumpStalledFWCommonContext(psCurrentServerTransferCtx->sTDMData.psServerCommonContext,
		                           pfnDumpDebugPrintf, pvDumpDebugFile);
	}

	OSWRLockReleaseRead(psDevInfo->hTDMCtxListLock);
}


IMG_UINT32 CheckForStalledClientTDMTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_UINT32 ui32ContextBitMask = 0;

	OSWRLockAcquireRead(psDevInfo->hTDMCtxListLock);

	dllist_foreach_node(&psDevInfo->sTDMCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_TQ_TDM_CONTEXT *psCurrentServerTransferCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_TDM_CONTEXT, sListNode);

		if (CheckStalledClientCommonContext(
			             psCurrentServerTransferCtx->sTDMData.psServerCommonContext, RGX_KICK_TYPE_DM_TDM_2D)
			         == PVRSRV_ERROR_CCCB_STALLED) {
			ui32ContextBitMask = RGX_KICK_TYPE_DM_TDM_2D;
		}
	}

	OSWRLockReleaseRead(psDevInfo->hTDMCtxListLock);
	return ui32ContextBitMask;
}



/**************************************************************************//**
 End of file (rgxtdmtransfer.c)
******************************************************************************/
