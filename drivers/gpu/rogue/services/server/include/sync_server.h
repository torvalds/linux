/**************************************************************************/ /*!
@File
@Title          Server side synchronisation interface
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Describes the server side synchronisation functions
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
*/ /***************************************************************************/

#include "img_types.h"
#include "device.h"
#include "devicemem.h"
#include "pdump.h"
#include "pvrsrv_error.h"
#include "connection_server.h"

#ifndef _SYNC_SERVER_H_
#define _SYNC_SERVER_H_

typedef struct _SERVER_OP_COOKIE_ SERVER_OP_COOKIE;
typedef struct _SERVER_SYNC_PRIMITIVE_ SERVER_SYNC_PRIMITIVE;
typedef struct _SYNC_PRIMITIVE_BLOCK_ SYNC_PRIMITIVE_BLOCK;
typedef struct _SERVER_SYNC_EXPORT_ SERVER_SYNC_EXPORT;
typedef struct _SYNC_CONNECTION_DATA_ SYNC_CONNECTION_DATA;
typedef struct SYNC_RECORD* SYNC_RECORD_HANDLE;

PVRSRV_ERROR
PVRSRVAllocSyncPrimitiveBlockKM(CONNECTION_DATA *psConnection,
								PVRSRV_DEVICE_NODE *psDevNode,
								SYNC_PRIMITIVE_BLOCK **ppsSyncBlk,
								IMG_UINT32 *puiSyncPrimVAddr,
								IMG_UINT32 *puiSyncPrimBlockSize,
								DEVMEM_EXPORTCOOKIE **psExportCookie);

PVRSRV_ERROR
PVRSRVExportSyncPrimitiveBlockKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk,
								 DEVMEM_EXPORTCOOKIE **psExportCookie);

PVRSRV_ERROR
PVRSRVUnexportSyncPrimitiveBlockKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk);

PVRSRV_ERROR
PVRSRVFreeSyncPrimitiveBlockKM(SYNC_PRIMITIVE_BLOCK *ppsSyncBlk);

PVRSRV_ERROR
PVRSRVSyncPrimSetKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Index,
					IMG_UINT32 ui32Value);

PVRSRV_ERROR
PVRSRVServerSyncPrimSetKM(SERVER_SYNC_PRIMITIVE *psServerSync, IMG_UINT32 ui32Value);

#if defined(SUPPORT_INSECURE_EXPORT)
PVRSRV_ERROR
PVRSRVSyncPrimServerExportKM(SERVER_SYNC_PRIMITIVE *psSync,
							SERVER_SYNC_EXPORT **ppsExport);

PVRSRV_ERROR
PVRSRVSyncPrimServerUnexportKM(SERVER_SYNC_EXPORT *psExport);

PVRSRV_ERROR
PVRSRVSyncPrimServerImportKM(SERVER_SYNC_EXPORT *psExport,
							 SERVER_SYNC_PRIMITIVE **ppsSync,
							 IMG_UINT32 *pui32SyncPrimVAddr);
#endif

#if defined(SUPPORT_SECURE_EXPORT)
PVRSRV_ERROR
PVRSRVSyncPrimServerSecureExportKM(CONNECTION_DATA *psConnection,
								   SERVER_SYNC_PRIMITIVE *psSync,
								   IMG_SECURE_TYPE *phSecure,
								   SERVER_SYNC_EXPORT **ppsExport,
								   CONNECTION_DATA **ppsSecureConnection);

PVRSRV_ERROR
PVRSRVSyncPrimServerSecureUnexportKM(SERVER_SYNC_EXPORT *psExport);

PVRSRV_ERROR
PVRSRVSyncPrimServerSecureImportKM(IMG_SECURE_TYPE hSecure,
								   SERVER_SYNC_PRIMITIVE **ppsSync,
								   IMG_UINT32 *pui32SyncPrimVAddr);
#endif

