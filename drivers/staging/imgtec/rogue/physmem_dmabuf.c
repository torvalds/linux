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

#include <linux/version.h>

#include "physmem_dmabuf.h"
#include "pvrsrv.h"
#include "pmr.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)) || defined(SUPPORT_ION) || defined(KERNEL_HAS_DMABUF_VMAP_MMAP)

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "pvrsrv_memallocflags.h"

#include "allocmem.h"
#include "osfunc.h"
#include "pmr_impl.h"
#include "hash.h"
#include "private_data.h"
#include "module_common.h"

#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif

#include "kernel_compatibility.h"

/*
 * dma_buf_ops
 *
 * These are all returning errors if used.
 * The point is to prevent anyone outside of our driver from importing
 * and using our dmabuf.
 */

static int PVRDmaBufOpsAttach(struct dma_buf *psDmaBuf,
                           struct dma_buf_attachment *psAttachment)
{
	return -ENOSYS;
}

static struct sg_table *PVRDmaBufOpsMap(struct dma_buf_attachment *psAttachment,
                                      enum dma_data_direction eDirection)
{
	/* Attach hasn't been called yet */
	return ERR_PTR(-EINVAL);
}

static void PVRDmaBufOpsUnmap(struct dma_buf_attachment *psAttachment,
                           struct sg_table *psTable,
                           enum dma_data_direction eDirection)
{
}

static void PVRDmaBufOpsRelease(struct dma_buf *psDmaBuf)
{
	PMR *psPMR = (PMR *) psDmaBuf->priv;

	PMRUnrefPMR(psPMR);
}

static void *PVRDmaBufOpsKMap(struct dma_buf *psDmaBuf, unsigned long uiPageNum)
{
	return ERR_PTR(-ENOSYS);
}

static int PVRDmaBufOpsMMap(struct dma_buf *psDmaBuf, struct vm_area_struct *psVMA)
{
	return -ENOSYS;
}

static const struct dma_buf_ops sPVRDmaBufOps =
{
	.attach        = PVRDmaBufOpsAttach,
	.map_dma_buf   = PVRDmaBufOpsMap,
	.unmap_dma_buf = PVRDmaBufOpsUnmap,
	.release       = PVRDmaBufOpsRelease,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
    //.map_atomic    = PVRDmaBufOpsKMap,
    .map           = PVRDmaBufOpsKMap,
#else
    .kmap_atomic   = PVRDmaBufOpsKMap,
    .kmap          = PVRDmaBufOpsKMap,
#endif
	.mmap          = PVRDmaBufOpsMMap,
};

/* end of dma_buf_ops */


typedef struct _PMR_DMA_BUF_DATA_
{
	/* Filled in at PMR create time */
	PHYS_HEAP *psPhysHeap;
	struct dma_buf_attachment *psAttachment;
	PFN_DESTROY_DMABUF_PMR pfnDestroy;
	IMG_BOOL bPoisonOnFree;

	/* Modified by PMR lock/unlock */
	struct sg_table *psSgTable;
	IMG_DEV_PHYADDR *pasDevPhysAddr;
	IMG_UINT32 ui32PhysPageCount;
	IMG_UINT32 ui32VirtPageCount;
} PMR_DMA_BUF_DATA;

/* Start size of the g_psDmaBufHash hash table */
#define DMA_BUF_HASH_SIZE 20

static HASH_TABLE *g_psDmaBufHash = NULL;
static IMG_UINT32 g_ui32HashRefCount = 0;

#if defined(PVR_ANDROID_ION_USE_SG_LENGTH)
#define pvr_sg_length(sg) ((sg)->length)
#else
#define pvr_sg_length(sg) sg_dma_len(sg)
#endif

static const IMG_CHAR _AllocPoison[] = "^PoIsOn";
static const IMG_UINT32 _AllocPoisonSize = 7;
static const IMG_CHAR _FreePoison[] = "<DEAD-BEEF>";
static const IMG_UINT32 _FreePoisonSize = 11;

