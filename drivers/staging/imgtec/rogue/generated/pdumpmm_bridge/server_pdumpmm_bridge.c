/*************************************************************************/ /*!
@File
@Title          Server bridge for pdumpmm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for pdumpmm
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
#include <linux/uaccess.h>

#include "osfunc.h"
#include "img_defs.h"

#include "devicemem_server.h"
#include "pmr.h"
#include "physmem.h"


#include "common_pdumpmm_bridge.h"

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
PVRSRVBridgePMRPDumpLoadMem(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEM *psPMRPDumpLoadMemIN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEM *psPMRPDumpLoadMemOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpLoadMemIN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpLoadMemOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpLoadMem_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpLoadMemOUT->eError =
		PMRPDumpLoadMem(
					psPMRInt,
					psPMRPDumpLoadMemIN->uiOffset,
					psPMRPDumpLoadMemIN->uiSize,
					psPMRPDumpLoadMemIN->ui32PDumpFlags,
					psPMRPDumpLoadMemIN->bbZero);




PMRPDumpLoadMem_exit:

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
PVRSRVBridgePMRPDumpLoadMemValue32(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE32 *psPMRPDumpLoadMemValue32IN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE32 *psPMRPDumpLoadMemValue32OUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpLoadMemValue32IN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemValue32OUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpLoadMemValue32OUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpLoadMemValue32_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpLoadMemValue32OUT->eError =
		PMRPDumpLoadMemValue32(
					psPMRInt,
					psPMRPDumpLoadMemValue32IN->uiOffset,
					psPMRPDumpLoadMemValue32IN->ui32Value,
					psPMRPDumpLoadMemValue32IN->ui32PDumpFlags);




PMRPDumpLoadMemValue32_exit:

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
PVRSRVBridgePMRPDumpLoadMemValue64(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE64 *psPMRPDumpLoadMemValue64IN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE64 *psPMRPDumpLoadMemValue64OUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpLoadMemValue64IN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemValue64OUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpLoadMemValue64OUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpLoadMemValue64_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpLoadMemValue64OUT->eError =
		PMRPDumpLoadMemValue64(
					psPMRInt,
					psPMRPDumpLoadMemValue64IN->uiOffset,
					psPMRPDumpLoadMemValue64IN->ui64Value,
					psPMRPDumpLoadMemValue64IN->ui32PDumpFlags);




PMRPDumpLoadMemValue64_exit:

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
PVRSRVBridgePMRPDumpSaveToFile(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPSAVETOFILE *psPMRPDumpSaveToFileIN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPSAVETOFILE *psPMRPDumpSaveToFileOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpSaveToFileIN->hPMR;
	PMR * psPMRInt = NULL;
	IMG_CHAR *uiFileNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psPMRPDumpSaveToFileIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psPMRPDumpSaveToFileIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psPMRPDumpSaveToFileOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PMRPDumpSaveToFile_exit;
			}
		}
	}

	if (psPMRPDumpSaveToFileIN->ui32ArraySize != 0)
	{
		uiFileNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiFileNameInt, psPMRPDumpSaveToFileIN->puiFileName, psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psPMRPDumpSaveToFileOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto PMRPDumpSaveToFile_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpSaveToFileOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpSaveToFileOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpSaveToFile_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpSaveToFileOUT->eError =
		PMRPDumpSaveToFile(
					psPMRInt,
					psPMRPDumpSaveToFileIN->uiOffset,
					psPMRPDumpSaveToFileIN->uiSize,
					psPMRPDumpSaveToFileIN->ui32ArraySize,
					uiFileNameInt,
					psPMRPDumpSaveToFileIN->ui32uiFileOffset);




PMRPDumpSaveToFile_exit:

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
PVRSRVBridgePMRPDumpSymbolicAddr(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPSYMBOLICADDR *psPMRPDumpSymbolicAddrIN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPSYMBOLICADDR *psPMRPDumpSymbolicAddrOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpSymbolicAddrIN->hPMR;
	PMR * psPMRInt = NULL;
	IMG_CHAR *puiMemspaceNameInt = NULL;
	IMG_CHAR *puiSymbolicAddrInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR)) +
			(psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR)) +
			0;



	psPMRPDumpSymbolicAddrOUT->puiMemspaceName = psPMRPDumpSymbolicAddrIN->puiMemspaceName;
	psPMRPDumpSymbolicAddrOUT->puiSymbolicAddr = psPMRPDumpSymbolicAddrIN->puiSymbolicAddr;


	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psPMRPDumpSymbolicAddrIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psPMRPDumpSymbolicAddrIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto PMRPDumpSymbolicAddr_exit;
			}
		}
	}

	if (psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen != 0)
	{
		puiMemspaceNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR);
	}

	if (psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen != 0)
	{
		puiSymbolicAddrInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR);
	}


	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpSymbolicAddrOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpSymbolicAddrOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpSymbolicAddr_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpSymbolicAddrOUT->eError =
		PMR_PDumpSymbolicAddr(
					psPMRInt,
					psPMRPDumpSymbolicAddrIN->uiOffset,
					psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen,
					puiMemspaceNameInt,
					psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen,
					puiSymbolicAddrInt,
					&psPMRPDumpSymbolicAddrOUT->uiNewOffset,
					&psPMRPDumpSymbolicAddrOUT->uiNextSymName);



	if ((psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR)) > 0)
	{
		if ( OSCopyToUser(NULL, psPMRPDumpSymbolicAddrOUT->puiMemspaceName, puiMemspaceNameInt,
			(psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR))) != PVRSRV_OK )
		{
			psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PMRPDumpSymbolicAddr_exit;
		}
	}

	if ((psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR)) > 0)
	{
		if ( OSCopyToUser(NULL, psPMRPDumpSymbolicAddrOUT->puiSymbolicAddr, puiSymbolicAddrInt,
			(psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR))) != PVRSRV_OK )
		{
			psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

			goto PMRPDumpSymbolicAddr_exit;
		}
	}


PMRPDumpSymbolicAddr_exit:

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
PVRSRVBridgePMRPDumpPol32(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPPOL32 *psPMRPDumpPol32IN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPPOL32 *psPMRPDumpPol32OUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpPol32IN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpPol32OUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpPol32OUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpPol32_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpPol32OUT->eError =
		PMRPDumpPol32(
					psPMRInt,
					psPMRPDumpPol32IN->uiOffset,
					psPMRPDumpPol32IN->ui32Value,
					psPMRPDumpPol32IN->ui32Mask,
					psPMRPDumpPol32IN->eOperator,
					psPMRPDumpPol32IN->ui32PDumpFlags);




PMRPDumpPol32_exit:

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
PVRSRVBridgePMRPDumpCBP(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_PMRPDUMPCBP *psPMRPDumpCBPIN,
					  PVRSRV_BRIDGE_OUT_PMRPDUMPCBP *psPMRPDumpCBPOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMR = psPMRPDumpCBPIN->hPMR;
	PMR * psPMRInt = NULL;







	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psPMRPDumpCBPOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psPMRInt,
											hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
											IMG_TRUE);
					if(psPMRPDumpCBPOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto PMRPDumpCBP_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psPMRPDumpCBPOUT->eError =
		PMRPDumpCBP(
					psPMRInt,
					psPMRPDumpCBPIN->uiReadOffset,
					psPMRPDumpCBPIN->uiWriteOffset,
					psPMRPDumpCBPIN->uiPacketSize,
					psPMRPDumpCBPIN->uiBufferSize);




PMRPDumpCBP_exit:

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
PVRSRVBridgeDevmemIntPDumpSaveToFileVirtual(IMG_UINT32 ui32DispatchTableEntry,
					  PVRSRV_BRIDGE_IN_DEVMEMINTPDUMPSAVETOFILEVIRTUAL *psDevmemIntPDumpSaveToFileVirtualIN,
					  PVRSRV_BRIDGE_OUT_DEVMEMINTPDUMPSAVETOFILEVIRTUAL *psDevmemIntPDumpSaveToFileVirtualOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevmemServerContext = psDevmemIntPDumpSaveToFileVirtualIN->hDevmemServerContext;
	DEVMEMINT_CTX * psDevmemServerContextInt = NULL;
	IMG_CHAR *uiFileNameInt = NULL;

	IMG_UINT32 ui32NextOffset = 0;
	IMG_BYTE   *pArrayArgsBuffer = NULL;
#if !defined(INTEGRITY_OS)
	IMG_BOOL bHaveEnoughSpace = IMG_FALSE;
#endif

	IMG_UINT32 ui32BufferSize = 
			(psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR)) +
			0;





	if (ui32BufferSize != 0)
	{
#if !defined(INTEGRITY_OS)
		/* Try to use remainder of input buffer for copies if possible, word-aligned for safety. */
		IMG_UINT32 ui32InBufferOffset = PVR_ALIGN(sizeof(*psDevmemIntPDumpSaveToFileVirtualIN), sizeof(unsigned long));
		IMG_UINT32 ui32InBufferExcessSize = ui32InBufferOffset >= PVRSRV_MAX_BRIDGE_IN_SIZE ? 0 :
			PVRSRV_MAX_BRIDGE_IN_SIZE - ui32InBufferOffset;

		bHaveEnoughSpace = ui32BufferSize <= ui32InBufferExcessSize;
		if (bHaveEnoughSpace)
		{
			IMG_BYTE *pInputBuffer = (IMG_BYTE *)psDevmemIntPDumpSaveToFileVirtualIN;

			pArrayArgsBuffer = &pInputBuffer[ui32InBufferOffset];		}
		else
