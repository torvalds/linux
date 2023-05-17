/*******************************************************************************
@File
@Title          Direct client bridge for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the client side of the bridge for sync
                which is used in calls from Server context.
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
*******************************************************************************/

#include "client_sync_bridge.h"
#include "img_defs.h"
#include "pvr_debug.h"

/* Module specific includes */
#include "pdump.h"
#include "pdumpdefs.h"
#include "devicemem_typedefs.h"
#include "pvrsrv_sync_km.h"
#include <powervr/pvrsrv_sync_ext.h>

#include "sync.h"
#include "sync_server.h"
#include "pdump.h"
#include "pvrsrv_sync_km.h"
#include "sync_fallback_server.h"
#include "sync_checkpoint.h"

IMG_INTERNAL PVRSRV_ERROR BridgeAllocSyncPrimitiveBlock(IMG_HANDLE hBridge,
							IMG_HANDLE * phSyncHandle,
							IMG_UINT32 * pui32SyncPrimVAddr,
							IMG_UINT32 * pui32SyncPrimBlockSize,
							IMG_HANDLE * phhSyncPMR)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt = NULL;
	PMR *pshSyncPMRInt = NULL;

	eError =
	    PVRSRVAllocSyncPrimitiveBlockKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
					    &psSyncHandleInt,
					    pui32SyncPrimVAddr,
					    pui32SyncPrimBlockSize, &pshSyncPMRInt);

	*phSyncHandle = psSyncHandleInt;
	*phhSyncPMR = pshSyncPMRInt;
	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeFreeSyncPrimitiveBlock(IMG_HANDLE hBridge, IMG_HANDLE hSyncHandle)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError = PVRSRVFreeSyncPrimitiveBlockKM(psSyncHandleInt);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncPrimSet(IMG_HANDLE hBridge,
					    IMG_HANDLE hSyncHandle,
					    IMG_UINT32 ui32Index, IMG_UINT32 ui32Value)
{
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError = PVRSRVSyncPrimSetKM(psSyncHandleInt, ui32Index, ui32Value);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncPrimPDump(IMG_HANDLE hBridge,
					      IMG_HANDLE hSyncHandle, IMG_UINT32 ui32Offset)
{
#if defined(PDUMP)
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError = PVRSRVSyncPrimPDumpKM(psSyncHandleInt, ui32Offset);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hBridge);
	PVR_UNREFERENCED_PARAMETER(hSyncHandle);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncPrimPDumpValue(IMG_HANDLE hBridge,
						   IMG_HANDLE hSyncHandle,
						   IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
#if defined(PDUMP)
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError = PVRSRVSyncPrimPDumpValueKM(psSyncHandleInt, ui32Offset, ui32Value);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hBridge);
	PVR_UNREFERENCED_PARAMETER(hSyncHandle);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncPrimPDumpPol(IMG_HANDLE hBridge,
						 IMG_HANDLE hSyncHandle,
						 IMG_UINT32 ui32Offset,
						 IMG_UINT32 ui32Value,
						 IMG_UINT32 ui32Mask,
						 PDUMP_POLL_OPERATOR eOperator,
						 PDUMP_FLAGS_T uiPDumpFlags)
{
#if defined(PDUMP)
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
	    PVRSRVSyncPrimPDumpPolKM(psSyncHandleInt,
				     ui32Offset, ui32Value, ui32Mask, eOperator, uiPDumpFlags);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hBridge);
	PVR_UNREFERENCED_PARAMETER(hSyncHandle);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(uiPDumpFlags);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncPrimPDumpCBP(IMG_HANDLE hBridge,
						 IMG_HANDLE hSyncHandle,
						 IMG_UINT32 ui32Offset,
						 IMG_DEVMEM_OFFSET_T uiWriteOffset,
						 IMG_DEVMEM_SIZE_T uiPacketSize,
						 IMG_DEVMEM_SIZE_T uiBufferSize)
{
#if defined(PDUMP)
	PVRSRV_ERROR eError;
	SYNC_PRIMITIVE_BLOCK *psSyncHandleInt;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	psSyncHandleInt = (SYNC_PRIMITIVE_BLOCK *) hSyncHandle;

	eError =
	    PVRSRVSyncPrimPDumpCBPKM(psSyncHandleInt,
				     ui32Offset, uiWriteOffset, uiPacketSize, uiBufferSize);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hBridge);
	PVR_UNREFERENCED_PARAMETER(hSyncHandle);
	PVR_UNREFERENCED_PARAMETER(ui32Offset);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncAllocEvent(IMG_HANDLE hBridge,
					       IMG_BOOL bServerSync,
					       IMG_UINT32 ui32FWAddr,
					       IMG_UINT32 ui32ClassNameSize,
					       const IMG_CHAR * puiClassName)
{
	PVRSRV_ERROR eError;

	eError =
	    PVRSRVSyncAllocEventKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge),
				   bServerSync, ui32FWAddr, ui32ClassNameSize, puiClassName);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncFreeEvent(IMG_HANDLE hBridge, IMG_UINT32 ui32FWAddr)
{
	PVRSRV_ERROR eError;

	eError = PVRSRVSyncFreeEventKM(NULL, (PVRSRV_DEVICE_NODE *) ((void *)hBridge), ui32FWAddr);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR BridgeSyncCheckpointSignalledPDumpPol(IMG_HANDLE hBridge,
								PVRSRV_FENCE hFence)
{
#if defined(PDUMP)
	PVRSRV_ERROR eError;
	PVR_UNREFERENCED_PARAMETER(hBridge);

	eError = PVRSRVSyncCheckpointSignalledPDumpPolKM(hFence);

	return eError;
#else
	PVR_UNREFERENCED_PARAMETER(hBridge);
	PVR_UNREFERENCED_PARAMETER(hFence);

	return PVRSRV_ERROR_NOT_IMPLEMENTED;
#endif
}
