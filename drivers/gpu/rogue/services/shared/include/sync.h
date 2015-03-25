/*************************************************************************/ /*!
@File
@Title          Synchronisation interface header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines the client side interface for synchronisation
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

#include "img_types.h"
#include "pvrsrv_error.h"
#include "sync_external.h"
#include "pdumpdefs.h"
#include "dllist.h"
#include "pvr_debug.h"

#ifndef _SYNC_
#define _SYNC_

#if defined(KERNEL) && defined(ANDROID)
#define __pvrsrv_defined_struct_enum__
#include <services_kernel_client.h>
#endif

/*************************************************************************/ /*!
@Function       SyncPrimContextCreate

@Description    Create a new synchronisation context

@Input          hBridge                 Bridge handle

@Input          hDeviceNode             Device node handle

@Output         hSyncPrimContext        Handle to the created synchronisation
                                        primitive context

@Return         PVRSRV_OK if the synchronisation primitive context was
                successfully created
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimContextCreate(SYNC_BRIDGE_HANDLE	hBridge,
					  IMG_HANDLE			hDeviceNode,
					  PSYNC_PRIM_CONTEXT	*hSyncPrimContext);

/*************************************************************************/ /*!
@Function       SyncPrimContextDestroy

@Description    Destroy a synchronisation context

@Input          hSyncPrimContext        Handle to the synchronisation
                                        primitive context to destroy

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimContextDestroy(PSYNC_PRIM_CONTEXT hSyncPrimContext);

/*************************************************************************/ /*!
@Function       SyncPrimAlloc

@Description    Allocate a new synchronisation primitive on the specified
                synchronisation context

@Input          hSyncPrimContext        Handle to the synchronisation
                                        primitive context

@Output         ppsSync                 Created synchronisation primitive

@Return         PVRSRV_OK if the synchronisation primitive was
                successfully created
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimAlloc(PSYNC_PRIM_CONTEXT		hSyncPrimContext,
			  PVRSRV_CLIENT_SYNC_PRIM	**ppsSync,
			  const IMG_CHAR 		*pszClassName);

/*************************************************************************/ /*!
@Function       SyncPrimFree

@Description    Free a synchronisation primitive

@Input          psSync                  The synchronisation primitive to free

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimFree(PVRSRV_CLIENT_SYNC_PRIM *psSync);

/*************************************************************************/ /*!
@Function       SyncPrimSet

@Description    Set the synchronisation primitive to a value

@Input          psSync                  The synchronisation primitive to set

@Input          ui32Value               Value to set it to

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimSet(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value);

#if defined(NO_HARDWARE)

/*************************************************************************/ /*!
@Function       SyncPrimNoHwUpdate

@Description    Updates the synchronisation primitive value (in NoHardware drivers)

@Input          psSync                  The synchronisation primitive to update

@Input          ui32Value               Value to update it to

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimNoHwUpdate(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value);
#endif

PVRSRV_ERROR
SyncPrimServerAlloc(SYNC_BRIDGE_HANDLE	hBridge,
					IMG_HANDLE			hDeviceNode,
					PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
					const IMG_CHAR		*pszClassName
					PVR_DBG_FILELINE_PARAM);

PVRSRV_ERROR
SyncPrimServerGetStatus(IMG_UINT32 ui32SyncCount,
						PVRSRV_CLIENT_SYNC_PRIM **papsSync,
						IMG_UINT32 *pui32UID,
						IMG_UINT32 *pui32FWAddr,
						IMG_UINT32 *pui32CurrentOp,
						IMG_UINT32 *pui32NextOp);

PVRSRV_ERROR
SyncPrimServerQueueOp(PVRSRV_CLIENT_SYNC_PRIM_OP *psSyncOp);

IMG_BOOL
SyncPrimIsServerSync(PVRSRV_CLIENT_SYNC_PRIM *psSync);

IMG_HANDLE
SyncPrimGetServerHandle(PVRSRV_CLIENT_SYNC_PRIM *psSync);



PVRSRV_ERROR
SyncPrimOpCreate(IMG_UINT32 ui32SyncCount,
				 PVRSRV_CLIENT_SYNC_PRIM **papsSyncPrim,
				 PSYNC_OP_COOKIE *ppsCookie);

PVRSRV_ERROR
SyncPrimOpTake(PSYNC_OP_COOKIE psCookie,
			   IMG_UINT32 ui32SyncCount,
			   PVRSRV_CLIENT_SYNC_PRIM_OP *pasSyncOp);

PVRSRV_ERROR
SyncPrimOpReady(PSYNC_OP_COOKIE psCookie,
				IMG_BOOL *pbReady);

PVRSRV_ERROR
SyncPrimOpComplete(PSYNC_OP_COOKIE psCookie);

IMG_VOID
SyncPrimOpDestroy(PSYNC_OP_COOKIE psCookie);

PVRSRV_ERROR
SyncPrimOpResolve(PSYNC_OP_COOKIE psCookie,
				  IMG_UINT32 *pui32SyncCount,
				  PVRSRV_CLIENT_SYNC_PRIM_OP **ppsSyncOp);

PVRSRV_ERROR
SyncPrimDumpSyncs(IMG_UINT32 ui32SyncCount, PVRSRV_CLIENT_SYNC_PRIM **papsSync, const IMG_CHAR *pcszExtraInfo);

#if defined(PDUMP)
/*************************************************************************/ /*!
@Function       SyncPrimPDump

@Description    PDump the current value of the synchronisation primitive

@Input          psSync                  The synchronisation primitive to PDump

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync);

/*************************************************************************/ /*!
@Function       SyncPrimPDumpValue

@Description    PDump the ui32Value as the value of the synchronisation 
				primitive (regardless of the current value).

@Input          psSync          The synchronisation primitive to PDump
@Input			ui32Value		Value to give to the sync prim on the pdump

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimPDumpValue(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value);

/*************************************************************************/ /*!
@Function       SyncPrimPDumpPol

@Description    Do a PDump poll of the synchronisation primitive

@Input          psSync                  The synchronisation primitive to PDump

@Input          ui32Value               Value to poll for 

@Input          ui32Mask                PDump mask operator

@Input          ui32PDumpFlags          PDump flags

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT32 ui32Value,
				 IMG_UINT32 ui32Mask,
				 PDUMP_POLL_OPERATOR eOperator,
				 IMG_UINT32 ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       SyncPrimOpPDumpPol

@Description    Do a PDump poll all the synchronisation primitives on this
				Operation cookie.

@Input          psCookie                Operation cookie

@Input          ui32PDumpFlags          PDump flags

@Return         None
*/
/*****************************************************************************/
IMG_VOID
SyncPrimOpPDumpPol(PSYNC_OP_COOKIE psCookie,
				 PDUMP_POLL_OPERATOR eOperator,
				 IMG_UINT32 ui32PDumpFlags);

