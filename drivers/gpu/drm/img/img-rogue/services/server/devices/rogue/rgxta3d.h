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

#ifndef RGXTA3D_H
#define RGXTA3D_H

#include "devicemem.h"
#include "devicemem_server.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgx_fwif_shared.h"
#include "rgx_fwif_resetframework.h"
#include "sync_server.h"
#include "connection_server.h"
#include "rgxdebug.h"
#include "pvr_notifier.h"

typedef struct _RGX_SERVER_RENDER_CONTEXT_ RGX_SERVER_RENDER_CONTEXT;
typedef struct _RGX_FREELIST_ RGX_FREELIST;
typedef struct _RGX_PMR_NODE_ RGX_PMR_NODE;

/*****************************************************************************
 * The Design of Data Storage System for Render Targets                      *
 * ====================================================                      *
 *   Relevant for                                                            *
 *     understanding RGXCreateHWRTDataSet & RGXDestroyHWRTDataSet            *
 *                                                                           *
 *                                                                           *
 *        +=========================================+                        *
 *        |           RenderTargetDataSet           |                        *
 *        +---------------|---------|---------------+                        *
 *                        |         |                                        *
 *                        V         V                                        *
 *  +- - - - - - - - - - - - +   +- - - - - - - - - - - - +                  *
 *  | KM_HW_RT_DATA_HANDLE_0 |   | KM_HW_RT_DATA_HANDLE_1 |                  *
 *  +- - -|- - - - - - - - - +   +- - - - - - - - - | - - +                  *
 *        |                                         |                        *
 *        |                                         |           [UM]Client   *
 *  ------|-----------------------------------------|----------------------- *
 *        |                                         |               Bridge   *
 *  ------|-----------------------------------------|----------------------- *
 *        |                                         |           [KM]Server   *
 *        |                                         |                        *
 *        | KM-ptr                                  | KM-ptr                 *
 *        V                                         V                        *
 *  +====================+           +====================+                  *
 *  |  KM_HW_RT_DATA_0   |           |   KM_HW_RT_DATA_1  |                  *
 *  +-----|------------|-+           +-|------------|-----+                  *
 *        |            |               |            |                        *
 *        |            |               |            |                        *
 *        |            |               |            |                        *
 *        |            |               |            |                        *
 *        |            | KM-ptr        | KM-ptr     |                        *
 *        |            V               V            |                        *
 *        |      +==========================+       |                        *
 *        |      | HW_RT_DATA_COMMON_COOKIE |       |                        *
 *        |      +--------------------------+       |                        *
 *        |                   |                     |                        *
 *        |                   |                     |                        *
 *  ------|-------------------|---------------------|----------------------- *
 *        |                   |                     |         [FW]Firmware   *
 *        |                   |                     |                        *
 *        | FW-addr           |                     | FW-addr                *
 *        V                   |                     V                        *
 *  +===============+         |           +===============+                  *
 *  | HW_RT_DATA_0  |         |           | HW_RT_DATA_1  |                  *
 *  +------------|--+         |           +--|------------+                  *
 *               |            |              |                               *
 *               | FW-addr    | FW-addr      | FW-addr                       *
 *               V            V              V                               *
 *        +=========================================+                        *
 *        |           HW_RT_DATA_COMMON             |                        *
 *        +-----------------------------------------+                        *
 *                                                                           *
 *****************************************************************************/

typedef struct _RGX_HWRTDATA_COMMON_COOKIE_
{
	DEVMEM_MEMDESC			*psHWRTDataCommonFwMemDesc;
	RGXFWIF_DEV_VIRTADDR	sHWRTDataCommonFwAddr;
	IMG_UINT32				ui32RefCount;

} RGX_HWRTDATA_COMMON_COOKIE;

