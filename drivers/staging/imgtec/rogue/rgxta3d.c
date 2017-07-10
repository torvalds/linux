/*************************************************************************/ /*!
@File
@Title          RGX TA/3D routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX TA/3D routines
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

#include "pdump_km.h"
#include "pvr_debug.h"
#include "rgxutils.h"
#include "rgxfwutils.h"
#include "rgxta3d.h"
#include "rgxmem.h"
#include "allocmem.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "ri_server.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "rgx_memallocflags.h"
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

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
#include "pvr_sync.h"
#endif

#if defined(SUPPORT_PDVFS)
#include "rgxpdvfs.h"
#endif

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
#include "hash.h"
#include "rgxworkest.h"

#define HASH_CLEAN_LIMIT 6
#endif

typedef struct _DEVMEM_REF_LOOKUP_
{
	IMG_UINT32 ui32ZSBufferID;
	RGX_ZSBUFFER_DATA *psZSBuffer;
} DEVMEM_REF_LOOKUP;

typedef struct _DEVMEM_FREELIST_LOOKUP_
{
	IMG_UINT32 ui32FreeListID;
	RGX_FREELIST *psFreeList;
} DEVMEM_FREELIST_LOOKUP;

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_RC_TA_DATA;

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_UINT32					ui32Priority;
} RGX_SERVER_RC_3D_DATA;

struct _RGX_SERVER_RENDER_CONTEXT_ {
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWRenderContextMemDesc;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	RGX_SERVER_RC_TA_DATA		sTAData;
	RGX_SERVER_RC_3D_DATA		s3DData;
	IMG_UINT32					ui32CleanupStatus;
#define RC_CLEANUP_TA_COMPLETE		(1 << 0)
#define RC_CLEANUP_3D_COMPLETE		(1 << 1)
	PVRSRV_CLIENT_SYNC_PRIM		*psCleanupSync;
	DLLIST_NODE					sListNode;
	SYNC_ADDR_LIST				sSyncAddrListTAFence;
	SYNC_ADDR_LIST				sSyncAddrListTAUpdate;
	SYNC_ADDR_LIST				sSyncAddrList3DFence;
	SYNC_ADDR_LIST				sSyncAddrList3DUpdate;
	ATOMIC_T					hJobId;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	WORKEST_HOST_DATA			sWorkEstData;
#endif
};


#if ! defined(NO_HARDWARE)
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


/*
	Static functions used by render context code
*/

static
PVRSRV_ERROR _DestroyTAContext(RGX_SERVER_RC_TA_DATA *psTAData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  psTAData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_TA,
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

	/* ... it has so we can free it's resources */
#if defined(DEBUG)
	/* Log the number of TA context stores which occurred */
	{
		RGXFWIF_TACTX_STATE	*psFWTAState;

		eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
										  (void**)&psFWTAState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware render context state (%u)",
					__FUNCTION__, eError));
		}
		else
		{
			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);
		}
	}
#endif
	FWCommonContextFree(psTAData->psServerCommonContext);
	DevmemFwFree(psDeviceNode->pvDevice, psTAData->psContextStateMemDesc);
	psTAData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}

static
PVRSRV_ERROR _Destroy3DContext(RGX_SERVER_RC_3D_DATA *ps3DData,
							   PVRSRV_DEVICE_NODE *psDeviceNode,
							   PVRSRV_CLIENT_SYNC_PRIM *psCleanupSync)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
											  ps3DData->psServerCommonContext,
											  psCleanupSync,
											  RGXFWIF_DM_3D,
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

	/* ... it has so we can free it's resources */
#if defined(DEBUG)
	/* Log the number of 3D context stores which occurred */
	{
		RGXFWIF_3DCTX_STATE	*psFW3DState;

		eError = DevmemAcquireCpuVirtAddr(ps3DData->psContextStateMemDesc,
										  (void**)&psFW3DState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"%s: Failed to map firmware render context state (%u)",
					__FUNCTION__, eError));
		}
		else
		{
			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(ps3DData->psContextStateMemDesc);
		}
	}
#endif

	FWCommonContextFree(ps3DData->psServerCommonContext);
	DevmemFwFree(psDeviceNode->pvDevice, ps3DData->psContextStateMemDesc);
	ps3DData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}

static void _RGXDumpPMRPageList(DLLIST_NODE *psNode)
{
	RGX_PMR_NODE *psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
	PVRSRV_ERROR			eError;

	eError = PMRDumpPageList(psPMRNode->psPMR,
							RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Error (%u) printing pmr %p", eError, psPMRNode->psPMR));
	}
}

IMG_BOOL RGXDumpFreeListPageList(RGX_FREELIST *psFreeList)
{
	DLLIST_NODE *psNode, *psNext;

	PVR_LOG(("Freelist FWAddr 0x%08x, ID = %d, CheckSum 0x%016llx",
				psFreeList->sFreeListFWDevVAddr.ui32Addr,
				psFreeList->ui32FreelistID,
				psFreeList->ui64FreelistChecksum));

	/* Dump Init FreeList page list */
	PVR_LOG(("  Initial Memory block"));
	dllist_foreach_node(&psFreeList->sMemoryBlockInitHead, psNode, psNext)
	{
		_RGXDumpPMRPageList(psNode);
	}

	/* Dump Grow FreeList page list */
	PVR_LOG(("  Grow Memory blocks"));
	dllist_foreach_node(&psFreeList->sMemoryBlockHead, psNode, psNext)
	{
		_RGXDumpPMRPageList(psNode);
	}

	return IMG_TRUE;
}

static PVRSRV_ERROR _UpdateFwFreelistSize(RGX_FREELIST *psFreeList,
										IMG_BOOL bGrow,
										IMG_UINT32 ui32DeltaSize)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_KCCB_CMD		sGPCCBCmd;

	sGPCCBCmd.eCmdType = (bGrow) ? RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE : RGXFWIF_KCCB_CMD_FREELIST_SHRINK_UPDATE;
	sGPCCBCmd.uCmdData.sFreeListGSData.sFreeListFWDevVAddr.ui32Addr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32DeltaSize;
	sGPCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = psFreeList->ui32CurrentFLPages;

	PVR_DPF((PVR_DBG_MESSAGE, "Send FW update: freelist [FWAddr=0x%08x] has 0x%08x pages",
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
		PVR_DPF((PVR_DBG_ERROR, "_UpdateFwFreelistSize: failed to update FW freelist size. (error = %u)", eError));
		return eError;
	}

	return eError;
}

static void _CheckFreelist(RGX_FREELIST *psFreeList,
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
		PVR_LOG(("_CheckFreelist: Failed to allocate buffer to check freelist %p!", psFreeList));
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
		PVR_LOG(("_CheckFreelist: Failed to get freelist data for freelist %p!", psFreeList));
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
				PVR_LOG(("_CheckFreelist: Freelist consistency failure: FW addr: 0x%08X, Double entry found 0x%08x on idx: %d and %d of %d",
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
		PVR_LOG(("_CheckFreelist: Checksum mismatch for freelist %p!  Expected 0x%016llx calculated 0x%016llx",
		        psFreeList, ui64ExpectedCheckSum, *pui64CalculatedCheckSum));
		bFreelistBad = IMG_TRUE;
	}
    
    if (bFreelistBad)
    {
		PVR_LOG(("_CheckFreelist: Sleeping for ever!"));
		sleep_for_ever();
//		PVR_ASSERT(!bFreelistBad);
	}
#endif
}

PVRSRV_ERROR RGXGrowFreeList(RGX_FREELIST *psFreeList,
							IMG_UINT32 ui32NumPages,
							PDLLIST_NODE pListHeader)
{
	RGX_PMR_NODE	*psPMRNode;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32  ui32MappingTable = 0;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_DEVMEM_SIZE_T uiLength;
	IMG_DEVMEM_SIZE_T uistartPage;
	PVRSRV_ERROR eError;
	const IMG_CHAR * pszAllocName = "Free List";

	/* Are we allowed to grow ? */
	if ((psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages) < ui32NumPages)
	{
		PVR_DPF((PVR_DBG_WARNING,"Freelist [0x%p]: grow by %u pages denied. Max PB size reached (current pages %u/%u)",
				psFreeList,
				ui32NumPages,
				psFreeList->ui32CurrentFLPages,
				psFreeList->ui32MaxFLPages));
		return PVRSRV_ERROR_PBSIZE_ALREADY_MAX;
	}

	/* Allocate kernel memory block structure */
	psPMRNode = OSAllocMem(sizeof(*psPMRNode));
	if (psPMRNode == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXGrowFreeList: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages
	 */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);


	psPMRNode->ui32NumPages = ui32NumPages;
	psPMRNode->psFreeList = psFreeList;

	/* Allocate Memory Block */
	PDUMPCOMMENT("Allocate PB Block (Pages %08X)", ui32NumPages);
	uiSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE;
	eError = PhysmemNewRamBackedPMR(NULL,
	                                psFreeList->psDevInfo->psDeviceNode,
									uiSize,
									uiSize,
									1,
									1,
									&ui32MappingTable,
									RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
									PVRSRV_MEMALLOCFLAG_GPU_READABLE,
									OSStringLength(pszAllocName) + 1,
									pszAllocName,
									&psPMRNode->psPMR);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXGrowFreeList: Failed to allocate PB block of size: 0x%016llX",
				 (IMG_UINT64)uiSize));
		goto ErrorBlockAlloc;
	}

	/* Zeroing physical pages pointed by the PMR */
	if (psFreeList->psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXGrowFreeList: Failed to zero PMR %p of freelist %p with Error %d",
									psPMRNode->psPMR,
									psFreeList,
									eError));
			PVR_ASSERT(0);
		}
	}

	uiLength = psPMRNode->ui32NumPages * sizeof(IMG_UINT32);
	uistartPage = (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages - psPMRNode->ui32NumPages);
	uiOffset = psFreeList->uiFreeListPMROffset + (uistartPage * sizeof(IMG_UINT32));

#if defined(PVR_RI_DEBUG)

	eError = RIWritePMREntryKM(psPMRNode->psPMR,
	                           OSStringNLength(pszAllocName, RI_MAX_TEXT_LEN),
	                           pszAllocName,
	                           uiSize);
	if( eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: call to RIWritePMREntryKM failed (eError=%d)",
				__func__,
				eError));
	}

	 /* Attach RI information */
	eError = RIWriteMEMDESCEntryKM(psPMRNode->psPMR,
	                               OSStringNLength(pszAllocName, RI_MAX_TEXT_LEN),
	                               pszAllocName,
	                               0,
	                               uiSize,
	                               uiSize,
	                               IMG_FALSE,
	                               IMG_FALSE,
	                               &psPMRNode->hRIHandle);
	if( eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: call to RIWriteMEMDESCEntryKM failed (eError=%d)",
				__func__,
				eError));
	}

#endif /* if defined(PVR_RI_DEBUG) */

	/* write Freelist with Memory Block physical addresses */
	eError = PMRWritePMPageList(
						/* Target PMR, offset, and length */
						psFreeList->psFreeListPMR,
						uiOffset,
						uiLength,
						/* Referenced PMR, and "page" granularity */
						psPMRNode->psPMR,
						RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
						&psPMRNode->psPageList);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "RGXGrowFreeList: Failed to write pages of Node %p",
				 psPMRNode));
		goto ErrorPopulateFreelist;
	}

	/* We add It must be added to the tail, otherwise the freelist population won't work */
	dllist_add_to_head(pListHeader, &psPMRNode->sMemoryBlock);

	/* Update number of available pages */
	psFreeList->ui32CurrentFLPages += ui32NumPages;

	/* Update statistics */
	if (psFreeList->ui32NumHighPages < psFreeList->ui32CurrentFLPages)
	{
		psFreeList->ui32NumHighPages = psFreeList->ui32CurrentFLPages;
	}

	if (psFreeList->bCheckFreelist)
	{
		/* We can only do a freelist check if the list is full (e.g. at initial creation time) */
		if (psFreeList->ui32CurrentFLPages == ui32NumPages)
		{
			IMG_UINT64  ui64Dummy;
			_CheckFreelist(psFreeList, ui32NumPages, psFreeList->ui64FreelistChecksum, &ui64Dummy);
		}
	}

	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	PVR_DPF((PVR_DBG_MESSAGE,"Freelist [%p]: grow by %u pages (current pages %u/%u)",
			psFreeList,
			ui32NumPages,
			psFreeList->ui32CurrentFLPages,
			psFreeList->ui32MaxFLPages));

	return PVRSRV_OK;

	/* Error handling */
ErrorPopulateFreelist:
	PMRUnrefPMR(psPMRNode->psPMR);

ErrorBlockAlloc:
	OSFreeMem(psPMRNode);
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;

}

static PVRSRV_ERROR RGXShrinkFreeList(PDLLIST_NODE pListHeader,
										RGX_FREELIST *psFreeList)
{
	DLLIST_NODE *psNode;
	RGX_PMR_NODE *psPMRNode;
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
	PVR_ASSERT(psFreeList->psDevInfo->hLockFreeList);

	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);

	/* Get node from head of list and remove it */
	psNode = dllist_get_next_node(pListHeader);
	if (psNode)
	{
		dllist_remove_node(psNode);

		psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
		PVR_ASSERT(psPMRNode);
		PVR_ASSERT(psPMRNode->psPMR);
		PVR_ASSERT(psPMRNode->psFreeList);

		/* remove block from freelist list */

		/* Unwrite Freelist with Memory Block physical addresses */
		eError = PMRUnwritePMPageList(psPMRNode->psPageList);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXRemoveBlockFromFreeListKM: Failed to unwrite pages of Node %p",
					 psPMRNode));
			PVR_ASSERT(IMG_FALSE);
		}

#if defined(PVR_RI_DEBUG)

		if (psPMRNode->hRIHandle)
		{
		    PVRSRV_ERROR eError;

		    eError = RIDeleteMEMDESCEntryKM(psPMRNode->hRIHandle);
			if( eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: call to RIDeleteMEMDESCEntryKM failed (eError=%d)", __func__, eError));
			}
		}

