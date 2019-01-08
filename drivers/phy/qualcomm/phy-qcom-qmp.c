// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include "phy-qcom-qmp.h"

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

/* QPHY_V3_DP_COM_RESET_OVRD_CTRL register bits */
/* DP PHY soft reset */
#define SW_DPPHY_RESET				BIT(0)
/* mux to select DP PHY reset control, 0:HW control, 1: software reset */
#define SW_DPPHY_RESET_MUX			BIT(1)
/* USB3 PHY soft reset */
#define SW_USB3PHY_RESET			BIT(2)
/* mux to select USB3 PHY reset control, 0:HW control, 1: software reset */
#define SW_USB3PHY_RESET_MUX			BIT(3)

/* QPHY_V3_DP_COM_PHY_MODE_CTRL register bits */
#define USB3_MODE				BIT(0) /* enables USB3 mode */
#define DP_MODE					BIT(1) /* enables DP mode */

/* QPHY_PCS_AUTONOMOUS_MODE_CTRL register bits */
#define ARCVR_DTCT_EN				BIT(0)
#define ALFPS_DTCT_EN				BIT(1)
#define ARCVR_DTCT_EVENT_SEL			BIT(4)

/* QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR register bits */
#define IRQ_CLEAR				BIT(0)

/* QPHY_PCS_LFPS_RXTERM_IRQ_STATUS register bits */
#define RCVR_DETECT				BIT(0)

/* QPHY_V3_PCS_MISC_CLAMP_ENABLE register bits */
#define CLAMP_EN				BIT(0) /* enables i/o clamp_n */

#define PHY_INIT_COMPLETE_TIMEOUT		1000
#define POWER_DOWN_DELAY_US_MIN			10
#define POWER_DOWN_DELAY_US_MAX			11

#define MAX_PROP_NAME				32

/* Define the assumed distance between lanes for underspecified device trees. */
#define QMP_PHY_LEGACY_LANE_STRIDE		0x400

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
	QPHY_PCS_AUTONOMOUS_MODE_CTRL,
	QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR,
	QPHY_PCS_LFPS_RXTERM_IRQ_STATUS,
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
	[QPHY_PCS_AUTONOMOUS_MODE_CTRL]	= 0x0d4,
	[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR]  = 0x0d8,
	[QPHY_PCS_LFPS_RXTERM_IRQ_STATUS] = 0x178,
};

static const unsigned int qmp_v3_usb3phy_regs_layout[] = {
	[QPHY_SW_RESET]			= 0x00,
	[QPHY_START_CTRL]		= 0x08,
	[QPHY_PCS_READY_STATUS]		= 0x174,
	[QPHY_PCS_AUTONOMOUS_MODE_CTRL]	= 0x0d8,
	[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR]  = 0x0dc,
	[QPHY_PCS_LFPS_RXTERM_IRQ_STATUS] = 0x170,
};

static const unsigned int sdm845_ufsphy_regs_layout[] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x160,
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

static const struct qmp_phy_init_tbl qmp_v3_usb3_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_IVCO, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BIAS_EN_CLKBUFLR_EN, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_RESETSM_CNTRL2, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CMN_CONFIG, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_HSCLK_SEL, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START1_MODE0, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START2_MODE0, 0xea),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START3_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CP_CTRL_MODE0, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN0_MODE0, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE2_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE1_MODE0, 0xc9),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CORECLK_DIV_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP2_MODE0, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP1_MODE0, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP_EN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_BUF_ENABLE, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE1, 0x85),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE2, 0x07),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_HIGHZ_DRVR_EN, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_LANE_MODE_1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_RX, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_TX, 0x06),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x75),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_pcs_tbl[] = {
	/* FLL settings */
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL2, 0x83),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_L, 0x09),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_H_TOL, 0xa2),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_MAN_CODE, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL1, 0x02),

	/* Lock Det settings */
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG1, 0xd1),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG2, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG3, 0x47),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_POWER_STATE_CONFIG2, 0x1b),

	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_LVL, 0xba),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V0, 0x9f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V1, 0x9f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V2, 0xb7),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V3, 0x4e),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V4, 0x65),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_LS, 0x6b),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V0, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V0, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V1, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V1, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V2, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V2, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V3, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V3, 0x1d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V4, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V4, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_LS, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_LS, 0x0d),

	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RATE_SLEW_CNTRL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TSYNC_RSYNC_TIME, 0x44),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_L, 0xe7),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_H, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_L, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_H, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_WAIT_TIME, 0x75),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LFPS_TX_ECSTART_EQTLOCK, 0x86),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_RUN_TIME, 0x13),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_uniphy_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_IVCO, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BIAS_EN_CLKBUFLR_EN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_RESETSM_CNTRL2, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_HSCLK_SEL, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START1_MODE0, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START2_MODE0, 0xea),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DIV_FRAC_START3_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CP_CTRL_MODE0, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN0_MODE0, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE2_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE1_MODE0, 0xc9),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CORECLK_DIV_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP2_MODE0, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP1_MODE0, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP_EN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_BUF_ENABLE, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE1, 0x85),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE2, 0x07),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_uniphy_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_HIGHZ_DRVR_EN, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_LANE_MODE_1, 0xc6),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_RX, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_TX, 0x06),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_uniphy_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_VGA_CAL_CNTRL2, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_MODE_00, 0x50),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x75),
};

