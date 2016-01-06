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
#include "rgxccb.h"
#include "rgxtimerquery.h"
#include "rgxhwperf.h"
#include "rgxsync.h"

#include "sync_server.h"
#include "sync_internal.h"

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
	SYNC_ADDR_LIST			sSyncAddrListFence;
	SYNC_ADDR_LIST			sSyncAddrListUpdate;
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
							"FirmwareTQ3DContext",
							&ps3DData->psFWContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextswitchstate;
	}

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 "TQ_3D",
									 IMG_NULL,
									 0,
									 psFWMemContextMemDesc,
									 ps3DData->psFWContextStateMemDesc,
									 RGX_CCB_SIZE_LOG2,
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
	DevmemFwFree(ps3DData->psFWContextStateMemDesc);
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
									 "TQ_2D",
									 IMG_NULL,
									 0,
									 psFWMemContextMemDesc,
									 IMG_NULL,
									 RGX_CCB_SIZE_LOG2,
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
											  PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  FWCommonContextGetFWAddress(ps2DData->psServerCommonContext),
											  psCleanupSync,
											  RGXFWIF_DM_2D);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				 __FUNCTION__,
				 PVRSRVGetErrorStringKM(eError)));
	}

	/* ... it has so we can free it's resources */
	FWCommonContextFree(ps2DData->psServerCommonContext);
	return PVRSRV_OK;
}

static PVRSRV_ERROR _Destroy3DTransferContext(RGX_SERVER_TQ_3D_DATA *ps3DData,
											  PVRSRV_DEVICE_NODE *psDeviceNode,
											  PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  FWCommonContextGetFWAddress(ps3DData->psServerCommonContext),
											  psCleanupSync,
											  RGXFWIF_DM_3D);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				 __FUNCTION__,
				 PVRSRVGetErrorStringKM(eError)));
	}

	/* ... it has so we can free it's resources */
	DevmemFwFree(ps3DData->psFWContextStateMemDesc);
	FWCommonContextFree(ps3DData->psServerCommonContext);

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
	DEVMEM_MEMDESC			*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO	sInfo;
	PVRSRV_ERROR			eError = PVRSRV_OK;

	/* Allocate the server side structure */
	*ppsTransferContext = IMG_NULL;
	psTransferContext = OSAllocMem(sizeof(*psTransferContext));
	if (psTransferContext == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psTransferContext, 0, sizeof(*psTransferContext));

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

	SyncAddrListInit(&psTransferContext->sSyncAddrListFence);
	SyncAddrListInit(&psTransferContext->sSyncAddrListUpdate);

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
	_Destroy3DTransferContext(&psTransferContext->s3DData,
							  psTransferContext->psDeviceNode,
							  psTransferContext->psCleanupSync);
fail_3dtransfercontext:
fail_frameworkcopy:
	DevmemFwFree(psTransferContext->psFWFrameworkMemDesc);
fail_frameworkcreate:
	SyncPrimFree(psTransferContext->psCleanupSync);
fail_syncalloc:
	OSFreeMem(psTransferContext);
	PVR_ASSERT(eError != PVRSRV_OK);
	*ppsTransferContext = IMG_NULL;
	return eError;
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyTransferContextKM(RGX_SERVER_TQ_CONTEXT *psTransferContext)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psTransferContext->psDeviceNode->pvDevice;

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hTransferCtxListLock);
	dllist_remove_node(&(psTransferContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hTransferCtxListLock);

	if (psTransferContext->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D)
	{
		eError = _Destroy2DTransferContext(&psTransferContext->s2DData,
										   psTransferContext->psDeviceNode,
										   psTransferContext->psCleanupSync);
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
										   psTransferContext->psCleanupSync);
		if (eError != PVRSRV_OK)
		{
			goto fail_destroy3d;
		}
		/* We've freed the 3D context, don't try to free it again */
		psTransferContext->ui32Flags &= ~RGX_SERVER_TQ_CONTEXT_FLAGS_3D;
	}

	DevmemFwFree(psTransferContext->psFWFrameworkMemDesc);
	SyncPrimFree(psTransferContext->psCleanupSync);

	SyncAddrListDeinit(&psTransferContext->sSyncAddrListFence);
	SyncAddrListDeinit(&psTransferContext->sSyncAddrListUpdate);

	OSFreeMem(psTransferContext);

	return PVRSRV_OK;

