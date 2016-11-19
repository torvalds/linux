/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "rxe.h"
#include "rxe_loc.h"

#define DMA_BAD_ADDER ((u64)0)

static int rxe_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	return dma_addr == DMA_BAD_ADDER;
}

static u64 rxe_dma_map_single(struct ib_device *dev,
			      void *cpu_addr, size_t size,
			      enum dma_data_direction direction)
{
	WARN_ON(!valid_dma_direction(direction));
	return (uintptr_t)cpu_addr;
}

static void rxe_dma_unmap_single(struct ib_device *dev,
				 u64 addr, size_t size,
				 enum dma_data_direction direction)
{
	WARN_ON(!valid_dma_direction(direction));
}

static u64 rxe_dma_map_page(struct ib_device *dev,
			    struct page *page,
			    unsigned long offset,
			    size_t size, enum dma_data_direction direction)
{
	u64 addr;

	WARN_ON(!valid_dma_direction(direction));

	if (offset + size > PAGE_SIZE) {
		addr = DMA_BAD_ADDER;
		goto done;
	}

	addr = (uintptr_t)page_address(page);
	if (addr)
		addr += offset;

done:
	return addr;
}

static void rxe_dma_unmap_page(struct ib_device *dev,
			       u64 addr, size_t size,
			       enum dma_data_direction direction)
{
	WARN_ON(!valid_dma_direction(direction));
}

static int rxe_map_sg(struct ib_device *dev, struct scatterlist *sgl,
		      int nents, enum dma_data_direction direction)
{
	struct scatterlist *sg;
	u64 addr;
	int i;
	int ret = nents;

	WARN_ON(!valid_dma_direction(direction));

	for_each_sg(sgl, sg, nents, i) {
		addr = (uintptr_t)page_address(sg_page(sg));
		if (!addr) {
			ret = 0;
			break;
		}
		sg->dma_address = addr + sg->offset;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		sg->dma_length = sg->length;
#endif
	}

	return ret;
}

static void rxe_unmap_sg(struct ib_device *dev,
			 struct scatterlist *sg, int nents,
			 enum dma_data_direction direction)
{
	WARN_ON(!valid_dma_direction(direction));
}

static int rxe_map_sg_attrs(struct ib_device *dev, struct scatterlist *sgl,
			    int nents, enum dma_data_direction direction,
			    unsigned long attrs)
{
	return rxe_map_sg(dev, sgl, nents, direction);
}

static void rxe_unmap_sg_attrs(struct ib_device *dev,
			       struct scatterlist *sg, int nents,
			       enum dma_data_direction direction,
			       unsigned long attrs)
{
	rxe_unmap_sg(dev, sg, nents, direction);
}

static void rxe_sync_single_for_cpu(struct ib_device *dev,
				    u64 addr,
				    size_t size, enum dma_data_direction dir)
{
}

static void rxe_sync_single_for_device(struct ib_device *dev,
				       u64 addr,
				       size_t size, enum dma_data_direction dir)
{
}

static void *rxe_dma_alloc_coherent(struct ib_device *dev, size_t size,
				    u64 *dma_handle, gfp_t flag)
{
	struct page *p;
	void *addr = NULL;

	p = alloc_pages(flag, get_order(size));
	if (p)
		addr = page_address(p);

	if (dma_handle)
		*dma_handle = (uintptr_t)addr;

	return addr;
}

static void rxe_dma_free_coherent(struct ib_device *dev, size_t size,
				  void *cpu_addr, u64 dma_handle)
{
	free_pages((unsigned long)cpu_addr, get_order(size));
}

struct ib_dma_mapping_ops rxe_dma_mapping_ops = {
	.mapping_error		= rxe_mapping_error,
	.map_single		= rxe_dma_map_single,
	.unmap_single		= rxe_dma_unmap_single,
	.map_page		= rxe_dma_map_page,
	.unmap_page		= rxe_dma_unmap_page,
	.map_sg			= rxe_map_sg,
	.unmap_sg		= rxe_unmap_sg,
	.map_sg_attrs		= rxe_map_sg_attrs,
	.unmap_sg_attrs		= rxe_unmap_sg_attrs,
	.sync_single_for_cpu	= rxe_sync_single_for_cpu,
	.sync_single_for_device	= rxe_sync_single_for_device,
	.alloc_coherent		= rxe_dma_alloc_coherent,
	.free_coherent		= rxe_dma_free_coherent
};