static void _Poison(void *pvKernAddr,
		    IMG_DEVMEM_SIZE_T uiBufferSize,
		    const IMG_CHAR *pacPoisonData,
		    size_t uiPoisonSize)
{
	IMG_DEVMEM_SIZE_T uiDestByteIndex;
	IMG_CHAR *pcDest = pvKernAddr;
	IMG_UINT32 uiSrcByteIndex = 0;

	for (uiDestByteIndex = 0; uiDestByteIndex < uiBufferSize; uiDestByteIndex++)
	{
		pcDest[uiDestByteIndex] = pacPoisonData[uiSrcByteIndex];
		uiSrcByteIndex++;
		if (uiSrcByteIndex == uiPoisonSize)
		{
			uiSrcByteIndex = 0;
		}
	}
}


/*****************************************************************************
 *                       PMR callback functions                              *
 *****************************************************************************/

static PVRSRV_ERROR PMRFinalizeDmaBuf(PMR_IMPL_PRIVDATA pvPriv)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	struct dma_buf_attachment *psAttachment = psPrivData->psAttachment;
	struct dma_buf *psDmaBuf = psAttachment->dmabuf;
	struct sg_table *psSgTable = psPrivData->psSgTable;
	PVRSRV_ERROR eError;

	psPrivData->ui32PhysPageCount = 0;

	dma_buf_unmap_attachment(psAttachment, psSgTable, DMA_BIDIRECTIONAL);


	if (psPrivData->bPoisonOnFree)
	{
		void *pvKernAddr;
		int i, err;

		err = dma_buf_begin_cpu_access(psDmaBuf, DMA_FROM_DEVICE);
		if (err)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Failed to begin cpu access for free poisoning (err=%d)",
					 __func__, err));
			PVR_ASSERT(IMG_FALSE);
			goto exit;
		}

		for (i = 0; i < psDmaBuf->size / PAGE_SIZE; i++)
		{
			pvKernAddr = dma_buf_kmap(psDmaBuf, i);
			if (IS_ERR_OR_NULL(pvKernAddr))
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to poison allocation before free (err=%ld)",
						 __func__, pvKernAddr ? PTR_ERR(pvKernAddr) : -ENOMEM));
				PVR_ASSERT(IMG_FALSE);
				goto exit_end_access;
			}

			_Poison(pvKernAddr, PAGE_SIZE, _FreePoison, _FreePoisonSize);

			dma_buf_kunmap(psDmaBuf, i, pvKernAddr);
		}

exit_end_access:
		do {
			err = dma_buf_end_cpu_access(psDmaBuf, DMA_TO_DEVICE);
		} while (err == -EAGAIN || err == -EINTR);
	}

exit:
	if (psPrivData->pfnDestroy)
	{
		eError = psPrivData->pfnDestroy(psPrivData->psPhysHeap, psPrivData->psAttachment);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	OSFreeMem(psPrivData->pasDevPhysAddr);
	OSFreeMem(psPrivData);

	return PVRSRV_OK;
}

static PVRSRV_ERROR PMRLockPhysAddressesDmaBuf(PMR_IMPL_PRIVDATA pvPriv)
{
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	return PVRSRV_OK;
}

static PVRSRV_ERROR PMRUnlockPhysAddressesDmaBuf(PMR_IMPL_PRIVDATA pvPriv)
{
	PVR_UNREFERENCED_PARAMETER(pvPriv);
	return PVRSRV_OK;
}

static PVRSRV_ERROR PMRDevPhysAddrDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
					 IMG_UINT32 ui32Log2PageSize,
					 IMG_UINT32 ui32NumOfPages,
					 IMG_DEVMEM_OFFSET_T *puiOffset,
					 IMG_BOOL *pbValid,
					 IMG_DEV_PHYADDR *psDevPAddr)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	IMG_UINT32 ui32PageIndex;
	IMG_UINT32 idx;

	if (ui32Log2PageSize != PAGE_SHIFT)
	{
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	for (idx=0; idx < ui32NumOfPages; idx++)
	{
		if (pbValid[idx])
		{
			IMG_UINT32 ui32InPageOffset;

			ui32PageIndex = puiOffset[idx] >> PAGE_SHIFT;
			ui32InPageOffset = puiOffset[idx] - ((IMG_DEVMEM_OFFSET_T)ui32PageIndex << PAGE_SHIFT);


			PVR_ASSERT(ui32PageIndex < psPrivData->ui32VirtPageCount);
			PVR_ASSERT(ui32InPageOffset < PAGE_SIZE);
			psDevPAddr[idx].uiAddr = psPrivData->pasDevPhysAddr[ui32PageIndex].uiAddr + ui32InPageOffset;
		}
	}
	return PVRSRV_OK;
}

