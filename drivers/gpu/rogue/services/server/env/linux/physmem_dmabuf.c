/*************************************************************************/ /*!
@File           physmem_dmabuf.c
@Title          dmabuf memory allocator
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management. This module is responsible for
                implementing the function callbacks for dmabuf memory.
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
#include "pmr.h"
#include "pmr_impl.h"
#include "physmem_dmabuf.h"
#include "ion_sys.h"
#include "hash.h"

#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>

typedef struct _PMR_DMA_BUF_DATA_
{
	struct dma_buf_attachment *psAttachment;
	struct sg_table *psSgTable;
	struct dma_buf *psDmaBuf;

	IMG_DEVMEM_SIZE_T uiSize;
	IMG_PVOID *pvKernAddr;

	IMG_DEV_PHYADDR *pasDevPhysAddr;
	IMG_UINT32 ui32PageCount;
	PHYS_HEAP *psPhysHeap;

	IMG_BOOL bPoisonOnFree;
	IMG_BOOL bPDumpMalloced;
	IMG_HANDLE hPDumpAllocInfo;
} PMR_DMA_BUF_DATA;

/* Start size of the g_psDmaBufHash hash table */
#define DMA_BUF_HASH_SIZE 20

static HASH_TABLE *g_psDmaBufHash = IMG_NULL;
static IMG_UINT32 g_ui32HashRefCount = 0;

#if defined(PVR_ANDROID_ION_USE_SG_LENGTH)
#define pvr_sg_length(sg) ((sg)->length)
#else
#define pvr_sg_length(sg) sg_dma_len(sg)
#endif

/*****************************************************************************
 *                     DMA-BUF specific functions                            *
 *****************************************************************************/

/*
	Obtain a list of physical pages from the dmabuf.
*/
static PVRSRV_ERROR DmaBufPhysAddrAcquire(PMR_DMA_BUF_DATA *psPrivData, int fd)
{
	struct dma_buf_attachment *psAttachment = psPrivData->psAttachment;
	IMG_DEV_PHYADDR *pasDevPhysAddr = NULL;
	IMG_CPU_PHYADDR sCpuPhysAddr;
	IMG_UINT32 ui32PageCount = 0;
	struct scatterlist *sg;
	struct sg_table *table;
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	table = dma_buf_map_attachment(psAttachment, DMA_NONE);
	if (!table)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exitFailMap;
	}

	/*
		We do a two pass process, 1st workout how many pages there
		are, 2nd fill in the data.
	*/
	for_each_sg(table->sgl, sg, table->nents, i)
	{
		ui32PageCount += PAGE_ALIGN(pvr_sg_length(sg)) / PAGE_SIZE;
	}

	if (WARN_ON(!ui32PageCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to import dmabuf with no pages",
				 __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exitFailMap;
	}

	pasDevPhysAddr = OSAllocMem(sizeof(IMG_DEV_PHYADDR)*ui32PageCount);
	if (!pasDevPhysAddr)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto exitFailAlloc;
	}

	ui32PageCount = 0;

	for_each_sg(table->sgl, sg, table->nents, i)
	{
		IMG_UINT32 j;

		for (j = 0; j < pvr_sg_length(sg); j += PAGE_SIZE)
		{
			/* Pass 2: Get the page data */
			sCpuPhysAddr.uiAddr = sg_phys(sg);

			pasDevPhysAddr[ui32PageCount] =
				IonCPUPhysToDevPhys(sCpuPhysAddr, j);
			ui32PageCount++;
		}
	}

	psPrivData->pasDevPhysAddr = pasDevPhysAddr;
	psPrivData->ui32PageCount = ui32PageCount;
	psPrivData->uiSize = (IMG_DEVMEM_SIZE_T)ui32PageCount * PAGE_SIZE;
	psPrivData->psSgTable = table;

	return PVRSRV_OK;

