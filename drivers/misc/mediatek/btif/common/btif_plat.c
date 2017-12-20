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

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG "MTK-BTIF"

#define NEW_TX_HANDLING_SUPPORT 1

#include "btif_pub.h"
#include "btif_priv.h"

#define BTIF_USER_ID "btif_driver"

static spinlock_t g_clk_cg_spinlock;	/*BTIF clock's spinlock */

/*-----------------------------BTIF Module Clock and Power Control Defination------------------*/

MTK_BTIF_IRQ_STR mtk_btif_irq = {
	.name = "mtk btif irq",
	.is_irq_sup = true,
	.reg_flag = false,
#ifdef CONFIG_OF
	.irq_flags = IRQF_TRIGGER_NONE,
#else
	.irq_id = MT_BTIF_IRQ_ID,
	.sens_type = IRQ_SENS_LVL,
	.lvl_type = IRQ_LVL_LOW,
#endif
	.p_irq_handler = NULL,
};

/*
 * will call clock manager's API export by WCP to control BTIF's clock,
 * but we may need to access these registers in case of btif clock control logic is wrong in clock manager
 */

MTK_BTIF_INFO_STR mtk_btif = {
#ifndef CONFIG_OF
	.base = MTK_BTIF_REG_BASE,
#endif
	.p_irq = &mtk_btif_irq,
	.tx_fifo_size = BTIF_TX_FIFO_SIZE,
	.rx_fifo_size = BTIF_RX_FIFO_SIZE,
	.tx_tri_lvl = BTIF_TX_FIFO_THRE,
	.rx_tri_lvl = BTIF_RX_FIFO_THRE,
	.rx_data_len = 0,
	.p_tx_fifo = NULL,
};
#if !(NEW_TX_HANDLING_SUPPORT)
static bool _btif_is_tx_allow(P_MTK_BTIF_INFO_STR p_btif);
#endif

static int btif_rx_irq_handler(P_MTK_BTIF_INFO_STR p_btif_info,
			       unsigned char *p_buf,
			       const unsigned int max_len);
static int btif_tx_irq_handler(P_MTK_BTIF_INFO_STR p_btif);

/*****************************************************************************
* FUNCTION
*  hal_btif_rx_ier_ctrl
* DESCRIPTION
*  BTIF Rx interrupt enable/disable
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* enable    [IN]        control if rx interrupt enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int hal_btif_rx_ier_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en);

/*****************************************************************************
* FUNCTION
*  hal_btif_tx_ier_ctrl
* DESCRIPTION
*  BTIF Tx interrupt enable/disable
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* enable    [IN]        control if tx interrupt enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int hal_btif_tx_ier_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en);

#ifndef MTK_BTIF_MARK_UNUSED_API
static int btif_sleep_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en);

/*****************************************************************************
* FUNCTION
*  _btif_receive_data
* DESCRIPTION
*  receive data from btif module in FIFO polling mode
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  positive means data is available, 0 means no data available
*****************************************************************************/
static int _btif_receive_data(P_MTK_BTIF_INFO_STR p_btif,
			      unsigned char *p_buf, const unsigned int max_len);
static int btif_tx_thr_set(P_MTK_BTIF_INFO_STR p_btif, unsigned int thr_count);
#endif

static int btif_dump_array(char *string, char *p_buf, int len)
{
	unsigned int idx = 0;
	unsigned char str[30];
	unsigned char *p_str;

	pr_debug("========dump %s start <length:%d>========\n", string, len);
	p_str = &str[0];
	for (idx = 0; idx < len; idx++, p_buf++) {
		sprintf(p_str, "%02x ", *p_buf);
		p_str += 3;
		if (7 == (idx % 8)) {
			*p_str++ = '\n';
			*p_str = '\0';
			pr_debug("%s", str);
			p_str = &str[0];
		}
	}
	if (len % 8) {
		*p_str++ = '\n';
		*p_str = '\0';
		pr_debug("%s", str);
	}
	pr_debug("========dump %s end========\n", string);
	return 0;
}

#if NEW_TX_HANDLING_SUPPORT
static int _btif_tx_fifo_init(P_MTK_BTIF_INFO_STR p_btif_info)
{
	int i_ret = -1;

	spin_lock_init(&(p_btif_info->tx_fifo_spinlock));

	if (p_btif_info->p_tx_fifo == NULL) {
		p_btif_info->p_tx_fifo = kzalloc(sizeof(struct kfifo),
						 GFP_ATOMIC);
		if (p_btif_info->p_tx_fifo == NULL) {
			i_ret = -ENOMEM;
			BTIF_ERR_FUNC("kzalloc for p_btif->p_tx_fifo failed\n");
			goto ret;
		}

		i_ret = kfifo_alloc(p_btif_info->p_tx_fifo,
				    BTIF_HAL_TX_FIFO_SIZE, GFP_ATOMIC);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("kfifo_alloc failed, errno(%d)\n", i_ret);
			i_ret = -ENOMEM;
			goto ret;
		}
		i_ret = 0;
	} else {
		BTIF_WARN_FUNC
		    ("p_btif_info->p_tx_fifo is already init p_btif_info->p_tx_fifo(0x%p)\n",
		     p_btif_info->p_tx_fifo);
		i_ret = 0;
	}
ret:
	return i_ret;
}

