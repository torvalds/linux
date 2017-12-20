/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <mtk_lpae.h>

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "MTK-BTIF-DMA"

#include "btif_dma_priv.h"

#define DMA_USER_ID "btif_driver"

/************************************Global variable***********************************/

static MTK_BTIF_DMA_VFIFO mtk_tx_dma_vfifo = {
	.vfifo = {
		  .p_vir_addr = NULL,
		  .phy_addr = 0,
		  .vfifo_size = TX_DMA_VFF_SIZE,
		  .thre = DMA_TX_THRE(TX_DMA_VFF_SIZE),
		  },
	.wpt = 0,
	.last_wpt_wrap = 0,
	.rpt = 0,
	.last_rpt_wrap = 0,
};

static MTK_BTIF_IRQ_STR mtk_btif_tx_dma_irq = {
	.name = "mtk btif tx dma irq",
	.is_irq_sup = true,
	.reg_flag = false,
#ifdef CONFIG_OF
	.irq_flags = IRQF_TRIGGER_NONE,
#else
	.irq_id = MT_DMA_BTIF_TX_IRQ_ID,
	.sens_type = IRQ_SENS_LVL,
	.lvl_type = IRQ_LVL_LOW,
#endif
	.p_irq_handler = NULL,
};

static MTK_BTIF_DMA_VFIFO mtk_rx_dma_vfifo = {
	.vfifo = {
		  .p_vir_addr = NULL,
		  .phy_addr = 0,
		  .vfifo_size = RX_DMA_VFF_SIZE,
		  .thre = DMA_RX_THRE(RX_DMA_VFF_SIZE),
		  },

	.wpt = 0,
	.last_wpt_wrap = 0,
	.rpt = 0,
	.last_rpt_wrap = 0,
};

static MTK_BTIF_IRQ_STR mtk_btif_rx_dma_irq = {
	.name = "mtk btif rx dma irq",
	.is_irq_sup = true,
	.reg_flag = false,
#ifdef CONFIG_OF
	.irq_flags = IRQF_TRIGGER_NONE,
#else
	.irq_id = MT_DMA_BTIF_RX_IRQ_ID,
	.sens_type = IRQ_SENS_LVL,
	.lvl_type = IRQ_LVL_LOW,
#endif
	.p_irq_handler = NULL,
};

static MTK_DMA_INFO_STR mtk_btif_tx_dma = {
#ifndef CONFIG_OF
	.base = AP_DMA_BASE + BTIF_TX_DMA_OFFSET,
#endif
	.dir = DMA_DIR_TX,
	.p_irq = &mtk_btif_tx_dma_irq,
	.p_vfifo = &(mtk_tx_dma_vfifo.vfifo),
};

static MTK_DMA_INFO_STR mtk_btif_rx_dma = {
#ifndef CONFIG_OF
	.base = AP_DMA_BASE + BTIF_RX_DMA_OFFSET,
#endif
	.dir = DMA_DIR_RX,
	.p_irq = &mtk_btif_rx_dma_irq,
	.p_vfifo = &(mtk_rx_dma_vfifo.vfifo),
};

static spinlock_t g_clk_cg_spinlock;	/*dma clock's spinlock */

/************************************Function declearation***********************************/
static int _is_tx_dma_in_flush(P_MTK_DMA_INFO_STR p_dma_info);
static int _tx_dma_flush(P_MTK_DMA_INFO_STR p_dma_info);
static int btif_rx_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info,
			    ENUM_DMA_CTRL ctrl_id);
static int btif_tx_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info,
			    ENUM_DMA_CTRL ctrl_id);
static int btif_rx_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en);
static int btif_tx_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en);
static int hal_rx_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info,
			       ENUM_BTIF_REG_ID flag);
static int hal_tx_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info,
			       ENUM_BTIF_REG_ID flag);
static int is_tx_dma_irq_finish_done(P_MTK_DMA_INFO_STR p_dma_info);
static int _btif_dma_dump_dbg_reg(void);
static void hal_btif_tx_dma_vff_set_for_4g(void);
static void hal_btif_rx_dma_vff_set_for_4g(void);

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_ier_ctrl
* DESCRIPTION
*  BTIF Tx DMA's interrupt enable/disable
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* enable    [IN]        control if tx interrupt enabled or not
* dma_dir  [IN]         DMA's direction
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int hal_btif_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en);

/*****************************************************************************
* FUNCTION
*  hal_dma_receive_data
* DESCRIPTION
*  receive data from btif module in DMA polling mode
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  positive means data is available, 0 means no data available
*****************************************************************************/
#ifndef MTK_BTIF_MARK_UNUSED_API
static int hal_dma_receive_data(P_MTK_DMA_INFO_STR p_dma_info,
				unsigned char *p_buf,
				const unsigned int max_len);

/************************************Function***********************************/
#endif

