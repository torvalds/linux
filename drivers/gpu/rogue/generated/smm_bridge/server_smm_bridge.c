/*************************************************************************/ /*!
@File
@Title          Server bridge for smm
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for smm
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

#include "pmr.h"
#include "secure_export.h"


#include "common_smm_bridge.h"

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

static PVRSRV_ERROR
PMRSecureUnexportPMRResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}



/* ***************************************************************************
 * Server-side bridge entry points
 */
 
static IMG_INT
PVRSRVBridgePMRSecureExportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRSECUREEXPORTPMR *psPMRSecureExportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRSECUREEXPORTPMR *psPMRSecureExportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;
	PMR * psPMROutInt = IMG_NULL;
	IMG_HANDLE hPMROutInt2 = IMG_NULL;
	CONNECTION_DATA *psSecureConnection;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SMM_PMRSECUREEXPORTPMR);





	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRSecureExportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRSecureExportPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRSecureExportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRSecureExportPMR_exit;
					}

					/* Look up the data from the resman address */
					psPMRSecureExportPMROUT->eError = ResManFindPrivateDataByPtr(hPMRInt2, (IMG_VOID **) &psPMRInt);

					if(psPMRSecureExportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRSecureExportPMR_exit;
					}
				}

	psPMRSecureExportPMROUT->eError =
		PMRSecureExportPMR(psConnection,
					psPMRInt,
					&psPMRSecureExportPMROUT->Export,
					&psPMROutInt, &psSecureConnection);
	/* Exit early if bridged call fails */
	if(psPMRSecureExportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRSecureExportPMR_exit;
	}
	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMROutInt2 = ResManRegisterRes(psSecureConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMROutInt,
												(RESMAN_FREE_FN)&PMRSecureUnexportPMR);
	if (hPMROutInt2 == IMG_NULL)
	{
		psPMRSecureExportPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRSecureExportPMR_exit;
	}


PMRSecureExportPMR_exit:
	if (psPMRSecureExportPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMROutInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMROutInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMROutInt)
		{
			PMRSecureUnexportPMR(psPMROutInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgePMRSecureUnexportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRSECUREUNEXPORTPMR *psPMRSecureUnexportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRSECUREUNEXPORTPMR *psPMRSecureUnexportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SMM_PMRSECUREUNEXPORTPMR);




	PMRLock();
				{
					/* Look up the address from the handle */
					psPMRSecureUnexportPMROUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hPMRInt2,
											psPMRSecureUnexportPMRIN->hPMR,
											PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);
					if(psPMRSecureUnexportPMROUT->eError != PVRSRV_OK)
					{
						PMRUnlock();
						goto PMRSecureUnexportPMR_exit;
					}

				}

	psPMRSecureUnexportPMROUT->eError = PMRSecureUnexportPMRResManProxy(hPMRInt2);
	/* Exit early if bridged call fails */
	if(psPMRSecureUnexportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRSecureUnexportPMR_exit;
	}
	PMRUnlock();

	psPMRSecureUnexportPMROUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psPMRSecureUnexportPMRIN->hPMR,
					PVRSRV_HANDLE_TYPE_PHYSMEM_PMR);


PMRSecureUnexportPMR_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgePMRSecureImportPMR(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_PMRSECUREIMPORTPMR *psPMRSecureImportPMRIN,
					 PVRSRV_BRIDGE_OUT_PMRSECUREIMPORTPMR *psPMRSecureImportPMROUT,
					 CONNECTION_DATA *psConnection)
{
	PMR * psPMRInt = IMG_NULL;
	IMG_HANDLE hPMRInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SMM_PMRSECUREIMPORTPMR);





	PMRLock();
	psPMRSecureImportPMROUT->eError =
		PMRSecureImportPMR(
					psPMRSecureImportPMRIN->Export,
					&psPMRInt,
					&psPMRSecureImportPMROUT->uiSize,
					&psPMRSecureImportPMROUT->sAlign);
	/* Exit early if bridged call fails */
	if(psPMRSecureImportPMROUT->eError != PVRSRV_OK)
	{
		PMRUnlock();
		goto PMRSecureImportPMR_exit;
	}
	PMRUnlock();

	/* Create a resman item and overwrite the handle with it */
	hPMRInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_PMR,
												psPMRInt,
												(RESMAN_FREE_FN)&PMRUnrefPMR);
	if (hPMRInt2 == IMG_NULL)
	{
		psPMRSecureImportPMROUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto PMRSecureImportPMR_exit;
	}
	psPMRSecureImportPMROUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psPMRSecureImportPMROUT->hPMR,
							(IMG_HANDLE) hPMRInt2,
							PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psPMRSecureImportPMROUT->eError != PVRSRV_OK)
	{
		goto PMRSecureImportPMR_exit;
	}


PMRSecureImportPMR_exit:
	if (psPMRSecureImportPMROUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hPMRInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hPMRInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psPMRInt)
		{
			PMRUnrefPMR(psPMRInt);
		}
	}


	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterSMMFunctions(IMG_VOID);
IMG_VOID UnregisterSMMFunctions(IMG_VOID);

/*
 * Register all SMM functions with services
 */
PVRSRV_ERROR RegisterSMMFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_SMM_PMRSECUREEXPORTPMR, PVRSRVBridgePMRSecureExportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SMM_PMRSECUREUNEXPORTPMR, PVRSRVBridgePMRSecureUnexportPMR);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SMM_PMRSECUREIMPORTPMR, PVRSRVBridgePMRSecureImportPMR);

	return PVRSRV_OK;
}

/*
 * Unregister all smm functions with services
 */
IMG_VOID UnregisterSMMFunctions(IMG_VOID)
{
}
