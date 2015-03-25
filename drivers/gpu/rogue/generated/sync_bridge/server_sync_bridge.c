/*************************************************************************/ /*!
@File
@Title          Server bridge for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for sync
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
#include "pdump.h"


#include "common_sync_bridge.h"

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
FreeSyncPrimitiveBlockResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
ServerSyncFreeResManProxy(IMG_HANDLE hResmanItem)
{
	PVRSRV_ERROR eError;

	eError = ResManFreeResByPtr(hResmanItem);

	/* Freeing a resource should never fail... */
	PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));

	return eError;
}

static PVRSRV_ERROR
SyncPrimOpDestroyResManProxy(IMG_HANDLE hResmanItem)
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
PVRSRVBridgeAllocSyncPrimitiveBlock(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK *psAllocSyncPrimitiveBlockIN,
					 PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK *psAllocSyncPrimitiveBlockOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;
	DEVMEM_EXPORTCOOKIE * psExportCookieInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK);



	psAllocSyncPrimitiveBlockOUT->hSyncHandle = IMG_NULL;


				{
					/* Look up the address from the handle */
					psAllocSyncPrimitiveBlockOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psAllocSyncPrimitiveBlockIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
					{
						goto AllocSyncPrimitiveBlock_exit;
					}

				}

	psAllocSyncPrimitiveBlockOUT->eError =
		PVRSRVAllocSyncPrimitiveBlockKM(psConnection,
					hDevNodeInt,
					&psSyncHandleInt,
					&psAllocSyncPrimitiveBlockOUT->ui32SyncPrimVAddr,
					&psAllocSyncPrimitiveBlockOUT->ui32SyncPrimBlockSize,
					&psExportCookieInt);
	/* Exit early if bridged call fails */
	if(psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		goto AllocSyncPrimitiveBlock_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hSyncHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SYNC_PRIMITIVE_BLOCK,
												psSyncHandleInt,
												(RESMAN_FREE_FN)&PVRSRVFreeSyncPrimitiveBlockKM);
	if (hSyncHandleInt2 == IMG_NULL)
	{
		psAllocSyncPrimitiveBlockOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto AllocSyncPrimitiveBlock_exit;
	}
	psAllocSyncPrimitiveBlockOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psAllocSyncPrimitiveBlockOUT->hSyncHandle,
							(IMG_HANDLE) hSyncHandleInt2,
							PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		goto AllocSyncPrimitiveBlock_exit;
	}
	psAllocSyncPrimitiveBlockOUT->eError = PVRSRVAllocSubHandle(psConnection->psHandleBase,
							&psAllocSyncPrimitiveBlockOUT->hExportCookie,
							(IMG_HANDLE) psExportCookieInt,
							PVRSRV_HANDLE_TYPE_SERVER_EXPORTCOOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							,psAllocSyncPrimitiveBlockOUT->hSyncHandle);
	if (psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		goto AllocSyncPrimitiveBlock_exit;
	}


AllocSyncPrimitiveBlock_exit:
	if (psAllocSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		if (psAllocSyncPrimitiveBlockOUT->hSyncHandle)
		{
			PVRSRVReleaseHandle(psConnection->psHandleBase,
						(IMG_HANDLE) psAllocSyncPrimitiveBlockOUT->hSyncHandle,
						PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
		}

		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hSyncHandleInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hSyncHandleInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psSyncHandleInt)
		{
			PVRSRVFreeSyncPrimitiveBlockKM(psSyncHandleInt);
		}
	}


	return 0;
}

static IMG_INT
PVRSRVBridgeFreeSyncPrimitiveBlock(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK *psFreeSyncPrimitiveBlockIN,
					 PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK *psFreeSyncPrimitiveBlockOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK);





				{
					/* Look up the address from the handle */
					psFreeSyncPrimitiveBlockOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psFreeSyncPrimitiveBlockIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psFreeSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
					{
						goto FreeSyncPrimitiveBlock_exit;
					}

				}

	psFreeSyncPrimitiveBlockOUT->eError = FreeSyncPrimitiveBlockResManProxy(hSyncHandleInt2);
	/* Exit early if bridged call fails */
	if(psFreeSyncPrimitiveBlockOUT->eError != PVRSRV_OK)
	{
		goto FreeSyncPrimitiveBlock_exit;
	}

	psFreeSyncPrimitiveBlockOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psFreeSyncPrimitiveBlockIN->hSyncHandle,
					PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);


FreeSyncPrimitiveBlock_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimSet(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMSET *psSyncPrimSetIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMSET *psSyncPrimSetOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMSET);





				{
					/* Look up the address from the handle */
					psSyncPrimSetOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimSetIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimSetOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimSet_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimSetOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimSetOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimSet_exit;
					}
				}

	psSyncPrimSetOUT->eError =
		PVRSRVSyncPrimSetKM(
					psSyncHandleInt,
					psSyncPrimSetIN->ui32Index,
					psSyncPrimSetIN->ui32Value);



