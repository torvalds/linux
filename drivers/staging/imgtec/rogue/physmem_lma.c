/*************************************************************************/ /*!
@File           physmem_lma.c
@Title          Local card memory allocator
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management. This module is responsible for
                implementing the function callbacks for local card memory.
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

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"
#include "rgx_pdump_panics.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "devicemem_server_utils.h"
#include "physmem_lma.h"
#include "pdump_km.h"
#include "pmr.h"
#include "pmr_impl.h"
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION)
#include "rgxutils.h"
#endif

typedef struct _PMR_LMALLOCARRAY_DATA_ {
	PVRSRV_DEVICE_NODE *psDevNode;
    IMG_INT32 iNumPagesAllocated;
    /*
     * uiTotalNumPages:
     * Total number of pages supported by this PMR. (Fixed as of now due the fixed Page table array size)
     */
    IMG_UINT32 uiTotalNumPages;
    IMG_UINT32 uiPagesToAlloc;

	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiAllocSize;
	IMG_DEV_PHYADDR *pasDevPAddr;

	IMG_BOOL bZeroOnAlloc;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bFwLocalAlloc;

	/* Tells if allocation is physically backed */
	IMG_BOOL bHasLMPages;
	IMG_BOOL bOnDemand;

	/*
	  record at alloc time whether poisoning will be required when the
	  PMR is freed.
	*/
	IMG_BOOL bPoisonOnFree;

	/* Physical heap and arena pointers for this allocation */
	PHYS_HEAP* psPhysHeap;
	RA_ARENA* psArena;
	PVRSRV_MEMALLOCFLAGS_T uiAllocFlags;

} PMR_LMALLOCARRAY_DATA;

static PVRSRV_ERROR _MapAlloc(PVRSRV_DEVICE_NODE *psDevNode, 
							  IMG_DEV_PHYADDR *psDevPAddr,
							  size_t uiSize,
							  IMG_BOOL bFwLocalAlloc,
							  PMR_FLAGS_T ulFlags,
							  void **pvPtr)
{
	IMG_UINT32 ui32CPUCacheFlags = DevmemCPUCacheMode(psDevNode, ulFlags);
	IMG_CPU_PHYADDR sCpuPAddr;
	PHYS_HEAP *psPhysHeap;

	if (bFwLocalAlloc)
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL];
	}
	else
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL];
	}

	PhysHeapDevPAddrToCpuPAddr(psPhysHeap, 1, &sCpuPAddr, psDevPAddr);

	*pvPtr = OSMapPhysToLin(sCpuPAddr, uiSize, ui32CPUCacheFlags);
	if (*pvPtr == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	else
	{
		return PVRSRV_OK;
	}
}

static void _UnMapAlloc(PVRSRV_DEVICE_NODE *psDevNode,
						size_t uiSize,
						IMG_BOOL bFwLocalAlloc,
						PMR_FLAGS_T ulFlags,
						void *pvPtr)
{
	OSUnMapPhysToLin(pvPtr, uiSize, PVRSRV_CPU_CACHE_MODE(ulFlags));
}

static PVRSRV_ERROR
_PoisonAlloc(PVRSRV_DEVICE_NODE *psDevNode,
			 IMG_DEV_PHYADDR *psDevPAddr,
			 IMG_BOOL bFwLocalAlloc,
			 IMG_UINT32 uiAllocSize,
			 const IMG_CHAR *pacPoisonData,
			 size_t uiPoisonSize)
{
	IMG_UINT32 uiSrcByteIndex;
	IMG_UINT32 uiDestByteIndex;
	void *pvKernLin = NULL;
	IMG_CHAR *pcDest = NULL;

	PVRSRV_ERROR eError;

	eError = _MapAlloc(psDevNode,
					   psDevPAddr,
					   uiAllocSize,
					   bFwLocalAlloc,
					   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
					   &pvKernLin);
	if (eError != PVRSRV_OK)
	{
		goto map_failed;
	}
	pcDest = pvKernLin;

	uiSrcByteIndex = 0;
	for(uiDestByteIndex=0; uiDestByteIndex<uiAllocSize; uiDestByteIndex++)
	{
		pcDest[uiDestByteIndex] = pacPoisonData[uiSrcByteIndex];
		uiSrcByteIndex++;
		if (uiSrcByteIndex == uiPoisonSize)
		{
			uiSrcByteIndex = 0;
		}
	}

	_UnMapAlloc(psDevNode, uiAllocSize, bFwLocalAlloc, 0,pvKernLin);

	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to poison allocation"));
	return eError;
}

static PVRSRV_ERROR
_ZeroAlloc(PVRSRV_DEVICE_NODE *psDevNode,
		   IMG_DEV_PHYADDR *psDevPAddr,
		   IMG_BOOL bFwLocalAlloc,
		   IMG_UINT32 uiAllocSize)
{
	void *pvKernLin = NULL;
	PVRSRV_ERROR eError;

	eError = _MapAlloc(psDevNode, 
					   psDevPAddr,
					   uiAllocSize,
					   bFwLocalAlloc,
					   PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
					   &pvKernLin);
	if (eError != PVRSRV_OK)
	{
		goto map_failed;
	}

	/* NOTE: 'CachedMemSet' means the operating system default memset, which
	 *       we *assume* in the LMA code will be faster, and doesn't need to
	 *       worry about ARM64.
	 */
	OSCachedMemSet(pvKernLin, 0, uiAllocSize);

	_UnMapAlloc(psDevNode, uiAllocSize, bFwLocalAlloc, 0, pvKernLin);

	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to zero allocation"));
	return eError;
}

static const IMG_CHAR _AllocPoison[] = "^PoIsOn";
static const IMG_UINT32 _AllocPoisonSize = 7;
static const IMG_CHAR _FreePoison[] = "<DEAD-BEEF>";
static const IMG_UINT32 _FreePoisonSize = 11;

