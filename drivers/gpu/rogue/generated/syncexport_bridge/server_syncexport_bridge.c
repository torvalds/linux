/*************************************************************************/ /*!
@File
@Title          Server bridge for syncexport
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for syncexport
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


#include "common_syncexport_bridge.h"

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
SyncPrimServerUnexportResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgeSyncPrimServerExport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVEREXPORT *psSyncPrimServerExportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVEREXPORT *psSyncPrimServerExportOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;
	SERVER_SYNC_EXPORT * psExportInt = IMG_NULL;
	IMG_HANDLE hExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVEREXPORT);





				{
					/* Look up the address from the handle */
					psSyncPrimServerExportOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimServerExportIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psSyncPrimServerExportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerExport_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimServerExportOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimServerExportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerExport_exit;
					}
				}

	psSyncPrimServerExportOUT->eError =
		PVRSRVSyncPrimServerExportKM(
					psSyncHandleInt,
					&psExportInt);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerExportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerExport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hExportInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SERVER_SYNC_EXPORT,
												psExportInt,
												(RESMAN_FREE_FN)&PVRSRVSyncPrimServerUnexportKM);
	if (hExportInt2 == IMG_NULL)
	{
		psSyncPrimServerExportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncPrimServerExport_exit;
	}
	/* see if it's already exported */
	psSyncPrimServerExportOUT->eError =
		PVRSRVFindHandle(KERNEL_HANDLE_BASE,
							&psSyncPrimServerExportOUT->hExport,
							(IMG_HANDLE) hExportInt2,
							PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);
	if(psSyncPrimServerExportOUT->eError == PVRSRV_OK)
	{
		/* It's already exported */
		return 0;
	}

	psSyncPrimServerExportOUT->eError = PVRSRVAllocHandle(KERNEL_HANDLE_BASE,
							&psSyncPrimServerExportOUT->hExport,
							(IMG_HANDLE) hExportInt2,
							PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psSyncPrimServerExportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerExport_exit;
	}


SyncPrimServerExport_exit:
	if (psSyncPrimServerExportOUT->eError != PVRSRV_OK)
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
			PVRSRVSyncPrimServerUnexportKM(psExportInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimServerUnexport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVERUNEXPORT *psSyncPrimServerUnexportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVERUNEXPORT *psSyncPrimServerUnexportOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hExportInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVERUNEXPORT);

	PVR_UNREFERENCED_PARAMETER(psConnection);




				{
					/* Look up the address from the handle */
					psSyncPrimServerUnexportOUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_HANDLE *) &hExportInt2,
											psSyncPrimServerUnexportIN->hExport,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);
					if(psSyncPrimServerUnexportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerUnexport_exit;
					}

				}

	psSyncPrimServerUnexportOUT->eError = SyncPrimServerUnexportResManProxy(hExportInt2);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerUnexportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerUnexport_exit;
	}

	psSyncPrimServerUnexportOUT->eError =
		PVRSRVReleaseHandle(KERNEL_HANDLE_BASE,
					(IMG_HANDLE) psSyncPrimServerUnexportIN->hExport,
					PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);


SyncPrimServerUnexport_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimServerImport(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSERVERIMPORT *psSyncPrimServerImportIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSERVERIMPORT *psSyncPrimServerImportOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_EXPORT * psImportInt = IMG_NULL;
	IMG_HANDLE hImportInt2 = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVERIMPORT);




#if defined (SUPPORT_AUTH)
	psSyncPrimServerImportOUT->eError = OSCheckAuthentication(psConnection, 1);
	if (psSyncPrimServerImportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerImport_exit;
	}
#endif

				{
					/* Look up the address from the handle */
					psSyncPrimServerImportOUT->eError =
						PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
											(IMG_HANDLE *) &hImportInt2,
											psSyncPrimServerImportIN->hImport,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT);
					if(psSyncPrimServerImportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerImport_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimServerImportOUT->eError = ResManFindPrivateDataByPtr(hImportInt2, (IMG_VOID **) &psImportInt);

					if(psSyncPrimServerImportOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimServerImport_exit;
					}
				}

	psSyncPrimServerImportOUT->eError =
		PVRSRVSyncPrimServerImportKM(
					psImportInt,
					&psSyncHandleInt,
					&psSyncPrimServerImportOUT->ui32SyncPrimVAddr);
	/* Exit early if bridged call fails */
	if(psSyncPrimServerImportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerImport_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hSyncHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SERVER_SYNC_PRIMITIVE,
												psSyncHandleInt,
												(RESMAN_FREE_FN)&PVRSRVServerSyncFreeKM);
	if (hSyncHandleInt2 == IMG_NULL)
	{
		psSyncPrimServerImportOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncPrimServerImport_exit;
	}
	psSyncPrimServerImportOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psSyncPrimServerImportOUT->hSyncHandle,
							(IMG_HANDLE) hSyncHandleInt2,
							PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psSyncPrimServerImportOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimServerImport_exit;
	}


SyncPrimServerImport_exit:
	if (psSyncPrimServerImportOUT->eError != PVRSRV_OK)
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
 
PVRSRV_ERROR RegisterSYNCEXPORTFunctions(IMG_VOID);
IMG_VOID UnregisterSYNCEXPORTFunctions(IMG_VOID);

/*
 * Register all SYNCEXPORT functions with services
 */
PVRSRV_ERROR RegisterSYNCEXPORTFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVEREXPORT, PVRSRVBridgeSyncPrimServerExport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVERUNEXPORT, PVRSRVBridgeSyncPrimServerUnexport);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNCEXPORT_SYNCPRIMSERVERIMPORT, PVRSRVBridgeSyncPrimServerImport);

	return PVRSRV_OK;
}

/*
 * Unregister all syncexport functions with services
 */
IMG_VOID UnregisterSYNCEXPORTFunctions(IMG_VOID)
{
}
