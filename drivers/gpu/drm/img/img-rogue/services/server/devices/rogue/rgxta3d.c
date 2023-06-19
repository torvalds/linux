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
#if defined(__linux__)
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

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
#include "ospvr_gputrace.h"
#include "rgxsyncutils.h"
#include "htbserver.h"

#include "rgxdefs_km.h"
#include "rgx_fwif_km.h"
#include "physmem.h"
#include "sync_server.h"
#include "sync_internal.h"
#include "sync.h"
#include "process_stats.h"

#include "rgxtimerquery.h"

#if defined(SUPPORT_BUFFER_SYNC)
#include "pvr_buffer_sync.h"
#endif

#include "sync_checkpoint.h"
#include "sync_checkpoint_internal.h"

#if defined(SUPPORT_PDVFS)
#include "rgxpdvfs.h"
#endif

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
#include "rgxworkest.h"

#define HASH_CLEAN_LIMIT 6
#endif

/* Enable this to dump the compiled list of UFOs prior to kick call */
#define ENABLE_TA3D_UFO_DUMP	0

//#define TA3D_CHECKPOINT_DEBUG

#if defined(TA3D_CHECKPOINT_DEBUG)
#define CHKPT_DBG(X) PVR_DPF(X)
static INLINE
void _DebugSyncValues(const IMG_CHAR *pszFunction,
		const IMG_UINT32 *pui32UpdateValues,
		const IMG_UINT32 ui32Count)
{
	IMG_UINT32 i;
	IMG_UINT32 *pui32Tmp = (IMG_UINT32*)pui32UpdateValues;

	for (i = 0; i < ui32Count; i++)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: pui32IntAllocatedUpdateValues[%d](<%p>) = 0x%x", pszFunction, i, (void*)pui32Tmp, *pui32Tmp));
		pui32Tmp++;
	}
}

static INLINE
void _DebugSyncCheckpoints(const IMG_CHAR *pszFunction,
		const IMG_CHAR *pszDMName,
		const PSYNC_CHECKPOINT *apsSyncCheckpoints,
		const IMG_UINT32 ui32Count)
{
	IMG_UINT32 i;

	for (i = 0; i < ui32Count; i++)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: apsFence%sSyncCheckpoints[%d]=<%p>", pszFunction, pszDMName, i, *(apsSyncCheckpoints + i)));
	}
}

#else
#define CHKPT_DBG(X)
#endif

/* define the number of commands required to be set up by the CCB helper */
/* 1 command for the TA */
#define CCB_CMD_HELPER_NUM_TA_COMMANDS 1
/* Up to 3 commands for the 3D (partial render fence, partial render, and render) */
#define CCB_CMD_HELPER_NUM_3D_COMMANDS 3

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
#define WORKEST_CYCLES_PREDICTION_GET(x) ((x).ui32CyclesPrediction)
#else
#define WORKEST_CYCLES_PREDICTION_GET(x) (NO_CYCEST)
#endif

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_INT32					i32Priority;
} RGX_SERVER_RC_TA_DATA;

typedef struct {
	DEVMEM_MEMDESC				*psContextStateMemDesc;
	RGX_SERVER_COMMON_CONTEXT	*psServerCommonContext;
	IMG_INT32					i32Priority;
} RGX_SERVER_RC_3D_DATA;

struct _RGX_SERVER_RENDER_CONTEXT_ {
	/* this lock protects usage of the render context.
	 * it ensures only one kick is being prepared and/or submitted on
	 * this render context at any time
	 */
	POS_LOCK				hLock;
	RGX_CCB_CMD_HELPER_DATA asTACmdHelperData[CCB_CMD_HELPER_NUM_TA_COMMANDS];
	RGX_CCB_CMD_HELPER_DATA as3DCmdHelperData[CCB_CMD_HELPER_NUM_3D_COMMANDS];
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	DEVMEM_MEMDESC				*psFWRenderContextMemDesc;
	DEVMEM_MEMDESC				*psFWFrameworkMemDesc;
	RGX_SERVER_RC_TA_DATA		sTAData;
	RGX_SERVER_RC_3D_DATA		s3DData;
	IMG_UINT32					ui32CleanupStatus;
#define RC_CLEANUP_TA_COMPLETE		(1 << 0)
#define RC_CLEANUP_3D_COMPLETE		(1 << 1)
	DLLIST_NODE					sListNode;
	SYNC_ADDR_LIST				sSyncAddrListTAFence;
	SYNC_ADDR_LIST				sSyncAddrListTAUpdate;
	SYNC_ADDR_LIST				sSyncAddrList3DFence;
	SYNC_ADDR_LIST				sSyncAddrList3DUpdate;
	ATOMIC_T					hIntJobRef;
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	WORKEST_HOST_DATA			sWorkEstData;
#endif
#if defined(SUPPORT_BUFFER_SYNC)
	struct pvr_buffer_sync_context *psBufferSyncContext;
#endif
};


/*
	Static functions used by render context code
*/

static
PVRSRV_ERROR _DestroyTAContext(RGX_SERVER_RC_TA_DATA *psTAData,
		PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
			psTAData->psServerCommonContext,
			RGXFWIF_DM_GEOM,
			PDUMP_FLAGS_CONTINUOUS);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */
#if defined(DEBUG)
	/* Log the number of TA context stores which occurred */
	{
		RGXFWIF_TACTX_STATE	*psFWTAState;

		eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
				(void**)&psFWTAState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to map firmware render context state (%s)",
					__func__, PVRSRVGetErrorString(eError)));
		}
		else
		{
			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);
		}
	}
#endif
	FWCommonContextFree(psTAData->psServerCommonContext);
	DevmemFwUnmapAndFree(psDeviceNode->pvDevice, psTAData->psContextStateMemDesc);
	psTAData->psServerCommonContext = NULL;
	return PVRSRV_OK;
}

static
PVRSRV_ERROR _Destroy3DContext(RGX_SERVER_RC_3D_DATA *ps3DData,
		PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;

	/* Check if the FW has finished with this resource ... */
	eError = RGXFWRequestCommonContextCleanUp(psDeviceNode,
			ps3DData->psServerCommonContext,
			RGXFWIF_DM_3D,
			PDUMP_FLAGS_CONTINUOUS);
	if (eError == PVRSRV_ERROR_RETRY)
	{
		return eError;
	}
	else if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RGXFWRequestCommonContextCleanUp (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		return eError;
	}

	/* ... it has so we can free its resources */
#if defined(DEBUG)
	/* Log the number of 3D context stores which occurred */
	{
		RGXFWIF_3DCTX_STATE	*psFW3DState;

		eError = DevmemAcquireCpuVirtAddr(ps3DData->psContextStateMemDesc,
				(void**)&psFW3DState);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to map firmware render context state (%s)",
					__func__, PVRSRVGetErrorString(eError)));
		}
		else
		{
			/* Release the CPU virt addr */
			DevmemReleaseCpuVirtAddr(ps3DData->psContextStateMemDesc);
		}
	}
#endif

	FWCommonContextFree(ps3DData->psServerCommonContext);
	DevmemFwUnmapAndFree(psDeviceNode->pvDevice, ps3DData->psContextStateMemDesc);
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
		PVR_DPF((PVR_DBG_ERROR,
				"Error (%s) printing pmr %p",
				PVRSRVGetErrorString(eError),
				psPMRNode->psPMR));
	}
}

IMG_BOOL RGXDumpFreeListPageList(RGX_FREELIST *psFreeList)
{
	DLLIST_NODE *psNode, *psNext;

	PVR_LOG(("Freelist FWAddr 0x%08x, ID = %d, CheckSum 0x%016" IMG_UINT64_FMTSPECx,
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
	IMG_BOOL bFreelistBad = IMG_FALSE;

	*pui64CalculatedCheckSum = 0;

	PVR_ASSERT(ui32NumOfPagesToCheck <= (psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages));

	/* Allocate Buffer of the size of the freelist */
	pui8Buffer = OSAllocMem(ui32NumOfPagesToCheck * sizeof(IMG_UINT32));
	if (pui8Buffer == NULL)
	{
		PVR_LOG(("%s: Failed to allocate buffer to check freelist %p!",
				__func__, psFreeList));
		PVR_ASSERT(0);
		return;
	}

	/* Copy freelist content into Buffer */
	eError = PMR_ReadBytes(psFreeList->psFreeListPMR,
			psFreeList->uiFreeListPMROffset +
			(((psFreeList->ui32MaxFLPages -
					psFreeList->ui32CurrentFLPages -
					psFreeList->ui32ReadyFLPages) * sizeof(IMG_UINT32)) &
					~((IMG_UINT64)RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE-1)),
					pui8Buffer,
					ui32NumOfPagesToCheck * sizeof(IMG_UINT32),
					&uiNumBytes);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(pui8Buffer);
		PVR_LOG(("%s: Failed to get freelist data for freelist %p!",
				__func__, psFreeList));
		PVR_ASSERT(0);
		return;
	}

	PVR_ASSERT(uiNumBytes == ui32NumOfPagesToCheck * sizeof(IMG_UINT32));

	/* Generate checksum (skipping the first page if not allocated) */
	pui32Buffer = (IMG_UINT32 *)pui8Buffer;
	ui32Entry = ((psFreeList->ui32GrowFLPages == 0  &&  psFreeList->ui32CurrentFLPages > 1) ? 1 : 0);
	for (/*ui32Entry*/ ; ui32Entry < ui32NumOfPagesToCheck; ui32Entry++)
	{
		ui32CheckSumAdd += pui32Buffer[ui32Entry];
		ui32CheckSumXor ^= pui32Buffer[ui32Entry];

		/* Check for double entries */
		for (ui32Entry2 = ui32Entry+1; ui32Entry2 < ui32NumOfPagesToCheck; ui32Entry2++)
		{
			if (pui32Buffer[ui32Entry] == pui32Buffer[ui32Entry2])
			{
				PVR_LOG(("%s: Freelist consistency failure: FW addr: 0x%08X, Double entry found 0x%08x on idx: %d and %d of %d",
						__func__,
						psFreeList->sFreeListFWDevVAddr.ui32Addr,
						pui32Buffer[ui32Entry2],
						ui32Entry,
						ui32Entry2,
						psFreeList->ui32CurrentFLPages));
				bFreelistBad = IMG_TRUE;
				break;
			}
		}
	}

	OSFreeMem(pui8Buffer);

	/* Check the calculated checksum against the expected checksum... */
	*pui64CalculatedCheckSum = ((IMG_UINT64)ui32CheckSumXor << 32) | ui32CheckSumAdd;

	if (ui64ExpectedCheckSum != 0  &&  ui64ExpectedCheckSum != *pui64CalculatedCheckSum)
	{
		PVR_LOG(("%s: Checksum mismatch for freelist %p! Expected 0x%016" IMG_UINT64_FMTSPECx " calculated 0x%016" IMG_UINT64_FMTSPECx,
				__func__, psFreeList,
				ui64ExpectedCheckSum, *pui64CalculatedCheckSum));
		bFreelistBad = IMG_TRUE;
	}

	if (bFreelistBad)
	{
		PVR_LOG(("%s: Sleeping for ever!", __func__));
		PVR_ASSERT(!bFreelistBad);
	}
#endif
}


/*
 *  Function to work out the number of freelist pages to reserve for growing
 *  within the FW without having to wait for the host to progress a grow
 *  request.
 *
 *  The number of pages must be a multiple of 4 to align the PM addresses
 *  for the initial freelist allocation and also be less than the grow size.
 *
 *  If the threshold or grow size means less than 4 pages, then the feature
 *  is not used.
 */
static IMG_UINT32 _CalculateFreelistReadyPages(RGX_FREELIST *psFreeList,
		IMG_UINT32  ui32FLPages)
{
	IMG_UINT32  ui32ReadyFLPages = ((ui32FLPages * psFreeList->ui32GrowThreshold) / 100) &
			~((RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE/sizeof(IMG_UINT32))-1);

	if (ui32ReadyFLPages > psFreeList->ui32GrowFLPages)
	{
		ui32ReadyFLPages = psFreeList->ui32GrowFLPages;
	}

	return ui32ReadyFLPages;
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
	static const IMG_CHAR szAllocName[] = "Free List";

	/* Are we allowed to grow ? */
	if (psFreeList->ui32MaxFLPages - (psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages) < ui32NumPages)
	{
		PVR_DPF((PVR_DBG_WARNING,
				"Freelist [0x%p]: grow by %u pages denied. "
				"Max PB size reached (current pages %u+%u/%u)",
				psFreeList,
				ui32NumPages,
				psFreeList->ui32CurrentFLPages,
				psFreeList->ui32ReadyFLPages,
				psFreeList->ui32MaxFLPages));
		return PVRSRV_ERROR_PBSIZE_ALREADY_MAX;
	}

	/* Allocate kernel memory block structure */
	psPMRNode = OSAllocMem(sizeof(*psPMRNode));
	if (psPMRNode == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed to allocate host data structure",
				__func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/*
	 * Lock protects simultaneous manipulation of:
	 * - the memory block list
	 * - the freelist's ui32CurrentFLPages
	 */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);


	/*
	 *  The PM never takes the last page in a freelist, so if this block
	 *  of pages is the first one and there is no ability to grow, then
	 *  we can skip allocating one 4K page for the lowest entry.
	 */
	if (OSGetPageSize() > RGX_BIF_PM_PHYSICAL_PAGE_SIZE)
	{
		/*
		 * Allocation size will be rounded up to the OS page size,
		 * any attempt to change it a bit now will be invalidated later.
		 */
		psPMRNode->bFirstPageMissing = IMG_FALSE;
	}
	else
	{
		psPMRNode->bFirstPageMissing = (psFreeList->ui32GrowFLPages == 0  &&  ui32NumPages > 1);
	}

	psPMRNode->ui32NumPages = ui32NumPages;
	psPMRNode->psFreeList = psFreeList;

	/* Allocate Memory Block */
	PDUMPCOMMENT(psFreeList->psDevInfo->psDeviceNode, "Allocate PB Block (Pages %08X)", ui32NumPages);
	uiSize = (IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE;
	if (psPMRNode->bFirstPageMissing)
	{
		uiSize -= RGX_BIF_PM_PHYSICAL_PAGE_SIZE;
	}
	eError = PhysmemNewRamBackedPMR(psFreeList->psConnection,
			psFreeList->psDevInfo->psDeviceNode,
			uiSize,
			1,
			1,
			&ui32MappingTable,
			RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
			PVRSRV_MEMALLOCFLAG_GPU_READABLE |
			PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(GPU_PRIVATE),
			sizeof(szAllocName),
			szAllocName,
			psFreeList->ownerPid,
			&psPMRNode->psPMR,
			PDUMP_NONE,
			NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate PB block of size: 0x%016" IMG_UINT64_FMTSPECX,
				__func__,
				(IMG_UINT64)uiSize));
		goto ErrorBlockAlloc;
	}

	/* Zeroing physical pages pointed by the PMR */
	if (psFreeList->psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to zero PMR %p of freelist %p (%s)",
					__func__,
					psPMRNode->psPMR,
					psFreeList,
					PVRSRVGetErrorString(eError)));
			PVR_ASSERT(0);
		}
	}

	uiLength = psPMRNode->ui32NumPages * sizeof(IMG_UINT32);
	uistartPage = (psFreeList->ui32MaxFLPages - psFreeList->ui32CurrentFLPages - psPMRNode->ui32NumPages);
	uiOffset = psFreeList->uiFreeListPMROffset + ((uistartPage * sizeof(IMG_UINT32)) & ~((IMG_UINT64)RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE-1));

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)

	eError = RIWritePMREntryWithOwnerKM(psPMRNode->psPMR,
			psFreeList->ownerPid);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: call to RIWritePMREntryWithOwnerKM failed (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
	}

	/* Attach RI information */
	eError = RIWriteMEMDESCEntryKM(psPMRNode->psPMR,
			OSStringNLength(szAllocName, DEVMEM_ANNOTATION_MAX_LEN),
			szAllocName,
			0,
			uiSize,
			IMG_FALSE,
			IMG_FALSE,
			&psPMRNode->hRIHandle);
	PVR_LOG_IF_ERROR(eError, "RIWriteMEMDESCEntryKM");

#endif /* if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) */

	/* write Freelist with Memory Block physical addresses */
	eError = PMRWritePMPageList(
			/* Target PMR, offset, and length */
			psFreeList->psFreeListPMR,
			(psPMRNode->bFirstPageMissing ? uiOffset + sizeof(IMG_UINT32) : uiOffset),
			(psPMRNode->bFirstPageMissing ? uiLength - sizeof(IMG_UINT32) : uiLength),
			/* Referenced PMR, and "page" granularity */
			psPMRNode->psPMR,
			RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
			&psPMRNode->psPageList);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to write pages of Node %p",
				__func__,
				psPMRNode));
		goto ErrorPopulateFreelist;
	}

#if defined(SUPPORT_SHADOW_FREELISTS)
	/* Copy freelist memory to shadow freelist */
	{
		const IMG_UINT32 ui32FLMaxSize = psFreeList->ui32MaxFLPages * sizeof(IMG_UINT32);
		const IMG_UINT32 ui32MapSize = ui32FLMaxSize * 2;
		const IMG_UINT32 ui32CopyOffset = uiOffset - psFreeList->uiFreeListPMROffset;
		IMG_BYTE *pFLMapAddr;
		size_t uiNumBytes;
		PVRSRV_ERROR res;
		IMG_HANDLE hMapHandle;

		/* Map both the FL and the shadow FL */
		res = PMRAcquireKernelMappingData(psFreeList->psFreeListPMR, psFreeList->uiFreeListPMROffset, ui32MapSize,
				(void**) &pFLMapAddr, &uiNumBytes, &hMapHandle);
		if (res != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to map freelist (ID=%d)",
					__func__,
					psFreeList->ui32FreelistID));
			goto ErrorPopulateFreelist;
		}

		/* Copy only the newly added memory */
		OSCachedMemCopy(pFLMapAddr + ui32FLMaxSize + ui32CopyOffset, pFLMapAddr + ui32CopyOffset , uiLength);
		OSWriteMemoryBarrier(pFLMapAddr);

#if defined(PDUMP)
		PDUMPCOMMENT(psFreeList->psDevInfo->psDeviceNode, "Initialize shadow freelist");

		/* Translate memcpy to pdump */
		{
			IMG_DEVMEM_OFFSET_T uiCurrOffset;

			for (uiCurrOffset = uiOffset; (uiCurrOffset - uiOffset) < uiLength; uiCurrOffset += sizeof(IMG_UINT32))
			{
				PMRPDumpCopyMem32(psFreeList->psFreeListPMR,
						uiCurrOffset + ui32FLMaxSize,
						psFreeList->psFreeListPMR,
						uiCurrOffset,
						":SYSMEM:$1",
						PDUMP_FLAGS_CONTINUOUS);
			}
		}
#endif


		res = PMRReleaseKernelMappingData(psFreeList->psFreeListPMR, hMapHandle);

		if (res != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to release freelist mapping (ID=%d)",
					__func__,
					psFreeList->ui32FreelistID));
			goto ErrorPopulateFreelist;
		}
	}
#endif

	/* We add It must be added to the tail, otherwise the freelist population won't work */
	dllist_add_to_head(pListHeader, &psPMRNode->sMemoryBlock);

	/* Update number of available pages */
	psFreeList->ui32CurrentFLPages += ui32NumPages;

	/* Update statistics (needs to happen before the ReadyFL calculation to also count those pages) */
	if (psFreeList->ui32NumHighPages < psFreeList->ui32CurrentFLPages)
	{
		psFreeList->ui32NumHighPages = psFreeList->ui32CurrentFLPages;
	}

	/* Reserve a number ready pages to allow the FW to process OOM quickly and asynchronously request a grow. */
	psFreeList->ui32ReadyFLPages    = _CalculateFreelistReadyPages(psFreeList, psFreeList->ui32CurrentFLPages);
	psFreeList->ui32CurrentFLPages -= psFreeList->ui32ReadyFLPages;

	if (psFreeList->bCheckFreelist)
	{
		/*
		 *  We can only calculate the freelist checksum when the list is full
		 *  (e.g. at initial creation time). At other times the checksum cannot
		 *  be calculated and has to be disabled for this freelist.
		 */
		if ((psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages) == ui32NumPages)
		{
			_CheckFreelist(psFreeList, ui32NumPages, 0, &psFreeList->ui64FreelistChecksum);
		}
		else
		{
			psFreeList->ui64FreelistChecksum = 0;
		}
	}
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	PVR_DPF((PVR_DBG_MESSAGE,
			"Freelist [%p]: %s %u pages (pages=%u+%u/%u checksum=0x%016" IMG_UINT64_FMTSPECx "%s)",
			psFreeList,
			((psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages) == ui32NumPages ? "Create initial" : "Grow by"),
			ui32NumPages,
			psFreeList->ui32CurrentFLPages,
			psFreeList->ui32ReadyFLPages,
			psFreeList->ui32MaxFLPages,
			psFreeList->ui64FreelistChecksum,
			(psPMRNode->bFirstPageMissing ? " - lowest page not allocated" : "")));

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
					"%s: Failed to unwrite pages of Node %p",
					__func__,
					psPMRNode));
			PVR_ASSERT(IMG_FALSE);
		}

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)

		if (psPMRNode->hRIHandle)
		{
			PVRSRV_ERROR eError;

			eError = RIDeleteMEMDESCEntryKM(psPMRNode->hRIHandle);
			PVR_LOG_IF_ERROR(eError, "RIDeleteMEMDESCEntryKM");
		}

#endif  /* if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) */

		/* Free PMR (We should be the only one that holds a ref on the PMR) */
		eError = PMRUnrefPMR(psPMRNode->psPMR);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to free PB block %p (%s)",
					__func__,
					psPMRNode->psPMR,
					PVRSRVGetErrorString(eError)));
			PVR_ASSERT(IMG_FALSE);
		}

		/* update available pages in freelist */
		ui32OldValue = psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages;

		/*
		 * Deallocated pages should first be deducted from ReadyPages bank, once
		 * there are no more left, start deducting them from CurrentPage bank.
		 */
		if (psPMRNode->ui32NumPages > psFreeList->ui32ReadyFLPages)
		{
			psFreeList->ui32CurrentFLPages -= psPMRNode->ui32NumPages - psFreeList->ui32ReadyFLPages;
			psFreeList->ui32ReadyFLPages = 0;
		}
		else
		{
			psFreeList->ui32ReadyFLPages -= psPMRNode->ui32NumPages;
		}

		/* check underflow */
		PVR_ASSERT(ui32OldValue > (psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages));

		PVR_DPF((PVR_DBG_MESSAGE, "Freelist [%p]: shrink by %u pages (current pages %u/%u)",
				psFreeList,
				psPMRNode->ui32NumPages,
				psFreeList->ui32CurrentFLPages,
				psFreeList->ui32MaxFLPages));

		OSFreeMem(psPMRNode);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING,
				"Freelist [0x%p]: shrink denied. PB already at initial PB size (%u pages)",
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
		/* Since the FW made the request, it has already consumed the ready pages, update the host struct */
		psFreeList->ui32CurrentFLPages += psFreeList->ui32ReadyFLPages;
		psFreeList->ui32ReadyFLPages = 0;

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
			PVRSRVStatsUpdateFreelistStats(psDevInfo->psDeviceNode,
					0,
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
			PVR_DPF((PVR_DBG_ERROR,
					"Grow for FreeList %p failed (%s)",
					psFreeList,
					PVRSRVGetErrorString(eError)));
		}

		/* send feedback */
		s3DCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELIST_GROW_UPDATE;
		s3DCCBCmd.uCmdData.sFreeListGSData.sFreeListFWDevVAddr.ui32Addr = psFreeList->sFreeListFWDevVAddr.ui32Addr;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32DeltaPages = ui32GrowValue;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32NewPages = psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages;
		s3DCCBCmd.uCmdData.sFreeListGSData.ui32ReadyPages = psFreeList->ui32ReadyFLPages;


		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
					RGXFWIF_DM_3D,
					&s3DCCBCmd,
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
		PVR_DPF((PVR_DBG_ERROR,
				"FreeList Lookup for FreeList ID 0x%08x failed (Populate)",
				ui32FreelistID));
		PVR_ASSERT(IMG_FALSE);
	}
}

