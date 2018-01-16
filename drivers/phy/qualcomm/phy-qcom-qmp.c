/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <dt-bindings/phy/phy.h>

/* QMP PHY QSERDES COM registers */
#define QSERDES_COM_BG_TIMER				0x00c
#define QSERDES_COM_SSC_EN_CENTER			0x010
#define QSERDES_COM_SSC_ADJ_PER1			0x014
#define QSERDES_COM_SSC_ADJ_PER2			0x018
#define QSERDES_COM_SSC_PER1				0x01c
#define QSERDES_COM_SSC_PER2				0x020
#define QSERDES_COM_SSC_STEP_SIZE1			0x024
#define QSERDES_COM_SSC_STEP_SIZE2			0x028
#define QSERDES_COM_BIAS_EN_CLKBUFLR_EN			0x034
#define QSERDES_COM_CLK_ENABLE1				0x038
#define QSERDES_COM_SYS_CLK_CTRL			0x03c
#define QSERDES_COM_SYSCLK_BUF_ENABLE			0x040
#define QSERDES_COM_PLL_IVCO				0x048
#define QSERDES_COM_LOCK_CMP1_MODE0			0x04c
#define QSERDES_COM_LOCK_CMP2_MODE0			0x050
#define QSERDES_COM_LOCK_CMP3_MODE0			0x054
#define QSERDES_COM_LOCK_CMP1_MODE1			0x058
#define QSERDES_COM_LOCK_CMP2_MODE1			0x05c
#define QSERDES_COM_LOCK_CMP3_MODE1			0x060
#define QSERDES_COM_BG_TRIM				0x070
#define QSERDES_COM_CLK_EP_DIV				0x074
#define QSERDES_COM_CP_CTRL_MODE0			0x078
#define QSERDES_COM_CP_CTRL_MODE1			0x07c
#define QSERDES_COM_PLL_RCTRL_MODE0			0x084
#define QSERDES_COM_PLL_RCTRL_MODE1			0x088
#define QSERDES_COM_PLL_CCTRL_MODE0			0x090
#define QSERDES_COM_PLL_CCTRL_MODE1			0x094
#define QSERDES_COM_BIAS_EN_CTRL_BY_PSM			0x0a8
#define QSERDES_COM_SYSCLK_EN_SEL			0x0ac
#define QSERDES_COM_RESETSM_CNTRL			0x0b4
#define QSERDES_COM_RESTRIM_CTRL			0x0bc
#define QSERDES_COM_RESCODE_DIV_NUM			0x0c4
#define QSERDES_COM_LOCK_CMP_EN				0x0c8
#define QSERDES_COM_LOCK_CMP_CFG			0x0cc
#define QSERDES_COM_DEC_START_MODE0			0x0d0
#define QSERDES_COM_DEC_START_MODE1			0x0d4
#define QSERDES_COM_DIV_FRAC_START1_MODE0		0x0dc
#define QSERDES_COM_DIV_FRAC_START2_MODE0		0x0e0
#define QSERDES_COM_DIV_FRAC_START3_MODE0		0x0e4
#define QSERDES_COM_DIV_FRAC_START1_MODE1		0x0e8
#define QSERDES_COM_DIV_FRAC_START2_MODE1		0x0ec
#define QSERDES_COM_DIV_FRAC_START3_MODE1		0x0f0
#define QSERDES_COM_INTEGLOOP_GAIN0_MODE0		0x108
#define QSERDES_COM_INTEGLOOP_GAIN1_MODE0		0x10c
#define QSERDES_COM_INTEGLOOP_GAIN0_MODE1		0x110
#define QSERDES_COM_INTEGLOOP_GAIN1_MODE1		0x114
#define QSERDES_COM_VCO_TUNE_CTRL			0x124
#define QSERDES_COM_VCO_TUNE_MAP			0x128
#define QSERDES_COM_VCO_TUNE1_MODE0			0x12c
#define QSERDES_COM_VCO_TUNE2_MODE0			0x130
#define QSERDES_COM_VCO_TUNE1_MODE1			0x134
#define QSERDES_COM_VCO_TUNE2_MODE1			0x138
#define QSERDES_COM_VCO_TUNE_TIMER1			0x144
#define QSERDES_COM_VCO_TUNE_TIMER2			0x148
#define QSERDES_COM_BG_CTRL				0x170
#define QSERDES_COM_CLK_SELECT				0x174
#define QSERDES_COM_HSCLK_SEL				0x178
#define QSERDES_COM_CORECLK_DIV				0x184
#define QSERDES_COM_CORE_CLK_EN				0x18c
#define QSERDES_COM_C_READY_STATUS			0x190
#define QSERDES_COM_CMN_CONFIG				0x194
#define QSERDES_COM_SVS_MODE_CLK_SEL			0x19c
#define QSERDES_COM_DEBUG_BUS0				0x1a0
#define QSERDES_COM_DEBUG_BUS1				0x1a4
#define QSERDES_COM_DEBUG_BUS2				0x1a8
#define QSERDES_COM_DEBUG_BUS3				0x1ac
#define QSERDES_COM_DEBUG_BUS_SEL			0x1b0
#define QSERDES_COM_CORECLK_DIV_MODE1			0x1bc