static PVRSRV_ERROR
PMRAcquireKernelMappingDataDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
				  size_t uiOffset,
				  size_t uiSize,
				  void **ppvKernelAddressOut,
				  IMG_HANDLE *phHandleOut,
				  PMR_FLAGS_T ulFlags)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	struct dma_buf *psDmaBuf = psPrivData->psAttachment->dmabuf;
	void *pvKernAddr;
	PVRSRV_ERROR eError;
	int err;

	if (psPrivData->ui32PhysPageCount != psPrivData->ui32VirtPageCount)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Kernel mappings for sparse DMABufs "
				"are not allowed!", __func__));
		eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
		goto fail;
	}

	err = dma_buf_begin_cpu_access(psDmaBuf, DMA_BIDIRECTIONAL);
	if (err)
	{
		eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
		goto fail;
	}

	pvKernAddr = dma_buf_vmap(psDmaBuf);
	if (IS_ERR_OR_NULL(pvKernAddr))
	{
		eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
		goto fail_kmap;
	}

	*ppvKernelAddressOut = pvKernAddr + uiOffset;
	*phHandleOut = pvKernAddr;

	return PVRSRV_OK;

fail_kmap:
	do {
		err = dma_buf_end_cpu_access(psDmaBuf, DMA_BIDIRECTIONAL);
	} while (err == -EAGAIN || err == -EINTR);

fail:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static void PMRReleaseKernelMappingDataDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
					      IMG_HANDLE hHandle)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	struct dma_buf *psDmaBuf = psPrivData->psAttachment->dmabuf;
	void *pvKernAddr = hHandle;
	int err;

	dma_buf_vunmap(psDmaBuf, pvKernAddr);

	do {
		err = dma_buf_end_cpu_access(psDmaBuf, DMA_BIDIRECTIONAL);
	} while (err == -EAGAIN || err == -EINTR);
}

static PVRSRV_ERROR PMRMMapDmaBuf(PMR_IMPL_PRIVDATA pvPriv,
                                  PMR *psPMR,
                                  PMR_MMAP_DATA pOSMMapData)
{
	PMR_DMA_BUF_DATA *psPrivData = pvPriv;
	struct dma_buf *psDmaBuf = psPrivData->psAttachment->dmabuf;
	struct vm_area_struct *psVma = pOSMMapData;
	int err;

	if (psPrivData->ui32PhysPageCount != psPrivData->ui32VirtPageCount)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Not possible to MMAP sparse DMABufs",
				__func__));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	err = dma_buf_mmap(psDmaBuf, psVma, 0);
	if (err)
	{
		return (err == -EINVAL) ? PVRSRV_ERROR_NOT_SUPPORTED : PVRSRV_ERROR_BAD_MAPPING;
	}

	return PVRSRV_OK;
}

static PMR_IMPL_FUNCTAB _sPMRDmaBufFuncTab =
{
	.pfnLockPhysAddresses		= PMRLockPhysAddressesDmaBuf,
	.pfnUnlockPhysAddresses		= PMRUnlockPhysAddressesDmaBuf,
	.pfnDevPhysAddr			= PMRDevPhysAddrDmaBuf,
	.pfnAcquireKernelMappingData	= PMRAcquireKernelMappingDataDmaBuf,
	.pfnReleaseKernelMappingData	= PMRReleaseKernelMappingDataDmaBuf,
	.pfnMMap			= PMRMMapDmaBuf,
	.pfnFinalize			= PMRFinalizeDmaBuf,
};

/*****************************************************************************
 *                       Public facing interface                             *
 *****************************************************************************/

