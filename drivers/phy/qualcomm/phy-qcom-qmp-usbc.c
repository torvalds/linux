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
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_mux.h>

#include "phy-qcom-qmp-common.h"

#include "phy-qcom-qmp.h"
#include "phy-qcom-qmp-pcs-misc-v3.h"

#define PHY_INIT_COMPLETE_TIMEOUT		10000

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* PCS registers */
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_STATUS,
	QPHY_PCS_AUTONOMOUS_MODE_CTRL,
	QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR,
	QPHY_PCS_POWER_DOWN_CONTROL,
	/* Keep last to ensure regs_layout arrays are properly initialized */
	QPHY_LAYOUT_SIZE
};

static const unsigned int qmp_v3_usb3phy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_SW_RESET]			= QPHY_V3_PCS_SW_RESET,
	[QPHY_START_CTRL]		= QPHY_V3_PCS_START_CONTROL,
	[QPHY_PCS_STATUS]		= QPHY_V3_PCS_PCS_STATUS,
	[QPHY_PCS_AUTONOMOUS_MODE_CTRL]	= QPHY_V3_PCS_AUTONOMOUS_MODE_CTRL,
	[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR] = QPHY_V3_PCS_LFPS_RXTERM_IRQ_CLEAR,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= QPHY_V3_PCS_POWER_DOWN_CONTROL,
};

static const unsigned int qmp_v3_usb3phy_regs_layout_qcm2290[QPHY_LAYOUT_SIZE] = {
	[QPHY_SW_RESET]			= QPHY_V3_PCS_SW_RESET,
	[QPHY_START_CTRL]		= QPHY_V3_PCS_START_CONTROL,
	[QPHY_PCS_STATUS]		= QPHY_V3_PCS_PCS_STATUS,
	[QPHY_PCS_AUTONOMOUS_MODE_CTRL]	= QPHY_V3_PCS_AUTONOMOUS_MODE_CTRL,
	[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR] = QPHY_V3_PCS_LFPS_RXTERM_IRQ_CLEAR,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= QPHY_V3_PCS_POWER_DOWN_CONTROL,
};

static const struct qmp_phy_init_tbl msm8998_usb3_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BIAS_EN_CLKBUFLR_EN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SYS_CLK_CTRL, 0x06),
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
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_PLL_IVCO, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_INTEGLOOP_INITVAL, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_CMN_MODE, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE1, 0x85),
	QMP_PHY_INIT_CFG(QSERDES_V3_COM_SSC_STEP_SIZE2, 0x07),
};

static const struct qmp_phy_init_tbl msm8998_usb3_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_HIGHZ_DRVR_EN, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_LANE_MODE_1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_TX, 0x00),
};

static const struct qmp_phy_init_tbl msm8998_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x43),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x75),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_LOW, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_PI_CONTROLS, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_GAIN, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_ENABLES, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_VGA_CAL_CNTRL2, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_MODE_00, 0x05),
};

static const struct qmp_phy_init_tbl msm8998_usb3_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL2, 0x83),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_L, 0x09),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_H_TOL, 0xa2),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_MAN_CODE, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL1, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG1, 0xd1),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG2, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG3, 0x47),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_POWER_STATE_CONFIG2, 0x1b),
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
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V3, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V4, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V4, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_LS, 0x15),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_LS, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RATE_SLEW_CNTRL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TSYNC_RSYNC_TIME, 0x44),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_L, 0xe7),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_H, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_L, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_H, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_LVL, 0x8a),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_WAIT_TIME, 0x75),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LFPS_TX_ECSTART_EQTLOCK, 0x86),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_RUN_TIME, 0x13),
};

static const struct qmp_phy_init_tbl qcm2290_usb3_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESETSM_CNTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESETSM_CNTRL2, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_CFG, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE1, 0xde),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE2, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_INITVAL, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CTRL_BY_PSM, 0x01),
};

static const struct qmp_phy_init_tbl qcm2290_usb3_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_HIGHZ_DRVR_EN, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RCV_DETECT_LVL_2, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_LANE_MODE_1, 0xc6),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_TX, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_TX_RES_CODE_LANE_OFFSET_RX, 0x00),
};

static const struct qmp_phy_init_tbl qcm2290_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_PI_CONTROLS, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_LOW, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_GAIN, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x75),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_VGA_CAL_CNTRL2, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_ENABLES, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_MODE_00, 0x00),
};