#endif  /* if defined(PVR_RI_DEBUG) */

		/* Free PMR (We should be the only one that holds a ref on the PMR) */
		eError = PMRUnrefPMR(psPMRNode->psPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "RGXRemoveBlockFromFreeListKM: Failed to free PB block %p (error %u)",
					 psPMRNode->psPMR,
					 eError));
			PVR_ASSERT(IMG_FALSE);
		}

		/* update available pages in freelist */
		ui32OldValue = psFreeList->ui32CurrentFLPages;
		psFreeList->ui32CurrentFLPages -= psPMRNode->ui32NumPages;

		/* check underflow */
		PVR_ASSERT(ui32OldValue > psFreeList->ui32CurrentFLPages);

		PVR_DPF((PVR_DBG_MESSAGE, "Freelist [%p]: shrink by %u pages (current pages %u/%u)",
								psFreeList,
								psPMRNode->ui32NumPages,
								psFreeList->ui32CurrentFLPages,
								psFreeList->ui32MaxFLPages));

		OSFreeMem(psPMRNode);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING,"Freelist [0x%p]: shrink denied. PB already at initial PB size (%u pages)",
								psFreeList,
								psFreeList->ui32InitFLPages));
		eError = PVRSRV_ERROR_PBSIZE_ALREADY_MIN;
	}

	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	return eError;
}

static RGX_FREELIST *FindFreeList(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32FreelistID)
{
	DLLIST_NODE *psNode, *psNext;
	RGX_FREELIST *psFreeList = NULL;

	OSLockAcquire(psDevInfo->hLockFreeList);

	dllist_foreach_node(&psDevInfo->sFreeListHead, psNode, psNext)
	{
		RGX_FREELIST *psThisFreeList = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);

		if (psThisFreeList->ui32FreelistID == ui32FreelistID)
		{
			psFreeList = psThisFreeList;
			break;
		}
	}

	OSLockRelease(psDevInfo->hLockFreeList);
	return psFreeList;
}

void RGXProcessRequestGrow(PVRSRV_RGXDEV_INFO *psDevInfo,
						   IMG_UINT32 ui32FreelistID)
{
	RGX_FREELIST *psFreeList = NULL;
	RGXFWIF_KCCB_CMD s3DCCBCmd;
	IMG_UINT32 ui32GrowValue;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	psFreeList = FindFreeList(psDevInfo, ui32FreelistID);

	if (psFreeList)
	{
		/* Try to grow the freelist */
		eError = RGXGrowFreeList(psFreeList,
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
			PVR_DPF((PVR_DBG_ERROR,"Grow for FreeList %p failed (error %u)",
									psFreeList,
									eError));
		}

		/* send feedback */
		s3DCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE;
		s3DCCBCmd.uCmdData.sFreeListGSData.sFreeListFWDevVAddr.ui32Addr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32DeltaSize = ui32GrowValue;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32NewSize = psFreeList->ui32CurrentFLPages;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_3D,
										&s3DCCBCmd,
										sizeof(s3DCCBCmd),
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

static void _RGXCheckFreeListReconstruction(PDLLIST_NODE psNode)
{

	PVRSRV_RGXDEV_INFO 		*psDevInfo;
	RGX_FREELIST			*psFreeList;
	RGX_PMR_NODE			*psPMRNode;
	PVRSRV_ERROR			eError;
	IMG_DEVMEM_OFFSET_T		uiOffset;
	IMG_DEVMEM_SIZE_T		uiLength;
	IMG_UINT32				ui32StartPage;

	psPMRNode = IMG_CONTAINER_OF(psNode, RGX_PMR_NODE, sMemoryBlock);
	psFreeList = psPMRNode->psFreeList;
	PVR_ASSERT(psFreeList);
	psDevInfo = psFreeList->psDevInfo;
	PVR_ASSERT(psDevInfo);

	uiLength = psPMRNode->ui32NumPages * sizeof(IMG_UINT32);
	ui32StartPage = (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages - psPMRNode->ui32NumPages);
	uiOffset = psFreeList->uiFreeListPMROffset + (ui32StartPage * sizeof(IMG_UINT32));

	PMRUnwritePMPageList(psPMRNode->psPageList);
	psPMRNode->psPageList = NULL;
	eError = PMRWritePMPageList(
						/* Target PMR, offset, and length */
						psFreeList->psFreeListPMR,
						uiOffset,
						uiLength,
						/* Referenced PMR, and "page" granularity */
						psPMRNode->psPMR,
						RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
						&psPMRNode->psPageList);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"Error (%u) writing FL 0x%08x", eError, (IMG_UINT32)psFreeList->ui32FreelistID));
	}

	/* Zeroing physical pages pointed by the reconstructed freelist */
	if (psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"_RGXCheckFreeListReconstruction: Failed to zero PMR %p of freelist %p with Error %d",
									psPMRNode->psPMR,
									psFreeList,
									eError));
			PVR_ASSERT(0);
		}
	}

	psFreeList->ui32CurrentFLPages += psPMRNode->ui32NumPages;
}


static PVRSRV_ERROR RGXReconstructFreeList(RGX_FREELIST *psFreeList)
{
	DLLIST_NODE       *psNode, *psNext;
	RGXFWIF_FREELIST  *psFWFreeList;
	PVRSRV_ERROR      eError;

	//PVR_DPF((PVR_DBG_ERROR, "FreeList RECONSTRUCTION: Reconstructing freelist %p (ID=%u)", psFreeList, psFreeList->ui32FreelistID));

	/* Do the FreeList Reconstruction */
	psFreeList->ui32CurrentFLPages = 0;

	/* Reconstructing Init FreeList pages */
	dllist_foreach_node(&psFreeList->sMemoryBlockInitHead, psNode, psNext)
	{
		_RGXCheckFreeListReconstruction(psNode);
	}

	/* Reconstructing Grow FreeList pages */
	dllist_foreach_node(&psFreeList->sMemoryBlockHead, psNode, psNext)
	{
		_RGXCheckFreeListReconstruction(psNode);
	}

	/* Reset the firmware freelist structure */
	eError = DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (void **)&psFWFreeList);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	psFWFreeList->ui32CurrentStackTop       = psFWFreeList->ui32CurrentPages - 1;
	psFWFreeList->ui32AllocatedPageCount    = 0;
	psFWFreeList->ui32AllocatedMMUPageCount = 0;
	psFWFreeList->ui32HWRCounter++;

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);

	/* Check the Freelist checksum if required (as the list is fully populated) */
	if (psFreeList->bCheckFreelist)
	{
		IMG_UINT64  ui64CheckSum;
		
		_CheckFreelist(psFreeList, psFreeList->ui32CurrentFLPages, psFreeList->ui64FreelistChecksum, &ui64CheckSum);
	}

	return eError;
}


void RGXProcessRequestFreelistsReconstruction(PVRSRV_RGXDEV_INFO *psDevInfo,
                                              IMG_UINT32 ui32FreelistsCount,
                                              IMG_UINT32 *paui32Freelists)
{
	PVRSRV_ERROR      eError = PVRSRV_OK;
	DLLIST_NODE       *psNode, *psNext;
	IMG_UINT32        ui32Loop;
	RGXFWIF_KCCB_CMD  sTACCBCmd;
	
	PVR_ASSERT(psDevInfo != NULL);
	PVR_ASSERT(ui32FreelistsCount <= (MAX_HW_TA3DCONTEXTS * RGXFW_MAX_FREELISTS));

	//PVR_DPF((PVR_DBG_ERROR, "FreeList RECONSTRUCTION: %u freelist(s) requested for reconstruction", ui32FreelistsCount));

	/*
	 *  Initialise the response command (in case we don't find a freelist ID)...
	 */
	sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE;
	sTACCBCmd.uCmdData.sFreeListsReconstructionData.ui32FreelistsCount = ui32FreelistsCount;

	for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
	{
		sTACCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] = paui32Freelists[ui32Loop] |
		                                                                             RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG;
	}

	/*
	 *  The list of freelists we have been given for reconstruction will
	 *  consist of local and global freelists (maybe MMU as well). Any
	 *  local freelists will have their global list specified as well.
	 *  However there may be other local freelists not listed, which are
	 *  going to have their global freelist reconstructed. Therefore we
	 *  have to find those freelists as well meaning we will have to
	 *  iterate the entire list of freelists to find which must be reconstructed.
	 */
	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_foreach_node(&psDevInfo->sFreeListHead, psNode, psNext)
	{
		RGX_FREELIST  *psFreeList  = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);
		IMG_BOOL      bReconstruct = IMG_FALSE;

		/*
		 *  Check if this freelist needs to be reconstructed (was it requested
		 *  or was its global freelist requested)...
		 */
		for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
		{
			if (paui32Freelists[ui32Loop] == psFreeList->ui32FreelistID  ||
			    paui32Freelists[ui32Loop] == psFreeList->ui32FreelistGlobalID)
			{
				bReconstruct = IMG_TRUE;
				break;
			}
		}

		if (bReconstruct)
		{
			eError = RGXReconstructFreeList(psFreeList);
			if (eError == PVRSRV_OK)
			{
				for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
				{
					if (paui32Freelists[ui32Loop] == psFreeList->ui32FreelistID)
					{
						/* Reconstruction of this requested freelist was successful... */
						sTACCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] &= ~RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG;
						break;
					}
				}
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR,"Reconstructing of FreeList %p failed (error %u)",
						 psFreeList,
						 eError));
			}
		}
	}
	OSLockRelease(psDevInfo->hLockFreeList);

	/* Check that all freelists were found and reconstructed... */
	for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
	{
		PVR_ASSERT((sTACCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] &
		            RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG) == 0);
	}

	/* send feedback */
	LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
	{
		eError = RGXScheduleCommand(psDevInfo,
		                            RGXFWIF_DM_TA,
		                            &sTACCBCmd,
		                            sizeof(sTACCBCmd),
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

/* Create HWRTDataSet */
IMG_EXPORT
PVRSRV_ERROR RGXCreateHWRTData(CONNECTION_DATA      *psConnection,
                               PVRSRV_DEVICE_NODE	*psDeviceNode,
							   IMG_UINT32			psRenderTarget, /* FIXME this should not be IMG_UINT32 */
							   IMG_DEV_VIRTADDR		psPMMListDevVAddr,
							   IMG_DEV_VIRTADDR		psVFPPageTableAddr,
							   RGX_FREELIST			*apsFreeLists[RGXFW_MAX_FREELISTS],
							   RGX_RTDATA_CLEANUP_DATA	**ppsCleanupData,
							   DEVMEM_MEMDESC		**ppsRTACtlMemDesc,
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
							   DEVMEM_MEMDESC		**ppsMemDesc,
							   IMG_UINT32			*puiHWRTData)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	RGXFWIF_DEV_VIRTADDR pFirmwareAddr;
	RGXFWIF_HWRTDATA *psHWRTData;
	RGXFWIF_RTA_CTL *psRTACtl;
	IMG_UINT32 ui32Loop;
	RGX_RTDATA_CLEANUP_DATA *psTmpCleanup;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	/* Prepare cleanup struct */
	psTmpCleanup = OSAllocZMem(sizeof(*psTmpCleanup));
	if (psTmpCleanup == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto AllocError;
	}

	*ppsCleanupData = psTmpCleanup;

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psTmpCleanup->psCleanupSync,
						   "HWRTData cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto SyncAlloc;
	}

	psDevInfo = psDeviceNode->pvDevice;

	/*
	 * This FW RT-Data is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(RGXFWIF_HWRTDATA),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FwHWRTData",
							ppsMemDesc);
	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateHWRTData: DevmemAllocate for RGX_FWIF_HWRTDATA failed"));
		goto FWRTDataAllocateError;
	}

	psTmpCleanup->psDeviceNode = psDeviceNode;
	psTmpCleanup->psFWHWRTDataMemDesc = *ppsMemDesc;

	RGXSetFirmwareAddress(&pFirmwareAddr, *ppsMemDesc, 0, RFW_FWADDR_FLAG_NONE);

	*puiHWRTData = pFirmwareAddr.ui32Addr;

	eError = DevmemAcquireCpuVirtAddr(*ppsMemDesc, (void **)&psHWRTData);
	PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", FWRTDataCpuMapError);

	/* FIXME: MList is something that that PM writes physical addresses to,
	 * so ideally its best allocated in kernel */
	psHWRTData->psPMMListDevVAddr = psPMMListDevVAddr;
	psHWRTData->psParentRenderTarget.ui32Addr = psRenderTarget;
	#if defined(SUPPORT_VFP)
	psHWRTData->sVFPPageTableAddr = psVFPPageTableAddr;
	#endif

	psHWRTData->ui32PPPScreen         = ui32PPPScreen;
	psHWRTData->ui32PPPGridOffset     = ui32PPPGridOffset;
	psHWRTData->ui64PPPMultiSampleCtl = ui64PPPMultiSampleCtl;
	psHWRTData->ui32TPCStride         = ui32TPCStride;
	psHWRTData->sTailPtrsDevVAddr     = sTailPtrsDevVAddr;
	psHWRTData->ui32TPCSize           = ui32TPCSize;
	psHWRTData->ui32TEScreen          = ui32TEScreen;
	psHWRTData->ui32TEAA              = ui32TEAA;
	psHWRTData->ui32TEMTILE1          = ui32TEMTILE1;
	psHWRTData->ui32TEMTILE2          = ui32TEMTILE2;
	psHWRTData->ui32MTileStride       = ui32MTileStride;
	psHWRTData->ui32ISPMergeLowerX = ui32ISPMergeLowerX;
	psHWRTData->ui32ISPMergeLowerY = ui32ISPMergeLowerY;
	psHWRTData->ui32ISPMergeUpperX = ui32ISPMergeUpperX;
	psHWRTData->ui32ISPMergeUpperY = ui32ISPMergeUpperY;
	psHWRTData->ui32ISPMergeScaleX = ui32ISPMergeScaleX;
	psHWRTData->ui32ISPMergeScaleY = ui32ISPMergeScaleY;

	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		psTmpCleanup->apsFreeLists[ui32Loop] = apsFreeLists[ui32Loop];
		psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount++;
		psHWRTData->apsFreeLists[ui32Loop].ui32Addr = psTmpCleanup->apsFreeLists[ui32Loop]->sFreeListFWDevVAddr.ui32Addr;		
		/* invalid initial snapshot value, the snapshot is always taken during first kick
		 * and hence the value get replaced during the first kick anyway. So it's safe to set it 0.
		*/
		psHWRTData->aui32FreeListHWRSnapshot[ui32Loop] = 0;
	}
	OSLockRelease(psDevInfo->hLockFreeList);
	
	PDUMPCOMMENT("Allocate RGXFW RTA control");
	eError = DevmemFwAllocate(psDevInfo,
										sizeof(RGXFWIF_RTA_CTL),
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
										PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
										PVRSRV_MEMALLOCFLAG_GPU_READABLE |
										PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
										PVRSRV_MEMALLOCFLAG_UNCACHED |
										PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
										"FwRTAControl",
										ppsRTACtlMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate RGX RTA control (%u)",
				eError));
		goto FWRTAAllocateError;
	}
	psTmpCleanup->psRTACtlMemDesc = *ppsRTACtlMemDesc;
	RGXSetFirmwareAddress(&psHWRTData->psRTACtl,
								   *ppsRTACtlMemDesc,
								   0, RFW_FWADDR_FLAG_NONE);
	
	eError = DevmemAcquireCpuVirtAddr(*ppsRTACtlMemDesc, (void **)&psRTACtl);
	PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", FWRTACpuMapError);
	psRTACtl->ui32RenderTargetIndex = 0;
	psRTACtl->ui32ActiveRenderTargets = 0;

	if (ui16MaxRTs > 1)
	{
		/* Allocate memory for the checks */
		PDUMPCOMMENT("Allocate memory for shadow render target cache");
		eError = DevmemFwAllocate(psDevInfo,
								ui16MaxRTs * sizeof(IMG_UINT32),
								PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
								PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
								PVRSRV_MEMALLOCFLAG_GPU_READABLE |
								PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
								PVRSRV_MEMALLOCFLAG_UNCACHED|
								PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
								"FwShadowRTCache",
								&psTmpCleanup->psRTArrayMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate %d bytes for render target array (%u)",
				ui16MaxRTs, eError));
			goto FWAllocateRTArryError;
		}

		RGXSetFirmwareAddress(&psRTACtl->sValidRenderTargets,
										psTmpCleanup->psRTArrayMemDesc,
										0, RFW_FWADDR_FLAG_NONE);

		/* Allocate memory for the checks */
		PDUMPCOMMENT("Allocate memory for tracking renders accumulation");
		eError = DevmemFwAllocate(psDevInfo,
                                                        ui16MaxRTs * sizeof(IMG_UINT32),
                                                        PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
                                                        PVRSRV_MEMALLOCFLAG_GPU_READABLE |
                                                        PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
                                                        PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
                                                        PVRSRV_MEMALLOCFLAG_UNCACHED|
                                                        PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC,
                                                        "FwRendersAccumulation",
                                                        &psTmpCleanup->psRendersAccArrayMemDesc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXCreateHWRTData: Failed to allocate %d bytes for render target array (%u) (renders accumulation)",
						  ui16MaxRTs, eError));
			goto FWAllocateRTAccArryError;
		}

		RGXSetFirmwareAddress(&psRTACtl->sNumRenders,
                                                psTmpCleanup->psRendersAccArrayMemDesc,
                                                0, RFW_FWADDR_FLAG_NONE);
		psRTACtl->ui16MaxRTs = ui16MaxRTs;
	}
	else
	{
		psRTACtl->sValidRenderTargets.ui32Addr = 0;
		psRTACtl->sNumRenders.ui32Addr = 0;
		psRTACtl->ui16MaxRTs = 1;
	}
		
	PDUMPCOMMENT("Dump HWRTData 0x%08X", *puiHWRTData);
	DevmemPDumpLoadMem(*ppsMemDesc, 0, sizeof(*psHWRTData), PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("Dump RTACtl");
	DevmemPDumpLoadMem(*ppsRTACtlMemDesc, 0, sizeof(*psRTACtl), PDUMP_FLAGS_CONTINUOUS);

	DevmemReleaseCpuVirtAddr(*ppsMemDesc);
	DevmemReleaseCpuVirtAddr(*ppsRTACtlMemDesc);
	return PVRSRV_OK;

FWAllocateRTAccArryError:
	DevmemFwFree(psDevInfo, psTmpCleanup->psRTArrayMemDesc);
FWAllocateRTArryError:
	DevmemReleaseCpuVirtAddr(*ppsRTACtlMemDesc);
FWRTACpuMapError:
	RGXUnsetFirmwareAddress(*ppsRTACtlMemDesc);
	DevmemFwFree(psDevInfo, *ppsRTACtlMemDesc);
FWRTAAllocateError:
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psTmpCleanup->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
	OSLockRelease(psDevInfo->hLockFreeList);
	DevmemReleaseCpuVirtAddr(*ppsMemDesc);
FWRTDataCpuMapError:
	RGXUnsetFirmwareAddress(*ppsMemDesc);
	DevmemFwFree(psDevInfo, *ppsMemDesc);
FWRTDataAllocateError:
	SyncPrimFree(psTmpCleanup->psCleanupSync);
SyncAlloc:
	*ppsCleanupData = NULL;
	OSFreeMem(psTmpCleanup);

AllocError:
	return eError;
}

