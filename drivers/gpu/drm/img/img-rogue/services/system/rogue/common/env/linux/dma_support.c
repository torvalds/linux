/*************************************************************************/ /*!
@File           dma_support.c
@Title          System DMA support
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides a contiguous memory allocator (i.e. DMA allocator);
                APIs are used for allocation/ioremapping (DMA/PA <-> CPU/VA)
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
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <asm-generic/getorder.h>

#include "allocmem.h"
#include "dma_support.h"
#include "pvr_vmap.h"
#include "kernel_compatibility.h"

#define DMA_MAX_IOREMAP_ENTRIES 2
static IMG_BOOL gbEnableDmaIoRemapping = IMG_FALSE;
static DMA_ALLOC gsDmaIoRemapArray[DMA_MAX_IOREMAP_ENTRIES] = {{0}};

extern void do_invalid_range(unsigned long start, unsigned long len);

static void*
SysDmaAcquireKernelAddress(struct page *psPage, IMG_UINT64 ui64Size, DMA_ALLOC *psDmaAlloc)
{
	IMG_BOOL bPageByPage = IMG_TRUE;
	IMG_UINT32 uiIdx;
	void *pvVirtAddr = NULL;
	IMG_UINT32 ui32PgCount = (IMG_UINT32)(ui64Size >> OSGetPageShift());
	PVRSRV_DEVICE_NODE *psDevNode = OSAllocZMemNoStats(sizeof(*psDevNode));
	PVRSRV_DEVICE_CONFIG *psDevConfig = OSAllocZMemNoStats(sizeof(*psDevConfig));
	struct page **pagearray = OSAllocZMemNoStats(ui32PgCount * sizeof(struct page *));
	void *pvOSDevice = psDmaAlloc->pvOSDevice;
#if defined(CONFIG_ARM64)
	pgprot_t prot = pgprot_writecombine(PAGE_KERNEL);
#else
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);
#endif

	/* Validate all required dynamic tmp buffer allocations */
	if (psDevNode == NULL || psDevConfig == NULL || pagearray == NULL)
	{
		if (psDevNode)
		{
			OSFreeMem(psDevNode);
		}

		if (psDevConfig)
		{
			OSFreeMem(psDevConfig);
		}

		if (pagearray)
		{
			OSFreeMem(pagearray);
		}

		goto e0;
	}

	/* Fake psDevNode->psDevConfig->pvOSDevice */
	psDevConfig->pvOSDevice = pvOSDevice;
	psDevNode->psDevConfig = psDevConfig;

	/* Evict any page data contents from d-cache */
	for (uiIdx = 0; uiIdx < ui32PgCount; uiIdx++)
	{
		void *pvVirtStart, *pvVirtEnd;
		IMG_CPU_PHYADDR sCPUPhysStart, sCPUPhysEnd;

		/* Prepare array required for vmap */
		pagearray[uiIdx] = &psPage[uiIdx];

		if (bPageByPage)
		{
#if defined(CONFIG_64BIT)
			bPageByPage = IMG_FALSE;

			pvVirtStart = kmap(&psPage[uiIdx]);
			pvVirtEnd = pvVirtStart + ui64Size;

			sCPUPhysStart.uiAddr = page_to_phys(&psPage[uiIdx]);
			sCPUPhysEnd.uiAddr = sCPUPhysStart.uiAddr + ui64Size;
			/* all pages have a kernel linear address, flush entire range */
#else
			pvVirtStart = kmap(&psPage[uiIdx]);
			pvVirtEnd = pvVirtStart + PAGE_SIZE;

			sCPUPhysStart.uiAddr = page_to_phys(&psPage[uiIdx]);
			sCPUPhysEnd.uiAddr = sCPUPhysStart.uiAddr + PAGE_SIZE;
			/* pages might be from HIGHMEM, need to kmap/flush per page */
#endif

			/* Fallback to range-based d-cache flush */
			OSCPUCacheInvalidateRangeKM(psDevNode,
										pvVirtStart, pvVirtEnd,
										sCPUPhysStart, sCPUPhysEnd);

			kunmap(&psPage[uiIdx]);
		}
	}

    do_invalid_range(0x0, 0x200000);

	/* Remap pages into VMALLOC space */
	pvVirtAddr = pvr_vmap(pagearray, ui32PgCount, VM_MAP, prot);
	psDmaAlloc->PageProps = prot;

	/* Clean-up tmp buffers */
	OSFreeMem(psDevConfig);
	OSFreeMem(psDevNode);
	OSFreeMem(pagearray);

