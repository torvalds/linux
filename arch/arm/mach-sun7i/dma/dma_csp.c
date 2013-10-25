/*
 * arch/arm/mach-sun7i/dma/dma_csp.c
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * liugang <liugang@reuuimllatech.com>
 *
 * sun7i dma csp functions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "dma_include.h"

struct clk 	*g_dma_ahb_clk = NULL;

/**
 * dma_clk_init - init dma clock
 *
 * Returns 0 if sucess, otherwise failed.
 */
u32 dma_clk_init(void)
{
	/* config dma ahb clock */
	WARN_ON(g_dma_ahb_clk);
	g_dma_ahb_clk = clk_get(NULL, CLK_AHB_DMA);
	DMA_DBG("%s: get g_dma_ahb_clk 0x%08x\n", __func__, (u32)g_dma_ahb_clk);
	if(NULL == g_dma_ahb_clk || IS_ERR(g_dma_ahb_clk)) {
		printk("%s err: clk_get %s failed\n", __func__, CLK_AHB_DMA);
		return -EPERM;
	} else {
		if(0 != clk_enable(g_dma_ahb_clk)) {
			DMA_ERR("%s err: clk_enable failed, line %d\n", __func__, __LINE__);
			return -EPERM;
		}
		DMA_DBG("%s: clk_enable g_dma_ahb_clk success\n", __func__);
	}

	DMA_DBG("%s success\n", __func__);
	return 0;
}

/**
 * dma_clk_deinit - deinit dma clock
 *
 * Returns 0 if sucess, otherwise failed.
 */
u32 dma_clk_deinit(void)
{
	/* release dma ahb clock */
	DMA_DBG("%s: g_dma_ahb_clk 0x%08x\n", __func__, (u32)g_dma_ahb_clk);
	if(NULL == g_dma_ahb_clk || IS_ERR(g_dma_ahb_clk)) {
		DMA_INF("%s err: g_dma_ahb_clk 0x%08x invalid\n", __func__, (u32)g_dma_ahb_clk);
		return 0;
	} else {
		clk_disable(g_dma_ahb_clk);
		clk_put(g_dma_ahb_clk);
		g_dma_ahb_clk = NULL;
	}

	DMA_DBG("%s success\n", __func__);
	return 0;
}

/**
 * csp_dma_init - dma reg init, clear irq pending, disable irq
 */
void csp_dma_init(void)
{
	u32 	i = 0;

	/* init dma clock */
	if(0 != dma_clk_init())
		DMA_ERR("%s err, dma_clk_init failed, line %d\n", __func__, __LINE__);

	/* Disable & clear all interrupts */
	DMA_WRITE_REG(0, DMA_IRQ_EN_REG);
	DMA_WRITE_REG(0xffffffff, DMA_IRQ_PEND_REG);
	/* init enable reg */
	for(i = 0; i < DMA_CHAN_TOTAL; i++)
		DMA_WRITE_REG(0, DMA_CTRL_REG(i));
}

/**
 * csp_dma_start - start dma channel
 * @pchan:	dma channel handle
 */
void inline csp_dma_start(dma_channel_t * pchan)
{
	u32 temp = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	temp |= (1<<31);
	DMA_WRITE_REG(temp, pchan->reg_base + DMA_OFF_REG_CTRL);
}

/**
 * csp_dma_stop - stop dma channel
 * @pchan:	dma channel handle
 */
void inline csp_dma_stop(dma_channel_t * pchan)
{
	u32 temp = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	temp &= ~(1<<31);
	DMA_WRITE_REG(temp, pchan->reg_base + DMA_OFF_REG_CTRL);
}

/* if ndma work in continous mode, auto clock should disable */
void inline csp_ndma_autoclk_enable(void)
{
	u32 val = DMA_READ_REG(NDMA_AUTO_GAT_REG);
	val &= ~(1<<16);
	DMA_WRITE_REG(val, NDMA_AUTO_GAT_REG);
}

void inline csp_ndma_autoclk_disable(void)
{
	u32 val = DMA_READ_REG(NDMA_AUTO_GAT_REG);
	val |= (1<<16);
	DMA_WRITE_REG(val, NDMA_AUTO_GAT_REG);
}

