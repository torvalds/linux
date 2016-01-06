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

#include "allocmem.h"
#include "osfunc.h"
#include "pvrsrv.h"
#include "physmem_lma.h"
#include "pdump_physmem.h"
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
	IMG_UINT32 uiNumAllocs;
	IMG_UINT32 uiLog2AllocSize;
	IMG_UINT32 uiAllocSize;
	IMG_DEV_PHYADDR *pasDevPAddr;

	IMG_BOOL bZeroOnAlloc;
	IMG_BOOL bPoisonOnAlloc;

	/* Tells if allocation is physically backed */
	IMG_BOOL bHasLMPages;
	IMG_BOOL bOnDemand;

	/*
	  for pdump...
	*/
	IMG_BOOL bPDumpMalloced;
	IMG_HANDLE hPDumpAllocInfo;

	/*
	  record at alloc time whether poisoning will be required when the
	  PMR is freed.
	*/
	IMG_BOOL bPoisonOnFree;
} PMR_LMALLOCARRAY_DATA;

static PVRSRV_ERROR _MapAlloc(PVRSRV_DEVICE_NODE *psDevNode, IMG_DEV_PHYADDR *psDevPAddr,
								IMG_SIZE_T uiSize, IMG_VOID **pvPtr, PMR_FLAGS_T ulFlags)
{
	IMG_CPU_PHYADDR sCpuPAddr;

	PhysHeapDevPAddrToCpuPAddr(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL], 1, &sCpuPAddr, psDevPAddr);
	*pvPtr = OSMapPhysToLin(sCpuPAddr,
							uiSize,
							ulFlags);

	if (*pvPtr == IMG_NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	else
	{
		return PVRSRV_OK;
	}
}

static IMG_VOID _UnMapAlloc(PVRSRV_DEVICE_NODE *psDevNode, IMG_SIZE_T uiSize, IMG_VOID *pvPtr)
{
	OSUnMapPhysToLin(pvPtr, uiSize, 0);
}

static PVRSRV_ERROR
_PoisonAlloc(PVRSRV_DEVICE_NODE *psDevNode,
			 IMG_DEV_PHYADDR *psDevPAddr,
			 IMG_UINT32 uiAllocSize,
			 const IMG_CHAR *pacPoisonData,
			 IMG_SIZE_T uiPoisonSize)
{
	IMG_UINT32 uiSrcByteIndex;
	IMG_UINT32 uiDestByteIndex;
	IMG_VOID *pvKernLin = IMG_NULL;
	IMG_CHAR *pcDest = IMG_NULL;

	PVRSRV_ERROR eError;

	eError = _MapAlloc(psDevNode, psDevPAddr, uiAllocSize, &pvKernLin, 0);
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

	_UnMapAlloc(psDevNode, uiAllocSize, pvKernLin);
	return PVRSRV_OK;

map_failed:
	PVR_DPF((PVR_DBG_ERROR, "Failed to poison allocation"));
	return eError;
}

