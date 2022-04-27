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
#include "rgxhwperf.h"
#include "ospvr_gputrace.h"
#include "htbuffer.h"
#include "rgxshader.h"

#include "pdump_km.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"
#include "rgx_bvnc_defs_km.h"

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"

#include "rgxtimerquery.h"

/* Enable this to dump the compiled list of UFOs prior to kick call */
#define ENABLE_TQ_UFO_DUMP	0

//#define TRANSFER_CHECKPOINT_DEBUG 1

#if defined(TRANSFER_CHECKPOINT_DEBUG)
#define CHKPT_DBG(X) PVR_DPF(X)
#else
#define CHKPT_DBG(X)
#endif

typedef struct {
	DEVMEM_MEMDESC				*psFWContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_context *psBufferSyncContext;
#endif
} RGX_SERVER_TQ_3D_DATA;

typedef struct {
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_context *psBufferSyncContext;
#endif
} RGX_SERVER_TQ_2D_DATA;

struct _RGX_SERVER_TQ_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	DEVMEM_MEMDESC              *psFWTransferContextMemDesc;
	IMG_UINT32					ui32Flags;
#define RGX_SERVER_TQ_CONTEXT_FLAGS_2D		(1<<0)
#define RGX_SERVER_TQ_CONTEXT_FLAGS_3D		(1<<1)
	RGX_SERVER_TQ_3D_DATA		s3DData;
	RGX_SERVER_TQ_2D_DATA		s2DData;
	DLLIST_NODE					sListNode;
	ATOMIC_T			hIntJobRef;
	IMG_UINT32			ui32PDumpFlags;
	/* per-prepare sync address lists */
	SYNC_ADDR_LIST			asSyncAddrListFence[TQ_MAX_PREPARES_PER_SUBMIT];
	SYNC_ADDR_LIST			asSyncAddrListUpdate[TQ_MAX_PREPARES_PER_SUBMIT];
	POS_LOCK				hLock;
};

/*
	Static functions used by transfer context code
*/
static PVRSRV_ERROR _Create3DTransferContext(CONNECTION_DATA *psConnection,
											 PVRSRV_DEVICE_NODE *psDeviceNode,
											 DEVMEM_MEMDESC *psAllocatedMemDesc,
											 IMG_UINT32 ui32AllocatedOffset,
											 DEVMEM_MEMDESC *psFWMemContextMemDesc,
											 IMG_UINT32 ui32Priority,
											 RGX_COMMON_CONTEXT_INFO *psInfo,
											 RGX_SERVER_TQ_3D_DATA *ps3DData,
											 IMG_UINT32 ui32CCBAllocSizeLog2,
											 IMG_UINT32 ui32CCBMaxAllocSizeLog2,
											 IMG_UINT32 ui32ContextFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;
	IMG_UINT	ui3DRegISPStateStoreSize = 0;
	IMG_UINT	uiNumISPStoreRegs = 1; /* default value 1 expected */
	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware TQ/3D context suspend state");

	if (!RGX_IS_FEATURE_SUPPORTED(psDevInfo, XE_MEMORY_HIERARCHY))
	{
		uiNumISPStoreRegs = psDeviceNode->pfnGetDeviceFeatureValue(psDeviceNode,
													RGX_FEATURE_NUM_ISP_IPP_PIPES_IDX);
	}

	/* Calculate the size of the 3DCTX ISP state */
	ui3DRegISPStateStoreSize = sizeof(RGXFWIF_3DCTX_STATE) +
			uiNumISPStoreRegs * sizeof(((RGXFWIF_3DCTX_STATE *)0)->au3DReg_ISP_STORE[0]);

#if defined(SUPPORT_BUFFER_SYNC)
	ps3DData->psBufferSyncContext =
		pvr_buffer_sync_context_create(psDeviceNode->psDevConfig->pvOSDevice,
									   "rogue-tq3d");
	if (IS_ERR(ps3DData->psBufferSyncContext))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: failed to create buffer_sync context (err=%ld)",
				 __func__, PTR_ERR(ps3DData->psBufferSyncContext)));

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_buffer_sync_context_create;
	}
#endif

	eError = DevmemFwAllocate(psDevInfo,
							ui3DRegISPStateStoreSize,
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
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
	                                 psFWMemContextMemDesc,
	                                 ps3DData->psFWContextStateMemDesc,
	                                 ui32CCBAllocSizeLog2 ? ui32CCBAllocSizeLog2 : RGX_TQ3D_CCB_SIZE_LOG2,
	                                 ui32CCBMaxAllocSizeLog2 ? ui32CCBMaxAllocSizeLog2 : RGX_TQ3D_CCB_MAX_SIZE_LOG2,
	                                 ui32ContextFlags,
	                                 ui32Priority,
	                                 UINT_MAX, /* max deadline MS */
	                                 0, /* robustness address */
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
	DevmemFwUnmapAndFree(psDevInfo, ps3DData->psFWContextStateMemDesc);
fail_contextswitchstate:
#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_context_destroy(ps3DData->psBufferSyncContext);
	ps3DData->psBufferSyncContext = NULL;
fail_buffer_sync_context_create:
#endif
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR _Create2DTransferContext(CONNECTION_DATA *psConnection,
											 PVRSRV_DEVICE_NODE *psDeviceNode,
											 DEVMEM_MEMDESC *psFWMemContextMemDesc,
											 IMG_UINT32 ui32Priority,
											 RGX_COMMON_CONTEXT_INFO *psInfo,
											 RGX_SERVER_TQ_2D_DATA *ps2DData,
											 IMG_UINT32 ui32CCBAllocSizeLog2,
											 IMG_UINT32 ui32CCBMaxAllocSizeLog2,
											 IMG_UINT32 ui32ContextFlags)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_BUFFER_SYNC)
	ps2DData->psBufferSyncContext =
		pvr_buffer_sync_context_create(psDeviceNode->psDevConfig->pvOSDevice,
									   "rogue-tqtla");
	if (IS_ERR(ps2DData->psBufferSyncContext))
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: failed to create buffer_sync context (err=%ld)",
				 __func__, PTR_ERR(ps2DData->psBufferSyncContext)));

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_buffer_sync_context_create;
	}
#endif

	eError = FWCommonContextAllocate(psConnection,
	                                 psDeviceNode,
	                                 REQ_TYPE_TQ_2D,
	                                 RGXFWIF_DM_2D,
									 NULL,
	                                 NULL,
	                                 0,
	                                 psFWMemContextMemDesc,
	                                 NULL,
	                                 ui32CCBAllocSizeLog2 ? ui32CCBAllocSizeLog2 : RGX_TQ2D_CCB_SIZE_LOG2,
	                                 ui32CCBMaxAllocSizeLog2 ? ui32CCBMaxAllocSizeLog2 : RGX_TQ2D_CCB_MAX_SIZE_LOG2,
	                                 ui32ContextFlags,
	                                 ui32Priority,
	                                 UINT_MAX, /* max deadline MS */
	                                 0, /* robustness address */
	                                 psInfo,
	                                 &ps2DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_contextalloc;
	}

	ps2DData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_contextalloc:
