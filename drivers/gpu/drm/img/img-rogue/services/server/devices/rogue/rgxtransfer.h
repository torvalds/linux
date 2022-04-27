/*************************************************************************/ /*!
@File
@Title          RGX Transfer queue Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX Transfer queue Functionality
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

#if !defined(RGXTRANSFER_H)
#define RGXTRANSFER_H

#include "devicemem.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgxfwutils.h"
#include "rgx_fwif_resetframework.h"
#include "rgxdebug.h"
#include "pvr_notifier.h"

#include "sync_server.h"
#include "connection_server.h"

typedef struct _RGX_SERVER_TQ_CONTEXT_ RGX_SERVER_TQ_CONTEXT;

/*!
*******************************************************************************

 @Function	PVRSRVRGXCreateTransferContextKM

 @Description
	Server-side implementation of RGXCreateTransferContext

 @Input pvDeviceNode - device node

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRGXCreateTransferContextKM(CONNECTION_DATA		*psConnection,
										   PVRSRV_DEVICE_NODE		*psDeviceNode,
										   IMG_UINT32				ui32Priority,
										   IMG_UINT32				ui32FrameworkCommandSize,
										   IMG_PBYTE				pabyFrameworkCommand,
										   IMG_HANDLE				hMemCtxPrivData,
										   IMG_UINT32				ui32PackedCCBSizeU8888,
										   IMG_UINT32				ui32ContextFlags,
										   RGX_SERVER_TQ_CONTEXT	**ppsTransferContext,
										   PMR						**ppsCLIPMRMem,
										   PMR						**ppsUSCPMRMem);


/*!
*******************************************************************************

 @Function	PVRSRVRGXDestroyTransferContextKM

 @Description
	Server-side implementation of RGXDestroyTransferContext

 @Input psTransferContext - Transfer context

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRGXDestroyTransferContextKM(RGX_SERVER_TQ_CONTEXT *psTransferContext);

/*!
*******************************************************************************

 @Function	PVRSRVSubmitTransferKM

 @Description
	Schedules one or more 2D or 3D HW commands on the firmware


 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRGXSubmitTransferKM(RGX_SERVER_TQ_CONTEXT	*psTransferContext,
									IMG_UINT32				ui32ClientCacheOpSeqNum,
									IMG_UINT32				ui32PrepareCount,
									IMG_UINT32				*paui32ClientUpdateCount,
									SYNC_PRIMITIVE_BLOCK		***papauiClientUpdateUFODevVarBlock,
									IMG_UINT32				**papaui32ClientUpdateSyncOffset,
									IMG_UINT32				**papaui32ClientUpdateValue,
									PVRSRV_FENCE			iCheckFence,
									PVRSRV_TIMELINE			i2DUpdateTimeline,
									PVRSRV_FENCE			*pi2DUpdateFence,
									PVRSRV_TIMELINE			i3DUpdateTimeline,
									PVRSRV_FENCE			*pi3DUpdateFence,
									IMG_CHAR				szFenceName[32],
									IMG_UINT32				*paui32FWCommandSize,
									IMG_UINT8				**papaui8FWCommand,
									IMG_UINT32				*pui32TQPrepareFlags,
									IMG_UINT32				ui32ExtJobRef,
									IMG_UINT32				ui32SyncPMRCount,
									IMG_UINT32				*paui32SyncPMRFlags,
									PMR						**ppsSyncPMRs);

PVRSRV_ERROR PVRSRVRGXSetTransferContextPriorityKM(CONNECTION_DATA *psConnection,
                                                   PVRSRV_DEVICE_NODE * psDevNode,
												   RGX_SERVER_TQ_CONTEXT *psTransferContext,
												   IMG_UINT32 ui32Priority);

PVRSRV_ERROR PVRSRVRGXSetTransferContextPropertyKM(RGX_SERVER_TQ_CONTEXT *psTransferContext,
												   RGX_CONTEXT_PROPERTY eContextProperty,
												   IMG_UINT64 ui64Input,
												   IMG_UINT64 *pui64Output);

/* Debug - Dump debug info of transfer contexts on this device */
void DumpTransferCtxtsInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                           DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                           void *pvDumpDebugFile,
                           IMG_UINT32 ui32VerbLevel);

/* Debug/Watchdog - check if client transfer contexts are stalled */
IMG_UINT32 CheckForStalledClientTransferCtxt(PVRSRV_RGXDEV_INFO *psDevInfo);

#endif /* RGXTRANSFER_H */