/* the only difference is QSERDES_V3_RX_UCDR_PI_CONTROLS */
static const struct qmp_phy_init_tbl sdm660_usb3_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_FO_GAIN, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_PI_CONTROLS, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_LOW, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_FO_GAIN, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_GAIN, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x75),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4e),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQU_ADAPTOR_CNTRL4, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_VGA_CAL_CNTRL2, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_CNTRL, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_DEGLITCH_CNTRL, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_SIGDET_ENABLES, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V3_RX_RX_MODE_00, 0x00),
};

static const struct qmp_phy_init_tbl qcm2290_usb3_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXMGN_V0, 0x9f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M6DB_V0, 0x17),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TXDEEMPH_M3P5DB_V0, 0x0f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL2, 0x83),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNTRL1, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_L, 0x09),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_CNT_VAL_H_TOL, 0xa2),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_FLL_MAN_CODE, 0x85),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG1, 0xd1),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG2, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LOCK_DETECT_CONFIG3, 0x47),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_WAIT_TIME, 0x75),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RXEQTRAINING_RUN_TIME, 0x13),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_LFPS_TX_ECSTART_EQTLOCK, 0x86),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x04),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_TSYNC_RSYNC_TIME, 0x44),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_L, 0xe7),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_P1U2_H, 0x03),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_L, 0x40),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RCVR_DTCT_DLY_U3_H, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V3_PCS_RX_SIGDET_LVL, 0x88),
};

struct qmp_usbc_offsets {
	u16 serdes;
	u16 pcs;
	u16 pcs_misc;
	u16 tx;
	u16 rx;
	/* for PHYs with >= 2 lanes */
	u16 tx2;
	u16 rx2;
};

/* struct qmp_phy_cfg - per-PHY initialization config */
struct qmp_phy_cfg {
	const struct qmp_usbc_offsets *offsets;

	/* Init sequence for PHY blocks - serdes, tx, rx, pcs */
	const struct qmp_phy_init_tbl *serdes_tbl;
	int serdes_tbl_num;
	const struct qmp_phy_init_tbl *tx_tbl;
	int tx_tbl_num;
	const struct qmp_phy_init_tbl *rx_tbl;
	int rx_tbl_num;
	const struct qmp_phy_init_tbl *pcs_tbl;
	int pcs_tbl_num;

	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;

	/* array of registers with different offsets */
	const unsigned int *regs;
};

struct qmp_usbc {
	struct device *dev;

	const struct qmp_phy_cfg *cfg;

	void __iomem *serdes;
	void __iomem *pcs;
	void __iomem *pcs_misc;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *tx2;
	void __iomem *rx2;

	struct regmap *tcsr_map;
	u32 vls_clamp_reg;

	struct clk *pipe_clk;
	struct clk_bulk_data *clks;
	int num_clks;
	int num_resets;
	struct reset_control_bulk_data *resets;
	struct regulator_bulk_data *vregs;

	struct mutex phy_mutex;

	enum phy_mode mode;
	unsigned int usb_init_count;

	struct phy *phy;

	struct clk_fixed_rate pipe_clk_fixed;

	struct typec_switch_dev *sw;
	enum typec_orientation orientation;
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
static const char * const qmp_usbc_phy_clk_l[] = {
	"aux", "cfg_ahb", "ref", "com_aux",
};

/* list of resets */
static const char * const usb3phy_legacy_reset_l[] = {
	"phy", "common",
};

static const char * const usb3phy_reset_l[] = {
	"phy_phy", "phy",
};

/* list of regulators */
static const char * const qmp_phy_vreg_l[] = {
	"vdda-phy", "vdda-pll",
};

static const struct qmp_usbc_offsets qmp_usbc_offsets_v3_qcm2290 = {
	.serdes		= 0x0,
	.pcs		= 0xc00,
	.pcs_misc	= 0xa00,
	.tx		= 0x200,
	.rx		= 0x400,
	.tx2		= 0x600,
	.rx2		= 0x800,
};

static const struct qmp_phy_cfg msm8998_usb3phy_cfg = {
	.offsets		= &qmp_usbc_offsets_v3_qcm2290,

	.serdes_tbl             = msm8998_usb3_serdes_tbl,
	.serdes_tbl_num         = ARRAY_SIZE(msm8998_usb3_serdes_tbl),
	.tx_tbl                 = msm8998_usb3_tx_tbl,
	.tx_tbl_num             = ARRAY_SIZE(msm8998_usb3_tx_tbl),
	.rx_tbl                 = msm8998_usb3_rx_tbl,
	.rx_tbl_num             = ARRAY_SIZE(msm8998_usb3_rx_tbl),
	.pcs_tbl                = msm8998_usb3_pcs_tbl,
	.pcs_tbl_num            = ARRAY_SIZE(msm8998_usb3_pcs_tbl),
	.vreg_list              = qmp_phy_vreg_l,
	.num_vregs              = ARRAY_SIZE(qmp_phy_vreg_l),
	.regs                   = qmp_v3_usb3phy_regs_layout,
};

static const struct qmp_phy_cfg qcm2290_usb3phy_cfg = {
	.offsets		= &qmp_usbc_offsets_v3_qcm2290,

	.serdes_tbl		= qcm2290_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qcm2290_usb3_serdes_tbl),
	.tx_tbl			= qcm2290_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_tx_tbl),
	.rx_tbl			= qcm2290_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_rx_tbl),
	.pcs_tbl		= qcm2290_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qcm2290_usb3_pcs_tbl),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout_qcm2290,
};