/* Destroy HWRTDataSet */
IMG_EXPORT
PVRSRV_ERROR RGXDestroyHWRTData(RGX_RTDATA_CLEANUP_DATA *psCleanupData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR eError;
	PRGXFWIF_HWRTDATA psHWRTData;
	IMG_UINT32 ui32Loop;

	PVR_ASSERT(psCleanupData);

	RGXSetFirmwareAddress(&psHWRTData, psCleanupData->psFWHWRTDataMemDesc, 0, RFW_FWADDR_NOREF_FLAG);

	/* Cleanup HWRTData in TA */
	eError = RGXFWRequestHWRTDataCleanUp(psCleanupData->psDeviceNode,
										 psHWRTData,
										 psCleanupData->psCleanupSync,
										 RGXFWIF_DM_TA);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}

	psDevInfo = psCleanupData->psDeviceNode->pvDevice;

	/* Cleanup HWRTData in 3D */
	eError = RGXFWRequestHWRTDataCleanUp(psCleanupData->psDeviceNode,
										 psHWRTData,
										 psCleanupData->psCleanupSync,
										 RGXFWIF_DM_3D);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}

	/* If we got here then TA and 3D operations on this RTData have finished */
	if(psCleanupData->psRTACtlMemDesc)
	{
		RGXUnsetFirmwareAddress(psCleanupData->psRTACtlMemDesc);
		DevmemFwFree(psDevInfo, psCleanupData->psRTACtlMemDesc);
	}
	
	RGXUnsetFirmwareAddress(psCleanupData->psFWHWRTDataMemDesc);
	DevmemFwFree(psDevInfo, psCleanupData->psFWHWRTDataMemDesc);
	
	if(psCleanupData->psRTArrayMemDesc)
	{
		RGXUnsetFirmwareAddress(psCleanupData->psRTArrayMemDesc);
		DevmemFwFree(psDevInfo, psCleanupData->psRTArrayMemDesc);
	}

	if(psCleanupData->psRendersAccArrayMemDesc)
	{
		RGXUnsetFirmwareAddress(psCleanupData->psRendersAccArrayMemDesc);
		DevmemFwFree(psDevInfo, psCleanupData->psRendersAccArrayMemDesc);
	}

	SyncPrimFree(psCleanupData->psCleanupSync);

	/* decrease freelist refcount */
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psCleanupData->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psCleanupData->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
	OSLockRelease(psDevInfo->hLockFreeList);

	OSFreeMem(psCleanupData);

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR RGXCreateFreeList(CONNECTION_DATA      *psConnection,
                               PVRSRV_DEVICE_NODE	*psDeviceNode, 
							   IMG_UINT32			ui32MaxFLPages,
							   IMG_UINT32			ui32InitFLPages,
							   IMG_UINT32			ui32GrowFLPages,
							   RGX_FREELIST			*psGlobalFreeList,
							   IMG_BOOL				bCheckFreelist,
							   IMG_DEV_VIRTADDR		sFreeListDevVAddr,
							   PMR					*psFreeListPMR,
							   IMG_DEVMEM_OFFSET_T	uiFreeListPMROffset,
							   RGX_FREELIST			**ppsFreeList)
{
	PVRSRV_ERROR				eError;
	RGXFWIF_FREELIST			*psFWFreeList;
	DEVMEM_MEMDESC				*psFWFreelistMemDesc;
	RGX_FREELIST				*psFreeList;
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

	/* Allocate kernel freelist struct */
	psFreeList = OSAllocZMem(sizeof(*psFreeList));
	if (psFreeList == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateFreeList: failed to allocate host data structure"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psFreeList->psCleanupSync,
						   "ta3d free list cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateFreeList: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto SyncAlloc;
	}

	/*
	 * This FW FreeList context is only mapped into kernel for initialisation
	 * and reconstruction (at other times it is not mapped and only used by
	 * the FW. Therefore the GPU cache doesn't need coherency, and write-combine
	 * is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFWFreeList),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FwFreeList",
							&psFWFreelistMemDesc);
	if (eError != PVRSRV_OK) 
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateFreeList: DevmemAllocate for RGXFWIF_FREELIST failed"));
		goto FWFreeListAlloc;
	}

	/* Initialise host data structures */
	psFreeList->psDevInfo = psDevInfo;
	psFreeList->psFreeListPMR = psFreeListPMR;
	psFreeList->uiFreeListPMROffset = uiFreeListPMROffset;
	psFreeList->psFWFreelistMemDesc = psFWFreelistMemDesc;
	RGXSetFirmwareAddress(&psFreeList->sFreeListFWDevVAddr, psFWFreelistMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	psFreeList->ui32FreelistID = psDevInfo->ui32FreelistCurrID++;
	psFreeList->ui32FreelistGlobalID = (psGlobalFreeList ? psGlobalFreeList->ui32FreelistID : 0);
	psFreeList->ui32MaxFLPages = ui32MaxFLPages;
	psFreeList->ui32InitFLPages = ui32InitFLPages;
	psFreeList->ui32GrowFLPages = ui32GrowFLPages;
	psFreeList->ui32CurrentFLPages = 0;
	psFreeList->ui64FreelistChecksum = 0;
	psFreeList->ui32RefCount = 0;
	psFreeList->bCheckFreelist = bCheckFreelist;
	dllist_init(&psFreeList->sMemoryBlockHead);
	dllist_init(&psFreeList->sMemoryBlockInitHead);


	/* Add to list of freelists */
	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_add_to_tail(&psDevInfo->sFreeListHead, &psFreeList->sNode);
	OSLockRelease(psDevInfo->hLockFreeList);


	/* Initialise FW data structure */
	eError = DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (void **)&psFWFreeList);
	PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", FWFreeListCpuMap);

	psFWFreeList->ui32MaxPages = ui32MaxFLPages;
	psFWFreeList->ui32CurrentPages = ui32InitFLPages;
	psFWFreeList->ui32GrowPages = ui32GrowFLPages;
	psFWFreeList->ui32CurrentStackTop = ui32InitFLPages - 1;
	psFWFreeList->psFreeListDevVAddr = sFreeListDevVAddr;
	psFWFreeList->ui64CurrentDevVAddr = sFreeListDevVAddr.uiAddr + ((ui32MaxFLPages - ui32InitFLPages) * sizeof(IMG_UINT32));
	psFWFreeList->ui32FreeListID = psFreeList->ui32FreelistID;
	psFWFreeList->bGrowPending = IMG_FALSE;

	PVR_DPF((PVR_DBG_MESSAGE,"Freelist %p created: Max pages 0x%08x, Init pages 0x%08x, Max FL base address 0x%016llx, Init FL base address 0x%016llx",
			psFreeList,
			ui32MaxFLPages,
			ui32InitFLPages,
			sFreeListDevVAddr.uiAddr,
			psFWFreeList->psFreeListDevVAddr.uiAddr));

	PDUMPCOMMENT("Dump FW FreeList");
	DevmemPDumpLoadMem(psFreeList->psFWFreelistMemDesc, 0, sizeof(*psFWFreeList), PDUMP_FLAGS_CONTINUOUS);

	/*
	 * Separate dump of the Freelist's number of Pages and stack pointer.
	 * This allows to easily modify the PB size in the out2.txt files.
	 */
	PDUMPCOMMENT("FreeList TotalPages");
	DevmemPDumpLoadMemValue32(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_FREELIST, ui32CurrentPages),
							psFWFreeList->ui32CurrentPages,
							PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT("FreeList StackPointer");
	DevmemPDumpLoadMemValue32(psFreeList->psFWFreelistMemDesc,
							offsetof(RGXFWIF_FREELIST, ui32CurrentStackTop),
							psFWFreeList->ui32CurrentStackTop,
							PDUMP_FLAGS_CONTINUOUS);

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);


	/* Add initial PB block */
	eError = RGXGrowFreeList(psFreeList,
								ui32InitFLPages,
								&psFreeList->sMemoryBlockInitHead);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"RGXCreateFreeList: failed to allocate initial memory block for free list 0x%016llx (error = %u)",
				sFreeListDevVAddr.uiAddr,
				eError));
		goto FWFreeListCpuMap;
	}
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
			/* Update Stats */
			PVRSRVStatsUpdateFreelistStats(1, /* Add 1 to the appropriate counter (Requests by App)*/
	                               0,
	                               psFreeList->ui32InitFLPages,
	                               psFreeList->ui32NumHighPages,
	                               psFreeList->ownerPid);