#ifdef CONFIG_OF
static void hal_dma_set_default_setting(ENUM_DMA_DIR dma_dir)
{
	struct device_node *node = NULL;
	unsigned int irq_info[3] = {0, 0, 0};
	unsigned int phy_base;

	if (dma_dir == DMA_DIR_RX) {
		node = of_find_compatible_node(NULL, NULL, "mediatek,btif_rx");
		if (node) {
			mtk_btif_rx_dma.p_irq->irq_id = irq_of_parse_and_map(node, 0);
			/*fixme, be compitable arch 64bits*/
			mtk_btif_rx_dma.base = (unsigned long)of_iomap(node, 0);
			BTIF_INFO_FUNC("get rx_dma irq(%d),register base(0x%lx)\n",
				mtk_btif_rx_dma.p_irq->irq_id, mtk_btif_rx_dma.base);
		} else {
			BTIF_ERR_FUNC("get rx_dma device node fail\n");
		}
		/* get the interrupt line behaviour */
		if (of_property_read_u32_array(node, "interrupts", irq_info, ARRAY_SIZE(irq_info))) {
			BTIF_ERR_FUNC("get interrupt flag from DTS fail\n");
		} else {
			mtk_btif_rx_dma.p_irq->irq_flags = irq_info[2];
			BTIF_INFO_FUNC("get interrupt flag(0x%x)\n",
				mtk_btif_rx_dma.p_irq->irq_flags);
		}
		if (of_property_read_u32_index(node, "reg", 1, &phy_base)) {
			BTIF_ERR_FUNC("get register phy base from DTS fail,dma_dir(%d)\n",
					dma_dir);
		} else {
			BTIF_INFO_FUNC("get register phy base dma_dir(%d)(0x%x)\n",
					dma_dir, (unsigned int)phy_base);
		}
	} else if (dma_dir == DMA_DIR_TX) {
		node = of_find_compatible_node(NULL, NULL, "mediatek,btif_tx");
		if (node) {
			mtk_btif_tx_dma.p_irq->irq_id = irq_of_parse_and_map(node, 0);
			/*fixme, be compitable arch 64bits*/
			mtk_btif_tx_dma.base = (unsigned long)of_iomap(node, 0);
			BTIF_INFO_FUNC("get tx_dma irq(%d),register base(0x%lx)\n",
				mtk_btif_tx_dma.p_irq->irq_id, mtk_btif_tx_dma.base);
		} else {
			BTIF_ERR_FUNC("get tx_dma device node fail\n");
		}
		/* get the interrupt line behaviour */
		if (of_property_read_u32_array(node, "interrupts", irq_info, ARRAY_SIZE(irq_info))) {
			BTIF_ERR_FUNC("get interrupt flag from DTS fail\n");
		} else {
			mtk_btif_tx_dma.p_irq->irq_flags = irq_info[2];
			BTIF_INFO_FUNC("get interrupt flag(0x%x)\n",
				mtk_btif_tx_dma.p_irq->irq_flags);
		}

		if (of_property_read_u32_index(node, "reg", 1, &phy_base)) {
			BTIF_ERR_FUNC("get register phy base from DTS fail,dma_dir(%d)\n",
				dma_dir);
		} else {
			BTIF_INFO_FUNC("get register phy base dma_dir(%d)(0x%x)\n",
					dma_dir, (unsigned int)phy_base);
		}
	}

}
#endif

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_info_get
* DESCRIPTION
*  get btif tx dma channel's information
* PARAMETERS
* dma_dir        [IN]         DMA's direction
* RETURNS
*  pointer to btif dma's information structure
*****************************************************************************/
P_MTK_DMA_INFO_STR hal_btif_dma_info_get(ENUM_DMA_DIR dma_dir)
{
	P_MTK_DMA_INFO_STR p_dma_info = NULL;

	BTIF_TRC_FUNC();
#ifdef CONFIG_OF
	hal_dma_set_default_setting(dma_dir);
#endif
	if (dma_dir == DMA_DIR_RX)
		/*Rx DMA*/
		p_dma_info = &mtk_btif_rx_dma;
	else if (dma_dir == DMA_DIR_TX)
		/*Tx DMA*/
		p_dma_info = &mtk_btif_tx_dma;
	else
		/*print error log*/
		BTIF_ERR_FUNC("invalid DMA dir (%d)\n", dma_dir);
	spin_lock_init(&g_clk_cg_spinlock);
	BTIF_TRC_FUNC();
	return p_dma_info;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_clk_ctrl
* DESCRIPTION
*  control clock output enable/disable of DMA module
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_dma_clk_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_CLOCK_CTRL flag)
{
/*In MTK DMA BTIF channel, there's only one global CG on AP_DMA, no sub channel's CG bit*/
/*according to Artis's comment, clock of DMA and BTIF is default off, so we assume it to be off by default*/
	int i_ret = 0;
	unsigned long irq_flag = 0;

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	static atomic_t s_clk_ref = ATOMIC_INIT(0);
#else
	static ENUM_CLOCK_CTRL status = CLK_OUT_DISABLE;
#endif

	spin_lock_irqsave(&(g_clk_cg_spinlock), irq_flag);

#if MTK_BTIF_ENABLE_CLK_CTL

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER

	if (flag == CLK_OUT_ENABLE) {
		if (atomic_inc_return(&s_clk_ref) == 1) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = enable_clock(MTK_BTIF_APDMA_CLK_CG, DMA_USER_ID);
#else
			BTIF_DBG_FUNC("[CCF]enable clk_btif_apdma\n");
			i_ret = clk_enable(clk_btif_apdma);
#endif /* defined(CONFIG_MTK_CLKMGR) */
			if (i_ret) {
				BTIF_WARN_FUNC
					("enable_clock for MTK_BTIF_APDMA_CLK_CG failed, ret:%d",
					 i_ret);
			}
		}
	} else if (flag == CLK_OUT_DISABLE) {
		if (atomic_dec_return(&s_clk_ref) == 0) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = disable_clock(MTK_BTIF_APDMA_CLK_CG, DMA_USER_ID);
			if (i_ret) {
				BTIF_WARN_FUNC
					("disable_clock for MTK_BTIF_APDMA_CLK_CG failed, ret:%d",
					 i_ret);
			}
#else
			BTIF_DBG_FUNC("[CCF] clk_disable(clk_btif_apdma) calling\n");
			clk_disable(clk_btif_apdma);
#endif /* defined(CONFIG_MTK_CLKMGR) */
		}
	} else {
		i_ret = ERR_INVALID_PAR;
		BTIF_ERR_FUNC("invalid  clock ctrl flag (%d)\n", flag);
	}

#else

	if (status == flag) {
		i_ret = 0;
		BTIF_DBG_FUNC("dma clock already %s\n",
			      CLK_OUT_ENABLE ==
			      status ? "enabled" : "disabled");
	} else {
		if (flag == CLK_OUT_ENABLE) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = enable_clock(MTK_BTIF_APDMA_CLK_CG, DMA_USER_ID);
#else
			BTIF_DBG_FUNC("[CCF]enable clk_btif_apdma\n");
			i_ret = clk_enable(clk_btif_apdma);
#endif /* defined(CONFIG_MTK_CLKMGR) */
			status = (i_ret == 0) ? flag : status;
			if (i_ret) {
				BTIF_WARN_FUNC
					("enable_clock for MTK_BTIF_APDMA_CLK_CG failed, ret:%d",
					 i_ret);
			}
		} else if (flag == CLK_OUT_DISABLE) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = disable_clock(MTK_BTIF_APDMA_CLK_CG, DMA_USER_ID);
			status = (i_ret == 0) ? flag : status;
			if (i_ret) {
				BTIF_WARN_FUNC
					("disable_clock for MTK_BTIF_APDMA_CLK_CG failed, ret:%d",
					 i_ret);
			}
#else
			BTIF_DBG_FUNC("[CCF] clk_disable_unprepare(clk_btif_apdma) calling\n");
			clk_disable(clk_btif_apdma);
#endif /* defined(CONFIG_MTK_CLKMGR) */
		} else {
			i_ret = ERR_INVALID_PAR;
			BTIF_ERR_FUNC("invalid  clock ctrl flag (%d)\n", flag);
		}
	}
#endif

#else

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER

#else

	status = flag;
#endif

	i_ret = 0;
#endif

	spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);

#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
	if (i_ret == 0) {
		BTIF_DBG_FUNC("dma clock %s\n", flag == CLK_OUT_ENABLE ? "enabled" : "disabled");
	} else {
		BTIF_ERR_FUNC("%s dma clock failed, ret(%d)\n",
				flag == CLK_OUT_ENABLE ? "enable" : "disable", i_ret);
	}
#else

	if (i_ret == 0) {
		BTIF_DBG_FUNC("dma clock %s\n", flag == CLK_OUT_ENABLE ? "enabled" : "disabled");
	} else {
		BTIF_ERR_FUNC("%s dma clock failed, ret(%d)\n",
				flag == CLK_OUT_ENABLE ? "enable" : "disable", i_ret);
	}
#endif
#if defined(CONFIG_MTK_CLKMGR)
	BTIF_DBG_FUNC("DMA's clock is %s\n", (clock_is_on(MTK_BTIF_APDMA_CLK_CG) == 0) ? "off" : "on");
#endif
	return i_ret;
}

