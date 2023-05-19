/*******************************************************************************
@File
@Title          Server bridge for devicememhistory
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for devicememhistory
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

#include "pmr.h"
#include "devicemem_history_server.h"

#include "common_devicememhistory_bridge.h"

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

#include "lock.h"

/* ***************************************************************************
 * Server-side bridge entry points
 */

static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDevicememHistoryMap(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psDevicememHistoryMapIN_UI8,
				IMG_UINT8 * psDevicememHistoryMapOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAP *psDevicememHistoryMapIN =
	    (PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAP *) IMG_OFFSET_ADDR(psDevicememHistoryMapIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAP *psDevicememHistoryMapOUT =
	    (PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAP *) IMG_OFFSET_ADDR(psDevicememHistoryMapOUT_UI8,
								      0);

	IMG_HANDLE hPMR = psDevicememHistoryMapIN->hPMR;
	PMR *psPMRInt = NULL;
	IMG_CHAR *uiTextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize = ((IMG_UINT64) DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) + 0;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDevicememHistoryMapOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DevicememHistoryMap_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDevicememHistoryMapIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDevicememHistoryMapIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDevicememHistoryMapOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevicememHistoryMap_exit;
			}
		}
	}

	{
		uiTextInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextInt, (const void __user *)psDevicememHistoryMapIN->puiText,
		     DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDevicememHistoryMapOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistoryMap_exit;
		}
		((IMG_CHAR *) uiTextInt)[(DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevicememHistoryMapOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevicememHistoryMapOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevicememHistoryMap_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevicememHistoryMapOUT->eError =
	    DevicememHistoryMapKM(psPMRInt,
				  psDevicememHistoryMapIN->uiOffset,
				  psDevicememHistoryMapIN->sDevVAddr,
				  psDevicememHistoryMapIN->uiSize,
				  uiTextInt,
				  psDevicememHistoryMapIN->ui32Log2PageSize,
				  psDevicememHistoryMapIN->ui32AllocationIndex,
				  &psDevicememHistoryMapOUT->ui32AllocationIndexOut);

DevicememHistoryMap_exit:

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

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDevicememHistoryMapOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDevicememHistoryUnmap(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psDevicememHistoryUnmapIN_UI8,
				  IMG_UINT8 * psDevicememHistoryUnmapOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAP *psDevicememHistoryUnmapIN =
	    (PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAP *)
	    IMG_OFFSET_ADDR(psDevicememHistoryUnmapIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAP *psDevicememHistoryUnmapOUT =
	    (PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAP *)
	    IMG_OFFSET_ADDR(psDevicememHistoryUnmapOUT_UI8, 0);

	IMG_HANDLE hPMR = psDevicememHistoryUnmapIN->hPMR;
	PMR *psPMRInt = NULL;
	IMG_CHAR *uiTextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize = ((IMG_UINT64) DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) + 0;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDevicememHistoryUnmapOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DevicememHistoryUnmap_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDevicememHistoryUnmapIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDevicememHistoryUnmapIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDevicememHistoryUnmapOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevicememHistoryUnmap_exit;
			}
		}
	}

	{
		uiTextInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextInt, (const void __user *)psDevicememHistoryUnmapIN->puiText,
		     DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDevicememHistoryUnmapOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistoryUnmap_exit;
		}
		((IMG_CHAR *) uiTextInt)[(DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevicememHistoryUnmapOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevicememHistoryUnmapOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevicememHistoryUnmap_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevicememHistoryUnmapOUT->eError =
	    DevicememHistoryUnmapKM(psPMRInt,
				    psDevicememHistoryUnmapIN->uiOffset,
				    psDevicememHistoryUnmapIN->sDevVAddr,
				    psDevicememHistoryUnmapIN->uiSize,
				    uiTextInt,
				    psDevicememHistoryUnmapIN->ui32Log2PageSize,
				    psDevicememHistoryUnmapIN->ui32AllocationIndex,
				    &psDevicememHistoryUnmapOUT->ui32AllocationIndexOut);

DevicememHistoryUnmap_exit:

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

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDevicememHistoryUnmapOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDevicememHistoryMapVRange(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psDevicememHistoryMapVRangeIN_UI8,
				      IMG_UINT8 * psDevicememHistoryMapVRangeOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAPVRANGE *psDevicememHistoryMapVRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYMAPVRANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistoryMapVRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAPVRANGE *psDevicememHistoryMapVRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYMAPVRANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistoryMapVRangeOUT_UI8, 0);

	IMG_CHAR *uiTextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize = ((IMG_UINT64) DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) + 0;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDevicememHistoryMapVRangeOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DevicememHistoryMapVRange_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDevicememHistoryMapVRangeIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDevicememHistoryMapVRangeIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDevicememHistoryMapVRangeOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevicememHistoryMapVRange_exit;
			}
		}
	}

	{
		uiTextInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextInt, (const void __user *)psDevicememHistoryMapVRangeIN->puiText,
		     DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDevicememHistoryMapVRangeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistoryMapVRange_exit;
		}
		((IMG_CHAR *) uiTextInt)[(DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}

	psDevicememHistoryMapVRangeOUT->eError =
	    DevicememHistoryMapVRangeKM(psConnection, OSGetDevNode(psConnection),
					psDevicememHistoryMapVRangeIN->sBaseDevVAddr,
					psDevicememHistoryMapVRangeIN->ui32ui32StartPage,
					psDevicememHistoryMapVRangeIN->ui32NumPages,
					psDevicememHistoryMapVRangeIN->uiAllocSize,
					uiTextInt,
					psDevicememHistoryMapVRangeIN->ui32Log2PageSize,
					psDevicememHistoryMapVRangeIN->ui32AllocationIndex,
					&psDevicememHistoryMapVRangeOUT->ui32AllocationIndexOut);

DevicememHistoryMapVRange_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDevicememHistoryMapVRangeOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDevicememHistoryUnmapVRange(IMG_UINT32 ui32DispatchTableEntry,
					IMG_UINT8 * psDevicememHistoryUnmapVRangeIN_UI8,
					IMG_UINT8 * psDevicememHistoryUnmapVRangeOUT_UI8,
					CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAPVRANGE *psDevicememHistoryUnmapVRangeIN =
	    (PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYUNMAPVRANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistoryUnmapVRangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAPVRANGE *psDevicememHistoryUnmapVRangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYUNMAPVRANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistoryUnmapVRangeOUT_UI8, 0);

	IMG_CHAR *uiTextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize = ((IMG_UINT64) DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) + 0;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDevicememHistoryUnmapVRangeOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DevicememHistoryUnmapVRange_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDevicememHistoryUnmapVRangeIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer =
			    (IMG_BYTE *) (void *)psDevicememHistoryUnmapVRangeIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDevicememHistoryUnmapVRangeOUT->eError =
				    PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevicememHistoryUnmapVRange_exit;
			}
		}
	}

	{
		uiTextInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextInt, (const void __user *)psDevicememHistoryUnmapVRangeIN->puiText,
		     DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDevicememHistoryUnmapVRangeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistoryUnmapVRange_exit;
		}
		((IMG_CHAR *) uiTextInt)[(DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}

	psDevicememHistoryUnmapVRangeOUT->eError =
	    DevicememHistoryUnmapVRangeKM(psConnection, OSGetDevNode(psConnection),
					  psDevicememHistoryUnmapVRangeIN->sBaseDevVAddr,
					  psDevicememHistoryUnmapVRangeIN->ui32ui32StartPage,
					  psDevicememHistoryUnmapVRangeIN->ui32NumPages,
					  psDevicememHistoryUnmapVRangeIN->uiAllocSize,
					  uiTextInt,
					  psDevicememHistoryUnmapVRangeIN->ui32Log2PageSize,
					  psDevicememHistoryUnmapVRangeIN->ui32AllocationIndex,
					  &psDevicememHistoryUnmapVRangeOUT->
					  ui32AllocationIndexOut);

DevicememHistoryUnmapVRange_exit:

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDevicememHistoryUnmapVRangeOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DEVMEM_ANNOTATION_MAX_LEN <= IMG_UINT32_MAX,
	      "DEVMEM_ANNOTATION_MAX_LEN must not be larger than IMG_UINT32_MAX");
