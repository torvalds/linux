/*************************************************************************/ /*!
@File
@Title          Common bridge header for rgxtq2
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declares common defines and structures used by both the client
                and server side of the bridge for rgxtq2
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

#ifndef COMMON_RGXTQ2_BRIDGE_H
#define COMMON_RGXTQ2_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include <powervr/sync_external.h>


#define PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMCREATETRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMDESTROYTRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMSUBMITTRANSFER			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMNOTIFYWRITEOFFSETUPDATE			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXTQ2_CMD_LAST			(PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+4)


/*******************************************
            RGXTDMCreateTransferContext          
 *******************************************/

/* Bridge in structure for RGXTDMCreateTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT_TAG
{
	IMG_UINT32 ui32Priority;
	IMG_DEV_VIRTADDR sMCUFenceAddr;
	IMG_UINT32 ui32FrameworkCmdize;
	IMG_BYTE * psFrameworkCmd;
	IMG_HANDLE hPrivData;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT;

/* Bridge out structure for RGXTDMCreateTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT;


/*******************************************
            RGXTDMDestroyTransferContext          
 *******************************************/

/* Bridge in structure for RGXTDMDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT;

/* Bridge out structure for RGXTDMDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT;


/*******************************************
            RGXTDMSubmitTransfer          
 *******************************************/

/* Bridge in structure for RGXTDMSubmitTransfer */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32ClientCacheOpSeqNum;
	IMG_UINT32 ui32ClientFenceCount;
	IMG_HANDLE * phFenceUFOSyncPrimBlock;
	IMG_UINT32 * pui32FenceSyncOffset;
	IMG_UINT32 * pui32FenceValue;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_HANDLE * phUpdateUFOSyncPrimBlock;
	IMG_UINT32 * pui32UpdateSyncOffset;
	IMG_UINT32 * pui32UpdateValue;
	IMG_UINT32 ui32ServerSyncCount;
	IMG_UINT32 * pui32ServerSyncFlags;
	IMG_HANDLE * phServerSync;
	IMG_INT32 i32CheckFenceFD;
	IMG_INT32 i32UpdateTimelineFD;
	IMG_CHAR * puiUpdateFenceName;
	IMG_UINT32 ui32CommandSize;
	IMG_UINT8 * pui8FWCommand;
	IMG_UINT32 ui32ExternalJobReference;
	IMG_UINT32 ui32SyncPMRCount;
	IMG_UINT32 * pui32SyncPMRFlags;
	IMG_HANDLE * phSyncPMRs;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER;

/* Bridge out structure for RGXTDMSubmitTransfer */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER_TAG
{
	IMG_INT32 i32UpdateFenceFD;
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER;


/*******************************************
            RGXTDMSetTransferContextPriority          
 *******************************************/

/* Bridge in structure for RGXTDMSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32Priority;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY;

/* Bridge out structure for RGXTDMSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY;


/*******************************************
            RGXTDMNotifyWriteOffsetUpdate          
 *******************************************/

/* Bridge in structure for RGXTDMNotifyWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32PDumpFlags;
} __attribute__((packed)) PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE;

/* Bridge out structure for RGXTDMNotifyWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE_TAG
{
	PVRSRV_ERROR eError;
} __attribute__((packed)) PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE;


#endif /* COMMON_RGXTQ2_BRIDGE_H */