/* set wate state, for ndma only. state: 0~7, from spec */
void inline csp_ndma_set_wait_state(dma_channel_t * pchan, u32 state)
{
	ndma_ctrl_t ctrl;
	u32 val;

	BUG_ON(unlikely(pchan->id >= 8));
	val = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	ctrl = *(ndma_ctrl_t *)&val;
	ctrl.wait_state = state;
	DMA_WRITE_REG(*(u32 *)&ctrl, pchan->reg_base + DMA_OFF_REG_CTRL);
	/* refresh ctrl val, set on next start */
	pchan->ctrl.n.wait_state = state;
}

/* set security, both for ndma and ddma */
void inline csp_dma_set_security(dma_channel_t * pchan, u32 para)
{
	dma_ctrl_u ctrl;
	u32 val;

	val = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	ctrl = *(dma_ctrl_u *)&val;
	switch(para) {
	case SRC_SECU_DST_SECU:
		IS_DEDICATE(pchan->id) ? (ctrl.d.src_sec = 0, ctrl.d.dst_sec = 0)
					: (ctrl.n.src_sec = 0, ctrl.n.dst_sec = 0);
		/* refresh ctrl val, set on next start */
		IS_DEDICATE(pchan->id) ? (pchan->ctrl.d.src_sec = 0, pchan->ctrl.d.dst_sec = 0)
					: (pchan->ctrl.n.src_sec = 0, pchan->ctrl.n.dst_sec = 0);
		break;
	case SRC_SECU_DST_NON_SECU:
		IS_DEDICATE(pchan->id) ? (ctrl.d.src_sec = 0, ctrl.d.dst_sec = 1)
					: (ctrl.n.src_sec = 0, ctrl.n.dst_sec = 1);
		IS_DEDICATE(pchan->id) ? (pchan->ctrl.d.src_sec = 0, pchan->ctrl.d.dst_sec = 1)
					: (pchan->ctrl.n.src_sec = 0, pchan->ctrl.n.dst_sec = 1);
		break;
	case SRC_NON_SECU_DST_SECU:
		IS_DEDICATE(pchan->id) ? (ctrl.d.src_sec = 1, ctrl.d.dst_sec = 0)
					: (ctrl.n.src_sec = 1, ctrl.n.dst_sec = 0);
		IS_DEDICATE(pchan->id) ? (pchan->ctrl.d.src_sec = 1, pchan->ctrl.d.dst_sec = 0)
					: (pchan->ctrl.n.src_sec = 1, pchan->ctrl.n.dst_sec = 0);
		break;
	case SRC_NON_SECU_DST_NON_SECU:
		IS_DEDICATE(pchan->id) ? (ctrl.d.src_sec = 1, ctrl.d.dst_sec = 1)
					: (ctrl.n.src_sec = 1, ctrl.n.dst_sec = 1);
		IS_DEDICATE(pchan->id) ? (pchan->ctrl.d.src_sec = 1, pchan->ctrl.d.dst_sec = 1)
					: (pchan->ctrl.n.src_sec = 1, pchan->ctrl.n.dst_sec = 1);
		break;
	default:
		BUG();
		break;
	}
	DMA_WRITE_REG(*(u32 *)&ctrl, pchan->reg_base + DMA_OFF_REG_CTRL);
}

void inline csp_dma_set_saddr(dma_channel_t * pchan, u32 ustart_addr)
{
	DMA_WRITE_REG(ustart_addr, pchan->reg_base + DMA_OFF_REG_SADR);
}

void inline csp_dma_set_daddr(dma_channel_t * pchan, u32 ustart_addr)
{
	DMA_WRITE_REG(ustart_addr, pchan->reg_base + DMA_OFF_REG_DADR);
}

void inline csp_dma_set_bcnt(dma_channel_t * pchan, u32 byte_cnt)
{
	DMA_WRITE_REG(byte_cnt, pchan->reg_base + DMA_OFF_REG_BC);
}