fail_destroy2d:
fail_destroy3d:
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
									   IMG_UINT32				ui32NumCheckFenceFDs,
									   IMG_INT32				*pai32CheckFenceFDs,
									   IMG_INT32				i32UpdateFenceFD,
									   IMG_UINT32				*paui32FWCommandSize,
									   IMG_UINT8				**papaui8FWCommand,
									   IMG_UINT32				*pui32TQPrepareFlags,
									   IMG_UINT32				ui32ExtJobRef,
									   IMG_UINT32				ui32IntJobRef)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psTransferContext->psDeviceNode;
	RGX_CCB_CMD_HELPER_DATA *pas3DCmdHelper;
	RGX_CCB_CMD_HELPER_DATA *pas2DCmdHelper;
	IMG_UINT32 ui323DCmdCount = 0;
	IMG_UINT32 ui322DCmdCount = 0;
	IMG_BOOL bKick2D = IMG_FALSE;
	IMG_BOOL bKick3D = IMG_FALSE;
	IMG_BOOL bPDumpContinuous = IMG_FALSE;
	IMG_UINT32 i;
	IMG_UINT32 ui32IntClientFenceCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntFenceUFOAddress = IMG_NULL;
	IMG_UINT32 *paui32IntFenceValue = IMG_NULL;
	IMG_UINT32 ui32IntClientUpdateCount = 0;
	PRGXFWIF_UFO_ADDR *pauiIntUpdateUFOAddress = IMG_NULL;
	IMG_UINT32 *paui32IntUpdateValue = IMG_NULL;
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;

	RGXFWIF_DEV_VIRTADDR pPreTimestamp;
	RGXFWIF_DEV_VIRTADDR pPostTimestamp;
	PRGXFWIF_UFO_ADDR    pRMWUFOAddr;


