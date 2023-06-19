/*************************************************************************/ /*!
@File
@Title          RGX ray functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX compute functionality
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

#if !defined(RGXRAY_H_)
#define RGXRAY_H_

#include "devicemem.h"
#include "device.h"
#include "rgxfwutils.h"
#include "rgxdebug.h"
#include "pvr_notifier.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "connection_server.h"


typedef struct _RGX_SERVER_RAY_CONTEXT_ RGX_SERVER_RAY_CONTEXT;

/*!
*******************************************************************************
 @Function	PVRSRVRGXCreateRayContextKM


******************************************************************************/
PVRSRV_ERROR PVRSRVRGXCreateRayContextKM(CONNECTION_DATA		*psConnection,
										 PVRSRV_DEVICE_NODE		*psDeviceNode,
										 IMG_INT32				i32Priority,
										 IMG_HANDLE				hMemCtxPrivData,
										 IMG_UINT32				ui32ContextFlags,
										 IMG_UINT32				ui32StaticRayContextStateSize,
										 IMG_PBYTE				pStaticRayContextState,
										 IMG_UINT64				ui64RobustnessAddress,
										 IMG_UINT32				ui32MaxDeadlineMS,
										 RGX_SERVER_RAY_CONTEXT	**ppsRayContext);

/*!
*******************************************************************************
 @Function	PVRSRVRGXDestroyRayContextKM

 @Description
	Server-side implementation of RGXDestroyRayContext

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXDestroyRayContextKM(RGX_SERVER_RAY_CONTEXT *psRayContext);


/*!
*******************************************************************************
 @Function	PVRSRVRGXKickRDMKM

 @Description
	Server-side implementation of RGXKickRDM

 @Return   PVRSRV_ERROR
******************************************************************************/
PVRSRV_ERROR PVRSRVRGXKickRDMKM(RGX_SERVER_RAY_CONTEXT	*psRayContext,
								IMG_UINT32				ui32ClientUpdateCount,
								SYNC_PRIMITIVE_BLOCK	**pauiClientUpdateUFODevVarBlock,
								IMG_UINT32				*paui32ClientUpdateSyncOffset,
								IMG_UINT32				*paui32ClientUpdateValue,
								PVRSRV_FENCE			iCheckFence,
								PVRSRV_TIMELINE			iUpdateTimeline,
								PVRSRV_FENCE			*piUpdateFence,
								IMG_CHAR				pcszUpdateFenceName[PVRSRV_SYNC_NAME_LENGTH],
								IMG_UINT32				ui32CmdSize,
								IMG_PBYTE				pui8DMCmd,
								IMG_UINT32				ui32PDumpFlags,
								IMG_UINT32				ui32ExtJobRef,
								IMG_UINT32				ui32AccStructSizeInBytes,
								IMG_UINT32				ui32DispatchSize,
								IMG_UINT64				ui64DeadlineInus);


#endif /* RGXRAY_H_ */