static PVRSRV_ERROR
_AllocLMPageArray(PVRSRV_DEVICE_NODE *psDevNode,
			  PMR_SIZE_T uiSize,
			  PMR_SIZE_T uiChunkSize,
			  IMG_UINT32 ui32NumPhysChunks,
			  IMG_UINT32 ui32NumVirtChunks,
			  IMG_UINT32 *pabMappingTable,
			  IMG_UINT32 uiLog2PageSize,
			  IMG_BOOL bZero,
			  IMG_BOOL bPoisonOnAlloc,
			  IMG_BOOL bPoisonOnFree,
			  IMG_BOOL bContig,
			  IMG_BOOL bOnDemand,
			  IMG_BOOL bFwLocalAlloc,
			  PHYS_HEAP* psPhysHeap,
			  PVRSRV_MEMALLOCFLAGS_T uiAllocFlags,
			  PMR_LMALLOCARRAY_DATA **ppsPageArrayDataPtr
			  )
{
	PMR_LMALLOCARRAY_DATA *psPageArrayData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(!bZero || !bPoisonOnAlloc);

	if (uiSize >= 0x1000000000ULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "physmem_lma.c: Do you really want 64GB of physical memory in one go?  This is likely a bug"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errorOnParam;
	}

	PVR_ASSERT(OSGetPageShift() <= uiLog2PageSize);

	if ((uiSize & ((1ULL << uiLog2PageSize) - 1)) != 0)
	{
		eError = PVRSRV_ERROR_PMR_NOT_PAGE_MULTIPLE;
		goto errorOnParam;
	}

	psPageArrayData = OSAllocZMem(sizeof(PMR_LMALLOCARRAY_DATA));
	if (psPageArrayData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errorOnAllocArray;
	}

	if (bContig)
	{
		/*
			Some allocations require kernel mappings in which case in order
			to be virtually contiguous we also have to be physically contiguous.
		*/
		psPageArrayData->uiPagesToAlloc = psPageArrayData->uiTotalNumPages = 1;
		psPageArrayData->uiAllocSize = TRUNCATE_64BITS_TO_32BITS(uiSize);
		psPageArrayData->uiLog2AllocSize = uiLog2PageSize;
	}
	else
	{
		IMG_UINT32 uiNumPages;

		/* Use of cast below is justified by the assertion that follows to
		prove that no significant bits have been truncated */
		uiNumPages = (IMG_UINT32)(((uiSize-1)>>uiLog2PageSize) + 1);
		PVR_ASSERT(((PMR_SIZE_T)uiNumPages << uiLog2PageSize) == uiSize);
		psPageArrayData->uiTotalNumPages = uiNumPages;
		if((1 == ui32NumPhysChunks) && (1 == ui32NumVirtChunks))
		{
			psPageArrayData->uiPagesToAlloc = uiNumPages;
		}else{
			psPageArrayData->uiPagesToAlloc = ui32NumPhysChunks;
		}
		psPageArrayData->uiAllocSize = 1 << uiLog2PageSize;
		psPageArrayData->uiLog2AllocSize = uiLog2PageSize;
	}
	psPageArrayData->psDevNode = psDevNode;
	psPageArrayData->pasDevPAddr = OSAllocMem(sizeof(IMG_DEV_PHYADDR)*
												psPageArrayData->uiTotalNumPages);
	if (psPageArrayData->pasDevPAddr == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errorOnAllocAddr;
	}

	OSCachedMemSet(&psPageArrayData->pasDevPAddr[0], INVALID_PAGE, sizeof(IMG_DEV_PHYADDR)*
												psPageArrayData->uiTotalNumPages);

    psPageArrayData->iNumPagesAllocated = 0;
    psPageArrayData->bZeroOnAlloc = bZero;
	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
 	psPageArrayData->bHasLMPages = IMG_FALSE;
 	psPageArrayData->bOnDemand = bOnDemand;
 	psPageArrayData->bFwLocalAlloc = bFwLocalAlloc;
 	psPageArrayData->psPhysHeap = psPhysHeap;
 	psPageArrayData->uiAllocFlags = uiAllocFlags;

	*ppsPageArrayDataPtr = psPageArrayData;

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/

errorOnAllocAddr:
	OSFreeMem(psPageArrayData);

errorOnAllocArray:
errorOnParam:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}


static PVRSRV_ERROR
_AllocLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData, IMG_UINT32 *pui32MapTable)
{
	PVRSRV_ERROR eError;
	RA_BASE_T uiCardAddr;
	RA_LENGTH_T uiActualSize;
	IMG_UINT32 i,ui32Index=0;
	IMG_UINT32 uiAllocSize;
	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiRegionId;
	PVRSRV_DEVICE_NODE *psDevNode;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc;
	RA_ARENA *pArena;

	PVR_ASSERT(NULL != psPageArrayData);
	PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);

	uiAllocSize = psPageArrayData->uiAllocSize;
	uiLog2AllocSize = psPageArrayData->uiLog2AllocSize;
	psDevNode = psPageArrayData->psDevNode;
	bPoisonOnAlloc =  psPageArrayData->bPoisonOnAlloc;
	bZeroOnAlloc =  psPageArrayData->bZeroOnAlloc;

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	if (psPageArrayData->bFwLocalAlloc)
	{
		PVR_ASSERT(psDevNode->uiKernelFwRAIdx < RGXFW_NUM_OS);
		pArena = psDevNode->psKernelFwMemArena[psDevNode->uiKernelFwRAIdx];
		psDevNode->uiKernelFwRAIdx = 0;
	}
	else
#endif
	{
		/* Get suitable local memory region for this allocation */
		uiRegionId = PhysHeapGetRegionId(psPageArrayData->psPhysHeap, psPageArrayData->uiAllocFlags);

		PVR_ASSERT(uiRegionId < psDevNode->ui32NumOfLocalMemArenas);
		pArena = psDevNode->apsLocalDevMemArenas[uiRegionId];
	}

	if(psPageArrayData->uiTotalNumPages < (psPageArrayData->iNumPagesAllocated + psPageArrayData->uiPagesToAlloc))
	{
		PVR_DPF((PVR_DBG_ERROR,"Pages requested to allocate larger than original PMR alloc Size"));
		eError = PVRSRV_ERROR_PMR_BAD_MAPPINGTABLE_SIZE;
		return eError;
	}


#if defined(SUPPORT_GPUVIRT_VALIDATION)
	{
		IMG_UINT32  ui32OSid=0, ui32OSidReg=0;
		IMG_BOOL    bOSidAxiProt;
		IMG_PID     pId;

		pId=OSGetCurrentClientProcessIDKM();
		RetrieveOSidsfromPidList(pId, &ui32OSid, &ui32OSidReg, &bOSidAxiProt);

		pArena=psDevNode->psOSidSubArena[ui32OSid];
		PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Giving from OS slot %d",ui32OSid));
	}