int hal_btif_dma_hw_init(P_MTK_DMA_INFO_STR p_dma_info)
{
	int i_ret = 0;
	unsigned int dat = 0;
	unsigned long base = p_dma_info->base;
	unsigned long addr_h = 0;
	P_DMA_VFIFO p_vfifo = p_dma_info->p_vfifo;
	P_MTK_BTIF_DMA_VFIFO p_mtk_dma_vfifo = container_of(p_vfifo,
							    MTK_BTIF_DMA_VFIFO,
							    vfifo);

	if (p_dma_info->dir == DMA_DIR_RX) {
		/*Rx DMA*/
		/*do hardware reset*/
		/*		BTIF_SET_BIT(RX_DMA_RST(base), DMA_HARD_RST);*/
		/*		BTIF_CLR_BIT(RX_DMA_RST(base), DMA_HARD_RST);*/
		BTIF_SET_BIT(RX_DMA_RST(base), DMA_WARM_RST);
		do {
			dat = BTIF_READ32(RX_DMA_EN(base));
		} while (0x01 & dat);
		/*write vfifo base address to VFF_ADDR*/
		btif_reg_sync_writel(p_vfifo->phy_addr, RX_DMA_VFF_ADDR(base));
		if (enable_4G())
			hal_btif_rx_dma_vff_set_for_4g();
		else {
			addr_h = p_vfifo->phy_addr >> 16;
			addr_h = addr_h >> 16;
			btif_reg_sync_writel(addr_h, RX_DMA_VFF_ADDR_H(base));
		}
		/*write vfifo length to VFF_LEN*/
		btif_reg_sync_writel(p_vfifo->vfifo_size, RX_DMA_VFF_LEN(base));
		/*write wpt to VFF_WPT*/
		btif_reg_sync_writel(p_mtk_dma_vfifo->wpt,
				     RX_DMA_VFF_WPT(base));
		btif_reg_sync_writel(p_mtk_dma_vfifo->rpt,
					 RX_DMA_VFF_RPT(base));
		/*write vff_thre to VFF_THRESHOLD*/
		btif_reg_sync_writel(p_vfifo->thre, RX_DMA_VFF_THRE(base));
		/*clear Rx DMA's interrupt status*/
		BTIF_SET_BIT(RX_DMA_INT_FLAG(base),
			     RX_DMA_INT_DONE | RX_DMA_INT_THRE);

		/*enable Rx IER by default*/
		btif_rx_dma_ier_ctrl(p_dma_info, true);
	} else {
/*Tx DMA*/
/*do hardware reset*/
/*		BTIF_SET_BIT(TX_DMA_RST(base), DMA_HARD_RST);*/
/*		BTIF_CLR_BIT(TX_DMA_RST(base), DMA_HARD_RST);*/
		BTIF_SET_BIT(TX_DMA_RST(base), DMA_WARM_RST);
		do {
			dat = BTIF_READ32(TX_DMA_EN(base));
		} while (0x01 & dat);
/*write vfifo base address to VFF_ADDR*/
		btif_reg_sync_writel(p_vfifo->phy_addr, TX_DMA_VFF_ADDR(base));
		if (enable_4G())
			hal_btif_tx_dma_vff_set_for_4g();
		else {
			addr_h = p_vfifo->phy_addr >> 16;
			addr_h = addr_h >> 16;
			btif_reg_sync_writel(addr_h, TX_DMA_VFF_ADDR_H(base));
		}
/*write vfifo length to VFF_LEN*/
		btif_reg_sync_writel(p_vfifo->vfifo_size, TX_DMA_VFF_LEN(base));
/*write wpt to VFF_WPT*/
		btif_reg_sync_writel(p_mtk_dma_vfifo->wpt,
				     TX_DMA_VFF_WPT(base));
		btif_reg_sync_writel(p_mtk_dma_vfifo->rpt,
				     TX_DMA_VFF_RPT(base));
/*write vff_thre to VFF_THRESHOLD*/
		btif_reg_sync_writel(p_vfifo->thre, TX_DMA_VFF_THRE(base));

		BTIF_CLR_BIT(TX_DMA_INT_FLAG(base), TX_DMA_INT_FLAG_MASK);

		hal_btif_dma_ier_ctrl(p_dma_info, false);
	}

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_ctrl
* DESCRIPTION
* enable/disable Tx DMA channel
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* ctrl_id          [IN]        enable/disable ID
* RETURNS
*  0 means success; negative means fail
*****************************************************************************/
int hal_btif_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_DMA_CTRL ctrl_id)
{
	unsigned int i_ret = -1;
	ENUM_DMA_DIR dir = p_dma_info->dir;

	if (dir == DMA_DIR_RX)
		i_ret = btif_rx_dma_ctrl(p_dma_info, ctrl_id);
	else if (dir == DMA_DIR_TX)
		i_ret = btif_tx_dma_ctrl(p_dma_info, ctrl_id);
	else {
		/*TODO: print error log*/
		BTIF_ERR_FUNC("invalid dma ctrl id (%d)\n", ctrl_id);
		i_ret = ERR_INVALID_PAR;
	}
	return i_ret;
}

int hal_btif_dma_rx_cb_reg(P_MTK_DMA_INFO_STR p_dma_info,
			   dma_rx_buf_write rx_cb)
{
	if (p_dma_info->rx_cb != NULL) {
		BTIF_DBG_FUNC
		    ("rx_cb already registered, replace (0x%p) with (0x%p)\n",
		     p_dma_info->rx_cb, rx_cb);
	}
	p_dma_info->rx_cb = rx_cb;
	return 0;
}

int btif_tx_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_DMA_CTRL ctrl_id)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int dat;

	BTIF_TRC_FUNC();
	if (ctrl_id == DMA_CTRL_DISABLE) {
		/*if write 0 to EN bit, DMA will be stopped imediately*/
		/*if write 1 to STOP bit, DMA will be stopped after current transaction finished*/
		/*BTIF_CLR_BIT(TX_DMA_EN(base), DMA_EN_BIT);*/
		BTIF_SET_BIT(TX_DMA_STOP(base), DMA_STOP_BIT);
		do {
			dat = BTIF_READ32(TX_DMA_STOP(base));
		} while (0x1 & dat);
		BTIF_DBG_FUNC("BTIF Tx DMA disabled,EN(0x%x),STOP(0x%x)\n",
			BTIF_READ32(TX_DMA_EN(base)), BTIF_READ32(TX_DMA_STOP(base)));
		i_ret = 0;
	} else if (ctrl_id == DMA_CTRL_ENABLE) {
		BTIF_SET_BIT(TX_DMA_EN(base), DMA_EN_BIT);
		BTIF_DBG_FUNC("BTIF Tx DMA enabled\n");
		i_ret = 0;
	} else {
/*TODO: print error log*/
		BTIF_ERR_FUNC("invalid DMA ctrl_id (%d)\n", ctrl_id);
		i_ret = ERR_INVALID_PAR;
	}
	BTIF_TRC_FUNC();
	return i_ret;
}

