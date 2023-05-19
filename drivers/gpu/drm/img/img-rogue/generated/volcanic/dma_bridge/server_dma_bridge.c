/*******************************************************************************
@File
@Title          Server bridge for dma
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for dma
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

#include "dma_km.h"

#include "common_dma_bridge.h"

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

static_assert(MAX_DMA_OPS <= IMG_UINT32_MAX, "MAX_DMA_OPS must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDmaTransfer(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psDmaTransferIN_UI8,
			IMG_UINT8 * psDmaTransferOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DMATRANSFER *psDmaTransferIN =
	    (PVRSRV_BRIDGE_IN_DMATRANSFER *) IMG_OFFSET_ADDR(psDmaTransferIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DMATRANSFER *psDmaTransferOUT =
	    (PVRSRV_BRIDGE_OUT_DMATRANSFER *) IMG_OFFSET_ADDR(psDmaTransferOUT_UI8, 0);

	PMR **psPMRInt = NULL;
	IMG_HANDLE *hPMRInt2 = NULL;
	IMG_UINT64 *ui64AddressInt = NULL;
	IMG_DEVMEM_OFFSET_T *uiOffsetInt = NULL;
	IMG_DEVMEM_SIZE_T *uiSizeInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psDmaTransferIN->ui32NumDMAs * sizeof(PMR *)) +
	    ((IMG_UINT64) psDmaTransferIN->ui32NumDMAs * sizeof(IMG_HANDLE)) +
	    ((IMG_UINT64) psDmaTransferIN->ui32NumDMAs * sizeof(IMG_UINT64)) +
	    ((IMG_UINT64) psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_OFFSET_T)) +
	    ((IMG_UINT64) psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_SIZE_T)) + 0;

	if (unlikely(psDmaTransferIN->ui32NumDMAs > MAX_DMA_OPS))
	{
		psDmaTransferOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto DmaTransfer_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDmaTransferOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DmaTransfer_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDmaTransferIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDmaTransferIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDmaTransferOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DmaTransfer_exit;
			}
		}
	}

	if (psDmaTransferIN->ui32NumDMAs != 0)
	{
		psPMRInt = (PMR **) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		OSCachedMemSet(psPMRInt, 0, psDmaTransferIN->ui32NumDMAs * sizeof(PMR *));
		ui32NextOffset += psDmaTransferIN->ui32NumDMAs * sizeof(PMR *);
		hPMRInt2 = (IMG_HANDLE *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDmaTransferIN->ui32NumDMAs * sizeof(IMG_HANDLE);
	}

	/* Copy the data over */
	if (psDmaTransferIN->ui32NumDMAs * sizeof(IMG_HANDLE) > 0)
	{
		if (OSCopyFromUser
		    (NULL, hPMRInt2, (const void __user *)psDmaTransferIN->phPMR,
		     psDmaTransferIN->ui32NumDMAs * sizeof(IMG_HANDLE)) != PVRSRV_OK)
		{
			psDmaTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DmaTransfer_exit;
		}
	}
	if (psDmaTransferIN->ui32NumDMAs != 0)
	{
		ui64AddressInt = (IMG_UINT64 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDmaTransferIN->ui32NumDMAs * sizeof(IMG_UINT64);
	}

	/* Copy the data over */
	if (psDmaTransferIN->ui32NumDMAs * sizeof(IMG_UINT64) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui64AddressInt, (const void __user *)psDmaTransferIN->pui64Address,
		     psDmaTransferIN->ui32NumDMAs * sizeof(IMG_UINT64)) != PVRSRV_OK)
		{
			psDmaTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DmaTransfer_exit;
		}
	}
	if (psDmaTransferIN->ui32NumDMAs != 0)
	{
		uiOffsetInt =
		    (IMG_DEVMEM_OFFSET_T *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_OFFSET_T);
	}

	/* Copy the data over */
	if (psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_OFFSET_T) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiOffsetInt, (const void __user *)psDmaTransferIN->puiOffset,
		     psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_OFFSET_T)) != PVRSRV_OK)
		{
			psDmaTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DmaTransfer_exit;
		}
	}
	if (psDmaTransferIN->ui32NumDMAs != 0)
	{
		uiSizeInt = (IMG_DEVMEM_SIZE_T *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_SIZE_T);
	}

	/* Copy the data over */
	if (psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_SIZE_T) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiSizeInt, (const void __user *)psDmaTransferIN->puiSize,
		     psDmaTransferIN->ui32NumDMAs * sizeof(IMG_DEVMEM_SIZE_T)) != PVRSRV_OK)
		{
			psDmaTransferOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DmaTransfer_exit;
		}
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	{
		IMG_UINT32 i;

		for (i = 0; i < psDmaTransferIN->ui32NumDMAs; i++)
		{
			/* Look up the address from the handle */
			psDmaTransferOUT->eError =
			    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
						       (void **)&psPMRInt[i],
						       hPMRInt2[i],
						       PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
			if (unlikely(psDmaTransferOUT->eError != PVRSRV_OK))
			{
				UnlockHandle(psConnection->psHandleBase);
				goto DmaTransfer_exit;
			}
		}
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDmaTransferOUT->eError =
	    DmaTransfer(psConnection, OSGetDevNode(psConnection),
			psDmaTransferIN->ui32NumDMAs,
			psPMRInt,
			ui64AddressInt,
			uiOffsetInt,
			uiSizeInt, psDmaTransferIN->ui32uiFlags, psDmaTransferIN->hUpdateTimeline);

DmaTransfer_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	if (hPMRInt2)
	{
		IMG_UINT32 i;

		for (i = 0; i < psDmaTransferIN->ui32NumDMAs; i++)
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
	if (psDmaTransferOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(32 <= IMG_UINT32_MAX, "32 must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDmaSparseMappingTable(IMG_UINT32 ui32DispatchTableEntry,
				  IMG_UINT8 * psDmaSparseMappingTableIN_UI8,
				  IMG_UINT8 * psDmaSparseMappingTableOUT_UI8,
				  CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DMASPARSEMAPPINGTABLE *psDmaSparseMappingTableIN =
	    (PVRSRV_BRIDGE_IN_DMASPARSEMAPPINGTABLE *)
	    IMG_OFFSET_ADDR(psDmaSparseMappingTableIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DMASPARSEMAPPINGTABLE *psDmaSparseMappingTableOUT =
	    (PVRSRV_BRIDGE_OUT_DMASPARSEMAPPINGTABLE *)
	    IMG_OFFSET_ADDR(psDmaSparseMappingTableOUT_UI8, 0);

	IMG_HANDLE hPMR = psDmaSparseMappingTableIN->hPMR;
	PMR *psPMRInt = NULL;
	IMG_BOOL *pbTableInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) psDmaSparseMappingTableIN->ui32SizeInPages * sizeof(IMG_BOOL)) + 0;

	if (psDmaSparseMappingTableIN->ui32SizeInPages > 32)
	{
		psDmaSparseMappingTableOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto DmaSparseMappingTable_exit;
	}

	psDmaSparseMappingTableOUT->pbTable = psDmaSparseMappingTableIN->pbTable;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDmaSparseMappingTableOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DmaSparseMappingTable_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDmaSparseMappingTableIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDmaSparseMappingTableIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDmaSparseMappingTableOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DmaSparseMappingTable_exit;
			}
		}
	}

	if (psDmaSparseMappingTableIN->ui32SizeInPages != 0)
	{
		pbTableInt = (IMG_BOOL *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDmaSparseMappingTableIN->ui32SizeInPages * sizeof(IMG_BOOL);
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDmaSparseMappingTableOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psDmaSparseMappingTableOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DmaSparseMappingTable_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDmaSparseMappingTableOUT->eError =
	    DmaSparseMappingTable(psPMRInt,
				  psDmaSparseMappingTableIN->uiOffset,
				  psDmaSparseMappingTableIN->ui32SizeInPages, pbTableInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDmaSparseMappingTableOUT->eError != PVRSRV_OK))
	{
		goto DmaSparseMappingTable_exit;
	}

	/* If dest ptr is non-null and we have data to copy */
	if ((pbTableInt) && ((psDmaSparseMappingTableIN->ui32SizeInPages * sizeof(IMG_BOOL)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psDmaSparseMappingTableOUT->pbTable, pbTableInt,
		      (psDmaSparseMappingTableIN->ui32SizeInPages * sizeof(IMG_BOOL))) !=
		     PVRSRV_OK))
		{
			psDmaSparseMappingTableOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DmaSparseMappingTable_exit;
		}
	}