#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_context_destroy(ps2DData->psBufferSyncContext);
	ps2DData->psBufferSyncContext = NULL;
fail_buffer_sync_context_create:
#endif
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR _Destroy2DTransferContext(RGX_SERVER_TQ_2D_DATA *ps2DData,
											  PVRSRV_DEVICE_NODE *psDeviceNode,
											  IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  ps2DData->psServerCommonContext,
											  RGXFWIF_DM_2D,
											  ui32PDumpFlags);
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

	/* ... it has so we can free it's resources */
	FWCommonContextFree(ps2DData->psServerCommonContext);
	ps2DData->psServerCommonContext = NULL;

#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_context_destroy(ps2DData->psBufferSyncContext);
	ps2DData->psBufferSyncContext = NULL;
#endif

	return PVRSRV_OK;
}

static PVRSRV_ERROR _Destroy3DTransferContext(RGX_SERVER_TQ_3D_DATA *ps3DData,
											  PVRSRV_DEVICE_NODE *psDeviceNode,
											  IMG_UINT32 ui32PDumpFlags)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  ps3DData->psServerCommonContext,
											  RGXFWIF_DM_3D,
											  ui32PDumpFlags);
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

	/* ... it has so we can free it's resources */
	DevmemFwUnmapAndFree(psDeviceNode->pvDevice, ps3DData->psFWContextStateMemDesc);
	FWCommonContextFree(ps3DData->psServerCommonContext);
	ps3DData->psServerCommonContext = NULL;

#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_context_destroy(ps3DData->psBufferSyncContext);
	ps3DData->psBufferSyncContext = NULL;
#endif

	return PVRSRV_OK;
}


/*
 * PVRSRVCreateTransferContextKM
 */
PVRSRV_ERROR PVRSRVRGXCreateTransferContextKM(CONNECTION_DATA		*psConnection,
										   PVRSRV_DEVICE_NODE		*psDeviceNode,
										   IMG_UINT32				ui32Priority,
										   IMG_UINT32				ui32FrameworkCommandSize,
										   IMG_PBYTE				pabyFrameworkCommand,
										   IMG_HANDLE				hMemCtxPrivData,
										   IMG_UINT32				ui32PackedCCBSizeU8888,
										   IMG_UINT32				ui32ContextFlags,
										   RGX_SERVER_TQ_CONTEXT	**ppsTransferContext,
										   PMR						**ppsCLIPMRMem,
										   PMR						**ppsUSCPMRMem)
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

	/*
		Create the FW transfer context, this has the TQ common
		context embedded within it
	 */
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(RGXFWIF_FWTRANSFERCONTEXT),
			RGX_FWCOMCTX_ALLOCFLAGS,
			"FwTransferContext",
			&psTransferContext->psFWTransferContextMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_fwtransfercontext;
	}

	eError = OSLockCreate(&psTransferContext->hLock);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create lock (%s)",
									__func__,
									PVRSRVGetErrorString(eError)));
		goto fail_createlock;
	}

	psTransferContext->psDeviceNode = psDeviceNode;

	if (ui32FrameworkCommandSize)
	{
		/*
		 * Create the FW framework buffer
		 */
		eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode,
				&psTransferContext->psFWFrameworkMemDesc,
				ui32FrameworkCommandSize);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to allocate firmware GPU framework state (%s)",
						__func__,
						PVRSRVGetErrorString(eError)));
			goto fail_frameworkcreate;
		}

		/* Copy the Framework client data into the framework buffer */
		eError = PVRSRVRGXFrameworkCopyCommand(psTransferContext->psFWFrameworkMemDesc,
				pabyFrameworkCommand,
				ui32FrameworkCommandSize);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to populate the framework buffer (%s)",
						__func__,
				PVRSRVGetErrorString(eError)));
			goto fail_frameworkcopy;
		}

		sInfo.psFWFrameworkMemDesc = psTransferContext->psFWFrameworkMemDesc;
	}

	eError = _Create3DTransferContext(psConnection,
									  psDeviceNode,
									  psTransferContext->psFWTransferContextMemDesc,
									  offsetof(RGXFWIF_FWTRANSFERCONTEXT, sTQContext),
									  psFWMemContextMemDesc,
									  ui32Priority,
									  &sInfo,
									  &psTransferContext->s3DData,
									  U32toU8_Unpack3(ui32PackedCCBSizeU8888),
									  U32toU8_Unpack4(ui32PackedCCBSizeU8888),
									  ui32ContextFlags);
	if (eError != PVRSRV_OK)
	{
		goto fail_3dtransfercontext;
	}
	psTransferContext->ui32Flags |= RGX_SERVER_TQ_CONTEXT_FLAGS_3D;

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA))
	{
		eError = _Create2DTransferContext(psConnection,
										  psDeviceNode,
										  psFWMemContextMemDesc,
										  ui32Priority,
										  &sInfo,
										  &psTransferContext->s2DData,
										  U32toU8_Unpack1(ui32PackedCCBSizeU8888),
										  U32toU8_Unpack2(ui32PackedCCBSizeU8888),
										  ui32ContextFlags);
		if (eError != PVRSRV_OK)
		{
			goto fail_2dtransfercontext;
		}
		psTransferContext->ui32Flags |= RGX_SERVER_TQ_CONTEXT_FLAGS_2D;
	}

	PVRSRVTQAcquireShaders(psDeviceNode, ppsCLIPMRMem, ppsUSCPMRMem);

	{
		PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

		OSWRLockAcquireWrite(psDevInfo->hTransferCtxListLock);
		dllist_add_to_tail(&(psDevInfo->sTransferCtxtListHead), &(psTransferContext->sListNode));
		OSWRLockReleaseWrite(psDevInfo->hTransferCtxListLock);
		*ppsTransferContext = psTransferContext;
	}

	return PVRSRV_OK;

fail_2dtransfercontext:
	_Destroy3DTransferContext(&psTransferContext->s3DData,
							  psTransferContext->psDeviceNode,
							  psTransferContext->ui32PDumpFlags);
fail_3dtransfercontext:
fail_frameworkcopy:
	if (psTransferContext->psFWFrameworkMemDesc)
	{
		DevmemFwUnmapAndFree(psDevInfo, psTransferContext->psFWFrameworkMemDesc);
	}
fail_frameworkcreate:
	OSLockDestroy(psTransferContext->hLock);
fail_createlock:
	DevmemFwUnmapAndFree(psDevInfo, psTransferContext->psFWTransferContextMemDesc);