#endif

	psPageArrayData->psArena = pArena;

	for(i=0;i<psPageArrayData->uiPagesToAlloc;i++)
	{

		/*This part of index finding should happen before allocating page. Just avoiding intricate paths */
		if(psPageArrayData->uiTotalNumPages == psPageArrayData->uiPagesToAlloc)
		{
			ui32Index = i;
		}
		else
		{
			if(NULL == pui32MapTable)
			{
				PVR_DPF((PVR_DBG_MESSAGE,"Mapping table cannot be null"));
				eError = PVRSRV_ERROR_PMR_INVALID_MAP_INDEX_ARRAY;
				goto errorOnRAAlloc;
			}

			ui32Index = pui32MapTable[i];
			if(ui32Index >= psPageArrayData->uiTotalNumPages)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "%s: Page alloc request Index out of bounds for PMR @0x%p",__func__, psPageArrayData));
				eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
				goto errorOnRAAlloc;
			}

			if(INVALID_PAGE != psPageArrayData->pasDevPAddr[ui32Index].uiAddr)
			{
				PVR_DPF((PVR_DBG_MESSAGE,"Mapping already exists"));
				eError = PVRSRV_ERROR_PMR_MAPPING_ALREADY_EXISTS;
				goto errorOnRAAlloc;
			}
		}

		eError = RA_Alloc(pArena,
		                  uiAllocSize,
		                  RA_NO_IMPORT_MULTIPLIER,
		                  0,                       /* No flags */
		                  1ULL << uiLog2AllocSize,
		                  "LMA_Page_Alloc",
		                  &uiCardAddr,
		                  &uiActualSize,
		                  NULL);                   /* No private handle */

#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Address: %llu \n",uiCardAddr));
}
#endif

		if (PVRSRV_OK != eError)
		{
			PVR_DPF((PVR_DBG_ERROR,"Failed to Allocate the page @index:%d",ui32Index));
			eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
			goto errorOnRAAlloc;
		}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* Allocation is done a page at a time */
		PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiActualSize);
#else
		{
			IMG_CPU_PHYADDR sLocalCpuPAddr;

			sLocalCpuPAddr.uiAddr = (IMG_UINT64)uiCardAddr;
			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES,
									 NULL,
									 sLocalCpuPAddr,
									 uiActualSize,
									 NULL);
		}
#endif
#endif

		psPageArrayData->pasDevPAddr[ui32Index].uiAddr = uiCardAddr;
		if (bPoisonOnAlloc)
		{
			eError = _PoisonAlloc(psDevNode,
								  &psPageArrayData->pasDevPAddr[i],
								  psPageArrayData->bFwLocalAlloc,
								  uiAllocSize,
								  _AllocPoison,
								  _AllocPoisonSize);
			if (eError !=PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Failed to poison the page"));
				goto errorOnPoison;
			}
		}

		if (bZeroOnAlloc)
		{
			eError = _ZeroAlloc(psDevNode,
								&psPageArrayData->pasDevPAddr[i],
								psPageArrayData->bFwLocalAlloc,
								uiAllocSize);
			if (eError !=PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"Failed to zero the page"));
				goto errorOnZero;
			}
		}
	}
	psPageArrayData->iNumPagesAllocated += psPageArrayData->uiPagesToAlloc;
	if(psPageArrayData->iNumPagesAllocated)
	{
		psPageArrayData->bHasLMPages = IMG_TRUE;
	}

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnZero:
errorOnPoison:
	eError = PVRSRV_ERROR_PMR_FAILED_TO_ALLOC_PAGES;
errorOnRAAlloc:
PVR_DPF((PVR_DBG_ERROR,
                     "%s: alloc_pages failed to honour request %d @index: %d of %d pages: (%s)",__func__,
                     ui32Index,
                     i,
                     psPageArrayData->uiPagesToAlloc,
                     PVRSRVGetErrorStringKM(eError)));
	while (--i < psPageArrayData->uiPagesToAlloc)
	{
		if(psPageArrayData->uiTotalNumPages == psPageArrayData->uiPagesToAlloc)
		{
			ui32Index = i;
		}
		else
		{
			if(NULL != pui32MapTable)
				ui32Index = pui32MapTable[i];
		}

		if(ui32Index < psPageArrayData->uiTotalNumPages)
		{
			RA_Free(pArena, psPageArrayData->pasDevPAddr[ui32Index].uiAddr);
			psPageArrayData->pasDevPAddr[ui32Index].uiAddr = INVALID_PAGE;
		}
	}
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR
_FreeLMPageArray(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	OSFreeMem(psPageArrayData->pasDevPAddr);

	PVR_DPF((PVR_DBG_MESSAGE, "physmem_lma.c: freed local memory array structure for PMR @0x%p", psPageArrayData));

	OSFreeMem(psPageArrayData);

	return PVRSRV_OK;
}

static PVRSRV_ERROR
_FreeLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData,IMG_UINT32 *pui32FreeIndices, IMG_UINT32 ui32FreePageCount)
{
	IMG_UINT32 uiAllocSize;
	IMG_UINT32 i,ui32PagesToFree=0,ui32PagesFreed=0,ui32Index=0;
	RA_ARENA *pArena = psPageArrayData->psArena;

#if defined(SUPPORT_PVRSRV_GPUVIRT)
	PVRSRV_DEVICE_NODE *psDevNode = psPageArrayData->psDevNode;
	if (psPageArrayData->bFwLocalAlloc)
	{
		PVR_ASSERT(psDevNode->uiKernelFwRAIdx < RGXFW_NUM_OS);
		pArena = psDevNode->psKernelFwMemArena[psDevNode->uiKernelFwRAIdx];
		psDevNode->uiKernelFwRAIdx = 0;
	}
#endif

	PVR_ASSERT(psPageArrayData->bHasLMPages);

	uiAllocSize = psPageArrayData->uiAllocSize;

	ui32PagesToFree = (NULL == pui32FreeIndices)?psPageArrayData->uiTotalNumPages:ui32FreePageCount;

	for (i = 0;i < ui32PagesToFree;i++)
	{
		if(NULL == pui32FreeIndices)
		{
			ui32Index = i;
		}
		else
		{
			ui32Index = pui32FreeIndices[i];
		}

		if (INVALID_PAGE != psPageArrayData->pasDevPAddr[ui32Index].uiAddr)
		{
			ui32PagesFreed++;
			if (psPageArrayData->bPoisonOnFree)
			{
				_PoisonAlloc(psPageArrayData->psDevNode,
							 &psPageArrayData->pasDevPAddr[ui32Index],
							 psPageArrayData->bFwLocalAlloc,
							 uiAllocSize,
							 _FreePoison,
							 _FreePoisonSize);
			}

			RA_Free(pArena,	psPageArrayData->pasDevPAddr[ui32Index].uiAddr);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			/* Allocation is done a page at a time */
			PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiAllocSize);
#else
			{
				PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, psPageArrayData->pasDevPAddr[ui32Index].uiAddr);
			}
#endif
#endif
			psPageArrayData->pasDevPAddr[ui32Index].uiAddr = INVALID_PAGE;
		}
	}
	psPageArrayData->iNumPagesAllocated -= ui32PagesFreed;

    PVR_ASSERT(0 <= psPageArrayData->iNumPagesAllocated);

	if(0 == psPageArrayData->iNumPagesAllocated)
	{
		psPageArrayData->bHasLMPages = IMG_FALSE;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: freed %d local memory for PMR @0x%p",__func__,(ui32PagesFreed*uiAllocSize), psPageArrayData));
	return PVRSRV_OK;
}

