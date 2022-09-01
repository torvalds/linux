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
#define PHYSTATUS_4_20				BIT(7)
/* QPHY_PCS_READY_STATUS & QPHY_COM_PCS_READY_STATUS bit */
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

#define PHY_INIT_COMPLETE_TIMEOUT		10000
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
	bool in_layout;
	/*
	 * mask of lanes for which this register is written
	 * for cases when second lane needs different values
	 */
	u8 lane_mask;
};

#define QMP_PHY_INIT_CFG(o, v)		\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_L(o, v)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.in_layout = true,	\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_LANE(o, v, l)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = l,		\
	}

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* Common block control registers */
	QPHY_COM_SW_RESET,
	QPHY_COM_POWER_DOWN_CONTROL,
	QPHY_COM_START_CONTROL,
	QPHY_COM_PCS_READY_STATUS,
	/* PCS registers */
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_READY_STATUS,
	QPHY_PCS_STATUS,
	QPHY_PCS_AUTONOMOUS_MODE_CTRL,
	QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR,
	QPHY_PCS_LFPS_RXTERM_IRQ_STATUS,
	QPHY_PCS_POWER_DOWN_CONTROL,
	/* PCS_MISC registers */
	QPHY_PCS_MISC_TYPEC_CTRL,
	/* Keep last to ensure regs_layout arrays are properly initialized */
	QPHY_LAYOUT_SIZE
};

static const unsigned int msm8996_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x168,
};

static const unsigned int sdm845_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x160,
};

static const unsigned int sm6115_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x168,
};

static const unsigned int sm8150_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= QPHY_V4_PCS_UFS_PHY_START,
	[QPHY_PCS_READY_STATUS]		= QPHY_V4_PCS_UFS_READY_STATUS,
	[QPHY_SW_RESET]			= QPHY_V4_PCS_UFS_SW_RESET,
};

static const struct qmp_phy_init_tbl msm8996_ufs_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0xd7),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV_MODE1, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESETSM_CNTRL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER2, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x54),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE1_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE2_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE1, 0x98),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE1, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE1, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE1, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE1_MODE1, 0xd6),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE2_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE1, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE1, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE1, 0x00),
};

static const struct qmp_phy_init_tbl msm8996_ufs_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x02),
};

static const struct qmp_phy_init_tbl msm8996_ufs_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_LVL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_CNTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_INTERFACE_MODE, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_FASTLOCK_FO_GAIN, 0x0B),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_TERM_BW, 0x5b),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN1_LSB, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN1_MSB, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN2_LSB, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN2_MSB, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0E),
};

static const struct qmp_phy_init_tbl sm6115_ufsphy_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV_MODE1, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESETSM_CNTRL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER2, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE1_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE2_MODE0, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE1, 0x98),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE1, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE1, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE1, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE1_MODE1, 0xd6),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE2_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE1, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE1, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_INITVAL1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_INITVAL2, 0x00),

	/* Rate B */
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x44),
};

static const struct qmp_phy_init_tbl sm6115_ufsphy_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x06),
};

static const struct qmp_phy_init_tbl sm6115_ufsphy_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_LVL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_CNTRL, 0x0F),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_INTERFACE_MODE, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x1E),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_FASTLOCK_FO_GAIN, 0x0B),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_TERM_BW, 0x5B),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN1_LSB, 0xFF),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN1_MSB, 0x3F),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN2_LSB, 0xFF),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQ_GAIN2_MSB, 0x3F),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0D),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SVS_SO_GAIN_HALF, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SVS_SO_GAIN_QUARTER, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SVS_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x5B),
};

static const struct qmp_phy_init_tbl sm6115_ufsphy_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_PWM_GEAR_BAND, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_SIGDET_CTRL2, 0x6d),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_TX_LARGE_AMP_DRV_LVL, 0x0f),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_TX_SMALL_AMP_DRV_LVL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_MIN_STALL_NOCONFIG_TIME_CAP, 0x28),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_SYM_RESYNC_CTRL, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_TX_LARGE_AMP_POST_EMP_LVL, 0x12),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_TX_SMALL_AMP_POST_EMP_LVL, 0x0f),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_MIN_HIBERN8_TIME, 0x9a), /* 8 us */
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
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_RX_SIGDET_CTRL2, 0x6e),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_TX_LARGE_AMP_DRV_LVL, 0x0a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_TX_SMALL_AMP_DRV_LVL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_RX_SYM_RESYNC_CTRL, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_TX_MID_TERM_CTRL1, 0x43),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_RX_SIGDET_CTRL1, 0x0f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_RX_MIN_HIBERN8_TIME, 0x9a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_UFS_MULTI_LANE_CTRL1, 0x02),
};