#endif

	psFreeList->ownerPid = OSGetCurrentClientProcessIDKM();
	/* return values */
	*ppsFreeList = psFreeList;

	return PVRSRV_OK;

	/* Error handling */

FWFreeListCpuMap:
	/* Remove freelists from list  */
	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_remove_node(&psFreeList->sNode);
	OSLockRelease(psDevInfo->hLockFreeList);

	RGXUnsetFirmwareAddress(psFWFreelistMemDesc);
	DevmemFwFree(psDevInfo, psFWFreelistMemDesc);

FWFreeListAlloc:
	SyncPrimFree(psFreeList->psCleanupSync);

SyncAlloc:
	OSFreeMem(psFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyFreeList
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyFreeList(RGX_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psFreeList);

	if (psFreeList->ui32RefCount != 0)
	{
		/* Freelist still busy */
		return PVRSRV_ERROR_RETRY;
	}

	/* Freelist is not in use => start firmware cleanup */
	eError = RGXFWRequestFreeListCleanUp(psFreeList->psDevInfo,
										 psFreeList->sFreeListFWDevVAddr,
										 psFreeList->psCleanupSync);
	if(eError != PVRSRV_OK)
	{
		/* Can happen if the firmware took too long to handle the cleanup request,
		 * or if SLC-flushes didn't went through (due to some GPU lockup) */
		return eError;
	}

	/* Remove FreeList from linked list before we destroy it... */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);
	dllist_remove_node(&psFreeList->sNode);
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	if (psFreeList->bCheckFreelist)
	{
		RGXFWIF_FREELIST  *psFWFreeList;
		IMG_UINT64        ui32CurrentStackTop;
		IMG_UINT64        ui64CheckSum;
		
		/* Get the current stack pointer for this free list */
		DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (void **)&psFWFreeList);
		ui32CurrentStackTop = psFWFreeList->ui32CurrentStackTop;
		DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);
		
		if (ui32CurrentStackTop == psFreeList->ui32CurrentFLPages-1)
		{
			/* Do consistency tests (as the list is fully populated) */
			_CheckFreelist(psFreeList, psFreeList->ui32CurrentFLPages, psFreeList->ui64FreelistChecksum, &ui64CheckSum);
		}
		else
		{
			/* Check for duplicate pages, but don't check the checksum as the list is not fully populated */
			_CheckFreelist(psFreeList, ui32CurrentStackTop+1, 0, &ui64CheckSum);
		}
	}

	/* Destroy FW structures */
	RGXUnsetFirmwareAddress(psFreeList->psFWFreelistMemDesc);
	DevmemFwFree(psFreeList->psDevInfo, psFreeList->psFWFreelistMemDesc);

	/* Remove grow shrink blocks */
	while (!dllist_is_empty(&psFreeList->sMemoryBlockHead))
	{
		eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockHead, psFreeList);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	/* Remove initial PB block */
	eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockInitHead, psFreeList);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* consistency checks */
	PVR_ASSERT(dllist_is_empty(&psFreeList->sMemoryBlockInitHead));
	PVR_ASSERT(psFreeList->ui32CurrentFLPages == 0);

	SyncPrimFree(psFreeList->psCleanupSync);

	/* free Freelist */
	OSFreeMem(psFreeList);

	return eError;
}



/*
	RGXAddBlockToFreeListKM
*/

IMG_EXPORT
PVRSRV_ERROR RGXAddBlockToFreeListKM(RGX_FREELIST *psFreeList,
										IMG_UINT32 ui32NumPages)
{
	PVRSRV_ERROR eError;

	/* Check if we have reference to freelist's PMR */
	if (psFreeList->psFreeListPMR == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,	"Freelist is not configured for grow"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* grow freelist */
	eError = RGXGrowFreeList(psFreeList,
							ui32NumPages,
							&psFreeList->sMemoryBlockHead);
	if(eError == PVRSRV_OK)
	{
		/* update freelist data in firmware */
		_UpdateFwFreelistSize(psFreeList, IMG_TRUE, ui32NumPages);

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
	RGXRemoveBlockFromFreeListKM
*/

IMG_EXPORT
PVRSRV_ERROR RGXRemoveBlockFromFreeListKM(RGX_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;

	/*
	 * Make sure the pages part of the memory block are not in use anymore.
	 * Instruct the firmware to update the freelist pointers accordingly.
	 */

	eError = RGXShrinkFreeList(&psFreeList->sMemoryBlockHead,
								psFreeList);

	return eError;
}


/*
	RGXCreateRenderTarget
*/
IMG_EXPORT
PVRSRV_ERROR RGXCreateRenderTarget(CONNECTION_DATA      *psConnection,
                                   PVRSRV_DEVICE_NODE	*psDeviceNode, 
								   IMG_DEV_VIRTADDR		psVHeapTableDevVAddr,
								   RGX_RT_CLEANUP_DATA 	**ppsCleanupData,
								   IMG_UINT32			*sRenderTargetFWDevVAddr)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RENDER_TARGET	*psRenderTarget;
	RGXFWIF_DEV_VIRTADDR	pFirmwareAddr;
	PVRSRV_RGXDEV_INFO 		*psDevInfo = psDeviceNode->pvDevice;
	RGX_RT_CLEANUP_DATA		*psCleanupData;

	PVR_UNREFERENCED_PARAMETER(psConnection);
	
	psCleanupData = OSAllocZMem(sizeof(*psCleanupData));
	if (psCleanupData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_out;
	}

	psCleanupData->psDeviceNode = psDeviceNode;
	/*
	 * This FW render target context is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency,
	 * and write-combine is suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psRenderTarget),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FwRenderTarget",
							&psCleanupData->psRenderTargetMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXCreateRenderTarget: DevmemAllocate for Render Target failed"));
		goto err_free;
	}
	RGXSetFirmwareAddress(&pFirmwareAddr, psCleanupData->psRenderTargetMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	*sRenderTargetFWDevVAddr = pFirmwareAddr.ui32Addr;

	eError = DevmemAcquireCpuVirtAddr(psCleanupData->psRenderTargetMemDesc, (void **)&psRenderTarget);
	PVR_LOGG_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", err_fwalloc);

	psRenderTarget->psVHeapTableDevVAddr = psVHeapTableDevVAddr;
	psRenderTarget->bTACachesNeedZeroing = IMG_FALSE;
	PDUMPCOMMENT("Dump RenderTarget");
	DevmemPDumpLoadMem(psCleanupData->psRenderTargetMemDesc, 0, sizeof(*psRenderTarget), PDUMP_FLAGS_CONTINUOUS);
	DevmemReleaseCpuVirtAddr(psCleanupData->psRenderTargetMemDesc);

	*ppsCleanupData = psCleanupData;

err_out:
	return eError;

err_free:
	OSFreeMem(psCleanupData);
	goto err_out;

err_fwalloc:
	DevmemFwFree(psDevInfo, psCleanupData->psRenderTargetMemDesc);
	goto err_free;

}


/*
	RGXDestroyRenderTarget
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyRenderTarget(RGX_RT_CLEANUP_DATA *psCleanupData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psCleanupData->psDeviceNode;

	RGXUnsetFirmwareAddress(psCleanupData->psRenderTargetMemDesc);

	/*
		Note:
		When we get RT cleanup in the FW call that instead
	*/
	/* Flush the the SLC before freeing */
	{
		RGXFWIF_KCCB_CMD sFlushInvalCmd;
		PVRSRV_ERROR eError;

		/* Schedule the SLC flush command ... */
#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(PDUMP_FLAGS_CONTINUOUS, "Submit SLC flush and invalidate");
#endif
		sFlushInvalCmd.eCmdType = RGXFWIF_KCCB_CMD_SLCFLUSHINVAL;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bInval = IMG_TRUE;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.bDMContext = IMG_FALSE;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.eDM = 0;
		sFlushInvalCmd.uCmdData.sSLCFlushInvalData.psContext.ui32Addr = 0;

		eError = RGXSendCommandWithPowLock(psDeviceNode->pvDevice,
											RGXFWIF_DM_GP,
											&sFlushInvalCmd,
											sizeof(sFlushInvalCmd),
											PDUMP_FLAGS_CONTINUOUS);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"RGXDestroyRenderTarget: Failed to schedule SLC flush command with error (%u)", eError));
		}
		else
		{
			/* Wait for the SLC flush to complete */
			eError = RGXWaitForFWOp(psDeviceNode->pvDevice, RGXFWIF_DM_GP, psDeviceNode->psSyncPrim, PDUMP_FLAGS_CONTINUOUS);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"RGXDestroyRenderTarget: SLC flush and invalidate aborted with error (%u)", eError));
			}
		}
	}

	DevmemFwFree(psDeviceNode->pvDevice, psCleanupData->psRenderTargetMemDesc);
	OSFreeMem(psCleanupData);
	return PVRSRV_OK;
}

/*
	RGXCreateZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXCreateZSBufferKM(CONNECTION_DATA * psConnection,
                                 PVRSRV_DEVICE_NODE	*psDeviceNode,
                                 DEVMEMINT_RESERVATION 	*psReservation,
                                 PMR 					*psPMR,
                                 PVRSRV_MEMALLOCFLAGS_T 	uiMapFlags,
                                 RGX_ZSBUFFER_DATA **ppsZSBuffer,
                                 IMG_UINT32 *pui32ZSBufferFWDevVAddr)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_FWZSBUFFER			*psFWZSBuffer;
	RGX_ZSBUFFER_DATA			*psZSBuffer;
	DEVMEM_MEMDESC				*psFWZSBufferMemDesc;
	IMG_BOOL					bOnDemand = PVRSRV_CHECK_ON_DEMAND(uiMapFlags) ? IMG_TRUE : IMG_FALSE;

	/* Allocate host data structure */
	psZSBuffer = OSAllocZMem(sizeof(*psZSBuffer));
	if (psZSBuffer == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate cleanup data structure for ZS-Buffer"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocCleanup;
	}

	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psZSBuffer->psCleanupSync,
						   "ta3d zs buffer cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto ErrorSyncAlloc;
	}

	/* Populate Host data */
	psZSBuffer->psDevInfo = psDevInfo;
	psZSBuffer->psReservation = psReservation;
	psZSBuffer->psPMR = psPMR;
	psZSBuffer->uiMapFlags = uiMapFlags;
	psZSBuffer->ui32RefCount = 0;
	psZSBuffer->bOnDemand = bOnDemand;
    if (bOnDemand)
    {
    	psZSBuffer->ui32ZSBufferID = psDevInfo->ui32ZSBufferCurrID++;
		psZSBuffer->psMapping = NULL;

		OSLockAcquire(psDevInfo->hLockZSBuffer);
    	dllist_add_to_tail(&psDevInfo->sZSBufferHead, &psZSBuffer->sNode);
		OSLockRelease(psDevInfo->hLockZSBuffer);
    }

	/* Allocate firmware memory for ZS-Buffer. */
	PDUMPCOMMENT("Allocate firmware ZS-Buffer data structure");
	eError = DevmemFwAllocate(psDevInfo,
							sizeof(*psFWZSBuffer),
							PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
							PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
							PVRSRV_MEMALLOCFLAG_GPU_READABLE |
							PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
							PVRSRV_MEMALLOCFLAG_CPU_READABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
							PVRSRV_MEMALLOCFLAG_CPU_WRITE_COMBINE |
							PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE,
							"FwZSBuffer",
							&psFWZSBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to allocate firmware ZS-Buffer (%u)", eError));
		goto ErrorAllocFWZSBuffer;
	}
	psZSBuffer->psZSBufferMemDesc = psFWZSBufferMemDesc;

	/* Temporarily map the firmware render context to the kernel. */
	eError = DevmemAcquireCpuVirtAddr(psFWZSBufferMemDesc,
                                      (void **)&psFWZSBuffer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"RGXCreateZSBufferKM: Failed to map firmware ZS-Buffer (%u)", eError));
		goto ErrorAcquireFWZSBuffer;
	}

	/* Populate FW ZS-Buffer data structure */
	psFWZSBuffer->bOnDemand = bOnDemand;
	psFWZSBuffer->eState = (bOnDemand) ? RGXFWIF_ZSBUFFER_UNBACKED : RGXFWIF_ZSBUFFER_BACKED;
	psFWZSBuffer->ui32ZSBufferID = psZSBuffer->ui32ZSBufferID;

	/* Get firmware address of ZS-Buffer. */
	RGXSetFirmwareAddress(&psZSBuffer->sZSBufferFWDevVAddr, psFWZSBufferMemDesc, 0, RFW_FWADDR_FLAG_NONE);

	/* Dump the ZS-Buffer and the memory content */
	PDUMPCOMMENT("Dump firmware ZS-Buffer");
	DevmemPDumpLoadMem(psFWZSBufferMemDesc, 0, sizeof(*psFWZSBuffer), PDUMP_FLAGS_CONTINUOUS);

	/* Release address acquired above. */
	DevmemReleaseCpuVirtAddr(psFWZSBufferMemDesc);


	/* define return value */
	*ppsZSBuffer = psZSBuffer;
	*pui32ZSBufferFWDevVAddr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;

	PVR_DPF((PVR_DBG_MESSAGE, "ZS-Buffer [%p] created (%s)",
							psZSBuffer,
							(bOnDemand) ? "On-Demand": "Up-front"));

	psZSBuffer->owner=OSGetCurrentClientProcessIDKM();

	return PVRSRV_OK;

	/* error handling */

ErrorAcquireFWZSBuffer:
	DevmemFwFree(psDevInfo, psFWZSBufferMemDesc);

ErrorAllocFWZSBuffer:
	SyncPrimFree(psZSBuffer->psCleanupSync);

ErrorSyncAlloc:
	OSFreeMem(psZSBuffer);

