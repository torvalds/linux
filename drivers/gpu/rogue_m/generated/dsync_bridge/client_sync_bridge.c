/*************************************************************************/ /*!
@Title          Direct client bridge for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include "client_sync_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "pdump.h"
#include "pdumpdefs.h"
#include "devicemem_typedefs.h"

#include "sync_server.h"
#include "pdump.h"


IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeAllocSyncPrimitiveBlock(IMG_HANDLE hBridge,
								     IMG_HANDLE hDevNode,
								     IMG_HANDLE *phSyncHandle,
								     IMG_UINT32 *pui32SyncPrimVAddr,
								     IMG_UINT32 *pui32SyncPrimBlockSize,
								     DEVMEM_SERVER_EXPORTCOOKIE *phExportCookie)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hDevNodeInt;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	DEVMEM_EXPORTCOOKIE * psExportCookieInt;

	hDevNodeInt = (IMG_HANDLE) hDevNode;

	eError =
		PVRSRVAllocSyncPrimitiveBlockKM(hBridge
		,
					hDevNodeInt,
					&psSyncHandleInt,
					pui32SyncPrimVAddr,
					pui32SyncPrimBlockSize,
					&psExportCookieInt);

	*phSyncHandle = psSyncHandleInt;
	*phExportCookie = psExportCookieInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeFreeSyncPrimitiveBlock(IMG_HANDLE hBridge,
								    IMG_HANDLE hSyncHandle)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVFreeSyncPrimitiveBlockKM(
					psSyncHandleInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimSet(IMG_HANDLE hBridge,
							 IMG_HANDLE hSyncHandle,
							 IMG_UINT32 ui32Index,
							 IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVSyncPrimSetKM(
					psSyncHandleInt,
					ui32Index,
					ui32Value);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeServerSyncPrimSet(IMG_HANDLE hBridge,
							       IMG_HANDLE hSyncHandle,
							       IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SERVER_SYNC_PRIMITIVE *) hSyncHandle;

	eError =
		PVRSRVServerSyncPrimSetKM(
					psSyncHandleInt,
					ui32Value);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncRecordRemoveByHandle(IMG_HANDLE hBridge,
								      IMG_HANDLE hhRecord)
{
	PVRSRV_ERROR eError;
	SYNC_RECORD_HANDLE pshRecordInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	pshRecordInt = (SYNC_RECORD_HANDLE) hhRecord;

	eError =
		PVRSRVSyncRecordRemoveByHandleKM(
					pshRecordInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncRecordAdd(IMG_HANDLE hBridge,
							   IMG_HANDLE *phhRecord,
							   IMG_HANDLE hhServerSyncPrimBlock,
							   IMG_UINT32 ui32ui32FwBlockAddr,
							   IMG_UINT32 ui32ui32SyncOffset,
							   IMG_BOOL bbServerSync,
							   IMG_UINT32 ui32ClassNameSize,
							   const IMG_CHAR *puiClassName)
{
	PVRSRV_ERROR eError;
	SYNC_RECORD_HANDLE pshRecordInt;
	SYNC_PRIMITIVE_BLOCK * pshServerSyncPrimBlockInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	pshServerSyncPrimBlockInt = (SYNC_PRIMITIVE_BLOCK *) hhServerSyncPrimBlock;

	eError =
		PVRSRVSyncRecordAddKM(
					&pshRecordInt,
					pshServerSyncPrimBlockInt,
					ui32ui32FwBlockAddr,
					ui32ui32SyncOffset,
					bbServerSync,
					ui32ClassNameSize,
					puiClassName);

	*phhRecord = pshRecordInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeServerSyncAlloc(IMG_HANDLE hBridge,
							     IMG_HANDLE hDevNode,
							     IMG_HANDLE *phSyncHandle,
							     IMG_UINT32 *pui32SyncPrimVAddr,
							     IMG_UINT32 ui32ClassNameSize,
							     const IMG_CHAR *puiClassName)
{
	PVRSRV_ERROR eError;
	IMG_HANDLE hDevNodeInt;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	hDevNodeInt = (IMG_HANDLE) hDevNode;

	eError =
		PVRSRVServerSyncAllocKM(
					hDevNodeInt,
					&psSyncHandleInt,
					pui32SyncPrimVAddr,
					ui32ClassNameSize,
					puiClassName);

	*phSyncHandle = psSyncHandleInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeServerSyncFree(IMG_HANDLE hBridge,
							    IMG_HANDLE hSyncHandle)
{
	PVRSRV_ERROR eError;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SERVER_SYNC_PRIMITIVE *) hSyncHandle;

	eError =
		PVRSRVServerSyncFreeKM(
					psSyncHandleInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeServerSyncQueueHWOp(IMG_HANDLE hBridge,
								 IMG_HANDLE hSyncHandle,
								 IMG_BOOL bbUpdate,
								 IMG_UINT32 *pui32FenceValue,
								 IMG_UINT32 *pui32UpdateValue)
{
	PVRSRV_ERROR eError;
	SERVER_SYNC_PRIMITIVE * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SERVER_SYNC_PRIMITIVE *) hSyncHandle;

	eError =
		PVRSRVServerSyncQueueHWOpKM(
					psSyncHandleInt,
					bbUpdate,
					pui32FenceValue,
					pui32UpdateValue);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeServerSyncGetStatus(IMG_HANDLE hBridge,
								 IMG_UINT32 ui32SyncCount,
								 IMG_HANDLE *phSyncHandle,
								 IMG_UINT32 *pui32UID,
								 IMG_UINT32 *pui32FWAddr,
								 IMG_UINT32 *pui32CurrentOp,
								 IMG_UINT32 *pui32NextOp)
{
	PVRSRV_ERROR eError;
	SERVER_SYNC_PRIMITIVE * *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SERVER_SYNC_PRIMITIVE **) phSyncHandle;

	eError =
		PVRSRVServerSyncGetStatusKM(
					ui32SyncCount,
					psSyncHandleInt,
					pui32UID,
					pui32FWAddr,
					pui32CurrentOp,
					pui32NextOp);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpCreate(IMG_HANDLE hBridge,
							      IMG_UINT32 ui32SyncBlockCount,
							      IMG_HANDLE *phBlockList,
							      IMG_UINT32 ui32ClientSyncCount,
							      IMG_UINT32 *pui32SyncBlockIndex,
							      IMG_UINT32 *pui32Index,
							      IMG_UINT32 ui32ServerSyncCount,
							      IMG_HANDLE *phServerSync,
							      IMG_HANDLE *phServerCookie)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * *psBlockListInt;
	SERVER_SYNC_PRIMITIVE * *psServerSyncInt;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psBlockListInt = (SYNC_PRIMITIVE_BLOCK **) phBlockList;
	psServerSyncInt = (SERVER_SYNC_PRIMITIVE **) phServerSync;

	eError =
		PVRSRVSyncPrimOpCreateKM(
					ui32SyncBlockCount,
					psBlockListInt,
					ui32ClientSyncCount,
					pui32SyncBlockIndex,
					pui32Index,
					ui32ServerSyncCount,
					psServerSyncInt,
					&psServerCookieInt);

	*phServerCookie = psServerCookieInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpTake(IMG_HANDLE hBridge,
							    IMG_HANDLE hServerCookie,
							    IMG_UINT32 ui32ClientSyncCount,
							    IMG_UINT32 *pui32Flags,
							    IMG_UINT32 *pui32FenceValue,
							    IMG_UINT32 *pui32UpdateValue,
							    IMG_UINT32 ui32ServerSyncCount,
							    IMG_UINT32 *pui32ServerFlags)
{
	PVRSRV_ERROR eError;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psServerCookieInt = (SERVER_OP_COOKIE *) hServerCookie;

	eError =
		PVRSRVSyncPrimOpTakeKM(
					psServerCookieInt,
					ui32ClientSyncCount,
					pui32Flags,
					pui32FenceValue,
					pui32UpdateValue,
					ui32ServerSyncCount,
					pui32ServerFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpReady(IMG_HANDLE hBridge,
							     IMG_HANDLE hServerCookie,
							     IMG_BOOL *pbReady)
{
	PVRSRV_ERROR eError;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psServerCookieInt = (SERVER_OP_COOKIE *) hServerCookie;

	eError =
		PVRSRVSyncPrimOpReadyKM(
					psServerCookieInt,
					pbReady);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpComplete(IMG_HANDLE hBridge,
								IMG_HANDLE hServerCookie)
{
	PVRSRV_ERROR eError;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psServerCookieInt = (SERVER_OP_COOKIE *) hServerCookie;

	eError =
		PVRSRVSyncPrimOpCompleteKM(
					psServerCookieInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpDestroy(IMG_HANDLE hBridge,
							       IMG_HANDLE hServerCookie)
{
	PVRSRV_ERROR eError;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psServerCookieInt = (SERVER_OP_COOKIE *) hServerCookie;

	eError =
		PVRSRVSyncPrimOpDestroyKM(
					psServerCookieInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimPDump(IMG_HANDLE hBridge,
							   IMG_HANDLE hSyncHandle,
							   IMG_UINT32 ui32Offset)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVSyncPrimPDumpKM(
					psSyncHandleInt,
					ui32Offset);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimPDumpValue(IMG_HANDLE hBridge,
								IMG_HANDLE hSyncHandle,
								IMG_UINT32 ui32Offset,
								IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVSyncPrimPDumpValueKM(
					psSyncHandleInt,
					ui32Offset,
					ui32Value);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimPDumpPol(IMG_HANDLE hBridge,
							      IMG_HANDLE hSyncHandle,
							      IMG_UINT32 ui32Offset,
							      IMG_UINT32 ui32Value,
							      IMG_UINT32 ui32Mask,
							      PDUMP_POLL_OPERATOR eOperator,
							      PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVSyncPrimPDumpPolKM(
					psSyncHandleInt,
					ui32Offset,
					ui32Value,
					ui32Mask,
					eOperator,
					uiPDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimOpPDumpPol(IMG_HANDLE hBridge,
								IMG_HANDLE hServerCookie,
								PDUMP_POLL_OPERATOR eOperator,
								PDUMP_FLAGS_T uiPDumpFlags)
{
	PVRSRV_ERROR eError;
	SERVER_OP_COOKIE * psServerCookieInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psServerCookieInt = (SERVER_OP_COOKIE *) hServerCookie;

	eError =
		PVRSRVSyncPrimOpPDumpPolKM(
					psServerCookieInt,
					eOperator,
					uiPDumpFlags);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR IMG_CALLCONV BridgeSyncPrimPDumpCBP(IMG_HANDLE hBridge,
							      IMG_HANDLE hSyncHandle,
							      IMG_UINT32 ui32Offset,
							      IMG_DEVMEM_OFFSET_T uiWriteOffset,
							      IMG_DEVMEM_SIZE_T uiPacketSize,
							      IMG_DEVMEM_SIZE_T uiBufferSize)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK * psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
		PVRSRVSyncPrimPDumpCBPKM(
					psSyncHandleInt,
					ui32Offset,
					uiWriteOffset,
					uiPacketSize,
					uiBufferSize);

	return eError;
}

