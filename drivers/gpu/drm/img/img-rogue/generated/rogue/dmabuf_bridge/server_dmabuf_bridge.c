/*******************************************************************************
@File
@Title          Server bridge for dmabuf
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for dmabuf
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

#include "physmem_dmabuf.h"
#include "pmr.h"

#include "common_dmabuf_bridge.h"

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

static PVRSRV_ERROR _PhysmemImportDmaBufpsPMRPtrIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePhysmemImportDmaBuf(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psPhysmemImportDmaBufIN_UI8,
				IMG_UINT8 * psPhysmemImportDmaBufOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSMEMIMPORTDMABUF *psPhysmemImportDmaBufIN =
	    (PVRSRV_BRIDGE_IN_PHYSMEMIMPORTDMABUF *) IMG_OFFSET_ADDR(psPhysmemImportDmaBufIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTDMABUF *psPhysmemImportDmaBufOUT =
	    (PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTDMABUF *) IMG_OFFSET_ADDR(psPhysmemImportDmaBufOUT_UI8,
								      0);

	IMG_CHAR *uiNameInt = NULL;
	PMR *psPMRPtrInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psPhysmemImportDmaBufIN->ui32NameSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psPhysmemImportDmaBufIN->ui32NameSize > DEVMEM_ANNOTATION_MAX_LEN))
	{
		psPhysmemImportDmaBufOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysmemImportDmaBuf_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psPhysmemImportDmaBufIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psPhysmemImportDmaBufIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psPhysmemImportDmaBufOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PhysmemImportDmaBuf_exit;
			}
		}
	}

	if (psPhysmemImportDmaBufIN->ui32NameSize != 0)
	{
		uiNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psPhysmemImportDmaBufIN->ui32NameSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psPhysmemImportDmaBufIN->ui32NameSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiNameInt, (const void __user *)psPhysmemImportDmaBufIN->puiName,
		     psPhysmemImportDmaBufIN->ui32NameSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psPhysmemImportDmaBufOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysmemImportDmaBuf_exit;
		}
		((IMG_CHAR *) uiNameInt)[(psPhysmemImportDmaBufIN->ui32NameSize * sizeof(IMG_CHAR))
					 - 1] = '\0';
	}

	psPhysmemImportDmaBufOUT->eError =
	    PhysmemImportDmaBuf(psConnection, OSGetDevNode(psConnection),
				psPhysmemImportDmaBufIN->ifd,
				psPhysmemImportDmaBufIN->uiFlags,
				psPhysmemImportDmaBufIN->ui32NameSize,
				uiNameInt,
				&psPMRPtrInt,
				&psPhysmemImportDmaBufOUT->uiSize,
				&psPhysmemImportDmaBufOUT->uiAlign);
	/* Exit early if bridged call fails */
	if (unlikely(psPhysmemImportDmaBufOUT->eError != PVRSRV_OK))
	{
		goto PhysmemImportDmaBuf_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPhysmemImportDmaBufOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								     &psPhysmemImportDmaBufOUT->
								     hPMRPtr, (void *)psPMRPtrInt,
								     PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
								     PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								     (PFN_HANDLE_RELEASE) &
								     _PhysmemImportDmaBufpsPMRPtrIntRelease);
	if (unlikely(psPhysmemImportDmaBufOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PhysmemImportDmaBuf_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PhysmemImportDmaBuf_exit:

	if (psPhysmemImportDmaBufOUT->eError != PVRSRV_OK)
	{
		if (psPMRPtrInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRPtrInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

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
PVRSRVBridgePhysmemExportDmaBuf(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psPhysmemExportDmaBufIN_UI8,
				IMG_UINT8 * psPhysmemExportDmaBufOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSMEMEXPORTDMABUF *psPhysmemExportDmaBufIN =
	    (PVRSRV_BRIDGE_IN_PHYSMEMEXPORTDMABUF *) IMG_OFFSET_ADDR(psPhysmemExportDmaBufIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_PHYSMEMEXPORTDMABUF *psPhysmemExportDmaBufOUT =
	    (PVRSRV_BRIDGE_OUT_PHYSMEMEXPORTDMABUF *) IMG_OFFSET_ADDR(psPhysmemExportDmaBufOUT_UI8,
								      0);

	IMG_HANDLE hPMR = psPhysmemExportDmaBufIN->hPMR;
	PMR *psPMRInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psPhysmemExportDmaBufOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRInt,
				       hPMR, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psPhysmemExportDmaBufOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PhysmemExportDmaBuf_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psPhysmemExportDmaBufOUT->eError =
	    PhysmemExportDmaBuf(psConnection, OSGetDevNode(psConnection),
				psPMRInt, &psPhysmemExportDmaBufOUT->iFd);

PhysmemExportDmaBuf_exit:

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

static PVRSRV_ERROR _PhysmemImportSparseDmaBufpsPMRPtrIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePhysmemImportSparseDmaBuf(IMG_UINT32 ui32DispatchTableEntry,
				      IMG_UINT8 * psPhysmemImportSparseDmaBufIN_UI8,
				      IMG_UINT8 * psPhysmemImportSparseDmaBufOUT_UI8,
				      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSMEMIMPORTSPARSEDMABUF *psPhysmemImportSparseDmaBufIN =
	    (PVRSRV_BRIDGE_IN_PHYSMEMIMPORTSPARSEDMABUF *)
	    IMG_OFFSET_ADDR(psPhysmemImportSparseDmaBufIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTSPARSEDMABUF *psPhysmemImportSparseDmaBufOUT =
	    (PVRSRV_BRIDGE_OUT_PHYSMEMIMPORTSPARSEDMABUF *)
	    IMG_OFFSET_ADDR(psPhysmemImportSparseDmaBufOUT_UI8, 0);

	IMG_UINT32 *ui32MappingTableInt = NULL;
	IMG_CHAR *uiNameInt = NULL;
	PMR *psPMRPtrInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks * sizeof(IMG_UINT32)) +
	    (psPhysmemImportSparseDmaBufIN->ui32NameSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely
	    (psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks > PMR_MAX_SUPPORTED_PAGE_COUNT))
	{
		psPhysmemImportSparseDmaBufOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysmemImportSparseDmaBuf_exit;
	}

	if (unlikely(psPhysmemImportSparseDmaBufIN->ui32NameSize > DEVMEM_ANNOTATION_MAX_LEN))
	{
		psPhysmemImportSparseDmaBufOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto PhysmemImportSparseDmaBuf_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psPhysmemImportSparseDmaBufIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psPhysmemImportSparseDmaBufIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psPhysmemImportSparseDmaBufOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PhysmemImportSparseDmaBuf_exit;
			}
		}
	}

	if (psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks != 0)
	{
		ui32MappingTableInt =
		    (IMG_UINT32 *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset +=
		    psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks * sizeof(IMG_UINT32);
	}

	/* Copy the data over */
	if (psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks * sizeof(IMG_UINT32) > 0)
	{
		if (OSCopyFromUser
		    (NULL, ui32MappingTableInt,
		     (const void __user *)psPhysmemImportSparseDmaBufIN->pui32MappingTable,
		     psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks * sizeof(IMG_UINT32)) !=
		    PVRSRV_OK)
		{
			psPhysmemImportSparseDmaBufOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysmemImportSparseDmaBuf_exit;
		}
	}
	if (psPhysmemImportSparseDmaBufIN->ui32NameSize != 0)
	{
		uiNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psPhysmemImportSparseDmaBufIN->ui32NameSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psPhysmemImportSparseDmaBufIN->ui32NameSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiNameInt, (const void __user *)psPhysmemImportSparseDmaBufIN->puiName,
		     psPhysmemImportSparseDmaBufIN->ui32NameSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psPhysmemImportSparseDmaBufOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PhysmemImportSparseDmaBuf_exit;
		}
		((IMG_CHAR *)
		 uiNameInt)[(psPhysmemImportSparseDmaBufIN->ui32NameSize * sizeof(IMG_CHAR)) - 1] =
       '\0';
	}

	psPhysmemImportSparseDmaBufOUT->eError =
	    PhysmemImportSparseDmaBuf(psConnection, OSGetDevNode(psConnection),
				      psPhysmemImportSparseDmaBufIN->ifd,
				      psPhysmemImportSparseDmaBufIN->uiFlags,
				      psPhysmemImportSparseDmaBufIN->uiChunkSize,
				      psPhysmemImportSparseDmaBufIN->ui32NumPhysChunks,
				      psPhysmemImportSparseDmaBufIN->ui32NumVirtChunks,
				      ui32MappingTableInt,
				      psPhysmemImportSparseDmaBufIN->ui32NameSize,
				      uiNameInt,
				      &psPMRPtrInt,
				      &psPhysmemImportSparseDmaBufOUT->uiSize,
				      &psPhysmemImportSparseDmaBufOUT->uiAlign);
	/* Exit early if bridged call fails */
	if (unlikely(psPhysmemImportSparseDmaBufOUT->eError != PVRSRV_OK))
	{
		goto PhysmemImportSparseDmaBuf_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPhysmemImportSparseDmaBufOUT->eError =
	    PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
				      &psPhysmemImportSparseDmaBufOUT->hPMRPtr, (void *)psPMRPtrInt,
				      PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
				      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				      (PFN_HANDLE_RELEASE) &
				      _PhysmemImportSparseDmaBufpsPMRPtrIntRelease);
	if (unlikely(psPhysmemImportSparseDmaBufOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PhysmemImportSparseDmaBuf_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PhysmemImportSparseDmaBuf_exit:

	if (psPhysmemImportSparseDmaBufOUT->eError != PVRSRV_OK)
	{
		if (psPMRPtrInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRPtrInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

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

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitDMABUFBridge(void);
PVRSRV_ERROR DeinitDMABUFBridge(void);

/*
 * Register all DMABUF functions with services
 */
PVRSRV_ERROR InitDMABUFBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF, PVRSRV_BRIDGE_DMABUF_PHYSMEMIMPORTDMABUF,
			      PVRSRVBridgePhysmemImportDmaBuf, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF, PVRSRV_BRIDGE_DMABUF_PHYSMEMEXPORTDMABUF,
			      PVRSRVBridgePhysmemExportDmaBuf, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF, PVRSRV_BRIDGE_DMABUF_PHYSMEMIMPORTSPARSEDMABUF,
			      PVRSRVBridgePhysmemImportSparseDmaBuf, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all dmabuf functions with services
 */
PVRSRV_ERROR DeinitDMABUFBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF, PVRSRV_BRIDGE_DMABUF_PHYSMEMIMPORTDMABUF);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF, PVRSRV_BRIDGE_DMABUF_PHYSMEMEXPORTDMABUF);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DMABUF,
				PVRSRV_BRIDGE_DMABUF_PHYSMEMIMPORTSPARSEDMABUF);

	return PVRSRV_OK;
}