static const struct qmp_phy_cfg sdm660_usb3phy_cfg = {
	.offsets		= &qmp_usbc_offsets_v3_qcm2290,

	.serdes_tbl		= qcm2290_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qcm2290_usb3_serdes_tbl),
	.tx_tbl			= qcm2290_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_tx_tbl),
	.rx_tbl			= sdm660_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(sdm660_usb3_rx_tbl),
	.pcs_tbl		= qcm2290_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qcm2290_usb3_pcs_tbl),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout_qcm2290,
};

static int qmp_usbc_init(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qmp->pcs;
	u32 val = 0;
	int ret;

	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		return ret;
	}

	ret = reset_control_bulk_assert(qmp->num_resets, qmp->resets);
	if (ret) {
		dev_err(qmp->dev, "reset assert failed\n");
		goto err_disable_regulators;
	}

	ret = reset_control_bulk_deassert(qmp->num_resets, qmp->resets);
	if (ret) {
		dev_err(qmp->dev, "reset deassert failed\n");
		goto err_disable_regulators;
	}

	ret = clk_bulk_prepare_enable(qmp->num_clks, qmp->clks);
	if (ret)
		goto err_assert_reset;

	qphy_setbits(pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL], SW_PWRDN);

#define SW_PORTSELECT_VAL			BIT(0)
#define SW_PORTSELECT_MUX			BIT(1)
	/* Use software based port select and switch on typec orientation */
	val = SW_PORTSELECT_MUX;
	if (qmp->orientation == TYPEC_ORIENTATION_REVERSE)
		val |= SW_PORTSELECT_VAL;
	writel(val, qmp->pcs_misc);

	return 0;

err_assert_reset:
	reset_control_bulk_assert(qmp->num_resets, qmp->resets);
err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return ret;
}

static int qmp_usbc_exit(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	reset_control_bulk_assert(qmp->num_resets, qmp->resets);

	clk_bulk_disable_unprepare(qmp->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return 0;
}

static int qmp_usbc_power_on(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *status;
	unsigned int val;
	int ret;

	qmp_configure(qmp->dev, qmp->serdes, cfg->serdes_tbl,
		      cfg->serdes_tbl_num);

	ret = clk_prepare_enable(qmp->pipe_clk);
	if (ret) {
		dev_err(qmp->dev, "pipe_clk enable failed err=%d\n", ret);
		return ret;
	}

	/* Tx, Rx, and PCS configurations */
	qmp_configure_lane(qmp->dev, qmp->tx, cfg->tx_tbl, cfg->tx_tbl_num, 1);
	qmp_configure_lane(qmp->dev, qmp->rx, cfg->rx_tbl, cfg->rx_tbl_num, 1);

	qmp_configure_lane(qmp->dev, qmp->tx2, cfg->tx_tbl, cfg->tx_tbl_num, 2);
	qmp_configure_lane(qmp->dev, qmp->rx2, cfg->rx_tbl, cfg->rx_tbl_num, 2);

	qmp_configure(qmp->dev, qmp->pcs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	/* Pull PHY out of reset state */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* start SerDes and Phy-Coding-Sublayer */
	qphy_setbits(qmp->pcs, cfg->regs[QPHY_START_CTRL], SERDES_START | PCS_START);

	status = qmp->pcs + cfg->regs[QPHY_PCS_STATUS];
	ret = readl_poll_timeout(status, val, !(val & PHYSTATUS), 200,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		goto err_disable_pipe_clk;
	}

	return 0;

err_disable_pipe_clk:
	clk_disable_unprepare(qmp->pipe_clk);

	return ret;
}

static int qmp_usbc_power_off(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	clk_disable_unprepare(qmp->pipe_clk);

	/* PHY reset */
	qphy_setbits(qmp->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_START_CTRL],
			SERDES_START | PCS_START);

	/* Put PHY into POWER DOWN state: active low */
	qphy_clrbits(qmp->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			SW_PWRDN);

	return 0;
}

static int qmp_usbc_enable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	int ret;

	mutex_lock(&qmp->phy_mutex);

	ret = qmp_usbc_init(phy);
	if (ret)
		goto out_unlock;

	ret = qmp_usbc_power_on(phy);
	if (ret) {
		qmp_usbc_exit(phy);
		goto out_unlock;
	}

	qmp->usb_init_count++;
out_unlock:
	mutex_unlock(&qmp->phy_mutex);

	return ret;
}