SyncPrimSet_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeServerSyncPrimSet(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SERVERSYNCPRIMSET *psServerSyncPrimSetIN,
					 PVRSRV_BRIDGE_OUT_SERVERSYNCPRIMSET *psServerSyncPrimSetOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SERVERSYNCPRIMSET);





				{
					/* Look up the address from the handle */
					psServerSyncPrimSetOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psServerSyncPrimSetIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psServerSyncPrimSetOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncPrimSet_exit;
					}

					/* Look up the data from the resman address */
					psServerSyncPrimSetOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psServerSyncPrimSetOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncPrimSet_exit;
					}
				}

	psServerSyncPrimSetOUT->eError =
		PVRSRVServerSyncPrimSetKM(
					psSyncHandleInt,
					psServerSyncPrimSetIN->ui32Value);



ServerSyncPrimSet_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncRecordRemoveByHandle(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_SYNCRECORDREMOVEBYHANDLE *psSyncRecordRemoveByHandleIN,
					  PVRSRV_BRIDGE_OUT_SYNCRECORDREMOVEBYHANDLE *psSyncRecordRemoveByHandleOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hRecordHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCRECORDREMOVEBYHANDLE);
				
	/* Look up the address from the handle */
	psSyncRecordRemoveByHandleOUT->eError =
		PVRSRVLookupHandle(psConnection->psHandleBase,
							(IMG_HANDLE *) &hRecordHandleInt2,
							psSyncRecordRemoveByHandleIN->hhRecord,
							PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE);
	if(psSyncRecordRemoveByHandleOUT->eError != PVRSRV_OK)
	{
		goto SyncRecordRemoveByHandle_exit;
	}

	psSyncRecordRemoveByHandleOUT->eError = ServerSyncFreeResManProxy(hRecordHandleInt2);
	/* Exit early if bridged call fails */
	if(psSyncRecordRemoveByHandleOUT->eError != PVRSRV_OK)
	{
		goto SyncRecordRemoveByHandle_exit;
	}

	psSyncRecordRemoveByHandleOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psSyncRecordRemoveByHandleIN->hhRecord,
					PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE);

	if ((psSyncRecordRemoveByHandleOUT->eError != PVRSRV_OK) && (psSyncRecordRemoveByHandleOUT->eError != PVRSRV_ERROR_RETRY))
	{
		PVR_ASSERT(0);
		goto SyncRecordRemoveByHandle_exit;
	}