static const struct qmp_phy_init_tbl qmp_v3_usb3_uniphy_pcs_tbl[] = {
	/* FLL settings */
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL2, 0x83),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_L, 0x09),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_H_TOL, 0xa2),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_MAN_CODE, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL1, 0x02),

	/* Lock Det settings */
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG1, 0xd1),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG2, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG3, 0x47),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_POWER_STATE_CONFIG2, 0x1b),

	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_LVL, 0xba),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V0, 0x9f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V1, 0x9f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V2, 0xb5),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V3, 0x4c),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V4, 0x64),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_LS, 0x6a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V0, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V0, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V1, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V1, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V2, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V2, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V3, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V3, 0x1d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V4, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V4, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_LS, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_LS, 0x0d),

	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RATE_SLEW_CNTRL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TSYNC_RSYNC_TIME, 0x44),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_L, 0xe7),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_H, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_L, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_H, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_WAIT_TIME, 0x75),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LFPS_TX_ECSTART_EQTLOCK, 0x86),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_RUN_TIME, 0x13),

	QMP_PHY_INIT_CFG(QPHY_V3_PCS_REFGEN_REQ_CONFIG1, 0x21),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_REFGEN_REQ_CONFIG2, 0x60),
};

static const struct qmp_phy_init_tbl sdm845_ufsphy_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BIAS_EN_CLKBUFLR_EN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_IVCO, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_EN_SEL, 0xd5),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_RESETSM_CNTRL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP_EN, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_MAP, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SVS_MODE_CLK_SEL, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_INITVAL1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_INITVAL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CP_CTRL_MODE0, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN0_MODE0, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE1_MODE0, 0xda),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE2_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP1_MODE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP2_MODE0, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_DEC_START_MODE1, 0x98),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CP_CTRL_MODE1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_CCTRL_MODE1, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN0_MODE1, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_GAIN1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE1_MODE1, 0xc1),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE2_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP1_MODE1, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_LOCK_CMP2_MODE1, 0x0f),

	/* Rate B */
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_VCO_TUNE_MAP, 0x44),
};

static const struct qmp_phy_init_tbl sdm845_ufsphy_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_LANE_MODE_1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_TX, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_RX, 0x07),
};

static const struct qmp_phy_init_tbl sdm845_ufsphy_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_LVL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_INTERFACE_MODE, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_TERM_BW, 0x5b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x1b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SVS_SO_GAIN_HALF, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SVS_SO_GAIN_QUARTER, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SVS_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_PI_CONTROLS, 0x81),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_LOW, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_MODE_00, 0x59),
};

static const struct qmp_phy_init_tbl sdm845_ufsphy_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_CTRL2, 0x6e),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TX_LARGE_AMP_DRV_LVL, 0x0a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TX_SMALL_AMP_DRV_LVL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SYM_RESYNC_CTRL, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TX_MID_TERM_CTRL1, 0x43),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_CTRL1, 0x0f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_MIN_HIBERN8_TIME, 0x9a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_MULTI_LANE_CTRL1, 0x02),
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

	/* true, if PHY has a separate DP_COM control block */
	bool has_phy_dp_com_ctrl;
	/* true, if PHY has secondary tx/rx lanes to be configured */
	bool is_dual_lane_phy;

	/* true, if PCS block has no separate SW_RESET register */
	bool no_pcs_sw_reset;
};