/*
 *
 * Implementation of callback functions
 *
 */

/* destructor func is called after last reference disappears, but
   before PMR itself is freed. */
static PVRSRV_ERROR
PMRFinalizeLocalMem(PMR_IMPL_PRIVDATA pvPriv
				 )
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;

	psLMAllocArrayData = pvPriv;

	/*  We can't free pages until now. */
	if (psLMAllocArrayData->bHasLMPages)
	{
		eError = _FreeLMPages(psLMAllocArrayData,NULL,0);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}

	eError = _FreeLMPageArray(psLMAllocArrayData);
	PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */

	return PVRSRV_OK;
}

/* callback function for locking the system physical page addresses.
   As we are LMA there is nothing to do as we control physical memory. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesLocalMem(PMR_IMPL_PRIVDATA pvPriv)
{

    PVRSRV_ERROR eError;
    PMR_LMALLOCARRAY_DATA *psLMAllocArrayData;

    psLMAllocArrayData = pvPriv;

    if (psLMAllocArrayData->bOnDemand)
    {
		/* Allocate Memory for deferred allocation */
		eError = _AllocLMPages(psLMAllocArrayData, NULL);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

	return PVRSRV_OK;

}

static PVRSRV_ERROR
PMRUnlockSysPhysAddressesLocalMem(PMR_IMPL_PRIVDATA pvPriv
							   )
{
    PVRSRV_ERROR eError = PVRSRV_OK;
    PMR_LMALLOCARRAY_DATA *psLMAllocArrayData;

    psLMAllocArrayData = pvPriv;

	if (psLMAllocArrayData->bOnDemand)
    {
		/* Free Memory for deferred allocation */
		eError = _FreeLMPages(psLMAllocArrayData, NULL, 0);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

	PVR_ASSERT(eError == PVRSRV_OK);
	return eError;
}

/* N.B.  It is assumed that PMRLockSysPhysAddressesLocalMem() is called _before_ this function! */
static PVRSRV_ERROR
PMRSysPhysAddrLocalMem(PMR_IMPL_PRIVDATA pvPriv,
					   IMG_UINT32 ui32Log2PageSize,
					   IMG_UINT32 ui32NumOfPages,
					   IMG_DEVMEM_OFFSET_T *puiOffset,
					   IMG_BOOL *pbValid,
					   IMG_DEV_PHYADDR *psDevPAddr)
{
	IMG_UINT32 idx;
	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiNumAllocs;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = pvPriv;

	if (psLMAllocArrayData->uiLog2AllocSize < ui32Log2PageSize)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Requested physical addresses from PMR "
		         "for incompatible contiguity %u!",
		         __FUNCTION__,
		         ui32Log2PageSize));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	uiNumAllocs = psLMAllocArrayData->uiTotalNumPages;
	if (uiNumAllocs > 1)
	{
		PVR_ASSERT(psLMAllocArrayData->uiLog2AllocSize != 0);
		uiLog2AllocSize = psLMAllocArrayData->uiLog2AllocSize;

		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				uiAllocIndex = puiOffset[idx] >> uiLog2AllocSize;
				uiInAllocOffset = puiOffset[idx] - (uiAllocIndex << uiLog2AllocSize);

				PVR_ASSERT(uiAllocIndex < uiNumAllocs);
				PVR_ASSERT(uiInAllocOffset < (1ULL << uiLog2AllocSize));

				psDevPAddr[idx].uiAddr = psLMAllocArrayData->pasDevPAddr[uiAllocIndex].uiAddr + uiInAllocOffset;
			}
		}
	}
	else
	{
		for (idx=0; idx < ui32NumOfPages; idx++)
		{
			if (pbValid[idx])
			{
				psDevPAddr[idx].uiAddr = psLMAllocArrayData->pasDevPAddr[0].uiAddr + puiOffset[idx];
			}
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
								 size_t uiOffset,
								 size_t uiSize,
								 void **ppvKernelAddressOut,
								 IMG_HANDLE *phHandleOut,
								 PMR_FLAGS_T ulFlags)
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	void *pvKernLinAddr = NULL;
	IMG_UINT32 ui32PageIndex = 0;
	size_t uiOffsetMask = uiOffset;

	psLMAllocArrayData = pvPriv;

	/* Check that we can map this in contiguously */
	if (psLMAllocArrayData->uiTotalNumPages != 1)
	{
		size_t uiStart = uiOffset;
		size_t uiEnd = uiOffset + uiSize - 1;
		size_t uiPageMask = ~((1 << psLMAllocArrayData->uiLog2AllocSize) - 1);

		/* We can still map if only one page is required */
		if ((uiStart & uiPageMask) != (uiEnd & uiPageMask))
		{
			eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
			goto e0;
		}

		/* Locate the desired physical page to map in */
		ui32PageIndex = uiOffset >> psLMAllocArrayData->uiLog2AllocSize;
		uiOffsetMask = (1U << psLMAllocArrayData->uiLog2AllocSize) - 1;
	}

	PVR_ASSERT(ui32PageIndex < psLMAllocArrayData->uiTotalNumPages);

	eError = _MapAlloc(psLMAllocArrayData->psDevNode,
						&psLMAllocArrayData->pasDevPAddr[ui32PageIndex],
						psLMAllocArrayData->uiAllocSize,
						psLMAllocArrayData->bFwLocalAlloc,
						ulFlags,
						&pvKernLinAddr);

	*ppvKernelAddressOut = ((IMG_CHAR *) pvKernLinAddr) + (uiOffset & uiOffsetMask);
	*phHandleOut = pvKernLinAddr;

	return eError;

	/*
	  error exit paths follow
	*/

 e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static void PMRReleaseKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
												 IMG_HANDLE hHandle)
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	void *pvKernLinAddr = NULL;

	psLMAllocArrayData = (PMR_LMALLOCARRAY_DATA *) pvPriv;
	pvKernLinAddr = (void *) hHandle;

	_UnMapAlloc(psLMAllocArrayData->psDevNode,
				psLMAllocArrayData->uiAllocSize,
				psLMAllocArrayData->bFwLocalAlloc, 
				0,
				pvKernLinAddr);
}


