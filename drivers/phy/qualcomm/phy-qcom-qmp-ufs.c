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

#include "phy-qcom-qmp.h"

/* QPHY_SW_RESET bit */
#define SW_RESET				BIT(0)
/* QPHY_POWER_DOWN_CONTROL */
#define SW_PWRDN				BIT(0)
/* QPHY_START_CONTROL bits */
#define SERDES_START				BIT(0)
#define PCS_START				BIT(1)
/* QPHY_PCS_READY_STATUS bit */
#define PCS_READY				BIT(0)

#define PHY_INIT_COMPLETE_TIMEOUT		10000

struct qmp_phy_init_tbl {
	unsigned int offset;
	unsigned int val;
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

#define QMP_PHY_INIT_CFG_LANE(o, v, l)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = l,		\
	}

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* PCS registers */
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_READY_STATUS,
	QPHY_PCS_POWER_DOWN_CONTROL,
	/* Keep last to ensure regs_layout arrays are properly initialized */
	QPHY_LAYOUT_SIZE
};

static const unsigned int msm8996_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x168,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= 0x04,
};

static const unsigned int sdm845_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x160,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= 0x04,
};

static const unsigned int sm6115_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= 0x00,
	[QPHY_PCS_READY_STATUS]		= 0x168,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= 0x04,
};

static const unsigned int sm8150_ufsphy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_START_CTRL]		= QPHY_V4_PCS_UFS_PHY_START,
	[QPHY_PCS_READY_STATUS]		= QPHY_V4_PCS_UFS_READY_STATUS,
	[QPHY_SW_RESET]			= QPHY_V4_PCS_UFS_SW_RESET,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= QPHY_V4_PCS_UFS_POWER_DOWN_CONTROL,
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

struct qmp_ufs_offsets {
	u16 serdes;
	u16 pcs;
	u16 tx;
	u16 rx;
	u16 tx2;
	u16 rx2;
};

/* struct qmp_phy_cfg - per-PHY initialization config */
struct qmp_phy_cfg {
	int lanes;

	const struct qmp_ufs_offsets *offsets;

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

	/* true, if PCS block has no separate SW_RESET register */
	bool no_pcs_sw_reset;
};

struct qmp_ufs {
	struct device *dev;

	const struct qmp_phy_cfg *cfg;

	void __iomem *serdes;
	void __iomem *pcs;
	void __iomem *pcs_misc;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *tx2;
	void __iomem *rx2;

	struct clk_bulk_data *clks;
	struct regulator_bulk_data *vregs;
	struct reset_control *ufs_reset;

	struct phy *phy;
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

static const struct qmp_ufs_offsets qmp_ufs_offsets_v5 = {
	.serdes		= 0,
	.pcs		= 0xc00,
	.tx		= 0x400,
	.rx		= 0x600,
	.tx2		= 0x800,
	.rx2		= 0xa00,
};

static const struct qmp_phy_cfg msm8996_ufs_cfg = {
	.lanes			= 1,

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

	.no_pcs_sw_reset	= true,
};

static const struct qmp_phy_cfg sc8280xp_ufsphy_cfg = {
	.lanes			= 2,

	.offsets		= &qmp_ufs_offsets_v5,

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
};

static const struct qmp_phy_cfg sdm845_ufsphy_cfg = {
	.lanes			= 2,

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

	.no_pcs_sw_reset	= true,
};

static const struct qmp_phy_cfg sm6115_ufsphy_cfg = {
	.lanes			= 1,

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

	.no_pcs_sw_reset	= true,
};

static const struct qmp_phy_cfg sm8150_ufsphy_cfg = {
	.lanes			= 2,

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
};

static const struct qmp_phy_cfg sm8350_ufsphy_cfg = {
	.lanes			= 2,

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
};

static const struct qmp_phy_cfg sm8450_ufsphy_cfg = {
	.lanes			= 2,

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
};

static void qmp_ufs_configure_lane(void __iomem *base,
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

		writel(t->val, base + t->offset);
	}
}

static void qmp_ufs_configure(void __iomem *base,
				   const struct qmp_phy_init_tbl tbl[],
				   int num)
{
	qmp_ufs_configure_lane(base, tbl, num, 0xff);
}