/**
 * struct qmp_phy - per-lane phy descriptor
 *
 * @phy: generic phy
 * @tx: iomapped memory space for lane's tx
 * @rx: iomapped memory space for lane's rx
 * @pcs: iomapped memory space for lane's pcs
 * @tx2: iomapped memory space for second lane's tx (in dual lane PHYs)
 * @rx2: iomapped memory space for second lane's rx (in dual lane PHYs)
 * @pcs_misc: iomapped memory space for lane's pcs_misc
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
	void __iomem *tx2;
	void __iomem *rx2;
	void __iomem *pcs_misc;
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
 * @dp_com: iomapped memory space for phy's dp_com control block
 *
 * @clks: array of clocks required by phy
 * @resets: array of resets required by phy
 * @vregs: regulator supplies bulk data
 *
 * @cfg: phy specific configuration
 * @phys: array of per-lane phy descriptors
 * @phy_mutex: mutex lock for PHY common block initialization
 * @init_count: phy common block initialization count
 * @phy_initialized: indicate if PHY has been initialized
 * @mode: current PHY mode
 */
struct qcom_qmp {
	struct device *dev;
	void __iomem *serdes;
	void __iomem *dp_com;

	struct clk_bulk_data *clks;
	struct reset_control **resets;
	struct regulator_bulk_data *vregs;

	const struct qmp_phy_cfg *cfg;
	struct qmp_phy **phys;

	struct mutex phy_mutex;
	int init_count;
	bool phy_initialized;
	enum phy_mode mode;
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

static const char * const qmp_v3_phy_clk_l[] = {
	"aux", "cfg_ahb", "ref", "com_aux",
};

static const char * const sdm845_ufs_phy_clk_l[] = {
	"ref", "ref_aux",
};

/* list of resets */
static const char * const msm8996_pciephy_reset_l[] = {
	"phy", "common", "cfg",
};

static const char * const msm8996_usb3phy_reset_l[] = {
	"phy", "common",
};

/* list of regulators */
static const char * const qmp_phy_vreg_l[] = {
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
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
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
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
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

static const struct qmp_phy_cfg qmp_v3_usb3phy_cfg = {
	.type			= PHY_TYPE_USB3,
	.nlanes			= 1,

	.serdes_tbl		= qmp_v3_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_serdes_tbl),
	.tx_tbl			= qmp_v3_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_tx_tbl),
	.rx_tbl			= qmp_v3_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_rx_tbl),
	.pcs_tbl		= qmp_v3_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_pcs_tbl),
	.clk_list		= qmp_v3_phy_clk_l,
	.num_clks		= ARRAY_SIZE(qmp_v3_phy_clk_l),
	.reset_list		= msm8996_usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_usb3phy_reset_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout,

	.start_ctrl		= SERDES_START | PCS_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.mask_pcs_ready		= PHYSTATUS,

	.has_pwrdn_delay	= true,
	.pwrdn_delay_min	= POWER_DOWN_DELAY_US_MIN,
	.pwrdn_delay_max	= POWER_DOWN_DELAY_US_MAX,

	.has_phy_dp_com_ctrl	= true,
	.is_dual_lane_phy	= true,
};

static const struct qmp_phy_cfg qmp_v3_usb3_uniphy_cfg = {
	.type			= PHY_TYPE_USB3,
	.nlanes			= 1,

	.serdes_tbl		= qmp_v3_usb3_uniphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_uniphy_serdes_tbl),
	.tx_tbl			= qmp_v3_usb3_uniphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_uniphy_tx_tbl),
	.rx_tbl			= qmp_v3_usb3_uniphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_uniphy_rx_tbl),
	.pcs_tbl		= qmp_v3_usb3_uniphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qmp_v3_usb3_uniphy_pcs_tbl),
	.clk_list		= qmp_v3_phy_clk_l,
	.num_clks		= ARRAY_SIZE(qmp_v3_phy_clk_l),
	.reset_list		= msm8996_usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_usb3phy_reset_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout,

	.start_ctrl		= SERDES_START | PCS_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.mask_pcs_ready		= PHYSTATUS,

	.has_pwrdn_delay	= true,
	.pwrdn_delay_min	= POWER_DOWN_DELAY_US_MIN,
	.pwrdn_delay_max	= POWER_DOWN_DELAY_US_MAX,
};

