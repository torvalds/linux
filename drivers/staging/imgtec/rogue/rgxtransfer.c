/*************************************************************************/ /*!
@File
@Title          Device specific transfer queue routines
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
#include "rgxtransfer.h"
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
#include "rgx_bvnc_defs_km.h"

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif

typedef struct {
	DEVMEM_MEMDESC				*psFWContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_TQ_3D_DATA;


typedef struct {
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_TQ_2D_DATA;

struct _RGX_SERVER_TQ_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	IMG_UINT32					ui32Flags;
#define RGX_SERVER_TQ_CONTEXT_FLAGS_2D		(1<<0)
#define RGX_SERVER_TQ_CONTEXT_FLAGS_3D		(1<<1)
	RGX_SERVER_TQ_3D_DATA		s3DData;
	RGX_SERVER_TQ_2D_DATA		s2DData;
	PVRSRV_CLIENT_SYNC_PRIM		*psCleanupSync;
	DLLIST_NODE					sListNode;
	ATOMIC_T			hJobId;
	IMG_UINT32			ui32PDumpFlags;
	/* per-prepare sync address lists */
	SYNC_ADDR_LIST			asSyncAddrListFence[TQ_MAX_PREPARES_PER_SUBMIT];
	SYNC_ADDR_LIST			asSyncAddrListUpdate[TQ_MAX_PREPARES_PER_SUBMIT];
};

/*
	Static functions used by transfer context code
*/
static PVRSRV_ERROR _Create3DTransferContext(CONNECTION_DATA *psConnection,
											 PVRSRV_DEVICE_NODE *psDeviceNode,
											 DEVMEM_MEMDESC *psFWMemContextMemDesc,
											 IMG_UINT32 ui32Priority,
											 RGX_COMMON_CONTEXT_INFO *psInfo,
											 RGX_SERVER_TQ_3D_DATA *ps3DData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;

	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware TQ/3D context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_3DCTX_STATE),
							RGX_FWCOMCTX_ALLOCFLAGS,
							"FwTQ3DContext",
							&ps3DData->psFWContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextswitchstate;
	}

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_TQ_3D,
									 RGXFWIF_DM_3D,
									 NULL,
									 0,
									 psFWMemContextMemDesc,
									 ps3DData->psFWContextStateMemDesc,
									 RGX_TQ3D_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &ps3DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextalloc;
	}


	PDUMPCOMMENT("Dump 3D context suspend state buffer");
	DevmemPDumpLoadMem(ps3DData->psFWContextStateMemDesc, 0, sizeof(RGXFWIF_3DCTX_STATE), PDUMP_FLAGS_CONTINUOUS);

	ps3DData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_contextalloc:
	DevmemFwFree(psDevInfo, ps3DData->psFWContextStateMemDesc);
fail_contextswitchstate:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR _Create2DTransferContext(CONNECTION_DATA *psConnection,
											 PVRSRV_DEVICE_NODE *psDeviceNode,
											 DEVMEM_MEMDESC *psFWMemContextMemDesc,
											 IMG_UINT32 ui32Priority,
											 RGX_COMMON_CONTEXT_INFO *psInfo,
											 RGX_SERVER_TQ_2D_DATA *ps2DData)
{
	PVRSRV_ERROR eError;

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_TQ_2D,
									 RGXFWIF_DM_2D,
									 NULL,
									 0,
									 psFWMemContextMemDesc,
									 NULL,
									 RGX_TQ2D_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &ps2DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextalloc;
	}

	ps2DData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_contextalloc:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR _Destroy2DTransferContext(RGX_SERVER_TQ_2D_DATA *ps2DData,
											  PVRSRV_DEVICE_NODE *psDeviceNode,
											  PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync,
											  IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  ps2DData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_2D,
											  ui32PDumpFlags);
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
	FWCommonContextFree(ps2DData->psServerCommonContext);
	ps2DData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}

static PVRSRV_ERROR _Destroy3DTransferContext(RGX_SERVER_TQ_3D_DATA *ps3DData,
											  PVRSRV_DEVICE_NODE *psDeviceNode,
											  PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync,
											  IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  ps3DData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_3D,
											  ui32PDumpFlags);
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
	DevmemFwFree(psDeviceNode->pvDevice, ps3DData->psFWContextStateMemDesc);
	FWCommonContextFree(ps3DData->psServerCommonContext);
	ps3DData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}