static const struct qmp_phy_init_tbl sm8150_ufsphy_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_SYSCLK_EN_SEL, 0xd9),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_HSCLK_SEL, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_HSCLK_HS_SWITCH_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_LOCK_CMP_EN, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_VCO_TUNE_MAP, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_VCO_TUNE_INITVAL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_BIN_VCOCAL_HSCLK_SEL, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_CP_CTRL_MODE0, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_LOCK_CMP1_MODE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_LOCK_CMP2_MODE0, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0xac),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_DEC_START_MODE1, 0x98),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_CP_CTRL_MODE1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_PLL_CCTRL_MODE1, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_LOCK_CMP1_MODE1, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_LOCK_CMP2_MODE1, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_BIN_VCOCAL_CMP_CODE1_MODE1, 0xdd),
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_BIN_VCOCAL_CMP_CODE2_MODE1, 0x23),

	/* Rate B */
	QMP_PHY_INIT_CFG(QSERDES_V4_COM_VCO_TUNE_MAP, 0x06),
};

static const struct qmp_phy_init_tbl sm8150_ufsphy_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_PWM_GEAR_1_DIVIDER_BAND0_1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_PWM_GEAR_2_DIVIDER_BAND0_1, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_PWM_GEAR_3_DIVIDER_BAND0_1, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_PWM_GEAR_4_DIVIDER_BAND0_1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_LANE_MODE_1, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_V4_TX_TRAN_DRVR_EMP_EN, 0x0c),
};

static const struct qmp_phy_init_tbl sm8150_ufsphy_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_SIGDET_LVL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_SIGDET_CNTRL, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_SIGDET_DEGLITCH_CNTRL, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_BAND, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_FASTLOCK_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_PI_CONTROLS, 0xf1),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_FASTLOCK_COUNT_LOW, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_PI_CTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_FO_GAIN, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_UCDR_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_TERM_BW, 0x1b),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_EQU_ADAPTOR_CNTRL2, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_EQU_ADAPTOR_CNTRL3, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_EQU_ADAPTOR_CNTRL4, 0x1d),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_IDAC_MEASURE_TIME, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_IDAC_TSETTLE_LOW, 0xc0),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_IDAC_TSETTLE_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_00_LOW, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_00_HIGH, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_00_HIGH2, 0xf6),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_00_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_00_HIGH4, 0x3d),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_01_LOW, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_01_HIGH, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_01_HIGH2, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_01_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_01_HIGH4, 0xb1),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_10_LOW, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_10_HIGH, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_10_HIGH2, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_10_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V4_RX_RX_MODE_10_HIGH4, 0xb1),

};

static const struct qmp_phy_init_tbl sm8150_ufsphy_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_RX_SIGDET_CTRL2, 0x6d),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_TX_LARGE_AMP_DRV_LVL, 0x0a),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_TX_SMALL_AMP_DRV_LVL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_TX_MID_TERM_CTRL1, 0x43),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_DEBUG_BUS_CLKSEL, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_RX_MIN_HIBERN8_TIME, 0xff),
	QMP_PHY_INIT_CFG(QPHY_V4_PCS_UFS_MULTI_LANE_CTRL1, 0x02),
};