PVRSRV_ERROR
PhysmemCreateNewDmaBufBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
                                PHYS_HEAP *psHeap,
                                struct dma_buf_attachment *psAttachment,
                                PFN_DESTROY_DMABUF_PMR pfnDestroy,
                                PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                IMG_DEVMEM_SIZE_T uiChunkSize,
                                IMG_UINT32 ui32NumPhysChunks,
                                IMG_UINT32 ui32NumVirtChunks,
                                IMG_UINT32 *pui32MappingTable,
                                PMR **ppsPMRPtr)
{
	struct dma_buf *psDmaBuf = psAttachment->dmabuf;
	PMR_DMA_BUF_DATA *psPrivData;
	PMR_FLAGS_T uiPMRFlags;
	IMG_BOOL bZeroOnAlloc;
	IMG_BOOL bPoisonOnAlloc;
	IMG_BOOL bPoisonOnFree;
	PVRSRV_ERROR eError;
	IMG_UINT32 i, j;
	IMG_UINT32 uiPagesPerChunk = uiChunkSize >> PAGE_SHIFT;
	IMG_UINT32 ui32PageCount = 0;
	struct scatterlist *sg;
	struct sg_table *table;
	IMG_UINT32 uiSglOffset;

	bZeroOnAlloc = PVRSRV_CHECK_ZERO_ON_ALLOC(uiFlags);
	bPoisonOnAlloc = PVRSRV_CHECK_POISON_ON_ALLOC(uiFlags);
	bPoisonOnFree = PVRSRV_CHECK_POISON_ON_FREE(uiFlags);

	if (bZeroOnAlloc && bPoisonOnFree)
	{
		/* Zero on Alloc and Poison on Alloc are mutually exclusive */
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errReturn;
	}

	psPrivData = OSAllocZMem(sizeof(*psPrivData));
	if (psPrivData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errReturn;
	}

	psPrivData->psPhysHeap = psHeap;
	psPrivData->psAttachment = psAttachment;
	psPrivData->pfnDestroy = pfnDestroy;
	psPrivData->bPoisonOnFree = bPoisonOnFree;
	psPrivData->ui32VirtPageCount =
			(ui32NumVirtChunks * uiChunkSize) >> PAGE_SHIFT;

	psPrivData->pasDevPhysAddr =
			OSAllocZMem(sizeof(*(psPrivData->pasDevPhysAddr)) *
			            psPrivData->ui32VirtPageCount);
	if (!psPrivData->pasDevPhysAddr)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Failed to allocate buffer for physical addresses (oom)",
				 __func__));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto errFreePrivData;
	}

	if (bZeroOnAlloc || bPoisonOnAlloc)
	{
		void *pvKernAddr;
		int i, err;

		err = dma_buf_begin_cpu_access(psDmaBuf, DMA_FROM_DEVICE);
		if (err)
		{
			eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;
			goto errFreePhysAddr;
		}

		for (i = 0; i < psDmaBuf->size / PAGE_SIZE; i++)
		{
			pvKernAddr = dma_buf_kmap(psDmaBuf, i);
			if (IS_ERR_OR_NULL(pvKernAddr))
			{
				PVR_DPF((PVR_DBG_ERROR,
						 "%s: Failed to map page for %s (err=%ld)",
						 __func__, bZeroOnAlloc ? "zeroing" : "poisoning",
						 pvKernAddr ? PTR_ERR(pvKernAddr) : -ENOMEM));
				eError = PVRSRV_ERROR_PMR_NO_KERNEL_MAPPING;

				do {
					err = dma_buf_end_cpu_access(psDmaBuf, DMA_TO_DEVICE);
				} while (err == -EAGAIN || err == -EINTR);

				goto errFreePhysAddr;
			}

			if (bZeroOnAlloc)
			{
				memset(pvKernAddr, 0, PAGE_SIZE);
			}
			else
			{
				_Poison(pvKernAddr, PAGE_SIZE, _AllocPoison, _AllocPoisonSize);
			}

			dma_buf_kunmap(psDmaBuf, i, pvKernAddr);
		}

		do {
			err = dma_buf_end_cpu_access(psDmaBuf, DMA_TO_DEVICE);
		} while (err == -EAGAIN || err == -EINTR);
	}

	table = dma_buf_map_attachment(psAttachment, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(table))
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errFreePhysAddr;
	}

	/*
	 * We do a two pass process: first work out how many pages there
	 * are and second, fill in the data.
	 */
	for_each_sg(table->sgl, sg, table->nents, i)
	{
		ui32PageCount += PAGE_ALIGN(pvr_sg_length(sg)) / PAGE_SIZE;
	}

	if (WARN_ON(!ui32PageCount))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Number of phys. pages must not be zero",
				 __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errUnmap;
	}

	if  (WARN_ON(ui32PageCount != ui32NumPhysChunks * uiPagesPerChunk))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Requested physical chunks and actual "
				"number of physical dma buf pages don't match",
				 __func__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errUnmap;
	}

	psPrivData->ui32PhysPageCount = ui32PageCount;
	psPrivData->psSgTable = table;
	sg = table->sgl;
	uiSglOffset = 0;


	/* Fill physical address array */
	for (i = 0; i < ui32NumPhysChunks; i++)
	{
		for (j = 0; j < uiPagesPerChunk; j++)
		{
			IMG_UINT32 uiIdx = pui32MappingTable[i] * uiPagesPerChunk + j;

			psPrivData->pasDevPhysAddr[uiIdx].uiAddr =
					sg_dma_address(sg) + uiSglOffset;

			/* Get the next offset for the current sgl or the next sgl */
			uiSglOffset += PAGE_SIZE;
			if (uiSglOffset >= pvr_sg_length(sg))
			{
				sg = sg_next(sg);
				uiSglOffset = 0;

				/* Check that we haven't looped */
				if (WARN_ON(sg == table->sgl))
				{
					PVR_DPF((PVR_DBG_ERROR, "%s: Failed to fill phys. address "
							"array ",
							 __func__));
					eError = PVRSRV_ERROR_INVALID_PARAMS;
					goto errUnmap;
				}
			}
		}
	}

	uiPMRFlags = (PMR_FLAGS_T)(uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK);

	/*
	 * Check no significant bits were lost in cast due to different
	 * bit widths for flags
	 */
	PVR_ASSERT(uiPMRFlags == (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK));

	eError = PMRCreatePMR(psDevNode,
			      psHeap,
			      ui32NumVirtChunks * uiChunkSize,
			      uiChunkSize,
			      ui32NumPhysChunks,
			      ui32NumVirtChunks,
			      pui32MappingTable,
			      PAGE_SHIFT,
			      uiPMRFlags,
			      "IMPORTED_DMABUF",
			      &_sPMRDmaBufFuncTab,
			      psPrivData,
			      PMR_TYPE_DMABUF,
			      ppsPMRPtr,
			      IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create PMR (%s)",
				 __func__, PVRSRVGetErrorStringKM(eError)));
		goto errFreePhysAddr;
	}

	return PVRSRV_OK;

