/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxtq
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxtq
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

#ifndef COMMON_RGXTQ_BRIDGE_H
#define COMMON_RGXTQ_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include <powervr/sync_external.h>


#define PVRSRV_BRIDGE_RGXTQ_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXTQ_CMD_LAST			(PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+3)


/*******************************************
            RGXCreateTransferContext          
 *******************************************/

/* Bridge in structure for RGXCreateTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT_TAG
{
	IMG_UINT32 ui32Priority;
	IMG_DEV_VIRTADDR sMCUFenceAddr;
	IMG_UINT32 ui32FrameworkCmdize;
	IMG_BYTE * psFrameworkCmd;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT;

/* Bridge out structure for RGXCreateTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT;


/*******************************************
            RGXDestroyTransferContext          
 *******************************************/

/* Bridge in structure for RGXDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT;

/* Bridge out structure for RGXDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT;


/*******************************************
            RGXSubmitTransfer          
 *******************************************/

/* Bridge in structure for RGXSubmitTransfer */
typedef struct PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32ClientCacheOpSeqNum;
	IMG_UINT32 ui32PrepareCount;
	IMG_UINT32 * pui32ClientFenceCount;
	IMG_HANDLE* * phFenceUFOSyncPrimBlock;
	IMG_UINT32* * pui32FenceSyncOffset;
	IMG_UINT32* * pui32FenceValue;
	IMG_UINT32 * pui32ClientUpdateCount;
	IMG_HANDLE* * phUpdateUFOSyncPrimBlock;
	IMG_UINT32* * pui32UpdateSyncOffset;
	IMG_UINT32* * pui32UpdateValue;
	IMG_UINT32 * pui32ServerSyncCount;
	IMG_UINT32* * pui32ServerSyncFlags;
	IMG_HANDLE* * phServerSync;
	IMG_INT32 i32CheckFenceFD;
	IMG_INT32 i32UpdateTimelineFD;
	IMG_CHAR * puiUpdateFenceName;
	IMG_UINT32 * pui32CommandSize;
	IMG_UINT8* * pui8FWCommand;
	IMG_UINT32 * pui32TQPrepareFlags;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32SyncPMRCount;
	IMG_UINT32 * pui32SyncPMRFlags;
	IMG_HANDLE * phSyncPMRs;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER;

/* Bridge out structure for RGXSubmitTransfer */
typedef struct PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER_TAG
{
	IMG_INT32 i32UpdateFenceFD;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER;


/*******************************************
            RGXSetTransferContextPriority          
 *******************************************/

/* Bridge in structure for RGXSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32Priority;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY;

/* Bridge out structure for RGXSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY;


#endif /* COMMON_RGXTQ_BRIDGE_H */
