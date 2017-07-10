/*************************************************************************/ /*!
@File
@Title          RGX ray tracing routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX ray tracing routines
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
/* for the offsetof macro */
#include <stddef.h>
#if defined(INTEGRITY_OS)
#include <string.h>
#endif

#include "pdump_km.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxray.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "devicemem_server.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "rgxccb.h"
#include "rgxhwperf.h"
#include "rgxtimerquery.h"
#include "htbuffer.h"

#include "rgxdefs_km.h"
#include "rgx_fwif_km.h"
#include "physmem.h"
#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"
#include "process_stats.h"


/*
 * FIXME: Defs copied from "rgxrpmdefs.h"
 */

typedef struct _RGX_RPM_DATA_RTU_FREE_PAGE_LIST {
     IMG_UINT32 u32_0; 
} RGX_RPM_DATA_RTU_FREE_PAGE_LIST;

/*
Page table index.
                                                        The field is a pointer to a free page 
*/
#define RGX_RPM_DATA_RTU_FREE_PAGE_LIST_PTI_WOFF          (0U)
#define RGX_RPM_DATA_RTU_FREE_PAGE_LIST_PTI_SHIFT         (0U)
#define RGX_RPM_DATA_RTU_FREE_PAGE_LIST_PTI_CLRMSK        (0XFFC00000U)
#define RGX_RPM_DATA_RTU_FREE_PAGE_LIST_SET_PTI(_ft_,_x_) ((_ft_).u32_0 = (((_ft_).u32_0 & RGX_RPM_DATA_RTU_FREE_PAGE_LIST_PTI_CLRMSK ) | (((_x_) & (0x003fffff))  <<  0)))
#define RGX_RPM_DATA_RTU_FREE_PAGE_LIST_GET_PTI(_ft_)     (((_ft_).u32_0  >>  (0)) & 0x003fffff)

typedef struct _RGX_RPM_DATA_RTU_PAGE_TABLE {
     IMG_UINT32 u32_0; 
} RGX_RPM_DATA_RTU_PAGE_TABLE;

/*
 Page Table State
                                                        <br> 00: Empty Block
                                                        <br> 01: Full Block
                                                        <br> 10: Fragmented Block: Partially full page 
*/
#define RGX_RPM_DATA_RTU_PAGE_TABLE_PTS_WOFF              (0U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_PTS_SHIFT             (30U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_PTS_CLRMSK            (0X3FFFFFFFU)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_SET_PTS(_ft_,_x_)     ((_ft_).u32_0 = (((_ft_).u32_0 & RGX_RPM_DATA_RTU_PAGE_TABLE_PTS_CLRMSK ) | (((_x_) & (0x00000003))  <<  30)))
#define RGX_RPM_DATA_RTU_PAGE_TABLE_GET_PTS(_ft_)         (((_ft_).u32_0  >>  (30)) & 0x00000003)
/*
 Primitives in Page.
                                                        Number of unique primitives stored in this page.
                                                        The memory manager will re-use this page when the RCNT drops to zero.  
*/
#define RGX_RPM_DATA_RTU_PAGE_TABLE_RCNT_WOFF             (0U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_RCNT_SHIFT            (22U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_RCNT_CLRMSK           (0XC03FFFFFU)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_SET_RCNT(_ft_,_x_)    ((_ft_).u32_0 = (((_ft_).u32_0 & RGX_RPM_DATA_RTU_PAGE_TABLE_RCNT_CLRMSK ) | (((_x_) & (0x000000ff))  <<  22)))
#define RGX_RPM_DATA_RTU_PAGE_TABLE_GET_RCNT(_ft_)        (((_ft_).u32_0  >>  (22)) & 0x000000ff)
/*
Next page table index.
                                                        The field is a pointer to the next page for this primitive.  
*/
#define RGX_RPM_DATA_RTU_PAGE_TABLE_NPTI_WOFF             (0U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_NPTI_SHIFT            (0U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_NPTI_CLRMSK           (0XFFC00000U)
#define RGX_RPM_DATA_RTU_PAGE_TABLE_SET_NPTI(_ft_,_x_)    ((_ft_).u32_0 = (((_ft_).u32_0 & RGX_RPM_DATA_RTU_PAGE_TABLE_NPTI_CLRMSK ) | (((_x_) & (0x003fffff))  <<  0)))
#define RGX_RPM_DATA_RTU_PAGE_TABLE_GET_NPTI(_ft_)        (((_ft_).u32_0  >>  (0)) & 0x003fffff)


#define RGX_CR_RPM_PAGE_TABLE_BASE_VALUE_ALIGNSHIFT		(2U)
#define RGX_CR_RPM_SHF_FPL_BASE_ALIGNSHIFT				(2U)


typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
#if 0	
	/* FIXME - multiple frame contexts? */
	RGX_RPM_FREELIST				*psSHFFreeList;
	RGX_RPM_FREELIST				*psSHGFreeList;
#endif
} RGX_SERVER_RAY_SH_DATA;


typedef enum {
	NODE_EMPTY = 0,
	NODE_SCENE_HIERARCHY,
	NODE_RPM_PAGE_TABLE,
	NODE_RPM_FREE_PAGE_LIST
} RGX_DEVMEM_NODE_TYPE;

typedef struct _RGX_DEVMEM_NODE_ {
	RGX_DEVMEM_NODE_TYPE	eNodeType;			/*!< Alloc type */
	PMR						*psPMR; 			/*!< Scene hierarchy/page table/free page list phys pages */
	DEVMEMINT_HEAP			*psDevMemHeap;		/*!< Heap where the virtual mapping is made */
	IMG_DEV_VIRTADDR		sAddr;				/*!< GPU virtual address where the phys pages are mapped into */
	IMG_UINT32				ui32NumPhysPages;	/*!< Number of physical pages mapped in for this node */
	IMG_UINT32				ui32StartOfMappingIndex;	/*!< Start of mapping index (i.e. OS page offset from virtual base) */
	IMG_BOOL				bInternal;
} RGX_DEVMEM_NODE;

typedef struct _RGX_RPM_DEVMEM_DESC_ {
	DLLIST_NODE			sMemoryDescBlock;		/*!< the hierarchy scene memory block  */
	RGX_RPM_FREELIST	*psFreeList;			/*!< Free list this allocation is associated with */
	IMG_UINT32			ui32NumPages;			/*!< Number of RPM pages added */
	RGX_DEVMEM_NODE		sSceneHierarchyNode;	/*!< scene hierarchy block descriptor */
	RGX_DEVMEM_NODE		sRPMPageListNode;		/*!< RPM page list block descriptor */
	RGX_DEVMEM_NODE		sRPMFreeListNode;		/*!< RPM free list block descriptor */
} RGX_RPM_DEVMEM_DESC;

typedef struct _DEVMEM_RPM_FREELIST_LOOKUP_
{
	IMG_UINT32 ui32FreeListID;
	RGX_RPM_FREELIST *psFreeList;
} DEVMEM_RPM_FREELIST_LOOKUP;

typedef struct {
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
	RGX_CLIENT_CCB *psFCClientCCB[DPX_MAX_RAY_CONTEXTS];
	DEVMEM_MEMDESC *psFCClientCCBMemDesc[DPX_MAX_RAY_CONTEXTS];
	DEVMEM_MEMDESC *psFCClientCCBCtrlMemDesc[DPX_MAX_RAY_CONTEXTS];
} RGX_SERVER_RAY_RS_DATA;


struct _RGX_SERVER_RAY_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWRayContextMemDesc;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	RGX_SERVER_RAY_SH_DATA		sSHData;
	RGX_SERVER_RAY_RS_DATA		sRSData;
	IMG_UINT32					ui32CleanupStatus;
#define RAY_CLEANUP_SH_COMPLETE		(1 << 0)
#define RAY_CLEANUP_RS_COMPLETE		(1 << 1)
	PVRSRV_CLIENT_SYNC_PRIM		*psCleanupSync;
	DLLIST_NODE					sListNode;
	SYNC_ADDR_LIST				sSyncAddrListFence;
	SYNC_ADDR_LIST				sSyncAddrListUpdate;
	ATOMIC_T					hJobId;
};


#if 0
static
#ifdef __GNUC__
	__attribute__((noreturn))
#endif
void sleep_for_ever(void)
{
#if defined(__KLOCWORK__) // klocworks would report an infinite loop because of while(1).
	PVR_ASSERT(0); 
#else
	while(1)
	{
		OSSleepms(~0); // sleep the maximum amount of time possible
	}
#endif
}
#endif

static
PVRSRV_ERROR _RGXCreateRPMSparsePMR(CONNECTION_DATA *psConnection,
									PVRSRV_DEVICE_NODE	 *psDeviceNode,
									RGX_DEVMEM_NODE_TYPE eBlockType,
									IMG_UINT32		ui32NumPages,
									IMG_UINT32		uiLog2DopplerPageSize,
									PMR				**ppsPMR);

static PVRSRV_ERROR _RGXMapRPMPBBlock(RGX_DEVMEM_NODE	*psDevMemNode,
					RGX_RPM_FREELIST *psFreeList,
					RGX_DEVMEM_NODE_TYPE eBlockType,
					DEVMEMINT_HEAP *psDevmemHeap,
					IMG_UINT32 ui32NumPages,
					IMG_DEV_VIRTADDR sDevVAddrBase);

static
PVRSRV_ERROR _RGXUnmapRPMPBBlock(RGX_DEVMEM_NODE	*psDevMemNode,
					RGX_RPM_FREELIST *psFreeList,
					IMG_DEV_VIRTADDR sDevVAddrBase);

static
PVRSRV_ERROR _CreateSHContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_DEV_VIRTADDR sVRMCallStackAddr,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RAY_SH_DATA *psSHData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_VRDMCTX_STATE *psContextState;
	PVRSRV_ERROR eError;
	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware SHG context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_VRDMCTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FwRaySHGContextSuspendState",
							  &psSHData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to allocate firmware GPU context suspend state (%u)",
				eError));
		goto fail_shcontextsuspendalloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psSHData->psContextStateMemDesc,
                                      (void **)&psContextState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to map firmware render context state (%u)",
				eError));
		goto fail_suspendcpuvirtacquire;
	}
	psContextState->uVRDMReg_VRM_CALL_STACK_POINTER = sVRMCallStackAddr.uiAddr;
	DevmemReleaseCpuVirtAddr(psSHData->psContextStateMemDesc);

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_SH,
									 RGXFWIF_DM_SHG,
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
									 psSHData->psContextStateMemDesc,
									 RGX_RTU_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &psSHData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to init TA fw common context (%u)",
				eError));
		goto fail_shcommoncontext;
	}
	
	/*
	 * Dump the FW SH context suspend state buffer
	 */
	PDUMPCOMMENT("Dump the SH context suspend state buffer");
	DevmemPDumpLoadMem(psSHData->psContextStateMemDesc,
					   0,
					   sizeof(RGXFWIF_VRDMCTX_STATE),
					   PDUMP_FLAGS_CONTINUOUS);

	psSHData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_shcommoncontext:
fail_suspendcpuvirtacquire:
	DevmemFwFree(psDevInfo, psSHData->psContextStateMemDesc);
fail_shcontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

static
PVRSRV_ERROR _CreateRSContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RAY_RS_DATA *psRSData)
{
	PVRSRV_ERROR eError;

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_RS,
									 RGXFWIF_DM_RTU,
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
                                     NULL,
									 RGX_RTU_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &psRSData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to init 3D fw common context (%u)",
				eError));
		goto fail_rscommoncontext;
	}

	psRSData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_rscommoncontext:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


/*
	Static functions used by ray context code
*/

static
PVRSRV_ERROR _DestroySHContext(RGX_SERVER_RAY_SH_DATA *psSHData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  psSHData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_SHG,
											  PDUMP_FLAGS_NONE);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__FUNCTION__,
				PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */
	FWCommonContextFree(psSHData->psServerCommonContext);
	DevmemFwFree(psDeviceNode->pvDevice, psSHData->psContextStateMemDesc);
	psSHData->psContextStateMemDesc = NULL;
	psSHData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}

static
PVRSRV_ERROR _DestroyRSContext(RGX_SERVER_RAY_RS_DATA *psRSData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  psRSData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_RTU,
											  PDUMP_FLAGS_NONE);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				 __FUNCTION__,
				 PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */


	FWCommonContextFree(psRSData->psServerCommonContext);
	psRSData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}


/*
 * RPM driver management rev 2
 * 
 * The RPM freelists are opaque to the client driver. Scene Hierarchy pages
 * are managed in Blocks (analogous to PB blocks) which are alloc'd in KM
 * and mapped into the client MMU context.
 * 
 * Page tables are set up for each existing Scene Memory Block.
 * 
 * Freelist entries are also updated according to the list of Scene Memory Blocks.
 * 
 * NOTES:
 * 
 * (1) Scene Hierarchy shrink is not expected to be used.
 * (2) The RPM FreeLists are Circular buffers and must be contiguous in virtual space
 * (3) Each PMR is created with no phys backing pages. Pages are mapped in on-demand
 * via RGXGrowRPMFreeList.
 * 
 */