int btif_rx_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_DMA_CTRL ctrl_id)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int dat;

	BTIF_TRC_FUNC();

	if (ctrl_id == DMA_CTRL_DISABLE) {
		/*if write 0 to EN bit, DMA will be stopped imediately*/
		/*if write 1 to STOP bit, DMA will be stopped after current transaction finished*/
		/*BTIF_CLR_BIT(RX_DMA_EN(base), DMA_EN_BIT);*/
		BTIF_SET_BIT(RX_DMA_STOP(base), DMA_STOP_BIT);
		do {
			dat = BTIF_READ32(RX_DMA_STOP(base));
		} while (0x1 & dat);
		BTIF_DBG_FUNC("BTIF Rx DMA disabled,EN(0x%x),STOP(0x%x)\n",
			BTIF_READ32(RX_DMA_EN(base)), BTIF_READ32(RX_DMA_STOP(base)));
		i_ret = 0;
	} else if (ctrl_id == DMA_CTRL_ENABLE) {
		BTIF_SET_BIT(RX_DMA_EN(base), DMA_EN_BIT);
		BTIF_DBG_FUNC("BTIF Rx DMA enabled\n");
		i_ret = 0;
	} else {
/*TODO: print error log*/
		BTIF_ERR_FUNC("invalid DMA ctrl_id (%d)\n", ctrl_id);
		i_ret = ERR_INVALID_PAR;
	}
	BTIF_TRC_FUNC();

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_tx_vfifo_reset
* DESCRIPTION
*  reset tx virtual fifo information, except memory information
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_vfifo_reset(P_MTK_DMA_INFO_STR p_dma_info)
{
	unsigned int i_ret = -1;
	P_DMA_VFIFO p_vfifo = p_dma_info->p_vfifo;
	P_MTK_BTIF_DMA_VFIFO p_mtk_dma_vfifo = container_of(p_vfifo,
							    MTK_BTIF_DMA_VFIFO,
							    vfifo);

	BTIF_TRC_FUNC();
	p_mtk_dma_vfifo->rpt = 0;
	p_mtk_dma_vfifo->last_rpt_wrap = 0;
	p_mtk_dma_vfifo->wpt = 0;
	p_mtk_dma_vfifo->last_wpt_wrap = 0;
	BTIF_TRC_FUNC();
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_ier_ctrl
* DESCRIPTION
*  BTIF Tx DMA's interrupt enable/disable
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* enable    [IN]        control if tx interrupt enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en)
{
	unsigned int i_ret = -1;
	ENUM_DMA_DIR dir = p_dma_info->dir;

	if (dir == DMA_DIR_RX) {
		i_ret = btif_rx_dma_ier_ctrl(p_dma_info, en);
	} else if (dir == DMA_DIR_TX) {
		i_ret = btif_tx_dma_ier_ctrl(p_dma_info, en);
	} else {
/*TODO: print error log*/
		BTIF_ERR_FUNC("invalid DMA dma dir (%d)\n", dir);
		i_ret = ERR_INVALID_PAR;
	}

	return i_ret;
}

int btif_rx_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;

	BTIF_TRC_FUNC();
	if (!en) {
		BTIF_CLR_BIT(RX_DMA_INT_EN(base),
			     (RX_DMA_INT_THRE_EN | RX_DMA_INT_DONE_EN));
	} else {
		BTIF_SET_BIT(RX_DMA_INT_EN(base),
			     (RX_DMA_INT_THRE_EN | RX_DMA_INT_DONE_EN));
	}
	i_ret = 0;
	BTIF_TRC_FUNC();

	return i_ret;
}

int btif_tx_dma_ier_ctrl(P_MTK_DMA_INFO_STR p_dma_info, bool en)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;

	BTIF_TRC_FUNC();
	if (!en)
		BTIF_CLR_BIT(TX_DMA_INT_EN(base), TX_DMA_INTEN_BIT);
	else
		BTIF_SET_BIT(TX_DMA_INT_EN(base), TX_DMA_INTEN_BIT);
	i_ret = 0;
	BTIF_TRC_FUNC();

	return i_ret;
}

static int is_tx_dma_irq_finish_done(P_MTK_DMA_INFO_STR p_dma_info)
{
	int tx_irq_done = 0;
#if MTK_BTIF_ENABLE_CLK_REF_COUNTER
/*if we enable this clock reference couner, just return , because when enter IRQ handler, DMA's clock will be opened*/
	tx_irq_done = 1;
#else
	unsigned long flag = 0;
	unsigned long base = p_dma_info->base;

	spin_lock_irqsave(&(g_clk_cg_spinlock), flag);
	tx_irq_done = ((BTIF_READ32(TX_DMA_INT_FLAG(base)) & TX_DMA_INT_FLAG_MASK) == 0) ? 1 : 0;
	spin_unlock_irqrestore(&(g_clk_cg_spinlock), flag);
#endif
	return tx_irq_done;
}

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_irq_handler
* DESCRIPTION
*  lower level tx interrupt handler
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_tx_dma_irq_handler(P_MTK_DMA_INFO_STR p_dma_info)
{
#define MAX_CONTINIOUS_TIMES 512
	unsigned int i_ret = -1;
	unsigned int valid_size = 0;
	unsigned int vff_len = 0;
	unsigned int left_len = 0;
	unsigned long base = p_dma_info->base;
	static int flush_irq_counter;
	static struct timeval start_timer;
	static struct timeval end_timer;
	unsigned long flag = 0;

	spin_lock_irqsave(&(g_clk_cg_spinlock), flag);

#if defined(CONFIG_MTK_CLKMGR)
	if (clock_is_on(MTK_BTIF_APDMA_CLK_CG) == 0) {
		spin_unlock_irqrestore(&(g_clk_cg_spinlock), flag);
		BTIF_ERR_FUNC
		    ("%s: clock is off before irq status clear done!!!\n",
		     __FILE__);
		return i_ret;
	}
#endif
/*check if Tx VFF Left Size equal to VFIFO size or not*/
	vff_len = BTIF_READ32(TX_DMA_VFF_LEN(base));
	valid_size = BTIF_READ32(TX_DMA_VFF_VALID_SIZE(base));
	left_len = BTIF_READ32(TX_DMA_VFF_LEFT_SIZE(base));
	if (flush_irq_counter == 0)
		do_gettimeofday(&start_timer);
	if ((valid_size > 0) && (valid_size < 8)) {
		i_ret = _tx_dma_flush(p_dma_info);
		flush_irq_counter++;
		if (flush_irq_counter >= MAX_CONTINIOUS_TIMES) {
			do_gettimeofday(&end_timer);
/*
 * when btif tx fifo cannot accept any data and counts of bytes left in tx vfifo < 8 for a while
 * we assume that btif cannot send data for a long time
 * in order not to generate interrupt continiously, which may effect system's performance.
 * we clear tx flag and disable btif tx interrupt
 */
/*clear interrupt flag*/
			BTIF_CLR_BIT(TX_DMA_INT_FLAG(base),
				     TX_DMA_INT_FLAG_MASK);
/*vFIFO data has been read by DMA controller, just disable tx dma's irq*/
			i_ret = hal_btif_dma_ier_ctrl(p_dma_info, false);
			BTIF_ERR_FUNC
			    ("**********************ERROR, ERROR, ERROR**************************\n");
			BTIF_ERR_FUNC
			    ("BTIF Tx IRQ happened %d times (continiously), between %d.%d and %d.%d\n",
			     MAX_CONTINIOUS_TIMES, start_timer.tv_sec,
			     start_timer.tv_usec, end_timer.tv_usec,
			     end_timer.tv_usec);
		}
	} else if (vff_len == left_len) {
		flush_irq_counter = 0;
/*clear interrupt flag*/
		BTIF_CLR_BIT(TX_DMA_INT_FLAG(base), TX_DMA_INT_FLAG_MASK);
/*vFIFO data has been read by DMA controller, just disable tx dma's irq*/
		i_ret = hal_btif_dma_ier_ctrl(p_dma_info, false);
	} else {
#if 0
		BTIF_ERR_FUNC
		    ("**********************WARNING**************************\n");
		BTIF_ERR_FUNC("invalid irq condition, dump register\n");
		hal_dma_dump_reg(p_dma_info, REG_TX_DMA_ALL);
#endif
		BTIF_DBG_FUNC
		    ("superious IRQ occurs, vff_len(%d), valid_size(%d), left_len(%d)\n",
		     vff_len, valid_size, left_len);
	}

	spin_unlock_irqrestore(&(g_clk_cg_spinlock), flag);

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_send_data
* DESCRIPTION
*  send data through btif in DMA mode
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN]        pointer to rx data buffer
* max_len  [IN]        tx buffer length
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_dma_send_data(P_MTK_DMA_INFO_STR p_dma_info,
		      const unsigned char *p_buf, const unsigned int buf_len)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;
	P_DMA_VFIFO p_vfifo = p_dma_info->p_vfifo;
	unsigned int len_to_send = buf_len;
	unsigned int ava_len = 0;
	unsigned int wpt = 0;
	unsigned int last_wpt_wrap = 0;
	unsigned int vff_size = 0;
	unsigned char *p_data = (unsigned char *)p_buf;
	P_MTK_BTIF_DMA_VFIFO p_mtk_vfifo = container_of(p_vfifo,
							MTK_BTIF_DMA_VFIFO,
							vfifo);

	BTIF_TRC_FUNC();
	if ((p_buf == NULL) || (buf_len == 0)) {
		i_ret = ERR_INVALID_PAR;
		BTIF_ERR_FUNC("invalid parameters, p_buf:0x%p, buf_len:%d\n",
			      p_buf, buf_len);
		return i_ret;
	}
