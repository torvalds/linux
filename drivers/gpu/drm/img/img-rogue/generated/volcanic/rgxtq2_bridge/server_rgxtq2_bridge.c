/*******************************************************************************
@File
@Title          Server bridge for rgxtq2
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxtq2
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "rgxtdmtransfer.h"

#include "common_rgxtq2_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

#include "rgx_bvnc_defs_km.h"

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _RGXTDMCreateTransferContextpsTransferContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXTDMDestroyTransferContextKM((RGX_SERVER_TQ_TDM_CONTEXT *) pvData);
	return eError;
}

static_assert(RGXFWIF_RF_CMD_SIZE <= IMG_UINT32_MAX,
	      "RGXFWIF_RF_CMD_SIZE must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeRGXTDMCreateTransferContext(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psRGXTDMCreateTransferContextIN_UI8,
					IMG_UINT8 * psRGXTDMCreateTransferContextOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT *psRGXTDMCreateTransferContextIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXTDMCreateTransferContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT *psRGXTDMCreateTransferContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXTDMCreateTransferContextOUT_UI8, 0);

	IMG_BYTE *ui8FrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXTDMCreateTransferContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize *
	     sizeof(IMG_BYTE)) + 0;

	if (unlikely(psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize > RGXFWIF_RF_CMD_SIZE))
	{
		psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXTDMCreateTransferContext_exit;
	}

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMCreateTransferContext_exit;
		}
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto RGXTDMCreateTransferContext_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXTDMCreateTransferContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer =
			    (IMG_BYTE *) (void *)psRGXTDMCreateTransferContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXTDMCreateTransferContextOUT->eError =
				    PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXTDMCreateTransferContext_exit;
			}
		}
	}

	if (psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize != 0)
	{
		ui8FrameworkCmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8FrameworkCmdInt,
		     (const void __user *)psRGXTDMCreateTransferContextIN->pui8FrameworkCmd,
		     psRGXTDMCreateTransferContextIN->ui32FrameworkCmdSize * sizeof(IMG_BYTE)) !=
		    PVRSRV_OK)
		{
			psRGXTDMCreateTransferContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMCreateTransferContext_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXTDMCreateTransferContextOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMCreateTransferContext_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXTDMCreateTransferContextOUT->eError =
	    PVRSRVRGXTDMCreateTransferContextKM(psConnection, OSGetDevNode(psConnection),
						psRGXTDMCreateTransferContextIN->i32Priority,
						psRGXTDMCreateTransferContextIN->
						ui32FrameworkCmdSize, ui8FrameworkCmdInt,
						hPrivDataInt,
						psRGXTDMCreateTransferContextIN->
						ui32PackedCCBSizeU88,
						psRGXTDMCreateTransferContextIN->ui32ContextFlags,
						psRGXTDMCreateTransferContextIN->
						ui64RobustnessAddress, &psTransferContextInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		goto RGXTDMCreateTransferContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXTDMCreateTransferContextOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
				      &psRGXTDMCreateTransferContextOUT->hTransferContext,
				      (void *)psTransferContextInt,
				      PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _RGXTDMCreateTransferContextpsTransferContextIntRelease);
	if (unlikely(psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMCreateTransferContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTDMCreateTransferContext_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (hPrivDataInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRGXTDMCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		if (psTransferContextInt)
		{
			PVRSRVRGXTDMDestroyTransferContextKM(psTransferContextInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psRGXTDMCreateTransferContextOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTDMDestroyTransferContext(IMG_UINT32 ui32DispatchTableEntry,
					 IMG_UINT8 * psRGXTDMDestroyTransferContextIN_UI8,
					 IMG_UINT8 * psRGXTDMDestroyTransferContextOUT_UI8,
					 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT *psRGXTDMDestroyTransferContextIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXTDMDestroyTransferContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT *psRGXTDMDestroyTransferContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXTDMDestroyTransferContextOUT_UI8, 0);

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMDestroyTransferContextOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMDestroyTransferContext_exit;
		}
	}

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXTDMDestroyTransferContextOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psRGXTDMDestroyTransferContextIN->
					      hTransferContext,
					      PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	if (unlikely
	    ((psRGXTDMDestroyTransferContextOUT->eError != PVRSRV_OK)
	     && (psRGXTDMDestroyTransferContextOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL)
	     && (psRGXTDMDestroyTransferContextOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__,
			 PVRSRVGetErrorString(psRGXTDMDestroyTransferContextOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMDestroyTransferContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTDMDestroyTransferContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTDMSetTransferContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					     IMG_UINT8 * psRGXTDMSetTransferContextPriorityIN_UI8,
					     IMG_UINT8 * psRGXTDMSetTransferContextPriorityOUT_UI8,
					     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY *psRGXTDMSetTransferContextPriorityIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXTDMSetTransferContextPriorityIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY *psRGXTDMSetTransferContextPriorityOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXTDMSetTransferContextPriorityOUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXTDMSetTransferContextPriorityIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContextInt = NULL;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMSetTransferContextPriorityOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMSetTransferContextPriority_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXTDMSetTransferContextPriorityOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXTDMSetTransferContextPriorityOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMSetTransferContextPriority_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXTDMSetTransferContextPriorityOUT->eError =
	    PVRSRVRGXTDMSetTransferContextPriorityKM(psConnection, OSGetDevNode(psConnection),
						     psTransferContextInt,
						     psRGXTDMSetTransferContextPriorityIN->
						     i32Priority);

RGXTDMSetTransferContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTDMNotifyWriteOffsetUpdate(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psRGXTDMNotifyWriteOffsetUpdateIN_UI8,
					  IMG_UINT8 * psRGXTDMNotifyWriteOffsetUpdateOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE *psRGXTDMNotifyWriteOffsetUpdateIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE *)
	    IMG_OFFSET_ADDR(psRGXTDMNotifyWriteOffsetUpdateIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE *psRGXTDMNotifyWriteOffsetUpdateOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE *)
	    IMG_OFFSET_ADDR(psRGXTDMNotifyWriteOffsetUpdateOUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXTDMNotifyWriteOffsetUpdateIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContextInt = NULL;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMNotifyWriteOffsetUpdateOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMNotifyWriteOffsetUpdate_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXTDMNotifyWriteOffsetUpdateOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXTDMNotifyWriteOffsetUpdateOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMNotifyWriteOffsetUpdate_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXTDMNotifyWriteOffsetUpdateOUT->eError =
	    PVRSRVRGXTDMNotifyWriteOffsetUpdateKM(psTransferContextInt,
						  psRGXTDMNotifyWriteOffsetUpdateIN->
						  ui32PDumpFlags);

RGXTDMNotifyWriteOffsetUpdate_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static_assert(PVRSRV_MAX_SYNCS <= IMG_UINT32_MAX,
	      "PVRSRV_MAX_SYNCS must not be larger than IMG_UINT32_MAX");
static_assert(PVRSRV_SYNC_NAME_LENGTH <= IMG_UINT32_MAX,
	      "PVRSRV_SYNC_NAME_LENGTH must not be larger than IMG_UINT32_MAX");
static_assert(RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE <= IMG_UINT32_MAX,
	      "RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE must not be larger than IMG_UINT32_MAX");
static_assert(PVRSRV_MAX_SYNCS <= IMG_UINT32_MAX,
	      "PVRSRV_MAX_SYNCS must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeRGXTDMSubmitTransfer2(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psRGXTDMSubmitTransfer2IN_UI8,
				  IMG_UINT8 * psRGXTDMSubmitTransfer2OUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER2 *psRGXTDMSubmitTransfer2IN =
	    (PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER2 *)
	    IMG_OFFSET_ADDR(psRGXTDMSubmitTransfer2IN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER2 *psRGXTDMSubmitTransfer2OUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER2 *)
	    IMG_OFFSET_ADDR(psRGXTDMSubmitTransfer2OUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXTDMSubmitTransfer2IN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK **psUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE *hUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 *ui32UpdateSyncOffsetInt = NULL;
	IMG_UINT32 *ui32UpdateValueInt = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;
	IMG_UINT8 *ui8FWCommandInt = NULL;
	IMG_UINT32 *ui32SyncPMRFlagsInt = NULL;
	PMR **psSyncPMRsInt = NULL;
	IMG_HANDLE *hSyncPMRsInt2 = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount *
	     sizeof(SYNC_PRIMITIVE_BLOCK *)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32CommandSize * sizeof(IMG_UINT8)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *)) +
	    ((IMG_UINT64) psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) + 0;

	if (unlikely(psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount > PVRSRV_MAX_SYNCS))
	{
		psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXTDMSubmitTransfer2_exit;
	}

	if (unlikely
	    (psRGXTDMSubmitTransfer2IN->ui32CommandSize > RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE))
	{
		psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXTDMSubmitTransfer2_exit;
	}

	if (unlikely(psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount > PVRSRV_MAX_SYNCS))
	{
		psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXTDMSubmitTransfer2_exit;
	}

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto RGXTDMSubmitTransfer2_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXTDMSubmitTransfer2IN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXTDMSubmitTransfer2IN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXTDMSubmitTransfer2_exit;
			}
		}
	}

	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount != 0)
	{
		psUpdateUFOSyncPrimBlockInt =
		    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		OSCachedMemSet(psUpdateUFOSyncPrimBlockInt, 0,
			       psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount *
			       sizeof(SYNC_PRIMITIVE_BLOCK *));
		ui32NextOffset +=
		    psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount *
		    sizeof(SYNC_PRIMITIVE_BLOCK *);
		hUpdateUFOSyncPrimBlockInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hUpdateUFOSyncPrimBlockInt2,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->phUpdateUFOSyncPrimBlock,
		     psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) !=
		    PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}
	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateSyncOffsetInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32UpdateSyncOffsetInt,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->pui32UpdateSyncOffset,
		     psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}
	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateValueInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32UpdateValueInt,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->pui32UpdateValue,
		     psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}

	{
		uiUpdateFenceNameInt =
		    (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiUpdateFenceNameInt,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->puiUpdateFenceName,
		     PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
		((IMG_CHAR *) uiUpdateFenceNameInt)[(PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) -
						    1] = '\0';
	}
	if (psRGXTDMSubmitTransfer2IN->ui32CommandSize != 0)
	{
		ui8FWCommandInt = (IMG_UINT8 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransfer2IN->ui32CommandSize * sizeof(IMG_UINT8);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32CommandSize * sizeof(IMG_UINT8) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8FWCommandInt,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->pui8FWCommand,
		     psRGXTDMSubmitTransfer2IN->ui32CommandSize * sizeof(IMG_UINT8)) != PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}
	if (psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount != 0)
	{
		ui32SyncPMRFlagsInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32SyncPMRFlagsInt,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->pui32SyncPMRFlags,
		     psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}
	if (psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount != 0)
	{
		psSyncPMRsInt = (PMR **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		OSCachedMemSet(psSyncPMRsInt, 0,
			       psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *));
		ui32NextOffset += psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *);
		hSyncPMRsInt2 = (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hSyncPMRsInt2,
		     (const void __user *)psRGXTDMSubmitTransfer2IN->phSyncPMRs,
		     psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXTDMSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXTDMSubmitTransfer2_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXTDMSubmitTransfer2OUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXTDMSubmitTransfer2OUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMSubmitTransfer2_exit;
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount; i++)
		{
			/* Look up the address from the handle */
			psRGXTDMSubmitTransfer2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psUpdateUFOSyncPrimBlockInt[i],
						       hUpdateUFOSyncPrimBlockInt2[i],
						       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
						       IMG_TRUE);
			if (unlikely(psRGXTDMSubmitTransfer2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXTDMSubmitTransfer2_exit;
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount; i++)
		{
			/* Look up the address from the handle */
			psRGXTDMSubmitTransfer2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psSyncPMRsInt[i],
						       hSyncPMRsInt2[i],
						       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
			if (unlikely(psRGXTDMSubmitTransfer2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXTDMSubmitTransfer2_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXTDMSubmitTransfer2OUT->eError =
	    PVRSRVRGXTDMSubmitTransferKM(psTransferContextInt,
					 psRGXTDMSubmitTransfer2IN->ui32PDumpFlags,
					 psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount,
					 psUpdateUFOSyncPrimBlockInt,
					 ui32UpdateSyncOffsetInt,
					 ui32UpdateValueInt,
					 psRGXTDMSubmitTransfer2IN->hCheckFenceFD,
					 psRGXTDMSubmitTransfer2IN->hUpdateTimeline,
					 &psRGXTDMSubmitTransfer2OUT->hUpdateFence,
					 uiUpdateFenceNameInt,
					 psRGXTDMSubmitTransfer2IN->ui32CommandSize,
					 ui8FWCommandInt,
					 psRGXTDMSubmitTransfer2IN->ui32ExternalJobReference,
					 psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount,
					 ui32SyncPMRFlagsInt,
					 psSyncPMRsInt,
					 psRGXTDMSubmitTransfer2IN->ui32Characteristic1,
					 psRGXTDMSubmitTransfer2IN->ui32Characteristic2,
					 psRGXTDMSubmitTransfer2IN->ui64DeadlineInus);

RGXTDMSubmitTransfer2_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	}

	if (hUpdateUFOSyncPrimBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXTDMSubmitTransfer2IN->ui32ClientUpdateCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (psUpdateUFOSyncPrimBlockInt && psUpdateUFOSyncPrimBlockInt[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hUpdateUFOSyncPrimBlockInt2[i],
							    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			}
		}
	}

	if (hSyncPMRsInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXTDMSubmitTransfer2IN->ui32SyncPMRCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (psSyncPMRsInt && psSyncPMRsInt[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hSyncPMRsInt2[i],
							    PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
			}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psRGXTDMSubmitTransfer2OUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static PVRSRV_ERROR _RGXTDMGetSharedMemorypsCLIPMRMemIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXTDMReleaseSharedMemoryKM((PMR *) pvData);
	return eError;
}

static PVRSRV_ERROR _RGXTDMGetSharedMemorypsUSCPMRMemIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXTDMReleaseSharedMemoryKM((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXTDMGetSharedMemory(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psRGXTDMGetSharedMemoryIN_UI8,
				  IMG_UINT8 * psRGXTDMGetSharedMemoryOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMGETSHAREDMEMORY *psRGXTDMGetSharedMemoryIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMGETSHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTDMGetSharedMemoryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMGETSHAREDMEMORY *psRGXTDMGetSharedMemoryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMGETSHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTDMGetSharedMemoryOUT_UI8, 0);

	PMR *psCLIPMRMemInt = NULL;
	PMR *psUSCPMRMemInt = NULL;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMGetSharedMemoryOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMGetSharedMemory_exit;
		}
	}

	PVR_UNREFERENCED_PARAMETER(psRGXTDMGetSharedMemoryIN);

	psRGXTDMGetSharedMemoryOUT->eError =
	    PVRSRVRGXTDMGetSharedMemoryKM(psConnection, OSGetDevNode(psConnection),
					  &psCLIPMRMemInt, &psUSCPMRMemInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXTDMGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		goto RGXTDMGetSharedMemory_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXTDMGetSharedMemoryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								       &psRGXTDMGetSharedMemoryOUT->
								       hCLIPMRMem,
								       (void *)psCLIPMRMemInt,
								       PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
								       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								       (PFN_HANDLE_RELEASE) &
								       _RGXTDMGetSharedMemorypsCLIPMRMemIntRelease);
	if (unlikely(psRGXTDMGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMGetSharedMemory_exit;
	}

	psRGXTDMGetSharedMemoryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								       &psRGXTDMGetSharedMemoryOUT->
								       hUSCPMRMem,
								       (void *)psUSCPMRMemInt,
								       PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
								       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								       (PFN_HANDLE_RELEASE) &
								       _RGXTDMGetSharedMemorypsUSCPMRMemIntRelease);
	if (unlikely(psRGXTDMGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMGetSharedMemory_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTDMGetSharedMemory_exit:

	if (psRGXTDMGetSharedMemoryOUT->eError != PVRSRV_OK)
	{
		if (psCLIPMRMemInt)
		{
			PVRSRVRGXTDMReleaseSharedMemoryKM(psCLIPMRMemInt);
		}
		if (psUSCPMRMemInt)
		{
			PVRSRVRGXTDMReleaseSharedMemoryKM(psUSCPMRMemInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTDMReleaseSharedMemory(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psRGXTDMReleaseSharedMemoryIN_UI8,
				      IMG_UINT8 * psRGXTDMReleaseSharedMemoryOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMRELEASESHAREDMEMORY *psRGXTDMReleaseSharedMemoryIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMRELEASESHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTDMReleaseSharedMemoryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMRELEASESHAREDMEMORY *psRGXTDMReleaseSharedMemoryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMRELEASESHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTDMReleaseSharedMemoryOUT_UI8, 0);

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMReleaseSharedMemoryOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMReleaseSharedMemory_exit;
		}
	}

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXTDMReleaseSharedMemoryOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psRGXTDMReleaseSharedMemoryIN->hPMRMem,
					      PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if (unlikely((psRGXTDMReleaseSharedMemoryOUT->eError != PVRSRV_OK) &&
		     (psRGXTDMReleaseSharedMemoryOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psRGXTDMReleaseSharedMemoryOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXTDMReleaseSharedMemoryOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMReleaseSharedMemory_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTDMReleaseSharedMemory_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTDMSetTransferContextProperty(IMG_UINT32 ui32DispatchTableEntry,
					     IMG_UINT8 * psRGXTDMSetTransferContextPropertyIN_UI8,
					     IMG_UINT8 * psRGXTDMSetTransferContextPropertyOUT_UI8,
					     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPROPERTY *psRGXTDMSetTransferContextPropertyIN =
	    (PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXTDMSetTransferContextPropertyIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPROPERTY *psRGXTDMSetTransferContextPropertyOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXTDMSetTransferContextPropertyOUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXTDMSetTransferContextPropertyIN->hTransferContext;
	RGX_SERVER_TQ_TDM_CONTEXT *psTransferContextInt = NULL;

	{
		PVRSRV_DEVICE_NODE *psDeviceNode = OSGetDevNode(psConnection);

		/* Check that device supports the required feature */
		if ((psDeviceNode->pfnCheckDeviceFeature) &&
		    !psDeviceNode->pfnCheckDeviceFeature(psDeviceNode,
							 RGX_FEATURE_FASTRENDER_DM_BIT_MASK))
		{
			psRGXTDMSetTransferContextPropertyOUT->eError = PVRSRV_ERROR_NOT_SUPPORTED;

			goto RGXTDMSetTransferContextProperty_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXTDMSetTransferContextPropertyOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXTDMSetTransferContextPropertyOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTDMSetTransferContextProperty_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXTDMSetTransferContextPropertyOUT->eError =
	    PVRSRVRGXTDMSetTransferContextPropertyKM(psTransferContextInt,
						     psRGXTDMSetTransferContextPropertyIN->
						     ui32Property,
						     psRGXTDMSetTransferContextPropertyIN->
						     ui64Input,
						     &psRGXTDMSetTransferContextPropertyOUT->
						     ui64Output);

RGXTDMSetTransferContextProperty_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXTQ2Bridge(void);
void DeinitRGXTQ2Bridge(void);

/*
 * Register all RGXTQ2 functions with services
 */
PVRSRV_ERROR InitRGXTQ2Bridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
			      PVRSRV_BRIDGE_RGXTQ2_RGXTDMCREATETRANSFERCONTEXT,
			      PVRSRVBridgeRGXTDMCreateTransferContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
			      PVRSRV_BRIDGE_RGXTQ2_RGXTDMDESTROYTRANSFERCONTEXT,
			      PVRSRVBridgeRGXTDMDestroyTransferContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
			      PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPRIORITY,
			      PVRSRVBridgeRGXTDMSetTransferContextPriority, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
			      PVRSRV_BRIDGE_RGXTQ2_RGXTDMNOTIFYWRITEOFFSETUPDATE,
			      PVRSRVBridgeRGXTDMNotifyWriteOffsetUpdate, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMSUBMITTRANSFER2,
			      PVRSRVBridgeRGXTDMSubmitTransfer2, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMGETSHAREDMEMORY,
			      PVRSRVBridgeRGXTDMGetSharedMemory, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMRELEASESHAREDMEMORY,
			      PVRSRVBridgeRGXTDMReleaseSharedMemory, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
			      PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPROPERTY,
			      PVRSRVBridgeRGXTDMSetTransferContextProperty, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxtq2 functions with services
 */
void DeinitRGXTQ2Bridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMCREATETRANSFERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMDESTROYTRANSFERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPRIORITY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMNOTIFYWRITEOFFSETUPDATE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMSUBMITTRANSFER2);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2, PVRSRV_BRIDGE_RGXTQ2_RGXTDMGETSHAREDMEMORY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMRELEASESHAREDMEMORY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ2,
				PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPROPERTY);

}