typedef struct _RGX_KM_HW_RT_DATASET_
{
	RGX_HWRTDATA_COMMON_COOKIE *psHWRTDataCommonCookie;

	PVRSRV_DEVICE_NODE *psDeviceNode;
	RGXFWIF_DEV_VIRTADDR sHWRTDataFwAddr;

	DEVMEM_MEMDESC *psHWRTDataFwMemDesc;
	DEVMEM_MEMDESC *psRTArrayFwMemDesc;
	DEVMEM_MEMDESC *psRendersAccArrayFwMemDesc;

	RGX_FREELIST *apsFreeLists[RGXFW_MAX_FREELISTS];
#if !defined(SUPPORT_SHADOW_FREELISTS)
	DLLIST_NODE			sNodeHWRTData;
#endif

} RGX_KM_HW_RT_DATASET;

struct _RGX_FREELIST_ {
	PVRSRV_RGXDEV_INFO		*psDevInfo;
	CONNECTION_DATA			*psConnection;

	/* Free list PMR */
	PMR						*psFreeListPMR;
	IMG_DEVMEM_OFFSET_T		uiFreeListPMROffset;

	/* Freelist config */
	IMG_UINT32				ui32MaxFLPages;
	IMG_UINT32				ui32InitFLPages;
	IMG_UINT32				ui32CurrentFLPages;
	IMG_UINT32				ui32GrowFLPages;
	IMG_UINT32				ui32ReadyFLPages;
	IMG_UINT32				ui32GrowThreshold;		/* Percentage of FL memory used that should trigger a new grow request */
	IMG_UINT32				ui32FreelistID;
	IMG_UINT32				ui32FreelistGlobalID;	/* related global freelist for this freelist */
	IMG_UINT64				ui64FreelistChecksum;	/* checksum over freelist content */
	IMG_BOOL				bCheckFreelist;			/* freelist check enabled */
	IMG_UINT32				ui32RefCount;			/* freelist reference counting */

	IMG_UINT32				ui32NumGrowReqByApp;	/* Total number of grow requests by Application */
	IMG_UINT32				ui32NumGrowReqByFW;		/* Total Number of grow requests by Firmware */
	IMG_UINT32				ui32NumHighPages;		/* High Mark of pages in the freelist */

	IMG_PID					ownerPid;				/* Pid of the owner of the list */

	/* Memory Blocks */
	DLLIST_NODE				sMemoryBlockHead;
	DLLIST_NODE				sMemoryBlockInitHead;
	DLLIST_NODE				sNode;
#if !defined(SUPPORT_SHADOW_FREELISTS)
	/* HWRTData nodes linked to local freelist */
	DLLIST_NODE				sNodeHWRTDataHead;
#endif

	/* FW data structures */
	DEVMEM_MEMDESC			*psFWFreelistMemDesc;
	RGXFWIF_DEV_VIRTADDR	sFreeListFWDevVAddr;
};

struct _RGX_PMR_NODE_ {
	RGX_FREELIST			*psFreeList;
	PMR						*psPMR;
	PMR_PAGELIST			*psPageList;
	DLLIST_NODE				sMemoryBlock;
	IMG_UINT32				ui32NumPages;
	IMG_BOOL				bFirstPageMissing;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	RI_HANDLE				hRIHandle;
#endif
};

typedef struct {
	PVRSRV_RGXDEV_INFO		*psDevInfo;
	DEVMEM_MEMDESC			*psFWZSBufferMemDesc;
	RGXFWIF_DEV_VIRTADDR	sZSBufferFWDevVAddr;

	DEVMEMINT_RESERVATION	*psReservation;
	PMR						*psPMR;
	DEVMEMINT_MAPPING		*psMapping;
	PVRSRV_MEMALLOCFLAGS_T	uiMapFlags;
	IMG_UINT32				ui32ZSBufferID;
	IMG_UINT32				ui32RefCount;
	IMG_BOOL				bOnDemand;

	IMG_BOOL				ui32NumReqByApp;		/* Number of Backing Requests from Application */
	IMG_BOOL				ui32NumReqByFW;			/* Number of Backing Requests from Firmware */

	IMG_PID					owner;

	DLLIST_NODE	sNode;
}RGX_ZSBUFFER_DATA;

typedef struct {
	RGX_ZSBUFFER_DATA		*psZSBuffer;
} RGX_POPULATION;

/* Dump the physical pages of a freelist */
IMG_BOOL RGXDumpFreeListPageList(RGX_FREELIST *psFreeList);


