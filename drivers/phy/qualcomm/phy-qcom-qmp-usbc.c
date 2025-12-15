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
#include <dt-bindings/phy/phy-qcom-qmp.h>

#include "phy-qcom-qmp-common.h"

#include "phy-qcom-qmp.h"
#include "phy-qcom-qmp-pcs-misc-v3.h"

#include "phy-qcom-qmp-dp-phy.h"
#include "phy-qcom-qmp-dp-phy-v2.h"

#define PHY_INIT_COMPLETE_TIMEOUT		10000
#define SW_PORTSELECT_VAL			BIT(0)
#define SW_PORTSELECT_MUX			BIT(1)

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

static const struct qmp_phy_init_tbl qmp_v2_dp_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x37),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x0e),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_CTRL, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_BUF_ENABLE, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE2_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x02),
};

static const struct qmp_phy_init_tbl qmp_v2_dp_serdes_tbl_rbr[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x2c),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x69),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0xbf),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x21),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
};

static const struct qmp_phy_init_tbl qmp_v2_dp_serdes_tbl_hbr[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x69),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x3f),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x38),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
};

static const struct qmp_phy_init_tbl qmp_v2_dp_serdes_tbl_hbr2[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x8c),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x7f),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x70),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
};

static const struct qmp_phy_init_tbl qmp_v2_dp_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TRANSCEIVER_BIAS_EN, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_VMODE_CTRL1, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_PRE_STALL_LDO_BOOST_EN, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_INTERFACE_SELECT, 0x3d),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_CLKBUF_ENABLE, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_RESET_TSYNC_EN, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TRAN_DRVR_EMP_EN, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_PARRATE_REC_DETECT_IDLE_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TX_INTERFACE_MODE, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TX_EMP_POST1_LVL, 0x2b),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TX_DRV_LVL, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_TX_BAND, 0x4),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_RES_CODE_LANE_OFFSET_TX, 0x12),
	QMP_PHY_INIT_CFG(QSERDES_V2_TX_RES_CODE_LANE_OFFSET_RX, 0x12),
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

	u16 dp_serdes;
	u16 dp_txa;
	u16 dp_txb;
	u16 dp_dp_phy;
};

struct qmp_usbc;
struct qmp_phy_cfg {
	const struct qmp_usbc_offsets *offsets;

	/* Init sequence for USB PHY blocks - serdes, tx, rx, pcs */
	const struct qmp_phy_init_tbl *serdes_tbl;
	int serdes_tbl_num;
	const struct qmp_phy_init_tbl *tx_tbl;
	int tx_tbl_num;
	const struct qmp_phy_init_tbl *rx_tbl;
	int rx_tbl_num;
	const struct qmp_phy_init_tbl *pcs_tbl;
	int pcs_tbl_num;

	/* Init sequence for DP PHY blocks - serdes, tx, rbr, hbr, hbr2 */
	const struct qmp_phy_init_tbl *dp_serdes_tbl;
	int dp_serdes_tbl_num;
	const struct qmp_phy_init_tbl *dp_tx_tbl;
	int dp_tx_tbl_num;
	const struct qmp_phy_init_tbl *serdes_tbl_rbr;
	int serdes_tbl_rbr_num;
	const struct qmp_phy_init_tbl *serdes_tbl_hbr;
	int serdes_tbl_hbr_num;
	const struct qmp_phy_init_tbl *serdes_tbl_hbr2;
	int serdes_tbl_hbr2_num;

	const u8 (*swing_tbl)[4][4];
	const u8 (*pre_emphasis_tbl)[4][4];

	/* DP PHY callbacks */
	void (*dp_aux_init)(struct qmp_usbc *qmp);
	void (*configure_dp_tx)(struct qmp_usbc *qmp);
	int (*configure_dp_phy)(struct qmp_usbc *qmp);
	int (*calibrate_dp_phy)(struct qmp_usbc *qmp);

	const char * const *reset_list;
	int num_resets;
	const struct regulator_bulk_data *vreg_list;
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
	void __iomem *dp_dp_phy;
	void __iomem *dp_tx;
	void __iomem *dp_tx2;
	void __iomem *dp_serdes;

	struct clk *pipe_clk;
	struct clk_fixed_rate pipe_clk_fixed;

	struct clk_hw dp_link_hw;
	struct clk_hw dp_pixel_hw;
	struct clk_bulk_data *clks;
	int num_clks;
	int num_resets;
	struct reset_control_bulk_data *resets;
	struct regulator_bulk_data *vregs;

	struct regmap *tcsr_map;
	u32 vls_clamp_reg;
	u32 dp_phy_mode_reg;

	struct mutex phy_mutex;

	struct phy *usb_phy;
	enum phy_mode mode;
	unsigned int usb_init_count;

