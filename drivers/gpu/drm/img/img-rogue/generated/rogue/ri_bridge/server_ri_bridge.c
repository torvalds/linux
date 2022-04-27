/*******************************************************************************
@File
@Title          Server bridge for ri
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for ri
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

#include "ri_server.h"

#include "common_ri_bridge.h"

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
PVRSRVBridgeRIWritePMREntry(IMG_UINT32 ui32DispatchTableEntry,
			    IMG_UINT8 * psRIWritePMREntryIN_UI8,
			    IMG_UINT8 * psRIWritePMREntryOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIWRITEPMRENTRY *psRIWritePMREntryIN =
	    (PVRSRV_BRIDGE_IN_RIWRITEPMRENTRY *) IMG_OFFSET_ADDR(psRIWritePMREntryIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRY *psRIWritePMREntryOUT =
	    (PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRY *) IMG_OFFSET_ADDR(psRIWritePMREntryOUT_UI8, 0);

	IMG_HANDLE hPMRHandle = psRIWritePMREntryIN->hPMRHandle;
	PMR *psPMRHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRIWritePMREntryOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRHandleInt,
				       hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRIWritePMREntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIWritePMREntry_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRIWritePMREntryOUT->eError = RIWritePMREntryKM(psPMRHandleInt);

RIWritePMREntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static PVRSRV_ERROR _RIWriteMEMDESCEntrypsRIHandleIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RIDeleteMEMDESCEntryKM((RI_HANDLE) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRIWriteMEMDESCEntry(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRIWriteMEMDESCEntryIN_UI8,
				IMG_UINT8 * psRIWriteMEMDESCEntryOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryIN =
	    (PVRSRV_BRIDGE_IN_RIWRITEMEMDESCENTRY *) IMG_OFFSET_ADDR(psRIWriteMEMDESCEntryIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryOUT =
	    (PVRSRV_BRIDGE_OUT_RIWRITEMEMDESCENTRY *) IMG_OFFSET_ADDR(psRIWriteMEMDESCEntryOUT_UI8,
								      0);

	IMG_HANDLE hPMRHandle = psRIWriteMEMDESCEntryIN->hPMRHandle;
	PMR *psPMRHandleInt = NULL;
	IMG_CHAR *uiTextBInt = NULL;
	RI_HANDLE psRIHandleInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = (psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psRIWriteMEMDESCEntryIN->ui32TextBSize > DEVMEM_ANNOTATION_MAX_LEN))
	{
		psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RIWriteMEMDESCEntry_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRIWriteMEMDESCEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRIWriteMEMDESCEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RIWriteMEMDESCEntry_exit;
			}
		}
	}

	if (psRIWriteMEMDESCEntryIN->ui32TextBSize != 0)
	{
		uiTextBInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextBInt, (const void __user *)psRIWriteMEMDESCEntryIN->puiTextB,
		     psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RIWriteMEMDESCEntry_exit;
		}
		((IMG_CHAR *)
		 uiTextBInt)[(psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) - 1] =
       '\0';
	}

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRIWriteMEMDESCEntryOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRHandleInt,
				       hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIWriteMEMDESCEntry_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRIWriteMEMDESCEntryOUT->eError =
	    RIWriteMEMDESCEntryKM(psPMRHandleInt,
				  psRIWriteMEMDESCEntryIN->ui32TextBSize,
				  uiTextBInt,
				  psRIWriteMEMDESCEntryIN->ui64Offset,
				  psRIWriteMEMDESCEntryIN->ui64Size,
				  psRIWriteMEMDESCEntryIN->bIsImport,
				  psRIWriteMEMDESCEntryIN->bIsSuballoc, &psRIHandleInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK))
	{
		goto RIWriteMEMDESCEntry_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRIWriteMEMDESCEntryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								     &psRIWriteMEMDESCEntryOUT->
								     hRIHandle,
								     (void *)psRIHandleInt,
								     PVRSRV_HANDLE_TYPE_RI_HANDLE,
								     PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								     (PFN_HANDLE_RELEASE) &
								     _RIWriteMEMDESCEntrypsRIHandleIntRelease);
	if (unlikely(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIWriteMEMDESCEntry_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RIWriteMEMDESCEntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	if (psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		if (psRIHandleInt)
		{
			RIDeleteMEMDESCEntryKM(psRIHandleInt);
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

static PVRSRV_ERROR _RIWriteProcListEntrypsRIHandleIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = RIDeleteMEMDESCEntryKM((RI_HANDLE) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgeRIWriteProcListEntry(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psRIWriteProcListEntryIN_UI8,
				 IMG_UINT8 * psRIWriteProcListEntryOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIWRITEPROCLISTENTRY *psRIWriteProcListEntryIN =
	    (PVRSRV_BRIDGE_IN_RIWRITEPROCLISTENTRY *) IMG_OFFSET_ADDR(psRIWriteProcListEntryIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_RIWRITEPROCLISTENTRY *psRIWriteProcListEntryOUT =
	    (PVRSRV_BRIDGE_OUT_RIWRITEPROCLISTENTRY *)
	    IMG_OFFSET_ADDR(psRIWriteProcListEntryOUT_UI8, 0);

	IMG_CHAR *uiTextBInt = NULL;
	RI_HANDLE psRIHandleInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize =
	    (psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) + 0;

	if (unlikely(psRIWriteProcListEntryIN->ui32TextBSize > DEVMEM_ANNOTATION_MAX_LEN))
	{
		psRIWriteProcListEntryOUT->eError = PVRSRV_ERROR_BRIDGE_ARRAY_SIZE_TOO_BIG;
		goto RIWriteProcListEntry_exit;
	}

	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset =
		    PVR_ALIGN(sizeof(*psRIWriteProcListEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize =
		    ui32InBufferOffset >=
		    PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 : PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *) (void *)psRIWriteProcListEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];
		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocZMemNoStats(ui32BufferSize);

			if (!pArrayArgsBuffer)
			{
				psRIWriteProcListEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RIWriteProcListEntry_exit;
			}
		}
	}

	if (psRIWriteProcListEntryIN->ui32TextBSize != 0)
	{
		uiTextBInt = (IMG_CHAR *) IMG_OFFSET_ADDR(pArrayArgsBuffer, ui32NextOffset);
		ui32NextOffset += psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR);
	}

	/* Copy the data over */
	if (psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR) > 0)
	{
		if (OSCopyFromUser
		    (NULL, uiTextBInt, (const void __user *)psRIWriteProcListEntryIN->puiTextB,
		     psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) != PVRSRV_OK)
		{
			psRIWriteProcListEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto RIWriteProcListEntry_exit;
		}
		((IMG_CHAR *)
		 uiTextBInt)[(psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) - 1] =
       '\0';
	}

	psRIWriteProcListEntryOUT->eError =
	    RIWriteProcListEntryKM(psRIWriteProcListEntryIN->ui32TextBSize,
				   uiTextBInt,
				   psRIWriteProcListEntryIN->ui64Size,
				   psRIWriteProcListEntryIN->ui64DevVAddr, &psRIHandleInt);
	/* Exit early if bridged call fails */
	if (unlikely(psRIWriteProcListEntryOUT->eError != PVRSRV_OK))
	{
		goto RIWriteProcListEntry_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psRIWriteProcListEntryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								      &psRIWriteProcListEntryOUT->
								      hRIHandle,
								      (void *)psRIHandleInt,
								      PVRSRV_HANDLE_TYPE_RI_HANDLE,
								      PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								      (PFN_HANDLE_RELEASE) &
								      _RIWriteProcListEntrypsRIHandleIntRelease);
	if (unlikely(psRIWriteProcListEntryOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIWriteProcListEntry_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

RIWriteProcListEntry_exit:

	if (psRIWriteProcListEntryOUT->eError != PVRSRV_OK)
	{
		if (psRIHandleInt)
		{
			RIDeleteMEMDESCEntryKM(psRIHandleInt);
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
PVRSRVBridgeRIUpdateMEMDESCAddr(IMG_UINT32 ui32DispatchTableEntry,
				IMG_UINT8 * psRIUpdateMEMDESCAddrIN_UI8,
				IMG_UINT8 * psRIUpdateMEMDESCAddrOUT_UI8,
				CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrIN =
	    (PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCADDR *) IMG_OFFSET_ADDR(psRIUpdateMEMDESCAddrIN_UI8,
								     0);
	PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrOUT =
	    (PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCADDR *) IMG_OFFSET_ADDR(psRIUpdateMEMDESCAddrOUT_UI8,
								      0);

	IMG_HANDLE hRIHandle = psRIUpdateMEMDESCAddrIN->hRIHandle;
	RI_HANDLE psRIHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRIUpdateMEMDESCAddrOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psRIHandleInt,
				       hRIHandle, PVRSRV_HANDLE_TYPE_RI_HANDLE, IMG_TRUE);
	if (unlikely(psRIUpdateMEMDESCAddrOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIUpdateMEMDESCAddr_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRIUpdateMEMDESCAddrOUT->eError =
	    RIUpdateMEMDESCAddrKM(psRIHandleInt, psRIUpdateMEMDESCAddrIN->sAddr);

RIUpdateMEMDESCAddr_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psRIHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hRIHandle, PVRSRV_HANDLE_TYPE_RI_HANDLE);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDeleteMEMDESCEntry(IMG_UINT32 ui32DispatchTableEntry,
				 IMG_UINT8 * psRIDeleteMEMDESCEntryIN_UI8,
				 IMG_UINT8 * psRIDeleteMEMDESCEntryOUT_UI8,
				 CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryIN =
	    (PVRSRV_BRIDGE_IN_RIDELETEMEMDESCENTRY *) IMG_OFFSET_ADDR(psRIDeleteMEMDESCEntryIN_UI8,
								      0);
	PVRSRV_BRIDGE_OUT_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryOUT =
	    (PVRSRV_BRIDGE_OUT_RIDELETEMEMDESCENTRY *)
	    IMG_OFFSET_ADDR(psRIDeleteMEMDESCEntryOUT_UI8, 0);

	/* Lock over handle destruction. */
	LockHandle(psConnection->psHandleBase);

	psRIDeleteMEMDESCEntryOUT->eError =
	    PVRSRVReleaseHandleStagedUnlock(psConnection->psHandleBase,
					    (IMG_HANDLE) psRIDeleteMEMDESCEntryIN->hRIHandle,
					    PVRSRV_HANDLE_TYPE_RI_HANDLE);
	if (unlikely((psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_OK) &&
		     (psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_ERROR_RETRY)))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: %s",
			 __func__, PVRSRVGetErrorString(psRIDeleteMEMDESCEntryOUT->eError)));
		UnlockHandle(psConnection->psHandleBase);
		goto RIDeleteMEMDESCEntry_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle(psConnection->psHandleBase);

RIDeleteMEMDESCEntry_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpList(IMG_UINT32 ui32DispatchTableEntry,
		       IMG_UINT8 * psRIDumpListIN_UI8,
		       IMG_UINT8 * psRIDumpListOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIDUMPLIST *psRIDumpListIN =
	    (PVRSRV_BRIDGE_IN_RIDUMPLIST *) IMG_OFFSET_ADDR(psRIDumpListIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RIDUMPLIST *psRIDumpListOUT =
	    (PVRSRV_BRIDGE_OUT_RIDUMPLIST *) IMG_OFFSET_ADDR(psRIDumpListOUT_UI8, 0);

	IMG_HANDLE hPMRHandle = psRIDumpListIN->hPMRHandle;
	PMR *psPMRHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRIDumpListOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRHandleInt,
				       hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRIDumpListOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIDumpList_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRIDumpListOUT->eError = RIDumpListKM(psPMRHandleInt);

RIDumpList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpAll(IMG_UINT32 ui32DispatchTableEntry,
		      IMG_UINT8 * psRIDumpAllIN_UI8,
		      IMG_UINT8 * psRIDumpAllOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIDUMPALL *psRIDumpAllIN =
	    (PVRSRV_BRIDGE_IN_RIDUMPALL *) IMG_OFFSET_ADDR(psRIDumpAllIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RIDUMPALL *psRIDumpAllOUT =
	    (PVRSRV_BRIDGE_OUT_RIDUMPALL *) IMG_OFFSET_ADDR(psRIDumpAllOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psRIDumpAllIN);

	psRIDumpAllOUT->eError = RIDumpAllKM();

	return 0;
}

static IMG_INT
PVRSRVBridgeRIDumpProcess(IMG_UINT32 ui32DispatchTableEntry,
			  IMG_UINT8 * psRIDumpProcessIN_UI8,
			  IMG_UINT8 * psRIDumpProcessOUT_UI8, CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIDUMPPROCESS *psRIDumpProcessIN =
	    (PVRSRV_BRIDGE_IN_RIDUMPPROCESS *) IMG_OFFSET_ADDR(psRIDumpProcessIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RIDUMPPROCESS *psRIDumpProcessOUT =
	    (PVRSRV_BRIDGE_OUT_RIDUMPPROCESS *) IMG_OFFSET_ADDR(psRIDumpProcessOUT_UI8, 0);

	PVR_UNREFERENCED_PARAMETER(psConnection);

	psRIDumpProcessOUT->eError = RIDumpProcessKM(psRIDumpProcessIN->ui32Pid);

	return 0;
}

static IMG_INT
PVRSRVBridgeRIWritePMREntryWithOwner(IMG_UINT32 ui32DispatchTableEntry,
				     IMG_UINT8 * psRIWritePMREntryWithOwnerIN_UI8,
				     IMG_UINT8 * psRIWritePMREntryWithOwnerOUT_UI8,
				     CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_RIWRITEPMRENTRYWITHOWNER *psRIWritePMREntryWithOwnerIN =
	    (PVRSRV_BRIDGE_IN_RIWRITEPMRENTRYWITHOWNER *)
	    IMG_OFFSET_ADDR(psRIWritePMREntryWithOwnerIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRYWITHOWNER *psRIWritePMREntryWithOwnerOUT =
	    (PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRYWITHOWNER *)
	    IMG_OFFSET_ADDR(psRIWritePMREntryWithOwnerOUT_UI8, 0);

	IMG_HANDLE hPMRHandle = psRIWritePMREntryWithOwnerIN->hPMRHandle;
	PMR *psPMRHandleInt = NULL;

	/* Lock over handle lookup. */
	LockHandle(psConnection->psHandleBase);

	/* Look up the address from the handle */
	psRIWritePMREntryWithOwnerOUT->eError =
	    PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
				       (void **)&psPMRHandleInt,
				       hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR, IMG_TRUE);
	if (unlikely(psRIWritePMREntryWithOwnerOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto RIWritePMREntryWithOwner_exit;
	}
	/* Release now we have looked up handles. */
	UnlockHandle(psConnection->psHandleBase);

	psRIWritePMREntryWithOwnerOUT->eError =
	    RIWritePMREntryWithOwnerKM(psPMRHandleInt, psRIWritePMREntryWithOwnerIN->ui32Owner);

RIWritePMREntryWithOwner_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle(psConnection->psHandleBase);

	/* Unreference the previously looked up handle */
	if (psPMRHandleInt)
	{
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					    hPMRHandle, PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
	}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle(psConnection->psHandleBase);

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitRIBridge(void);
PVRSRV_ERROR DeinitRIBridge(void);

/*
 * Register all RI functions with services
 */
PVRSRV_ERROR InitRIBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRY,
			      PVRSRVBridgeRIWritePMREntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEMEMDESCENTRY,
			      PVRSRVBridgeRIWriteMEMDESCEntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPROCLISTENTRY,
			      PVRSRVBridgeRIWriteProcListEntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCADDR,
			      PVRSRVBridgeRIUpdateMEMDESCAddr, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDELETEMEMDESCENTRY,
			      PVRSRVBridgeRIDeleteMEMDESCEntry, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPLIST, PVRSRVBridgeRIDumpList,
			      NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPALL, PVRSRVBridgeRIDumpAll,
			      NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPPROCESS,
			      PVRSRVBridgeRIDumpProcess, NULL);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRYWITHOWNER,
			      PVRSRVBridgeRIWritePMREntryWithOwner, NULL);

	return PVRSRV_OK;
}

/*
 * Unregister all ri functions with services
 */
PVRSRV_ERROR DeinitRIBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEMEMDESCENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPROCLISTENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCADDR);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDELETEMEMDESCENTRY);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPLIST);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPALL);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPPROCESS);

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRYWITHOWNER);

	return PVRSRV_OK;
}