static const struct qmp_phy_cfg sdm845_ufsphy_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 2,

	.serdes_tbl		= sdm845_ufsphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sdm845_ufsphy_serdes_tbl),
	.tx_tbl			= sdm845_ufsphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sdm845_ufsphy_tx_tbl),
	.rx_tbl			= sdm845_ufsphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sdm845_ufsphy_rx_tbl),
	.pcs_tbl		= sdm845_ufsphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sdm845_ufsphy_pcs_tbl),
	.clk_list		= sdm845_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(sdm845_ufs_phy_clk_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= sdm845_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.mask_pcs_ready		= PCS_READY,

	.is_dual_lane_phy	= true,
	.no_pcs_sw_reset	= true,
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

static int qcom_qmp_phy_com_init(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *serdes = qmp->serdes;
	void __iomem *pcs = qphy->pcs;
	void __iomem *dp_com = qmp->dp_com;
	int ret, i;

	mutex_lock(&qmp->phy_mutex);
	if (qmp->init_count++) {
		mutex_unlock(&qmp->phy_mutex);
		return 0;
	}

	/* turn on regulator supplies */
	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		goto err_reg_enable;
	}

	for (i = 0; i < cfg->num_resets; i++) {
		ret = reset_control_assert(qmp->resets[i]);
		if (ret) {
			dev_err(qmp->dev, "%s reset assert failed\n",
				cfg->reset_list[i]);
			goto err_rst_assert;
		}
	}

	for (i = cfg->num_resets - 1; i >= 0; i--) {
		ret = reset_control_deassert(qmp->resets[i]);
		if (ret) {
			dev_err(qmp->dev, "%s reset deassert failed\n",
				qmp->cfg->reset_list[i]);
			goto err_rst;
		}
	}

	ret = clk_bulk_prepare_enable(cfg->num_clks, qmp->clks);
	if (ret) {
		dev_err(qmp->dev, "failed to enable clks, err=%d\n", ret);
		goto err_rst;
	}

	if (cfg->has_phy_dp_com_ctrl) {
		qphy_setbits(dp_com, QPHY_V3_DP_COM_POWER_DOWN_CTRL,
			     SW_PWRDN);
		/* override hardware control for reset of qmp phy */
		qphy_setbits(dp_com, QPHY_V3_DP_COM_RESET_OVRD_CTRL,
			     SW_DPPHY_RESET_MUX | SW_DPPHY_RESET |
			     SW_USB3PHY_RESET_MUX | SW_USB3PHY_RESET);

		qphy_setbits(dp_com, QPHY_V3_DP_COM_PHY_MODE_CTRL,
			     USB3_MODE | DP_MODE);

		/* bring both QMP USB and QMP DP PHYs PCS block out of reset */
		qphy_clrbits(dp_com, QPHY_V3_DP_COM_RESET_OVRD_CTRL,
			     SW_DPPHY_RESET_MUX | SW_DPPHY_RESET |
			     SW_USB3PHY_RESET_MUX | SW_USB3PHY_RESET);
	}

	if (cfg->has_phy_com_ctrl)
		qphy_setbits(serdes, cfg->regs[QPHY_COM_POWER_DOWN_CONTROL],
			     SW_PWRDN);
	else
		qphy_setbits(pcs, QPHY_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

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
			goto err_com_init;
		}
	}

	mutex_unlock(&qmp->phy_mutex);

	return 0;

err_com_init:
	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);
err_rst:
	while (++i < cfg->num_resets)
		reset_control_assert(qmp->resets[i]);