static void _RGXFreeListReconstruction(PDLLIST_NODE psNode)
{

	PVRSRV_RGXDEV_INFO		*psDevInfo;
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
	uiOffset = psFreeList->uiFreeListPMROffset + ((ui32StartPage * sizeof(IMG_UINT32)) & ~((IMG_UINT64)RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE-1));

	PMRUnwritePMPageList(psPMRNode->psPageList);
	psPMRNode->psPageList = NULL;
	eError = PMRWritePMPageList(
			/* Target PMR, offset, and length */
			psFreeList->psFreeListPMR,
			(psPMRNode->bFirstPageMissing ? uiOffset + sizeof(IMG_UINT32) : uiOffset),
			(psPMRNode->bFirstPageMissing ? uiLength - sizeof(IMG_UINT32) : uiLength),
			/* Referenced PMR, and "page" granularity */
			psPMRNode->psPMR,
			RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
			&psPMRNode->psPageList);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Error (%s) writing FL 0x%08x",
				__func__,
				PVRSRVGetErrorString(eError),
				(IMG_UINT32)psFreeList->ui32FreelistID));
	}

	/* Zeroing physical pages pointed by the reconstructed freelist */
	if (psDevInfo->ui32DeviceFlags & RGXKM_DEVICE_STATE_ZERO_FREELIST)
	{
		eError = PMRZeroingPMR(psPMRNode->psPMR, RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to zero PMR %p of freelist %p (%s)",
					__func__,
					psPMRNode->psPMR,
					psFreeList,
					PVRSRVGetErrorString(eError)));
			PVR_ASSERT(0);
		}
	}


	psFreeList->ui32CurrentFLPages += psPMRNode->ui32NumPages;
}


static PVRSRV_ERROR RGXReconstructFreeList(RGX_FREELIST *psFreeList)
{
	IMG_UINT32        ui32OriginalFLPages;
	DLLIST_NODE       *psNode, *psNext;
	RGXFWIF_FREELIST  *psFWFreeList;
	PVRSRV_ERROR      eError;

	//PVR_DPF((PVR_DBG_ERROR, "FreeList RECONSTRUCTION: Reconstructing freelist %p (ID=%u)", psFreeList, psFreeList->ui32FreelistID));

	/* Do the FreeList Reconstruction */
	ui32OriginalFLPages            = psFreeList->ui32CurrentFLPages;
	psFreeList->ui32CurrentFLPages = 0;

	/* Reconstructing Init FreeList pages */
	dllist_foreach_node(&psFreeList->sMemoryBlockInitHead, psNode, psNext)
	{
		_RGXFreeListReconstruction(psNode);
	}

	/* Reconstructing Grow FreeList pages */
	dllist_foreach_node(&psFreeList->sMemoryBlockHead, psNode, psNext)
	{
		_RGXFreeListReconstruction(psNode);
	}

	/* Ready pages are allocated but kept hidden until OOM occurs. */
	psFreeList->ui32CurrentFLPages -= psFreeList->ui32ReadyFLPages;
	if (psFreeList->ui32CurrentFLPages != ui32OriginalFLPages)
	{
		PVR_ASSERT(psFreeList->ui32CurrentFLPages == ui32OriginalFLPages);
		return PVRSRV_ERROR_FREELIST_RECONSTRUCTION_FAILED;
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

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);

	/* Check the Freelist checksum if required (as the list is fully populated) */
	if (psFreeList->bCheckFreelist)
	{
		IMG_UINT64  ui64CheckSum;

		_CheckFreelist(psFreeList, psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages, psFreeList->ui64FreelistChecksum, &ui64CheckSum);
	}

	return eError;
}


void RGXProcessRequestFreelistsReconstruction(PVRSRV_RGXDEV_INFO *psDevInfo,
                                              IMG_UINT32 ui32FreelistsCount,
                                              const IMG_UINT32 *paui32Freelists)
{
	PVRSRV_ERROR      eError = PVRSRV_OK;
	DLLIST_NODE       *psNode, *psNext;
	IMG_UINT32        ui32Loop;
	RGXFWIF_KCCB_CMD  sTACCBCmd;
#if !defined(SUPPORT_SHADOW_FREELISTS)
	DLLIST_NODE       *psNodeHWRTData, *psNextHWRTData;
	RGX_KM_HW_RT_DATASET *psKMHWRTDataSet;
	RGXFWIF_HWRTDATA     *psHWRTData;
#endif
	IMG_UINT32        ui32FinalFreelistsCount = 0;
	IMG_UINT32        aui32FinalFreelists[RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT * 2]; /* Worst-case is double what we are sent */

	PVR_ASSERT(psDevInfo != NULL);
	PVR_ASSERT(ui32FreelistsCount <= RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT);
	if (ui32FreelistsCount > RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT)
	{
		ui32FreelistsCount = RGXFWIF_MAX_FREELISTS_TO_RECONSTRUCT;
	}

	//PVR_DPF((PVR_DBG_ERROR, "FreeList RECONSTRUCTION: %u freelist(s) requested for reconstruction", ui32FreelistsCount));

	/*
	 *  Initialise the response command (in case we don't find a freelist ID).
	 *  Also copy the list to the 'final' freelist array.
	 */
	sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE;
	sTACCBCmd.uCmdData.sFreeListsReconstructionData.ui32FreelistsCount = ui32FreelistsCount;

	for (ui32Loop = 0; ui32Loop < ui32FreelistsCount; ui32Loop++)
	{
		sTACCBCmd.uCmdData.sFreeListsReconstructionData.aui32FreelistIDs[ui32Loop] = paui32Freelists[ui32Loop] |
				RGXFWIF_FREELISTS_RECONSTRUCTION_FAILED_FLAG;
		aui32FinalFreelists[ui32Loop] = paui32Freelists[ui32Loop];
	}

	ui32FinalFreelistsCount = ui32FreelistsCount;

	/*
	 *  The list of freelists we have been given for reconstruction will
	 *  consist of local and global freelists (maybe MMU as well). Any
	 *  local freelists should have their global list specified as well.
	 *  There may be cases where the global freelist is not given (in
	 *  cases of partial setups before a poll failure for example). To
	 *  handle that we must first ensure every local freelist has a global
	 *  freelist specified, otherwise we add that to the 'final' list.
	 *  This final list of freelists is created in a first pass.
	 *
	 *  Even with the global freelists listed, there may be other local
	 *  freelists not listed, which are going to have their global freelist
	 *  reconstructed. Therefore we have to find those freelists as well
	 *  meaning we will have to iterate the entire list of freelists to
	 *  find which must be reconstructed. This is the second pass.
	 */
	OSLockAcquire(psDevInfo->hLockFreeList);
	dllist_foreach_node(&psDevInfo->sFreeListHead, psNode, psNext)
	{
		RGX_FREELIST  *psFreeList   = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);
		IMG_BOOL      bInList       = IMG_FALSE;
		IMG_BOOL      bGlobalInList = IMG_FALSE;

		/* Check if this local freelist is in the list and ensure its global is too. */
		if (psFreeList->ui32FreelistGlobalID != 0)
		{
			for (ui32Loop = 0; ui32Loop < ui32FinalFreelistsCount; ui32Loop++)
			{
				if (aui32FinalFreelists[ui32Loop] == psFreeList->ui32FreelistID)
				{
					bInList = IMG_TRUE;
				}
				if (aui32FinalFreelists[ui32Loop] == psFreeList->ui32FreelistGlobalID)
				{
					bGlobalInList = IMG_TRUE;
				}
			}

			if (bInList  &&  !bGlobalInList)
			{
				aui32FinalFreelists[ui32FinalFreelistsCount] = psFreeList->ui32FreelistGlobalID;
				ui32FinalFreelistsCount++;
			}
		}
	}
	dllist_foreach_node(&psDevInfo->sFreeListHead, psNode, psNext)
	{
		RGX_FREELIST  *psFreeList  = IMG_CONTAINER_OF(psNode, RGX_FREELIST, sNode);
		IMG_BOOL      bReconstruct = IMG_FALSE;

		/*
		 *  Check if this freelist needs to be reconstructed (was it requested
		 *  or is its global freelist going to be reconstructed)...
		 */
		for (ui32Loop = 0; ui32Loop < ui32FinalFreelistsCount; ui32Loop++)
		{
			if (aui32FinalFreelists[ui32Loop] == psFreeList->ui32FreelistID  ||
			    aui32FinalFreelists[ui32Loop] == psFreeList->ui32FreelistGlobalID)
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
#if !defined(SUPPORT_SHADOW_FREELISTS)
				/* Mark all HWRTData's of reconstructing local freelists as HWR (applies to TA/3D's not finished yet) */
				dllist_foreach_node(&psFreeList->sNodeHWRTDataHead, psNodeHWRTData, psNextHWRTData)
				{
					psKMHWRTDataSet = IMG_CONTAINER_OF(psNodeHWRTData, RGX_KM_HW_RT_DATASET, sNodeHWRTData);
					eError = DevmemAcquireCpuVirtAddr(psKMHWRTDataSet->psHWRTDataFwMemDesc, (void **)&psHWRTData);
					if (eError != PVRSRV_OK)
					{
						PVR_DPF((PVR_DBG_ERROR,
									"Devmem AcquireCpuVirtAddr Failed during Reconstructing of FreeList, FwMemDesc(%p),psHWRTData(%p)",
									psKMHWRTDataSet->psHWRTDataFwMemDesc,
									psHWRTData));
						continue;
					}

					psHWRTData->eState = RGXFWIF_RTDATA_STATE_HWR;
					psHWRTData->ui32HWRTDataFlags &= ~HWRTDATA_HAS_LAST_TA;

					DevmemReleaseCpuVirtAddr(psKMHWRTDataSet->psHWRTDataFwMemDesc);
				}
#endif

				/* Update the response for this freelist if it was specifically requested for reconstruction. */
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
				PVR_DPF((PVR_DBG_ERROR,
						"Reconstructing of FreeList %p failed (%s)",
						psFreeList,
						PVRSRVGetErrorString(eError)));
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
				RGXFWIF_DM_GEOM,
				&sTACCBCmd,
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

/* Create a single HWRTData instance */
static PVRSRV_ERROR RGXCreateHWRTData_aux(
		CONNECTION_DATA      *psConnection,
		PVRSRV_DEVICE_NODE	*psDeviceNode,
		IMG_DEV_VIRTADDR	psVHeapTableDevVAddr,
		IMG_DEV_VIRTADDR		psPMMListDevVAddr, /* per-HWRTData */
		RGX_FREELIST			*apsFreeLists[RGXFW_MAX_FREELISTS],
		IMG_DEV_VIRTADDR		sTailPtrsDevVAddr,
		IMG_DEV_VIRTADDR	sMacrotileArrayDevVAddr, /* per-HWRTData */
		IMG_DEV_VIRTADDR	sRgnHeaderDevVAddr, /* per-HWRTData */
		IMG_DEV_VIRTADDR	sRTCDevVAddr,
		IMG_UINT16			ui16MaxRTs,
		RGX_HWRTDATA_COMMON_COOKIE	*psHWRTDataCommonCookie,
		RGX_KM_HW_RT_DATASET **ppsKMHWRTDataSet) /* per-HWRTData */
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32Loop;

	/* KM cookie storing all the FW/HW data */
	RGX_KM_HW_RT_DATASET *psKMHWRTDataSet;

	/* local pointers for memory descriptors of FW allocations */
	DEVMEM_MEMDESC *psHWRTDataFwMemDesc = NULL;
	DEVMEM_MEMDESC *psRTArrayFwMemDesc = NULL;
	DEVMEM_MEMDESC *psRendersAccArrayFwMemDesc = NULL;

	/* local pointer for CPU-mapped [FW]HWRTData */
	RGXFWIF_HWRTDATA *psHWRTData = NULL;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* Prepare the HW RT DataSet struct */
	psKMHWRTDataSet = OSAllocZMem(sizeof(*psKMHWRTDataSet));
	if (psKMHWRTDataSet == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto AllocError;
	}

	*ppsKMHWRTDataSet = psKMHWRTDataSet;
	psKMHWRTDataSet->psDeviceNode = psDeviceNode;

	psKMHWRTDataSet->psHWRTDataCommonCookie = psHWRTDataCommonCookie;

	psDevInfo = psDeviceNode->pvDevice;

	/*
	 * This FW RT-Data is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency, and write-combine will
	 * suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(RGXFWIF_HWRTDATA),
			RGX_FWCOMCTX_ALLOCFLAGS,
			"FwHWRTData",
			&psHWRTDataFwMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: DevmemAllocate for RGX_FWIF_HWRTDATA failed",
				__func__));
		goto FWRTDataAllocateError;
	}

	psKMHWRTDataSet->psHWRTDataFwMemDesc = psHWRTDataFwMemDesc;
	eError = RGXSetFirmwareAddress( &psKMHWRTDataSet->sHWRTDataFwAddr,
							psHWRTDataFwMemDesc,
							0,
							RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress:1", FWRTDataFwAddrError);

	eError = DevmemAcquireCpuVirtAddr(psHWRTDataFwMemDesc,
									  (void **)&psHWRTData);
	PVR_LOG_GOTO_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", FWRTDataCpuMapError);

	psHWRTData->psVHeapTableDevVAddr = psVHeapTableDevVAddr;

	psHWRTData->sHWRTDataCommonFwAddr = psHWRTDataCommonCookie->sHWRTDataCommonFwAddr;

	psHWRTData->psPMMListDevVAddr = psPMMListDevVAddr;

	psHWRTData->sTailPtrsDevVAddr     = sTailPtrsDevVAddr;
	psHWRTData->sMacrotileArrayDevVAddr = sMacrotileArrayDevVAddr;
	psHWRTData->sRgnHeaderDevVAddr		= sRgnHeaderDevVAddr;
	psHWRTData->sRTCDevVAddr			= sRTCDevVAddr;

	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		psKMHWRTDataSet->apsFreeLists[ui32Loop] = apsFreeLists[ui32Loop];
		psKMHWRTDataSet->apsFreeLists[ui32Loop]->ui32RefCount++;
		psHWRTData->apsFreeLists[ui32Loop].ui32Addr = psKMHWRTDataSet->apsFreeLists[ui32Loop]->sFreeListFWDevVAddr.ui32Addr;
		/* invalid initial snapshot value, the snapshot is always taken during first kick
		 * and hence the value get replaced during the first kick anyway. So it's safe to set it 0.
		 */
		psHWRTData->aui32FreeListHWRSnapshot[ui32Loop] = 0;
	}
#if !defined(SUPPORT_SHADOW_FREELISTS)
	dllist_add_to_tail(&apsFreeLists[RGXFW_LOCAL_FREELIST]->sNodeHWRTDataHead, &(psKMHWRTDataSet->sNodeHWRTData));
#endif
	OSLockRelease(psDevInfo->hLockFreeList);

	{
		RGXFWIF_RTA_CTL *psRTACtl = &psHWRTData->sRTACtl;

		psRTACtl->ui32RenderTargetIndex = 0;
		psRTACtl->ui32ActiveRenderTargets = 0;
		psRTACtl->sValidRenderTargets.ui32Addr = 0;
		psRTACtl->sRTANumPartialRenders.ui32Addr = 0;
		psRTACtl->ui32MaxRTs = (IMG_UINT32) ui16MaxRTs;

		if (ui16MaxRTs > 1)
		{
			PDUMPCOMMENT(psDeviceNode, "Allocate memory for shadow render target cache");
			eError = DevmemFwAllocate(psDevInfo,
					ui16MaxRTs * sizeof(IMG_UINT32),
					PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
					PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
					PVRSRV_MEMALLOCFLAG_GPU_READABLE |
					PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
					PVRSRV_MEMALLOCFLAG_GPU_UNCACHED |
					PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
					PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
					"FwShadowRTCache",
					&psRTArrayFwMemDesc);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to allocate %u bytes for render target array (%s)",
						__func__,
						ui16MaxRTs, PVRSRVGetErrorString(eError)));
				goto FWAllocateRTArryError;
			}

			psKMHWRTDataSet->psRTArrayFwMemDesc = psRTArrayFwMemDesc;
			eError = RGXSetFirmwareAddress(&psRTACtl->sValidRenderTargets,
					psRTArrayFwMemDesc,
					0,
					RFW_FWADDR_FLAG_NONE);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress:2", FWAllocateRTArryFwAddrError);

			PDUMPCOMMENT(psDeviceNode, "Allocate memory for tracking renders accumulation");
			eError = DevmemFwAllocate(psDevInfo,
					ui16MaxRTs * sizeof(IMG_UINT32),
					PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
					PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
					PVRSRV_MEMALLOCFLAG_GPU_READABLE |
					PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
					PVRSRV_MEMALLOCFLAG_GPU_UNCACHED |
					PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
					PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
					"FwRendersAccumulation",
					&psRendersAccArrayFwMemDesc);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to allocate %u bytes for render target array (%s) (renders accumulation)",
						__func__,
						ui16MaxRTs, PVRSRVGetErrorString(eError)));
				goto FWAllocateRTAccArryError;
			}
			psKMHWRTDataSet->psRendersAccArrayFwMemDesc = psRendersAccArrayFwMemDesc;
			eError = RGXSetFirmwareAddress(&psRTACtl->sRTANumPartialRenders,
					psRendersAccArrayFwMemDesc,
					0,
					RFW_FWADDR_FLAG_NONE);
			PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress:3", FWAllocRTAccArryFwAddrError);
		}
	}

#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode, "Dump HWRTData 0x%08X", psKMHWRTDataSet->sHWRTDataFwAddr.ui32Addr);
	DevmemPDumpLoadMem(psKMHWRTDataSet->psHWRTDataFwMemDesc, 0, sizeof(*psHWRTData), PDUMP_FLAGS_CONTINUOUS);
#endif

	DevmemReleaseCpuVirtAddr(psKMHWRTDataSet->psHWRTDataFwMemDesc);
	return PVRSRV_OK;

FWAllocRTAccArryFwAddrError:
	DevmemFwUnmapAndFree(psDevInfo, psRendersAccArrayFwMemDesc);
FWAllocateRTAccArryError:
	RGXUnsetFirmwareAddress(psKMHWRTDataSet->psRTArrayFwMemDesc);
FWAllocateRTArryFwAddrError:
	DevmemFwUnmapAndFree(psDevInfo, psKMHWRTDataSet->psRTArrayFwMemDesc);
FWAllocateRTArryError:
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psKMHWRTDataSet->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psKMHWRTDataSet->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
	OSLockRelease(psDevInfo->hLockFreeList);
	DevmemReleaseCpuVirtAddr(psKMHWRTDataSet->psHWRTDataFwMemDesc);
FWRTDataCpuMapError:
	RGXUnsetFirmwareAddress(psKMHWRTDataSet->psHWRTDataFwMemDesc);
FWRTDataFwAddrError:
	DevmemFwUnmapAndFree(psDevInfo, psKMHWRTDataSet->psHWRTDataFwMemDesc);
FWRTDataAllocateError:
	*ppsKMHWRTDataSet = NULL;
	OSFreeMem(psKMHWRTDataSet);

AllocError:
	return eError;
}

static void RGXDestroyHWRTData_aux(RGX_KM_HW_RT_DATASET *psKMHWRTDataSet)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_UINT32 ui32Loop;

	if (psKMHWRTDataSet == NULL)
	{
		return;
	}

	psDevInfo = psKMHWRTDataSet->psDeviceNode->pvDevice;

	if (psKMHWRTDataSet->psRTArrayFwMemDesc)
	{
		RGXUnsetFirmwareAddress(psKMHWRTDataSet->psRTArrayFwMemDesc);
		DevmemFwUnmapAndFree(psDevInfo, psKMHWRTDataSet->psRTArrayFwMemDesc);
	}

	if (psKMHWRTDataSet->psRendersAccArrayFwMemDesc)
	{
		RGXUnsetFirmwareAddress(psKMHWRTDataSet->psRendersAccArrayFwMemDesc);
		DevmemFwUnmapAndFree(psDevInfo, psKMHWRTDataSet->psRendersAccArrayFwMemDesc);
	}

	/* Decrease freelist refcount */
	OSLockAcquire(psDevInfo->hLockFreeList);
	for (ui32Loop = 0; ui32Loop < RGXFW_MAX_FREELISTS; ui32Loop++)
	{
		PVR_ASSERT(psKMHWRTDataSet->apsFreeLists[ui32Loop]->ui32RefCount > 0);
		psKMHWRTDataSet->apsFreeLists[ui32Loop]->ui32RefCount--;
	}
#if !defined(SUPPORT_SHADOW_FREELISTS)
	dllist_remove_node(&psKMHWRTDataSet->sNodeHWRTData);
#endif
	OSLockRelease(psDevInfo->hLockFreeList);

	/* Freeing the memory has to happen _after_ removing the HWRTData from the freelist
	 * otherwise we risk traversing the freelist to find a pointer from a freed data structure */
	RGXUnsetFirmwareAddress(psKMHWRTDataSet->psHWRTDataFwMemDesc);
	DevmemFwUnmapAndFree(psDevInfo, psKMHWRTDataSet->psHWRTDataFwMemDesc);

	OSFreeMem(psKMHWRTDataSet);
}