exitFailAlloc:
exitFailMap:
	PVR_ASSERT(eError!= PVRSRV_OK);
	return eError;
}

static void DmaBufPhysAddrRelease(PMR_DMA_BUF_DATA *psPrivData)
{
	struct dma_buf_attachment *psAttachment = psPrivData->psAttachment;
	struct sg_table *psSgTable = psPrivData->psSgTable;

	dma_buf_unmap_attachment(psAttachment, psSgTable, DMA_NONE);

	OSFreeMem(psPrivData->pasDevPhysAddr);
}

static IMG_BOOL _DmaBufKeyCompare(IMG_SIZE_T uKeySize, void *pKey1, void *pKey2)
{
	IMG_DEV_PHYADDR *psKey1 = pKey1;
	IMG_DEV_PHYADDR *psKey2 = pKey2;
	PVR_ASSERT(uKeySize == sizeof(IMG_DEV_PHYADDR));
	
	return psKey1->uiAddr == psKey2->uiAddr;
}

/*****************************************************************************
 *                       PMR callback functions                              *
 *****************************************************************************/


static void _Poison(IMG_PVOID pvKernAddr, IMG_DEVMEM_SIZE_T uiBufferSize,
					const IMG_CHAR *pacPoisonData, IMG_SIZE_T uiPoisonSize)
{
	IMG_DEVMEM_SIZE_T uiDestByteIndex;
	IMG_CHAR *pcDest = pvKernAddr;
	IMG_UINT32 uiSrcByteIndex = 0;

	for(uiDestByteIndex=0; uiDestByteIndex<uiBufferSize; uiDestByteIndex++)
	{
		pcDest[uiDestByteIndex] = pacPoisonData[uiSrcByteIndex];
		uiSrcByteIndex++;
		if (uiSrcByteIndex == uiPoisonSize)
		{
			uiSrcByteIndex = 0;
		}
	}
}

static const IMG_CHAR _AllocPoison[] = "^PoIsOn";
static const IMG_UINT32 _AllocPoisonSize = 7;
static const IMG_CHAR _FreePoison[] = "<DEAD-BEEF>";
static const IMG_UINT32 _FreePoisonSize = 11;

static PVRSRV_ERROR PMRFinalizeDmaBuf(PMR_IMPL_PRIVDATA pvPriv)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;

	HASH_Remove_Extended(g_psDmaBufHash, &psPrivData->pasDevPhysAddr[0]);
	g_ui32HashRefCount--;
	if (g_ui32HashRefCount == 0)
	{
		HASH_Delete(g_psDmaBufHash);
		g_psDmaBufHash = IMG_NULL;
	}

	if (psPrivData->bPDumpMalloced)
	{
		PDumpPMRFree(psPrivData->hPDumpAllocInfo);
	}

	if (psPrivData->bPoisonOnFree)
	{
		IMG_PVOID pvKernAddr;
		int i, err;

		err = dma_buf_begin_cpu_access(psPrivData->psDmaBuf, 0,
									   psPrivData->uiSize, DMA_NONE);
		if (err)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to begin cpu access", __func__));
			PVR_ASSERT(IMG_FALSE);
		}

		for (i = 0; i < psPrivData->uiSize / PAGE_SIZE; i++)
		{
			pvKernAddr = dma_buf_kmap(psPrivData->psDmaBuf, i);

			if (IS_ERR_OR_NULL(pvKernAddr))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to poison allocation before free", __func__));
				PVR_ASSERT(IMG_FALSE);
			}

			_Poison(pvKernAddr, PAGE_SIZE, _FreePoison, _FreePoisonSize);

			dma_buf_kunmap(psPrivData->psDmaBuf, i, pvKernAddr);
		}

		dma_buf_end_cpu_access(psPrivData->psDmaBuf, 0,
							   psPrivData->uiSize, DMA_NONE);
	}

	DmaBufPhysAddrRelease(psPrivData);
	dma_buf_detach(psPrivData->psDmaBuf, psPrivData->psAttachment);
	dma_buf_put(psPrivData->psDmaBuf);
	PhysHeapRelease(psPrivData->psPhysHeap);
	OSFreeMem(psPrivData);

	return PVRSRV_OK;
}

