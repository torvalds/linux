/*************************************************************************/ /*!
@File
@Title          RGX ray tracing functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the RGX ray tracing functionality
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

#if !defined(__RGXRAY_H__)
#define __RGXRAY_H__

#include "devicemem.h"
#include "devicemem_server.h"
#include "device.h"
#include "rgxdevice.h"
#include "rgx_fwif_shared.h"
#include "rgx_fwif_resetframework.h"
#include "rgxfwutils.h"
#include "pvr_notifier.h"

typedef struct _RGX_SERVER_RAY_CONTEXT_ RGX_SERVER_RAY_CONTEXT;
typedef struct _RGX_SERVER_RPM_CONTEXT_ RGX_SERVER_RPM_CONTEXT;
typedef struct _RGX_RPM_FREELIST_ RGX_RPM_FREELIST;


struct _RGX_SERVER_RPM_CONTEXT_
{
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	DEVMEM_MEMDESC			*psFWRPMContextMemDesc;
	//DEVMEM_MEMDESC		*psRTACtlMemDesc;
	//DEVMEM_MEMDESC		*psRTArrayMemDesc;
	PVRSRV_CLIENT_SYNC_PRIM	*psCleanupSync;
	IMG_UINT32				uiFLRefCount;		/*!< increments each time a free list references this parent context */

	DEVMEMINT_HEAP	*psSceneHeap;
	DEVMEMINT_HEAP	*psRPMPageTableHeap;
	DEVMEMINT_HEAP	*psRPMFreeListHeap;

	IMG_DEV_VIRTADDR	sSceneMemoryBaseAddr;
	IMG_DEV_VIRTADDR	sDopplerHeapBaseAddr;	/*!< Base address of the virtual heap where Doppler scene is mapped */
	IMG_DEV_VIRTADDR	sRPMPageTableBaseAddr;

	IMG_UINT32		ui32TotalRPMPages;			/*!< Total virtual pages available */
	IMG_UINT32		uiLog2DopplerPageSize;		/*!< Doppler virtual page size, may be sub-4KB */
	IMG_UINT32		ui32UnallocatedPages;		/*!< Unmapped pages which may be mapped and added to a RPM free list */
	IMG_UINT32		ui32RPMEntriesInPage;		/*!< Number of remaining RPM page entries (dwords) in current mapped pages */

	/* Sparse mappings */
	PMR 		*psSceneHierarchyPMR;	/*!< Scene hierarchy phys page resource */
	PMR 		*psRPMPageTablePMR;		/*!< RPM pages in use by scene hierarchy phys page resource */

	/* Current page offset at the end of the physical allocation (PMR)
	 * for the scene memory and RPM page tables. This is where new phys pages
	 * will be mapped when the grow occurs (using sparse dev mem API). */
	IMG_UINT32				ui32SceneMemorySparseMappingIndex;
	IMG_UINT32				ui32RPMPageTableSparseMappingIndex;
};

/*
 * RPM host freelist (analogous to PM host freelist)
 */
struct _RGX_RPM_FREELIST_ {
    PVRSRV_RGXDEV_INFO 		*psDevInfo;
    CONNECTION_DATA   		*psConnection;
    RGX_SERVER_RPM_CONTEXT	*psParentCtx;

	/* Free list PMR. Used for grow */
	PMR						*psFreeListPMR;
	IMG_DEVMEM_OFFSET_T		uiFreeListPMROffset;

	IMG_DEV_VIRTADDR		sBaseDevVAddr;

	/* Current page offset at the end of the physical allocation (PMR)
	 * for the scene memory and RPM page tables. This is where new phys pages
	 * will be mapped when the grow occurs (using sparse dev mem API). */
	IMG_UINT32				ui32RPMFreeListSparseMappingIndex;

	IMG_UINT32				ui32ReadOffset;			/*!< FPL circular buffer read offset */
	IMG_UINT32				ui32WriteOffset;		/*!< FPL circular buffer write offset */

	/* Freelist config */
	IMG_UINT32				ui32MaxFLPages;
	IMG_UINT32				ui32InitFLPages;
	IMG_UINT32				ui32CurrentFLPages;
	IMG_UINT32				ui32GrowFLPages;
	IMG_UINT32				ui32FreelistID;
	IMG_UINT64				ui64FreelistChecksum;	/* checksum over freelist content */
	IMG_BOOL				bCheckFreelist;			/* freelist check enabled */
	IMG_UINT32				ui32RefCount;			/* freelist reference counting */
	IMG_UINT32				uiLog2DopplerPageSize;	/*!< Doppler virtual page size, may be sub-4KB */
	IMG_UINT32				ui32EntriesInPage;		/*!< Number of remaining FPL page entries (dwords) in current mapped pages */

	IMG_UINT32				ui32NumGrowReqByApp;	/* Total number of grow requests by Application*/
	IMG_UINT32				ui32NumGrowReqByFW;		/* Total Number of grow requests by Firmware */
	IMG_UINT32				ui32NumHighPages;		/* High Mark of pages in the freelist */