/* Create set of HWRTData(s) and bind it with a shared FW HWRTDataCommon */
PVRSRV_ERROR RGXCreateHWRTDataSet(CONNECTION_DATA      *psConnection,
		PVRSRV_DEVICE_NODE	*psDeviceNode,
		IMG_DEV_VIRTADDR	asVHeapTableDevVAddr[RGXMKIF_NUM_GEOMDATAS],
		IMG_DEV_VIRTADDR		asPMMListDevVAddr[RGXMKIF_NUM_RTDATAS],
		RGX_FREELIST			*apsFreeLists[RGXMKIF_NUM_RTDATA_FREELISTS],
		IMG_UINT32           ui32ScreenPixelMax,
		IMG_UINT64           ui64MultiSampleCtl,
		IMG_UINT64           ui64FlippedMultiSampleCtl,
		IMG_UINT32           ui32TPCStride,
		IMG_DEV_VIRTADDR		asTailPtrsDevVAddr[RGXMKIF_NUM_GEOMDATAS],
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
		IMG_DEV_VIRTADDR	asMacrotileArrayDevVAddr[RGXMKIF_NUM_RTDATAS],
		IMG_DEV_VIRTADDR	asRgnHeaderDevVAddr[RGXMKIF_NUM_RTDATAS],
		IMG_DEV_VIRTADDR	asRTCDevVAddr[RGXMKIF_NUM_GEOMDATAS],
		IMG_UINT32			uiRgnHeaderSize,
		IMG_UINT32			ui32ISPMtileSize,
		IMG_UINT16			ui16MaxRTs,
		RGX_KM_HW_RT_DATASET *pasKMHWRTDataSet[RGXMKIF_NUM_RTDATAS])
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32RTDataID;
	PVRSRV_RGXDEV_INFO		*psDevInfo = psDeviceNode->pvDevice;

	RGX_HWRTDATA_COMMON_COOKIE	*psHWRTDataCommonCookie;
	RGXFWIF_HWRTDATA_COMMON		*psHWRTDataCommon;
	DEVMEM_MEMDESC				*psHWRTDataCommonFwMemDesc;
	RGXFWIF_DEV_VIRTADDR		sHWRTDataCommonFwAddr;

	/* Prepare KM cleanup object for HWRTDataCommon FW object */
	psHWRTDataCommonCookie = OSAllocZMem(sizeof(*psHWRTDataCommonCookie));
	if (psHWRTDataCommonCookie == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err_HWRTDataCommonCookieAlloc;
	}

	/*
	 * This FW common context is only mapped into kernel for initialisation.
	 * Otherwise this allocation is only used by the FW.
	 * Therefore the GPU cache doesn't need coherency, and write-combine will
	 * suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(RGXFWIF_HWRTDATA_COMMON),
			RGX_FWCOMCTX_ALLOCFLAGS,
			"FwHWRTDataCommon",
			&psHWRTDataCommonFwMemDesc);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: DevmemAllocate for FwHWRTDataCommon failed", __func__));
		goto err_HWRTDataCommonAlloc;
	}
	eError = RGXSetFirmwareAddress(&sHWRTDataCommonFwAddr, psHWRTDataCommonFwMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", err_HWRTDataCommonFwAddr);

	eError = DevmemAcquireCpuVirtAddr(psHWRTDataCommonFwMemDesc, (void **)&psHWRTDataCommon);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", err_HWRTDataCommonVA);

	psHWRTDataCommon->bTACachesNeedZeroing = IMG_FALSE;
	psHWRTDataCommon->ui32ScreenPixelMax    = ui32ScreenPixelMax;
	psHWRTDataCommon->ui64MultiSampleCtl = ui64MultiSampleCtl;
	psHWRTDataCommon->ui64FlippedMultiSampleCtl = ui64FlippedMultiSampleCtl;
	psHWRTDataCommon->ui32TPCStride         = ui32TPCStride;
	psHWRTDataCommon->ui32TPCSize           = ui32TPCSize;
	psHWRTDataCommon->ui32TEScreen          = ui32TEScreen;
	psHWRTDataCommon->ui32TEAA              = ui32TEAA;
	psHWRTDataCommon->ui32TEMTILE1          = ui32TEMTILE1;
	psHWRTDataCommon->ui32TEMTILE2          = ui32TEMTILE2;
	psHWRTDataCommon->ui32MTileStride       = ui32MTileStride;
	psHWRTDataCommon->ui32ISPMergeLowerX = ui32ISPMergeLowerX;
	psHWRTDataCommon->ui32ISPMergeLowerY = ui32ISPMergeLowerY;
	psHWRTDataCommon->ui32ISPMergeUpperX = ui32ISPMergeUpperX;
	psHWRTDataCommon->ui32ISPMergeUpperY = ui32ISPMergeUpperY;
	psHWRTDataCommon->ui32ISPMergeScaleX = ui32ISPMergeScaleX;
	psHWRTDataCommon->ui32ISPMergeScaleY = ui32ISPMergeScaleY;
	psHWRTDataCommon->uiRgnHeaderSize			= uiRgnHeaderSize;
	psHWRTDataCommon->ui32ISPMtileSize		= ui32ISPMtileSize;
#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode, "Dump HWRTDataCommon");
	DevmemPDumpLoadMem(psHWRTDataCommonFwMemDesc, 0, sizeof(*psHWRTDataCommon), PDUMP_FLAGS_CONTINUOUS);
#endif
	DevmemReleaseCpuVirtAddr(psHWRTDataCommonFwMemDesc);

	psHWRTDataCommonCookie->ui32RefCount = 0;
	psHWRTDataCommonCookie->psHWRTDataCommonFwMemDesc = psHWRTDataCommonFwMemDesc;
	psHWRTDataCommonCookie->sHWRTDataCommonFwAddr = sHWRTDataCommonFwAddr;

	/* Here we are creating a set of HWRTData(s)
	   the number of elements in the set equals RGXMKIF_NUM_RTDATAS.
	*/

	for (ui32RTDataID = 0; ui32RTDataID < RGXMKIF_NUM_RTDATAS; ui32RTDataID++)
	{
		eError = RGXCreateHWRTData_aux(
			psConnection,
			psDeviceNode,
			asVHeapTableDevVAddr[ui32RTDataID % RGXMKIF_NUM_GEOMDATAS],
			asPMMListDevVAddr[ui32RTDataID],
			&apsFreeLists[(ui32RTDataID % RGXMKIF_NUM_GEOMDATAS) * RGXFW_MAX_FREELISTS],
			asTailPtrsDevVAddr[ui32RTDataID % RGXMKIF_NUM_GEOMDATAS],
			asMacrotileArrayDevVAddr[ui32RTDataID],
			asRgnHeaderDevVAddr[ui32RTDataID],
			asRTCDevVAddr[ui32RTDataID % RGXMKIF_NUM_GEOMDATAS],
			ui16MaxRTs,
			psHWRTDataCommonCookie,
			&pasKMHWRTDataSet[ui32RTDataID]);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to create HWRTData [slot %u] (%s)",
					__func__,
					ui32RTDataID,
					PVRSRVGetErrorString(eError)));
			goto err_HWRTDataAlloc;
		}
		psHWRTDataCommonCookie->ui32RefCount += 1;
	}

	return PVRSRV_OK;

err_HWRTDataAlloc:
	PVR_DPF((PVR_DBG_WARNING, "%s: err_HWRTDataAlloc %u",
			 __func__, psHWRTDataCommonCookie->ui32RefCount));
	if (pasKMHWRTDataSet)
	{
		for (ui32RTDataID = psHWRTDataCommonCookie->ui32RefCount; ui32RTDataID > 0; ui32RTDataID--)
		{
			if (pasKMHWRTDataSet[ui32RTDataID-1] != NULL)
			{
				RGXDestroyHWRTData_aux(pasKMHWRTDataSet[ui32RTDataID-1]);
				pasKMHWRTDataSet[ui32RTDataID-1] = NULL;
			}
		}
	}
err_HWRTDataCommonVA:
	RGXUnsetFirmwareAddress(psHWRTDataCommonFwMemDesc);
err_HWRTDataCommonFwAddr:
	DevmemFwUnmapAndFree(psDevInfo, psHWRTDataCommonFwMemDesc);
err_HWRTDataCommonAlloc:
	OSFreeMem(psHWRTDataCommonCookie);
err_HWRTDataCommonCookieAlloc:

	return eError;
}

/* Destroy a single instance of HWRTData.
   Additionally, destroy the HWRTDataCommon{Cookie} objects
   when it is the last HWRTData within a corresponding set of HWRTDatas.
*/
PVRSRV_ERROR RGXDestroyHWRTDataSet(RGX_KM_HW_RT_DATASET *psKMHWRTDataSet)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	PVRSRV_ERROR eError;
	PRGXFWIF_HWRTDATA psHWRTData;
	RGX_HWRTDATA_COMMON_COOKIE *psCommonCookie;

	PVR_ASSERT(psKMHWRTDataSet);

	psDevNode = psKMHWRTDataSet->psDeviceNode;

	eError = RGXSetFirmwareAddress(&psHWRTData,
	                               psKMHWRTDataSet->psHWRTDataFwMemDesc, 0,
	                               RFW_FWADDR_NOREF_FLAG);
	PVR_RETURN_IF_ERROR(eError);

	/* Cleanup HWRTData */
	eError = RGXFWRequestHWRTDataCleanUp(psDevNode, psHWRTData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	psCommonCookie = psKMHWRTDataSet->psHWRTDataCommonCookie;

	RGXDestroyHWRTData_aux(psKMHWRTDataSet);

	/* We've got past potential PVRSRV_ERROR_RETRY events, so we are sure
	   that the HWRTDATA instance will be destroyed during this call.
	   Consequently, we decrease the ref count for HWRTDataCommonCookie.

	   NOTE: This ref count does not require locks or atomics.
	   -------------------------------------------------------
	     HWRTDatas bound into one pair are always destroyed sequentially,
	     within a single loop on the Client side.
	     The Common/Cookie objects always belong to only one pair of
	     HWRTDatas, and ref count is used to ensure that the Common/Cookie
	     objects will be destroyed after destruction of all HWRTDatas
	     within a single pair.
	*/
	psCommonCookie->ui32RefCount--;

	/* When ref count for HWRTDataCommonCookie hits ZERO
	 * we have to destroy the HWRTDataCommon [FW object] and the cookie
	 * [KM object] afterwards. */
	if (psCommonCookie->ui32RefCount == 0)
	{
		RGXUnsetFirmwareAddress(psCommonCookie->psHWRTDataCommonFwMemDesc);

		/* We don't need to flush the SLC before freeing.
		 * FW RequestCleanUp has already done that for HWRTData, so we're fine
		 * now. */

		DevmemFwUnmapAndFree(psDevNode->pvDevice,
		                     psCommonCookie->psHWRTDataCommonFwMemDesc);
		OSFreeMem(psCommonCookie);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR RGXCreateFreeList(CONNECTION_DATA      *psConnection,
		PVRSRV_DEVICE_NODE	*psDeviceNode,
		IMG_HANDLE			hMemCtxPrivData,
		IMG_UINT32			ui32MaxFLPages,
		IMG_UINT32			ui32InitFLPages,
		IMG_UINT32			ui32GrowFLPages,
		IMG_UINT32           ui32GrowParamThreshold,
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

	if (OSGetPageShift() > RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT)
	{
		IMG_UINT32 ui32Size, ui32NewInitFLPages, ui32NewMaxFLPages, ui32NewGrowFLPages;

		/* Round up number of FL pages to the next multiple of the OS page size */

		ui32Size = ui32InitFLPages << RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;
		ui32Size = PVR_ALIGN(ui32Size, (IMG_DEVMEM_SIZE_T)OSGetPageSize());
		ui32NewInitFLPages = ui32Size >> RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;

		ui32Size = ui32GrowFLPages << RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;
		ui32Size = PVR_ALIGN(ui32Size, (IMG_DEVMEM_SIZE_T)OSGetPageSize());
		ui32NewGrowFLPages = ui32Size >> RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;

		ui32Size = ui32MaxFLPages << RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;
		ui32Size = PVR_ALIGN(ui32Size, (IMG_DEVMEM_SIZE_T)OSGetPageSize());
		ui32NewMaxFLPages = ui32Size >> RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;

		PVR_DPF((PVR_DBG_WARNING, "%s: Increased number of PB pages: Init %u -> %u, Grow %u -> %u, Max %u -> %u",
				 __func__, ui32InitFLPages, ui32NewInitFLPages, ui32GrowFLPages, ui32NewGrowFLPages, ui32MaxFLPages, ui32NewMaxFLPages));

		ui32InitFLPages = ui32NewInitFLPages;
		ui32GrowFLPages = ui32NewGrowFLPages;
		ui32MaxFLPages = ui32NewMaxFLPages;
	}

	/* Allocate kernel freelist struct */
	psFreeList = OSAllocZMem(sizeof(*psFreeList));
	if (psFreeList == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed to allocate host data structure",
				__func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocHost;
	}

	/*
	 * This FW FreeList context is only mapped into kernel for initialisation
	 * and reconstruction (at other times it is not mapped and only used by the
	 * FW).
	 * Therefore the GPU cache doesn't need coherency, and write-combine will
	 * suffice on the CPU side (WC buffer will be flushed at the first TA-kick)
	 */
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(*psFWFreeList),
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
			PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
			PVRSRV_MEMALLOCFLAG_GPU_READABLE |
			PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
			PVRSRV_MEMALLOCFLAG_CPU_READABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC |
			PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
			PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
			"FwFreeList",
			&psFWFreelistMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: DevmemAllocate for RGXFWIF_FREELIST failed",
				__func__));
		goto FWFreeListAlloc;
	}

	/* Initialise host data structures */
	psFreeList->psDevInfo = psDevInfo;
	psFreeList->psConnection = psConnection;
	psFreeList->psFreeListPMR = psFreeListPMR;
	psFreeList->uiFreeListPMROffset = uiFreeListPMROffset;
	psFreeList->psFWFreelistMemDesc = psFWFreelistMemDesc;
	eError = RGXSetFirmwareAddress(&psFreeList->sFreeListFWDevVAddr, psFWFreelistMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", ErrorSetFwAddr);

	/* psFreeList->ui32FreelistID set below with lock... */
	psFreeList->ui32FreelistGlobalID = (psGlobalFreeList ? psGlobalFreeList->ui32FreelistID : 0);
	psFreeList->ui32MaxFLPages = ui32MaxFLPages;
	psFreeList->ui32InitFLPages = ui32InitFLPages;
	psFreeList->ui32GrowFLPages = ui32GrowFLPages;
	psFreeList->ui32CurrentFLPages = 0;
	psFreeList->ui32ReadyFLPages = 0;
	psFreeList->ui32GrowThreshold = ui32GrowParamThreshold;
	psFreeList->ui64FreelistChecksum = 0;
	psFreeList->ui32RefCount = 0;
	psFreeList->bCheckFreelist = bCheckFreelist;
	dllist_init(&psFreeList->sMemoryBlockHead);
	dllist_init(&psFreeList->sMemoryBlockInitHead);
#if !defined(SUPPORT_SHADOW_FREELISTS)
	dllist_init(&psFreeList->sNodeHWRTDataHead);
#endif
	psFreeList->ownerPid = OSGetCurrentClientProcessIDKM();


	/* Add to list of freelists */
	OSLockAcquire(psDevInfo->hLockFreeList);
	psFreeList->ui32FreelistID = psDevInfo->ui32FreelistCurrID++;
	dllist_add_to_tail(&psDevInfo->sFreeListHead, &psFreeList->sNode);
	OSLockRelease(psDevInfo->hLockFreeList);


	/* Initialise FW data structure */
	eError = DevmemAcquireCpuVirtAddr(psFreeList->psFWFreelistMemDesc, (void **)&psFWFreeList);
	PVR_LOG_GOTO_IF_ERROR(eError, "Devmem AcquireCpuVirtAddr", FWFreeListCpuMap);

	{
		const IMG_UINT32 ui32ReadyPages = _CalculateFreelistReadyPages(psFreeList, ui32InitFLPages);

		psFWFreeList->ui32MaxPages = ui32MaxFLPages;
		psFWFreeList->ui32CurrentPages = ui32InitFLPages - ui32ReadyPages;
		psFWFreeList->ui32GrowPages = ui32GrowFLPages;
		psFWFreeList->ui32CurrentStackTop = psFWFreeList->ui32CurrentPages - 1;
		psFWFreeList->psFreeListDevVAddr = sFreeListDevVAddr;
		psFWFreeList->ui64CurrentDevVAddr = (sFreeListDevVAddr.uiAddr +
				((ui32MaxFLPages - psFWFreeList->ui32CurrentPages) * sizeof(IMG_UINT32))) &
						~((IMG_UINT64)RGX_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE-1);
		psFWFreeList->ui32FreeListID = psFreeList->ui32FreelistID;
		psFWFreeList->bGrowPending = IMG_FALSE;
		psFWFreeList->ui32ReadyPages = ui32ReadyPages;

#if defined(SUPPORT_SHADOW_FREELISTS)
		/* Get the FW Memory Context address... */
		eError = RGXSetFirmwareAddress(&psFWFreeList->psFWMemContext,
		                               RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData),
		                               0, RFW_FWADDR_NOREF_FLAG);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: RGXSetFirmwareAddress for RGXFWIF_FWMEMCONTEXT failed",
					__func__));
			DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);
			goto FWFreeListCpuMap;
		}
#else
		PVR_UNREFERENCED_PARAMETER(hMemCtxPrivData);
#endif
	}

	PVR_DPF((PVR_DBG_MESSAGE,
			"Freelist %p created: Max pages 0x%08x, Init pages 0x%08x, "
			"Max FL base address 0x%016" IMG_UINT64_FMTSPECx ", "
			"Init FL base address 0x%016" IMG_UINT64_FMTSPECx,
			psFreeList,
			ui32MaxFLPages,
			ui32InitFLPages,
			sFreeListDevVAddr.uiAddr,
			psFWFreeList->ui64CurrentDevVAddr));
#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode, "Dump FW FreeList");
	DevmemPDumpLoadMem(psFreeList->psFWFreelistMemDesc, 0, sizeof(*psFWFreeList), PDUMP_FLAGS_CONTINUOUS);

	/*
	 * Separate dump of the Freelist's number of Pages and stack pointer.
	 * This allows to easily modify the PB size in the out2.txt files.
	 */
	PDUMPCOMMENT(psDeviceNode, "FreeList TotalPages");
	DevmemPDumpLoadMemValue32(psFreeList->psFWFreelistMemDesc,
			offsetof(RGXFWIF_FREELIST, ui32CurrentPages),
			psFWFreeList->ui32CurrentPages,
			PDUMP_FLAGS_CONTINUOUS);
	PDUMPCOMMENT(psDeviceNode, "FreeList StackPointer");
	DevmemPDumpLoadMemValue32(psFreeList->psFWFreelistMemDesc,
			offsetof(RGXFWIF_FREELIST, ui32CurrentStackTop),
			psFWFreeList->ui32CurrentStackTop,
			PDUMP_FLAGS_CONTINUOUS);
#endif

	DevmemReleaseCpuVirtAddr(psFreeList->psFWFreelistMemDesc);


	/* Add initial PB block */
	eError = RGXGrowFreeList(psFreeList,
			ui32InitFLPages,
			&psFreeList->sMemoryBlockInitHead);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed to allocate initial memory block for free list 0x%016" IMG_UINT64_FMTSPECx " (%d)",
				__func__,
				sFreeListDevVAddr.uiAddr,
				eError));
		goto FWFreeListCpuMap;
	}
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	/* Update Stats */
	PVRSRVStatsUpdateFreelistStats(psDeviceNode,
			1, /* Add 1 to the appropriate counter (Requests by App)*/
			0,
			psFreeList->ui32InitFLPages,
			psFreeList->ui32NumHighPages,
			psFreeList->ownerPid);

#endif

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

ErrorSetFwAddr:
	DevmemFwUnmapAndFree(psDevInfo, psFWFreelistMemDesc);

FWFreeListAlloc:
	OSFreeMem(psFreeList);

ErrorAllocHost:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyFreeList
 */
PVRSRV_ERROR RGXDestroyFreeList(RGX_FREELIST *psFreeList)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32RefCount;

	PVR_ASSERT(psFreeList);

	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);
	ui32RefCount = psFreeList->ui32RefCount;
	OSLockRelease(psFreeList->psDevInfo->hLockFreeList);

	if (ui32RefCount != 0)
	{
		/* Freelist still busy */
		return PVRSRV_ERROR_RETRY;
	}

	/* Freelist is not in use => start firmware cleanup */
	eError = RGXFWRequestFreeListCleanUp(psFreeList->psDevInfo,
			psFreeList->sFreeListFWDevVAddr);
	if (eError != PVRSRV_OK)
	{
		/* Can happen if the firmware took too long to handle the cleanup request,
		 * or if SLC-flushes didn't went through (due to some GPU lockup) */
		return eError;
	}

	/* Remove FreeList from linked list before we destroy it... */
	OSLockAcquire(psFreeList->psDevInfo->hLockFreeList);
	dllist_remove_node(&psFreeList->sNode);
#if !defined(SUPPORT_SHADOW_FREELISTS)
	/* Confirm all HWRTData nodes are freed before releasing freelist */
	PVR_ASSERT(dllist_is_empty(&psFreeList->sNodeHWRTDataHead));
#endif
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
			_CheckFreelist(psFreeList, psFreeList->ui32CurrentFLPages + psFreeList->ui32ReadyFLPages, psFreeList->ui64FreelistChecksum, &ui64CheckSum);
		}
		else
		{
			/* Check for duplicate pages, but don't check the checksum as the list is not fully populated */
			_CheckFreelist(psFreeList, ui32CurrentStackTop+1, 0, &ui64CheckSum);
		}
	}

	/* Destroy FW structures */
	RGXUnsetFirmwareAddress(psFreeList->psFWFreelistMemDesc);
	DevmemFwUnmapAndFree(psFreeList->psDevInfo, psFreeList->psFWFreelistMemDesc);

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

	/* free Freelist */
	OSFreeMem(psFreeList);

	return eError;
}


/*
	RGXCreateZSBuffer
 */
