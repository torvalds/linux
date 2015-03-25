/*************************************************************************/ /*!
@File
@Title          RGX TA and 3D Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX TA and 3D Functionality
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

#if !defined(__RGXTA3D_H__)
#define __RGXTA3D_H__

#include "devicemem.h"
#include "devicemem_server.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgx_fwif_shared.h"
#include "rgx_fwif_resetframework.h"
#include "sync_server.h"
#include "connection_server.h"
#include "rgxdebug.h"

typedef struct _RGX_SERVER_RENDER_CONTEXT_ RGX_SERVER_RENDER_CONTEXT;
typedef struct _RGX_FREELIST_ RGX_FREELIST;
typedef struct _RGX_PMR_NODE_ RGX_PMR_NODE;

typedef struct {
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	DEVMEM_MEMDESC			*psFWHWRTDataMemDesc;
	DEVMEM_MEMDESC			*psRTACtlMemDesc;
	DEVMEM_MEMDESC			*psRTArrayMemDesc;
	RGX_FREELIST 			*apsFreeLists[RGXFW_MAX_FREELISTS];
	PVRSRV_CLIENT_SYNC_PRIM	*psCleanupSync;
} RGX_RTDATA_CLEANUP_DATA;

struct _RGX_FREELIST_ {
	PVRSRV_RGXDEV_INFO 		*psDevInfo;

	/* Free list PMR */
	PMR						*psFreeListPMR;
	IMG_DEVMEM_OFFSET_T		uiFreeListPMROffset;

	/* Freelist config */
	IMG_UINT32				ui32MaxFLPages;
	IMG_UINT32				ui32InitFLPages;
	IMG_UINT32				ui32CurrentFLPages;
	IMG_UINT32				ui32GrowFLPages;
	IMG_UINT32				ui32FreelistID;
	IMG_UINT64				ui64FreelistChecksum;	/* checksum over freelist content */
	IMG_BOOL				bCheckFreelist;			/* freelist check enabled */
	IMG_UINT32				ui32RefCount;			/* freelist reference counting */

	IMG_UINT32				ui32NumGrowReqByApp;	/* Total number of grow requests by Application*/
	IMG_UINT32				ui32NumGrowReqByFW;		/* Total Number of grow requests by Firmware */
	IMG_UINT32				ui32NumHighPages;		/* High Mark of pages in the freelist */

	IMG_PID					ownerPid;			/* Pid of the owner of the list */

	/* Memory Blocks */
	DLLIST_NODE				sMemoryBlockHead;
	DLLIST_NODE				sMemoryBlockInitHead;
	DLLIST_NODE				sNode;

	/* FW data structures */
	DEVMEM_MEMDESC			*psFWFreelistMemDesc;
	RGXFWIF_DEV_VIRTADDR	sFreeListFWDevVAddr;

	PVRSRV_CLIENT_SYNC_PRIM	*psCleanupSync;
} ;

struct _RGX_PMR_NODE_ {
	RGX_FREELIST			*psFreeList;
	PMR						*psPMR;
	PMR_PAGELIST 			*psPageList;
	DLLIST_NODE				sMemoryBlock;
	IMG_UINT32				ui32NumPages;
	IMG_BOOL				bInternal;
} ;

typedef struct {
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	DEVMEM_MEMDESC			*psRenderTargetMemDesc;
} RGX_RT_CLEANUP_DATA;

typedef struct {
	PVRSRV_RGXDEV_INFO		*psDevInfo;
	DEVMEM_MEMDESC			*psZSBufferMemDesc;
	RGXFWIF_DEV_VIRTADDR	sZSBufferFWDevVAddr;

	DEVMEMINT_RESERVATION 	*psReservation;
	PMR 					*psPMR;
	DEVMEMINT_MAPPING 		*psMapping;
	PVRSRV_MEMALLOCFLAGS_T 	uiMapFlags;
	IMG_UINT32 				ui32ZSBufferID;
	IMG_UINT32 				ui32RefCount;
	IMG_BOOL				bOnDemand;

	IMG_BOOL				ui32NumReqByApp;		/* Number of Backing Requests from  Application */
	IMG_BOOL				ui32NumReqByFW;			/* Number of Backing Requests from Firmware */

	IMG_PID					owner;

	DLLIST_NODE	sNode;

	PVRSRV_CLIENT_SYNC_PRIM	*psCleanupSync;
}RGX_ZSBUFFER_DATA;

typedef struct {
	RGX_ZSBUFFER_DATA		*psZSBuffer;
} RGX_POPULATION;

/* Dump the physical pages of a freelist */
IMG_BOOL RGXDumpFreeListPageList(RGX_FREELIST *psFreeList);