e0:
	return pvVirtAddr;
}

static void SysDmaReleaseKernelAddress(void *pvVirtAddr, IMG_UINT64 ui64Size, pgprot_t pgprot)
{
	pvr_vunmap(pvVirtAddr, ui64Size >> OSGetPageShift(), pgprot);
}

/*!
******************************************************************************
 @Function			SysDmaAllocMem

 @Description		Allocates physically contiguous memory

 @Return			PVRSRV_ERROR	PVRSRV_OK on success. Otherwise, a PVRSRV_
									error code
 ******************************************************************************/
PVRSRV_ERROR SysDmaAllocMem(DMA_ALLOC *psDmaAlloc)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	struct device *psDev;
	struct page *psPage;
	size_t uiSize;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->hHandle ||
		psDmaAlloc->pvVirtAddr ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->sBusAddr.uiAddr ||
		psDmaAlloc->pvOSDevice == NULL)
	{
		PVR_LOG_IF_FALSE((IMG_FALSE), "Invalid parameter");
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	uiSize = PVR_ALIGN(psDmaAlloc->ui64Size, PAGE_SIZE);
	psDev = (struct device *)psDmaAlloc->pvOSDevice;

	psDmaAlloc->hHandle = dma_alloc_coherent(psDev, uiSize, (dma_addr_t *)&psDmaAlloc->sBusAddr.uiAddr, GFP_KERNEL);

	if (psDmaAlloc->hHandle)
	{
		psDmaAlloc->pvVirtAddr = psDmaAlloc->hHandle;

		PVR_DPF((PVR_DBG_MESSAGE,
				"Allocated DMA buffer V:0x%p P:0x%llx S:0x"IMG_SIZE_FMTSPECX,
				psDmaAlloc->pvVirtAddr,
				psDmaAlloc->sBusAddr.uiAddr,
				uiSize));
	}
	else if ((psPage = alloc_pages(GFP_KERNEL, get_order(uiSize))))
	{
		psDmaAlloc->sBusAddr.uiAddr = dma_map_page(psDev, psPage, 0, uiSize, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(psDev, psDmaAlloc->sBusAddr.uiAddr))
		{
			PVR_DPF((PVR_DBG_ERROR,
					"dma_map_page() failed, page 0x%p order %d",
					psPage,
					get_order(uiSize)));
			__free_pages(psPage, get_order(uiSize));
			goto e0;
		}
		psDmaAlloc->psPage = psPage;

		psDmaAlloc->pvVirtAddr = SysDmaAcquireKernelAddress(psPage, uiSize, psDmaAlloc);
		if (! psDmaAlloc->pvVirtAddr)
		{
			PVR_DPF((PVR_DBG_ERROR,
					"SysDmaAcquireKernelAddress() failed, page 0x%p order %d",
					psPage,
					get_order(uiSize)));
			dma_unmap_page(psDev, psDmaAlloc->sBusAddr.uiAddr, uiSize, DMA_BIDIRECTIONAL);
			__free_pages(psPage, get_order(uiSize));
			goto e0;
		}

		PVR_DPF((PVR_DBG_MESSAGE,
				"Allocated contiguous buffer V:0x%p P:0x%llx S:0x"IMG_SIZE_FMTSPECX,
				psDmaAlloc->pvVirtAddr,
				psDmaAlloc->sBusAddr.uiAddr,
				uiSize));
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,	"Unable to allocate contiguous buffer, size: 0x"IMG_SIZE_FMTSPECX, uiSize));
		eError = PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES;
	}

e0:
	PVR_LOG_RETURN_IF_FALSE((psDmaAlloc->pvVirtAddr), "DMA/CMA allocation failed", PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES);
	return eError;
}

/*!
******************************************************************************
 @Function			SysDmaFreeMem

 @Description		Free physically contiguous memory

 @Return			void
 ******************************************************************************/