static int _get_btif_tx_fifo_room(P_MTK_BTIF_INFO_STR p_btif_info)
{
	int i_ret = 0;
	unsigned long flag = 0;

	spin_lock_irqsave(&(p_btif_info->tx_fifo_spinlock), flag);
	if (p_btif_info->p_tx_fifo == NULL)
		i_ret = 0;
	else
		i_ret = kfifo_avail(p_btif_info->p_tx_fifo);
	spin_unlock_irqrestore(&(p_btif_info->tx_fifo_spinlock), flag);
	BTIF_DBG_FUNC("tx kfifo:0x%p, available room:%d\n", p_btif_info->p_tx_fifo, i_ret);
	return i_ret;
}

static int _btif_tx_fifo_reset(P_MTK_BTIF_INFO_STR p_btif_info)
{
	int i_ret = 0;

	if (p_btif_info->p_tx_fifo != NULL)
		kfifo_reset(p_btif_info->p_tx_fifo);
	return i_ret;
}

#endif

#ifdef CONFIG_OF
static void _btif_set_default_setting(void)
{
	struct device_node *node = NULL;
	unsigned int irq_info[3] = {0, 0, 0};
	unsigned int phy_base;

	node = of_find_compatible_node(NULL, NULL, "mediatek,btif");
	if (node) {
		mtk_btif.p_irq->irq_id = irq_of_parse_and_map(node, 0);
		/*fixme, be compitable arch 64bits*/
		mtk_btif.base = (unsigned long)of_iomap(node, 0);
		BTIF_INFO_FUNC("get btif irq(%d),register base(0x%lx)\n",
			mtk_btif.p_irq->irq_id, mtk_btif.base);
	} else {
		BTIF_ERR_FUNC("get btif device node fail\n");
	}

	/* get the interrupt line behaviour */
	if (of_property_read_u32_array(node, "interrupts", irq_info, ARRAY_SIZE(irq_info))) {
		BTIF_ERR_FUNC("get interrupt flag from DTS fail\n");
	} else {
		mtk_btif.p_irq->irq_flags = irq_info[2];
		BTIF_INFO_FUNC("get interrupt flag(0x%x)\n", mtk_btif.p_irq->irq_flags);
	}

	if (of_property_read_u32_index(node, "reg", 1, &phy_base))
		BTIF_ERR_FUNC("get register phy base from DTS fail\n");
	else
		BTIF_INFO_FUNC("get register phy base(0x%x)\n", (unsigned int)phy_base);
}
#endif

/*****************************************************************************
* FUNCTION
*  hal_btif_info_get
* DESCRIPTION
*  get btif's information included base address , irq related information
* PARAMETERS
* RETURNS
*  BTIF's information
*****************************************************************************/
P_MTK_BTIF_INFO_STR hal_btif_info_get(void)
{
#if NEW_TX_HANDLING_SUPPORT
	int i_ret = 0;
/*tx fifo and fifo lock init*/
	i_ret = _btif_tx_fifo_init(&mtk_btif);
	if (i_ret == 0)
		BTIF_INFO_FUNC("_btif_tx_fifo_init succeed\n");
	else
		BTIF_ERR_FUNC("_btif_tx_fifo_init failed, i_ret:%d\n", i_ret);

#endif

#ifdef CONFIG_OF
	_btif_set_default_setting();
#endif

	spin_lock_init(&g_clk_cg_spinlock);

	return &mtk_btif;
}
/*****************************************************************************
* FUNCTION
*  hal_btif_clk_get_and_prepare
* DESCRIPTION
*  get clock from device tree and prepare for enable/disable control
* PARAMETERS
* pdev  device pointer
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
#if !defined(CONFIG_MTK_CLKMGR)
int hal_btif_clk_get_and_prepare(struct platform_device *pdev)
{
		int i_ret = -1;

		clk_btif = devm_clk_get(&pdev->dev, "btifc");
		if (IS_ERR(clk_btif)) {
			BTIF_ERR_FUNC("[CCF]cannot get clk_btif clock.\n");
			return PTR_ERR(clk_btif);
		}
		BTIF_ERR_FUNC("[CCF]clk_btif=%p\n", clk_btif);
		clk_btif_apdma = devm_clk_get(&pdev->dev, "apdmac");
		if (IS_ERR(clk_btif_apdma)) {
			BTIF_ERR_FUNC("[CCF]cannot get clk_btif_apdma clock.\n");
			return PTR_ERR(clk_btif_apdma);
		}
		BTIF_ERR_FUNC("[CCF]clk_btif_apdma=%p\n", clk_btif_apdma);

		i_ret = clk_prepare(clk_btif);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("clk_prepare clk_btif failed! ret:%d\n", i_ret);
			return i_ret;
		}

		i_ret = clk_prepare(clk_btif_apdma);
		if (i_ret != 0) {
			BTIF_ERR_FUNC("clk_prepare clk_btif_apdma failed! ret:%d\n", i_ret);
			return i_ret;
		}
		return i_ret;
}
/*****************************************************************************
* FUNCTION
*  hal_btif_clk_unprepare
* DESCRIPTION
*  unprepare btif clock and apdma clock
* PARAMETERS
* none
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_clk_unprepare(void)
{
	clk_unprepare(clk_btif);
	clk_unprepare(clk_btif_apdma);
	return 0;
}
#endif
/*****************************************************************************
* FUNCTION
*  hal_btif_clk_ctrl
* DESCRIPTION
*  control clock output enable/disable of BTIF module
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_clk_ctrl(P_MTK_BTIF_INFO_STR p_btif, ENUM_CLOCK_CTRL flag)
{
/*In MTK BTIF, there's only one global CG on AP_DMA, no sub channel's CG bit*/
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
			i_ret = enable_clock(MTK_BTIF_CG_BIT, BTIF_USER_ID);
#else
			BTIF_DBG_FUNC("[CCF]enable clk_btif\n");
			i_ret = clk_enable(clk_btif);