#if defined(DEBUG)
static PVRSRV_ERROR _ReadRPMFreePageList(PMR		 *psPMR,
										 IMG_DEVMEM_OFFSET_T uiLogicalOffset,
										 IMG_UINT32  ui32PageCount)
{
	PVRSRV_ERROR	eError;
	IMG_UINT32		uiIdx, j;
	size_t			uNumBytesCopied;
	RGX_RPM_DATA_RTU_FREE_PAGE_LIST		*psFreeListBuffer;
	IMG_UINT32		ui32PTI[4];

	/* Allocate scratch area for setting up Page table indices */
	psFreeListBuffer = OSAllocMem(ui32PageCount * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST));
    if (psFreeListBuffer == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_WriteRPMPageList: failed to allocate scratch page table"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	
	/* Read scratch buffer from PMR (FPL entries must be contiguous) */
	eError = PMR_ReadBytes(psPMR,
				 uiLogicalOffset,
				 (IMG_UINT8 *) psFreeListBuffer,
				 ui32PageCount * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST),
				 &uNumBytesCopied);

	if (eError == PVRSRV_OK)
	{
		for (uiIdx = 0; uiIdx < ui32PageCount; uiIdx +=4)
		{
			for (j=0; j<4; j++)
			{
				ui32PTI[j] = RGX_RPM_DATA_RTU_FREE_PAGE_LIST_GET_PTI(psFreeListBuffer[uiIdx + j]);
			}
			PVR_DPF((PVR_DBG_MESSAGE, "%4d:  %7d %7d %7d %7d", uiIdx,
					ui32PTI[0], ui32PTI[1], ui32PTI[2], ui32PTI[3]));
		}
	}

	/* Free scratch buffer */
	OSFreeMem(psFreeListBuffer);

	return eError;
}

static IMG_BOOL RGXDumpRPMFreeListPageList(RGX_RPM_FREELIST *psFreeList)
{
	PVR_LOG(("RPM Freelist FWAddr 0x%08x, ID = %d, CheckSum 0x%016llx",
				psFreeList->sFreeListFWDevVAddr.ui32Addr,
				psFreeList->ui32FreelistID,
				psFreeList->ui64FreelistChecksum));

	/* Dump FreeList page list */
	_ReadRPMFreePageList(psFreeList->psFreeListPMR, 0, psFreeList->ui32CurrentFLPages);

	return IMG_TRUE;
}
#endif

static PVRSRV_ERROR _UpdateFwRPMFreelistSize(RGX_RPM_FREELIST *psFreeList,
											 IMG_BOOL bGrow,
											 IMG_BOOL bRestartRPM,
											 IMG_UINT32 ui32DeltaSize)
{
	PVRSRV_ERROR			eError;
	RGXFWIF_KCCB_CMD		sGPCCBCmd;

	if(!bGrow)
	{
		PVR_DPF((PVR_DBG_ERROR, "_UpdateFwRPMFreelistSize: RPM freelist shrink not supported."));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* send feedback */
	sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_DOPPLER_MEMORY_GROW;
	sGPCCBCmd.uCmdData.sFreeListGSData.sFreeListFWDevVAddr.ui32Addr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32DeltaSize;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = 
		((bRestartRPM) ? RGX_FREELIST_GSDATA_RPM_RESTART_EN : 0) |
		psFreeList->ui32CurrentFLPages;

	PVR_DPF((PVR_DBG_MESSAGE, "Send FW update: RPM freelist [FWAddr=0x%08x] has 0x%08x pages",
								psFreeList->sFreeListFWDevVAddr.ui32Addr,
								psFreeList->ui32CurrentFLPages));

	/* Submit command to the firmware.  */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psFreeList->psDevInfo,
									RGXFWIF_DM_GP,
									&sGPCCBCmd,
									sizeof(sGPCCBCmd),
									0,
									PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_UpdateFwRPMFreelistSize: failed to update FW freelist size. (error = %u)", eError));
		return eError;
	}

	return PVRSRV_OK;
}

#if 0
static void _CheckRPMFreelist(RGX_RPM_FREELIST *psFreeList,
                   	   	   	   IMG_UINT32 ui32NumOfPagesToCheck,
                   	   	   	   IMG_UINT64 ui64ExpectedCheckSum,
                   	   	   	   IMG_UINT64 *pui64CalculatedCheckSum)
{
#if defined(NO_HARDWARE)
	/* No checksum needed as we have all information in the pdumps */
	PVR_UNREFERENCED_PARAMETER(psFreeList);
	PVR_UNREFERENCED_PARAMETER(ui32NumOfPagesToCheck);
	PVR_UNREFERENCED_PARAMETER(ui64ExpectedCheckSum);
	*pui64CalculatedCheckSum = 0;
#else
	PVRSRV_ERROR eError;
	size_t uiNumBytes;
    IMG_UINT8* pui8Buffer;
    IMG_UINT32* pui32Buffer;
    IMG_UINT32 ui32CheckSumAdd = 0;
    IMG_UINT32 ui32CheckSumXor = 0;
    IMG_UINT32 ui32Entry;
    IMG_UINT32 ui32Entry2;
    IMG_BOOL  bFreelistBad = IMG_FALSE;

	*pui64CalculatedCheckSum = 0;

	/* Allocate Buffer of the size of the freelist */
	pui8Buffer = OSAllocMem(psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32));
    if (pui8Buffer == NULL)
    {
		PVR_LOG(("_CheckRPMFreelist: Failed to allocate buffer to check freelist %p!", psFreeList));
		sleep_for_ever();
		//PVR_ASSERT(0);
        return;
    }

    /* Copy freelist content into Buffer */
    eError = PMR_ReadBytes(psFreeList->psFreeListPMR,
    				psFreeList->uiFreeListPMROffset + (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages) * sizeof(IMG_UINT32),
    				pui8Buffer,
    				psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32),
            		&uiNumBytes);
    if (eError != PVRSRV_OK)
    {
		OSFreeMem(pui8Buffer);
		PVR_LOG(("_CheckRPMFreelist: Failed to get freelist data for RPM freelist %p!", psFreeList));
		sleep_for_ever();
		//PVR_ASSERT(0);
        return;
    }

    PVR_ASSERT(uiNumBytes == psFreeList->ui32CurrentFLPages * sizeof(IMG_UINT32));
    PVR_ASSERT(ui32NumOfPagesToCheck <= psFreeList->ui32CurrentFLPages);

    /* Generate checksum */
    pui32Buffer = (IMG_UINT32 *)pui8Buffer;
    for(ui32Entry = 0; ui32Entry < ui32NumOfPagesToCheck; ui32Entry++)
    {
    	ui32CheckSumAdd += pui32Buffer[ui32Entry];
    	ui32CheckSumXor ^= pui32Buffer[ui32Entry];

    	/* Check for double entries */
    	for (ui32Entry2 = 0; ui32Entry2 < ui32NumOfPagesToCheck; ui32Entry2++)
    	{
			if ((ui32Entry != ui32Entry2) &&
				(pui32Buffer[ui32Entry] == pui32Buffer[ui32Entry2]))
			{
				PVR_LOG(("_CheckRPMFreelist: RPM Freelist consistency failure: FW addr: 0x%08X, Double entry found 0x%08x on idx: %d and %d of %d",
											psFreeList->sFreeListFWDevVAddr.ui32Addr,
											pui32Buffer[ui32Entry2],
											ui32Entry,
											ui32Entry2,
											psFreeList->ui32CurrentFLPages));
				bFreelistBad = IMG_TRUE;
			}
    	}
    }

    OSFreeMem(pui8Buffer);

	/* Check the calculated checksum against the expected checksum... */
	*pui64CalculatedCheckSum = ((IMG_UINT64)ui32CheckSumXor << 32) | ui32CheckSumAdd;

	if (ui64ExpectedCheckSum != 0  &&  ui64ExpectedCheckSum != *pui64CalculatedCheckSum)
	{
		PVR_LOG(("_CheckRPMFreelist: Checksum mismatch for RPM freelist %p!  Expected 0x%016llx calculated 0x%016llx",
		        psFreeList, ui64ExpectedCheckSum, *pui64CalculatedCheckSum));
		bFreelistBad = IMG_TRUE;
	}
    
    if (bFreelistBad)
    {
		PVR_LOG(("_CheckRPMFreelist: Sleeping for ever!"));
		sleep_for_ever();
//		PVR_ASSERT(!bFreelistBad);
	}
#endif
}
#endif

static PVRSRV_ERROR _WriteRPMFreePageList(PMR		 *psPMR,
										  IMG_DEVMEM_OFFSET_T uiLogicalOffset,
										  IMG_UINT32  ui32NextPageIndex,
										  IMG_UINT32  ui32PageCount)
{
	PVRSRV_ERROR	eError;
	IMG_UINT32		uiIdx;
	size_t		uNumBytesCopied;
	RGX_RPM_DATA_RTU_FREE_PAGE_LIST		*psFreeListBuffer;

	/* Allocate scratch area for setting up Page table indices */
	psFreeListBuffer = OSAllocMem(ui32PageCount * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST));
    if (psFreeListBuffer == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_WriteRPMPageList: failed to allocate scratch page table"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	
	for (uiIdx = 0; uiIdx < ui32PageCount; uiIdx ++, ui32NextPageIndex ++)
	{
		psFreeListBuffer[uiIdx].u32_0 = 0;
		RGX_RPM_DATA_RTU_FREE_PAGE_LIST_SET_PTI(psFreeListBuffer[uiIdx], ui32NextPageIndex);
	}
	
	/* Copy scratch buffer to PMR */
	eError = PMR_WriteBytes(psPMR,
				 uiLogicalOffset,
				 (IMG_UINT8 *) psFreeListBuffer,
				 ui32PageCount * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST),
				 &uNumBytesCopied);
	
	/* Free scratch buffer */
	OSFreeMem(psFreeListBuffer);

#if defined(PDUMP)
	/* Pdump the Page tables */
	PDUMPCOMMENT("Dump %u RPM free page list entries.", ui32PageCount);
	PMRPDumpLoadMem(psPMR,
					uiLogicalOffset,
					ui32PageCount * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST),
					PDUMP_FLAGS_CONTINUOUS,
					IMG_FALSE);
#endif
	return eError;
}


static RGX_RPM_FREELIST* FindRPMFreeList(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FreelistID)
{
	DLLIST_NODE *psNode, *psNext;
	RGX_RPM_FREELIST *psFreeList = NULL;

	OSLockAcquire(psDevInfo->hLockRPMFreeList);
	dllist_foreach_node(&psDevInfo->sRPMFreeListHead, psNode, psNext)
	{
		RGX_RPM_FREELIST *psThisFreeList = IMG_CONTAINER_OF(psNode, RGX_RPM_FREELIST, sNode);

		if (psThisFreeList->ui32FreelistID == ui32FreelistID)
		{
			psFreeList = psThisFreeList;
			break;
		}
	}
	OSLockRelease(psDevInfo->hLockRPMFreeList);
	
	return psFreeList;
}

void RGXProcessRequestRPMGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
							  IMG_UINT32 ui32FreelistID)
{
	RGX_RPM_FREELIST *psFreeList = NULL;
	RGXFWIF_KCCB_CMD sVRDMCCBCmd;
	IMG_UINT32 ui32GrowValue;
	PVRSRV_ERROR eError;
	IMG_BOOL bRestartRPM = IMG_TRUE; /* FIXME */

	PVR_ASSERT(psDevInfo);

	/* find the freelist with the corresponding ID */
	psFreeList = FindRPMFreeList(psDevInfo, ui32FreelistID);

	if (psFreeList)
	{
		/* Try to grow the freelist */
		eError = RGXGrowRPMFreeList(psFreeList,
									psFreeList->ui32GrowFLPages,
									&psFreeList->sMemoryBlockHead);
		if (eError == PVRSRV_OK)
		{
			/* Grow successful, return size of grow size */
			ui32GrowValue = psFreeList->ui32GrowFLPages;

			psFreeList->ui32NumGrowReqByFW++;

 #if defined(PVRSRV_ENABLE_PROCESS_STATS)
			/* Update Stats */
			PVRSRVStatsUpdateFreelistStats(0,
	                               1, /* Add 1 to the appropriate counter (Requests by FW) */
	                               psFreeList->ui32InitFLPages,
	                               psFreeList->ui32NumHighPages,
	                               psFreeList->ownerPid);

 #endif

		}
		else
		{
			/* Grow failed */
			ui32GrowValue = 0;
			PVR_DPF((PVR_DBG_ERROR,"Grow for FreeList %p [ID %d] failed (error %u)",
									psFreeList,
									psFreeList->ui32FreelistID,
									eError));
		}

		/* send feedback */
		sVRDMCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_DOPPLER_MEMORY_GROW;
		sVRDMCCBCmd.uCmdData.sFreeListGSData.sFreeListFWDevVAddr.ui32Addr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
		sVRDMCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32GrowValue;
		sVRDMCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = 
			((bRestartRPM) ? RGX_FREELIST_GSDATA_RPM_RESTART_EN : 0) |
			(psFreeList->ui32CurrentFLPages);

		PVR_DPF((PVR_DBG_ERROR,"Send feedback to RPM after grow on freelist [ID %d]", ui32FreelistID));
		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_SHG,
										&sVRDMCCBCmd,
										sizeof(sVRDMCCBCmd),
										0,
										PDUMP_FLAGS_NONE);
			if (eError != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
		/* Kernel CCB should never fill up, as the FW is processing them right away  */

		PVR_ASSERT(eError == PVRSRV_OK);
	}
	else
	{
		/* Should never happen */
		PVR_DPF((PVR_DBG_ERROR,"FreeList Lookup for FreeList ID 0x%08x failed (Populate)", ui32FreelistID));
		PVR_ASSERT(IMG_FALSE);
	}
}


