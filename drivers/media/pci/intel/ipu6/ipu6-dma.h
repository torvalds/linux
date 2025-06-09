/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU6_DMA_H
#define IPU6_DMA_H

#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#include "ipu6-bus.h"

struct ipu6_mmu_info;

struct ipu6_dma_mapping {
	struct ipu6_mmu_info *mmu_info;
	struct iova_domain iovad;
};

void ipu6_dma_sync_single(struct ipu6_bus_device *sys, dma_addr_t dma_handle,
			  size_t size);
void ipu6_dma_sync_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		      int nents);
void ipu6_dma_sync_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt);
void *ipu6_dma_alloc(struct ipu6_bus_device *sys, size_t size,
		     dma_addr_t *dma_handle, gfp_t gfp,
		     unsigned long attrs);
void ipu6_dma_free(struct ipu6_bus_device *sys, size_t size, void *vaddr,
		   dma_addr_t dma_handle, unsigned long attrs);
int ipu6_dma_mmap(struct ipu6_bus_device *sys, struct vm_area_struct *vma,
		  void *addr, dma_addr_t iova, size_t size,
		  unsigned long attrs);
int ipu6_dma_map_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		    int nents, enum dma_data_direction dir,
		    unsigned long attrs);
void ipu6_dma_unmap_sg(struct ipu6_bus_device *sys, struct scatterlist *sglist,
		       int nents, enum dma_data_direction dir,
		       unsigned long attrs);
int ipu6_dma_map_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt,
			 enum dma_data_direction dir, unsigned long attrs);
void ipu6_dma_unmap_sgtable(struct ipu6_bus_device *sys, struct sg_table *sgt,
			    enum dma_data_direction dir, unsigned long attrs);
#endif /* IPU6_DMA_H */