DmaSparseMappingTable_exit:

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
	if (psDmaSparseMappingTableOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeDmaDeviceParams(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psDmaDeviceParamsIN_UI8,
			    IMG_UINT8 * psDmaDeviceParamsOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DMADEVICEPARAMS *psDmaDeviceParamsIN =
	    (PVRSRV_BRIDGE_IN_DMADEVICEPARAMS *) IMG_OFFSET_ADDR(psDmaDeviceParamsIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DMADEVICEPARAMS *psDmaDeviceParamsOUT =
	    (PVRSRV_BRIDGE_OUT_DMADEVICEPARAMS *) IMG_OFFSET_ADDR(psDmaDeviceParamsOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psDmaDeviceParamsIN);

	psDmaDeviceParamsOUT->eError =
	    DmaDeviceParams(psConnection, OSGetDevNode(psConnection),
			    &psDmaDeviceParamsOUT->ui32DmaBuffAlign,
			    &psDmaDeviceParamsOUT->ui32DmaTransferMult);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitDMABridge(void);
void DeinitDMABridge(void);

/*
 * Register all DMA functions with services
 */
PVRSRV_ERROR InitDMABridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMATRANSFER,
			      PVRSRVBridgeDmaTransfer, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMASPARSEMAPPINGTABLE,
			      PVRSRVBridgeDmaSparseMappingTable, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMADEVICEPARAMS,
			      PVRSRVBridgeDmaDeviceParams, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all dma functions with services
 */
void DeinitDMABridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMATRANSFER);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMASPARSEMAPPINGTABLE);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMA, PVRSRV_BRIDGE_DMA_DMADEVICEPARAMS);

}