IMG_UINT32 PVRSRVServerSyncRequesterRegisterKM(IMG_UINT32 *pui32SyncRequesterID);
IMG_VOID PVRSRVServerSyncRequesterUnregisterKM(IMG_UINT32 ui32SyncRequesterID);

PVRSRV_ERROR
PVRSRVSyncRecordAddKM(
			SYNC_RECORD_HANDLE * phRecord,
			SYNC_PRIMITIVE_BLOCK * hServerSyncPrimBlock,
			IMG_UINT32 ui32FwBlockAddr,
			IMG_UINT32 ui32SyncOffset,
			IMG_BOOL bServerSync,
			IMG_UINT32 ui32ClassNameSize,
			const IMG_CHAR *pszClassName);
PVRSRV_ERROR
PVRSRVSyncRecordRemoveByHandleKM(
			SYNC_RECORD_HANDLE hRecord);

PVRSRV_ERROR
PVRSRVServerSyncAllocKM(PVRSRV_DEVICE_NODE *psDevNode,
						SERVER_SYNC_PRIMITIVE **ppsSync,
						IMG_UINT32 *pui32SyncPrimVAddr,
						IMG_UINT32 ui32ClassNameSize,
						const IMG_CHAR *szClassName);
PVRSRV_ERROR
PVRSRVServerSyncFreeKM(SERVER_SYNC_PRIMITIVE *psSync);

PVRSRV_ERROR
PVRSRVServerSyncGetStatusKM(IMG_UINT32 ui32SyncCount,
							SERVER_SYNC_PRIMITIVE **papsSyncs,
							IMG_UINT32 *pui32UID,
							IMG_UINT32 *pui32FWAddr,
							IMG_UINT32 *pui32CurrentOp,
							IMG_UINT32 *pui32NextOp);

PVRSRV_ERROR
PVRSRVServerSyncQueueSWOpKM(SERVER_SYNC_PRIMITIVE *psSync,
						  IMG_UINT32 *pui32FenceValue,
						  IMG_UINT32 *pui32UpdateValue,
						  IMG_UINT32 ui32SyncRequesterID,
						  IMG_BOOL bUpdate,
						  IMG_BOOL *pbFenceRequired);

PVRSRV_ERROR
PVRSRVServerSyncQueueHWOpKM(SERVER_SYNC_PRIMITIVE *psSync,
							   IMG_BOOL bUpdate,
						       IMG_UINT32 *pui32FenceValue,
						       IMG_UINT32 *pui32UpdateValue);

IMG_BOOL
ServerSyncFenceIsMet(SERVER_SYNC_PRIMITIVE *psSync,
					 IMG_UINT32 ui32FenceValue);

IMG_VOID
ServerSyncCompleteOp(SERVER_SYNC_PRIMITIVE *psSync,
					 IMG_BOOL bDoUpdate,
					 IMG_UINT32 ui32UpdateValue);


PVRSRV_ERROR
PVRSRVSyncPrimOpCreateKM(IMG_UINT32 ui32SyncBlockCount,
						 SYNC_PRIMITIVE_BLOCK **papsSyncPrimBlock,
						 IMG_UINT32 ui32ClientSyncCount,
						 IMG_UINT32 *paui32SyncBlockIndex,
						 IMG_UINT32 *paui32Index,
						 IMG_UINT32 ui32ServerSyncCount,
						 SERVER_SYNC_PRIMITIVE **papsServerSync,
						 SERVER_OP_COOKIE **ppsServerCookie);

PVRSRV_ERROR
PVRSRVSyncPrimOpTakeKM(SERVER_OP_COOKIE *psServerCookie,
					       IMG_UINT32 ui32ClientSyncCount,
					       IMG_UINT32 *paui32Flags,
					       IMG_UINT32 *paui32FenceValue,
					       IMG_UINT32 *paui32UpdateValue,
					       IMG_UINT32 ui32ServerSyncCount,
						   IMG_UINT32 *paui32ServerFlags);

