/*************************************************************************/ /*!
@File
@Title          Common bridge header for sync
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures that are used by both
                the client and sever side of the bridge for sync
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

#ifndef COMMON_SYNC_BRIDGE_H
#define COMMON_SYNC_BRIDGE_H

#include "img_types.h"
#include "pvrsrv_error.h"

#include "pdump.h"
#include "pdumpdefs.h"
#include "devicemem_typedefs.h"


#define PVRSRV_BRIDGE_SYNC_CMD_FIRST			0
#define PVRSRV_BRIDGE_SYNC_ALLOCSYNCPRIMITIVEBLOCK			PVRSRV_BRIDGE_SYNC_CMD_FIRST+0
#define PVRSRV_BRIDGE_SYNC_FREESYNCPRIMITIVEBLOCK			PVRSRV_BRIDGE_SYNC_CMD_FIRST+1
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMSET			PVRSRV_BRIDGE_SYNC_CMD_FIRST+2
#define PVRSRV_BRIDGE_SYNC_SERVERSYNCPRIMSET			PVRSRV_BRIDGE_SYNC_CMD_FIRST+3
#define PVRSRV_BRIDGE_SYNC_SYNCRECORDREMOVEBYHANDLE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+4
#define PVRSRV_BRIDGE_SYNC_SYNCRECORDADD			PVRSRV_BRIDGE_SYNC_CMD_FIRST+5
#define PVRSRV_BRIDGE_SYNC_SERVERSYNCALLOC			PVRSRV_BRIDGE_SYNC_CMD_FIRST+6
#define PVRSRV_BRIDGE_SYNC_SERVERSYNCFREE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+7
#define PVRSRV_BRIDGE_SYNC_SERVERSYNCQUEUEHWOP			PVRSRV_BRIDGE_SYNC_CMD_FIRST+8
#define PVRSRV_BRIDGE_SYNC_SERVERSYNCGETSTATUS			PVRSRV_BRIDGE_SYNC_CMD_FIRST+9
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCREATE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+10
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPTAKE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+11
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPREADY			PVRSRV_BRIDGE_SYNC_CMD_FIRST+12
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPCOMPLETE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+13
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPDESTROY			PVRSRV_BRIDGE_SYNC_CMD_FIRST+14
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMP			PVRSRV_BRIDGE_SYNC_CMD_FIRST+15
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPVALUE			PVRSRV_BRIDGE_SYNC_CMD_FIRST+16
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPPOL			PVRSRV_BRIDGE_SYNC_CMD_FIRST+17
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMOPPDUMPPOL			PVRSRV_BRIDGE_SYNC_CMD_FIRST+18
#define PVRSRV_BRIDGE_SYNC_SYNCPRIMPDUMPCBP			PVRSRV_BRIDGE_SYNC_CMD_FIRST+19
#define PVRSRV_BRIDGE_SYNC_CMD_LAST			(PVRSRV_BRIDGE_SYNC_CMD_FIRST+19)


/*******************************************
            AllocSyncPrimitiveBlock          
 *******************************************/

/* Bridge in structure for AllocSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK_TAG
{
	IMG_HANDLE hDevNode;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_ALLOCSYNCPRIMITIVEBLOCK;


/* Bridge out structure for AllocSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32SyncPrimVAddr;
	IMG_UINT32 ui32SyncPrimBlockSize;
	DEVMEM_SERVER_EXPORTCOOKIE hExportCookie;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK;

/*******************************************
            FreeSyncPrimitiveBlock          
 *******************************************/

/* Bridge in structure for FreeSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK_TAG
{
	IMG_HANDLE hSyncHandle;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_FREESYNCPRIMITIVEBLOCK;


/* Bridge out structure for FreeSyncPrimitiveBlock */
typedef struct PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_FREESYNCPRIMITIVEBLOCK;

/*******************************************
            SyncPrimSet          
 *******************************************/

/* Bridge in structure for SyncPrimSet */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMSET_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Index;
	IMG_UINT32 ui32Value;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMSET;


/* Bridge out structure for SyncPrimSet */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMSET_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMSET;

/*******************************************
            ServerSyncPrimSet          
 *******************************************/

/* Bridge in structure for ServerSyncPrimSet */
typedef struct PVRSRV_BRIDGE_IN_SERVERSYNCPRIMSET_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Value;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SERVERSYNCPRIMSET;