err_rst_assert:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
err_reg_enable:
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

	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

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
	void __iomem *dp_com = qmp->dp_com;
	void __iomem *status;
	unsigned int mask, val;
	int ret;

	dev_vdbg(qmp->dev, "Initializing QMP phy\n");

	ret = qcom_qmp_phy_com_init(qphy);
	if (ret)
		return ret;

	if (cfg->has_lane_rst) {
		ret = reset_control_deassert(qphy->lane_rst);
		if (ret) {
			dev_err(qmp->dev, "lane%d reset deassert failed\n",
				qphy->index);
			goto err_lane_rst;
		}
	}

	ret = clk_prepare_enable(qphy->pipe_clk);
	if (ret) {
		dev_err(qmp->dev, "pipe_clk enable failed err=%d\n", ret);
		goto err_clk_enable;
	}

	/* Tx, Rx, and PCS configurations */
	qcom_qmp_phy_configure(tx, cfg->regs, cfg->tx_tbl, cfg->tx_tbl_num);
	/* Configuration for other LANE for USB-DP combo PHY */
	if (cfg->is_dual_lane_phy)
		qcom_qmp_phy_configure(qphy->tx2, cfg->regs,
				       cfg->tx_tbl, cfg->tx_tbl_num);

	qcom_qmp_phy_configure(rx, cfg->regs, cfg->rx_tbl, cfg->rx_tbl_num);
	if (cfg->is_dual_lane_phy)
		qcom_qmp_phy_configure(qphy->rx2, cfg->regs,
				       cfg->rx_tbl, cfg->rx_tbl_num);

	qcom_qmp_phy_configure(pcs, cfg->regs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	/*
	 * UFS PHY requires the deassert of software reset before serdes start.
	 * For UFS PHYs that do not have software reset control bits, defer
	 * starting serdes until the power on callback.
	 */
	if ((cfg->type == PHY_TYPE_UFS) && cfg->no_pcs_sw_reset)
		goto out;

	/*
	 * Pull out PHY from POWER DOWN state.
	 * This is active low enable signal to power-down PHY.
	 */
	if(cfg->type == PHY_TYPE_PCIE)
		qphy_setbits(pcs, QPHY_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

	if (cfg->has_pwrdn_delay)
		usleep_range(cfg->pwrdn_delay_min, cfg->pwrdn_delay_max);

	/* Pull PHY out of reset state */
	qphy_clrbits(pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);
	if (cfg->has_phy_dp_com_ctrl)
		qphy_clrbits(dp_com, QPHY_V3_DP_COM_SW_RESET, SW_RESET);

	/* start SerDes and Phy-Coding-Sublayer */
	qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	status = pcs + cfg->regs[QPHY_PCS_READY_STATUS];
	mask = cfg->mask_pcs_ready;

	ret = readl_poll_timeout(status, val, !(val & mask), 1,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		goto err_pcs_ready;
	}
	qmp->phy_initialized = true;

out:
	return ret;

err_pcs_ready:
	clk_disable_unprepare(qphy->pipe_clk);
err_clk_enable:
	if (cfg->has_lane_rst)
		reset_control_assert(qphy->lane_rst);
err_lane_rst:
	qcom_qmp_phy_com_exit(qmp);

	return ret;
}

static int qcom_qmp_phy_exit(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	clk_disable_unprepare(qphy->pipe_clk);

	/* PHY reset */
	if (!cfg->no_pcs_sw_reset)
		qphy_setbits(qphy->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qphy->pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	/* Put PHY into POWER DOWN state: active low */
	qphy_clrbits(qphy->pcs, QPHY_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

	if (cfg->has_lane_rst)
		reset_control_assert(qphy->lane_rst);

	qcom_qmp_phy_com_exit(qmp);

	qmp->phy_initialized = false;

	return 0;
}

static int qcom_qmp_phy_poweron(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qphy->pcs;
	void __iomem *status;
	unsigned int mask, val;
	int ret = 0;

	if (cfg->type != PHY_TYPE_UFS)
		return 0;

	/*
	 * For UFS PHY that has not software reset control, serdes start
	 * should only happen when UFS driver explicitly calls phy_power_on
	 * after it deasserts software reset.
	 */
	if (cfg->no_pcs_sw_reset && !qmp->phy_initialized &&
	    (qmp->init_count != 0)) {
		/* start SerDes and Phy-Coding-Sublayer */
		qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

		status = pcs + cfg->regs[QPHY_PCS_READY_STATUS];
		mask = cfg->mask_pcs_ready;

		ret = readl_poll_timeout(status, val, !(val & mask), 1,
					 PHY_INIT_COMPLETE_TIMEOUT);
		if (ret) {
			dev_err(qmp->dev, "phy initialization timed-out\n");
			return ret;
		}
		qmp->phy_initialized = true;
	}

	return ret;
}

static int qcom_qmp_phy_set_mode(struct phy *phy,
				 enum phy_mode mode, int submode)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;

	qmp->mode = mode;

	return 0;
}

static void qcom_qmp_phy_enable_autonomous_mode(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qphy->pcs;
	void __iomem *pcs_misc = qphy->pcs_misc;
	u32 intr_mask;

	if (qmp->mode == PHY_MODE_USB_HOST_SS ||
	    qmp->mode == PHY_MODE_USB_DEVICE_SS)
		intr_mask = ARCVR_DTCT_EN | ALFPS_DTCT_EN;
	else
		intr_mask = ARCVR_DTCT_EN | ARCVR_DTCT_EVENT_SEL;

	/* Clear any pending interrupts status */
	qphy_setbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);
	/* Writing 1 followed by 0 clears the interrupt */
	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);

	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_AUTONOMOUS_MODE_CTRL],
		     ARCVR_DTCT_EN | ALFPS_DTCT_EN | ARCVR_DTCT_EVENT_SEL);

	/* Enable required PHY autonomous mode interrupts */
	qphy_setbits(pcs, cfg->regs[QPHY_PCS_AUTONOMOUS_MODE_CTRL], intr_mask);

	/* Enable i/o clamp_n for autonomous mode */
	if (pcs_misc)
		qphy_clrbits(pcs_misc, QPHY_V3_PCS_MISC_CLAMP_ENABLE, CLAMP_EN);
}