/*
 * PVRSRVCreateTransferContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateTransferContextKM(CONNECTION_DATA		*psConnection,
										   PVRSRV_DEVICE_NODE		*psDeviceNode,
										   IMG_UINT32				ui32Priority,
										   IMG_DEV_VIRTADDR			sMCUFenceAddr,
										   IMG_UINT32				ui32FrameworkCommandSize,
										   IMG_PBYTE				pabyFrameworkCommand,
										   IMG_HANDLE				hMemCtxPrivData,
										   RGX_SERVER_TQ_CONTEXT	**ppsTransferContext)
{
	RGX_SERVER_TQ_CONTEXT	*psTransferContext;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	DEVMEM_MEMDESC			*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO	sInfo;
	PVRSRV_ERROR			eError = PVRSRV_OK;

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

	eError = _Create3DTransferContext(psConnection,
									  psDeviceNode,
									  psFWMemContextMemDesc,
									  ui32Priority,
									  &sInfo,
									  &psTransferContext->s3DData);
	if (eError != PVRSRV_OK)
	{
		goto fail_3dtransfercontext;
	}
	psTransferContext->ui32Flags |= RGX_SERVER_TQ_CONTEXT_FLAGS_3D;

	if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK)
	{
		eError = _Create2DTransferContext(psConnection,
										  psDeviceNode,
										  psFWMemContextMemDesc,
										  ui32Priority,
										  &sInfo,
										  &psTransferContext->s2DData);
		if (eError != PVRSRV_OK)
		{
			goto fail_2dtransfercontext;
		}
		psTransferContext->ui32Flags |= RGX_SERVER_TQ_CONTEXT_FLAGS_2D;
	}

	{
		PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

		OSWRLockAcquireWrite(psDevInfo->hTransferCtxListLock);
		dllist_add_to_tail(&(psDevInfo->sTransferCtxtListHead), &(psTransferContext->sListNode));
		OSWRLockReleaseWrite(psDevInfo->hTransferCtxListLock);
		*ppsTransferContext = psTransferContext;
	}

	*ppsTransferContext = psTransferContext;
	
	return PVRSRV_OK;


fail_2dtransfercontext:
	if(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK)
	{
		_Destroy3DTransferContext(&psTransferContext->s3DData,
								  psTransferContext->psDeviceNode,
								  psTransferContext->psCleanupSync,
								  psTransferContext->ui32PDumpFlags);
	}

fail_3dtransfercontext:
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
PVRSRV_ERROR PVRSRVRGXDestroyTransferContextKM(RGX_SERVER_TQ_CONTEXT *psTransferContext)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psTransferContext->psDeviceNode->pvDevice;
	IMG_UINT32 i;

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hTransferCtxListLock);
	dllist_remove_node(&(psTransferContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hTransferCtxListLock);

	if ((psTransferContext->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) && \
			(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
	{
		eError = _Destroy2DTransferContext(&psTransferContext->s2DData,
										   psTransferContext->psDeviceNode,
										   psTransferContext->psCleanupSync,
										   PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			goto fail_destroy2d;
		}
		/* We've freed the 2D context, don't try to free it again */
		psTransferContext->ui32Flags &= ~RGX_SERVER_TQ_CONTEXT_FLAGS_2D;
	}

	if (psTransferContext->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_3D)
	{
		eError = _Destroy3DTransferContext(&psTransferContext->s3DData,
										   psTransferContext->psDeviceNode,
										   psTransferContext->psCleanupSync,
										   PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			goto fail_destroy3d;
		}
		/* We've freed the 3D context, don't try to free it again */
		psTransferContext->ui32Flags &= ~RGX_SERVER_TQ_CONTEXT_FLAGS_3D;
	}

	/* free any resources within the per-prepare UFO address stores */
	for(i = 0; i < TQ_MAX_PREPARES_PER_SUBMIT; i++)
	{
		SyncAddrListDeinit(&psTransferContext->asSyncAddrListFence[i]);
		SyncAddrListDeinit(&psTransferContext->asSyncAddrListUpdate[i]);
	}

	DevmemFwFree(psDevInfo, psTransferContext->psFWFrameworkMemDesc);
	SyncPrimFree(psTransferContext->psCleanupSync);

	OSFreeMem(psTransferContext);

	return PVRSRV_OK;