SyncRecordRemoveByHandle_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncRecordAdd(IMG_UINT32 ui32BridgeID,
					  PVRSRV_BRIDGE_IN_SYNCRECORDADD *psSyncRecordAddIN,
					  PVRSRV_BRIDGE_OUT_SYNCRECORDADD *psSyncRecordAddOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_RECORD_HANDLE pshRecordInt = IMG_NULL;
	IMG_HANDLE hRecordHandleInt2 = IMG_NULL;
	SYNC_PRIMITIVE_BLOCK * pshServerSyncPrimBlockInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;
	IMG_CHAR *uiClassNameInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCRECORDADD);

	if (psSyncRecordAddIN->ui32ClassNameSize != 0)
	{
		uiClassNameInt = OSAllocMem(psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR));
		if (!uiClassNameInt)
		{
			psSyncRecordAddOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto SyncRecordAdd_exit;
		}
	}

	/* Copy the data over */
	if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncRecordAddIN->puiClassName, psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR))
		|| (OSCopyFromUser(NULL, uiClassNameInt, psSyncRecordAddIN->puiClassName,
		psSyncRecordAddIN->ui32ClassNameSize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
	{
		psSyncRecordAddOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto SyncRecordAdd_exit;
	}

	{
		/* Look up the address from the handle */
		psSyncRecordAddOUT->eError =
			PVRSRVLookupHandle(psConnection->psHandleBase,
								(IMG_HANDLE *) &hSyncHandleInt2,
								psSyncRecordAddIN->hhServerSyncPrimBlock,
								PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
		if(psSyncRecordAddOUT->eError != PVRSRV_OK)
		{
			goto SyncRecordAdd_exit;
		}

		/* Look up the data from the resman address */
		psSyncRecordAddOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &pshServerSyncPrimBlockInt);

		if(psSyncRecordAddOUT->eError != PVRSRV_OK)
		{
			goto SyncRecordAdd_exit;
		}
	}

	psSyncRecordAddOUT->eError =
		PVRSRVSyncRecordAddKM(
					&pshRecordInt,
					pshServerSyncPrimBlockInt,
					psSyncRecordAddIN->ui32ui32FwBlockAddr,
					psSyncRecordAddIN->ui32ui32SyncOffset,
					psSyncRecordAddIN->bbServerSync,
					psSyncRecordAddIN->ui32ClassNameSize,
					uiClassNameInt);

	/* Exit early if bridged call fails */
	if(psSyncRecordAddOUT->eError != PVRSRV_OK)
	{
		goto SyncRecordAdd_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hRecordHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SYNC_RECORD_HANDLE,
												pshRecordInt,
												(RESMAN_FREE_FN)&PVRSRVSyncRecordRemoveByHandleKM);
	if (hRecordHandleInt2 == IMG_NULL)
	{
		psSyncRecordAddOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncRecordAdd_exit;
	}

	psSyncRecordAddOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psSyncRecordAddOUT->hhRecord,
							(IMG_VOID *) hRecordHandleInt2,
							PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psSyncRecordAddOUT->eError != PVRSRV_OK)
	{
		goto SyncRecordAdd_exit;
	}

SyncRecordAdd_exit:
	if (psSyncRecordAddOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hRecordHandleInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hRecordHandleInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (pshRecordInt)
		{
			PVRSRVSyncRecordRemoveByHandleKM(pshRecordInt);
		}
	}

	if (uiClassNameInt)
		OSFreeMem(uiClassNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeServerSyncAlloc(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SERVERSYNCALLOC *psServerSyncAllocIN,
					 PVRSRV_BRIDGE_OUT_SERVERSYNCALLOC *psServerSyncAllocOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hDevNodeInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;
	IMG_CHAR *uiClassNameInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SERVERSYNCALLOC);




	if (psServerSyncAllocIN->ui32ClassNameSize != 0)
	{
		uiClassNameInt = OSAllocMem(psServerSyncAllocIN->ui32ClassNameSize * sizeof(IMG_CHAR));
		if (!uiClassNameInt)
		{
			psServerSyncAllocOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncAlloc_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psServerSyncAllocIN->puiClassName, psServerSyncAllocIN->ui32ClassNameSize * sizeof(IMG_CHAR))
				|| (OSCopyFromUser(NULL, uiClassNameInt, psServerSyncAllocIN->puiClassName,
				psServerSyncAllocIN->ui32ClassNameSize * sizeof(IMG_CHAR)) != PVRSRV_OK) )
			{
				psServerSyncAllocOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto ServerSyncAlloc_exit;
			}

				{
					/* Look up the address from the handle */
					psServerSyncAllocOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hDevNodeInt,
											psServerSyncAllocIN->hDevNode,
											PVRSRV_HANDLE_TYPE_DEV_NODE);
					if(psServerSyncAllocOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncAlloc_exit;
					}

				}

	psServerSyncAllocOUT->eError =
		PVRSRVServerSyncAllocKM(
					hDevNodeInt,
					&psSyncHandleInt,
					&psServerSyncAllocOUT->ui32SyncPrimVAddr,
					psServerSyncAllocIN->ui32ClassNameSize,
					uiClassNameInt);
	/* Exit early if bridged call fails */
	if(psServerSyncAllocOUT->eError != PVRSRV_OK)
	{
		goto ServerSyncAlloc_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hSyncHandleInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SERVER_SYNC_PRIMITIVE,
												psSyncHandleInt,
												(RESMAN_FREE_FN)&PVRSRVServerSyncFreeKM);
	if (hSyncHandleInt2 == IMG_NULL)
	{
		psServerSyncAllocOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto ServerSyncAlloc_exit;
	}
	psServerSyncAllocOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psServerSyncAllocOUT->hSyncHandle,
							(IMG_HANDLE) hSyncHandleInt2,
							PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psServerSyncAllocOUT->eError != PVRSRV_OK)
	{
		goto ServerSyncAlloc_exit;
	}


ServerSyncAlloc_exit:
	if (psServerSyncAllocOUT->eError != PVRSRV_OK)
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

	if (uiClassNameInt)
		OSFreeMem(uiClassNameInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeServerSyncFree(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SERVERSYNCFREE *psServerSyncFreeIN,
					 PVRSRV_BRIDGE_OUT_SERVERSYNCFREE *psServerSyncFreeOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SERVERSYNCFREE);





				{
					/* Look up the address from the handle */
					psServerSyncFreeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psServerSyncFreeIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psServerSyncFreeOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncFree_exit;
					}

				}

	psServerSyncFreeOUT->eError = ServerSyncFreeResManProxy(hSyncHandleInt2);
	/* Exit early if bridged call fails */
	if(psServerSyncFreeOUT->eError != PVRSRV_OK)
	{
		goto ServerSyncFree_exit;
	}

	psServerSyncFreeOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psServerSyncFreeIN->hSyncHandle,
					PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);


ServerSyncFree_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeServerSyncQueueHWOp(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SERVERSYNCQUEUEHWOP *psServerSyncQueueHWOpIN,
					 PVRSRV_BRIDGE_OUT_SERVERSYNCQUEUEHWOP *psServerSyncQueueHWOpOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SERVERSYNCQUEUEHWOP);





				{
					/* Look up the address from the handle */
					psServerSyncQueueHWOpOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psServerSyncQueueHWOpIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psServerSyncQueueHWOpOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncQueueHWOp_exit;
					}

					/* Look up the data from the resman address */
					psServerSyncQueueHWOpOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psServerSyncQueueHWOpOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncQueueHWOp_exit;
					}
				}

	psServerSyncQueueHWOpOUT->eError =
		PVRSRVServerSyncQueueHWOpKM(
					psSyncHandleInt,
					psServerSyncQueueHWOpIN->bbUpdate,
					&psServerSyncQueueHWOpOUT->ui32FenceValue,
					&psServerSyncQueueHWOpOUT->ui32UpdateValue);