fail_fwtransfercontext:
	OSFreeMem(psTransferContext);
	PVR_ASSERT(eError != PVRSRV_OK);
	*ppsTransferContext = NULL;
	return eError;
}

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

	if ((psTransferContext->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) &&
			(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
	{
		eError = _Destroy2DTransferContext(&psTransferContext->s2DData,
										   psTransferContext->psDeviceNode,
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
										   PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			goto fail_destroy3d;
		}
		/* We've freed the 3D context, don't try to free it again */
		psTransferContext->ui32Flags &= ~RGX_SERVER_TQ_CONTEXT_FLAGS_3D;
	}

	/* free any resources within the per-prepare UFO address stores */
	for (i = 0; i < TQ_MAX_PREPARES_PER_SUBMIT; i++)
	{
		SyncAddrListDeinit(&psTransferContext->asSyncAddrListFence[i]);
		SyncAddrListDeinit(&psTransferContext->asSyncAddrListUpdate[i]);
	}

	if (psTransferContext->psFWFrameworkMemDesc)
	{
		DevmemFwUnmapAndFree(psDevInfo, psTransferContext->psFWFrameworkMemDesc);
	}

	DevmemFwUnmapAndFree(psDevInfo, psTransferContext->psFWTransferContextMemDesc);

	OSLockDestroy(psTransferContext->hLock);

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
PVRSRV_ERROR PVRSRVRGXSubmitTransferKM(RGX_SERVER_TQ_CONTEXT	*psTransferContext,
									   IMG_UINT32				ui32ClientCacheOpSeqNum,
									   IMG_UINT32				ui32PrepareCount,
									   IMG_UINT32				*paui32ClientUpdateCount,
									   SYNC_PRIMITIVE_BLOCK		***papauiClientUpdateUFODevVarBlock,
									   IMG_UINT32				**papaui32ClientUpdateSyncOffset,
									   IMG_UINT32				**papaui32ClientUpdateValue,
									   PVRSRV_FENCE				iCheckFence,
									   PVRSRV_TIMELINE			i2DUpdateTimeline,
									   PVRSRV_FENCE				*pi2DUpdateFence,
									   PVRSRV_TIMELINE			i3DUpdateTimeline,
									   PVRSRV_FENCE				*pi3DUpdateFence,
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
	IMG_UINT32 ui323DCmdLast = 0;
	IMG_UINT32 ui322DCmdLast = 0;
	IMG_UINT32 ui323DCmdOffset = 0;
	IMG_UINT32 ui322DCmdOffset = 0;
	IMG_UINT32 ui32PDumpFlags = PDUMP_FLAGS_NONE;
	IMG_UINT32 i;
	IMG_UINT64 uiCheckFenceUID = 0;
	IMG_UINT64 ui2DUpdateFenceUID = 0;
	IMG_UINT64 ui3DUpdateFenceUID = 0;

	PSYNC_CHECKPOINT ps2DUpdateSyncCheckpoint = NULL;
	PSYNC_CHECKPOINT ps3DUpdateSyncCheckpoint = NULL;
	PSYNC_CHECKPOINT *apsFenceSyncCheckpoints = NULL;
	IMG_UINT32 ui32FenceSyncCheckpointCount = 0;
	IMG_UINT32 *pui322DIntAllocatedUpdateValues = NULL;
	IMG_UINT32 *pui323DIntAllocatedUpdateValues = NULL;
	PVRSRV_CLIENT_SYNC_PRIM *ps2DFenceTimelineUpdateSync = NULL;
	PVRSRV_CLIENT_SYNC_PRIM *ps3DFenceTimelineUpdateSync = NULL;
	IMG_UINT32 ui322DFenceTimelineUpdateValue = 0;
	IMG_UINT32 ui323DFenceTimelineUpdateValue = 0;
	void *pv2DUpdateFenceFinaliseData = NULL;
	void *pv3DUpdateFenceFinaliseData = NULL;
#if defined(SUPPORT_BUFFER_SYNC)
	PSYNC_CHECKPOINT psBufferUpdateSyncCheckpoint = NULL;
	struct pvr_buffer_sync_append_data *psBufferSyncData = NULL;
	PSYNC_CHECKPOINT *apsBufferFenceSyncCheckpoints = NULL;
	IMG_UINT32 ui32BufferFenceSyncCheckpointCount = 0;
#endif /* defined(SUPPORT_BUFFER_SYNC) */

	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_ERROR eError2;
	PVRSRV_FENCE i2DUpdateFence = PVRSRV_NO_FENCE;
	PVRSRV_FENCE i3DUpdateFence = PVRSRV_NO_FENCE;
	IMG_UINT32   ui32IntJobRef = OSAtomicIncrement(&psDevInfo->iCCBSubmissionOrdinal);
	IMG_UINT32   ui32PreparesDone = 0;


	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;

	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psDeviceNode->pvDevice,
	                          &pPreAddr,
	                          &pPostAddr,
	                          &pRMWUFOAddr);

	if (i2DUpdateTimeline != PVRSRV_NO_TIMELINE && !pi2DUpdateFence)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	if (i3DUpdateTimeline != PVRSRV_NO_TIMELINE && !pi3DUpdateFence)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Validate sync prim fence/update value ptrs
	 * for each prepare.
	 */
	{
		IMG_UINT32 ui32Prepare;
		IMG_UINT32 *pui32UpdateCount = paui32ClientUpdateCount;
		IMG_UINT32 **papui32UpdateValue = papaui32ClientUpdateValue;

		/* Check that we have not been given a null ptr for
		 * update count parameters.
		 */
		PVR_LOG_RETURN_IF_FALSE((paui32ClientUpdateCount != NULL),
		                        "paui32ClientUpdateCount NULL",
		                        PVRSRV_ERROR_INVALID_PARAMS);

		for (ui32Prepare=0; ui32Prepare<ui32PrepareCount; ui32Prepare++)
		{
			/* Ensure we haven't been given a null ptr to
			 * update values if we have been told we
			 * have updates for this prepare
			 */
			if (*pui32UpdateCount > 0)
			{
				PVR_LOG_RETURN_IF_FALSE(*papui32UpdateValue != NULL,
				                        "paui32ClientUpdateValue NULL but "
				                        "ui32ClientUpdateCount > 0",
				                        PVRSRV_ERROR_INVALID_PARAMS);
			}
			/* Advance local ptr to update values ptr for next prepare. */
			papui32UpdateValue++;
			/* Advance local ptr to update count for next prepare. */
			pui32UpdateCount++;
		}
	}

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

	OSLockAcquire(psTransferContext->hLock);

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

	if (iCheckFence != PVRSRV_NO_FENCE)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointResolveFence (iCheckFence=%d), psDeviceNode->hSyncCheckpointContext=<%p>...", __func__, iCheckFence, (void*)psDeviceNode->hSyncCheckpointContext));
		/* Resolve the sync checkpoints that make up the input fence */
		eError = SyncCheckpointResolveFence(psDeviceNode->hSyncCheckpointContext,
											iCheckFence,
											&ui32FenceSyncCheckpointCount,
											&apsFenceSyncCheckpoints,
											&uiCheckFenceUID,
											ui32PDumpFlags);
		if (eError != PVRSRV_OK)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, returned ERROR (eError=%d)", __func__, eError));
			goto fail_resolve_fencesync_input_fence;
		}
		CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, fence %d contained %d checkpoints (apsFenceSyncCheckpoints=<%p>)", __func__, iCheckFence, ui32FenceSyncCheckpointCount, (void*)apsFenceSyncCheckpoints));
#if defined(TRANSFER_CHECKPOINT_DEBUG)
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
	}
	/*
		Ensure we do the right thing for server syncs which cross call boundaries
	*/
	for (i=0;i<ui32PrepareCount;i++)
	{
		if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 3D))
		{
			ui323DCmdLast++;
		} else if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 2D) &&
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
		{
			ui322DCmdLast++;
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
		PRGXFWIF_UFO_ADDR *pauiIntFenceUFOAddress = NULL;
		PRGXFWIF_UFO_ADDR *pauiIntUpdateUFOAddress = NULL;
		SYNC_ADDR_LIST *psSyncAddrListFence = &psTransferContext->asSyncAddrListFence[i];
		SYNC_ADDR_LIST *psSyncAddrListUpdate = &psTransferContext->asSyncAddrListUpdate[i];
		IMG_UINT32 ui32IntClientFenceCount = 0U;
		IMG_UINT32 ui32IntClientUpdateCount = paui32ClientUpdateCount[i];
		IMG_UINT32 *paui32IntUpdateValue = papaui32ClientUpdateValue[i];
#if defined(SUPPORT_BUFFER_SYNC)
		struct pvr_buffer_sync_context *psBufferSyncContext;
#endif

		PVRSRV_FENCE *piUpdateFence = NULL;
		PVRSRV_TIMELINE	iUpdateTimeline = PVRSRV_NO_TIMELINE;
		void **ppvUpdateFenceFinaliseData = NULL;
		PSYNC_CHECKPOINT * ppsUpdateSyncCheckpoint = NULL;
		PVRSRV_CLIENT_SYNC_PRIM **ppsFenceTimelineUpdateSync = NULL;
		IMG_UINT32 *pui32FenceTimelineUpdateValue = NULL;
		IMG_UINT32 **ppui32IntAllocatedUpdateValues = NULL;
		IMG_BOOL bCheckFence = IMG_FALSE;
		IMG_BOOL bUpdateFence = IMG_FALSE;
		IMG_UINT64 *puiUpdateFenceUID = NULL;

		IMG_BOOL bCCBStateOpen = IMG_FALSE;

		if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 3D))
		{
			psServerCommonCtx = psTransferContext->s3DData.psServerCommonContext;
			psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
			pszCommandName = "TQ-3D";
			psCmdHelper = &pas3DCmdHelper[ui323DCmdCount++];
			eType = RGXFWIF_CCB_CMD_TYPE_TQ_3D;
#if defined(SUPPORT_BUFFER_SYNC)
			psBufferSyncContext = psTransferContext->s3DData.psBufferSyncContext;
#endif
			bCheckFence = ui323DCmdCount == 1;
			bUpdateFence = ui323DCmdCount == ui323DCmdLast
				&& i3DUpdateTimeline != PVRSRV_NO_TIMELINE;

			if (bUpdateFence)
			{
				piUpdateFence = &i3DUpdateFence;
				iUpdateTimeline = i3DUpdateTimeline;
				ppvUpdateFenceFinaliseData = &pv3DUpdateFenceFinaliseData;
				ppsUpdateSyncCheckpoint = &ps3DUpdateSyncCheckpoint;
				ppsFenceTimelineUpdateSync = &ps3DFenceTimelineUpdateSync;
				pui32FenceTimelineUpdateValue = &ui323DFenceTimelineUpdateValue;
				ppui32IntAllocatedUpdateValues = &pui323DIntAllocatedUpdateValues;
				puiUpdateFenceUID = &ui3DUpdateFenceUID;
			}
		}
		else if (TQ_PREP_FLAGS_COMMAND_IS(pui32TQPrepareFlags[i], 2D) &&
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
		{
			psServerCommonCtx = psTransferContext->s2DData.psServerCommonContext;
			psClientCCB = FWCommonContextGetClientCCB(psServerCommonCtx);
			pszCommandName = "TQ-2D";
			psCmdHelper = &pas2DCmdHelper[ui322DCmdCount++];
			eType = RGXFWIF_CCB_CMD_TYPE_TQ_2D;
#if defined(SUPPORT_BUFFER_SYNC)
			psBufferSyncContext = psTransferContext->s2DData.psBufferSyncContext;
#endif
			bCheckFence = ui322DCmdCount == 1;
			bUpdateFence = ui322DCmdCount == ui322DCmdLast
				&& i2DUpdateTimeline != PVRSRV_NO_TIMELINE;

			if (bUpdateFence)
			{
				piUpdateFence = &i2DUpdateFence;
				iUpdateTimeline = i2DUpdateTimeline;
				ppvUpdateFenceFinaliseData = &pv2DUpdateFenceFinaliseData;
				ppsUpdateSyncCheckpoint = &ps2DUpdateSyncCheckpoint;
				ppsFenceTimelineUpdateSync = &ps2DFenceTimelineUpdateSync;
				pui32FenceTimelineUpdateValue = &ui322DFenceTimelineUpdateValue;
				ppui32IntAllocatedUpdateValues = &pui322DIntAllocatedUpdateValues;
				puiUpdateFenceUID = &ui2DUpdateFenceUID;
			}
		}
		else
		{
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_prepare_loop;
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
				PVR_DPF((PVR_DBG_ERROR, "%s: Mixing of continuous and non-continuous command in a batch is not permitted", __func__));
				goto fail_prepare_loop;
			}
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s: SyncAddrListPopulate(psTransferContext->sSyncAddrListFence, %d fences)", __func__, ui32IntClientFenceCount));
		eError = SyncAddrListPopulate(psSyncAddrListFence,
										0,
										NULL,
										NULL);
		if (eError != PVRSRV_OK)
		{
			goto fail_prepare_loop;
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s: SyncAddrListPopulate(psTransferContext->asSyncAddrListUpdate[], %d updates)", __func__, ui32IntClientUpdateCount));
		eError = SyncAddrListPopulate(psSyncAddrListUpdate,
										ui32IntClientUpdateCount,
										papauiClientUpdateUFODevVarBlock[i],
										papaui32ClientUpdateSyncOffset[i]);
		if (eError != PVRSRV_OK)
		{
			goto fail_prepare_loop;
		}
		if (!pauiIntUpdateUFOAddress)
		{
			pauiIntUpdateUFOAddress = psSyncAddrListUpdate->pasFWAddrs;
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s:   (after sync prims) ui32IntClientUpdateCount=%d", __func__, ui32IntClientUpdateCount));
		if (ui32SyncPMRCount)
		{
#if defined(SUPPORT_BUFFER_SYNC)
			int err;

			CHKPT_DBG((PVR_DBG_ERROR, "%s:   Calling pvr_buffer_sync_resolve_and_create_fences", __func__));
			err = pvr_buffer_sync_resolve_and_create_fences(psBufferSyncContext,
			                                                psTransferContext->psDeviceNode->hSyncCheckpointContext,
			                                                ui32SyncPMRCount,
			                                                ppsSyncPMRs,
			                                                paui32SyncPMRFlags,
			                                                &ui32BufferFenceSyncCheckpointCount,
			                                                &apsBufferFenceSyncCheckpoints,
			                                                &psBufferUpdateSyncCheckpoint,
			                                                &psBufferSyncData);
			if (err)
			{
				switch (err)
				{
					case -EINTR:
						eError = PVRSRV_ERROR_RETRY;
						break;
					case -ENOMEM:
						eError = PVRSRV_ERROR_OUT_OF_MEMORY;
						break;
					default:
						eError = PVRSRV_ERROR_INVALID_PARAMS;
						break;
				}

				if (eError != PVRSRV_ERROR_RETRY)
				{
					PVR_DPF((PVR_DBG_ERROR, "%s:   pvr_buffer_sync_resolve_and_create_fences failed (%s)", __func__, PVRSRVGetErrorString(eError)));
				}
				goto fail_resolve_buffersync_input_fence;
			}

			/* Append buffer sync fences */
			if (ui32BufferFenceSyncCheckpointCount > 0)
			{
				CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append %d buffer sync checkpoints to TQ Fence (psSyncAddrListFence=<%p>, pauiIntFenceUFOAddress=<%p>)...", __func__, ui32BufferFenceSyncCheckpointCount, (void*)psSyncAddrListFence , (void*)pauiIntFenceUFOAddress));
				SyncAddrListAppendAndDeRefCheckpoints(psSyncAddrListFence,
													  ui32BufferFenceSyncCheckpointCount,
													  apsBufferFenceSyncCheckpoints);
				if (!pauiIntFenceUFOAddress)
				{
					pauiIntFenceUFOAddress = psSyncAddrListFence->pasFWAddrs;
				}
				ui32IntClientFenceCount += ui32BufferFenceSyncCheckpointCount;
			}

			if (psBufferUpdateSyncCheckpoint)
			{
				CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append 1 buffer sync checkpoint<%p> to TQ Update (&psTransferContext->asSyncAddrListUpdate[i]=<%p>, pauiIntUpdateUFOAddress=<%p>)...", __func__, (void*)psBufferUpdateSyncCheckpoint, (void*)psSyncAddrListUpdate , (void*)pauiIntUpdateUFOAddress));
				/* Append the update (from output fence) */
				SyncAddrListAppendCheckpoints(psSyncAddrListUpdate,
											  1,
											  &psBufferUpdateSyncCheckpoint);
				if (!pauiIntUpdateUFOAddress)
				{
					pauiIntUpdateUFOAddress = psSyncAddrListUpdate->pasFWAddrs;
				}
				ui32IntClientUpdateCount++;
			}
			CHKPT_DBG((PVR_DBG_ERROR, "%s:   (after buffer_sync) ui32IntClientFenceCount=%d, ui32IntClientUpdateCount=%d", __func__, ui32IntClientFenceCount, ui32IntClientUpdateCount));
#else /* defined(SUPPORT_BUFFER_SYNC) */
			PVR_DPF((PVR_DBG_ERROR, "%s: Buffer sync not supported but got %u buffers", __func__, ui32SyncPMRCount));
			PVR_DPF((PVR_DBG_ERROR, "%s:   <--EXIT(%d)", __func__, PVRSRV_ERROR_INVALID_PARAMS));
			OSLockRelease(psTransferContext->hLock);
			return PVRSRV_ERROR_INVALID_PARAMS;
#endif /* defined(SUPPORT_BUFFER_SYNC) */
		}

		/* Create the output fence (if required) */
		if (bUpdateFence)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointCreateFence (piUpdateFence=%p, iUpdateTimeline=%d,  psTranserContext->psDeviceNode->hSyncCheckpointContext=<%p>)", __func__, piUpdateFence, iUpdateTimeline, (void*)psDeviceNode->hSyncCheckpointContext));
			eError = SyncCheckpointCreateFence(psDeviceNode,
			                                   szFenceName,
			                                   iUpdateTimeline,
			                                   psDeviceNode->hSyncCheckpointContext,
			                                   piUpdateFence,
			                                   puiUpdateFenceUID,
			                                   ppvUpdateFenceFinaliseData,
			                                   ppsUpdateSyncCheckpoint,
			                                   (void*)ppsFenceTimelineUpdateSync,
			                                   pui32FenceTimelineUpdateValue,
			                                   ui32PDumpFlags);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s:   SyncCheckpointCreateFence failed (%s)",
						__func__,
						PVRSRVGetErrorString(eError)));
				goto fail_prepare_loop;
			}

			CHKPT_DBG((PVR_DBG_ERROR, "%s: returned from SyncCheckpointCreateFence (piUpdateFence=%p)", __func__, piUpdateFence));

			/* Append the sync prim update for the timeline (if required) */
			if (*ppsFenceTimelineUpdateSync)
			{
				IMG_UINT32 *pui32TimelineUpdateWp = NULL;

				/* Allocate memory to hold the list of update values (including our timeline update) */
				*ppui32IntAllocatedUpdateValues = OSAllocMem(sizeof(**ppui32IntAllocatedUpdateValues) * (ui32IntClientUpdateCount+1));
				if (!*ppui32IntAllocatedUpdateValues)
				{
					/* Failed to allocate memory */
					eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					goto fail_prepare_loop;
				}
				OSCachedMemSet(*ppui32IntAllocatedUpdateValues, 0xbb, sizeof(**ppui32IntAllocatedUpdateValues) * (ui32IntClientUpdateCount+1));
#if defined(SUPPORT_BUFFER_SYNC)
				if (psBufferUpdateSyncCheckpoint)
				{
					/* Copy the update values into the new memory, then append our timeline update value */
					OSCachedMemCopy(*ppui32IntAllocatedUpdateValues, paui32IntUpdateValue, sizeof(**ppui32IntAllocatedUpdateValues) * (ui32IntClientUpdateCount-1));
					pui32TimelineUpdateWp = *ppui32IntAllocatedUpdateValues + (ui32IntClientUpdateCount-1);
				}
				else
#endif
				{
					/* Copy the update values into the new memory, then append our timeline update value */
					OSCachedMemCopy(*ppui32IntAllocatedUpdateValues, paui32IntUpdateValue, sizeof(**ppui32IntAllocatedUpdateValues) * ui32IntClientUpdateCount);
					pui32TimelineUpdateWp = *ppui32IntAllocatedUpdateValues + ui32IntClientUpdateCount;
				}
				CHKPT_DBG((PVR_DBG_ERROR, "%s: Appending the additional update value 0x%x)", __func__, *pui32FenceTimelineUpdateValue));
				/* Now set the additional update value */
				*pui32TimelineUpdateWp = *pui32FenceTimelineUpdateValue;
#if defined(TRANSFER_CHECKPOINT_DEBUG)
				if (ui32IntClientUpdateCount > 0)
				{
					IMG_UINT32 iii;
					IMG_UINT32 *pui32Tmp = (IMG_UINT32*)*ppui32IntAllocatedUpdateValues;

					for (iii=0; iii<ui32IntClientUpdateCount; iii++)
					{
						CHKPT_DBG((PVR_DBG_ERROR, "%s: *ppui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
						pui32Tmp++;
					}
				}
#endif
				/* Now append the timeline sync prim addr to the transfer context update list */
				SyncAddrListAppendSyncPrim(psSyncAddrListUpdate,
				                           *ppsFenceTimelineUpdateSync);
				ui32IntClientUpdateCount++;
#if defined(TRANSFER_CHECKPOINT_DEBUG)
				if (ui32IntClientUpdateCount > 0)
				{
					IMG_UINT32 iii;
					IMG_UINT32 *pui32Tmp = (IMG_UINT32*)*ppui32IntAllocatedUpdateValues;

					for (iii=0; iii<ui32IntClientUpdateCount; iii++)
					{
						CHKPT_DBG((PVR_DBG_ERROR, "%s: *ppui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
						pui32Tmp++;
					}
				}
#endif
				/* Ensure paui32IntUpdateValue is now pointing to our new array of update values */
				CHKPT_DBG((PVR_DBG_ERROR, "%s: set paui32IntUpdateValue<%p> to point to *ppui32IntAllocatedUpdateValues<%p>", __func__, (void*)paui32IntUpdateValue, (void*)*ppui32IntAllocatedUpdateValues));
				paui32IntUpdateValue = *ppui32IntAllocatedUpdateValues;
			}
		}

		if (bCheckFence && ui32FenceSyncCheckpointCount)
		{
			/* Append the checks (from input fence) */
			if (ui32FenceSyncCheckpointCount > 0)
			{
				CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append %d sync checkpoints to TQ Fence (psSyncAddrListFence=<%p>)...", __func__, ui32FenceSyncCheckpointCount, (void*)psSyncAddrListFence));
				SyncAddrListAppendCheckpoints(psSyncAddrListFence,
											  ui32FenceSyncCheckpointCount,
											  apsFenceSyncCheckpoints);
				if (!pauiIntFenceUFOAddress)
				{
					pauiIntFenceUFOAddress = psSyncAddrListFence->pasFWAddrs;
				}
				ui32IntClientFenceCount += ui32FenceSyncCheckpointCount;
			}
#if defined(TRANSFER_CHECKPOINT_DEBUG)
			if (ui32IntClientFenceCount > 0)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiIntFenceUFOAddress;

				for (iii=0; iii<ui32IntClientFenceCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR, "%s: psSyncAddrListFence->pasFWAddrs[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
		}
		if (bUpdateFence && *ppsUpdateSyncCheckpoint)
		{
			/* Append the update (from output fence) */
			CHKPT_DBG((PVR_DBG_ERROR, "%s:   Append 1 sync checkpoint to TQ Update (psSyncAddrListUpdate=<%p>, pauiIntUpdateUFOAddress=<%p>)...", __func__, (void*)&psTransferContext->asSyncAddrListUpdate , (void*)pauiIntUpdateUFOAddress));
			SyncAddrListAppendCheckpoints(psSyncAddrListUpdate,
										  1,
										  ppsUpdateSyncCheckpoint);
			if (!pauiIntUpdateUFOAddress)
			{
				pauiIntUpdateUFOAddress = psSyncAddrListUpdate->pasFWAddrs;
			}
			ui32IntClientUpdateCount++;
#if defined(TRANSFER_CHECKPOINT_DEBUG)
			{
				IMG_UINT32 iii;
				IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pauiIntUpdateUFOAddress;

				for (iii=0; iii<ui32IntClientUpdateCount; iii++)
				{
					CHKPT_DBG((PVR_DBG_ERROR, "%s: pauiIntUpdateUFOAddress[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
					pui32Tmp++;
				}
			}
#endif
		}
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   (after pvr_sync) ui32IntClientFenceCount=%d, ui32IntClientUpdateCount=%d", __func__, ui32IntClientFenceCount, ui32IntClientUpdateCount));

#if (ENABLE_TQ_UFO_DUMP == 1)
		PVR_DPF((PVR_DBG_ERROR, "%s: dumping TQ fence/updates syncs...", __func__));
		{
			IMG_UINT32 ii;
			PRGXFWIF_UFO_ADDR *psTmpIntFenceUFOAddress = pauiIntFenceUFOAddress;
			PRGXFWIF_UFO_ADDR *psTmpIntUpdateUFOAddress = pauiIntUpdateUFOAddress;
			IMG_UINT32 *pui32TmpIntUpdateValue = paui32IntUpdateValue;

			/* Dump Fence syncs and Update syncs */
			PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d TQ fence syncs (&psTransferContext->asSyncAddrListFence=<%p>, pauiIntFenceUFOAddress=<%p>):", __func__, ui32IntClientFenceCount, (void*)&psTransferContext->asSyncAddrListFence, (void*)pauiIntFenceUFOAddress));
			for (ii=0; ii<ui32IntClientFenceCount; ii++)
			{
				PVR_ASSERT(psTmpIntFenceUFOAddress->ui32Addr & 0x1);
				PVR_DPF((PVR_DBG_ERROR, "%s:   %d/%d<%p>. FWAddr=0x%x, CheckValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED", __func__, ii+1, ui32IntClientFenceCount, (void*)psTmpIntFenceUFOAddress, psTmpIntFenceUFOAddress->ui32Addr));

				psTmpIntFenceUFOAddress++;
			}
			PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d TQ update syncs (&psTransferContext->asSyncAddrListUpdate=<%p>, pauiIntUpdateUFOAddress=<%p>):", __func__, ui32IntClientUpdateCount, (void*)&psTransferContext->asSyncAddrListUpdate, (void*)pauiIntUpdateUFOAddress));
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

		ui32PreparesDone++;

		/*
			Create the command helper data for this command
		*/
		RGXCmdHelperInitCmdCCB(psClientCCB,
		                       0,
		                       ui32IntClientFenceCount,
		                       pauiIntFenceUFOAddress,
		                       NULL, /* fence value */
		                       ui32IntClientUpdateCount,
		                       pauiIntUpdateUFOAddress,
		                       paui32IntUpdateValue,
		                       paui32FWCommandSize[i],
		                       papaui8FWCommand[i],
							   &pPreAddr,
							   &pPostAddr,
							   &pRMWUFOAddr,
		                       eType,
		                       ui32ExtJobRef,
		                       ui32IntJobRef,
		                       ui32PDumpFlags,
		                       NULL,
		                       pszCommandName,
		                       bCCBStateOpen,
		                       psCmdHelper);
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
			goto fail_cmdacquire;
		}
	}

	if (ui322DCmdCount)
	{
		eError = RGXCmdHelperAcquireCmdCCB(ui322DCmdCount,
										   &pas2DCmdHelper[0]);
		if (eError != PVRSRV_OK)
		{
			goto fail_cmdacquire;
		}
	}

	/*
		We should acquire the kernel CCB(s) space here as the schedule could fail
		and we would have to roll back all the syncs
	*/

	if (ui323DCmdCount)
	{
		ui323DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s3DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
								  &pas3DCmdHelper[0],
								  "TQ_3D",
								  FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext).ui32Addr);
	}

	if ((ui322DCmdCount) && (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
	{
		ui322DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psTransferContext->s2DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui322DCmdCount,
								  &pas2DCmdHelper[0],
								  "TQ_2D",
								  FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext).ui32Addr);
	}

	if (ui323DCmdCount)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd;
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext).ui32Addr;
		RGX_CLIENT_CCB *ps3DTQCCB = FWCommonContextGetClientCCB(psTransferContext->s3DData.psServerCommonContext);

		RGX_CCB_CMD_HELPER_DATA *psCmdHelper = &pas3DCmdHelper[ui323DCmdCount];
		CMD_COMMON *psTransferCmdCmn = IMG_OFFSET_ADDR(psCmdHelper->pui8DMCmd, 0);

		/* Construct the kernel 3D CCB command. */
		s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s3DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s3DData.psServerCommonContext);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(ps3DTQCCB);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(ps3DTQCCB);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;

		HTBLOGK(HTB_SF_MAIN_KICK_3D,
				s3DKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui323DCmdOffset,
				psTransferCmdCmn->ui32FrameNum,
				ui32ExtJobRef,
				ui32IntJobRef
				);

		RGXSRV_HWPERF_ENQ(psTransferContext,
		                  OSGetCurrentClientProcessIDKM(),
		                  ui32FWCtx,
		                  ui32ExtJobRef,
		                  ui32IntJobRef,
		                  RGX_HWPERF_KICK_TYPE_TQ3D,
		                  iCheckFence,
		                  i3DUpdateFence,
		                  i3DUpdateTimeline,
		                  uiCheckFenceUID,
		                  ui3DUpdateFenceUID,
		                  NO_DEADLINE,
		                  NO_CYCEST);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_3D,
										&s3DKCCBCmd,
										ui32ClientCacheOpSeqNum,
										ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

		PVRGpuTraceEnqueueEvent(psDeviceNode, ui32FWCtx, ui32ExtJobRef,
		                        ui32IntJobRef, RGX_HWPERF_KICK_TYPE_TQ3D);
	}

	if ((ui322DCmdCount) && (RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
	{
		RGXFWIF_KCCB_CMD s2DKCCBCmd;
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext).ui32Addr;
		RGX_CLIENT_CCB *ps2DTQCCB = FWCommonContextGetClientCCB(psTransferContext->s2DData.psServerCommonContext);
		RGX_CCB_CMD_HELPER_DATA *psCmdHelper = &pas2DCmdHelper[ui322DCmdCount];
		CMD_COMMON *psTransferCmdCmn = IMG_OFFSET_ADDR(psCmdHelper->pui8DMCmd, 0);

		/* Construct the kernel 2D CCB command. */
		s2DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s2DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psTransferContext->s2DData.psServerCommonContext);
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(ps2DTQCCB);
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(ps2DTQCCB);
		s2DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

		HTBLOGK(HTB_SF_MAIN_KICK_2D,
				s2DKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui322DCmdOffset,
				psTransferCmdCmn->ui32FrameNum,
				ui32ExtJobRef,
				ui32IntJobRef);

		RGXSRV_HWPERF_ENQ(psTransferContext,
		                  OSGetCurrentClientProcessIDKM(),
		                  ui32FWCtx,
		                  ui32ExtJobRef,
		                  ui32IntJobRef,
		                  RGX_HWPERF_KICK_TYPE_TQ2D,
		                  iCheckFence,
		                  i2DUpdateFence,
		                  i2DUpdateTimeline,
		                  uiCheckFenceUID,
		                  ui2DUpdateFenceUID,
		                  NO_DEADLINE,
		                  NO_CYCEST);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_2D,
										&s2DKCCBCmd,
										ui32ClientCacheOpSeqNum,
										ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

		PVRGpuTraceEnqueueEvent(psDeviceNode, ui32FWCtx, ui32ExtJobRef,
		                        ui32IntJobRef, RGX_HWPERF_KICK_TYPE_TQ2D);
	}

	/*
	 * Now check eError (which may have returned an error from our earlier calls
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (eError != PVRSRV_OK )
	{
		goto fail_cmdacquire;
	}

#if defined(NO_HARDWARE)
	/* If NO_HARDWARE, signal the output fence's sync checkpoint and sync prim */
	if (ps2DUpdateSyncCheckpoint)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Signalling TLA NOHW sync checkpoint<%p>, ID:%d, FwAddr=0x%x", __func__, (void*)ps2DUpdateSyncCheckpoint, SyncCheckpointGetId(ps2DUpdateSyncCheckpoint), SyncCheckpointGetFirmwareAddr(ps2DUpdateSyncCheckpoint)));
		SyncCheckpointSignalNoHW(ps2DUpdateSyncCheckpoint);
	}
	if (ps2DFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Updating TLA NOHW sync prim<%p> to %d", __func__, (void*)ps2DFenceTimelineUpdateSync, ui322DFenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(ps2DFenceTimelineUpdateSync, ui322DFenceTimelineUpdateValue);
	}
	if (ps3DUpdateSyncCheckpoint)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Signalling TQ3D NOHW sync checkpoint<%p>, ID:%d, FwAddr=0x%x", __func__, (void*)ps3DUpdateSyncCheckpoint, SyncCheckpointGetId(ps3DUpdateSyncCheckpoint), SyncCheckpointGetFirmwareAddr(ps3DUpdateSyncCheckpoint)));
		SyncCheckpointSignalNoHW(ps3DUpdateSyncCheckpoint);
	}
	if (ps3DFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Updating TQ3D NOHW sync prim<%p> to %d", __func__, (void*)ps3DFenceTimelineUpdateSync, ui323DFenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(ps3DFenceTimelineUpdateSync, ui323DFenceTimelineUpdateValue);
	}
	SyncCheckpointNoHWUpdateTimelines(NULL);
