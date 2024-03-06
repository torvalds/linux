// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/clk.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define QSERDES_QMP_PLL					0x0
#define QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0		(QSERDES_QMP_PLL + 0x1ac)
#define QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0		(QSERDES_QMP_PLL + 0x1b0)
#define QSERDES_COM_BIN_VCOCAL_HSCLK_SEL		(QSERDES_QMP_PLL + 0x1bc)
#define QSERDES_COM_CORE_CLK_EN				(QSERDES_QMP_PLL + 0x174)
#define QSERDES_COM_CORECLK_DIV_MODE0			(QSERDES_QMP_PLL + 0x168)
#define QSERDES_COM_CP_CTRL_MODE0			(QSERDES_QMP_PLL + 0x74)
#define QSERDES_COM_DEC_START_MODE0			(QSERDES_QMP_PLL + 0xbc)
#define QSERDES_COM_DIV_FRAC_START1_MODE0		(QSERDES_QMP_PLL + 0xcc)
#define QSERDES_COM_DIV_FRAC_START2_MODE0		(QSERDES_QMP_PLL + 0xd0)
#define QSERDES_COM_DIV_FRAC_START3_MODE0		(QSERDES_QMP_PLL + 0xd4)
#define QSERDES_COM_HSCLK_HS_SWITCH_SEL			(QSERDES_QMP_PLL + 0x15c)
#define QSERDES_COM_HSCLK_SEL				(QSERDES_QMP_PLL + 0x158)
#define QSERDES_COM_LOCK_CMP1_MODE0			(QSERDES_QMP_PLL + 0xac)
#define QSERDES_COM_LOCK_CMP2_MODE0			(QSERDES_QMP_PLL + 0xb0)
#define QSERDES_COM_PLL_CCTRL_MODE0			(QSERDES_QMP_PLL + 0x84)
#define QSERDES_COM_PLL_IVCO				(QSERDES_QMP_PLL + 0x58)
#define QSERDES_COM_PLL_RCTRL_MODE0			(QSERDES_QMP_PLL + 0x7c)
#define QSERDES_COM_SYSCLK_EN_SEL			(QSERDES_QMP_PLL + 0x94)
#define QSERDES_COM_VCO_TUNE1_MODE0			(QSERDES_QMP_PLL + 0x110)
#define QSERDES_COM_VCO_TUNE2_MODE0			(QSERDES_QMP_PLL + 0x114)
#define QSERDES_COM_VCO_TUNE_INITVAL2			(QSERDES_QMP_PLL + 0x124)
#define QSERDES_COM_C_READY_STATUS			(QSERDES_QMP_PLL + 0x178)
#define QSERDES_COM_CMN_STATUS				(QSERDES_QMP_PLL + 0x140)

