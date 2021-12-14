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

void rga_dma_print_session_info(struct rga_dma_session *session);

int rga_dma_import_fd(int fd);
int rga_dma_release_fd(int fd);

int rga_dma_buf_get(struct rga_job *job);

int rga_dma_get_info(struct rga_job *job);
void rga_dma_put_info(struct rga_job *job);

int rga_get_format_bits(u32 format);

#endif /* #ifndef __RGA3_DMA_BUF_H__ */

