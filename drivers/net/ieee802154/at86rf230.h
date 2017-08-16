/*
 * AT86RF230/RF231 driver
 *
 * Copyright (C) 2009-2012 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 * Alexander Smirnov <alex.bluesman.smirnov@gmail.com>
 */

#ifndef _AT86RF230_H
#define _AT86RF230_H

#define RG_TRX_STATUS	(0x01)
#define SR_TRX_STATUS		0x01, 0x1f, 0
#define SR_RESERVED_01_3	0x01, 0x20, 5
#define SR_CCA_STATUS		0x01, 0x40, 6
#define SR_CCA_DONE		0x01, 0x80, 7
#define RG_TRX_STATE	(0x02)
#define SR_TRX_CMD		0x02, 0x1f, 0
#define SR_TRAC_STATUS		0x02, 0xe0, 5
#define RG_TRX_CTRL_0	(0x03)
#define SR_CLKM_CTRL		0x03, 0x07, 0
#define SR_CLKM_SHA_SEL		0x03, 0x08, 3
#define SR_PAD_IO_CLKM		0x03, 0x30, 4
#define SR_PAD_IO		0x03, 0xc0, 6
#define RG_TRX_CTRL_1	(0x04)
#define SR_IRQ_POLARITY		0x04, 0x01, 0
#define SR_IRQ_MASK_MODE	0x04, 0x02, 1
#define SR_SPI_CMD_MODE		0x04, 0x0c, 2
#define SR_RX_BL_CTRL		0x04, 0x10, 4
#define SR_TX_AUTO_CRC_ON	0x04, 0x20, 5
#define SR_IRQ_2_EXT_EN		0x04, 0x40, 6
#define SR_PA_EXT_EN		0x04, 0x80, 7
#define RG_PHY_TX_PWR	(0x05)
#define SR_TX_PWR_23X		0x05, 0x0f, 0
#define SR_PA_LT_230		0x05, 0x30, 4
#define SR_PA_BUF_LT_230	0x05, 0xc0, 6
#define SR_TX_PWR_212		0x05, 0x1f, 0
#define SR_GC_PA_212		0x05, 0x60, 5
#define SR_PA_BOOST_LT_212	0x05, 0x80, 7
#define RG_PHY_RSSI	(0x06)
#define SR_RSSI			0x06, 0x1f, 0
#define SR_RND_VALUE		0x06, 0x60, 5
#define SR_RX_CRC_VALID		0x06, 0x80, 7
#define RG_PHY_ED_LEVEL	(0x07)
#define SR_ED_LEVEL		0x07, 0xff, 0
#define RG_PHY_CC_CCA	(0x08)
#define SR_CHANNEL		0x08, 0x1f, 0
#define SR_CCA_MODE		0x08, 0x60, 5
#define SR_CCA_REQUEST		0x08, 0x80, 7
#define RG_CCA_THRES	(0x09)
#define SR_CCA_ED_THRES		0x09, 0x0f, 0
#define SR_RESERVED_09_1	0x09, 0xf0, 4
#define RG_RX_CTRL	(0x0a)
#define SR_PDT_THRES		0x0a, 0x0f, 0
#define SR_RESERVED_0a_1	0x0a, 0xf0, 4
#define RG_SFD_VALUE	(0x0b)
#define SR_SFD_VALUE		0x0b, 0xff, 0
#define RG_TRX_CTRL_2	(0x0c)
#define SR_OQPSK_DATA_RATE	0x0c, 0x03, 0
#define SR_SUB_MODE		0x0c, 0x04, 2
#define SR_BPSK_QPSK		0x0c, 0x08, 3
#define SR_OQPSK_SUB1_RC_EN	0x0c, 0x10, 4
#define SR_RESERVED_0c_5	0x0c, 0x60, 5
#define SR_RX_SAFE_MODE		0x0c, 0x80, 7
#define RG_ANT_DIV	(0x0d)
#define SR_ANT_CTRL		0x0d, 0x03, 0
#define SR_ANT_EXT_SW_EN	0x0d, 0x04, 2
#define SR_ANT_DIV_EN		0x0d, 0x08, 3
#define SR_RESERVED_0d_2	0x0d, 0x70, 4
#define SR_ANT_SEL		0x0d, 0x80, 7
#define RG_IRQ_MASK	(0x0e)
#define SR_IRQ_MASK		0x0e, 0xff, 0
#define RG_IRQ_STATUS	(0x0f)
#define SR_IRQ_0_PLL_LOCK	0x0f, 0x01, 0
#define SR_IRQ_1_PLL_UNLOCK	0x0f, 0x02, 1
#define SR_IRQ_2_RX_START	0x0f, 0x04, 2
#define SR_IRQ_3_TRX_END	0x0f, 0x08, 3
#define SR_IRQ_4_CCA_ED_DONE	0x0f, 0x10, 4
#define SR_IRQ_5_AMI		0x0f, 0x20, 5
#define SR_IRQ_6_TRX_UR		0x0f, 0x40, 6
#define SR_IRQ_7_BAT_LOW	0x0f, 0x80, 7
#define RG_VREG_CTRL	(0x10)
#define SR_RESERVED_10_6	0x10, 0x03, 0
#define SR_DVDD_OK		0x10, 0x04, 2
#define SR_DVREG_EXT		0x10, 0x08, 3
#define SR_RESERVED_10_3	0x10, 0x30, 4
#define SR_AVDD_OK		0x10, 0x40, 6
#define SR_AVREG_EXT		0x10, 0x80, 7
#define RG_BATMON	(0x11)
#define SR_BATMON_VTH		0x11, 0x0f, 0
#define SR_BATMON_HR		0x11, 0x10, 4
#define SR_BATMON_OK		0x11, 0x20, 5
#define SR_RESERVED_11_1	0x11, 0xc0, 6
#define RG_XOSC_CTRL	(0x12)
#define SR_XTAL_TRIM		0x12, 0x0f, 0
#define SR_XTAL_MODE		0x12, 0xf0, 4
#define RG_RX_SYN	(0x15)
#define SR_RX_PDT_LEVEL		0x15, 0x0f, 0
#define SR_RESERVED_15_2	0x15, 0x70, 4
#define SR_RX_PDT_DIS		0x15, 0x80, 7
#define RG_XAH_CTRL_1	(0x17)
#define SR_RESERVED_17_8	0x17, 0x01, 0
#define SR_AACK_PROM_MODE	0x17, 0x02, 1
#define SR_AACK_ACK_TIME	0x17, 0x04, 2
#define SR_RESERVED_17_5	0x17, 0x08, 3
#define SR_AACK_UPLD_RES_FT	0x17, 0x10, 4
#define SR_AACK_FLTR_RES_FT	0x17, 0x20, 5
#define SR_CSMA_LBT_MODE	0x17, 0x40, 6
#define SR_RESERVED_17_1	0x17, 0x80, 7
#define RG_FTN_CTRL	(0x18)
#define SR_RESERVED_18_2	0x18, 0x7f, 0
#define SR_FTN_START		0x18, 0x80, 7
#define RG_PLL_CF	(0x1a)
#define SR_RESERVED_1a_2	0x1a, 0x7f, 0
#define SR_PLL_CF_START		0x1a, 0x80, 7
#define RG_PLL_DCU	(0x1b)
#define SR_RESERVED_1b_3	0x1b, 0x3f, 0
#define SR_RESERVED_1b_2	0x1b, 0x40, 6
#define SR_PLL_DCU_START	0x1b, 0x80, 7
#define RG_PART_NUM	(0x1c)
#define SR_PART_NUM		0x1c, 0xff, 0
#define RG_VERSION_NUM	(0x1d)
#define SR_VERSION_NUM		0x1d, 0xff, 0
#define RG_MAN_ID_0	(0x1e)
#define SR_MAN_ID_0		0x1e, 0xff, 0
#define RG_MAN_ID_1	(0x1f)
#define SR_MAN_ID_1		0x1f, 0xff, 0
#define RG_SHORT_ADDR_0	(0x20)
#define SR_SHORT_ADDR_0		0x20, 0xff, 0
#define RG_SHORT_ADDR_1	(0x21)
#define SR_SHORT_ADDR_1		0x21, 0xff, 0
#define RG_PAN_ID_0	(0x22)
#define SR_PAN_ID_0		0x22, 0xff, 0
#define RG_PAN_ID_1	(0x23)
#define SR_PAN_ID_1		0x23, 0xff, 0
#define RG_IEEE_ADDR_0	(0x24)
#define SR_IEEE_ADDR_0		0x24, 0xff, 0
#define RG_IEEE_ADDR_1	(0x25)
#define SR_IEEE_ADDR_1		0x25, 0xff, 0
#define RG_IEEE_ADDR_2	(0x26)
#define SR_IEEE_ADDR_2		0x26, 0xff, 0
#define RG_IEEE_ADDR_3	(0x27)
#define SR_IEEE_ADDR_3		0x27, 0xff, 0
#define RG_IEEE_ADDR_4	(0x28)
#define SR_IEEE_ADDR_4		0x28, 0xff, 0
#define RG_IEEE_ADDR_5	(0x29)
#define SR_IEEE_ADDR_5		0x29, 0xff, 0
#define RG_IEEE_ADDR_6	(0x2a)
#define SR_IEEE_ADDR_6		0x2a, 0xff, 0
#define RG_IEEE_ADDR_7	(0x2b)
#define SR_IEEE_ADDR_7		0x2b, 0xff, 0
#define RG_XAH_CTRL_0	(0x2c)
#define SR_SLOTTED_OPERATION	0x2c, 0x01, 0
#define SR_MAX_CSMA_RETRIES	0x2c, 0x0e, 1
#define SR_MAX_FRAME_RETRIES	0x2c, 0xf0, 4
#define RG_CSMA_SEED_0	(0x2d)
#define SR_CSMA_SEED_0		0x2d, 0xff, 0
#define RG_CSMA_SEED_1	(0x2e)
#define SR_CSMA_SEED_1		0x2e, 0x07, 0
#define SR_AACK_I_AM_COORD	0x2e, 0x08, 3
#define SR_AACK_DIS_ACK		0x2e, 0x10, 4
#define SR_AACK_SET_PD		0x2e, 0x20, 5
#define SR_AACK_FVN_MODE	0x2e, 0xc0, 6
#define RG_CSMA_BE	(0x2f)
#define SR_MIN_BE		0x2f, 0x0f, 0
#define SR_MAX_BE		0x2f, 0xf0, 4

