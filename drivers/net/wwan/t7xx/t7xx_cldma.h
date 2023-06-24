/* SPDX-License-Identifier: GPL-2.0-only
 *
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
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_CLDMA_H__
#define __T7XX_CLDMA_H__

#include <linux/bits.h>
#include <linux/types.h>

#define CLDMA_TXQ_NUM			8
#define CLDMA_RXQ_NUM			8
#define CLDMA_ALL_Q			GENMASK(7, 0)

/* Interrupt status bits */
#define EMPTY_STATUS_BITMASK		GENMASK(15, 8)
#define TXRX_STATUS_BITMASK		GENMASK(7, 0)
#define EQ_STA_BIT_OFFSET		8
#define L2_INT_BIT_COUNT		16
#define EQ_STA_BIT(index)		(BIT((index) + EQ_STA_BIT_OFFSET) & EMPTY_STATUS_BITMASK)

#define TQ_ERR_INT_BITMASK		GENMASK(23, 16)
#define TQ_ACTIVE_START_ERR_INT_BITMASK	GENMASK(31, 24)

#define RQ_ERR_INT_BITMASK		GENMASK(23, 16)
#define RQ_ACTIVE_START_ERR_INT_BITMASK	GENMASK(31, 24)

#define CLDMA0_AO_BASE			0x10049000
#define CLDMA0_PD_BASE			0x1021d000
#define CLDMA1_AO_BASE			0x1004b000
#define CLDMA1_PD_BASE			0x1021f000

#define CLDMA_R_AO_BASE			0x10023000
#define CLDMA_R_PD_BASE			0x1023d000

/* CLDMA TX */
#define REG_CLDMA_UL_START_ADDRL_0	0x0004
#define REG_CLDMA_UL_START_ADDRH_0	0x0008
#define REG_CLDMA_UL_CURRENT_ADDRL_0	0x0044
#define REG_CLDMA_UL_CURRENT_ADDRH_0	0x0048
#define REG_CLDMA_UL_STATUS		0x0084
#define REG_CLDMA_UL_START_CMD		0x0088
#define REG_CLDMA_UL_RESUME_CMD		0x008c
#define REG_CLDMA_UL_STOP_CMD		0x0090
#define REG_CLDMA_UL_ERROR		0x0094
#define REG_CLDMA_UL_CFG		0x0098
#define UL_CFG_BIT_MODE_36		BIT(5)
#define UL_CFG_BIT_MODE_40		BIT(6)
#define UL_CFG_BIT_MODE_64		BIT(7)
#define UL_CFG_BIT_MODE_MASK		GENMASK(7, 5)

#define REG_CLDMA_UL_MEM		0x009c
#define UL_MEM_CHECK_DIS		BIT(0)

/* CLDMA RX */
#define REG_CLDMA_DL_START_CMD		0x05bc
#define REG_CLDMA_DL_RESUME_CMD		0x05c0
#define REG_CLDMA_DL_STOP_CMD		0x05c4
#define REG_CLDMA_DL_MEM		0x0508
#define DL_MEM_CHECK_DIS		BIT(0)

#define REG_CLDMA_DL_CFG		0x0404
#define DL_CFG_UP_HW_LAST		BIT(2)
#define DL_CFG_BIT_MODE_36		BIT(10)
#define DL_CFG_BIT_MODE_40		BIT(11)
#define DL_CFG_BIT_MODE_64		BIT(12)
#define DL_CFG_BIT_MODE_MASK		GENMASK(12, 10)

#define REG_CLDMA_DL_START_ADDRL_0	0x0478
#define REG_CLDMA_DL_START_ADDRH_0	0x047c
#define REG_CLDMA_DL_CURRENT_ADDRL_0	0x04b8
#define REG_CLDMA_DL_CURRENT_ADDRH_0	0x04bc
#define REG_CLDMA_DL_STATUS		0x04f8

/* CLDMA MISC */
#define REG_CLDMA_L2TISAR0		0x0810
#define REG_CLDMA_L2TISAR1		0x0814
#define REG_CLDMA_L2TIMR0		0x0818
#define REG_CLDMA_L2TIMR1		0x081c
#define REG_CLDMA_L2TIMCR0		0x0820
#define REG_CLDMA_L2TIMCR1		0x0824
#define REG_CLDMA_L2TIMSR0		0x0828
#define REG_CLDMA_L2TIMSR1		0x082c
#define REG_CLDMA_L3TISAR0		0x0830
#define REG_CLDMA_L3TISAR1		0x0834
#define REG_CLDMA_L2RISAR0		0x0850
#define REG_CLDMA_L2RISAR1		0x0854
#define REG_CLDMA_L3RISAR0		0x0870
#define REG_CLDMA_L3RISAR1		0x0874
#define REG_CLDMA_IP_BUSY		0x08b4
#define IP_BUSY_WAKEUP			BIT(0)
#define CLDMA_L2TISAR0_ALL_INT_MASK	GENMASK(15, 0)
#define CLDMA_L2RISAR0_ALL_INT_MASK	GENMASK(15, 0)