ErrorAllocCleanup:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyZSBuffer
*/
IMG_EXPORT
PVRSRV_ERROR RGXDestroyZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psZSBuffer);
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	/* Request ZS Buffer cleanup */
	eError = RGXFWRequestZSBufferCleanUp(psZSBuffer->psDevInfo,
										psZSBuffer->sZSBufferFWDevVAddr,
										psZSBuffer->psCleanupSync);
	if (eError != PVRSRV_ERROR_RETRY)
	{
		/* Free the firmware render context. */
    	RGXUnsetFirmwareAddress(psZSBuffer->psZSBufferMemDesc);
		DevmemFwFree(psZSBuffer->psDevInfo, psZSBuffer->psZSBufferMemDesc);

	    /* Remove Deferred Allocation from list */
		if (psZSBuffer->bOnDemand)
		{
			OSLockAcquire(hLockZSBuffer);
			PVR_ASSERT(dllist_node_is_in_list(&psZSBuffer->sNode));
			dllist_remove_node(&psZSBuffer->sNode);
			OSLockRelease(hLockZSBuffer);
		}

		SyncPrimFree(psZSBuffer->psCleanupSync);

		PVR_ASSERT(psZSBuffer->ui32RefCount == 0);

		PVR_DPF((PVR_DBG_MESSAGE,"ZS-Buffer [%p] destroyed",psZSBuffer));

		/* Free ZS-Buffer host data structure */
		OSFreeMem(psZSBuffer);

	}

	return eError;
}

PVRSRV_ERROR
RGXBackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	if (!psZSBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (!psZSBuffer->bOnDemand)
	{
		/* Only deferred allocations can be populated */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_DPF((PVR_DBG_MESSAGE,"ZS Buffer [%p, ID=0x%08x]: Physical backing requested",
								psZSBuffer,
								psZSBuffer->ui32ZSBufferID));
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	OSLockAcquire(hLockZSBuffer);

	if (psZSBuffer->ui32RefCount == 0)
	{
		if (psZSBuffer->bOnDemand)
		{
			IMG_HANDLE hDevmemHeap;

			PVR_ASSERT(psZSBuffer->psMapping == NULL);

			/* Get Heap */
			eError = DevmemServerGetHeapHandle(psZSBuffer->psReservation, &hDevmemHeap);
			PVR_ASSERT(psZSBuffer->psMapping == NULL);

			eError = DevmemIntMapPMR(hDevmemHeap,
									psZSBuffer->psReservation,
									psZSBuffer->psPMR,
									psZSBuffer->uiMapFlags,
									&psZSBuffer->psMapping);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Unable populate ZS Buffer [%p, ID=0x%08x] with error %u",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID,
										eError));
				OSLockRelease(hLockZSBuffer);
				return eError;

			}
			PVR_DPF((PVR_DBG_MESSAGE, "ZS Buffer [%p, ID=0x%08x]: Physical backing acquired",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID));
		}
	}

	/* Increase refcount*/
	psZSBuffer->ui32RefCount++;

	OSLockRelease(hLockZSBuffer);

	return PVRSRV_OK;
}


PVRSRV_ERROR
RGXPopulateZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer,
					RGX_POPULATION **ppsPopulation)
{
	RGX_POPULATION *psPopulation;
	PVRSRV_ERROR eError;

	psZSBuffer->ui32NumReqByApp++;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsUpdateZSBufferStats(1,0,psZSBuffer->owner);
#endif

	/* Do the backing */
	eError = RGXBackingZSBuffer(psZSBuffer);
	if (eError != PVRSRV_OK)
	{
		goto OnErrorBacking;
	}

	/* Create the handle to the backing */
	psPopulation = OSAllocMem(sizeof(*psPopulation));
	if (psPopulation == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto OnErrorAlloc;
	}

	psPopulation->psZSBuffer = psZSBuffer;

	/* return value */
	*ppsPopulation = psPopulation;

	return PVRSRV_OK;

OnErrorAlloc:
	RGXUnbackingZSBuffer(psZSBuffer);

OnErrorBacking:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR
RGXUnbackingZSBuffer(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	if (!psZSBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT(psZSBuffer->ui32RefCount);

	PVR_DPF((PVR_DBG_MESSAGE,"ZS Buffer [%p, ID=0x%08x]: Physical backing removal requested",
								psZSBuffer,
								psZSBuffer->ui32ZSBufferID));

	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	OSLockAcquire(hLockZSBuffer);

	if (psZSBuffer->bOnDemand)
	{
		if (psZSBuffer->ui32RefCount == 1)
		{
			PVR_ASSERT(psZSBuffer->psMapping);

			eError = DevmemIntUnmapPMR(psZSBuffer->psMapping);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Unable to unpopulate ZS Buffer [%p, ID=0x%08x] with error %u",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID,
										eError));
				OSLockRelease(hLockZSBuffer);
				return eError;
			}

			PVR_DPF((PVR_DBG_MESSAGE, "ZS Buffer [%p, ID=0x%08x]: Physical backing removed",
										psZSBuffer,
										psZSBuffer->ui32ZSBufferID));
		}
	}

	/* Decrease refcount*/
	psZSBuffer->ui32RefCount--;

	OSLockRelease(hLockZSBuffer);

	return PVRSRV_OK;
}

PVRSRV_ERROR
RGXUnpopulateZSBufferKM(RGX_POPULATION *psPopulation)
{
	PVRSRV_ERROR eError;

	if (!psPopulation)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = RGXUnbackingZSBuffer(psPopulation->psZSBuffer);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	OSFreeMem(psPopulation);

	return PVRSRV_OK;
}

static RGX_ZSBUFFER_DATA *FindZSBuffer(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32ZSBufferID)
{
	DLLIST_NODE *psNode, *psNext;
	RGX_ZSBUFFER_DATA *psZSBuffer = NULL;

	OSLockAcquire(psDevInfo->hLockZSBuffer);

	dllist_foreach_node(&psDevInfo->sZSBufferHead, psNode, psNext)
	{
		RGX_ZSBUFFER_DATA *psThisZSBuffer = IMG_CONTAINER_OF(psNode, RGX_ZSBUFFER_DATA, sNode);

		if (psThisZSBuffer->ui32ZSBufferID == ui32ZSBufferID)
		{
			psZSBuffer = psThisZSBuffer;
			break;
		}
	}

	OSLockRelease(psDevInfo->hLockZSBuffer);
	return psZSBuffer;
}

void RGXProcessRequestZSBufferBacking(PVRSRV_RGXDEV_INFO *psDevInfo,
									  IMG_UINT32 ui32ZSBufferID)
{
	RGX_ZSBUFFER_DATA *psZSBuffer;
	RGXFWIF_KCCB_CMD sTACCBCmd;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	/* scan all deferred allocations */
	psZSBuffer = FindZSBuffer(psDevInfo, ui32ZSBufferID);

	if (psZSBuffer)
	{
		IMG_BOOL bBackingDone = IMG_TRUE;

		/* Populate ZLS */
		eError = RGXBackingZSBuffer(psZSBuffer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Populating ZS-Buffer failed with error %u (ID = 0x%08x)", eError, ui32ZSBufferID));
			bBackingDone = IMG_FALSE;
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.sZSBufferFWDevVAddr.ui32Addr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = bBackingDone;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_TA,
										&sTACCBCmd,
										sizeof(sTACCBCmd),
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

		psZSBuffer->ui32NumReqByFW++;

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
		PVRSRVStatsUpdateZSBufferStats(0,1,psZSBuffer->owner);
#endif

	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (Populate)", ui32ZSBufferID));
	}
}

void RGXProcessRequestZSBufferUnbacking(PVRSRV_RGXDEV_INFO *psDevInfo,
											IMG_UINT32 ui32ZSBufferID)
{
	RGX_ZSBUFFER_DATA *psZSBuffer;
	RGXFWIF_KCCB_CMD sTACCBCmd;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psDevInfo);

	/* scan all deferred allocations */
	psZSBuffer = FindZSBuffer(psDevInfo, ui32ZSBufferID);

	if (psZSBuffer)
	{
		/* Unpopulate ZLS */
		eError = RGXUnbackingZSBuffer(psZSBuffer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"UnPopulating ZS-Buffer failed failed with error %u (ID = 0x%08x)", eError, ui32ZSBufferID));
			PVR_ASSERT(IMG_FALSE);
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.sZSBufferFWDevVAddr.ui32Addr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = IMG_TRUE;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
										RGXFWIF_DM_TA,
										&sTACCBCmd,
										sizeof(sTACCBCmd),
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
		PVR_DPF((PVR_DBG_ERROR,"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (UnPopulate)", ui32ZSBufferID));
	}
}

static
PVRSRV_ERROR _CreateTAContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_DEV_VIRTADDR sVDMCallStackAddr,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RC_TA_DATA *psTAData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_TACTX_STATE *psContextState;
	PVRSRV_ERROR eError;
	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware TA context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_TACTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FwTAContextState",
							  &psTAData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU context suspend state (%u)",
				eError));
		goto fail_tacontextsuspendalloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
                                      (void **)&psContextState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to map firmware render context state (%u)",
				eError));
		goto fail_suspendcpuvirtacquire;
	}
	psContextState->uTAReg_VDM_CALL_STACK_POINTER_Init = sVDMCallStackAddr.uiAddr;
	DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_TA,
									 RGXFWIF_DM_TA,
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
									 psTAData->psContextStateMemDesc,
									 RGX_TA_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &psTAData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to init TA fw common context (%u)",
				eError));
		goto fail_tacommoncontext;
	}
	
	/*
	 * Dump the FW 3D context suspend state buffer
	 */
	PDUMPCOMMENT("Dump the TA context suspend state buffer");
	DevmemPDumpLoadMem(psTAData->psContextStateMemDesc,
					   0,
					   sizeof(RGXFWIF_TACTX_STATE),
					   PDUMP_FLAGS_CONTINUOUS);

	psTAData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_tacommoncontext:
fail_suspendcpuvirtacquire:
	DevmemFwFree(psDevInfo, psTAData->psContextStateMemDesc);
fail_tacontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

static
PVRSRV_ERROR _Create3DContext(CONNECTION_DATA *psConnection,
							  PVRSRV_DEVICE_NODE *psDeviceNode,
							  DEVMEM_MEMDESC *psAllocatedMemDesc,
							  IMG_UINT32 ui32AllocatedOffset,
							  DEVMEM_MEMDESC *psFWMemContextMemDesc,
							  IMG_UINT32 ui32Priority,
							  RGX_COMMON_CONTEXT_INFO *psInfo,
							  RGX_SERVER_RC_3D_DATA *ps3DData)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;

	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	*/
	PDUMPCOMMENT("Allocate RGX firmware 3D context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_3DCTX_STATE),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "Fw3DContextState",
							  &ps3DData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU context suspend state (%u)",
				eError));
		goto fail_3dcontextsuspendalloc;
	}

	eError = FWCommonContextAllocate(psConnection,
									 psDeviceNode,
									 REQ_TYPE_3D,
									 RGXFWIF_DM_3D,
									 psAllocatedMemDesc,
									 ui32AllocatedOffset,
									 psFWMemContextMemDesc,
									 ps3DData->psContextStateMemDesc,
									 RGX_3D_CCB_SIZE_LOG2,
									 ui32Priority,
									 psInfo,
									 &ps3DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to init 3D fw common context (%u)",
				eError));
		goto fail_3dcommoncontext;
	}

	/*
	 * Dump the FW 3D context suspend state buffer
	 */
	PDUMPCOMMENT("Dump the 3D context suspend state buffer");
	DevmemPDumpLoadMem(ps3DData->psContextStateMemDesc,
					   0,
					   sizeof(RGXFWIF_3DCTX_STATE),
					   PDUMP_FLAGS_CONTINUOUS);

	ps3DData->ui32Priority = ui32Priority;
	return PVRSRV_OK;

fail_3dcommoncontext:
	DevmemFwFree(psDevInfo, ps3DData->psContextStateMemDesc);
fail_3dcontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


/*
 * PVRSRVRGXCreateRenderContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXCreateRenderContextKM(CONNECTION_DATA				*psConnection,
											PVRSRV_DEVICE_NODE			*psDeviceNode,
											IMG_UINT32					ui32Priority,
											IMG_DEV_VIRTADDR			sMCUFenceAddr,
											IMG_DEV_VIRTADDR			sVDMCallStackAddr,
											IMG_UINT32					ui32FrameworkRegisterSize,
											IMG_PBYTE					pabyFrameworkRegisters,
											IMG_HANDLE					hMemCtxPrivData,
											RGX_SERVER_RENDER_CONTEXT	**ppsRenderContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 			*psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_RENDER_CONTEXT	*psRenderContext;
	DEVMEM_MEMDESC				*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO		sInfo;

	/* Prepare cleanup structure */
	*ppsRenderContext = NULL;
	psRenderContext = OSAllocZMem(sizeof(*psRenderContext));
	if (psRenderContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRenderContext->psDeviceNode = psDeviceNode;

	/*
		Create the FW render context, this has the TA and 3D FW common
		contexts embedded within it
	*/
	eError = DevmemFwAllocate(psDevInfo,
							  sizeof(RGXFWIF_FWRENDERCONTEXT),
							  RGX_FWCOMCTX_ALLOCFLAGS,
							  "FwRenderContext",
							  &psRenderContext->psFWRenderContextMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto fail_fwrendercontext;
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	WorkEstRCInit(&(psRenderContext->sWorkEstData));
#endif

	/* Allocate cleanup sync */
	eError = SyncPrimAlloc(psDeviceNode->hSyncPrimContext,
						   &psRenderContext->psCleanupSync,
						   "ta3d render context cleanup");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate cleanup sync (0x%x)",
				eError));
		goto fail_syncalloc;
	}

	/* 
	 * Create the FW framework buffer
	 */
	eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode,
										&psRenderContext->psFWFrameworkMemDesc,
										ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to allocate firmware GPU framework state (%u)",
				eError));
		goto fail_frameworkcreate;
	}

	/* Copy the Framework client data into the framework buffer */
	eError = PVRSRVRGXFrameworkCopyCommand(psRenderContext->psFWFrameworkMemDesc,
										   pabyFrameworkRegisters,
										   ui32FrameworkRegisterSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXCreateRenderContextKM: Failed to populate the framework buffer (%u)",
				eError));
		goto fail_frameworkcopy;
	}

	sInfo.psFWFrameworkMemDesc = psRenderContext->psFWFrameworkMemDesc;
	sInfo.psMCUFenceAddr = &sMCUFenceAddr;

	eError = _CreateTAContext(psConnection,
							  psDeviceNode,
							  psRenderContext->psFWRenderContextMemDesc,
							  offsetof(RGXFWIF_FWRENDERCONTEXT, sTAContext),
							  psFWMemContextMemDesc,
							  sVDMCallStackAddr,
							  ui32Priority,
							  &sInfo,
							  &psRenderContext->sTAData);
	if (eError != PVRSRV_OK)
	{
		goto fail_tacontext;
	}

	eError = _Create3DContext(psConnection,
							  psDeviceNode,
							  psRenderContext->psFWRenderContextMemDesc,
							  offsetof(RGXFWIF_FWRENDERCONTEXT, s3DContext),
							  psFWMemContextMemDesc,
							  ui32Priority,
							  &sInfo,
							  &psRenderContext->s3DData);
	if (eError != PVRSRV_OK)
	{
		goto fail_3dcontext;
	}

	SyncAddrListInit(&psRenderContext->sSyncAddrListTAFence);
	SyncAddrListInit(&psRenderContext->sSyncAddrListTAUpdate);
	SyncAddrListInit(&psRenderContext->sSyncAddrList3DFence);
	SyncAddrListInit(&psRenderContext->sSyncAddrList3DUpdate);

	{
		PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;

		OSWRLockAcquireWrite(psDevInfo->hRenderCtxListLock);
		dllist_add_to_tail(&(psDevInfo->sRenderCtxtListHead), &(psRenderContext->sListNode));
		OSWRLockReleaseWrite(psDevInfo->hRenderCtxListLock);
	}

	*ppsRenderContext= psRenderContext;
	return PVRSRV_OK;