/*
	Lock and unlock function for physical address
	don't do anything for as we acquire the physical
	address at create time.
*/
static PVRSRV_ERROR PMRLockPhysAddressesDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
											   IMG_UINT32 uiLog2DevPageSize)
{
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	PVR_UNREFERENCED_PARAMETER(uiLog2DevPageSize);

	return PVRSRV_OK;

}

static PVRSRV_ERROR PMRUnlockPhysAddressesDmaBuf(PMR_IMPL_PRIVDATA pvPriv)
{
	PVR_UNREFERENCED_PARAMETER(pvPriv);

	return PVRSRV_OK;
}

static PVRSRV_ERROR PMRDevPhysAddrDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
										 IMG_DEVMEM_OFFSET_T uiOffset,
										 IMG_DEV_PHYADDR *psDevPAddr)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	IMG_UINT32 ui32PageCount;
	IMG_UINT32 ui32PageIndex;
	IMG_UINT32 ui32InPageOffset;

	ui32PageCount = psPrivData->ui32PageCount;

	ui32PageIndex = uiOffset >> PAGE_SHIFT;
	ui32InPageOffset = uiOffset - ((IMG_DEVMEM_OFFSET_T)ui32PageIndex << PAGE_SHIFT);
	PVR_ASSERT(ui32PageIndex < ui32PageCount);
	PVR_ASSERT(ui32InPageOffset < PAGE_SIZE);

	psDevPAddr->uiAddr = psPrivData->pasDevPhysAddr[ui32PageIndex].uiAddr + ui32InPageOffset;

	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
								  IMG_SIZE_T uiOffset,
								  IMG_SIZE_T uiSize,
								  void **ppvKernelAddressOut,
								  IMG_HANDLE *phHandleOut,
								  PMR_FLAGS_T ulFlags)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	IMG_PVOID pvKernAddr;
	PVRSRV_ERROR eError;
	int err;

	PVR_ASSERT(psPrivData->pvKernAddr == IMG_NULL);

	err = dma_buf_begin_cpu_access(psPrivData->psDmaBuf, 0,
								   psPrivData->uiSize, DMA_NONE);
	if (err)
	{
		eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
		goto fail;
	}

	pvKernAddr = dma_buf_vmap(psPrivData->psDmaBuf);
	if (IS_ERR_OR_NULL(pvKernAddr))
	{
		eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
		goto fail_kmap;
	}

	*ppvKernelAddressOut = pvKernAddr + uiOffset;
	psPrivData->pvKernAddr = pvKernAddr;
	return PVRSRV_OK;

fail_kmap:
	dma_buf_end_cpu_access(psPrivData->psDmaBuf, 0,
						   psPrivData->uiSize, DMA_NONE);
fail:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static void PMRReleaseKernelMappingDataDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
											  IMG_HANDLE hHandle)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;

	PVR_UNREFERENCED_PARAMETER(hHandle);

	dma_buf_vunmap(psPrivData->psDmaBuf, psPrivData->pvKernAddr);
	psPrivData->pvKernAddr = IMG_NULL;

	dma_buf_end_cpu_access(psPrivData->psDmaBuf, 0,
						   psPrivData->uiSize, DMA_NONE);
}

static PMR_IMPL_FUNCTAB _sPMRDmaBufFuncTab =
{
	.pfnLockPhysAddresses			= PMRLockPhysAddressesDmaBuf,
	.pfnUnlockPhysAddresses			= PMRUnlockPhysAddressesDmaBuf,
	.pfnDevPhysAddr					= PMRDevPhysAddrDmaBuf,
	.pfnAcquireKernelMappingData	= PMRAcquireKernelMappingDataDmaBuf,
	.pfnReleaseKernelMappingData	= PMRReleaseKernelMappingDataDmaBuf,
	.pfnFinalize					= PMRFinalizeDmaBuf,
};