void SysDmaFreeMem(DMA_ALLOC *psDmaAlloc)
{
	size_t uiSize;
	struct device *psDev;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->pvOSDevice == NULL ||
		psDmaAlloc->pvVirtAddr == NULL ||
		psDmaAlloc->sBusAddr.uiAddr == 0)
	{
		PVR_LOG_IF_FALSE((IMG_FALSE), "Invalid parameter");
		return;
	}

	uiSize = PVR_ALIGN(psDmaAlloc->ui64Size, PAGE_SIZE);
	psDev = (struct device *)psDmaAlloc->pvOSDevice;

	if (psDmaAlloc->pvVirtAddr != psDmaAlloc->hHandle)
	{
		SysDmaReleaseKernelAddress(psDmaAlloc->pvVirtAddr, uiSize, psDmaAlloc->PageProps);
	}

	if (! psDmaAlloc->hHandle)
	{
		struct page *psPage;
		dma_unmap_page(psDev, psDmaAlloc->sBusAddr.uiAddr, uiSize, DMA_BIDIRECTIONAL);
		psPage = psDmaAlloc->psPage;
		__free_pages(psPage, get_order(uiSize));
		return;
	}

	dma_free_coherent(psDev, uiSize, psDmaAlloc->hHandle, (dma_addr_t )psDmaAlloc->sBusAddr.uiAddr);
}

/*!
******************************************************************************
 @Function			SysDmaRegisterForIoRemapping

 @Description		Registers DMA_ALLOC for manual I/O remapping

 @Return			PVRSRV_ERROR	PVRSRV_OK on success. Otherwise, a PVRSRV_
									error code
 ******************************************************************************/
PVRSRV_ERROR SysDmaRegisterForIoRemapping(DMA_ALLOC *psDmaAlloc)
{
	size_t uiSize;
	IMG_UINT32 ui32Idx;
	IMG_BOOL bTabEntryFound = IMG_TRUE;
	PVRSRV_ERROR eError = PVRSRV_ERROR_TOO_FEW_BUFFERS;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->pvOSDevice == NULL ||
		psDmaAlloc->pvVirtAddr == NULL ||
		psDmaAlloc->sBusAddr.uiAddr == 0)
	{
		PVR_LOG_IF_FALSE((IMG_FALSE), "Invalid parameter");
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	uiSize = PVR_ALIGN(psDmaAlloc->ui64Size, PAGE_SIZE);

	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		/* Check if an I/O remap entry exists for remapping */
		if (gsDmaIoRemapArray[ui32Idx].pvVirtAddr == NULL)
		{
			PVR_ASSERT(gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr == 0);
			PVR_ASSERT(gsDmaIoRemapArray[ui32Idx].ui64Size == 0);
			break;
		}
	}

	if (ui32Idx >= DMA_MAX_IOREMAP_ENTRIES)
	{
		bTabEntryFound = IMG_FALSE;
	}

	if (bTabEntryFound)
	{
		IMG_BOOL bSameVAddr, bSamePAddr, bSameSize;

		bSamePAddr = gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr == psDmaAlloc->sBusAddr.uiAddr;
		bSameVAddr = gsDmaIoRemapArray[ui32Idx].pvVirtAddr == psDmaAlloc->pvVirtAddr;
		bSameSize = gsDmaIoRemapArray[ui32Idx].ui64Size == uiSize;

		if (bSameVAddr)
		{
			if (bSamePAddr && bSameSize)
			{
				eError = PVRSRV_OK;
			}
			else
			{
				eError = PVRSRV_ERROR_ALREADY_EXISTS;
			}
		}
		else
		{
			PVR_ASSERT(bSamePAddr == IMG_FALSE);

			gsDmaIoRemapArray[ui32Idx].ui64Size = uiSize;
			gsDmaIoRemapArray[ui32Idx].sBusAddr = psDmaAlloc->sBusAddr;
			gsDmaIoRemapArray[ui32Idx].pvVirtAddr = psDmaAlloc->pvVirtAddr;

			PVR_DPF((PVR_DBG_MESSAGE,
					"DMA: register I/O remap: "
					"VA: 0x%p, PA: 0x%llx, Size: 0x"IMG_SIZE_FMTSPECX,
					psDmaAlloc->pvVirtAddr,
					psDmaAlloc->sBusAddr.uiAddr,
					uiSize));

			gbEnableDmaIoRemapping = IMG_TRUE;
			eError = PVRSRV_OK;
		}
	}

	return eError;
}

/*!
******************************************************************************
 @Function			SysDmaDeregisterForIoRemapping

 @Description		Deregisters DMA_ALLOC from manual I/O remapping

 @Return			void
 ******************************************************************************/