static int qmp_usbc_disable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	int ret;

	qmp->usb_init_count--;
	ret = qmp_usbc_power_off(phy);
	if (ret)
		return ret;
	return qmp_usbc_exit(phy);
}

static int qmp_usbc_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);

	qmp->mode = mode;

	return 0;
}

static const struct phy_ops qmp_usbc_phy_ops = {
	.init		= qmp_usbc_enable,
	.exit		= qmp_usbc_disable,
	.set_mode	= qmp_usbc_set_mode,
	.owner		= THIS_MODULE,
};

static void qmp_usbc_enable_autonomous_mode(struct qmp_usbc *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qmp->pcs;
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
	if (qmp->tcsr_map && qmp->vls_clamp_reg)
		regmap_write(qmp->tcsr_map, qmp->vls_clamp_reg, 1);
}

static void qmp_usbc_disable_autonomous_mode(struct qmp_usbc *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *pcs = qmp->pcs;

	/* Disable i/o clamp_n on resume for normal mode */
	if (qmp->tcsr_map && qmp->vls_clamp_reg)
		regmap_write(qmp->tcsr_map, qmp->vls_clamp_reg, 0);

	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_AUTONOMOUS_MODE_CTRL],
		     ARCVR_DTCT_EN | ARCVR_DTCT_EVENT_SEL | ALFPS_DTCT_EN);

	qphy_setbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);
	/* Writing 1 followed by 0 clears the interrupt */
	qphy_clrbits(pcs, cfg->regs[QPHY_PCS_LFPS_RXTERM_IRQ_CLEAR], IRQ_CLEAR);
}

static int __maybe_unused qmp_usbc_runtime_suspend(struct device *dev)
{
	struct qmp_usbc *qmp = dev_get_drvdata(dev);

	dev_vdbg(dev, "Suspending QMP phy, mode:%d\n", qmp->mode);

	if (!qmp->phy->init_count) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	qmp_usbc_enable_autonomous_mode(qmp);

	clk_disable_unprepare(qmp->pipe_clk);
	clk_bulk_disable_unprepare(qmp->num_clks, qmp->clks);

	return 0;
}

static int __maybe_unused qmp_usbc_runtime_resume(struct device *dev)
{
	struct qmp_usbc *qmp = dev_get_drvdata(dev);
	int ret = 0;

	dev_vdbg(dev, "Resuming QMP phy, mode:%d\n", qmp->mode);

	if (!qmp->phy->init_count) {
		dev_vdbg(dev, "PHY not initialized, bailing out\n");
		return 0;
	}

	ret = clk_bulk_prepare_enable(qmp->num_clks, qmp->clks);
	if (ret)
		return ret;

	ret = clk_prepare_enable(qmp->pipe_clk);
	if (ret) {
		dev_err(dev, "pipe_clk enable failed, err=%d\n", ret);
		clk_bulk_disable_unprepare(qmp->num_clks, qmp->clks);
		return ret;
	}

	qmp_usbc_disable_autonomous_mode(qmp);

	return 0;
}

static const struct dev_pm_ops qmp_usbc_pm_ops = {
	SET_RUNTIME_PM_OPS(qmp_usbc_runtime_suspend,
			   qmp_usbc_runtime_resume, NULL)
};

static int qmp_usbc_vreg_init(struct qmp_usbc *qmp)
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