#define QSERDES_RX					0x600
#define QSERDES_RX_UCDR_FO_GAIN				(QSERDES_RX + 0x8)
#define QSERDES_RX_UCDR_SO_GAIN				(QSERDES_RX + 0x14)
#define QSERDES_RX_UCDR_FASTLOCK_FO_GAIN		(QSERDES_RX + 0x30)
#define QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE	(QSERDES_RX + 0x34)
#define QSERDES_RX_UCDR_FASTLOCK_COUNT_LOW		(QSERDES_RX + 0x3c)
#define QSERDES_RX_UCDR_FASTLOCK_COUNT_HIGH		(QSERDES_RX + 0x40)
#define QSERDES_RX_UCDR_PI_CONTROLS			(QSERDES_RX + 0x44)
#define QSERDES_RX_UCDR_PI_CTRL2			(QSERDES_RX + 0x48)
#define QSERDES_RX_RX_TERM_BW				(QSERDES_RX + 0x80)
#define QSERDES_RX_VGA_CAL_CNTRL2			(QSERDES_RX + 0xd8)
#define QSERDES_RX_GM_CAL				(QSERDES_RX + 0xdc)
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL1		(QSERDES_RX + 0xe8)
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2		(QSERDES_RX + 0xec)
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3		(QSERDES_RX + 0xf0)
#define QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4		(QSERDES_RX + 0xf4)
#define QSERDES_RX_RX_IDAC_TSETTLE_LOW			(QSERDES_RX + 0xf8)
#define QSERDES_RX_RX_IDAC_TSETTLE_HIGH			(QSERDES_RX + 0xfc)
#define QSERDES_RX_RX_IDAC_MEASURE_TIME			(QSERDES_RX + 0x100)
#define QSERDES_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1		(QSERDES_RX + 0x110)
#define QSERDES_RX_RX_OFFSET_ADAPTOR_CNTRL2		(QSERDES_RX + 0x114)
#define QSERDES_RX_SIGDET_CNTRL				(QSERDES_RX + 0x11c)
#define QSERDES_RX_SIGDET_DEGLITCH_CNTRL		(QSERDES_RX + 0x124)
#define QSERDES_RX_RX_BAND				(QSERDES_RX + 0x128)
#define QSERDES_RX_RX_MODE_00_LOW			(QSERDES_RX + 0x15c)
#define QSERDES_RX_RX_MODE_00_HIGH			(QSERDES_RX + 0x160)
#define QSERDES_RX_RX_MODE_00_HIGH2			(QSERDES_RX + 0x164)
#define QSERDES_RX_RX_MODE_00_HIGH3			(QSERDES_RX + 0x168)
#define QSERDES_RX_RX_MODE_00_HIGH4			(QSERDES_RX + 0x16c)
#define QSERDES_RX_RX_MODE_01_LOW			(QSERDES_RX + 0x170)
#define QSERDES_RX_RX_MODE_01_HIGH			(QSERDES_RX + 0x174)
#define QSERDES_RX_RX_MODE_01_HIGH2			(QSERDES_RX + 0x178)
#define QSERDES_RX_RX_MODE_01_HIGH3			(QSERDES_RX + 0x17c)
#define QSERDES_RX_RX_MODE_01_HIGH4			(QSERDES_RX + 0x180)
#define QSERDES_RX_RX_MODE_10_LOW			(QSERDES_RX + 0x184)
#define QSERDES_RX_RX_MODE_10_HIGH			(QSERDES_RX + 0x188)
#define QSERDES_RX_RX_MODE_10_HIGH2			(QSERDES_RX + 0x18c)
#define QSERDES_RX_RX_MODE_10_HIGH3			(QSERDES_RX + 0x190)
#define QSERDES_RX_RX_MODE_10_HIGH4			(QSERDES_RX + 0x194)
#define QSERDES_RX_DCC_CTRL1				(QSERDES_RX + 0x1a8)

#define QSERDES_TX					0x400
#define QSERDES_TX_TX_BAND				(QSERDES_TX + 0x24)
#define QSERDES_TX_SLEW_CNTL				(QSERDES_TX + 0x28)
#define QSERDES_TX_RES_CODE_LANE_OFFSET_TX		(QSERDES_TX + 0x3c)
#define QSERDES_TX_RES_CODE_LANE_OFFSET_RX		(QSERDES_TX + 0x40)
#define QSERDES_TX_LANE_MODE_1				(QSERDES_TX + 0x84)
#define QSERDES_TX_LANE_MODE_3				(QSERDES_TX + 0x8c)
#define QSERDES_TX_RCV_DETECT_LVL_2			(QSERDES_TX + 0xa4)
#define QSERDES_TX_TRAN_DRVR_EMP_EN			(QSERDES_TX + 0xc0)

#define QSERDES_PCS					0xC00
#define QSERDES_PCS_PHY_START				(QSERDES_PCS + 0x0)
#define QSERDES_PCS_POWER_DOWN_CONTROL			(QSERDES_PCS + 0x4)
#define QSERDES_PCS_SW_RESET				(QSERDES_PCS + 0x8)
#define QSERDES_PCS_LINE_RESET_TIME			(QSERDES_PCS + 0xc)
#define QSERDES_PCS_TX_LARGE_AMP_DRV_LVL		(QSERDES_PCS + 0x20)
#define QSERDES_PCS_TX_SMALL_AMP_DRV_LVL		(QSERDES_PCS + 0x28)
#define QSERDES_PCS_TX_MID_TERM_CTRL1			(QSERDES_PCS + 0xd8)
#define QSERDES_PCS_TX_MID_TERM_CTRL2			(QSERDES_PCS + 0xdc)
#define QSERDES_PCS_SGMII_MISC_CTRL8			(QSERDES_PCS + 0x118)
#define QSERDES_PCS_PCS_READY_STATUS			(QSERDES_PCS + 0x94)