PVRSRV_ERROR
PVRSRVSyncPrimOpReadyKM(SERVER_OP_COOKIE *psServerCookie,
						IMG_BOOL *pbReady);

PVRSRV_ERROR
PVRSRVSyncPrimOpCompleteKM(SERVER_OP_COOKIE *psServerCookie);

PVRSRV_ERROR
PVRSRVSyncPrimOpDestroyKM(SERVER_OP_COOKIE *psServerCookie);

IMG_UINT32 ServerSyncGetId(SERVER_SYNC_PRIMITIVE *psSync);

IMG_UINT32 ServerSyncGetFWAddr(SERVER_SYNC_PRIMITIVE *psSync);

IMG_UINT32 ServerSyncGetValue(SERVER_SYNC_PRIMITIVE *psSync);

IMG_UINT32 ServerSyncGetNextValue(SERVER_SYNC_PRIMITIVE *psSync);

IMG_VOID ServerSyncDumpPending(IMG_VOID);

PVRSRV_ERROR SyncRegisterConnection(SYNC_CONNECTION_DATA **ppsSyncConnectionData);
IMG_VOID SyncUnregisterConnection(SYNC_CONNECTION_DATA *ppsSyncConnectionData);
IMG_VOID SyncConnectionPDumpSyncBlocks(SYNC_CONNECTION_DATA *ppsSyncConnectionData);

PVRSRV_ERROR ServerSyncInit(IMG_VOID);
IMG_VOID ServerSyncDeinit(IMG_VOID);

#if defined(PDUMP)
PVRSRV_ERROR
PVRSRVSyncPrimPDumpKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset);

PVRSRV_ERROR
PVRSRVSyncPrimPDumpValueKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset, 
							IMG_UINT32 ui32Value);

PVRSRV_ERROR
PVRSRVSyncPrimPDumpPolKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset,
						 IMG_UINT32 ui32Value, IMG_UINT32 ui32Mask,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T uiDumpFlags);

PVRSRV_ERROR
PVRSRVSyncPrimOpPDumpPolKM(SERVER_OP_COOKIE *psServerCookie,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T ui32PDumpFlags);

PVRSRV_ERROR
PVRSRVSyncPrimPDumpCBPKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT64 ui32Offset,
						 IMG_UINT64 uiWriteOffset, IMG_UINT64 uiPacketSize,
						 IMG_UINT64 uiBufferSize);

#else	/* PDUMP */

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpKM)
#endif
static INLINE PVRSRV_ERROR
PVRSRVSyncPrimPDumpKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset)
{
	PVR_UNREFERENCED_PARAMETER(psSyncBlk);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpValueKM)
#endif
static INLINE PVRSRV_ERROR
PVRSRVSyncPrimPDumpValueKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset, 
							IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psSyncBlk);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
PVRSRVSyncPrimPDumpPolKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT32 ui32Offset,
						 IMG_UINT32 ui32Value, IMG_UINT32 ui32Mask,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T uiDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psSyncBlk);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(uiDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimOpPDumpPolKM)
#endif
static INLINE PVRSRV_ERROR
PVRSRVSyncPrimOpPDumpPolKM(SERVER_OP_COOKIE *psServerCookie,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T uiDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psServerCookie);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(uiDumpFlags);
	return PVRSRV_OK;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(PVRSRVSyncPrimPDumpCBPKM)
#endif
static INLINE PVRSRV_ERROR
PVRSRVSyncPrimPDumpCBPKM(SYNC_PRIMITIVE_BLOCK *psSyncBlk, IMG_UINT64 ui32Offset,
						 IMG_UINT64 uiWriteOffset, IMG_UINT64 uiPacketSize,
						 IMG_UINT64 uiBufferSize)
{
	PVR_UNREFERENCED_PARAMETER(psSyncBlk);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);
	return PVRSRV_OK;
}
#endif	/* PDUMP */
#endif	/*_SYNC_SERVER_H_ */