errUnmap:
	dma_buf_unmap_attachment(psAttachment, table, DMA_BIDIRECTIONAL);
errFreePhysAddr:
	OSFreeMem(psPrivData->pasDevPhysAddr);
errFreePrivData:
	OSFreeMem(psPrivData);
errReturn:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

static PVRSRV_ERROR PhysmemDestroyDmaBuf(PHYS_HEAP *psHeap,
					 struct dma_buf_attachment *psAttachment)
{
	struct dma_buf *psDmaBuf = psAttachment->dmabuf;

	HASH_Remove(g_psDmaBufHash, (uintptr_t) psDmaBuf);
	g_ui32HashRefCount--;

	if (g_ui32HashRefCount == 0)
	{
		HASH_Delete(g_psDmaBufHash);
		g_psDmaBufHash = NULL;
	}

	PhysHeapRelease(psHeap);

	dma_buf_detach(psDmaBuf, psAttachment);
	dma_buf_put(psDmaBuf);

	return PVRSRV_OK;
}

struct dma_buf *
PhysmemGetDmaBuf(PMR *psPMR)
{
	PMR_DMA_BUF_DATA *psPrivData;

	psPrivData = PMRGetPrivateDataHack(psPMR, &_sPMRDmaBufFuncTab);
	if (psPrivData)
	{
		return psPrivData->psAttachment->dmabuf;
	}

	return NULL;
}

