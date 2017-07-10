/*************************************************************************/ /*!
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
*/ /**************************************************************************/

#include <stddef.h>
#include <asm/uaccess.h>

#include "img_defs.h"

#include "ri_server.h"


#include "common_ri_bridge.h"

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
PVRSRVBridgeRIWritePMREntry(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIWRITEPMRENTRY *psRIWritePMREntryIN,
					  PVRSRV_BRIDGE_OUT_RIWRITEPMRENTRY *psRIWritePMREntryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRHandle = psRIWritePMREntryIN->hPMRHandle;
	PMR * psPMRHandleInt = NULL;
	IMG_CHAR *uiTextAInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRIWritePMREntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRIWritePMREntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRIWritePMREntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RIWritePMREntry_exit;
			}
		}
	}

	if (psRIWritePMREntryIN->ui32TextASize != 0)
	{
		uiTextAInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiTextAInt, psRIWritePMREntryIN->puiTextA, psRIWritePMREntryIN->ui32TextASize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRIWritePMREntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RIWritePMREntry_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIWritePMREntryOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRHandleInt,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRIWritePMREntryOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIWritePMREntry_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIWritePMREntryOUT->eError =
		RIWritePMREntryKM(
					psPMRHandleInt,
					psRIWritePMREntryIN->ui32TextASize,
					uiTextAInt,
					psRIWritePMREntryIN->ui64LogicalSize);




RIWritePMREntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
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
PVRSRVBridgeRIWriteMEMDESCEntry(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryIN,
					  PVRSRV_BRIDGE_OUT_RIWRITEMEMDESCENTRY *psRIWriteMEMDESCEntryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRHandle = psRIWriteMEMDESCEntryIN->hPMRHandle;
	PMR * psPMRHandleInt = NULL;
	IMG_CHAR *uiTextBInt = NULL;
	RI_HANDLE psRIHandleInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRIWriteMEMDESCEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRIWriteMEMDESCEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RIWriteMEMDESCEntry_exit;
			}
		}
	}

	if (psRIWriteMEMDESCEntryIN->ui32TextBSize != 0)
	{
		uiTextBInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiTextBInt, psRIWriteMEMDESCEntryIN->puiTextB, psRIWriteMEMDESCEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRIWriteMEMDESCEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RIWriteMEMDESCEntry_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIWriteMEMDESCEntryOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRHandleInt,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIWriteMEMDESCEntry_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIWriteMEMDESCEntryOUT->eError =
		RIWriteMEMDESCEntryKM(
					psPMRHandleInt,
					psRIWriteMEMDESCEntryIN->ui32TextBSize,
					uiTextBInt,
					psRIWriteMEMDESCEntryIN->ui64Offset,
					psRIWriteMEMDESCEntryIN->ui64Size,
					psRIWriteMEMDESCEntryIN->ui64BackedSize,
					psRIWriteMEMDESCEntryIN->bIsImport,
					psRIWriteMEMDESCEntryIN->bIsExportable,
					&psRIHandleInt);
	/* Exit early if bridged call fails */
	if(psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		goto RIWriteMEMDESCEntry_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRIWriteMEMDESCEntryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRIWriteMEMDESCEntryOUT->hRIHandle,
							(void *) psRIHandleInt,
							PVRSRV_HANDLE_TYPE_RI_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RIDeleteMEMDESCEntryKM);
	if (psRIWriteMEMDESCEntryOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RIWriteMEMDESCEntry_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



RIWriteMEMDESCEntry_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();

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
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRIWriteProcListEntry(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIWRITEPROCLISTENTRY *psRIWriteProcListEntryIN,
					  PVRSRV_BRIDGE_OUT_RIWRITEPROCLISTENTRY *psRIWriteProcListEntryOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_CHAR *uiTextBInt = NULL;
	RI_HANDLE psRIHandleInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psRIWriteProcListEntryIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psRIWriteProcListEntryIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psRIWriteProcListEntryOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto RIWriteProcListEntry_exit;
			}
		}
	}

	if (psRIWriteProcListEntryIN->ui32TextBSize != 0)
	{
		uiTextBInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiTextBInt, psRIWriteProcListEntryIN->puiTextB, psRIWriteProcListEntryIN->ui32TextBSize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psRIWriteProcListEntryOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto RIWriteProcListEntry_exit;
				}
			}


	psRIWriteProcListEntryOUT->eError =
		RIWriteProcListEntryKM(
					psRIWriteProcListEntryIN->ui32TextBSize,
					uiTextBInt,
					psRIWriteProcListEntryIN->ui64Size,
					psRIWriteProcListEntryIN->ui64BackedSize,
					psRIWriteProcListEntryIN->ui64DevVAddr,
					&psRIHandleInt);
	/* Exit early if bridged call fails */
	if(psRIWriteProcListEntryOUT->eError != PVRSRV_OK)
	{
		goto RIWriteProcListEntry_exit;
	}

	/* Lock over handle creation. */
	LockHandle();





	psRIWriteProcListEntryOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,

							&psRIWriteProcListEntryOUT->hRIHandle,
							(void *) psRIHandleInt,
							PVRSRV_HANDLE_TYPE_RI_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_MULTI
							,(PFN_HANDLE_RELEASE)&RIDeleteMEMDESCEntryKM);
	if (psRIWriteProcListEntryOUT->eError != PVRSRV_OK)
	{
		UnlockHandle();
		goto RIWriteProcListEntry_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle();



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
	if(pArrayArgsBuffer)
#else
	if(!bHaveEnoughSpace && pArrayArgsBuffer)
#endif
		OSFreeMemNoStats(pArrayArgsBuffer);


	return 0;
}


