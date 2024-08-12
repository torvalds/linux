// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/kthread.h>

#include "dwmac-qcom-serdes.h"

static int qcom_ethqos_serdes3_sgmii_1Gb(struct qcom_ethqos *ethqos)
{
	int retry = 500;
	unsigned int val;
	int ret = 0;

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES3_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES3_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES3_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES3_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES3_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES3_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x82, ethqos->sgmii_base + QSERDES3_COM_DEC_START_MODE0);
	writel_relaxed(0x55, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x55, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x24, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE1_MODE0);

	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES3_COM_HSCLK_SEL);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_HSCLK_HS_SWITCH_SEL);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_CORE_CLK_EN);
	writel_relaxed(0xB9, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_HSCLK_SEL);

	/******************MODULE: QSERDES3_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES3_TX_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_TX_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_TX_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_TX_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES3_TX_LANE_MODE_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_TX_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES3_TX_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_TX_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES3_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES3_RX_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES3_RX_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES3_RX_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES3_RX_UCDR_PI_CTRL2);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES3_RX_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES3_RX_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_RX_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_RX_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES3_RX_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES3_RX_RX_BAND);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_RX_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_PCS_LINE_RESET_TIME);
	writel_relaxed(0x1F, ethqos->sgmii_base + QSERDES3_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES3_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + QSERDES3_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES3_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_PCS_SGMII_MISC_CTRL8);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_PCS_SW_RESET);

	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_PHY_START);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_COM_C_READY_STATUS);
		val &= QSERDES3_COM_C_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("QSERDES3_COM_C_READY_STATUS timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_PCS_PCS_READY_STATUS);
		val &= QSERDES3_PCS_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PCS_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_PCS_PCS_READY_STATUS);
		val &= QSERDES3_PCS_SGMIIPHY_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("SGMIIPHY_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 5000;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_COM_CMN_STATUS);
		val &= QSERDES3_COM_C_PLL_LOCKED;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PLL Lock Status timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

err_ret:
		return ret;
}

static int qcom_ethqos_serdes3_sgmii_2_5Gb(struct qcom_ethqos *ethqos)
{
	int retry = 500;
	unsigned int val;
	int ret = 0;

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES3_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES3_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES3_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES3_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES3_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x41, ethqos->sgmii_base + QSERDES3_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x7A, ethqos->sgmii_base + QSERDES3_COM_DEC_START_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0xA1, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE1_MODE0);

	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES3_COM_HSCLK_SEL);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_HSCLK_HS_SWITCH_SEL);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES3_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_COM_CORE_CLK_EN);
	writel_relaxed(0xCD, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1C, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES3_COM_BIN_VCOCAL_HSCLK_SEL);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES3_TX_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_TX_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_TX_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES3_TX_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES3_TX_LANE_MODE_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_TX_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES3_TX_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_TX_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES3_RX_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES3_RX_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_RX_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES3_RX_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES3_RX_UCDR_PI_CTRL2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES3_RX_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_RX_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES3_RX_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES3_RX_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES3_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_RX_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES3_RX_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES3_RX_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES3_RX_RX_BAND);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH3);
	writel_relaxed(0xB8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES3_RX_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_RX_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES3_PCS_LINE_RESET_TIME);
	writel_relaxed(0x1F, ethqos->sgmii_base + QSERDES3_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES3_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + QSERDES3_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES3_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x8C, ethqos->sgmii_base + QSERDES3_PCS_SGMII_MISC_CTRL8);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES3_PCS_SW_RESET);

	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES3_PCS_PHY_START);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_COM_C_READY_STATUS);
		val &= QSERDES3_COM_C_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("QSERDES3_COM_C_READY_STATUS timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_PCS_PCS_READY_STATUS);
		val &= QSERDES3_PCS_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PCS_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_PCS_PCS_READY_STATUS);
		val &= QSERDES3_PCS_SGMIIPHY_READY;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("SGMIIPHY_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 5000;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES3_COM_CMN_STATUS);
		val &= QSERDES3_COM_C_PLL_LOCKED;
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PLL Lock Status timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

err_ret:
		return ret;
}

static int qcom_ethqos_serdes_sgmii_1Gb(struct qcom_ethqos *ethqos)
{
	int ret = 0;
	int retry = 5000;
	unsigned int val;

	if (ethqos->emac_ver == EMAC_HW_v3_1_0)
		return qcom_ethqos_serdes3_sgmii_1Gb(ethqos);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	msleep(50);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0E, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_BG_TIMER);
	writel_relaxed(0x0F, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_PLL_IVCO);

	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x82, ethqos->sgmii_base + QSERDES_COM_DEC_START_MODE0);
	writel_relaxed(0x55, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x55, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x84, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE1_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_COM_HSCLK_SEL_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_HSCLK_HS_SWITCH_SEL_1);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_CORE_CLK_EN);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_CMN_CONFIG_1);
	writel_relaxed(0xB9, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_HSCLK_SEL_1);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_TX0_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_1);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES_TX0_RCV_DETECT_LVL_2);
	writel_relaxed(0x1F, ethqos->sgmii_base + QSERDES_TX_TX_DRV_LVL);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX_TX_EMP_POST1_LVL);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX0_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CTRL2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES_RX0_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES_RX0_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_RX0_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_RX0_RX_BAND);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH3);
	writel_relaxed(0xB8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_LINE_RESET_TIME);
	writel_relaxed(0x03, ethqos->sgmii_base + SGMII_PHY_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x1F, ethqos->sgmii_base + SGMII_PHY_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CAL_CTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CAL_CTRL2_AND_CDR_LOCK_EDGE);
	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL7);
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL8);

	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	msleep(220);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_PHY_START);
	msleep(220);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_CMN_STATUS);
		val &= BIT(1);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR(" %s : PLL Lock Status timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_C_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : C_READY_STATUS timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(7);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : PCS_READY timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : SGMIIPHY_READY timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	return ret;

err_ret:
	return ret;
}

static int qcom_ethqos_serdes_sgmii_25Gb(struct qcom_ethqos *ethqos)
{
	int ret = 0;
	int retry = 5000;
	unsigned int val;

	if (ethqos->emac_ver == EMAC_HW_v3_1_0)
		return qcom_ethqos_serdes3_sgmii_2_5Gb(ethqos);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	msleep(220);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0E, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_BG_TIMER);
	writel_relaxed(0x0F, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x41, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x7A, ethqos->sgmii_base + QSERDES_COM_DEC_START_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE1_MODE0);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES_COM_HSCLK_SEL_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_HSCLK_HS_SWITCH_SEL_1);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_CORE_CLK_EN);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_CMN_CONFIG_1);
	writel_relaxed(0xCD, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1C, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_HSCLK_SEL_1);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_TX0_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_TX0_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_1);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES_TX0_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX0_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CTRL2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES_RX0_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES_RX0_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_RX0_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_BAND);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH3);
	writel_relaxed(0xB8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_LINE_RESET_TIME);
	writel_relaxed(0x03, ethqos->sgmii_base + SGMII_PHY_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x1F, ethqos->sgmii_base + SGMII_PHY_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL2);

	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CAL_CTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CAL_CTRL2_AND_CDR_LOCK_EDGE);
	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL7);
	writel_relaxed(0x8c, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL8);

	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	msleep(220);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_PHY_START);
	msleep(220);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_CMN_STATUS);
		val &= BIT(1);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : PLL Lock Status timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_C_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : C_READY_STATUS timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(7);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : PCS_READY timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("%s : SGMIIPHY_READY timedout with retry = %d\n",
			  __func__,
			  retry);
		ret = -1;
		goto err_ret;
	}

	return ret;

err_ret:
	return ret;
}

static int qcom_ethqos_serdes_usxgmii_25Gb(struct qcom_ethqos *ethqos)
{
	int ret = 0;
	int retry = 5000;
	unsigned int val;

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0E, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_BG_TIMER);
	writel_relaxed(0x0F, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x41, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x7A, ethqos->sgmii_base + QSERDES_COM_DEC_START_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE1_MODE0);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES_COM_HSCLK_SEL_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_HSCLK_HS_SWITCH_SEL_1);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_CORE_CLK_EN);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_CMN_CONFIG_1);
	writel_relaxed(0xCD, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1C, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_HSCLK_SEL_1);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_TX0_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_TX0_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_1);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES_TX0_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX0_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CTRL2);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_RX0_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES_RX0_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES_RX0_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_RX0_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_BAND);
	writel_relaxed(0xe0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_LINE_RESET_TIME);
	writel_relaxed(0x1F, ethqos->sgmii_base + SGMII_PHY_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x03, ethqos->sgmii_base + SGMII_PHY_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x8c, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL8);

	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_PHY_START);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_CMN_STATUS);
		val &= BIT(1);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PLL Lock Status timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_C_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("C_READY_STATUS timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(7);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PCS_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("SGMIIPHY_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	ETHQOSINFO("%s : Serdes is up on USXGMII 2.5 Gbps mode", __func__);
	return ret;

err_ret:
	return ret;
}

static int qcom_ethqos_serdes_usxgmii_5Gb(struct qcom_ethqos *ethqos)
{
	int ret = 0;
	int retry = 5000;
	unsigned int val;

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0E, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_BG_TIMER);
	writel_relaxed(0x0F, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x41, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x7A, ethqos->sgmii_base + QSERDES_COM_DEC_START_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE1_MODE0);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES_COM_HSCLK_SEL_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_HSCLK_HS_SWITCH_SEL_1);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_CORE_CLK_EN);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_CMN_CONFIG_1);
	writel_relaxed(0xCD, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1C, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_HSCLK_SEL_1);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_TX0_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_TX0_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_1);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES_TX0_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX0_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CTRL2);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_RX0_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES_RX0_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES_RX0_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_RX0_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_BAND);
	writel_relaxed(0xe0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_LINE_RESET_TIME);
	writel_relaxed(0x1F, ethqos->sgmii_base + SGMII_PHY_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x03, ethqos->sgmii_base + SGMII_PHY_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x8c, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL8);

	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_PHY_START);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_CMN_STATUS);
		val &= BIT(1);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PLL Lock Status timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_C_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("C_READY_STATUS timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(7);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PCS_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("SGMIIPHY_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	return ret;

err_ret:
	return ret;
}

static int qcom_ethqos_serdes_usxgmii_10Gb_1Gb(struct qcom_ethqos *ethqos)
{
	int ret = 0;
	int retry = 5000;
	unsigned int val;

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_POWER_DOWN_CONTROL);

	/***************** MODULE: QSERDES_COM_SGMII_QMP_PLL*********/
	writel_relaxed(0x0E, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_BG_TIMER);
	writel_relaxed(0x0F, ethqos->sgmii_base + SGMII_PHY_0_QSERDES_COM_PLL_IVCO);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_COM_CP_CTRL_MODE0);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_PLL_RCTRL_MODE0);
	writel_relaxed(0x36, ethqos->sgmii_base + QSERDES_COM_PLL_CCTRL_MODE0);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_SYSCLK_EN_SEL);
	writel_relaxed(0x1A, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP1_MODE0);
	writel_relaxed(0x41, ethqos->sgmii_base + QSERDES_COM_LOCK_CMP2_MODE0);
	writel_relaxed(0x7A, ethqos->sgmii_base + QSERDES_COM_DEC_START_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START1_MODE0);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START2_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_DIV_FRAC_START3_MODE0);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE1_MODE0);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE2_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_VCO_TUNE_INITVAL2);
	writel_relaxed(0x03, ethqos->sgmii_base + QSERDES_COM_HSCLK_SEL_1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_HSCLK_HS_SWITCH_SEL_1);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_COM_CORECLK_DIV_MODE0);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_COM_CORE_CLK_EN);
	writel_relaxed(0x16, ethqos->sgmii_base + QSERDES_COM_CMN_CONFIG_1);
	writel_relaxed(0xCD, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	writel_relaxed(0x1C, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	writel_relaxed(0x11, ethqos->sgmii_base + QSERDES_COM_BIN_VCOCAL_HSCLK_SEL_1);

	/******************MODULE: QSERDES_TX0_SGMII_QMP_TX***********************/
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_TX0_TX_BAND);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_TX0_SLEW_CNTL);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_TX);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_TX0_RES_CODE_LANE_OFFSET_RX);
	writel_relaxed(0x05, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_1);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_2);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_TX0_LANE_MODE_3);
	writel_relaxed(0x12, ethqos->sgmii_base + QSERDES_TX0_RCV_DETECT_LVL_2);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_TX0_TRAN_DRVR_EMP_EN);

	/*****************MODULE: QSERDES_RX0_SGMII_QMP_RX*******************/
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FO_GAIN);
	writel_relaxed(0x06, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_GAIN);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_FO_GAIN);
	writel_relaxed(0x7F, ethqos->sgmii_base + QSERDES_RX0_UCDR_SO_SATURATION_AND_ENABLE);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_UCDR_FASTLOCK_COUNT_HIGH);
	writel_relaxed(0x81, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CONTROLS);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_UCDR_PI_CTRL2);
	writel_relaxed(0x02, ethqos->sgmii_base + QSERDES_RX0_RX_TERM_BW);
	writel_relaxed(0x08, ethqos->sgmii_base + QSERDES_RX0_VGA_CAL_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_GM_CAL);
	writel_relaxed(0x04, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL2);
	writel_relaxed(0x4A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL3);
	writel_relaxed(0x0A, ethqos->sgmii_base + QSERDES_RX0_RX_EQU_ADAPTOR_CNTRL4);
	writel_relaxed(0x80, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_LOW);
	writel_relaxed(0x01, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_TSETTLE_HIGH);
	writel_relaxed(0x20, ethqos->sgmii_base + QSERDES_RX0_RX_IDAC_MEASURE_TIME);
	writel_relaxed(0x17, ethqos->sgmii_base + QSERDES_RX0_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	writel_relaxed(0x00, ethqos->sgmii_base + QSERDES_RX0_RX_OFFSET_ADAPTOR_CNTRL2);
	writel_relaxed(0x0F, ethqos->sgmii_base + QSERDES_RX0_SIGDET_CNTRL);
	writel_relaxed(0x1E, ethqos->sgmii_base + QSERDES_RX0_SIGDET_DEGLITCH_CNTRL);
	writel_relaxed(0x18, ethqos->sgmii_base + QSERDES_RX0_RX_BAND);
	writel_relaxed(0xe0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_00_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH2);
	writel_relaxed(0x09, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH3);
	writel_relaxed(0xB1, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_01_HIGH4);
	writel_relaxed(0xE0, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_LOW);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH);
	writel_relaxed(0xC8, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH2);
	writel_relaxed(0x3B, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH3);
	writel_relaxed(0xB7, ethqos->sgmii_base + QSERDES_RX0_RX_MODE_10_HIGH4);
	writel_relaxed(0x0C, ethqos->sgmii_base + QSERDES_RX0_DCC_CTRL1);

	/****************MODULE: SGMII_PHY_SGMII_PCS**********************************/
	writel_relaxed(0x0C, ethqos->sgmii_base + SGMII_PHY_PCS_LINE_RESET_TIME);
	writel_relaxed(0x1F, ethqos->sgmii_base + SGMII_PHY_PCS_TX_LARGE_AMP_DRV_LVL);
	writel_relaxed(0x03, ethqos->sgmii_base + SGMII_PHY_PCS_TX_SMALL_AMP_DRV_LVL);
	writel_relaxed(0x83, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL1);
	writel_relaxed(0x08, ethqos->sgmii_base + SGMII_PHY_PCS_TX_MID_TERM_CTRL2);
	writel_relaxed(0x8c, ethqos->sgmii_base + SGMII_PHY_PCS_SGMII_MISC_CTRL8);

	writel_relaxed(0x00, ethqos->sgmii_base + SGMII_PHY_PCS_SW_RESET);
	writel_relaxed(0x01, ethqos->sgmii_base + SGMII_PHY_PCS_PHY_START);

	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_CMN_STATUS);
		val &= BIT(1);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PLL Lock Status timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + QSERDES_COM_C_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("C_READY_STATUS timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(7);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("PCS_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	retry = 500;
	do {
		val = readl_relaxed(ethqos->sgmii_base + SGMII_PHY_PCS_READY_STATUS);
		val &= BIT(0);
		if (val)
			break;
		usleep_range(1000, 1500);
		retry--;
	} while (retry > 0);
	if (!retry) {
		ETHQOSERR("SGMIIPHY_READY timedout with retry = %d\n", retry);
		ret = -1;
		goto err_ret;
	}

	return ret;

err_ret:
	return ret;
}