	struct phy *dp_phy;
	unsigned int dp_aux_cfg;
	struct phy_configure_opts_dp dp_opts;
	unsigned int dp_init_count;

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

static const char * const usb3dpphy_reset_l[] = {
	"phy_phy", "dp_phy",
};

static const struct regulator_bulk_data qmp_phy_msm8998_vreg_l[] = {
	{ .supply = "vdda-phy", .init_load_uA = 68600 },
	{ .supply = "vdda-pll", .init_load_uA = 14200 },
};

static const struct regulator_bulk_data qmp_phy_sm2290_vreg_l[] = {
	{ .supply = "vdda-phy", .init_load_uA = 66100 },
	{ .supply = "vdda-pll", .init_load_uA = 13300 },
};

static const struct regulator_bulk_data qmp_phy_qcs615_vreg_l[] = {
	{ .supply = "vdda-phy", .init_load_uA = 50000 },
	{ .supply = "vdda-pll", .init_load_uA = 20000 },
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

static const struct qmp_usbc_offsets qmp_usbc_usb3dp_offsets_qcs615 = {
	.serdes		= 0x0,
	.pcs		= 0xc00,
	.pcs_misc	= 0xa00,
	.tx		= 0x200,
	.rx		= 0x400,
	.tx2		= 0x600,
	.rx2		= 0x800,
	.dp_serdes	= 0x1c00,
	.dp_txa		= 0x1400,
	.dp_txb		= 0x1800,
	.dp_dp_phy	= 0x1000,
};

static const u8 qmp_v2_dp_pre_emphasis_hbr2_rbr[4][4] = {
	{0x00, 0x0b, 0x12, 0xff},
	{0x00, 0x0a, 0x12, 0xff},
	{0x00, 0x0c, 0xff, 0xff},
	{0xff, 0xff, 0xff, 0xff}
};

static const u8 qmp_v2_dp_voltage_swing_hbr2_rbr[4][4] = {
	{0x07, 0x0f, 0x14, 0xff},
	{0x11, 0x1d, 0x1f, 0xff},
	{0x18, 0x1f, 0xff, 0xff},
	{0xff, 0xff, 0xff, 0xff}
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
	.reset_list		= usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(usb3phy_reset_l),
	.vreg_list              = qmp_phy_msm8998_vreg_l,
	.num_vregs              = ARRAY_SIZE(qmp_phy_msm8998_vreg_l),
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
	.reset_list		= usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(usb3phy_reset_l),
	.vreg_list		= qmp_phy_sm2290_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_sm2290_vreg_l),
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
	.reset_list		= usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(usb3phy_reset_l),
	.vreg_list		= qmp_phy_msm8998_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_msm8998_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout_qcm2290,
};

static const struct qmp_phy_cfg qcs615_usb3phy_cfg = {
	.offsets		= &qmp_usbc_offsets_v3_qcm2290,

	.serdes_tbl		= qcm2290_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qcm2290_usb3_serdes_tbl),
	.tx_tbl			= qcm2290_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_tx_tbl),
	.rx_tbl			= qcm2290_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_rx_tbl),
	.pcs_tbl		= qcm2290_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qcm2290_usb3_pcs_tbl),
	.reset_list		= usb3phy_reset_l,
	.num_resets		= ARRAY_SIZE(usb3phy_reset_l),
	.vreg_list		= qmp_phy_qcs615_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_qcs615_vreg_l),
	.regs			= qmp_v3_usb3phy_regs_layout_qcm2290,
};

static void qmp_v2_dp_aux_init(struct qmp_usbc *qmp);
static void qmp_v2_configure_dp_tx(struct qmp_usbc *qmp);
static int qmp_v2_configure_dp_phy(struct qmp_usbc *qmp);
static int qmp_v2_calibrate_dp_phy(struct qmp_usbc *qmp);

static const struct qmp_phy_cfg qcs615_usb3dp_phy_cfg = {
	.offsets		= &qmp_usbc_usb3dp_offsets_qcs615,

	.serdes_tbl		= qcm2290_usb3_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(qcm2290_usb3_serdes_tbl),
	.tx_tbl			= qcm2290_usb3_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_tx_tbl),
	.rx_tbl			= qcm2290_usb3_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(qcm2290_usb3_rx_tbl),
	.pcs_tbl		= qcm2290_usb3_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(qcm2290_usb3_pcs_tbl),

	.regs			= qmp_v3_usb3phy_regs_layout_qcm2290,

	.dp_serdes_tbl		= qmp_v2_dp_serdes_tbl,
	.dp_serdes_tbl_num	= ARRAY_SIZE(qmp_v2_dp_serdes_tbl),
	.dp_tx_tbl		= qmp_v2_dp_tx_tbl,
	.dp_tx_tbl_num		= ARRAY_SIZE(qmp_v2_dp_tx_tbl),

	.serdes_tbl_rbr		= qmp_v2_dp_serdes_tbl_rbr,
	.serdes_tbl_rbr_num	= ARRAY_SIZE(qmp_v2_dp_serdes_tbl_rbr),
	.serdes_tbl_hbr		= qmp_v2_dp_serdes_tbl_hbr,
	.serdes_tbl_hbr_num	= ARRAY_SIZE(qmp_v2_dp_serdes_tbl_hbr),
	.serdes_tbl_hbr2	= qmp_v2_dp_serdes_tbl_hbr2,
	.serdes_tbl_hbr2_num	= ARRAY_SIZE(qmp_v2_dp_serdes_tbl_hbr2),

	.swing_tbl		= &qmp_v2_dp_voltage_swing_hbr2_rbr,
	.pre_emphasis_tbl	= &qmp_v2_dp_pre_emphasis_hbr2_rbr,

	.dp_aux_init		= qmp_v2_dp_aux_init,
	.configure_dp_tx	= qmp_v2_configure_dp_tx,
	.configure_dp_phy	= qmp_v2_configure_dp_phy,
	.calibrate_dp_phy	= qmp_v2_calibrate_dp_phy,

	.reset_list		= usb3dpphy_reset_l,
	.num_resets		= ARRAY_SIZE(usb3dpphy_reset_l),
	.vreg_list		= qmp_phy_qcs615_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_qcs615_vreg_l),
};