#endif /* defined(CONFIG_MTK_CLKMGR) */
			if (i_ret) {
				BTIF_WARN_FUNC
					("enable_clock for MTK_BTIF_CG_BIT failed, ret:%d",
					 i_ret);
			}
		}
	} else if (flag == CLK_OUT_DISABLE) {
		if (atomic_dec_return(&s_clk_ref) == 0) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = disable_clock(MTK_BTIF_CG_BIT, BTIF_USER_ID);
			if (i_ret) {
				BTIF_WARN_FUNC
					("disable_clock for MTK_BTIF_CG_BIT failed, ret:%d",
					 i_ret);
			}
#else
			BTIF_DBG_FUNC("[CCF] clk_disable(clk_btif) calling\n");
			clk_disable(clk_btif);
#endif /* defined(CONFIG_MTK_CLKMGR) */
		}
	} else {
		i_ret = ERR_INVALID_PAR;
		BTIF_ERR_FUNC("invalid	clock ctrl flag (%d)\n", flag);
	}

#else

	if (status == flag) {
		i_ret = 0;
		BTIF_DBG_FUNC("btif clock already %s\n",
			      CLK_OUT_ENABLE ==
			      status ? "enabled" : "disabled");
	} else {
		if (flag == CLK_OUT_ENABLE) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = enable_clock(MTK_BTIF_CG_BIT, BTIF_USER_ID);
#else
			BTIF_DBG_FUNC("[CCF]enable clk_btif\n");
			i_ret = clk_enable(clk_btif);
#endif /* defined(CONFIG_MTK_CLKMGR) */
			status = (i_ret == 0) ? flag : status;
			if (i_ret) {
				BTIF_WARN_FUNC
					("enable_clock for MTK_BTIF_CG_BIT failed, ret:%d",
					 i_ret);
			}
		} else if (flag == CLK_OUT_DISABLE) {
#if defined(CONFIG_MTK_CLKMGR)
			i_ret = disable_clock(MTK_BTIF_CG_BIT, BTIF_USER_ID);
			status = (i_ret == 0) ? flag : status;
			if (i_ret) {
				BTIF_WARN_FUNC
					("disable_clock for MTK_BTIF_CG_BIT failed, ret:%d",
					 i_ret);
			}
#else
			BTIF_DBG_FUNC("[CCF] clk_disable(clk_btif) calling\n");
			clk_disable(clk_btif);
#endif /* defined(CONFIG_MTK_CLKMGR) */
		} else {
			i_ret = ERR_INVALID_PAR;
			BTIF_ERR_FUNC("invalid	clock ctrl flag (%d)\n", flag);
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
		BTIF_DBG_FUNC("btif clock %s\n", flag == CLK_OUT_ENABLE ? "enabled" : "disabled");
	} else {
		BTIF_ERR_FUNC("%s btif clock failed, ret(%d)\n",
				flag == CLK_OUT_ENABLE ? "enable" : "disable", i_ret);
	}
#else

	if (i_ret == 0) {
		BTIF_DBG_FUNC("btif clock %s\n", flag == CLK_OUT_ENABLE ? "enabled" : "disabled");
	} else {
		BTIF_ERR_FUNC("%s btif clock failed, ret(%d)\n",
			      flag == CLK_OUT_ENABLE ? "enable" : "disable", i_ret);
	}
#endif
#if defined(CONFIG_MTK_CLKMGR)
	BTIF_DBG_FUNC("BTIF's clock is %s\n", (clock_is_on(MTK_BTIF_CG_BIT) == 0) ? "off" : "on");
#endif
	return i_ret;
}

static int btif_new_handshake_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool enable)
{
	unsigned long base = p_btif->base;

	if (enable == true)
		BTIF_SET_BIT(BTIF_HANDSHAKE(base), BTIF_HANDSHAKE_EN_HANDSHAKE);
	else
		BTIF_CLR_BIT(BTIF_HANDSHAKE(base), BTIF_HANDSHAKE_EN_HANDSHAKE);
	return true;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_hw_init
* DESCRIPTION
*  BTIF hardware init
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_hw_init(P_MTK_BTIF_INFO_STR p_btif)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;
	unsigned long base = p_btif->base;

#if NEW_TX_HANDLING_SUPPORT
	_btif_tx_fifo_reset(p_btif);
#endif

/*set to normal mode*/
	btif_reg_sync_writel(BTIF_FAKELCR_NORMAL_MODE, BTIF_FAKELCR(base));
/*set to newhandshake mode*/
	btif_new_handshake_ctrl(p_btif, true);
/*No need to access: enable sleep mode*/
/*No need to access: set Rx timeout count*/
/*set Tx threshold*/
/*set Rx threshold*/
/*disable internal loopback test*/

/*set Rx FIFO clear bit to 1*/
	BTIF_SET_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_RX);
/*clear Rx FIFO clear bit to 0*/
	BTIF_CLR_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_RX);
/*set Tx FIFO clear bit to 1*/
	BTIF_SET_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_TX);
/*clear Tx FIFO clear bit to 0*/
	BTIF_CLR_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_TX);

	btif_reg_sync_writel(BTIF_TRI_LVL_TX(p_btif->tx_tri_lvl)
			     | BTIF_TRI_LVL_RX(p_btif->rx_tri_lvl)
			     | BTIF_TRI_LOOP_DIS, BTIF_TRI_LVL(base));
	hal_btif_loopback_ctrl(p_btif, false);
/*disable BTIF Tx DMA mode*/
	hal_btif_tx_mode_ctrl(p_btif, false);