/* set para reg, for ddma only */
void inline csp_dma_set_para(dma_channel_t * pchan, dma_para_t para)
{
	BUG_ON(pchan->id < 8);
	DMA_WRITE_REG(*(u32 *)&para, pchan->reg_base + DMA_OFF_REG_PARA);
}

void inline csp_dma_set_ctrl(dma_channel_t * pchan, u32 val)
{
	DMA_WRITE_REG(val, pchan->reg_base + DMA_OFF_REG_CTRL);
}

/**
 * csp_dma_get_status - get dma channel status, for ddma only
 * @pchan:	dma channel handle
 *
 * Returns 1 indicate channel is busy, 0 idle
 */
u32 inline csp_dma_get_status(dma_channel_t * pchan)
{
	u32 temp = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	BUG_ON(!IS_DEDICATE(pchan->id));
	return !!(temp & (1<<30));
}

/**
 * csp_dma_get_saddr - get start src addr reg
 * @pchan:	dma channel handle
 *
 * Returns the channel's start src addr reg
 */
u32 inline csp_dma_get_saddr(dma_channel_t * pchan)
{
	return DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_SADR);
}

/**
 * csp_dma_get_daddr - get start dst addr reg
 * @pchan:	dma channel handle
 *
 * Returns the channel's start dst addr reg
 */
u32 inline csp_dma_get_daddr(dma_channel_t * pchan)
{
	return DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_DADR);
}

/**
 * csp_dma_get_bcnt - get left byte cnt
 * @pchan:	dma channel handle
 *
 * Returns the channel's left byte cnt
 */
u32 inline csp_dma_get_bcnt(dma_channel_t * pchan)
{
	return DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_BC);
}

/**
 * csp_dma_get_para - get para reg value, for ddma only
 * @pchan:	dma channel handle
 *
 * Returns the dma channel's para reg value
 */
dma_para_t inline csp_dma_get_para(dma_channel_t *pchan)
{
	u32 reg_val;

	BUG_ON(!IS_DEDICATE(pchan->id));
	reg_val = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_PARA);
	return *(dma_para_t *)&reg_val;
}

/**
 * csp_dma_get_ctrl - get ctrl reg value
 * @pchan:	dma channel handle
 *
 * Returns the dma channel's ctrl reg value
 */
dma_ctrl_u inline csp_dma_get_ctrl(dma_channel_t * pchan)
{
	u32 val = DMA_READ_REG(pchan->reg_base + DMA_OFF_REG_CTRL);
	return *(dma_ctrl_u *)&val;
}

/**
 * csp_dma_irq_enable - enable dma channel irq
 * @pchan:	dma channel handle
 * @irq_type:	irq type that will be enabled
 */
void inline csp_dma_irq_enable(dma_channel_t * pchan, u32 irq_type)
{
	u32 	uTemp = 0;

	uTemp = DMA_READ_REG(DMA_IRQ_EN_REG);
	uTemp &= (~(0x3 << (pchan->id << 1)));
	uTemp |= (irq_type << (pchan->id << 1));
	DMA_WRITE_REG(uTemp, DMA_IRQ_EN_REG);
}

/**
 * csp_dma_get_status - get dma channel irq pending val
 * @pchan:	dma channel handle
 *
 * Returns the irq pend value, eg: 0b11
 */
u32 inline csp_dma_get_irqpend(dma_channel_t * pchan)
{
	u32 	utemp = 0;
	utemp = DMA_READ_REG(DMA_IRQ_PEND_REG);
	return (utemp >> (pchan->id << 1)) & 0x3;
}

/**
 * csp_dma_clear_irqpend - clear the dma channel irq pending
 * @pchan:	dma channel handle
 * @irq_type:	irq type that willbe cleared, eg: CHAN_IRQ_HD|CHAN_IRQ_FD
 */
void inline csp_dma_clear_irqpend(dma_channel_t * pchan, u32 irq_type)
{
	u32 	utemp = 0;
	utemp = DMA_READ_REG(DMA_IRQ_PEND_REG);
	utemp &= (irq_type << (pchan->id << 1));
	DMA_WRITE_REG(utemp, DMA_IRQ_PEND_REG);
}