/* QMP PHY TX registers */
#define QSERDES_TX_RES_CODE_LANE_OFFSET			0x054
#define QSERDES_TX_DEBUG_BUS_SEL			0x064
#define QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN	0x068
#define QSERDES_TX_LANE_MODE				0x094
#define QSERDES_TX_RCV_DETECT_LVL_2			0x0ac

/* QMP PHY RX registers */
#define QSERDES_RX_UCDR_SO_GAIN_HALF			0x010
#define QSERDES_RX_UCDR_SO_GAIN				0x01c
#define QSERDES_RX_UCDR_FASTLOCK_FO_GAIN		0x040
#define QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE	0x048
#define QSERDES_RX_RX_TERM_BW				0x090
#define QSERDES_RX_RX_EQ_GAIN1_LSB			0x0c4
#define QSERDES_RX_RX_EQ_GAIN1_MSB			0x0c8
#define QSERDES_RX_RX_EQ_GAIN2_LSB			0x0cc
#define QSERDES_RX_RX_EQ_GAIN2_MSB			0x0d0
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2		0x0d8
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3		0x0dc
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4		0x0e0
#define QSERDES_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1		0x108
#define QSERDES_RX_RX_OFFSET_ADAPTOR_CNTRL2		0x10c
#define QSERDES_RX_SIGDET_ENABLES			0x110
#define QSERDES_RX_SIGDET_CNTRL				0x114
#define QSERDES_RX_SIGDET_LVL				0x118
#define QSERDES_RX_SIGDET_DEGLITCH_CNTRL		0x11c
#define QSERDES_RX_RX_BAND				0x120
#define QSERDES_RX_RX_INTERFACE_MODE			0x12c

/* QMP PHY PCS registers */
#define QPHY_POWER_DOWN_CONTROL				0x04
#define QPHY_TXDEEMPH_M6DB_V0				0x24
#define QPHY_TXDEEMPH_M3P5DB_V0				0x28
#define QPHY_ENDPOINT_REFCLK_DRIVE			0x54
#define QPHY_RX_IDLE_DTCT_CNTRL				0x58
#define QPHY_POWER_STATE_CONFIG1			0x60
#define QPHY_POWER_STATE_CONFIG2			0x64
#define QPHY_POWER_STATE_CONFIG4			0x6c
#define QPHY_LOCK_DETECT_CONFIG1			0x80
#define QPHY_LOCK_DETECT_CONFIG2			0x84
#define QPHY_LOCK_DETECT_CONFIG3			0x88
#define QPHY_PWRUP_RESET_DLY_TIME_AUXCLK		0xa0
#define QPHY_LP_WAKEUP_DLY_TIME_AUXCLK			0xa4
#define QPHY_PLL_LOCK_CHK_DLY_TIME_AUXCLK_LSB		0x1A8
#define QPHY_OSC_DTCT_ACTIONS				0x1AC
#define QPHY_RX_SIGDET_LVL				0x1D8
#define QPHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_LSB		0x1DC
#define QPHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_MSB		0x1E0

/* QPHY_SW_RESET bit */
#define SW_RESET				BIT(0)
/* QPHY_POWER_DOWN_CONTROL */
#define SW_PWRDN				BIT(0)
#define REFCLK_DRV_DSBL				BIT(1)
/* QPHY_START_CONTROL bits */
#define SERDES_START				BIT(0)
#define PCS_START				BIT(1)
#define PLL_READY_GATE_EN			BIT(3)
/* QPHY_PCS_STATUS bit */
#define PHYSTATUS				BIT(6)
/* QPHY_COM_PCS_READY_STATUS bit */
#define PCS_READY				BIT(0)

#define PHY_INIT_COMPLETE_TIMEOUT		1000
#define POWER_DOWN_DELAY_US_MIN			10
#define POWER_DOWN_DELAY_US_MAX			11

#define MAX_PROP_NAME				32

struct qmp_phy_init_tbl {
	unsigned int offset;
	unsigned int val;
	/*
	 * register part of layout ?
	 * if yes, then offset gives index in the reg-layout
	 */
	int in_layout;
};

#define QMP_PHY_INIT_CFG(o, v)		\
	{				\
		.offset = o,		\
		.val = v,		\
	}

#define QMP_PHY_INIT_CFG_L(o, v)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.in_layout = 1,		\
	}

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* Common block control registers */
	QPHY_COM_SW_RESET,
	QPHY_COM_POWER_DOWN_CONTROL,
	QPHY_COM_START_CONTROL,
	QPHY_COM_PCS_READY_STATUS,
	/* PCS registers */
	QPHY_PLL_LOCK_CHK_DLY_TIME,
	QPHY_FLL_CNTRL1,
	QPHY_FLL_CNTRL2,
	QPHY_FLL_CNT_VAL_L,
	QPHY_FLL_CNT_VAL_H_TOL,
	QPHY_FLL_MAN_CODE,
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_READY_STATUS,
};