/*disable BTIF Rx DMA mode*/
	hal_btif_rx_mode_ctrl(p_btif, false);
/*auto reset*/
	BTIF_SET_BIT(BTIF_DMA_EN(base), BTIF_DMA_EN_AUTORST_EN);
/*disable Tx IER*/
	hal_btif_tx_ier_ctrl(p_btif, false);
/*enable Rx IER by default*/
	hal_btif_rx_ier_ctrl(p_btif, true);

	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_rx_ier_ctrl
* DESCRIPTION
*  BTIF Rx interrupt enable/disable
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* enable    [IN]        control if rx interrupt enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_rx_ier_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (en == false)
		BTIF_CLR_BIT(BTIF_IER(base), BTIF_IER_RXFEN);
	else
		BTIF_SET_BIT(BTIF_IER(base), BTIF_IER_RXFEN);

/*TODO:do we need to read back ? Answer: no*/
	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_tx_ier_ctrl
* DESCRIPTION
*  BTIF Tx interrupt enable/disable
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* enable    [IN]        control if tx interrupt enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_tx_ier_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (en == false)
		BTIF_CLR_BIT(BTIF_IER(base), BTIF_IER_TXEEN);
	else
		BTIF_SET_BIT(BTIF_IER(base), BTIF_IER_TXEEN);

/*TODO:do we need to read back ? Answer: no*/
	i_ret = 0;

	return i_ret;
}

#ifndef MTK_BTIF_MARK_UNUSED_API

/*****************************************************************************
* FUNCTION
*  _btif_receive_data
* DESCRIPTION
*  receive data from btif module in FIFO polling mode
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  positive means data is available, 0 means no data available
*****************************************************************************/
int _btif_receive_data(P_MTK_BTIF_INFO_STR p_btif,
		       unsigned char *p_buf, const unsigned int max_len)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;

/*check parameter valid or not*/
	if ((p_buf == NULL) || (max_len == 0)) {
		i_ret = ERR_INVALID_PAR;
		return i_ret;
	}
	i_ret = btif_rx_irq_handler(p_btif, p_buf, max_len);

	return i_ret;
}

int btif_sleep_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (en == false)
		BTIF_CLR_BIT(BTIF_SLEEP_EN(base), BTIF_SLEEP_EN_BIT);
	else
		BTIF_SET_BIT(BTIF_SLEEP_EN(base), BTIF_SLEEP_EN_BIT);

/*TODO:do we need to read back ? Answer: no*/
/*TODO:do we need to dsb?*/
	i_ret = 0;
	return i_ret;
}

static int btif_tx_thr_set(P_MTK_BTIF_INFO_STR p_btif, unsigned int thr_count)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;
	unsigned int value = 0;

/*read BTIF_TRI_LVL*/
	value = BTIF_READ32(BTIF_TRI_LVL(base));
/*clear Tx threshold bits*/
	value &= (~BTIF_TRI_LVL_TX_MASK);
/*set tx threshold bits*/
	value |= BTIF_TRI_LVL_TX(BTIF_TX_FIFO_THRE);
/*write back to BTIF_TRI_LVL*/
	btif_reg_sync_writel(value, BTIF_TRI_LVL(base));

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  btif_rx_fifo_reset
* DESCRIPTION
*  reset BTIF's rx fifo
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* ec         [IN]        control if loopback mode is enabled or not
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int btif_rx_fifo_reset(P_MTK_BTIF_INFO_STR p_btif)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;
	unsigned long base = p_btif->base;

/*set Rx FIFO clear bit to 1*/
	BTIF_SET_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_RX);

/*clear Rx FIFO clear bit to 0*/
	BTIF_CLR_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_RX);

/*TODO:do we need to read back ? Answer: no*/
/*TODO:do we need to dsb?*/
	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  btif_tx_fifo_reset
* DESCRIPTION
*  reset BTIF's tx fifo
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int btif_tx_fifo_reset(P_MTK_BTIF_INFO_STR p_btif)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

/*set Tx FIFO clear bit to 1*/
	BTIF_SET_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_TX);

/*clear Tx FIFO clear bit to 0*/
	BTIF_CLR_BIT(BTIF_FIFOCTRL(base), BTIF_FIFOCTRL_CLR_TX);

/*TODO:do we need to read back ? Answer: no*/
/*TODO:do we need to dsb?*/
	i_ret = 0;
	return i_ret;
}

#endif

/*****************************************************************************
* FUNCTION
*  hal_btif_loopback_ctrl
* DESCRIPTION
*  BTIF Tx/Rx loopback mode set, this operation can only be done after set BTIF to normal mode
* PARAMETERS
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_loopback_ctrl(P_MTK_BTIF_INFO_STR p_btif, bool en)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (en == false)
		BTIF_CLR_BIT(BTIF_TRI_LVL(base), BTIF_TRI_LOOP_EN);
	else
		BTIF_SET_BIT(BTIF_TRI_LVL(base), BTIF_TRI_LOOP_EN);

/*TODO:do we need to read back ? Answer: no*/
/*TODO:do we need to dsb?*/
	i_ret = 0;
	return i_ret;
}


/*****************************************************************************
* FUNCTION
*  hal_btif_rx_handler
* DESCRIPTION
*  lower level interrupt handler
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  0 means success; negative means fail; positive means rx data length
*****************************************************************************/
int hal_btif_irq_handler(P_MTK_BTIF_INFO_STR p_btif,
			 unsigned char *p_buf, const unsigned int max_len)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;
	unsigned int iir = 0;
	unsigned int rx_len = 0;
	unsigned long base = p_btif->base;