fail_3dcontext:
	_DestroyTAContext(&psRenderContext->sTAData,
					  psDeviceNode,
					  psRenderContext->psCleanupSync);
fail_tacontext:
fail_frameworkcopy:
	DevmemFwFree(psDevInfo, psRenderContext->psFWFrameworkMemDesc);
fail_frameworkcreate:
	SyncPrimFree(psRenderContext->psCleanupSync);
fail_syncalloc:
	DevmemFwFree(psDevInfo, psRenderContext->psFWRenderContextMemDesc);
fail_fwrendercontext:
	OSFreeMem(psRenderContext);
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
 * PVRSRVRGXDestroyRenderContextKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXDestroyRenderContextKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO 	*psDevInfo = psRenderContext->psDeviceNode->pvDevice;
	RGXFWIF_FWRENDERCONTEXT	*psFWRenderContext;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	IMG_UINT32 ui32WorkEstCCBSubmitted;
#endif

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hRenderCtxListLock);
	dllist_remove_node(&(psRenderContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRenderCtxListLock);

	/* Cleanup the TA if we haven't already */
	if ((psRenderContext->ui32CleanupStatus & RC_CLEANUP_TA_COMPLETE) == 0)
	{
		eError = _DestroyTAContext(&psRenderContext->sTAData,
								   psRenderContext->psDeviceNode,
								   psRenderContext->psCleanupSync);
		if (eError == PVRSRV_OK)
		{
			psRenderContext->ui32CleanupStatus |= RC_CLEANUP_TA_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

	/* Cleanup the 3D if we haven't already */
	if ((psRenderContext->ui32CleanupStatus & RC_CLEANUP_3D_COMPLETE) == 0)
	{
		eError = _Destroy3DContext(&psRenderContext->s3DData,
								   psRenderContext->psDeviceNode,
								   psRenderContext->psCleanupSync);
		if (eError == PVRSRV_OK)
		{
			psRenderContext->ui32CleanupStatus |= RC_CLEANUP_3D_COMPLETE;
		}
		else
		{
			goto e0;
		}
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	eError = DevmemAcquireCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc,
	                                  (void **)&psFWRenderContext);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXDestroyRenderContextKM: Failed to map firmware render context (%u)",
		         eError));
		goto e0;
	}

	ui32WorkEstCCBSubmitted = psFWRenderContext->ui32WorkEstCCBSubmitted;

	DevmemReleaseCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc);

	/* Check if all of the workload estimation CCB commands for this workload
	 * are read
	 */
	if(ui32WorkEstCCBSubmitted != psRenderContext->sWorkEstData.ui32WorkEstCCBReceived)
	{
		eError = PVRSRV_ERROR_RETRY;
		goto e0;
	}
#endif

	/*
		Only if both TA and 3D contexts have been cleaned up can we
		free the shared resources
	*/
	if (psRenderContext->ui32CleanupStatus == (RC_CLEANUP_3D_COMPLETE | RC_CLEANUP_TA_COMPLETE))
	{

		/* Update SPM statistics */
		eError = DevmemAcquireCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc,
	                                      (void **)&psFWRenderContext);
		if (eError == PVRSRV_OK)
		{
			DevmemReleaseCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVRGXDestroyRenderContextKM: Failed to map firmware render context (%u)",
					eError));
		}

		/* Free the framework buffer */
		DevmemFwFree(psDevInfo, psRenderContext->psFWFrameworkMemDesc);
	
		/* Free the firmware render context */
		DevmemFwFree(psDevInfo, psRenderContext->psFWRenderContextMemDesc);

		/* Free the cleanup sync */
		SyncPrimFree(psRenderContext->psCleanupSync);

		SyncAddrListDeinit(&psRenderContext->sSyncAddrListTAFence);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrListTAUpdate);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrList3DFence);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrList3DUpdate);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		WorkEstRCDeInit(&(psRenderContext->sWorkEstData),
                        psDevInfo);
#endif

		OSFreeMem(psRenderContext);
	}

	return PVRSRV_OK;

e0:
	OSWRLockAcquireWrite(psDevInfo->hRenderCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sRenderCtxtListHead), &(psRenderContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRenderCtxListLock);
	return eError;
}


/* TODO !!! this was local on the stack, and we managed to blow the stack for the kernel. 
 * THIS - 46 argument function needs to be sorted out.
 */
/* 1 command for the TA */
static RGX_CCB_CMD_HELPER_DATA asTACmdHelperData[1];
/* Up to 3 commands for the 3D (partial render fence, partial reader, and render) */
static RGX_CCB_CMD_HELPER_DATA as3DCmdHelperData[3];

/*
 * PVRSRVRGXKickTA3DKM
 */
IMG_EXPORT
PVRSRV_ERROR PVRSRVRGXKickTA3DKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
								 IMG_UINT32					ui32ClientCacheOpSeqNum,
								 IMG_UINT32					ui32ClientTAFenceCount,
								 SYNC_PRIMITIVE_BLOCK				**apsClientTAFenceSyncPrimBlock,
								 IMG_UINT32					*paui32ClientTAFenceSyncOffset,
								 IMG_UINT32					*paui32ClientTAFenceValue,
								 IMG_UINT32					ui32ClientTAUpdateCount,
								 SYNC_PRIMITIVE_BLOCK				**apsClientTAUpdateSyncPrimBlock,
								 IMG_UINT32					*paui32ClientTAUpdateSyncOffset,
								 IMG_UINT32					*paui32ClientTAUpdateValue,
								 IMG_UINT32					ui32ServerTASyncPrims,
								 IMG_UINT32					*paui32ServerTASyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServerTASyncs,
								 IMG_UINT32					ui32Client3DFenceCount,
								 SYNC_PRIMITIVE_BLOCK				**apsClient3DFenceSyncPrimBlock,
								 IMG_UINT32					*paui32Client3DFenceSyncOffset,
								 IMG_UINT32					*paui32Client3DFenceValue,
								 IMG_UINT32					ui32Client3DUpdateCount,
								 SYNC_PRIMITIVE_BLOCK				**apsClient3DUpdateSyncPrimBlock,
								 IMG_UINT32					*paui32Client3DUpdateSyncOffset,
								 IMG_UINT32					*paui32Client3DUpdateValue,
								 IMG_UINT32					ui32Server3DSyncPrims,
								 IMG_UINT32					*paui32Server3DSyncFlags,
								 SERVER_SYNC_PRIMITIVE 		**pasServer3DSyncs,
								 SYNC_PRIMITIVE_BLOCK				*psPRFenceSyncPrimBlock,
								 IMG_UINT32					ui32PRFenceSyncOffset,
								 IMG_UINT32					ui32PRFenceValue,
								 IMG_INT32					i32CheckFenceFD,
								 IMG_INT32					i32UpdateTimelineFD,
								 IMG_INT32					*pi32UpdateFenceFD,
								 IMG_CHAR					szFenceName[32],
								 IMG_UINT32					ui32TACmdSize,
								 IMG_PBYTE					pui8TADMCmd,
								 IMG_UINT32					ui323DPRCmdSize,
								 IMG_PBYTE					pui83DPRDMCmd,
								 IMG_UINT32					ui323DCmdSize,
								 IMG_PBYTE					pui83DDMCmd,
								 IMG_UINT32					ui32ExtJobRef,
								 IMG_BOOL					bLastTAInScene,
								 IMG_BOOL					bKickTA,
								 IMG_BOOL					bKickPR,
								 IMG_BOOL					bKick3D,
								 IMG_BOOL					bAbort,
								 IMG_UINT32					ui32PDumpFlags,
								 RGX_RTDATA_CLEANUP_DATA	*psRTDataCleanup,
								 RGX_ZSBUFFER_DATA		*psZBuffer,
								 RGX_ZSBUFFER_DATA		*psSBuffer,
								 IMG_BOOL			bCommitRefCountsTA,
								 IMG_BOOL			bCommitRefCounts3D,
								 IMG_BOOL			*pbCommittedRefCountsTA,
								 IMG_BOOL			*pbCommittedRefCounts3D,
								 IMG_UINT32			ui32SyncPMRCount,
								 IMG_UINT32			*paui32SyncPMRFlags,
								 PMR				**ppsSyncPMRs,
								 IMG_UINT32			ui32RenderTargetSize,
								 IMG_UINT32			ui32NumberOfDrawCalls,
								 IMG_UINT32			ui32NumberOfIndices,
								 IMG_UINT32			ui32NumberOfMRTs,
								 IMG_UINT64			ui64DeadlineInus)
{

	IMG_UINT32				ui32TACmdCount=0;
	IMG_UINT32				ui323DCmdCount=0;
	IMG_UINT32				ui32TACmdOffset=0;
	IMG_UINT32				ui323DCmdOffset=0;
	RGXFWIF_UFO				sPRUFO;
	IMG_UINT32				i;
	PVRSRV_ERROR			eError = PVRSRV_OK;
	PVRSRV_ERROR			eError2;
	IMG_INT32				i32UpdateFenceFD = -1;
	IMG_UINT32				ui32JobId;

	IMG_UINT32				ui32ClientPRUpdateCount = 0;
	PRGXFWIF_UFO_ADDR		*pauiClientPRUpdateUFOAddress = NULL;
	IMG_UINT32				*paui32ClientPRUpdateValue = NULL;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;

	PRGXFWIF_UFO_ADDR			*pauiClientTAFenceUFOAddress;
	PRGXFWIF_UFO_ADDR			*pauiClientTAUpdateUFOAddress;
	PRGXFWIF_UFO_ADDR			*pauiClient3DFenceUFOAddress;
	PRGXFWIF_UFO_ADDR			*pauiClient3DUpdateUFOAddress;
	PRGXFWIF_UFO_ADDR			uiPRFenceUFOAddress;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	RGXFWIF_WORKEST_KICK_DATA sWorkloadKickDataTA;
	RGXFWIF_WORKEST_KICK_DATA sWorkloadKickData3D;
	IMG_UINT32 ui32TACommandOffset = 0;
	IMG_UINT32 ui323DCommandOffset = 0;
	IMG_UINT32 ui32TACmdHeaderOffset = 0;
	IMG_UINT32 ui323DCmdHeaderOffset = 0;
	IMG_UINT32 ui323DFullRenderCommandOffset = 0;
	IMG_UINT32 ui32TACmdOffsetWrapCheck = 0;
	IMG_UINT32 ui323DCmdOffsetWrapCheck = 0;
#endif

#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_append_data *psAppendData = NULL;
#endif

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* Android fd sync update info */
	struct pvr_sync_append_data *psFDData = NULL;
	if (i32UpdateTimelineFD >= 0 && !pi32UpdateFenceFD)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#else
	if (i32UpdateTimelineFD >= 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Providing native sync timeline (%d) in non native sync enabled driver",
			__func__, i32UpdateTimelineFD));
	}
	if (i32CheckFenceFD >= 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Providing native check sync (%d) in non native sync enabled driver",
			__func__, i32CheckFenceFD));
	}
#endif

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	sWorkloadKickDataTA.ui64ReturnDataIndex = 0;
	sWorkloadKickData3D.ui64ReturnDataIndex = 0;