static int qcom_ethqos_serdes_update_sgmii(struct qcom_ethqos *ethqos,
					   int speed)
{
	int ret = 0;

	switch (speed) {
	case SPEED_1000:
		if (ethqos->curr_serdes_speed != SPEED_1000)
			ret = qcom_ethqos_serdes_sgmii_1Gb(ethqos);

		ethqos->curr_serdes_speed = SPEED_1000;
		break;
	case SPEED_2500:
		if (ethqos->curr_serdes_speed != SPEED_2500)
			ret = qcom_ethqos_serdes_sgmii_25Gb(ethqos);

		ethqos->curr_serdes_speed = SPEED_2500;
		break;
	case SPEED_100:
	case SPEED_10:
		if (ethqos->curr_serdes_speed != SPEED_1000) {
			ETHQOSINFO("%s : Serdes Speed set to 1GB speed", __func__);
			ret = qcom_ethqos_serdes_sgmii_1Gb(ethqos);
			ethqos->curr_serdes_speed = SPEED_1000;
		}
		break;
	default:
		ETHQOSERR("%s : Serdes Speed not set for speed = %d\n",
			  __func__,
			   speed);
		ethqos->curr_serdes_speed = 0;
		break;
	}

	ETHQOSINFO("%s : Exit serdes speed = %d",
		   __func__,
		   ethqos->curr_serdes_speed);
	return ret;
}