/* Bridge out structure for ServerSyncPrimSet */
typedef struct PVRSRV_BRIDGE_OUT_SERVERSYNCPRIMSET_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SERVERSYNCPRIMSET;

/*******************************************
            SyncRecordRemoveByHandle          
 *******************************************/

/* Bridge in structure for SyncRecordRemoveByHandle */
typedef struct PVRSRV_BRIDGE_IN_SYNCRECORDREMOVEBYHANDLE_TAG
{
	IMG_HANDLE hhRecord;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCRECORDREMOVEBYHANDLE;


/* Bridge out structure for SyncRecordRemoveByHandle */
typedef struct PVRSRV_BRIDGE_OUT_SYNCRECORDREMOVEBYHANDLE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCRECORDREMOVEBYHANDLE;

/*******************************************
            SyncRecordAdd          
 *******************************************/

/* Bridge in structure for SyncRecordAdd */
typedef struct PVRSRV_BRIDGE_IN_SYNCRECORDADD_TAG
{
	IMG_HANDLE hhServerSyncPrimBlock;
	IMG_UINT32 ui32ui32FwBlockAddr;
	IMG_UINT32 ui32ui32SyncOffset;
	IMG_BOOL bbServerSync;
	IMG_UINT32 ui32ClassNameSize;
	const IMG_CHAR * puiClassName;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCRECORDADD;


/* Bridge out structure for SyncRecordAdd */
typedef struct PVRSRV_BRIDGE_OUT_SYNCRECORDADD_TAG
{
	IMG_HANDLE hhRecord;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCRECORDADD;

/*******************************************
            ServerSyncAlloc          
 *******************************************/

/* Bridge in structure for ServerSyncAlloc */
typedef struct PVRSRV_BRIDGE_IN_SERVERSYNCALLOC_TAG
{
	IMG_HANDLE hDevNode;
	IMG_UINT32 ui32ClassNameSize;
	const IMG_CHAR * puiClassName;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SERVERSYNCALLOC;


/* Bridge out structure for ServerSyncAlloc */
typedef struct PVRSRV_BRIDGE_OUT_SERVERSYNCALLOC_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32SyncPrimVAddr;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SERVERSYNCALLOC;

/*******************************************
            ServerSyncFree          
 *******************************************/

/* Bridge in structure for ServerSyncFree */
typedef struct PVRSRV_BRIDGE_IN_SERVERSYNCFREE_TAG
{
	IMG_HANDLE hSyncHandle;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SERVERSYNCFREE;


/* Bridge out structure for ServerSyncFree */
typedef struct PVRSRV_BRIDGE_OUT_SERVERSYNCFREE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SERVERSYNCFREE;

/*******************************************
            ServerSyncQueueHWOp          
 *******************************************/

/* Bridge in structure for ServerSyncQueueHWOp */
typedef struct PVRSRV_BRIDGE_IN_SERVERSYNCQUEUEHWOP_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_BOOL bbUpdate;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SERVERSYNCQUEUEHWOP;


/* Bridge out structure for ServerSyncQueueHWOp */
typedef struct PVRSRV_BRIDGE_OUT_SERVERSYNCQUEUEHWOP_TAG
{
	IMG_UINT32 ui32FenceValue;
	IMG_UINT32 ui32UpdateValue;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SERVERSYNCQUEUEHWOP;

/*******************************************
            ServerSyncGetStatus          
 *******************************************/

/* Bridge in structure for ServerSyncGetStatus */
typedef struct PVRSRV_BRIDGE_IN_SERVERSYNCGETSTATUS_TAG
{
	IMG_UINT32 ui32SyncCount;
	IMG_HANDLE * phSyncHandle;
	/* Output pointer pui32UID is also an implied input */
	IMG_UINT32 * pui32UID;
	/* Output pointer pui32FWAddr is also an implied input */
	IMG_UINT32 * pui32FWAddr;
	/* Output pointer pui32CurrentOp is also an implied input */
	IMG_UINT32 * pui32CurrentOp;
	/* Output pointer pui32NextOp is also an implied input */
	IMG_UINT32 * pui32NextOp;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SERVERSYNCGETSTATUS;


/* Bridge out structure for ServerSyncGetStatus */
typedef struct PVRSRV_BRIDGE_OUT_SERVERSYNCGETSTATUS_TAG
{
	IMG_UINT32 * pui32UID;
	IMG_UINT32 * pui32FWAddr;
	IMG_UINT32 * pui32CurrentOp;
	IMG_UINT32 * pui32NextOp;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SERVERSYNCGETSTATUS;

/*******************************************
            SyncPrimOpCreate          
 *******************************************/

/* Bridge in structure for SyncPrimOpCreate */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPCREATE_TAG
{
	IMG_UINT32 ui32SyncBlockCount;
	IMG_HANDLE * phBlockList;
	IMG_UINT32 ui32ClientSyncCount;
	IMG_UINT32 * pui32SyncBlockIndex;
	IMG_UINT32 * pui32Index;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_HANDLE * phServerSync;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPCREATE;


/* Bridge out structure for SyncPrimOpCreate */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPCREATE_TAG
{
	IMG_HANDLE hServerCookie;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPCREATE;

/*******************************************
            SyncPrimOpTake          
 *******************************************/

/* Bridge in structure for SyncPrimOpTake */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPTAKE_TAG
{
	IMG_HANDLE hServerCookie;
	IMG_UINT32 ui32ClientSyncCount;
	IMG_UINT32 * pui32Flags;
	IMG_UINT32 * pui32FenceValue;
	IMG_UINT32 * pui32UpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPTAKE;


/* Bridge out structure for SyncPrimOpTake */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPTAKE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPTAKE;

/*******************************************
            SyncPrimOpReady          
 *******************************************/

/* Bridge in structure for SyncPrimOpReady */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPREADY_TAG
{
	IMG_HANDLE hServerCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPREADY;


/* Bridge out structure for SyncPrimOpReady */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPREADY_TAG
{
	IMG_BOOL bReady;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPREADY;

/*******************************************
            SyncPrimOpComplete          
 *******************************************/

/* Bridge in structure for SyncPrimOpComplete */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPCOMPLETE_TAG
{
	IMG_HANDLE hServerCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPCOMPLETE;


/* Bridge out structure for SyncPrimOpComplete */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPCOMPLETE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPCOMPLETE;

/*******************************************
            SyncPrimOpDestroy          
 *******************************************/

/* Bridge in structure for SyncPrimOpDestroy */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPDESTROY_TAG
{
	IMG_HANDLE hServerCookie;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPDESTROY;


/* Bridge out structure for SyncPrimOpDestroy */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPDESTROY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPDESTROY;

/*******************************************
            SyncPrimPDump          
 *******************************************/

/* Bridge in structure for SyncPrimPDump */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMPDUMP;


/* Bridge out structure for SyncPrimPDump */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMP;

/*******************************************
            SyncPrimPDumpValue          
 *******************************************/

/* Bridge in structure for SyncPrimPDumpValue */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Value;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPVALUE;


/* Bridge out structure for SyncPrimPDumpValue */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPVALUE;

/*******************************************
            SyncPrimPDumpPol          
 *******************************************/

/* Bridge in structure for SyncPrimPDumpPol */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32Value;
	IMG_UINT32 ui32Mask;
	PDUMP_POLL_OPERATOR eOperator;
	PDUMP_FLAGS_T uiPDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPPOL;


/* Bridge out structure for SyncPrimPDumpPol */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPPOL;

/*******************************************
            SyncPrimOpPDumpPol          
 *******************************************/

/* Bridge in structure for SyncPrimOpPDumpPol */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMOPPDUMPPOL_TAG
{
	IMG_HANDLE hServerCookie;
	PDUMP_POLL_OPERATOR eOperator;
	PDUMP_FLAGS_T uiPDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMOPPDUMPPOL;


/* Bridge out structure for SyncPrimOpPDumpPol */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMOPPDUMPPOL_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMOPPDUMPPOL;

/*******************************************
            SyncPrimPDumpCBP          
 *******************************************/

/* Bridge in structure for SyncPrimPDumpCBP */
typedef struct PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP_TAG
{
	IMG_HANDLE hSyncHandle;
	IMG_UINT32 ui32Offset;
	IMG_DEVMEM_OFFSET_T uiWriteOffset;
	IMG_DEVMEM_SIZE_T uiPacketSize;
	IMG_DEVMEM_SIZE_T uiBufferSize;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_SYNCPRIMPDUMPCBP;


/* Bridge out structure for SyncPrimPDumpCBP */
typedef struct PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_SYNCPRIMPDUMPCBP;

#endif /* COMMON_SYNC_BRIDGE_H */