/*****************************************************************************
 *                       Public facing interface                             *
 *****************************************************************************/

PVRSRV_ERROR
PhysmemImportDmaBuf(CONNECTION_DATA *psConnection,
					IMG_INT fd,
					PVRSRV_MEMALLOCFLAGS_T uiFlags,
					PMR **ppsPMRPtr,
					IMG_DEVMEM_SIZE_T *puiSize,
					IMG_DEVMEM_ALIGN_T *puiAlign)
{
	PMR_DMA_BUF_DATA *psPrivData = IMG_NULL;
	IMG_HANDLE hPDumpAllocInfo = IMG_NULL;
	IMG_BOOL bMappingTable = IMG_TRUE;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	PMR_FLAGS_T uiPMRFlags;
	PMR *psPMR = IMG_NULL;
	PVRSRV_ERROR eError;
	IMG_BOOL bZero;

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

	if ((uiFlags & PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC) &&
		(uiFlags & PVRSRV_MEMALLOCFLAG_POISON_ON_ALLOC))
	{
		/* Zero on Alloc and Poison on Alloc are mutually exclusive */
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_params;
	}

	if (!psConnection)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto fail_params;
	}

	psPrivData = OSAllocMem(sizeof(*psPrivData));
	if (psPrivData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_privalloc;
	}

	/*
		Get the physical heap for this PMR
		
		Note:
		While we have no way to determine the type of the buffer
		we just assume that all dmabufs are from the same
		physical heap.
	*/
	eError = PhysHeapAcquire(IonPhysHeapID(), &psPrivData->psPhysHeap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed PhysHeapAcquire", __func__));
		goto fail_physheap;
	}

	/* Get the buffer handle */
	psPrivData->psDmaBuf = dma_buf_get(fd);

	if (IS_ERR_OR_NULL(psPrivData->psDmaBuf))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: dma_buf_get failed", __func__));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto fail_dma_buf_get;
	}

	/* Attach a fake device to to the dmabuf */
	psPrivData->psAttachment = dma_buf_attach(psPrivData->psDmaBuf, (void *)0x1);

	if (IS_ERR_OR_NULL(psPrivData->psAttachment))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: dma_buf_get failed", __func__));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto fail_attach;
	}

	/*
		Note:

		We could defer the import until lock address time but we
		do it here as then we can detect any errors at import time.
		Also we need to know the dmabuf size here and there seems
		to be no other way to find that other then map the buffer for dma.
	*/
	eError = DmaBufPhysAddrAcquire(psPrivData, fd);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: DmaBufPhysAddrAcquire failed", __func__));
		goto fail_acquire;
	}

	if (g_psDmaBufHash == IMG_NULL)
	{
		/*
			As different processes may import the same dmabuf we need to
			create a hash table so we don't generate a duplicate PMR but
			rather just take a reference on an existing one.
		*/
		g_psDmaBufHash = HASH_Create_Extended(DMA_BUF_HASH_SIZE, sizeof(psPrivData->pasDevPhysAddr[0]), HASH_Func_Default, _DmaBufKeyCompare);
		if (g_psDmaBufHash == IMG_NULL)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
	else
	{
		/*
			We have a hash table so check if have already seen this
			this dmabuf before
		*/
		psPMR = (PMR *) HASH_Retrieve_Extended(g_psDmaBufHash, &psPrivData->pasDevPhysAddr[0]);
		if (psPMR != IMG_NULL)
		{
			/*
				We already know about this dmabuf but we had to do a bunch
				for work to determine that so here we have to undo it
			*/
			DmaBufPhysAddrRelease(psPrivData);
			dma_buf_detach(psPrivData->psDmaBuf, psPrivData->psAttachment);
			dma_buf_put(psPrivData->psDmaBuf);
			PhysHeapRelease(psPrivData->psPhysHeap);
			OSFreeMem(psPrivData);
			
			/* Reuse the PMR we already created */
			PMRRefPMR(psPMR);

			*ppsPMRPtr = psPMR;
			psPrivData = PMRGetPrivateDataHack(psPMR, &_sPMRDmaBufFuncTab);
			*puiSize = psPrivData->uiSize;
			*puiAlign = PAGE_SIZE;
			return PVRSRV_OK;
		}
	}

	if (bZero || bPoisonOnAlloc)
	{
		IMG_PVOID pvKernAddr;
		int i, err;

		err = dma_buf_begin_cpu_access(psPrivData->psDmaBuf, 0,
									   psPrivData->uiSize, DMA_NONE);
		if (err)
		{
			eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
			goto fail_begin;
		}

		for (i = 0; i < psPrivData->uiSize / PAGE_SIZE; i++)
		{
			pvKernAddr = dma_buf_kmap(psPrivData->psDmaBuf, i);

			if (IS_ERR_OR_NULL(pvKernAddr))
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to poison allocation before free", __func__));
				eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
				goto fail_kmap;
			}

			if (bZero)
			{
				memset(pvKernAddr, 0, PAGE_SIZE);
			}
			else
			{
				_Poison(pvKernAddr, PAGE_SIZE, _AllocPoison, _AllocPoisonSize);
			}

			dma_buf_kunmap(psPrivData->psDmaBuf, i, pvKernAddr);
		}

		dma_buf_end_cpu_access(psPrivData->psDmaBuf, 0,
							   psPrivData->uiSize, DMA_NONE);
	}

	psPrivData->bPoisonOnFree = bPoisonOnFree;

	uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);
	/* check no significant bits were lost in cast due to different
	   bit widths for flags */
	PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

	eError = PMRCreatePMR(psPrivData->psPhysHeap,
						  psPrivData->uiSize,
                          psPrivData->uiSize,
                          1,
                          1,
                          &bMappingTable,
						  PAGE_SHIFT,
						  uiPMRFlags,
						  "PMRDMABUF",
						  &_sPMRDmaBufFuncTab,
						  psPrivData,
						  &psPMR,
						  &hPDumpAllocInfo,
						  IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create PMR", __func__));
		goto fail_pmrcreate;
	}