PVRSRV_ERROR
PhysmemExportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    PMR *psPMR,
                    IMG_INT *piFd)
{
	struct dma_buf *psDmaBuf;
	IMG_DEVMEM_SIZE_T uiPMRSize;
	PVRSRV_ERROR eError;
	IMG_INT iFd;

	PMRRefPMR(psPMR);

	eError = PMR_LogicalSize(psPMR, &uiPMRSize);
	if (eError != PVRSRV_OK)
	{
		goto fail_pmr_ref;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	{
		DEFINE_DMA_BUF_EXPORT_INFO(sDmaBufExportInfo);

		sDmaBufExportInfo.priv  = psPMR;
		sDmaBufExportInfo.ops   = &sPVRDmaBufOps;
		sDmaBufExportInfo.size  = uiPMRSize;
		sDmaBufExportInfo.flags = O_RDWR;

		psDmaBuf = dma_buf_export(&sDmaBufExportInfo);
	}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	psDmaBuf = dma_buf_export(psPMR, &sPVRDmaBufOps,
	                          uiPMRSize, O_RDWR, NULL);
#else
	psDmaBuf = dma_buf_export(psPMR, &sPVRDmaBufOps,
	                          uiPMRSize, O_RDWR);
#endif

	if (IS_ERR_OR_NULL(psDmaBuf))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to export buffer (err=%ld)",
		         __func__, psDmaBuf ? PTR_ERR(psDmaBuf) : -ENOMEM));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_pmr_ref;
	}

	iFd = dma_buf_fd(psDmaBuf, O_RDWR);
	if (iFd < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get dma-buf fd (err=%d)",
		         __func__, iFd));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto fail_dma_buf;
	}

	*piFd = iFd;
	return PVRSRV_OK;

fail_dma_buf:
	dma_buf_put(psDmaBuf);

fail_pmr_ref:
	PMRUnrefPMR(psPMR);

	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

PVRSRV_ERROR
PhysmemImportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    IMG_INT fd,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    PMR **ppsPMRPtr,
                    IMG_DEVMEM_SIZE_T *puiSize,
                    IMG_DEVMEM_ALIGN_T *puiAlign)
{
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT32 ui32MappingTable = 0;
	struct dma_buf *psDmaBuf;

	/* Get the buffer handle */
	psDmaBuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(psDmaBuf))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get dma-buf from fd (err=%ld)",
				 __func__, psDmaBuf ? PTR_ERR(psDmaBuf) : -ENOMEM));
		return PVRSRV_ERROR_BAD_MAPPING;

	}

	uiSize = psDmaBuf->size;

	dma_buf_put(psDmaBuf);

	return PhysmemImportSparseDmaBuf(psConnection,
	                                 psDevNode,
	                                 fd,
	                                 uiFlags,
	                                 uiSize,
	                                 1,
	                                 1,
	                                 &ui32MappingTable,
	                                 ppsPMRPtr,
	                                 puiSize,
	                                 puiAlign);


}