static int qcom_ethqos_serdes_update_usxgmii(struct qcom_ethqos *ethqos,
					     int speed)
{
	int ret = 0;

	switch (speed) {
	case SPEED_2500:
		ret = qcom_ethqos_serdes_usxgmii_25Gb(ethqos);
		break;
	case SPEED_5000:
		ret = qcom_ethqos_serdes_usxgmii_5Gb(ethqos);
		break;
	case SPEED_1000:
	case SPEED_10000:
		ret = qcom_ethqos_serdes_usxgmii_10Gb_1Gb(ethqos);
		break;
	default:
		ETHQOSINFO("%s : Serdes Speed 1GB set by default", __func__);
		ret = qcom_ethqos_serdes_usxgmii_10Gb_1Gb(ethqos);
		break;
	}

	ETHQOSINFO("%s : exit ret = %d", __func__, ret);
	return ret;
}

int qcom_ethqos_serdes_update(struct qcom_ethqos *ethqos,
			      int speed,
			      int interface)
{
	int ret = 0;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		ret = qcom_ethqos_serdes_update_sgmii(ethqos, speed);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		ret = qcom_ethqos_serdes_update_usxgmii(ethqos, speed);
		break;
	default:
		ETHQOSINFO("%s : PHY interface not supported", __func__);
		ret = EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_ethqos_serdes_update);