/* Create HWRTDataSet */
IMG_EXPORT
PVRSRV_ERROR RGXCreateHWRTData(PVRSRV_DEVICE_NODE	*psDeviceNode, 
							   IMG_UINT32			psRenderTarget,
							   IMG_DEV_VIRTADDR		psPMMListDevVAddr,
							   IMG_DEV_VIRTADDR		psVFPPageTableAddr,
							   RGX_FREELIST			*apsFreeLists[RGXFW_MAX_FREELISTS],
							   RGX_RTDATA_CLEANUP_DATA	**ppsCleanupData,
							   DEVMEM_MEMDESC			**ppsRTACtlMemDesc,
							   IMG_UINT32           ui32PPPScreen,
							   IMG_UINT32           ui32PPPGridOffset,
							   IMG_UINT64           ui64PPPMultiSampleCtl,
							   IMG_UINT32           ui32TPCStride,
							   IMG_DEV_VIRTADDR		sTailPtrsDevVAddr,
							   IMG_UINT32           ui32TPCSize,
							   IMG_UINT32           ui32TEScreen,
							   IMG_UINT32           ui32TEAA,
							   IMG_UINT32           ui32TEMTILE1,
							   IMG_UINT32           ui32TEMTILE2,
							   IMG_UINT32           ui32MTileStride,
							   IMG_UINT32                 ui32ISPMergeLowerX,
							   IMG_UINT32                 ui32ISPMergeLowerY,
							   IMG_UINT32                 ui32ISPMergeUpperX,
							   IMG_UINT32                 ui32ISPMergeUpperY,
							   IMG_UINT32                 ui32ISPMergeScaleX,
							   IMG_UINT32                 ui32ISPMergeScaleY,
							   IMG_UINT16			ui16MaxRTs,
							   DEVMEM_MEMDESC		**psMemDesc,
							   IMG_UINT32			*puiHWRTData);

/* Destroy HWRTData */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyHWRTData(RGX_RTDATA_CLEANUP_DATA *psCleanupData);

/* Create Render Target */
IMG_EXPORT
PVRSRV_ERROR RGXCreateRenderTarget(PVRSRV_DEVICE_NODE	*psDeviceNode,
								   IMG_DEV_VIRTADDR		psVHeapTableDevVAddr,
								   RGX_RT_CLEANUP_DATA	**ppsCleanupData,
								   IMG_UINT32			*sRenderTargetFWDevVAddr);

/* Destroy render target */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRenderTarget(RGX_RT_CLEANUP_DATA *psCleanupData);


/*
	RGXCreateZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXCreateZSBufferKM(PVRSRV_DEVICE_NODE				*psDeviceNode,
								DEVMEMINT_RESERVATION 	*psReservation,
								PMR 					*psPMR,
								PVRSRV_MEMALLOCFLAGS_T 	uiMapFlags,
								RGX_ZSBUFFER_DATA		 	**ppsZSBuffer,
								IMG_UINT32					*sRenderTargetFWDevVAddr);

/*
	RGXDestroyZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer);


/*
 * RGXBackingZSBuffer()
 *
 * Backs ZS-Buffer with physical pages
 */
PVRSRV_ERROR
RGXBackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer);

/*
 * RGXPopulateZSBufferKM()
 *
 * Backs ZS-Buffer with physical pages (called by Bridge calls)
 */
IMG_EXPORT
PVRSRV_ERROR RGXPopulateZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer,
									RGX_POPULATION **ppsPopulation);

/*
 * RGXUnbackingZSBuffer()
 *
 * Frees ZS-Buffer's physical pages
 */
IMG_EXPORT
PVRSRV_ERROR RGXUnbackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer);

/*
 * RGXUnpopulateZSBufferKM()
 *
 * Frees ZS-Buffer's physical pages (called by Bridge calls )
 */
IMG_EXPORT
PVRSRV_ERROR RGXUnpopulateZSBufferKM(RGX_POPULATION *psPopulation);

/*
	RGXProcessRequestZSBufferBacking
*/
IMG_EXPORT
IMG_VOID RGXProcessRequestZSBufferBacking(PVRSRV_RGXDEV_INFO *psDevInfo,
										IMG_UINT32 ui32ZSBufferID);

/*
	RGXProcessRequestZSBufferUnbacking
*/
IMG_EXPORT
IMG_VOID RGXProcessRequestZSBufferUnbacking(PVRSRV_RGXDEV_INFO *psDevInfo,
										IMG_UINT32 ui32ZSBufferID);