#define QSERDES_COM_C_READY				BIT(0)
#define QSERDES_PCS_READY				BIT(0)
#define QSERDES_PCS_SGMIIPHY_READY			BIT(7)
#define QSERDES_COM_C_PLL_LOCKED			BIT(1)

struct qcom_dwmac_sgmii_phy_data {
	struct regmap *regmap;
	struct clk *refclk;
	int speed;
};

static void qcom_dwmac_sgmii_phy_init_1g(struct regmap *regmap)
{
	regmap_write(regmap, QSERDES_PCS_SW_RESET, 0x01);
	regmap_write(regmap, QSERDES_PCS_POWER_DOWN_CONTROL, 0x01);

	regmap_write(regmap, QSERDES_COM_PLL_IVCO, 0x0F);
	regmap_write(regmap, QSERDES_COM_CP_CTRL_MODE0, 0x06);
	regmap_write(regmap, QSERDES_COM_PLL_RCTRL_MODE0, 0x16);
	regmap_write(regmap, QSERDES_COM_PLL_CCTRL_MODE0, 0x36);
	regmap_write(regmap, QSERDES_COM_SYSCLK_EN_SEL, 0x1A);
	regmap_write(regmap, QSERDES_COM_LOCK_CMP1_MODE0, 0x0A);
	regmap_write(regmap, QSERDES_COM_LOCK_CMP2_MODE0, 0x1A);
	regmap_write(regmap, QSERDES_COM_DEC_START_MODE0, 0x82);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x03);
	regmap_write(regmap, QSERDES_COM_VCO_TUNE1_MODE0, 0x24);

	regmap_write(regmap, QSERDES_COM_VCO_TUNE2_MODE0, 0x02);
	regmap_write(regmap, QSERDES_COM_VCO_TUNE_INITVAL2, 0x00);
	regmap_write(regmap, QSERDES_COM_HSCLK_SEL, 0x04);
	regmap_write(regmap, QSERDES_COM_HSCLK_HS_SWITCH_SEL, 0x00);
	regmap_write(regmap, QSERDES_COM_CORECLK_DIV_MODE0, 0x0A);
	regmap_write(regmap, QSERDES_COM_CORE_CLK_EN, 0x00);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0xB9);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x1E);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_HSCLK_SEL, 0x11);

	regmap_write(regmap, QSERDES_TX_TX_BAND, 0x05);
	regmap_write(regmap, QSERDES_TX_SLEW_CNTL, 0x0A);
	regmap_write(regmap, QSERDES_TX_RES_CODE_LANE_OFFSET_TX, 0x09);
	regmap_write(regmap, QSERDES_TX_RES_CODE_LANE_OFFSET_RX, 0x09);
	regmap_write(regmap, QSERDES_TX_LANE_MODE_1, 0x05);
	regmap_write(regmap, QSERDES_TX_LANE_MODE_3, 0x00);
	regmap_write(regmap, QSERDES_TX_RCV_DETECT_LVL_2, 0x12);
	regmap_write(regmap, QSERDES_TX_TRAN_DRVR_EMP_EN, 0x0C);

	regmap_write(regmap, QSERDES_RX_UCDR_FO_GAIN, 0x0A);
	regmap_write(regmap, QSERDES_RX_UCDR_SO_GAIN, 0x06);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_FO_GAIN, 0x0A);
	regmap_write(regmap, QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x7F);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_COUNT_LOW, 0x00);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x01);
	regmap_write(regmap, QSERDES_RX_UCDR_PI_CONTROLS, 0x81);
	regmap_write(regmap, QSERDES_RX_UCDR_PI_CTRL2, 0x80);
	regmap_write(regmap, QSERDES_RX_RX_TERM_BW, 0x04);
	regmap_write(regmap, QSERDES_RX_VGA_CAL_CNTRL2, 0x08);
	regmap_write(regmap, QSERDES_RX_GM_CAL, 0x0F);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL1, 0x04);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x00);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4A);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0x0A);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_TSETTLE_LOW, 0x80);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_TSETTLE_HIGH, 0x01);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_MEASURE_TIME, 0x20);
	regmap_write(regmap, QSERDES_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x17);
	regmap_write(regmap, QSERDES_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x00);
	regmap_write(regmap, QSERDES_RX_SIGDET_CNTRL, 0x0F);
	regmap_write(regmap, QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x1E);
	regmap_write(regmap, QSERDES_RX_RX_BAND, 0x05);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_LOW, 0xE0);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH3, 0x09);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH4, 0xB1);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_LOW, 0xE0);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH3, 0x09);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH4, 0xB1);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_LOW, 0xE0);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH3, 0x3B);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH4, 0xB7);
	regmap_write(regmap, QSERDES_RX_DCC_CTRL1, 0x0C);

	regmap_write(regmap, QSERDES_PCS_LINE_RESET_TIME, 0x0C);
	regmap_write(regmap, QSERDES_PCS_TX_LARGE_AMP_DRV_LVL, 0x1F);
	regmap_write(regmap, QSERDES_PCS_TX_SMALL_AMP_DRV_LVL, 0x03);
	regmap_write(regmap, QSERDES_PCS_TX_MID_TERM_CTRL1, 0x83);
	regmap_write(regmap, QSERDES_PCS_TX_MID_TERM_CTRL2, 0x08);
	regmap_write(regmap, QSERDES_PCS_SGMII_MISC_CTRL8, 0x0C);
	regmap_write(regmap, QSERDES_PCS_SW_RESET, 0x00);

	regmap_write(regmap, QSERDES_PCS_PHY_START, 0x01);
}