/* CLDMA MISC */
#define REG_CLDMA_L2RIMR0		0x0858
#define REG_CLDMA_L2RIMR1		0x085c
#define REG_CLDMA_L2RIMCR0		0x0860
#define REG_CLDMA_L2RIMCR1		0x0864
#define REG_CLDMA_L2RIMSR0		0x0868
#define REG_CLDMA_L2RIMSR1		0x086c
#define REG_CLDMA_BUSY_MASK		0x0954
#define BUSY_MASK_PCIE			BIT(0)
#define BUSY_MASK_AP			BIT(1)
#define BUSY_MASK_MD			BIT(2)

#define REG_CLDMA_INT_MASK		0x0960

/* CLDMA RESET */
#define REG_INFRA_RST4_SET		0x0730
#define RST4_CLDMA1_SW_RST_SET		BIT(20)

#define REG_INFRA_RST4_CLR		0x0734
#define RST4_CLDMA1_SW_RST_CLR		BIT(20)

#define REG_INFRA_RST2_SET		0x0140
#define RST2_PMIC_SW_RST_SET		BIT(18)

#define REG_INFRA_RST2_CLR		0x0144
#define RST2_PMIC_SW_RST_CLR		BIT(18)

enum mtk_txrx {
	MTK_TX,
	MTK_RX,
};

enum t7xx_hw_mode {
	MODE_BIT_32,
	MODE_BIT_36,
	MODE_BIT_40,
	MODE_BIT_64,
};

struct t7xx_cldma_hw {
	enum t7xx_hw_mode		hw_mode;
	void __iomem			*ap_ao_base;
	void __iomem			*ap_pdn_base;
	u32				phy_interrupt_id;
};

void t7xx_cldma_hw_irq_dis_txrx(struct t7xx_cldma_hw *hw_info, unsigned int qno,
				enum mtk_txrx tx_rx);
void t7xx_cldma_hw_irq_dis_eq(struct t7xx_cldma_hw *hw_info, unsigned int qno,
			      enum mtk_txrx tx_rx);
void t7xx_cldma_hw_irq_en_txrx(struct t7xx_cldma_hw *hw_info, unsigned int qno,
			       enum mtk_txrx tx_rx);
void t7xx_cldma_hw_irq_en_eq(struct t7xx_cldma_hw *hw_info, unsigned int qno, enum mtk_txrx tx_rx);
unsigned int t7xx_cldma_hw_queue_status(struct t7xx_cldma_hw *hw_info, unsigned int qno,
					enum mtk_txrx tx_rx);
void t7xx_cldma_hw_init(struct t7xx_cldma_hw *hw_info);
void t7xx_cldma_hw_resume_queue(struct t7xx_cldma_hw *hw_info, unsigned int qno,
				enum mtk_txrx tx_rx);
void t7xx_cldma_hw_start(struct t7xx_cldma_hw *hw_info);
void t7xx_cldma_hw_start_queue(struct t7xx_cldma_hw *hw_info, unsigned int qno,
			       enum mtk_txrx tx_rx);
void t7xx_cldma_hw_tx_done(struct t7xx_cldma_hw *hw_info, unsigned int bitmask);
void t7xx_cldma_hw_rx_done(struct t7xx_cldma_hw *hw_info, unsigned int bitmask);
void t7xx_cldma_hw_stop_all_qs(struct t7xx_cldma_hw *hw_info, enum mtk_txrx tx_rx);
void t7xx_cldma_hw_set_start_addr(struct t7xx_cldma_hw *hw_info,
				  unsigned int qno, u64 address, enum mtk_txrx tx_rx);
void t7xx_cldma_hw_reset(void __iomem *ao_base);
void t7xx_cldma_hw_stop(struct t7xx_cldma_hw *hw_info, enum mtk_txrx tx_rx);
unsigned int t7xx_cldma_hw_int_status(struct t7xx_cldma_hw *hw_info, unsigned int bitmask,
				      enum mtk_txrx tx_rx);
void t7xx_cldma_hw_restore(struct t7xx_cldma_hw *hw_info);
void t7xx_cldma_clear_ip_busy(struct t7xx_cldma_hw *hw_info);
bool t7xx_cldma_tx_addr_is_set(struct t7xx_cldma_hw *hw_info, unsigned int qno);
#endif