static const struct qmp_phy_init_tbl sm8350_ufsphy_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_SYSCLK_EN_SEL, 0xd9),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_HSCLK_SEL, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_HSCLK_HS_SWITCH_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_LOCK_CMP_EN, 0x42),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_VCO_TUNE_MAP, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_VCO_TUNE_INITVAL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_BIN_VCOCAL_HSCLK_SEL, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_CP_CTRL_MODE0, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_PLL_RCTRL_MODE0, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_PLL_CCTRL_MODE0, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_LOCK_CMP1_MODE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_LOCK_CMP2_MODE0, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0xac),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_DEC_START_MODE1, 0x98),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_CP_CTRL_MODE1, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_PLL_RCTRL_MODE1, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_PLL_CCTRL_MODE1, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_LOCK_CMP1_MODE1, 0x65),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_LOCK_CMP2_MODE1, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE1_MODE1, 0xdd),
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE2_MODE1, 0x23),

	/* Rate B */
	QMP_PHY_INIT_CFG(QSERDES_V5_COM_VCO_TUNE_MAP, 0x06),
};

static const struct qmp_phy_init_tbl sm8350_ufsphy_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_PWM_GEAR_1_DIVIDER_BAND0_1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_PWM_GEAR_2_DIVIDER_BAND0_1, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_PWM_GEAR_3_DIVIDER_BAND0_1, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_PWM_GEAR_4_DIVIDER_BAND0_1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_LANE_MODE_1, 0xf5),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_LANE_MODE_3, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_RES_CODE_LANE_OFFSET_TX, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_RES_CODE_LANE_OFFSET_RX, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_V5_TX_TRAN_DRVR_EMP_EN, 0x0c),
};

static const struct qmp_phy_init_tbl sm8350_ufsphy_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_SIGDET_LVL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_SIGDET_CNTRL, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_SIGDET_DEGLITCH_CNTRL, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_BAND, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_FASTLOCK_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x5a),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_PI_CONTROLS, 0xf1),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_FASTLOCK_COUNT_LOW, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_PI_CTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_FO_GAIN, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_UCDR_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_TERM_BW, 0x1b),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL1, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL2, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL3, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL4, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x17),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_IDAC_MEASURE_TIME, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_IDAC_TSETTLE_LOW, 0xc0),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_IDAC_TSETTLE_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_00_LOW, 0x6d),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_00_HIGH, 0x6d),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_00_HIGH2, 0xed),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_00_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_00_HIGH4, 0x3c),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_01_LOW, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_01_HIGH, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_01_HIGH2, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_01_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_01_HIGH4, 0xb7),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_10_LOW, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_10_HIGH, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_10_HIGH2, 0xc8),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_10_HIGH3, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_RX_MODE_10_HIGH4, 0xb7),
	QMP_PHY_INIT_CFG(QSERDES_V5_RX_DCC_CTRL1, 0x0c),
};

static const struct qmp_phy_init_tbl sm8350_ufsphy_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_RX_SIGDET_CTRL2, 0x6d),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_LARGE_AMP_DRV_LVL, 0x0a),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_SMALL_AMP_DRV_LVL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_MID_TERM_CTRL1, 0x43),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_DEBUG_BUS_CLKSEL, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_RX_MIN_HIBERN8_TIME, 0xff),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_PLL_CNTL, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TIMER_20US_CORECLK_STEPS_MSB, 0x16),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TIMER_20US_CORECLK_STEPS_LSB, 0xd8),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_PWM_GEAR_BAND, 0xaa),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_HS_GEAR_BAND, 0x06),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_TX_HSGEAR_CAPABILITY, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_RX_HSGEAR_CAPABILITY, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_RX_SIGDET_CTRL1, 0x0e),
	QMP_PHY_INIT_CFG(QPHY_V5_PCS_UFS_MULTI_LANE_CTRL1, 0x02),
};

struct qmp_phy;

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
	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;

	/* array of registers with different offsets */
	const unsigned int *regs;

	unsigned int start_ctrl;
	unsigned int pwrdn_ctrl;
	/* bit offset of PHYSTATUS in QPHY_PCS_STATUS register */
	unsigned int phy_status;

	/* true, if PHY has secondary tx/rx lanes to be configured */
	bool is_dual_lane_phy;

	/* true, if PCS block has no separate SW_RESET register */
	bool no_pcs_sw_reset;
};