/*!
 * RGXGrowRPMFreeList
 *
 * Allocate and map physical backing pages for RPM buffers
 * 
 * @param	ppsRPMDevMemDesc - RPM buffer descriptor representing new Scene memory block
 * 								and its associated RPM page table and free page list entries
 * @param	psRPMContext - RPM context
 * @param	psFreeList - RPM freelist descriptor
 * @param	ui32RequestNumPages - number of RPM pages to add to Doppler scene hierarchy
 * @param	pListHeader - linked list of RGX_RPM_DEVMEM_DESC blocks
 * 
 */
PVRSRV_ERROR RGXGrowRPMFreeList(RGX_RPM_FREELIST *psFreeList,
								IMG_UINT32 ui32RequestNumPages,
								PDLLIST_NODE pListHeader)
{
	PVRSRV_ERROR			eError;
	RGX_SERVER_RPM_CONTEXT	*psRPMContext = psFreeList->psParentCtx;
	RGX_RPM_DEVMEM_DESC		*psRPMDevMemDesc;
	IMG_DEVMEM_OFFSET_T		uiPMROffset;
	IMG_UINT32				ui32NextPageIndex;

	/* Are we allowed to grow ? */
	if (ui32RequestNumPages > psFreeList->psParentCtx->ui32UnallocatedPages)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: Scene Hierarchy buffer exceeded (0x%x pages required, 0x%x pages available).",
				ui32RequestNumPages, psFreeList->psParentCtx->ui32UnallocatedPages));
		return PVRSRV_ERROR_RPM_PBSIZE_ALREADY_MAX;
	}

	/* Allocate descriptor */
	psRPMDevMemDesc = OSAllocZMem(sizeof(*psRPMDevMemDesc));
    if (psRPMDevMemDesc == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: failed to allocate host data structure"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages
	 * - the context's ui32UnallocatedPages
	 */
	OSLockAcquire(psFreeList->psDevInfo->hLockRPMFreeList);
	OSLockAcquire(psFreeList->psDevInfo->hLockRPMContext);

	/* Update the sparse PMRs */
	psRPMDevMemDesc->psFreeList = psFreeList;
	psRPMDevMemDesc->ui32NumPages = ui32RequestNumPages;
	psRPMDevMemDesc->sSceneHierarchyNode.psPMR = psRPMContext->psSceneHierarchyPMR;
	psRPMDevMemDesc->sRPMPageListNode.psPMR = psRPMContext->psRPMPageTablePMR;
	psRPMDevMemDesc->sRPMFreeListNode.psPMR = psFreeList->psFreeListPMR;


	PVR_DPF((PVR_DBG_MESSAGE, "RGXGrowRPMFreeList: mapping %d pages for Doppler scene memory to VA 0x%llx with heap ID %p",
			ui32RequestNumPages, psRPMContext->sSceneMemoryBaseAddr.uiAddr, psRPMContext->psSceneHeap));

	/* 
	 * 1. Doppler scene hierarchy
	 */
	PDUMPCOMMENT("Allocate %d pages with mapping index %d for Doppler scene memory.",
				 ui32RequestNumPages,
				 psRPMContext->ui32SceneMemorySparseMappingIndex);
	eError = _RGXMapRPMPBBlock(&psRPMDevMemDesc->sSceneHierarchyNode,
					psFreeList,
					NODE_SCENE_HIERARCHY,
					psRPMContext->psSceneHeap,
					ui32RequestNumPages,
					psRPMContext->sSceneMemoryBaseAddr);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: Unable to map RPM scene hierarchy block (status %d)", eError));
		goto ErrorSceneBlock;
	}

	/* 
	 * 2. RPM page list
	 */
	if (ui32RequestNumPages > psRPMContext->ui32RPMEntriesInPage)
	{
		/* we need to map in phys pages for RPM page table */
		PDUMPCOMMENT("Allocate %d (%d requested) page table entries with mapping index %d for RPM page table.",
					 ui32RequestNumPages - psRPMContext->ui32RPMEntriesInPage,
					 ui32RequestNumPages,
					 psRPMContext->ui32RPMPageTableSparseMappingIndex);
		eError = _RGXMapRPMPBBlock(&psRPMDevMemDesc->sRPMPageListNode,
						psFreeList,
						NODE_RPM_PAGE_TABLE,
						psRPMContext->psRPMPageTableHeap,
						ui32RequestNumPages - psRPMContext->ui32RPMEntriesInPage,
						psRPMContext->sRPMPageTableBaseAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: Unable to map RPM page table block (status %d)", eError));
			goto ErrorPageTableBlock;
		}
	}

	/*
	 * 3. Free page list (FPL)
	 */
	if (ui32RequestNumPages > psFreeList->ui32EntriesInPage)
	{
		/* we need to map in phys pages for RPM free page list */
		PDUMPCOMMENT("Allocate %d (%d requested) FPL entries with mapping index %d for RPM free page list.",
					 ui32RequestNumPages - psFreeList->ui32EntriesInPage,
					 ui32RequestNumPages,
					 psFreeList->ui32RPMFreeListSparseMappingIndex);
		eError = _RGXMapRPMPBBlock(&psRPMDevMemDesc->sRPMFreeListNode,
						psFreeList,
						NODE_RPM_FREE_PAGE_LIST,
						psRPMContext->psRPMPageTableHeap,
						ui32RequestNumPages - psFreeList->ui32EntriesInPage,
						psFreeList->sBaseDevVAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: Unable to map RPM free page list (status %d)", eError));
			goto ErrorFreeListBlock;
		}
	}

	/*
	 * Update FPL entries
	 */

	/* Calculate doppler page index from base of Doppler heap */
	ui32NextPageIndex = (psRPMDevMemDesc->sSceneHierarchyNode.sAddr.uiAddr -
		psRPMContext->sDopplerHeapBaseAddr.uiAddr) >> psFreeList->uiLog2DopplerPageSize;

	/* Calculate write offset into FPL PMR assuming pages are mapped in order with no gaps */
	uiPMROffset = (size_t)psFreeList->ui32CurrentFLPages * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST);

	eError = _WriteRPMFreePageList(psFreeList->psFreeListPMR, uiPMROffset, ui32NextPageIndex, ui32RequestNumPages);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowRPMFreeList: error writing RPM free list entries (%d)", eError));
		goto ErrorFreeListWriteEntries;
	}

	{
		/* 
		 * Update the entries remaining in the last mapped RPM and FPL pages.
		 * 
		 * psRPMDevMemDesc->sRPMPageListNode.ui32NumPhysPages * 1024 entries are added (can be zero)
		 * ui32RequestNumPages entries are committed
		 * 
		 * The number of entries remaining should always be less than a full page.
		 */
		IMG_UINT32	ui32PTEntriesPerChunk = OSGetPageSize() / sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST);
		IMG_UINT32	ui32PTEntriesPerChunkClearMask = ~(ui32PTEntriesPerChunk - 1);

		psRPMContext->ui32RPMEntriesInPage = psRPMContext->ui32RPMEntriesInPage +
			(psRPMDevMemDesc->sRPMPageListNode.ui32NumPhysPages * ui32PTEntriesPerChunk) - ui32RequestNumPages;
		PVR_ASSERT((psRPMContext->ui32RPMEntriesInPage & ui32PTEntriesPerChunkClearMask) == 0);

		psFreeList->ui32EntriesInPage = psFreeList->ui32EntriesInPage +
			(psRPMDevMemDesc->sRPMFreeListNode.ui32NumPhysPages * ui32PTEntriesPerChunk) - ui32RequestNumPages;
		PVR_ASSERT((psFreeList->ui32EntriesInPage & ui32PTEntriesPerChunkClearMask) == 0);
	}

	/* Add node to link list */
	dllist_add_to_head(pListHeader, &psRPMDevMemDesc->sMemoryDescBlock);

	/* Update number of available pages */
	psFreeList->ui32CurrentFLPages += ui32RequestNumPages;
	psRPMContext->ui32UnallocatedPages -= ui32RequestNumPages;

#if defined(DEBUG)
	RGXDumpRPMFreeListPageList(psFreeList);
#endif

	OSLockRelease(psFreeList->psDevInfo->hLockRPMContext);
	OSLockRelease(psFreeList->psDevInfo->hLockRPMFreeList);

	PVR_DPF((PVR_DBG_MESSAGE,"RPM Freelist [%p, ID %d]: grow by %u pages (current pages %u/%u, unallocated pages %u)",
			psFreeList,
			psFreeList->ui32FreelistID,
			ui32RequestNumPages,
			psFreeList->ui32CurrentFLPages,
			psRPMContext->ui32TotalRPMPages,
			psRPMContext->ui32UnallocatedPages));

	return PVRSRV_OK;

	/* Error handling */
ErrorFreeListWriteEntries:
	/* TODO: unmap sparse block for RPM FPL */
ErrorFreeListBlock:
	/* TODO: unmap sparse block for RPM page table */
ErrorPageTableBlock:
	/* TODO: unmap sparse block for scene hierarchy */