fail_destroy3d:

fail_destroy2d:
	OSWRLockAcquireWrite(psDevInfo->hTransferCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sTransferCtxtListHead), &(psTransferContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hTransferCtxListLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
 * PVRSRVSubmitTQ3DKickKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXSubmitTransferKM(RGX_SERVER_TQ_CONTEXT	*psTransferContext,
									   IMG_UINT32				ui32ClientCacheOpSeqNum,
									   IMG_UINT32				ui32PrepareCount,
									   IMG_UINT32				*paui32ClientFenceCount,
									   SYNC_PRIMITIVE_BLOCK		***papauiClientFenceUFOSyncPrimBlock,
									   IMG_UINT32				**papaui32ClientFenceSyncOffset,
									   IMG_UINT32				**papaui32ClientFenceValue,
									   IMG_UINT32				*paui32ClientUpdateCount,
									   SYNC_PRIMITIVE_BLOCK		***papauiClientUpdateUFOSyncPrimBlock,
									   IMG_UINT32				**papaui32ClientUpdateSyncOffset,
									   IMG_UINT32				**papaui32ClientUpdateValue,
									   IMG_UINT32				*paui32ServerSyncCount,
									   IMG_UINT32				**papaui32ServerSyncFlags,
									   SERVER_SYNC_PRIMITIVE	***papapsServerSyncs,
									   IMG_INT32				i32CheckFenceFD,
									   IMG_INT32				i32UpdateTimelineFD,
									   IMG_INT32				*pi32UpdateFenceFD,
									   IMG_CHAR					szFenceName[32],
									   IMG_UINT32				*paui32FWCommandSize,
									   IMG_UINT8				**papaui8FWCommand,
									   IMG_UINT32				*pui32TQPrepareFlags,
									   IMG_UINT32				ui32ExtJobRef,
									   IMG_UINT32				ui32SyncPMRCount,
									   IMG_UINT32				*paui32SyncPMRFlags,
									   PMR						**ppsSyncPMRs)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psTransferContext->psDeviceNode;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGX_CCB_CMD_HELPER_DATA *pas3DCmdHelper;
	RGX_CCB_CMD_HELPER_DATA *pas2DCmdHelper;
	IMG_UINT32 ui323DCmdCount = 0;
	IMG_UINT32 ui322DCmdCount = 0;
	IMG_UINT32 ui323DCmdOffset = 0;
	IMG_UINT32 ui322DCmdOffset = 0;
	IMG_UINT32 ui32PDumpFlags = PDUMP_FLAGS_NONE;
	IMG_UINT32 i;
	IMG_UINT32 ui32IntClientFenceCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntFenceUFOAddress = NULL;
	IMG_UINT32 *paui32IntFenceValue = NULL;
	IMG_UINT32 ui32IntClientUpdateCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntUpdateUFOAddress = NULL;
	IMG_UINT32 *paui32IntUpdateValue = NULL;
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	IMG_INT32 i32UpdateFenceFD = -1;
	IMG_UINT32 ui32JobId;

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

	if ((ui32PrepareCount == 0) || (ui32PrepareCount > TQ_MAX_PREPARES_PER_SUBMIT))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32SyncPMRCount != 0)
	{
		if (!ppsSyncPMRs)
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

#if defined(SUPPORT_BUFFER_SYNC)
		/* PMR sync is valid only when there is no batching */
		if ((ui32PrepareCount != 1))
#endif
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	if (i32CheckFenceFD >= 0 || i32UpdateTimelineFD >= 0)
	{
#if defined(SUPPORT_NATIVE_FENCE_SYNC)
		/* Fence FD's are only valid in the 3D case with no batching */
		if ((ui32PrepareCount !=1) && (!TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[0], 3D)))
		{
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

#else
		/* We only support Fence FD's if built with SUPPORT_NATIVE_FENCE_SYNC */
		return PVRSRV_ERROR_INVALID_PARAMS;
#endif
	}

	/* We can't allocate the required amount of stack space on all consumer architectures */
	pas3DCmdHelper = OSAllocMem(sizeof(*pas3DCmdHelper) * ui32PrepareCount);
	if (pas3DCmdHelper == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc3dhelper;
	}
	pas2DCmdHelper = OSAllocMem(sizeof(*pas2DCmdHelper) * ui32PrepareCount);
	if (pas2DCmdHelper == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc2dhelper;
	}

	/*
		Ensure we do the right thing for server syncs which cross call boundaries
	*/
	for (i=0;i<ui32PrepareCount;i++)
	{
		IMG_BOOL bHaveStartPrepare = pui32TQPrepareFlags[i] & TQ_PREP_FLAGS_START;
		IMG_BOOL bHaveEndPrepare = IMG_FALSE;

		if (bHaveStartPrepare)
		{
			IMG_UINT32 k;
			/*
				We've at the start of a transfer operation (which might be made
				up of multiple HW operations) so check if we also have then
				end of the transfer operation in the batch
			*/
			for (k=i;k<ui32PrepareCount;k++)
			{
				if (pui32TQPrepareFlags[k] & TQ_PREP_FLAGS_END)
				{
					bHaveEndPrepare = IMG_TRUE;
					break;
				}
			}

			if (!bHaveEndPrepare)
			{
				/*
					We don't have the complete command passed in this call
					so drop the update request. When we get called again with
					the last HW command in this transfer operation we'll do
					the update at that point.
				*/
				for (k=0;k<paui32ServerSyncCount[i];k++)
				{
					papaui32ServerSyncFlags[i][k] &= ~PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE;
				}
			}
		}
	}


	/*
		Init the command helper commands for all the prepares
	*/
	for (i=0;i<ui32PrepareCount;i++)
	{
		RGX_CLIENT_CCB *psClientCCB;
		RGX_SERVER_COMMON_CONTEXT *psServerCommonCtx;
		IMG_CHAR *pszCommandName;
		RGX_CCB_CMD_HELPER_DATA *psCmdHelper;
		RGXFWIF_CCB_CMD_TYPE eType;
		SYNC_ADDR_LIST *psSyncAddrListFence;
		SYNC_ADDR_LIST *psSyncAddrListUpdate;

		if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 3D))
		{
			psServerCommonCtx = psTransferContext->s3DData.psServerCommonContext;
			psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
			pszCommandName = "TQ-3D";
			psCmdHelper = &pas3DCmdHelper[ui323DCmdCount++];
			eType = RGXFWIF_CCB_CMD_TYPE_TQ_3D;
		}
		else if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 2D) && \
				(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
		{
			psServerCommonCtx = psTransferContext->s2DData.psServerCommonContext;
			psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
			pszCommandName = "TQ-2D";
			psCmdHelper = &pas2DCmdHelper[ui322DCmdCount++];
			eType = RGXFWIF_CCB_CMD_TYPE_TQ_2D;
		}
		else
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_cmdtype;
		}

		if (i == 0)
		{
			ui32PDumpFlags = ((pui32TQPrepareFlags[i] & TQ_PREP_FLAGS_PDUMPCONTINUOUS) != 0) ? PDUMP_FLAGS_CONTINUOUS : PDUMP_FLAGS_NONE;
			PDUMPCOMMENTWITHFLAGS(ui32PDumpFlags,
					"%s Command Server Submit on FWCtx %08x", pszCommandName, FWCommonContextGetFWAddress(psServerCommonCtx).ui32Addr);
			psTransferContext->ui32PDumpFlags |= ui32PDumpFlags;
		}
		else
		{
			IMG_UINT32 ui32NewPDumpFlags = ((pui32TQPrepareFlags[i] & TQ_PREP_FLAGS_PDUMPCONTINUOUS) != 0) ? PDUMP_FLAGS_CONTINUOUS : PDUMP_FLAGS_NONE;
			if (ui32NewPDumpFlags != ui32PDumpFlags)
			{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				PVR_DPF((PVR_DBG_ERROR, "%s: Mixing of continuous and non-continuous command in a batch is not permitted", __FUNCTION__));
				goto fail_pdumpcheck;
			}
		}

		psSyncAddrListFence = &psTransferContext->asSyncAddrListFence[i];
		ui32IntClientFenceCount  = paui32ClientFenceCount[i];
		eError = SyncAddrListPopulate(psSyncAddrListFence,
										ui32IntClientFenceCount,
										papauiClientFenceUFOSyncPrimBlock[i],
										papaui32ClientFenceSyncOffset[i]);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntFenceUFOAddress = psSyncAddrListFence->pasFWAddrs;

		paui32IntFenceValue      = papaui32ClientFenceValue[i];
		psSyncAddrListUpdate = &psTransferContext->asSyncAddrListUpdate[i];
		ui32IntClientUpdateCount = paui32ClientUpdateCount[i];
		eError = SyncAddrListPopulate(psSyncAddrListUpdate,
										ui32IntClientUpdateCount,
										papauiClientUpdateUFOSyncPrimBlock[i],
										papaui32ClientUpdateSyncOffset[i]);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntUpdateUFOAddress = psSyncAddrListUpdate->pasFWAddrs;
		paui32IntUpdateValue     = papaui32ClientUpdateValue[i];

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
		                                paui32ServerSyncCount[i],
		                                papaui32ServerSyncFlags[i],
		                                SYNC_FLAG_MASK_ALL,
		                                papapsServerSyncs[i],
		                                paui32FWCommandSize[i],
		                                papaui8FWCommand[i],
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
	if (ui323DCmdCount)
	{
		eError = RGXCmdHelperAcquireCmdCCB(ui323DCmdCount,
										   &pas3DCmdHelper[0]);
		if (eError != PVRSRV_OK)
		{
			goto fail_3dcmdacquire;
		}
	}

	if (ui322DCmdCount)
	{
		eError = RGXCmdHelperAcquireCmdCCB(ui322DCmdCount,
										   &pas2DCmdHelper[0]);
		if (eError != PVRSRV_OK)
		{
			if (ui323DCmdCount)
			{
				ui323DCmdCount = 0;
				ui322DCmdCount = 0;
			}
			else
			{
				goto fail_2dcmdacquire;
			}
		}
	}

	/*
		We should acquire the kernel CCB(s) space here as the schedule could fail
		and we would have to roll back all the syncs
	*/

	/*
		Only do the command helper release (which takes the server sync
		operations if the acquire succeeded
	*/
	if (ui323DCmdCount)
	{
		ui323DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s3DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
								  &pas3DCmdHelper[0],
								  "TQ_3D",
								  FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext).ui32Addr);
		
	}

	if ((ui322DCmdCount) && (psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
	{
		ui322DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s2DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui322DCmdCount,
								  &pas2DCmdHelper[0],
								  "TQ_2D",
								  FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext).ui32Addr);
	}

	/*
		Even if we failed to acquire the client CCB space we might still need
		to kick the HW to process a padding packet to release space for us next
		time round
	*/
	if (ui323DCmdCount)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd;
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext).ui32Addr;

		/* Construct the kernel 3D CCB command. */
		s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s3DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s3DData.psServerCommonContext));
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		s3DKCCBCmd.uCmdData.sCmdKickData.sWorkloadDataFWAddress.ui32Addr = 0;
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;
		HTBLOGK(HTB_SF_MAIN_KICK_3D,
				s3DKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui323DCmdOffset);
		RGX_HWPERF_HOST_ENQ(psTransferContext, OSGetCurrentClientProcessIDKM(),
		                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
		                    RGX_HWPERF_KICK_TYPE_TQ3D);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDeviceNode->pvDevice,
										RGXFWIF_DM_3D,
										&s3DKCCBCmd,
										sizeof(s3DKCCBCmd),
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
				ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_TQ3D);