/*check if tx dma in flush operation? if yes, should wait until DMA finish flush operation*/
/*currently uplayer logic will make sure this pre-condition*/
/*disable Tx IER, in case Tx irq happens, flush bit may be set in irq handler*/
	btif_tx_dma_ier_ctrl(p_dma_info, false);

	vff_size = p_mtk_vfifo->vfifo.vfifo_size;
	ava_len = BTIF_READ32(TX_DMA_VFF_LEFT_SIZE(base));
	wpt = BTIF_READ32(TX_DMA_VFF_WPT(base)) & DMA_WPT_MASK;
	last_wpt_wrap = BTIF_READ32(TX_DMA_VFF_WPT(base)) & DMA_WPT_WRAP;

/*
 * copy data to vFIFO, Note: ava_len should always large than buf_len,
 * otherwise common logic layer will not call hal_dma_send_data
 */
	if (buf_len > ava_len) {
		BTIF_ERR_FUNC
		    ("length to send:(%d) < length available(%d), abnormal!!!---!!!\n",
		     buf_len, ava_len);
		WARN_ON(buf_len > ava_len);	/* this will cause kernel panic */
	}

	len_to_send = buf_len < ava_len ? buf_len : ava_len;
	if (len_to_send + wpt >= vff_size) {
		unsigned int tail_len = vff_size - wpt;

		memcpy((p_mtk_vfifo->vfifo.p_vir_addr + wpt), p_data, tail_len);
		p_data += tail_len;
		memcpy(p_mtk_vfifo->vfifo.p_vir_addr,
		       p_data, len_to_send - tail_len);
/*make sure all data write to memory area tx vfifo locates*/
		mb();

/*calculate WPT*/
		wpt = wpt + len_to_send - vff_size;
		last_wpt_wrap ^= DMA_WPT_WRAP;
	} else {
		memcpy((p_mtk_vfifo->vfifo.p_vir_addr + wpt),
		       p_data, len_to_send);
/*make sure all data write to memory area tx vfifo locates*/
		mb();

/*calculate WPT*/
		wpt += len_to_send;
	}
	p_mtk_vfifo->wpt = wpt;
	p_mtk_vfifo->last_wpt_wrap = last_wpt_wrap;

/*make sure tx dma is allowed(tx flush bit is not set) to use before update WPT*/
	if (hal_dma_is_tx_allow(p_dma_info)) {
		/*make sure tx dma enabled*/
		hal_btif_dma_ctrl(p_dma_info, DMA_CTRL_ENABLE);

		/*update WTP to Tx DMA controller's control register*/
		btif_reg_sync_writel(wpt | last_wpt_wrap, TX_DMA_VFF_WPT(base));

		if ((BTIF_READ32(TX_DMA_VFF_VALID_SIZE(base)) < 8) &&
		    (BTIF_READ32(TX_DMA_VFF_VALID_SIZE(base)) > 0)) {
			/*
			 * 0 < valid size in Tx vFIFO < 8 && TX Flush is not in process<should always be done>?
			 * if yes, set flush bit to DMA
			 */
			_tx_dma_flush(p_dma_info);
		}
		i_ret = len_to_send;
	} else {
/*TODO: print error log*/
		BTIF_ERR_FUNC("Tx DMA flush operation is in process, this case should never happen,",
		"please check if tx operation is allowed before call this API\n");
/*if flush operation is in process , we will return 0*/
		i_ret = 0;
	}