/* Create set of HWRTData(s) */
PVRSRV_ERROR RGXCreateHWRTDataSet(CONNECTION_DATA	*psConnection,
							   PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_DEV_VIRTADDR		psVHeapTableDevVAddr,
							   IMG_DEV_VIRTADDR		psPMMListDevVAddr_0,
							   IMG_DEV_VIRTADDR		psPMMListDevVAddr_1,
							   RGX_FREELIST			*apsFreeLists[RGXFW_MAX_FREELISTS],
							   IMG_UINT32			ui32ScreenPixelMax,
							   IMG_UINT64			ui64MultiSampleCtl,
							   IMG_UINT64			ui64FlippedMultiSampleCtl,
							   IMG_UINT32			ui32TPCStride,
							   IMG_DEV_VIRTADDR		sTailPtrsDevVAddr,
							   IMG_UINT32			ui32TPCSize,
							   IMG_UINT32			ui32TEScreen,
							   IMG_UINT32			ui32TEAA,
							   IMG_UINT32			ui32TEMTILE1,
							   IMG_UINT32			ui32TEMTILE2,
							   IMG_UINT32			ui32MTileStride,
							   IMG_UINT32			ui32ISPMergeLowerX,
							   IMG_UINT32			ui32ISPMergeLowerY,
							   IMG_UINT32			ui32ISPMergeUpperX,
							   IMG_UINT32			ui32ISPMergeUpperY,
							   IMG_UINT32			ui32ISPMergeScaleX,
							   IMG_UINT32			ui32ISPMergeScaleY,
							   IMG_DEV_VIRTADDR		sMacrotileArrayDevVAddr_0,
							   IMG_DEV_VIRTADDR		sMacrotileArrayDevVAddr_1,
							   IMG_DEV_VIRTADDR		sRgnHeaderDevVAddr_0,
							   IMG_DEV_VIRTADDR		sRgnHeaderDevVAddr_1,
							   IMG_DEV_VIRTADDR		sRTCDevVAddr,
							   IMG_UINT64			uiRgnHeaderSize,
							   IMG_UINT32			ui32ISPMtileSize,
							   IMG_UINT16			ui16MaxRTs,
							   RGX_KM_HW_RT_DATASET	**ppsKMHWRTDataSet_0,
							   RGX_KM_HW_RT_DATASET	**ppsKMHWRTDataSet_1);

/* Destroy HWRTDataSet */
PVRSRV_ERROR RGXDestroyHWRTDataSet(RGX_KM_HW_RT_DATASET *psKMHWRTDataSet);

/*
	RGXCreateZSBufferKM
*/
PVRSRV_ERROR RGXCreateZSBufferKM(CONNECTION_DATA			*psConnection,
                                 PVRSRV_DEVICE_NODE			*psDeviceNode,
                                 DEVMEMINT_RESERVATION		*psReservation,
                                 PMR						*psPMR,
                                 PVRSRV_MEMALLOCFLAGS_T		uiMapFlags,
                                 RGX_ZSBUFFER_DATA			**ppsZSBuffer);

/*
	RGXDestroyZSBufferKM
*/
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
PVRSRV_ERROR RGXPopulateZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer,
								   RGX_POPULATION **ppsPopulation);

/*
 * RGXUnbackingZSBuffer()
 *
 * Frees ZS-Buffer's physical pages
 */
PVRSRV_ERROR RGXUnbackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer);

/*
 * RGXUnpopulateZSBufferKM()
 *
 * Frees ZS-Buffer's physical pages (called by Bridge calls)
 */
PVRSRV_ERROR RGXUnpopulateZSBufferKM(RGX_POPULATION *psPopulation);

/*
	RGXProcessRequestZSBufferBacking
*/
void RGXProcessRequestZSBufferBacking(PVRSRV_RGXDEV_INFO *psDevInfo,
									  IMG_UINT32 ui32ZSBufferID);

/*
	RGXProcessRequestZSBufferUnbacking
*/
void RGXProcessRequestZSBufferUnbacking(PVRSRV_RGXDEV_INFO *psDevInfo,
										IMG_UINT32 ui32ZSBufferID);