static void qcom_qmp_phy_disable_autonomous_mode(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qphy->pcs;
	void __iomem *pcs_misc = qphy->pcs_misc;

	/* Disable i/o clamp_n on resume for normal mode */
	if (pcs_misc)
		qphy_setbits(pcs_misc, QPHY_V3_PCS_MISC_CLAMP_ENABLE, CLAMP_EN);

	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_AUTONOMOUS_MODE_CTRL],
		     ARCVR_DTCT_EN | ARCVR_DTCT_EVENT_SEL | ALFPS_DTCT_EN);

	qphy_setbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);
	/* Writing 1 followed by 0 clears the interrupt */
	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);
}

static int __maybe_unused qcom_qmp_phy_runtime_suspend(struct device *dev)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	struct qmp_phy *qphy = qmp->phys[0];
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	dev_vdbg(dev, "Suspending QMP phy, mode:%d\n", qmp->mode);

	/* Supported only for USB3 PHY */
	if (cfg->type != PHY_TYPE_USB3)
		return 0;

	if (!qmp->phy_initialized) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	qcom_qmp_phy_enable_autonomous_mode(qphy);

	clk_disable_unprepare(qphy->pipe_clk);
	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);

	return 0;
}

static int __maybe_unused qcom_qmp_phy_runtime_resume(struct device *dev)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	struct qmp_phy *qphy = qmp->phys[0];
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int ret = 0;

	dev_vdbg(dev, "Resuming QMP phy, mode:%d\n", qmp->mode);

	/* Supported only for USB3 PHY */
	if (cfg->type != PHY_TYPE_USB3)
		return 0;

	if (!qmp->phy_initialized) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	ret = clk_bulk_prepare_enable(cfg->num_clks, qmp->clks);
	if (ret) {
		dev_err(qmp->dev, "failed to enable clks, err=%d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(qphy->pipe_clk);
	if (ret) {
		dev_err(dev, "pipe_clk enable failed, err=%d\n", ret);
		clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);
		return ret;
	}

	qcom_qmp_phy_disable_autonomous_mode(qphy);

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
	int num = qmp->cfg->num_clks;
	int i;

	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->clks[i].id = qmp->cfg->clk_list[i];

	return devm_clk_bulk_get(dev, num, qmp->clks);
}

static void phy_pipe_clk_release_provider(void *res)
{
	of_clk_del_provider(res);
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
		dev_err(qmp->dev, "%pOFn: No clock-output-names\n", np);
		return ret;
	}

	fixed = devm_kzalloc(qmp->dev, sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return -ENOMEM;

	init.ops = &clk_fixed_rate_ops;

	/* controllers using QMP phys use 125MHz pipe clock interface */
	fixed->fixed_rate = 125000000;
	fixed->hw.init = &init;

	ret = devm_clk_hw_register(qmp->dev, &fixed->hw);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, &fixed->hw);
	if (ret)
		return ret;

	/*
	 * Roll a devm action because the clock provider is the child node, but
	 * the child node is not actually a device.
	 */
	ret = devm_add_action(qmp->dev, phy_pipe_clk_release_provider, np);
	if (ret)
		phy_pipe_clk_release_provider(np);

	return ret;
}