static const unsigned int pciephy_regs_layout[] = {
	[QPHY_COM_SW_RESET]		= 0x400,
	[QPHY_COM_POWER_DOWN_CONTROL]	= 0x404,
	[QPHY_COM_START_CONTROL]	= 0x408,
	[QPHY_COM_PCS_READY_STATUS]	= 0x448,
	[QPHY_PLL_LOCK_CHK_DLY_TIME]	= 0xa8,
	[QPHY_FLL_CNTRL1]		= 0xc4,
	[QPHY_FLL_CNTRL2]		= 0xc8,
	[QPHY_FLL_CNT_VAL_L]		= 0xcc,
	[QPHY_FLL_CNT_VAL_H_TOL]	= 0xd0,
	[QPHY_FLL_MAN_CODE]		= 0xd4,
	[QPHY_SW_RESET]			= 0x00,
	[QPHY_START_CTRL]		= 0x08,
	[QPHY_PCS_READY_STATUS]		= 0x174,
};

static const unsigned int usb3phy_regs_layout[] = {
	[QPHY_FLL_CNTRL1]		= 0xc0,
	[QPHY_FLL_CNTRL2]		= 0xc4,
	[QPHY_FLL_CNT_VAL_L]		= 0xc8,
	[QPHY_FLL_CNT_VAL_H_TOL]	= 0xcc,
	[QPHY_FLL_MAN_CODE]		= 0xd0,
	[QPHY_SW_RESET]			= 0x00,
	[QPHY_START_CTRL]		= 0x08,
	[QPHY_PCS_READY_STATUS]		= 0x17c,
};

static const struct qmp_phy_init_tbl msm8996_pcie_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x42),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER2, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_BUF_ENABLE, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE1, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE2, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESCODE_DIV_NUM, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_EP_DIV, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESCODE_DIV_NUM, 0x40),
};

static const struct qmp_phy_init_tbl msm8996_pcie_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x06),
};

static const struct qmp_phy_init_tbl msm8996_pcie_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_ENABLES, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0xdb),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_BAND, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN_HALF, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_LVL, 0x19),
};

static const struct qmp_phy_init_tbl msm8996_pcie_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_RX_IDLE_DTCT_CNTRL, 0x4c),
	QMP_PHY_INIT_CFG(QPHY_PWRUP_RESET_DLY_TIME_AUXCLK, 0x00),
	QMP_PHY_INIT_CFG(QPHY_LP_WAKEUP_DLY_TIME_AUXCLK, 0x01),

	QMP_PHY_INIT_CFG_L(QPHY_PLL_LOCK_CHK_DLY_TIME, 0x05),

	QMP_PHY_INIT_CFG(QPHY_ENDPOINT_REFCLK_DRIVE, 0x05),
	QMP_PHY_INIT_CFG(QPHY_POWER_DOWN_CONTROL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_POWER_STATE_CONFIG4, 0x00),
	QMP_PHY_INIT_CFG(QPHY_POWER_STATE_CONFIG1, 0xa3),
	QMP_PHY_INIT_CFG(QPHY_TXDEEMPH_M3P5DB_V0, 0x0e),
};

static const struct qmp_phy_init_tbl msm8996_usb3_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x04),
	/* PLL and Loop filter settings */
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x0a),
	/* SSC settings */
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE1, 0xde),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE2, 0x07),
};

static const struct qmp_phy_init_tbl msm8996_usb3_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x06),
};

static const struct qmp_phy_init_tbl msm8996_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4c),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0xbb),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_CNTRL, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_LVL, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x16),
};

static const struct qmp_phy_init_tbl msm8996_usb3_pcs_tbl[] = {
	/* FLL settings */
	QMP_PHY_INIT_CFG_L(QPHY_FLL_CNTRL2, 0x03),
	QMP_PHY_INIT_CFG_L(QPHY_FLL_CNTRL1, 0x02),
	QMP_PHY_INIT_CFG_L(QPHY_FLL_CNT_VAL_L, 0x09),
	QMP_PHY_INIT_CFG_L(QPHY_FLL_CNT_VAL_H_TOL, 0x42),
	QMP_PHY_INIT_CFG_L(QPHY_FLL_MAN_CODE, 0x85),

	/* Lock Det settings */
	QMP_PHY_INIT_CFG(QPHY_LOCK_DETECT_CONFIG1, 0xd1),
	QMP_PHY_INIT_CFG(QPHY_LOCK_DETECT_CONFIG2, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_LOCK_DETECT_CONFIG3, 0x47),
	QMP_PHY_INIT_CFG(QPHY_POWER_STATE_CONFIG2, 0x08),
};

static const struct qmp_phy_init_tbl ipq8074_pcie_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0xf),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER1, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER2, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x6),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0xf),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0xa),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESETSM_CNTRL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0xa),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0xa),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x3),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0xD),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0xD04),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x2),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_BUF_ENABLE, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0xb),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CTRL_BY_PSM, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0xa),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_EN_CENTER, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER2, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER1, 0x2),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER2, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE1, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE2, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_EP_DIV, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_CNTRL, 0x7),
};