ServerSyncQueueHWOp_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeServerSyncGetStatus(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SERVERSYNCGETSTATUS *psServerSyncGetStatusIN,
					 PVRSRV_BRIDGE_OUT_SERVERSYNCGETSTATUS *psServerSyncGetStatusOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_SYNC_PRIMITIVE * *psSyncHandleInt = IMG_NULL;
	IMG_HANDLE *hSyncHandleInt2 = IMG_NULL;
	IMG_UINT32 *pui32UIDInt = IMG_NULL;
	IMG_UINT32 *pui32FWAddrInt = IMG_NULL;
	IMG_UINT32 *pui32CurrentOpInt = IMG_NULL;
	IMG_UINT32 *pui32NextOpInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SERVERSYNCGETSTATUS);


	psServerSyncGetStatusOUT->pui32UID = psServerSyncGetStatusIN->pui32UID;
	psServerSyncGetStatusOUT->pui32FWAddr = psServerSyncGetStatusIN->pui32FWAddr;
	psServerSyncGetStatusOUT->pui32CurrentOp = psServerSyncGetStatusIN->pui32CurrentOp;
	psServerSyncGetStatusOUT->pui32NextOp = psServerSyncGetStatusIN->pui32NextOp;


	if (psServerSyncGetStatusIN->ui32SyncCount != 0)
	{
		psSyncHandleInt = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psSyncHandleInt)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
		hSyncHandleInt2 = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_HANDLE));
		if (!hSyncHandleInt2)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psServerSyncGetStatusIN->phSyncHandle, psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hSyncHandleInt2, psServerSyncGetStatusIN->phSyncHandle,
				psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto ServerSyncGetStatus_exit;
			}
	if (psServerSyncGetStatusIN->ui32SyncCount != 0)
	{
		pui32UIDInt = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32));
		if (!pui32UIDInt)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
	}

	if (psServerSyncGetStatusIN->ui32SyncCount != 0)
	{
		pui32FWAddrInt = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32));
		if (!pui32FWAddrInt)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
	}

	if (psServerSyncGetStatusIN->ui32SyncCount != 0)
	{
		pui32CurrentOpInt = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32));
		if (!pui32CurrentOpInt)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
	}

	if (psServerSyncGetStatusIN->ui32SyncCount != 0)
	{
		pui32NextOpInt = OSAllocMem(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32));
		if (!pui32NextOpInt)
		{
			psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto ServerSyncGetStatus_exit;
		}
	}


	{
		IMG_UINT32 i;

		for (i=0;i<psServerSyncGetStatusIN->ui32SyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psServerSyncGetStatusOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2[i],
											hSyncHandleInt2[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psServerSyncGetStatusOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncGetStatus_exit;
					}

					/* Look up the data from the resman address */
					psServerSyncGetStatusOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2[i], (IMG_VOID **) &psSyncHandleInt[i]);

					if(psServerSyncGetStatusOUT->eError != PVRSRV_OK)
					{
						goto ServerSyncGetStatus_exit;
					}
				}
		}
	}

	psServerSyncGetStatusOUT->eError =
		PVRSRVServerSyncGetStatusKM(
					psServerSyncGetStatusIN->ui32SyncCount,
					psSyncHandleInt,
					pui32UIDInt,
					pui32FWAddrInt,
					pui32CurrentOpInt,
					pui32NextOpInt);


	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psServerSyncGetStatusOUT->pui32UID, (psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psServerSyncGetStatusOUT->pui32UID, pui32UIDInt,
		(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto ServerSyncGetStatus_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psServerSyncGetStatusOUT->pui32FWAddr, (psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psServerSyncGetStatusOUT->pui32FWAddr, pui32FWAddrInt,
		(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto ServerSyncGetStatus_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psServerSyncGetStatusOUT->pui32CurrentOp, (psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psServerSyncGetStatusOUT->pui32CurrentOp, pui32CurrentOpInt,
		(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto ServerSyncGetStatus_exit;
	}

	if ( !OSAccessOK(PVR_VERIFY_WRITE, (IMG_VOID*) psServerSyncGetStatusOUT->pui32NextOp, (psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) 
		|| (OSCopyToUser(NULL, psServerSyncGetStatusOUT->pui32NextOp, pui32NextOpInt,
		(psServerSyncGetStatusIN->ui32SyncCount * sizeof(IMG_UINT32))) != PVRSRV_OK) )
	{
		psServerSyncGetStatusOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

		goto ServerSyncGetStatus_exit;
	}


ServerSyncGetStatus_exit:
	if (psSyncHandleInt)
		OSFreeMem(psSyncHandleInt);
	if (hSyncHandleInt2)
		OSFreeMem(hSyncHandleInt2);
	if (pui32UIDInt)
		OSFreeMem(pui32UIDInt);
	if (pui32FWAddrInt)
		OSFreeMem(pui32FWAddrInt);
	if (pui32CurrentOpInt)
		OSFreeMem(pui32CurrentOpInt);
	if (pui32NextOpInt)
		OSFreeMem(pui32NextOpInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpCreate(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPCREATE *psSyncPrimOpCreateIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPCREATE *psSyncPrimOpCreateOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * *psBlockListInt = IMG_NULL;
	IMG_HANDLE *hBlockListInt2 = IMG_NULL;
	IMG_UINT32 *ui32SyncBlockIndexInt = IMG_NULL;
	IMG_UINT32 *ui32IndexInt = IMG_NULL;
	SERVER_SYNC_PRIMITIVE * *psServerSyncInt = IMG_NULL;
	IMG_HANDLE *hServerSyncInt2 = IMG_NULL;
	SERVER_OP_COOKIE * psServerCookieInt = IMG_NULL;
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCREATE);




	if (psSyncPrimOpCreateIN->ui32SyncBlockCount != 0)
	{
		psBlockListInt = OSAllocMem(psSyncPrimOpCreateIN->ui32SyncBlockCount * sizeof(SYNC_PRIMITIVE_BLOCK *));
		if (!psBlockListInt)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
		hBlockListInt2 = OSAllocMem(psSyncPrimOpCreateIN->ui32SyncBlockCount * sizeof(IMG_HANDLE));
		if (!hBlockListInt2)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpCreateIN->phBlockList, psSyncPrimOpCreateIN->ui32SyncBlockCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hBlockListInt2, psSyncPrimOpCreateIN->phBlockList,
				psSyncPrimOpCreateIN->ui32SyncBlockCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpCreate_exit;
			}
	if (psSyncPrimOpCreateIN->ui32ClientSyncCount != 0)
	{
		ui32SyncBlockIndexInt = OSAllocMem(psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32));
		if (!ui32SyncBlockIndexInt)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpCreateIN->pui32SyncBlockIndex, psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32SyncBlockIndexInt, psSyncPrimOpCreateIN->pui32SyncBlockIndex,
				psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpCreate_exit;
			}
	if (psSyncPrimOpCreateIN->ui32ClientSyncCount != 0)
	{
		ui32IndexInt = OSAllocMem(psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32));
		if (!ui32IndexInt)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpCreateIN->pui32Index, psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32IndexInt, psSyncPrimOpCreateIN->pui32Index,
				psSyncPrimOpCreateIN->ui32ClientSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpCreate_exit;
			}
	if (psSyncPrimOpCreateIN->ui32ServerSyncCount != 0)
	{
		psServerSyncInt = OSAllocMem(psSyncPrimOpCreateIN->ui32ServerSyncCount * sizeof(SERVER_SYNC_PRIMITIVE *));
		if (!psServerSyncInt)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
		hServerSyncInt2 = OSAllocMem(psSyncPrimOpCreateIN->ui32ServerSyncCount * sizeof(IMG_HANDLE));
		if (!hServerSyncInt2)
		{
			psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpCreate_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpCreateIN->phServerSync, psSyncPrimOpCreateIN->ui32ServerSyncCount * sizeof(IMG_HANDLE))
				|| (OSCopyFromUser(NULL, hServerSyncInt2, psSyncPrimOpCreateIN->phServerSync,
				psSyncPrimOpCreateIN->ui32ServerSyncCount * sizeof(IMG_HANDLE)) != PVRSRV_OK) )
			{
				psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpCreate_exit;
			}

	{
		IMG_UINT32 i;

		for (i=0;i<psSyncPrimOpCreateIN->ui32SyncBlockCount;i++)
		{
				{
					/* Look up the address from the handle */
					psSyncPrimOpCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hBlockListInt2[i],
											hBlockListInt2[i],
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpCreate_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpCreateOUT->eError = ResManFindPrivateDataByPtr(hBlockListInt2[i], (IMG_VOID **) &psBlockListInt[i]);

					if(psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpCreate_exit;
					}
				}
		}
	}

	{
		IMG_UINT32 i;

		for (i=0;i<psSyncPrimOpCreateIN->ui32ServerSyncCount;i++)
		{
				{
					/* Look up the address from the handle */
					psSyncPrimOpCreateOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerSyncInt2[i],
											psSyncPrimOpCreateIN->phServerSync[i],
											PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE);
					if(psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpCreate_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpCreateOUT->eError = ResManFindPrivateDataByPtr(hServerSyncInt2[i], (IMG_VOID **) &psServerSyncInt[i]);

					if(psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpCreate_exit;
					}
				}
		}
	}

	psSyncPrimOpCreateOUT->eError =
		PVRSRVSyncPrimOpCreateKM(
					psSyncPrimOpCreateIN->ui32SyncBlockCount,
					psBlockListInt,
					psSyncPrimOpCreateIN->ui32ClientSyncCount,
					ui32SyncBlockIndexInt,
					ui32IndexInt,
					psSyncPrimOpCreateIN->ui32ServerSyncCount,
					psServerSyncInt,
					&psServerCookieInt);
	/* Exit early if bridged call fails */
	if(psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimOpCreate_exit;
	}

	/* Create a resman item and overwrite the handle with it */
	hServerCookieInt2 = ResManRegisterRes(psConnection->hResManContext,
												RESMAN_TYPE_SERVER_OP_COOKIE,
												psServerCookieInt,
												(RESMAN_FREE_FN)&PVRSRVSyncPrimOpDestroyKM);
	if (hServerCookieInt2 == IMG_NULL)
	{
		psSyncPrimOpCreateOUT->eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
		goto SyncPrimOpCreate_exit;
	}
	psSyncPrimOpCreateOUT->eError = PVRSRVAllocHandle(psConnection->psHandleBase,
							&psSyncPrimOpCreateOUT->hServerCookie,
							(IMG_HANDLE) hServerCookieInt2,
							PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE,
							PVRSRV_HANDLE_ALLOC_FLAG_NONE
							);
	if (psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimOpCreate_exit;
	}


SyncPrimOpCreate_exit:
	if (psSyncPrimOpCreateOUT->eError != PVRSRV_OK)
	{
		/* If we have a valid resman item we should undo the bridge function by freeing the resman item */
		if (hServerCookieInt2)
		{
			PVRSRV_ERROR eError = ResManFreeResByPtr(hServerCookieInt2);

			/* Freeing a resource should never fail... */
			PVR_ASSERT((eError == PVRSRV_OK) || (eError == PVRSRV_ERROR_RETRY));
		}
		else if (psServerCookieInt)
		{
			PVRSRVSyncPrimOpDestroyKM(psServerCookieInt);
		}
	}

	if (psBlockListInt)
		OSFreeMem(psBlockListInt);
	if (hBlockListInt2)
		OSFreeMem(hBlockListInt2);
	if (ui32SyncBlockIndexInt)
		OSFreeMem(ui32SyncBlockIndexInt);
	if (ui32IndexInt)
		OSFreeMem(ui32IndexInt);
	if (psServerSyncInt)
		OSFreeMem(psServerSyncInt);
	if (hServerSyncInt2)
		OSFreeMem(hServerSyncInt2);

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpTake(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPTAKE *psSyncPrimOpTakeIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPTAKE *psSyncPrimOpTakeOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_OP_COOKIE * psServerCookieInt = IMG_NULL;
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;
	IMG_UINT32 *ui32FlagsInt = IMG_NULL;
	IMG_UINT32 *ui32FenceValueInt = IMG_NULL;
	IMG_UINT32 *ui32UpdateValueInt = IMG_NULL;
	IMG_UINT32 *ui32ServerFlagsInt = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPTAKE);




	if (psSyncPrimOpTakeIN->ui32ClientSyncCount != 0)
	{
		ui32FlagsInt = OSAllocMem(psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32));
		if (!ui32FlagsInt)
		{
			psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpTake_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpTakeIN->pui32Flags, psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32FlagsInt, psSyncPrimOpTakeIN->pui32Flags,
				psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpTake_exit;
			}
	if (psSyncPrimOpTakeIN->ui32ClientSyncCount != 0)
	{
		ui32FenceValueInt = OSAllocMem(psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32));
		if (!ui32FenceValueInt)
		{
			psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpTake_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpTakeIN->pui32FenceValue, psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32FenceValueInt, psSyncPrimOpTakeIN->pui32FenceValue,
				psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpTake_exit;
			}
	if (psSyncPrimOpTakeIN->ui32ClientSyncCount != 0)
	{
		ui32UpdateValueInt = OSAllocMem(psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32));
		if (!ui32UpdateValueInt)
		{
			psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpTake_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpTakeIN->pui32UpdateValue, psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32UpdateValueInt, psSyncPrimOpTakeIN->pui32UpdateValue,
				psSyncPrimOpTakeIN->ui32ClientSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpTake_exit;
			}
	if (psSyncPrimOpTakeIN->ui32ServerSyncCount != 0)
	{
		ui32ServerFlagsInt = OSAllocMem(psSyncPrimOpTakeIN->ui32ServerSyncCount * sizeof(IMG_UINT32));
		if (!ui32ServerFlagsInt)
		{
			psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_OUT_OF_MEMORY;
	
			goto SyncPrimOpTake_exit;
		}
	}

			/* Copy the data over */
			if ( !OSAccessOK(PVR_VERIFY_READ, (IMG_VOID*) psSyncPrimOpTakeIN->pui32ServerFlags, psSyncPrimOpTakeIN->ui32ServerSyncCount * sizeof(IMG_UINT32))
				|| (OSCopyFromUser(NULL, ui32ServerFlagsInt, psSyncPrimOpTakeIN->pui32ServerFlags,
				psSyncPrimOpTakeIN->ui32ServerSyncCount * sizeof(IMG_UINT32)) != PVRSRV_OK) )
			{
				psSyncPrimOpTakeOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;

				goto SyncPrimOpTake_exit;
			}

				{
					/* Look up the address from the handle */
					psSyncPrimOpTakeOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerCookieInt2,
											psSyncPrimOpTakeIN->hServerCookie,
											PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);
					if(psSyncPrimOpTakeOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpTake_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpTakeOUT->eError = ResManFindPrivateDataByPtr(hServerCookieInt2, (IMG_VOID **) &psServerCookieInt);

					if(psSyncPrimOpTakeOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpTake_exit;
					}
				}

	psSyncPrimOpTakeOUT->eError =
		PVRSRVSyncPrimOpTakeKM(
					psServerCookieInt,
					psSyncPrimOpTakeIN->ui32ClientSyncCount,
					ui32FlagsInt,
					ui32FenceValueInt,
					ui32UpdateValueInt,
					psSyncPrimOpTakeIN->ui32ServerSyncCount,
					ui32ServerFlagsInt);



SyncPrimOpTake_exit:
	if (ui32FlagsInt)
		OSFreeMem(ui32FlagsInt);
	if (ui32FenceValueInt)
		OSFreeMem(ui32FenceValueInt);
	if (ui32UpdateValueInt)
		OSFreeMem(ui32UpdateValueInt);
	if (ui32ServerFlagsInt)
		OSFreeMem(ui32ServerFlagsInt);

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpReady(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPREADY *psSyncPrimOpReadyIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPREADY *psSyncPrimOpReadyOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_OP_COOKIE * psServerCookieInt = IMG_NULL;
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPREADY);





				{
					/* Look up the address from the handle */
					psSyncPrimOpReadyOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerCookieInt2,
											psSyncPrimOpReadyIN->hServerCookie,
											PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);
					if(psSyncPrimOpReadyOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpReady_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpReadyOUT->eError = ResManFindPrivateDataByPtr(hServerCookieInt2, (IMG_VOID **) &psServerCookieInt);

					if(psSyncPrimOpReadyOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpReady_exit;
					}
				}

	psSyncPrimOpReadyOUT->eError =
		PVRSRVSyncPrimOpReadyKM(
					psServerCookieInt,
					&psSyncPrimOpReadyOUT->bReady);



SyncPrimOpReady_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpComplete(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPCOMPLETE *psSyncPrimOpCompleteIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPCOMPLETE *psSyncPrimOpCompleteOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_OP_COOKIE * psServerCookieInt = IMG_NULL;
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCOMPLETE);





				{
					/* Look up the address from the handle */
					psSyncPrimOpCompleteOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerCookieInt2,
											psSyncPrimOpCompleteIN->hServerCookie,
											PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);
					if(psSyncPrimOpCompleteOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpComplete_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpCompleteOUT->eError = ResManFindPrivateDataByPtr(hServerCookieInt2, (IMG_VOID **) &psServerCookieInt);

					if(psSyncPrimOpCompleteOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpComplete_exit;
					}
				}

	psSyncPrimOpCompleteOUT->eError =
		PVRSRVSyncPrimOpCompleteKM(
					psServerCookieInt);



SyncPrimOpComplete_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpDestroy(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPDESTROY *psSyncPrimOpDestroyIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPDESTROY *psSyncPrimOpDestroyOUT,
					 CONNECTION_DATA *psConnection)
{
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPDESTROY);





				{
					/* Look up the address from the handle */
					psSyncPrimOpDestroyOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerCookieInt2,
											psSyncPrimOpDestroyIN->hServerCookie,
											PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);
					if(psSyncPrimOpDestroyOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpDestroy_exit;
					}

				}

	psSyncPrimOpDestroyOUT->eError = SyncPrimOpDestroyResManProxy(hServerCookieInt2);
	/* Exit early if bridged call fails */
	if(psSyncPrimOpDestroyOUT->eError != PVRSRV_OK)
	{
		goto SyncPrimOpDestroy_exit;
	}

	psSyncPrimOpDestroyOUT->eError =
		PVRSRVReleaseHandle(psConnection->psHandleBase,
					(IMG_HANDLE) psSyncPrimOpDestroyIN->hServerCookie,
					PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);


SyncPrimOpDestroy_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimPDump(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP *psSyncPrimPDumpIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP *psSyncPrimPDumpOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP);





				{
					/* Look up the address from the handle */
					psSyncPrimPDumpOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimPDumpIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimPDumpOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDump_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimPDumpOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimPDumpOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDump_exit;
					}
				}

	psSyncPrimPDumpOUT->eError =
		PVRSRVSyncPrimPDumpKM(
					psSyncHandleInt,
					psSyncPrimPDumpIN->ui32Offset);



SyncPrimPDump_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimPDumpValue(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE *psSyncPrimPDumpValueIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE *psSyncPrimPDumpValueOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE);





				{
					/* Look up the address from the handle */
					psSyncPrimPDumpValueOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimPDumpValueIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimPDumpValueOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpValue_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimPDumpValueOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimPDumpValueOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpValue_exit;
					}
				}

	psSyncPrimPDumpValueOUT->eError =
		PVRSRVSyncPrimPDumpValueKM(
					psSyncHandleInt,
					psSyncPrimPDumpValueIN->ui32Offset,
					psSyncPrimPDumpValueIN->ui32Value);



SyncPrimPDumpValue_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimPDumpPol(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL *psSyncPrimPDumpPolIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL *psSyncPrimPDumpPolOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL);





				{
					/* Look up the address from the handle */
					psSyncPrimPDumpPolOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimPDumpPolIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimPDumpPolOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpPol_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimPDumpPolOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimPDumpPolOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpPol_exit;
					}
				}

	psSyncPrimPDumpPolOUT->eError =
		PVRSRVSyncPrimPDumpPolKM(
					psSyncHandleInt,
					psSyncPrimPDumpPolIN->ui32Offset,
					psSyncPrimPDumpPolIN->ui32Value,
					psSyncPrimPDumpPolIN->ui32Mask,
					psSyncPrimPDumpPolIN->eOperator,
					psSyncPrimPDumpPolIN->uiPDumpFlags);



SyncPrimPDumpPol_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimOpPDumpPol(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMOPPDUMPPOL *psSyncPrimOpPDumpPolIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMOPPDUMPPOL *psSyncPrimOpPDumpPolOUT,
					 CONNECTION_DATA *psConnection)
{
	SERVER_OP_COOKIE * psServerCookieInt = IMG_NULL;
	IMG_HANDLE hServerCookieInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMOPPDUMPPOL);





				{
					/* Look up the address from the handle */
					psSyncPrimOpPDumpPolOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hServerCookieInt2,
											psSyncPrimOpPDumpPolIN->hServerCookie,
											PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE);
					if(psSyncPrimOpPDumpPolOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpPDumpPol_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimOpPDumpPolOUT->eError = ResManFindPrivateDataByPtr(hServerCookieInt2, (IMG_VOID **) &psServerCookieInt);

					if(psSyncPrimOpPDumpPolOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimOpPDumpPol_exit;
					}
				}

	psSyncPrimOpPDumpPolOUT->eError =
		PVRSRVSyncPrimOpPDumpPolKM(
					psServerCookieInt,
					psSyncPrimOpPDumpPolIN->eOperator,
					psSyncPrimOpPDumpPolIN->uiPDumpFlags);



SyncPrimOpPDumpPol_exit:

	return 0;
}