static const struct phy_ops qcom_qmp_phy_gen_ops = {
	.init		= qcom_qmp_phy_init,
	.exit		= qcom_qmp_phy_exit,
	.power_on	= qcom_qmp_phy_poweron,
	.set_mode	= qcom_qmp_phy_set_mode,
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
	 * For dual lane PHYs: tx2 -> 3, rx2 -> 4, pcs_misc (optional) -> 5
	 * For single lane PHYs: pcs_misc (optional) -> 3.
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
	 * If this is a dual-lane PHY, then there should be registers for the
	 * second lane. Some old device trees did not specify this, so fall
	 * back to old legacy behavior of assuming they can be reached at an
	 * offset from the first lane.
	 */
	if (qmp->cfg->is_dual_lane_phy) {
		qphy->tx2 = of_iomap(np, 3);
		qphy->rx2 = of_iomap(np, 4);
		if (!qphy->tx2 || !qphy->rx2) {
			dev_warn(dev,
				 "Underspecified device tree, falling back to legacy register regions\n");

			/* In the old version, pcs_misc is at index 3. */
			qphy->pcs_misc = qphy->tx2;
			qphy->tx2 = qphy->tx + QMP_PHY_LEGACY_LANE_STRIDE;
			qphy->rx2 = qphy->rx + QMP_PHY_LEGACY_LANE_STRIDE;

		} else {
			qphy->pcs_misc = of_iomap(np, 5);
		}

	} else {
		qphy->pcs_misc = of_iomap(np, 3);
	}

	if (!qphy->pcs_misc)
		dev_vdbg(dev, "PHY pcs_misc-reg not used\n");

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
	}, {
		.compatible = "qcom,sdm845-qmp-usb3-phy",
		.data = &qmp_v3_usb3phy_cfg,
	}, {
		.compatible = "qcom,sdm845-qmp-usb3-uni-phy",
		.data = &qmp_v3_usb3_uniphy_cfg,
	}, {
		.compatible = "qcom,sdm845-qmp-ufs-phy",
		.data = &sdm845_ufsphy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_qmp_phy_of_match_table);

static const struct dev_pm_ops qcom_qmp_phy_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_qmp_phy_runtime_suspend,
			   qcom_qmp_phy_runtime_resume, NULL)
};

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

	/* Get the specific init parameters of QMP phy */
	qmp->cfg = of_device_get_match_data(dev);
	if (!qmp->cfg)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* per PHY serdes; usually located at base address */
	qmp->serdes = base;

	/* per PHY dp_com; if PHY has dp_com control block */
	if (qmp->cfg->has_phy_dp_com_ctrl) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "dp_com");
		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		qmp->dp_com = base;
	}

	mutex_init(&qmp->phy_mutex);

	ret = qcom_qmp_phy_clk_init(dev);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_reset_init(dev);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_vreg_init(dev);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get regulator supplies: %d\n",
				ret);
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
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Prevent runtime pm from being ON by default. Users can enable
	 * it using power/control in sysfs.
	 */
	pm_runtime_forbid(dev);

	for_each_available_child_of_node(dev->of_node, child) {
		/* Create per-lane phy */
		ret = qcom_qmp_phy_create(dev, child, id);
		if (ret) {
			dev_err(dev, "failed to create lane%d phy, %d\n",
				id, ret);
			pm_runtime_disable(dev);
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
			pm_runtime_disable(dev);
			return ret;
		}
		id++;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_info(dev, "Registered Qcom-QMP phy\n");
	else
		pm_runtime_disable(dev);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver qcom_qmp_phy_driver = {
	.probe		= qcom_qmp_phy_probe,
	.driver = {
		.name	= "qcom-qmp-phy",
		.pm	= &qcom_qmp_phy_pm_ops,
		.of_match_table = qcom_qmp_phy_of_match_table,
	},
};

module_platform_driver(qcom_qmp_phy_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP PHY driver");
MODULE_LICENSE("GPL v2");
