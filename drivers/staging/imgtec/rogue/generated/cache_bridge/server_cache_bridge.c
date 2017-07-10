/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "cache_km.h"


#include "common_cache_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#include "rgx_bridge.h"
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>






/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgeCacheOpQueue(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_CACHEOPQUEUE *psCacheOpQueueIN,
					  PVRSRV_BRIDGE_OUT_CACHEOPQUEUE *psCacheOpQueueOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * *psPMRInt = NULL;
	IMG_HANDLE *hPMRInt2 = NULL;
	IMG_DEVMEM_OFFSET_T *uiOffsetInt = NULL;
	IMG_DEVMEM_SIZE_T *uiSizeInt = NULL;
	PVRSRV_CACHE_OP *iuCacheOpInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psCacheOpQueueIN->ui32NumCacheOps * sizeof(PMR *)) +
			(psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE)) +
			(psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T)) +
			(psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T)) +
			(psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psCacheOpQueueIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psCacheOpQueueIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psCacheOpQueueOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto CacheOpQueue_exit;
			}
		}
	}

	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		psPMRInt = (PMR **)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(PMR *);
		hPMRInt2 = (IMG_HANDLE *)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset); 
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE);
	}

			/* Copy the data over */
			if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE) > 0)
			{
				if ( OSCopyFromUser(NULL, hPMRInt2, psCacheOpQueueIN->phPMR, psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_HANDLE)) != PVRSRV_OK )
				{
					psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto CacheOpQueue_exit;
				}
			}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		uiOffsetInt = (IMG_DEVMEM_OFFSET_T*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T);
	}

			/* Copy the data over */
			if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T) > 0)
			{
				if ( OSCopyFromUser(NULL, uiOffsetInt, psCacheOpQueueIN->puiOffset, psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_OFFSET_T)) != PVRSRV_OK )
				{
					psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto CacheOpQueue_exit;
				}
			}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		uiSizeInt = (IMG_DEVMEM_SIZE_T*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T);
	}

			/* Copy the data over */
			if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T) > 0)
			{
				if ( OSCopyFromUser(NULL, uiSizeInt, psCacheOpQueueIN->puiSize, psCacheOpQueueIN->ui32NumCacheOps * sizeof(IMG_DEVMEM_SIZE_T)) != PVRSRV_OK )
				{
					psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto CacheOpQueue_exit;
				}
			}
	if (psCacheOpQueueIN->ui32NumCacheOps != 0)
	{
		iuCacheOpInt = (PVRSRV_CACHE_OP*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP);
	}

			/* Copy the data over */
			if (psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP) > 0)
			{
				if ( OSCopyFromUser(NULL, iuCacheOpInt, psCacheOpQueueIN->piuCacheOp, psCacheOpQueueIN->ui32NumCacheOps * sizeof(PVRSRV_CACHE_OP)) != PVRSRV_OK )
				{
					psCacheOpQueueOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto CacheOpQueue_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





	{
		IMG_UINT32 i;

		for (i=0;i<psCacheOpQueueIN->ui32NumCacheOps;i++)
		{
				{
					/* Look up the address from the handle */
					psCacheOpQueueOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt[i],
											hPMRInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psCacheOpQueueOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto CacheOpQueue_exit;
					}
				}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psCacheOpQueueOUT->eError =
		CacheOpQueue(
					psCacheOpQueueIN->ui32NumCacheOps,
					psPMRInt,
					uiOffsetInt,
					uiSizeInt,
					iuCacheOpInt,
					&psCacheOpQueueOUT->ui32CacheOpSeqNum);




CacheOpQueue_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






	{
		IMG_UINT32 i;

		for (i=0;i<psCacheOpQueueIN->ui32NumCacheOps;i++)
		{
				{
					/* Unreference the previously looked up handle */
						if(psPMRInt[i])
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMRInt2[i],
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
		}
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

	/* Allocated space should be equal to the last updated offset */
	PVR_ASSERT(ui32BufferSize == ui32NextOffset);

#if defined(INTEGRITY_OS)
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeCacheOpExec(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_CACHEOPEXEC *psCacheOpExecIN,
					  PVRSRV_BRIDGE_OUT_CACHEOPEXEC *psCacheOpExecOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psCacheOpExecIN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psCacheOpExecOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psCacheOpExecOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto CacheOpExec_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psCacheOpExecOUT->eError =
		CacheOpExec(
					psPMRInt,
					psCacheOpExecIN->uiOffset,
					psCacheOpExecIN->uiSize,
					psCacheOpExecIN->iuCacheOp);




CacheOpExec_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeCacheOpSetTimeline(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_CACHEOPSETTIMELINE *psCacheOpSetTimelineIN,
					  PVRSRV_BRIDGE_OUT_CACHEOPSETTIMELINE *psCacheOpSetTimelineOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psConnection);





	psCacheOpSetTimelineOUT->eError =
		CacheOpSetTimeline(
					psCacheOpSetTimelineIN->i32OpTimeline);








	return 0;
}


static IMG_INT
PVRSRVBridgeCacheOpLog(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_CACHEOPLOG *psCacheOpLogIN,
					  PVRSRV_BRIDGE_OUT_CACHEOPLOG *psCacheOpLogOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psCacheOpLogIN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psCacheOpLogOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psCacheOpLogOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto CacheOpLog_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psCacheOpLogOUT->eError =
		CacheOpLog(
					psPMRInt,
					psCacheOpLogIN->uiOffset,
					psCacheOpLogIN->uiSize,
					psCacheOpLogIN->i64QueuedTimeUs,
					psCacheOpLogIN->i64ExecuteTimeUs,
					psCacheOpLogIN->iuCacheOp);




CacheOpLog_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeCacheOpGetLineSize(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_CACHEOPGETLINESIZE *psCacheOpGetLineSizeIN,
					  PVRSRV_BRIDGE_OUT_CACHEOPGETLINESIZE *psCacheOpGetLineSizeOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psCacheOpGetLineSizeIN);





	psCacheOpGetLineSizeOUT->eError =
		CacheOpGetLineSize(
					&psCacheOpGetLineSizeOUT->ui32L1DataCacheLineSize);








	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitCACHEBridge(void);
PVRSRV_ERROR DeinitCACHEBridge(void);

/*
 * Register all CACHE functions with services
 */
PVRSRV_ERROR InitCACHEBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPQUEUE, PVRSRVBridgeCacheOpQueue,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPEXEC, PVRSRVBridgeCacheOpExec,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPSETTIMELINE, PVRSRVBridgeCacheOpSetTimeline,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPLOG, PVRSRVBridgeCacheOpLog,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE, PVRSRV_BRIDGE_CACHE_CACHEOPGETLINESIZE, PVRSRVBridgeCacheOpGetLineSize,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all cache functions with services
 */
PVRSRV_ERROR DeinitCACHEBridge(void)
{
	return PVRSRV_OK;
}