static const struct qmp_phy_init_tbl ipq8074_pcie_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x6),
	QMP_PHY_INIT_CFG(QSERDES_TX_RES_CODE_LANE_OFFSET, 0x2),
	QMP_PHY_INIT_CFG(QSERDES_TX_RCV_DETECT_LVL_2, 0x12),
};

static const struct qmp_phy_init_tbl ipq8074_pcie_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_ENABLES, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x1),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x0),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0xdb),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN, 0x4),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN_HALF, 0x4),
};

static const struct qmp_phy_init_tbl ipq8074_pcie_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_ENDPOINT_REFCLK_DRIVE, 0x4),
	QMP_PHY_INIT_CFG(QPHY_OSC_DTCT_ACTIONS, 0x0),
	QMP_PHY_INIT_CFG(QPHY_PWRUP_RESET_DLY_TIME_AUXCLK, 0x40),
	QMP_PHY_INIT_CFG(QPHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_MSB, 0x0),
	QMP_PHY_INIT_CFG(QPHY_L1SS_WAKEUP_DLY_TIME_AUXCLK_LSB, 0x40),
	QMP_PHY_INIT_CFG(QPHY_PLL_LOCK_CHK_DLY_TIME_AUXCLK_LSB, 0x0),
	QMP_PHY_INIT_CFG(QPHY_LP_WAKEUP_DLY_TIME_AUXCLK, 0x40),
	QMP_PHY_INIT_CFG_L(QPHY_PLL_LOCK_CHK_DLY_TIME, 0x73),
	QMP_PHY_INIT_CFG(QPHY_RX_SIGDET_LVL, 0x99),
	QMP_PHY_INIT_CFG(QPHY_TXDEEMPH_M6DB_V0, 0x15),
	QMP_PHY_INIT_CFG(QPHY_TXDEEMPH_M3P5DB_V0, 0xe),
	QMP_PHY_INIT_CFG_L(QPHY_SW_RESET, 0x0),
	QMP_PHY_INIT_CFG_L(QPHY_START_CTRL, 0x3),
};

/* struct qmp_phy_cfg - per-PHY initialization config */
struct qmp_phy_cfg {
	/* phy-type - PCIE/UFS/USB */
	unsigned int type;
	/* number of lanes provided by phy */
	int nlanes;

	/* Init sequence for PHY blocks - serdes, tx, rx, pcs */
	const struct qmp_phy_init_tbl *serdes_tbl;
	int serdes_tbl_num;
	const struct qmp_phy_init_tbl *tx_tbl;
	int tx_tbl_num;
	const struct qmp_phy_init_tbl *rx_tbl;
	int rx_tbl_num;
	const struct qmp_phy_init_tbl *pcs_tbl;
	int pcs_tbl_num;

	/* clock ids to be requested */
	const char * const *clk_list;
	int num_clks;
	/* resets to be requested */
	const char * const *reset_list;
	int num_resets;
	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;

	/* array of registers with different offsets */
	const unsigned int *regs;

	unsigned int start_ctrl;
	unsigned int pwrdn_ctrl;
	unsigned int mask_pcs_ready;
	unsigned int mask_com_pcs_ready;

	/* true, if PHY has a separate PHY_COM control block */
	bool has_phy_com_ctrl;
	/* true, if PHY has a reset for individual lanes */
	bool has_lane_rst;
	/* true, if PHY needs delay after POWER_DOWN */
	bool has_pwrdn_delay;
	/* power_down delay in usec */
	int pwrdn_delay_min;
	int pwrdn_delay_max;
};

/**
 * struct qmp_phy - per-lane phy descriptor
 *
 * @phy: generic phy
 * @tx: iomapped memory space for lane's tx
 * @rx: iomapped memory space for lane's rx
 * @pcs: iomapped memory space for lane's pcs
 * @pipe_clk: pipe lock
 * @index: lane index
 * @qmp: QMP phy to which this lane belongs
 * @lane_rst: lane's reset controller
 */
struct qmp_phy {
	struct phy *phy;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *pcs;
	struct clk *pipe_clk;
	unsigned int index;
	struct qcom_qmp *qmp;
	struct reset_control *lane_rst;
};

/**
 * struct qcom_qmp - structure holding QMP phy block attributes
 *
 * @dev: device
 * @serdes: iomapped memory space for phy's serdes
 *
 * @clks: array of clocks required by phy
 * @resets: array of resets required by phy
 * @vregs: regulator supplies bulk data
 *
 * @cfg: phy specific configuration
 * @phys: array of per-lane phy descriptors
 * @phy_mutex: mutex lock for PHY common block initialization
 * @init_count: phy common block initialization count
 */
struct qcom_qmp {
	struct device *dev;
	void __iomem *serdes;

	struct clk **clks;
	struct reset_control **resets;
	struct regulator_bulk_data *vregs;

	const struct qmp_phy_cfg *cfg;
	struct qmp_phy **phys;