/*
	RGXGrowFreeList
*/
IMG_INTERNAL
PVRSRV_ERROR RGXGrowFreeList(RGX_FREELIST *psFreeList,
									IMG_UINT32 ui32NumPages,
									PDLLIST_NODE pListHeader);

/* Create free list */
IMG_EXPORT
PVRSRV_ERROR RGXCreateFreeList(PVRSRV_DEVICE_NODE	*psDeviceNode, 
							   IMG_UINT32			ui32MaxFLPages,
							   IMG_UINT32			ui32InitFLPages,
							   IMG_UINT32			ui32GrowFLPages,
							   IMG_BOOL				bCheckFreelist,
							   IMG_DEV_VIRTADDR		sFreeListDevVAddr,
							   PMR					*psFreeListPMR,
							   IMG_DEVMEM_OFFSET_T	uiFreeListPMROffset,
							   RGX_FREELIST			**ppsFreeList);

/* Destroy free list */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyFreeList(RGX_FREELIST *psFreeList);

/*
	RGXProcessRequestGrow
*/
IMG_EXPORT
IMG_VOID RGXProcessRequestGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
								IMG_UINT32 ui32FreelistID);


/* Grow free list */
IMG_EXPORT
PVRSRV_ERROR RGXAddBlockToFreeListKM(RGX_FREELIST *psFreeList,
										IMG_UINT32 ui32NumPages);

/* Shrink free list */
IMG_EXPORT
PVRSRV_ERROR RGXRemoveBlockFromFreeListKM(RGX_FREELIST *psFreeList);


/* Reconstruct free list after Hardware Recovery */
IMG_VOID RGXProcessRequestFreelistsReconstruction(PVRSRV_RGXDEV_INFO *psDevInfo,
								RGXFWIF_DM eDM,
								IMG_UINT32 ui32FreelistsCount,
								IMG_UINT32 *paui32Freelists);

/*!
*******************************************************************************

 @Function	PVRSRVRGXCreateRenderContextKM

 @Description
	Server-side implementation of RGXCreateRenderContext

 @Input pvDeviceNode - device node
 @Input psTACCBMemDesc - TA CCB Memory descriptor
 @Input psTACCBCtlMemDesc - TA CCB Ctrl Memory descriptor
 @Input ps3DCCBMemDesc - 3D CCB Memory descriptor
 @Input ps3DCCBCtlMemDesc - 3D CCB Ctrl Memory descriptor
 @Input ui32Priority - context priority
 @Input sMCUFenceAddr - MCU Fence device virtual address
 @Input psVDMStackPointer - VDM call stack device virtual address
 @Input ui32FrameworkRegisterSize - framework register size
 @Input pbyFrameworkRegisters - ptr to framework register
 @Input hMemCtxPrivData - memory context private data
 @Output ppsCleanupData - clean up data
 @Output ppsFWRenderContextMemDesc - firmware render context memory descriptor
 @Output ppsFWContextStateMemDesc - firmware context state memory descriptor

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateRenderContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sMCUFenceAddr,
											IMG_DEV_VIRTADDR			sVDMCallStackAddr,
											IMG_UINT32					ui32FrameworkCommandSize,
											IMG_PBYTE					pabyFrameworkCommand,
											IMG_HANDLE					hMemCtxPrivData,
											RGX_SERVER_RENDER_CONTEXT	**ppsRenderContext);


/*!
*******************************************************************************

 @Function	PVRSRVRGXDestroyRenderContextKM

 @Description
	Server-side implementation of RGXDestroyRenderContext

 @Input psCleanupData - clean up data

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyRenderContextKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext);


/*!
*******************************************************************************

 @Function	PVRSRVRGXKickTA3DKM

 @Description
	Server-side implementation of RGXKickTA3D

 @Input psRTDataCleanup - RT data associated with the kick (or NULL)
 @Input psZBuffer - Z-buffer associated with the kick (or NULL)
 @Input psSBuffer - S-buffer associated with the kick (or NULL)

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickTA3DKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
								 IMG_UINT32					ui32ClientTAFenceCount,
								 PRGXFWIF_UFO_ADDR			*pauiClientTAFenceUFOAddress,
								 IMG_UINT32					*paui32ClientTAFenceValue,
								 IMG_UINT32					ui32ClientTAUpdateCount,
								 PRGXFWIF_UFO_ADDR			*pauiClientUpdateTAUFOAddress,
								 IMG_UINT32					*paui32ClientTAUpdateValue,
								 IMG_UINT32					ui32ServerTASyncPrims,
								 IMG_UINT32					*paui32ServerTASyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServerTASyncs,
								 IMG_UINT32					ui32Client3DFenceCount,
								 PRGXFWIF_UFO_ADDR			*pauiClient3DFenceUFOAddress,
								 IMG_UINT32					*paui32Client3DFenceValue,
								 IMG_UINT32					ui32Client3DUpdateCount,
								 PRGXFWIF_UFO_ADDR			*pauiClientUpdate3DUFOAddress,
								 IMG_UINT32					*paui32Client3DUpdateValue,
								 IMG_UINT32					ui32Server3DSyncPrims,
								 IMG_UINT32					*paui32Server3DSyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServer3DSyncs,
								 PRGXFWIF_UFO_ADDR			uiPRFenceUFOAddress,
								 IMG_UINT32					ui32PRFenceValue,
								 IMG_UINT32					ui32NumFenceFds,
								 IMG_INT32					*pai32FenceFds,
								 IMG_UINT32					ui32TACmdSize,
								 IMG_PBYTE					pui8TADMCmd,
								 IMG_UINT32					ui323DPRCmdSize,
								 IMG_PBYTE					pui83DPRDMCmd,
								 IMG_UINT32					ui323DCmdSize,
								 IMG_PBYTE					pui83DDMCmd,
								 IMG_UINT32					TAFrameNum,
								 IMG_UINT32					TARTData,
								 IMG_BOOL					bLastTAInScene,
								 IMG_BOOL					bKickTA,
								 IMG_BOOL					bKickPR,
								 IMG_BOOL					bKick3D,
								 IMG_BOOL					bAbort,
								 IMG_BOOL					bPDumpContinuous,
								 RGX_RTDATA_CLEANUP_DATA        *psRTDataCleanup,
								 RGX_ZSBUFFER_DATA              *psZBuffer,
								 RGX_ZSBUFFER_DATA               *psSBuffer,
								 IMG_BOOL						bCommitRefCountsTA,
								 IMG_BOOL						bCommitRefCounts3D,
								 IMG_BOOL						*pbCommittedRefCountsTA,
								 IMG_BOOL						*pbCommittedRefCounts3D);


PVRSRV_ERROR PVRSRVRGXSetRenderContextPriorityKM(CONNECTION_DATA *psConnection,
												 RGX_SERVER_RENDER_CONTEXT *psRenderContext,
												 IMG_UINT32 ui32Priority);

PVRSRV_ERROR PVRSRVRGXGetLastRenderContextResetReasonKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
                                                        IMG_UINT32 *peLastResetReason);

PVRSRV_ERROR PVRSRVRGXGetPartialRenderCountKM(DEVMEM_MEMDESC *psHWRTDataMemDesc,
											  IMG_UINT32 *pui32NumPartialRenders);

/* Debug - check if render context is waiting on a fence */
IMG_VOID CheckForStalledRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
								   DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf);

