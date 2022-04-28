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
#include "img_defs.h"
#include "pvrsrv.h"
#include "device.h"
#include "devicemem.h"
#include "pdump.h"
#include "pvrsrv_error.h"
#include "connection_server.h"
#include "pdump_km.h"

#ifndef SYNC_SERVER_H
#define SYNC_SERVER_H

typedef struct _SYNC_PRIMITIVE_BLOCK_ SYNC_PRIMITIVE_BLOCK;
typedef struct _SYNC_CONNECTION_DATA_ SYNC_CONNECTION_DATA;
typedef struct SYNC_RECORD* SYNC_RECORD_HANDLE;

typedef struct _SYNC_ADDR_LIST_
{
	IMG_UINT32 ui32NumSyncs;
	PRGXFWIF_UFO_ADDR *pasFWAddrs;
} SYNC_ADDR_LIST;

PVRSRV_ERROR
SyncPrimitiveBlockToFWAddr(SYNC_PRIMITIVE_BLOCK *psSyncPrimBlock,
						IMG_UINT32 ui32Offset,
						PRGXFWIF_UFO_ADDR *psAddrOut);

void
SyncAddrListInit(SYNC_ADDR_LIST *psList);

void
SyncAddrListDeinit(SYNC_ADDR_LIST *psList);

PVRSRV_ERROR
SyncAddrListPopulate(SYNC_ADDR_LIST *psList,
						IMG_UINT32 ui32NumSyncs,
						SYNC_PRIMITIVE_BLOCK **apsSyncPrimBlock,
						IMG_UINT32 *paui32SyncOffset);

PVRSRV_ERROR
SyncAddrListAppendSyncPrim(SYNC_ADDR_LIST          *psList,
						   PVRSRV_CLIENT_SYNC_PRIM *psSyncPrim);
PVRSRV_ERROR
SyncAddrListAppendCheckpoints(SYNC_ADDR_LIST *psList,
								IMG_UINT32 ui32NumCheckpoints,
								PSYNC_CHECKPOINT *apsSyncCheckpoint);

PVRSRV_ERROR
SyncAddrListAppendAndDeRefCheckpoints(SYNC_ADDR_LIST *psList,
									  IMG_UINT32 ui32NumCheckpoints,
									  PSYNC_CHECKPOINT *apsSyncCheckpoint);

void
SyncAddrListDeRefCheckpoints(IMG_UINT32 ui32NumCheckpoints,
							 PSYNC_CHECKPOINT *apsSyncCheckpoint);

PVRSRV_ERROR
SyncAddrListRollbackCheckpoints(PVRSRV_DEVICE_NODE *psDevNode, SYNC_ADDR_LIST *psList);

PVRSRV_ERROR
PVRSRVAllocSyncPrimitiveBlockKM(CONNECTION_DATA *psConnection,
                                PVRSRV_DEVICE_NODE * psDevNode,
								SYNC_PRIMITIVE_BLOCK **ppsSyncBlk,
								IMG_UINT32 *puiSyncPrimVAddr,
								IMG_UINT32 *puiSyncPrimBlockSize,
								PMR        **ppsSyncPMR);

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
PVRSRVSyncAllocEventKM(CONNECTION_DATA *psConnection,
					   PVRSRV_DEVICE_NODE *psDevNode,
					   IMG_BOOL bServerSync,
                       IMG_UINT32 ui32FWAddr,
                       IMG_UINT32 ui32ClassNameSize,
                       const IMG_CHAR *pszClassName);

PVRSRV_ERROR
PVRSRVSyncFreeEventKM(CONNECTION_DATA *psConnection,
					   PVRSRV_DEVICE_NODE *psDevNode,
					   IMG_UINT32 ui32FWAddr);

PVRSRV_ERROR
PVRSRVSyncRecordAddKM(CONNECTION_DATA *psConnection,
					  PVRSRV_DEVICE_NODE *psDevNode,
					  SYNC_RECORD_HANDLE *phRecord,
					  SYNC_PRIMITIVE_BLOCK *hServerSyncPrimBlock,
					  IMG_UINT32 ui32FwBlockAddr,
					  IMG_UINT32 ui32SyncOffset,
					  IMG_BOOL bServerSync,
					  IMG_UINT32 ui32ClassNameSize,
					  const IMG_CHAR *pszClassName);

PVRSRV_ERROR
PVRSRVSyncRecordRemoveByHandleKM(
			SYNC_RECORD_HANDLE hRecord);
void SyncRecordLookup(PVRSRV_DEVICE_NODE *psDevNode, IMG_UINT32 ui32FwAddr,
					  IMG_CHAR * pszSyncInfo, size_t len);

void ServerSyncDumpPending(void);

PVRSRV_ERROR SyncRegisterConnection(SYNC_CONNECTION_DATA **ppsSyncConnectionData);
void SyncUnregisterConnection(SYNC_CONNECTION_DATA *ppsSyncConnectionData);
void SyncConnectionPDumpSyncBlocks(void *hSyncPrivData, PDUMP_TRANSITION_EVENT eEvent);

/*!
******************************************************************************
@Function      SyncServerInit

@Description   Per-device initialisation for the ServerSync module
******************************************************************************/
PVRSRV_ERROR SyncServerInit(PVRSRV_DEVICE_NODE *psDevNode);
void SyncServerDeinit(PVRSRV_DEVICE_NODE *psDevNode);


/*!
******************************************************************************
@Function      PVRSRVLockServerSync

@Description   Acquire a global lock to maintain server sync consistency
******************************************************************************/
void PVRSRVLockServerSync(void);
/*!
******************************************************************************
@Function      PVRSRVUnlockServerSync

@Description   Release the global server sync lock
******************************************************************************/
void PVRSRVUnlockServerSync(void);

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
#endif	/*SYNC_SERVER_H */
