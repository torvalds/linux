/**************************************************************************/ /*!
@File
@Title          Services pool implementation
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides a generic pool implementation
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
*/ /***************************************************************************/

#include "img_defs.h"
#include "pvr_debug.h"
#include "pvrsrv.h"
#include "lock.h"
#include "dllist.h"
#include "allocmem.h"

struct _PVRSRV_POOL_
{
	POS_LOCK hLock;
	/* total max number of permitted entries in the pool */
	IMG_UINT uiMaxEntries;
	/* currently number of pool entries created. these may be in the pool
	 * or in-use
	 */
	IMG_UINT uiNumBusy;
	/* number of not-in-use entries currently free in the pool */
	IMG_UINT uiNumFree;

	DLLIST_NODE sFreeList;

	const IMG_CHAR *pszName;

	PVRSRV_POOL_ALLOC_FUNC *pfnAlloc;
	PVRSRV_POOL_FREE_FUNC *pfnFree;
	void *pvPrivData;
};

typedef struct _PVRSRV_POOL_ENTRY_
{
	DLLIST_NODE sNode;
	void *pvData;
} PVRSRV_POOL_ENTRY;

PVRSRV_ERROR PVRSRVPoolCreate(PVRSRV_POOL_ALLOC_FUNC *pfnAlloc,
					PVRSRV_POOL_FREE_FUNC *pfnFree,
					IMG_UINT32 ui32MaxEntries,
					const IMG_CHAR *pszName,
					void *pvPrivData,
					PVRSRV_POOL **ppsPool)
{
	PVRSRV_POOL *psPool;
	PVRSRV_ERROR eError;

	psPool = OSAllocMem(sizeof(PVRSRV_POOL));
	PVR_GOTO_IF_NOMEM(psPool, eError, err_alloc);

	eError = OSLockCreate(&psPool->hLock);

	PVR_GOTO_IF_ERROR(eError, err_lock_create);

	psPool->uiMaxEntries = ui32MaxEntries;
	psPool->uiNumBusy = 0;
	psPool->uiNumFree = 0;
	psPool->pfnAlloc = pfnAlloc;
	psPool->pfnFree = pfnFree;
	psPool->pvPrivData = pvPrivData;
	psPool->pszName = pszName;

	dllist_init(&psPool->sFreeList);

	*ppsPool = psPool;

	return PVRSRV_OK;

err_lock_create:
	OSFreeMem(psPool);
err_alloc:
	return eError;
}

static PVRSRV_ERROR _DestroyPoolEntry(PVRSRV_POOL *psPool,
					PVRSRV_POOL_ENTRY *psEntry)
{
	psPool->pfnFree(psPool->pvPrivData, psEntry->pvData);
	OSFreeMem(psEntry);

	return PVRSRV_OK;
}

void PVRSRVPoolDestroy(PVRSRV_POOL *psPool)
{
	if (psPool->uiNumBusy != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Attempt to destroy pool %s "
						"with %u entries still in use",
						__func__,
						psPool->pszName,
						psPool->uiNumBusy));
		return;
	}

	OSLockDestroy(psPool->hLock);

	if (psPool->uiNumFree)
	{
		PVRSRV_POOL_ENTRY *psEntry;
		DLLIST_NODE *psChosenNode;

		psChosenNode = dllist_get_next_node(&psPool->sFreeList);

		while (psChosenNode)
		{
			dllist_remove_node(psChosenNode);

			psEntry = IMG_CONTAINER_OF(psChosenNode, PVRSRV_POOL_ENTRY, sNode);
			_DestroyPoolEntry(psPool, psEntry);

			psPool->uiNumFree--;

			psChosenNode = dllist_get_next_node(&psPool->sFreeList);
		}

		PVR_ASSERT(psPool->uiNumFree == 0);
	}

	OSFreeMem(psPool);
}

static PVRSRV_ERROR _CreateNewPoolEntry(PVRSRV_POOL *psPool,
					PVRSRV_POOL_ENTRY **ppsEntry)
{
	PVRSRV_POOL_ENTRY *psNewEntry;
	PVRSRV_ERROR eError;

	psNewEntry = OSAllocMem(sizeof(PVRSRV_POOL_ENTRY));
	PVR_GOTO_IF_NOMEM(psNewEntry, eError, err_allocmem);

	dllist_init(&psNewEntry->sNode);

	eError = psPool->pfnAlloc(psPool->pvPrivData, &psNewEntry->pvData);

	PVR_GOTO_IF_ERROR(eError, err_pfn_alloc);

	*ppsEntry = psNewEntry;

	return PVRSRV_OK;

err_pfn_alloc:
	OSFreeMem(psNewEntry);
err_allocmem:
	return eError;
}

PVRSRV_ERROR PVRSRVPoolGet(PVRSRV_POOL *psPool,
					PVRSRV_POOL_TOKEN *hToken,
					void **ppvDataOut)
{
	PVRSRV_POOL_ENTRY *psEntry;
	PVRSRV_ERROR eError = PVRSRV_OK;
	DLLIST_NODE *psChosenNode;

	OSLockAcquire(psPool->hLock);

	psChosenNode = dllist_get_next_node(&psPool->sFreeList);
	if (unlikely(psChosenNode == NULL))
	{
		/* no available elements in the pool. try to create one */

		eError = _CreateNewPoolEntry(psPool, &psEntry);

		PVR_GOTO_IF_ERROR(eError, out_unlock);
	}
	else
	{
		dllist_remove_node(psChosenNode);

		psEntry = IMG_CONTAINER_OF(psChosenNode, PVRSRV_POOL_ENTRY, sNode);

		psPool->uiNumFree--;
	}

#if defined(DEBUG) || defined(SUPPORT_VALIDATION)
	/* Don't poison the IN buffer as that is copied from client and would be
	 * waste of cycles.
	 */
	OSCachedMemSet(((IMG_PBYTE)psEntry->pvData)+PVRSRV_MAX_BRIDGE_IN_SIZE,
			PVRSRV_POISON_ON_ALLOC_VALUE, PVRSRV_MAX_BRIDGE_OUT_SIZE);
#endif

	psPool->uiNumBusy++;
	*hToken = psEntry;
	*ppvDataOut = psEntry->pvData;

out_unlock:
	OSLockRelease(psPool->hLock);
	return eError;
}

PVRSRV_ERROR PVRSRVPoolPut(PVRSRV_POOL *psPool, PVRSRV_POOL_TOKEN hToken)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_POOL_ENTRY *psEntry = hToken;

	PVR_ASSERT(psPool->uiNumBusy > 0);

	OSLockAcquire(psPool->hLock);

	/* put this entry in the pool if the pool has space,
	 * otherwise free it
	 */
	if (psPool->uiNumFree < psPool->uiMaxEntries)
	{
		dllist_add_to_tail(&psPool->sFreeList, &psEntry->sNode);
		psPool->uiNumFree++;
	}
	else
	{
		eError = _DestroyPoolEntry(psPool, psEntry);
	}

	psPool->uiNumBusy--;

	OSLockRelease(psPool->hLock);

	return eError;
}