PVRSRV_ERROR RGXCreateZSBufferKM(CONNECTION_DATA * psConnection,
		PVRSRV_DEVICE_NODE	*psDeviceNode,
		DEVMEMINT_RESERVATION	*psReservation,
		PMR						*psPMR,
		PVRSRV_MEMALLOCFLAGS_T	uiMapFlags,
		RGX_ZSBUFFER_DATA **ppsZSBuffer)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_PRBUFFER			*psFWZSBuffer;
	RGX_ZSBUFFER_DATA			*psZSBuffer;
	DEVMEM_MEMDESC				*psFWZSBufferMemDesc;
	IMG_BOOL					bOnDemand = PVRSRV_CHECK_ON_DEMAND(uiMapFlags) ? IMG_TRUE : IMG_FALSE;

	/* Allocate host data structure */
	psZSBuffer = OSAllocZMem(sizeof(*psZSBuffer));
	if (psZSBuffer == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate cleanup data structure for ZS-Buffer",
				__func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorAllocCleanup;
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
		/* psZSBuffer->ui32ZSBufferID set below with lock... */
		psZSBuffer->psMapping = NULL;

		OSLockAcquire(psDevInfo->hLockZSBuffer);
		psZSBuffer->ui32ZSBufferID = psDevInfo->ui32ZSBufferCurrID++;
		dllist_add_to_tail(&psDevInfo->sZSBufferHead, &psZSBuffer->sNode);
		OSLockRelease(psDevInfo->hLockZSBuffer);
	}

	/* Allocate firmware memory for ZS-Buffer. */
	PDUMPCOMMENT(psDeviceNode, "Allocate firmware ZS-Buffer data structure");
	eError = DevmemFwAllocate(psDevInfo,
			sizeof(*psFWZSBuffer),
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
			PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(FIRMWARE_CACHED) |
			PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
			PVRSRV_MEMALLOCFLAG_GPU_READABLE |
			PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT |
			PVRSRV_MEMALLOCFLAG_CPU_READABLE |
			PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
			PVRSRV_MEMALLOCFLAG_CPU_UNCACHED_WC |
			PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
			PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN),
			"FwZSBuffer",
			&psFWZSBufferMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate firmware ZS-Buffer (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto ErrorAllocFWZSBuffer;
	}
	psZSBuffer->psFWZSBufferMemDesc = psFWZSBufferMemDesc;

	/* Temporarily map the firmware render context to the kernel. */
	eError = DevmemAcquireCpuVirtAddr(psFWZSBufferMemDesc,
			(void **)&psFWZSBuffer);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to map firmware ZS-Buffer (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto ErrorAcquireFWZSBuffer;
	}

	/* Populate FW ZS-Buffer data structure */
	psFWZSBuffer->bOnDemand = bOnDemand;
	psFWZSBuffer->eState = (bOnDemand) ? RGXFWIF_PRBUFFER_UNBACKED : RGXFWIF_PRBUFFER_BACKED;
	psFWZSBuffer->ui32BufferID = psZSBuffer->ui32ZSBufferID;

	/* Get firmware address of ZS-Buffer. */
	eError = RGXSetFirmwareAddress(&psZSBuffer->sZSBufferFWDevVAddr, psFWZSBufferMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXSetFirmwareAddress", ErrorSetFwAddr);

	/* Dump the ZS-Buffer and the memory content */
#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode, "Dump firmware ZS-Buffer");
	DevmemPDumpLoadMem(psFWZSBufferMemDesc, 0, sizeof(*psFWZSBuffer), PDUMP_FLAGS_CONTINUOUS);
#endif

	/* Release address acquired above. */
	DevmemReleaseCpuVirtAddr(psFWZSBufferMemDesc);


	/* define return value */
	*ppsZSBuffer = psZSBuffer;

	PVR_DPF((PVR_DBG_MESSAGE, "ZS-Buffer [%p] created (%s)",
			psZSBuffer,
			(bOnDemand) ? "On-Demand": "Up-front"));

	psZSBuffer->owner=OSGetCurrentClientProcessIDKM();

	return PVRSRV_OK;

	/* error handling */

ErrorSetFwAddr:
	DevmemReleaseCpuVirtAddr(psFWZSBufferMemDesc);
ErrorAcquireFWZSBuffer:
	DevmemFwUnmapAndFree(psDevInfo, psFWZSBufferMemDesc);

ErrorAllocFWZSBuffer:
	OSFreeMem(psZSBuffer);

ErrorAllocCleanup:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


/*
	RGXDestroyZSBuffer
 */
PVRSRV_ERROR RGXDestroyZSBufferKM(RGX_ZSBUFFER_DATA *psZSBuffer)
{
	POS_LOCK hLockZSBuffer;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psZSBuffer);
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	/* Request ZS Buffer cleanup */
	eError = RGXFWRequestZSBufferCleanUp(psZSBuffer->psDevInfo,
			psZSBuffer->sZSBufferFWDevVAddr);
	if (eError == PVRSRV_OK)
	{
		/* Free the firmware render context. */
		RGXUnsetFirmwareAddress(psZSBuffer->psFWZSBufferMemDesc);
		DevmemFwUnmapAndFree(psZSBuffer->psDevInfo, psZSBuffer->psFWZSBufferMemDesc);

		/* Remove Deferred Allocation from list */
		if (psZSBuffer->bOnDemand)
		{
			OSLockAcquire(hLockZSBuffer);
			PVR_ASSERT(dllist_node_is_in_list(&psZSBuffer->sNode));
			dllist_remove_node(&psZSBuffer->sNode);
			OSLockRelease(hLockZSBuffer);
		}

		PVR_ASSERT(psZSBuffer->ui32RefCount == 0);

		PVR_DPF((PVR_DBG_MESSAGE, "ZS-Buffer [%p] destroyed", psZSBuffer));

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

	PVR_DPF((PVR_DBG_MESSAGE,
			"ZS Buffer [%p, ID=0x%08x]: Physical backing requested",
			psZSBuffer,
			psZSBuffer->ui32ZSBufferID));
	hLockZSBuffer = psZSBuffer->psDevInfo->hLockZSBuffer;

	OSLockAcquire(hLockZSBuffer);

	if (psZSBuffer->ui32RefCount == 0)
	{
		IMG_HANDLE hDevmemHeap;

		PVR_ASSERT(psZSBuffer->psMapping == NULL);

		/* Get Heap */
		eError = DevmemServerGetHeapHandle(psZSBuffer->psReservation, &hDevmemHeap);
		PVR_ASSERT(psZSBuffer->psMapping == NULL);
		if (unlikely(hDevmemHeap == (IMG_HANDLE)NULL))
		{
			OSLockRelease(hLockZSBuffer);
			return PVRSRV_ERROR_INVALID_HEAP;
		}

		eError = DevmemIntMapPMR(hDevmemHeap,
				psZSBuffer->psReservation,
				psZSBuffer->psPMR,
				psZSBuffer->uiMapFlags,
				&psZSBuffer->psMapping);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"Unable populate ZS Buffer [%p, ID=0x%08x] (%s)",
					psZSBuffer,
					psZSBuffer->ui32ZSBufferID,
					PVRSRVGetErrorString(eError)));
			OSLockRelease(hLockZSBuffer);
			return eError;

		}
		PVR_DPF((PVR_DBG_MESSAGE, "ZS Buffer [%p, ID=0x%08x]: Physical backing acquired",
				psZSBuffer,
				psZSBuffer->ui32ZSBufferID));
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
	PVRSRVStatsUpdateZSBufferStats(psZSBuffer->psDevInfo->psDeviceNode,
								   1, 0, psZSBuffer->owner);
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

	PVR_DPF((PVR_DBG_MESSAGE,
			"ZS Buffer [%p, ID=0x%08x]: Physical backing removal requested",
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
				PVR_DPF((PVR_DBG_ERROR,
						"Unable to unpopulate ZS Buffer [%p, ID=0x%08x] (%s)",
						psZSBuffer,
						psZSBuffer->ui32ZSBufferID,
						PVRSRVGetErrorString(eError)));
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
			PVR_DPF((PVR_DBG_ERROR,
					"Populating ZS-Buffer (ID = 0x%08x) failed (%s)",
					ui32ZSBufferID,
					PVRSRVGetErrorString(eError)));
			bBackingDone = IMG_FALSE;
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_BACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.sZSBufferFWDevVAddr.ui32Addr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = bBackingDone;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
					RGXFWIF_DM_GEOM,
					&sTACCBCmd,
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
		PVRSRVStatsUpdateZSBufferStats(psDevInfo->psDeviceNode,
									   0, 1, psZSBuffer->owner);
#endif

	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,
				"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (Populate)",
				ui32ZSBufferID));
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
			PVR_DPF((PVR_DBG_ERROR,
					"UnPopulating ZS-Buffer (ID = 0x%08x) failed (%s)",
					ui32ZSBufferID,
					PVRSRVGetErrorString(eError)));
			PVR_ASSERT(IMG_FALSE);
		}

		/* send confirmation */
		sTACCBCmd.eCmdType = RGXFWIF_KCCB_CMD_ZSBUFFER_UNBACKING_UPDATE;
		sTACCBCmd.uCmdData.sZSBufferBackingData.sZSBufferFWDevVAddr.ui32Addr = psZSBuffer->sZSBufferFWDevVAddr.ui32Addr;
		sTACCBCmd.uCmdData.sZSBufferBackingData.bDone = IMG_TRUE;

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommand(psDevInfo,
					RGXFWIF_DM_GEOM,
					&sTACCBCmd,
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
		PVR_DPF((PVR_DBG_ERROR,
				"ZS Buffer Lookup for ZS Buffer ID 0x%08x failed (UnPopulate)",
				ui32ZSBufferID));
	}
}

static
PVRSRV_ERROR _CreateTAContext(CONNECTION_DATA *psConnection,
		PVRSRV_DEVICE_NODE *psDeviceNode,
		DEVMEM_MEMDESC *psAllocatedMemDesc,
		IMG_UINT32 ui32AllocatedOffset,
		DEVMEM_MEMDESC *psFWMemContextMemDesc,
		IMG_DEV_VIRTADDR sVDMCallStackAddr,
		IMG_UINT32 ui32CallStackDepth,
		IMG_INT32 i32Priority,
		IMG_UINT32 ui32MaxDeadlineMS,
		IMG_UINT64 ui64RobustnessAddress,
		RGX_COMMON_CONTEXT_INFO *psInfo,
		RGX_SERVER_RC_TA_DATA *psTAData,
		IMG_UINT32 ui32CCBAllocSizeLog2,
		IMG_UINT32 ui32CCBMaxAllocSizeLog2,
		IMG_UINT32 ui32ContextFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_TACTX_STATE *psContextState;
	IMG_UINT32 uiCoreIdx;
	PVRSRV_ERROR eError;
	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	 */
	PDUMPCOMMENT(psDeviceNode, "Allocate RGX firmware TA context suspend state");

	eError = DevmemFwAllocate(psDevInfo,
			sizeof(RGXFWIF_TACTX_STATE),
			RGX_FWCOMCTX_ALLOCFLAGS,
			"FwTAContextState",
			&psTAData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate firmware GPU context suspend state (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto fail_tacontextsuspendalloc;
	}

	eError = DevmemAcquireCpuVirtAddr(psTAData->psContextStateMemDesc,
			(void **)&psContextState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to map firmware render context state (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto fail_suspendcpuvirtacquire;
	}

	for (uiCoreIdx = 0; uiCoreIdx < RGX_NUM_GEOM_CORES; uiCoreIdx++)
	{
		psContextState->asGeomCore[uiCoreIdx].uTAReg_VDM_CALL_STACK_POINTER_Init =
			sVDMCallStackAddr.uiAddr + (uiCoreIdx * ui32CallStackDepth * sizeof(IMG_UINT64));
	}

	DevmemReleaseCpuVirtAddr(psTAData->psContextStateMemDesc);

	eError = FWCommonContextAllocate(psConnection,
			psDeviceNode,
			REQ_TYPE_TA,
			RGXFWIF_DM_GEOM,
			NULL,
			psAllocatedMemDesc,
			ui32AllocatedOffset,
			psFWMemContextMemDesc,
			psTAData->psContextStateMemDesc,
			ui32CCBAllocSizeLog2 ? ui32CCBAllocSizeLog2 : RGX_TA_CCB_SIZE_LOG2,
			ui32CCBMaxAllocSizeLog2 ? ui32CCBMaxAllocSizeLog2 : RGX_TA_CCB_MAX_SIZE_LOG2,
			ui32ContextFlags,
			i32Priority,
			ui32MaxDeadlineMS,
			ui64RobustnessAddress,
			psInfo,
			&psTAData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to init TA fw common context (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto fail_tacommoncontext;
	}

	/*
	 * Dump the FW 3D context suspend state buffer
	 */
#if defined(PDUMP)
	PDUMPCOMMENT(psDeviceNode, "Dump the TA context suspend state buffer");
	DevmemPDumpLoadMem(psTAData->psContextStateMemDesc,
			0,
			sizeof(RGXFWIF_TACTX_STATE),
			PDUMP_FLAGS_CONTINUOUS);
#endif

	psTAData->i32Priority = i32Priority;
	return PVRSRV_OK;

fail_tacommoncontext:
fail_suspendcpuvirtacquire:
	DevmemFwUnmapAndFree(psDevInfo, psTAData->psContextStateMemDesc);
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
		IMG_INT32 i32Priority,
		IMG_UINT32 ui32MaxDeadlineMS,
		IMG_UINT64 ui64RobustnessAddress,
		RGX_COMMON_CONTEXT_INFO *psInfo,
		RGX_SERVER_RC_3D_DATA *ps3DData,
		IMG_UINT32 ui32CCBAllocSizeLog2,
		IMG_UINT32 ui32CCBMaxAllocSizeLog2,
		IMG_UINT32 ui32ContextFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;
	IMG_UINT	uiNumISPStoreRegs;
	IMG_UINT	ui3DRegISPStateStoreSize = 0;

	/*
		Allocate device memory for the firmware GPU context suspend state.
		Note: the FW reads/writes the state to memory by accessing the GPU register interface.
	 */
	PDUMPCOMMENT(psDeviceNode, "Allocate RGX firmware 3D context suspend state");

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, XE_MEMORY_HIERARCHY))
	{
		uiNumISPStoreRegs = psDeviceNode->pfnGetDeviceFeatureValue(psDeviceNode,
				RGX_FEATURE_NUM_RASTER_PIPES_IDX);
		if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT))
		{
			uiNumISPStoreRegs *= (1U + psDeviceNode->pfnGetDeviceFeatureValue(psDeviceNode,
					RGX_FEATURE_XPU_MAX_SLAVES_IDX));
		}
	}
	else
	{
		uiNumISPStoreRegs = psDeviceNode->pfnGetDeviceFeatureValue(psDeviceNode,
				RGX_FEATURE_NUM_ISP_IPP_PIPES_IDX);
	}

	/* Size of the CS buffer */
	/* Calculate the size of the 3DCTX ISP state */
	ui3DRegISPStateStoreSize = sizeof(RGXFWIF_3DCTX_STATE) +
			uiNumISPStoreRegs * sizeof(((RGXFWIF_3DCTX_STATE *)0)->au3DReg_ISP_STORE[0]);

	eError = DevmemFwAllocate(psDevInfo,
			ui3DRegISPStateStoreSize,
			RGX_FWCOMCTX_ALLOCFLAGS,
			"Fw3DContextState",
			&ps3DData->psContextStateMemDesc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate firmware GPU context suspend state (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto fail_3dcontextsuspendalloc;
	}

	eError = FWCommonContextAllocate(psConnection,
			psDeviceNode,
			REQ_TYPE_3D,
			RGXFWIF_DM_3D,
			NULL,
			psAllocatedMemDesc,
			ui32AllocatedOffset,
			psFWMemContextMemDesc,
			ps3DData->psContextStateMemDesc,
			ui32CCBAllocSizeLog2 ? ui32CCBAllocSizeLog2 : RGX_3D_CCB_SIZE_LOG2,
			ui32CCBMaxAllocSizeLog2 ? ui32CCBMaxAllocSizeLog2 : RGX_3D_CCB_MAX_SIZE_LOG2,
			ui32ContextFlags,
			i32Priority,
			ui32MaxDeadlineMS,
			ui64RobustnessAddress,
			psInfo,
			&ps3DData->psServerCommonContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to init 3D fw common context (%s)",
				__func__,
				PVRSRVGetErrorString(eError)));
		goto fail_3dcommoncontext;
	}

	/*
	 * Dump the FW 3D context suspend state buffer
	 */
	PDUMPCOMMENT(psDeviceNode, "Dump the 3D context suspend state buffer");
	DevmemPDumpLoadMem(ps3DData->psContextStateMemDesc,
			0,
			sizeof(RGXFWIF_3DCTX_STATE),
			PDUMP_FLAGS_CONTINUOUS);

	ps3DData->i32Priority = i32Priority;
	return PVRSRV_OK;

fail_3dcommoncontext:
	DevmemFwUnmapAndFree(psDevInfo, ps3DData->psContextStateMemDesc);
fail_3dcontextsuspendalloc:
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}


/*
 * PVRSRVRGXCreateRenderContextKM
 */
PVRSRV_ERROR PVRSRVRGXCreateRenderContextKM(CONNECTION_DATA				*psConnection,
		PVRSRV_DEVICE_NODE			*psDeviceNode,
		IMG_INT32					i32Priority,
		IMG_DEV_VIRTADDR			sVDMCallStackAddr,
		IMG_UINT32					ui32CallStackDepth,
		IMG_UINT32					ui32FrameworkRegisterSize,
		IMG_PBYTE					pabyFrameworkRegisters,
		IMG_HANDLE					hMemCtxPrivData,
		IMG_UINT32					ui32StaticRenderContextStateSize,
		IMG_PBYTE					pStaticRenderContextState,
		IMG_UINT32					ui32PackedCCBSizeU8888,
		IMG_UINT32					ui32ContextFlags,
		IMG_UINT64					ui64RobustnessAddress,
		IMG_UINT32					ui32MaxTADeadlineMS,
		IMG_UINT32					ui32Max3DDeadlineMS,
		RGX_SERVER_RENDER_CONTEXT	**ppsRenderContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO			*psDevInfo = psDeviceNode->pvDevice;
	RGX_SERVER_RENDER_CONTEXT	*psRenderContext;
	DEVMEM_MEMDESC				*psFWMemContextMemDesc = RGXGetFWMemDescFromMemoryContextHandle(hMemCtxPrivData);
	RGX_COMMON_CONTEXT_INFO		sInfo = {NULL};
	RGXFWIF_FWRENDERCONTEXT		*psFWRenderContext;

	*ppsRenderContext = NULL;

	if (ui32StaticRenderContextStateSize > RGXFWIF_STATIC_RENDERCONTEXT_SIZE)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psRenderContext = OSAllocZMem(sizeof(*psRenderContext));
	if (psRenderContext == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = OSLockCreate(&psRenderContext->hLock);

	if (eError != PVRSRV_OK)
	{
		goto fail_lock;
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
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		WorkEstInitTA3D(psDevInfo, &psRenderContext->sWorkEstData);
	}
#endif

	if (ui32FrameworkRegisterSize)
	{
		/*
		 * Create the FW framework buffer
		 */
		eError = PVRSRVRGXFrameworkCreateKM(psDeviceNode,
				&psRenderContext->psFWFrameworkMemDesc,
				ui32FrameworkRegisterSize);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to allocate firmware GPU framework state (%s)",
						__func__,
						PVRSRVGetErrorString(eError)));
			goto fail_frameworkcreate;
		}

		/* Copy the Framework client data into the framework buffer */
		eError = PVRSRVRGXFrameworkCopyCommand(psDeviceNode,
				psRenderContext->psFWFrameworkMemDesc,
				pabyFrameworkRegisters,
				ui32FrameworkRegisterSize);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
						"%s: Failed to populate the framework buffer (%s)",
						__func__,
						PVRSRVGetErrorString(eError)));
			goto fail_frameworkcopy;
		}

		sInfo.psFWFrameworkMemDesc = psRenderContext->psFWFrameworkMemDesc;
	}

	eError = _Create3DContext(psConnection,
			psDeviceNode,
			psRenderContext->psFWRenderContextMemDesc,
			offsetof(RGXFWIF_FWRENDERCONTEXT, s3DContext),
			psFWMemContextMemDesc,
			i32Priority,
			ui32Max3DDeadlineMS,
			ui64RobustnessAddress,
			&sInfo,
			&psRenderContext->s3DData,
			U32toU8_Unpack3(ui32PackedCCBSizeU8888),
			U32toU8_Unpack4(ui32PackedCCBSizeU8888),
			ui32ContextFlags);
	if (eError != PVRSRV_OK)
	{
		goto fail_3dcontext;
	}

	eError = _CreateTAContext(psConnection,
			psDeviceNode,
			psRenderContext->psFWRenderContextMemDesc,
			offsetof(RGXFWIF_FWRENDERCONTEXT, sTAContext),
			psFWMemContextMemDesc,
			sVDMCallStackAddr,
			ui32CallStackDepth,
			i32Priority,
			ui32MaxTADeadlineMS,
			ui64RobustnessAddress,
			&sInfo,
			&psRenderContext->sTAData,
			U32toU8_Unpack1(ui32PackedCCBSizeU8888),
			U32toU8_Unpack2(ui32PackedCCBSizeU8888),
			ui32ContextFlags);
	if (eError != PVRSRV_OK)
	{
		goto fail_tacontext;
	}

	eError = DevmemAcquireCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc,
			(void **)&psFWRenderContext);
	if (eError != PVRSRV_OK)
	{
		goto fail_acquire_cpu_mapping;
	}

	/* Copy the static render context data */
	OSDeviceMemCopy(&psFWRenderContext->sStaticRenderContextState, pStaticRenderContextState, ui32StaticRenderContextStateSize);
#if defined(SUPPORT_TRP)
	psFWRenderContext->eTRPGeomCoreAffinity = RGXFWIF_DM_MAX;
#endif
	DevmemPDumpLoadMem(psRenderContext->psFWRenderContextMemDesc, 0, sizeof(RGXFWIF_FWRENDERCONTEXT), PDUMP_FLAGS_CONTINUOUS);
	DevmemReleaseCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc);

#if defined(SUPPORT_BUFFER_SYNC)
	psRenderContext->psBufferSyncContext =
			pvr_buffer_sync_context_create(psDeviceNode->psDevConfig->pvOSDevice,
					"rogue-ta3d");
	if (IS_ERR(psRenderContext->psBufferSyncContext))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: failed to create buffer_sync context (err=%ld)",
				__func__, PTR_ERR(psRenderContext->psBufferSyncContext)));

		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_buffer_sync_context_create;
	}
#endif

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

	*ppsRenderContext = psRenderContext;
	return PVRSRV_OK;

#if defined(SUPPORT_BUFFER_SYNC)
fail_buffer_sync_context_create:
#endif
fail_acquire_cpu_mapping:
	_DestroyTAContext(&psRenderContext->sTAData,
			psDeviceNode);
fail_tacontext:
	_Destroy3DContext(&psRenderContext->s3DData,
			psRenderContext->psDeviceNode);
fail_3dcontext:
fail_frameworkcopy:
	if (psRenderContext->psFWFrameworkMemDesc)
	{
		DevmemFwUnmapAndFree(psDevInfo, psRenderContext->psFWFrameworkMemDesc);
	}
fail_frameworkcreate:
	DevmemFwUnmapAndFree(psDevInfo, psRenderContext->psFWRenderContextMemDesc);
fail_fwrendercontext:
	OSLockDestroy(psRenderContext->hLock);
fail_lock:
	OSFreeMem(psRenderContext);
	PVR_ASSERT(eError != PVRSRV_OK);

	return eError;
}

/*
 * PVRSRVRGXDestroyRenderContextKM
 */
PVRSRV_ERROR PVRSRVRGXDestroyRenderContextKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext)
{
	PVRSRV_ERROR				eError;
	PVRSRV_RGXDEV_INFO	*psDevInfo = psRenderContext->psDeviceNode->pvDevice;

	/* remove node from list before calling destroy - as destroy, if successful
	 * will invalidate the node
	 * must be re-added if destroy fails
	 */
	OSWRLockAcquireWrite(psDevInfo->hRenderCtxListLock);
	dllist_remove_node(&(psRenderContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRenderCtxListLock);

#if defined(SUPPORT_BUFFER_SYNC)
	/* Check psBufferSyncContext has not been destroyed already (by a previous
	 * call to this function which then later returned PVRSRV_ERROR_RETRY)
	 */
	if (psRenderContext->psBufferSyncContext != NULL)
	{
		pvr_buffer_sync_context_destroy(psRenderContext->psBufferSyncContext);
		psRenderContext->psBufferSyncContext = NULL;
	}
#endif

	/* Cleanup the TA if we haven't already */
	if ((psRenderContext->ui32CleanupStatus & RC_CLEANUP_TA_COMPLETE) == 0)
	{
		eError = _DestroyTAContext(&psRenderContext->sTAData,
				psRenderContext->psDeviceNode);
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
				psRenderContext->psDeviceNode);
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
	if (!PVRSRV_VZ_MODE_IS(GUEST))
	{
		RGXFWIF_FWRENDERCONTEXT	*psFWRenderContext;
		IMG_UINT32 ui32WorkEstCCBSubmitted;

		eError = DevmemAcquireCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc,
				(void **)&psFWRenderContext);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Failed to map firmware render context (%s)",
					__func__,
					PVRSRVGetErrorString(eError)));
			goto e0;
		}

		ui32WorkEstCCBSubmitted = psFWRenderContext->ui32WorkEstCCBSubmitted;

		DevmemReleaseCpuVirtAddr(psRenderContext->psFWRenderContextMemDesc);

		/* Check if all of the workload estimation CCB commands for this workload are read */
		if (ui32WorkEstCCBSubmitted != psRenderContext->sWorkEstData.ui32WorkEstCCBReceived)
		{

			PVR_DPF((PVR_DBG_WARNING,
					"%s: WorkEst # cmds submitted (%u) and received (%u) mismatch",
					__func__, ui32WorkEstCCBSubmitted,
					psRenderContext->sWorkEstData.ui32WorkEstCCBReceived));

			eError = PVRSRV_ERROR_RETRY;
			goto e0;
		}
	}
