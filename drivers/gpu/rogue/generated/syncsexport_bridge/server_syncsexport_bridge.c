/*************************************************************************/ /*!
@File
@Title          Server bridge for syncsexport
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for syncsexport
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

#include "sync_server.h"


#include "common_syncsexport_bridge.h"

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
SyncPrimServerSecureUnexportResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgeSyncPrimServerSecureExport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVERSECUREEXPORT *psSyncPrimServerSecureExportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVERSECUREEXPORT *psSyncPrimServerSecureExportOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;
	SERVER_SYNC_EXPORT * psExportInt = IMG_NULL;
	IMG_HANDLE hExportInt2 = IMG_NULL;
	CONNECTION_DATA *psSecureConnection;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREEXPORT);





				{
					/* Look up the address from the handle */
					psSyncPrimServerSecureExportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimServerSecureExportIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psSyncPrimServerSecureExportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerSecureExport_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimServerSecureExportOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimServerSecureExportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerSecureExport_exit;
					}
				}

	psSyncPrimServerSecureExportOUT->eError =
		PVRSRVSyncPrimServerSecureExportKM(psConnection,
					psSyncHandleInt,
					&psSyncPrimServerSecureExportOUT->Export,
					&psExportInt, &psSecureConnection);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerSecureExportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerSecureExport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hExportInt2 = ResManRegisterRes(psSecureConnection->hResManContext,
												RESMAN_TYPE_SERVER_SYNC_EXPORT,
												psExportInt,
												(RESMAN_FREE_FN)&PVRSRVSyncPrimServerSecureUnexportKM);
	if (hExportInt2 == IMG_NULL)
	{
		psSyncPrimServerSecureExportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncPrimServerSecureExport_exit;
	}


SyncPrimServerSecureExport_exit:
	if (psSyncPrimServerSecureExportOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hExportInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hExportInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psExportInt)
		{
			PVRSRVSyncPrimServerSecureUnexportKM(psExportInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimServerSecureUnexport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVERSECUREUNEXPORT *psSyncPrimServerSecureUnexportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVERSECUREUNEXPORT *psSyncPrimServerSecureUnexportOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREUNEXPORT);





				{
					/* Look up the address from the handle */
					psSyncPrimServerSecureUnexportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hExportInt2,
											psSyncPrimServerSecureUnexportIN->hExport,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);
					if(psSyncPrimServerSecureUnexportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerSecureUnexport_exit;
					}

				}

	psSyncPrimServerSecureUnexportOUT->eError = SyncPrimServerSecureUnexportResManProxy(hExportInt2);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerSecureUnexportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerSecureUnexport_exit;
	}

	psSyncPrimServerSecureUnexportOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psSyncPrimServerSecureUnexportIN->hExport,
					PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);


SyncPrimServerSecureUnexport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimServerSecureImport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVERSECUREIMPORT *psSyncPrimServerSecureImportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVERSECUREIMPORT *psSyncPrimServerSecureImportOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREIMPORT);





	psSyncPrimServerSecureImportOUT->eError =
		PVRSRVSyncPrimServerSecureImportKM(
					psSyncPrimServerSecureImportIN->Export,
					&psSyncHandleInt,
					&psSyncPrimServerSecureImportOUT->ui32SyncPrimVAddr);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerSecureImportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerSecureImport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hSyncHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SERVER_SYNC_PRIMITIVE,
												psSyncHandleInt,
												(RESMAN_FREE_FN)&PVRSRVServerSyncFreeKM);
	if (hSyncHandleInt2 == IMG_NULL)
	{
		psSyncPrimServerSecureImportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncPrimServerSecureImport_exit;
	}
	psSyncPrimServerSecureImportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psSyncPrimServerSecureImportOUT->hSyncHandle,
							(IMG_HANDLE) hSyncHandleInt2,
							PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psSyncPrimServerSecureImportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerSecureImport_exit;
	}


SyncPrimServerSecureImport_exit:
	if (psSyncPrimServerSecureImportOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hSyncHandleInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hSyncHandleInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psSyncHandleInt)
		{
			PVRSRVServerSyncFreeKM(psSyncHandleInt);
		}
	}


	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterSYNCSEXPORTFunctions(IMG_VOID);
IMG_VOID UnregisterSYNCSEXPORTFunctions(IMG_VOID);

/*
 * Register all SYNCSEXPORT functions with services
 */
PVRSRV_ERROR RegisterSYNCSEXPORTFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREEXPORT, PVRSRVBridgeSyncPrimServerSecureExport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREUNEXPORT, PVRSRVBridgeSyncPrimServerSecureUnexport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCSEXPORT_SYNCPRIMSERVERSECUREIMPORT, PVRSRVBridgeSyncPrimServerSecureImport);

	return PVRSRV_OK;
}

/*
 * Unregister all syncsexport functions with services
 */
IMG_VOID UnregisterSYNCSEXPORTFunctions(IMG_VOID)
{
}