PVRSRV_ERROR
PhysmemImportSparseDmaBuf(CONNECTION_DATA *psConnection,
                          PVRSRV_DEVICE_NODE *psDevNode,
                          IMG_INT fd,
                          PVRSRV_MEMALLOCFLAGS_T uiFlags,
                          IMG_DEVMEM_SIZE_T uiChunkSize,
                          IMG_UINT32 ui32NumPhysChunks,
                          IMG_UINT32 ui32NumVirtChunks,
                          IMG_UINT32 *pui32MappingTable,
                          PMR **ppsPMRPtr,
                          IMG_DEVMEM_SIZE_T *puiSize,
                          IMG_DEVMEM_ALIGN_T *puiAlign)
{
	PMR *psPMR = NULL;
	struct dma_buf_attachment *psAttachment;
	struct dma_buf *psDmaBuf;
	PHYS_HEAP *psHeap;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	if (!psDevNode)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto errReturn;
	}

	/* Get the buffer handle */
	psDmaBuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(psDmaBuf))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to get dma-buf from fd (err=%ld)",
				 __func__, psDmaBuf ? PTR_ERR(psDmaBuf) : -ENOMEM));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto errReturn;
	}

	if (psDmaBuf->ops == &sPVRDmaBufOps)
	{
		PVRSRV_DEVICE_NODE *psPMRDevNode;

		/* We exported this dma_buf, so we can just get its PMR */
		psPMR = (PMR *) psDmaBuf->priv;

		/* However, we can't import it if it belongs to a different device */
		psPMRDevNode = PMR_DeviceNode(psPMR);
		if (psPMRDevNode != psDevNode)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: PMR invalid for this device\n",
					 __func__));
			eError = PVRSRV_ERROR_PMR_NOT_PERMITTED;
			goto errDMAPut;
		}
	}
	else if (g_psDmaBufHash)
	{
		/* We have a hash table so check if we've seen this dmabuf before */
		psPMR = (PMR *) HASH_Retrieve(g_psDmaBufHash, (uintptr_t) psDmaBuf);
	}

	if (psPMR)
	{
		/* Reuse the PMR we already created */
		PMRRefPMR(psPMR);

		*ppsPMRPtr = psPMR;
		PMR_LogicalSize(psPMR, puiSize);
		*puiAlign = PAGE_SIZE;

		dma_buf_put(psDmaBuf);

		return PVRSRV_OK;
	}

	/* Do we want this to be a sparse PMR? */
	if (ui32NumVirtChunks > 1)
	{
		IMG_UINT32 i;

		/* Parameter validation */
		if (psDmaBuf->size != (uiChunkSize * ui32NumPhysChunks) ||
		    uiChunkSize != PAGE_SIZE ||
		    ui32NumPhysChunks > ui32NumVirtChunks)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Requesting sparse buffer: "
					"uiChunkSize ("IMG_DEVMEM_SIZE_FMTSPEC") must be equal to "
					"OS page size (%lu). uiChunkSize * ui32NumPhysChunks "
					"("IMG_DEVMEM_SIZE_FMTSPEC") must"
					" be equal to the buffer size ("IMG_SIZE_FMTSPEC"). "
					"ui32NumPhysChunks (%u) must be lesser or equal to "
					"ui32NumVirtChunks (%u)",
					 __func__,
					uiChunkSize,
					PAGE_SIZE,
					uiChunkSize * ui32NumPhysChunks,
					psDmaBuf->size,
					ui32NumPhysChunks,
					ui32NumVirtChunks));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto errDMAPut;
		}

		/* Parameter validation - Mapping table entries*/
		for (i = 0; i < ui32NumPhysChunks; i++)
		{
			if (pui32MappingTable[i] > ui32NumVirtChunks)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Requesting sparse buffer: "
						"Entry in mapping table (%u) is out of allocation "
						"bounds (%u)",
						 __func__,
						 (IMG_UINT32) pui32MappingTable[i],
						 (IMG_UINT32) ui32NumVirtChunks));
				eError = PVRSRV_ERROR_INVALID_PARAMS;
				goto errDMAPut;
			}
		}
	}
	else
	{
		/* Make sure parameters are valid for non-sparse allocations as well */
		uiChunkSize = psDmaBuf->size;
		ui32NumPhysChunks = 1;
		ui32NumVirtChunks = 1;
		pui32MappingTable[0] = 0;
	}


	psAttachment = dma_buf_attach(psDmaBuf, psDevNode->psDevConfig->pvOSDevice);
	if (IS_ERR_OR_NULL(psAttachment))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to attach to dma-buf (err=%ld)",
				 __func__, psAttachment? PTR_ERR(psAttachment) : -ENOMEM));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto errDMAPut;
	}

	/*
	 * Get the physical heap for this PMR
	 *
	 * Note:
	 * While we have no way to determine the type of the buffer
	 * we just assume that all dmabufs are from the same
	 * physical heap.
	 */
	eError = PhysHeapAcquire(DMABUF_IMPORT_PHYSHEAP_ID, &psHeap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to acquire physical heap (%s)",
				 __func__, PVRSRVGetErrorStringKM(eError)));
		goto errDMADetach;
	}

	eError = PhysmemCreateNewDmaBufBackedPMR(psDevNode,
	                                         psHeap,
	                                         psAttachment,
	                                         PhysmemDestroyDmaBuf,
	                                         uiFlags,
	                                         uiChunkSize,
	                                         ui32NumPhysChunks,
	                                         ui32NumVirtChunks,
	                                         pui32MappingTable,
	                                         &psPMR);
	if (eError != PVRSRV_OK)
	{
		goto errHeapRelease;
	}

	if (!g_psDmaBufHash)
	{
		/*
		 * As different processes may import the same dmabuf we need to
		 * create a hash table so we don't generate a duplicate PMR but
		 * rather just take a reference on an existing one.
		 */
		g_psDmaBufHash = HASH_Create(DMA_BUF_HASH_SIZE);
		if (!g_psDmaBufHash)
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto errUnrefPMR;
		}
	}

	/* First time we've seen this dmabuf so store it in the hash table */
	HASH_Insert(g_psDmaBufHash, (uintptr_t) psDmaBuf, (uintptr_t) psPMR);
	g_ui32HashRefCount++;

	*ppsPMRPtr = psPMR;
	*puiSize = ui32NumVirtChunks * uiChunkSize;
	*puiAlign = PAGE_SIZE;

	return PVRSRV_OK;