static void qmp_usbc_set_phy_mode(struct qmp_usbc *qmp, bool is_dp)
{
	if (qmp->tcsr_map && qmp->dp_phy_mode_reg)
		regmap_write(qmp->tcsr_map, qmp->dp_phy_mode_reg, is_dp);
}

static int qmp_usbc_com_init(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
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

	return 0;

err_assert_reset:
	reset_control_bulk_assert(qmp->num_resets, qmp->resets);
err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return ret;
}

static int qmp_usbc_com_exit(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	reset_control_bulk_assert(qmp->num_resets, qmp->resets);

	clk_bulk_disable_unprepare(qmp->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	return 0;
}

static void qmp_v2_dp_aux_init(struct qmp_usbc *qmp)
{
	writel(DP_PHY_PD_CTL_AUX_PWRDN |
	       DP_PHY_PD_CTL_LANE_0_1_PWRDN | DP_PHY_PD_CTL_LANE_2_3_PWRDN |
	       DP_PHY_PD_CTL_PLL_PWRDN,
	       qmp->dp_dp_phy + QSERDES_DP_PHY_PD_CTL);

	writel(DP_PHY_PD_CTL_PWRDN | DP_PHY_PD_CTL_AUX_PWRDN |
	       DP_PHY_PD_CTL_LANE_0_1_PWRDN | DP_PHY_PD_CTL_LANE_2_3_PWRDN |
	       DP_PHY_PD_CTL_PLL_PWRDN,
	       qmp->dp_dp_phy + QSERDES_DP_PHY_PD_CTL);

	writel(0x00, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG0);
	writel(0x13, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG1);
	writel(0x00, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG2);
	writel(0x00, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG3);
	writel(0x0a, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG4);
	writel(0x26, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG5);
	writel(0x0a, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG6);
	writel(0x03, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG7);
	writel(0xbb, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG8);
	writel(0x03, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG9);
	qmp->dp_aux_cfg = 0;

	writel(PHY_AUX_STOP_ERR_MASK | PHY_AUX_DEC_ERR_MASK |
	       PHY_AUX_SYNC_ERR_MASK | PHY_AUX_ALIGN_ERR_MASK |
	       PHY_AUX_REQ_ERR_MASK,
	       qmp->dp_dp_phy + QSERDES_V2_DP_PHY_AUX_INTERRUPT_MASK);
}

static int qmp_v2_configure_dp_swing(struct qmp_usbc *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct phy_configure_opts_dp *dp_opts = &qmp->dp_opts;
	void __iomem *tx = qmp->dp_tx;
	void __iomem *tx2 = qmp->dp_tx2;
	unsigned int v_level = 0, p_level = 0;
	u8 voltage_swing_cfg, pre_emphasis_cfg;
	int i;

	if (dp_opts->lanes > 4) {
		dev_err(qmp->dev, "Invalid lane_num(%d)\n", dp_opts->lanes);
		return -EINVAL;
	}

	for (i = 0; i < dp_opts->lanes; i++) {
		v_level = max(v_level, dp_opts->voltage[i]);
		p_level = max(p_level, dp_opts->pre[i]);
	}

	if (v_level > 4 || p_level > 4) {
		dev_err(qmp->dev, "Invalid v(%d) | p(%d) level)\n",
			v_level, p_level);
		return -EINVAL;
	}

	voltage_swing_cfg = (*cfg->swing_tbl)[v_level][p_level];
	pre_emphasis_cfg = (*cfg->pre_emphasis_tbl)[v_level][p_level];

	voltage_swing_cfg |= DP_PHY_TXn_TX_DRV_LVL_MUX_EN;
	pre_emphasis_cfg |= DP_PHY_TXn_TX_EMP_POST1_LVL_MUX_EN;

	if (voltage_swing_cfg == 0xff && pre_emphasis_cfg == 0xff)
		return -EINVAL;

	writel(voltage_swing_cfg, tx + QSERDES_V2_TX_TX_DRV_LVL);
	writel(pre_emphasis_cfg, tx + QSERDES_V2_TX_TX_EMP_POST1_LVL);
	writel(voltage_swing_cfg, tx2 + QSERDES_V2_TX_TX_DRV_LVL);
	writel(pre_emphasis_cfg, tx2 + QSERDES_V2_TX_TX_EMP_POST1_LVL);

	return 0;
}

static void qmp_usbc_configure_dp_mode(struct qmp_usbc *qmp)
{
	bool reverse = (qmp->orientation == TYPEC_ORIENTATION_REVERSE);
	u32 val;

	val = DP_PHY_PD_CTL_PWRDN | DP_PHY_PD_CTL_AUX_PWRDN |
	      DP_PHY_PD_CTL_PLL_PWRDN | DP_PHY_PD_CTL_LANE_0_1_PWRDN | DP_PHY_PD_CTL_LANE_2_3_PWRDN;

	writel(val, qmp->dp_dp_phy + QSERDES_DP_PHY_PD_CTL);

	if (reverse)
		writel(0xc9, qmp->dp_dp_phy + QSERDES_DP_PHY_MODE);
	else
		writel(0xd9, qmp->dp_dp_phy + QSERDES_DP_PHY_MODE);
}

static int qmp_usbc_configure_dp_clocks(struct qmp_usbc *qmp)
{
	const struct phy_configure_opts_dp *dp_opts = &qmp->dp_opts;
	u32 phy_vco_div;
	unsigned long pixel_freq;

	switch (dp_opts->link_rate) {
	case 1620:
		phy_vco_div = 0x1;
		pixel_freq = 1620000000UL / 2;
		break;
	case 2700:
		phy_vco_div = 0x1;
		pixel_freq = 2700000000UL / 2;
		break;
	case 5400:
		phy_vco_div = 0x2;
		pixel_freq = 5400000000UL / 4;
		break;
	default:
		dev_err(qmp->dev, "link rate:%d not supported\n", dp_opts->link_rate);
		return -EINVAL;
	}
	writel(phy_vco_div, qmp->dp_dp_phy + QSERDES_V2_DP_PHY_VCO_DIV);

	clk_set_rate(qmp->dp_link_hw.clk, dp_opts->link_rate * 100000);
	clk_set_rate(qmp->dp_pixel_hw.clk, pixel_freq);

	return 0;
}

static void qmp_v2_configure_dp_tx(struct qmp_usbc *qmp)
{
	const struct phy_configure_opts_dp *dp_opts = &qmp->dp_opts;
	void __iomem *tx = qmp->dp_tx;
	void __iomem *tx2 = qmp->dp_tx2;

	/* program default setting first */
	writel(0x2a, tx + QSERDES_V2_TX_TX_DRV_LVL);
	writel(0x20, tx + QSERDES_V2_TX_TX_EMP_POST1_LVL);
	writel(0x2a, tx2 + QSERDES_V2_TX_TX_DRV_LVL);
	writel(0x20, tx2 + QSERDES_V2_TX_TX_EMP_POST1_LVL);

	if (dp_opts->link_rate >= 2700) {
		writel(0xc4, tx + QSERDES_V2_TX_LANE_MODE_1);
		writel(0xc4, tx2 + QSERDES_V2_TX_LANE_MODE_1);
	} else {
		writel(0xc6, tx + QSERDES_V2_TX_LANE_MODE_1);
		writel(0xc6, tx2 + QSERDES_V2_TX_LANE_MODE_1);
	}

	qmp_v2_configure_dp_swing(qmp);
}

static int qmp_v2_configure_dp_phy(struct qmp_usbc *qmp)
{
	u32 status;
	int ret;

	qmp_usbc_configure_dp_mode(qmp);

	writel(0x05, qmp->dp_dp_phy + QSERDES_V2_DP_PHY_TX0_TX1_LANE_CTL);
	writel(0x05, qmp->dp_dp_phy + QSERDES_V2_DP_PHY_TX2_TX3_LANE_CTL);

	ret = qmp_usbc_configure_dp_clocks(qmp);
	if (ret)
		return ret;

	writel(0x01, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);
	writel(0x05, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);
	writel(0x01, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);
	writel(0x09, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);

	writel(0x20, qmp->dp_serdes + QSERDES_COM_RESETSM_CNTRL);

	if (readl_poll_timeout(qmp->dp_serdes + QSERDES_COM_C_READY_STATUS,
			       status,
			       ((status & BIT(0)) > 0),
			       500,
			       10000)) {
		dev_err(qmp->dev, "C_READY not ready\n");
		return -ETIMEDOUT;
	}

	if (readl_poll_timeout(qmp->dp_serdes + QSERDES_COM_CMN_STATUS,
			       status,
			       ((status & BIT(0)) > 0),
			       500,
			       10000)){
		dev_err(qmp->dev, "FREQ_DONE not ready\n");
		return -ETIMEDOUT;
	}

	if (readl_poll_timeout(qmp->dp_serdes + QSERDES_COM_CMN_STATUS,
			       status,
			       ((status & BIT(1)) > 0),
			       500,
			       10000)){
		dev_err(qmp->dev, "PLL_LOCKED not ready\n");
		return -ETIMEDOUT;
	}

	writel(0x19, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);

	if (readl_poll_timeout(qmp->dp_dp_phy + QSERDES_V2_DP_PHY_STATUS,
			       status,
			       ((status & BIT(0)) > 0),
			       500,
			       10000)){
		dev_err(qmp->dev, "TSYNC_DONE not ready\n");
		return -ETIMEDOUT;
	}

	if (readl_poll_timeout(qmp->dp_dp_phy + QSERDES_V2_DP_PHY_STATUS,
			       status,
			       ((status & BIT(1)) > 0),
			       500,
			       10000)){
		dev_err(qmp->dev, "PHY_READY not ready\n");
		return -ETIMEDOUT;
	}

	writel(0x3f, qmp->dp_tx + QSERDES_V2_TX_TRANSCEIVER_BIAS_EN);
	writel(0x10, qmp->dp_tx + QSERDES_V2_TX_HIGHZ_DRVR_EN);
	writel(0x0a, qmp->dp_tx + QSERDES_V2_TX_TX_POL_INV);
	writel(0x3f, qmp->dp_tx2 + QSERDES_V2_TX_TRANSCEIVER_BIAS_EN);
	writel(0x10, qmp->dp_tx2 + QSERDES_V2_TX_HIGHZ_DRVR_EN);
	writel(0x0a, qmp->dp_tx2 + QSERDES_V2_TX_TX_POL_INV);

	writel(0x18, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);
	writel(0x19, qmp->dp_dp_phy + QSERDES_DP_PHY_CFG);

	if (readl_poll_timeout(qmp->dp_dp_phy + QSERDES_V2_DP_PHY_STATUS,
			       status,
			       ((status & BIT(1)) > 0),
			       500,
			       10000)){
		dev_err(qmp->dev, "PHY_READY not ready\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int qmp_v2_calibrate_dp_phy(struct qmp_usbc *qmp)
{
	static const u8 cfg1_settings[] = {0x13, 0x23, 0x1d};
	u8 val;

	qmp->dp_aux_cfg++;
	qmp->dp_aux_cfg %= ARRAY_SIZE(cfg1_settings);
	val = cfg1_settings[qmp->dp_aux_cfg];

	writel(val, qmp->dp_dp_phy + QSERDES_DP_PHY_AUX_CFG1);

	return 0;
}

static int qmp_usbc_usb_power_on(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *status;
	unsigned int val;
	int ret;

	qphy_setbits(qmp->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL], SW_PWRDN);

	/* Use software based port select and switch on typec orientation */
	val = SW_PORTSELECT_MUX;
	if (qmp->orientation == TYPEC_ORIENTATION_REVERSE)
		val |= SW_PORTSELECT_VAL;
	writel(val, qmp->pcs_misc);

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

static int qmp_usbc_usb_power_off(struct phy *phy)
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

static int qmp_usbc_check_phy_status(struct qmp_usbc *qmp, bool is_dp)
{
	if ((is_dp && qmp->usb_init_count) ||
	    (!is_dp && qmp->dp_init_count)) {
		dev_err(qmp->dev,
			"PHY is configured for %s, can not enable %s\n",
			is_dp ? "USB" : "DP", is_dp ? "DP" : "USB");
		return -EBUSY;
	}

	return 0;
}

static int qmp_usbc_usb_enable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	int ret;

	mutex_lock(&qmp->phy_mutex);

	ret = qmp_usbc_check_phy_status(qmp, false);
	if (ret)
		goto out_unlock;

	ret = qmp_usbc_com_init(phy);
	if (ret)
		goto out_unlock;

	qmp_usbc_set_phy_mode(qmp, false);

	ret = qmp_usbc_usb_power_on(phy);
	if (ret) {
		qmp_usbc_com_exit(phy);
		goto out_unlock;
	}

	qmp->usb_init_count++;
out_unlock:
	mutex_unlock(&qmp->phy_mutex);

	return ret;
}

static int qmp_usbc_usb_disable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	int ret;

	qmp->usb_init_count--;
	ret = qmp_usbc_usb_power_off(phy);
	if (ret)
		return ret;
	return qmp_usbc_com_exit(phy);
}

static int qmp_usbc_usb_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);

	qmp->mode = mode;

	return 0;
}