ErrorSceneBlock:	
	OSLockRelease(psFreeList->psDevInfo->hLockRPMContext);
	OSLockRelease(psFreeList->psDevInfo->hLockRPMFreeList);
	OSFreeMem(psRPMDevMemDesc);

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR RGXShrinkRPMFreeList(PDLLIST_NODE pListHeader,
										 RGX_RPM_FREELIST *psFreeList)
{
	DLLIST_NODE *psNode;
	RGX_RPM_DEVMEM_DESC	*psRPMDevMemNode;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32OldValue;

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages value
	 */
	PVR_ASSERT(pListHeader);
	PVR_ASSERT(psFreeList);
	PVR_ASSERT(psFreeList->psDevInfo);
	PVR_ASSERT(psFreeList->psDevInfo->hLockRPMFreeList);

	OSLockAcquire(psFreeList->psDevInfo->hLockRPMFreeList);

	/********************************************************************
	 * All scene memory blocks must be freed together as non-contiguous
	 * virtual mappings are not yet supported.
	 ********************************************************************/

	/* Get node from head of list and remove it */
	psNode = dllist_get_next_node(pListHeader);
	PVR_DPF((PVR_DBG_MESSAGE, "Found node %p", psNode));
	if (psNode)
	{
		dllist_remove_node(psNode);

		psRPMDevMemNode = IMG_CONTAINER_OF(psNode, RGX_RPM_DEVMEM_DESC, sMemoryDescBlock);
		PVR_ASSERT(psRPMDevMemNode);
		PVR_ASSERT(psRPMDevMemNode->psFreeList);
		PVR_ASSERT(psRPMDevMemNode->sSceneHierarchyNode.psPMR);

		/* remove scene hierarchy block */
		PVR_DPF((PVR_DBG_MESSAGE, "Removing scene hierarchy node"));
		eError = _RGXUnmapRPMPBBlock(&psRPMDevMemNode->sSceneHierarchyNode,
									 psRPMDevMemNode->psFreeList,
									 psFreeList->psParentCtx->sSceneMemoryBaseAddr);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "RGXShrinkRPMFreeList: Failed to unmap %d pages with mapping index %d (status %d)",
					psRPMDevMemNode->sSceneHierarchyNode.ui32NumPhysPages,
					psRPMDevMemNode->sSceneHierarchyNode.ui32StartOfMappingIndex,
					eError));
			goto UnMapError;
		}

		/* 
		 * If the grow size is sub OS page size then the page lists may not need updating 
		 */
		if (psRPMDevMemNode->sRPMPageListNode.eNodeType != NODE_EMPTY)
		{
			/* unmap the RPM page table backing pages */
			PVR_DPF((PVR_DBG_MESSAGE, "Removing RPM page list node"));
			PVR_ASSERT(psRPMDevMemNode->sRPMPageListNode.psPMR);
			eError = _RGXUnmapRPMPBBlock(&psRPMDevMemNode->sRPMPageListNode,
										 psRPMDevMemNode->psFreeList,
										 psFreeList->psParentCtx->sRPMPageTableBaseAddr);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXShrinkRPMFreeList: Failed to unmap %d pages with mapping index %d (status %d)",
						psRPMDevMemNode->sRPMPageListNode.ui32NumPhysPages,
						psRPMDevMemNode->sRPMPageListNode.ui32StartOfMappingIndex,
						eError));
				goto UnMapError;
			}
		}

		if (psRPMDevMemNode->sRPMFreeListNode.eNodeType != NODE_EMPTY)
		{
			/* unmap the RPM free page list backing pages */
			PVR_DPF((PVR_DBG_MESSAGE, "Removing RPM free list node"));
			PVR_ASSERT(psRPMDevMemNode->sRPMFreeListNode.psPMR);
			eError = _RGXUnmapRPMPBBlock(&psRPMDevMemNode->sRPMFreeListNode,
										 psRPMDevMemNode->psFreeList,
										 psFreeList->sBaseDevVAddr);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "RGXShrinkRPMFreeList: Failed to unmap %d pages with mapping index %d (status %d)",
						psRPMDevMemNode->sRPMFreeListNode.ui32NumPhysPages,
						psRPMDevMemNode->sRPMFreeListNode.ui32StartOfMappingIndex,
						eError));
				goto UnMapError;
			}
		}

		/* update available RPM pages in freelist (NOTE: may be different from phys page count) */
		ui32OldValue = psFreeList->ui32CurrentFLPages;
		psFreeList->ui32CurrentFLPages -= psRPMDevMemNode->ui32NumPages;

		/* check underflow */
		PVR_ASSERT(ui32OldValue > psFreeList->ui32CurrentFLPages);

		PVR_DPF((PVR_DBG_MESSAGE, "Freelist [%p, ID %d]: shrink by %u pages (current pages %u/%u)",
								psFreeList,
								psFreeList->ui32FreelistID,
								psRPMDevMemNode->ui32NumPages,
								psFreeList->ui32CurrentFLPages,
								psFreeList->psParentCtx->ui32UnallocatedPages));

		OSFreeMem(psRPMDevMemNode);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING,"Freelist [0x%p]: shrink denied. PB already at zero PB size (%u pages)",
								psFreeList,
								psFreeList->ui32CurrentFLPages));
		eError = PVRSRV_ERROR_PBSIZE_ALREADY_MIN;
	}

	OSLockRelease(psFreeList->psDevInfo->hLockRPMFreeList);
	return PVRSRV_OK;

UnMapError:
	OSFreeMem(psRPMDevMemNode);
	OSLockRelease(psFreeList->psDevInfo->hLockRPMFreeList);

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*!
 *	_RGXCreateRPMSparsePMR
 * 
 * Creates a PMR container with no phys pages initially. Phys pages will be allocated
 * and mapped later when requested by client or by HW RPM Out of Memory event.
 * The PMR is created with zero phys backing pages.
 * The sparse PMR is associated to either the RPM context or to the RPM freelist(s):
 * 
 * RGX_SERVER_RPM_CONTEXT - Scene hierarchy, page table
 * RGX_RPM_FREELIST - free page list PMR
 * 
 * @param	eBlockType - whether block is for scene hierarchy pages or page
 * 				tables. This parameter is used to calculate size.
 * @param	ui32NumPages - total number of pages
 * @param	uiLog2DopplerPageSize - log2 Doppler/RPM page size
 * @param	ppsPMR - (Output) new PMR container.
 * 
 * See the documentation for more details.
 */
static
PVRSRV_ERROR _RGXCreateRPMSparsePMR(CONNECTION_DATA *psConnection,
									PVRSRV_DEVICE_NODE	 *psDeviceNode,
									RGX_DEVMEM_NODE_TYPE eBlockType,
									IMG_UINT32		ui32NumPages,
									IMG_UINT32		uiLog2DopplerPageSize,
									PMR				**ppsPMR)
{
	PVRSRV_ERROR		eError;
	IMG_DEVMEM_SIZE_T	uiMaxSize = 0;
	IMG_UINT32			ui32NumVirtPages = 0; /*!< number of virtual pages to cover virtual range */
	IMG_UINT32			ui32Log2OSPageSize = OSGetPageShift();
	IMG_UINT32			ui32ChunkSize = OSGetPageSize();
	PVRSRV_MEMALLOCFLAGS_T uiCustomFlags = 0;

	/* Work out the allocation logical size = virtual size */
	switch(eBlockType)
	{
		case NODE_EMPTY:
			PVR_ASSERT(IMG_FALSE);
			return PVRSRV_ERROR_INVALID_PARAMS;
		case NODE_SCENE_HIERARCHY:
			PDUMPCOMMENT("Allocate Scene Hierarchy PMR (Pages %08X)", ui32NumPages);
			uiMaxSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * (1 << uiLog2DopplerPageSize);
			break;
		case NODE_RPM_PAGE_TABLE:
			PDUMPCOMMENT("Allocate RPM Page Table PMR (Page entries %08X)", ui32NumPages);
			uiMaxSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * sizeof(RGX_RPM_DATA_RTU_PAGE_TABLE);
			break;
		case NODE_RPM_FREE_PAGE_LIST:
			/* 
			 * Each RPM free page list (FPL) supports the maximum range.
			 * In practise the maximum range is divided between allocations in each FPL
			 */
			PDUMPCOMMENT("Allocate RPM Free Page List PMR (Page entries %08X)", ui32NumPages);
			uiMaxSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST);
			uiCustomFlags |= PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE; /*(PVRSRV_MEMALLOCFLAG_CPU_READABLE | PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE | PVRSRV_MEMALLOCFLAG_CPU_UNCACHED); */
			break;
		/* no default case because the build should error out if a case is unhandled */
	}

	uiMaxSize = (uiMaxSize + ui32ChunkSize - 1) & ~(ui32ChunkSize - 1);
	ui32NumVirtPages = uiMaxSize >> ui32Log2OSPageSize;

	eError = PhysmemNewRamBackedPMR(psConnection,
									psDeviceNode,
									uiMaxSize, /* the maximum size which should match num virtual pages * page size */
									ui32ChunkSize,
									0,
									ui32NumVirtPages,
									NULL,
									ui32Log2OSPageSize,
									(PVRSRV_MEMALLOCFLAG_GPU_READABLE | PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE | PVRSRV_MEMALLOCFLAG_SPARSE_NO_DUMMY_BACKING | uiCustomFlags),
									strlen("RPM Buffer") + 1,
									"RPM Buffer",
									ppsPMR);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "_RGXCreateRPMSparsePMR: Failed to allocate sparse PMR of size: 0x%016llX",
				 (IMG_UINT64)uiMaxSize));
	}
	
	return eError;
}

/*!
 *	_RGXMapRPMPBBlock
 * 
 * Maps in a block of phys pages for one of the following:
 * 
 * NODE_SCENE_HIERARCHY - scene hierarchy
 * NODE_RPM_PAGE_TABLE - RPM page table entries
 * NODE_RPM_FREE_PAGE_LIST - RPM free page list entries
 * 
 * @param	psDevMemNode - device mem block descriptor (allocated by caller)
 * @param	psFreeList - free list descriptor
 * @param	eBlockType - block type: scene memory, RPM page table or RPM page free list
 * @param	psDevmemHeap - heap for GPU virtual mapping
 * @param	ui32NumPages - number of pages for scene memory, OR
 * 							number of PT entries for RPM page table or page free list
 * @param	sDevVAddrBase - GPU virtual base address i.e. base address at start of sparse allocation
 * 
 * @return	PVRSRV_OK if no error occurred
 */
static
PVRSRV_ERROR _RGXMapRPMPBBlock(RGX_DEVMEM_NODE	*psDevMemNode,
					RGX_RPM_FREELIST *psFreeList,
					RGX_DEVMEM_NODE_TYPE eBlockType,
					DEVMEMINT_HEAP *psDevmemHeap,
					IMG_UINT32 ui32NumPages,
					IMG_DEV_VIRTADDR sDevVAddrBase)
{
	PVRSRV_ERROR	eError;
    IMG_UINT64 		sCpuVAddrNULL = 0; 			/* no CPU mapping needed */
	IMG_UINT32		*paui32AllocPageIndices;	/* table of virtual indices for sparse mapping */
	IMG_PUINT32 	pui32MappingIndex = NULL;	/* virtual index where next physical chunk is mapped */
	IMG_UINT32		i;
	size_t			uiSize = 0;
	IMG_UINT32		ui32Log2OSPageSize = OSGetPageShift();
	IMG_UINT32		ui32ChunkSize = OSGetPageSize();
	IMG_UINT32		ui32NumPhysPages = 0; /*!< number of physical pages for data pages or RPM PTs */
	PVRSRV_MEMALLOCFLAGS_T uiCustomFlags = 0;


	/* Allocate Memory Block for scene hierarchy */
	switch(eBlockType)
	{
		case NODE_EMPTY:
			PVR_ASSERT(IMG_FALSE);
			return PVRSRV_ERROR_INVALID_PARAMS;
		case NODE_SCENE_HIERARCHY:
			PDUMPCOMMENT("Allocate Scene Hierarchy Block (Pages %08X)", ui32NumPages);
			uiSize = (size_t)ui32NumPages * (1 << psFreeList->psParentCtx->uiLog2DopplerPageSize);
			pui32MappingIndex = &psFreeList->psParentCtx->ui32SceneMemorySparseMappingIndex;
			break;
		case NODE_RPM_PAGE_TABLE:
			PDUMPCOMMENT("Allocate RPM Page Table Block (Page entries %08X)", ui32NumPages);
			uiSize = (size_t)ui32NumPages * sizeof(RGX_RPM_DATA_RTU_PAGE_TABLE);
			pui32MappingIndex = &psFreeList->psParentCtx->ui32RPMPageTableSparseMappingIndex;
			break;
		case NODE_RPM_FREE_PAGE_LIST:
			PDUMPCOMMENT("Allocate RPM Free Page List Block (Page entries %08X)", ui32NumPages);
			uiSize = (size_t)ui32NumPages * sizeof(RGX_RPM_DATA_RTU_FREE_PAGE_LIST);
			pui32MappingIndex = &psFreeList->ui32RPMFreeListSparseMappingIndex;
			uiCustomFlags |= PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE; /*(PVRSRV_MEMALLOCFLAG_CPU_READABLE | PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE);*/
			break;
		/* no default case because the build should error out if a case is unhandled */
	}

	/* 
	 * Round size up to multiple of the sparse chunk size = OS page size.
	 */
	uiSize = (uiSize + ui32ChunkSize - 1) & ~(ui32ChunkSize - 1);
	ui32NumPhysPages = uiSize >> ui32Log2OSPageSize;

	paui32AllocPageIndices = OSAllocMem(ui32NumPhysPages * sizeof(IMG_UINT32));
    if (paui32AllocPageIndices == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_RGXCreateRPMPBBlockSparse: failed to allocate sparse mapping index list"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}
	for(i=0; i<ui32NumPhysPages; i++)
	{
		paui32AllocPageIndices[i] = *pui32MappingIndex + i;
	}

	/* Set up some state */
	psDevMemNode->eNodeType = eBlockType;
	psDevMemNode->psDevMemHeap = psDevmemHeap;
	if (eBlockType == NODE_SCENE_HIERARCHY)
	{
		/* the mapped-in scene hierarchy device address will be used to set up the FPL entries */
		psDevMemNode->sAddr.uiAddr = sDevVAddrBase.uiAddr + (*pui32MappingIndex * ui32ChunkSize);
	}
	psDevMemNode->ui32NumPhysPages = ui32NumPhysPages;
	psDevMemNode->ui32StartOfMappingIndex = *pui32MappingIndex;

	{
		if ((eBlockType == NODE_SCENE_HIERARCHY) &&
			(ui32NumPhysPages > psFreeList->psParentCtx->ui32UnallocatedPages))
		{
			PVR_DPF((PVR_DBG_ERROR, "_RGXCreateRPMPBBlockSparse: virtual address space exceeded (0x%x pages required, 0x%x pages available).",
					ui32NumPhysPages, psFreeList->psParentCtx->ui32UnallocatedPages));
			OSFreeMem(paui32AllocPageIndices);
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		eError = PMRLockSysPhysAddresses(psDevMemNode->psPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_RGXCreateRPMPBBlockSparse: unable to lock PMR physical pages (status %d)", eError));
			goto ErrorLockPhys;
		}

		eError = DevmemIntChangeSparse(psDevmemHeap,
						psDevMemNode->psPMR,
						ui32NumPhysPages,
						paui32AllocPageIndices,
						0,
						NULL,
						SPARSE_RESIZE_ALLOC,
						(PVRSRV_MEMALLOCFLAG_GPU_READABLE | PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE | uiCustomFlags),
						sDevVAddrBase,
						sCpuVAddrNULL);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_RGXCreateRPMPBBlockSparse: change sparse mapping failed with %d pages starting at %d (status %d)",
					ui32NumPhysPages, *pui32MappingIndex, eError));
			goto ErrorSparseMapping;
		}

		/* FIXME: leave locked until destroy */
		PMRUnlockSysPhysAddresses(psDevMemNode->psPMR);
	}

	/* 
	 * Update the mapping index for the next allocation.
	 * The virtual pages should be contiguous.
	 */
	*pui32MappingIndex += ui32NumPhysPages;

	OSFreeMem(paui32AllocPageIndices);

	return PVRSRV_OK;