static PVRSRV_ERROR
_ZeroAlloc(PVRSRV_DEVICE_NODE *psDevNode,
		   IMG_DEV_PHYADDR *psDevPAddr,
		   IMG_UINT32 uiAllocSize)
{
	IMG_VOID *pvKernLin = IMG_NULL;
	PVRSRV_ERROR eError;

	eError = _MapAlloc(psDevNode, psDevPAddr, uiAllocSize, &pvKernLin, 0);
	if (eError != PVRSRV_OK)
	{
		goto map_failed;
	}

	OSMemSet(pvKernLin, 0, uiAllocSize);

	_UnMapAlloc(psDevNode, uiAllocSize, pvKernLin);
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
			  IMG_BOOL *pabMappingTable,
			  IMG_UINT32 uiLog2PageSize,
			  IMG_BOOL bZero,
			  IMG_BOOL bPoisonOnAlloc,
			  IMG_BOOL bPoisonOnFree,
			  IMG_BOOL bContig,
			  IMG_BOOL bOnDemand,
			  PMR_LMALLOCARRAY_DATA **ppsPageArrayDataPtr
			  )
{
	PMR_LMALLOCARRAY_DATA *psPageArrayData = IMG_NULL;
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

	psPageArrayData = OSAllocMem(sizeof(PMR_LMALLOCARRAY_DATA));
	if (psPageArrayData == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errorOnAllocArray;
	}
	OSMemSet(psPageArrayData, 0, sizeof(PMR_LMALLOCARRAY_DATA));

	if (bContig)
	{
		/*
			Some allocations require kernel mappings in which case in order
			to be virtually contiguous we also have to be physically contiguous.
		*/
		psPageArrayData->uiNumAllocs = 1;
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

		psPageArrayData->uiNumAllocs = uiNumPages;
		psPageArrayData->uiAllocSize = 1 << uiLog2PageSize;
		psPageArrayData->uiLog2AllocSize = uiLog2PageSize;
	}
	psPageArrayData->psDevNode = psDevNode;
	psPageArrayData->pasDevPAddr = OSAllocMem(sizeof(IMG_DEV_PHYADDR)*
												psPageArrayData->uiNumAllocs);
	if (psPageArrayData->pasDevPAddr == IMG_NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errorOnAllocAddr;
	}
	OSMemSet(psPageArrayData->pasDevPAddr, 0, sizeof(IMG_DEV_PHYADDR)*
												psPageArrayData->uiNumAllocs);

	/* N.B.  We have a window of opportunity where a failure in
	   createPMR the finalize function can be called before the PMR
	   MALLOC and thus the hPDumpAllocInfo won't be set.  So we have
	   to conditionally call the PDumpFree function. */
	psPageArrayData->bPDumpMalloced = IMG_FALSE;

	psPageArrayData->bZeroOnAlloc = bZero;
	psPageArrayData->bPoisonOnAlloc = bPoisonOnAlloc;
	psPageArrayData->bPoisonOnFree = bPoisonOnFree;
 	psPageArrayData->bHasLMPages = IMG_FALSE;
 	psPageArrayData->bOnDemand = bOnDemand;

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
_AllocLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayDataPtr)
{
	IMG_UINT32 uiAllocSize = psPageArrayDataPtr->uiAllocSize;
	IMG_UINT32 uiLog2AllocSize = psPageArrayDataPtr->uiLog2AllocSize;
	PVRSRV_DEVICE_NODE *psDevNode = psPageArrayDataPtr->psDevNode;
	IMG_BOOL bPoisonOnAlloc =  psPageArrayDataPtr->bPoisonOnAlloc;
	IMG_BOOL bZeroOnAlloc =  psPageArrayDataPtr->bZeroOnAlloc;
	PVRSRV_ERROR eError;
	IMG_BOOL bAllocResult;
	RA_BASE_T uiCardAddr;
	RA_LENGTH_T uiActualSize;
	IMG_UINT32 i;
	RA_ARENA *pArena=psDevNode->psLocalDevMemArena;

	PVR_ASSERT(!psPageArrayDataPtr->bHasLMPages);

	for(i=0;i<psPageArrayDataPtr->uiNumAllocs;i++)
	{
#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		IMG_UINT32  ui32OSid=0, ui32OSidReg=0;
		IMG_PID     pId;

		pId=OSGetCurrentProcessID();
		RetrieveOSidsfromPidList(pId, &ui32OSid, &ui32OSidReg);

		pArena=psDevNode->psOSidSubArena[ui32OSid];
		PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Giving from OS slot %d",ui32OSid));
}
#endif

		bAllocResult = RA_Alloc(pArena,
								uiAllocSize,
								0,                                      /* No flags */
								1ULL << uiLog2AllocSize,
								&uiCardAddr,
								&uiActualSize,
								IMG_NULL);                      /* No private handle */
#if defined(SUPPORT_GPUVIRT_VALIDATION)
{
		PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Address: %llu \n",uiCardAddr));
}
#endif

		if (!bAllocResult)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
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
									 IMG_NULL,
									 sLocalCpuPAddr,
									 uiActualSize,
									 IMG_NULL);
		}
