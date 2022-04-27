/*******************************************************************************
@File
@Title          Server bridge for rgxkicksync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for rgxkicksync
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

#include "rgxkicksync.h"

#include "common_rgxkicksync_bridge.h"

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

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _RGXCreateKickSyncContextpsKickSyncContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVRGXDestroyKickSyncContextKM((RGX_SERVER_KICKSYNC_CONTEXT *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRGXCreateKickSyncContext(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psRGXCreateKickSyncContextIN_UI8,
				     IMG_UINT8 * psRGXCreateKickSyncContextOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXCREATEKICKSYNCCONTEXT *psRGXCreateKickSyncContextIN =
	    (PVRSRV_BRIDGE_IN_RGXCREATEKICKSYNCCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateKickSyncContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXCREATEKICKSYNCCONTEXT *psRGXCreateKickSyncContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXCREATEKICKSYNCCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXCreateKickSyncContextOUT_UI8, 0);

	IMG_HANDLE hPrivData = psRGXCreateKickSyncContextIN->hPrivData;
	IMG_HANDLE hPrivDataInt = NULL;
	RGX_SERVER_KICKSYNC_CONTEXT *psKickSyncContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXCreateKickSyncContextOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&hPrivDataInt,
				       hPrivData, PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA, IMG_TRUE);
	if (unlikely(psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateKickSyncContext_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXCreateKickSyncContextOUT->eError =
	    PVRSRVRGXCreateKickSyncContextKM(psConnection, OSGetDevNode(psConnection),
					     hPrivDataInt,
					     psRGXCreateKickSyncContextIN->ui32PackedCCBSizeU88,
					     psRGXCreateKickSyncContextIN->ui32ContextFlags,
					     &psKickSyncContextInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK))
	{
		goto RGXCreateKickSyncContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRGXCreateKickSyncContextOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
				      &psRGXCreateKickSyncContextOUT->hKickSyncContext,
				      (void *)psKickSyncContextInt,
				      PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _RGXCreateKickSyncContextpsKickSyncContextIntRelease);
	if (unlikely(psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXCreateKickSyncContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXCreateKickSyncContext_exit:

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

	if (psRGXCreateKickSyncContextOUT->eError != PVRSRV_OK)
	{
		if (psKickSyncContextInt)
		{
			PVRSRVRGXDestroyKickSyncContextKM(psKickSyncContextInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXDestroyKickSyncContext(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psRGXDestroyKickSyncContextIN_UI8,
				      IMG_UINT8 * psRGXDestroyKickSyncContextOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXDESTROYKICKSYNCCONTEXT *psRGXDestroyKickSyncContextIN =
	    (PVRSRV_BRIDGE_IN_RGXDESTROYKICKSYNCCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyKickSyncContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXDESTROYKICKSYNCCONTEXT *psRGXDestroyKickSyncContextOUT =
	    (PVRSRV_BRIDGE_OUT_RGXDESTROYKICKSYNCCONTEXT *)
	    IMG_OFFSET_ADDR(psRGXDestroyKickSyncContextOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRGXDestroyKickSyncContextOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRGXDestroyKickSyncContextIN->
					    hKickSyncContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT);
	if (unlikely
	    ((psRGXDestroyKickSyncContextOUT->eError != PVRSRV_OK)
	     && (psRGXDestroyKickSyncContextOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRGXDestroyKickSyncContextOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RGXDestroyKickSyncContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RGXDestroyKickSyncContext_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXKickSync2(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psRGXKickSync2IN_UI8,
			 IMG_UINT8 * psRGXKickSync2OUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXKICKSYNC2 *psRGXKickSync2IN =
	    (PVRSRV_BRIDGE_IN_RGXKICKSYNC2 *) IMG_OFFSET_ADDR(psRGXKickSync2IN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXKICKSYNC2 *psRGXKickSync2OUT =
	    (PVRSRV_BRIDGE_OUT_RGXKICKSYNC2 *) IMG_OFFSET_ADDR(psRGXKickSync2OUT_UI8, 0);

	IMG_HANDLE hKickSyncContext = psRGXKickSync2IN->hKickSyncContext;
	RGX_SERVER_KICKSYNC_CONTEXT *psKickSyncContextInt = NULL;
	SYNC_PRIMITIVE_BLOCK **psUpdateUFODevVarBlockInt = NULL;
	IMG_HANDLE *hUpdateUFODevVarBlockInt2 = NULL;
	IMG_UINT32 *ui32UpdateDevVarOffsetInt = NULL;
	IMG_UINT32 *ui32UpdateValueInt = NULL;
	IMG_CHAR *uiUpdateFenceNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *)) +
	    (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) +
	    (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
	    (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) +
	    (PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psRGXKickSync2IN->ui32ClientUpdateCount > PVRSRV_MAX_DEV_VARS))
	{
		psRGXKickSync2OUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RGXKickSync2_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRGXKickSync2IN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRGXKickSync2IN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRGXKickSync2OUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RGXKickSync2_exit;
			}
		}
	}

	if (psRGXKickSync2IN->ui32ClientUpdateCount != 0)
	{
		psUpdateUFODevVarBlockInt =
		    (SYNC_PRIMITIVE_BLOCK **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(SYNC_PRIMITIVE_BLOCK *);
		hUpdateUFODevVarBlockInt2 =
		    (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hUpdateUFODevVarBlockInt2,
		     (const void __user *)psRGXKickSync2IN->phUpdateUFODevVarBlock,
		     psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psRGXKickSync2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickSync2_exit;
		}
	}
	if (psRGXKickSync2IN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateDevVarOffsetInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32UpdateDevVarOffsetInt,
		     (const void __user *)psRGXKickSync2IN->pui32UpdateDevVarOffset,
		     psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickSync2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickSync2_exit;
		}
	}
	if (psRGXKickSync2IN->ui32ClientUpdateCount != 0)
	{
		ui32UpdateValueInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32UpdateValueInt,
		     (const void __user *)psRGXKickSync2IN->pui32UpdateValue,
		     psRGXKickSync2IN->ui32ClientUpdateCount * sizeof(IMG_UINT32)) != PVRSRV_OK)
		{
			psRGXKickSync2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickSync2_exit;
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
		     (const void __user *)psRGXKickSync2IN->puiUpdateFenceName,
		     PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRGXKickSync2OUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RGXKickSync2_exit;
		}
		((IMG_CHAR *) uiUpdateFenceNameInt)[(PVRSRV_SYNC_NAME_LENGTH * sizeof(IMG_CHAR)) -
						    1] = '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXKickSync2OUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psKickSyncContextInt,
				       hKickSyncContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXKickSync2OUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXKickSync2_exit;
	}

	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickSync2IN->ui32ClientUpdateCount; i++)
		{
			/* Look up the address from the handle */
			psRGXKickSync2OUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psUpdateUFODevVarBlockInt[i],
						       hUpdateUFODevVarBlockInt2[i],
						       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
						       IMG_TRUE);
			if (unlikely(psRGXKickSync2OUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto RGXKickSync2_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXKickSync2OUT->eError =
	    PVRSRVRGXKickSyncKM(psKickSyncContextInt,
				psRGXKickSync2IN->ui32ClientCacheOpSeqNum,
				psRGXKickSync2IN->ui32ClientUpdateCount,
				psUpdateUFODevVarBlockInt,
				ui32UpdateDevVarOffsetInt,
				ui32UpdateValueInt,
				psRGXKickSync2IN->hCheckFenceFD,
				psRGXKickSync2IN->hTimelineFenceFD,
				&psRGXKickSync2OUT->hUpdateFenceFD,
				uiUpdateFenceNameInt, psRGXKickSync2IN->ui32ExtJobRef);

RGXKickSync2_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psKickSyncContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hKickSyncContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT);
	}

	if (hUpdateUFODevVarBlockInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psRGXKickSync2IN->ui32ClientUpdateCount; i++)
		{

			/* Unreference the previously looked up handle */
			if (hUpdateUFODevVarBlockInt2[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hUpdateUFODevVarBlockInt2[i],
							    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if (pArrayArgsBuffer)
#else
	if (!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeRGXSetKickSyncContextProperty(IMG_UINT32 ui32DispatchTableEntry,
					  IMG_UINT8 * psRGXSetKickSyncContextPropertyIN_UI8,
					  IMG_UINT8 * psRGXSetKickSyncContextPropertyOUT_UI8,
					  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RGXSETKICKSYNCCONTEXTPROPERTY *psRGXSetKickSyncContextPropertyIN =
	    (PVRSRV_BRIDGE_IN_RGXSETKICKSYNCCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetKickSyncContextPropertyIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RGXSETKICKSYNCCONTEXTPROPERTY *psRGXSetKickSyncContextPropertyOUT =
	    (PVRSRV_BRIDGE_OUT_RGXSETKICKSYNCCONTEXTPROPERTY *)
	    IMG_OFFSET_ADDR(psRGXSetKickSyncContextPropertyOUT_UI8, 0);

	IMG_HANDLE hKickSyncContext = psRGXSetKickSyncContextPropertyIN->hKickSyncContext;
	RGX_SERVER_KICKSYNC_CONTEXT *psKickSyncContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRGXSetKickSyncContextPropertyOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psKickSyncContextInt,
				       hKickSyncContext,
				       PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT, IMG_TRUE);
	if (unlikely(psRGXSetKickSyncContextPropertyOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RGXSetKickSyncContextProperty_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRGXSetKickSyncContextPropertyOUT->eError =
	    PVRSRVRGXSetKickSyncContextPropertyKM(psKickSyncContextInt,
						  psRGXSetKickSyncContextPropertyIN->ui32Property,
						  psRGXSetKickSyncContextPropertyIN->ui64Input,
						  &psRGXSetKickSyncContextPropertyOUT->ui64Output);

RGXSetKickSyncContextProperty_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psKickSyncContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hKickSyncContext,
					    PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRGXKICKSYNCBridge(void);
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void);

/*
 * Register all RGXKICKSYNC functions with services
 */
PVRSRV_ERROR InitRGXKICKSYNCBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
			      PVRSRV_BRIDGE_RGXKICKSYNC_RGXCREATEKICKSYNCCONTEXT,
			      PVRSRVBridgeRGXCreateKickSyncContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
			      PVRSRV_BRIDGE_RGXKICKSYNC_RGXDESTROYKICKSYNCCONTEXT,
			      PVRSRVBridgeRGXDestroyKickSyncContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC, PVRSRV_BRIDGE_RGXKICKSYNC_RGXKICKSYNC2,
			      PVRSRVBridgeRGXKickSync2, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
			      PVRSRV_BRIDGE_RGXKICKSYNC_RGXSETKICKSYNCCONTEXTPROPERTY,
			      PVRSRVBridgeRGXSetKickSyncContextProperty, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all rgxkicksync functions with services
 */
PVRSRV_ERROR DeinitRGXKICKSYNCBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
				PVRSRV_BRIDGE_RGXKICKSYNC_RGXCREATEKICKSYNCCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
				PVRSRV_BRIDGE_RGXKICKSYNC_RGXDESTROYKICKSYNCCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC, PVRSRV_BRIDGE_RGXKICKSYNC_RGXKICKSYNC2);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RGXKICKSYNC,
				PVRSRV_BRIDGE_RGXKICKSYNC_RGXSETKICKSYNCCONTEXTPROPERTY);

	return PVRSRV_OK;
}