ErrorSparseMapping:
	PMRUnlockSysPhysAddresses(psDevMemNode->psPMR);

ErrorLockPhys:
	OSFreeMem(paui32AllocPageIndices);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*!
 * _RGXUnmapRPMPBBlock
 * 
 * NOTE: because the SHF and SHG requests for memory are interleaved, the
 * page mapping offset cannot be updated (non-contiguous virtual mapping
 * is not supported).
 * 
 * So either
 *  (i) the allocated virtual address range is unusable after unmap
 * (ii) all of the scene memory must be freed
 * 
 * @param	psDevMemNode - block to free
 * @param	psFreeList - RPM free list
 * @param	sDevVAddrBase - the virtual base address (i.e. where page 1 of the PMR is mapped)
 */
static
PVRSRV_ERROR _RGXUnmapRPMPBBlock(RGX_DEVMEM_NODE	*psDevMemNode,
					RGX_RPM_FREELIST *psFreeList,
					IMG_DEV_VIRTADDR sDevVAddrBase)
{
	PVRSRV_ERROR	eError;
	IMG_UINT64 		sCpuVAddrNULL = 0; 			/* no CPU mapping needed */
	IMG_UINT32		*paui32FreePageIndices;		/* table of virtual indices for sparse unmapping */
	IMG_UINT32		i;
	IMG_UINT32		ui32NumPhysPages = psDevMemNode->ui32NumPhysPages; /*!< number of physical pages for data pages or RPM PTs */

#if defined(PDUMP)
	/* Free Memory Block for scene hierarchy */
	switch(psDevMemNode->eNodeType)
	{
		case NODE_EMPTY:
			PVR_ASSERT(IMG_FALSE);
			return PVRSRV_ERROR_INVALID_PARAMS;
		case NODE_SCENE_HIERARCHY:
			PDUMPCOMMENT("Free Scene Hierarchy Block (Pages %08X)", ui32NumPhysPages);
			break;
		case NODE_RPM_PAGE_TABLE:
			PDUMPCOMMENT("Free RPM Page Table Block (Page entries %08X)", ui32NumPhysPages);
			break;
		case NODE_RPM_FREE_PAGE_LIST:
			PDUMPCOMMENT("Free RPM Free Page List Block (Page entries %08X)", ui32NumPhysPages);
			break;
		/* no default case because the build should error out if a case is unhandled */
	}
#endif

	paui32FreePageIndices = OSAllocMem(ui32NumPhysPages * sizeof(IMG_UINT32));
    if (paui32FreePageIndices == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_RGXUnmapRPMPBBlock: failed to allocate sparse mapping index list"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}
	for(i=0; i<ui32NumPhysPages; i++)
	{
		paui32FreePageIndices[i] = psDevMemNode->ui32StartOfMappingIndex + i;
	}

	{
		eError = PMRLockSysPhysAddresses(psDevMemNode->psPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_RGXUnmapRPMPBBlock: unable to lock PMR physical pages (status %d)", eError));
			goto ErrorLockPhys;
		}

		eError = DevmemIntChangeSparse(psDevMemNode->psDevMemHeap,
						psDevMemNode->psPMR,
						0, /* no pages are mapped here */
						NULL,
						ui32NumPhysPages,
						paui32FreePageIndices,
						SPARSE_RESIZE_FREE,
						(PVRSRV_MEMALLOCFLAG_GPU_READABLE | PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE),
						sDevVAddrBase,
						sCpuVAddrNULL);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "_RGXUnmapRPMPBBlock: free sparse mapping failed with %d pages starting at %d (status %d)",
					ui32NumPhysPages, psDevMemNode->ui32StartOfMappingIndex, eError));
			goto ErrorSparseMapping;
		}

		PMRUnlockSysPhysAddresses(psDevMemNode->psPMR);
	}

	OSFreeMem(paui32FreePageIndices);

	return PVRSRV_OK;

ErrorSparseMapping:
	PMRUnlockSysPhysAddresses(psDevMemNode->psPMR);

ErrorLockPhys:
	OSFreeMem(paui32FreePageIndices);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*!
 *	RGXCreateRPMFreeList
 * 
 * @param	ui32InitFLPages - initial allocation of mapped-in physical pages
 * @param	ui32GrowFLPages - physical pages to add to scene hierarchy if RPM OOM occurs
 * @param	sFreeListDevVAddr - virtual base address of free list
 * @param	sRPMPageListDevVAddr (DEPRECATED -- cached in RPM Context)
 * @param	ui32FLSyncAddr (DEPRECATED)
 * @param	ppsFreeList - returns a RPM freelist handle to client
 * @param	puiHWFreeList - 'handle' to FW freelist, passed in VRDM kick (FIXME)
 * @param	bIsExternal - flag which marks if the freelist is an external one
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
							   IMG_BOOL				bIsExternal)
{
	PVRSRV_ERROR				eError;
	RGXFWIF_RPM_FREELIST		*psFWRPMFreeList;
	DEVMEM_MEMDESC				*psFWRPMFreelistMemDesc;
	RGX_RPM_FREELIST			*psFreeList;
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

	/* Allocate kernel freelist struct */
	psFreeList = OSAllocZMem(sizeof(*psFreeList));
    if (psFreeList == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMFreeList: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psFreeList->psCleanupSync,
						   "RPM free list cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateRPMFreeList: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto ErrorSyncAlloc;
	}

	/*
	 * This FW FreeList context is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 * 
	 * TODO - RPM freelist will be modified after creation, but only from host-side.
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFWRPMFreeList),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE,
							"FwRPMFreeList",
							&psFWRPMFreelistMemDesc);
	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMFreeList: DevmemAllocate for RGXFWIF_FREELIST failed"));
		goto ErrorFWFreeListAlloc;
	}

	/* Initialise host data structures */
	psFreeList->psConnection = psConnection;
	psFreeList->psDevInfo = psDevInfo;
	psFreeList->psParentCtx = psRPMContext;
	psFreeList->psFWFreelistMemDesc = psFWRPMFreelistMemDesc;
	psFreeList->sBaseDevVAddr = sFreeListDevVAddr;
	RGXSetFirmwareAddress(&psFreeList->sFreeListFWDevVAddr, psFWRPMFreelistMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	psFreeList->ui32FreelistID = psDevInfo->ui32RPMFreelistCurrID++;
	//psFreeList->ui32MaxFLPages = ui32MaxFLPages;
	/* TODO: is it really needed? */
	if(bIsExternal == IMG_FALSE)
	{
		psFreeList->ui32InitFLPages = ui32InitFLPages;
		psFreeList->ui32GrowFLPages = ui32GrowFLPages;
	}
	//psFreeList->ui32CurrentFLPages = ui32InitFLPages;
	psFreeList->ui32RefCount = 0;
	dllist_init(&psFreeList->sMemoryBlockHead);

	/* Wizard2 -- support per-freelist Doppler virtual page size */
	psFreeList->uiLog2DopplerPageSize = psRPMContext->uiLog2DopplerPageSize;

	/* Initialise FW data structure */
	eError = DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (void **)&psFWRPMFreeList);
	PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", ErrorFWFreeListCpuMap);

	/*
	 * FIXME - the max pages are shared with the other freelists so this
	 * over-estimates the number of free pages. The full check is
	 * implemented in RGXGrowRPMFreeList.
	 */
	if(bIsExternal == IMG_TRUE)
	{
		/* An external RPM FreeList will never grow */
		psFWRPMFreeList->ui32MaxPages = ui32InitFLPages;
	}
	else
	{
		psFWRPMFreeList->ui32MaxPages = psFreeList->psParentCtx->ui32TotalRPMPages;
	}
	psFWRPMFreeList->ui32CurrentPages = ui32InitFLPages;
	psFWRPMFreeList->ui32GrowPages = ui32GrowFLPages;
	psFWRPMFreeList->ui32ReadOffset = 0;
	psFWRPMFreeList->ui32WriteOffset = RGX_CR_RPM_SHG_FPL_WRITE_TOGGLE_EN; /* FL is full */
	psFWRPMFreeList->bReadToggle = IMG_FALSE;
	psFWRPMFreeList->bWriteToggle = IMG_TRUE;
	psFWRPMFreeList->sFreeListDevVAddr.uiAddr = sFreeListDevVAddr.uiAddr;
	psFWRPMFreeList->ui32FreeListID = psFreeList->ui32FreelistID;
	psFWRPMFreeList->bGrowPending = IMG_FALSE;

	PVR_DPF((PVR_DBG_MESSAGE, "RPM Freelist %p created: FW freelist: %p, Init pages 0x%08x, Max FL base address " IMG_DEVMEM_SIZE_FMTSPEC ", Init FL base address " IMG_DEVMEM_SIZE_FMTSPEC,
			psFreeList,
			psFWRPMFreeList,
			ui32InitFLPages,
			sFreeListDevVAddr.uiAddr,
			psFWRPMFreeList->sFreeListDevVAddr.uiAddr));

	PVR_DPF((PVR_DBG_MESSAGE,"RPM FW Freelist %p created: sync FW addr 0x%08x", psFWRPMFreeList, psFWRPMFreeList->sSyncAddr));

	PDUMPCOMMENT("Dump FW RPM FreeList");
	DevmemPDumpLoadMem(psFreeList->psFWFreelistMemDesc, 0, sizeof(*psFWRPMFreeList), PDUMP_FLAGS_CONTINUOUS);

	/*
	 * Separate dump of the Freelist's number of Pages and stack pointer.
	 * This allows to easily modify the PB size in the out2.txt files.
	 */
	PDUMPCOMMENT("RPM FreeList TotalPages");
	DevmemPDumpLoadMemValue32(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_RPM_FREELIST, ui32CurrentPages),
							psFWRPMFreeList->ui32CurrentPages,
							PDUMP_FLAGS_CONTINUOUS);

	PDUMPCOMMENT("RPM FreeList device virtual base address");
	DevmemPDumpLoadMemValue64(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_RPM_FREELIST, sFreeListDevVAddr),
							psFWRPMFreeList->sFreeListDevVAddr.uiAddr,
							PDUMP_FLAGS_CONTINUOUS);

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);

	if (bIsExternal == IMG_TRUE)
	{
		/* Mark the freelist as an external */
		psFreeList->bIsExternal = IMG_TRUE;

		/* In case of an external RPM FreeList it is not needed to:
		 * 		- create sparse PMR
		 * 		- allocate physical memory for the freelist
		 * 		- add it to the list of freelist
		 */

		/* return values */
		*puiHWFreeList = psFreeList->sFreeListFWDevVAddr.ui32Addr;
		*ppsFreeList = psFreeList;

		return PVRSRV_OK;
	}

	psFreeList->bIsExternal = IMG_FALSE;

	/*
	 * Create the sparse PMR for the RPM free page list
	 */
	eError = _RGXCreateRPMSparsePMR(psConnection, psDeviceNode,
									NODE_RPM_FREE_PAGE_LIST,
									psRPMContext->ui32TotalRPMPages,
									psRPMContext->uiLog2DopplerPageSize,
									&psFreeList->psFreeListPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMContext: failed to allocate PMR for RPM Free page list (%d)", eError));
		goto ErrorSparsePMR;
	}

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages
	 */
	/* Add to list of freelists */
	OSLockAcquire(psDevInfo->hLockRPMFreeList);
	psFreeList->psParentCtx->uiFLRefCount++;
	dllist_add_to_tail(&psDevInfo->sRPMFreeListHead, &psFreeList->sNode);
	OSLockRelease(psDevInfo->hLockRPMFreeList);

	/*
	 * Add initial scene hierarchy block
	 * Allocate phys memory for scene hierarchy, free page list and RPM page-in-use list
	 */
	eError = RGXGrowRPMFreeList(psFreeList, ui32InitFLPages, &psFreeList->sMemoryBlockHead);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMFreeList: error during phys memory allocation and mapping (%d)", eError));
		goto ErrorGrowFreeList;
	}

	/* return values */
	*puiHWFreeList = psFreeList->sFreeListFWDevVAddr.ui32Addr;
	*ppsFreeList = psFreeList;

	return PVRSRV_OK;

	/* Error handling */