static IMG_INT
PVRSRVBridgeSyncPrimPDumpCBP(IMG_UINT32 ui32BridgeID,
					 PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP *psSyncPrimPDumpCBPIN,
					 PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP *psSyncPrimPDumpCBPOUT,
					 CONNECTION_DATA *psConnection)
{
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt = IMG_NULL;
	IMG_HANDLE hSyncHandleInt2 = IMG_NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP);





				{
					/* Look up the address from the handle */
					psSyncPrimPDumpCBPOUT->eError =
						PVRSRVLookupHandle(psConnection->psHandleBase,
											(IMG_HANDLE *) &hSyncHandleInt2,
											psSyncPrimPDumpCBPIN->hSyncHandle,
											PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK);
					if(psSyncPrimPDumpCBPOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpCBP_exit;
					}

					/* Look up the data from the resman address */
					psSyncPrimPDumpCBPOUT->eError = ResManFindPrivateDataByPtr(hSyncHandleInt2, (IMG_VOID **) &psSyncHandleInt);

					if(psSyncPrimPDumpCBPOUT->eError != PVRSRV_OK)
					{
						goto SyncPrimPDumpCBP_exit;
					}
				}

	psSyncPrimPDumpCBPOUT->eError =
		PVRSRVSyncPrimPDumpCBPKM(
					psSyncHandleInt,
					psSyncPrimPDumpCBPIN->ui32Offset,
					psSyncPrimPDumpCBPIN->uiWriteOffset,
					psSyncPrimPDumpCBPIN->uiPacketSize,
					psSyncPrimPDumpCBPIN->uiBufferSize);