#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	struct pvr_sync_append_data *psFDFenceData = NULL;
#endif

	if (ui32PrepareCount == 0)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NumCheckFenceFDs != 0 || i32UpdateFenceFD >= 0)
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
	if (pas3DCmdHelper == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc3dhelper;
	}
	pas2DCmdHelper = OSAllocMem(sizeof(*pas2DCmdHelper) * ui32PrepareCount);
	if (pas2DCmdHelper == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_alloc2dhelper;
	}

	/*
		Ensure we do the right thing for server syncs which cross call bounderies
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

		if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 3D))
		{
			psServerCommonCtx = psTransferContext->s3DData.psServerCommonContext;
			psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
			pszCommandName = "TQ-3D";
			psCmdHelper = &pas3DCmdHelper[ui323DCmdCount++];
			eType = RGXFWIF_CCB_CMD_TYPE_TQ_3D;
		}
		else if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 2D))
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
			bPDumpContinuous = ((pui32TQPrepareFlags[i] & TQ_PREP_FLAGS_PDUMPCONTINUOUS) == TQ_PREP_FLAGS_PDUMPCONTINUOUS);
			PDUMPCOMMENTWITHFLAGS((bPDumpContinuous) ? PDUMP_FLAGS_CONTINUOUS : 0,
					"%s Command Server Submit on FWCtx %08x", pszCommandName, FWCommonContextGetFWAddress(psServerCommonCtx).ui32Addr);
		}
		else
		{
			IMG_BOOL bNewPDumpContinuous = ((pui32TQPrepareFlags[i] & TQ_PREP_FLAGS_PDUMPCONTINUOUS) == TQ_PREP_FLAGS_PDUMPCONTINUOUS);

			if (bNewPDumpContinuous != bPDumpContinuous)
			{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				PVR_DPF((PVR_DBG_ERROR, "%s: Mixing of continuous and non-continuous command in a batch is not permitted", __FUNCTION__));
				goto fail_pdumpcheck;
			}
		}

		ui32IntClientFenceCount  = paui32ClientFenceCount[i];
		eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListFence,
										ui32IntClientFenceCount,
										papauiClientFenceUFOSyncPrimBlock[i],
										papaui32ClientFenceSyncOffset[i]);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntFenceUFOAddress = psTransferContext->sSyncAddrListFence.pasFWAddrs;

		paui32IntFenceValue      = papaui32ClientFenceValue[i];
		ui32IntClientUpdateCount = paui32ClientUpdateCount[i];
		eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListUpdate,
										ui32IntClientUpdateCount,
										papauiClientUpdateUFOSyncPrimBlock[i],
										papaui32ClientUpdateSyncOffset[i]);
		if(eError != PVRSRV_OK)
		{
			goto fail_populate_sync_addr_list;
		}
		pauiIntUpdateUFOAddress = psTransferContext->sSyncAddrListUpdate.pasFWAddrs;
		paui32IntUpdateValue     = papaui32ClientUpdateValue[i];

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	if (ui32NumCheckFenceFDs || i32UpdateFenceFD >= 0)
	{
		eError =
		  pvr_sync_append_fences("TQ",
		                               ui32NumCheckFenceFDs,
		                               pai32CheckFenceFDs,
		                               i32UpdateFenceFD,
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
		                          & pPreTimestamp,
		                          & pPostTimestamp,
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
		                                papapsServerSyncs[i],
		                                paui32FWCommandSize[i],
		                                papaui8FWCommand[i],
		                                & pPreTimestamp,
		                                & pPostTimestamp,
		                                & pRMWUFOAddr,
		                                eType,
		                                bPDumpContinuous,
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
										   &pas3DCmdHelper[0],
										   &bKick3D);
		if (eError != PVRSRV_OK)
		{
			if (bKick3D)
			{
				ui323DCmdCount = 0;
				ui322DCmdCount = 0;
			}
			else
			{
				goto fail_3dcmdacquire;
			}
		}
	}

	if (ui322DCmdCount)
	{
		eError = RGXCmdHelperAcquireCmdCCB(ui322DCmdCount,
										   &pas2DCmdHelper[0],
										   &bKick2D);
	
		if (eError != PVRSRV_OK)
		{
			if (bKick2D || bKick3D)
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
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
								  &pas3DCmdHelper[0],
								  "TQ_3D",
								  FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext).ui32Addr);
		
	}

	if (ui322DCmdCount)
	{
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
	if (bKick3D)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd;

		/* Construct the kernel 3D CCB command. */
		s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s3DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s3DData.psServerCommonContext));
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDeviceNode->pvDevice,
										RGXFWIF_DM_3D,
										&s3DKCCBCmd,
										sizeof(s3DKCCBCmd),
										bPDumpContinuous);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
        	RGXHWPerfFTraceGPUEnqueueEvent(psDeviceNode->pvDevice,
        			ui32ExtJobRef, ui32IntJobRef, "TQ3D");
#endif
	}

	if (bKick2D)
	{
		RGXFWIF_KCCB_CMD s2DKCCBCmd;

		/* Construct the kernel 3D CCB command. */
		s2DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s2DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext);
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s2DData.psServerCommonContext));
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDeviceNode->pvDevice,
										RGXFWIF_DM_2D,
										&s2DKCCBCmd,
										sizeof(s2DKCCBCmd),
										bPDumpContinuous);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
        	RGXHWPerfFTraceGPUEnqueueEvent(psDeviceNode->pvDevice,
        			ui32ExtJobRef, ui32IntJobRef, "TQ2D");
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
#if defined(NO_HARDWARE)
	pvr_sync_nohw_complete_fences(psFDFenceData);
#endif
	/*
		Free the merged sync memory if required
	*/
	pvr_sync_free_append_fences_data(psFDFenceData);
#endif

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

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
fail_syncinit:
	/* Relocated cleanup here as the loop could fail after the first iteration
	 * at the above goto tags at which point the psFDCheckData memory would
	 * have been allocated.
	 */
	if (psFDFenceData)
	{
		pvr_sync_rollback_append_fences(psFDFenceData);
		pvr_sync_free_append_fences_data(psFDFenceData);
		psFDFenceData = NULL;
	}
#endif
fail_populate_sync_addr_list:
	PVR_ASSERT(eError != PVRSRV_OK);
	OSFreeMem(pas2DCmdHelper);
fail_alloc2dhelper:
	OSFreeMem(pas3DCmdHelper);