#endif /* defined(NO_HARDWARE) */

#if defined(SUPPORT_BUFFER_SYNC)
	if (psBufferSyncData)
	{
		pvr_buffer_sync_kick_succeeded(psBufferSyncData);
	}
	if (apsBufferFenceSyncCheckpoints)
	{
		kfree(apsBufferFenceSyncCheckpoints);
	}
#endif /* defined(SUPPORT_BUFFER_SYNC) */

	if (pi2DUpdateFence)
	{
		*pi2DUpdateFence = i2DUpdateFence;
	}
	if (pi3DUpdateFence)
	{
		*pi3DUpdateFence = i3DUpdateFence;
	}
	if (pv2DUpdateFenceFinaliseData && (i2DUpdateFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psDeviceNode, i2DUpdateFence, pv2DUpdateFenceFinaliseData,
		                            ps2DUpdateSyncCheckpoint, szFenceName);
	}
	if (pv3DUpdateFenceFinaliseData && (i3DUpdateFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psDeviceNode, i3DUpdateFence, pv3DUpdateFenceFinaliseData,
		                            ps3DUpdateSyncCheckpoint, szFenceName);
	}

	OSFreeMem(pas2DCmdHelper);
	OSFreeMem(pas3DCmdHelper);

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
	if (pui322DIntAllocatedUpdateValues)
	{
		OSFreeMem(pui322DIntAllocatedUpdateValues);
		pui322DIntAllocatedUpdateValues = NULL;
	}
	if (pui323DIntAllocatedUpdateValues)
	{
		OSFreeMem(pui323DIntAllocatedUpdateValues);
		pui323DIntAllocatedUpdateValues = NULL;
	}

	OSLockRelease(psTransferContext->hLock);
	return PVRSRV_OK;