static int qmp_usbc_reset_init(struct qmp_usbc *qmp,
			      const char *const *reset_list,
			      int num_resets)
{
	struct device *dev = qmp->dev;
	int i;
	int ret;

	qmp->resets = devm_kcalloc(dev, num_resets,
				   sizeof(*qmp->resets), GFP_KERNEL);
	if (!qmp->resets)
		return -ENOMEM;

	for (i = 0; i < num_resets; i++)
		qmp->resets[i].id = reset_list[i];

	qmp->num_resets = num_resets;

	ret = devm_reset_control_bulk_get_exclusive(dev, num_resets, qmp->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get resets\n");

	return 0;
}

static int qmp_usbc_clk_init(struct qmp_usbc *qmp)
{
	struct device *dev = qmp->dev;
	int num = ARRAY_SIZE(qmp_usbc_phy_clk_l);
	int i;

	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->clks[i].id = qmp_usbc_phy_clk_l[i];

	qmp->num_clks = num;

	return devm_clk_bulk_get_optional(dev, num, qmp->clks);
}

static void phy_clk_release_provider(void *res)
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
static int phy_pipe_clk_register(struct qmp_usbc *qmp, struct device_node *np)
{
	struct clk_fixed_rate *fixed = &qmp->pipe_clk_fixed;
	struct clk_init_data init = { };
	int ret;

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret) {
		dev_err(qmp->dev, "%pOFn: No clock-output-names\n", np);
		return ret;
	}

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
	return devm_add_action_or_reset(qmp->dev, phy_clk_release_provider, np);
}

#if IS_ENABLED(CONFIG_TYPEC)
static int qmp_usbc_typec_switch_set(struct typec_switch_dev *sw,
				      enum typec_orientation orientation)
{
	struct qmp_usbc *qmp = typec_switch_get_drvdata(sw);

	if (orientation == qmp->orientation || orientation == TYPEC_ORIENTATION_NONE)
		return 0;

	mutex_lock(&qmp->phy_mutex);
	qmp->orientation = orientation;

	if (qmp->usb_init_count) {
		qmp_usbc_power_off(qmp->phy);
		qmp_usbc_exit(qmp->phy);

		qmp_usbc_init(qmp->phy);
		qmp_usbc_power_on(qmp->phy);
	}

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static void qmp_usbc_typec_unregister(void *data)
{
	struct qmp_usbc *qmp = data;

	typec_switch_unregister(qmp->sw);
}

static int qmp_usbc_typec_switch_register(struct qmp_usbc *qmp)
{
	struct typec_switch_desc sw_desc = {};
	struct device *dev = qmp->dev;

	sw_desc.drvdata = qmp;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = qmp_usbc_typec_switch_set;
	qmp->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(qmp->sw)) {
		dev_err(dev, "Unable to register typec switch: %pe\n", qmp->sw);
		return PTR_ERR(qmp->sw);
	}

	return devm_add_action_or_reset(dev, qmp_usbc_typec_unregister, qmp);
}
#else
static int qmp_usbc_typec_switch_register(struct qmp_usbc *qmp)
{
	return 0;
}
#endif

static int qmp_usbc_parse_dt_legacy(struct qmp_usbc *qmp, struct device_node *np)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	struct device *dev = qmp->dev;
	int ret;

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

	qmp->tx2 = devm_of_iomap(dev, np, 3, NULL);
	if (IS_ERR(qmp->tx2))
		return PTR_ERR(qmp->tx2);

	qmp->rx2 = devm_of_iomap(dev, np, 4, NULL);
	if (IS_ERR(qmp->rx2))
		return PTR_ERR(qmp->rx2);

	qmp->pcs_misc = devm_of_iomap(dev, np, 5, NULL);
	if (IS_ERR(qmp->pcs_misc)) {
		dev_vdbg(dev, "PHY pcs_misc-reg not used\n");
		qmp->pcs_misc = NULL;
	}

	qmp->pipe_clk = devm_get_clk_from_child(dev, np, NULL);
	if (IS_ERR(qmp->pipe_clk)) {
		return dev_err_probe(dev, PTR_ERR(qmp->pipe_clk),
				     "failed to get pipe clock\n");
	}

	ret = devm_clk_bulk_get_all(qmp->dev, &qmp->clks);
	if (ret < 0)
		return ret;

	qmp->num_clks = ret;

	ret = qmp_usbc_reset_init(qmp, usb3phy_legacy_reset_l,
				 ARRAY_SIZE(usb3phy_legacy_reset_l));
	if (ret)
		return ret;

	return 0;
}