	IMG_PID					ownerPid;			/* Pid of the owner of the list */

	/* 
	 * External freelists don't use common RPM memory and are not added to global list of freelists.
	 * They're created and destroyed on demand, e.g. when loading offline hierarchies.
	 */
	IMG_BOOL				bIsExternal;		/* Mark if the freelist is external */

	/* Memory Blocks */
	DLLIST_NODE				sMemoryBlockHead;		/* head of list of RGX_RPM_DEVMEM_DESC block descriptors */
	DLLIST_NODE				sNode;					/* node used to reference list of freelists on device */

	/* FW data structures */
	DEVMEM_MEMDESC			*psFWFreelistMemDesc;
	RGXFWIF_DEV_VIRTADDR	sFreeListFWDevVAddr;

	PVRSRV_CLIENT_SYNC_PRIM	*psCleanupSync;
} ;


/*!
 *	RGXCreateRPMFreeList
 * 
 * @param	ui32MaxFLPages
 * @param	ui32InitFLPages
 * @param	ui32GrowFLPages
 * @param	bCheckFreelist
 * @param	sFreeListDevVAddr
 * @param	sRPMPageListDevVAddr
 * @param	psFreeListPMR
 * @param	uiFreeListPMROffset
 * @param	ppsFreeList
 * @param	bIsExternal
 */
IMG_EXPORT
PVRSRV_ERROR RGXCreateRPMFreeList(CONNECTION_DATA *psConnection,
							   PVRSRV_DEVICE_NODE	 *psDeviceNode, 
							   RGX_SERVER_RPM_CONTEXT	*psRPMContext,
							   IMG_UINT32			ui32InitFLPages,
							   IMG_UINT32			ui32GrowFLPages,
							   IMG_DEV_VIRTADDR		sFreeListDevVAddr,
							   RGX_RPM_FREELIST	  **ppsFreeList,
							   IMG_UINT32		   *puiHWFreeList,
							   IMG_BOOL				bIsExternal);

/*!
 *	RGXGrowRPMFreeList
 */
PVRSRV_ERROR RGXGrowRPMFreeList(RGX_RPM_FREELIST *psFreeList,
								IMG_UINT32 ui32RequestNumPages,
								PDLLIST_NODE pListHeader);

/*!
 *	RGXDestroyRPMFreeList
 */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRPMFreeList(RGX_RPM_FREELIST *psFreeList);

/*!
 * RGXCreateRPMContext
 */
IMG_EXPORT
PVRSRV_ERROR RGXCreateRPMContext(CONNECTION_DATA *psConnection,
								 PVRSRV_DEVICE_NODE	 *psDeviceNode, 
								 RGX_SERVER_RPM_CONTEXT	**ppsRPMContext,
								 IMG_UINT32			ui32TotalRPMPages,
								 IMG_UINT32			uiLog2DopplerPageSize,
								 IMG_DEV_VIRTADDR	sSceneMemoryBaseAddr,
								 IMG_DEV_VIRTADDR	sDopplerHeapBaseAddr,
								 DEVMEMINT_HEAP		*psSceneHeap,
								 IMG_DEV_VIRTADDR	sRPMPageTableBaseAddr,
								 DEVMEMINT_HEAP		*psRPMPageTableHeap,
								 DEVMEM_MEMDESC		**ppsMemDesc,
							     IMG_UINT32		     *puiHWFrameData);

/*!
 * RGXDestroyRPMContext
 */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRPMContext(RGX_SERVER_RPM_CONTEXT *psCleanupData);

/*!
	RGXProcessRequestRPMGrow
*/
IMG_EXPORT
void RGXProcessRequestRPMGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
							  IMG_UINT32 ui32FreelistID);


/*! 
	RGXAddBlockToRPMFreeListKM
*/
IMG_EXPORT
PVRSRV_ERROR RGXAddBlockToRPMFreeListKM(RGX_RPM_FREELIST *psFreeList,
										IMG_UINT32 ui32NumPages);


