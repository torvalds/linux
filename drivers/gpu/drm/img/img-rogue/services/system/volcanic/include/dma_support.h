/*************************************************************************/ /*!
@File           dma_support.h
@Title          Device contiguous memory allocator and I/O re-mapper
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides a contiguous memory allocator API; mainly
                used for allocating / ioremapping (DMA/PA <-> CPU/VA)
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

#ifndef DMA_SUPPORT_H
#define DMA_SUPPORT_H

#include "osfunc.h"
#include "pvrsrv.h"

typedef struct _DMA_ALLOC_
{
	IMG_UINT64       ui64Size;
	IMG_CPU_VIRTADDR pvVirtAddr;
	IMG_DEV_PHYADDR  sBusAddr;
	void             *pvOSDevice;
} DMA_ALLOC;

/*!
*******************************************************************************
 @Function      SysDmaAllocMem
 @Description   Allocates physically contiguous memory
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR SysDmaAllocMem(DMA_ALLOC *psDmaAlloc);

/*!
*******************************************************************************
 @Function      SysDmaFreeMem
 @Description   Free physically contiguous memory
 @Return        void
******************************************************************************/
void SysDmaFreeMem(DMA_ALLOC *psCmaAlloc);

/*!
*******************************************************************************
 @Function      SysDmaRegisterForIoRemapping
 @Description   Registers DMA_ALLOC for manual I/O remapping
 @Return        PVRSRV_OK on success. Otherwise, a PVRSRV error code
******************************************************************************/
PVRSRV_ERROR SysDmaRegisterForIoRemapping(DMA_ALLOC *psPhysHeapDmaAlloc);

/*!
*******************************************************************************
 @Function      SysDmaDeregisterForIoRemapping
 @Description   Deregisters DMA_ALLOC from manual I/O remapping
 @Return        void
******************************************************************************/
void SysDmaDeregisterForIoRemapping(DMA_ALLOC *psPhysHeapDmaAlloc);

/*!
*******************************************************************************
 @Function      SysDmaDevPAddrToCpuVAddr
 @Description   Maps a DMA_ALLOC physical address to CPU virtual address
 @Return        IMG_CPU_VIRTADDR on success. Otherwise, a NULL
******************************************************************************/
IMG_CPU_VIRTADDR
SysDmaDevPAddrToCpuVAddr(IMG_UINT64 uiAddr, IMG_UINT64 ui64Size);

/*!
*******************************************************************************
 @Function      SysDmaCpuVAddrToDevPAddr
 @Description   Maps a DMA_ALLOC CPU virtual address to physical address
 @Return        Non-zero value on success. Otherwise, a 0
******************************************************************************/
IMG_UINT64 SysDmaCpuVAddrToDevPAddr(IMG_CPU_VIRTADDR pvDMAVirtAddr);

#endif /* DMA_SUPPORT_H */

/******************************************************************************
 End of file (dma_support.h)
******************************************************************************/
