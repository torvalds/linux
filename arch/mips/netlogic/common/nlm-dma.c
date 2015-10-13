/*
*  Copyright (C) 2003-2013 Broadcom Corporation
*  All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/swiotlb.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/bootinfo.h>

static char *nlm_swiotlb;

static void *nlm_dma_alloc_coherent(struct device *dev, size_t size,
	dma_addr_t *dma_handle, gfp_t gfp, struct dma_attrs *attrs)
{
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);

#ifdef CONFIG_ZONE_DMA32
	if (dev->coherent_dma_mask <= DMA_BIT_MASK(32))
		gfp |= __GFP_DMA32;
#endif

	/* Don't invoke OOM killer */
	gfp |= __GFP_NORETRY;

	return swiotlb_alloc_coherent(dev, size, dma_handle, gfp);
}

static void nlm_dma_free_coherent(struct device *dev, size_t size,
	void *vaddr, dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	swiotlb_free_coherent(dev, size, vaddr, dma_handle);
}

struct dma_map_ops nlm_swiotlb_dma_ops = {
	.alloc = nlm_dma_alloc_coherent,
	.free = nlm_dma_free_coherent,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.mapping_error = swiotlb_dma_mapping_error,
	.dma_supported = swiotlb_dma_supported
};

void __init plat_swiotlb_setup(void)
{
	size_t swiotlbsize;
	unsigned long swiotlb_nslabs;

	swiotlbsize = 1 << 20; /* 1 MB for now */
	swiotlb_nslabs = swiotlbsize >> IO_TLB_SHIFT;
	swiotlb_nslabs = ALIGN(swiotlb_nslabs, IO_TLB_SEGSIZE);
	swiotlbsize = swiotlb_nslabs << IO_TLB_SHIFT;

	nlm_swiotlb = alloc_bootmem_low_pages(swiotlbsize);
	swiotlb_init_with_tbl(nlm_swiotlb, swiotlb_nslabs, 1);
}