fail_alloc3dhelper:
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetTransferContextPriorityKM(CONNECTION_DATA *psConnection,
												   RGX_SERVER_TQ_CONTEXT *psTransferContext,
												   IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;

	if (psTransferContext->s2DData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psTransferContext->s2DData.psServerCommonContext,
									psConnection,
									psTransferContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_2D);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 2D part of the transfercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
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
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 3D part of the transfercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
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

static IMG_BOOL CheckForStalledTransferCtxtCommand(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	RGX_SERVER_TQ_CONTEXT 		*psCurrentServerTransferCtx = IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_CONTEXT, sListNode);
	RGX_SERVER_TQ_2D_DATA		*psTransferCtx2DData = &(psCurrentServerTransferCtx->s2DData);
	RGX_SERVER_COMMON_CONTEXT	*psCurrentServerTQ2DCommonCtx = psTransferCtx2DData->psServerCommonContext;
	RGX_SERVER_TQ_3D_DATA		*psTransferCtx3DData = &(psCurrentServerTransferCtx->s3DData);
	RGX_SERVER_COMMON_CONTEXT	*psCurrentServerTQ3DCommonCtx = psTransferCtx3DData->psServerCommonContext;
	DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf = pvCallbackData;


	DumpStalledFWCommonContext(psCurrentServerTQ2DCommonCtx, pfnDumpDebugPrintf);
	DumpStalledFWCommonContext(psCurrentServerTQ3DCommonCtx, pfnDumpDebugPrintf);

	return IMG_TRUE;
}
IMG_VOID CheckForStalledTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
									 DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf)
{
	OSWRLockAcquireRead(psDevInfo->hTransferCtxListLock);
	dllist_foreach_node(&(psDevInfo->sTransferCtxtListHead),
						CheckForStalledTransferCtxtCommand, pfnDumpDebugPrintf);
	OSWRLockReleaseRead(psDevInfo->hTransferCtxListLock);
}

static IMG_BOOL CheckForStalledClientTransferCtxtCommand(PDLLIST_NODE psNode, IMG_PVOID pvCallbackData)
{
	PVRSRV_ERROR *peError = (PVRSRV_ERROR*)pvCallbackData;
	RGX_SERVER_TQ_CONTEXT 		*psCurrentServerTransferCtx = IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_CONTEXT, sListNode);
	RGX_SERVER_TQ_2D_DATA		*psTransferCtx2DData = &(psCurrentServerTransferCtx->s2DData);
	RGX_SERVER_COMMON_CONTEXT	*psCurrentServerTQ2DCommonCtx = psTransferCtx2DData->psServerCommonContext;
	RGX_SERVER_TQ_3D_DATA		*psTransferCtx3DData = &(psCurrentServerTransferCtx->s3DData);
	RGX_SERVER_COMMON_CONTEXT	*psCurrentServerTQ3DCommonCtx = psTransferCtx3DData->psServerCommonContext;

	if (PVRSRV_ERROR_CCCB_STALLED == CheckStalledClientCommonContext(psCurrentServerTQ2DCommonCtx))
	{
		*peError = PVRSRV_ERROR_CCCB_STALLED;
	}
	if (PVRSRV_ERROR_CCCB_STALLED == CheckStalledClientCommonContext(psCurrentServerTQ3DCommonCtx))
	{
		*peError = PVRSRV_ERROR_CCCB_STALLED;
	}

	return IMG_TRUE;
}
IMG_BOOL CheckForStalledClientTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	OSWRLockAcquireRead(psDevInfo->hTransferCtxListLock);
	dllist_foreach_node(&(psDevInfo->sTransferCtxtListHead), 
						CheckForStalledClientTransferCtxtCommand, &eError);
	OSWRLockReleaseRead(psDevInfo->hTransferCtxListLock);
	return (PVRSRV_ERROR_CCCB_STALLED == eError)? IMG_TRUE: IMG_FALSE;
}