SyncPrimPDumpCBP_exit:

	return 0;
}



/* *************************************************************************** 
 * Server bridge dispatch related glue 
 */
 
PVRSRV_ERROR RegisterSYNCFunctions(IMG_VOID);
IMG_VOID UnregisterSYNCFunctions(IMG_VOID);

/*
 * Register all SYNC functions with services
 */
PVRSRV_ERROR RegisterSYNCFunctions(IMG_VOID)
{
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK, PVRSRVBridgeAllocSyncPrimitiveBlock);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK, PVRSRVBridgeFreeSyncPrimitiveBlock);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMSET, PVRSRVBridgeSyncPrimSet);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SERVERSYNCPRIMSET, PVRSRVBridgeServerSyncPrimSet);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCRECORDREMOVEBYHANDLE, PVRSRVBridgeSyncRecordRemoveByHandle);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCRECORDADD, PVRSRVBridgeSyncRecordAdd);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SERVERSYNCALLOC, PVRSRVBridgeServerSyncAlloc);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SERVERSYNCFREE, PVRSRVBridgeServerSyncFree);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SERVERSYNCQUEUEHWOP, PVRSRVBridgeServerSyncQueueHWOp);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SERVERSYNCGETSTATUS, PVRSRVBridgeServerSyncGetStatus);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCREATE, PVRSRVBridgeSyncPrimOpCreate);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPTAKE, PVRSRVBridgeSyncPrimOpTake);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPREADY, PVRSRVBridgeSyncPrimOpReady);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCOMPLETE, PVRSRVBridgeSyncPrimOpComplete);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPDESTROY, PVRSRVBridgeSyncPrimOpDestroy);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP, PVRSRVBridgeSyncPrimPDump);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE, PVRSRVBridgeSyncPrimPDumpValue);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL, PVRSRVBridgeSyncPrimPDumpPol);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMOPPDUMPPOL, PVRSRVBridgeSyncPrimOpPDumpPol);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP, PVRSRVBridgeSyncPrimPDumpCBP);

	return PVRSRV_OK;
}

/*
 * Unregister all sync functions with services
 */
IMG_VOID UnregisterSYNCFunctions(IMG_VOID)
{
}
