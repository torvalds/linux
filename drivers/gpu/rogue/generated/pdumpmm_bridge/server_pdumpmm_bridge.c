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
#include <asm/uaccess.h>

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

#if defined (SUPPORT_AUTH)
#include "osauth.h"
#endif

#include <linux/slab.h>

/* ***************************************************************************
 * Bridge proxy functions
 */



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgePMRPDumpLoadMem(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEM *psPMRPDumpLoadMemIN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEM *psPMRPDumpLoadMemOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEM);





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpLoadMemIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpLoadMemOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMem_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpLoadMemOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpLoadMemOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMem_exit;
					}
				}

	psPMRPDumpLoadMemOUT->eError =
		PMRPDumpLoadMem(
					psPMRInt,
					psPMRPDumpLoadMemIN->uiOffset,
					psPMRPDumpLoadMemIN->uiSize,
					psPMRPDumpLoadMemIN->ui32PDumpFlags,
					psPMRPDumpLoadMemIN->bbZero);



PMRPDumpLoadMem_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpLoadMemValue32(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE32 *psPMRPDumpLoadMemValue32IN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE32 *psPMRPDumpLoadMemValue32OUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE32);





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemValue32OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpLoadMemValue32IN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpLoadMemValue32OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMemValue32_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpLoadMemValue32OUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpLoadMemValue32OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMemValue32_exit;
					}
				}

	psPMRPDumpLoadMemValue32OUT->eError =
		PMRPDumpLoadMemValue32(
					psPMRInt,
					psPMRPDumpLoadMemValue32IN->uiOffset,
					psPMRPDumpLoadMemValue32IN->ui32Value,
					psPMRPDumpLoadMemValue32IN->ui32PDumpFlags);



PMRPDumpLoadMemValue32_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpLoadMemValue64(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPLOADMEMVALUE64 *psPMRPDumpLoadMemValue64IN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPLOADMEMVALUE64 *psPMRPDumpLoadMemValue64OUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE64);





				{
					/* Look up the address from the handle */
					psPMRPDumpLoadMemValue64OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpLoadMemValue64IN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpLoadMemValue64OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMemValue64_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpLoadMemValue64OUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpLoadMemValue64OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpLoadMemValue64_exit;
					}
				}

	psPMRPDumpLoadMemValue64OUT->eError =
		PMRPDumpLoadMemValue64(
					psPMRInt,
					psPMRPDumpLoadMemValue64IN->uiOffset,
					psPMRPDumpLoadMemValue64IN->ui64Value,
					psPMRPDumpLoadMemValue64IN->ui32PDumpFlags);



PMRPDumpLoadMemValue64_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpSaveToFile(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPSAVETOFILE *psPMRPDumpSaveToFileIN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPSAVETOFILE *psPMRPDumpSaveToFileOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	IMG_CHAR *uiFileNameInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSAVETOFILE);




	if (psPMRPDumpSaveToFileIN->ui32ArraySize != 0)
	{
		uiFileNameInt = OSAllocMem(psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR));
		if (!uiFileNameInt)
		{
			psPMRPDumpSaveToFileOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PMRPDumpSaveToFile_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psPMRPDumpSaveToFileIN->puiFileName, psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiFileNameInt, psPMRPDumpSaveToFileIN->puiFileName,
				psPMRPDumpSaveToFileIN->ui32ArraySize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psPMRPDumpSaveToFileOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto PMRPDumpSaveToFile_exit;
			}

				{
					/* Look up the address from the handle */
					psPMRPDumpSaveToFileOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpSaveToFileIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpSaveToFileOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpSaveToFile_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpSaveToFileOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpSaveToFileOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpSaveToFile_exit;
					}
				}

	psPMRPDumpSaveToFileOUT->eError =
		PMRPDumpSaveToFile(
					psPMRInt,
					psPMRPDumpSaveToFileIN->uiOffset,
					psPMRPDumpSaveToFileIN->uiSize,
					psPMRPDumpSaveToFileIN->ui32ArraySize,
					uiFileNameInt);