int qcom_ethqos_serdes_configure_dt(struct qcom_ethqos *ethqos)
{
	struct resource *res = NULL;
	struct platform_device *pdev = ethqos->pdev;
	int ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "serdes");
	ethqos->sgmii_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ethqos->sgmii_base)) {
		dev_err(&pdev->dev, "Can't get sgmii base\n");
		ret = PTR_ERR(ethqos->sgmii_base);
		goto err_mem;
	}

	ethqos->sgmiref_clk = devm_clk_get(&pdev->dev, "sgmi_ref");
	if (IS_ERR(ethqos->sgmiref_clk)) {
		ETHQOSERR("%s : configure_serdes_dt Failed sgmi_ref", __func__);
		ret = PTR_ERR(ethqos->sgmiref_clk);
		goto err_mem;
	}
	ethqos->phyaux_clk = devm_clk_get(&pdev->dev, "phyaux");
	if (IS_ERR(ethqos->phyaux_clk)) {
		ETHQOSERR("%s : configure_serdes_dt Failed phyaux", __func__);
		ret = PTR_ERR(ethqos->phyaux_clk);
		goto err_mem;
	}

	ret = clk_prepare_enable(ethqos->sgmiref_clk);
	if (ret)
		goto err_mem;

	ret = clk_prepare_enable(ethqos->phyaux_clk);
	if (ret)
		goto err_mem;

	return 0;

err_mem:
	ETHQOSERR("%s : configure_serdes_dt Failed", __func__);
	return -1;
}
EXPORT_SYMBOL_GPL(qcom_ethqos_serdes_configure_dt);
MODULE_LICENSE("GPL");
