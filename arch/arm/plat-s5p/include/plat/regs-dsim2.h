/* linux/arch/arm/plat-s5p/include/plat/regs-dsim.h
 *
 * Register definition file for Samsung MIPI-DSIM driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * InKi Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _REGS_DSIM_H
#define _REGS_DSIM_H

#define S5P_DSIM_STATUS		(0x0)	/* Status register */
#define S5P_DSIM_SWRST		(0x4)	/* Software reset register */
#define S5P_DSIM_CLKCTRL	(0x8)	/* Clock control register */
#define S5P_DSIM_TIMEOUT	(0xc)	/* Time out register */
#define S5P_DSIM_CONFIG		(0x10)	/* Configuration register */
#define S5P_DSIM_ESCMODE	(0x14)	/* Escape mode register */

/* Main display image resolution register */
#define S5P_DSIM_MDRESOL	(0x18)
#define S5P_DSIM_MVPORCH	(0x1c)	/* Main display Vporch register */
#define S5P_DSIM_MHPORCH	(0x20)	/* Main display Hporch register */
#define S5P_DSIM_MSYNC		(0x24)	/* Main display sync area register */

/* Sub display image resolution register */
#define S5P_DSIM_SDRESOL	(0x28)
#define S5P_DSIM_INTSRC		(0x2c)	/* Interrupt source register */
#define S5P_DSIM_INTMSK		(0x30)	/* Interrupt mask register */
#define S5P_DSIM_PKTHDR		(0x34)	/* Packet Header FIFO register */
#define S5P_DSIM_PAYLOAD	(0x38)	/* Payload FIFO register */
#define S5P_DSIM_RXFIFO		(0x3c)	/* Read FIFO register */
#define S5P_DSIM_FIFOTHLD	(0x40)	/* FIFO threshold level register */
#define S5P_DSIM_FIFOCTRL	(0x44)	/* FIFO status and control register */

/* FIFO memory AC characteristic register */
#define S5P_DSIM_PLLCTRL	(0x4c)	/* PLL control register */
#define S5P_DSIM_PLLTMR		(0x50)	/* PLL timer register */
#define S5P_DSIM_PHYACCHR	(0x54)	/* D-PHY AC characteristic register */
#define S5P_DSIM_PHYACCHR1	(0x58)	/* D-PHY AC characteristic register1 */

/* DSIM_STATUS */
#define DSIM_STOP_STATE_DAT(x)	(((x) & 0xf) << 0)
#define DSIM_STOP_STATE_CLK	(1 << 8)
#define DSIM_TX_READY_HS_CLK	(1 << 10)

/* DSIM_SWRST */
#define DSIM_FUNCRST		(1 << 16)
#define DSIM_SWRST		(1 << 0)

/* S5P_DSIM_TIMEOUT */
#define DSIM_LPDR_TOUT_SHIFT	(0)
#define DSIM_BTA_TOUT_SHIFT	(16)

/* S5P_DSIM_CLKCTRL */
#define DSIM_LANE_ESC_CLKEN_SHIFT	(19)
#define DSIM_BYTE_CLKEN_SHIFT		(24)
#define DSIM_BYTE_CLK_SRC_SHIFT		(25)
#define DSIM_PLL_BYPASS_SHIFT		(27)
#define DSIM_ESC_CLKEN_SHIFT		(28)
#define DSIM_TX_REQUEST_HSCLK_SHIFT	(31)
#define DSIM_LANE_ESC_CLKEN(x)		(((x) & 0x1f) << \
						DSIM_LANE_ESC_CLKEN_SHIFT)
#define DSIM_BYTE_CLK_ENABLE		(1 << DSIM_BYTE_CLKEN_SHIFT)
#define DSIM_BYTE_CLK_DISABLE		(0 << DSIM_BYTE_CLKEN_SHIFT)
#define DSIM_PLL_BYPASS_EXTERNAL	(1 << DSIM_PLL_BYPASS_SHIFT)
#define DSIM_ESC_CLKEN_ENABLE		(1 << DSIM_ESC_CLKEN_SHIFT)
#define DSIM_ESC_CLKEN_DISABLE		(0 << DSIM_ESC_CLKEN_SHIFT)

/* S5P_DSIM_CONFIG */
#define DSIM_NUM_OF_DATALANE_SHIFT	(5)
#define DSIM_HSA_MODE_SHIFT		(20)
#define DSIM_HBP_MODE_SHIFT		(21)
#define DSIM_HFP_MODE_SHIFT		(22)
#define DSIM_HSE_MODE_SHIFT		(23)
#define DSIM_AUTO_MODE_SHIFT		(24)
#define DSIM_LANE_ENx(x)		(((x) & 0x1f) << 0)