static PVRSRV_ERROR
CopyBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  size_t uiBufSz,
				  size_t *puiNumBytes,
				  void (*pfnCopyBytes)(IMG_UINT8 *pcBuffer,
									   IMG_UINT8 *pcPMR,
									   size_t uiSize))
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = NULL;
	size_t uiBytesCopied;
	size_t uiBytesToCopy;
	size_t uiBytesCopyableFromAlloc;
	void *pvMapping = NULL;
	IMG_UINT8 *pcKernelPointer = NULL;
	size_t uiBufferOffset;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	PVRSRV_ERROR eError;

	psLMAllocArrayData = pvPriv;

	uiBytesCopied = 0;
	uiBytesToCopy = uiBufSz;
	uiBufferOffset = 0;

	if (psLMAllocArrayData->uiTotalNumPages > 1)
	{
		while (uiBytesToCopy > 0)
		{
			/* we have to map one alloc in at a time */
			PVR_ASSERT(psLMAllocArrayData->uiLog2AllocSize != 0);
			uiAllocIndex = uiOffset >> psLMAllocArrayData->uiLog2AllocSize;
			uiInAllocOffset = uiOffset - (uiAllocIndex << psLMAllocArrayData->uiLog2AllocSize);
			uiBytesCopyableFromAlloc = uiBytesToCopy;
			if (uiBytesCopyableFromAlloc + uiInAllocOffset > (1ULL << psLMAllocArrayData->uiLog2AllocSize))
			{
				uiBytesCopyableFromAlloc = TRUNCATE_64BITS_TO_SIZE_T((1ULL << psLMAllocArrayData->uiLog2AllocSize)-uiInAllocOffset);
			}

			PVR_ASSERT(uiBytesCopyableFromAlloc != 0);
			PVR_ASSERT(uiAllocIndex < psLMAllocArrayData->uiTotalNumPages);
			PVR_ASSERT(uiInAllocOffset < (1ULL << psLMAllocArrayData->uiLog2AllocSize));

			eError = _MapAlloc(psLMAllocArrayData->psDevNode,
								&psLMAllocArrayData->pasDevPAddr[uiAllocIndex],
								psLMAllocArrayData->uiAllocSize,
								psLMAllocArrayData->bFwLocalAlloc,
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
								&pvMapping);
			if (eError != PVRSRV_OK)
			{
				goto e0;
			}
			pcKernelPointer = pvMapping;
			pfnCopyBytes(&pcBuffer[uiBufferOffset], &pcKernelPointer[uiInAllocOffset], uiBytesCopyableFromAlloc);

			_UnMapAlloc(psLMAllocArrayData->psDevNode, 
						psLMAllocArrayData->uiAllocSize,
						psLMAllocArrayData->bFwLocalAlloc,
						0,
						pvMapping);

			uiBufferOffset += uiBytesCopyableFromAlloc;
			uiBytesToCopy -= uiBytesCopyableFromAlloc;
			uiOffset += uiBytesCopyableFromAlloc;
			uiBytesCopied += uiBytesCopyableFromAlloc;
		}
	}
	else
	{
			PVR_ASSERT((uiOffset + uiBufSz) <= psLMAllocArrayData->uiAllocSize);
			PVR_ASSERT(psLMAllocArrayData->uiAllocSize != 0);
			eError = _MapAlloc(psLMAllocArrayData->psDevNode,
								&psLMAllocArrayData->pasDevPAddr[0],
								psLMAllocArrayData->uiAllocSize,
								psLMAllocArrayData->bFwLocalAlloc,
								PVRSRV_MEMALLOCFLAG_CPU_UNCACHED,
								&pvMapping);
			if (eError != PVRSRV_OK)
			{
				goto e0;
			}
			pcKernelPointer = pvMapping;
			pfnCopyBytes(pcBuffer, &pcKernelPointer[uiOffset], uiBufSz);

			_UnMapAlloc(psLMAllocArrayData->psDevNode, 
						psLMAllocArrayData->uiAllocSize,
						psLMAllocArrayData->bFwLocalAlloc, 
						0,
						pvMapping);
			
			uiBytesCopied = uiBufSz;
	}
	*puiNumBytes = uiBytesCopied;
	return PVRSRV_OK;
e0:
	*puiNumBytes = uiBytesCopied;
	return eError;
}

static void ReadLocalMem(IMG_UINT8 *pcBuffer,
						 IMG_UINT8 *pcPMR,
						 size_t uiSize)
{
	/* NOTE: 'CachedMemCopy' means the operating system default memcpy, which
	 *       we *assume* in the LMA code will be faster, and doesn't need to
	 *       worry about ARM64.
	 */
	OSCachedMemCopy(pcBuffer, pcPMR, uiSize);
}

static PVRSRV_ERROR
PMRReadBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  size_t uiBufSz,
				  size_t *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 ReadLocalMem);
}

static void WriteLocalMem(IMG_UINT8 *pcBuffer,
						  IMG_UINT8 *pcPMR,
						  size_t uiSize)
{
	/* NOTE: 'CachedMemCopy' means the operating system default memcpy, which
	 *       we *assume* in the LMA code will be faster, and doesn't need to
	 *       worry about ARM64.
	 */
	OSCachedMemCopy(pcPMR, pcBuffer, uiSize);
}