static IMG_INT
PVRSRVBridgeRIUpdateMEMDESCAddr(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrIN,
					  PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCADDR *psRIUpdateMEMDESCAddrOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRIHandle = psRIUpdateMEMDESCAddrIN->hRIHandle;
	RI_HANDLE psRIHandleInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIUpdateMEMDESCAddrOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRIHandleInt,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE,
											IMG_TRUE);
					if(psRIUpdateMEMDESCAddrOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIUpdateMEMDESCAddr_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIUpdateMEMDESCAddrOUT->eError =
		RIUpdateMEMDESCAddrKM(
					psRIHandleInt,
					psRIUpdateMEMDESCAddrIN->sAddr);




RIUpdateMEMDESCAddr_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRIHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRIUpdateMEMDESCPinning(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCPINNING *psRIUpdateMEMDESCPinningIN,
					  PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCPINNING *psRIUpdateMEMDESCPinningOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRIHandle = psRIUpdateMEMDESCPinningIN->hRIHandle;
	RI_HANDLE psRIHandleInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIUpdateMEMDESCPinningOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRIHandleInt,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE,
											IMG_TRUE);
					if(psRIUpdateMEMDESCPinningOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIUpdateMEMDESCPinning_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIUpdateMEMDESCPinningOUT->eError =
		RIUpdateMEMDESCPinningKM(
					psRIHandleInt,
					psRIUpdateMEMDESCPinningIN->bIsPinned);




RIUpdateMEMDESCPinning_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRIHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRIUpdateMEMDESCBacking(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIUPDATEMEMDESCBACKING *psRIUpdateMEMDESCBackingIN,
					  PVRSRV_BRIDGE_OUT_RIUPDATEMEMDESCBACKING *psRIUpdateMEMDESCBackingOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRIHandle = psRIUpdateMEMDESCBackingIN->hRIHandle;
	RI_HANDLE psRIHandleInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIUpdateMEMDESCBackingOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psRIHandleInt,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE,
											IMG_TRUE);
					if(psRIUpdateMEMDESCBackingOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIUpdateMEMDESCBacking_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIUpdateMEMDESCBackingOUT->eError =
		RIUpdateMEMDESCBackingKM(
					psRIHandleInt,
					psRIUpdateMEMDESCBackingIN->i32NumModified);




RIUpdateMEMDESCBacking_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psRIHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hRIHandle,
											PVRSRV_HANDLE_TYPE_RI_HANDLE);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRIDeleteMEMDESCEntry(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryIN,
					  PVRSRV_BRIDGE_OUT_RIDELETEMEMDESCENTRY *psRIDeleteMEMDESCEntryOUT,
					 CONNECTION_DATA *psConnection)
{









	/* Lock over handle destruction. */
	LockHandle();





	psRIDeleteMEMDESCEntryOUT->eError =
		PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
					(IMG_HANDLE) psRIDeleteMEMDESCEntryIN->hRIHandle,
					PVRSRV_HANDLE_TYPE_RI_HANDLE);
	if ((psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_OK) &&
	    (psRIDeleteMEMDESCEntryOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "PVRSRVBridgeRIDeleteMEMDESCEntry: %s",
		        PVRSRVGetErrorStringKM(psRIDeleteMEMDESCEntryOUT->eError)));
		PVR_ASSERT(0);
		UnlockHandle();
		goto RIDeleteMEMDESCEntry_exit;
	}

	/* Release now we have destroyed handles. */
	UnlockHandle();



RIDeleteMEMDESCEntry_exit:




	return 0;
}


