/*******************************************************************************
@File
@Title          Server bridge for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for sync
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

#include "sync.h"
#include "sync_server.h"
#include "pdump.h"
#include "pvrsrv_sync_km.h"
#include "sync_fallback_server.h"
#include "sync_checkpoint.h"

#include "common_sync_bridge.h"

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

static PVRSRV_ERROR _AllocSyncPrimitiveBlockpsSyncHandleIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVFreeSyncPrimitiveBlockKM((SYNC_PRIMITIVE_BLOCK *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeAllocSyncPrimitiveBlock(IMG_UINT32 ui32DispatchTableEntry,
				    IMG_UINT8 * psAllocSyncPrimitiveBlockIN_UI8,
				    IMG_UINT8 * psAllocSyncPrimitiveBlockOUT_UI8,
				    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK *psAllocSyncPrimitiveBlockIN =
	    (PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK *)
	    IMG_OFFSET_ADDR(psAllocSyncPrimitiveBlockIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK *psAllocSyncPrimitiveBlockOUT =
	    (PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK *)
	    IMG_OFFSET_ADDR(psAllocSyncPrimitiveBlockOUT_UI8, 0);

	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;
	PMR *pshSyncPMRInt = NULL;

	PVR_UNREFERENCED_PARAMETER(psAllocSyncPrimitiveBlockIN);

	psAllocSyncPrimitiveBlockOUT->hSyncHandle = NULL;

	psAllocSyncPrimitiveBlockOUT->eError =
	    PVRSRVAllocSyncPrimitiveBlockKM(psConnection, OSGetDevNode(psConnection),
					    &psSyncHandleInt,
					    &psAllocSyncPrimitiveBlockOUT->ui32SyncPrimVAddr,
					    &psAllocSyncPrimitiveBlockOUT->ui32SyncPrimBlockSize,
					    &pshSyncPMRInt);
	/* Exit early if bridged call fails */
	if (unlikely(psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK))
	{
		goto AllocSyncPrimitiveBlock_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psAllocSyncPrimitiveBlockOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
									 &psAllocSyncPrimitiveBlockOUT->
									 hSyncHandle,
									 (void *)psSyncHandleInt,
									 PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
									 PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
									 (PFN_HANDLE_RELEASE) &
									 _AllocSyncPrimitiveBlockpsSyncHandleIntRelease);
	if (unlikely(psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto AllocSyncPrimitiveBlock_exit;
	}

	psAllocSyncPrimitiveBlockOUT->eError =
	    PVRSRVAllocSubHandleUnlocked(psConnection->psHandleBase,
					 &psAllocSyncPrimitiveBlockOUT->hhSyncPMR,
					 (void *)pshSyncPMRInt,
					 PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
					 PVRSRV_HANDLE_ALLOC_FLAG_NONE,
					 psAllocSyncPrimitiveBlockOUT->hSyncHandle);
	if (unlikely(psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto AllocSyncPrimitiveBlock_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

AllocSyncPrimitiveBlock_exit:

	if (psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		if (psAllocSyncPrimitiveBlockOUT->hSyncHandle)
		{
			PVRSRV_ERROR eError;

			/* Lock over handle creation cleanup. */
			LockHandle(psConnection->psHandleBase);

			eError = PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							     (IMG_HANDLE)
							     psAllocSyncPrimitiveBlockOUT->
							     hSyncHandle,
							     PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
			if (unlikely((eError != PVRSRV_OK) && (eError != PVRSRV_ERROR_RETRY)))
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "%s: %s", __func__, PVRSRVGetErrorString(eError)));
			}
			/* Releasing the handle should free/destroy/release the resource.
			 * This should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

			/* Avoid freeing/destroying/releasing the resource a second time below */
			psSyncHandleInt = NULL;
			/* Release now we have cleaned up creation handles. */
			UnlockHandle(psConnection->psHandleBase);

		}

		if (psSyncHandleInt)
		{
			PVRSRVFreeSyncPrimitiveBlockKM(psSyncHandleInt);
		}
	}

	return 0;
}