#endif
		{
			pArrayArgsBuffer = OSAllocMemNoStats(ui32BufferSize);

			if(!pArrayArgsBuffer)
			{
				psDevmemIntPDumpSaveToFileVirtualOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto DevmemIntPDumpSaveToFileVirtual_exit;
			}
		}
	}

	if (psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize != 0)
	{
		uiFileNameInt = (IMG_CHAR*)(((IMG_UINT8 *)pArrayArgsBuffer) + ui32NextOffset);
		ui32NextOffset += psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR);
	}

			/* Copy the data over */
			if (psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR) > 0)
			{
				if ( OSCopyFromUser(NULL, uiFileNameInt, psDevmemIntPDumpSaveToFileVirtualIN->puiFileName, psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR)) != PVRSRV_OK )
				{
					psDevmemIntPDumpSaveToFileVirtualOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

					goto DevmemIntPDumpSaveToFileVirtual_exit;
				}
			}

	/* Lock over handle lookup. */
	LockHandle();





				{
					/* Look up the address from the handle */
					psDevmemIntPDumpSaveToFileVirtualOUT->eError =
						PVRSRVLookupHandleUnlocked(psConnection->psHandleBase,
											(void **) &psDevmemServerContextInt,
											hDevmemServerContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
											IMG_TRUE);
					if(psDevmemIntPDumpSaveToFileVirtualOUT->eError != PVRSRV_OK)
					{
						UnlockHandle();
						goto DevmemIntPDumpSaveToFileVirtual_exit;
					}
				}
	/* Release now we have looked up handles. */
	UnlockHandle();

	psDevmemIntPDumpSaveToFileVirtualOUT->eError =
		DevmemIntPDumpSaveToFileVirtual(
					psDevmemServerContextInt,
					psDevmemIntPDumpSaveToFileVirtualIN->sAddress,
					psDevmemIntPDumpSaveToFileVirtualIN->uiSize,
					psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize,
					uiFileNameInt,
					psDevmemIntPDumpSaveToFileVirtualIN->ui32FileOffset,
					psDevmemIntPDumpSaveToFileVirtualIN->ui32PDumpFlags);