#endif
#endif

		psPageArrayDataPtr->pasDevPAddr[i].uiAddr = uiCardAddr;

		if (bPoisonOnAlloc)
		{
			eError = _PoisonAlloc(psDevNode,
								  &psPageArrayDataPtr->pasDevPAddr[i],
								  uiAllocSize,
								  _AllocPoison,
								  _AllocPoisonSize);
			if (eError !=PVRSRV_OK)
			{
				goto errorOnPoison;
			}
		}

		if (bZeroOnAlloc)
		{
			eError = _ZeroAlloc(psDevNode,
								&psPageArrayDataPtr->pasDevPAddr[i],
								uiAllocSize);
			if (eError !=PVRSRV_OK)
			{
				goto errorOnZero;
			}
		}
	}

	psPageArrayDataPtr->bHasLMPages = IMG_TRUE;

	return PVRSRV_OK;

	/*
	  error exit paths follow:
	*/
errorOnZero:
errorOnPoison:
errorOnRAAlloc:
	while (i)
	{
		i--;
		RA_Free(psDevNode->psLocalDevMemArena,
				psPageArrayDataPtr->pasDevPAddr[i].uiAddr);
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
_FreeLMPages(PMR_LMALLOCARRAY_DATA *psPageArrayData)
{
	IMG_UINT32 uiAllocSize;
	IMG_UINT32 i;

	PVR_ASSERT(psPageArrayData->bHasLMPages);

	uiAllocSize = psPageArrayData->uiAllocSize;

	for (i = 0;i < psPageArrayData->uiNumAllocs;i++)
	{
		if (psPageArrayData->bPoisonOnFree)
		{
			_PoisonAlloc(psPageArrayData->psDevNode,
						 &psPageArrayData->pasDevPAddr[i],
						 uiAllocSize,
						 _FreePoison,
						 _FreePoisonSize);
		}
		RA_Free(psPageArrayData->psDevNode->psLocalDevMemArena,
				psPageArrayData->pasDevPAddr[i].uiAddr);

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		/* Allocation is done a page at a time */
		PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, uiAllocSize);
#else
        {
			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES, psPageArrayData->pasDevPAddr[i].uiAddr);
        }
#endif
#endif
	}

	psPageArrayData->bHasLMPages = IMG_FALSE;

	PVR_DPF((PVR_DBG_MESSAGE, "physmem_lma.c: freed local memory for PMR @0x%p", psPageArrayData));

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
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = IMG_NULL;

	psLMAllocArrayData = pvPriv;

	/* Conditionally do the PDump free, because if CreatePMR failed we
	   won't have done the PDump MALLOC.  */
	if (psLMAllocArrayData->bPDumpMalloced)
	{
		PDumpPMRFree(psLMAllocArrayData->hPDumpAllocInfo);
	}

	/*  We can't free pages until now. */
	if (psLMAllocArrayData->bHasLMPages)
	{
		eError = _FreeLMPages(psLMAllocArrayData);
		PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */
	}

	eError = _FreeLMPageArray(psLMAllocArrayData);
	PVR_ASSERT (eError == PVRSRV_OK); /* can we do better? */

	return PVRSRV_OK;
}

/* callback function for locking the system physical page addresses.
   As we are LMA there is nothing to do as we control physical memory. */