/*
	RGXGrowFreeList
*/
PVRSRV_ERROR RGXGrowFreeList(RGX_FREELIST *psFreeList,
                             IMG_UINT32 ui32NumPages,
                             PDLLIST_NODE pListHeader);

/* Create free list */
PVRSRV_ERROR RGXCreateFreeList(CONNECTION_DATA		*psConnection,
							   PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_HANDLE			hMemCtxPrivData,
							   IMG_UINT32			ui32MaxFLPages,
							   IMG_UINT32			ui32InitFLPages,
							   IMG_UINT32			ui32GrowFLPages,
							   IMG_UINT32			ui32GrowParamThreshold,
							   RGX_FREELIST			*psGlobalFreeList,
							   IMG_BOOL				bCheckFreelist,
							   IMG_DEV_VIRTADDR		sFreeListDevVAddr,
							   PMR					*psFreeListPMR,
							   IMG_DEVMEM_OFFSET_T	uiFreeListPMROffset,
							   RGX_FREELIST			**ppsFreeList);

/* Destroy free list */
PVRSRV_ERROR RGXDestroyFreeList(RGX_FREELIST *psFreeList);

/*
	RGXProcessRequestGrow
*/
void RGXProcessRequestGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
						   IMG_UINT32 ui32FreelistID);


/* Reconstruct free list after Hardware Recovery */
void RGXProcessRequestFreelistsReconstruction(PVRSRV_RGXDEV_INFO *psDevInfo,
											  IMG_UINT32 ui32FreelistsCount,
											  IMG_UINT32 *paui32Freelists);