static void qcom_dwmac_sgmii_phy_init_2p5g(struct regmap *regmap)
{
	regmap_write(regmap, QSERDES_PCS_SW_RESET, 0x01);
	regmap_write(regmap, QSERDES_PCS_POWER_DOWN_CONTROL, 0x01);

	regmap_write(regmap, QSERDES_COM_PLL_IVCO, 0x0F);
	regmap_write(regmap, QSERDES_COM_CP_CTRL_MODE0, 0x06);
	regmap_write(regmap, QSERDES_COM_PLL_RCTRL_MODE0, 0x16);
	regmap_write(regmap, QSERDES_COM_PLL_CCTRL_MODE0, 0x36);
	regmap_write(regmap, QSERDES_COM_SYSCLK_EN_SEL, 0x1A);
	regmap_write(regmap, QSERDES_COM_LOCK_CMP1_MODE0, 0x1A);
	regmap_write(regmap, QSERDES_COM_LOCK_CMP2_MODE0, 0x41);
	regmap_write(regmap, QSERDES_COM_DEC_START_MODE0, 0x7A);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START1_MODE0, 0x00);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START2_MODE0, 0x20);
	regmap_write(regmap, QSERDES_COM_DIV_FRAC_START3_MODE0, 0x01);
	regmap_write(regmap, QSERDES_COM_VCO_TUNE1_MODE0, 0xA1);

	regmap_write(regmap, QSERDES_COM_VCO_TUNE2_MODE0, 0x02);
	regmap_write(regmap, QSERDES_COM_VCO_TUNE_INITVAL2, 0x00);
	regmap_write(regmap, QSERDES_COM_HSCLK_SEL, 0x03);
	regmap_write(regmap, QSERDES_COM_HSCLK_HS_SWITCH_SEL, 0x00);
	regmap_write(regmap, QSERDES_COM_CORECLK_DIV_MODE0, 0x05);
	regmap_write(regmap, QSERDES_COM_CORE_CLK_EN, 0x00);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0xCD);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x1C);
	regmap_write(regmap, QSERDES_COM_BIN_VCOCAL_HSCLK_SEL, 0x11);

	regmap_write(regmap, QSERDES_TX_TX_BAND, 0x04);
	regmap_write(regmap, QSERDES_TX_SLEW_CNTL, 0x0A);
	regmap_write(regmap, QSERDES_TX_RES_CODE_LANE_OFFSET_TX, 0x09);
	regmap_write(regmap, QSERDES_TX_RES_CODE_LANE_OFFSET_RX, 0x02);
	regmap_write(regmap, QSERDES_TX_LANE_MODE_1, 0x05);
	regmap_write(regmap, QSERDES_TX_LANE_MODE_3, 0x00);
	regmap_write(regmap, QSERDES_TX_RCV_DETECT_LVL_2, 0x12);
	regmap_write(regmap, QSERDES_TX_TRAN_DRVR_EMP_EN, 0x0C);

	regmap_write(regmap, QSERDES_RX_UCDR_FO_GAIN, 0x0A);
	regmap_write(regmap, QSERDES_RX_UCDR_SO_GAIN, 0x06);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_FO_GAIN, 0x0A);
	regmap_write(regmap, QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x7F);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_COUNT_LOW, 0x00);
	regmap_write(regmap, QSERDES_RX_UCDR_FASTLOCK_COUNT_HIGH, 0x01);
	regmap_write(regmap, QSERDES_RX_UCDR_PI_CONTROLS, 0x81);
	regmap_write(regmap, QSERDES_RX_UCDR_PI_CTRL2, 0x80);
	regmap_write(regmap, QSERDES_RX_RX_TERM_BW, 0x00);
	regmap_write(regmap, QSERDES_RX_VGA_CAL_CNTRL2, 0x08);
	regmap_write(regmap, QSERDES_RX_GM_CAL, 0x0F);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL1, 0x04);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x00);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x4A);
	regmap_write(regmap, QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0x0A);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_TSETTLE_LOW, 0x80);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_TSETTLE_HIGH, 0x01);
	regmap_write(regmap, QSERDES_RX_RX_IDAC_MEASURE_TIME, 0x20);
	regmap_write(regmap, QSERDES_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1, 0x17);
	regmap_write(regmap, QSERDES_RX_RX_OFFSET_ADAPTOR_CNTRL2, 0x00);
	regmap_write(regmap, QSERDES_RX_SIGDET_CNTRL, 0x0F);
	regmap_write(regmap, QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x1E);
	regmap_write(regmap, QSERDES_RX_RX_BAND, 0x18);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_LOW, 0x18);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH3, 0x0C);
	regmap_write(regmap, QSERDES_RX_RX_MODE_00_HIGH4, 0xB8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_LOW, 0xE0);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH3, 0x09);
	regmap_write(regmap, QSERDES_RX_RX_MODE_01_HIGH4, 0xB1);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_LOW, 0xE0);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH2, 0xC8);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH3, 0x3B);
	regmap_write(regmap, QSERDES_RX_RX_MODE_10_HIGH4, 0xB7);
	regmap_write(regmap, QSERDES_RX_DCC_CTRL1, 0x0C);

	regmap_write(regmap, QSERDES_PCS_LINE_RESET_TIME, 0x0C);
	regmap_write(regmap, QSERDES_PCS_TX_LARGE_AMP_DRV_LVL, 0x1F);
	regmap_write(regmap, QSERDES_PCS_TX_SMALL_AMP_DRV_LVL, 0x03);
	regmap_write(regmap, QSERDES_PCS_TX_MID_TERM_CTRL1, 0x83);
	regmap_write(regmap, QSERDES_PCS_TX_MID_TERM_CTRL2, 0x08);
	regmap_write(regmap, QSERDES_PCS_SGMII_MISC_CTRL8, 0x8C);
	regmap_write(regmap, QSERDES_PCS_SW_RESET, 0x00);

	regmap_write(regmap, QSERDES_PCS_PHY_START, 0x01);
}