static int qmp_ufs_serdes_init(struct qmp_ufs *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *serdes = qmp->serdes;
	const struct qmp_phy_init_tbl *serdes_tbl = cfg->serdes_tbl;
	int serdes_tbl_num = cfg->serdes_tbl_num;

	qmp_ufs_configure(serdes, serdes_tbl, serdes_tbl_num);

	return 0;
}

static int qmp_ufs_com_init(struct qmp_ufs *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qmp->pcs;
	int ret;

	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(cfg->num_clks, qmp->clks);
	if (ret)
		goto err_disable_regulators;

	qphy_setbits(pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL], SW_PWRDN);

	return 0;

err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return ret;
}

static int qmp_ufs_com_exit(struct qmp_ufs *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	reset_control_assert(qmp->ufs_reset);

	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return 0;
}

static int qmp_ufs_init(struct phy *phy)
{
	struct qmp_ufs *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
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

	ret = qmp_ufs_com_init(qmp);
	if (ret)
		return ret;

	return 0;
}

static int qmp_ufs_power_on(struct phy *phy)
{
	struct qmp_ufs *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *tx = qmp->tx;
	void __iomem *rx = qmp->rx;
	void __iomem *pcs = qmp->pcs;
	void __iomem *status;
	unsigned int val;
	int ret;

	qmp_ufs_serdes_init(qmp);

	/* Tx, Rx, and PCS configurations */
	qmp_ufs_configure_lane(tx, cfg->tx_tbl, cfg->tx_tbl_num, 1);
	qmp_ufs_configure_lane(rx, cfg->rx_tbl, cfg->rx_tbl_num, 1);

	if (cfg->lanes >= 2) {
		qmp_ufs_configure_lane(qmp->tx2, cfg->tx_tbl, cfg->tx_tbl_num, 2);
		qmp_ufs_configure_lane(qmp->rx2, cfg->rx_tbl, cfg->rx_tbl_num, 2);
	}

	qmp_ufs_configure(pcs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	ret = reset_control_deassert(qmp->ufs_reset);
	if (ret)
		return ret;

	/* Pull PHY out of reset state */
	if (!cfg->no_pcs_sw_reset)
		qphy_clrbits(pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* start SerDes */
	qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], SERDES_START);

	status = pcs + cfg->regs[QPHY_PCS_READY_STATUS];
	ret = readl_poll_timeout(status, val, (val & PCS_READY), 200,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		return ret;
	}

	return 0;
}

static int qmp_ufs_power_off(struct phy *phy)
{
	struct qmp_ufs *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	/* PHY reset */
	if (!cfg->no_pcs_sw_reset)
		qphy_setbits(qmp->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_START_CTRL], SERDES_START);

	/* Put PHY into POWER DOWN state: active low */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			SW_PWRDN);

	return 0;
}

static int qmp_ufs_exit(struct phy *phy)
{
	struct qmp_ufs *qmp = phy_get_drvdata(phy);

	qmp_ufs_com_exit(qmp);

	return 0;
}

static int qmp_ufs_enable(struct phy *phy)
{
	int ret;

	ret = qmp_ufs_init(phy);
	if (ret)
		return ret;

	ret = qmp_ufs_power_on(phy);
	if (ret)
		qmp_ufs_exit(phy);

	return ret;
}

static int qmp_ufs_disable(struct phy *phy)
{
	int ret;

	ret = qmp_ufs_power_off(phy);
	if (ret)
		return ret;
	return qmp_ufs_exit(phy);
}

static const struct phy_ops qcom_qmp_ufs_phy_ops = {
	.power_on	= qmp_ufs_enable,
	.power_off	= qmp_ufs_disable,
	.owner		= THIS_MODULE,
};

static int qmp_ufs_vreg_init(struct qmp_ufs *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int num = cfg->num_vregs;
	int i;

	qmp->vregs = devm_kcalloc(dev, num, sizeof(*qmp->vregs), GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->vregs[i].supply = cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, qmp->vregs);
}

static int qmp_ufs_clk_init(struct qmp_ufs *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int num = cfg->num_clks;
	int i;

	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->clks[i].id = cfg->clk_list[i];

	return devm_clk_bulk_get(dev, num, qmp->clks);
}

