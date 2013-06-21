/*
 * arch/arm/mach-sun6i/dma/dma_core.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma header file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __DMA_CORE_H
#define __DMA_CORE_H

u32 dma_enqueue(dma_hdl_t dma_hdl, u32 src_addr, u32 dst_addr, u32 byte_cnt);
void dma_config(dma_hdl_t dma_hdl, dma_config_t *pcfg);
u32 dma_ctrl(dma_hdl_t dma_hdl, dma_op_type_e op, void *parg);
void dma_release(dma_hdl_t dma_hdl);
void dma_request_init(dma_channel_t *pchan);
void dma_dump_chain(dma_channel_t *pchan);
u32 dma_hdl_irq_fd(dma_channel_t *pchan);

#endif  /* __DMA_CORE_H */