#endif

	ui32JobId = OSAtomicIncrement(&psRenderContext->hJobId);

	/* Ensure the string is null-terminated (Required for safety) */
	szFenceName[31] = '\0';
	*pbCommittedRefCountsTA = IMG_FALSE;
	*pbCommittedRefCounts3D = IMG_FALSE;

	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrListTAFence,
										ui32ClientTAFenceCount,
										apsClientTAFenceSyncPrimBlock,
										paui32ClientTAFenceSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClientTAFenceUFOAddress = psRenderContext->sSyncAddrListTAFence.pasFWAddrs;

	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrListTAUpdate,
										ui32ClientTAUpdateCount,
										apsClientTAUpdateSyncPrimBlock,
										paui32ClientTAUpdateSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClientTAUpdateUFOAddress = psRenderContext->sSyncAddrListTAUpdate.pasFWAddrs;

	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrList3DFence,
										ui32Client3DFenceCount,
										apsClient3DFenceSyncPrimBlock,
										paui32Client3DFenceSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClient3DFenceUFOAddress = psRenderContext->sSyncAddrList3DFence.pasFWAddrs;

	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrList3DUpdate,
										ui32Client3DUpdateCount,
										apsClient3DUpdateSyncPrimBlock,
										paui32Client3DUpdateSyncOffset);
	if(eError != PVRSRV_OK)
	{
		goto err_populate_sync_addr_list;
	}

	pauiClient3DUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;

	eError = SyncPrimitiveBlockToFWAddr(psPRFenceSyncPrimBlock,
									ui32PRFenceSyncOffset,
									&uiPRFenceUFOAddress);

	if(eError != PVRSRV_OK)
	{
		goto err_pr_fence_address;
	}



	/* Sanity check the server fences */
	for (i=0;i<ui32ServerTASyncPrims;i++)
	{
		if (!(paui32ServerTASyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on TA) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	for (i=0;i<ui32Server3DSyncPrims;i++)
	{
		if (!(paui32Server3DSyncFlags[i] & PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Server fence (on 3D) must fence", __FUNCTION__));
			return PVRSRV_ERROR_INVALID_SYNC_PRIM_OP;
		}
	}

	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psRenderContext->psDeviceNode->pvDevice,
	                          & pPreAddr,
	                          & pPostAddr,
	                          & pRMWUFOAddr);

	/*
		Sanity check we have a PR kick if there are client or server fences
	*/
	if (!bKickPR && ((ui32Client3DFenceCount != 0) || (ui32Server3DSyncPrims != 0)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: 3D fence (client or server) passed without a PR kick", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32SyncPMRCount)
	{
#if defined(SUPPORT_BUFFER_SYNC)
		PVRSRV_DEVICE_NODE *psDeviceNode = psRenderContext->psDeviceNode;
		IMG_UINT32 ui32ClientIntUpdateCount = 0;
		PRGXFWIF_UFO_ADDR *pauiClientIntUpdateUFOAddress = NULL;
		IMG_UINT32 *paui32ClientIntUpdateValue = NULL;
		int err;

		if (!bKickTA)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Buffer sync only supported for kicks including a TA",
					 __FUNCTION__));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		if (!bKickPR)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Buffer sync only supported for kicks including a PR",
					 __FUNCTION__));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		if (bKick3D)
		{
			ui32ClientIntUpdateCount = ui32Client3DUpdateCount;
			pauiClientIntUpdateUFOAddress = pauiClient3DUpdateUFOAddress;
			paui32ClientIntUpdateValue = paui32Client3DUpdateValue;
		}
		else
		{
			ui32ClientIntUpdateCount = ui32ClientPRUpdateCount;
			pauiClientIntUpdateUFOAddress = pauiClientPRUpdateUFOAddress;
			paui32ClientIntUpdateValue = paui32ClientPRUpdateValue;
		}

		err = pvr_buffer_sync_append_start(psDeviceNode->psBufferSyncContext,
										   ui32SyncPMRCount,
										   ppsSyncPMRs,
										   paui32SyncPMRFlags,
										   ui32ClientTAFenceCount,
										   pauiClientTAFenceUFOAddress,
										   paui32ClientTAFenceValue,
										   ui32ClientIntUpdateCount,
										   pauiClientIntUpdateUFOAddress,
										   paui32ClientIntUpdateValue,
										   &psAppendData);
		if (err)
		{
			eError = (err == -ENOMEM) ? PVRSRV_ERROR_OUT_OF_MEMORY : PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_sync_append;
		}

		pvr_buffer_sync_append_checks_get(psAppendData,
										  &ui32ClientTAFenceCount,
										  &pauiClientTAFenceUFOAddress,
										  &paui32ClientTAFenceValue);

		if (bKick3D)
		{
			pvr_buffer_sync_append_updates_get(psAppendData,
											   &ui32Client3DUpdateCount,
											   &pauiClient3DUpdateUFOAddress,
											   &paui32Client3DUpdateValue);
		}
		else
		{
			pvr_buffer_sync_append_updates_get(psAppendData,
											   &ui32ClientPRUpdateCount,
											   &pauiClientPRUpdateUFOAddress,
											   &paui32ClientPRUpdateValue);
		}
#else
		PVR_DPF((PVR_DBG_ERROR, "%s: Buffer sync not supported but got %u buffers", __FUNCTION__, ui32SyncPMRCount));
		return PVRSRV_ERROR_INVALID_PARAMS;
#endif /* defined(SUPPORT_BUFFER_SYNC) */
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	/* 
	 * The hardware requires a PR to be submitted if there is a TA (otherwise
	 * it can wedge if we run out of PB space with no PR to run)
	 *
	 * If we only have a TA, attach native checks to the TA and updates to the PR
	 * If we have a TA and 3D, attach checks to TA, updates to 3D
	 * If we only have a 3D, attach checks and updates to the 3D
	 *
	 * Note that 'updates' includes the cleanup syncs for 'check' fence FDs, in
	 * addition to the update fence FD (if supplied)
	 *
	 * Currently, the client driver never kicks only the 3D, so we only support
	 * that for the time being.
	 */
	if (i32CheckFenceFD >= 0 || i32UpdateTimelineFD >= 0)
	{
		IMG_UINT32			ui32ClientIntUpdateCount = 0;
		PRGXFWIF_UFO_ADDR	*pauiClientIntUpdateUFOAddress = NULL;
		IMG_UINT32			*paui32ClientIntUpdateValue = NULL;

		if (!bKickTA)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Native syncs only supported for kicks including a TA",
				__FUNCTION__));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_fdsync;
		}
		if (!bKickPR)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Native syncs require a PR for all kicks",
				__FUNCTION__));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_fdsync;
		}
		/* If we have a 3D, attach updates to that. Otherwise, we attach it to a PR */
		if (bKick3D)
		{
			ui32ClientIntUpdateCount = ui32Client3DUpdateCount;
			pauiClientIntUpdateUFOAddress = pauiClient3DUpdateUFOAddress;
			paui32ClientIntUpdateValue = paui32Client3DUpdateValue;
		}
		else
		{
			ui32ClientIntUpdateCount = ui32ClientPRUpdateCount;
			pauiClientIntUpdateUFOAddress = pauiClientPRUpdateUFOAddress;
			paui32ClientIntUpdateValue = paui32ClientPRUpdateValue;
		}

		eError =
			pvr_sync_append_fences(szFenceName,
			                       i32CheckFenceFD,
			                       i32UpdateTimelineFD,
			                       ui32ClientIntUpdateCount,
			                       pauiClientIntUpdateUFOAddress,
			                       paui32ClientIntUpdateValue,
			                       ui32ClientTAFenceCount,
			                       pauiClientTAFenceUFOAddress,
			                       paui32ClientTAFenceValue,
			                       &psFDData);
		if (eError != PVRSRV_OK)
		{
			goto fail_fdsync;
		}
		/* If we have a 3D, attach updates to that. Otherwise, we attach it to a PR */
		if (bKick3D)
		{
			pvr_sync_get_updates(psFDData, &ui32Client3DUpdateCount,
				&pauiClient3DUpdateUFOAddress, &paui32Client3DUpdateValue);
		}
		else
		{
			pvr_sync_get_updates(psFDData, &ui32ClientPRUpdateCount,
				&pauiClientPRUpdateUFOAddress, &paui32ClientPRUpdateValue);
		}
		pvr_sync_get_checks(psFDData, &ui32ClientTAFenceCount,
			&pauiClientTAFenceUFOAddress, &paui32ClientTAFenceValue);
		if (ui32ClientPRUpdateCount)
		{
			PVR_ASSERT(pauiClientPRUpdateUFOAddress);
			PVR_ASSERT(paui32ClientPRUpdateValue);
		}
		if (ui32Client3DUpdateCount)
		{
			PVR_ASSERT(pauiClient3DUpdateUFOAddress);
			PVR_ASSERT(paui32Client3DUpdateValue);
		}
	}
#endif /* SUPPORT_NATIVE_FENCE_SYNC */

	/* Init and acquire to TA command if required */
	if(bKickTA)
	{
		RGX_SERVER_RC_TA_DATA *psTAData = &psRenderContext->sTAData;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* Prepare workload estimation */
		WorkEstPrepare(psRenderContext->psDeviceNode->pvDevice,
		               &(psRenderContext->sWorkEstData),
		               &(psRenderContext->sWorkEstData.sWorkloadMatchingDataTA),
		               ui32RenderTargetSize,
		               ui32NumberOfDrawCalls,
		               ui32NumberOfIndices,
		               ui32NumberOfMRTs,
		               ui64DeadlineInus,
		               &sWorkloadKickDataTA);
#endif

		/* Init the TA command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(psTAData->psServerCommonContext),
		                                ui32ClientTAFenceCount,
		                                pauiClientTAFenceUFOAddress,
		                                paui32ClientTAFenceValue,
		                                ui32ClientTAUpdateCount,
		                                pauiClientTAUpdateUFOAddress,
		                                paui32ClientTAUpdateValue,
		                                ui32ServerTASyncPrims,
		                                paui32ServerTASyncFlags,
		                                SYNC_FLAG_MASK_ALL,
		                                pasServerTASyncs,
		                                ui32TACmdSize,
		                                pui8TADMCmd,
		                                & pPreAddr,
		                                (bKick3D ? NULL : & pPostAddr),
		                                (bKick3D ? NULL : & pRMWUFOAddr),
		                                RGXFWIF_CCB_CMD_TYPE_TA,
		                                ui32ExtJobRef,
		                                ui32JobId,
		                                ui32PDumpFlags,
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		                                &sWorkloadKickDataTA,
#else
										NULL,
#endif
		                                "TA",
		                                asTACmdHelperData);
		if (eError != PVRSRV_OK)
		{
			goto fail_tacmdinit;
		}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* The following is used to determine the offset of the command header
		 * containing the workload estimation data so that can be accessed when
		 * the KCCB is read.
		 */
		ui32TACmdHeaderOffset = RGXCmdHelperGetDMCommandHeaderOffset(asTACmdHelperData);
#endif

		eError = RGXCmdHelperAcquireCmdCCB(IMG_ARR_NUM_ELEMS(asTACmdHelperData),
		                                   asTACmdHelperData);
		if (eError != PVRSRV_OK)
		{
			goto fail_taacquirecmd;
		}
		else
		{
			ui32TACmdCount++;
		}
	}

	/* Only kick the 3D if required */
	if (bKickPR)
	{
		RGX_SERVER_RC_3D_DATA *ps3DData = &psRenderContext->s3DData;

		/*
			The command helper doesn't know about the PR fence so create
			the command with all the fences against it and later create
			the PR command itself which _must_ come after the PR fence.
		*/
		sPRUFO.puiAddrUFO = uiPRFenceUFOAddress;
		sPRUFO.ui32Value = ui32PRFenceValue;

		/* Init the PR fence command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										ui32Client3DFenceCount,
										pauiClient3DFenceUFOAddress,
										paui32Client3DFenceValue,
										0,
										NULL,
										NULL,
										(bKick3D ? ui32Server3DSyncPrims : 0),
										paui32Server3DSyncFlags,
										PVRSRV_CLIENT_SYNC_PRIM_OP_CHECK,
										pasServer3DSyncs,
										sizeof(sPRUFO),
										(IMG_UINT8*) &sPRUFO,
										NULL,
										NULL,
										NULL,
										RGXFWIF_CCB_CMD_TYPE_FENCE_PR,
										ui32ExtJobRef,
										ui32JobId,
										ui32PDumpFlags,
										NULL,
										"3D-PR-Fence",
										&as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_prfencecmdinit;
		}

		/* Init the 3D PR command helper */
		/*
			See note above PVRFDSyncQueryFencesKM as to why updates for android
			syncs are passed in with the PR
		*/
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										0,
										NULL,
										NULL,
										ui32ClientPRUpdateCount,
										pauiClientPRUpdateUFOAddress,
										paui32ClientPRUpdateValue,
										0,
										NULL,
										SYNC_FLAG_MASK_ALL,
										NULL,
										ui323DPRCmdSize,
										pui83DPRDMCmd,
										NULL,
										NULL,
										NULL,
										RGXFWIF_CCB_CMD_TYPE_3D_PR,
										ui32ExtJobRef,
										ui32JobId,
										ui32PDumpFlags,
										NULL,
										"3D-PR",
										&as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_prcmdinit;
		}
	}

	if (bKick3D || bAbort)
	{
		RGX_SERVER_RC_3D_DATA *ps3DData = &psRenderContext->s3DData;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* Prepare workload estimation */
		WorkEstPrepare(psRenderContext->psDeviceNode->pvDevice,
		               &(psRenderContext->sWorkEstData),
		               &(psRenderContext->sWorkEstData.sWorkloadMatchingData3D),
		               ui32RenderTargetSize,
		               ui32NumberOfDrawCalls,
		               ui32NumberOfIndices,
		               ui32NumberOfMRTs,
		               ui64DeadlineInus,
		               &sWorkloadKickData3D);
#endif
		/* Init the 3D command helper */
		eError = RGXCmdHelperInitCmdCCB(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
										0,
										NULL,
										NULL,
										ui32Client3DUpdateCount,
										pauiClient3DUpdateUFOAddress,
										paui32Client3DUpdateValue,
										ui32Server3DSyncPrims,
										paui32Server3DSyncFlags,
										PVRSRV_CLIENT_SYNC_PRIM_OP_UPDATE,
										pasServer3DSyncs,
										ui323DCmdSize,
										pui83DDMCmd,
										(bKickTA ? NULL : & pPreAddr),
										& pPostAddr,
										& pRMWUFOAddr,
										RGXFWIF_CCB_CMD_TYPE_3D,
										ui32ExtJobRef,
										ui32JobId,
										ui32PDumpFlags,
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
										&sWorkloadKickData3D,
#else
										NULL,
#endif
										"3D",
										&as3DCmdHelperData[ui323DCmdCount++]);
		if (eError != PVRSRV_OK)
		{
			goto fail_3dcmdinit;
		}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* The following are used to determine the offset of the command header
		 * containing the workload estimation data so that can be accessed when
		 * the KCCB is read.
		 */
		ui323DCmdHeaderOffset =
			RGXCmdHelperGetDMCommandHeaderOffset(&as3DCmdHelperData[ui323DCmdCount - 1]);
		ui323DFullRenderCommandOffset =
			RGXCmdHelperGetCommandOffset(as3DCmdHelperData,
			                             ui323DCmdCount - 1);
#endif
	}

	/* Protect against array overflow in RGXCmdHelperAcquireCmdCCB() */
	if (ui323DCmdCount > IMG_ARR_NUM_ELEMS(as3DCmdHelperData))
	{
		goto fail_3dcmdinit;
	}

	if (ui323DCmdCount)
	{
		PVR_ASSERT(bKickPR || bKick3D);

		/* Acquire space for all the 3D command(s) */
		eError = RGXCmdHelperAcquireCmdCCB(ui323DCmdCount,
										   as3DCmdHelperData);
		if (eError != PVRSRV_OK)
		{
			/* If RGXCmdHelperAcquireCmdCCB fails we skip the scheduling
			 * of a new TA command with the same Write offset in Kernel CCB.
			 */
			goto fail_3dacquirecmd;
		}
	}

	/*
		We should acquire the space in the kernel CCB here as after this point
		we release the commands which will take operations on server syncs
		which can't be undone
	*/

	/*
		Everything is ready to go now, release the commands
	*/
	if (ui32TACmdCount)
	{
		ui32TACmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui32TACmdCount,
								  asTACmdHelperData,
								  "TA",
								  FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext).ui32Addr);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		ui32TACmdOffsetWrapCheck =
			RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext));

		/* This checks if the command would wrap around at the end of the CCB
		 * and therefore would start at an offset of 0 rather than the current
		 * command offset.
		 */
		if(ui32TACmdOffset < ui32TACmdOffsetWrapCheck)
		{
			ui32TACommandOffset = ui32TACmdOffset;
		}
		else
		{
			ui32TACommandOffset = 0;
		}