PMRPDumpSaveToFile_exit:
	if (uiFileNameInt)
		OSFreeMem(uiFileNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpSymbolicAddr(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPSYMBOLICADDR *psPMRPDumpSymbolicAddrIN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPSYMBOLICADDR *psPMRPDumpSymbolicAddrOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	IMG_CHAR *puiMemspaceNameInt = IMG_NULL;
	IMG_CHAR *puiSymbolicAddrInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSYMBOLICADDR);


	psPMRPDumpSymbolicAddrOUT->puiMemspaceName = psPMRPDumpSymbolicAddrIN->puiMemspaceName;
	psPMRPDumpSymbolicAddrOUT->puiSymbolicAddr = psPMRPDumpSymbolicAddrIN->puiSymbolicAddr;


	if (psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen != 0)
	{
		puiMemspaceNameInt = OSAllocMem(psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR));
		if (!puiMemspaceNameInt)
		{
			psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PMRPDumpSymbolicAddr_exit;
		}
	}

	if (psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen != 0)
	{
		puiSymbolicAddrInt = OSAllocMem(psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR));
		if (!puiSymbolicAddrInt)
		{
			psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto PMRPDumpSymbolicAddr_exit;
		}
	}


				{
					/* Look up the address from the handle */
					psPMRPDumpSymbolicAddrOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpSymbolicAddrIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpSymbolicAddrOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpSymbolicAddr_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpSymbolicAddrOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpSymbolicAddrOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpSymbolicAddr_exit;
					}
				}

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


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psPMRPDumpSymbolicAddrOUT->puiMemspaceName, (psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psPMRPDumpSymbolicAddrOUT->puiMemspaceName, puiMemspaceNameInt,
		(psPMRPDumpSymbolicAddrIN->ui32MemspaceNameLen * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto PMRPDumpSymbolicAddr_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psPMRPDumpSymbolicAddrOUT->puiSymbolicAddr, (psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR))) 
		|| (OSCopyToUser(NULL, psPMRPDumpSymbolicAddrOUT->puiSymbolicAddr, puiSymbolicAddrInt,
		(psPMRPDumpSymbolicAddrIN->ui32SymbolicAddrLen * sizeof(IMG_CHAR))) != PVRSRV_OK) )
	{
		psPMRPDumpSymbolicAddrOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto PMRPDumpSymbolicAddr_exit;
	}


PMRPDumpSymbolicAddr_exit:
	if (puiMemspaceNameInt)
		OSFreeMem(puiMemspaceNameInt);
	if (puiSymbolicAddrInt)
		OSFreeMem(puiSymbolicAddrInt);

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpPol32(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPPOL32 *psPMRPDumpPol32IN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPPOL32 *psPMRPDumpPol32OUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPPOL32);





				{
					/* Look up the address from the handle */
					psPMRPDumpPol32OUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpPol32IN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpPol32OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpPol32_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpPol32OUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpPol32OUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpPol32_exit;
					}
				}

	psPMRPDumpPol32OUT->eError =
		PMRPDumpPol32(
					psPMRInt,
					psPMRPDumpPol32IN->uiOffset,
					psPMRPDumpPol32IN->ui32Value,
					psPMRPDumpPol32IN->ui32Mask,
					psPMRPDumpPol32IN->eOperator,
					psPMRPDumpPol32IN->ui32PDumpFlags);



PMRPDumpPol32_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRPDumpCBP(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRPDUMPCBP *psPMRPDumpCBPIN,
					 PVRSRV_BRIDGE_OUT_PMRPDUMPCBP *psPMRPDumpCBPOUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPCBP);





				{
					/* Look up the address from the handle */
					psPMRPDumpCBPOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRPDumpCBPIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRPDumpCBPOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpCBP_exit;
					}

					/* Look up the data from the resman address */
					psPMRPDumpCBPOUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRPDumpCBPOUT->eError != PVRSRV_OK)
					{
						goto PMRPDumpCBP_exit;
					}
				}

	psPMRPDumpCBPOUT->eError =
		PMRPDumpCBP(
					psPMRInt,
					psPMRPDumpCBPIN->uiReadOffset,
					psPMRPDumpCBPIN->uiWriteOffset,
					psPMRPDumpCBPIN->uiPacketSize,
					psPMRPDumpCBPIN->uiBufferSize);



