/*******************************************************************************
@File
@Title          Server bridge for rgxtq
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxtq
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

#include "rgxtransfer.h"
#include "rgx_tq_shared.h"

#include "common_rgxtq_bridge.h"

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

#if defined(SUPPORT_RGXTQ_BRIDGE)

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _RGXCreateTransferContextpsTransferContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXDestroyTransferContextKM((RGX_SERVER_TQ_CONTEXT *) pvData);
	return eError;
}

static_assert(RGXFWIF_RF_CMD_SIZE <= IMG_UINT32_MAX,
	      "RGXFWIF_RF_CMD_SIZE must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeRGXCreateTransferContext(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psRGXCreateTransferContextIN_UI8,
				     IMG_UINT8 * psRGXCreateTransferContextOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT *psRGXCreateTransferContextIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateTransferContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT *psRGXCreateTransferContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateTransferContextOUT_UI8, 0);

	IMG_BYTE *ui8FrameworkCmdInt = NULL;
	IMG_HANDLE hPrivData = psRGXCreateTransferContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_TQ_CONTEXT *psTransferContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) + 0;

	if (unlikely(psRGXCreateTransferContextIN->ui32FrameworkCmdize > RGXFWIF_RF_CMD_SIZE))
	{
		psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXCreateTransferContext_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto RGXCreateTransferContext_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXCreateTransferContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXCreateTransferContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXCreateTransferContext_exit;
			}
		}
	}

	if (psRGXCreateTransferContextIN->ui32FrameworkCmdize != 0)
	{
		ui8FrameworkCmdInt = (IMG_BYTE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE);
	}

	/* Copy the data over */
	if (psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui8FrameworkCmdInt,
		     (const void __user *)psRGXCreateTransferContextIN->pui8FrameworkCmd,
		     psRGXCreateTransferContextIN->ui32FrameworkCmdize * sizeof(IMG_BYTE)) !=
		    PVRSRV_OK)
		{
			psRGXCreateTransferContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXCreateTransferContext_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXCreateTransferContextOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateTransferContext_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateTransferContextOUT->eError =
	    PVRSRVRGXCreateTransferContextKM(psConnection, OSGetDevNode(psConnection),
					     psRGXCreateTransferContextIN->i32Priority,
					     psRGXCreateTransferContextIN->ui32FrameworkCmdize,
					     ui8FrameworkCmdInt,
					     hPrivDataInt,
					     psRGXCreateTransferContextIN->ui32PackedCCBSizeU8888,
					     psRGXCreateTransferContextIN->ui32ContextFlags,
					     psRGXCreateTransferContextIN->ui64RobustnessAddress,
					     &psTransferContextInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateTransferContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateTransferContextOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
				      &psRGXCreateTransferContextOUT->hTransferContext,
				      (void *)psTransferContextInt,
				      PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _RGXCreateTransferContextpsTransferContextIntRelease);
	if (unlikely(psRGXCreateTransferContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateTransferContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateTransferContext_exit:

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

	if (psRGXCreateTransferContextOUT->eError != PVRSRV_OK)
	{
		if (psTransferContextInt)
		{
			PVRSRVRGXDestroyTransferContextKM(psTransferContextInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psRGXCreateTransferContextOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyTransferContext(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psRGXDestroyTransferContextIN_UI8,
				      IMG_UINT8 * psRGXDestroyTransferContextOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT *psRGXDestroyTransferContextIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyTransferContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT *psRGXDestroyTransferContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyTransferContextOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyTransferContextOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psRGXDestroyTransferContextIN->
					      hTransferContext,
					      PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
	if (unlikely
	    ((psRGXDestroyTransferContextOUT->eError != PVRSRV_OK)
	     && (psRGXDestroyTransferContextOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL)
	     && (psRGXDestroyTransferContextOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyTransferContextOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyTransferContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyTransferContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetTransferContextPriority(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psRGXSetTransferContextPriorityIN_UI8,
					  IMG_UINT8 * psRGXSetTransferContextPriorityOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY *psRGXSetTransferContextPriorityIN =
	    (PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXSetTransferContextPriorityIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY *psRGXSetTransferContextPriorityOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY *)
	    IMG_OFFSET_ADDR(psRGXSetTransferContextPriorityOUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXSetTransferContextPriorityIN->hTransferContext;
	RGX_SERVER_TQ_CONTEXT *psTransferContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetTransferContextPriorityOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSetTransferContextPriorityOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetTransferContextPriority_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetTransferContextPriorityOUT->eError =
	    PVRSRVRGXSetTransferContextPriorityKM(psConnection, OSGetDevNode(psConnection),
						  psTransferContextInt,
						  psRGXSetTransferContextPriorityIN->i32Priority);

RGXSetTransferContextPriority_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
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
PVRSRVBridgeRGXSubmitTransfer2(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psRGXSubmitTransfer2IN_UI8,
			       IMG_UINT8 * psRGXSubmitTransfer2OUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER2 *psRGXSubmitTransfer2IN =
	    (PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER2 *) IMG_OFFSET_ADDR(psRGXSubmitTransfer2IN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER2 *psRGXSubmitTransfer2OUT =
	    (PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER2 *) IMG_OFFSET_ADDR(psRGXSubmitTransfer2OUT_UI8,
								     0);

	IMG_HANDLE hTransferContext = psRGXSubmitTransfer2IN->hTransferContext;
	RGX_SERVER_TQ_CONTEXT *psTransferContextInt = NULL;
	IMG_UINT32 *ui32ClientUpdateCountInt = NULL;
	SYNC_PRIMITIVE_BLOCK ***psUpdateUFOSyncPrimBlockInt = NULL;
	IMG_HANDLE **hUpdateUFOSyncPrimBlockInt2 = NULL;
	IMG_UINT32 **ui32UpdateSyncOffsetInt = NULL;
	IMG_UINT32 **ui32UpdateValueInt = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;
	IMG_UINT32 *ui32CommandSizeInt = NULL;
	IMG_UINT8 **ui8FWCommandInt = NULL;
	IMG_UINT32 *ui32TQPrepareFlagsInt = NULL;
	IMG_UINT32 *ui32SyncPMRFlagsInt = NULL;
	PMR **psSyncPMRsInt = NULL;
	IMG_HANDLE *hSyncPMRsInt2 = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BYTE *pArrayArgsBuffer2 = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) +
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *)) +
	    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) + 0;
	IMG_UINT32 ui32BufferSize2 = 0;
	IMG_UINT32 ui32NextOffset2 = 0;

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{

		ui64BufferSize +=
		    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount *
		     sizeof(SYNC_PRIMITIVE_BLOCK **));
		ui64BufferSize +=
		    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_HANDLE **));
		ui64BufferSize +=
		    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32 *));
		ui64BufferSize +=
		    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32 *));
		ui64BufferSize +=
		    ((IMG_UINT64) psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT8 *));
	}

	if (unlikely(psRGXSubmitTransfer2IN->ui32SyncPMRCount > PVRSRV_MAX_SYNCS))
	{
		psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXSubmitTransfer2_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto RGXSubmitTransfer2_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXSubmitTransfer2IN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXSubmitTransfer2IN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXSubmitTransfer2_exit;
			}
		}
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		ui32ClientUpdateCountInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32ClientUpdateCountInt,
		     (const void __user *)psRGXSubmitTransfer2IN->pui32ClientUpdateCount,
		     psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		/* Assigning psUpdateUFOSyncPrimBlockInt to the right offset in the pool buffer for first dimension */
		psUpdateUFOSyncPrimBlockInt =
		    (SYNC_PRIMITIVE_BLOCK ***) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(SYNC_PRIMITIVE_BLOCK **);
		/* Assigning hUpdateUFOSyncPrimBlockInt2 to the right offset in the pool buffer for first dimension */
		hUpdateUFOSyncPrimBlockInt2 =
		    (IMG_HANDLE **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_HANDLE);
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		/* Assigning ui32UpdateSyncOffsetInt to the right offset in the pool buffer for first dimension */
		ui32UpdateSyncOffsetInt =
		    (IMG_UINT32 **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32 *);
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		/* Assigning ui32UpdateValueInt to the right offset in the pool buffer for first dimension */
		ui32UpdateValueInt =
		    (IMG_UINT32 **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32 *);
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
		     (const void __user *)psRGXSubmitTransfer2IN->puiUpdateFenceName,
		     PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
		((IMG_CHAR *) uiUpdateFenceNameInt)[(PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) -
						    1] = '\0';
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		ui32CommandSizeInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32CommandSizeInt,
		     (const void __user *)psRGXSubmitTransfer2IN->pui32CommandSize,
		     psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		/* Assigning ui8FWCommandInt to the right offset in the pool buffer for first dimension */
		ui8FWCommandInt = (IMG_UINT8 **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT8 *);
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		ui32TQPrepareFlagsInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32TQPrepareFlagsInt,
		     (const void __user *)psRGXSubmitTransfer2IN->pui32TQPrepareFlags,
		     psRGXSubmitTransfer2IN->ui32PrepareCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
	}
	if (psRGXSubmitTransfer2IN->ui32SyncPMRCount != 0)
	{
		ui32SyncPMRFlagsInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32SyncPMRFlagsInt,
		     (const void __user *)psRGXSubmitTransfer2IN->pui32SyncPMRFlags,
		     psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
	}
	if (psRGXSubmitTransfer2IN->ui32SyncPMRCount != 0)
	{
		psSyncPMRsInt = (PMR **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		OSCachedMemSet(psSyncPMRsInt, 0,
			       psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *));
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(PMR *);
		hSyncPMRsInt2 = (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hSyncPMRsInt2, (const void __user *)psRGXSubmitTransfer2IN->phSyncPMRs,
		     psRGXSubmitTransfer2IN->ui32SyncPMRCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXSubmitTransfer2_exit;
		}
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		IMG_UINT32 i;
		ui64BufferSize = 0;
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			ui64BufferSize +=
			    ((IMG_UINT64) ui32ClientUpdateCountInt[i] *
			     sizeof(SYNC_PRIMITIVE_BLOCK *));
			ui64BufferSize +=
			    ((IMG_UINT64) ui32ClientUpdateCountInt[i] * sizeof(IMG_HANDLE *));
			ui64BufferSize +=
			    ((IMG_UINT64) ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32));
			ui64BufferSize +=
			    ((IMG_UINT64) ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32));
			ui64BufferSize += ((IMG_UINT64) ui32CommandSizeInt[i] * sizeof(IMG_UINT8));
		}
		if (ui64BufferSize > IMG_UINT32_MAX)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
			goto RGXSubmitTransfer2_exit;
		}
		ui32BufferSize2 = (IMG_UINT32) ui64BufferSize;
	}

	if (ui32BufferSize2 != 0)
	{
		pArrayArgsBuffer2 = OSAllocMemNoStats(ui32BufferSize2);

		if (!pArrayArgsBuffer2)
		{
			psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto RGXSubmitTransfer2_exit;
		}
	}

	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		IMG_UINT32 i;
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			if (ui32ClientUpdateCountInt[i] > PVRSRV_MAX_SYNCS)
			{
				psRGXSubmitTransfer2OUT->eError =
				    PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
				goto RGXSubmitTransfer2_exit;
			}

			/* Assigning each psUpdateUFOSyncPrimBlockInt to the right offset in the pool buffer (this is the second dimension) */
			psUpdateUFOSyncPrimBlockInt[i] =
			    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer2,
								      ui32NextOffset2);
			OSCachedMemSet(psUpdateUFOSyncPrimBlockInt[i], 0,
				       ui32ClientUpdateCountInt[i] *
				       sizeof(SYNC_PRIMITIVE_BLOCK *));
			ui32NextOffset2 +=
			    ui32ClientUpdateCountInt[i] * sizeof(SYNC_PRIMITIVE_BLOCK *);
			/* Assigning each hUpdateUFOSyncPrimBlockInt2 to the right offset in the pool buffer (this is the second dimension) */
			hUpdateUFOSyncPrimBlockInt2[i] =
			    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer2, ui32NextOffset2);
			ui32NextOffset2 += ui32ClientUpdateCountInt[i] * sizeof(IMG_HANDLE);
		}
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		IMG_UINT32 i;
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Assigning each ui32UpdateSyncOffsetInt to the right offset in the pool buffer (this is the second dimension) */
			ui32UpdateSyncOffsetInt[i] =
			    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer2, ui32NextOffset2);
			ui32NextOffset2 += ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32);
		}
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		IMG_UINT32 i;
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Assigning each ui32UpdateValueInt to the right offset in the pool buffer (this is the second dimension) */
			ui32UpdateValueInt[i] =
			    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer2, ui32NextOffset2);
			ui32NextOffset2 += ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32);
		}
	}
	if (psRGXSubmitTransfer2IN->ui32PrepareCount != 0)
	{
		IMG_UINT32 i;
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			if (ui32CommandSizeInt[i] > RGXFWIF_DM_INDEPENDENT_KICK_CMD_SIZE)
			{
				psRGXSubmitTransfer2OUT->eError =
				    PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
				goto RGXSubmitTransfer2_exit;
			}

			/* Assigning each ui8FWCommandInt to the right offset in the pool buffer (this is the second dimension) */
			ui8FWCommandInt[i] =
			    (IMG_UINT8 *) IMG_OFFSET_ADDR(pArrayArgsBuffer2, ui32NextOffset2);
			ui32NextOffset2 += ui32CommandSizeInt[i] * sizeof(IMG_UINT8);
		}
	}

	{
		IMG_UINT32 i;
		IMG_HANDLE **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Copy the pointer over from the client side */
			if (OSCopyFromUser
			    (NULL, &psPtr,
			     (const void __user *)&psRGXSubmitTransfer2IN->
			     phUpdateUFOSyncPrimBlock[i], sizeof(IMG_HANDLE **)) != PVRSRV_OK)
			{
				psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer2_exit;
			}

			/* Copy the data over */
			if ((ui32ClientUpdateCountInt[i] * sizeof(IMG_HANDLE)) > 0)
			{
				if (OSCopyFromUser
				    (NULL, (hUpdateUFOSyncPrimBlockInt2[i]),
				     (const void __user *)psPtr,
				     (ui32ClientUpdateCountInt[i] * sizeof(IMG_HANDLE))) !=
				    PVRSRV_OK)
				{
					psRGXSubmitTransfer2OUT->eError =
					    PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXSubmitTransfer2_exit;
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Copy the pointer over from the client side */
			if (OSCopyFromUser
			    (NULL, &psPtr,
			     (const void __user *)&psRGXSubmitTransfer2IN->pui32UpdateSyncOffset[i],
			     sizeof(IMG_UINT32 **)) != PVRSRV_OK)
			{
				psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer2_exit;
			}

			/* Copy the data over */
			if ((ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32)) > 0)
			{
				if (OSCopyFromUser
				    (NULL, (ui32UpdateSyncOffsetInt[i]), (const void __user *)psPtr,
				     (ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32))) !=
				    PVRSRV_OK)
				{
					psRGXSubmitTransfer2OUT->eError =
					    PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXSubmitTransfer2_exit;
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT32 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Copy the pointer over from the client side */
			if (OSCopyFromUser
			    (NULL, &psPtr,
			     (const void __user *)&psRGXSubmitTransfer2IN->pui32UpdateValue[i],
			     sizeof(IMG_UINT32 **)) != PVRSRV_OK)
			{
				psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer2_exit;
			}

			/* Copy the data over */
			if ((ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32)) > 0)
			{
				if (OSCopyFromUser
				    (NULL, (ui32UpdateValueInt[i]), (const void __user *)psPtr,
				     (ui32ClientUpdateCountInt[i] * sizeof(IMG_UINT32))) !=
				    PVRSRV_OK)
				{
					psRGXSubmitTransfer2OUT->eError =
					    PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXSubmitTransfer2_exit;
				}
			}
		}
	}

	{
		IMG_UINT32 i;
		IMG_UINT8 **psPtr;

		/* Loop over all the pointers in the array copying the data into the kernel */
		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			/* Copy the pointer over from the client side */
			if (OSCopyFromUser
			    (NULL, &psPtr,
			     (const void __user *)&psRGXSubmitTransfer2IN->pui8FWCommand[i],
			     sizeof(IMG_UINT8 **)) != PVRSRV_OK)
			{
				psRGXSubmitTransfer2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto RGXSubmitTransfer2_exit;
			}

			/* Copy the data over */
			if ((ui32CommandSizeInt[i] * sizeof(IMG_UINT8)) > 0)
			{
				if (OSCopyFromUser
				    (NULL, (ui8FWCommandInt[i]), (const void __user *)psPtr,
				     (ui32CommandSizeInt[i] * sizeof(IMG_UINT8))) != PVRSRV_OK)
				{
					psRGXSubmitTransfer2OUT->eError =
					    PVRSRV_ERROR_INVALID_PARAMS;

					goto RGXSubmitTransfer2_exit;
				}
			}
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSubmitTransfer2OUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSubmitTransfer2OUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSubmitTransfer2_exit;
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			IMG_UINT32 j;
			for (j = 0; j < ui32ClientUpdateCountInt[i]; j++)
			{
				/* Look up the address from the handle */
				psRGXSubmitTransfer2OUT->eError =
				    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
							       (void **)
							       &psUpdateUFOSyncPrimBlockInt[i][j],
							       hUpdateUFOSyncPrimBlockInt2[i][j],
							       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
							       IMG_TRUE);
				if (unlikely(psRGXSubmitTransfer2OUT->eError != PVRSRV_OK))
				{
					UnlockHandle(psConnection->psHandleBase);
					goto RGXSubmitTransfer2_exit;
				}
			}
		}
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXSubmitTransfer2IN->ui32SyncPMRCount; i++)
		{
			/* Look up the address from the handle */
			psRGXSubmitTransfer2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psSyncPMRsInt[i],
						       hSyncPMRsInt2[i],
						       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
			if (unlikely(psRGXSubmitTransfer2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXSubmitTransfer2_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSubmitTransfer2OUT->eError =
	    PVRSRVRGXSubmitTransferKM(psTransferContextInt,
				      psRGXSubmitTransfer2IN->ui32PrepareCount,
				      ui32ClientUpdateCountInt,
				      psUpdateUFOSyncPrimBlockInt,
				      ui32UpdateSyncOffsetInt,
				      ui32UpdateValueInt,
				      psRGXSubmitTransfer2IN->hCheckFenceFD,
				      psRGXSubmitTransfer2IN->h2DUpdateTimeline,
				      &psRGXSubmitTransfer2OUT->h2DUpdateFence,
				      psRGXSubmitTransfer2IN->h3DUpdateTimeline,
				      &psRGXSubmitTransfer2OUT->h3DUpdateFence,
				      uiUpdateFenceNameInt,
				      ui32CommandSizeInt,
				      ui8FWCommandInt,
				      ui32TQPrepareFlagsInt,
				      psRGXSubmitTransfer2IN->ui32ExtJobRef,
				      psRGXSubmitTransfer2IN->ui32SyncPMRCount,
				      ui32SyncPMRFlagsInt, psSyncPMRsInt);

RGXSubmitTransfer2_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
	}

	if (hUpdateUFOSyncPrimBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXSubmitTransfer2IN->ui32PrepareCount; i++)
		{
			IMG_UINT32 j;
			for (j = 0; j < ui32ClientUpdateCountInt[i]; j++)
			{

				/* Unreference the previously looked up handle */
				if (psUpdateUFOSyncPrimBlockInt && psUpdateUFOSyncPrimBlockInt[i]
				    && psUpdateUFOSyncPrimBlockInt[i][j])
				{
					PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
								    hUpdateUFOSyncPrimBlockInt2[i]
								    [j],
								    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
				}
			}
		}
	}

	if (hSyncPMRsInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXSubmitTransfer2IN->ui32SyncPMRCount; i++)
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
	if (psRGXSubmitTransfer2OUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psRGXSubmitTransfer2OUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize2 == ui32NextOffset2);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (pArrayArgsBuffer2)
		OSFreeMemNoStats(pArrayArgsBuffer2);

	return 0;
}

