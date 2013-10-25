/*
 * arch/arm/mach-sun7i/dma/dma_csp.h
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma csp header file
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __DMA_CSP_H
#define __DMA_CSP_H

extern struct clk *g_dma_ahb_clk;
extern struct clk *g_dma_mod_clk;

u32 dma_clk_init(void);
u32 dma_clk_deinit(void);

void csp_dma_init(void);
void csp_ndma_autoclk_enable(void);
void csp_ndma_autoclk_disable(void);
void csp_ndma_set_wait_state(dma_channel_t * pchan, u32 state);
void csp_dma_set_saddr(dma_channel_t * pchan, u32 ustart_addr);
void csp_dma_set_daddr(dma_channel_t * pchan, u32 ustart_addr);
void csp_dma_set_bcnt(dma_channel_t * pchan, u32 byte_cnt);
void csp_dma_set_para(dma_channel_t * pchan, dma_para_t para);
void csp_dma_start(dma_channel_t * pchan);
void csp_dma_stop(dma_channel_t * pchan);
u32 csp_dma_get_status(dma_channel_t * pchan);
u32 csp_dma_get_saddr(dma_channel_t * pchan);
u32 csp_dma_get_daddr(dma_channel_t * pchan);
u32 csp_dma_get_bcnt(dma_channel_t * pchan);
dma_para_t csp_dma_get_para(dma_channel_t *pchan);
void csp_dma_irq_enable(dma_channel_t * pchan, u32 irq_type);
u32 csp_dma_get_irqpend(dma_channel_t * pchan);
void csp_dma_clear_irqpend(dma_channel_t * pchan, u32 irq_type);
void csp_dma_set_security(dma_channel_t * pchan, u32 para);
void csp_dma_set_ctrl(dma_channel_t * pchan, u32 val);
dma_ctrl_u csp_dma_get_ctrl(dma_channel_t * pchan);

#endif  /* __DMA_CSP_H */