static int qmp_ufs_parse_dt_legacy(struct qmp_ufs *qmp, struct device_node *np)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;

	qmp->serdes = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qmp->serdes))
		return PTR_ERR(qmp->serdes);

	/*
	 * Get memory resources for the PHY:
	 * Resources are indexed as: tx -> 0; rx -> 1; pcs -> 2.
	 * For dual lane PHYs: tx2 -> 3, rx2 -> 4, pcs_misc (optional) -> 5
	 * For single lane PHYs: pcs_misc (optional) -> 3.
	 */
	qmp->tx = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(qmp->tx))
		return PTR_ERR(qmp->tx);

	qmp->rx = devm_of_iomap(dev, np, 1, NULL);
	if (IS_ERR(qmp->rx))
		return PTR_ERR(qmp->rx);

	qmp->pcs = devm_of_iomap(dev, np, 2, NULL);
	if (IS_ERR(qmp->pcs))
		return PTR_ERR(qmp->pcs);

	if (cfg->lanes >= 2) {
		qmp->tx2 = devm_of_iomap(dev, np, 3, NULL);
		if (IS_ERR(qmp->tx2))
			return PTR_ERR(qmp->tx2);

		qmp->rx2 = devm_of_iomap(dev, np, 4, NULL);
		if (IS_ERR(qmp->rx2))
			return PTR_ERR(qmp->rx2);

		qmp->pcs_misc = devm_of_iomap(dev, np, 5, NULL);
	} else {
		qmp->pcs_misc = devm_of_iomap(dev, np, 3, NULL);
	}

	if (IS_ERR(qmp->pcs_misc))
		dev_vdbg(dev, "PHY pcs_misc-reg not used\n");

	return 0;
}

static int qmp_ufs_parse_dt(struct qmp_ufs *qmp)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_ufs_offsets *offs = cfg->offsets;
	void __iomem *base;

	if (!offs)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	qmp->serdes = base + offs->serdes;
	qmp->pcs = base + offs->pcs;
	qmp->tx = base + offs->tx;
	qmp->rx = base + offs->rx;

	if (cfg->lanes >= 2) {
		qmp->tx2 = base + offs->tx2;
		qmp->rx2 = base + offs->rx2;
	}

	return 0;
}

static int qmp_ufs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct device_node *np;
	struct qmp_ufs *qmp;
	int ret;

	qmp = devm_kzalloc(dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = dev;

	qmp->cfg = of_device_get_match_data(dev);
	if (!qmp->cfg)
		return -EINVAL;

	ret = qmp_ufs_clk_init(qmp);
	if (ret)
		return ret;

	ret = qmp_ufs_vreg_init(qmp);
	if (ret)
		return ret;

	/* Check for legacy binding with child node. */
	np = of_get_next_available_child(dev->of_node, NULL);
	if (np) {
		ret = qmp_ufs_parse_dt_legacy(qmp, np);
	} else {
		np = of_node_get(dev->of_node);
		ret = qmp_ufs_parse_dt(qmp);
	}
	if (ret)
		goto err_node_put;

	qmp->phy = devm_phy_create(dev, np, &qcom_qmp_ufs_phy_ops);
	if (IS_ERR(qmp->phy)) {
		ret = PTR_ERR(qmp->phy);
		dev_err(dev, "failed to create PHY: %d\n", ret);
		goto err_node_put;
	}

	phy_set_drvdata(qmp->phy, qmp);

	of_node_put(np);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);

err_node_put:
	of_node_put(np);
	return ret;
}

static const struct of_device_id qmp_ufs_of_match_table[] = {
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
		.data = &sc8280xp_ufsphy_cfg,
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
MODULE_DEVICE_TABLE(of, qmp_ufs_of_match_table);

static struct platform_driver qmp_ufs_driver = {
	.probe		= qmp_ufs_probe,
	.driver = {
		.name	= "qcom-qmp-ufs-phy",
		.of_match_table = qmp_ufs_of_match_table,
	},
};

module_platform_driver(qmp_ufs_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP UFS PHY driver");
MODULE_LICENSE("GPL v2");