void SysDmaDeregisterForIoRemapping(DMA_ALLOC *psDmaAlloc)
{
	size_t uiSize;
	IMG_UINT32 ui32Idx;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->pvOSDevice == NULL ||
		psDmaAlloc->pvVirtAddr == NULL ||
		psDmaAlloc->sBusAddr.uiAddr == 0)
	{
		PVR_LOG_IF_FALSE((IMG_FALSE), "Invalid parameter");
		return;
	}

	uiSize = PVR_ALIGN(psDmaAlloc->ui64Size, PAGE_SIZE);

	/* Remove specified entries from list of I/O remap entries */
	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		if (gsDmaIoRemapArray[ui32Idx].pvVirtAddr == psDmaAlloc->pvVirtAddr)
		{
			gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr = 0;
			gsDmaIoRemapArray[ui32Idx].pvVirtAddr = NULL;
			gsDmaIoRemapArray[ui32Idx].ui64Size = 0;

			PVR_DPF((PVR_DBG_MESSAGE,
					"DMA: deregister I/O remap: "
					"VA: 0x%p, PA: 0x%llx, Size: 0x"IMG_SIZE_FMTSPECX,
					psDmaAlloc->pvVirtAddr,
					psDmaAlloc->sBusAddr.uiAddr,
					uiSize));

			break;
		}
	}

	/* Check if no other I/O remap entries exists for remapping */
	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		if (gsDmaIoRemapArray[ui32Idx].pvVirtAddr != NULL)
		{
			break;
		}
	}

	if (ui32Idx == DMA_MAX_IOREMAP_ENTRIES)
	{
		/* No entries found so disable remapping */
		gbEnableDmaIoRemapping = IMG_FALSE;
	}
}

/*!
******************************************************************************
 @Function			SysDmaDevPAddrToCpuVAddr

 @Description		Maps a DMA_ALLOC physical address to CPU virtual address

 @Return			IMG_CPU_VIRTADDR on success. Otherwise, a NULL
 ******************************************************************************/
IMG_CPU_VIRTADDR SysDmaDevPAddrToCpuVAddr(IMG_UINT64 uiAddr, IMG_UINT64 ui64Size)
{
	IMG_CPU_VIRTADDR pvDMAVirtAddr = NULL;
	DMA_ALLOC *psHeapDmaAlloc;
	IMG_UINT32 ui32Idx;

	if (gbEnableDmaIoRemapping == IMG_FALSE)
	{
		return pvDMAVirtAddr;
	}

	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		psHeapDmaAlloc = &gsDmaIoRemapArray[ui32Idx];
		if (psHeapDmaAlloc->sBusAddr.uiAddr && uiAddr >= psHeapDmaAlloc->sBusAddr.uiAddr)
		{
			IMG_UINT64 uiSpan = psHeapDmaAlloc->ui64Size;
			IMG_UINT64 uiOffset = uiAddr - psHeapDmaAlloc->sBusAddr.uiAddr;

			if (uiOffset < uiSpan)
			{
				PVR_ASSERT((uiOffset+ui64Size-1) < uiSpan);
				pvDMAVirtAddr = psHeapDmaAlloc->pvVirtAddr + uiOffset;

				PVR_DPF((PVR_DBG_MESSAGE,
					"DMA: remap: PA: 0x%llx => VA: 0x%p",
					uiAddr, pvDMAVirtAddr));

				break;
			}
		}
	}

	return pvDMAVirtAddr;
}

/*!
******************************************************************************
 @Function			SysDmaCpuVAddrToDevPAddr

 @Description		Maps a DMA_ALLOC CPU virtual address to physical address

 @Return			Non-zero value on success. Otherwise, a 0
 ******************************************************************************/
IMG_UINT64 SysDmaCpuVAddrToDevPAddr(IMG_CPU_VIRTADDR pvDMAVirtAddr)
{
	IMG_UINT64 uiAddr = 0;
	DMA_ALLOC *psHeapDmaAlloc;
	IMG_UINT32 ui32Idx;

	if (gbEnableDmaIoRemapping == IMG_FALSE)
	{
		return uiAddr;
	}

	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		psHeapDmaAlloc = &gsDmaIoRemapArray[ui32Idx];
		if (psHeapDmaAlloc->pvVirtAddr && pvDMAVirtAddr >= psHeapDmaAlloc->pvVirtAddr)
		{
			IMG_UINT64 uiSpan = psHeapDmaAlloc->ui64Size;
			IMG_UINT64 uiOffset = pvDMAVirtAddr - psHeapDmaAlloc->pvVirtAddr;

			if (uiOffset < uiSpan)
			{
				uiAddr = psHeapDmaAlloc->sBusAddr.uiAddr + uiOffset;

				PVR_DPF((PVR_DBG_MESSAGE,
					"DMA: remap: VA: 0x%p => PA: 0x%llx",
					pvDMAVirtAddr, uiAddr));

				break;
			}
		}
	}

	return uiAddr;
}

/******************************************************************************
 End of file (dma_support.c)
******************************************************************************/