static PVRSRV_ERROR
PMRWriteBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
					  IMG_DEVMEM_OFFSET_T uiOffset,
					  IMG_UINT8 *pcBuffer,
					  size_t uiBufSz,
					  size_t *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 WriteLocalMem);
}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemLocalMem
@Description    This function Changes the sparse mapping by allocating & freeing
				of pages. It does also change the GPU maps accordingly
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static PVRSRV_ERROR
PMRChangeSparseMemLocalMem(PMR_IMPL_PRIVDATA pPriv,
                           const PMR *psPMR,
                           IMG_UINT32 ui32AllocPageCount,
                           IMG_UINT32 *pai32AllocIndices,
                           IMG_UINT32 ui32FreePageCount,
                           IMG_UINT32 *pai32FreeIndices,
                           IMG_UINT32 uiFlags)
{
	PVRSRV_ERROR eError = PVRSRV_ERROR_INVALID_PARAMS;

	IMG_UINT32 ui32AdtnlAllocPages = 0;
	IMG_UINT32 ui32AdtnlFreePages = 0;
	IMG_UINT32 ui32CommonRequstCount = 0;
	IMG_UINT32 ui32Loop = 0;
	IMG_UINT32 ui32Index = 0;
	IMG_UINT32 uiAllocpgidx;
	IMG_UINT32 uiFreepgidx;

	PMR_LMALLOCARRAY_DATA *psPMRPageArrayData = (PMR_LMALLOCARRAY_DATA *)pPriv;
	IMG_DEV_PHYADDR sPhyAddr;

#if defined(DEBUG)
	IMG_BOOL bPoisonFail = IMG_FALSE;
	IMG_BOOL bZeroFail = IMG_FALSE;
#endif

	/* Fetch the Page table array represented by the PMR */
	IMG_DEV_PHYADDR *psPageArray = psPMRPageArrayData->pasDevPAddr;
	PMR_MAPPING_TABLE *psPMRMapTable = PMR_GetMappigTable(psPMR);

	/* The incoming request is classified into two operations independent of
	 * each other: alloc & free pages.
	 * These operations can be combined with two mapping operations as well
	 * which are GPU & CPU space mappings.
	 *
	 * From the alloc and free page requests, the net amount of pages to be
	 * allocated or freed is computed. Pages that were requested to be freed
	 * will be reused to fulfil alloc requests.
	 *
	 * The order of operations is:
	 * 1. Allocate new pages from the OS
	 * 2. Move the free pages from free request to alloc positions.
	 * 3. Free the rest of the pages not used for alloc
	 *
	 * Alloc parameters are validated at the time of allocation
	 * and any error will be handled then. */

	if (SPARSE_RESIZE_BOTH == (uiFlags & SPARSE_RESIZE_BOTH))
	{
		ui32CommonRequstCount = (ui32AllocPageCount > ui32FreePageCount) ?
				ui32FreePageCount : ui32AllocPageCount;

		PDUMP_PANIC(SPARSEMEM_SWAP, "Request to swap alloc & free pages not supported");
	}

	if (SPARSE_RESIZE_ALLOC == (uiFlags & SPARSE_RESIZE_ALLOC))
	{
		ui32AdtnlAllocPages = ui32AllocPageCount - ui32CommonRequstCount;
	}
	else
	{
		ui32AllocPageCount = 0;
	}

	if (SPARSE_RESIZE_FREE == (uiFlags & SPARSE_RESIZE_FREE))
	{
		ui32AdtnlFreePages = ui32FreePageCount - ui32CommonRequstCount;
	}
	else
	{
		ui32FreePageCount = 0;
	}

	if (0 == (ui32CommonRequstCount || ui32AdtnlAllocPages || ui32AdtnlFreePages))
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		return eError;
	}

	{
		/* Validate the free page indices */
		if (ui32FreePageCount)
		{
			if (NULL != pai32FreeIndices)
			{
				for (ui32Loop = 0; ui32Loop < ui32FreePageCount; ui32Loop++)
				{
					uiFreepgidx = pai32FreeIndices[ui32Loop];

					if (uiFreepgidx > psPMRPageArrayData->uiTotalNumPages)
					{
						eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
						goto e0;
					}

					if (INVALID_PAGE == psPageArray[uiFreepgidx].uiAddr)
					{
						eError = PVRSRV_ERROR_INVALID_PARAMS;
						goto e0;
					}
				}
			}else{
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				return eError;
			}
		}

		/*The following block of code verifies any issues with common alloc page indices */
		for (ui32Loop = ui32AdtnlAllocPages; ui32Loop < ui32AllocPageCount; ui32Loop++)
		{
			uiAllocpgidx = pai32AllocIndices[ui32Loop];
			if (uiAllocpgidx > psPMRPageArrayData->uiTotalNumPages)
			{
				eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
				goto e0;
			}

			if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
			{
				if ((INVALID_PAGE != psPageArray[uiAllocpgidx].uiAddr) ||
						(TRANSLATION_INVALID != psPMRMapTable->aui32Translation[uiAllocpgidx]))
				{
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto e0;
				}
			}
			else
			{
				if ((INVALID_PAGE ==  psPageArray[uiAllocpgidx].uiAddr) ||
				    (TRANSLATION_INVALID == psPMRMapTable->aui32Translation[uiAllocpgidx]))
				{
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto e0;
				}
			}
		}


		ui32Loop = 0;

		/* Allocate new pages */
		if (0 != ui32AdtnlAllocPages)
		{
			/* Say how many pages to allocate */
			psPMRPageArrayData->uiPagesToAlloc = ui32AdtnlAllocPages;

			eError = _AllocLMPages(psPMRPageArrayData, pai32AllocIndices);
			if (PVRSRV_OK != eError)
			{
				PVR_DPF((PVR_DBG_ERROR,
				         "%s: New Addtl Allocation of pages failed",
				         __FUNCTION__));
				goto e0;
			}

			/* Mark the corresponding pages of translation table as valid */
			for (ui32Loop = 0; ui32Loop < ui32AdtnlAllocPages; ui32Loop++)
			{
				psPMRMapTable->aui32Translation[pai32AllocIndices[ui32Loop]] = pai32AllocIndices[ui32Loop];
			}
		}

		ui32Index = ui32Loop;

		/* Move the corresponding free pages to alloc request */
		for (ui32Loop = 0; ui32Loop < ui32CommonRequstCount; ui32Loop++, ui32Index++)
		{

			uiAllocpgidx = pai32AllocIndices[ui32Index];
			uiFreepgidx =  pai32FreeIndices[ui32Loop];
			sPhyAddr = psPageArray[uiAllocpgidx];
			psPageArray[uiAllocpgidx] = psPageArray[uiFreepgidx];

			/* Is remap mem used in real world scenario? Should it be turned to a
			 *  debug feature? The condition check needs to be out of loop, will be
			 *  done at later point though after some analysis */
			if (SPARSE_REMAP_MEM != (uiFlags & SPARSE_REMAP_MEM))
			{
				psPMRMapTable->aui32Translation[uiFreepgidx] = TRANSLATION_INVALID;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
				psPageArray[uiFreepgidx].uiAddr = INVALID_PAGE;
			}
			else
			{
				psPageArray[uiFreepgidx] = sPhyAddr;
				psPMRMapTable->aui32Translation[uiFreepgidx] = uiFreepgidx;
				psPMRMapTable->aui32Translation[uiAllocpgidx] = uiAllocpgidx;
			}

			/* Be sure to honour the attributes associated with the allocation
			 * such as zeroing, poisoning etc. */
			if (psPMRPageArrayData->bPoisonOnAlloc)
			{
				eError = _PoisonAlloc(psPMRPageArrayData->psDevNode,
				                      &psPMRPageArrayData->pasDevPAddr[uiAllocpgidx],
				                      psPMRPageArrayData->bFwLocalAlloc,
				                      psPMRPageArrayData->uiAllocSize,
				                      _AllocPoison,
				                      _AllocPoisonSize);

				/* Consider this as a soft failure and go ahead but log error to kernel log */
				if (eError != PVRSRV_OK)
				{
#if defined(DEBUG)
					bPoisonFail = IMG_TRUE;
#endif
				}
			}
			else
			{
				if (psPMRPageArrayData->bZeroOnAlloc)
				{
					eError = _ZeroAlloc(psPMRPageArrayData->psDevNode,
					                    &psPMRPageArrayData->pasDevPAddr[uiAllocpgidx],
					                    psPMRPageArrayData->bFwLocalAlloc,
					                    psPMRPageArrayData->uiAllocSize);
					/* Consider this as a soft failure and go ahead but log error to kernel log */
					if (eError != PVRSRV_OK)
					{
#if defined(DEBUG)
						/*Don't think we need to zero  any pages further*/
						bZeroFail = IMG_TRUE;
#endif
					}
				}
			}
		}

		/*Free the additional free pages */
		if (0 != ui32AdtnlFreePages)
		{
			ui32Index = ui32Loop;
			_FreeLMPages(psPMRPageArrayData, &pai32FreeIndices[ui32Loop], ui32AdtnlFreePages);
			ui32Loop = 0;

			while(ui32Loop++ < ui32AdtnlFreePages)
			{
				/*Set the corresponding mapping table entry to invalid address */
				psPMRMapTable->aui32Translation[pai32FreeIndices[ui32Index++]] = TRANSLATION_INVALID;
			}
		}

	}