/*!
*******************************************************************************

 @Function	PVRSRVRGXCreateRenderContextKM

 @Description
	Server-side implementation of RGXCreateRenderContext

 @Input psConnection -
 @Input psDeviceNode - device node
 @Input ui32Priority - context priority
 @Input sVDMCallStackAddr - VDM call stack device virtual address
 @Input ui32FrameworkCommandSize - framework command size
 @Input pabyFrameworkCommand - ptr to framework command
 @Input hMemCtxPrivData - memory context private data
 @Input ui32StaticRenderContextStateSize - size of fixed render state
 @Input pStaticRenderContextState - ptr to fixed render state buffer
 @Input ui32PackedCCBSizeU8888 :
		ui8TACCBAllocSizeLog2 - TA CCB size
		ui8TACCBMaxAllocSizeLog2 - maximum size to which TA CCB can grow
		ui83DCCBAllocSizeLog2 - 3D CCB size
		ui83DCCBMaxAllocSizeLog2 - maximum size to which 3D CCB can grow
 @Input ui32ContextFlags - flags which specify properties of the context
 @Output ppsRenderContext -

 @Return   PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR PVRSRVRGXCreateRenderContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sVDMCallStackAddr,
											IMG_UINT32					ui32FrameworkCommandSize,
											IMG_PBYTE					pabyFrameworkCommand,
											IMG_HANDLE					hMemCtxPrivData,
											IMG_UINT32					ui32StaticRenderContextStateSize,
											IMG_PBYTE					pStaticRenderContextState,
											IMG_UINT32					ui32PackedCCBSizeU8888,
											IMG_UINT32					ui32ContextFlags,
											IMG_UINT64					ui64RobustnessAddress,
											IMG_UINT32					ui32MaxTADeadlineMS,
											IMG_UINT32					ui32Max3DDeadlineMS,
											RGX_SERVER_RENDER_CONTEXT	**ppsRenderContext);


/*!
*******************************************************************************

 @Function	PVRSRVRGXDestroyRenderContextKM

 @Description
	Server-side implementation of RGXDestroyRenderContext

 @Input psRenderContext -

 @Return   PVRSRV_ERROR

******************************************************************************/
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
PVRSRV_ERROR PVRSRVRGXKickTA3DKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
								 IMG_UINT32					ui32ClientCacheOpSeqNum,
								 IMG_UINT32					ui32ClientTAFenceCount,
								 SYNC_PRIMITIVE_BLOCK		**apsClientTAFenceSyncPrimBlock,
								 IMG_UINT32					*paui32ClientTAFenceSyncOffset,
								 IMG_UINT32					*paui32ClientTAFenceValue,
								 IMG_UINT32					ui32ClientTAUpdateCount,
								 SYNC_PRIMITIVE_BLOCK		**apsClientUpdateSyncPrimBlock,
								 IMG_UINT32					*paui32ClientUpdateSyncOffset,
								 IMG_UINT32					*paui32ClientTAUpdateValue,
								 IMG_UINT32					ui32Client3DUpdateCount,
								 SYNC_PRIMITIVE_BLOCK		**apsClient3DUpdateSyncPrimBlock,
								 IMG_UINT32					*paui32Client3DUpdateSyncOffset,
								 IMG_UINT32					*paui32Client3DUpdateValue,
								 SYNC_PRIMITIVE_BLOCK		*psPRSyncPrimBlock,
								 IMG_UINT32					ui32PRSyncOffset,
								 IMG_UINT32					ui32PRFenceValue,
								 PVRSRV_FENCE				iCheckFence,
								 PVRSRV_TIMELINE			iUpdateTimeline,
								 PVRSRV_FENCE				*piUpdateFence,
								 IMG_CHAR					szFenceName[PVRSRV_SYNC_NAME_LENGTH],
								 PVRSRV_FENCE				iCheckFence3D,
								 PVRSRV_TIMELINE			iUpdateTimeline3D,
								 PVRSRV_FENCE				*piUpdateFence3D,
								 IMG_CHAR					szFenceName3D[PVRSRV_SYNC_NAME_LENGTH],
								 IMG_UINT32					ui32TACmdSize,
								 IMG_PBYTE					pui8TADMCmd,
								 IMG_UINT32					ui323DPRCmdSize,
								 IMG_PBYTE					pui83DPRDMCmd,
								 IMG_UINT32					ui323DCmdSize,
								 IMG_PBYTE					pui83DDMCmd,
								 IMG_UINT32					ui32ExtJobRef,
								 IMG_BOOL					bKickTA,
								 IMG_BOOL					bKickPR,
								 IMG_BOOL					bKick3D,
								 IMG_BOOL					bAbort,
								 IMG_UINT32					ui32PDumpFlags,
								 RGX_KM_HW_RT_DATASET		*psKMHWRTDataSet,
								 RGX_ZSBUFFER_DATA			*psZSBuffer,
								 RGX_ZSBUFFER_DATA			*psMSAAScratchBuffer,
								 IMG_UINT32					ui32SyncPMRCount,
								 IMG_UINT32					*paui32SyncPMRFlags,
								 PMR						**ppsSyncPMRs,
								 IMG_UINT32					ui32RenderTargetSize,
								 IMG_UINT32					ui32NumberOfDrawCalls,
								 IMG_UINT32					ui32NumberOfIndices,
								 IMG_UINT32					ui32NumberOfMRTs,
								 IMG_UINT64					ui64DeadlineInus);


PVRSRV_ERROR PVRSRVRGXSetRenderContextPriorityKM(CONNECTION_DATA *psConnection,
                                                 PVRSRV_DEVICE_NODE * psDevNode,
                                                 RGX_SERVER_RENDER_CONTEXT *psRenderContext,
                                                 IMG_UINT32 ui32Priority);

PVRSRV_ERROR PVRSRVRGXSetRenderContextPropertyKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext,
												 RGX_CONTEXT_PROPERTY eContextProperty,
												 IMG_UINT64 ui64Input,
												 IMG_UINT64 *pui64Output);

/* Debug - Dump debug info of render contexts on this device */
void DumpRenderCtxtsInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                         DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                         void *pvDumpDebugFile,
                         IMG_UINT32 ui32VerbLevel);

/* Debug/Watchdog - check if client contexts are stalled */
IMG_UINT32 CheckForStalledClientRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo);

PVRSRV_ERROR RGXRenderContextStalledKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext);

#endif /* RGXTA3D_H */