PVRSRV_ERROR PVRSRVRGXKickSyncTransferKM(RGX_SERVER_TQ_CONTEXT	*psTransferContext,
									   IMG_UINT32				ui32ClientFenceCount,
									   SYNC_PRIMITIVE_BLOCK		**pauiClientFenceUFOSyncPrimBlock,
									   IMG_UINT32				*paui32ClientFenceSyncOffset,
									   IMG_UINT32				*paui32ClientFenceValue,
									   IMG_UINT32				ui32ClientUpdateCount,
									   SYNC_PRIMITIVE_BLOCK		**pauiClientUpdateUFOSyncPrimBlock,
									   IMG_UINT32				*paui32ClientUpdateSyncOffset,
									   IMG_UINT32				*paui32ClientUpdateValue,
									   IMG_UINT32				ui32ServerSyncCount,
									   IMG_UINT32				*pui32ServerSyncFlags,
									   SERVER_SYNC_PRIMITIVE	**pasServerSyncs,
									   IMG_UINT32				ui32NumCheckFenceFDs,
									   IMG_INT32				*pai32CheckFenceFDs,
									   IMG_INT32				i32UpdateFenceFD,
									   IMG_UINT32				ui32TQPrepareFlags)
{
	PVRSRV_ERROR                eError;
	RGX_SERVER_COMMON_CONTEXT   *psServerCommonCtx;
	IMG_CHAR                    *pszCommandName;
	RGXFWIF_DM                  eDM;
	IMG_BOOL                    bPDumpContinuous;

	PRGXFWIF_UFO_ADDR *pauiClientFenceUFOAddress;
	PRGXFWIF_UFO_ADDR *pauiClientUpdateUFOAddress;

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* Android fd sync update info */
	struct pvr_sync_append_data *psFDFenceData = NULL;
#endif

	eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListFence,
									ui32ClientFenceCount,
									pauiClientFenceUFOSyncPrimBlock,
									paui32ClientFenceSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClientFenceUFOAddress = psTransferContext->sSyncAddrListFence.pasFWAddrs;

	eError = SyncAddrListPopulate(&psTransferContext->sSyncAddrListUpdate,
									ui32ClientUpdateCount,
									pauiClientUpdateUFOSyncPrimBlock,
									paui32ClientUpdateSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClientUpdateUFOAddress = psTransferContext->sSyncAddrListUpdate.pasFWAddrs;


	bPDumpContinuous = ((ui32TQPrepareFlags & TQ_PREP_FLAGS_PDUMPCONTINUOUS) == TQ_PREP_FLAGS_PDUMPCONTINUOUS);

	if (TQ_PREP_FLAGS_COMMAND_IS(ui32TQPrepareFlags, 3D))
	{
		psServerCommonCtx = psTransferContext->s3DData.psServerCommonContext;
		pszCommandName = "SyncTQ-3D";
		eDM = RGXFWIF_DM_3D;
	}
	else if (TQ_PREP_FLAGS_COMMAND_IS(ui32TQPrepareFlags, 2D))
	{
		psServerCommonCtx = psTransferContext->s2DData.psServerCommonContext;
		pszCommandName = "SyncTQ-2D";
		eDM = RGXFWIF_DM_2D;
	}
	else
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* Android FD fences are hardcoded to updates (IMG_TRUE below), Fences go to the TA and updates to the 3D */
	if (ui32NumCheckFenceFDs || i32UpdateFenceFD >= 0)
	{
		eError =
		  pvr_sync_append_fences("TQ",
		                         ui32NumCheckFenceFDs,
		                         pai32CheckFenceFDs,
		                         i32UpdateFenceFD,
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

	eError = 
		RGXKickSyncKM(psTransferContext->psDeviceNode,
				      psServerCommonCtx,
				      eDM,
				      pszCommandName,
				      ui32ClientFenceCount,
				      pauiClientFenceUFOAddress,
				      paui32ClientFenceValue,
				      ui32ClientUpdateCount,
				      pauiClientUpdateUFOAddress,
				      paui32ClientUpdateValue,
				      ui32ServerSyncCount,
				      pui32ServerSyncFlags,
				      pasServerSyncs,
				      bPDumpContinuous);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error calling RGXKickSyncKM (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
		goto fail_kicksync;
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#if defined(NO_HARDWARE)
	pvr_sync_nohw_complete_fences(psFDFenceData);
#endif
	pvr_sync_free_append_fences_data(psFDFenceData);
#endif

	return eError;

fail_kicksync:
#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_rollback_append_fences(psFDFenceData);
	pvr_sync_free_append_fences_data(psFDFenceData);
fail_fdsync:
#endif
err_populate_sync_addr_list:
	return eError;
}

/**************************************************************************//**
 End of file (rgxtransfer.c)
******************************************************************************/