ErrorGrowFreeList:
	/* Remove freelists from list  */
	OSLockAcquire(psDevInfo->hLockRPMFreeList);
	dllist_remove_node(&psFreeList->sNode);
	psFreeList->psParentCtx->uiFLRefCount--;
	OSLockRelease(psDevInfo->hLockRPMFreeList);

ErrorSparsePMR:
	SyncPrimFree(psFreeList->psCleanupSync);

ErrorFWFreeListCpuMap:
	RGXUnsetFirmwareAddress(psFWRPMFreelistMemDesc);
	DevmemFwFree(psDevInfo, psFWRPMFreelistMemDesc);

ErrorFWFreeListAlloc:
	PMRUnrefPMR(psFreeList->psFreeListPMR);

ErrorSyncAlloc:
	OSFreeMem(psFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

/*
 *	RGXDestroyRPMFreeList
 */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRPMFreeList(RGX_RPM_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;
	//IMG_UINT64 ui64CheckSum;

	PVR_ASSERT(psFreeList);

	if(psFreeList->ui32RefCount != 0 && psFreeList->bIsExternal == IMG_FALSE)
	{
		/* Freelist still busy */
		PVR_DPF((PVR_DBG_WARNING, "Freelist %p is busy", psFreeList));
		return PVRSRV_ERROR_RETRY;
	}

	/* Freelist is not in use => start firmware cleanup */
	eError = RGXFWRequestRPMFreeListCleanUp(psFreeList->psDevInfo,
											psFreeList->sFreeListFWDevVAddr,
											psFreeList->psCleanupSync);
	if(eError != PVRSRV_OK)
	{
		/* Can happen if the firmware took too long to handle the cleanup request,
		 * or if SLC-flushes didn't went through (due to some GPU lockup) */
		return eError;
	}

	/* update the statistics */
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsUpdateFreelistStats(psFreeList->ui32NumGrowReqByApp,
	                               psFreeList->ui32NumGrowReqByFW,
	                               psFreeList->ui32InitFLPages,
	                               psFreeList->ui32NumHighPages,
	                               0); /* FIXME - owner PID */
#endif

	/* Destroy FW structures */
	RGXUnsetFirmwareAddress(psFreeList->psFWFreelistMemDesc);
	DevmemFwFree(psFreeList->psDevInfo, psFreeList->psFWFreelistMemDesc);

	if(psFreeList->bIsExternal == IMG_FALSE)
	{
		/* Free the phys mem block descriptors. */
		PVR_DPF((PVR_DBG_WARNING, "Cleaning RPM freelist index %d", psFreeList->ui32FreelistID));
		while (!dllist_is_empty(&psFreeList->sMemoryBlockHead))
		{
			eError = RGXShrinkRPMFreeList(&psFreeList->sMemoryBlockHead, psFreeList);
			PVR_ASSERT(eError == PVRSRV_OK);
		}
		psFreeList->psParentCtx->uiFLRefCount--;

		/* consistency checks */
		PVR_ASSERT(dllist_is_empty(&psFreeList->sMemoryBlockHead));
		PVR_ASSERT(psFreeList->ui32CurrentFLPages == 0);

		/* Free RPM Free page list PMR */
		eError = PMRUnrefPMR(psFreeList->psFreeListPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXDestroyRPMFreeList: Failed to free RPM free page list PMR %p (error %u)",
					 psFreeList->psFreeListPMR,
					 eError));
			PVR_ASSERT(IMG_FALSE);
		}

		/* Remove RPM FreeList from list */
		OSLockAcquire(psFreeList->psDevInfo->hLockRPMFreeList);
		dllist_remove_node(&psFreeList->sNode);
		OSLockRelease(psFreeList->psDevInfo->hLockRPMFreeList);
	}

	SyncPrimFree(psFreeList->psCleanupSync);

	/* free Freelist */
	OSFreeMem(psFreeList);

	return eError;
}


/*!
 *	RGXAddBlockToRPMFreeListKM
 * 
 * NOTE: This API isn't used but it's provided for symmetry with the parameter
 * management API.
*/
IMG_EXPORT
PVRSRV_ERROR RGXAddBlockToRPMFreeListKM(RGX_RPM_FREELIST *psFreeList,
										IMG_UINT32 ui32NumPages)
{
	PVRSRV_ERROR eError;

	/* Check if we have reference to freelist's PMR */
	if (psFreeList->psFreeListPMR == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,	"RPM Freelist is not configured for grow"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* grow freelist */
	eError = RGXGrowRPMFreeList(psFreeList,
								ui32NumPages,
								&psFreeList->sMemoryBlockHead);
	if(eError == PVRSRV_OK)
	{
		/* update freelist data in firmware */
		_UpdateFwRPMFreelistSize(psFreeList, IMG_TRUE, IMG_TRUE, ui32NumPages);

		psFreeList->ui32NumGrowReqByApp++;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			/* Update Stats */
			PVRSRVStatsUpdateFreelistStats(1, /* Add 1 to the appropriate counter (Requests by App)*/
	                               0,
	                               psFreeList->ui32InitFLPages,
	                               psFreeList->ui32NumHighPages,
	                               psFreeList->ownerPid);

#endif
	}

	return eError;
}


/*
 * RGXCreateRPMContext
 */
IMG_EXPORT
PVRSRV_ERROR RGXCreateRPMContext(CONNECTION_DATA * psConnection,
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
							     IMG_UINT32		     *puiHWFrameData)
{
	PVRSRV_ERROR					eError;
	PVRSRV_RGXDEV_INFO 				*psDevInfo = psDeviceNode->pvDevice;
	//DEVMEM_MEMDESC				*psFWRPMContextMemDesc;
	RGX_SERVER_RPM_CONTEXT			*psRPMContext;
	RGXFWIF_RAY_FRAME_DATA			*psFrameData;
	RGXFWIF_DEV_VIRTADDR 			 sFirmwareAddr;

	/* Allocate kernel RPM context */
	psRPMContext = OSAllocZMem(sizeof(*psRPMContext));
    if (psRPMContext == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMContext: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	*ppsRPMContext = psRPMContext;

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psRPMContext->psCleanupSync,
						   "RPM context cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateRPMContext: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto ErrorSyncAlloc;
	}

	/*
	 * 1. Create the sparse PMR for scene hierarchy
	 */
	eError = _RGXCreateRPMSparsePMR(psConnection, psDeviceNode,
									NODE_SCENE_HIERARCHY,
									ui32TotalRPMPages,
									uiLog2DopplerPageSize,
									&psRPMContext->psSceneHierarchyPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMContext: failed to allocate PMR for Scene hierarchy (%d)", eError));
		goto ErrorSparsePMR1;
	}

	/*
	 * 2. Create the sparse PMR for the RPM page list
	 */
	eError = _RGXCreateRPMSparsePMR(psConnection, psDeviceNode,
									NODE_RPM_PAGE_TABLE,
									ui32TotalRPMPages,
									uiLog2DopplerPageSize,
									&psRPMContext->psRPMPageTablePMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMContext: failed to allocate PMR for RPM Page list (%d)", eError));
		goto ErrorSparsePMR2;
	}

	/* Allocate FW structure and return FW address to client */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFrameData),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FwRPMContext",
							ppsMemDesc);
	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRPMContext: DevmemAllocate for RGXFWIF_FREELIST failed"));
		goto ErrorFWRPMContextAlloc;
	}

	/* Update the unallocated pages, which are shared between the RPM freelists */
	psRPMContext->ui32UnallocatedPages = psRPMContext->ui32TotalRPMPages = ui32TotalRPMPages;
	psRPMContext->psDeviceNode = psDeviceNode;
	psRPMContext->psFWRPMContextMemDesc = *ppsMemDesc;
	psRPMContext->uiLog2DopplerPageSize = uiLog2DopplerPageSize;

	/* Cache the virtual alloc state for future phys page mapping */
	psRPMContext->sDopplerHeapBaseAddr = sDopplerHeapBaseAddr;
	psRPMContext->sSceneMemoryBaseAddr = sSceneMemoryBaseAddr;
	psRPMContext->psSceneHeap = psSceneHeap;
	psRPMContext->sRPMPageTableBaseAddr = sRPMPageTableBaseAddr;
	psRPMContext->psRPMPageTableHeap = psRPMPageTableHeap;

	/*
	 * TODO - implement RPM abort control using HW frame data to track
	 * abort status in RTU.
	 */
	RGXSetFirmwareAddress(&sFirmwareAddr, *ppsMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	*puiHWFrameData = sFirmwareAddr.ui32Addr;

	//eError = DevmemAcquireCpuVirtAddr(*ppsMemDesc, (void **)&psFrameData);
	//PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", ErrorFrameDataCpuMap);

	/*
	 * TODO: pdumping
	 */


	return PVRSRV_OK;

ErrorFWRPMContextAlloc:
	PMRUnrefPMR(psRPMContext->psRPMPageTablePMR);

ErrorSparsePMR2:
	PMRUnrefPMR(psRPMContext->psSceneHierarchyPMR);

ErrorSparsePMR1:
	SyncPrimFree(psRPMContext->psCleanupSync);

ErrorSyncAlloc:
	OSFreeMem(psRPMContext);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}	


/*
 * RGXDestroyRPMContext
 */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRPMContext(RGX_SERVER_RPM_CONTEXT *psCleanupData)
{
	PVRSRV_ERROR				 eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo;
	PRGXFWIF_RAY_FRAME_DATA		 psFrameData;

	/* Wait for FW to process all commands */

	PVR_ASSERT(psCleanupData);

	RGXSetFirmwareAddress(&psFrameData, psCleanupData->psFWRPMContextMemDesc, 0, RFW_FWADDR_NOREF_FLAG);

	/* Cleanup frame data in SHG */
	eError = RGXFWRequestRayFrameDataCleanUp(psCleanupData->psDeviceNode,
										  psFrameData,
										  psCleanupData->psCleanupSync,
										  RGXFWIF_DM_SHG);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		PVR_DPF((PVR_DBG_WARNING, "FrameData busy in SHG"));
		return eError;
	}

	psDevInfo = psCleanupData->psDeviceNode->pvDevice;

	/* Cleanup frame data in RTU */
	eError = RGXFWRequestRayFrameDataCleanUp(psCleanupData->psDeviceNode,
										  psFrameData,
										  psCleanupData->psCleanupSync,
										  RGXFWIF_DM_RTU);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		PVR_DPF((PVR_DBG_WARNING, "FrameData busy in RTU"));
		return eError;
	}

	/* Free Scene hierarchy PMR (We should be the only one that holds a ref on the PMR) */
	eError = PMRUnrefPMR(psCleanupData->psSceneHierarchyPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXDestroyRPMContext: Failed to free scene hierarchy PMR %p (error %u)",
				 psCleanupData->psSceneHierarchyPMR,
				 eError));
		PVR_ASSERT(IMG_FALSE);
	}

	/* Free RPM Page list PMR */
	eError = PMRUnrefPMR(psCleanupData->psRPMPageTablePMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXDestroyRPMContext: Failed to free RPM page list PMR %p (error %u)",
				 psCleanupData->psRPMPageTablePMR,
				 eError));
		PVR_ASSERT(IMG_FALSE);
	}

	if (psCleanupData->uiFLRefCount > 0)
	{
		/* Kernel RPM freelists hold reference to RPM context */
		PVR_DPF((PVR_DBG_WARNING, "RGXDestroyRPMContext: Free list ref count non-zero."));
		return PVRSRV_ERROR_NONZERO_REFCOUNT;
	}

	/* If we got here then SHG and RTU operations on this FrameData have finished */
	SyncPrimFree(psCleanupData->psCleanupSync);

	/* Free the FW RPM descriptor */
	RGXUnsetFirmwareAddress(psCleanupData->psFWRPMContextMemDesc);
	DevmemFwFree(psDevInfo, psCleanupData->psFWRPMContextMemDesc);

	OSFreeMem(psCleanupData);

	return PVRSRV_OK;
}