/**
 * struct qmp_phy - per-lane phy descriptor
 *
 * @phy: generic phy
 * @cfg: phy specific configuration
 * @serdes: iomapped memory space for phy's serdes (i.e. PLL)
 * @tx: iomapped memory space for lane's tx
 * @rx: iomapped memory space for lane's rx
 * @pcs: iomapped memory space for lane's pcs
 * @tx2: iomapped memory space for second lane's tx (in dual lane PHYs)
 * @rx2: iomapped memory space for second lane's rx (in dual lane PHYs)
 * @pcs_misc: iomapped memory space for lane's pcs_misc
 * @index: lane index
 * @qmp: QMP phy to which this lane belongs
 * @mode: current PHY mode
 */
struct qmp_phy {
	struct phy *phy;
	const struct qmp_phy_cfg *cfg;
	void __iomem *serdes;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *pcs;
	void __iomem *tx2;
	void __iomem *rx2;
	void __iomem *pcs_misc;
	unsigned int index;
	struct qcom_qmp *qmp;
	enum phy_mode mode;
};

/**
 * struct qcom_qmp - structure holding QMP phy block attributes
 *
 * @dev: device
 *
 * @clks: array of clocks required by phy
 * @resets: array of resets required by phy
 * @vregs: regulator supplies bulk data
 *
 * @phys: array of per-lane phy descriptors
 * @ufs_reset: optional UFS PHY reset handle
 */
struct qcom_qmp {
	struct device *dev;

	struct clk_bulk_data *clks;
	struct regulator_bulk_data *vregs;

	struct qmp_phy **phys;

	struct reset_control *ufs_reset;
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
static const char * const msm8996_ufs_phy_clk_l[] = {
	"ref",
};

/* the primary usb3 phy on sm8250 doesn't have a ref clock */
static const char * const sm8450_ufs_phy_clk_l[] = {
	"qref", "ref", "ref_aux",
};

static const char * const sdm845_ufs_phy_clk_l[] = {
	"ref", "ref_aux",
};

/* list of regulators */
static const char * const qmp_phy_vreg_l[] = {
	"vdda-phy", "vdda-pll",
};

static const struct qmp_phy_cfg msm8996_ufs_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 1,

	.serdes_tbl		= msm8996_ufs_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(msm8996_ufs_serdes_tbl),
	.tx_tbl			= msm8996_ufs_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(msm8996_ufs_tx_tbl),
	.rx_tbl			= msm8996_ufs_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(msm8996_ufs_rx_tbl),

	.clk_list		= msm8996_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(msm8996_ufs_phy_clk_l),

	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),

	.regs			= msm8996_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.phy_status		= PHYSTATUS,

	.no_pcs_sw_reset	= true,
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
	.phy_status		= PHYSTATUS,

	.is_dual_lane_phy	= true,
	.no_pcs_sw_reset	= true,
};

static const struct qmp_phy_cfg sm6115_ufsphy_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 1,

	.serdes_tbl		= sm6115_ufsphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sm6115_ufsphy_serdes_tbl),
	.tx_tbl			= sm6115_ufsphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sm6115_ufsphy_tx_tbl),
	.rx_tbl			= sm6115_ufsphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sm6115_ufsphy_rx_tbl),
	.pcs_tbl		= sm6115_ufsphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sm6115_ufsphy_pcs_tbl),
	.clk_list		= sdm845_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(sdm845_ufs_phy_clk_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= sm6115_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,

	.no_pcs_sw_reset	= true,
};

static const struct qmp_phy_cfg sm8150_ufsphy_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 2,

	.serdes_tbl		= sm8150_ufsphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sm8150_ufsphy_serdes_tbl),
	.tx_tbl			= sm8150_ufsphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sm8150_ufsphy_tx_tbl),
	.rx_tbl			= sm8150_ufsphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sm8150_ufsphy_rx_tbl),
	.pcs_tbl		= sm8150_ufsphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sm8150_ufsphy_pcs_tbl),
	.clk_list		= sdm845_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(sdm845_ufs_phy_clk_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= sm8150_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.phy_status		= PHYSTATUS,

	.is_dual_lane_phy	= true,
};

