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

int rga_buf_size_cal(unsigned long yrgb_addr, unsigned long uv_addr,
		      unsigned long v_addr, int format, uint32_t w,
		      uint32_t h, unsigned long *StartAddr, unsigned long *size);

int rga_dma_buf_get(struct rga_job *job);
void rga_get_dma_buf(struct rga_job *job);

int rga_iommu_map_virt_addr(struct rga_memory_parm *memory_parm,
			    struct rga_dma_buffer *virt_dma_buf,
			    struct device *rga_dev,
			    struct mm_struct *mm);
void rga_iommu_unmap_virt_addr(struct rga_dma_buffer *virt_addr);

int rga_dma_map_fd(int fd, struct rga_dma_buffer *rga_dma_buffer,
		   enum dma_data_direction dir, struct device *rga_dev);
void rga_dma_unmap_fd(struct rga_dma_buffer *rga_dma_buffer);

int rga_dma_get_info(struct rga_job *job);
void rga_dma_put_info(struct rga_job *job);

#endif /* #ifndef __RGA3_DMA_BUF_H__ */

