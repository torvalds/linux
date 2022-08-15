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

#ifndef SYNC_H
#define SYNC_H

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "sync_prim_internal.h"
#include "pdumpdefs.h"
#include "dllist.h"
#include "pvr_debug.h"

#include "device_connection.h"

#if defined(__KERNEL__) && defined(__linux__) && !defined(__GENKSYMS__)
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
SyncPrimContextCreate(SHARED_DEV_CONNECTION hDevConnection,
                      PSYNC_PRIM_CONTEXT    *hSyncPrimContext);

/*************************************************************************/ /*!
@Function       SyncPrimContextDestroy

@Description    Destroy a synchronisation context

@Input          hSyncPrimContext        Handle to the synchronisation
                                        primitive context to destroy

@Return         None
*/
/*****************************************************************************/
void
SyncPrimContextDestroy(PSYNC_PRIM_CONTEXT hSyncPrimContext);

/*************************************************************************/ /*!
@Function       SyncPrimAlloc

@Description    Allocate a new synchronisation primitive on the specified
                synchronisation context

@Input          hSyncPrimContext        Handle to the synchronisation
                                        primitive context

@Output         ppsSync                 Created synchronisation primitive

@Input          pszClassName            Sync source annotation

@Return         PVRSRV_OK if the synchronisation primitive was
                successfully created
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimAlloc(PSYNC_PRIM_CONTEXT      hSyncPrimContext,
              PVRSRV_CLIENT_SYNC_PRIM **ppsSync,
              const IMG_CHAR          *pszClassName);

/*************************************************************************/ /*!
@Function       SyncPrimFree

@Description    Free a synchronisation primitive

@Input          psSync                  The synchronisation primitive to free

@Return         PVRSRV_OK if the synchronisation primitive was
                successfully freed
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimFree(PVRSRV_CLIENT_SYNC_PRIM *psSync);

/*************************************************************************/ /*!
@Function       SyncPrimSet

@Description    Set the synchronisation primitive to a value

@Input          psSync                  The synchronisation primitive to set

@Input          ui32Value               Value to set it to

@Return         PVRSRV_OK on success
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimSet(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value);

#if defined(NO_HARDWARE)

/*************************************************************************/ /*!
@Function       SyncPrimNoHwUpdate

@Description    Updates the synchronisation primitive value (in NoHardware drivers)

@Input          psSync                  The synchronisation primitive to update

@Input          ui32Value               Value to update it to

@Return         PVRSRV_OK on success
*/
/*****************************************************************************/
PVRSRV_ERROR
SyncPrimNoHwUpdate(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value);
#endif

#if defined(PDUMP)
/*************************************************************************/ /*!
@Function       SyncPrimPDump

@Description    PDump the current value of the synchronisation primitive

@Input          psSync                  The synchronisation primitive to PDump

@Return         None
*/
/*****************************************************************************/
void
SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync);

/*************************************************************************/ /*!
@Function       SyncPrimPDumpValue

@Description    PDump the ui32Value as the value of the synchronisation
                primitive (regardless of the current value).

@Input          psSync          The synchronisation primitive to PDump
@Input          ui32Value       Value to give to the sync prim on the pdump

@Return         None
*/
/*****************************************************************************/
void
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
void
SyncPrimPDumpPol(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT32 ui32Value,
				 IMG_UINT32 ui32Mask,
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
void
SyncPrimPDumpCBP(PVRSRV_CLIENT_SYNC_PRIM *psSync,
				 IMG_UINT64 uiWriteOffset,
				 IMG_UINT64 uiPacketSize,
				 IMG_UINT64 uiBufferSize);

#else

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDumpValue)
#endif
static INLINE void
SyncPrimPDumpValue(PVRSRV_CLIENT_SYNC_PRIM *psSync, IMG_UINT32 ui32Value)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
	PVR_UNREFERENCED_PARAMETER(ui32Value);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDump)
#endif
static INLINE void
SyncPrimPDump(PVRSRV_CLIENT_SYNC_PRIM *psSync)
{
	PVR_UNREFERENCED_PARAMETER(psSync);
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SyncPrimPDumpPol)
#endif
static INLINE void
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
#pragma inline(SyncPrimPDumpCBP)
#endif
static INLINE void
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
#endif /* PDUMP */
#endif /* SYNC_H */