static const struct qmp_phy_cfg sm8350_ufsphy_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 2,

	.serdes_tbl		= sm8350_ufsphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_serdes_tbl),
	.tx_tbl			= sm8350_ufsphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_tx_tbl),
	.rx_tbl			= sm8350_ufsphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_rx_tbl),
	.pcs_tbl		= sm8350_ufsphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_pcs_tbl),
	.clk_list		= sdm845_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(sdm845_ufs_phy_clk_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= sm8150_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.phy_status		= PHYSTATUS,

	.is_dual_lane_phy	= true,
};

static const struct qmp_phy_cfg sm8450_ufsphy_cfg = {
	.type			= PHY_TYPE_UFS,
	.nlanes			= 2,

	.serdes_tbl		= sm8350_ufsphy_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_serdes_tbl),
	.tx_tbl			= sm8350_ufsphy_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_tx_tbl),
	.rx_tbl			= sm8350_ufsphy_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_rx_tbl),
	.pcs_tbl		= sm8350_ufsphy_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(sm8350_ufsphy_pcs_tbl),
	.clk_list		= sm8450_ufs_phy_clk_l,
	.num_clks		= ARRAY_SIZE(sm8450_ufs_phy_clk_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= sm8150_ufsphy_regs_layout,

	.start_ctrl		= SERDES_START,
	.pwrdn_ctrl		= SW_PWRDN,
	.phy_status		= PHYSTATUS,

	.is_dual_lane_phy	= true,
};

static void qcom_qmp_phy_ufs_configure_lane(void __iomem *base,
					const unsigned int *regs,
					const struct qmp_phy_init_tbl tbl[],
					int num,
					u8 lane_mask)
{
	int i;
	const struct qmp_phy_init_tbl *t = tbl;

	if (!t)
		return;

	for (i = 0; i < num; i++, t++) {
		if (!(t->lane_mask & lane_mask))
			continue;

		if (t->in_layout)
			writel(t->val, base + regs[t->offset]);
		else
			writel(t->val, base + t->offset);
	}
}

static void qcom_qmp_phy_ufs_configure(void __iomem *base,
				   const unsigned int *regs,
				   const struct qmp_phy_init_tbl tbl[],
				   int num)
{
	qcom_qmp_phy_ufs_configure_lane(base, regs, tbl, num, 0xff);
}

static int qcom_qmp_phy_ufs_serdes_init(struct qmp_phy *qphy)
{
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *serdes = qphy->serdes;
	const struct qmp_phy_init_tbl *serdes_tbl = cfg->serdes_tbl;
	int serdes_tbl_num = cfg->serdes_tbl_num;

	qcom_qmp_phy_ufs_configure(serdes, cfg->regs, serdes_tbl, serdes_tbl_num);

	return 0;
}

static int qcom_qmp_phy_ufs_com_init(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *pcs = qphy->pcs;
	int ret;

	/* turn on regulator supplies */
	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(cfg->num_clks, qmp->clks);
	if (ret)
		goto err_disable_regulators;

	if (cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL])
		qphy_setbits(pcs,
			     cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			     cfg->pwrdn_ctrl);
	else
		qphy_setbits(pcs, QPHY_V2_PCS_POWER_DOWN_CONTROL,
			     cfg->pwrdn_ctrl);

	return 0;

err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return ret;
}

static int qcom_qmp_phy_ufs_com_exit(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;

	reset_control_assert(qmp->ufs_reset);

	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return 0;
}

static int qcom_qmp_phy_ufs_init(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	int ret;
	dev_vdbg(qmp->dev, "Initializing QMP phy\n");

	if (cfg->no_pcs_sw_reset) {
		/*
		 * Get UFS reset, which is delayed until now to avoid a
		 * circular dependency where UFS needs its PHY, but the PHY
		 * needs this UFS reset.
		 */
		if (!qmp->ufs_reset) {
			qmp->ufs_reset =
				devm_reset_control_get_exclusive(qmp->dev,
								 "ufsphy");

			if (IS_ERR(qmp->ufs_reset)) {
				ret = PTR_ERR(qmp->ufs_reset);
				dev_err(qmp->dev,
					"failed to get UFS reset: %d\n",
					ret);

				qmp->ufs_reset = NULL;
				return ret;
			}
		}

		ret = reset_control_assert(qmp->ufs_reset);
		if (ret)
			return ret;
	}

	ret = qcom_qmp_phy_ufs_com_init(qphy);
	if (ret)
		return ret;

	return 0;
}

