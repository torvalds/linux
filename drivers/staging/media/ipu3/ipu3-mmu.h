/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */
/* Copyright 2018 Google LLC. */

#ifndef __IPU3_MMU_H
#define __IPU3_MMU_H

/**
 * struct imgu_mmu_info - Describes mmu geometry
 *
 * @aperture_start:	First address that can be mapped
 * @aperture_end:	Last address that can be mapped
 * @pgsize_bitmap:	Bitmap of page sizes in use
 */
struct imgu_mmu_info {
	dma_addr_t aperture_start;
	dma_addr_t aperture_end;
	unsigned long pgsize_bitmap;
};

struct device;
struct scatterlist;

struct imgu_mmu_info *imgu_mmu_init(struct device *parent, void __iomem *base);
void imgu_mmu_exit(struct imgu_mmu_info *info);
void imgu_mmu_suspend(struct imgu_mmu_info *info);
void imgu_mmu_resume(struct imgu_mmu_info *info);

int imgu_mmu_map(struct imgu_mmu_info *info, unsigned long iova,
		 phys_addr_t paddr, size_t size);
size_t imgu_mmu_unmap(struct imgu_mmu_info *info, unsigned long iova,
		      size_t size);
size_t imgu_mmu_map_sg(struct imgu_mmu_info *info, unsigned long iova,
		       struct scatterlist *sg, unsigned int nents);
#endif