/*!
*******************************************************************************

 @Function	PVRSRVRGXCreateRenderContextKM

 @Description
	Server-side implementation of RGXCreateRenderContext

 @Input pvDeviceNode - device node
 @Input psSHGCCBMemDesc - SHG CCB Memory descriptor
 @Input psSHGCCBCtlMemDesc - SHG CCB Ctrl Memory descriptor
 @Input psRTUCCBMemDesc - RTU CCB Memory descriptor
 @Input psRTUCCBCtlMemDesc - RTU CCB Ctrl Memory descriptor
 @Input ui32Priority - context priority
 @Input sMCUFenceAddr - MCU Fence device virtual address
 @Input sVRMCallStackAddr - VRM call stack device virtual address
 @Input ui32FrameworkRegisterSize - framework register size
 @Input pbyFrameworkRegisters - ptr to framework register
 @Input hMemCtxPrivData - memory context private data
 @Output ppsCleanupData - clean up data
 @Output ppsFWRayContextMemDesc - firmware ray context memory descriptor
 @Output ppsFWRayContextStateMemDesc - firmware ray context state memory descriptor

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateRayContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sMCUFenceAddr,
											IMG_DEV_VIRTADDR			sVRMCallStackAddr,
											IMG_UINT32					ui32FrameworkCommandSize,
											IMG_PBYTE					pabyFrameworkCommand,
											IMG_HANDLE					hMemCtxPrivData,
											RGX_SERVER_RAY_CONTEXT	**ppsRayContext);


/*!
*******************************************************************************

 @Function	PVRSRVRGXDestroyRayContextKM

 @Description
	Server-side implementation of RGXDestroyRayContext

 @Input psRayContext - Ray context

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyRayContextKM(RGX_SERVER_RAY_CONTEXT *psRayContext);


/*!
*******************************************************************************

 @Function	PVRSRVRGXKickRSKM

 @Description
	Server-side implementation of RGXKickRS

 @Input pvDeviceNode - device node
 @Input psFWRayContextMemDesc - memdesc for the firmware render context
 @Input ui32RTUcCCBWoffUpdate - New fw Woff for the client RTU CCB

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickRSKM(RGX_SERVER_RAY_CONTEXT		*psRayContext,
								IMG_UINT32					ui32ClientCacheOpSeqNum,
								IMG_UINT32					ui32ClientFenceCount,
								SYNC_PRIMITIVE_BLOCK			**pauiClientFenceUFOSyncPrimBlock,
								IMG_UINT32					*paui32ClientFenceOffset,
								IMG_UINT32					*paui32ClientFenceValue,
								IMG_UINT32					ui32ClientUpdateCount,
								SYNC_PRIMITIVE_BLOCK			**pauiClientUpdateUFOSyncPrimBlock,
								IMG_UINT32					*paui32ClientUpdateOffset,
								IMG_UINT32					*paui32ClientUpdateValue,
								IMG_UINT32					ui32ServerSyncPrims,
								IMG_UINT32					*paui32ServerSyncFlags,
								SERVER_SYNC_PRIMITIVE 		**pasServerSyncs,
								IMG_UINT32					ui32CmdSize,
								IMG_PBYTE					pui8DMCmd,
								IMG_UINT32					ui32FCCmdSize,
								IMG_PBYTE					pui8FCDMCmd,
								IMG_UINT32					ui32FrameContextID,
								IMG_UINT32					ui32PDumpFlags,
								IMG_UINT32					ui32ExtJobRef);
/*!
*******************************************************************************

 @Function	PVRSRVRGXKickVRDMKM

 @Description
	Server-side implementation of PVRSRVRGXKickVRDMKM

 @Input pvDeviceNode - device node
 @Input psFWRayContextMemDesc - memdesc for the firmware render context
 @Input ui32SHGcCCBWoffUpdate - New fw Woff for the client SHG CCB

 @Return   PVRSRV_ERROR

******************************************************************************/
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickVRDMKM(RGX_SERVER_RAY_CONTEXT		*psRayContext,
								 IMG_UINT32					ui32ClientCacheOpSeqNum,
								 IMG_UINT32					ui32ClientFenceCount,
								 SYNC_PRIMITIVE_BLOCK			**pauiClientFenceUFOSyncPrimBlock,
								 IMG_UINT32					*paui32ClientFenceOffset,
								 IMG_UINT32					*paui32ClientFenceValue,
								 IMG_UINT32					ui32ClientUpdateCount,
								 SYNC_PRIMITIVE_BLOCK			**pauiClientUpdateUFOSyncPrimBlock,
								 IMG_UINT32					*paui32ClientUpdateOffset,
								 IMG_UINT32					*paui32ClientUpdateValue,
								 IMG_UINT32					ui32ServerSyncPrims,
								 IMG_UINT32					*paui32ServerSyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServerSyncs,
								 IMG_UINT32					ui32CmdSize,
								 IMG_PBYTE					pui8DMCmd,
								 IMG_UINT32					ui32PDumpFlags,
								 IMG_UINT32					ui32ExtJobRef);

PVRSRV_ERROR PVRSRVRGXSetRayContextPriorityKM(CONNECTION_DATA *psConnection,
                                              PVRSRV_DEVICE_NODE *psDevNode,
												 RGX_SERVER_RAY_CONTEXT *psRayContext,
												 IMG_UINT32 ui32Priority);

/* Debug - check if ray context is waiting on a fence */
void CheckForStalledRayCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile);

/* Debug/Watchdog - check if client ray contexts are stalled */
IMG_UINT32 CheckForStalledClientRayCtxt(PVRSRV_RGXDEV_INFO *psDevInfo);

#endif /* __RGXRAY_H__ */