#if 0
/*check parameter valid or not*/
	if ((p_buf == NULL) || (max_len == 0)) {
		i_ret = ERR_INVALID_PAR;
		return i_ret;
	}
#endif
	unsigned long irq_flag = 0;

	spin_lock_irqsave(&(g_clk_cg_spinlock), irq_flag);

#if defined(CONFIG_MTK_CLKMGR)
	if (clock_is_on(MTK_BTIF_CG_BIT) == 0) {
		spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);
		BTIF_ERR_FUNC("%s: clock is off before irq handle done!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
/*read interrupt identifier register*/
	iir = BTIF_READ32(BTIF_IIR(base));

/*is rx interrupt exist?*/
#if 0
	while ((iir & BTIF_IIR_RX) && (rx_len < max_len)) {
		rx_len +=
		    btif_rx_irq_handler(p_btif, (p_buf + rx_len),
					(max_len - rx_len));

/*update IIR*/
		iir = BTIF_READ32(BTIF_IIR(base));
	}
#endif

	while (iir & (BTIF_IIR_RX | BTIF_IIR_RX_TIMEOUT)) {
		rx_len += btif_rx_irq_handler(p_btif, p_buf, max_len);

/*update IIR*/
		iir = BTIF_READ32(BTIF_IIR(base));
	}

/*is tx interrupt exist?*/
	if (iir & BTIF_IIR_TX_EMPTY)
		i_ret = btif_tx_irq_handler(p_btif);
	spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);

	i_ret = rx_len != 0 ? rx_len : i_ret;
	return i_ret;
}

int hal_btif_rx_cb_reg(P_MTK_BTIF_INFO_STR p_btif_info, btif_rx_buf_write rx_cb)
{
	if (p_btif_info->rx_cb != NULL)
		BTIF_DBG_FUNC("rx_cb already registered, replace (0x%p) with (0x%p)\n",
		     p_btif_info->rx_cb, rx_cb);
	p_btif_info->rx_cb = rx_cb;

	return 0;
}