static inline int
qcom_dwmac_sgmii_phy_poll_status(struct regmap *regmap, unsigned int reg,
				 unsigned int bit)
{
	unsigned int val;

	return regmap_read_poll_timeout(regmap, reg, val,
					val & bit, 1500, 750000);
}

static int qcom_dwmac_sgmii_phy_calibrate(struct phy *phy)
{
	struct qcom_dwmac_sgmii_phy_data *data = phy_get_drvdata(phy);
	struct device *dev = phy->dev.parent;

	switch (data->speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		qcom_dwmac_sgmii_phy_init_1g(data->regmap);
		break;
	case SPEED_2500:
		qcom_dwmac_sgmii_phy_init_2p5g(data->regmap);
		break;
	}

	if (qcom_dwmac_sgmii_phy_poll_status(data->regmap,
					     QSERDES_COM_C_READY_STATUS,
					     QSERDES_COM_C_READY)) {
		dev_err(dev, "QSERDES_COM_C_READY_STATUS timed-out");
		return -ETIMEDOUT;
	}

	if (qcom_dwmac_sgmii_phy_poll_status(data->regmap,
					     QSERDES_PCS_PCS_READY_STATUS,
					     QSERDES_PCS_READY)) {
		dev_err(dev, "PCS_READY timed-out");
		return -ETIMEDOUT;
	}

	if (qcom_dwmac_sgmii_phy_poll_status(data->regmap,
					     QSERDES_PCS_PCS_READY_STATUS,
					     QSERDES_PCS_SGMIIPHY_READY)) {
		dev_err(dev, "SGMIIPHY_READY timed-out");
		return -ETIMEDOUT;
	}

	if (qcom_dwmac_sgmii_phy_poll_status(data->regmap,
					     QSERDES_COM_CMN_STATUS,
					     QSERDES_COM_C_PLL_LOCKED)) {
		dev_err(dev, "PLL Lock Status timed-out");
		return -ETIMEDOUT;
	}

	return 0;
}