#endif

	/*
		Only if both TA and 3D contexts have been cleaned up can we
		free the shared resources
	 */
	if (psRenderContext->ui32CleanupStatus == (RC_CLEANUP_3D_COMPLETE | RC_CLEANUP_TA_COMPLETE))
	{
		if (psRenderContext->psFWFrameworkMemDesc)
		{
			/* Free the framework buffer */
			DevmemFwUnmapAndFree(psDevInfo, psRenderContext->psFWFrameworkMemDesc);
		}

		/* Free the firmware render context */
		DevmemFwUnmapAndFree(psDevInfo, psRenderContext->psFWRenderContextMemDesc);

		SyncAddrListDeinit(&psRenderContext->sSyncAddrListTAFence);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrListTAUpdate);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrList3DFence);
		SyncAddrListDeinit(&psRenderContext->sSyncAddrList3DUpdate);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			WorkEstDeInitTA3D(psDevInfo, &psRenderContext->sWorkEstData);
		}
#endif

		OSLockDestroy(psRenderContext->hLock);

		OSFreeMem(psRenderContext);
	}

	return PVRSRV_OK;

e0:
	OSWRLockAcquireWrite(psDevInfo->hRenderCtxListLock);
	dllist_add_to_tail(&(psDevInfo->sRenderCtxtListHead), &(psRenderContext->sListNode));
	OSWRLockReleaseWrite(psDevInfo->hRenderCtxListLock);
	return eError;
}



#if (ENABLE_TA3D_UFO_DUMP == 1)
static void DumpUfoList(IMG_UINT32 ui32ClientTAFenceCount,
                        IMG_UINT32 ui32ClientTAUpdateCount,
                        IMG_UINT32 ui32Client3DFenceCount,
                        IMG_UINT32 ui32Client3DUpdateCount,
                        PRGXFWIF_UFO_ADDR *pauiClientTAFenceUFOAddress,
                        IMG_UINT32 *paui32ClientTAFenceValue,
                        PRGXFWIF_UFO_ADDR *pauiClientTAUpdateUFOAddress,
                        IMG_UINT32 *paui32ClientTAUpdateValue,
                        PRGXFWIF_UFO_ADDR *pauiClient3DFenceUFOAddress,
                        IMG_UINT32 *paui32Client3DFenceValue,
                        PRGXFWIF_UFO_ADDR *pauiClient3DUpdateUFOAddress,
                        IMG_UINT32 *paui32Client3DUpdateValue)
{
	IMG_UINT32 i;

	PVR_DPF((PVR_DBG_ERROR, "%s: ~~~ After populating sync prims ~~~",
			 __func__));

	/* Dump Fence syncs, Update syncs and PR Update syncs */
	PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d TA fence syncs:",
	        __func__, ui32ClientTAFenceCount));
	for (i = 0; i < ui32ClientTAFenceCount; i++)
	{
		if (BITMASK_HAS(pauiClientTAFenceUFOAddress->ui32Addr, 1))
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x,"
			        " CheckValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
			        __func__, i + 1, ui32ClientTAFenceCount,
			        (void *) pauiClientTAFenceUFOAddress,
			        pauiClientTAFenceUFOAddress->ui32Addr));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x, CheckValue=%d(0x%x)",
			        __func__, i + 1, ui32ClientTAFenceCount,
			        (void *) pauiClientTAFenceUFOAddress,
			        pauiClientTAFenceUFOAddress->ui32Addr,
			        *paui32ClientTAFenceValue,
			        *paui32ClientTAFenceValue));
			paui32ClientTAFenceValue++;
		}
		pauiClientTAFenceUFOAddress++;
	}

	PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d TA update syncs:",
			 __func__, ui32ClientTAUpdateCount));
	for (i = 0; i < ui32ClientTAUpdateCount; i++)
	{
		if (BITMASK_HAS(pauiClientTAUpdateUFOAddress->ui32Addr, 1))
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x,"
			        " UpdateValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
			        __func__, i + 1, ui32ClientTAUpdateCount,
			        (void *) pauiClientTAUpdateUFOAddress,
			        pauiClientTAUpdateUFOAddress->ui32Addr));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x, UpdateValue=%d(0x%x)",
			        __func__, i + 1, ui32ClientTAUpdateCount,
			        (void *) pauiClientTAUpdateUFOAddress,
			        pauiClientTAUpdateUFOAddress->ui32Addr,
			        *paui32ClientTAUpdateValue,
			        *paui32ClientTAUpdateValue));
			paui32ClientTAUpdateValue++;
		}
		pauiClientTAUpdateUFOAddress++;
	}

	PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d 3D fence syncs:",
			 __func__, ui32Client3DFenceCount));
	for (i = 0; i < ui32Client3DFenceCount; i++)
	{
		if (BITMASK_HAS(pauiClient3DFenceUFOAddress->ui32Addr, 1))
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x,"
			        " CheckValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
			        __func__, i + 1, ui32Client3DFenceCount,
			        (void *) pauiClient3DFenceUFOAddress,
			        pauiClient3DFenceUFOAddress->ui32Addr));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x, CheckValue=%d(0x%x)",
			        __func__, i + 1, ui32Client3DFenceCount,
			        (void *) pauiClient3DFenceUFOAddress,
			        pauiClient3DFenceUFOAddress->ui32Addr,
			        *paui32Client3DFenceValue,
			        *paui32Client3DFenceValue));
			paui32Client3DFenceValue++;
		}
		pauiClient3DFenceUFOAddress++;
	}

	PVR_DPF((PVR_DBG_ERROR, "%s: Prepared %d 3D update syncs:",
			 __func__, ui32Client3DUpdateCount));
	for (i = 0; i < ui32Client3DUpdateCount; i++)
	{
		if (BITMASK_HAS(pauiClient3DUpdateUFOAddress->ui32Addr, 1))
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x,"
			        " UpdateValue=PVRSRV_SYNC_CHECKPOINT_SIGNALLED",
			        __func__, i + 1, ui32Client3DUpdateCount,
			        (void *) pauiClient3DUpdateUFOAddress,
			        pauiClient3DUpdateUFOAddress->ui32Addr));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s:   %d/%d<%p>. FWAddr=0x%x, UpdateValue=%d(0x%x)",
			        __func__, i + 1, ui32Client3DUpdateCount,
			        (void *) pauiClient3DUpdateUFOAddress,
			        pauiClient3DUpdateUFOAddress->ui32Addr,
			        *paui32Client3DUpdateValue,
			        *paui32Client3DUpdateValue));
			paui32Client3DUpdateValue++;
		}
		pauiClient3DUpdateUFOAddress++;
	}
}
#endif /* (ENABLE_TA3D_UFO_DUMP == 1) */

/*
 * PVRSRVRGXKickTA3DKM
 */
PVRSRV_ERROR PVRSRVRGXKickTA3DKM(RGX_SERVER_RENDER_CONTEXT	*psRenderContext,
		IMG_UINT32					ui32ClientTAFenceCount,
		SYNC_PRIMITIVE_BLOCK		**apsClientTAFenceSyncPrimBlock,
		IMG_UINT32					*paui32ClientTAFenceSyncOffset,
		IMG_UINT32					*paui32ClientTAFenceValue,
		IMG_UINT32					ui32ClientTAUpdateCount,
		SYNC_PRIMITIVE_BLOCK		**apsClientTAUpdateSyncPrimBlock,
		IMG_UINT32					*paui32ClientTAUpdateSyncOffset,
		IMG_UINT32					*paui32ClientTAUpdateValue,
		IMG_UINT32					ui32Client3DUpdateCount,
		SYNC_PRIMITIVE_BLOCK		**apsClient3DUpdateSyncPrimBlock,
		IMG_UINT32					*paui32Client3DUpdateSyncOffset,
		IMG_UINT32					*paui32Client3DUpdateValue,
		SYNC_PRIMITIVE_BLOCK		*psPRFenceSyncPrimBlock,
		IMG_UINT32					ui32PRFenceSyncOffset,
		IMG_UINT32					ui32PRFenceValue,
		PVRSRV_FENCE				iCheckTAFence,
		PVRSRV_TIMELINE			iUpdateTATimeline,
		PVRSRV_FENCE				*piUpdateTAFence,
		IMG_CHAR					szFenceNameTA[PVRSRV_SYNC_NAME_LENGTH],
		PVRSRV_FENCE				iCheck3DFence,
		PVRSRV_TIMELINE			iUpdate3DTimeline,
		PVRSRV_FENCE				*piUpdate3DFence,
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
		RGX_ZSBUFFER_DATA		*psZSBuffer,
		RGX_ZSBUFFER_DATA		*psMSAAScratchBuffer,
		IMG_UINT32			ui32SyncPMRCount,
		IMG_UINT32			*paui32SyncPMRFlags,
		PMR				**ppsSyncPMRs,
		IMG_UINT32			ui32RenderTargetSize,
		IMG_UINT32			ui32NumberOfDrawCalls,
		IMG_UINT32			ui32NumberOfIndices,
		IMG_UINT32			ui32NumberOfMRTs,
		IMG_UINT64			ui64DeadlineInus)
{
	/* per-context helper structures */
	RGX_CCB_CMD_HELPER_DATA *pasTACmdHelperData = psRenderContext->asTACmdHelperData;
	RGX_CCB_CMD_HELPER_DATA *pas3DCmdHelperData = psRenderContext->as3DCmdHelperData;

	IMG_UINT32				ui32TACmdCount=0;
	IMG_UINT32				ui323DCmdCount=0;
	IMG_UINT32				ui32TACmdOffset=0;
	IMG_UINT32				ui323DCmdOffset=0;
	RGXFWIF_UFO				sPRUFO;
	IMG_UINT32				i;
	PVRSRV_ERROR			eError = PVRSRV_OK;
	PVRSRV_ERROR			eError2 = PVRSRV_OK;

	PVRSRV_RGXDEV_INFO      *psDevInfo = FWCommonContextGetRGXDevInfo(psRenderContext->s3DData.psServerCommonContext);
	IMG_UINT32              ui32IntJobRef = OSAtomicIncrement(&psDevInfo->iCCBSubmissionOrdinal);
	IMG_BOOL                bCCBStateOpen = IMG_FALSE;

	IMG_UINT32				ui32ClientPRUpdateCount = 0;
	PRGXFWIF_UFO_ADDR		*pauiClientPRUpdateUFOAddress = NULL;
	IMG_UINT32				*paui32ClientPRUpdateValue = NULL;

	PRGXFWIF_TIMESTAMP_ADDR pPreAddr;
	PRGXFWIF_TIMESTAMP_ADDR pPostAddr;
	PRGXFWIF_UFO_ADDR       pRMWUFOAddr;

	PRGXFWIF_UFO_ADDR		*pauiClientTAFenceUFOAddress = NULL;
	PRGXFWIF_UFO_ADDR		*pauiClientTAUpdateUFOAddress = NULL;
	PRGXFWIF_UFO_ADDR		*pauiClient3DFenceUFOAddress = NULL;
	PRGXFWIF_UFO_ADDR		*pauiClient3DUpdateUFOAddress = NULL;
	PRGXFWIF_UFO_ADDR		uiPRFenceUFOAddress;

	IMG_UINT64               uiCheckTAFenceUID = 0;
	IMG_UINT64               uiCheck3DFenceUID = 0;
	IMG_UINT64               uiUpdateTAFenceUID = 0;
	IMG_UINT64               uiUpdate3DFenceUID = 0;

	IMG_BOOL bUseCombined3DAnd3DPR = bKickPR && bKick3D && !pui83DPRDMCmd;

	RGXFWIF_KCCB_CMD_KICK_DATA	sTACmdKickData;
	RGXFWIF_KCCB_CMD_KICK_DATA	s3DCmdKickData;
	IMG_BOOL bUseSingleFWCommand = bKickTA && (bKickPR || bKick3D);

	IMG_UINT32 ui32TACmdSizeTmp = 0, ui323DCmdSizeTmp = 0;

	IMG_BOOL bTAFenceOnSyncCheckpointsOnly = IMG_FALSE;

	PVRSRV_FENCE	iUpdateTAFence = PVRSRV_NO_FENCE;
	PVRSRV_FENCE	iUpdate3DFence = PVRSRV_NO_FENCE;

	IMG_BOOL b3DFenceOnSyncCheckpointsOnly = IMG_FALSE;
	IMG_UINT32 ui32TAFenceTimelineUpdateValue = 0;
	IMG_UINT32 ui323DFenceTimelineUpdateValue = 0;

	/*
	 * Count of the number of TA and 3D update values (may differ from number of
	 * TA and 3D updates later, as sync checkpoints do not need to specify a value)
	 */
	IMG_UINT32 ui32ClientPRUpdateValueCount = 0;
	IMG_UINT32 ui32ClientTAUpdateValueCount = ui32ClientTAUpdateCount;
	IMG_UINT32 ui32Client3DUpdateValueCount = ui32Client3DUpdateCount;
	PSYNC_CHECKPOINT *apsFenceTASyncCheckpoints = NULL;				/*!< TA fence checkpoints */
	PSYNC_CHECKPOINT *apsFence3DSyncCheckpoints = NULL;				/*!< 3D fence checkpoints */
	IMG_UINT32 ui32FenceTASyncCheckpointCount = 0;
	IMG_UINT32 ui32Fence3DSyncCheckpointCount = 0;
	PSYNC_CHECKPOINT psUpdateTASyncCheckpoint = NULL;				/*!< TA update checkpoint (output) */
	PSYNC_CHECKPOINT psUpdate3DSyncCheckpoint = NULL;				/*!< 3D update checkpoint (output) */
	PVRSRV_CLIENT_SYNC_PRIM *psTAFenceTimelineUpdateSync = NULL;
	PVRSRV_CLIENT_SYNC_PRIM *ps3DFenceTimelineUpdateSync = NULL;
	void *pvTAUpdateFenceFinaliseData = NULL;
	void *pv3DUpdateFenceFinaliseData = NULL;

	RGX_SYNC_DATA sTASyncData = {NULL};		/*!< Contains internal update syncs for TA */
	RGX_SYNC_DATA s3DSyncData = {NULL};		/*!< Contains internal update syncs for 3D */

	IMG_BOOL bTestSLRAdd3DCheck = IMG_FALSE;
#if defined(SUPPORT_VALIDATION)
	PVRSRV_FENCE hTestSLRTmpFence = PVRSRV_NO_FENCE;
	PSYNC_CHECKPOINT psDummySyncCheckpoint = NULL;
#endif

#if defined(SUPPORT_BUFFER_SYNC)
	PSYNC_CHECKPOINT *apsBufferFenceSyncCheckpoints = NULL;
	IMG_UINT32 ui32BufferFenceSyncCheckpointCount = 0;
	PSYNC_CHECKPOINT psBufferUpdateSyncCheckpoint = NULL;
	struct pvr_buffer_sync_append_data *psBufferSyncData = NULL;
#endif /* defined(SUPPORT_BUFFER_SYNC) */

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	RGXFWIF_WORKEST_KICK_DATA sWorkloadKickDataTA = {0};
	RGXFWIF_WORKEST_KICK_DATA sWorkloadKickData3D = {0};
	IMG_UINT32 ui32TACommandOffset = 0;
	IMG_UINT32 ui323DCommandOffset = 0;
	IMG_UINT32 ui32TACmdHeaderOffset = 0;
	IMG_UINT32 ui323DCmdHeaderOffset = 0;
	IMG_UINT32 ui323DFullRenderCommandOffset = 0;
	IMG_UINT32 ui32TACmdOffsetWrapCheck = 0;
	IMG_UINT32 ui323DCmdOffsetWrapCheck = 0;
	RGX_WORKLOAD sWorkloadCharacteristics = {0};
#endif

	IMG_UINT32 ui32TAFenceCount, ui323DFenceCount;
	IMG_UINT32 ui32TAUpdateCount, ui323DUpdateCount;
	IMG_UINT32 ui32PRUpdateCount;

	IMG_PID uiCurrentProcess = OSGetCurrentClientProcessIDKM();

	IMG_UINT32 ui32Client3DFenceCount = 0;

	/* Ensure we haven't been given a null ptr to
	 * TA fence values if we have been told we
	 * have TA sync prim fences
	 */
	if (ui32ClientTAFenceCount > 0)
	{
		PVR_LOG_RETURN_IF_FALSE(paui32ClientTAFenceValue != NULL,
		                        "paui32ClientTAFenceValue NULL but "
		                        "ui32ClientTAFenceCount > 0",
		                        PVRSRV_ERROR_INVALID_PARAMS);
	}
	/* Ensure we haven't been given a null ptr to
	 * TA update values if we have been told we
	 * have TA updates
	 */
	if (ui32ClientTAUpdateCount > 0)
	{
		PVR_LOG_RETURN_IF_FALSE(paui32ClientTAUpdateValue != NULL,
		                        "paui32ClientTAUpdateValue NULL but "
		                        "ui32ClientTAUpdateCount > 0",
		                        PVRSRV_ERROR_INVALID_PARAMS);
	}
	/* Ensure we haven't been given a null ptr to
	 * 3D update values if we have been told we
	 * have 3D updates
	 */
	if (ui32Client3DUpdateCount > 0)
	{
		PVR_LOG_RETURN_IF_FALSE(paui32Client3DUpdateValue != NULL,
		                        "paui32Client3DUpdateValue NULL but "
		                        "ui32Client3DUpdateCount > 0",
		                        PVRSRV_ERROR_INVALID_PARAMS);
	}

	/* Write FW addresses into CMD SHARED BLOCKs */
	{
		CMDTA3D_SHARED *psGeomCmdShared = (CMDTA3D_SHARED *)pui8TADMCmd;
		CMDTA3D_SHARED *ps3DCmdShared = (CMDTA3D_SHARED *)pui83DDMCmd;
		CMDTA3D_SHARED *psPR3DCmdShared = (CMDTA3D_SHARED *)pui83DPRDMCmd;

		if (psKMHWRTDataSet == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "KMHWRTDataSet is a null-pointer"));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}

		/* Write FW address for TA CMD
		*/
		if (psGeomCmdShared != NULL)
		{
			psGeomCmdShared->sHWRTData = psKMHWRTDataSet->sHWRTDataFwAddr;

			if (psZSBuffer != NULL)
			{
				psGeomCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_ZSBUFFER] = psZSBuffer->sZSBufferFWDevVAddr;
			}
			if (psMSAAScratchBuffer != NULL)
			{
				psGeomCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_MSAABUFFER] = psMSAAScratchBuffer->sZSBufferFWDevVAddr;
			}
		}

		/* Write FW address for 3D CMD
		*/
		if (ps3DCmdShared != NULL)
		{
			ps3DCmdShared->sHWRTData = psKMHWRTDataSet->sHWRTDataFwAddr;

			if (psZSBuffer != NULL)
			{
				ps3DCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_ZSBUFFER] = psZSBuffer->sZSBufferFWDevVAddr;
			}
			if (psMSAAScratchBuffer != NULL)
			{
				ps3DCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_MSAABUFFER] = psMSAAScratchBuffer->sZSBufferFWDevVAddr;
			}
		}

		/* Write FW address for PR3D CMD
		*/
		if (psPR3DCmdShared != NULL)
		{
			psPR3DCmdShared->sHWRTData = psKMHWRTDataSet->sHWRTDataFwAddr;

			if (psZSBuffer != NULL)
			{
				psPR3DCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_ZSBUFFER] = psZSBuffer->sZSBufferFWDevVAddr;
			}
			if (psMSAAScratchBuffer != NULL)
			{
				psPR3DCmdShared->asPRBuffer[RGXFWIF_PRBUFFER_MSAABUFFER] = psMSAAScratchBuffer->sZSBufferFWDevVAddr;
			}
		}
	}

	if (unlikely(iUpdateTATimeline >= 0 && !piUpdateTAFence))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	if (unlikely(iUpdate3DTimeline >= 0 && !piUpdate3DFence))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: ui32ClientTAFenceCount=%d, ui32ClientTAUpdateCount=%d, "
			   "ui32Client3DFenceCount=%d, ui32Client3DUpdateCount=%d",
			   __func__,
			   ui32ClientTAFenceCount, ui32ClientTAUpdateCount,
			   ui32Client3DFenceCount, ui32Client3DUpdateCount));


	RGX_GetTimestampCmdHelper((PVRSRV_RGXDEV_INFO*) psRenderContext->psDeviceNode->pvDevice,
	                          &pPreAddr,
	                          &pPostAddr,
	                          &pRMWUFOAddr);

	/* Double-check we have a PR kick if there are client fences */
	if (unlikely(!bKickPR && ui32Client3DFenceCount != 0))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: 3D fence passed without a PR kick",
		        __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Ensure the string is null-terminated (Required for safety) */
	szFenceNameTA[PVRSRV_SYNC_NAME_LENGTH-1] = '\0';
	szFenceName3D[PVRSRV_SYNC_NAME_LENGTH-1] = '\0';

	OSLockAcquire(psRenderContext->hLock);

	ui32TAFenceCount = ui32ClientTAFenceCount;
	ui323DFenceCount = ui32Client3DFenceCount;
	ui32TAUpdateCount = ui32ClientTAUpdateCount;
	ui323DUpdateCount = ui32Client3DUpdateCount;
	ui32PRUpdateCount = ui32ClientPRUpdateCount;

#if defined(SUPPORT_BUFFER_SYNC)
	if (ui32SyncPMRCount)
	{
		int err;

		CHKPT_DBG((PVR_DBG_ERROR, "%s:   Calling"
		          " pvr_buffer_sync_resolve_and_create_fences", __func__));

		err = pvr_buffer_sync_resolve_and_create_fences(
		    psRenderContext->psBufferSyncContext,
		    psRenderContext->psDeviceNode->hSyncCheckpointContext,
		    ui32SyncPMRCount,
		    ppsSyncPMRs,
		    paui32SyncPMRFlags,
		    &ui32BufferFenceSyncCheckpointCount,
		    &apsBufferFenceSyncCheckpoints,
		    &psBufferUpdateSyncCheckpoint,
		    &psBufferSyncData
		);

		if (unlikely(err))
		{
			switch (err)
			{
				case -EINTR:
					eError = PVRSRV_ERROR_RETRY;
					break;
				case -ENOMEM:
					eError = PVRSRV_ERROR_OUT_OF_MEMORY;
					break;
				default:
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					break;
			}

			if (eError != PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s:   "
				        "pvr_buffer_sync_resolve_and_create_fences failed (%d)",
				        __func__, eError));
			}
			OSLockRelease(psRenderContext->hLock);

			return eError;
		}

#if !defined(SUPPORT_STRIP_RENDERING)
		if (bKickTA)
		{
			ui32TAFenceCount += ui32BufferFenceSyncCheckpointCount;
		}
		else
		{
			ui323DFenceCount += ui32BufferFenceSyncCheckpointCount;
		}
#else /* !defined(SUPPORT_STRIP_RENDERING) */
		ui323DFenceCount += ui32BufferFenceSyncCheckpointCount;

		PVR_UNREFERENCED_PARAMETER(bTAFenceOnSyncCheckpointsOnly);
#endif /* !defined(SUPPORT_STRIP_RENDERING) */

		if (psBufferUpdateSyncCheckpoint != NULL)
		{
			if (bKick3D)
			{
				ui323DUpdateCount++;
			}
			else
			{
				ui32PRUpdateCount++;
			}
		}
	}