#if defined(DEBUG)
	if(IMG_TRUE == bPoisonFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in poisoning the page", __FUNCTION__));
	}

	if(IMG_TRUE == bZeroFail)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error in zeroing the page", __FUNCTION__));
	}
#endif

	/* Update the PMR memory holding information */
	eError = PVRSRV_OK;

e0:
		return eError;

}

/*************************************************************************/ /*!
@Function       PMRChangeSparseMemCPUMapLocalMem
@Description    This function Changes CPU maps accordingly
@Return         PVRSRV_ERROR failure code
*/ /**************************************************************************/
static
PVRSRV_ERROR PMRChangeSparseMemCPUMapLocalMem(PMR_IMPL_PRIVDATA pPriv,
                                              const PMR *psPMR,
                                              IMG_UINT64 sCpuVAddrBase,
                                              IMG_UINT32 ui32AllocPageCount,
                                              IMG_UINT32 *pai32AllocIndices,
                                              IMG_UINT32 ui32FreePageCount,
                                              IMG_UINT32 *pai32FreeIndices)
{
	IMG_DEV_PHYADDR *psPageArray;
	PMR_LMALLOCARRAY_DATA *psPMRPageArrayData = (PMR_LMALLOCARRAY_DATA *)pPriv;
	uintptr_t sCpuVABase = sCpuVAddrBase;
	IMG_CPU_PHYADDR sCpuAddrPtr;
	IMG_BOOL bValid;

	/*Get the base address of the heap */
	PMR_CpuPhysAddr(psPMR,
	                psPMRPageArrayData->uiLog2AllocSize,
	                1,
	                0,	/* offset zero here mean first page in the PMR */
	                &sCpuAddrPtr,
	                &bValid);

	/* Phys address of heap is computed here by subtracting the offset of this page
	 * basically phys address of any page = Base address of heap + offset of the page */
	sCpuAddrPtr.uiAddr -= psPMRPageArrayData->pasDevPAddr[0].uiAddr;
	psPageArray = psPMRPageArrayData->pasDevPAddr;

	return OSChangeSparseMemCPUAddrMap((void **)psPageArray,
	                                   sCpuVABase,
	                                   sCpuAddrPtr,
	                                   ui32AllocPageCount,
	                                   pai32AllocIndices,
	                                   ui32FreePageCount,
	                                   pai32FreeIndices,
	                                   IMG_TRUE);
}


static PMR_IMPL_FUNCTAB _sPMRLMAFuncTab = {
	/* pfnLockPhysAddresses */
	&PMRLockSysPhysAddressesLocalMem,
	/* pfnUnlockPhysAddresses */
	&PMRUnlockSysPhysAddressesLocalMem,
	/* pfnDevPhysAddr */
	&PMRSysPhysAddrLocalMem,
	/* pfnAcquireKernelMappingData */
	&PMRAcquireKernelMappingDataLocalMem,
	/* pfnReleaseKernelMappingData */
	&PMRReleaseKernelMappingDataLocalMem,
#if defined(INTEGRITY_OS)
	/* pfnMapMemoryObject */
    NULL,
	/* pfnUnmapMemoryObject */
    NULL,
#endif
	/* pfnReadBytes */
	&PMRReadBytesLocalMem,
	/* pfnWriteBytes */
	&PMRWriteBytesLocalMem,
	/* .pfnUnpinMem */
	NULL,
	/* .pfnPinMem */
	NULL,
	/* pfnChangeSparseMem*/
	&PMRChangeSparseMemLocalMem,
	/* pfnChangeSparseMemCPUMap */
	&PMRChangeSparseMemCPUMapLocalMem,
	/* pfnMMap */
	NULL,
	/* pfnFinalize */
	&PMRFinalizeLocalMem
};

