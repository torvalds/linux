/*******************************************************************************
@File
@Title          Server bridge for synctracking
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for synctracking
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

#include "common_synctracking_bridge.h"

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

static IMG_INT
PVRSRVBridgeSyncRecordRemoveByHandle(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psSyncRecordRemoveByHandleIN_UI8,
				     IMG_UINT8 * psSyncRecordRemoveByHandleOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCRECORDREMOVEBYHANDLE *psSyncRecordRemoveByHandleIN =
	    (PVRSRV_BRIDGE_IN_SYNCRECORDREMOVEBYHANDLE *)
	    IMG_OFFSET_ADDR(psSyncRecordRemoveByHandleIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCRECORDREMOVEBYHANDLE *psSyncRecordRemoveByHandleOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCRECORDREMOVEBYHANDLE *)
	    IMG_OFFSET_ADDR(psSyncRecordRemoveByHandleOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psSyncRecordRemoveByHandleOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psSyncRecordRemoveByHandleIN->hhRecord,
					      PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE);
	if (unlikely((psSyncRecordRemoveByHandleOUT->eError != PVRSRV_OK) &&
		     (psSyncRecordRemoveByHandleOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psSyncRecordRemoveByHandleOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psSyncRecordRemoveByHandleOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto SyncRecordRemoveByHandle_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

SyncRecordRemoveByHandle_exit:

	return 0;
}

static PVRSRV_ERROR _SyncRecordAddpshRecordIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PVRSRVSyncRecordRemoveByHandleKM((SYNC_RECORD_HANDLE) pvData);
	return eError;
}

static_assert(PVRSRV_SYNC_NAME_LENGTH <= IMG_UINT32_MAX,
	      "PVRSRV_SYNC_NAME_LENGTH must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeSyncRecordAdd(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psSyncRecordAddIN_UI8,
			  IMG_UINT8 * psSyncRecordAddOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_SYNCRECORDADD *psSyncRecordAddIN =
	    (PVRSRV_BRIDGE_IN_SYNCRECORDADD *) IMG_OFFSET_ADDR(psSyncRecordAddIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_SYNCRECORDADD *psSyncRecordAddOUT =
	    (PVRSRV_BRIDGE_OUT_SYNCRECORDADD *) IMG_OFFSET_ADDR(psSyncRecordAddOUT_UI8, 0);

	SYNC_RECORD_HANDLE pshRecordInt = NULL;
	IMG_HANDLE hhServerSyncPrimBlock = psSyncRecordAddIN->hhServerSyncPrimBlock;
	SYNC_PRIMITIVE_BLOCK *pshServerSyncPrimBlockInt = NULL;
	IMG_CHAR *uiClassNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psSyncRecordAddIN->ui32ClassNameSize > PVRSRV_SYNC_NAME_LENGTH))
	{
		psSyncRecordAddOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto SyncRecordAdd_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psSyncRecordAddOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto SyncRecordAdd_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psSyncRecordAddIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psSyncRecordAddIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psSyncRecordAddOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto SyncRecordAdd_exit;
			}
		}
	}

	if (psSyncRecordAddIN->ui32ClassNameSize != 0)
	{
		uiClassNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiClassNameInt, (const void __user *)psSyncRecordAddIN->puiClassName,
		     psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psSyncRecordAddOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto SyncRecordAdd_exit;
		}
		((IMG_CHAR *)
		 uiClassNameInt)[(psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR)) - 1] =
       '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psSyncRecordAddOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&pshServerSyncPrimBlockInt,
				       hhServerSyncPrimBlock,
				       PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK, IMG_TRUE);
	if (unlikely(psSyncRecordAddOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncRecordAdd_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psSyncRecordAddOUT->eError =
	    PVRSRVSyncRecordAddKM(psConnection, OSGetDevNode(psConnection),
				  &pshRecordInt,
				  pshServerSyncPrimBlockInt,
				  psSyncRecordAddIN->ui32ui32FwBlockAddr,
				  psSyncRecordAddIN->ui32ui32SyncOffset,
				  psSyncRecordAddIN->bbServerSync,
				  psSyncRecordAddIN->ui32ClassNameSize, uiClassNameInt);
	/* Exit early if bridged call fails */
	if (unlikely(psSyncRecordAddOUT->eError != PVRSRV_OK))
	{
		goto SyncRecordAdd_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psSyncRecordAddOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
							       &psSyncRecordAddOUT->hhRecord,
							       (void *)pshRecordInt,
							       PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE,
							       PVRSRV_HANDLE_ALLOC_FLAG_NONE,
							       (PFN_HANDLE_RELEASE) &
							       _SyncRecordAddpshRecordIntRelease);
	if (unlikely(psSyncRecordAddOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto SyncRecordAdd_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

SyncRecordAdd_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (pshServerSyncPrimBlockInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hhServerSyncPrimBlock,
					    PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psSyncRecordAddOUT->eError != PVRSRV_OK)
	{
		if (pshRecordInt)
		{
			PVRSRVSyncRecordRemoveByHandleKM(pshRecordInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psSyncRecordAddOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitSYNCTRACKINGBridge(void);
void DeinitSYNCTRACKINGBridge(void);

/*
 * Register all SYNCTRACKING functions with services
 */
PVRSRV_ERROR InitSYNCTRACKINGBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCTRACKING,
			      PVRSRV_BRIDGE_SYNCTRACKING_SYNCRECORDREMOVEBYHANDLE,
			      PVRSRVBridgeSyncRecordRemoveByHandle, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCTRACKING, PVRSRV_BRIDGE_SYNCTRACKING_SYNCRECORDADD,
			      PVRSRVBridgeSyncRecordAdd, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all synctracking functions with services
 */
void DeinitSYNCTRACKINGBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNCTRACKING,
				PVRSRV_BRIDGE_SYNCTRACKING_SYNCRECORDREMOVEBYHANDLE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_SYNCTRACKING,
				PVRSRV_BRIDGE_SYNCTRACKING_SYNCRECORDADD);

}
