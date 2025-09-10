/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2025 Intel Corporation */

#ifndef IPU7_DMA_H
#define IPU7_DMA_H

#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#include "ipu7-bus.h"

#define DMA_ATTR_RESERVE_REGION		BIT(31)
struct ipu7_mmu_info;

struct ipu7_dma_mapping {
	struct ipu7_mmu_info *mmu_info;
	struct iova_domain iovad;
};

void ipu7_dma_sync_single(struct ipu7_bus_device *sys, dma_addr_t dma_handle,
			  size_t size);
void ipu7_dma_sync_sg(struct ipu7_bus_device *sys, struct scatterlist *sglist,
		      unsigned int nents);
void ipu7_dma_sync_sgtable(struct ipu7_bus_device *sys, struct sg_table *sgt);
void *ipu7_dma_alloc(struct ipu7_bus_device *sys, size_t size,
		     dma_addr_t *dma_handle, gfp_t gfp,
		     unsigned long attrs);
void ipu7_dma_free(struct ipu7_bus_device *sys, size_t size, void *vaddr,
		   dma_addr_t dma_handle, unsigned long attrs);
int ipu7_dma_mmap(struct ipu7_bus_device *sys, struct vm_area_struct *vma,
		  void *addr, dma_addr_t iova, size_t size,
		  unsigned long attrs);
int ipu7_dma_map_sg(struct ipu7_bus_device *sys, struct scatterlist *sglist,
		    int nents, enum dma_data_direction dir,
		    unsigned long attrs);
void ipu7_dma_unmap_sg(struct ipu7_bus_device *sys, struct scatterlist *sglist,
		       int nents, enum dma_data_direction dir,
		       unsigned long attrs);
int ipu7_dma_map_sgtable(struct ipu7_bus_device *sys, struct sg_table *sgt,
			 enum dma_data_direction dir, unsigned long attrs);
void ipu7_dma_unmap_sgtable(struct ipu7_bus_device *sys, struct sg_table *sgt,
			    enum dma_data_direction dir, unsigned long attrs);
#endif /* IPU7_DMA_H */