/*Enable Tx IER*/
	btif_tx_dma_ier_ctrl(p_dma_info, true);

	BTIF_TRC_FUNC();
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_is_tx_complete
* DESCRIPTION
*  get tx complete flag
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  true means tx complete, false means tx in process
*****************************************************************************/
bool hal_dma_is_tx_complete(P_MTK_DMA_INFO_STR p_dma_info)
{
	bool b_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int valid_size = BTIF_READ32(TX_DMA_VFF_VALID_SIZE(base));
	unsigned int inter_size = BTIF_READ32(TX_DMA_INT_BUF_SIZE(base));
	unsigned int tx_done = is_tx_dma_irq_finish_done(p_dma_info);

/*
 * only when virtual FIFO valid size and Tx channel internal buffer size are both becomes to be 0,
 * we can identify tx operation finished
 * confirmed with DE.
 */
	if ((valid_size == 0) && (inter_size == 0) && (tx_done == 1)) {
		b_ret = true;
		BTIF_DBG_FUNC("DMA tx finished.\n");
	} else {
		BTIF_DBG_FUNC
		    ("DMA tx is in process. vfifo valid size(%d), dma internal size (%d), tx_done(%d)\n",
		     valid_size, inter_size, tx_done);
		b_ret = false;
	}

	return b_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_get_ava_room
* DESCRIPTION
*  get tx available room
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  available room  size
*****************************************************************************/
int hal_dma_get_ava_room(P_MTK_DMA_INFO_STR p_dma_info)
{
	int i_ret = -1;
	unsigned long base = p_dma_info->base;

/*read vFIFO's left size*/
	i_ret = BTIF_READ32(TX_DMA_VFF_LEFT_SIZE(base));
	BTIF_DBG_FUNC("DMA tx ava room (%d).\n", i_ret);
	if (i_ret == 0)
		BTIF_INFO_FUNC("DMA tx vfifo is full.\n");

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_is_tx_allow
* DESCRIPTION
*  is tx operation allowed by DMA
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  true if tx operation is allowed; false if tx is not allowed
*****************************************************************************/
bool hal_dma_is_tx_allow(P_MTK_DMA_INFO_STR p_dma_info)
{
#define MIN_TX_MB ((26 * 1000000 / 13) / 1000000)
#define AVE_TX_MB ((26 * 1000000 / 8) / 1000000)

	bool b_ret = false;
	unsigned int wait_us = 8 / MIN_TX_MB;	/*only ava length */
/*see if flush operation is in process*/
	b_ret = _is_tx_dma_in_flush(p_dma_info) ? false : true;
	if (!b_ret) {
		usleep_range(wait_us, 2 * wait_us);
		b_ret = _is_tx_dma_in_flush(p_dma_info) ? false : true;
	}
	if (!b_ret)
		BTIF_WARN_FUNC("btif tx dma is not allowed\n");
/*after Tx flush operation finished, HW will set DMA_EN back to 0 and stop DMA*/
	return b_ret;
}


/*****************************************************************************
* FUNCTION
*  hal_rx_dma_irq_handler
* DESCRIPTION
*  lower level rx interrupt handler
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_rx_dma_irq_handler(P_MTK_DMA_INFO_STR p_dma_info,
			   unsigned char *p_buf, const unsigned int max_len)
{
	int i_ret = -1;
	unsigned int valid_len = 0;
	unsigned int wpt_wrap = 0;
	unsigned int rpt_wrap = 0;
	unsigned int wpt = 0;
	unsigned int rpt = 0;
	unsigned int tail_len = 0;
	unsigned int real_len = 0;
	unsigned long base = p_dma_info->base;
	P_DMA_VFIFO p_vfifo = p_dma_info->p_vfifo;
	dma_rx_buf_write rx_cb = p_dma_info->rx_cb;
	unsigned char *p_vff_buf = NULL;
	unsigned char *vff_base = p_vfifo->p_vir_addr;
	unsigned int vff_size = p_vfifo->vfifo_size;
	P_MTK_BTIF_DMA_VFIFO p_mtk_vfifo = container_of(p_vfifo,
							MTK_BTIF_DMA_VFIFO,
							vfifo);
	unsigned long flag = 0;

	spin_lock_irqsave(&(g_clk_cg_spinlock), flag);
#if defined(CONFIG_MTK_CLKMGR)
	if (clock_is_on(MTK_BTIF_APDMA_CLK_CG) == 0) {
		spin_unlock_irqrestore(&(g_clk_cg_spinlock), flag);
		BTIF_ERR_FUNC("%s: clock is off before irq handle done!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
/*disable DMA Rx IER*/
	hal_btif_dma_ier_ctrl(p_dma_info, false);

/*clear Rx DMA's interrupt status*/
	BTIF_SET_BIT(RX_DMA_INT_FLAG(base), RX_DMA_INT_DONE | RX_DMA_INT_THRE);

	valid_len = BTIF_READ32(RX_DMA_VFF_VALID_SIZE(base));
	rpt = BTIF_READ32(RX_DMA_VFF_RPT(base));
	wpt = BTIF_READ32(RX_DMA_VFF_WPT(base));
	if ((valid_len == 0) && (rpt == wpt)) {
		BTIF_DBG_FUNC
		    ("rx interrupt, no data available in Rx DMA, wpt(0x%08x), rpt(0x%08x)\n",
		     rpt, wpt);
	}

	i_ret = 0;

	while ((valid_len > 0) || (rpt != wpt)) {
		rpt_wrap = rpt & DMA_RPT_WRAP;
		wpt_wrap = wpt & DMA_WPT_WRAP;
		rpt &= DMA_RPT_MASK;
		wpt &= DMA_WPT_MASK;

/*calcaute length of available data  in vFIFO*/
		if (wpt_wrap != p_mtk_vfifo->last_wpt_wrap)
			real_len = wpt + vff_size - rpt;
		else
			real_len = wpt - rpt;

		if (rx_cb != NULL) {
			tail_len = vff_size - rpt;
			p_vff_buf = vff_base + rpt;
			if (tail_len >= real_len) {
				(*rx_cb) (p_dma_info, p_vff_buf, real_len);
			} else {
				(*rx_cb) (p_dma_info, p_vff_buf, tail_len);
				p_vff_buf = vff_base;
				(*rx_cb) (p_dma_info, p_vff_buf, real_len -
					  tail_len);
			}
			i_ret += real_len;
		} else
			BTIF_ERR_FUNC("no rx_cb found, please check your init process\n");
		mb();
		rpt += real_len;
		if (rpt >= vff_size) {
			/*read wrap bit should be revert*/
			rpt_wrap ^= DMA_RPT_WRAP;
			rpt %= vff_size;
		}
		rpt |= rpt_wrap;
/*record wpt, last_wpt_wrap, rpt, last_rpt_wrap*/
		p_mtk_vfifo->wpt = wpt;
		p_mtk_vfifo->last_wpt_wrap = wpt_wrap;

		p_mtk_vfifo->rpt = rpt;
		p_mtk_vfifo->last_rpt_wrap = rpt_wrap;

/*update rpt information to DMA controller*/
		btif_reg_sync_writel(rpt, RX_DMA_VFF_RPT(base));

/*get vff valid size again and check if rx data is processed completely*/
		valid_len = BTIF_READ32(RX_DMA_VFF_VALID_SIZE(base));

		rpt = BTIF_READ32(RX_DMA_VFF_RPT(base));
		wpt = BTIF_READ32(RX_DMA_VFF_WPT(base));
	}

/*enable DMA Rx IER*/
	hal_btif_dma_ier_ctrl(p_dma_info, true);

	spin_unlock_irqrestore(&(g_clk_cg_spinlock), flag);

	return i_ret;
}

static int hal_tx_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info,
			       ENUM_BTIF_REG_ID flag)
{
	int i_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int int_flag = 0;
	unsigned int enable = 0;
	unsigned int stop = 0;
	unsigned int flush = 0;
	unsigned int wpt = 0;
	unsigned int rpt = 0;
	unsigned int int_buf = 0;
	unsigned int valid_size = 0;
	/*unsigned long irq_flag = 0;*/

#if defined(CONFIG_MTK_CLKMGR)
	/*spin_lock_irqsave(&(g_clk_cg_spinlock), irq_flag);*/
	if (clock_is_on(MTK_BTIF_APDMA_CLK_CG) == 0) {
		/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/
		BTIF_ERR_FUNC("%s: clock is off, this should never happen!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
	int_flag = BTIF_READ32(TX_DMA_INT_FLAG(base));
	enable = BTIF_READ32(TX_DMA_EN(base));
	stop = BTIF_READ32(TX_DMA_STOP(base));
	flush = BTIF_READ32(TX_DMA_FLUSH(base));
	wpt = BTIF_READ32(TX_DMA_VFF_WPT(base));
	rpt = BTIF_READ32(TX_DMA_VFF_RPT(base));
	int_buf = BTIF_READ32(TX_DMA_INT_BUF_SIZE(base));
	valid_size = BTIF_READ32(TX_DMA_VFF_VALID_SIZE(base));
	/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/

	BTIF_INFO_FUNC("DMA's clock is on\n");
	BTIF_INFO_FUNC("Tx DMA's base address: 0x%lx\n", base);

	if (flag == REG_TX_DMA_ALL) {
		BTIF_INFO_FUNC("TX_EN(:0x%x\n", enable);
		BTIF_INFO_FUNC("INT_FLAG:0x%x\n", int_flag);
		BTIF_INFO_FUNC("TX_STOP:0x%x\n", stop);
		BTIF_INFO_FUNC("TX_FLUSH:0x%x\n", flush);
		BTIF_INFO_FUNC("TX_WPT:0x%x\n", wpt);
		BTIF_INFO_FUNC("TX_RPT:0x%x\n", rpt);
		BTIF_INFO_FUNC("INT_BUF_SIZE:0x%x\n", int_buf);
		BTIF_INFO_FUNC("VALID_SIZE:0x%x\n", valid_size);
		BTIF_INFO_FUNC("INT_EN:0x%x\n",
			       BTIF_READ32(TX_DMA_INT_EN(base)));
		BTIF_INFO_FUNC("TX_RST:0x%x\n", BTIF_READ32(TX_DMA_RST(base)));
		BTIF_INFO_FUNC("VFF_ADDR:0x%x\n",
			       BTIF_READ32(TX_DMA_VFF_ADDR(base)));
		BTIF_INFO_FUNC("VFF_LEN:0x%x\n",
			       BTIF_READ32(TX_DMA_VFF_LEN(base)));
		BTIF_INFO_FUNC("TX_THRE:0x%x\n",
			       BTIF_READ32(TX_DMA_VFF_THRE(base)));
		BTIF_INFO_FUNC("W_INT_BUF_SIZE:0x%x\n",
			       BTIF_READ32(TX_DMA_W_INT_BUF_SIZE(base)));
		BTIF_INFO_FUNC("LEFT_SIZE:0x%x\n",
			       BTIF_READ32(TX_DMA_VFF_LEFT_SIZE(base)));
		BTIF_INFO_FUNC("DBG_STATUS:0x%x\n",
			       BTIF_READ32(TX_DMA_DEBUG_STATUS(base)));
		i_ret = 0;
	} else {
		BTIF_WARN_FUNC("unknown flag:%d\n", flag);
	}
	BTIF_INFO_FUNC("tx dma %s\n", (enable & DMA_EN_BIT) &&
		       (!(stop && DMA_STOP_BIT)) ? "enabled" : "stopped");
	BTIF_INFO_FUNC("data in tx dma is %s sent by HW\n",
		       ((wpt == rpt) &&
			(int_buf == 0)) ? "completely" : "not completely");

	return i_ret;
}

static int hal_rx_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info,
			       ENUM_BTIF_REG_ID flag)
{
	int i_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int int_flag = 0;
	unsigned int enable = 0;
	unsigned int stop = 0;
	unsigned int flush = 0;
	unsigned int wpt = 0;
	unsigned int rpt = 0;
	unsigned int int_buf = 0;
	unsigned int valid_size = 0;
	/*unsigned long irq_flag = 0;*/
#if defined(CONFIG_MTK_CLKMGR)
	/*spin_lock_irqsave(&(g_clk_cg_spinlock), irq_flag);*/
	if (clock_is_on(MTK_BTIF_APDMA_CLK_CG) == 0) {
		/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/
		BTIF_ERR_FUNC("%s: clock is off, this should never happen!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
	BTIF_INFO_FUNC("dump DMA status register\n");
	_btif_dma_dump_dbg_reg();

	int_flag = BTIF_READ32(RX_DMA_INT_FLAG(base));
	enable = BTIF_READ32(RX_DMA_EN(base));
	stop = BTIF_READ32(RX_DMA_STOP(base));
	flush = BTIF_READ32(RX_DMA_FLUSH(base));
	wpt = BTIF_READ32(RX_DMA_VFF_WPT(base));
	rpt = BTIF_READ32(RX_DMA_VFF_RPT(base));
	int_buf = BTIF_READ32(RX_DMA_INT_BUF_SIZE(base));
	valid_size = BTIF_READ32(RX_DMA_VFF_VALID_SIZE(base));
	/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/

	BTIF_INFO_FUNC("DMA's clock is on\n");
	BTIF_INFO_FUNC("Rx DMA's base address: 0x%lx\n", base);

	if (flag == REG_RX_DMA_ALL) {
		BTIF_INFO_FUNC("RX_EN(:0x%x\n", enable);
		BTIF_INFO_FUNC("RX_STOP:0x%x\n", stop);
		BTIF_INFO_FUNC("RX_FLUSH:0x%x\n", flush);
		BTIF_INFO_FUNC("INT_FLAG:0x%x\n", int_flag);
		BTIF_INFO_FUNC("RX_WPT:0x%x\n", wpt);
		BTIF_INFO_FUNC("RX_RPT:0x%x\n", rpt);
		BTIF_INFO_FUNC("INT_BUF_SIZE:0x%x\n", int_buf);
		BTIF_INFO_FUNC("VALID_SIZE:0x%x\n", valid_size);
		BTIF_INFO_FUNC("INT_EN:0x%x\n",
			       BTIF_READ32(RX_DMA_INT_EN(base)));
		BTIF_INFO_FUNC("RX_RST:0x%x\n", BTIF_READ32(RX_DMA_RST(base)));
		BTIF_INFO_FUNC("VFF_ADDR:0x%x\n",
			       BTIF_READ32(RX_DMA_VFF_ADDR(base)));
		BTIF_INFO_FUNC("VFF_LEN:0x%x\n",
			       BTIF_READ32(RX_DMA_VFF_LEN(base)));
		BTIF_INFO_FUNC("RX_THRE:0x%x\n",
			       BTIF_READ32(RX_DMA_VFF_THRE(base)));
		BTIF_INFO_FUNC("RX_FLOW_CTRL_THRE:0x%x\n",
			       BTIF_READ32(RX_DMA_FLOW_CTRL_THRE(base)));
		BTIF_INFO_FUNC("LEFT_SIZE:0x%x\n",
			       BTIF_READ32(RX_DMA_VFF_LEFT_SIZE(base)));
		BTIF_INFO_FUNC("DBG_STATUS:0x%x\n",
			       BTIF_READ32(RX_DMA_DEBUG_STATUS(base)));
		i_ret = 0;
	} else {
		BTIF_WARN_FUNC("unknown flag:%d\n", flag);
	}
	BTIF_INFO_FUNC("rx dma %s\n", (enable & DMA_EN_BIT) &&
		       (!(stop && DMA_STOP_BIT)) ? "enabled" : "stopped");
	BTIF_INFO_FUNC("data in rx dma is %s by driver\n",
		       ((wpt == rpt) &&
			(int_buf == 0)) ? "received" : "not received");

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_dump_reg
* DESCRIPTION
*  dump BTIF module's information when needed
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* flag             [IN]        register id flag
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info, ENUM_BTIF_REG_ID flag)
{
	unsigned int i_ret = -1;

	if (p_dma_info->dir == DMA_DIR_TX)
		i_ret = hal_tx_dma_dump_reg(p_dma_info, flag);
	else if (p_dma_info->dir == DMA_DIR_RX)
		i_ret = hal_rx_dma_dump_reg(p_dma_info, flag);
	else
		BTIF_WARN_FUNC("unknown dir:%d\n", p_dma_info->dir);

	return i_ret;
}

static int _tx_dma_flush(P_MTK_DMA_INFO_STR p_dma_info)
{
	unsigned int i_ret = -1;
	unsigned long base = p_dma_info->base;
	unsigned int stop = BTIF_READ32(TX_DMA_STOP(base));

/*in MTK DMA BTIF channel we cannot set STOP and FLUSH bit at the same time*/
	if ((stop && DMA_STOP_BIT) != 0)
		BTIF_ERR_FUNC("BTIF's DMA in stop state, omit flush operation\n");
	else {
		BTIF_DBG_FUNC("flush tx dma\n");
		BTIF_SET_BIT(TX_DMA_FLUSH(base), DMA_FLUSH_BIT);
		i_ret = 0;
	}
	return i_ret;
}

static int _is_tx_dma_in_flush(P_MTK_DMA_INFO_STR p_dma_info)
{
	bool b_ret = true;
	unsigned long base = p_dma_info->base;

/*see if flush operation is in process*/
	b_ret = ((DMA_FLUSH_BIT & BTIF_READ32(TX_DMA_FLUSH(base))) != 0) ? true : false;

	return b_ret;
}

int hal_dma_pm_ops(P_MTK_DMA_INFO_STR p_dma_info, MTK_BTIF_PM_OPID opid)
{
	int i_ret = -1;

	BTIF_INFO_FUNC("op id: %d\n", opid);
	switch (opid) {
	case BTIF_PM_DPIDLE_EN:
		i_ret = 0;
		break;
	case BTIF_PM_DPIDLE_DIS:
		i_ret = 0;
		break;
	case BTIF_PM_SUSPEND:
		i_ret = 0;
		break;
	case BTIF_PM_RESUME:
		i_ret = 0;
		break;
	case BTIF_PM_RESTORE_NOIRQ:{
			unsigned int flag = 0;
			P_MTK_BTIF_IRQ_STR p_irq = p_dma_info->p_irq;

#ifdef CONFIG_OF
			flag = p_irq->irq_flags;
#else
			switch (p_irq->sens_type) {
			case IRQ_SENS_EDGE:
				if (p_irq->edge_type == IRQ_EDGE_FALL)
					flag = IRQF_TRIGGER_FALLING;
				else if (p_irq->edge_type == IRQ_EDGE_RAISE)
					flag = IRQF_TRIGGER_RISING;
				else if (p_irq->edge_type == IRQ_EDGE_BOTH)
					flag = IRQF_TRIGGER_RISING |
					    IRQF_TRIGGER_FALLING;
				else
					flag = IRQF_TRIGGER_FALLING;	/*make this as default type */
				break;
			case IRQ_SENS_LVL:
				if (p_irq->lvl_type == IRQ_LVL_LOW)
					flag = IRQF_TRIGGER_LOW;
				else if (p_irq->lvl_type == IRQ_LVL_HIGH)
					flag = IRQF_TRIGGER_HIGH;
				else
					flag = IRQF_TRIGGER_LOW;	/*make this as default type */
				break;
			default:
				flag = IRQF_TRIGGER_LOW;	/*make this as default type */
				break;
			}
#endif
/* irq_set_irq_type(p_irq->irq_id, flag); */
			i_ret = 0;
		}
		i_ret = 0;
		break;
	default:
		i_ret = ERR_INVALID_PAR;
		break;
	}

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_dma_receive_data
* DESCRIPTION
*  receive data from btif module in DMA polling mode
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  positive means data is available, 0 means no data available
*****************************************************************************/
#ifndef MTK_BTIF_MARK_UNUSED_API
int hal_dma_receive_data(P_MTK_DMA_INFO_STR p_dma_info,
			 unsigned char *p_buf, const unsigned int max_len)
{
	unsigned int i_ret = -1;

	return i_ret;
}
#endif

int _btif_dma_dump_dbg_reg(void)
{
#if 0
	static MTK_BTIF_DMA_REG_DMP_DBG g_dma_dbg_regs[] = {
		{0x10201180, 0x0},
		{0x10201184, 0x0},
		{0x10201188, 0x0},
		{0x1020118C, 0x0},
		{0x10201190, 0x0},
		{0x1000320C, 0x0},
		{0x10003210, 0x0},
		{0x10003214, 0x0},
	};

	int i = 0;
	char *addr1 = NULL;
	char *addr2 = NULL;

	int array_num = ARRAY_SIZE(g_dma_dbg_regs)

	addr1 = ioremap(g_dma_dbg_regs[0].reg_addr, 0x20);
	if (addr1) {
		for (i = 0; i < 5; i++)
			g_dma_dbg_regs[i].reg_val = *(volatile unsigned int*)(addr1 + i*4);
		iounmap(addr1);
	}

	addr2 = ioremap(g_dma_dbg_regs[5].reg_addr, 0x10);
	if (addr2) {
		g_dma_dbg_regs[5].reg_val = *(volatile unsigned int*)(addr2);
		g_dma_dbg_regs[6].reg_val = *(volatile unsigned int*)(addr2+4);
		g_dma_dbg_regs[7].reg_val = *(volatile unsigned int*)(addr2+8);
		iounmap(addr2);
	}

	for (i = 0; i < array_num; i++)
		BTIF_INFO_FUNC("<reg, val>-<0x%lx, 0x%08x>\n", g_dma_dbg_regs[i].reg_addr, g_dma_dbg_regs[i].reg_val);
#endif
	return 0;
}

static void hal_btif_tx_dma_vff_set_for_4g(void)
{
	BTIF_DBG_FUNC("Set btif tx_vff_addr bit29\n");
	BTIF_SET_BIT(TX_DMA_VFF_ADDR_H(mtk_btif_tx_dma.base), DMA_VFF_BIT29_OFFSET);
	BTIF_DBG_FUNC("Dump value of bit29 0x%x:(0x%x)\n", TX_DMA_VFF_ADDR_H(mtk_btif_tx_dma.base),
					BTIF_READ32(TX_DMA_VFF_ADDR_H(mtk_btif_tx_dma.base)));
}
static void hal_btif_rx_dma_vff_set_for_4g(void)
{
	BTIF_DBG_FUNC("Set btif rx_vff_addr bit29\n");
	BTIF_SET_BIT(RX_DMA_VFF_ADDR_H(mtk_btif_rx_dma.base), DMA_VFF_BIT29_OFFSET);
	BTIF_DBG_FUNC("Dump value of bit29 0x%x:(0x%x)\n", RX_DMA_VFF_ADDR_H(mtk_btif_rx_dma.base),
					BTIF_READ32(RX_DMA_VFF_ADDR_H(mtk_btif_rx_dma.base)));
}