static int qmp_usbc_parse_dt(struct qmp_usbc *qmp)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_usbc_offsets *offs = cfg->offsets;
	struct device *dev = qmp->dev;
	void __iomem *base;
	int ret;

	if (!offs)
		return -EINVAL;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	qmp->serdes = base + offs->serdes;
	qmp->pcs = base + offs->pcs;
	if (offs->pcs_misc)
		qmp->pcs_misc = base + offs->pcs_misc;
	qmp->tx = base + offs->tx;
	qmp->rx = base + offs->rx;

	qmp->tx2 = base + offs->tx2;
	qmp->rx2 = base + offs->rx2;

	ret = qmp_usbc_clk_init(qmp);
	if (ret)
		return ret;

	qmp->pipe_clk = devm_clk_get(dev, "pipe");
	if (IS_ERR(qmp->pipe_clk)) {
		return dev_err_probe(dev, PTR_ERR(qmp->pipe_clk),
				     "failed to get pipe clock\n");
	}

	ret = qmp_usbc_reset_init(qmp, usb3phy_reset_l,
				 ARRAY_SIZE(usb3phy_reset_l));
	if (ret)
		return ret;

	return 0;
}

static int qmp_usbc_parse_vls_clamp(struct qmp_usbc *qmp)
{
	struct of_phandle_args tcsr_args;
	struct device *dev = qmp->dev;
	int ret;

	/*  for backwards compatibility ignore if there is no property */
	ret = of_parse_phandle_with_fixed_args(dev->of_node, "qcom,tcsr-reg", 1, 0,
					       &tcsr_args);
	if (ret == -ENOENT)
		return 0;
	else if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to parse qcom,tcsr-reg\n");

	qmp->tcsr_map = syscon_node_to_regmap(tcsr_args.np);
	of_node_put(tcsr_args.np);
	if (IS_ERR(qmp->tcsr_map))
		return PTR_ERR(qmp->tcsr_map);

	qmp->vls_clamp_reg = tcsr_args.args[0];

	return 0;
}

static int qmp_usbc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct device_node *np;
	struct qmp_usbc *qmp;
	int ret;

	qmp = devm_kzalloc(dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = dev;
	dev_set_drvdata(dev, qmp);

	qmp->orientation = TYPEC_ORIENTATION_NORMAL;

	qmp->cfg = of_device_get_match_data(dev);
	if (!qmp->cfg)
		return -EINVAL;

	mutex_init(&qmp->phy_mutex);

	ret = qmp_usbc_vreg_init(qmp);
	if (ret)
		return ret;

	ret = qmp_usbc_typec_switch_register(qmp);
	if (ret)
		return ret;

	ret = qmp_usbc_parse_vls_clamp(qmp);
	if (ret)
		return ret;

	/* Check for legacy binding with child node. */
	np = of_get_child_by_name(dev->of_node, "phy");
	if (np) {
		ret = qmp_usbc_parse_dt_legacy(qmp, np);
	} else {
		np = of_node_get(dev->of_node);
		ret = qmp_usbc_parse_dt(qmp);
	}
	if (ret)
		goto err_node_put;

	pm_runtime_set_active(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		goto err_node_put;
	/*
	 * Prevent runtime pm from being ON by default. Users can enable
	 * it using power/control in sysfs.
	 */
	pm_runtime_forbid(dev);

	ret = phy_pipe_clk_register(qmp, np);
	if (ret)
		goto err_node_put;

	qmp->phy = devm_phy_create(dev, np, &qmp_usbc_phy_ops);
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

static const struct of_device_id qmp_usbc_of_match_table[] = {
	{
		.compatible = "qcom,msm8998-qmp-usb3-phy",
		.data = &msm8998_usb3phy_cfg,
	}, {
		.compatible = "qcom,qcm2290-qmp-usb3-phy",
		.data = &qcm2290_usb3phy_cfg,
	}, {
		.compatible = "qcom,sdm660-qmp-usb3-phy",
		.data = &sdm660_usb3phy_cfg,
	}, {
		.compatible = "qcom,sm6115-qmp-usb3-phy",
		.data = &qcm2290_usb3phy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qmp_usbc_of_match_table);

static struct platform_driver qmp_usbc_driver = {
	.probe		= qmp_usbc_probe,
	.driver = {
		.name	= "qcom-qmp-usbc-phy",
		.pm	= &qmp_usbc_pm_ops,
		.of_match_table = qmp_usbc_of_match_table,
	},
};

module_platform_driver(qmp_usbc_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP USB-C PHY driver");
MODULE_LICENSE("GPL");