DevmemIntPDumpSaveToFileVirtual_exit:

	/* Lock over handle lookup cleanup. */
	LockHandle();






				{
					/* Unreference the previously looked up handle */
						if(psDevmemServerContextInt)
						{
							PVRSRVReleaseHandleUnlocked(psConnection->psHandleBase,
											hDevmemServerContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
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




/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */

static IMG_BOOL bUseLock = IMG_TRUE;

PVRSRV_ERROR InitPDUMPMMBridge(void);
PVRSRV_ERROR DeinitPDUMPMMBridge(void);

/*
 * Register all PDUMPMM functions with services
 */
PVRSRV_ERROR InitPDUMPMMBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEM, PVRSRVBridgePMRPDumpLoadMem,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE32, PVRSRVBridgePMRPDumpLoadMemValue32,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE64, PVRSRVBridgePMRPDumpLoadMemValue64,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSAVETOFILE, PVRSRVBridgePMRPDumpSaveToFile,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSYMBOLICADDR, PVRSRVBridgePMRPDumpSymbolicAddr,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPPOL32, PVRSRVBridgePMRPDumpPol32,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPCBP, PVRSRVBridgePMRPDumpCBP,
					NULL, bUseLock);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM, PVRSRV_BRIDGE_PDUMPMM_DEVMEMINTPDUMPSAVETOFILEVIRTUAL, PVRSRVBridgeDevmemIntPDumpSaveToFileVirtual,
					NULL, bUseLock);


	return PVRSRV_OK;
}

/*
 * Unregister all pdumpmm functions with services
 */
PVRSRV_ERROR DeinitPDUMPMMBridge(void)
{
	return PVRSRV_OK;
}
