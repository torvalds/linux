/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author:
 *  Cerf Yu <cerf.yu@rock-chips.com>
 *  Huang Lee <Putin.li@rock-chips.com>
 */
#ifndef __RGA3_DMA_BUF_H__
#define __RGA3_DMA_BUF_H__

#include "rga_drv.h"

#ifndef for_each_sgtable_sg
/*
 * Loop over each sg element in the given sg_table object.
 */
#define for_each_sgtable_sg(sgt, sg, i)		\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#endif

int rga_buf_size_cal(unsigned long yrgb_addr, unsigned long uv_addr,
		      unsigned long v_addr, int format, uint32_t w,
		      uint32_t h, unsigned long *StartAddr, unsigned long *size);

int rga_virtual_memory_check(void *vaddr, u32 w, u32 h, u32 format, int fd);
int rga_dma_memory_check(struct rga_dma_buffer *rga_dma_buffer, struct rga_img_info_t *img);

int rga_iommu_map_sgt(struct sg_table *sgt, size_t size,
		      struct rga_dma_buffer *buffer,
		      struct device *rga_dev);
int rga_iommu_map(phys_addr_t paddr, size_t size,
		  struct rga_dma_buffer *buffer,
		  struct device *rga_dev);
void rga_iommu_unmap(struct rga_dma_buffer *buffer);

int rga_dma_map_buf(struct dma_buf *dma_buf, struct rga_dma_buffer *rga_dma_buffer,
		    enum dma_data_direction dir, struct device *rga_dev);
int rga_dma_map_fd(int fd, struct rga_dma_buffer *rga_dma_buffer,
		   enum dma_data_direction dir, struct device *rga_dev);
void rga_dma_unmap_buf(struct rga_dma_buffer *rga_dma_buffer);

void rga_dma_sync_flush_range(void *pstart, void *pend, struct rga_scheduler_t *scheduler);

#endif /* #ifndef __RGA3_DMA_BUF_H__ */