static PVRSRV_ERROR
PMRLockSysPhysAddressesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
							 IMG_UINT32 uiLog2DevPageSize)
{

    PVRSRV_ERROR eError;
    PMR_LMALLOCARRAY_DATA *psLMAllocArrayData;

    psLMAllocArrayData = pvPriv;

    if (psLMAllocArrayData->bOnDemand)
    {
		/* Allocate Memory for deferred allocation */
    	eError = _AllocLMPages(psLMAllocArrayData);
    	if (eError != PVRSRV_OK)
    	{
    		return eError;
    	}
    }

	PVR_UNREFERENCED_PARAMETER(uiLog2DevPageSize);

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
    	eError = _FreeLMPages(psLMAllocArrayData);
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

	uiNumAllocs = psLMAllocArrayData->uiNumAllocs;
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
								 IMG_SIZE_T uiOffset,
								 IMG_SIZE_T uiSize,
								 IMG_VOID **ppvKernelAddressOut,
								 IMG_HANDLE *phHandleOut,
								 PMR_FLAGS_T ulFlags)
{
	PVRSRV_ERROR eError;
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = IMG_NULL;
	IMG_VOID *pvKernLinAddr = IMG_NULL;
	IMG_UINT32 ui32PageIndex = 0;

	PVR_UNREFERENCED_PARAMETER(ulFlags);

	psLMAllocArrayData = pvPriv;

	/* Check that we can map this in contiguously */
	if (psLMAllocArrayData->uiNumAllocs != 1)
	{
		IMG_SIZE_T uiStart = uiOffset;
		IMG_SIZE_T uiEnd = uiOffset + uiSize - 1;
		IMG_SIZE_T uiPageMask = ~((1 << psLMAllocArrayData->uiLog2AllocSize) - 1);

		/* We can still map if only one page is required */
		if ((uiStart & uiPageMask) != (uiEnd & uiPageMask))
		{
			eError = PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
			goto e0;
		}

		/* Locate the desired physical page to map in */
		ui32PageIndex = uiOffset >> psLMAllocArrayData->uiLog2AllocSize;
	}

	PVR_ASSERT(ui32PageIndex < psLMAllocArrayData->uiNumAllocs);

	eError = _MapAlloc(psLMAllocArrayData->psDevNode,
						&psLMAllocArrayData->pasDevPAddr[ui32PageIndex],
						psLMAllocArrayData->uiAllocSize,
						&pvKernLinAddr, 
						ulFlags);

	*ppvKernelAddressOut = ((IMG_CHAR *) pvKernLinAddr) + (uiOffset & ((1U << psLMAllocArrayData->uiLog2AllocSize) - 1));
	*phHandleOut = pvKernLinAddr;

	return eError;

	/*
	  error exit paths follow
	*/

 e0:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static IMG_VOID PMRReleaseKernelMappingDataLocalMem(PMR_IMPL_PRIVDATA pvPriv,
												 IMG_HANDLE hHandle)
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = IMG_NULL;
	IMG_VOID *pvKernLinAddr = IMG_NULL;

	psLMAllocArrayData = (PMR_LMALLOCARRAY_DATA *) pvPriv;
	pvKernLinAddr = (IMG_VOID *) hHandle;

	_UnMapAlloc(psLMAllocArrayData->psDevNode,
				psLMAllocArrayData->uiAllocSize, pvKernLinAddr);
}


static PVRSRV_ERROR
CopyBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  IMG_SIZE_T uiBufSz,
				  IMG_SIZE_T *puiNumBytes,
				  IMG_VOID (*pfnCopyBytes)(IMG_UINT8 *pcBuffer,
										   IMG_UINT8 *pcPMR,
										   IMG_SIZE_T uiSize))
{
	PMR_LMALLOCARRAY_DATA *psLMAllocArrayData = IMG_NULL;
	IMG_SIZE_T uiBytesCopied;
	IMG_SIZE_T uiBytesToCopy;
	IMG_SIZE_T uiBytesCopyableFromAlloc;
	IMG_VOID *pvMapping = IMG_NULL;
	IMG_UINT8 *pcKernelPointer = IMG_NULL;
	IMG_SIZE_T uiBufferOffset;
	IMG_UINT64 uiAllocIndex;
	IMG_DEVMEM_OFFSET_T uiInAllocOffset;
	PVRSRV_ERROR eError;

	psLMAllocArrayData = pvPriv;

	uiBytesCopied = 0;
	uiBytesToCopy = uiBufSz;
	uiBufferOffset = 0;

	if (psLMAllocArrayData->uiNumAllocs > 1)
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
			PVR_ASSERT(uiAllocIndex < psLMAllocArrayData->uiNumAllocs);
			PVR_ASSERT(uiInAllocOffset < (1ULL << psLMAllocArrayData->uiLog2AllocSize));

			eError = _MapAlloc(psLMAllocArrayData->psDevNode,
								&psLMAllocArrayData->pasDevPAddr[uiAllocIndex],
								psLMAllocArrayData->uiAllocSize,
								&pvMapping, 0);
			if (eError != PVRSRV_OK)
			{
				goto e0;
			}
			pcKernelPointer = pvMapping;
			pfnCopyBytes(&pcBuffer[uiBufferOffset], &pcKernelPointer[uiInAllocOffset], uiBytesCopyableFromAlloc);
			_UnMapAlloc(psLMAllocArrayData->psDevNode, psLMAllocArrayData->uiAllocSize, pvMapping);
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
								&pvMapping, 0);
			if (eError != PVRSRV_OK)
			{
				goto e0;
			}
			pcKernelPointer = pvMapping;
			pfnCopyBytes(pcBuffer, &pcKernelPointer[uiOffset], uiBufSz);
			_UnMapAlloc(psLMAllocArrayData->psDevNode, psLMAllocArrayData->uiAllocSize, pvMapping);
			uiBytesCopied = uiBufSz;
	}
	*puiNumBytes = uiBytesCopied;
	return PVRSRV_OK;