/*
	No resources are created in this function so there is nothing to free
	unless we had to merge syncs.
	If we fail after the client CCB acquire there is still nothing to do
	as only the client CCB release will modify the client CCB
*/
fail_cmdacquire:
fail_prepare_loop:

	PVR_ASSERT(eError != PVRSRV_OK);

	for (i=0;i<ui32PreparesDone;i++)
	{
		SyncAddrListRollbackCheckpoints(psDeviceNode, &psTransferContext->asSyncAddrListFence[i]);
		SyncAddrListRollbackCheckpoints(psDeviceNode, &psTransferContext->asSyncAddrListUpdate[i]);
	}
#if defined(SUPPORT_BUFFER_SYNC)
	if (ui32PreparesDone > 0)
	{
		/* Prevent duplicate rollback in case of buffer sync. */
		psBufferUpdateSyncCheckpoint = NULL;
	}
#endif

	/* Free memory allocated to hold the internal list of update values */
	if (pui322DIntAllocatedUpdateValues)
	{
		OSFreeMem(pui322DIntAllocatedUpdateValues);
		pui322DIntAllocatedUpdateValues = NULL;
	}
	if (pui323DIntAllocatedUpdateValues)
	{
		OSFreeMem(pui323DIntAllocatedUpdateValues);
		pui323DIntAllocatedUpdateValues = NULL;
	}

	if (i2DUpdateFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(i2DUpdateFence, pv2DUpdateFenceFinaliseData);
	}
	if (i3DUpdateFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(i3DUpdateFence, pv3DUpdateFenceFinaliseData);
	}
