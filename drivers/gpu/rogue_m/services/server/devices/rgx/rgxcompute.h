/*************************************************************************/ /*!
@File
@Title          RGX compute functionality
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

#if !defined(__RGXCOMPUTE_H__)
#define __RGXCOMPUTE_H__

#include "devicemem.h"
#include "device.h"
#include "rgxfwutils.h"
#include "rgx_fwif_resetframework.h"
#include "rgxdebug.h"

#include "sync_server.h"
#include "sync_internal.h"
#include "connection_server.h"


typedef struct _RGX_SERVER_COMPUTE_CONTEXT_ RGX_SERVER_COMPUTE_CONTEXT;

/*!
*******************************************************************************
 @Function	PVRSRVRGXCreateComputeContextKM

 @Description
	

 @Input pvDeviceNode 
 @Input psCmpCCBMemDesc - 
 @Input psCmpCCBCtlMemDesc - 
 @Output ppsFWComputeContextMemDesc - 

 @Return   PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateComputeContextKM(CONNECTION_DATA			*psConnection,
											 PVRSRV_DEVICE_NODE			*psDeviceNode,
											 IMG_UINT32					ui32Priority,
											 IMG_DEV_VIRTADDR			sMCUFenceAddr,
											 IMG_UINT32					ui32FrameworkRegisterSize,
											 IMG_PBYTE					pbyFrameworkRegisters,
											 IMG_HANDLE					hMemCtxPrivData,
											 RGX_SERVER_COMPUTE_CONTEXT	**ppsComputeContext);

/*! 
*******************************************************************************
 @Function	PVRSRVRGXDestroyComputeContextKM

 @Description
	Server-side implementation of RGXDestroyComputeContext

 @Input psCleanupData - 

 @Return   PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyComputeContextKM(RGX_SERVER_COMPUTE_CONTEXT *psComputeContext);


/*!
*******************************************************************************
 @Function	PVRSRVRGXKickCDMKM

 @Description
	Server-side implementation of RGXKickCDM

 @Input psDeviceNode - RGX Device node
 @Input psFWComputeContextMemDesc - Mem desc for firmware compute context
 @Input ui32cCCBWoffUpdate - New fw Woff for the client CDM CCB

 @Return   PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickCDMKM(RGX_SERVER_COMPUTE_CONTEXT	*psComputeContext,
								IMG_UINT32					ui32ClientFenceCount,
								SYNC_PRIMITIVE_BLOCK			**pauiClientFenceUFOSyncPrimBlock,
								IMG_UINT32					*paui32ClientFenceSyncOffset,
								IMG_UINT32					*paui32ClientFenceValue,
								IMG_UINT32					ui32ClientUpdateCount,
								SYNC_PRIMITIVE_BLOCK			**pauiClientUpdateUFOSyncPrimBlock,
								IMG_UINT32					*paui32ClientUpdateSyncOffset,
								IMG_UINT32					*paui32ClientUpdateValue,
								IMG_UINT32					ui32ServerSyncPrims,
								IMG_UINT32					*paui32ServerSyncFlags,
								SERVER_SYNC_PRIMITIVE 		**pasServerSyncs,
								IMG_UINT32					ui32CmdSize,
								IMG_PBYTE					pui8DMCmd,
								IMG_BOOL					bPDumpContinuous,
							    IMG_UINT32					ui32ExtJobRef,
								IMG_UINT32					ui32IntJobRef);

/*!
*******************************************************************************
 @Function	PVRSRVRGXFlushComputeDataKM

 @Description
	Server-side implementation of RGXFlushComputeData

 @Input psComputeContext - Compute context to flush

 @Return   PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXFlushComputeDataKM(RGX_SERVER_COMPUTE_CONTEXT *psComputeContext);

PVRSRV_ERROR PVRSRVRGXSetComputeContextPriorityKM(CONNECTION_DATA *psConnection,
												  RGX_SERVER_COMPUTE_CONTEXT *psComputeContext,
												  IMG_UINT32 ui32Priority);

/* Debug - check if compute context is waiting on a fence */
IMG_VOID CheckForStalledComputeCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
									DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf);

/* Debug/Watchdog - check if client compute contexts are stalled */
IMG_BOOL CheckForStalledClientComputeCtxt(PVRSRV_RGXDEV_INFO *psDevInfo);

/*!
*******************************************************************************
 @Function	PVRSRVRGXKickSyncCDMKM

 @Description
	Sending a sync kick command though this CDM context

 @Return   PVRSRV_ERROR
******************************************************************************/
IMG_EXPORT PVRSRV_ERROR 
PVRSRVRGXKickSyncCDMKM(RGX_SERVER_COMPUTE_CONTEXT  *psComputeContext,
                       IMG_UINT32                  ui32ClientFenceCount,
                       SYNC_PRIMITIVE_BLOCK          **pauiClientFenceUFOSyncPrimBlock,
                       IMG_UINT32                  *paui32ClientFenceSyncOffset,
                       IMG_UINT32                  *paui32ClientFenceValue,
                       IMG_UINT32                  ui32ClientUpdateCount,
                       SYNC_PRIMITIVE_BLOCK           **pauiClientUpdateUFOSyncPrimBlock,
                       IMG_UINT32                  *paui32ClientUpdateSyncOffset,
                       IMG_UINT32                  *paui32ClientUpdateValue,
                       IMG_UINT32                  ui32ServerSyncPrims,
                       IMG_UINT32                  *paui32ServerSyncFlags,
                       SERVER_SYNC_PRIMITIVE       **pasServerSyncs,
					   IMG_UINT32				   ui32NumCheckFenceFDs,
					   IMG_INT32				   *pai32CheckFenceFDs,
					   IMG_INT32                   i32UpdateFenceFD,
                       IMG_BOOL                    bPDumpContinuous);

#endif /* __RGXCOMPUTE_H__ */