static int qmp_usbc_dp_enable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int ret;

	if (qmp->dp_init_count) {
		dev_err(qmp->dev, "DP already inited\n");
		return 0;
	}

	mutex_lock(&qmp->phy_mutex);

	ret = qmp_usbc_check_phy_status(qmp, true);
	if (ret)
		goto dp_init_unlock;

	ret = qmp_usbc_com_init(phy);
	if (ret)
		goto dp_init_unlock;

	qmp_usbc_set_phy_mode(qmp, true);

	cfg->dp_aux_init(qmp);

	qmp->dp_init_count++;

dp_init_unlock:
	mutex_unlock(&qmp->phy_mutex);
	return ret;
}

static int qmp_usbc_dp_disable(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);

	mutex_lock(&qmp->phy_mutex);

	qmp_usbc_com_exit(phy);

	qmp->dp_init_count--;

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static int qmp_usbc_dp_configure(struct phy *phy, union phy_configure_opts *opts)
{
	const struct phy_configure_opts_dp *dp_opts = &opts->dp;
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	mutex_lock(&qmp->phy_mutex);

	memcpy(&qmp->dp_opts, dp_opts, sizeof(*dp_opts));
	if (qmp->dp_opts.set_voltages) {
		cfg->configure_dp_tx(qmp);
		qmp->dp_opts.set_voltages = 0;
	}

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static int qmp_usbc_dp_calibrate(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int ret = 0;

	mutex_lock(&qmp->phy_mutex);

	if (cfg->calibrate_dp_phy) {
		ret = cfg->calibrate_dp_phy(qmp);
		if (ret) {
			dev_err(qmp->dev, "dp calibrate err(%d)\n", ret);
			mutex_unlock(&qmp->phy_mutex);
			return ret;
		}
	}

	mutex_unlock(&qmp->phy_mutex);
	return 0;
}

static int qmp_usbc_dp_serdes_init(struct qmp_usbc *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	void __iomem *serdes = qmp->dp_serdes;
	const struct phy_configure_opts_dp *dp_opts = &qmp->dp_opts;

	qmp_configure(qmp->dev, serdes, cfg->dp_serdes_tbl,
		      cfg->dp_serdes_tbl_num);

	switch (dp_opts->link_rate) {
	case 1620:
		qmp_configure(qmp->dev, serdes, cfg->serdes_tbl_rbr,
			      cfg->serdes_tbl_rbr_num);
		break;
	case 2700:
		qmp_configure(qmp->dev, serdes, cfg->serdes_tbl_hbr,
			      cfg->serdes_tbl_hbr_num);
		break;
	case 5400:
		qmp_configure(qmp->dev, serdes, cfg->serdes_tbl_hbr2,
			      cfg->serdes_tbl_hbr2_num);
		break;
	default:
		/* Other link rates aren't supported */
		return -EINVAL;
	}

	return 0;
}

static int qmp_usbc_dp_power_on(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	void __iomem *tx = qmp->dp_tx;
	void __iomem *tx2 = qmp->dp_tx2;

	/*
	 * FIXME: SW_PORTSELECT handling for DP orientation flip is not implemented.
	 * Expected:
	 * - For standard lane mapping: configure SW_PORTSELECT in QSERDES_DP_PHY_CFG_1.
	 * - For non-standard mapping: pass orientation to dp_ctrl and handle flip
	 *   via logical2physical lane remapping.
	 */

	mutex_lock(&qmp->phy_mutex);

	qmp_usbc_dp_serdes_init(qmp);

	qmp_configure_lane(qmp->dev, tx, cfg->dp_tx_tbl, cfg->dp_tx_tbl_num, 1);
	qmp_configure_lane(qmp->dev, tx2, cfg->dp_tx_tbl, cfg->dp_tx_tbl_num, 2);

	/* Configure special DP tx tunings */
	cfg->configure_dp_tx(qmp);

	/* Configure link rate, swing, etc. */
	cfg->configure_dp_phy(qmp);

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static int qmp_usbc_dp_power_off(struct phy *phy)
{
	struct qmp_usbc *qmp = phy_get_drvdata(phy);

	mutex_lock(&qmp->phy_mutex);

	/* Assert DP PHY power down */
	writel(DP_PHY_PD_CTL_PSR_PWRDN, qmp->dp_dp_phy + QSERDES_DP_PHY_PD_CTL);

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static const struct phy_ops qmp_usbc_usb_phy_ops = {
	.init		= qmp_usbc_usb_enable,
	.exit		= qmp_usbc_usb_disable,
	.set_mode	= qmp_usbc_usb_set_mode,
	.owner		= THIS_MODULE,
};

static const struct phy_ops qmp_usbc_dp_phy_ops = {
	.init		= qmp_usbc_dp_enable,
	.exit		= qmp_usbc_dp_disable,
	.configure	= qmp_usbc_dp_configure,
	.calibrate	= qmp_usbc_dp_calibrate,
	.power_on	= qmp_usbc_dp_power_on,
	.power_off	= qmp_usbc_dp_power_off,
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

	if (!qmp->usb_init_count && !qmp->dp_init_count) {
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

	if (!qmp->usb_init_count && !qmp->dp_init_count) {
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

static struct clk_hw *qmp_usbc_clks_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct qmp_usbc *qmp = data;

	if (clkspec->args_count == 0)
		return &qmp->pipe_clk_fixed.hw;

	switch (clkspec->args[0]) {
	case QMP_USB43DP_USB3_PIPE_CLK:
		return &qmp->pipe_clk_fixed.hw;
	case QMP_USB43DP_DP_LINK_CLK:
		return &qmp->dp_link_hw;
	case QMP_USB43DP_DP_VCO_DIV_CLK:
		return &qmp->dp_pixel_hw;
	}

	return ERR_PTR(-EINVAL);
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
	char name[64];
	int ret;

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret) {
		/* Clock name is not mandatory. */
		snprintf(name, sizeof(name), "%s::pipe_clk", dev_name(qmp->dev));
		init.name = name;
	}

	init.ops = &clk_fixed_rate_ops;

	/* controllers using QMP phys use 125MHz pipe clock interface */
	fixed->fixed_rate = 125000000;
	fixed->hw.init = &init;

	return devm_clk_hw_register(qmp->dev, &fixed->hw);
}

/*
 * Display Port PLL driver block diagram for branch clocks
 *
 *              +------------------------------+
 *              |         DP_VCO_CLK           |
 *              |                              |
 *              |    +-------------------+     |
 *              |    |   (DP PLL/VCO)    |     |
 *              |    +---------+---------+     |
 *              |              v               |
 *              |   +----------+-----------+   |
 *              |   | hsclk_divsel_clk_src |   |
 *              |   +----------+-----------+   |
 *              +------------------------------+
 *                              |
 *          +---------<---------v------------>----------+
 *          |                                           |
 * +--------v----------------+                          |
 * |    dp_phy_pll_link_clk  |                          |
 * |     link_clk            |                          |
 * +--------+----------------+                          |
 *          |                                           |
 *          |                                           |
 *          v                                           v
 * Input to DISPCC block                                |
 * for link clk, crypto clk                             |
 * and interface clock                                  |
 *                                                      |
 *                                                      |
 *      +--------<------------+-----------------+---<---+
 *      |                     |                 |
 * +----v---------+  +--------v-----+  +--------v------+
 * | vco_divided  |  | vco_divided  |  | vco_divided   |
 * |    _clk_src  |  |    _clk_src  |  |    _clk_src   |
 * |              |  |              |  |               |
 * |divsel_six    |  |  divsel_two  |  |  divsel_four  |
 * +-------+------+  +-----+--------+  +--------+------+
 *         |                 |                  |
 *         v---->----------v-------------<------v
 *                         |
 *              +----------+-----------------+
 *              |   dp_phy_pll_vco_div_clk   |
 *              +---------+------------------+
 *                        |
 *                        v
 *              Input to DISPCC block
 *              for DP pixel clock
 *
 */
static int qmp_dp_pixel_clk_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	switch (req->rate) {
	case 1620000000UL / 2:
	case 2700000000UL / 2:
	/* 5.4 is same link rate as 2.7GHz, i.e. div 4 */
		return 0;
	default:
		return -EINVAL;
	}
}

static unsigned long qmp_dp_pixel_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	const struct qmp_usbc *qmp;
	const struct phy_configure_opts_dp *dp_opts;

	qmp = container_of(hw, struct qmp_usbc, dp_pixel_hw);

	dp_opts = &qmp->dp_opts;

	switch (dp_opts->link_rate) {
	case 1620:
		return 1620000000UL / 2;
	case 2700:
		return 2700000000UL / 2;
	case 5400:
		return 5400000000UL / 4;
	default:
		return 0;
	}
}

static const struct clk_ops qmp_dp_pixel_clk_ops = {
	.determine_rate	= qmp_dp_pixel_clk_determine_rate,
	.recalc_rate	= qmp_dp_pixel_clk_recalc_rate,
};

static int qmp_dp_link_clk_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	switch (req->rate) {
	case 162000000:
	case 270000000:
	case 540000000:
		return 0;
	default:
		return -EINVAL;
	}
}

static unsigned long qmp_dp_link_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	const struct qmp_usbc *qmp;
	const struct phy_configure_opts_dp *dp_opts;

	qmp = container_of(hw, struct qmp_usbc, dp_link_hw);
	dp_opts = &qmp->dp_opts;

	switch (dp_opts->link_rate) {
	case 1620:
	case 2700:
	case 5400:
		return dp_opts->link_rate * 100000;
	default:
		return 0;
	}
}