#if defined(SUPPORT_BUFFER_SYNC)
	if (psBufferUpdateSyncCheckpoint)
	{
		SyncAddrListRollbackCheckpoints(psDeviceNode, &psTransferContext->asSyncAddrListUpdate[0]);
	}
	if (psBufferSyncData)
	{
		pvr_buffer_sync_kick_failed(psBufferSyncData);
	}
	if (apsBufferFenceSyncCheckpoints)
	{
		kfree(apsBufferFenceSyncCheckpoints);
	}
fail_resolve_buffersync_input_fence:
#endif /* defined(SUPPORT_BUFFER_SYNC) */

	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence */
	SyncAddrListDeRefCheckpoints(ui32FenceSyncCheckpointCount,
								 apsFenceSyncCheckpoints);
	/* Free the memory that was allocated for the sync checkpoint list returned by ResolveFence() */
	if (apsFenceSyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFenceSyncCheckpoints);
	}
fail_resolve_fencesync_input_fence:
	OSFreeMem(pas2DCmdHelper);
fail_alloc2dhelper:
	OSFreeMem(pas3DCmdHelper);
fail_alloc3dhelper:

	OSLockRelease(psTransferContext->hLock);
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

	OSLockAcquire(psTransferContext->hLock);

	if ((psTransferContext->s2DData.ui32Priority != ui32Priority) &&
			(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
	{
		eError = ContextSetPriority(psTransferContext->s2DData.psServerCommonContext,
									psConnection,
									psTransferContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_2D);
		if (eError != PVRSRV_OK)
		{
			if (eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 2D part of the transfercontext (%s)", __func__, PVRSRVGetErrorString(eError)));
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
			if (eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 3D part of the transfercontext (%s)", __func__, PVRSRVGetErrorString(eError)));
			}
			goto fail_3dcontext;
		}
		psTransferContext->s3DData.ui32Priority = ui32Priority;
	}

	OSLockRelease(psTransferContext->hLock);
	return PVRSRV_OK;

fail_3dcontext:

fail_2dcontext:
	OSLockRelease(psTransferContext->hLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetTransferContextPropertyKM(RGX_SERVER_TQ_CONTEXT *psTransferContext,
												   RGX_CONTEXT_PROPERTY eContextProperty,
												   IMG_UINT64 ui64Input,
												   IMG_UINT64 *pui64Output)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2 = PVRSRV_OK;

	switch (eContextProperty)
	{
		case RGX_CONTEXT_PROPERTY_FLAGS:
		{
			OSLockAcquire(psTransferContext->hLock);
			eError = FWCommonContextSetFlags(psTransferContext->s2DData.psServerCommonContext,
			                                 (IMG_UINT32)ui64Input);
			if (eError == PVRSRV_OK)
			{
				eError2 = FWCommonContextSetFlags(psTransferContext->s3DData.psServerCommonContext,
				                                  (IMG_UINT32)ui64Input);
			}
			if ((eError == PVRSRV_OK) && (eError2 == PVRSRV_OK))
			{
				psTransferContext->ui32Flags = (IMG_UINT32)ui64Input;
			}
			OSLockRelease(psTransferContext->hLock);
			PVR_LOG_IF_ERROR(eError, "FWCommonContextSetFlags eError");
			PVR_LOG_IF_ERROR(eError2, "FWCommonContextSetFlags eError2");
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

void DumpTransferCtxtsInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                           void *pvDumpDebugFile,
                           IMG_UINT32 ui32VerbLevel)
{
	DLLIST_NODE *psNode, *psNext;

	OSWRLockAcquireRead(psDevInfo->hTransferCtxListLock);

	dllist_foreach_node(&psDevInfo->sTransferCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_TQ_CONTEXT *psCurrentServerTransferCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_TQ_CONTEXT, sListNode);

		if ((psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) &&
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
		{
			DumpFWCommonContextInfo(psCurrentServerTransferCtx->s2DData.psServerCommonContext,
			                        pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
		}

		if (psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_3D)
		{
			DumpFWCommonContextInfo(psCurrentServerTransferCtx->s3DData.psServerCommonContext,
			                        pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
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

		if ((psCurrentServerTransferCtx->ui32Flags & RGX_SERVER_TQ_CONTEXT_FLAGS_2D) &&
				(NULL != psCurrentServerTransferCtx->s2DData.psServerCommonContext) &&
				(RGX_IS_FEATURE_SUPPORTED(psDevInfo, TLA)))
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
