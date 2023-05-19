/*******************************************************************************
@File
@Title          Server bridge for di
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for di
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

#include "di_impl_brg.h"

#include "common_di_bridge.h"

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

static PVRSRV_ERROR _DICreateContextpsContextIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = DIDestroyContextKM((DI_CONTEXT *) pvData);
	return eError;
}

static_assert(PRVSRVTL_MAX_STREAM_NAME_SIZE <= IMG_UINT32_MAX,
	      "PRVSRVTL_MAX_STREAM_NAME_SIZE must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDICreateContext(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psDICreateContextIN_UI8,
			    IMG_UINT8 * psDICreateContextOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DICREATECONTEXT *psDICreateContextIN =
	    (PVRSRV_BRIDGE_IN_DICREATECONTEXT *) IMG_OFFSET_ADDR(psDICreateContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DICREATECONTEXT *psDICreateContextOUT =
	    (PVRSRV_BRIDGE_OUT_DICREATECONTEXT *) IMG_OFFSET_ADDR(psDICreateContextOUT_UI8, 0);

	IMG_CHAR *puiStreamNameInt = NULL;
	DI_CONTEXT *psContextInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) + 0;

	PVR_UNREFERENCED_PARAMETER(psDICreateContextIN);

	psDICreateContextOUT->puiStreamName = psDICreateContextIN->puiStreamName;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDICreateContextOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DICreateContext_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDICreateContextIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDICreateContextIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDICreateContextOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DICreateContext_exit;
			}
		}
	}

	if (IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset) != NULL)
	{
		puiStreamNameInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR);
	}

	psDICreateContextOUT->eError = DICreateContextKM(puiStreamNameInt, &psContextInt);
	/* Exit early if bridged call fails */
	if (unlikely(psDICreateContextOUT->eError != PVRSRV_OK))
	{
		goto DICreateContext_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psDICreateContextOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								 &psDICreateContextOUT->hContext,
								 (void *)psContextInt,
								 PVRSRV_HANDLE_TYPE_DI_CONTEXT,
								 PVRSRV_HANDLE_ALLOC_FLAG_NONE,
								 (PFN_HANDLE_RELEASE) &
								 _DICreateContextpsContextIntRelease);
	if (unlikely(psDICreateContextOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DICreateContext_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* If dest ptr is non-null and we have data to copy */
	if ((puiStreamNameInt) && ((PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR)) > 0))
	{
		if (unlikely
		    (OSCopyToUser
		     (NULL, (void __user *)psDICreateContextOUT->puiStreamName, puiStreamNameInt,
		      (PRVSRVTL_MAX_STREAM_NAME_SIZE * sizeof(IMG_CHAR))) != PVRSRV_OK))
		{
			psDICreateContextOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DICreateContext_exit;
		}
	}

DICreateContext_exit:

	if (psDICreateContextOUT->eError != PVRSRV_OK)
	{
		if (psContextInt)
		{
			DIDestroyContextKM(psContextInt);
		}
	}

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDICreateContextOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeDIDestroyContext(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psDIDestroyContextIN_UI8,
			     IMG_UINT8 * psDIDestroyContextOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DIDESTROYCONTEXT *psDIDestroyContextIN =
	    (PVRSRV_BRIDGE_IN_DIDESTROYCONTEXT *) IMG_OFFSET_ADDR(psDIDestroyContextIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DIDESTROYCONTEXT *psDIDestroyContextOUT =
	    (PVRSRV_BRIDGE_OUT_DIDESTROYCONTEXT *) IMG_OFFSET_ADDR(psDIDestroyContextOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psDIDestroyContextOUT->eError =
	    PVRSRVDestroyHandleStagedUnlocked(psConnection->psHandleBase,
					      (IMG_HANDLE) psDIDestroyContextIN->hContext,
					      PVRSRV_HANDLE_TYPE_DI_CONTEXT);
	if (unlikely((psDIDestroyContextOUT->eError != PVRSRV_OK) &&
		     (psDIDestroyContextOUT->eError != PVRSRV_ERROR_KERNEL_CCB_FULL) &&
		     (psDIDestroyContextOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s", __func__, PVRSRVGetErrorString(psDIDestroyContextOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto DIDestroyContext_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

DIDestroyContext_exit:

	return 0;
}

static_assert(DI_IMPL_BRG_PATH_LEN <= IMG_UINT32_MAX,
	      "DI_IMPL_BRG_PATH_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDIReadEntry(IMG_UINT32 ui32DispatchTableEntry,
			IMG_UINT8 * psDIReadEntryIN_UI8,
			IMG_UINT8 * psDIReadEntryOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DIREADENTRY *psDIReadEntryIN =
	    (PVRSRV_BRIDGE_IN_DIREADENTRY *) IMG_OFFSET_ADDR(psDIReadEntryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DIREADENTRY *psDIReadEntryOUT =
	    (PVRSRV_BRIDGE_OUT_DIREADENTRY *) IMG_OFFSET_ADDR(psDIReadEntryOUT_UI8, 0);

	IMG_HANDLE hContext = psDIReadEntryIN->hContext;
	DI_CONTEXT *psContextInt = NULL;
	IMG_CHAR *uiEntryPathInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize = ((IMG_UINT64) DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) + 0;

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDIReadEntryOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DIReadEntry_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDIReadEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDIReadEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDIReadEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DIReadEntry_exit;
			}
		}
	}

	{
		uiEntryPathInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiEntryPathInt, (const void __user *)psDIReadEntryIN->puiEntryPath,
		     DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDIReadEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DIReadEntry_exit;
		}
		((IMG_CHAR *) uiEntryPathInt)[(DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDIReadEntryOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psContextInt,
				       hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT, IMG_TRUE);
	if (unlikely(psDIReadEntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DIReadEntry_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDIReadEntryOUT->eError =
	    DIReadEntryKM(psContextInt,
			  uiEntryPathInt, psDIReadEntryIN->ui64Offset, psDIReadEntryIN->ui64Size);

DIReadEntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDIReadEntryOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static_assert(DI_IMPL_BRG_PATH_LEN <= IMG_UINT32_MAX,
	      "DI_IMPL_BRG_PATH_LEN must not be larger than IMG_UINT32_MAX");
static_assert(DI_IMPL_BRG_PATH_LEN <= IMG_UINT32_MAX,
	      "DI_IMPL_BRG_PATH_LEN must not be larger than IMG_UINT32_MAX");

static IMG_INT
PVRSRVBridgeDIWriteEntry(IMG_UINT32 ui32DispatchTableEntry,
			 IMG_UINT8 * psDIWriteEntryIN_UI8,
			 IMG_UINT8 * psDIWriteEntryOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DIWRITEENTRY *psDIWriteEntryIN =
	    (PVRSRV_BRIDGE_IN_DIWRITEENTRY *) IMG_OFFSET_ADDR(psDIWriteEntryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DIWRITEENTRY *psDIWriteEntryOUT =
	    (PVRSRV_BRIDGE_OUT_DIWRITEENTRY *) IMG_OFFSET_ADDR(psDIWriteEntryOUT_UI8, 0);

	IMG_HANDLE hContext = psDIWriteEntryIN->hContext;
	DI_CONTEXT *psContextInt = NULL;
	IMG_CHAR *uiEntryPathInt = NULL;
	IMG_CHAR *uiValueInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;

	IMG_UINT32 ui32BufferSize = 0;
	IMG_UINT64 ui64BufferSize =
	    ((IMG_UINT64) DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) +
	    ((IMG_UINT64) psDIWriteEntryIN->ui32ValueSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psDIWriteEntryIN->ui32ValueSize > DI_IMPL_BRG_PATH_LEN))
	{
		psDIWriteEntryOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto DIWriteEntry_exit;
	}

	if (ui64BufferSize > IMG_UINT32_MAX)
	{
		psDIWriteEntryOUT->eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
		goto DIWriteEntry_exit;
	}

	ui32BufferSize = (IMG_UINT32) ui64BufferSize;

	if (ui32BufferSize != 0)
	{
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psDIWriteEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psDIWriteEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psDIWriteEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DIWriteEntry_exit;
			}
		}
	}

	{
		uiEntryPathInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiEntryPathInt, (const void __user *)psDIWriteEntryIN->puiEntryPath,
		     DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDIWriteEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DIWriteEntry_exit;
		}
		((IMG_CHAR *) uiEntryPathInt)[(DI_IMPL_BRG_PATH_LEN * sizeof(IMG_CHAR)) - 1] = '\0';
	}
	if (psDIWriteEntryIN->ui32ValueSize != 0)
	{
		uiValueInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psDIWriteEntryIN->ui32ValueSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psDIWriteEntryIN->ui32ValueSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiValueInt, (const void __user *)psDIWriteEntryIN->puiValue,
		     psDIWriteEntryIN->ui32ValueSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psDIWriteEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto DIWriteEntry_exit;
		}
		((IMG_CHAR *) uiValueInt)[(psDIWriteEntryIN->ui32ValueSize * sizeof(IMG_CHAR)) -
					  1] = '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDIWriteEntryOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psContextInt,
				       hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT, IMG_TRUE);
	if (unlikely(psDIWriteEntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DIWriteEntry_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDIWriteEntryOUT->eError =
	    DIWriteEntryKM(psContextInt,
			   uiEntryPathInt, psDIWriteEntryIN->ui32ValueSize, uiValueInt);

DIWriteEntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	/* Allocated space should be equal to the last updated offset */
#ifdef PVRSRV_NEED_PVR_ASSERT
	if (psDIWriteEntryOUT->eError == PVRSRV_OK)
		PVR_ASSERT(ui32BufferSize == ui32NextOffset);
#endif /* PVRSRV_NEED_PVR_ASSERT */

	if (!bHaveEnoughSpace && pArrayArgsBuffer)
		OSFreeMemNoStats(pArrayArgsBuffer);

	return 0;
}

static IMG_INT
PVRSRVBridgeDIListAllEntries(IMG_UINT32 ui32DispatchTableEntry,
			     IMG_UINT8 * psDIListAllEntriesIN_UI8,
			     IMG_UINT8 * psDIListAllEntriesOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_DILISTALLENTRIES *psDIListAllEntriesIN =
	    (PVRSRV_BRIDGE_IN_DILISTALLENTRIES *) IMG_OFFSET_ADDR(psDIListAllEntriesIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_DILISTALLENTRIES *psDIListAllEntriesOUT =
	    (PVRSRV_BRIDGE_OUT_DILISTALLENTRIES *) IMG_OFFSET_ADDR(psDIListAllEntriesOUT_UI8, 0);

	IMG_HANDLE hContext = psDIListAllEntriesIN->hContext;
	DI_CONTEXT *psContextInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psDIListAllEntriesOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psContextInt,
				       hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT, IMG_TRUE);
	if (unlikely(psDIListAllEntriesOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto DIListAllEntries_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psDIListAllEntriesOUT->eError = DIListAllEntriesKM(psContextInt);

DIListAllEntries_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psContextInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hContext, PVRSRV_HANDLE_TYPE_DI_CONTEXT);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitDIBridge(void);
void DeinitDIBridge(void);

/*
 * Register all DI functions with services
 */
PVRSRV_ERROR InitDIBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DICREATECONTEXT,
			      PVRSRVBridgeDICreateContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIDESTROYCONTEXT,
			      PVRSRVBridgeDIDestroyContext, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIREADENTRY,
			      PVRSRVBridgeDIReadEntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIWRITEENTRY,
			      PVRSRVBridgeDIWriteEntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DILISTALLENTRIES,
			      PVRSRVBridgeDIListAllEntries, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all di functions with services
 */
void DeinitDIBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DICREATECONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIDESTROYCONTEXT);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIREADENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DIWRITEENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_DI, PVRSRV_BRIDGE_DI_DILISTALLENTRIES);

}