e0:
	*puiNumBytes = uiBytesCopied;
	return eError;
}

static IMG_VOID ReadLocalMem(IMG_UINT8 *pcBuffer,
							 IMG_UINT8 *pcPMR,
							 IMG_SIZE_T uiSize)
{
	OSMemCopy(pcBuffer, pcPMR, uiSize);
}

static PVRSRV_ERROR
PMRReadBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
				  IMG_DEVMEM_OFFSET_T uiOffset,
				  IMG_UINT8 *pcBuffer,
				  IMG_SIZE_T uiBufSz,
				  IMG_SIZE_T *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 ReadLocalMem);
}

static IMG_VOID WriteLocalMem(IMG_UINT8 *pcBuffer,
							  IMG_UINT8 *pcPMR,
							  IMG_SIZE_T uiSize)
{
	OSMemCopy(pcPMR, pcBuffer, uiSize);
}

static PVRSRV_ERROR
PMRWriteBytesLocalMem(PMR_IMPL_PRIVDATA pvPriv,
					  IMG_DEVMEM_OFFSET_T uiOffset,
					  IMG_UINT8 *pcBuffer,
					  IMG_SIZE_T uiBufSz,
					  IMG_SIZE_T *puiNumBytes)
{
	return CopyBytesLocalMem(pvPriv,
							 uiOffset,
							 pcBuffer,
							 uiBufSz,
							 puiNumBytes,
							 WriteLocalMem);
}

static PMR_IMPL_FUNCTAB _sPMRLMAFuncTab = {
	/* pfnLockPhysAddresses */
	&PMRLockSysPhysAddressesLocalMem,
	/* pfnUnlockPhysAddresses */
	&PMRUnlockSysPhysAddressesLocalMem,
	/* pfnDevPhysAddr */
	&PMRSysPhysAddrLocalMem,
	/* pfnPDumpSymbolicAddr */
	IMG_NULL,
	/* pfnAcquireKernelMappingData */
	&PMRAcquireKernelMappingDataLocalMem,
	/* pfnReleaseKernelMappingData */
	&PMRReleaseKernelMappingDataLocalMem,
	/* pfnReadBytes */
	&PMRReadBytesLocalMem,
	/* pfnWriteBytes */
	&PMRWriteBytesLocalMem,
	/* pfnFinalize */
	&PMRFinalizeLocalMem
};