static IMG_INT
PVRSRVBridgeFreeSyncPrimitiveBlock(IMG_UINT32 ui32DispatchTableEntry,
				   IMG_UINT8 * psFreeSyncPrimitiveBlockIN_UI8,
				   IMG_UINT8 * psFreeSyncPrimitiveBlockOUT_UI8,
				   CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK *psFreeSyncPrimitiveBlockIN =
	    (PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK *)
	    IMG_OFFSET_ADDR(psFreeSyncPrimitiveBlockIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK *psFreeSyncPrimitiveBlockOUT =
	    (PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK *)
	    IMG_OFFSET_ADDR(psFreeSyncPrimitiveBlockOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psFreeSyncPrimitiveBlockOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psFreeSyncPrimitiveBlockIN->hSyncHandle,
					    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	if (unlikely((psFreeSyncPrimitiveBlockOUT->eError != PVRSRV_OK) &&
		     (psFreeSyncPrimitiveBlockOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psFreeSyncPrimitiveBlockOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto FreeSyncPrimitiveBlock_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

FreeSyncPrimitiveBlock_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimSet(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psSyncPrimSetIN_UI8,
			IMG_UINT8 * psSyncPrimSetOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCPRIMSET *psSyncPrimSetIN =
	    (PVRSRV_BRIDGE_IN_SYNCPRIMSET *) IMG_OFFSET_ADDR(psSyncPrimSetIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCPRIMSET *psSyncPrimSetOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCPRIMSET *) IMG_OFFSET_ADDR(psSyncPrimSetOUT_UI8, 0);

	IMG_HANDLE hSyncHandle = psSyncPrimSetIN->hSyncHandle;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncPrimSetOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSyncHandleInt,
				       hSyncHandle,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncPrimSetOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncPrimSet_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncPrimSetOUT->eError =
	    PVRSRVSyncPrimSetKM(psSyncHandleInt,
				psSyncPrimSetIN->ui32Index, psSyncPrimSetIN->ui32Value);

SyncPrimSet_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSyncHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSyncHandle, PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#if defined(PDUMP)

static IMG_INT
PVRSRVBridgeSyncPrimPDump(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psSyncPrimPDumpIN_UI8,
			  IMG_UINT8 * psSyncPrimPDumpOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP *psSyncPrimPDumpIN =
	    (PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP *) IMG_OFFSET_ADDR(psSyncPrimPDumpIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP *psSyncPrimPDumpOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP *) IMG_OFFSET_ADDR(psSyncPrimPDumpOUT_UI8, 0);

	IMG_HANDLE hSyncHandle = psSyncPrimPDumpIN->hSyncHandle;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncPrimPDumpOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSyncHandleInt,
				       hSyncHandle,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncPrimPDumpOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncPrimPDump_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncPrimPDumpOUT->eError =
	    PVRSRVSyncPrimPDumpKM(psSyncHandleInt, psSyncPrimPDumpIN->ui32Offset);

SyncPrimPDump_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSyncHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSyncHandle, PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#else
#define PVRSRVBridgeSyncPrimPDump NULL
#endif

#if defined(PDUMP)

static IMG_INT
PVRSRVBridgeSyncPrimPDumpValue(IMG_UINT32 ui32DispatchTableEntry,
			       IMG_UINT8 * psSyncPrimPDumpValueIN_UI8,
			       IMG_UINT8 * psSyncPrimPDumpValueOUT_UI8,
			       CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE *psSyncPrimPDumpValueIN =
	    (PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE *) IMG_OFFSET_ADDR(psSyncPrimPDumpValueIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE *psSyncPrimPDumpValueOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE *) IMG_OFFSET_ADDR(psSyncPrimPDumpValueOUT_UI8,
								     0);

	IMG_HANDLE hSyncHandle = psSyncPrimPDumpValueIN->hSyncHandle;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncPrimPDumpValueOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSyncHandleInt,
				       hSyncHandle,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncPrimPDumpValueOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncPrimPDumpValue_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncPrimPDumpValueOUT->eError =
	    PVRSRVSyncPrimPDumpValueKM(psSyncHandleInt,
				       psSyncPrimPDumpValueIN->ui32Offset,
				       psSyncPrimPDumpValueIN->ui32Value);

SyncPrimPDumpValue_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSyncHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSyncHandle, PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#else
#define PVRSRVBridgeSyncPrimPDumpValue NULL
#endif

#if defined(PDUMP)

static IMG_INT
PVRSRVBridgeSyncPrimPDumpPol(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psSyncPrimPDumpPolIN_UI8,
			     IMG_UINT8 * psSyncPrimPDumpPolOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL *psSyncPrimPDumpPolIN =
	    (PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL *) IMG_OFFSET_ADDR(psSyncPrimPDumpPolIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL *psSyncPrimPDumpPolOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL *) IMG_OFFSET_ADDR(psSyncPrimPDumpPolOUT_UI8, 0);

	IMG_HANDLE hSyncHandle = psSyncPrimPDumpPolIN->hSyncHandle;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncPrimPDumpPolOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSyncHandleInt,
				       hSyncHandle,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncPrimPDumpPolOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncPrimPDumpPol_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncPrimPDumpPolOUT->eError =
	    PVRSRVSyncPrimPDumpPolKM(psSyncHandleInt,
				     psSyncPrimPDumpPolIN->ui32Offset,
				     psSyncPrimPDumpPolIN->ui32Value,
				     psSyncPrimPDumpPolIN->ui32Mask,
				     psSyncPrimPDumpPolIN->eOperator,
				     psSyncPrimPDumpPolIN->uiPDumpFlags);

SyncPrimPDumpPol_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSyncHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSyncHandle, PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#else
#define PVRSRVBridgeSyncPrimPDumpPol NULL
#endif

#if defined(PDUMP)

static IMG_INT
PVRSRVBridgeSyncPrimPDumpCBP(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psSyncPrimPDumpCBPIN_UI8,
			     IMG_UINT8 * psSyncPrimPDumpCBPOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP *psSyncPrimPDumpCBPIN =
	    (PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP *) IMG_OFFSET_ADDR(psSyncPrimPDumpCBPIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP *psSyncPrimPDumpCBPOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP *) IMG_OFFSET_ADDR(psSyncPrimPDumpCBPOUT_UI8, 0);

	IMG_HANDLE hSyncHandle = psSyncPrimPDumpCBPIN->hSyncHandle;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncPrimPDumpCBPOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psSyncHandleInt,
				       hSyncHandle,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncPrimPDumpCBPOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncPrimPDumpCBP_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncPrimPDumpCBPOUT->eError =
	    PVRSRVSyncPrimPDumpCBPKM(psSyncHandleInt,
				     psSyncPrimPDumpCBPIN->ui32Offset,
				     psSyncPrimPDumpCBPIN->uiWriteOffset,
				     psSyncPrimPDumpCBPIN->uiPacketSize,
				     psSyncPrimPDumpCBPIN->uiBufferSize);

SyncPrimPDumpCBP_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psSyncHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hSyncHandle, PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

#else
#define PVRSRVBridgeSyncPrimPDumpCBP NULL
#endif

static IMG_INT
PVRSRVBridgeSyncAllocEvent(IMG_UINT32 ui32DispatchTableEntry,
			   IMG_UINT8 * psSyncAllocEventIN_UI8,
			   IMG_UINT8 * psSyncAllocEventOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCALLOCEVENT *psSyncAllocEventIN =
	    (PVRSRV_BRIDGE_IN_SYNCALLOCEVENT *) IMG_OFFSET_ADDR(psSyncAllocEventIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCALLOCEVENT *psSyncAllocEventOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCALLOCEVENT *) IMG_OFFSET_ADDR(psSyncAllocEventOUT_UI8, 0);

	IMG_CHAR *uiClassNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psSyncAllocEventIN->ui32ClassNameSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psSyncAllocEventIN->ui32ClassNameSize > PVRSRV_SYNC_NAME_LENGTH))
	{
		psSyncAllocEventOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto SyncAllocEvent_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psSyncAllocEventIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psSyncAllocEventIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psSyncAllocEventOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto SyncAllocEvent_exit;
			}
		}
	}

	if (psSyncAllocEventIN->ui32ClassNameSize != 0)
	{
		uiClassNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psSyncAllocEventIN->ui32ClassNameSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psSyncAllocEventIN->ui32ClassNameSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiClassNameInt, (const void __user *)psSyncAllocEventIN->puiClassName,
		     psSyncAllocEventIN->ui32ClassNameSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psSyncAllocEventOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto SyncAllocEvent_exit;
		}
		((IMG_CHAR *)
		 uiClassNameInt)[(psSyncAllocEventIN->ui32ClassNameSize * sizeof(IMG_CHAR)) - 1] =
       '\0';
	}

	psSyncAllocEventOUT->eError =
	    PVRSRVSyncAllocEventKM(psConnection, OSGetDevNode(psConnection),
				   psSyncAllocEventIN->bServerSync,
				   psSyncAllocEventIN->ui32FWAddr,
				   psSyncAllocEventIN->ui32ClassNameSize, uiClassNameInt);

SyncAllocEvent_exit:

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
PVRSRVBridgeSyncFreeEvent(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psSyncFreeEventIN_UI8,
			  IMG_UINT8 * psSyncFreeEventOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCFREEEVENT *psSyncFreeEventIN =
	    (PVRSRV_BRIDGE_IN_SYNCFREEEVENT *) IMG_OFFSET_ADDR(psSyncFreeEventIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCFREEEVENT *psSyncFreeEventOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCFREEEVENT *) IMG_OFFSET_ADDR(psSyncFreeEventOUT_UI8, 0);

	psSyncFreeEventOUT->eError =
	    PVRSRVSyncFreeEventKM(psConnection, OSGetDevNode(psConnection),
				  psSyncFreeEventIN->ui32FWAddr);

	return 0;
}

#if defined(PDUMP)

static IMG_INT
PVRSRVBridgeSyncCheckpointSignalledPDumpPol(IMG_UINT32 ui32DispatchTableEntry,
					    IMG_UINT8 * psSyncCheckpointSignalledPDumpPolIN_UI8,
					    IMG_UINT8 * psSyncCheckpointSignalledPDumpPolOUT_UI8,
					    CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCCHECKPOINTSIGNALLEDPDUMPPOL *psSyncCheckpointSignalledPDumpPolIN =
	    (PVRSRV_BRIDGE_IN_SYNCCHECKPOINTSIGNALLEDPDUMPPOL *)
	    IMG_OFFSET_ADDR(psSyncCheckpointSignalledPDumpPolIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCCHECKPOINTSIGNALLEDPDUMPPOL *psSyncCheckpointSignalledPDumpPolOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCCHECKPOINTSIGNALLEDPDUMPPOL *)
	    IMG_OFFSET_ADDR(psSyncCheckpointSignalledPDumpPolOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psSyncCheckpointSignalledPDumpPolOUT->eError =
	    PVRSRVSyncCheckpointSignalledPDumpPolKM(psSyncCheckpointSignalledPDumpPolIN->hFence);

	return 0;
}

#else
#define PVRSRVBridgeSyncCheckpointSignalledPDumpPol NULL
#endif

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitSYNCBridge(void);
PVRSRV_ERROR DeinitSYNCBridge(void);

/*
 * Register all SYNC functions with services
 */
PVRSRV_ERROR InitSYNCBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK,
			      PVRSRVBridgeAllocSyncPrimitiveBlock, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK,
			      PVRSRVBridgeFreeSyncPrimitiveBlock, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMSET,
			      PVRSRVBridgeSyncPrimSet, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP,
			      PVRSRVBridgeSyncPrimPDump, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE,
			      PVRSRVBridgeSyncPrimPDumpValue, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL,
			      PVRSRVBridgeSyncPrimPDumpPol, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP,
			      PVRSRVBridgeSyncPrimPDumpCBP, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCALLOCEVENT,
			      PVRSRVBridgeSyncAllocEvent, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCFREEEVENT,
			      PVRSRVBridgeSyncFreeEvent, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC,
			      PVRSRV_BRIDGE_SYNC_SYNCCHECKPOINTSIGNALLEDPDUMPPOL,
			      PVRSRVBridgeSyncCheckpointSignalledPDumpPol, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all sync functions with services
 */
PVRSRV_ERROR DeinitSYNCBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMSET);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCALLOCEVENT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC, PVRSRV_BRIDGE_SYNC_SYNCFREEEVENT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNC,
				PVRSRV_BRIDGE_SYNC_SYNCCHECKPOINTSIGNALLEDPDUMPPOL);

	return PVRSRV_OK;
}