/*
 * PVRSRVRGXCreateRayContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateRayContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sMCUFenceAddr,
											IMG_DEV_VIRTADDR			sVRMCallStackAddr,
											IMG_UINT32					ui32FrameworkRegisterSize,
											IMG_PBYTE					pabyFrameworkRegisters,
											IMG_HANDLE					hMemCtxPrivData,
											RGX_SERVER_RAY_CONTEXT	**ppsRayContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_RAY_CONTEXT		*psRayContext;
	DEVMEM_MEMDESC				*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO		sInfo;
	RGXFWIF_FWRAYCONTEXT		*pFWRayContext;
	IMG_UINT32 i;

	/* Prepare cleanup structure */
    *ppsRayContext= NULL;
	psRayContext = OSAllocZMem(sizeof(*psRayContext));
    if (psRayContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRayContext->psDeviceNode = psDeviceNode;

	/*
		Allocate device memory for the firmware ray context.
	*/
	PDUMPCOMMENT("Allocate RGX firmware ray context");

	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_FWRAYCONTEXT),
							RGX_FWCOMCTX_ALLOCFLAGS,
							"FwRayContext",
							&psRayContext->psFWRayContextMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to allocate firmware ray context (%u)",
				eError));
		goto fail_fwraycontext;
	}
					   
	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psRayContext->psCleanupSync,
						   "Ray context cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto fail_syncalloc;
	}
	
	/* 
	 * Create the FW framework buffer
	 */
	eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode, &psRayContext->psFWFrameworkMemDesc, ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to allocate firmware GPU framework state (%u)",
				eError));
		goto fail_frameworkcreate;
	}
	
	/* Copy the Framework client data into the framework buffer */
	eError = PVRSRVRGXFrameworkCopyCommand(psRayContext->psFWFrameworkMemDesc, pabyFrameworkRegisters, ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRayContextKM: Failed to populate the framework buffer (%u)",
				eError));
		goto fail_frameworkcopy;
	}

	sInfo.psFWFrameworkMemDesc = psRayContext->psFWFrameworkMemDesc;
	sInfo.psMCUFenceAddr = &sMCUFenceAddr;
	
	eError = _CreateSHContext(psConnection,
							  psDeviceNode,
							  psRayContext->psFWRayContextMemDesc,
							  offsetof(RGXFWIF_FWRAYCONTEXT, sSHGContext),
							  psFWMemContextMemDesc,
							  sVRMCallStackAddr,
							  ui32Priority,
							  &sInfo,
							  &psRayContext->sSHData);
	if (eError != PVRSRV_OK)
	{
		goto fail_shcontext;
	}

	eError = _CreateRSContext(psConnection,
							  psDeviceNode,
							  psRayContext->psFWRayContextMemDesc,
							  offsetof(RGXFWIF_FWRAYCONTEXT, sRTUContext),
							  psFWMemContextMemDesc,
							  ui32Priority,
							  &sInfo,
							  &psRayContext->sRSData);
	if (eError != PVRSRV_OK)
	{
		goto fail_rscontext;
	}
		
	/*
		Temporarily map the firmware context to the kernel and init it
	*/
	eError = DevmemAcquireCpuVirtAddr(psRayContext->psFWRayContextMemDesc,
									  (void **)&pFWRayContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware %s ray context to CPU",
								__FUNCTION__,
								PVRSRVGetErrorStringKM(eError)));
		goto fail_rscontext;
	}

	
	for (i = 0; i < DPX_MAX_RAY_CONTEXTS; i++)
	{
		/* Allocate the frame context client CCB */
		eError = RGXCreateCCB(psDevInfo,
							  RGX_RTU_CCB_SIZE_LOG2,
							  psConnection,
							  REQ_TYPE_FC0 + i,
							  psRayContext->sRSData.psServerCommonContext,
							  &psRayContext->sRSData.psFCClientCCB[i],
							  &psRayContext->sRSData.psFCClientCCBMemDesc[i],
							  &psRayContext->sRSData.psFCClientCCBCtrlMemDesc[i]);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: failed to create CCB for frame context %u (%s)",
									__FUNCTION__,
									i,
									PVRSRVGetErrorStringKM(eError)));
			goto fail_rscontext;
		}

		/* Set the firmware CCB device addresses in the firmware common context */
		RGXSetFirmwareAddress(&pFWRayContext->psCCB[i],
							  psRayContext->sRSData.psFCClientCCBMemDesc[i],
							  0, RFW_FWADDR_FLAG_NONE);
		RGXSetFirmwareAddress(&pFWRayContext->psCCBCtl[i],
							  psRayContext->sRSData.psFCClientCCBCtrlMemDesc[i],
							  0, RFW_FWADDR_FLAG_NONE);
	}
	
	pFWRayContext->ui32ActiveFCMask = 0;
	pFWRayContext->ui32NextFC = RGXFWIF_INVALID_FRAME_CONTEXT;

	/* We've finished the setup so release the CPU mapping */
	DevmemReleaseCpuVirtAddr(psRayContext->psFWRayContextMemDesc);	
		
	/*
		As the common context alloc will dump the SH and RS common contexts
		after the've been setup we skip of the 2 common contexts and dump the
		rest of the structure
	*/
	PDUMPCOMMENT("Dump shared part of ray context context");
	DevmemPDumpLoadMem(psRayContext->psFWRayContextMemDesc,
					   (sizeof(RGXFWIF_FWCOMMONCONTEXT) * 2),
					   sizeof(RGXFWIF_FWRAYCONTEXT) - (sizeof(RGXFWIF_FWCOMMONCONTEXT) * 2),
					   PDUMP_FLAGS_CONTINUOUS);

	{
		PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

		OSWRLockAcquireWrite(psDevInfo->hRaytraceCtxListLock);
		dllist_add_to_tail(&(psDevInfo->sRaytraceCtxtListHead), &(psRayContext->sListNode));
		OSWRLockReleaseWrite(psDevInfo->hRaytraceCtxListLock);
	}

	*ppsRayContext= psRayContext;
	return PVRSRV_OK;

fail_rscontext:
	_DestroySHContext(&psRayContext->sSHData,
					  psDeviceNode,
					  psRayContext->psCleanupSync);
fail_shcontext:
fail_frameworkcopy:
	DevmemFwFree(psDevInfo, psRayContext->psFWFrameworkMemDesc);
fail_frameworkcreate:
	SyncPrimFree(psRayContext->psCleanupSync);
fail_syncalloc:
	DevmemFwFree(psDevInfo, psRayContext->psFWRayContextMemDesc);
fail_fwraycontext:
	OSFreeMem(psRayContext);
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


/*
 * PVRSRVRGXDestroyRayContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyRayContextKM(RGX_SERVER_RAY_CONTEXT *psRayContext)
{
	PVRSRV_ERROR				eError;
	IMG_UINT32 i;
	PVRSRV_RGXDEV_INFO *psDevInfo = psRayContext->psDeviceNode->pvDevice;

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hRaytraceCtxListLock);
	dllist_remove_node(&(psRayContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRaytraceCtxListLock);

	/* Cleanup the TA if we haven't already */
	if ((psRayContext->ui32CleanupStatus & RAY_CLEANUP_SH_COMPLETE) == 0)
	{
		eError = _DestroySHContext(&psRayContext->sSHData,
								   psRayContext->psDeviceNode,
								   psRayContext->psCleanupSync);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			psRayContext->ui32CleanupStatus |= RAY_CLEANUP_SH_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

	/* Cleanup the RS if we haven't already */
	if ((psRayContext->ui32CleanupStatus & RAY_CLEANUP_RS_COMPLETE) == 0)
	{
		eError = _DestroyRSContext(&psRayContext->sRSData,
								   psRayContext->psDeviceNode,
								   psRayContext->psCleanupSync);
		if (eError != PVRSRV_ERROR_RETRY)
		{
			psRayContext->ui32CleanupStatus |= RAY_CLEANUP_RS_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

#if 0
	/*
	 * 	FIXME - De-allocate RPM freelists (should be called from UM)
	 */
	RGXDestroyRPMFreeList(psRayContext->sSHData.psSHFFreeList);
	RGXDestroyRPMFreeList(psRayContext->sSHData.psSHGFreeList);
#endif
	
	for (i = 0; i < DPX_MAX_RAY_CONTEXTS; i++)
	{
		RGXUnsetFirmwareAddress(psRayContext->sRSData.psFCClientCCBMemDesc[i]);
		RGXUnsetFirmwareAddress(psRayContext->sRSData.psFCClientCCBCtrlMemDesc[i]);
		RGXDestroyCCB(psDevInfo, psRayContext->sRSData.psFCClientCCB[i]);
	}

	/*
		Only if both TA and 3D contexts have been cleaned up can we
		free the shared resources
	*/
	if (psRayContext->ui32CleanupStatus == (RAY_CLEANUP_RS_COMPLETE | RAY_CLEANUP_SH_COMPLETE))
	{
		/* Free the framework buffer */
		DevmemFwFree(psDevInfo, psRayContext->psFWFrameworkMemDesc);
	
		/* Free the firmware ray context */
		DevmemFwFree(psDevInfo, psRayContext->psFWRayContextMemDesc);

		/* Free the cleanup sync */
		SyncPrimFree(psRayContext->psCleanupSync);

		OSFreeMem(psRayContext);
	}

	return PVRSRV_OK;

e0:
	OSWRLockAcquireWrite(psDevInfo->hRaytraceCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sRaytraceCtxtListHead), &(psRayContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRaytraceCtxListLock);
	return eError;
}

/*
 * PVRSRVRGXKickRSKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickRSKM(RGX_SERVER_RAY_CONTEXT		*psRayContext,
								IMG_UINT32					ui32ClientCacheOpSeqNum,
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
								IMG_UINT32					ui32FCCmdSize,
								IMG_PBYTE					pui8FCDMCmd,
								IMG_UINT32					ui32FrameContextID,
								IMG_UINT32					ui32PDumpFlags,
								IMG_UINT32					ui32ExtJobRef)
{
	RGXFWIF_KCCB_CMD		sRSKCCBCmd;
	RGX_CCB_CMD_HELPER_DATA	asRSCmdHelperData[1] = {{0}};
	RGX_CCB_CMD_HELPER_DATA asFCCmdHelperData[1] = {{0}};
	PVRSRV_ERROR			eError;
	PVRSRV_ERROR			eError1;
	PVRSRV_ERROR			eError2;
	RGX_SERVER_RAY_RS_DATA *psRSData = &psRayContext->sRSData;
	IMG_UINT32				i;
	IMG_UINT32				ui32FCWoff;
	IMG_UINT32				ui32RTUCmdOffset = 0;
	IMG_UINT32				ui32JobId;
	IMG_UINT32				ui32FWCtx;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;
	
	ui32JobId = OSAtomicIncrement(&psRayContext->hJobId);

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListFence,
							ui32ClientFenceCount,
							pauiClientFenceUFOSyncPrimBlock,
							paui32ClientFenceSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListUpdate,
							ui32ClientUpdateCount,
							pauiClientUpdateUFOSyncPrimBlock,
							paui32ClientUpdateSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	/* Sanity check the server fences */
	for (i=0;i<ui32ServerSyncPrims;i++)
	{
		if (!(paui32ServerSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on RS) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psRayContext->psDeviceNode->pvDevice,
	                          & pPreAddr,
	                          & pPostAddr,
	                          & pRMWUFOAddr);


    if(pui8DMCmd != NULL)
	{
		eError = RGXCmdHelperInitCmdCCB(psRSData->psFCClientCCB[ui32FrameContextID],
	                                0,
                                    NULL,
                                    NULL,
	                                ui32ClientUpdateCount,
	                                psRayContext->sSyncAddrListUpdate.pasFWAddrs,
	                                paui32ClientUpdateValue,
	                                ui32ServerSyncPrims,
	                                paui32ServerSyncFlags,
	                                SYNC_FLAG_MASK_ALL,
	                                pasServerSyncs,
	                                ui32CmdSize,
	                                pui8DMCmd,
	                                & pPreAddr,
	                                & pPostAddr,
	                                & pRMWUFOAddr,
	                                RGXFWIF_CCB_CMD_TYPE_RTU,
	                                ui32ExtJobRef,
	                                ui32JobId,
	                                ui32PDumpFlags,
	                                NULL,
	                                "FC",
	                                asFCCmdHelperData);
	}
	else
	{
		eError = RGXCmdHelperInitCmdCCB(psRSData->psFCClientCCB[ui32FrameContextID],
	                                0,
                                    NULL,
                                    NULL,
	                                ui32ClientUpdateCount,
	                                psRayContext->sSyncAddrListUpdate.pasFWAddrs,
	                                paui32ClientUpdateValue,
	                                ui32ServerSyncPrims,
	                                paui32ServerSyncFlags,
	                                SYNC_FLAG_MASK_ALL,
	                                pasServerSyncs,
	                                ui32CmdSize,
	                                pui8DMCmd,
	                                & pPreAddr,
	                                & pPostAddr,
	                                & pRMWUFOAddr,
	                                RGXFWIF_CCB_CMD_TYPE_NULL,
	                                ui32ExtJobRef,
	                                ui32JobId,
	                                ui32PDumpFlags,
	                                NULL,
	                                "FC",
	                                asFCCmdHelperData);

	}

	if (eError != PVRSRV_OK)
	{
		goto PVRSRVRGXKickRSKM_Exit;
	}

	eError = RGXCmdHelperAcquireCmdCCB(IMG_ARR_NUM_ELEMS(asFCCmdHelperData),
	                                   asFCCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto PVRSRVRGXKickRSKM_Exit;
	}
	
	ui32FCWoff = RGXCmdHelperGetCommandSize(IMG_ARR_NUM_ELEMS(asFCCmdHelperData),
	                                        asFCCmdHelperData);
	
	*(IMG_UINT32*)pui8FCDMCmd = RGXGetHostWriteOffsetCCB(psRSData->psFCClientCCB[ui32FrameContextID]) + ui32FCWoff;

	/*
		We should reserved space in the kernel CCB here and fill in the command
		directly.
		This is so if there isn't space in the kernel CCB we can return with
		retry back to services client before we take any operations
	*/

	/*
		We might only be kicking for flush out a padding packet so only submit
		the command if the create was successful
	*/
	eError1 = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(psRSData->psServerCommonContext),
	                                 ui32ClientFenceCount,
	                                 psRayContext->sSyncAddrListFence.pasFWAddrs,
	                                 paui32ClientFenceValue,
	                                 0,
                                     NULL,
                                     NULL,
	                                 ui32ServerSyncPrims,
	                                 paui32ServerSyncFlags,
	                                 SYNC_FLAG_MASK_ALL,
	                                 pasServerSyncs,
	                                 ui32FCCmdSize,
	                                 pui8FCDMCmd,
                                     NULL,
	                                 & pPostAddr,
	                                 & pRMWUFOAddr,
	                                 RGXFWIF_CCB_CMD_TYPE_RTU_FC,
	                                 ui32ExtJobRef,
	                                 ui32JobId,
	                                 ui32PDumpFlags,
	                                 NULL,
	                                 "RS",
	                                 asRSCmdHelperData);
	if (eError1 != PVRSRV_OK)
	{
		goto PVRSRVRGXKickRSKM_Exit;
	}

	eError1 = RGXCmdHelperAcquireCmdCCB(IMG_ARR_NUM_ELEMS(asRSCmdHelperData),
	                                    asRSCmdHelperData);
	if (eError1 != PVRSRV_OK)
	{
		goto PVRSRVRGXKickRSKM_Exit;
	}
	
	
	/*
		We should reserved space in the kernel CCB here and fill in the command
		directly.
		This is so if there isn't space in the kernel CCB we can return with
		retry back to services client before we take any operations
	*/

	/*
		We might only be kicking for flush out a padding packet so only submit
		the command if the create was successful
	*/
	/*
		All the required resources are ready at this point, we can't fail so
		take the required server sync operations and commit all the resources
	*/
	RGXCmdHelperReleaseCmdCCB(IMG_ARR_NUM_ELEMS(asFCCmdHelperData),
	                          asFCCmdHelperData, "FC", 0);

	/*
		All the required resources are ready at this point, we can't fail so
		take the required server sync operations and commit all the resources
	*/
	ui32RTUCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRSData->psServerCommonContext));
	RGXCmdHelperReleaseCmdCCB(IMG_ARR_NUM_ELEMS(asRSCmdHelperData),
	                          asRSCmdHelperData, "RS",
	                          FWCommonContextGetFWAddress(psRSData->psServerCommonContext).ui32Addr);

	/*
	 * Construct the kernel RTU CCB command.
	 * (Safe to release reference to ray context virtual address because
	 * ray context destruction must flush the firmware).
	 */
	sRSKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sRSKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRSData->psServerCommonContext);
	sRSKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRSData->psServerCommonContext));
	sRSKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

	ui32FWCtx = FWCommonContextGetFWAddress(psRSData->psServerCommonContext).ui32Addr;

	HTBLOGK(HTB_SF_MAIN_KICK_RTU,
			sRSKCCBCmd.uCmdData.sCmdKickData.psContext,
			ui32RTUCmdOffset
			);
	RGX_HWPERF_HOST_ENQ(psRayContext, OSGetCurrentClientProcessIDKM(),
	                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
	                    RGX_HWPERF_KICK_TYPE_RS);

	/*
	 * Submit the RTU command to the firmware.
	 */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError2 = RGXScheduleCommand(psRayContext->psDeviceNode->pvDevice,
									RGXFWIF_DM_RTU,
									&sRSKCCBCmd,
									sizeof(sRSKCCBCmd),
									ui32ClientCacheOpSeqNum,
									ui32PDumpFlags);
		if (eError2 != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError2 != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXKickRSKM failed to schedule kernel RTU command. Error:%u", eError));
		eError = eError2;
		goto PVRSRVRGXKickRSKM_Exit;
	}
	else
	{
#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUEnqueueEvent(psRayContext->psDeviceNode->pvDevice,
				ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_RS);