static int qcom_qmp_phy_ufs_power_on(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *tx = qphy->tx;
	void __iomem *rx = qphy->rx;
	void __iomem *pcs = qphy->pcs;
	void __iomem *status;
	unsigned int mask, val, ready;
	int ret;

	qcom_qmp_phy_ufs_serdes_init(qphy);

	/* Tx, Rx, and PCS configurations */
	qcom_qmp_phy_ufs_configure_lane(tx, cfg->regs,
				    cfg->tx_tbl, cfg->tx_tbl_num, 1);

	/* Configuration for other LANE for USB-DP combo PHY */
	if (cfg->is_dual_lane_phy) {
		qcom_qmp_phy_ufs_configure_lane(qphy->tx2, cfg->regs,
					    cfg->tx_tbl, cfg->tx_tbl_num, 2);
	}

	qcom_qmp_phy_ufs_configure_lane(rx, cfg->regs,
				    cfg->rx_tbl, cfg->rx_tbl_num, 1);

	if (cfg->is_dual_lane_phy) {
		qcom_qmp_phy_ufs_configure_lane(qphy->rx2, cfg->regs,
					    cfg->rx_tbl, cfg->rx_tbl_num, 2);
	}

	qcom_qmp_phy_ufs_configure(pcs, cfg->regs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	ret = reset_control_deassert(qmp->ufs_reset);
	if (ret)
		return ret;

	/* Pull PHY out of reset state */
	if (!cfg->no_pcs_sw_reset)
		qphy_clrbits(pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);
	/* start SerDes and Phy-Coding-Sublayer */
	qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	status = pcs + cfg->regs[QPHY_PCS_READY_STATUS];
	mask = PCS_READY;
	ready = PCS_READY;

	ret = readl_poll_timeout(status, val, (val & mask) == ready, 10,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		return ret;
	}

	return 0;
}

static int qcom_qmp_phy_ufs_power_off(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qphy->cfg;

	/* PHY reset */
	if (!cfg->no_pcs_sw_reset)
		qphy_setbits(qphy->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qphy->pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	/* Put PHY into POWER DOWN state: active low */
	if (cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL]) {
		qphy_clrbits(qphy->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			     cfg->pwrdn_ctrl);
	} else {
		qphy_clrbits(qphy->pcs, QPHY_V2_PCS_POWER_DOWN_CONTROL,
				cfg->pwrdn_ctrl);
	}

	return 0;
}

static int qcom_qmp_phy_ufs_exit(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);

	qcom_qmp_phy_ufs_com_exit(qphy);

	return 0;
}

static int qcom_qmp_phy_ufs_enable(struct phy *phy)
{
	int ret;

	ret = qcom_qmp_phy_ufs_init(phy);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_ufs_power_on(phy);
	if (ret)
		qcom_qmp_phy_ufs_exit(phy);

	return ret;
}

static int qcom_qmp_phy_ufs_disable(struct phy *phy)
{
	int ret;

	ret = qcom_qmp_phy_ufs_power_off(phy);
	if (ret)
		return ret;
	return qcom_qmp_phy_ufs_exit(phy);
}

static int qcom_qmp_phy_ufs_set_mode(struct phy *phy,
				 enum phy_mode mode, int submode)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);

	qphy->mode = mode;

	return 0;
}

static int qcom_qmp_phy_ufs_vreg_init(struct device *dev, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int num = cfg->num_vregs;
	int i;

	qmp->vregs = devm_kcalloc(dev, num, sizeof(*qmp->vregs), GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->vregs[i].supply = cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, qmp->vregs);
}

static int qcom_qmp_phy_ufs_clk_init(struct device *dev, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int num = cfg->num_clks;
	int i;

	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->clks[i].id = cfg->clk_list[i];

	return devm_clk_bulk_get(dev, num, qmp->clks);
}

static const struct phy_ops qcom_qmp_ufs_ops = {
	.power_on	= qcom_qmp_phy_ufs_enable,
	.power_off	= qcom_qmp_phy_ufs_disable,
	.set_mode	= qcom_qmp_phy_ufs_set_mode,
	.owner		= THIS_MODULE,
};