#endif /* defined(SUPPORT_BUFFER_SYNC) */

#if !defined(UPDATE_FENCE_CHECKPOINT_COUNT) || UPDATE_FENCE_CHECKPOINT_COUNT != 1 && UPDATE_FENCE_CHECKPOINT_COUNT != 2
#error "Invalid value for UPDATE_FENCE_CHECKPOINT_COUNT. Must be either 1 or 2."
#endif /* !defined(UPDATE_FENCE_CHECKPOINT_COUNT) || UPDATE_FENCE_CHECKPOINT_COUNT != 1 && UPDATE_FENCE_CHECKPOINT_COUNT != 2 */

	if (iCheckTAFence != PVRSRV_NO_FENCE)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointResolveFence[TA]"
		          " (iCheckFence=%d),"
		          " psRenderContext->psDeviceNode->hSyncCheckpointContext=<%p>...",
		          __func__, iCheckTAFence,
		          (void *) psRenderContext->psDeviceNode->hSyncCheckpointContext));

		/* Resolve the sync checkpoints that make up the input fence */
		eError = SyncCheckpointResolveFence(
		    psRenderContext->psDeviceNode->hSyncCheckpointContext,
		    iCheckTAFence,
		    &ui32FenceTASyncCheckpointCount,
		    &apsFenceTASyncCheckpoints,
		    &uiCheckTAFenceUID,
		    ui32PDumpFlags);
		if (unlikely(eError != PVRSRV_OK))
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, returned ERROR (eError=%d)",
			          __func__, eError));
			goto fail_resolve_input_ta_fence;
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, fence %d contained %d "
		          "checkpoints (apsFenceSyncCheckpoints=<%p>)",
		          __func__, iCheckTAFence, ui32FenceTASyncCheckpointCount,
		          (void *) apsFenceTASyncCheckpoints));

#if defined(TA3D_CHECKPOINT_DEBUG)
		if (apsFenceTASyncCheckpoints)
		{
			_DebugSyncCheckpoints(__func__, "TA", apsFenceTASyncCheckpoints,
			                      ui32FenceTASyncCheckpointCount);
		}
#endif /* defined(TA3D_CHECKPOINT_DEBUG) */
	}

	if (iCheck3DFence != PVRSRV_NO_FENCE)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: calling SyncCheckpointResolveFence[3D]"
		          " (iCheckFence=%d), "
		          "psRenderContext->psDeviceNode->hSyncCheckpointContext=<%p>...",
		          __func__, iCheck3DFence,
		          (void*)psRenderContext->psDeviceNode->hSyncCheckpointContext));

		/* Resolve the sync checkpoints that make up the input fence */
		eError = SyncCheckpointResolveFence(
		    psRenderContext->psDeviceNode->hSyncCheckpointContext,
		    iCheck3DFence,
		    &ui32Fence3DSyncCheckpointCount,
		    &apsFence3DSyncCheckpoints,
		    &uiCheck3DFenceUID,
		    ui32PDumpFlags);
		if (unlikely(eError != PVRSRV_OK))
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, returned ERROR (eError=%d)",
			          __func__, eError));
			goto fail_resolve_input_3d_fence;
		}

		CHKPT_DBG((PVR_DBG_ERROR, "%s: ...done, fence %d contained %d "
		          "checkpoints (apsFenceSyncCheckpoints=<%p>)",
		          __func__, iCheck3DFence, ui32Fence3DSyncCheckpointCount,
		          (void*)apsFence3DSyncCheckpoints));

#if defined(TA3D_CHECKPOINT_DEBUG)
		if (apsFence3DSyncCheckpoints)
		{
			_DebugSyncCheckpoints(__func__, "3D", apsFence3DSyncCheckpoints,
			                      ui32Fence3DSyncCheckpointCount);
		}
#endif /* defined(TA3D_CHECKPOINT_DEBUG) */
	}

	if (iCheckTAFence >= 0 || iUpdateTATimeline >= 0 ||
	    iCheck3DFence >= 0 || iUpdate3DTimeline >= 0)
	{
		IMG_UINT32 i;

		if (bKickTA)
		{
			ui32TAFenceCount += ui32FenceTASyncCheckpointCount;

			for (i = 0; i < ui32Fence3DSyncCheckpointCount; i++)
			{
				if (SyncCheckpointGetCreator(apsFence3DSyncCheckpoints[i]) !=
				    uiCurrentProcess)
				{
					ui32TAFenceCount++;
				}
			}
		}

		if (bKick3D)
		{
			ui323DFenceCount += ui32Fence3DSyncCheckpointCount;
		}

		ui32TAUpdateCount += iUpdateTATimeline != PVRSRV_NO_TIMELINE ?
				UPDATE_FENCE_CHECKPOINT_COUNT : 0;
		ui323DUpdateCount += iUpdate3DTimeline != PVRSRV_NO_TIMELINE ?
				UPDATE_FENCE_CHECKPOINT_COUNT : 0;
		ui32PRUpdateCount += iUpdate3DTimeline != PVRSRV_NO_TIMELINE && !bKick3D ?
				UPDATE_FENCE_CHECKPOINT_COUNT : 0;
	}

#if defined(SUPPORT_VALIDATION)
	/* Check if TestingSLR is adding an extra sync checkpoint to the
	 * 3D fence check (which we won't signal)
	 */
	if ((psDevInfo->ui32TestSLRInterval > 0) &&
	    (--psDevInfo->ui32TestSLRCount == 0))
	{
		bTestSLRAdd3DCheck = IMG_TRUE;
		psDevInfo->ui32TestSLRCount = psDevInfo->ui32TestSLRInterval;
	}

	if ((bTestSLRAdd3DCheck) && (iUpdate3DTimeline != PVRSRV_NO_TIMELINE))
	{
		if (iUpdate3DTimeline == PVRSRV_NO_TIMELINE)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Would append additional SLR checkpoint "
			         "to 3D fence but no update 3D timeline provided", __func__));
		}
		else
		{
			SyncCheckpointAlloc(psRenderContext->psDeviceNode->hSyncCheckpointContext,
			                    iUpdate3DTimeline,
			                    hTestSLRTmpFence,
			                    "TestSLRCheck",
			                    &psDummySyncCheckpoint);
			PVR_DPF((PVR_DBG_WARNING, "%s: Appending additional SLR checkpoint to 3D fence "
			                          "checkpoints (psDummySyncCheckpoint=<%p>)",
			                          __func__, (void*)psDummySyncCheckpoint));
			SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DFence,
			                              1,
			                              &psDummySyncCheckpoint);
			if (!pauiClient3DFenceUFOAddress)
			{
				pauiClient3DFenceUFOAddress = psRenderContext->sSyncAddrList3DFence.pasFWAddrs;
			}

			if (ui32Client3DFenceCount == 0)
			{
				b3DFenceOnSyncCheckpointsOnly = IMG_TRUE;
			}
			ui323DFenceCount++;
		}
	}
#endif /* defined(SUPPORT_VALIDATION) */

	if (bKickTA)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   calling RGXCmdHelperInitCmdCCB(),"
		          " ui32ClientTAFenceCount=%d, ui32ClientTAUpdateCount=%d",
		          __func__, ui32TAFenceCount, ui32TAUpdateCount));

		RGXCmdHelperInitCmdCCB_CommandSize(
			psDevInfo,
		    0,
		    ui32TAFenceCount,
		    ui32TAUpdateCount,
		    ui32TACmdSize,
		    &pPreAddr,
		    (bKick3D ? NULL : &pPostAddr),
		    (bKick3D ? NULL : &pRMWUFOAddr),
		    pasTACmdHelperData
		);
	}

	if (bKickPR)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   calling RGXCmdHelperInitCmdCCB(),"
		          " ui32Client3DFenceCount=%d", __func__,
		          ui323DFenceCount));

		RGXCmdHelperInitCmdCCB_CommandSize(
			psDevInfo,
			0,
		    ui323DFenceCount,
		    0,
		    sizeof(sPRUFO),
			NULL,
			NULL,
			NULL,
		    &pas3DCmdHelperData[ui323DCmdCount++]
		);
	}

	if (bKickPR && !bUseCombined3DAnd3DPR)
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s:   calling RGXCmdHelperInitCmdCCB(),"
		          " ui32PRUpdateCount=%d", __func__,
		          ui32PRUpdateCount));

		RGXCmdHelperInitCmdCCB_CommandSize(
			psDevInfo,
		    0,
		    0,
		    ui32PRUpdateCount,
		    /* if the client has not provided a 3DPR command, the regular 3D
		     * command should be used instead */
		    pui83DPRDMCmd ? ui323DPRCmdSize : ui323DCmdSize,
			NULL,
			NULL,
			NULL,
		    &pas3DCmdHelperData[ui323DCmdCount++]
		);
	}

	if (bKick3D || bAbort)
	{
		if (!bKickTA)
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s:   calling RGXCmdHelperInitCmdCCB(),"
		          " ui32Client3DFenceCount=%d", __func__,
		          ui323DFenceCount));
		}

		RGXCmdHelperInitCmdCCB_CommandSize(
			psDevInfo,
			0,
			bKickTA ? 0 : ui323DFenceCount,
		    ui323DUpdateCount,
		    ui323DCmdSize,
			(bKickTA ? NULL : &pPreAddr),
			&pPostAddr,
			&pRMWUFOAddr,
		    &pas3DCmdHelperData[ui323DCmdCount++]
		);
	}

	if (bKickTA)
	{
		ui32TACmdSizeTmp = RGXCmdHelperGetCommandSize(1, pasTACmdHelperData);

		eError = RGXCheckSpaceCCB(
		    FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext),
		    ui32TACmdSizeTmp
		);
		if (eError != PVRSRV_OK)
		{
			goto err_not_enough_space;
		}
	}

	if (ui323DCmdCount > 0)
	{
		ui323DCmdSizeTmp = RGXCmdHelperGetCommandSize(ui323DCmdCount, pas3DCmdHelperData);

		eError = RGXCheckSpaceCCB(
		    FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext),
		    ui323DCmdSizeTmp
		);
		if (eError != PVRSRV_OK)
		{
			goto err_not_enough_space;
		}
	}

	/* need to reset the counter here */

	ui323DCmdCount = 0;

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: SyncAddrListPopulate(psRenderContext->sSyncAddrListTAFence, %d fences)...",
			   __func__, ui32ClientTAFenceCount));
	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrListTAFence,
			ui32ClientTAFenceCount,
			apsClientTAFenceSyncPrimBlock,
			paui32ClientTAFenceSyncOffset);
	if (unlikely(eError != PVRSRV_OK))
	{
		goto err_populate_sync_addr_list_ta_fence;
	}

	if (ui32ClientTAFenceCount)
	{
		pauiClientTAFenceUFOAddress = psRenderContext->sSyncAddrListTAFence.pasFWAddrs;
	}

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: pauiClientTAFenceUFOAddress=<%p> ",
			   __func__, (void*)pauiClientTAFenceUFOAddress));

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: SyncAddrListPopulate(psRenderContext->sSyncAddrListTAUpdate, %d updates)...",
			   __func__, ui32ClientTAUpdateCount));
	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrListTAUpdate,
			ui32ClientTAUpdateCount,
			apsClientTAUpdateSyncPrimBlock,
			paui32ClientTAUpdateSyncOffset);
	if (unlikely(eError != PVRSRV_OK))
	{
		goto err_populate_sync_addr_list_ta_update;
	}

	if (ui32ClientTAUpdateCount)
	{
		pauiClientTAUpdateUFOAddress = psRenderContext->sSyncAddrListTAUpdate.pasFWAddrs;
	}
	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: pauiClientTAUpdateUFOAddress=<%p> ",
			   __func__, (void*)pauiClientTAUpdateUFOAddress));

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: SyncAddrListPopulate(psRenderContext->sSyncAddrList3DFence, %d fences)...",
			   __func__, ui32Client3DFenceCount));
	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrList3DFence,
			ui32Client3DFenceCount,
			NULL,
			NULL);
	if (unlikely(eError != PVRSRV_OK))
	{
		goto err_populate_sync_addr_list_3d_fence;
	}

	if (ui32Client3DFenceCount)
	{
		pauiClient3DFenceUFOAddress = psRenderContext->sSyncAddrList3DFence.pasFWAddrs;
	}
	CHKPT_DBG((PVR_DBG_ERROR, "%s: pauiClient3DFenceUFOAddress=<%p> ",
			   __func__, (void*)pauiClient3DFenceUFOAddress));

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: SyncAddrListPopulate(psRenderContext->sSyncAddrList3DUpdate, %d updates)...",
			   __func__, ui32Client3DUpdateCount));
	eError = SyncAddrListPopulate(&psRenderContext->sSyncAddrList3DUpdate,
			ui32Client3DUpdateCount,
			apsClient3DUpdateSyncPrimBlock,
			paui32Client3DUpdateSyncOffset);
	if (unlikely(eError != PVRSRV_OK))
	{
		goto err_populate_sync_addr_list_3d_update;
	}

	if (ui32Client3DUpdateCount || (iUpdate3DTimeline != PVRSRV_NO_TIMELINE && piUpdate3DFence && bKick3D))
	{
		pauiClient3DUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;
	}
	CHKPT_DBG((PVR_DBG_ERROR, "%s: pauiClient3DUpdateUFOAddress=<%p> ",
			   __func__, (void*)pauiClient3DUpdateUFOAddress));

	eError = SyncPrimitiveBlockToFWAddr(psPRFenceSyncPrimBlock, ui32PRFenceSyncOffset, &uiPRFenceUFOAddress);

	if (unlikely(eError != PVRSRV_OK))
	{
		goto err_pr_fence_address;
	}

#if (ENABLE_TA3D_UFO_DUMP == 1)
	DumpUfoList(ui32ClientTAFenceCount, ui32ClientTAUpdateCount,
	            ui32Client3DFenceCount + (bTestSLRAdd3DCheck ? 1 : 0),
	            ui32Client3DUpdateCount,
	            pauiClientTAFenceUFOAddress, paui32ClientTAFenceValue,
	            pauiClientTAUpdateUFOAddress, paui32ClientTAUpdateValue,
	            pauiClient3DFenceUFOAddress, NULL,
	            pauiClient3DUpdateUFOAddress, paui32Client3DUpdateValue);
#endif /* (ENABLE_TA3D_UFO_DUMP == 1) */

	if (ui32SyncPMRCount)
	{
#if defined(SUPPORT_BUFFER_SYNC)
#if !defined(SUPPORT_STRIP_RENDERING)
		/* Append buffer sync fences to TA fences */
		if (ui32BufferFenceSyncCheckpointCount > 0 && bKickTA)
		{
			CHKPT_DBG((PVR_DBG_ERROR,
					   "%s:   Append %d buffer sync checkpoints to TA Fence "
					   "(&psRenderContext->sSyncAddrListTAFence=<%p>, "
					   "pauiClientTAFenceUFOAddress=<%p>)...",
					   __func__,
					   ui32BufferFenceSyncCheckpointCount,
					   (void*)&psRenderContext->sSyncAddrListTAFence ,
					   (void*)pauiClientTAFenceUFOAddress));
			SyncAddrListAppendAndDeRefCheckpoints(&psRenderContext->sSyncAddrListTAFence,
					ui32BufferFenceSyncCheckpointCount,
					apsBufferFenceSyncCheckpoints);
			if (!pauiClientTAFenceUFOAddress)
			{
				pauiClientTAFenceUFOAddress = psRenderContext->sSyncAddrListTAFence.pasFWAddrs;
			}
			if (ui32ClientTAFenceCount == 0)
			{
				bTAFenceOnSyncCheckpointsOnly = IMG_TRUE;
			}
			ui32ClientTAFenceCount += ui32BufferFenceSyncCheckpointCount;
		}
		else
#endif
		/* Append buffer sync fences to 3D fences */
		if (ui32BufferFenceSyncCheckpointCount > 0)
		{
			CHKPT_DBG((PVR_DBG_ERROR,
					   "%s:   Append %d buffer sync checkpoints to 3D Fence "
					   "(&psRenderContext->sSyncAddrList3DFence=<%p>, "
					   "pauiClient3DFenceUFOAddress=<%p>)...",
					   __func__,
					   ui32BufferFenceSyncCheckpointCount,
					   (void*)&psRenderContext->sSyncAddrList3DFence,
					   (void*)pauiClient3DFenceUFOAddress));
			SyncAddrListAppendAndDeRefCheckpoints(&psRenderContext->sSyncAddrList3DFence,
					ui32BufferFenceSyncCheckpointCount,
					apsBufferFenceSyncCheckpoints);
			if (!pauiClient3DFenceUFOAddress)
			{
				pauiClient3DFenceUFOAddress = psRenderContext->sSyncAddrList3DFence.pasFWAddrs;
			}
			if (ui32Client3DFenceCount == 0)
			{
				b3DFenceOnSyncCheckpointsOnly = IMG_TRUE;
			}
			ui32Client3DFenceCount += ui32BufferFenceSyncCheckpointCount;
		}

		if (psBufferUpdateSyncCheckpoint)
		{
			/* If we have a 3D kick append update to the 3D updates else append to the PR update */
			if (bKick3D)
			{
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s:   Append 1 buffer sync checkpoint<%p> to 3D Update"
						   " (&psRenderContext->sSyncAddrList3DUpdate=<%p>,"
						   " pauiClient3DUpdateUFOAddress=<%p>)...",
						   __func__,
						   (void*)psBufferUpdateSyncCheckpoint,
						   (void*)&psRenderContext->sSyncAddrList3DUpdate,
						   (void*)pauiClient3DUpdateUFOAddress));
				/* Append buffer sync update to 3D updates */
				SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DUpdate,
						1,
						&psBufferUpdateSyncCheckpoint);
				if (!pauiClient3DUpdateUFOAddress)
				{
					pauiClient3DUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;
				}
				ui32Client3DUpdateCount++;
			}
			else
			{
				CHKPT_DBG((PVR_DBG_ERROR,
				           "%s:   Append 1 buffer sync checkpoint<%p> to PR Update"
						   " (&psRenderContext->sSyncAddrList3DUpdate=<%p>,"
						   " pauiClientPRUpdateUFOAddress=<%p>)...",
				           __func__,
						   (void*)psBufferUpdateSyncCheckpoint,
						   (void*)&psRenderContext->sSyncAddrList3DUpdate,
						   (void*)pauiClientPRUpdateUFOAddress));
				/* Attach update to the 3D (used for PR) Updates */
				SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DUpdate,
						1,
						&psBufferUpdateSyncCheckpoint);
				if (!pauiClientPRUpdateUFOAddress)
				{
					pauiClientPRUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;
				}
				ui32ClientPRUpdateCount++;
			}
		}
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   (after buffer_sync) ui32ClientTAFenceCount=%d, "
				   "ui32ClientTAUpdateCount=%d, ui32Client3DFenceCount=%d, "
				   "ui32Client3DUpdateCount=%d, ui32ClientPRUpdateCount=%d,",
				   __func__, ui32ClientTAFenceCount, ui32ClientTAUpdateCount,
				   ui32Client3DFenceCount, ui32Client3DUpdateCount,
				   ui32ClientPRUpdateCount));

#else /* defined(SUPPORT_BUFFER_SYNC) */
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Buffer sync not supported but got %u buffers",
				 __func__, ui32SyncPMRCount));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err_no_buffer_sync_invalid_params;
#endif /* defined(SUPPORT_BUFFER_SYNC) */
	}

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
	if (iCheckTAFence >= 0 || iUpdateTATimeline >= 0 ||
	    iCheck3DFence >= 0 || iUpdate3DTimeline >= 0)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s: [TA] iCheckFence = %d, iUpdateTimeline = %d",
				   __func__, iCheckTAFence, iUpdateTATimeline));
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s: [3D] iCheckFence = %d, iUpdateTimeline = %d",
				   __func__, iCheck3DFence, iUpdate3DTimeline));

		{
			/* Create the output fence for TA (if required) */
			if (iUpdateTATimeline != PVRSRV_NO_TIMELINE)
			{
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s: calling SyncCheckpointCreateFence[TA] "
						   "(iUpdateFence=%d, iUpdateTimeline=%d, "
						   "psRenderContext->psDeviceNode->hSyncCheckpointContext=<%p>)",
						   __func__, iUpdateTAFence, iUpdateTATimeline,
						   (void*)psRenderContext->psDeviceNode->hSyncCheckpointContext));
				eError = SyncCheckpointCreateFence(psRenderContext->psDeviceNode,
						szFenceNameTA,
						iUpdateTATimeline,
						psRenderContext->psDeviceNode->hSyncCheckpointContext,
						&iUpdateTAFence,
						&uiUpdateTAFenceUID,
						&pvTAUpdateFenceFinaliseData,
						&psUpdateTASyncCheckpoint,
						(void*)&psTAFenceTimelineUpdateSync,
						&ui32TAFenceTimelineUpdateValue,
						ui32PDumpFlags);
				if (unlikely(eError != PVRSRV_OK))
				{
					PVR_DPF((PVR_DBG_ERROR,
							"%s:   SyncCheckpointCreateFence[TA] failed (%s)",
							__func__,
							PVRSRVGetErrorString(eError)));
					goto fail_create_ta_fence;
				}

				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s: returned from SyncCheckpointCreateFence[TA] "
						   "(iUpdateFence=%d, psFenceTimelineUpdateSync=<%p>, "
						   "ui32FenceTimelineUpdateValue=0x%x)",
						   __func__, iUpdateTAFence,
						   (void*)psTAFenceTimelineUpdateSync,
						   ui32TAFenceTimelineUpdateValue));

#if defined(TA3D_CHECKPOINT_DEBUG)
				{
					PRGXFWIF_UFO_ADDR *pauiClientTAIntUpdateUFOAddress = NULL;

					/* Store the FW address of the update sync checkpoint in pauiClientTAIntUpdateUFOAddress */
					pauiClientTAIntUpdateUFOAddress = SyncCheckpointGetRGXFWIFUFOAddr(psUpdateTASyncCheckpoint);
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s: pauiClientIntUpdateUFOAddress[TA]->ui32Addr=0x%x",
							   __func__, pauiClientTAIntUpdateUFOAddress->ui32Addr));
				}