/* Debug/Watchdog - check if client contexts are stalled */
IMG_BOOL CheckForStalledClientRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo);

IMG_EXPORT PVRSRV_ERROR 
PVRSRVRGXKickSyncTAKM(RGX_SERVER_RENDER_CONTEXT  *psRenderContext,
                       IMG_UINT32                  ui32TAClientFenceCount,
                       PRGXFWIF_UFO_ADDR           *pauiTAClientFenceUFOAddress,
                       IMG_UINT32                  *paui32TAClientFenceValue,
                       IMG_UINT32                  ui32TAClientUpdateCount,
                       PRGXFWIF_UFO_ADDR           *pauiTAClientUpdateUFOAddress,
                       IMG_UINT32                  *paui32TAClientUpdateValue,
                       IMG_UINT32                  ui32TAServerSyncPrims,
                       IMG_UINT32                  *paui32TAServerSyncFlags,
                       SERVER_SYNC_PRIMITIVE       **pasTAServerSyncs,
					   IMG_UINT32                  ui323DClientFenceCount,
					   PRGXFWIF_UFO_ADDR           *paui3DClientFenceUFOAddress,
					   IMG_UINT32                  *paui323DClientFenceValue,
					   IMG_UINT32                  ui323DClientUpdateCount,
					   PRGXFWIF_UFO_ADDR           *paui3DClientUpdateUFOAddress,
					   IMG_UINT32                  *paui323DClientUpdateValue,
					   IMG_UINT32                  ui323DServerSyncPrims,
					   IMG_UINT32                  *paui323DServerSyncFlags,
					   SERVER_SYNC_PRIMITIVE       **pas3DServerSyncs,
					   IMG_UINT32				   ui32NumFenceFDs,
					   IMG_INT32				   *paui32FenceFDs,
                       IMG_BOOL                    bPDumpContinuous);

#endif /* __RGXTA3D_H__ */
