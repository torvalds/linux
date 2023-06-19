/*******************************************************************************
@File
@Title          Common bridge header for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for sync
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

#ifndef COMMON_SYNC_BRIDGE_H
#define COMMON_SYNC_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "pdump.h"
#include "pdumpdefs.h"
#include "devicemem_typedefs.h"
#include "pvrsrv_sync_km.h"
#include <powervr/pvrsrv_sync_ext.h>

#define PVRSRV_BRIDGE_SYNC_CMD_FIRST			0
#define PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK			PVRSRV_BRIDGE_SYNC_CMD_FIRST+0
#define PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK			PVRSRV_BRIDGE_SYNC_CMD_FIRST+1
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMSET			PVRSRV_BRIDGE_SYNC_CMD_FIRST+2
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP			PVRSRV_BRIDGE_SYNC_CMD_FIRST+3
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+4
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL			PVRSRV_BRIDGE_SYNC_CMD_FIRST+5
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP			PVRSRV_BRIDGE_SYNC_CMD_FIRST+6
#define PVRSRV_BRIDGE_SYNC_SYNCALLOCEVENT			PVRSRV_BRIDGE_SYNC_CMD_FIRST+7
#define PVRSRV_BRIDGE_SYNC_SYNCFREEEVENT			PVRSRV_BRIDGE_SYNC_CMD_FIRST+8
#define PVRSRV_BRIDGE_SYNC_SYNCCHECKPOINTSIGNALLEDPDUMPPOL			PVRSRV_BRIDGE_SYNC_CMD_FIRST+9
#define PVRSRV_BRIDGE_SYNC_CMD_LAST			(PVRSRV_BRIDGE_SYNC_CMD_FIRST+9)

/*******************************************
            AllocSyncPrimitiveBlock
 *******************************************/

/* Bridge in structure for AllocSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK;

/* Bridge out structure for AllocSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_HANDLE hhSyncPMR;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32SyncPrimBlockSize;
	IMG_UINT32 ui32SyncPrimVAddr;
} __packed PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK;

/*******************************************
            FreeSyncPrimitiveBlock
 *******************************************/

/* Bridge in structure for FreeSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK_TAG
{
	IMG_HANDLE hSyncHandle;
} __packed PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK;

/* Bridge out structure for FreeSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK;

/*******************************************
            SyncPrimSet
 *******************************************/

/* Bridge in structure for SyncPrimSet */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMSET_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Index;
	IMG_UINT32 ui32Value;
} __packed PVRSRV_BRIDGE_IN_SYNCPRIMSET;

/* Bridge out structure for SyncPrimSet */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMSET_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCPRIMSET;

/*******************************************
            SyncPrimPDump
 *******************************************/

/* Bridge in structure for SyncPrimPDump */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
} __packed PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP;

/* Bridge out structure for SyncPrimPDump */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP;

/*******************************************
            SyncPrimPDumpValue
 *******************************************/

/* Bridge in structure for SyncPrimPDumpValue */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Value;
} __packed PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE;

/* Bridge out structure for SyncPrimPDumpValue */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE;

/*******************************************
            SyncPrimPDumpPol
 *******************************************/

/* Bridge in structure for SyncPrimPDumpPol */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL_TAG
{
	IMG_HANDLE hSyncHandle;
	PDUMP_POLL_OPERATOR eOperator;
	IMG_UINT32 ui32Mask;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Value;
	PDUMP_FLAGS_T uiPDumpFlags;
} __packed PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL;

/* Bridge out structure for SyncPrimPDumpPol */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL;

/*******************************************
            SyncPrimPDumpCBP
 *******************************************/

/* Bridge in structure for SyncPrimPDumpCBP */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP_TAG
{
	IMG_DEVMEM_SIZE_T uiBufferSize;
	IMG_DEVMEM_SIZE_T uiPacketSize;
	IMG_DEVMEM_OFFSET_T uiWriteOffset;
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
} __packed PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP;

/* Bridge out structure for SyncPrimPDumpCBP */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP;

/*******************************************
            SyncAllocEvent
 *******************************************/

/* Bridge in structure for SyncAllocEvent */
typedef struct PVRSRV_BRIDGE_IN_SYNCALLOCEVENT_TAG
{
	const IMG_CHAR *puiClassName;
	IMG_UINT32 ui32ClassNameSize;
	IMG_UINT32 ui32FWAddr;
	IMG_BOOL bServerSync;
} __packed PVRSRV_BRIDGE_IN_SYNCALLOCEVENT;

/* Bridge out structure for SyncAllocEvent */
typedef struct PVRSRV_BRIDGE_OUT_SYNCALLOCEVENT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCALLOCEVENT;

/*******************************************
            SyncFreeEvent
 *******************************************/

/* Bridge in structure for SyncFreeEvent */
typedef struct PVRSRV_BRIDGE_IN_SYNCFREEEVENT_TAG
{
	IMG_UINT32 ui32FWAddr;
} __packed PVRSRV_BRIDGE_IN_SYNCFREEEVENT;

/* Bridge out structure for SyncFreeEvent */
typedef struct PVRSRV_BRIDGE_OUT_SYNCFREEEVENT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCFREEEVENT;

/*******************************************
            SyncCheckpointSignalledPDumpPol
 *******************************************/

/* Bridge in structure for SyncCheckpointSignalledPDumpPol */
typedef struct PVRSRV_BRIDGE_IN_SYNCCHECKPOINTSIGNALLEDPDUMPPOL_TAG
{
	PVRSRV_FENCE hFence;
} __packed PVRSRV_BRIDGE_IN_SYNCCHECKPOINTSIGNALLEDPDUMPPOL;

/* Bridge out structure for SyncCheckpointSignalledPDumpPol */
typedef struct PVRSRV_BRIDGE_OUT_SYNCCHECKPOINTSIGNALLEDPDUMPPOL_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_SYNCCHECKPOINTSIGNALLEDPDUMPPOL;

#endif /* COMMON_SYNC_BRIDGE_H */