PVRSRV_ERROR
PhysmemNewLocalRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_DEVMEM_SIZE_T uiChunkSize,
							IMG_UINT32 ui32NumPhysChunks,
							IMG_UINT32 ui32NumVirtChunks,
							IMG_UINT32 *pui32MappingTable,
							IMG_UINT32 uiLog2PageSize,
							PVRSRV_MEMALLOCFLAGS_T uiFlags,
							const IMG_CHAR *pszAnnotation,
							PMR **ppsPMRPtr)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	PMR *psPMR = NULL;
	PMR_LMALLOCARRAY_DATA *psPrivData = NULL;
	PMR_FLAGS_T uiPMRFlags;
	PHYS_HEAP *psPhysHeap;
	IMG_BOOL bZero;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bOnDemand;
	IMG_BOOL bContig;
	IMG_BOOL bFwLocalAlloc;
	IMG_BOOL bCpuLocalAlloc;

	if (PVRSRV_CHECK_KERNEL_CPU_MAPPABLE(uiFlags) &&
		(ui32NumPhysChunks == ui32NumVirtChunks))
	{
		bContig = IMG_TRUE;
	}
	else
	{
		bContig = IMG_FALSE;
	}

	bOnDemand = PVRSRV_CHECK_ON_DEMAND(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bFwLocalAlloc = PVRSRV_CHECK_FW_LOCAL(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bCpuLocalAlloc = PVRSRV_CHECK_CPU_LOCAL(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bZero = PVRSRV_CHECK_ZERO_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bPoisonOnAlloc = PVRSRV_CHECK_POISON_ON_ALLOC(uiFlags) ? IMG_TRUE : IMG_FALSE;
	bPoisonOnFree = PVRSRV_CHECK_POISON_ON_FREE(uiFlags) ? IMG_TRUE : IMG_FALSE;

	if (bZero && bPoisonOnAlloc)
	{
		/* Zero on Alloc and Poison on Alloc are mutually exclusive */
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errorOnParam;
	}

	/* Silently round up alignment/pagesize if request was less that
	   PAGE_SHIFT, because it would never be harmful for memory to be
	   _more_ contiguous that was desired */

	uiLog2PageSize = OSGetPageShift() > uiLog2PageSize
		? OSGetPageShift()
		: uiLog2PageSize;

	/* In case we have a non-sparse allocation tolerate bad requests and round up.
	 * For sparse allocations the users have to make sure to meet the right
	 * requirements. */
	if (ui32NumPhysChunks == ui32NumVirtChunks &&
		ui32NumVirtChunks == 1)
	{
		/* Round up allocation size to at least a full OSGetPageSize() */
		uiSize = PVR_ALIGN(uiSize, OSGetPageSize());
		uiChunkSize = uiSize;
	}

	if (bFwLocalAlloc)
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_FW_LOCAL];
	}
	else if (bCpuLocalAlloc)
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_CPU_LOCAL];
	}
	else
	{
		psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL];
	}

	/* Create Array structure that holds the physical pages */
	eError = _AllocLMPageArray(psDevNode,
	                           uiChunkSize * ui32NumVirtChunks,
	                           uiChunkSize,
	                           ui32NumPhysChunks,
	                           ui32NumVirtChunks,
	                           pui32MappingTable,
	                           uiLog2PageSize,
	                           bZero,
	                           bPoisonOnAlloc,
	                           bPoisonOnFree,
	                           bContig,
	                           bOnDemand,
	                           bFwLocalAlloc,
	                           psPhysHeap,
	                           uiFlags,
	                           &psPrivData);
	if (eError != PVRSRV_OK)
	{
		goto errorOnAllocPageArray;
	}


	if (!bOnDemand)
	{
		/* Allocate the physical pages */
		eError = _AllocLMPages(psPrivData,pui32MappingTable);
		if (eError != PVRSRV_OK)
		{
			goto errorOnAllocPages;
		}
	}

	/* In this instance, we simply pass flags straight through.

	   Generically, uiFlags can include things that control the PMR
	   factory, but we don't need any such thing (at the time of
	   writing!), and our caller specifies all PMR flags so we don't
	   need to meddle with what was given to us.
	*/
	uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);
	/* check no significant bits were lost in cast due to different
	   bit widths for flags */
	PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

    if (bOnDemand)
    {
    	PDUMPCOMMENT("Deferred Allocation PMR (LMA)");
    }


	eError = PMRCreatePMR(psDevNode,
						  psPhysHeap,
						  uiSize,
                          uiChunkSize,
                          ui32NumPhysChunks,
                          ui32NumVirtChunks,
                          pui32MappingTable,
						  uiLog2PageSize,
						  uiPMRFlags,
						  pszAnnotation,
						  &_sPMRLMAFuncTab,
						  psPrivData,
						  PMR_TYPE_LMA,
						  &psPMR,
						  IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PhysmemNewLocalRamBackedPMR: Unable to create PMR (status=%d)", eError));
		goto errorOnCreate;
	}

	*ppsPMRPtr = psPMR;
	return PVRSRV_OK;

errorOnCreate:
	if(!bOnDemand && psPrivData->bHasLMPages)
	{
		eError2 = _FreeLMPages(psPrivData, NULL,0);
		PVR_ASSERT(eError2 == PVRSRV_OK);
	}

errorOnAllocPages:
	eError2 = _FreeLMPageArray(psPrivData);
	PVR_ASSERT(eError2 == PVRSRV_OK);

errorOnAllocPageArray:
errorOnParam:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

#if defined(SUPPORT_GPUVIRT_VALIDATION)

struct PidOSidCouplingList
{
	IMG_PID     pId;
	IMG_UINT32  ui32OSid;
	IMG_UINT32	ui32OSidReg;
    IMG_BOOL    bOSidAxiProt;

	struct PidOSidCouplingList *psNext;
};
typedef struct PidOSidCouplingList PidOSidCouplingList;

static PidOSidCouplingList *psPidOSidHead=NULL;
static PidOSidCouplingList *psPidOSidTail=NULL;

void InsertPidOSidsCoupling(IMG_PID pId, IMG_UINT32 ui32OSid, IMG_UINT32 ui32OSidReg, IMG_BOOL bOSidAxiProt)
{
	PidOSidCouplingList *psTmp;

    PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Inserting (PID/ OSid/ OSidReg/ IsSecure) (%d/ %d/ %d/ %s) into list",
                 pId,ui32OSid, ui32OSidReg, (bOSidAxiProt)?"Yes":"No"));

	psTmp=OSAllocMem(sizeof(PidOSidCouplingList));

	if (psTmp==NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"(GPU Virtualization Validation): Memory allocation failed. No list insertion => program will execute normally.\n"));
		return ;
	}

	psTmp->pId=pId;
	psTmp->ui32OSid=ui32OSid;
	psTmp->ui32OSidReg=ui32OSidReg;
    psTmp->bOSidAxiProt = bOSidAxiProt;

	psTmp->psNext=NULL;
	if (psPidOSidHead==NULL)
	{
		psPidOSidHead=psTmp;
		psPidOSidTail=psTmp;
	}
	else
	{
		psPidOSidTail->psNext=psTmp;
		psPidOSidTail=psTmp;
	}

	return ;
}

void RetrieveOSidsfromPidList(IMG_PID pId, IMG_UINT32 *pui32OSid, IMG_UINT32 *pui32OSidReg, IMG_BOOL *pbOSidAxiProt)
{
	PidOSidCouplingList *psTmp;

	for (psTmp=psPidOSidHead;psTmp!=NULL;psTmp=psTmp->psNext)
	{
		if (psTmp->pId==pId)
		{
            (*pui32OSid)     = psTmp->ui32OSid;
            (*pui32OSidReg)  = psTmp->ui32OSidReg;
            (*pbOSidAxiProt) = psTmp->bOSidAxiProt;

			return ;
		}
	}

	(*pui32OSid)=0;
	(*pui32OSidReg)=0;
    (*pbOSidAxiProt) = IMG_FALSE;

	return ;
}

void RemovePidOSidCoupling(IMG_PID pId)
{
	PidOSidCouplingList *psTmp, *psPrev=NULL;

	for (psTmp=psPidOSidHead; psTmp!=NULL; psTmp=psTmp->psNext)
	{
		if (psTmp->pId==pId) break;
		psPrev=psTmp;
	}

	if (psTmp==NULL)
	{
		return ;
	}

	PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Deleting Pairing %d / (%d - %d) from list",psTmp->pId, psTmp->ui32OSid, psTmp->ui32OSidReg));

	if (psTmp==psPidOSidHead)
	{
		if (psPidOSidHead->psNext==NULL)
		{
			psPidOSidHead=NULL;
			psPidOSidTail=NULL;
			OSFreeMem(psTmp);

			return ;
		}

		psPidOSidHead=psPidOSidHead->psNext;
		OSFreeMem(psTmp);
		return ;
	}

	if (psPrev==NULL) return ;

	psPrev->psNext=psTmp->psNext;
	if (psTmp==psPidOSidTail)
	{
		psPidOSidTail=psPrev;
	}

	OSFreeMem(psTmp);

	return ;
}

#endif