#if defined(PVR_RI_DEBUG)
	{
		eError = RIWritePMREntryKM (psPMR,
									sizeof("DMABUF"),
									"DMABUF",
									psPrivData->uiSize);
	}
#endif

	psPrivData->hPDumpAllocInfo = hPDumpAllocInfo;
	psPrivData->bPDumpMalloced = IMG_TRUE;

	/* First time we've seen this dmabuf so store it in the hash table */
	HASH_Insert_Extended(g_psDmaBufHash, &psPrivData->pasDevPhysAddr[0], (IMG_UINTPTR_T) psPMR);
	g_ui32HashRefCount++;

	*ppsPMRPtr = psPMR;
	*puiSize = psPrivData->uiSize;
	*puiAlign = PAGE_SIZE;
	return PVRSRV_OK;

fail_pmrcreate:
fail_kmap:
	dma_buf_end_cpu_access(psPrivData->psDmaBuf, 0,
						   psPrivData->uiSize, DMA_NONE);
fail_begin:
	DmaBufPhysAddrRelease(psPrivData);
fail_acquire:
	dma_buf_detach(psPrivData->psDmaBuf, psPrivData->psAttachment);
fail_attach:
	dma_buf_put(psPrivData->psDmaBuf);
fail_dma_buf_get:
	PhysHeapRelease(psPrivData->psPhysHeap);
fail_physheap:
	OSFreeMem(psPrivData);

fail_privalloc:
fail_params:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}