static const struct clk_ops qmp_dp_link_clk_ops = {
	.determine_rate	= qmp_dp_link_clk_determine_rate,
	.recalc_rate	= qmp_dp_link_clk_recalc_rate,
};

static int phy_dp_clks_register(struct qmp_usbc *qmp, struct device_node *np)
{
	struct clk_init_data init = { };
	char name[64];
	int ret;

	snprintf(name, sizeof(name), "%s::link_clk", dev_name(qmp->dev));
	init.ops = &qmp_dp_link_clk_ops;
	init.name = name;
	qmp->dp_link_hw.init = &init;
	ret = devm_clk_hw_register(qmp->dev, &qmp->dp_link_hw);
	if (ret < 0) {
		dev_err(qmp->dev, "link clk reg fail ret=%d\n", ret);
		return ret;
	}

	snprintf(name, sizeof(name), "%s::vco_div_clk", dev_name(qmp->dev));
	init.ops = &qmp_dp_pixel_clk_ops;
	init.name = name;
	qmp->dp_pixel_hw.init = &init;
	ret = devm_clk_hw_register(qmp->dev, &qmp->dp_pixel_hw);
	if (ret) {
		dev_err(qmp->dev, "pxl clk reg fail ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static void phy_clk_release_provider(void *res)
{
	of_clk_del_provider(res);
}

static int qmp_usbc_register_clocks(struct qmp_usbc *qmp, struct device_node *np)
{
	struct clk_fixed_rate *fixed = &qmp->pipe_clk_fixed;
	int ret;

	ret = phy_pipe_clk_register(qmp, np);
	if (ret)
		return ret;

	if (qmp->dp_serdes != 0) {
		ret = phy_dp_clks_register(qmp, np);
		if (ret)
			return ret;
	}

	if (np == qmp->dev->of_node)
		return devm_of_clk_add_hw_provider(qmp->dev, qmp_usbc_clks_hw_get, qmp);

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
		qmp_usbc_usb_power_off(qmp->usb_phy);
		qmp_usbc_com_exit(qmp->usb_phy);

		qmp_usbc_com_init(qmp->usb_phy);
		qmp_usbc_set_phy_mode(qmp, false);
		qmp_usbc_usb_power_on(qmp->usb_phy);
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

	if (offs->dp_serdes != 0) {
		qmp->dp_serdes = base + offs->dp_serdes;
		qmp->dp_tx = base + offs->dp_txa;
		qmp->dp_tx2 = base + offs->dp_txb;
		qmp->dp_dp_phy = base + offs->dp_dp_phy;
	}

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

	ret = qmp_usbc_reset_init(qmp, cfg->reset_list, cfg->num_resets);
	if (ret)
		return ret;

	return 0;
}

static int qmp_usbc_parse_tcsr(struct qmp_usbc *qmp)
{
	struct of_phandle_args tcsr_args;
	struct device *dev = qmp->dev;
	int ret, args_count;

	args_count = of_property_count_u32_elems(dev->of_node, "qcom,tcsr-reg");
	args_count = args_count - 1;
	ret = of_parse_phandle_with_fixed_args(dev->of_node, "qcom,tcsr-reg",
					       args_count, 0, &tcsr_args);
	if (ret == -ENOENT)
		return 0;
	else if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to parse qcom,tcsr-reg\n");

	qmp->tcsr_map = syscon_node_to_regmap(tcsr_args.np);
	of_node_put(tcsr_args.np);
	if (IS_ERR(qmp->tcsr_map))
		return PTR_ERR(qmp->tcsr_map);

	qmp->vls_clamp_reg = tcsr_args.args[0];

	if (args_count > 1)
		qmp->dp_phy_mode_reg = tcsr_args.args[1];

	return 0;
}

static struct phy *qmp_usbc_phy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct qmp_usbc *qmp = dev_get_drvdata(dev);

	if (args->args_count == 0)
		return qmp->usb_phy;

	switch (args->args[0]) {
	case QMP_USB43DP_USB3_PHY:
		return qmp->usb_phy;
	case QMP_USB43DP_DP_PHY:
		return qmp->dp_phy ?: ERR_PTR(-ENODEV);
	}

	return ERR_PTR(-EINVAL);
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

	ret = devm_regulator_bulk_get_const(qmp->dev, qmp->cfg->num_vregs,
					    qmp->cfg->vreg_list, &qmp->vregs);
	if (ret)
		return ret;

	ret = qmp_usbc_typec_switch_register(qmp);
	if (ret)
		return ret;

	ret = qmp_usbc_parse_tcsr(qmp);
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

	ret = qmp_usbc_register_clocks(qmp, np);
	if (ret)
		goto err_node_put;

	qmp->usb_phy = devm_phy_create(dev, np, &qmp_usbc_usb_phy_ops);
	if (IS_ERR(qmp->usb_phy)) {
		ret = PTR_ERR(qmp->usb_phy);
		dev_err(dev, "failed to create PHY: %d\n", ret);
		goto err_node_put;
	}

	phy_set_drvdata(qmp->usb_phy, qmp);

	if (qmp->dp_serdes != 0) {
		qmp->dp_phy = devm_phy_create(dev, np, &qmp_usbc_dp_phy_ops);
		if (IS_ERR(qmp->dp_phy)) {
			ret = PTR_ERR(qmp->dp_phy);
			dev_err(dev, "failed to create PHY: %d\n", ret);
			goto err_node_put;
		}
		phy_set_drvdata(qmp->dp_phy, qmp);
	}

	of_node_put(np);

	phy_provider = devm_of_phy_provider_register(dev, qmp_usbc_phy_xlate);

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
		.compatible = "qcom,qcs615-qmp-usb3-dp-phy",
		.data =  &qcs615_usb3dp_phy_cfg,
	}, {
		.compatible = "qcom,qcs615-qmp-usb3-phy",
		.data = &qcs615_usb3phy_cfg,
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