PVRSRV_ERROR
PhysmemNewLocalRamBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_DEVMEM_SIZE_T uiChunkSize,
							IMG_UINT32 ui32NumPhysChunks,
							IMG_UINT32 ui32NumVirtChunks,
							IMG_BOOL *pabMappingTable,
							IMG_UINT32 uiLog2PageSize,
							PVRSRV_MEMALLOCFLAGS_T uiFlags,
							PMR **ppsPMRPtr)
{
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eError2;
	PMR *psPMR = IMG_NULL;
	PMR_LMALLOCARRAY_DATA *psPrivData = IMG_NULL;
	IMG_HANDLE hPDumpAllocInfo = IMG_NULL;
	PMR_FLAGS_T uiPMRFlags;
	IMG_BOOL bZero;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bOnDemand = ((uiFlags & PVRSRV_MEMALLOCFLAG_NO_OSPAGES_ON_ALLOC) > 0);
	IMG_BOOL bContig;

	if (uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC)
	{
		bZero = IMG_TRUE;
	}
	else
	{
		bZero = IMG_FALSE;
	}

	if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC)
	{
		bPoisonOnAlloc = IMG_TRUE;
	}
	else
	{
		bPoisonOnAlloc = IMG_FALSE;
	}

	if (uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_FREE)
	{
		bPoisonOnFree = IMG_TRUE;
	}
	else
	{
		bPoisonOnFree = IMG_FALSE;
	}

	if (uiFlags & PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE)
	{
		bContig = IMG_TRUE;
	}
	else
	{
		bContig = IMG_FALSE;
	}

	if ((uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
		(uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
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

	/* Create Array structure that holds the physical pages */
	eError = _AllocLMPageArray(psDevNode,
						   uiChunkSize * ui32NumPhysChunks,
						   uiChunkSize,
                           ui32NumPhysChunks,
                           ui32NumVirtChunks,
                           pabMappingTable,
						   uiLog2PageSize,
						   bZero,
						   bPoisonOnAlloc,
						   bPoisonOnFree,
						   bContig,
						   bOnDemand,
						   &psPrivData);
	if (eError != PVRSRV_OK)
	{
		goto errorOnAllocPageArray;
	}


	if (!bOnDemand)
	{
		/* Allocate the physical pages */
		eError = _AllocLMPages(psPrivData);
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
	eError = PMRCreatePMR(psDevNode->apsPhysHeap[PVRSRV_DEVICE_PHYS_HEAP_GPU_LOCAL],
						  uiSize,
                          uiChunkSize,
                          ui32NumPhysChunks,
                          ui32NumVirtChunks,
                          pabMappingTable,
						  uiLog2PageSize,
						  uiPMRFlags,
						  "PMRLMA",
						  &_sPMRLMAFuncTab,
						  psPrivData,
						  &psPMR,
						  &hPDumpAllocInfo,
						  IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		goto errorOnCreate;
	}

	psPrivData->hPDumpAllocInfo = hPDumpAllocInfo;
	psPrivData->bPDumpMalloced = IMG_TRUE;

	*ppsPMRPtr = psPMR;
	return PVRSRV_OK;

errorOnCreate:
	if(!bOnDemand)
	{
		eError2 = _FreeLMPages(psPrivData);
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

	struct PidOSidCouplingList *psNext;
};
typedef struct PidOSidCouplingList PidOSidCouplingList;

static PidOSidCouplingList *psPidOSidHead=NULL;
static PidOSidCouplingList *psPidOSidTail=NULL;

IMG_VOID InsertPidOSidsCoupling(IMG_PID pId, IMG_UINT32 ui32OSid, IMG_UINT32 ui32OSidReg)
{
	PidOSidCouplingList *psTmp;

	PVR_DPF((PVR_DBG_MESSAGE,"(GPU Virtualization Validation): Inserting (PID/ OSid/ OSidReg) (%d/ %d/ %d) into list",pId,ui32OSid, ui32OSidReg));

	psTmp=OSAllocMem(sizeof(PidOSidCouplingList));

	if (psTmp==IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"(GPU Virtualization Validation): Memory allocation failed. No list insertion => program will execute normally.\n"));
		return ;
	}

	psTmp->pId=pId;
	psTmp->ui32OSid=ui32OSid;
	psTmp->ui32OSidReg=ui32OSidReg;

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

IMG_VOID RetrieveOSidsfromPidList(IMG_PID pId, IMG_UINT32 *pui32OSid, IMG_UINT32 * pui32OSidReg)
{
	PidOSidCouplingList *psTmp;

	for (psTmp=psPidOSidHead;psTmp!=NULL;psTmp=psTmp->psNext)
	{
		if (psTmp->pId==pId)
		{
			(*pui32OSid) = psTmp->ui32OSid;
			(*pui32OSidReg) = psTmp->ui32OSidReg;

			return ;
		}
	}

	(*pui32OSid)=0;
	(*pui32OSidReg)=0;
	return ;
}

IMG_VOID    RemovePidOSidCoupling(IMG_PID pId)
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

