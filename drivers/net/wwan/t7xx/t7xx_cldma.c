// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/types.h>

#include "t7xx_cldma.h"

#define ADDR_SIZE	8

void t7xx_cldma_clear_ip_busy(struct t7xx_cldma_hw *hw_info)
{
	u32 val;

	val = ioread32(hw_info->ap_pdn_base + REG_CLDMA_IP_BUSY);
	val |= IP_BUSY_WAKEUP;
	iowrite32(val, hw_info->ap_pdn_base + REG_CLDMA_IP_BUSY);
}

/**
 * t7xx_cldma_hw_restore() - Restore CLDMA HW registers.
 * @hw_info: Pointer to struct t7xx_cldma_hw.
 *
 * Restore HW after resume. Writes uplink configuration for CLDMA HW.
 */
void t7xx_cldma_hw_restore(struct t7xx_cldma_hw *hw_info)
{
	u32 ul_cfg;

	ul_cfg = ioread32(hw_info->ap_pdn_base + REG_CLDMA_UL_CFG);
	ul_cfg &= ~UL_CFG_BIT_MODE_MASK;

	if (hw_info->hw_mode == MODE_BIT_64)
		ul_cfg |= UL_CFG_BIT_MODE_64;
	else if (hw_info->hw_mode == MODE_BIT_40)
		ul_cfg |= UL_CFG_BIT_MODE_40;
	else if (hw_info->hw_mode == MODE_BIT_36)
		ul_cfg |= UL_CFG_BIT_MODE_36;

	iowrite32(ul_cfg, hw_info->ap_pdn_base + REG_CLDMA_UL_CFG);
	/* Disable TX and RX invalid address check */
	iowrite32(UL_MEM_CHECK_DIS, hw_info->ap_pdn_base + REG_CLDMA_UL_MEM);
	iowrite32(DL_MEM_CHECK_DIS, hw_info->ap_pdn_base + REG_CLDMA_DL_MEM);
}

void t7xx_cldma_hw_start_queue(struct t7xx_cldma_hw *hw_info, unsigned int qno,
			       enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_pdn_base + REG_CLDMA_DL_START_CMD :
				hw_info->ap_pdn_base + REG_CLDMA_UL_START_CMD;
	val = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	iowrite32(val, reg);
}

void t7xx_cldma_hw_start(struct t7xx_cldma_hw *hw_info)
{
	/* Enable the TX & RX interrupts */
	iowrite32(TXRX_STATUS_BITMASK, hw_info->ap_pdn_base + REG_CLDMA_L2TIMCR0);
	iowrite32(TXRX_STATUS_BITMASK, hw_info->ap_ao_base + REG_CLDMA_L2RIMCR0);
	/* Enable the empty queue interrupt */
	iowrite32(EMPTY_STATUS_BITMASK, hw_info->ap_pdn_base + REG_CLDMA_L2TIMCR0);
	iowrite32(EMPTY_STATUS_BITMASK, hw_info->ap_ao_base + REG_CLDMA_L2RIMCR0);
}

void t7xx_cldma_hw_reset(void __iomem *ao_base)
{
	u32 val;

	val = ioread32(ao_base + REG_INFRA_RST2_SET);
	val |= RST2_PMIC_SW_RST_SET;
	iowrite32(val, ao_base + REG_INFRA_RST2_SET);
	val = ioread32(ao_base + REG_INFRA_RST4_SET);
	val |= RST4_CLDMA1_SW_RST_SET;
	iowrite32(val, ao_base + REG_INFRA_RST4_SET);
	udelay(1);

	val = ioread32(ao_base + REG_INFRA_RST4_CLR);
	val |= RST4_CLDMA1_SW_RST_CLR;
	iowrite32(val, ao_base + REG_INFRA_RST4_CLR);
	val = ioread32(ao_base + REG_INFRA_RST2_CLR);
	val |= RST2_PMIC_SW_RST_CLR;
	iowrite32(val, ao_base + REG_INFRA_RST2_CLR);
}

bool t7xx_cldma_tx_addr_is_set(struct t7xx_cldma_hw *hw_info, unsigned int qno)
{
	u32 offset = REG_CLDMA_UL_START_ADDRL_0 + qno * ADDR_SIZE;

	return ioread64_lo_hi(hw_info->ap_pdn_base + offset);
}

void t7xx_cldma_hw_set_start_addr(struct t7xx_cldma_hw *hw_info, unsigned int qno, u64 address,
				  enum mtk_txrx tx_rx)
{
	u32 offset = qno * ADDR_SIZE;
	void __iomem *reg;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_DL_START_ADDRL_0 :
				hw_info->ap_pdn_base + REG_CLDMA_UL_START_ADDRL_0;
	iowrite64_lo_hi(address, reg + offset);
}

void t7xx_cldma_hw_resume_queue(struct t7xx_cldma_hw *hw_info, unsigned int qno,
				enum mtk_txrx tx_rx)
{
	void __iomem *base = hw_info->ap_pdn_base;

	if (tx_rx == MTK_RX)
		iowrite32(BIT(qno), base + REG_CLDMA_DL_RESUME_CMD);
	else
		iowrite32(BIT(qno), base + REG_CLDMA_UL_RESUME_CMD);
}

unsigned int t7xx_cldma_hw_queue_status(struct t7xx_cldma_hw *hw_info, unsigned int qno,
					enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 mask, val;

	mask = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_DL_STATUS :
				hw_info->ap_pdn_base + REG_CLDMA_UL_STATUS;
	val = ioread32(reg);

	return val & mask;
}