#endif
	}

	if ((ui322DCmdCount) && (psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
	{
		RGXFWIF_KCCB_CMD s2DKCCBCmd;
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext).ui32Addr;

		/* Construct the kernel 3D CCB command. */
		s2DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s2DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext);
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s2DData.psServerCommonContext));
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

		HTBLOGK(HTB_SF_MAIN_KICK_2D,
				s2DKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui322DCmdOffset);
		RGX_HWPERF_HOST_ENQ(psTransferContext, OSGetCurrentClientProcessIDKM(),
		                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
		                    RGX_HWPERF_KICK_TYPE_TQ2D);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDeviceNode->pvDevice,
										RGXFWIF_DM_2D,
										&s2DKCCBCmd,
										sizeof(s2DKCCBCmd),
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
				ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_TQ2D);
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

	*pi32UpdateFenceFD = i32UpdateFenceFD;

	OSFreeMem(pas2DCmdHelper);
	OSFreeMem(pas3DCmdHelper);

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

fail_pdumpcheck:
fail_cmdtype:

fail_populate_sync_addr_list:
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
	PVR_ASSERT(eError != PVRSRV_OK);
	OSFreeMem(pas2DCmdHelper);
fail_alloc2dhelper:
	OSFreeMem(pas3DCmdHelper);
