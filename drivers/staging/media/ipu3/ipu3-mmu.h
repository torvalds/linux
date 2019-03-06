/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */
/* Copyright 2018 Google LLC. */

#ifndef __IPU3_MMU_H
#define __IPU3_MMU_H

/**
 * struct ipu3_mmu_info - Describes mmu geometry
 *
 * @aperture_start:	First address that can be mapped
 * @aperture_end:	Last address that can be mapped
 * @pgsize_bitmap:	Bitmap of page sizes in use
 */
struct ipu3_mmu_info {
	dma_addr_t aperture_start;
	dma_addr_t aperture_end;
	unsigned long pgsize_bitmap;
};

struct device;
struct scatterlist;

struct ipu3_mmu_info *ipu3_mmu_init(struct device *parent, void __iomem *base);
void ipu3_mmu_exit(struct ipu3_mmu_info *info);
void ipu3_mmu_suspend(struct ipu3_mmu_info *info);
void ipu3_mmu_resume(struct ipu3_mmu_info *info);

int ipu3_mmu_map(struct ipu3_mmu_info *info, unsigned long iova,
		 phys_addr_t paddr, size_t size);
size_t ipu3_mmu_unmap(struct ipu3_mmu_info *info, unsigned long iova,
		      size_t size);
size_t ipu3_mmu_map_sg(struct ipu3_mmu_info *info, unsigned long iova,
		       struct scatterlist *sg, unsigned int nents);
#endif