errUnrefPMR:
	PMRUnrefPMR(psPMR);

errHeapRelease:
	PhysHeapRelease(psHeap);

errDMADetach:
	dma_buf_detach(psDmaBuf, psAttachment);

errDMAPut:
	dma_buf_put(psDmaBuf);

errReturn:
	PVR_ASSERT(eError != PVRSRV_OK);
	return eError;
}

#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0) || defined(SUPPORT_ION) */

PVRSRV_ERROR
PhysmemCreateNewDmaBufBackedPMR(PVRSRV_DEVICE_NODE *psDevNode,
                                PHYS_HEAP *psHeap,
                                struct dma_buf_attachment *psAttachment,
                                PFN_DESTROY_DMABUF_PMR pfnDestroy,
                                PVRSRV_MEMALLOCFLAGS_T uiFlags,
                                IMG_DEVMEM_SIZE_T uiChunkSize,
                                IMG_UINT32 ui32NumPhysChunks,
                                IMG_UINT32 ui32NumVirtChunks,
                                IMG_UINT32 *pui32MappingTable,
                                PMR **ppsPMRPtr)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(psHeap);
	PVR_UNREFERENCED_PARAMETER(psAttachment);
	PVR_UNREFERENCED_PARAMETER(pfnDestroy);
	PVR_UNREFERENCED_PARAMETER(uiFlags);
	PVR_UNREFERENCED_PARAMETER(uiChunkSize);
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
	PVR_UNREFERENCED_PARAMETER(ui32NumVirtChunks);
	PVR_UNREFERENCED_PARAMETER(pui32MappingTable);
	PVR_UNREFERENCED_PARAMETER(ppsPMRPtr);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

struct dma_buf *
PhysmemGetDmaBuf(PMR *psPMR)
{
	PVR_UNREFERENCED_PARAMETER(psPMR);

	return NULL;
}

PVRSRV_ERROR
PhysmemExportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    PMR *psPMR,
                    IMG_INT *piFd)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(piFd);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR
PhysmemImportDmaBuf(CONNECTION_DATA *psConnection,
                    PVRSRV_DEVICE_NODE *psDevNode,
                    IMG_INT fd,
                    PVRSRV_MEMALLOCFLAGS_T uiFlags,
                    PMR **ppsPMRPtr,
                    IMG_DEVMEM_SIZE_T *puiSize,
                    IMG_DEVMEM_ALIGN_T *puiAlign)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(fd);
	PVR_UNREFERENCED_PARAMETER(uiFlags);
	PVR_UNREFERENCED_PARAMETER(ppsPMRPtr);
	PVR_UNREFERENCED_PARAMETER(puiSize);
	PVR_UNREFERENCED_PARAMETER(puiAlign);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

PVRSRV_ERROR
PhysmemImportSparseDmaBuf(CONNECTION_DATA *psConnection,
                          PVRSRV_DEVICE_NODE *psDevNode,
                          IMG_INT fd,
                          PVRSRV_MEMALLOCFLAGS_T uiFlags,
                          IMG_DEVMEM_SIZE_T uiChunkSize,
                          IMG_UINT32 ui32NumPhysChunks,
                          IMG_UINT32 ui32NumVirtChunks,
                          IMG_UINT32 *pui32MappingTable,
                          PMR **ppsPMRPtr,
                          IMG_DEVMEM_SIZE_T *puiSize,
                          IMG_DEVMEM_ALIGN_T *puiAlign)
{
	PVR_UNREFERENCED_PARAMETER(psConnection);
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(fd);
	PVR_UNREFERENCED_PARAMETER(uiFlags);
	PVR_UNREFERENCED_PARAMETER(ppsPMRPtr);
	PVR_UNREFERENCED_PARAMETER(puiSize);
	PVR_UNREFERENCED_PARAMETER(puiAlign);
	PVR_UNREFERENCED_PARAMETER(uiChunkSize);
	PVR_UNREFERENCED_PARAMETER(ui32NumPhysChunks);
	PVR_UNREFERENCED_PARAMETER(ui32NumVirtChunks);
	PVR_UNREFERENCED_PARAMETER(pui32MappingTable);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0) || defined(SUPPORT_ION) || defined(KERNEL_HAS_DMABUF_VMAP_MMAP) */