	struct mutex phy_mutex;
	int init_count;
};

static inline void qphy_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static inline void qphy_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

/* list of clocks required by phy */
static const char * const msm8996_phy_clk_l[] = {
	"aux", "cfg_ahb", "ref",
};

/* list of resets */
static const char * const msm8996_pciephy_reset_l[] = {
	"phy", "common", "cfg",
};

static const char * const msm8996_usb3phy_reset_l[] = {
	"phy", "common",
};

/* list of regulators */
static const char * const msm8996_phy_vreg_l[] = {
	"vdda-phy", "vdda-pll",
};

static const struct qmp_phy_cfg msm8996_pciephy_cfg = {
	.type			= PHY_TYPE_PCIE,
	.nlanes			= 3,

	.serdes_tbl		= msm8996_pcie_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(msm8996_pcie_serdes_tbl),
	.tx_tbl			= msm8996_pcie_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(msm8996_pcie_tx_tbl),
	.rx_tbl			= msm8996_pcie_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(msm8996_pcie_rx_tbl),
	.pcs_tbl		= msm8996_pcie_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(msm8996_pcie_pcs_tbl),
	.clk_list		= msm8996_phy_clk_l,
	.num_clks		= ARRAY_SIZE(msm8996_phy_clk_l),
	.reset_list		= msm8996_pciephy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_pciephy_reset_l),
	.vreg_list		= msm8996_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(msm8996_phy_vreg_l),
	.regs			= pciephy_regs_layout,

	.start_ctrl		= PCS_START | PLL_READY_GATE_EN,
	.pwrdn_ctrl		= SW_PWRDN | REFCLK_DRV_DSBL,
	.mask_com_pcs_ready	= PCS_READY,

	.has_phy_com_ctrl	= true,
	.has_lane_rst		= true,
	.has_pwrdn_delay	= true,
	.pwrdn_delay_min	= POWER_DOWN_DELAY_US_MIN,
	.pwrdn_delay_max	= POWER_DOWN_DELAY_US_MAX,
};

static const struct qmp_phy_cfg msm8996_usb3phy_cfg = {
	.type			= PHY_TYPE_USB3,
	.nlanes			= 1,

	.serdes_tbl		= msm8996_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(msm8996_usb3_serdes_tbl),
	.tx_tbl			= msm8996_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(msm8996_usb3_tx_tbl),
	.rx_tbl			= msm8996_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(msm8996_usb3_rx_tbl),
	.pcs_tbl		= msm8996_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(msm8996_usb3_pcs_tbl),
	.clk_list		= msm8996_phy_clk_l,
	.num_clks		= ARRAY_SIZE(msm8996_phy_clk_l),
	.reset_list		= msm8996_usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_usb3phy_reset_l),
	.vreg_list		= msm8996_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(msm8996_phy_vreg_l),
	.regs			= usb3phy_regs_layout,

	.start_ctrl		= SERDES_START | PCS_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.mask_pcs_ready		= PHYSTATUS,
};

/* list of resets */
static const char * const ipq8074_pciephy_reset_l[] = {
	"phy", "common",
};

static const struct qmp_phy_cfg ipq8074_pciephy_cfg = {
	.type			= PHY_TYPE_PCIE,
	.nlanes			= 1,

	.serdes_tbl		= ipq8074_pcie_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(ipq8074_pcie_serdes_tbl),
	.tx_tbl			= ipq8074_pcie_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(ipq8074_pcie_tx_tbl),
	.rx_tbl			= ipq8074_pcie_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(ipq8074_pcie_rx_tbl),
	.pcs_tbl		= ipq8074_pcie_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(ipq8074_pcie_pcs_tbl),
	.clk_list		= NULL,
	.num_clks		= 0,
	.reset_list		= ipq8074_pciephy_reset_l,
	.num_resets		= ARRAY_SIZE(ipq8074_pciephy_reset_l),
	.vreg_list		= NULL,
	.num_vregs		= 0,
	.regs			= pciephy_regs_layout,

	.start_ctrl		= SERDES_START | PCS_START,
	.pwrdn_ctrl		= SW_PWRDN | REFCLK_DRV_DSBL,
	.mask_pcs_ready		= PHYSTATUS,

	.has_phy_com_ctrl	= false,
	.has_lane_rst		= false,
	.has_pwrdn_delay	= true,
	.pwrdn_delay_min	= 995,		/* us */
	.pwrdn_delay_max	= 1005,		/* us */
};

static void qcom_qmp_phy_configure(void __iomem *base,
				   const unsigned int *regs,
				   const struct qmp_phy_init_tbl tbl[],
				   int num)
{
	int i;
	const struct qmp_phy_init_tbl *t = tbl;

	if (!t)
		return;

	for (i = 0; i < num; i++, t++) {
		if (t->in_layout)
			writel(t->val, base + regs[t->offset]);
		else
			writel(t->val, base + t->offset);
	}
}