#endif
			}

			/* Append the sync prim update for the TA timeline (if required) */
			if (psTAFenceTimelineUpdateSync)
			{
				sTASyncData.ui32ClientUpdateCount		 = ui32ClientTAUpdateCount;
				sTASyncData.ui32ClientUpdateValueCount	 = ui32ClientTAUpdateValueCount;
				sTASyncData.ui32ClientPRUpdateValueCount = (bKick3D) ? 0 : ui32ClientPRUpdateValueCount;
				sTASyncData.paui32ClientUpdateValue		 = paui32ClientTAUpdateValue;

				eError = RGXSyncAppendTimelineUpdate(ui32TAFenceTimelineUpdateValue,
						&psRenderContext->sSyncAddrListTAUpdate,
						(bKick3D) ? NULL : &psRenderContext->sSyncAddrList3DUpdate,
								psTAFenceTimelineUpdateSync,
								&sTASyncData,
								bKick3D);
				if (unlikely(eError != PVRSRV_OK))
				{
					goto fail_alloc_update_values_mem_TA;
				}

				paui32ClientTAUpdateValue = sTASyncData.paui32ClientUpdateValue;
				ui32ClientTAUpdateValueCount = sTASyncData.ui32ClientUpdateValueCount;
				pauiClientTAUpdateUFOAddress = sTASyncData.pauiClientUpdateUFOAddress;
				ui32ClientTAUpdateCount = sTASyncData.ui32ClientUpdateCount;
			}

			/* Create the output fence for 3D (if required) */
			if (iUpdate3DTimeline != PVRSRV_NO_TIMELINE)
			{
				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s: calling SyncCheckpointCreateFence[3D] "
						   "(iUpdateFence=%d, iUpdateTimeline=%d, "
						   "psRenderContext->psDeviceNode->hSyncCheckpointContext=<%p>)",
						   __func__, iUpdate3DFence, iUpdate3DTimeline,
						   (void*)psRenderContext->psDeviceNode->hSyncCheckpointContext));
				eError = SyncCheckpointCreateFence(psRenderContext->psDeviceNode,
						szFenceName3D,
						iUpdate3DTimeline,
						psRenderContext->psDeviceNode->hSyncCheckpointContext,
						&iUpdate3DFence,
						&uiUpdate3DFenceUID,
						&pv3DUpdateFenceFinaliseData,
						&psUpdate3DSyncCheckpoint,
						(void*)&ps3DFenceTimelineUpdateSync,
						&ui323DFenceTimelineUpdateValue,
						ui32PDumpFlags);
				if (unlikely(eError != PVRSRV_OK))
				{
					PVR_DPF((PVR_DBG_ERROR,
							"%s:   SyncCheckpointCreateFence[3D] failed (%s)",
							__func__,
							PVRSRVGetErrorString(eError)));
					goto fail_create_3d_fence;
				}

				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s: returned from SyncCheckpointCreateFence[3D] "
						   "(iUpdateFence=%d, psFenceTimelineUpdateSync=<%p>, "
						   "ui32FenceTimelineUpdateValue=0x%x)",
						   __func__, iUpdate3DFence,
						   (void*)ps3DFenceTimelineUpdateSync,
						   ui323DFenceTimelineUpdateValue));

#if defined(TA3D_CHECKPOINT_DEBUG)
				{
					PRGXFWIF_UFO_ADDR *pauiClient3DIntUpdateUFOAddress = NULL;

					/* Store the FW address of the update sync checkpoint in pauiClient3DIntUpdateUFOAddress */
					pauiClient3DIntUpdateUFOAddress = SyncCheckpointGetRGXFWIFUFOAddr(psUpdate3DSyncCheckpoint);
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s: pauiClientIntUpdateUFOAddress[3D]->ui32Addr=0x%x",
							   __func__, pauiClient3DIntUpdateUFOAddress->ui32Addr));
				}
#endif
			}

			/* Append the sync prim update for the 3D timeline (if required) */
			if (ps3DFenceTimelineUpdateSync)
			{
				s3DSyncData.ui32ClientUpdateCount = ui32Client3DUpdateCount;
				s3DSyncData.ui32ClientUpdateValueCount = ui32Client3DUpdateValueCount;
				s3DSyncData.ui32ClientPRUpdateValueCount = ui32ClientPRUpdateValueCount;
				s3DSyncData.paui32ClientUpdateValue = paui32Client3DUpdateValue;

				eError = RGXSyncAppendTimelineUpdate(ui323DFenceTimelineUpdateValue,
						&psRenderContext->sSyncAddrList3DUpdate,
						&psRenderContext->sSyncAddrList3DUpdate,	/*!< PR update: is this required? */
						ps3DFenceTimelineUpdateSync,
						&s3DSyncData,
						bKick3D);
				if (unlikely(eError != PVRSRV_OK))
				{
					goto fail_alloc_update_values_mem_3D;
				}

				paui32Client3DUpdateValue = s3DSyncData.paui32ClientUpdateValue;
				ui32Client3DUpdateValueCount = s3DSyncData.ui32ClientUpdateValueCount;
				pauiClient3DUpdateUFOAddress = s3DSyncData.pauiClientUpdateUFOAddress;
				ui32Client3DUpdateCount = s3DSyncData.ui32ClientUpdateCount;

				if (!bKick3D)
				{
					paui32ClientPRUpdateValue = s3DSyncData.paui32ClientPRUpdateValue;
					ui32ClientPRUpdateValueCount = s3DSyncData.ui32ClientPRUpdateValueCount;
					pauiClientPRUpdateUFOAddress = s3DSyncData.pauiClientPRUpdateUFOAddress;
					ui32ClientPRUpdateCount = s3DSyncData.ui32ClientPRUpdateCount;
				}
			}

			/*
			 * The hardware requires a PR to be submitted if there is a TA OOM.
			 * If we only have a TA, attach native checks and updates to the TA
			 * and 3D updates to the PR.
			 * If we have a TA and 3D, attach the native TA checks and updates
			 * to the TA and similarly for the 3D.
			 * Note that 'updates' includes the cleanup syncs for 'check' fence
			 * FDs, in addition to the update fence FD (if supplied).
			 * Currently, the client driver never kicks only the 3D, so we don't
			 * support that for the time being.
			 */

			{
				if (bKickTA)
				{
					/* Attach checks and updates to TA */

					/* Checks (from input fence) */
					if (ui32FenceTASyncCheckpointCount > 0)
					{
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   Append %d sync checkpoints to TA Fence (apsFenceSyncCheckpoints=<%p>)...",
								   __func__,
								   ui32FenceTASyncCheckpointCount,
								   (void*)apsFenceTASyncCheckpoints));
						SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrListTAFence,
								ui32FenceTASyncCheckpointCount,
								apsFenceTASyncCheckpoints);
						if (!pauiClientTAFenceUFOAddress)
						{
							pauiClientTAFenceUFOAddress = psRenderContext->sSyncAddrListTAFence.pasFWAddrs;
						}
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   {ui32ClientTAFenceCount was %d, now %d}",
								   __func__, ui32ClientTAFenceCount,
								   ui32ClientTAFenceCount + ui32FenceTASyncCheckpointCount));
						if (ui32ClientTAFenceCount == 0)
						{
							bTAFenceOnSyncCheckpointsOnly = IMG_TRUE;
						}
						ui32ClientTAFenceCount += ui32FenceTASyncCheckpointCount;
					}
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s:   {ui32ClientTAFenceCount now %d}",
							   __func__, ui32ClientTAFenceCount));

					if (psUpdateTASyncCheckpoint)
					{
						/* Update (from output fence) */
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   Append 1 sync checkpoint<%p> (ID=%d) to TA Update...",
								   __func__, (void*)psUpdateTASyncCheckpoint,
								   SyncCheckpointGetId(psUpdateTASyncCheckpoint)));
						SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrListTAUpdate,
								1,
								&psUpdateTASyncCheckpoint);
						if (!pauiClientTAUpdateUFOAddress)
						{
							pauiClientTAUpdateUFOAddress = psRenderContext->sSyncAddrListTAUpdate.pasFWAddrs;
						}
						ui32ClientTAUpdateCount++;
					}

					if (!bKick3D && psUpdate3DSyncCheckpoint)
					{
						/* Attach update to the 3D (used for PR) Updates */
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   Append 1 sync checkpoint<%p> (ID=%d) to 3D(PR) Update...",
								   __func__, (void*)psUpdate3DSyncCheckpoint,
								   SyncCheckpointGetId(psUpdate3DSyncCheckpoint)));
						SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DUpdate,
								1,
								&psUpdate3DSyncCheckpoint);
						if (!pauiClientPRUpdateUFOAddress)
						{
							pauiClientPRUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;
						}
						ui32ClientPRUpdateCount++;
					}
				}

				if (bKick3D)
				{
					/* Attach checks and updates to the 3D */

					/* Checks (from input fence) */
					if (ui32Fence3DSyncCheckpointCount > 0)
					{
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   Append %d sync checkpoints to 3D Fence...",
								   __func__, ui32Fence3DSyncCheckpointCount));
						SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DFence,
								ui32Fence3DSyncCheckpointCount,
								apsFence3DSyncCheckpoints);
						if (!pauiClient3DFenceUFOAddress)
						{
							pauiClient3DFenceUFOAddress = psRenderContext->sSyncAddrList3DFence.pasFWAddrs;
						}
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   {ui32Client3DFenceCount was %d, now %d}",
								   __func__, ui32Client3DFenceCount,
								   ui32Client3DFenceCount + ui32Fence3DSyncCheckpointCount));
						if (ui32Client3DFenceCount == 0)
						{
							b3DFenceOnSyncCheckpointsOnly = IMG_TRUE;
						}
						ui32Client3DFenceCount += ui32Fence3DSyncCheckpointCount;
					}
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s:   {ui32Client3DFenceCount was %d}",
							   __func__, ui32Client3DFenceCount));

					if (psUpdate3DSyncCheckpoint)
					{
						/* Update (from output fence) */
						CHKPT_DBG((PVR_DBG_ERROR,
								   "%s:   Append 1 sync checkpoint<%p> (ID=%d) to 3D Update...",
								   __func__, (void*)psUpdate3DSyncCheckpoint,
								   SyncCheckpointGetId(psUpdate3DSyncCheckpoint)));
						SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrList3DUpdate,
								1,
								&psUpdate3DSyncCheckpoint);
						if (!pauiClient3DUpdateUFOAddress)
						{
							pauiClient3DUpdateUFOAddress = psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs;
						}
						ui32Client3DUpdateCount++;
					}
				}

				/*
				 * Relocate sync check points from the 3D fence that are
				 * external to the current process, to the TA fence.
				 * This avoids a sync lockup when dependent renders are
				 * submitted out-of-order and a PR must be scheduled.
				 */
				if (bKickTA)
				{
					/* Search for external timeline dependencies */
					CHKPT_DBG((PVR_DBG_ERROR,
							   "%s: Checking 3D fence for external sync points (%d)...",
							   __func__, ui32Fence3DSyncCheckpointCount));

					for (i=0; i<ui32Fence3DSyncCheckpointCount; i++)
					{
						/* Check to see if the checkpoint is on a TL owned by
						 * another process.
						 */
						if (SyncCheckpointGetCreator(apsFence3DSyncCheckpoints[i]) != uiCurrentProcess)
						{
							/* 3D Sync point represents cross process
							 * dependency, copy sync point to TA command fence. */
							CHKPT_DBG((PVR_DBG_ERROR,
									   "%s:   Append 1 sync checkpoint<%p> (ID=%d) to TA Fence...",
									   __func__, (void*)apsFence3DSyncCheckpoints[i],
									   SyncCheckpointGetId(apsFence3DSyncCheckpoints[i])));

							SyncAddrListAppendCheckpoints(&psRenderContext->sSyncAddrListTAFence,
														  1,
														  &apsFence3DSyncCheckpoints[i]);

							if (!pauiClientTAFenceUFOAddress)
							{
								pauiClientTAFenceUFOAddress = psRenderContext->sSyncAddrListTAFence.pasFWAddrs;
							}

							CHKPT_DBG((PVR_DBG_ERROR,
									   "%s:   {ui32ClientTAFenceCount was %d, now %d}",
									   __func__,
									   ui32ClientTAFenceCount,
									   ui32ClientTAFenceCount + 1));

							if (ui32ClientTAFenceCount == 0)
							{
								bTAFenceOnSyncCheckpointsOnly = IMG_TRUE;
							}

							ui32ClientTAFenceCount++;
						}
					}
				}

				CHKPT_DBG((PVR_DBG_ERROR,
						   "%s:   (after pvr_sync) ui32ClientTAFenceCount=%d, "
						   "ui32ClientTAUpdateCount=%d, ui32Client3DFenceCount=%d, "
						   "ui32Client3DUpdateCount=%d, ui32ClientPRUpdateCount=%d,",
						   __func__,
						   ui32ClientTAFenceCount, ui32ClientTAUpdateCount,
						   ui32Client3DFenceCount, ui32Client3DUpdateCount,
						   ui32ClientPRUpdateCount));
			}
		}

		if (ui32ClientTAFenceCount)
		{
			PVR_ASSERT(pauiClientTAFenceUFOAddress);
			if (!bTAFenceOnSyncCheckpointsOnly)
			{
				PVR_ASSERT(paui32ClientTAFenceValue);
			}
		}
		if (ui32ClientTAUpdateCount)
		{
			PVR_ASSERT(pauiClientTAUpdateUFOAddress);
			if (ui32ClientTAUpdateValueCount>0)
			{
				PVR_ASSERT(paui32ClientTAUpdateValue);
			}
		}
		if (ui32Client3DFenceCount)
		{
			PVR_ASSERT(pauiClient3DFenceUFOAddress);
			PVR_ASSERT(b3DFenceOnSyncCheckpointsOnly);
		}
		if (ui32Client3DUpdateCount)
		{
			PVR_ASSERT(pauiClient3DUpdateUFOAddress);
			if (ui32Client3DUpdateValueCount>0)
			{
				PVR_ASSERT(paui32Client3DUpdateValue);
			}
		}
		if (ui32ClientPRUpdateCount)
		{
			PVR_ASSERT(pauiClientPRUpdateUFOAddress);
			if (ui32ClientPRUpdateValueCount>0)
			{
				PVR_ASSERT(paui32ClientPRUpdateValue);
			}
		}

	}

	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: ui32ClientTAFenceCount=%d, pauiClientTAFenceUFOAddress=<%p> Line ",
			   __func__,
			   ui32ClientTAFenceCount,
			   (void*)paui32ClientTAFenceValue));
	CHKPT_DBG((PVR_DBG_ERROR,
			   "%s: ui32ClientTAUpdateCount=%d, pauiClientTAUpdateUFOAddress=<%p> Line ",
			   __func__,
			   ui32ClientTAUpdateCount,
			   (void*)pauiClientTAUpdateUFOAddress));
#if (ENABLE_TA3D_UFO_DUMP == 1)
	DumpUfoList(ui32ClientTAFenceCount, ui32ClientTAUpdateCount,
	            ui32Client3DFenceCount + (bTestSLRAdd3DCheck ? 1 : 0),
				ui32Client3DUpdateCount,
	            pauiClientTAFenceUFOAddress, paui32ClientTAFenceValue,
	            pauiClientTAUpdateUFOAddress, paui32ClientTAUpdateValue,
	            pauiClient3DFenceUFOAddress, NULL,
	            pauiClient3DUpdateUFOAddress, paui32Client3DUpdateValue);
#endif /* (ENABLE_TA3D_UFO_DUMP == 1) */

	/* Command size check */
	if (ui32TAFenceCount != ui32ClientTAFenceCount)
	{
		PVR_DPF((PVR_DBG_ERROR, "TA pre-calculated number of fences"
		        " is different than the actual number (%u != %u)",
		        ui32TAFenceCount, ui32ClientTAFenceCount));
	}
	if (ui32TAUpdateCount != ui32ClientTAUpdateCount)
	{
		PVR_DPF((PVR_DBG_ERROR, "TA pre-calculated number of updates"
		        " is different than the actual number (%u != %u)",
		        ui32TAUpdateCount, ui32ClientTAUpdateCount));
	}
	if (!bTestSLRAdd3DCheck && (ui323DFenceCount != ui32Client3DFenceCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "3D pre-calculated number of fences"
		        " is different than the actual number (%u != %u)",
		        ui323DFenceCount, ui32Client3DFenceCount));
	}
	if (ui323DUpdateCount != ui32Client3DUpdateCount)
	{
		PVR_DPF((PVR_DBG_ERROR, "3D pre-calculated number of updates"
		        " is different than the actual number (%u != %u)",
		        ui323DUpdateCount, ui32Client3DUpdateCount));
	}
	if (ui32PRUpdateCount != ui32ClientPRUpdateCount)
	{
		PVR_DPF((PVR_DBG_ERROR, "PR pre-calculated number of updates"
		        " is different than the actual number (%u != %u)",
		        ui32PRUpdateCount, ui32ClientPRUpdateCount));
	}

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
	if ((!PVRSRV_VZ_MODE_IS(GUEST)) && (bKickTA || bKick3D || bAbort))
	{
		sWorkloadCharacteristics.sTA3D.ui32RenderTargetSize  = ui32RenderTargetSize;
		sWorkloadCharacteristics.sTA3D.ui32NumberOfDrawCalls = ui32NumberOfDrawCalls;
		sWorkloadCharacteristics.sTA3D.ui32NumberOfIndices   = ui32NumberOfIndices;
		sWorkloadCharacteristics.sTA3D.ui32NumberOfMRTs      = ui32NumberOfMRTs;
	}
#endif

	/* Init and acquire to TA command if required */
	if (bKickTA)
	{
		RGX_SERVER_RC_TA_DATA *psTAData = &psRenderContext->sTAData;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* Prepare workload estimation */
			WorkEstPrepare(psRenderContext->psDeviceNode->pvDevice,
					&psRenderContext->sWorkEstData,
					&psRenderContext->sWorkEstData.uWorkloadMatchingData.sTA3D.sDataTA,
					RGXFWIF_CCB_CMD_TYPE_GEOM,
					&sWorkloadCharacteristics,
					ui64DeadlineInus,
					&sWorkloadKickDataTA);
		}
#endif

		/* Init the TA command helper */
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   calling RGXCmdHelperInitCmdCCB(), ui32ClientTAFenceCount=%d, ui32ClientTAUpdateCount=%d",
				   __func__, ui32ClientTAFenceCount, ui32ClientTAUpdateCount));
		RGXCmdHelperInitCmdCCB_OtherData(FWCommonContextGetClientCCB(psTAData->psServerCommonContext),
				ui32ClientTAFenceCount,
				pauiClientTAFenceUFOAddress,
				paui32ClientTAFenceValue,
				ui32ClientTAUpdateCount,
				pauiClientTAUpdateUFOAddress,
				paui32ClientTAUpdateValue,
				ui32TACmdSize,
				pui8TADMCmd,
		        &pPreAddr,
		        (bKick3D ? NULL : &pPostAddr),
		        (bKick3D ? NULL : &pRMWUFOAddr),
				RGXFWIF_CCB_CMD_TYPE_GEOM,
				ui32ExtJobRef,
				ui32IntJobRef,
				ui32PDumpFlags,
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
				&sWorkloadKickDataTA,
#else
				NULL,
#endif
				"TA",
				bCCBStateOpen,
				pasTACmdHelperData);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* The following is used to determine the offset of the command header containing
			   the workload estimation data so that can be accessed when the KCCB is read */
			ui32TACmdHeaderOffset = RGXCmdHelperGetDMCommandHeaderOffset(pasTACmdHelperData);
		}
#endif

		eError = RGXCmdHelperAcquireCmdCCB(CCB_CMD_HELPER_NUM_TA_COMMANDS, pasTACmdHelperData);
		if (unlikely(eError != PVRSRV_OK))
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line",
					   __func__, eError));
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
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   calling RGXCmdHelperInitCmdCCB(), ui32Client3DFenceCount=%d",
				   __func__, ui32Client3DFenceCount));
		RGXCmdHelperInitCmdCCB_OtherData(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
				ui32Client3DFenceCount + (bTestSLRAdd3DCheck ? 1 : 0),
				pauiClient3DFenceUFOAddress,
				NULL,
				0,
				NULL,
				NULL,
				sizeof(sPRUFO),
				(IMG_UINT8*) &sPRUFO,
				NULL,
				NULL,
				NULL,
				RGXFWIF_CCB_CMD_TYPE_FENCE_PR,
				ui32ExtJobRef,
				ui32IntJobRef,
				ui32PDumpFlags,
				NULL,
				"3D-PR-Fence",
				bCCBStateOpen,
				&pas3DCmdHelperData[ui323DCmdCount++]);

		/* Init the 3D PR command helper */
		/*
			Updates for Android (fence sync and Timeline sync prim) are provided in the PR-update
			if no 3D is present. This is so the timeline update cannot happen out of order with any
			other 3D already in flight for the same timeline (PR-updates are done in the 3D cCCB).
			This out of order timeline sync prim update could happen if we attach it to the TA update.
		 */
		if (ui32ClientPRUpdateCount)
		{
			CHKPT_DBG((PVR_DBG_ERROR,
					   "%s: Line %d, ui32ClientPRUpdateCount=%d, "
					   "pauiClientPRUpdateUFOAddress=0x%x, "
					   "ui32ClientPRUpdateValueCount=%d, "
					   "paui32ClientPRUpdateValue=0x%x",
					   __func__, __LINE__, ui32ClientPRUpdateCount,
					   pauiClientPRUpdateUFOAddress->ui32Addr,
					   ui32ClientPRUpdateValueCount,
					   (ui32ClientPRUpdateValueCount == 0) ? PVRSRV_SYNC_CHECKPOINT_SIGNALLED : *paui32ClientPRUpdateValue));
		}

		if (!bUseCombined3DAnd3DPR)
		{
			CHKPT_DBG((PVR_DBG_ERROR,
					   "%s:   calling RGXCmdHelperInitCmdCCB(), ui32ClientPRUpdateCount=%d",
					   __func__, ui32ClientPRUpdateCount));
			RGXCmdHelperInitCmdCCB_OtherData(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
					0,
					NULL,
					NULL,
					ui32ClientPRUpdateCount,
					pauiClientPRUpdateUFOAddress,
					paui32ClientPRUpdateValue,
					pui83DPRDMCmd ? ui323DPRCmdSize : ui323DCmdSize, // If the client has not provided a 3DPR command, the regular 3D command should be used instead
					pui83DPRDMCmd ? pui83DPRDMCmd : pui83DDMCmd,
					NULL,
					NULL,
					NULL,
					RGXFWIF_CCB_CMD_TYPE_3D_PR,
					ui32ExtJobRef,
					ui32IntJobRef,
					ui32PDumpFlags,
					NULL,
					"3D-PR",
					bCCBStateOpen,
					&pas3DCmdHelperData[ui323DCmdCount++]);
		}
	}

	if (bKick3D || bAbort)
	{
		RGX_SERVER_RC_3D_DATA *ps3DData = &psRenderContext->s3DData;
		const RGXFWIF_CCB_CMD_TYPE e3DCmdType = bAbort ? RGXFWIF_CCB_CMD_TYPE_ABORT : RGXFWIF_CCB_CMD_TYPE_3D;

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* Prepare workload estimation */
			WorkEstPrepare(psRenderContext->psDeviceNode->pvDevice,
					&psRenderContext->sWorkEstData,
					&psRenderContext->sWorkEstData.uWorkloadMatchingData.sTA3D.sData3D,
					e3DCmdType,
					&sWorkloadCharacteristics,
					ui64DeadlineInus,
					&sWorkloadKickData3D);
		}
#endif

		/* Init the 3D command helper */
		RGXCmdHelperInitCmdCCB_OtherData(FWCommonContextGetClientCCB(ps3DData->psServerCommonContext),
				bKickTA ? 0 : ui32Client3DFenceCount, /* For a kick with a TA, the 3D fences are added before the PR command instead */
				bKickTA ? NULL : pauiClient3DFenceUFOAddress,
				NULL,
				ui32Client3DUpdateCount,
				pauiClient3DUpdateUFOAddress,
				paui32Client3DUpdateValue,
				ui323DCmdSize,
				pui83DDMCmd,
				(bKickTA ? NULL : &pPreAddr),
				&pPostAddr,
				&pRMWUFOAddr,
				e3DCmdType,
				ui32ExtJobRef,
				ui32IntJobRef,
				ui32PDumpFlags,
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
				&sWorkloadKickData3D,
#else
				NULL,
#endif
				"3D",
				bCCBStateOpen,
				&pas3DCmdHelperData[ui323DCmdCount++]);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* The following are used to determine the offset of the command header containing the workload estimation
			   data so that can be accessed when the KCCB is read */
			ui323DCmdHeaderOffset =	RGXCmdHelperGetDMCommandHeaderOffset(&pas3DCmdHelperData[ui323DCmdCount - 1]);
			ui323DFullRenderCommandOffset =	RGXCmdHelperGetCommandOffset(pas3DCmdHelperData, ui323DCmdCount - 1);
		}
