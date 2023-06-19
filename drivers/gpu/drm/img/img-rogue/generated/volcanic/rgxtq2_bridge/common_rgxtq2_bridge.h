/*******************************************************************************
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
*******************************************************************************/

#ifndef COMMON_RGXTQ2_BRIDGE_H
#define COMMON_RGXTQ2_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMCREATETRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMDESTROYTRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMNOTIFYWRITEOFFSETUPDATE			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMSUBMITTRANSFER2			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMGETSHAREDMEMORY			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMRELEASESHAREDMEMORY			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXTQ2_RGXTDMSETTRANSFERCONTEXTPROPERTY			PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+7
#define PVRSRV_BRIDGE_RGXTQ2_CMD_LAST			(PVRSRV_BRIDGE_RGXTQ2_CMD_FIRST+7)

/*******************************************
            RGXTDMCreateTransferContext
 *******************************************/

/* Bridge in structure for RGXTDMCreateTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT_TAG
{
	IMG_UINT64 ui64RobustnessAddress;
	IMG_HANDLE hPrivData;
	IMG_BYTE *pui8FrameworkCmd;
	IMG_INT32 i32Priority;
	IMG_UINT32 ui32ContextFlags;
	IMG_UINT32 ui32FrameworkCmdSize;
	IMG_UINT32 ui32PackedCCBSizeU88;
} __packed PVRSRV_BRIDGE_IN_RGXTDMCREATETRANSFERCONTEXT;

/* Bridge out structure for RGXTDMCreateTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMCREATETRANSFERCONTEXT;

/*******************************************
            RGXTDMDestroyTransferContext
 *******************************************/

/* Bridge in structure for RGXTDMDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
} __packed PVRSRV_BRIDGE_IN_RGXTDMDESTROYTRANSFERCONTEXT;

/* Bridge out structure for RGXTDMDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMDESTROYTRANSFERCONTEXT;

/*******************************************
            RGXTDMSetTransferContextPriority
 *******************************************/

/* Bridge in structure for RGXTDMSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_INT32 i32Priority;
} __packed PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPRIORITY;

/* Bridge out structure for RGXTDMSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPRIORITY;

/*******************************************
            RGXTDMNotifyWriteOffsetUpdate
 *******************************************/

/* Bridge in structure for RGXTDMNotifyWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32PDumpFlags;
} __packed PVRSRV_BRIDGE_IN_RGXTDMNOTIFYWRITEOFFSETUPDATE;

/* Bridge out structure for RGXTDMNotifyWriteOffsetUpdate */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMNOTIFYWRITEOFFSETUPDATE;

/*******************************************
            RGXTDMSubmitTransfer2
 *******************************************/

/* Bridge in structure for RGXTDMSubmitTransfer2 */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER2_TAG
{
	IMG_UINT64 ui64DeadlineInus;
	IMG_HANDLE hTransferContext;
	IMG_UINT32 *pui32SyncPMRFlags;
	IMG_UINT32 *pui32UpdateSyncOffset;
	IMG_UINT32 *pui32UpdateValue;
	IMG_UINT8 *pui8FWCommand;
	IMG_CHAR *puiUpdateFenceName;
	IMG_HANDLE *phSyncPMRs;
	IMG_HANDLE *phUpdateUFOSyncPrimBlock;
	PVRSRV_FENCE hCheckFenceFD;
	PVRSRV_TIMELINE hUpdateTimeline;
	IMG_UINT32 ui32Characteristic1;
	IMG_UINT32 ui32Characteristic2;
	IMG_UINT32 ui32ClientUpdateCount;
	IMG_UINT32 ui32CommandSize;
	IMG_UINT32 ui32ExternalJobReference;
	IMG_UINT32 ui32PDumpFlags;
	IMG_UINT32 ui32SyncPMRCount;
} __packed PVRSRV_BRIDGE_IN_RGXTDMSUBMITTRANSFER2;

/* Bridge out structure for RGXTDMSubmitTransfer2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER2_TAG
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE hUpdateFence;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMSUBMITTRANSFER2;

/*******************************************
            RGXTDMGetSharedMemory
 *******************************************/

/* Bridge in structure for RGXTDMGetSharedMemory */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMGETSHAREDMEMORY_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_RGXTDMGETSHAREDMEMORY;

/* Bridge out structure for RGXTDMGetSharedMemory */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMGETSHAREDMEMORY_TAG
{
	IMG_HANDLE hCLIPMRMem;
	IMG_HANDLE hUSCPMRMem;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMGETSHAREDMEMORY;

/*******************************************
            RGXTDMReleaseSharedMemory
 *******************************************/

/* Bridge in structure for RGXTDMReleaseSharedMemory */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMRELEASESHAREDMEMORY_TAG
{
	IMG_HANDLE hPMRMem;
} __packed PVRSRV_BRIDGE_IN_RGXTDMRELEASESHAREDMEMORY;

/* Bridge out structure for RGXTDMReleaseSharedMemory */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMRELEASESHAREDMEMORY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMRELEASESHAREDMEMORY;

/*******************************************
            RGXTDMSetTransferContextProperty
 *******************************************/

/* Bridge in structure for RGXTDMSetTransferContextProperty */
typedef struct PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Input;
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32Property;
} __packed PVRSRV_BRIDGE_IN_RGXTDMSETTRANSFERCONTEXTPROPERTY;

/* Bridge out structure for RGXTDMSetTransferContextProperty */
typedef struct PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Output;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTDMSETTRANSFERCONTEXTPROPERTY;

#endif /* COMMON_RGXTQ2_BRIDGE_H */