#endif
	}

	if (ui323DCmdCount)
	{
		ui323DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
								  as3DCmdHelperData,
								  "3D",
								  FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext).ui32Addr);
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		ui323DCmdOffsetWrapCheck = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));

		if(ui323DCmdOffset < ui323DCmdOffsetWrapCheck)
		{
			ui323DCommandOffset = ui323DCmdOffset;
		}
		else
		{
			ui323DCommandOffset = 0;
		}
#endif
	}

	if (ui32TACmdCount)
	{
		RGXFWIF_KCCB_CMD sTAKCCBCmd;
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext).ui32Addr;

		/* Construct the kernel TA CCB command. */
		sTAKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		sTAKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext);
		sTAKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext));

		/* Add the Workload data into the KCCB kick */
		sTAKCCBCmd.uCmdData.sCmdKickData.sWorkloadDataFWAddress.ui32Addr = 0;
		sTAKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* Store the offset to the CCCB command header so that it can be
		 * referenced when the KCCB command reaches the FW
		 */
		sTAKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset =
			ui32TACommandOffset + ui32TACmdHeaderOffset;
#endif

		if(bCommitRefCountsTA)
		{
			AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &sTAKCCBCmd.uCmdData.sCmdKickData.apsCleanupCtl,
										&sTAKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl,
										RGXFWIF_DM_TA,
										bKickTA,
										psRTDataCleanup,
										psZBuffer,
										psSBuffer);
			*pbCommittedRefCountsTA = IMG_TRUE;
		}
		else
		{
			sTAKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		}

		HTBLOGK(HTB_SF_MAIN_KICK_TA,
				sTAKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui32TACmdOffset
				);
		RGX_HWPERF_HOST_ENQ(psRenderContext, OSGetCurrentClientProcessIDKM(),
		                    ui32FWCtx, ui32ExtJobRef, ui32JobId,
		                    RGX_HWPERF_KICK_TYPE_TA3D);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
										RGXFWIF_DM_TA,
										&sTAKCCBCmd,
										sizeof(sTAKCCBCmd),
										ui32ClientCacheOpSeqNum,
										ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

#if defined(SUPPORT_GPUTRACE_EVENTS)
		RGXHWPerfFTraceGPUEnqueueEvent(psRenderContext->psDeviceNode->pvDevice,
					ui32FWCtx, ui32JobId, RGX_HWPERF_KICK_TYPE_TA3D);
#endif
	}
	
	if (ui323DCmdCount)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd;

		/* Construct the kernel 3D CCB command. */
		s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
		s3DKCCBCmd.uCmdData.sCmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext);
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));

		/* Add the Workload data into the KCCB kick */
		s3DKCCBCmd.uCmdData.sCmdKickData.sWorkloadDataFWAddress.ui32Addr = 0;
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = 0;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		/* Store the offset to the CCCB command header so that it can be
		 * referenced when the KCCB command reaches the FW
		 */
		s3DKCCBCmd.uCmdData.sCmdKickData.ui32WorkEstCmdHeaderOffset = ui323DCommandOffset + ui323DCmdHeaderOffset + ui323DFullRenderCommandOffset;
#endif

		if(bCommitRefCounts3D)
		{
			AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &s3DKCCBCmd.uCmdData.sCmdKickData.apsCleanupCtl,
											&s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl,
											RGXFWIF_DM_3D,
											bKick3D,
											psRTDataCleanup,
											psZBuffer,
											psSBuffer);
			*pbCommittedRefCounts3D = IMG_TRUE;
		}
		else
		{
			s3DKCCBCmd.uCmdData.sCmdKickData.ui32NumCleanupCtl = 0;
		}


		HTBLOGK(HTB_SF_MAIN_KICK_3D,
				s3DKCCBCmd.uCmdData.sCmdKickData.psContext,
				ui323DCmdOffset);

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
										RGXFWIF_DM_3D,
										&s3DKCCBCmd,
										sizeof(s3DKCCBCmd),
										ui32ClientCacheOpSeqNum,
										ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
	}

	/*
	 * Now check eError (which may have returned an error from our earlier calls
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (eError != PVRSRV_OK )
	{
		goto fail_3dacquirecmd;
	}

#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	if (i32UpdateTimelineFD >= 0)
	{
		/* If we get here, this should never fail. Hitting that likely implies
		 * a code error above */
		i32UpdateFenceFD = pvr_sync_get_update_fd(psFDData);
		if (i32UpdateFenceFD < 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get install update sync fd",
				__FUNCTION__));
			/* If we fail here, we cannot rollback the syncs as the hw already
			 * has references to resources they may be protecting in the kick
			 * so fallthrough */
	
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto fail_3dacquirecmd;
		}
	}
#if defined(NO_HARDWARE)
	pvr_sync_nohw_complete_fences(psFDData);
#endif
	pvr_sync_free_append_fences_data(psFDData);

#endif

#if defined(SUPPORT_BUFFER_SYNC)
	if (psAppendData)
	{
		pvr_buffer_sync_append_finish(psAppendData);
	}
#endif

	*pi32UpdateFenceFD = i32UpdateFenceFD;

	return PVRSRV_OK;

fail_3dacquirecmd:
fail_3dcmdinit:
fail_prcmdinit:
fail_prfencecmdinit:
fail_taacquirecmd:
fail_tacmdinit:
#if defined(SUPPORT_NATIVE_FENCE_SYNC)
	pvr_sync_rollback_append_fences(psFDData);
	pvr_sync_free_append_fences_data(psFDData);
fail_fdsync:
#endif
#if defined(SUPPORT_BUFFER_SYNC)
	pvr_buffer_sync_append_abort(psAppendData);
fail_sync_append:
#endif
err_pr_fence_address:
err_populate_sync_addr_list:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetRenderContextPriorityKM(CONNECTION_DATA *psConnection,
                                                 PVRSRV_DEVICE_NODE * psDeviceNode,
												 RGX_SERVER_RENDER_CONTEXT *psRenderContext,
												 IMG_UINT32 ui32Priority)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	
	if (psRenderContext->sTAData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRenderContext->sTAData.psServerCommonContext,
									psConnection,
									psRenderContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_TA);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the TA part of the rendercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			goto fail_tacontext;
		}
		psRenderContext->sTAData.ui32Priority = ui32Priority;
	}

	if (psRenderContext->s3DData.ui32Priority != ui32Priority)
	{
		eError = ContextSetPriority(psRenderContext->s3DData.psServerCommonContext,
									psConnection,
									psRenderContext->psDeviceNode->pvDevice,
									ui32Priority,
									RGXFWIF_DM_3D);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to set the priority of the 3D part of the rendercontext (%s)", __FUNCTION__, PVRSRVGetErrorStringKM(eError)));
			goto fail_3dcontext;
		}
		psRenderContext->s3DData.ui32Priority = ui32Priority;
	}
	return PVRSRV_OK;

fail_3dcontext:
fail_tacontext:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
 * PVRSRVRGXGetLastRenderContextResetReasonKM
 */
PVRSRV_ERROR PVRSRVRGXGetLastRenderContextResetReasonKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext,
                                                        IMG_UINT32 *peLastResetReason,
                                                        IMG_UINT32 *pui32LastResetJobRef)
{
	RGX_SERVER_RC_TA_DATA         *psRenderCtxTAData;
	RGX_SERVER_RC_3D_DATA         *psRenderCtx3DData;
	RGX_SERVER_COMMON_CONTEXT     *psCurrentServerTACommonCtx, *psCurrentServer3DCommonCtx;
	RGXFWIF_CONTEXT_RESET_REASON  eLastTAResetReason, eLast3DResetReason;
	IMG_UINT32                    ui32LastTAResetJobRef, ui32Last3DResetJobRef;
	
	PVR_ASSERT(psRenderContext != NULL);
	PVR_ASSERT(peLastResetReason != NULL);
	PVR_ASSERT(pui32LastResetJobRef != NULL);

	psRenderCtxTAData          = &(psRenderContext->sTAData);
	psCurrentServerTACommonCtx = psRenderCtxTAData->psServerCommonContext;
	psRenderCtx3DData          = &(psRenderContext->s3DData);
	psCurrentServer3DCommonCtx = psRenderCtx3DData->psServerCommonContext;
	
	/* Get the last reset reasons from both the TA and 3D so they are reset... */
	eLastTAResetReason = FWCommonContextGetLastResetReason(psCurrentServerTACommonCtx, &ui32LastTAResetJobRef);
	eLast3DResetReason = FWCommonContextGetLastResetReason(psCurrentServer3DCommonCtx, &ui32Last3DResetJobRef);

	/* Combine the reset reason from TA and 3D into one... */
	*peLastResetReason    = (IMG_UINT32) eLast3DResetReason;
	*pui32LastResetJobRef = ui32Last3DResetJobRef;
	if (eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_NONE  ||
	    ((eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_LOCKUP  ||
	      eLast3DResetReason == RGXFWIF_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING)  &&
	     (eLastTAResetReason == RGXFWIF_CONTEXT_RESET_REASON_GUILTY_LOCKUP  ||
	      eLastTAResetReason == RGXFWIF_CONTEXT_RESET_REASON_GUILTY_OVERRUNING)))
	{
		*peLastResetReason    = eLastTAResetReason;
		*pui32LastResetJobRef = ui32LastTAResetJobRef;
	}

	return PVRSRV_OK;
}


/*
 * PVRSRVRGXGetPartialRenderCountKM
 */
PVRSRV_ERROR PVRSRVRGXGetPartialRenderCountKM(DEVMEM_MEMDESC *psHWRTDataMemDesc,
											  IMG_UINT32 *pui32NumPartialRenders)
{
	RGXFWIF_HWRTDATA *psHWRTData;
	PVRSRV_ERROR eError;

	eError = DevmemAcquireCpuVirtAddr(psHWRTDataMemDesc, (void **)&psHWRTData);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXGetPartialRenderCountKM: Failed to map Firmware Render Target Data (%u)", eError));
		return eError;
	}

	*pui32NumPartialRenders = psHWRTData->ui32NumPartialRenders;

	DevmemReleaseCpuVirtAddr(psHWRTDataMemDesc);

	return PVRSRV_OK;
}

void CheckForStalledRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo,
					DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
					void *pvDumpDebugFile)
{
	DLLIST_NODE *psNode, *psNext;
	OSWRLockAcquireRead(psDevInfo->hRenderCtxListLock);
	dllist_foreach_node(&psDevInfo->sRenderCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_RENDER_CONTEXT *psCurrentServerRenderCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_RENDER_CONTEXT, sListNode);

		DumpStalledFWCommonContext(psCurrentServerRenderCtx->sTAData.psServerCommonContext,
								   pfnDumpDebugPrintf, pvDumpDebugFile);
		DumpStalledFWCommonContext(psCurrentServerRenderCtx->s3DData.psServerCommonContext,
								   pfnDumpDebugPrintf, pvDumpDebugFile);
	}
	OSWRLockReleaseRead(psDevInfo->hRenderCtxListLock);
}

IMG_UINT32 CheckForStalledClientRenderCtxt(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	DLLIST_NODE *psNode, *psNext;
	IMG_UINT32 ui32ContextBitMask = 0;

	OSWRLockAcquireRead(psDevInfo->hRenderCtxListLock);

	dllist_foreach_node(&psDevInfo->sRenderCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_RENDER_CONTEXT *psCurrentServerRenderCtx =
			IMG_CONTAINER_OF(psNode, RGX_SERVER_RENDER_CONTEXT, sListNode);
		if(NULL != psCurrentServerRenderCtx->sTAData.psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerRenderCtx->sTAData.psServerCommonContext, RGX_KICK_TYPE_DM_TA) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_TA;
			}
		}

		if(NULL != psCurrentServerRenderCtx->s3DData.psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerRenderCtx->s3DData.psServerCommonContext, RGX_KICK_TYPE_DM_3D) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_3D;
			}
		}
	}

	OSWRLockReleaseRead(psDevInfo->hRenderCtxListLock);
	return ui32ContextBitMask;
}

/******************************************************************************
 End of file (rgxta3d.c)
******************************************************************************/