#endif
	}

	/* Protect against array overflow in RGXCmdHelperAcquireCmdCCB() */
	if (unlikely(ui323DCmdCount > CCB_CMD_HELPER_NUM_3D_COMMANDS))
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line", __func__, eError));
		goto fail_3dcmdinit;
	}

	if (ui323DCmdCount)
	{
		PVR_ASSERT(bKickPR || bKick3D);

		/* Acquire space for all the 3D command(s) */
		eError = RGXCmdHelperAcquireCmdCCB(ui323DCmdCount, pas3DCmdHelperData);
		if (unlikely(eError != PVRSRV_OK))
		{
			/* If RGXCmdHelperAcquireCmdCCB fails we skip the scheduling
			 * of a new TA command with the same Write offset in Kernel CCB.
			 */
			CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line", __func__, eError));
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
				pasTACmdHelperData,
				"TA",
				FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext).ui32Addr);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			ui32TACmdOffsetWrapCheck = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext));

			/* This checks if the command would wrap around at the end of the CCB and therefore  would start at an
			   offset of 0 rather than the current command offset */
			if (ui32TACmdOffset < ui32TACmdOffsetWrapCheck)
			{
				ui32TACommandOffset = ui32TACmdOffset;
			}
			else
			{
				ui32TACommandOffset = 0;
			}
		}
#endif
	}

	if (ui323DCmdCount)
	{
		ui323DCmdOffset = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));
		RGXCmdHelperReleaseCmdCCB(ui323DCmdCount,
				pas3DCmdHelperData,
				"3D",
				FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext).ui32Addr);

#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			ui323DCmdOffsetWrapCheck = RGXGetHostWriteOffsetCCB(FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext));

			if (ui323DCmdOffset < ui323DCmdOffsetWrapCheck)
			{
				ui323DCommandOffset = ui323DCmdOffset;
			}
			else
			{
				ui323DCommandOffset = 0;
			}
		}
#endif
	}

	if (ui32TACmdCount)
	{
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext).ui32Addr;
		RGX_CLIENT_CCB *psClientCCB = FWCommonContextGetClientCCB(psRenderContext->sTAData.psServerCommonContext);
		CMDTA3D_SHARED *psGeomCmdShared = IMG_OFFSET_ADDR(pui8TADMCmd, 0);

		sTACmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->sTAData.psServerCommonContext);
		sTACmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(psClientCCB);
		sTACmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(psClientCCB);

		/* Add the Workload data into the KCCB kick */
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* Store the offset to the CCCB command header so that it can be referenced when the KCCB command reaches the FW */
			sTACmdKickData.ui32WorkEstCmdHeaderOffset = ui32TACommandOffset + ui32TACmdHeaderOffset;
		}
#endif

		eError = AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &sTACmdKickData.apsCleanupCtl,
				&sTACmdKickData.ui32NumCleanupCtl,
				RGXFWIF_DM_GEOM,
				bKickTA,
				psKMHWRTDataSet,
				psZSBuffer,
				psMSAAScratchBuffer);
		if (unlikely(eError != PVRSRV_OK))
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line",
					   __func__, eError));
			goto fail_taattachcleanupctls;
		}

		if (psGeomCmdShared)
		{
			HTBLOGK(HTB_SF_MAIN_KICK_TA,
					sTACmdKickData.psContext,
					ui32TACmdOffset,
					psGeomCmdShared->sCmn.ui32FrameNum,
					ui32ExtJobRef,
					ui32IntJobRef);
		}

		RGXSRV_HWPERF_ENQ(psRenderContext,
		                  OSGetCurrentClientProcessIDKM(),
		                  ui32FWCtx,
		                  ui32ExtJobRef,
		                  ui32IntJobRef,
		                  RGX_HWPERF_KICK_TYPE2_GEOM,
		                  iCheckTAFence,
		                  iUpdateTAFence,
		                  iUpdateTATimeline,
		                  uiCheckTAFenceUID,
		                  uiUpdateTAFenceUID,
		                  ui64DeadlineInus,
		                  WORKEST_CYCLES_PREDICTION_GET(sWorkloadKickDataTA));

		if (!bUseSingleFWCommand)
		{
			/* Construct the kernel TA CCB command. */
			RGXFWIF_KCCB_CMD sTAKCCBCmd;
			sTAKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
			sTAKCCBCmd.uCmdData.sCmdKickData = sTACmdKickData;

			LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
			{
				eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
						RGXFWIF_DM_GEOM,
						&sTAKCCBCmd,
						ui32PDumpFlags);
				if (eError2 != PVRSRV_ERROR_RETRY)
				{
					break;
				}
				OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
			} END_LOOP_UNTIL_TIMEOUT();
		}

		if (eError2 != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXKicKTA3DKM failed to schedule kernel CCB command. (0x%x)", eError2));
			/* Mark the error and bail out */
			eError = eError2;
			goto fail_taacquirecmd;
		}

		PVRGpuTraceEnqueueEvent(psRenderContext->psDeviceNode,
		                        ui32FWCtx, ui32ExtJobRef, ui32IntJobRef,
		                        RGX_HWPERF_KICK_TYPE2_GEOM);
	}

	if (ui323DCmdCount)
	{
		RGXFWIF_KCCB_CMD s3DKCCBCmd = { 0 };
		IMG_UINT32 ui32FWCtx = FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext).ui32Addr;
		RGX_CLIENT_CCB *psClientCCB = FWCommonContextGetClientCCB(psRenderContext->s3DData.psServerCommonContext);
		CMDTA3D_SHARED *ps3DCmdShared = IMG_OFFSET_ADDR(pui83DDMCmd, 0);

		s3DCmdKickData.psContext = FWCommonContextGetFWAddress(psRenderContext->s3DData.psServerCommonContext);
		s3DCmdKickData.ui32CWoffUpdate = RGXGetHostWriteOffsetCCB(psClientCCB);
		s3DCmdKickData.ui32CWrapMaskUpdate = RGXGetWrapMaskCCB(psClientCCB);

		/* Add the Workload data into the KCCB kick */
#if defined(SUPPORT_WORKLOAD_ESTIMATION)
		if (!PVRSRV_VZ_MODE_IS(GUEST))
		{
			/* Store the offset to the CCCB command header so that it can be referenced when the KCCB command reaches the FW */
			s3DCmdKickData.ui32WorkEstCmdHeaderOffset = ui323DCommandOffset + ui323DCmdHeaderOffset + ui323DFullRenderCommandOffset;
		}
#endif

		eError = AttachKickResourcesCleanupCtls((PRGXFWIF_CLEANUP_CTL *) &s3DCmdKickData.apsCleanupCtl,
				&s3DCmdKickData.ui32NumCleanupCtl,
				RGXFWIF_DM_3D,
				bKick3D,
				psKMHWRTDataSet,
				psZSBuffer,
				psMSAAScratchBuffer);
		if (unlikely(eError != PVRSRV_OK))
		{
			CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line",
					   __func__, eError));
			goto fail_3dattachcleanupctls;
		}

		if (ps3DCmdShared)
		{
			HTBLOGK(HTB_SF_MAIN_KICK_3D,
					s3DCmdKickData.psContext,
					ui323DCmdOffset,
					ps3DCmdShared->sCmn.ui32FrameNum,
					ui32ExtJobRef,
					ui32IntJobRef);
		}

		RGXSRV_HWPERF_ENQ(psRenderContext,
		                  OSGetCurrentClientProcessIDKM(),
		                  ui32FWCtx,
		                  ui32ExtJobRef,
		                  ui32IntJobRef,
		                  RGX_HWPERF_KICK_TYPE2_3D,
		                  iCheck3DFence,
		                  iUpdate3DFence,
		                  iUpdate3DTimeline,
		                  uiCheck3DFenceUID,
		                  uiUpdate3DFenceUID,
		                  ui64DeadlineInus,
		                  WORKEST_CYCLES_PREDICTION_GET(sWorkloadKickData3D));

		if (bUseSingleFWCommand)
		{
			/* Construct the kernel TA/3D CCB command. */
			s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_COMBINED_TA_3D_KICK;
			s3DKCCBCmd.uCmdData.sCombinedTA3DCmdKickData.sTACmdKickData = sTACmdKickData;
			s3DKCCBCmd.uCmdData.sCombinedTA3DCmdKickData.s3DCmdKickData = s3DCmdKickData;
		}
		else
		{
			/* Construct the kernel 3D CCB command. */
			s3DKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_KICK;
			s3DKCCBCmd.uCmdData.sCmdKickData = s3DCmdKickData;
		}

		LOOP_UNTIL_TIMEOUT(MAX_HW_TIME_US)
		{
			eError2 = RGXScheduleCommand(psRenderContext->psDeviceNode->pvDevice,
			                             RGXFWIF_DM_3D,
			                             &s3DKCCBCmd,
			                             ui32PDumpFlags);
			if (eError2 != PVRSRV_ERROR_RETRY)
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();

		if (eError2 != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVRGXKicKTA3DKM failed to schedule kernel CCB command. (0x%x)", eError2));
			if (eError == PVRSRV_OK)
			{
				eError = eError2;
			}
			goto fail_3dacquirecmd;
		}

		PVRGpuTraceEnqueueEvent(psRenderContext->psDeviceNode,
		                        ui32FWCtx, ui32ExtJobRef, ui32IntJobRef,
		                        RGX_HWPERF_KICK_TYPE2_3D);
	}

	/*
	 * Now check eError (which may have returned an error from our earlier calls
	 * to RGXCmdHelperAcquireCmdCCB) - we needed to process any flush command first
	 * so we check it now...
	 */
	if (unlikely(eError != PVRSRV_OK ))
	{
		CHKPT_DBG((PVR_DBG_ERROR, "%s: Failed, eError=%d, Line",
				   __func__, eError));
		goto fail_3dacquirecmd;
	}

#if defined(NO_HARDWARE)
	/* If NO_HARDWARE, signal the output fence's sync checkpoint and sync prim */
	if (psUpdateTASyncCheckpoint)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Signalling NOHW sync checkpoint [TA] <%p>, ID:%d, FwAddr=0x%x",
				   __func__, (void*)psUpdateTASyncCheckpoint,
				   SyncCheckpointGetId(psUpdateTASyncCheckpoint),
				   SyncCheckpointGetFirmwareAddr(psUpdateTASyncCheckpoint)));
		SyncCheckpointSignalNoHW(psUpdateTASyncCheckpoint);
	}
	if (psTAFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Updating NOHW sync prim [TA] <%p> to %d",
				   __func__, (void*)psTAFenceTimelineUpdateSync,
				   ui32TAFenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(psTAFenceTimelineUpdateSync, ui32TAFenceTimelineUpdateValue);
	}

	if (psUpdate3DSyncCheckpoint)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Signalling NOHW sync checkpoint [3D] <%p>, ID:%d, FwAddr=0x%x",
				   __func__, (void*)psUpdate3DSyncCheckpoint,
				   SyncCheckpointGetId(psUpdate3DSyncCheckpoint),
				   SyncCheckpointGetFirmwareAddr(psUpdate3DSyncCheckpoint)));
		SyncCheckpointSignalNoHW(psUpdate3DSyncCheckpoint);
	}
	if (ps3DFenceTimelineUpdateSync)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   Updating NOHW sync prim [3D] <%p> to %d",
				   __func__, (void*)ps3DFenceTimelineUpdateSync,
				   ui323DFenceTimelineUpdateValue));
		SyncPrimNoHwUpdate(ps3DFenceTimelineUpdateSync, ui323DFenceTimelineUpdateValue);
	}
	SyncCheckpointNoHWUpdateTimelines(NULL);

#endif /* defined(NO_HARDWARE) */

#if defined(SUPPORT_BUFFER_SYNC)
	if (psBufferSyncData)
	{
		CHKPT_DBG((PVR_DBG_ERROR,
				   "%s:   calling pvr_buffer_sync_kick_succeeded(psBufferSyncData=<%p>)...",
				   __func__, (void*)psBufferSyncData));
		pvr_buffer_sync_kick_succeeded(psBufferSyncData);
	}
	if (apsBufferFenceSyncCheckpoints)
	{
		kfree(apsBufferFenceSyncCheckpoints);
	}
#endif /* defined(SUPPORT_BUFFER_SYNC) */

	if (piUpdateTAFence)
	{
		*piUpdateTAFence = iUpdateTAFence;
	}
	if (piUpdate3DFence)
	{
		*piUpdate3DFence = iUpdate3DFence;
	}

	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence.
	 * NOTE: 3D fence is always submitted, either via 3D or TA(PR).
	 */
	if (bKickTA)
	{
		SyncAddrListDeRefCheckpoints(ui32FenceTASyncCheckpointCount, apsFenceTASyncCheckpoints);
	}
	SyncAddrListDeRefCheckpoints(ui32Fence3DSyncCheckpointCount, apsFence3DSyncCheckpoints);

	if (pvTAUpdateFenceFinaliseData && (iUpdateTAFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psRenderContext->psDeviceNode, iUpdateTAFence,
									pvTAUpdateFenceFinaliseData,
									psUpdateTASyncCheckpoint, szFenceNameTA);
	}
	if (pv3DUpdateFenceFinaliseData && (iUpdate3DFence != PVRSRV_NO_FENCE))
	{
		SyncCheckpointFinaliseFence(psRenderContext->psDeviceNode, iUpdate3DFence,
									pv3DUpdateFenceFinaliseData,
									psUpdate3DSyncCheckpoint, szFenceName3D);
	}

	/* Free the memory that was allocated for the sync checkpoint list returned by ResolveFence() */
	if (apsFenceTASyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFenceTASyncCheckpoints);
	}
	if (apsFence3DSyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFence3DSyncCheckpoints);
	}

	if (sTASyncData.paui32ClientUpdateValue)
	{
		OSFreeMem(sTASyncData.paui32ClientUpdateValue);
	}
	if (s3DSyncData.paui32ClientUpdateValue)
	{
		OSFreeMem(s3DSyncData.paui32ClientUpdateValue);
	}

#if defined(SUPPORT_VALIDATION)
	if (bTestSLRAdd3DCheck)
	{
		SyncCheckpointFree(psDummySyncCheckpoint);
	}
#endif
	OSLockRelease(psRenderContext->hLock);

	return PVRSRV_OK;

fail_3dattachcleanupctls:
fail_taattachcleanupctls:
fail_3dacquirecmd:
fail_3dcmdinit:
fail_taacquirecmd:
	SyncAddrListRollbackCheckpoints(psRenderContext->psDeviceNode, &psRenderContext->sSyncAddrListTAFence);
	SyncAddrListRollbackCheckpoints(psRenderContext->psDeviceNode, &psRenderContext->sSyncAddrListTAUpdate);
	SyncAddrListRollbackCheckpoints(psRenderContext->psDeviceNode, &psRenderContext->sSyncAddrList3DFence);
	SyncAddrListRollbackCheckpoints(psRenderContext->psDeviceNode, &psRenderContext->sSyncAddrList3DUpdate);
	/* Where a TA-only kick (ie no 3D) is submitted, the PR update will make use of the unused 3DUpdate list.
	 * If this has happened, performing a rollback on pauiClientPRUpdateUFOAddress will simply repeat what
	 * has already been done for the sSyncAddrList3DUpdate above and result in a double decrement of the
	 * sync checkpoint's hEnqueuedCCBCount, so we need to check before rolling back the PRUpdate.
	 */
	if (pauiClientPRUpdateUFOAddress && (pauiClientPRUpdateUFOAddress != psRenderContext->sSyncAddrList3DUpdate.pasFWAddrs))
	{
		SyncCheckpointRollbackFromUFO(psRenderContext->psDeviceNode, pauiClientPRUpdateUFOAddress->ui32Addr);
	}

fail_alloc_update_values_mem_3D:
	if (iUpdate3DFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(iUpdate3DFence, pv3DUpdateFenceFinaliseData);
	}
fail_create_3d_fence:
fail_alloc_update_values_mem_TA:
	if (iUpdateTAFence != PVRSRV_NO_FENCE)
	{
		SyncCheckpointRollbackFenceData(iUpdateTAFence, pvTAUpdateFenceFinaliseData);
	}
fail_create_ta_fence:
#if !defined(SUPPORT_BUFFER_SYNC)
err_no_buffer_sync_invalid_params:
#endif /* !defined(SUPPORT_BUFFER_SYNC) */
err_pr_fence_address:
err_populate_sync_addr_list_3d_update:
err_populate_sync_addr_list_3d_fence:
err_populate_sync_addr_list_ta_update:
err_populate_sync_addr_list_ta_fence:
err_not_enough_space:
	/* Drop the references taken on the sync checkpoints in the
	 * resolved input fence.
	 * NOTE: 3D fence is always submitted, either via 3D or TA(PR).
	 */
#if defined(SUPPORT_BUFFER_SYNC)
	SyncAddrListDeRefCheckpoints(ui32BufferFenceSyncCheckpointCount,
	                             apsBufferFenceSyncCheckpoints);
#endif
	SyncAddrListDeRefCheckpoints(ui32Fence3DSyncCheckpointCount, apsFence3DSyncCheckpoints);
fail_resolve_input_3d_fence:
	if (bKickTA)
	{
		SyncAddrListDeRefCheckpoints(ui32FenceTASyncCheckpointCount, apsFenceTASyncCheckpoints);
	}
fail_resolve_input_ta_fence:
	/* Free the memory that was allocated for the sync checkpoint list returned by ResolveFence() */
	if (apsFenceTASyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFenceTASyncCheckpoints);
	}
	if (apsFence3DSyncCheckpoints)
	{
		SyncCheckpointFreeCheckpointListMem(apsFence3DSyncCheckpoints);
	}
	if (sTASyncData.paui32ClientUpdateValue)
	{
		OSFreeMem(sTASyncData.paui32ClientUpdateValue);
	}
	if (s3DSyncData.paui32ClientUpdateValue)
	{
		OSFreeMem(s3DSyncData.paui32ClientUpdateValue);
	}
#if defined(SUPPORT_VALIDATION)
	if (bTestSLRAdd3DCheck)
	{
		SyncCheckpointFree(psDummySyncCheckpoint);
	}
#endif
#if defined(SUPPORT_BUFFER_SYNC)
	if (psBufferSyncData)
	{
		pvr_buffer_sync_kick_failed(psBufferSyncData);
	}
	if (apsBufferFenceSyncCheckpoints)
	{
		kfree(apsBufferFenceSyncCheckpoints);
	}
#endif /* defined(SUPPORT_BUFFER_SYNC) */
	PVR_ASSERT(eError != PVRSRV_OK);
	OSLockRelease(psRenderContext->hLock);
	return eError;
}

PVRSRV_ERROR PVRSRVRGXSetRenderContextPriorityKM(CONNECTION_DATA *psConnection,
		PVRSRV_DEVICE_NODE * psDeviceNode,
		RGX_SERVER_RENDER_CONTEXT *psRenderContext,
		IMG_INT32 i32Priority)
{
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psDeviceNode);

	OSLockAcquire(psRenderContext->hLock);

	if (psRenderContext->sTAData.i32Priority != i32Priority)
	{
		eError = ContextSetPriority(psRenderContext->sTAData.psServerCommonContext,
				psConnection,
				psRenderContext->psDeviceNode->pvDevice,
				i32Priority,
				RGXFWIF_DM_GEOM);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to set the priority of the TA part of the rendercontext (%s)",
					 __func__, PVRSRVGetErrorString(eError)));
			goto fail_tacontext;
		}
		psRenderContext->sTAData.i32Priority = i32Priority;
	}

	if (psRenderContext->s3DData.i32Priority != i32Priority)
	{
		eError = ContextSetPriority(psRenderContext->s3DData.psServerCommonContext,
				psConnection,
				psRenderContext->psDeviceNode->pvDevice,
				i32Priority,
				RGXFWIF_DM_3D);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to set the priority of the 3D part of the rendercontext (%s)",
					 __func__, PVRSRVGetErrorString(eError)));
			goto fail_3dcontext;
		}
		psRenderContext->s3DData.i32Priority = i32Priority;
	}

	OSLockRelease(psRenderContext->hLock);
	return PVRSRV_OK;

fail_3dcontext:
fail_tacontext:
	OSLockRelease(psRenderContext->hLock);
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


PVRSRV_ERROR PVRSRVRGXSetRenderContextPropertyKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext,
                                                 RGX_CONTEXT_PROPERTY eContextProperty,
                                                 IMG_UINT64 ui64Input,
                                                 IMG_UINT64 *pui64Output)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	switch (eContextProperty)
	{
		case RGX_CONTEXT_PROPERTY_FLAGS:
		{
			IMG_UINT32 ui32ContextFlags = (IMG_UINT32)ui64Input;

			OSLockAcquire(psRenderContext->hLock);
			eError = FWCommonContextSetFlags(psRenderContext->sTAData.psServerCommonContext,
			                                 ui32ContextFlags);
			if (eError == PVRSRV_OK)
			{
				eError = FWCommonContextSetFlags(psRenderContext->s3DData.psServerCommonContext,
			                                     ui32ContextFlags);
			}
			OSLockRelease(psRenderContext->hLock);
			break;
		}

		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PVRSRV_ERROR_NOT_SUPPORTED - asked to set unknown property (%d)", __func__, eContextProperty));
			eError = PVRSRV_ERROR_NOT_SUPPORTED;
		}
	}

	return eError;
}


void DumpRenderCtxtsInfo(PVRSRV_RGXDEV_INFO *psDevInfo,
                         DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
                         void *pvDumpDebugFile,
                         IMG_UINT32 ui32VerbLevel)
{
	DLLIST_NODE *psNode, *psNext;
	OSWRLockAcquireRead(psDevInfo->hRenderCtxListLock);
	dllist_foreach_node(&psDevInfo->sRenderCtxtListHead, psNode, psNext)
	{
		RGX_SERVER_RENDER_CONTEXT *psCurrentServerRenderCtx =
				IMG_CONTAINER_OF(psNode, RGX_SERVER_RENDER_CONTEXT, sListNode);

		DumpFWCommonContextInfo(psCurrentServerRenderCtx->sTAData.psServerCommonContext,
		                        pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
		DumpFWCommonContextInfo(psCurrentServerRenderCtx->s3DData.psServerCommonContext,
		                        pfnDumpDebugPrintf, pvDumpDebugFile, ui32VerbLevel);
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
		if (NULL != psCurrentServerRenderCtx->sTAData.psServerCommonContext)
		{
			if (CheckStalledClientCommonContext(psCurrentServerRenderCtx->sTAData.psServerCommonContext, RGX_KICK_TYPE_DM_TA) == PVRSRV_ERROR_CCCB_STALLED)
			{
				ui32ContextBitMask |= RGX_KICK_TYPE_DM_TA;
			}
		}

		if (NULL != psCurrentServerRenderCtx->s3DData.psServerCommonContext)
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

/*
 * RGXRenderContextStalledKM
 */
PVRSRV_ERROR RGXRenderContextStalledKM(RGX_SERVER_RENDER_CONTEXT *psRenderContext)
{
	RGXCheckForStalledClientContexts((PVRSRV_RGXDEV_INFO *) psRenderContext->psDeviceNode->pvDevice, IMG_TRUE);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (rgxta3d.c)
******************************************************************************/