static
int qcom_qmp_phy_ufs_create(struct device *dev, struct device_node *np, int id,
			void __iomem *serdes, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	struct phy *generic_phy;
	struct qmp_phy *qphy;
	int ret;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->cfg = cfg;
	qphy->serdes = serdes;
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
	if (cfg->is_dual_lane_phy) {
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

	generic_phy = devm_phy_create(dev, np, &qcom_qmp_ufs_ops);
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

static const struct of_device_id qcom_qmp_phy_ufs_of_match_table[] = {
	{
		.compatible = "qcom,msm8996-qmp-ufs-phy",
		.data = &msm8996_ufs_cfg,
	}, {
		.compatible = "qcom,msm8998-qmp-ufs-phy",
		.data = &sdm845_ufsphy_cfg,
	}, {
		.compatible = "qcom,sc8180x-qmp-ufs-phy",
		.data = &sm8150_ufsphy_cfg,
	}, {
		.compatible = "qcom,sc8280xp-qmp-ufs-phy",
		.data = &sm8350_ufsphy_cfg,
	}, {
		.compatible = "qcom,sdm845-qmp-ufs-phy",
		.data = &sdm845_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm6115-qmp-ufs-phy",
		.data = &sm6115_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm6350-qmp-ufs-phy",
		.data = &sdm845_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm8150-qmp-ufs-phy",
		.data = &sm8150_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm8250-qmp-ufs-phy",
		.data = &sm8150_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm8350-qmp-ufs-phy",
		.data = &sm8350_ufsphy_cfg,
	}, {
		.compatible = "qcom,sm8450-qmp-ufs-phy",
		.data = &sm8450_ufsphy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_qmp_phy_ufs_of_match_table);

static int qcom_qmp_phy_ufs_probe(struct platform_device *pdev)
{
	struct qcom_qmp *qmp;
	struct device *dev = &pdev->dev;
	struct device_node *child;
	struct phy_provider *phy_provider;
	void __iomem *serdes;
	const struct qmp_phy_cfg *cfg = NULL;
	int num, id;
	int ret;

	qmp = devm_kzalloc(dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = dev;
	dev_set_drvdata(dev, qmp);

	/* Get the specific init parameters of QMP phy */
	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -EINVAL;

	/* per PHY serdes; usually located at base address */
	serdes = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(serdes))
		return PTR_ERR(serdes);

	ret = qcom_qmp_phy_ufs_clk_init(dev, cfg);
	if (ret)
		return ret;

	ret = qcom_qmp_phy_ufs_vreg_init(dev, cfg);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get regulator supplies: %d\n",
				ret);
		return ret;
	}

	num = of_get_available_child_count(dev->of_node);
	/* do we have a rogue child node ? */
	if (num > 1)
		return -EINVAL;

	qmp->phys = devm_kcalloc(dev, num, sizeof(*qmp->phys), GFP_KERNEL);
	if (!qmp->phys)
		return -ENOMEM;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Prevent runtime pm from being ON by default. Users can enable
	 * it using power/control in sysfs.
	 */
	pm_runtime_forbid(dev);

	id = 0;
	for_each_available_child_of_node(dev->of_node, child) {
		/* Create per-lane phy */
		ret = qcom_qmp_phy_ufs_create(dev, child, id, serdes, cfg);
		if (ret) {
			dev_err(dev, "failed to create lane%d phy, %d\n",
				id, ret);
			goto err_node_put;
		}

		id++;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_info(dev, "Registered Qcom-QMP phy\n");
	else
		pm_runtime_disable(dev);

	return PTR_ERR_OR_ZERO(phy_provider);

err_node_put:
	pm_runtime_disable(dev);
	of_node_put(child);
	return ret;
}

static struct platform_driver qcom_qmp_phy_ufs_driver = {
	.probe		= qcom_qmp_phy_ufs_probe,
	.driver = {
		.name	= "qcom-qmp-ufs-phy",
		.of_match_table = qcom_qmp_phy_ufs_of_match_table,
	},
};

module_platform_driver(qcom_qmp_phy_ufs_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP UFS PHY driver");
MODULE_LICENSE("GPL v2");