PMRPDumpCBP_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeDevmemIntPDumpSaveToFileVirtual(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_DEVMEMINTPDUMPSAVETOFILEVIRTUAL *psDevmemIntPDumpSaveToFileVirtualIN,
					 PVRSRV_BRIDGE_OUT_DEVMEMINTPDUMPSAVETOFILEVIRTUAL *psDevmemIntPDumpSaveToFileVirtualOUT,
					 CONNECTION_DATA *psConnection)
{
	DEVMEMINT_CTX * psDevmemServerContextInt = IMG_NULL;
	IMG_HANDLE hDevmemServerContextInt2 = IMG_NULL;
	IMG_CHAR *uiFileNameInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMPMM_DEVMEMINTPDUMPSAVETOFILEVIRTUAL);




	if (psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize != 0)
	{
		uiFileNameInt = OSAllocMem(psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR));
		if (!uiFileNameInt)
		{
			psDevmemIntPDumpSaveToFileVirtualOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto DevmemIntPDumpSaveToFileVirtual_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psDevmemIntPDumpSaveToFileVirtualIN->puiFileName, psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiFileNameInt, psDevmemIntPDumpSaveToFileVirtualIN->puiFileName,
				psDevmemIntPDumpSaveToFileVirtualIN->ui32ArraySize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psDevmemIntPDumpSaveToFileVirtualOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto DevmemIntPDumpSaveToFileVirtual_exit;
			}

				{
					/* Look up the address from the handle */
					psDevmemIntPDumpSaveToFileVirtualOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevmemServerContextInt2,
											psDevmemIntPDumpSaveToFileVirtualIN->hDevmemServerContext,
											PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX);
					if(psDevmemIntPDumpSaveToFileVirtualOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntPDumpSaveToFileVirtual_exit;
					}

					/* Look up the data from the resman address */
					psDevmemIntPDumpSaveToFileVirtualOUT->eError = ResManFindPrivateDataByPtr(hDevmemServerContextInt2, (IMG_VOID **) &psDevmemServerContextInt);

					if(psDevmemIntPDumpSaveToFileVirtualOUT->eError != PVRSRV_OK)
					{
						goto DevmemIntPDumpSaveToFileVirtual_exit;
					}
				}

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
	if (uiFileNameInt)
		OSFreeMem(uiFileNameInt);

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterPDUMPMMFunctions(IMG_VOID);
IMG_VOID UnregisterPDUMPMMFunctions(IMG_VOID);

/*
 * Register all PDUMPMM functions with services
 */
PVRSRV_ERROR RegisterPDUMPMMFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEM, PVRSRVBridgePMRPDumpLoadMem);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE32, PVRSRVBridgePMRPDumpLoadMemValue32);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPLOADMEMVALUE64, PVRSRVBridgePMRPDumpLoadMemValue64);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSAVETOFILE, PVRSRVBridgePMRPDumpSaveToFile);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPSYMBOLICADDR, PVRSRVBridgePMRPDumpSymbolicAddr);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPPOL32, PVRSRVBridgePMRPDumpPol32);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_PMRPDUMPCBP, PVRSRVBridgePMRPDumpCBP);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMPMM_DEVMEMINTPDUMPSAVETOFILEVIRTUAL, PVRSRVBridgeDevmemIntPDumpSaveToFileVirtual);

	return PVRSRV_OK;
}

/*
 * Unregister all pdumpmm functions with services
 */
IMG_VOID UnregisterPDUMPMMFunctions(IMG_VOID)
{
}