fail_alloc3dhelper:
	return eError;
}


PVRSRV_ERROR PVRSRVRGXSetTransferContextPriorityKM(CONNECTION_DATA *psConnection,
                                                   PVRSRV_DEVICE_NODE * psDevNode,
												   RGX_SERVER_TQ_CONTEXT *psTransferContext,
												   IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psDevNode);

	if ((psTransferContext->s2DData.ui32Priority != ui32Priority)  && \
			(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
	{
		eError = ContextSetPriority(psTransferContext->s2DData.psServerCommonContext,
									psConnection,
									psTransferContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_2D);
		if (eError != PVRSRV_OK)
		{
			if(eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 2D part of the transfercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			}
			goto fail_2dcontext;
		}
		psTransferContext->s2DData.ui32Priority = ui32Priority;
	}

	if (psTransferContext->s3DData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psTransferContext->s3DData.psServerCommonContext,
									psConnection,
									psTransferContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_3D);
		if (eError != PVRSRV_OK)
		{
			if(eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 3D part of the transfercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			}
			goto fail_3dcontext;
		}
		psTransferContext->s3DData.ui32Priority = ui32Priority;
	}
	
	return PVRSRV_OK;

fail_3dcontext:

fail_2dcontext:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void CheckForStalledTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	DLLIST_NODE *psNode, *psNext;

	OSWRLockAcquireRead(psDevInfo->hTransferCtxListLock);

	dllist_foreach_node(&psDevInfo->sTransferCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_TQ_CONTEXT *psCurrentServerTransferCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_CONTEXT, sListNode);

		if ((psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) && \
				(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
		{
			DumpStalledFWCommonContext(psCurrentServerTransferCtx->s2DData.psServerCommonContext,
									   pfnDumpDebugPrintf, pvDumpDebugFile);
		}

		if (psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_3D)
		{
			DumpStalledFWCommonContext(psCurrentServerTransferCtx->s3DData.psServerCommonContext,
									   pfnDumpDebugPrintf, pvDumpDebugFile);
		}
	}

	OSWRLockReleaseRead(psDevInfo->hTransferCtxListLock);
}

IMG_UINT32 CheckForStalledClientTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_UINT32 ui32ContextBitMask = 0;

	OSWRLockAcquireRead(psDevInfo->hTransferCtxListLock);

	dllist_foreach_node(&psDevInfo->sTransferCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_TQ_CONTEXT *psCurrentServerTransferCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_CONTEXT, sListNode);

		if ((psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) && \
				(NULL != psCurrentServerTransferCtx->s2DData.psServerCommonContext) && \
				(psDevInfo->sDevFeatureCfg.ui64Features & RGX_FEATURE_TLA_BIT_MASK))
		{
			if (CheckStalledClientCommonContext(psCurrentServerTransferCtx->s2DData.psServerCommonContext, RGX_KICK_TYPE_DM_TQ2D) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_TQ2D;
			}
		}

		if ((psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_3D) && (NULL != psCurrentServerTransferCtx->s3DData.psServerCommonContext))
		{
			if ((CheckStalledClientCommonContext(psCurrentServerTransferCtx->s3DData.psServerCommonContext, RGX_KICK_TYPE_DM_TQ3D) == PVRSRV_ERROR_CCCB_STALLED))
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_TQ3D;
			}
		}
	}

	OSWRLockReleaseRead(psDevInfo->hTransferCtxListLock);
	return ui32ContextBitMask;
}

/**************************************************************************//**
 End of file (rgxtransfer.c)
******************************************************************************/