static int qcom_dwmac_sgmii_phy_power_on(struct phy *phy)
{
	struct qcom_dwmac_sgmii_phy_data *data = phy_get_drvdata(phy);

	return clk_prepare_enable(data->refclk);
}

static int qcom_dwmac_sgmii_phy_power_off(struct phy *phy)
{
	struct qcom_dwmac_sgmii_phy_data *data = phy_get_drvdata(phy);

	regmap_write(data->regmap, QSERDES_PCS_TX_MID_TERM_CTRL2, 0x08);
	regmap_write(data->regmap, QSERDES_PCS_SW_RESET, 0x01);
	udelay(100);
	regmap_write(data->regmap, QSERDES_PCS_SW_RESET, 0x00);
	regmap_write(data->regmap, QSERDES_PCS_PHY_START, 0x01);

	clk_disable_unprepare(data->refclk);

	return 0;
}

static int qcom_dwmac_sgmii_phy_set_speed(struct phy *phy, int speed)
{
	struct qcom_dwmac_sgmii_phy_data *data = phy_get_drvdata(phy);

	if (speed != data->speed)
		data->speed = speed;

	return qcom_dwmac_sgmii_phy_calibrate(phy);
}

static const struct phy_ops qcom_dwmac_sgmii_phy_ops = {
	.power_on	= qcom_dwmac_sgmii_phy_power_on,
	.power_off	= qcom_dwmac_sgmii_phy_power_off,
	.set_speed	= qcom_dwmac_sgmii_phy_set_speed,
	.calibrate	= qcom_dwmac_sgmii_phy_calibrate,
	.owner		= THIS_MODULE,
};

static const struct regmap_config qcom_dwmac_sgmii_phy_regmap_cfg = {
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.use_relaxed_mmio	= true,
	.disable_locking	= true,
};

static int qcom_dwmac_sgmii_phy_probe(struct platform_device *pdev)
{
	struct qcom_dwmac_sgmii_phy_data *data;
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	void __iomem *base;
	struct phy *phy;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->speed = SPEED_10;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data->regmap = devm_regmap_init_mmio(dev, base,
					     &qcom_dwmac_sgmii_phy_regmap_cfg);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	phy = devm_phy_create(dev, NULL, &qcom_dwmac_sgmii_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	data->refclk = devm_clk_get(dev, "sgmi_ref");
	if (IS_ERR(data->refclk))
		return PTR_ERR(data->refclk);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	phy_set_drvdata(phy, data);

	return 0;
}

static const struct of_device_id qcom_dwmac_sgmii_phy_of_match[] = {
	{ .compatible = "qcom,sa8775p-dwmac-sgmii-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_dwmac_sgmii_phy_of_match);

static struct platform_driver qcom_dwmac_sgmii_phy_driver = {
	.probe	= qcom_dwmac_sgmii_phy_probe,
	.driver = {
		.name	= "qcom-dwmac-sgmii-phy",
		.of_match_table	= qcom_dwmac_sgmii_phy_of_match,
	}
};

module_platform_driver(qcom_dwmac_sgmii_phy_driver);

MODULE_DESCRIPTION("Qualcomm DWMAC SGMII PHY driver");
MODULE_LICENSE("GPL");