void t7xx_cldma_hw_tx_done(struct t7xx_cldma_hw *hw_info, unsigned int bitmask)
{
	unsigned int ch_id;

	ch_id = ioread32(hw_info->ap_pdn_base + REG_CLDMA_L2TISAR0);
	ch_id &= bitmask;
	/* Clear the ch IDs in the TX interrupt status register */
	iowrite32(ch_id, hw_info->ap_pdn_base + REG_CLDMA_L2TISAR0);
	ioread32(hw_info->ap_pdn_base + REG_CLDMA_L2TISAR0);
}

void t7xx_cldma_hw_rx_done(struct t7xx_cldma_hw *hw_info, unsigned int bitmask)
{
	unsigned int ch_id;

	ch_id = ioread32(hw_info->ap_pdn_base + REG_CLDMA_L2RISAR0);
	ch_id &= bitmask;
	/* Clear the ch IDs in the RX interrupt status register */
	iowrite32(ch_id, hw_info->ap_pdn_base + REG_CLDMA_L2RISAR0);
	ioread32(hw_info->ap_pdn_base + REG_CLDMA_L2RISAR0);
}

unsigned int t7xx_cldma_hw_int_status(struct t7xx_cldma_hw *hw_info, unsigned int bitmask,
				      enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_pdn_base + REG_CLDMA_L2RISAR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TISAR0;
	val = ioread32(reg);
	return val & bitmask;
}

void t7xx_cldma_hw_irq_dis_txrx(struct t7xx_cldma_hw *hw_info, unsigned int qno,
				enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_L2RIMSR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TIMSR0;
	val = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	iowrite32(val, reg);
}

void t7xx_cldma_hw_irq_dis_eq(struct t7xx_cldma_hw *hw_info, unsigned int qno, enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_L2RIMSR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TIMSR0;
	val = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	iowrite32(val << EQ_STA_BIT_OFFSET, reg);
}

void t7xx_cldma_hw_irq_en_txrx(struct t7xx_cldma_hw *hw_info, unsigned int qno,
			       enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_L2RIMCR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TIMCR0;
	val = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	iowrite32(val, reg);
}

void t7xx_cldma_hw_irq_en_eq(struct t7xx_cldma_hw *hw_info, unsigned int qno, enum mtk_txrx tx_rx)
{
	void __iomem *reg;
	u32 val;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_L2RIMCR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TIMCR0;
	val = qno == CLDMA_ALL_Q ? CLDMA_ALL_Q : BIT(qno);
	iowrite32(val << EQ_STA_BIT_OFFSET, reg);
}

/**
 * t7xx_cldma_hw_init() - Initialize CLDMA HW.
 * @hw_info: Pointer to struct t7xx_cldma_hw.
 *
 * Write uplink and downlink configuration to CLDMA HW.
 */
void t7xx_cldma_hw_init(struct t7xx_cldma_hw *hw_info)
{
	u32 ul_cfg, dl_cfg;

	ul_cfg = ioread32(hw_info->ap_pdn_base + REG_CLDMA_UL_CFG);
	dl_cfg = ioread32(hw_info->ap_ao_base + REG_CLDMA_DL_CFG);
	/* Configure the DRAM address mode */
	ul_cfg &= ~UL_CFG_BIT_MODE_MASK;
	dl_cfg &= ~DL_CFG_BIT_MODE_MASK;

	if (hw_info->hw_mode == MODE_BIT_64) {
		ul_cfg |= UL_CFG_BIT_MODE_64;
		dl_cfg |= DL_CFG_BIT_MODE_64;
	} else if (hw_info->hw_mode == MODE_BIT_40) {
		ul_cfg |= UL_CFG_BIT_MODE_40;
		dl_cfg |= DL_CFG_BIT_MODE_40;
	} else if (hw_info->hw_mode == MODE_BIT_36) {
		ul_cfg |= UL_CFG_BIT_MODE_36;
		dl_cfg |= DL_CFG_BIT_MODE_36;
	}

	iowrite32(ul_cfg, hw_info->ap_pdn_base + REG_CLDMA_UL_CFG);
	dl_cfg |= DL_CFG_UP_HW_LAST;
	iowrite32(dl_cfg, hw_info->ap_ao_base + REG_CLDMA_DL_CFG);
	iowrite32(0, hw_info->ap_ao_base + REG_CLDMA_INT_MASK);
	iowrite32(BUSY_MASK_MD, hw_info->ap_ao_base + REG_CLDMA_BUSY_MASK);
	iowrite32(UL_MEM_CHECK_DIS, hw_info->ap_pdn_base + REG_CLDMA_UL_MEM);
	iowrite32(DL_MEM_CHECK_DIS, hw_info->ap_pdn_base + REG_CLDMA_DL_MEM);
}

void t7xx_cldma_hw_stop_all_qs(struct t7xx_cldma_hw *hw_info, enum mtk_txrx tx_rx)
{
	void __iomem *reg;

	reg = tx_rx == MTK_RX ? hw_info->ap_pdn_base + REG_CLDMA_DL_STOP_CMD :
				hw_info->ap_pdn_base + REG_CLDMA_UL_STOP_CMD;
	iowrite32(CLDMA_ALL_Q, reg);
}

void t7xx_cldma_hw_stop(struct t7xx_cldma_hw *hw_info, enum mtk_txrx tx_rx)
{
	void __iomem *reg;

	reg = tx_rx == MTK_RX ? hw_info->ap_ao_base + REG_CLDMA_L2RIMSR0 :
				hw_info->ap_pdn_base + REG_CLDMA_L2TIMSR0;
	iowrite32(TXRX_STATUS_BITMASK, reg);
	iowrite32(EMPTY_STATUS_BITMASK, reg);
}