/*************************************************************************/ /*!
@Function       SyncPrimPDumpCBP

@Description    Do a PDump CB poll using the synchronisation primitive

@Input          psSync                  The synchronisation primitive to PDump

@Input          uiWriteOffset           Current write offset of buffer

@Input          uiPacketSize            Size of the packet to write into CB

@Input          uiBufferSize            Size of the CB

@Return         None
*/
/*****************************************************************************/
IMG_VOID 
SyncPrimPDumpCBP(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT64 uiWriteOffset,
				 IMG_UINT64 uiPacketSize,
				 IMG_UINT64 uiBufferSize);

#else

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDumpValue)
#endif
static INLINE IMG_VOID
SyncPrimPDumpValue(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDump)
#endif
static INLINE IMG_VOID
SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDumpPol)
#endif
static INLINE IMG_VOID
SyncPrimPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT32 ui32Value,
				 IMG_UINT32 ui32Mask,
				 PDUMP_POLL_OPERATOR eOperator,
				 IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
	PVR_UNREFERENCED_PARAMETER(ui32Mask);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimServerPDumpPol)
#endif
static INLINE IMG_VOID
SyncPrimServerPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 PDUMP_POLL_OPERATOR eOperator,
				 IMG_UINT32 ui32PDumpFlags)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
	PVR_UNREFERENCED_PARAMETER(eOperator);
	PVR_UNREFERENCED_PARAMETER(ui32PDumpFlags);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDumpCBP)
#endif
static INLINE IMG_VOID 
SyncPrimPDumpCBP(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT64 uiWriteOffset,
				 IMG_UINT64 uiPacketSize,
				 IMG_UINT64 uiBufferSize)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
	PVR_UNREFERENCED_PARAMETER(uiWriteOffset);
	PVR_UNREFERENCED_PARAMETER(uiPacketSize);
	PVR_UNREFERENCED_PARAMETER(uiBufferSize);
}
#endif	/* PDUMP */
#endif	/* _PVRSRV_SYNC_ */

