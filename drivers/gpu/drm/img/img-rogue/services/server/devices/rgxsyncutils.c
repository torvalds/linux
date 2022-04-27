/*************************************************************************/ /*!
@File           rgxsyncutils.c
@Title          RGX Sync Utilities
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX Sync helper functions
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
#include "rgxsyncutils.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"
#include "allocmem.h"

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"

//#define TA3D_CHECKPOINT_DEBUG

#if defined(TA3D_CHECKPOINT_DEBUG)
#define CHKPT_DBG(X) PVR_DPF(X)
static
void _DebugSyncValues(IMG_UINT32 *pui32UpdateValues,
					  IMG_UINT32 ui32Count)
{
	IMG_UINT32 iii;
	IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pui32UpdateValues;

	for (iii = 0; iii < ui32Count; iii++)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", __func__, iii, (void*)pui32Tmp, *pui32Tmp));
		pui32Tmp++;
	}
}
#else
#define CHKPT_DBG(X)
#endif


PVRSRV_ERROR RGXSyncAppendTimelineUpdate(IMG_UINT32 ui32FenceTimelineUpdateValue,
										 SYNC_ADDR_LIST	*psSyncList,
										 SYNC_ADDR_LIST	*psPRSyncList,
										 PVRSRV_CLIENT_SYNC_PRIM *psFenceTimelineUpdateSync,
										 RGX_SYNC_DATA *psSyncData,
										 IMG_BOOL bKick3D)
{
	IMG_UINT32 *pui32TimelineUpdateWOff = NULL;
	IMG_UINT32 *pui32IntAllocatedUpdateValues = NULL;

	IMG_UINT32 ui32ClientUpdateValueCount = psSyncData->ui32ClientUpdateValueCount;

	/* Space for original client updates, and the one new update */
	size_t uiUpdateSize = sizeof(*pui32IntAllocatedUpdateValues) * (ui32ClientUpdateValueCount + 1);

	if (!bKick3D)
	{
		/* Additional space for one PR update, only the newest one */
		uiUpdateSize += sizeof(*pui32IntAllocatedUpdateValues) * 1;
	}

	CHKPT_DBG((PVR_DBG_ERROR,
		   "%s: About to allocate memory to hold updates in pui32IntAllocatedUpdateValues(<%p>)",
		   __func__,
		   (void*)pui32IntAllocatedUpdateValues));

	/* Allocate memory to hold the list of update values (including our timeline update) */
	pui32IntAllocatedUpdateValues = OSAllocMem(uiUpdateSize);
	if (!pui32IntAllocatedUpdateValues)
	{
		/* Failed to allocate memory */
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSCachedMemSet(pui32IntAllocatedUpdateValues, 0xcc, uiUpdateSize);
	pui32TimelineUpdateWOff = pui32IntAllocatedUpdateValues;

	{
		CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: Copying %d %s update values into pui32IntAllocatedUpdateValues(<%p>)",
			   __func__,
			   ui32ClientUpdateValueCount,
			   bKick3D ? "TA/3D" : "TA/PR",
			   (void*)pui32IntAllocatedUpdateValues));
		/* Copy the update values into the new memory, then append our timeline update value */
		OSCachedMemCopy(pui32TimelineUpdateWOff, psSyncData->paui32ClientUpdateValue, ui32ClientUpdateValueCount * sizeof(*psSyncData->paui32ClientUpdateValue));

#if defined(TA3D_CHECKPOINT_DEBUG)
		_DebugSyncValues(pui32TimelineUpdateWOff, ui32ClientUpdateValueCount);
#endif

		pui32TimelineUpdateWOff += ui32ClientUpdateValueCount;
	}

	/* Now set the additional update value and append the timeline sync prim addr to either the
	 * render context 3D (or TA) update list
	 */
	CHKPT_DBG((PVR_DBG_ERROR,
		   "%s: Appending the additional update value (0x%x) to psRenderContext->sSyncAddrList%sUpdate...",
		   __func__,
		   ui32FenceTimelineUpdateValue,
		   bKick3D ? "TA/3D" : "TA/PR"));

	/* Append the TA/3D update */
	{
		*pui32TimelineUpdateWOff++ = ui32FenceTimelineUpdateValue;
		psSyncData->ui32ClientUpdateValueCount++;
		psSyncData->ui32ClientUpdateCount++;
		SyncAddrListAppendSyncPrim(psSyncList, psFenceTimelineUpdateSync);

		if (!psSyncData->pauiClientUpdateUFOAddress)
		{
			psSyncData->pauiClientUpdateUFOAddress = psSyncList->pasFWAddrs;
		}
		/* Update paui32ClientUpdateValue to point to our new list of update values */
		psSyncData->paui32ClientUpdateValue = pui32IntAllocatedUpdateValues;

#if defined(TA3D_CHECKPOINT_DEBUG)
		_DebugSyncValues(pui32IntAllocatedUpdateValues, psSyncData->ui32ClientUpdateValueCount);
#endif
	}

	if (!bKick3D)
	{
		/* Use the sSyncAddrList3DUpdate for PR (as it doesn't have one of its own) */
		*pui32TimelineUpdateWOff++ = ui32FenceTimelineUpdateValue;
		psSyncData->ui32ClientPRUpdateValueCount = 1;
		psSyncData->ui32ClientPRUpdateCount = 1;
		SyncAddrListAppendSyncPrim(psPRSyncList, psFenceTimelineUpdateSync);

		if (!psSyncData->pauiClientPRUpdateUFOAddress)
		{
			psSyncData->pauiClientPRUpdateUFOAddress = psPRSyncList->pasFWAddrs;
		}
		/* Update paui32ClientPRUpdateValue to point to our new list of update values */
		psSyncData->paui32ClientPRUpdateValue = &pui32IntAllocatedUpdateValues[psSyncData->ui32ClientUpdateValueCount];

#if defined(TA3D_CHECKPOINT_DEBUG)
		_DebugSyncValues(psSyncData->paui32ClientPRUpdateValue, psSyncData->ui32ClientPRUpdateValueCount);
#endif
	}

	/* Do not free the old psSyncData->ui32ClientUpdateValueCount,
	 * as it was constant data passed through the bridge down to PVRSRVRGXKickTA3DKM() */

	return PVRSRV_OK;
}
