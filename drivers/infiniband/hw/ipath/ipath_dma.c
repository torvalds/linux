/*
 * Copyright (c) 2006 QLogic, Corporation. All rights reserved.
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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <rdma/ib_verbs.h>

#include "ipath_verbs.h"

#define BAD_DMA_ADDRESS ((u64) 0)

/*
 * The following functions implement driver specific replacements
 * for the ib_dma_*() functions.
 *
 * These functions return kernel virtual addresses instead of
 * device bus addresses since the driver uses the CPU to copy
 * data instead of using hardware DMA.
 */

static int ipath_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	return dma_addr == BAD_DMA_ADDRESS;
}

static u64 ipath_dma_map_single(struct ib_device *dev,
			        void *cpu_addr, size_t size,
			        enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return (u64) cpu_addr;
}

static void ipath_dma_unmap_single(struct ib_device *dev,
				   u64 addr, size_t size,
				   enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}

static u64 ipath_dma_map_page(struct ib_device *dev,
			      struct page *page,
			      unsigned long offset,
			      size_t size,
			      enum dma_data_direction direction)
{
	u64 addr;

	BUG_ON(!valid_dma_direction(direction));

	if (offset + size > PAGE_SIZE) {
		addr = BAD_DMA_ADDRESS;
		goto done;
	}

	addr = (u64) page_address(page);
	if (addr)
		addr += offset;
	/* TODO: handle highmem pages */

done:
	return addr;
}

static void ipath_dma_unmap_page(struct ib_device *dev,
				 u64 addr, size_t size,
				 enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}

static int ipath_map_sg(struct ib_device *dev, struct scatterlist *sgl,
			int nents, enum dma_data_direction direction)
{
	struct scatterlist *sg;
	u64 addr;
	int i;
	int ret = nents;

	BUG_ON(!valid_dma_direction(direction));

	for_each_sg(sgl, sg, nents, i) {
		addr = (u64) page_address(sg_page(sg));
		/* TODO: handle highmem pages */
		if (!addr) {
			ret = 0;
			break;
		}
	}
	return ret;
}

static void ipath_unmap_sg(struct ib_device *dev,
			   struct scatterlist *sg, int nents,
			   enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}

static u64 ipath_sg_dma_address(struct ib_device *dev, struct scatterlist *sg)
{
	u64 addr = (u64) page_address(sg_page(sg));

	if (addr)
		addr += sg->offset;
	return addr;
}

static unsigned int ipath_sg_dma_len(struct ib_device *dev,
				     struct scatterlist *sg)
{
	return sg->length;
}

static void ipath_sync_single_for_cpu(struct ib_device *dev,
				      u64 addr,
				      size_t size,
				      enum dma_data_direction dir)
{
}

static void ipath_sync_single_for_device(struct ib_device *dev,
					 u64 addr,
					 size_t size,
					 enum dma_data_direction dir)
{
}

static void *ipath_dma_alloc_coherent(struct ib_device *dev, size_t size,
				      u64 *dma_handle, gfp_t flag)
{
	struct page *p;
	void *addr = NULL;

	p = alloc_pages(flag, get_order(size));
	if (p)
		addr = page_address(p);
	if (dma_handle)
		*dma_handle = (u64) addr;
	return addr;
}

static void ipath_dma_free_coherent(struct ib_device *dev, size_t size,
				    void *cpu_addr, u64 dma_handle)
{
	free_pages((unsigned long) cpu_addr, get_order(size));
}

struct ib_dma_mapping_ops ipath_dma_mapping_ops = {
	ipath_mapping_error,
	ipath_dma_map_single,
	ipath_dma_unmap_single,
	ipath_dma_map_page,
	ipath_dma_unmap_page,
	ipath_map_sg,
	ipath_unmap_sg,
	ipath_sg_dma_address,
	ipath_sg_dma_len,
	ipath_sync_single_for_cpu,
	ipath_sync_single_for_device,
	ipath_dma_alloc_coherent,
	ipath_dma_free_coherent
};