static int qcom_qmp_phy_poweron(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	int num = qmp->cfg->num_vregs;
	int ret;

	dev_vdbg(&phy->dev, "Powering on QMP phy\n");

	/* turn on regulator supplies */
	ret = regulator_bulk_enable(num, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(qphy->pipe_clk);
	if (ret) {
		dev_err(qmp->dev, "pipe_clk enable failed, err=%d\n", ret);
		regulator_bulk_disable(num, qmp->vregs);
		return ret;
	}

	return 0;
}

static int qcom_qmp_phy_poweroff(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;

	regulator_bulk_disable(qmp->cfg->num_vregs, qmp->vregs);

	return 0;
}

static int qcom_qmp_phy_com_init(struct qcom_qmp *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *serdes = qmp->serdes;
	int ret, i;

	mutex_lock(&qmp->phy_mutex);
	if (qmp->init_count++) {
		mutex_unlock(&qmp->phy_mutex);
		return 0;
	}

	for (i = 0; i < cfg->num_resets; i++) {
		ret = reset_control_deassert(qmp->resets[i]);
		if (ret) {
			dev_err(qmp->dev, "%s reset deassert failed\n",
				qmp->cfg->reset_list[i]);
			goto err_rst;
		}
	}

	if (cfg->has_phy_com_ctrl)
		qphy_setbits(serdes, cfg->regs[QPHY_COM_POWER_DOWN_CONTROL],
			     SW_PWRDN);

	/* Serdes configuration */
	qcom_qmp_phy_configure(serdes, cfg->regs, cfg->serdes_tbl,
			       cfg->serdes_tbl_num);

	if (cfg->has_phy_com_ctrl) {
		void __iomem *status;
		unsigned int mask, val;

		qphy_clrbits(serdes, cfg->regs[QPHY_COM_SW_RESET], SW_RESET);
		qphy_setbits(serdes, cfg->regs[QPHY_COM_START_CONTROL],
			     SERDES_START | PCS_START);

		status = serdes + cfg->regs[QPHY_COM_PCS_READY_STATUS];
		mask = cfg->mask_com_pcs_ready;

		ret = readl_poll_timeout(status, val, (val & mask), 10,
					 PHY_INIT_COMPLETE_TIMEOUT);
		if (ret) {
			dev_err(qmp->dev,
				"phy common block init timed-out\n");
			goto err_rst;
		}
	}

	mutex_unlock(&qmp->phy_mutex);

	return 0;

err_rst:
	while (--i >= 0)
		reset_control_assert(qmp->resets[i]);
	mutex_unlock(&qmp->phy_mutex);

	return ret;
}

static int qcom_qmp_phy_com_exit(struct qcom_qmp *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *serdes = qmp->serdes;
	int i = cfg->num_resets;

	mutex_lock(&qmp->phy_mutex);
	if (--qmp->init_count) {
		mutex_unlock(&qmp->phy_mutex);
		return 0;
	}

	if (cfg->has_phy_com_ctrl) {
		qphy_setbits(serdes, cfg->regs[QPHY_COM_START_CONTROL],
			     SERDES_START | PCS_START);
		qphy_clrbits(serdes, cfg->regs[QPHY_COM_SW_RESET],
			     SW_RESET);
		qphy_setbits(serdes, cfg->regs[QPHY_COM_POWER_DOWN_CONTROL],
			     SW_PWRDN);
	}

	while (--i >= 0)
		reset_control_assert(qmp->resets[i]);

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

/* PHY Initialization */
static int qcom_qmp_phy_init(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *tx = qphy->tx;
	void __iomem *rx = qphy->rx;
	void __iomem *pcs = qphy->pcs;
	void __iomem *status;
	unsigned int mask, val;
	int ret, i;

	dev_vdbg(qmp->dev, "Initializing QMP phy\n");

	for (i = 0; i < qmp->cfg->num_clks; i++) {
		ret = clk_prepare_enable(qmp->clks[i]);
		if (ret) {
			dev_err(qmp->dev, "failed to enable %s clk, err=%d\n",
				qmp->cfg->clk_list[i], ret);
			goto err_clk;
		}
	}

	ret = qcom_qmp_phy_com_init(qmp);
	if (ret)
		goto err_clk;

	if (cfg->has_lane_rst) {
		ret = reset_control_deassert(qphy->lane_rst);
		if (ret) {
			dev_err(qmp->dev, "lane%d reset deassert failed\n",
				qphy->index);
			goto err_lane_rst;
		}
	}

	/* Tx, Rx, and PCS configurations */
	qcom_qmp_phy_configure(tx, cfg->regs, cfg->tx_tbl, cfg->tx_tbl_num);
	qcom_qmp_phy_configure(rx, cfg->regs, cfg->rx_tbl, cfg->rx_tbl_num);
	qcom_qmp_phy_configure(pcs, cfg->regs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	/*
	 * Pull out PHY from POWER DOWN state.
	 * This is active low enable signal to power-down PHY.
	 */
	qphy_setbits(pcs, QPHY_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

	if (cfg->has_pwrdn_delay)
		usleep_range(cfg->pwrdn_delay_min, cfg->pwrdn_delay_max);

	/* start SerDes and Phy-Coding-Sublayer */
	qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	/* Pull PHY out of reset state */
	qphy_clrbits(pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	status = pcs + cfg->regs[QPHY_PCS_READY_STATUS];
	mask = cfg->mask_pcs_ready;

	ret = readl_poll_timeout(status, val, !(val & mask), 1,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		goto err_pcs_ready;
	}

	return ret;

err_pcs_ready:
	if (cfg->has_lane_rst)
		reset_control_assert(qphy->lane_rst);
err_lane_rst:
	qcom_qmp_phy_com_exit(qmp);
err_clk:
	while (--i >= 0)
		clk_disable_unprepare(qmp->clks[i]);

	return ret;
}

static int qcom_qmp_phy_exit(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i = cfg->num_clks;

	clk_disable_unprepare(qphy->pipe_clk);

	/* PHY reset */
	qphy_setbits(qphy->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qphy->pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	/* Put PHY into POWER DOWN state: active low */
	qphy_clrbits(qphy->pcs, QPHY_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

	if (cfg->has_lane_rst)
		reset_control_assert(qphy->lane_rst);

	qcom_qmp_phy_com_exit(qmp);

	while (--i >= 0)
		clk_disable_unprepare(qmp->clks[i]);

	return 0;
}

static int qcom_qmp_phy_vreg_init(struct device *dev)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int num = qmp->cfg->num_vregs;
	int i;

	qmp->vregs = devm_kcalloc(dev, num, sizeof(*qmp->vregs), GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->vregs[i].supply = qmp->cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, qmp->vregs);
}

static int qcom_qmp_phy_reset_init(struct device *dev)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int i;

	qmp->resets = devm_kcalloc(dev, qmp->cfg->num_resets,
				   sizeof(*qmp->resets), GFP_KERNEL);
	if (!qmp->resets)
		return -ENOMEM;

	for (i = 0; i < qmp->cfg->num_resets; i++) {
		struct reset_control *rst;
		const char *name = qmp->cfg->reset_list[i];

		rst = devm_reset_control_get(dev, name);
		if (IS_ERR(rst)) {
			dev_err(dev, "failed to get %s reset\n", name);
			return PTR_ERR(rst);
		}
		qmp->resets[i] = rst;
	}

	return 0;
}

static int qcom_qmp_phy_clk_init(struct device *dev)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int ret, i;

	qmp->clks = devm_kcalloc(dev, qmp->cfg->num_clks,
				 sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < qmp->cfg->num_clks; i++) {
		struct clk *_clk;
		const char *name = qmp->cfg->clk_list[i];

		_clk = devm_clk_get(dev, name);
		if (IS_ERR(_clk)) {
			ret = PTR_ERR(_clk);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "failed to get %s clk, %d\n",
					name, ret);
			return ret;
		}
		qmp->clks[i] = _clk;
	}

	return 0;
}

/*
 * Register a fixed rate pipe clock.
 *
 * The <s>_pipe_clksrc generated by PHY goes to the GCC that gate
 * controls it. The <s>_pipe_clk coming out of the GCC is requested
 * by the PHY driver for its operations.
 * We register the <s>_pipe_clksrc here. The gcc driver takes care
 * of assigning this <s>_pipe_clksrc as parent to <s>_pipe_clk.
 * Below picture shows this relationship.
 *
 *         +---------------+
 *         |   PHY block   |<<---------------------------------------+
 *         |               |                                         |
 *         |   +-------+   |                   +-----+               |
 *   I/P---^-->|  PLL  |---^--->pipe_clksrc--->| GCC |--->pipe_clk---+
 *    clk  |   +-------+   |                   +-----+
 *         +---------------+
 */
static int phy_pipe_clk_register(struct qcom_qmp *qmp, struct device_node *np)
{
	struct clk_fixed_rate *fixed;
	struct clk_init_data init = { };
	int ret;

	if ((qmp->cfg->type != PHY_TYPE_USB3) &&
	    (qmp->cfg->type != PHY_TYPE_PCIE)) {
		/* not all phys register pipe clocks, so return success */
		return 0;
	}

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret) {
		dev_err(qmp->dev, "%s: No clock-output-names\n", np->name);
		return ret;
	}

	fixed = devm_kzalloc(qmp->dev, sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return -ENOMEM;

	init.ops = &clk_fixed_rate_ops;

	/* controllers using QMP phys use 125MHz pipe clock interface */
	fixed->fixed_rate = 125000000;
	fixed->hw.init = &init;

	return devm_clk_hw_register(qmp->dev, &fixed->hw);
}

static const struct phy_ops qcom_qmp_phy_gen_ops = {
	.init		= qcom_qmp_phy_init,
	.exit		= qcom_qmp_phy_exit,
	.power_on	= qcom_qmp_phy_poweron,
	.power_off	= qcom_qmp_phy_poweroff,
	.owner		= THIS_MODULE,
};

static
int qcom_qmp_phy_create(struct device *dev, struct device_node *np, int id)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	struct phy *generic_phy;
	struct qmp_phy *qphy;
	char prop_name[MAX_PROP_NAME];
	int ret;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	/*
	 * Get memory resources for each phy lane:
	 * Resources are indexed as: tx -> 0; rx -> 1; pcs -> 2.
	 */
	qphy->tx = of_iomap(np, 0);
	if (!qphy->tx)
		return -ENOMEM;

	qphy->rx = of_iomap(np, 1);
	if (!qphy->rx)
		return -ENOMEM;

	qphy->pcs = of_iomap(np, 2);
	if (!qphy->pcs)
		return -ENOMEM;

	/*
	 * Get PHY's Pipe clock, if any. USB3 and PCIe are PIPE3
	 * based phys, so they essentially have pipe clock. So,
	 * we return error in case phy is USB3 or PIPE type.
	 * Otherwise, we initialize pipe clock to NULL for
	 * all phys that don't need this.
	 */
	snprintf(prop_name, sizeof(prop_name), "pipe%d", id);
	qphy->pipe_clk = of_clk_get_by_name(np, prop_name);
	if (IS_ERR(qphy->pipe_clk)) {
		if (qmp->cfg->type == PHY_TYPE_PCIE ||
		    qmp->cfg->type == PHY_TYPE_USB3) {
			ret = PTR_ERR(qphy->pipe_clk);
			if (ret != -EPROBE_DEFER)
				dev_err(dev,
					"failed to get lane%d pipe_clk, %d\n",
					id, ret);
			return ret;
		}
		qphy->pipe_clk = NULL;
	}

	/* Get lane reset, if any */
	if (qmp->cfg->has_lane_rst) {
		snprintf(prop_name, sizeof(prop_name), "lane%d", id);
		qphy->lane_rst = of_reset_control_get(np, prop_name);
		if (IS_ERR(qphy->lane_rst)) {
			dev_err(dev, "failed to get lane%d reset\n", id);
			return PTR_ERR(qphy->lane_rst);
		}
	}

	generic_phy = devm_phy_create(dev, np, &qcom_qmp_phy_gen_ops);
	if (IS_ERR(generic_phy)) {
		ret = PTR_ERR(generic_phy);
		dev_err(dev, "failed to create qphy %d\n", ret);
		return ret;
	}

	qphy->phy = generic_phy;
	qphy->index = id;
	qphy->qmp = qmp;
	qmp->phys[id] = qphy;
	phy_set_drvdata(generic_phy, qphy);

	return 0;
}

static const struct of_device_id qcom_qmp_phy_of_match_table[] = {
	{
		.compatible = "qcom,msm8996-qmp-pcie-phy",
		.data = &msm8996_pciephy_cfg,
	}, {
		.compatible = "qcom,msm8996-qmp-usb3-phy",
		.data = &msm8996_usb3phy_cfg,
	}, {
		.compatible = "qcom,ipq8074-qmp-pcie-phy",
		.data = &ipq8074_pciephy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_qmp_phy_of_match_table);

static int qcom_qmp_phy_probe(struct platform_device *pdev)
{
	struct qcom_qmp *qmp;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *child;
	struct phy_provider *phy_provider;
	void __iomem *base;
	int num, id;
	int ret;

	qmp = devm_kzalloc(dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = dev;
	dev_set_drvdata(dev, qmp);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* per PHY serdes; usually located at base address */
	qmp->serdes = base;

	mutex_init(&qmp->phy_mutex);

	/* Get the specific init parameters of QMP phy */
	qmp->cfg = of_device_get_match_data(dev);

	ret = qcom_qmp_phy_clk_init(dev);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_reset_init(dev);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_vreg_init(dev);
	if (ret) {
		dev_err(dev, "failed to get regulator supplies\n");
		return ret;
	}

	num = of_get_available_child_count(dev->of_node);
	/* do we have a rogue child node ? */
	if (num > qmp->cfg->nlanes)
		return -EINVAL;

	qmp->phys = devm_kcalloc(dev, num, sizeof(*qmp->phys), GFP_KERNEL);
	if (!qmp->phys)
		return -ENOMEM;

	id = 0;
	for_each_available_child_of_node(dev->of_node, child) {
		/* Create per-lane phy */
		ret = qcom_qmp_phy_create(dev, child, id);
		if (ret) {
			dev_err(dev, "failed to create lane%d phy, %d\n",
				id, ret);
			return ret;
		}

		/*
		 * Register the pipe clock provided by phy.
		 * See function description to see details of this pipe clock.
		 */
		ret = phy_pipe_clk_register(qmp, child);
		if (ret) {
			dev_err(qmp->dev,
				"failed to register pipe clock source\n");
			return ret;
		}
		id++;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_info(dev, "Registered Qcom-QMP phy\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver qcom_qmp_phy_driver = {
	.probe		= qcom_qmp_phy_probe,
	.driver = {
		.name	= "qcom-qmp-phy",
		.of_match_table = qcom_qmp_phy_of_match_table,
	},
};

module_platform_driver(qcom_qmp_phy_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP PHY driver");
MODULE_LICENSE("GPL v2");