static IMG_INT
PVRSRVBridgeRIDumpList(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIDUMPLIST *psRIDumpListIN,
					  PVRSRV_BRIDGE_OUT_RIDUMPLIST *psRIDumpListOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRHandle = psRIDumpListIN->hPMRHandle;
	PMR * psPMRHandleInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psRIDumpListOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRHandleInt,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psRIDumpListOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto RIDumpList_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psRIDumpListOUT->eError =
		RIDumpListKM(
					psPMRHandleInt);




RIDumpList_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psPMRHandleInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hPMRHandle,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
						}
				}
	/* Release now we have cleaned up look up handles. */
	UnlockHandle();


	return 0;
}


static IMG_INT
PVRSRVBridgeRIDumpAll(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIDUMPALL *psRIDumpAllIN,
					  PVRSRV_BRIDGE_OUT_RIDUMPALL *psRIDumpAllOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psRIDumpAllIN);





	psRIDumpAllOUT->eError =
		RIDumpAllKM(
					);








	return 0;
}


static IMG_INT
PVRSRVBridgeRIDumpProcess(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_RIDUMPPROCESS *psRIDumpProcessIN,
					  PVRSRV_BRIDGE_OUT_RIDUMPPROCESS *psRIDumpProcessOUT,
					 CONNECTION_DATA *psConnection)
{



	PVR_UNREFERENCED_PARAMETER(psConnection);





	psRIDumpProcessOUT->eError =
		RIDumpProcessKM(
					psRIDumpProcessIN->ui32Pid);








	return 0;
}




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitRIBridge(void);
PVRSRV_ERROR DeinitRIBridge(void);

/*
 * Register all RI functions with services
 */
PVRSRV_ERROR InitRIBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPMRENTRY, PVRSRVBridgeRIWritePMREntry,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEMEMDESCENTRY, PVRSRVBridgeRIWriteMEMDESCEntry,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIWRITEPROCLISTENTRY, PVRSRVBridgeRIWriteProcListEntry,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCADDR, PVRSRVBridgeRIUpdateMEMDESCAddr,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCPINNING, PVRSRVBridgeRIUpdateMEMDESCPinning,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIUPDATEMEMDESCBACKING, PVRSRVBridgeRIUpdateMEMDESCBacking,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDELETEMEMDESCENTRY, PVRSRVBridgeRIDeleteMEMDESCEntry,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPLIST, PVRSRVBridgeRIDumpList,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPALL, PVRSRVBridgeRIDumpAll,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_RI, PVRSRV_BRIDGE_RI_RIDUMPPROCESS, PVRSRVBridgeRIDumpProcess,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all ri functions with services
 */
PVRSRV_ERROR DeinitRIBridge(void)
{
	return PVRSRV_OK;
}