/*****************************************************************************
* FUNCTION
*  btif_rx_irq_handler
* DESCRIPTION
*  lower level rx interrupt handler
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  positive means length of rx data , negative means fail
*****************************************************************************/
static int btif_rx_irq_handler(P_MTK_BTIF_INFO_STR p_btif_info,
			       unsigned char *p_buf, const unsigned int max_len)
{
/*Chaozhong: To be implement*/
	int i_ret = 0;
	unsigned int iir = 0;
	unsigned int rx_len = 0;
	unsigned long base = p_btif_info->base;
	unsigned char rx_buf[256];
	unsigned int local_buf_len = 256;
	btif_rx_buf_write rx_cb = p_btif_info->rx_cb;
	unsigned int total_len = 0;

/*read interrupt identifier register*/
	iir = BTIF_READ32(BTIF_IIR(base));
	while ((iir & (BTIF_IIR_RX | BTIF_IIR_RX_TIMEOUT)) &&
	       (rx_len < local_buf_len)) {
		rx_buf[rx_len] = BTIF_READ8(base);
		rx_len++;
/*need to consult CC Hwang for advice */
/*
 * whether we need to do memory barrier here
 * Ans: no
 */
/*
 * whether we need to d memory barrier when call BTIF_SET_BIT or BTIF_CLR_BIT
 * Ans: no
 */
		if (rx_len == local_buf_len) {
			if (rx_cb)
				(*rx_cb) (p_btif_info, rx_buf, rx_len);
			rx_len = 0;
			total_len += rx_len;
		}
		iir = BTIF_READ32(BTIF_IIR(base));
	}
	total_len += rx_len;
	if (rx_len && rx_cb)
		(*rx_cb) (p_btif_info, rx_buf, rx_len);

/*
 * make sure all data write back to memory, mb or dsb?
 * need to consult CC Hwang for advice
 * Ans: no need here
 */
	i_ret = total_len;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  btif_tx_irq_handler
* DESCRIPTION
*  lower level tx interrupt handler
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
static int btif_tx_irq_handler(P_MTK_BTIF_INFO_STR p_btif)
{
	int i_ret = -1;

#if NEW_TX_HANDLING_SUPPORT
	int how_many = 0;
	unsigned int lsr;
	unsigned int ava_len = 0;
	unsigned long base = p_btif->base;
	char local_buf[BTIF_TX_FIFO_SIZE];
	char *p_data = local_buf;
	unsigned long flag = 0;

	struct kfifo *p_tx_fifo = p_btif->p_tx_fifo;

/*read LSR and check THER or TEMT, either one is 1 means can accept tx data*/
	lsr = BTIF_READ32(BTIF_LSR(base));

	if (lsr & BTIF_LSR_TEMT_BIT)
		/*Tx Holding Register if empty, which means we can write tx FIFO count to BTIF*/
		ava_len = BTIF_TX_FIFO_SIZE;
	else if (lsr & BTIF_LSR_THRE_BIT)
		/*Tx Holding Register if empty, which means we can write (Tx FIFO count - Tx threshold)to BTIF*/
		ava_len = BTIF_TX_FIFO_SIZE - BTIF_TX_FIFO_THRE;
	else {
		/*this means data size in tx FIFO is more than Tx threshold, we will not write data to THR*/
		ava_len = 0;
		goto ret;
	}
	spin_lock_irqsave(&(p_btif->tx_fifo_spinlock), flag);
	how_many = kfifo_out(p_tx_fifo, local_buf, ava_len);
	spin_unlock_irqrestore(&(p_btif->tx_fifo_spinlock), flag);
	BTIF_DBG_FUNC("BTIF tx size %d done, left:%d\n", how_many,
		      kfifo_avail(p_tx_fifo));
	while (how_many--)
		btif_reg_sync_writeb(*(p_data++), BTIF_THR(base));

	spin_lock_irqsave(&(p_btif->tx_fifo_spinlock), flag);
/*clear Tx enable flag if necessary*/
	if (kfifo_is_empty(p_tx_fifo)) {
		hal_btif_tx_ier_ctrl(p_btif, false);
		BTIF_DBG_FUNC("BTIF tx FIFO is empty\n");
	}
	spin_unlock_irqrestore(&(p_btif->tx_fifo_spinlock), flag);
ret:
#else
/*clear Tx enable flag*/
	hal_btif_tx_ier_ctrl(p_btif, false);
#endif
	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_tx_mode_ctrl
* DESCRIPTION
*  set BTIF tx to corresponding mode (PIO/DMA)
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* mode     [IN]        rx mode <PIO/DMA>
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_tx_mode_ctrl(P_MTK_BTIF_INFO_STR p_btif, ENUM_BTIF_MODE mode)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (mode == BTIF_MODE_DMA)
		/*set to DMA mode*/
		BTIF_SET_BIT(BTIF_DMA_EN(base), BTIF_DMA_EN_TX);
	else
		/*set to PIO mode*/
		BTIF_CLR_BIT(BTIF_DMA_EN(base), BTIF_DMA_EN_TX);

	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_rx_mode_ctrl
* DESCRIPTION
*  set BTIF rx to corresponding mode (PIO/DMA)
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* mode     [IN]        rx mode <PIO/DMA>
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_rx_mode_ctrl(P_MTK_BTIF_INFO_STR p_btif, ENUM_BTIF_MODE mode)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;

	if (mode == BTIF_MODE_DMA)
		/*set to DMA mode*/
		BTIF_SET_BIT(BTIF_DMA_EN(base), BTIF_DMA_EN_RX);
	else
		/*set to PIO mode*/
		BTIF_CLR_BIT(BTIF_DMA_EN(base), BTIF_DMA_EN_RX);

	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_send_data
* DESCRIPTION
*  send data through btif in FIFO mode
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* p_buf     [IN]        pointer to rx data buffer
* max_len  [IN]        tx buffer length
* RETURNS
*   positive means number of data sent; 0 means no data put to FIFO; negative means error happens
*****************************************************************************/
int hal_btif_send_data(P_MTK_BTIF_INFO_STR p_btif,
		       const unsigned char *p_buf, const unsigned int buf_len)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;

	unsigned int ava_len = 0;
	unsigned int sent_len = 0;

#if !(NEW_TX_HANDLING_SUPPORT)
	unsigned long base = p_btif->base;
	unsigned int lsr = 0;
	unsigned int left_len = 0;
	unsigned char *p_data = (unsigned char *)p_buf;
#endif

/*check parameter valid or not*/
	if ((p_buf == NULL) || (buf_len == 0)) {
		i_ret = ERR_INVALID_PAR;
		return i_ret;
	}
#if NEW_TX_HANDLING_SUPPORT
	ava_len = _get_btif_tx_fifo_room(p_btif);
	sent_len = buf_len <= ava_len ? buf_len : ava_len;
	if (sent_len > 0) {
		int enqueue_len = 0;
		unsigned long flag = 0;

		spin_lock_irqsave(&(p_btif->tx_fifo_spinlock), flag);
		enqueue_len = kfifo_in(p_btif->p_tx_fifo,
				       (unsigned char *)p_buf, sent_len);
		if (sent_len != enqueue_len) {
			BTIF_ERR_FUNC("target tx len:%d, len sent:%d\n",
				      sent_len, enqueue_len);
		}
		i_ret = enqueue_len;
		mb();
/*enable BTIF Tx IRQ*/
		hal_btif_tx_ier_ctrl(p_btif, true);
		spin_unlock_irqrestore(&(p_btif->tx_fifo_spinlock), flag);
		BTIF_DBG_FUNC("enqueue len:%d\n", enqueue_len);
	} else {
		i_ret = 0;
	}
#else
	while ((_btif_is_tx_allow(p_btif)) && (sent_len < buf_len)) {
		/*read LSR and check THER or TEMT, either one is 1 means can accept tx data*/
		lsr = BTIF_READ32(BTIF_LSR(base));

		if (lsr & BTIF_LSR_TEMT_BIT)
			/*Tx Holding Register if empty, which means we can write tx FIFO count to BTIF*/
			ava_len = BTIF_TX_FIFO_SIZE;
		else if (lsr & BTIF_LSR_THRE_BIT)
			/*Tx Holding Register if empty, which means we can write (Tx FIFO count - Tx threshold)to BTIF*/
			ava_len = BTIF_TX_FIFO_SIZE - BTIF_TX_FIFO_THRE;
		else {
			/*this means data size in tx FIFO is more than Tx threshold, we will not write data to THR*/
			ava_len = 0;
			break;
		}

		left_len = buf_len - sent_len;
/*ava_len will be real length will write to BTIF THR*/
		ava_len = ava_len > left_len ? left_len : ava_len;
/*update sent length valud after this operation*/
		sent_len += ava_len;
/*
 * whether we need memory barrier here?
 * Ans: No, no memory ordering issue exist,
 * CPU will make sure logically right
 */
		while (ava_len--)
			btif_reg_sync_writeb(*(p_data++), BTIF_THR(base));

	}
/* while ((hal_btif_is_tx_allow()) && (sent_len < buf_len)); */

	i_ret = sent_len;

/*enable BTIF Tx IRQ*/
	hal_btif_tx_ier_ctrl(p_btif, true);
#endif
	return i_ret;
}


/*****************************************************************************
* FUNCTION
*  hal_btif_raise_wak_sig
* DESCRIPTION
*  raise wakeup signal to counterpart
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_raise_wak_sig(P_MTK_BTIF_INFO_STR p_btif)
{
	int i_ret = -1;
	unsigned long base = p_btif->base;
#if defined(CONFIG_MTK_CLKMGR)
	if (clock_is_on(MTK_BTIF_CG_BIT) == 0) {
		BTIF_ERR_FUNC("%s: clock is off before send wakeup signal!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
/*write 0  to BTIF_WAK to pull ap_wakeup_consyss low */
	BTIF_CLR_BIT(BTIF_WAK(base), BTIF_WAK_BIT);

/*wait for a period for longer than 1/32k period, here we use 40us*/
	set_current_state(TASK_UNINTERRUPTIBLE);
	usleep_range(128, 160);
/*
 * according to linux/documentation/timers/timers-how-to, we choose usleep_range
 * SLEEPING FOR ~USECS OR SMALL MSECS ( 10us - 20ms):      * Use usleep_range
 */
/*write 1 to pull ap_wakeup_consyss high*/
	BTIF_SET_BIT(BTIF_WAK(base), BTIF_WAK_BIT);
	i_ret = 0;
	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_dump_reg
* DESCRIPTION
*  dump BTIF module's information when needed
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* flag        [IN]        register id flag
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_dump_reg(P_MTK_BTIF_INFO_STR p_btif, ENUM_BTIF_REG_ID flag)
{
/*Chaozhong: To be implement*/
	int i_ret = -1;
	int idx = 0;
	/*unsigned long irq_flag = 0;*/
	unsigned long base = p_btif->base;
	unsigned char reg_map[0xE0 / 4] = { 0 };
	unsigned int lsr = 0x0;
	unsigned int dma_en = 0;

	/*spin_lock_irqsave(&(g_clk_cg_spinlock), irq_flag);*/
#if defined(CONFIG_MTK_CLKMGR)
	if (clock_is_on(MTK_BTIF_CG_BIT) == 0) {
		/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/
		BTIF_ERR_FUNC("%s: clock is off, this should never happen!!!\n",
			      __FILE__);
		return i_ret;
	}
#endif
	lsr = BTIF_READ32(BTIF_LSR(base));
	dma_en = BTIF_READ32(BTIF_DMA_EN(base));
	/*
	 * here we omit 1st register which is THR/RBR register to avoid
	 * Rx data read by this debug information accidently
	 */
	for (idx = 1; idx < sizeof(reg_map); idx++)
		reg_map[idx] = BTIF_READ8(p_btif->base + (4 * idx));
	/*spin_unlock_irqrestore(&(g_clk_cg_spinlock), irq_flag);*/
	BTIF_INFO_FUNC("BTIF's clock is on\n");
	BTIF_INFO_FUNC("base address: 0x%lx\n", base);
	switch (flag) {
	case REG_BTIF_ALL:
#if 0
		BTIF_INFO_FUNC("BTIF_IER:0x%x\n", BTIF_READ32(BTIF_IER(base)));
		BTIF_INFO_FUNC("BTIF_IIR:0x%x\n", BTIF_READ32(BTIF_IIR(base)));
		BTIF_INFO_FUNC("BTIF_FAKELCR:0x%x\n",
			       BTIF_READ32(BTIF_FAKELCR(base)));
		BTIF_INFO_FUNC("BTIF_LSR:0x%x\n", BTIF_READ32(BTIF_LSR(base)));
		BTIF_INFO_FUNC("BTIF_SLEEP_EN:0x%x\n",
			       BTIF_READ32(BTIF_SLEEP_EN(base)));
		BTIF_INFO_FUNC("BTIF_DMA_EN:0x%x\n",
			       BTIF_READ32(BTIF_DMA_EN(base)));
		BTIF_INFO_FUNC("BTIF_RTOCNT:0x%x\n",
			       BTIF_READ32(BTIF_RTOCNT(base)));
		BTIF_INFO_FUNC("BTIF_TRI_LVL:0x%x\n",
			       BTIF_READ32(BTIF_TRI_LVL(base)));
		BTIF_INFO_FUNC("BTIF_WAT_TIME:0x%x\n",
			       BTIF_READ32(BTIF_WAT_TIME(base)));
		BTIF_INFO_FUNC("BTIF_HANDSHAKE:0x%x\n",
			       BTIF_READ32(BTIF_HANDSHAKE(base)));
#endif
		btif_dump_array("BTIF register", reg_map, sizeof(reg_map));
		break;
	default:
		break;
	}

	BTIF_INFO_FUNC("Tx DMA %s\n",
		       (dma_en & BTIF_DMA_EN_TX) ? "enabled" : "disabled");
	BTIF_INFO_FUNC("Rx DMA %s\n",
		       (dma_en & BTIF_DMA_EN_RX) ? "enabled" : "disabled");

	BTIF_INFO_FUNC("Rx data is %s\n",
		       (lsr & BTIF_LSR_DR_BIT) ? "not empty" : "empty");
	BTIF_INFO_FUNC("Tx data is %s\n",
		       (lsr & BTIF_LSR_TEMT_BIT) ? "empty" : "not empty");

	return i_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_is_tx_complete
* DESCRIPTION
*  get tx complete flag
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
*  true means tx complete, false means tx in process
*****************************************************************************/
bool hal_btif_is_tx_complete(P_MTK_BTIF_INFO_STR p_btif)
{
/*Chaozhong: To be implement*/
	bool b_ret = false;
	unsigned int lsr = 0;
	unsigned long flags = 0;
	unsigned long base = p_btif->base;
	unsigned int tx_empty = 0;
	unsigned int rx_dr = 0;
	unsigned int tx_irq_disable = 0;

/*
 * 3 conditions allow clock to be disable
 * 1. if TEMT is set or not
 * 2. if DR is set or not
 * 3. Tx IRQ is disabled or not
 */
	lsr = BTIF_READ32(BTIF_LSR(base));
	tx_empty = lsr & BTIF_LSR_TEMT_BIT;
	rx_dr = lsr & BTIF_LSR_DR_BIT;
	tx_irq_disable = BTIF_READ32(BTIF_IER(base)) & BTIF_IER_TXEEN;

	b_ret =
	    (tx_empty && (tx_irq_disable == 0) && (rx_dr == 0)) ? true : false;
	if (!b_ret) {
		BTIF_DBG_FUNC
		    ("BTIF flag, tx_empty:%d, rx_dr:%d, tx_irq_disable:%d\n",
		     tx_empty, rx_dr, tx_irq_disable);
	}
#if NEW_TX_HANDLING_SUPPORT
	spin_lock_irqsave(&(p_btif->tx_fifo_spinlock), flags);
/*clear Tx enable flag if necessary*/
	if (!(kfifo_is_empty(p_btif->p_tx_fifo))) {
		BTIF_DBG_FUNC("BTIF tx FIFO is not empty\n");
		b_ret = false;
	}
	spin_unlock_irqrestore(&(p_btif->tx_fifo_spinlock), flags);
#endif
	return b_ret;
}

/*****************************************************************************
* FUNCTION
*  hal_btif_is_tx_allow
* DESCRIPTION
*  whether tx is allowed
* PARAMETERS
* p_base   [IN]        BTIF module's base address
* RETURNS
* true if tx operation is allowed; false if tx is not allowed
*****************************************************************************/
bool hal_btif_is_tx_allow(P_MTK_BTIF_INFO_STR p_btif)
{
#define MIN_TX_MB ((26 * 1000000 / 13) / 1000000)
#define AVE_TX_MB ((26 * 1000000 / 8) / 1000000)

/*Chaozhong: To be implement*/
	bool b_ret = false;

#if NEW_TX_HANDLING_SUPPORT
	unsigned long flags = 0;

	spin_lock_irqsave(&(p_btif->tx_fifo_spinlock), flags);
/*clear Tx enable flag if necessary*/
	if (kfifo_is_full(p_btif->p_tx_fifo)) {
		BTIF_WARN_FUNC("BTIF tx FIFO is full\n");
		b_ret = false;
	} else {
		b_ret = true;
	}
	spin_unlock_irqrestore(&(p_btif->tx_fifo_spinlock), flags);
#else
	unsigned int lsr = 0;
	unsigned long base = p_btif->base;
	unsigned int wait_us = (BTIF_TX_FIFO_SIZE - BTIF_TX_FIFO_THRE) / MIN_TX_MB; /*only ava length */

/*read LSR and check THER or TEMT, either one is 1 means can accept tx data*/
	lsr = BTIF_READ32(BTIF_LSR(base));

	if (!(lsr & (BTIF_LSR_TEMT_BIT | BTIF_LSR_THRE_BIT))) {
		BTIF_DBG_FUNC("wait for %d ~ %d us\n", wait_us, 3 * wait_us);
/* usleep_range(wait_us, 3 * 10 * wait_us); */
		usleep_range(wait_us, 3 * wait_us);
	}
	lsr = BTIF_READ32(BTIF_LSR(base));
	b_ret = (lsr & (BTIF_LSR_TEMT_BIT | BTIF_LSR_THRE_BIT)) ? true : false;
	if (!b_ret)
		BTIF_DBG_FUNC(" tx is not allowed for the moment\n");
	else
		BTIF_DBG_FUNC(" tx is allowed\n");
#endif
	return b_ret;
}

#if !(NEW_TX_HANDLING_SUPPORT)

static bool _btif_is_tx_allow(P_MTK_BTIF_INFO_STR p_btif)
{
/*Chaozhong: To be implement*/
	bool b_ret = false;
	unsigned long base = p_btif->base;
	unsigned int lsr = 0;

/*read LSR and check THER or TEMT, either one is 1 means can accept tx data*/
	lsr = BTIF_READ32(BTIF_LSR(base));
	b_ret = (lsr & (BTIF_LSR_TEMT_BIT | BTIF_LSR_THRE_BIT)) ? true : false;
	return b_ret;
}
#endif

int hal_btif_pm_ops(P_MTK_BTIF_INFO_STR p_btif_info, MTK_BTIF_PM_OPID opid)
{
	int i_ret = -1;

	BTIF_DBG_FUNC("op id: %d\n", opid);
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
			P_MTK_BTIF_IRQ_STR p_irq = p_btif_info->p_irq;

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
		break;
	default:
		i_ret = ERR_INVALID_PAR;
		break;
	}

	return i_ret;
}
void mtk_btif_read_cpu_sw_rst_debug_plat(void)
{
#define CONSYS_AP2CONN_WAKEUP_OFFSET	0x00000064
	BTIF_WARN_FUNC("+CONSYS_AP2CONN_WAKEUP_OFFSET(0x%x)\n",
			   BTIF_READ32(mtk_btif.base + CONSYS_AP2CONN_WAKEUP_OFFSET));
}