#define DSIM_NUM_OF_DATA_LANE(x)	((x) << DSIM_NUM_OF_DATALANE_SHIFT)

/* S5P_DSIM_ESCMODE */
#define DSIM_TX_LPDT_SHIFT		(6)
#define DSIM_CMD_LPDT_SHIFT		(7)
#define DSIM_TX_LPDT_LP			(1 << DSIM_TX_LPDT_SHIFT)
#define DSIM_CMD_LPDT_LP		(1 << DSIM_CMD_LPDT_SHIFT)
#define DSIM_STOP_STATE_CNT_SHIFT	(21)
#define DSIM_FORCE_STOP_STATE_SHIFT	(20)

/* S5P_DSIM_MDRESOL */
#define DSIM_MAIN_STAND_BY		(1 << 31)
#define DSIM_MAIN_VRESOL(x)		(((x) & 0x7ff) << 16)
#define DSIM_MAIN_HRESOL(x)		(((x) & 0X7ff) << 0)

/* S5P_DSIM_MVPORCH */
#define DSIM_CMD_ALLOW_SHIFT		(28)
#define DSIM_STABLE_VFP_SHIFT		(16)
#define DSIM_MAIN_VBP_SHIFT		(0)
#define DSIM_CMD_ALLOW_MASK		(0xf << DSIM_CMD_ALLOW_SHIFT)
#define DSIM_STABLE_VFP_MASK		(0x7ff << DSIM_STABLE_VFP_SHIFT)
#define DSIM_MAIN_VBP_MASK		(0x7ff << DSIM_MAIN_VBP_SHIFT)

/* S5P_DSIM_MHPORCH */
#define DSIM_MAIN_HFP_SHIFT		(16)
#define DSIM_MAIN_HBP_SHIFT		(0)
#define DSIM_MAIN_HFP_MASK		((0xffff) << DSIM_MAIN_HFP_SHIFT)
#define DSIM_MAIN_HBP_MASK		((0xffff) << DSIM_MAIN_HBP_SHIFT)

/* S5P_DSIM_MSYNC */
#define DSIM_MAIN_VSA_SHIFT		(22)
#define DSIM_MAIN_HSA_SHIFT		(0)
#define DSIM_MAIN_VSA_MASK		((0x3ff) << DSIM_MAIN_VSA_SHIFT)
#define DSIM_MAIN_HSA_MASK		((0xffff) << DSIM_MAIN_HSA_SHIFT)

/* S5P_DSIM_SDRESOL */
#define DSIM_SUB_STANDY_SHIFT		(31)
#define DSIM_SUB_VRESOL_SHIFT		(16)
#define DSIM_SUB_HRESOL_SHIFT		(0)
#define DSIM_SUB_STANDY_MASK		((0x1) << DSIM_SUB_STANDY_SHIFT)
#define DSIM_SUB_VRESOL_MASK		((0x7ff) << DSIM_SUB_VRESOL_SHIFT)
#define DSIM_SUB_HRESOL_MASK		((0x7ff) << DSIM_SUB_HRESOL_SHIFT)

/* S5P_DSIM_INTSRC */
#define INTSRC_PLL_STABLE		(1 << 31)
#define INTSRC_SW_RST_RELEASE		(1 << 30)
#define INTSRC_SFR_FIFO_EMPTY		(1 << 29)
#define INTSRC_FRAME_DONE		(1 << 24)
#define INTSRC_RX_DATA_DONE		(1 << 18)

/* S5P_DSIM_INTMSK */
#define INTMSK_FIFO_EMPTY              (1 << 29)
#define INTMSK_BTA                     (1 << 25)
#define INTMSK_FRAME_DONE              (1 << 24)
#define INTMSK_RX_TIMEOUT              (1 << 21)
#define INTMSK_BTA_TIMEOUT             (1 << 20)
#define INTMSK_RX_DONE                 (1 << 18)
#define INTMSK_RX_TE                   (1 << 17)
#define INTMSK_RX_ACK                  (1 << 16)
#define INTMSK_RX_ECC_ERR              (1 << 15)
#define INTMSK_RX_CRC_ERR              (1 << 14)

/* S5P_DSIM_FIFOCTRL */
#define SFR_HEADER_EMPTY		(1 << 22)

/* S5P_DSIM_PHYACCHR */
#define DSIM_AFC_CTL(x)			(((x) & 0x7) << 5)

/* S5P_DSIM_PLLCTRL */
#define DSIM_PLL_EN_SHIFT		(23)
#define DSIM_FREQ_BAND_SHIFT		(24)

#endif /* _REGS_DSIM_H */
