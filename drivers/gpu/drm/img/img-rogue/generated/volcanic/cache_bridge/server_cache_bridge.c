/*******************************************************************************
@File
@Title          Server bridge for cache
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for cache
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

#include "cache_km.h"

#include "common_cache_bridge.h"

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

static_assert(CACHE_BATCH_MAX <= IMG_UINT32_MAX,
	      "CACHE_BATCH_MAX must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeCacheOpQueue(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psCacheOpQueueIN_UI8,
			 IMG_UINT8 * psCacheOpQueueOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_CACHEOPQUEUE *psCacheOpQueueIN =
	    (PVRSRV_BRIDGE_IN_CACHEOPQUEUE *) IMG_OFFSET_ADDR(psCacheOpQueueIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_CACHEOPQUEUE *psCacheOpQueueOUT =
	    (PVRSRV_BRIDGE_OUT_CACHEOPQUEUE *) IMG_OFFSET_ADDR(psCacheOpQueueOUT_UI8, 0);

	PMR **psPMRInt = NULL;
	IMG_HANDLE *hPMRInt2 = NULL;
	IMG_UINT64 *ui64AddressInt = NULL;
	IMG_DEVMEM_OFFSET_T *uiOffsetInt = NULL;
	IMG_DEVMEM_SIZE_T *uiSizeInt = NULL;
	PVRSRV_CACHE_OP *iuCacheOpInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(PMR *)) +
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE)) +
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_UINT64)) +
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T)) +
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T)) +
	    ((IMG_UINT64) psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP)) + 0;

	if (unlikely(psCacheOpQueueIN->ui32NumCacheOps > CACHE_BATCH_MAX))
	{
		psCacheOpQueueOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto CacheOpQueue_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psCacheOpQueueOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto CacheOpQueue_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psCacheOpQueueIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psCacheOpQueueIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psCacheOpQueueOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto CacheOpQueue_exit;
			}
		}
	}

	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		psPMRInt = (PMR **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		OSCachedMemSet(psPMRInt, 0, psCacheOpQueueIN->ui32NumCacheOps * sizeof(PMR *));
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(PMR *);
		hPMRInt2 = (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hPMRInt2, (const void __user *)psCacheOpQueueIN->phPMR,
		     psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto CacheOpQueue_exit;
		}
	}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		ui64AddressInt = (IMG_UINT64 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_UINT64);
	}

	/* Copy the data over */
	if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_UINT64) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui64AddressInt, (const void __user *)psCacheOpQueueIN->pui64Address,
		     psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_UINT64)) != PVRSRV_OK)
		{
			psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto CacheOpQueue_exit;
		}
	}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		uiOffsetInt =
		    (IMG_DEVMEM_OFFSET_T *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T);
	}

	/* Copy the data over */
	if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiOffsetInt, (const void __user *)psCacheOpQueueIN->puiOffset,
		     psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T)) != PVRSRV_OK)
		{
			psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto CacheOpQueue_exit;
		}
	}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		uiSizeInt = (IMG_DEVMEM_SIZE_T *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T);
	}

	/* Copy the data over */
	if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiSizeInt, (const void __user *)psCacheOpQueueIN->puiSize,
		     psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T)) != PVRSRV_OK)
		{
			psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto CacheOpQueue_exit;
		}
	}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		iuCacheOpInt =
		    (PVRSRV_CACHE_OP *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP);
	}

	/* Copy the data over */
	if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP) > 0)
	{
		if (OSCopyFromUser
		    (NULL, iuCacheOpInt, (const void __user *)psCacheOpQueueIN->piuCacheOp,
		     psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP)) != PVRSRV_OK)
		{
			psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto CacheOpQueue_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	{
		IMG_UINT32 i;

		for (i = 0; i < psCacheOpQueueIN->ui32NumCacheOps; i++)
		{
			/* Look up the address from the handle */
			psCacheOpQueueOUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psPMRInt[i],
						       hPMRInt2[i],
						       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
			if (unlikely(psCacheOpQueueOUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto CacheOpQueue_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psCacheOpQueueOUT->eError =
	    CacheOpQueue(psConnection, OSGetDevNode(psConnection),
			 psCacheOpQueueIN->ui32NumCacheOps,
			 psPMRInt,
			 ui64AddressInt,
			 uiOffsetInt, uiSizeInt, iuCacheOpInt, psCacheOpQueueIN->ui32OpTimeline);

CacheOpQueue_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	if (hPMRInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psCacheOpQueueIN->ui32NumCacheOps; i++)
		{

			/* Unreference the previously looked up handle */
			if (psPMRInt && psPMRInt[i])
			{
				PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
							    hPMRInt2[i],
							    PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
			}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psCacheOpQueueOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeCacheOpExec(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psCacheOpExecIN_UI8,
			IMG_UINT8 * psCacheOpExecOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_CACHEOPEXEC *psCacheOpExecIN =
	    (PVRSRV_BRIDGE_IN_CACHEOPEXEC *) IMG_OFFSET_ADDR(psCacheOpExecIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_CACHEOPEXEC *psCacheOpExecOUT =
	    (PVRSRV_BRIDGE_OUT_CACHEOPEXEC *) IMG_OFFSET_ADDR(psCacheOpExecOUT_UI8, 0);

	IMG_HANDLE hPMR = psCacheOpExecIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psCacheOpExecOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psCacheOpExecOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto CacheOpExec_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psCacheOpExecOUT->eError =
	    CacheOpValExec(psPMRInt,
			   psCacheOpExecIN->ui64Address,
			   psCacheOpExecIN->uiOffset,
			   psCacheOpExecIN->uiSize, psCacheOpExecIN->iuCacheOp);

CacheOpExec_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeCacheOpLog(IMG_UINT32 ui32DispatchTableEntry,
		       IMG_UINT8 * psCacheOpLogIN_UI8,
		       IMG_UINT8 * psCacheOpLogOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_CACHEOPLOG *psCacheOpLogIN =
	    (PVRSRV_BRIDGE_IN_CACHEOPLOG *) IMG_OFFSET_ADDR(psCacheOpLogIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_CACHEOPLOG *psCacheOpLogOUT =
	    (PVRSRV_BRIDGE_OUT_CACHEOPLOG *) IMG_OFFSET_ADDR(psCacheOpLogOUT_UI8, 0);

	IMG_HANDLE hPMR = psCacheOpLogIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psCacheOpLogOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psCacheOpLogOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto CacheOpLog_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psCacheOpLogOUT->eError =
	    CacheOpLog(psPMRInt,
		       psCacheOpLogIN->ui64Address,
		       psCacheOpLogIN->uiOffset,
		       psCacheOpLogIN->uiSize,
		       psCacheOpLogIN->i64StartTime,
		       psCacheOpLogIN->i64EndTime, psCacheOpLogIN->iuCacheOp);

CacheOpLog_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitCACHEBridge(void);
void DeinitCACHEBridge(void);

/*
 * Register all CACHE functions with services
 */
PVRSRV_ERROR InitCACHEBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPQUEUE,
			      PVRSRVBridgeCacheOpQueue, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPEXEC,
			      PVRSRVBridgeCacheOpExec, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPLOG,
			      PVRSRVBridgeCacheOpLog, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all cache functions with services
 */
void DeinitCACHEBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPQUEUE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPEXEC);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPLOG);

}