#define CMD_REG		0x80
#define CMD_REG_MASK	0x3f
#define CMD_WRITE	0x40
#define CMD_FB		0x20

#define IRQ_BAT_LOW	BIT(7)
#define IRQ_TRX_UR	BIT(6)
#define IRQ_AMI		BIT(5)
#define IRQ_CCA_ED	BIT(4)
#define IRQ_TRX_END	BIT(3)
#define IRQ_RX_START	BIT(2)
#define IRQ_PLL_UNL	BIT(1)
#define IRQ_PLL_LOCK	BIT(0)

#define IRQ_ACTIVE_HIGH	0
#define IRQ_ACTIVE_LOW	1

#define STATE_P_ON		0x00	/* BUSY */
#define STATE_BUSY_RX		0x01
#define STATE_BUSY_TX		0x02
#define STATE_FORCE_TRX_OFF	0x03
#define STATE_FORCE_TX_ON	0x04	/* IDLE */
/* 0x05 */				/* INVALID_PARAMETER */
#define STATE_RX_ON		0x06
/* 0x07 */				/* SUCCESS */
#define STATE_TRX_OFF		0x08
#define STATE_TX_ON		0x09
/* 0x0a - 0x0e */			/* 0x0a - UNSUPPORTED_ATTRIBUTE */
#define STATE_SLEEP		0x0F
#define STATE_PREP_DEEP_SLEEP	0x10
#define STATE_BUSY_RX_AACK	0x11
#define STATE_BUSY_TX_ARET	0x12
#define STATE_RX_AACK_ON	0x16
#define STATE_TX_ARET_ON	0x19
#define STATE_RX_ON_NOCLK	0x1C
#define STATE_RX_AACK_ON_NOCLK	0x1D
#define STATE_BUSY_RX_AACK_NOCLK 0x1E
#define STATE_TRANSITION_IN_PROGRESS 0x1F

#define TRX_STATE_MASK		(0x1F)
#define TRAC_MASK(x)		((x & 0xe0) >> 5)

#define TRAC_SUCCESS			0
#define TRAC_SUCCESS_DATA_PENDING	1
#define TRAC_SUCCESS_WAIT_FOR_ACK	2
#define TRAC_CHANNEL_ACCESS_FAILURE	3
#define TRAC_NO_ACK			5
#define TRAC_INVALID			7

#endif /* !_AT86RF230_H */