static_assert(PMR_MAX_SUPPORTED_4K_PAGE_COUNT <= IMG_UINT32_MAX,
	      "PMR_MAX_SUPPORTED_4K_PAGE_COUNT must not be larger than IMG_UINT32_MAX");
static_assert(PMR_MAX_SUPPORTED_4K_PAGE_COUNT <= IMG_UINT32_MAX,
	      "PMR_MAX_SUPPORTED_4K_PAGE_COUNT must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDevicememHistorySparseChange(IMG_UINT32 ui32DispatchTableEntry,
					 IMG_UINT8 * psDevicememHistorySparseChangeIN_UI8,
					 IMG_UINT8 * psDevicememHistorySparseChangeOUT_UI8,
					 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYSPARSECHANGE *psDevicememHistorySparseChangeIN =
	    (PVRSRV_BRIDGE_IN_DEVICEMEMHISTORYSPARSECHANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistorySparseChangeIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYSPARSECHANGE *psDevicememHistorySparseChangeOUT =
	    (PVRSRV_BRIDGE_OUT_DEVICEMEMHISTORYSPARSECHANGE *)
	    IMG_OFFSET_ADDR(psDevicememHistorySparseChangeOUT_UI8, 0);

	IMG_HANDLE hPMR = psDevicememHistorySparseChangeIN->hPMR;
	PMR *psPMRInt = NULL;
	IMG_CHAR *uiTextInt = NULL;
	IMG_UINT32 *ui32AllocPageIndicesInt = NULL;
	IMG_UINT32 *ui32FreePageIndicesInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) +
	    ((IMG_UINT64) psDevicememHistorySparseChangeIN->ui32AllocPageCount *
	     sizeof(IMG_UINT32)) +
	    ((IMG_UINT64) psDevicememHistorySparseChangeIN->ui32FreePageCount *
	     sizeof(IMG_UINT32)) + 0;

	if (unlikely
	    (psDevicememHistorySparseChangeIN->ui32AllocPageCount >
	     PMR_MAX_SUPPORTED_4K_PAGE_COUNT))
	{
		psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto DevicememHistorySparseChange_exit;
	}

	if (unlikely
	    (psDevicememHistorySparseChangeIN->ui32FreePageCount > PMR_MAX_SUPPORTED_4K_PAGE_COUNT))
	{
		psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto DevicememHistorySparseChange_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DevicememHistorySparseChange_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDevicememHistorySparseChangeIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer =
			    (IMG_BYTE *) (void *)psDevicememHistorySparseChangeIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDevicememHistorySparseChangeOUT->eError =
				    PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevicememHistorySparseChange_exit;
			}
		}
	}

	{
		uiTextInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextInt,
		     (const void __user *)psDevicememHistorySparseChangeIN->puiText,
		     DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistorySparseChange_exit;
		}
		((IMG_CHAR *) uiTextInt)[(DEVMEM_ANNOTATION_MAX_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}
	if (psDevicememHistorySparseChangeIN->ui32AllocPageCount != 0)
	{
		ui32AllocPageIndicesInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psDevicememHistorySparseChangeIN->ui32AllocPageCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psDevicememHistorySparseChangeIN->ui32AllocPageCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32AllocPageIndicesInt,
		     (const void __user *)psDevicememHistorySparseChangeIN->pui32AllocPageIndices,
		     psDevicememHistorySparseChangeIN->ui32AllocPageCount * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistorySparseChange_exit;
		}
	}
	if (psDevicememHistorySparseChangeIN->ui32FreePageCount != 0)
	{
		ui32FreePageIndicesInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psDevicememHistorySparseChangeIN->ui32FreePageCount * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psDevicememHistorySparseChangeIN->ui32FreePageCount * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32FreePageIndicesInt,
		     (const void __user *)psDevicememHistorySparseChangeIN->pui32FreePageIndices,
		     psDevicememHistorySparseChangeIN->ui32FreePageCount * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psDevicememHistorySparseChangeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DevicememHistorySparseChange_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDevicememHistorySparseChangeOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDevicememHistorySparseChangeOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DevicememHistorySparseChange_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDevicememHistorySparseChangeOUT->eError =
	    DevicememHistorySparseChangeKM(psPMRInt,
					   psDevicememHistorySparseChangeIN->uiOffset,
					   psDevicememHistorySparseChangeIN->sDevVAddr,
					   psDevicememHistorySparseChangeIN->uiSize,
					   uiTextInt,
					   psDevicememHistorySparseChangeIN->ui32Log2PageSize,
					   psDevicememHistorySparseChangeIN->ui32AllocPageCount,
					   ui32AllocPageIndicesInt,
					   psDevicememHistorySparseChangeIN->ui32FreePageCount,
					   ui32FreePageIndicesInt,
					   psDevicememHistorySparseChangeIN->ui32AllocationIndex,
					   &psDevicememHistorySparseChangeOUT->
					   ui32AllocationIndexOut);

DevicememHistorySparseChange_exit:

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

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDevicememHistorySparseChangeOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

static POS_LOCK pDEVICEMEMHISTORYBridgeLock;

PVRSRV_ERROR InitDEVICEMEMHISTORYBridge(void);
void DeinitDEVICEMEMHISTORYBridge(void);

/*
 * Register all DEVICEMEMHISTORY functions with services
 */
PVRSRV_ERROR InitDEVICEMEMHISTORYBridge(void)
{
	PVR_LOG_RETURN_IF_ERROR(OSLockCreate(&pDEVICEMEMHISTORYBridgeLock), "OSLockCreate");

	SetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
			      PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAP,
			      PVRSRVBridgeDevicememHistoryMap, pDEVICEMEMHISTORYBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
			      PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAP,
			      PVRSRVBridgeDevicememHistoryUnmap, pDEVICEMEMHISTORYBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
			      PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAPVRANGE,
			      PVRSRVBridgeDevicememHistoryMapVRange, pDEVICEMEMHISTORYBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
			      PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAPVRANGE,
			      PVRSRVBridgeDevicememHistoryUnmapVRange, pDEVICEMEMHISTORYBridgeLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
			      PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYSPARSECHANGE,
			      PVRSRVBridgeDevicememHistorySparseChange,
			      pDEVICEMEMHISTORYBridgeLock);

	return PVRSRV_OK;
}

/*
 * Unregister all devicememhistory functions with services
 */
void DeinitDEVICEMEMHISTORYBridge(void)
{
	OSLockDestroy(pDEVICEMEMHISTORYBridgeLock);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
				PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAP);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
				PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAP);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
				PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYMAPVRANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
				PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYUNMAPVRANGE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DEVICEMEMHISTORY,
				PVRSRV_BRIDGE_DEVICEMEMHISTORY_DEVICEMEMHISTORYSPARSECHANGE);

}
