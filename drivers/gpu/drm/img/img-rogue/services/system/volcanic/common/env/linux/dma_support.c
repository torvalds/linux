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
#if defined(__linux__)
#include <linux/device.h>
#include <linux/dma-mapping.h>
#endif

#include "allocmem.h"
#include "dma_support.h"

#define DMA_MAX_IOREMAP_ENTRIES 8
static IMG_BOOL gbEnableDmaIoRemapping = IMG_FALSE;
static DMA_ALLOC gsDmaIoRemapArray[DMA_MAX_IOREMAP_ENTRIES] = {{0}};
static IMG_UINT32 gsDmaIoRemapRef[DMA_MAX_IOREMAP_ENTRIES] = {0};

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

	if (psDmaAlloc != NULL && psDmaAlloc->pvOSDevice != NULL)
	{
#if defined(__linux__)
		psDmaAlloc->pvVirtAddr =
				dma_alloc_coherent((struct device *)psDmaAlloc->pvOSDevice,
								   (size_t) psDmaAlloc->ui64Size,
								   (dma_addr_t *)&psDmaAlloc->sBusAddr.uiAddr,
								   GFP_KERNEL);
		PVR_LOG_RETURN_IF_FALSE((NULL != psDmaAlloc->pvVirtAddr), "dma_alloc_coherent() failed", PVRSRV_ERROR_FAILED_TO_ALLOC_PAGES);
#else
		#error "Provide OS implementation of DMA allocation";
#endif
	}

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
	if (psDmaAlloc && psDmaAlloc->pvVirtAddr)
	{
#if defined(__linux__)
		dma_free_coherent((struct device *)psDmaAlloc->pvOSDevice,
						  (size_t) psDmaAlloc->ui64Size,
						  psDmaAlloc->pvVirtAddr,
						  (dma_addr_t )psDmaAlloc->sBusAddr.uiAddr);
#else
		#error "Provide OS implementation of DMA deallocation";
#endif
	}
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
	IMG_UINT32 ui32Idx;
	PVRSRV_ERROR eError = PVRSRV_ERROR_TOO_FEW_BUFFERS;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->sBusAddr.uiAddr == 0)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	else if (psDmaAlloc->pvVirtAddr == NULL)
	{
		/* Check if an I/O remap entry already exists for this request */
		for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
		{
			if (gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr &&
				gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr <= psDmaAlloc->sBusAddr.uiAddr &&
				gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr + gsDmaIoRemapArray[ui32Idx].ui64Size >= psDmaAlloc->sBusAddr.uiAddr + psDmaAlloc->ui64Size)
			{
				PVR_ASSERT(gsDmaIoRemapArray[ui32Idx].pvVirtAddr);
				break;
			}
		}

		if (ui32Idx < DMA_MAX_IOREMAP_ENTRIES)
		{
			IMG_UINT64 ui64Offset;
			ui64Offset = psDmaAlloc->sBusAddr.uiAddr - gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr;
			psDmaAlloc->pvVirtAddr = gsDmaIoRemapArray[ui32Idx].pvVirtAddr + (uintptr_t)ui64Offset;
			gsDmaIoRemapRef[ui32Idx] += 1;
			return PVRSRV_OK;
		}

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Check if there is a free I/O remap table entry for this request */
	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		if (gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr == 0)
		{
			PVR_ASSERT(gsDmaIoRemapArray[ui32Idx].pvVirtAddr == NULL);
			PVR_ASSERT(gsDmaIoRemapArray[ui32Idx].ui64Size == 0);
			break;
		}
	}

	if (ui32Idx >= DMA_MAX_IOREMAP_ENTRIES)
	{
		return eError;
	}

	gsDmaIoRemapArray[ui32Idx].ui64Size = psDmaAlloc->ui64Size;
	gsDmaIoRemapArray[ui32Idx].sBusAddr = psDmaAlloc->sBusAddr;
	gsDmaIoRemapArray[ui32Idx].pvVirtAddr = psDmaAlloc->pvVirtAddr;
	gsDmaIoRemapRef[ui32Idx] += 1;

	PVR_LOG(("DMA: register I/O remap: VA: 0x%p, PA: 0x%llx, Size: 0x%llx",
			psDmaAlloc->pvVirtAddr,
			psDmaAlloc->sBusAddr.uiAddr,
			psDmaAlloc->ui64Size));

	gbEnableDmaIoRemapping = IMG_TRUE;

	return PVRSRV_OK;
}

/*!
******************************************************************************
 @Function			SysDmaDeregisterForIoRemapping

 @Description		Deregisters DMA_ALLOC from manual I/O remapping

 @Return			void
 ******************************************************************************/
void SysDmaDeregisterForIoRemapping(DMA_ALLOC *psDmaAlloc)
{
	IMG_UINT32 ui32Idx;

	if (psDmaAlloc == NULL ||
		psDmaAlloc->ui64Size == 0 ||
		psDmaAlloc->pvVirtAddr == NULL ||
		psDmaAlloc->sBusAddr.uiAddr == 0)
	{
		return;
	}

	/* Remove specified entry from the list of I/O remap entries */
	for (ui32Idx = 0; ui32Idx < DMA_MAX_IOREMAP_ENTRIES; ++ui32Idx)
	{
		if (gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr &&
			gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr <= psDmaAlloc->sBusAddr.uiAddr &&
			gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr + gsDmaIoRemapArray[ui32Idx].ui64Size >= psDmaAlloc->sBusAddr.uiAddr + psDmaAlloc->ui64Size)
		{
			if (! --gsDmaIoRemapRef[ui32Idx])
			{
				PVR_LOG(("DMA: deregister I/O remap: VA: 0x%p, PA: 0x%llx, Size: 0x%llx",
						gsDmaIoRemapArray[ui32Idx].pvVirtAddr,
						gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr,
						gsDmaIoRemapArray[ui32Idx].ui64Size));

				gsDmaIoRemapArray[ui32Idx].sBusAddr.uiAddr = 0;
				gsDmaIoRemapArray[ui32Idx].pvVirtAddr = NULL;
				gsDmaIoRemapArray[ui32Idx].ui64Size = 0;
			}

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
