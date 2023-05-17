/*******************************************************************************
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
*******************************************************************************/

#ifndef COMMON_RGXTQ_BRIDGE_H
#define COMMON_RGXTQ_BRIDGE_H

#include <powervr/mem_types.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvrsrv_error.h"

#include "rgx_bridge.h"
#include "pvrsrv_sync_km.h"

#define PVRSRV_BRIDGE_RGXTQ_CMD_FIRST			0
#define PVRSRV_BRIDGE_RGXTQ_RGXCREATETRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+0
#define PVRSRV_BRIDGE_RGXTQ_RGXDESTROYTRANSFERCONTEXT			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+1
#define PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPRIORITY			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+2
#define PVRSRV_BRIDGE_RGXTQ_RGXSUBMITTRANSFER2			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+3
#define PVRSRV_BRIDGE_RGXTQ_RGXTQGETSHAREDMEMORY			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+4
#define PVRSRV_BRIDGE_RGXTQ_RGXTQRELEASESHAREDMEMORY			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+5
#define PVRSRV_BRIDGE_RGXTQ_RGXSETTRANSFERCONTEXTPROPERTY			PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+6
#define PVRSRV_BRIDGE_RGXTQ_CMD_LAST			(PVRSRV_BRIDGE_RGXTQ_CMD_FIRST+6)

/*******************************************
            RGXCreateTransferContext
 *******************************************/

/* Bridge in structure for RGXCreateTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT_TAG
{
	IMG_UINT64 ui64RobustnessAddress;
	IMG_HANDLE hPrivData;
	IMG_BYTE *pui8FrameworkCmd;
	IMG_INT32 i32Priority;
	IMG_UINT32 ui32ContextFlags;
	IMG_UINT32 ui32FrameworkCmdize;
	IMG_UINT32 ui32PackedCCBSizeU8888;
} __packed PVRSRV_BRIDGE_IN_RGXCREATETRANSFERCONTEXT;

/* Bridge out structure for RGXCreateTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXCREATETRANSFERCONTEXT;

/*******************************************
            RGXDestroyTransferContext
 *******************************************/

/* Bridge in structure for RGXDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT_TAG
{
	IMG_HANDLE hTransferContext;
} __packed PVRSRV_BRIDGE_IN_RGXDESTROYTRANSFERCONTEXT;

/* Bridge out structure for RGXDestroyTransferContext */
typedef struct PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXDESTROYTRANSFERCONTEXT;

/*******************************************
            RGXSetTransferContextPriority
 *******************************************/

/* Bridge in structure for RGXSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_INT32 i32Priority;
} __packed PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPRIORITY;

/* Bridge out structure for RGXSetTransferContextPriority */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPRIORITY;

/*******************************************
            RGXSubmitTransfer2
 *******************************************/

/* Bridge in structure for RGXSubmitTransfer2 */
typedef struct PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER2_TAG
{
	IMG_HANDLE hTransferContext;
	IMG_UINT32 *pui32ClientUpdateCount;
	IMG_UINT32 *pui32CommandSize;
	IMG_UINT32 *pui32SyncPMRFlags;
	IMG_UINT32 *pui32TQPrepareFlags;
	IMG_UINT32 **pui32UpdateSyncOffset;
	IMG_UINT32 **pui32UpdateValue;
	IMG_UINT8 **pui8FWCommand;
	IMG_CHAR *puiUpdateFenceName;
	IMG_HANDLE *phSyncPMRs;
	IMG_HANDLE **phUpdateUFOSyncPrimBlock;
	PVRSRV_TIMELINE h2DUpdateTimeline;
	PVRSRV_TIMELINE h3DUpdateTimeline;
	PVRSRV_FENCE hCheckFenceFD;
	IMG_UINT32 ui32ExtJobRef;
	IMG_UINT32 ui32PrepareCount;
	IMG_UINT32 ui32SyncPMRCount;
} __packed PVRSRV_BRIDGE_IN_RGXSUBMITTRANSFER2;

/* Bridge out structure for RGXSubmitTransfer2 */
typedef struct PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER2_TAG
{
	PVRSRV_ERROR eError;
	PVRSRV_FENCE h2DUpdateFence;
	PVRSRV_FENCE h3DUpdateFence;
} __packed PVRSRV_BRIDGE_OUT_RGXSUBMITTRANSFER2;

/*******************************************
            RGXTQGetSharedMemory
 *******************************************/

/* Bridge in structure for RGXTQGetSharedMemory */
typedef struct PVRSRV_BRIDGE_IN_RGXTQGETSHAREDMEMORY_TAG
{
	IMG_UINT32 ui32EmptyStructPlaceholder;
} __packed PVRSRV_BRIDGE_IN_RGXTQGETSHAREDMEMORY;

/* Bridge out structure for RGXTQGetSharedMemory */
typedef struct PVRSRV_BRIDGE_OUT_RGXTQGETSHAREDMEMORY_TAG
{
	IMG_HANDLE hCLIPMRMem;
	IMG_HANDLE hUSCPMRMem;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTQGETSHAREDMEMORY;

/*******************************************
            RGXTQReleaseSharedMemory
 *******************************************/

/* Bridge in structure for RGXTQReleaseSharedMemory */
typedef struct PVRSRV_BRIDGE_IN_RGXTQRELEASESHAREDMEMORY_TAG
{
	IMG_HANDLE hPMRMem;
} __packed PVRSRV_BRIDGE_IN_RGXTQRELEASESHAREDMEMORY;

/* Bridge out structure for RGXTQReleaseSharedMemory */
typedef struct PVRSRV_BRIDGE_OUT_RGXTQRELEASESHAREDMEMORY_TAG
{
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXTQRELEASESHAREDMEMORY;

/*******************************************
            RGXSetTransferContextProperty
 *******************************************/

/* Bridge in structure for RGXSetTransferContextProperty */
typedef struct PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Input;
	IMG_HANDLE hTransferContext;
	IMG_UINT32 ui32Property;
} __packed PVRSRV_BRIDGE_IN_RGXSETTRANSFERCONTEXTPROPERTY;

/* Bridge out structure for RGXSetTransferContextProperty */
typedef struct PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPROPERTY_TAG
{
	IMG_UINT64 ui64Output;
	PVRSRV_ERROR eError;
} __packed PVRSRV_BRIDGE_OUT_RGXSETTRANSFERCONTEXTPROPERTY;

#endif /* COMMON_RGXTQ_BRIDGE_H */