static PVRSRV_ERROR _RGXTQGetSharedMemorypsCLIPMRMemIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXTQReleaseSharedMemoryKM((PMR *) pvData);
	return eError;
}

static PVRSRV_ERROR _RGXTQGetSharedMemorypsUSCPMRMemIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXTQReleaseSharedMemoryKM((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXTQGetSharedMemory(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psRGXTQGetSharedMemoryIN_UI8,
				 IMG_UINT8 * psRGXTQGetSharedMemoryOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTQGETSHAREDMEMORY *psRGXTQGetSharedMemoryIN =
	    (PVRSRV_BRIDGE_IN_RGXTQGETSHAREDMEMORY *) IMG_OFFSET_ADDR(psRGXTQGetSharedMemoryIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_RGXTQGETSHAREDMEMORY *psRGXTQGetSharedMemoryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTQGETSHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTQGetSharedMemoryOUT_UI8, 0);

	PMR *psCLIPMRMemInt = NULL;
	PMR *psUSCPMRMemInt = NULL;

	PVR_UNREFERENCED_PARAMETER(psRGXTQGetSharedMemoryIN);

	psRGXTQGetSharedMemoryOUT->eError =
	    PVRSRVRGXTQGetSharedMemoryKM(psConnection, OSGetDevNode(psConnection),
					 &psCLIPMRMemInt, &psUSCPMRMemInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXTQGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		goto RGXTQGetSharedMemory_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXTQGetSharedMemoryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								      &psRGXTQGetSharedMemoryOUT->
								      hCLIPMRMem,
								      (void *)psCLIPMRMemInt,
								      PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
								      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								      (PFN_HANDLE_RELEASE) &
								      _RGXTQGetSharedMemorypsCLIPMRMemIntRelease);
	if (unlikely(psRGXTQGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTQGetSharedMemory_exit;
	}

	psRGXTQGetSharedMemoryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								      &psRGXTQGetSharedMemoryOUT->
								      hUSCPMRMem,
								      (void *)psUSCPMRMemInt,
								      PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
								      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								      (PFN_HANDLE_RELEASE) &
								      _RGXTQGetSharedMemorypsUSCPMRMemIntRelease);
	if (unlikely(psRGXTQGetSharedMemoryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTQGetSharedMemory_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTQGetSharedMemory_exit:

	if (psRGXTQGetSharedMemoryOUT->eError != PVRSRV_OK)
	{
		if (psCLIPMRMemInt)
		{
			PVRSRVRGXTQReleaseSharedMemoryKM(psCLIPMRMemInt);
		}
		if (psUSCPMRMemInt)
		{
			PVRSRVRGXTQReleaseSharedMemoryKM(psUSCPMRMemInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXTQReleaseSharedMemory(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psRGXTQReleaseSharedMemoryIN_UI8,
				     IMG_UINT8 * psRGXTQReleaseSharedMemoryOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXTQRELEASESHAREDMEMORY *psRGXTQReleaseSharedMemoryIN =
	    (PVRSRV_BRIDGE_IN_RGXTQRELEASESHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTQReleaseSharedMemoryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXTQRELEASESHAREDMEMORY *psRGXTQReleaseSharedMemoryOUT =
	    (PVRSRV_BRIDGE_OUT_RGXTQRELEASESHAREDMEMORY *)
	    IMG_OFFSET_ADDR(psRGXTQReleaseSharedMemoryOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXTQReleaseSharedMemoryOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psRGXTQReleaseSharedMemoryIN->hPMRMem,
					      PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE);
	if (unlikely((psRGXTQReleaseSharedMemoryOUT->eError != PVRSRV_OK) &&
		     (psRGXTQReleaseSharedMemoryOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psRGXTQReleaseSharedMemoryOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXTQReleaseSharedMemoryOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXTQReleaseSharedMemory_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXTQReleaseSharedMemory_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetTransferContextProperty(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psRGXSetTransferContextPropertyIN_UI8,
					  IMG_UINT8 * psRGXSetTransferContextPropertyOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPROPERTY *psRGXSetTransferContextPropertyIN =
	    (PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetTransferContextPropertyIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPROPERTY *psRGXSetTransferContextPropertyOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetTransferContextPropertyOUT_UI8, 0);

	IMG_HANDLE hTransferContext = psRGXSetTransferContextPropertyIN->hTransferContext;
	RGX_SERVER_TQ_CONTEXT *psTransferContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetTransferContextPropertyOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psTransferContextInt,
				       hTransferContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSetTransferContextPropertyOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetTransferContextProperty_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetTransferContextPropertyOUT->eError =
	    PVRSRVRGXSetTransferContextPropertyKM(psTransferContextInt,
						  psRGXSetTransferContextPropertyIN->ui32Property,
						  psRGXSetTransferContextPropertyIN->ui64Input,
						  &psRGXSetTransferContextPropertyOUT->ui64Output);

RGXSetTransferContextProperty_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psTransferContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hTransferContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

#endif /* SUPPORT_RGXTQ_BRIDGE */

#if defined(SUPPORT_RGXTQ_BRIDGE)
PVRSRV_ERROR InitRGXTQBridge(void);
void DeinitRGXTQBridge(void);

/*
 * Register all RGXTQ functions with services
 */
PVRSRV_ERROR InitRGXTQBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT,
			      PVRSRVBridgeRGXCreateTransferContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT,
			      PVRSRVBridgeRGXDestroyTransferContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ,
			      PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY,
			      PVRSRVBridgeRGXSetTransferContextPriority, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER2,
			      PVRSRVBridgeRGXSubmitTransfer2, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXTQGETSHAREDMEMORY,
			      PVRSRVBridgeRGXTQGetSharedMemory, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXTQRELEASESHAREDMEMORY,
			      PVRSRVBridgeRGXTQReleaseSharedMemory, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ,
			      PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPROPERTY,
			      PVRSRVBridgeRGXSetTransferContextProperty, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxtq functions with services
 */
void DeinitRGXTQBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ,
				PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER2);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXTQGETSHAREDMEMORY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ, PVRSRV_BRIDGE_RGXTQ_RGXTQRELEASESHAREDMEMORY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXTQ,
				PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPROPERTY);

}
#else /* SUPPORT_RGXTQ_BRIDGE */
/* This bridge is conditional on SUPPORT_RGXTQ_BRIDGE - when not defined,
 * do not populate the dispatch table with its functions
 */
#define InitRGXTQBridge() \
	PVRSRV_OK

#define DeinitRGXTQBridge()

#endif /* SUPPORT_RGXTQ_BRIDGE */