#endif
	}


PVRSRVRGXKickRSKM_Exit:
err_populate_sync_addr_list:
	return eError;
}

/*
 * PVRSRVRGXKickVRDMKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickVRDMKM(RGX_SERVER_RAY_CONTEXT		*psRayContext,
								 IMG_UINT32					ui32ClientCacheOpSeqNum,
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
								 IMG_UINT32					ui32PDumpFlags,
								 IMG_UINT32					ui32ExtJobRef)
{
	RGXFWIF_KCCB_CMD		sSHKCCBCmd;
	RGX_CCB_CMD_HELPER_DATA	sCmdHelperData;
	PVRSRV_ERROR			eError;
	PVRSRV_ERROR			eError2;
	RGX_SERVER_RAY_SH_DATA *psSHData = &psRayContext->sSHData;
	IMG_UINT32				i;
	IMG_UINT32				ui32SHGCmdOffset = 0;
	IMG_UINT32				ui32JobId;
	IMG_UINT32				ui32FWCtx;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;

	ui32JobId = OSAtomicIncrement(&psRayContext->hJobId);

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListFence,
							ui32ClientFenceCount,
							pauiClientFenceUFOSyncPrimBlock,
							paui32ClientFenceSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	eError = SyncAddrListPopulate(&psRayContext->sSyncAddrListUpdate,
							ui32ClientUpdateCount,
							pauiClientUpdateUFOSyncPrimBlock,
							paui32ClientUpdateSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	/* Sanity check the server fences */
	for (i=0;i<ui32ServerSyncPrims;i++)
	{
		if (!(paui32ServerSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on SH) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psRayContext->psDeviceNode->pvDevice,
	                          & pPreAddr,
	                          & pPostAddr,
	                          & pRMWUFOAddr);

	eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(psSHData->psServerCommonContext),
	                                ui32ClientFenceCount,
	                                psRayContext->sSyncAddrListFence.pasFWAddrs,
	                                paui32ClientFenceValue,
	                                ui32ClientUpdateCount,
	                                psRayContext->sSyncAddrListUpdate.pasFWAddrs,
	                                paui32ClientUpdateValue,
	                                ui32ServerSyncPrims,
	                                paui32ServerSyncFlags,
	                                SYNC_FLAG_MASK_ALL,
	                                pasServerSyncs,
	                                ui32CmdSize,
	                                pui8DMCmd,
	                                & pPreAddr,
	                                & pPostAddr,
	                                & pRMWUFOAddr,
	                                RGXFWIF_CCB_CMD_TYPE_SHG,
	                                ui32ExtJobRef,
	                                ui32JobId,
	                                ui32PDumpFlags,
	                                NULL,
	                                "SH",
	                                &sCmdHelperData);

	if (eError != PVRSRV_OK)
	{
		goto PVRSRVRGXKickSHKM_Exit;
	}

	eError = RGXCmdHelperAcquireCmdCCB(1, &sCmdHelperData);
	if (eError != PVRSRV_OK)
	{
		goto PVRSRVRGXKickSHKM_Exit;
	}
	
	
	/*
		We should reserve space in the kernel CCB here and fill in the command
		directly.
		This is so if there isn't space in the kernel CCB we can return with
		retry back to services client before we take any operations
	*/

	/*
		We might only be kicking for flush out a padding packet so only submit
		the command if the create was successful
	*/

	/*
		All the required resources are ready at this point, we can't fail so
		take the required server sync operations and commit all the resources
	*/
	ui32SHGCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psSHData->psServerCommonContext));
	RGXCmdHelperReleaseCmdCCB(1, &sCmdHelperData, "SH", FWCommonContextGetFWAddress(psSHData->psServerCommonContext).ui32Addr);

	
	/*
	 * Construct the kernel SHG CCB command.
	 * (Safe to release reference to ray context virtual address because
	 * ray context destruction must flush the firmware).
	 */
	sSHKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
	sSHKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psSHData->psServerCommonContext);
	sSHKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psSHData->psServerCommonContext));
	sSHKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;

	ui32FWCtx = FWCommonContextGetFWAddress(psSHData->psServerCommonContext).ui32Addr;

	HTBLOGK(HTB_SF_MAIN_KICK_SHG,
			sSHKCCBCmd.uCmdData.sCmdKickData.psContext,
			ui32SHGCmdOffset
			);
	RGX_HWPERF_HOST_ENQ(psRayContext, OSGetCurrentClientProcessIDKM(),
	                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
	                    RGX_HWPERF_KICK_TYPE_VRDM);

	/*
	 * Submit the RTU command to the firmware.
	 */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError2 = RGXScheduleCommand(psRayContext->psDeviceNode->pvDevice,
									RGXFWIF_DM_SHG,
									&sSHKCCBCmd,
									sizeof(sSHKCCBCmd),
									ui32ClientCacheOpSeqNum,
									ui32PDumpFlags);
		if (eError2 != PVRSRV_ERROR_RETRY)
		{
			break;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} END_LOOP_UNTIL_TIMEOUT();

	if (eError2 != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXKickSHKM failed to schedule kernel RTU command. Error:%u", eError));
		eError = eError2;
		goto PVRSRVRGXKickSHKM_Exit;
	}
	else
	{
#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUEnqueueEvent(psRayContext->psDeviceNode->pvDevice,
				ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_VRDM);
#endif
	}


PVRSRVRGXKickSHKM_Exit:
err_populate_sync_addr_list:
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetRayContextPriorityKM(CONNECTION_DATA *psConnection,
                                              PVRSRV_DEVICE_NODE * psDeviceNode,
												 RGX_SERVER_RAY_CONTEXT *psRayContext,
												 IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	if (psRayContext->sSHData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRayContext->sSHData.psServerCommonContext,
									psConnection,
									psRayContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_SHG);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the SH part of the rendercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			goto fail_shcontext;
		}

		psRayContext->sSHData.ui32Priority = ui32Priority;
	}

	if (psRayContext->sRSData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRayContext->sRSData.psServerCommonContext,
									psConnection,
									psRayContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_RTU);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the RS part of the rendercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			goto fail_rscontext;
		}

		psRayContext->sRSData.ui32Priority = ui32Priority;
	}
	return PVRSRV_OK;

fail_rscontext:
fail_shcontext:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

void CheckForStalledRayCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	DLLIST_NODE *psNode, *psNext;
	OSWRLockAcquireRead(psDevInfo->hRaytraceCtxListLock);
	dllist_foreach_node(&psDevInfo->sRaytraceCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_RAY_CONTEXT *psCurrentServerRayCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_RAY_CONTEXT, sListNode);

		DumpStalledFWCommonContext(psCurrentServerRayCtx->sSHData.psServerCommonContext,
								   pfnDumpDebugPrintf, pvDumpDebugFile);
		DumpStalledFWCommonContext(psCurrentServerRayCtx->sRSData.psServerCommonContext,
								   pfnDumpDebugPrintf, pvDumpDebugFile);
	}
	OSWRLockReleaseRead(psDevInfo->hRaytraceCtxListLock);
}

IMG_UINT32 CheckForStalledClientRayCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_UINT32 ui32ContextBitMask = 0;

	OSWRLockAcquireRead(psDevInfo->hRaytraceCtxListLock);

	dllist_foreach_node(&psDevInfo->sRaytraceCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_RAY_CONTEXT *psCurrentServerRayCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_RAY_CONTEXT, sListNode);
		if(NULL != psCurrentServerRayCtx->sSHData.psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerRayCtx->sSHData.psServerCommonContext, RGX_KICK_TYPE_DM_RTU) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_RTU;
			}
		}

		if(NULL != psCurrentServerRayCtx->sRSData.psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerRayCtx->sRSData.psServerCommonContext, RGX_KICK_TYPE_DM_SHG) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_SHG;
			}
		}
	}

	OSWRLockReleaseRead(psDevInfo->hRaytraceCtxListLock);
	return ui32ContextBitMask;
}

/******************************************************************************
 End of file (rgxSHGRTU.c)
******************************************************************************/
