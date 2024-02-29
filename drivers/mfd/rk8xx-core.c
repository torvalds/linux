// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFD core driver for Rockchip RK8XX
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#include <linux/interrupt.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reboot.h>

struct rk808_reg_data {
	int addr;
	int mask;
	int value;
};

static const struct resource rtc_resources[] = {
	DEFINE_RES_IRQ(RK808_IRQ_RTC_ALARM),
};

static const struct resource rk817_rtc_resources[] = {
	DEFINE_RES_IRQ(RK817_IRQ_RTC_ALARM),
};

static const struct resource rk805_key_resources[] = {
	DEFINE_RES_IRQ(RK805_IRQ_PWRON_RISE),
	DEFINE_RES_IRQ(RK805_IRQ_PWRON_FALL),
};

static struct resource rk806_pwrkey_resources[] = {
	DEFINE_RES_IRQ(RK806_IRQ_PWRON_FALL),
	DEFINE_RES_IRQ(RK806_IRQ_PWRON_RISE),
};

static const struct resource rk817_pwrkey_resources[] = {
	DEFINE_RES_IRQ(RK817_IRQ_PWRON_RISE),
	DEFINE_RES_IRQ(RK817_IRQ_PWRON_FALL),
};

static const struct resource rk817_charger_resources[] = {
	DEFINE_RES_IRQ(RK817_IRQ_PLUG_IN),
	DEFINE_RES_IRQ(RK817_IRQ_PLUG_OUT),
};

static const struct mfd_cell rk805s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk805-pinctrl", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = &rtc_resources[0],
	},
	{	.name = "rk805-pwrkey",
		.num_resources = ARRAY_SIZE(rk805_key_resources),
		.resources = &rk805_key_resources[0],
	},
};

static const struct mfd_cell rk806s[] = {
	{ .name = "rk805-pinctrl", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk805-pwrkey",
		.resources = rk806_pwrkey_resources,
		.num_resources = ARRAY_SIZE(rk806_pwrkey_resources),
	},
};

static const struct mfd_cell rk808s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = rtc_resources,
	},
};

static const struct mfd_cell rk817s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk805-pwrkey",
		.num_resources = ARRAY_SIZE(rk817_pwrkey_resources),
		.resources = &rk817_pwrkey_resources[0],
	},
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rk817_rtc_resources),
		.resources = &rk817_rtc_resources[0],
	},
	{ .name = "rk817-codec", },
	{
		.name = "rk817-charger",
		.num_resources = ARRAY_SIZE(rk817_charger_resources),
		.resources = &rk817_charger_resources[0],
	},
};

static const struct mfd_cell rk818s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = rtc_resources,
	},
};

static const struct rk808_reg_data rk805_pre_init_reg[] = {
	{RK805_BUCK1_CONFIG_REG, RK805_BUCK1_2_ILMAX_MASK,
				 RK805_BUCK1_2_ILMAX_4000MA},
	{RK805_BUCK2_CONFIG_REG, RK805_BUCK1_2_ILMAX_MASK,
				 RK805_BUCK1_2_ILMAX_4000MA},
	{RK805_BUCK3_CONFIG_REG, RK805_BUCK3_4_ILMAX_MASK,
				 RK805_BUCK3_ILMAX_3000MA},
	{RK805_BUCK4_CONFIG_REG, RK805_BUCK3_4_ILMAX_MASK,
				 RK805_BUCK4_ILMAX_3500MA},
	{RK805_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK, BUCK_ILMIN_400MA},
	{RK805_THERMAL_REG, TEMP_HOTDIE_MSK, TEMP115C},
};

static const struct rk808_reg_data rk806_pre_init_reg[] = {
	{ RK806_GPIO_INT_CONFIG, RK806_INT_POL_MSK, RK806_INT_POL_L },
	{ RK806_SYS_CFG3, RK806_SLAVE_RESTART_FUN_MSK, RK806_SLAVE_RESTART_FUN_EN },
	{ RK806_SYS_OPTION, RK806_SYS_ENB2_2M_MSK, RK806_SYS_ENB2_2M_EN },
};

static const struct rk808_reg_data rk808_pre_init_reg[] = {
	{ RK808_BUCK3_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_150MA },
	{ RK808_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK808_BUCK1_CONFIG_REG, BUCK1_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_DCDC_UV_ACT_REG,  BUCK_UV_ACT_MASK, BUCK_UV_ACT_DISABLE},
	{ RK808_VB_MON_REG,       MASK_ALL,         VB_LO_ACT |
						    VB_LO_SEL_3500MV },
};

static const struct rk808_reg_data rk817_pre_init_reg[] = {
	{RK817_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	/* Codec specific registers */
	{ RK817_CODEC_DTOP_VUCTL, MASK_ALL, 0x03 },
	{ RK817_CODEC_DTOP_VUCTIME, MASK_ALL, 0x00 },
	{ RK817_CODEC_DTOP_LPT_SRST, MASK_ALL, 0x00 },
	{ RK817_CODEC_DTOP_DIGEN_CLKE, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_AREF_RTCFG0 not defined in data sheet */
	{ RK817_CODEC_AREF_RTCFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_AREF_RTCFG1, MASK_ALL, 0x06 },
	{ RK817_CODEC_AADC_CFG0, MASK_ALL, 0xc8 },
	/* from vendor driver, CODEC_AADC_CFG1 not defined in data sheet */
	{ RK817_CODEC_AADC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_SR_ACL0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_ALC1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_ALC2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_NG, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_HPF, MASK_ALL, 0x00 },
	{ RK817_CODEC_DADC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AMIC_CFG0, MASK_ALL, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, MASK_ALL, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG2, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_ADAC_CFG0 not defined in data sheet */
	{ RK817_CODEC_ADAC_CFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, MASK_ALL, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, MASK_ALL, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, MASK_ALL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AMIC_CFG0, MASK_ALL, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, MASK_ALL, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DMIC_NG2, MASK_ALL, 0x00 },
	/* from vendor driver, CODEC_ADAC_CFG0 not defined in data sheet */
	{ RK817_CODEC_ADAC_CFG0, MASK_ALL, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, MASK_ALL, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, MASK_ALL, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, MASK_ALL, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, MASK_ALL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, MASK_ALL, 0xff },
	{ RK817_CODEC_DDAC_RVOLR, MASK_ALL, 0xff },
	{ RK817_CODEC_AHP_ANTI0, MASK_ALL, 0x00 },
	{ RK817_CODEC_AHP_ANTI1, MASK_ALL, 0x00 },
	{ RK817_CODEC_AHP_CFG0, MASK_ALL, 0xe0 },
	{ RK817_CODEC_AHP_CFG1, MASK_ALL, 0x1f },
	{ RK817_CODEC_AHP_CP, MASK_ALL, 0x09 },
	{ RK817_CODEC_ACLASSD_CFG1, MASK_ALL, 0x69 },
	{ RK817_CODEC_ACLASSD_CFG2, MASK_ALL, 0x44 },
	{ RK817_CODEC_APLL_CFG0, MASK_ALL, 0x04 },
	{ RK817_CODEC_APLL_CFG1, MASK_ALL, 0x00 },
	{ RK817_CODEC_APLL_CFG2, MASK_ALL, 0x30 },
	{ RK817_CODEC_APLL_CFG3, MASK_ALL, 0x19 },
	{ RK817_CODEC_APLL_CFG4, MASK_ALL, 0x65 },
	{ RK817_CODEC_APLL_CFG5, MASK_ALL, 0x01 },
	{ RK817_CODEC_DI2S_CKM, MASK_ALL, 0x01 },
	{ RK817_CODEC_DI2S_RSD, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_RXCR1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_RXCR2, MASK_ALL, 0x17 },
	{ RK817_CODEC_DI2S_RXCMD_TSD, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_TXCR1, MASK_ALL, 0x00 },
	{ RK817_CODEC_DI2S_TXCR2, MASK_ALL, 0x17 },
	{ RK817_CODEC_DI2S_TXCR3_TXCMD, MASK_ALL, 0x00 },
	{RK817_GPIO_INT_CFG, RK817_INT_POL_MSK, RK817_INT_POL_L},
	{RK817_SYS_CFG(1), RK817_HOTDIE_TEMP_MSK | RK817_TSD_TEMP_MSK,
					   RK817_HOTDIE_105 | RK817_TSD_140},
};

static const struct rk808_reg_data rk818_pre_init_reg[] = {
	/* improve efficiency */
	{ RK818_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_250MA },
	{ RK818_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_250MA },
	{ RK818_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK818_USB_CTRL_REG,	  RK818_USB_ILIM_SEL_MASK,
						    RK818_USB_ILMIN_2000MA },
	/* close charger when usb lower then 3.4V */
	{ RK818_USB_CTRL_REG,	  RK818_USB_CHG_SD_VSEL_MASK,
						    (0x7 << 4) },
	/* no action when vref */
	{ RK818_H5V_EN_REG,	  BIT(1),	    RK818_REF_RDY_CTRL },
	/* enable HDMI 5V */
	{ RK818_H5V_EN_REG,	  BIT(0),	    RK818_H5V_EN },
	{ RK808_VB_MON_REG,	  MASK_ALL,	    VB_LO_ACT |
						    VB_LO_SEL_3500MV },
};

static const struct regmap_irq rk805_irqs[] = {
	[RK805_IRQ_PWRON_RISE] = {
		.mask = RK805_IRQ_PWRON_RISE_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_VB_LOW] = {
		.mask = RK805_IRQ_VB_LOW_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_PWRON] = {
		.mask = RK805_IRQ_PWRON_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_PWRON_LP] = {
		.mask = RK805_IRQ_PWRON_LP_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_HOTDIE] = {
		.mask = RK805_IRQ_HOTDIE_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_RTC_ALARM] = {
		.mask = RK805_IRQ_RTC_ALARM_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_RTC_PERIOD] = {
		.mask = RK805_IRQ_RTC_PERIOD_MSK,
		.reg_offset = 0,
	},
	[RK805_IRQ_PWRON_FALL] = {
		.mask = RK805_IRQ_PWRON_FALL_MSK,
		.reg_offset = 0,
	},
};

static const struct regmap_irq rk806_irqs[] = {
	/* INT_STS0 IRQs */
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_FALL, 0, RK806_INT_STS_PWRON_FALL),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_RISE, 0, RK806_INT_STS_PWRON_RISE),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON, 0, RK806_INT_STS_PWRON),
	REGMAP_IRQ_REG(RK806_IRQ_PWRON_LP, 0, RK806_INT_STS_PWRON_LP),
	REGMAP_IRQ_REG(RK806_IRQ_HOTDIE, 0, RK806_INT_STS_HOTDIE),
	REGMAP_IRQ_REG(RK806_IRQ_VDC_RISE, 0, RK806_INT_STS_VDC_RISE),
	REGMAP_IRQ_REG(RK806_IRQ_VDC_FALL, 0, RK806_INT_STS_VDC_FALL),
	REGMAP_IRQ_REG(RK806_IRQ_VB_LO, 0, RK806_INT_STS_VB_LO),
	/* INT_STS1 IRQs */
	REGMAP_IRQ_REG(RK806_IRQ_REV0, 1, RK806_INT_STS_REV0),
	REGMAP_IRQ_REG(RK806_IRQ_REV1, 1, RK806_INT_STS_REV1),
	REGMAP_IRQ_REG(RK806_IRQ_REV2, 1, RK806_INT_STS_REV2),
	REGMAP_IRQ_REG(RK806_IRQ_CRC_ERROR, 1, RK806_INT_STS_CRC_ERROR),
	REGMAP_IRQ_REG(RK806_IRQ_SLP3_GPIO, 1, RK806_INT_STS_SLP3_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_SLP2_GPIO, 1, RK806_INT_STS_SLP2_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_SLP1_GPIO, 1, RK806_INT_STS_SLP1_GPIO),
	REGMAP_IRQ_REG(RK806_IRQ_WDT, 1, RK806_INT_STS_WDT),
};

static const struct regmap_irq rk808_irqs[] = {
	/* INT_STS */
	[RK808_IRQ_VOUT_LO] = {
		.mask = RK808_IRQ_VOUT_LO_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_VB_LO] = {
		.mask = RK808_IRQ_VB_LO_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_PWRON] = {
		.mask = RK808_IRQ_PWRON_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_PWRON_LP] = {
		.mask = RK808_IRQ_PWRON_LP_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_HOTDIE] = {
		.mask = RK808_IRQ_HOTDIE_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_RTC_ALARM] = {
		.mask = RK808_IRQ_RTC_ALARM_MSK,
		.reg_offset = 0,
	},
	[RK808_IRQ_RTC_PERIOD] = {
		.mask = RK808_IRQ_RTC_PERIOD_MSK,
		.reg_offset = 0,
	},

	/* INT_STS2 */
	[RK808_IRQ_PLUG_IN_INT] = {
		.mask = RK808_IRQ_PLUG_IN_INT_MSK,
		.reg_offset = 1,
	},
	[RK808_IRQ_PLUG_OUT_INT] = {
		.mask = RK808_IRQ_PLUG_OUT_INT_MSK,
		.reg_offset = 1,
	},
};

static const struct regmap_irq rk818_irqs[] = {
	/* INT_STS */
	[RK818_IRQ_VOUT_LO] = {
		.mask = RK818_IRQ_VOUT_LO_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_VB_LO] = {
		.mask = RK818_IRQ_VB_LO_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_PWRON] = {
		.mask = RK818_IRQ_PWRON_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_PWRON_LP] = {
		.mask = RK818_IRQ_PWRON_LP_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_HOTDIE] = {
		.mask = RK818_IRQ_HOTDIE_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_RTC_ALARM] = {
		.mask = RK818_IRQ_RTC_ALARM_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_RTC_PERIOD] = {
		.mask = RK818_IRQ_RTC_PERIOD_MSK,
		.reg_offset = 0,
	},
	[RK818_IRQ_USB_OV] = {
		.mask = RK818_IRQ_USB_OV_MSK,
		.reg_offset = 0,
	},

	/* INT_STS2 */
	[RK818_IRQ_PLUG_IN] = {
		.mask = RK818_IRQ_PLUG_IN_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_PLUG_OUT] = {
		.mask = RK818_IRQ_PLUG_OUT_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_OK] = {
		.mask = RK818_IRQ_CHG_OK_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_TE] = {
		.mask = RK818_IRQ_CHG_TE_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_TS1] = {
		.mask = RK818_IRQ_CHG_TS1_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_TS2] = {
		.mask = RK818_IRQ_TS2_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_CVTLIM] = {
		.mask = RK818_IRQ_CHG_CVTLIM_MSK,
		.reg_offset = 1,
	},
	[RK818_IRQ_DISCHG_ILIM] = {
		.mask = RK818_IRQ_DISCHG_ILIM_MSK,
		.reg_offset = 1,
	},
};

static const struct regmap_irq rk817_irqs[RK817_IRQ_END] = {
	REGMAP_IRQ_REG_LINE(0, 8),
	REGMAP_IRQ_REG_LINE(1, 8),
	REGMAP_IRQ_REG_LINE(2, 8),
	REGMAP_IRQ_REG_LINE(3, 8),
	REGMAP_IRQ_REG_LINE(4, 8),
	REGMAP_IRQ_REG_LINE(5, 8),
	REGMAP_IRQ_REG_LINE(6, 8),
	REGMAP_IRQ_REG_LINE(7, 8),
	REGMAP_IRQ_REG_LINE(8, 8),
	REGMAP_IRQ_REG_LINE(9, 8),
	REGMAP_IRQ_REG_LINE(10, 8),
	REGMAP_IRQ_REG_LINE(11, 8),
	REGMAP_IRQ_REG_LINE(12, 8),
	REGMAP_IRQ_REG_LINE(13, 8),
	REGMAP_IRQ_REG_LINE(14, 8),
	REGMAP_IRQ_REG_LINE(15, 8),
	REGMAP_IRQ_REG_LINE(16, 8),
	REGMAP_IRQ_REG_LINE(17, 8),
	REGMAP_IRQ_REG_LINE(18, 8),
	REGMAP_IRQ_REG_LINE(19, 8),
	REGMAP_IRQ_REG_LINE(20, 8),
	REGMAP_IRQ_REG_LINE(21, 8),
	REGMAP_IRQ_REG_LINE(22, 8),
	REGMAP_IRQ_REG_LINE(23, 8)
};

static struct regmap_irq_chip rk805_irq_chip = {
	.name = "rk805",
	.irqs = rk805_irqs,
	.num_irqs = ARRAY_SIZE(rk805_irqs),
	.num_regs = 1,
	.status_base = RK805_INT_STS_REG,
	.mask_base = RK805_INT_STS_MSK_REG,
	.ack_base = RK805_INT_STS_REG,
	.init_ack_masked = true,
};

static struct regmap_irq_chip rk806_irq_chip = {
	.name = "rk806",
	.irqs = rk806_irqs,
	.num_irqs = ARRAY_SIZE(rk806_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.mask_base = RK806_INT_MSK0,
	.status_base = RK806_INT_STS0,
	.ack_base = RK806_INT_STS0,
	.init_ack_masked = true,
};

static const struct regmap_irq_chip rk808_irq_chip = {
	.name = "rk808",
	.irqs = rk808_irqs,
	.num_irqs = ARRAY_SIZE(rk808_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.status_base = RK808_INT_STS_REG1,
	.mask_base = RK808_INT_STS_MSK_REG1,
	.ack_base = RK808_INT_STS_REG1,
	.init_ack_masked = true,
};

static struct regmap_irq_chip rk817_irq_chip = {
	.name = "rk817",
	.irqs = rk817_irqs,
	.num_irqs = ARRAY_SIZE(rk817_irqs),
	.num_regs = 3,
	.irq_reg_stride = 2,
	.status_base = RK817_INT_STS_REG0,
	.mask_base = RK817_INT_STS_MSK_REG0,
	.ack_base = RK817_INT_STS_REG0,
	.init_ack_masked = true,
};

static const struct regmap_irq_chip rk818_irq_chip = {
	.name = "rk818",
	.irqs = rk818_irqs,
	.num_irqs = ARRAY_SIZE(rk818_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.status_base = RK818_INT_STS_REG1,
	.mask_base = RK818_INT_STS_MSK_REG1,
	.ack_base = RK818_INT_STS_REG1,
	.init_ack_masked = true,
};

static int rk808_power_off(struct sys_off_data *data)
{
	struct rk808 *rk808 = data->cb_data;
	int ret;
	unsigned int reg, bit;

	switch (rk808->variant) {
	case RK805_ID:
		reg = RK805_DEV_CTRL_REG;
		bit = DEV_OFF;
		break;
	case RK806_ID:
		reg = RK806_SYS_CFG3;
		bit = DEV_OFF;
		break;
	case RK808_ID:
		reg = RK808_DEVCTRL_REG,
		bit = DEV_OFF_RST;
		break;
	case RK809_ID:
	case RK817_ID:
		reg = RK817_SYS_CFG(3);
		bit = DEV_OFF;
		break;
	case RK818_ID:
		reg = RK818_DEVCTRL_REG;
		bit = DEV_OFF;
		break;
	default:
		return NOTIFY_DONE;
	}
	ret = regmap_update_bits(rk808->regmap, reg, bit, bit);
	if (ret)
		dev_err(rk808->dev, "Failed to shutdown device!\n");

	return NOTIFY_DONE;
}

static int rk808_restart(struct sys_off_data *data)
{
	struct rk808 *rk808 = data->cb_data;
	unsigned int reg, bit;
	int ret;

	switch (rk808->variant) {
	case RK809_ID:
	case RK817_ID:
		reg = RK817_SYS_CFG(3);
		bit = DEV_RST;
		break;

	default:
		return NOTIFY_DONE;
	}
	ret = regmap_update_bits(rk808->regmap, reg, bit, bit);
	if (ret)
		dev_err(rk808->dev, "Failed to restart device!\n");

	return NOTIFY_DONE;
}

void rk8xx_shutdown(struct device *dev)
{
	struct rk808 *rk808 = dev_get_drvdata(dev);
	int ret;

	switch (rk808->variant) {
	case RK805_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK805_GPIO_IO_POL_REG,
					 SLP_SD_MSK,
					 SHUTDOWN_FUN);
		break;
	case RK809_ID:
	case RK817_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_DN_FUN);
		break;
	default:
		return;
	}
	if (ret)
		dev_warn(dev,
			 "Cannot switch to power down function\n");
}
EXPORT_SYMBOL_GPL(rk8xx_shutdown);

int rk8xx_probe(struct device *dev, int variant, unsigned int irq, struct regmap *regmap)
{
	struct rk808 *rk808;
	const struct rk808_reg_data *pre_init_reg;
	const struct mfd_cell *cells;
	int dual_support = 0;
	int nr_pre_init_regs;
	int nr_cells;
	int ret;
	int i;

	rk808 = devm_kzalloc(dev, sizeof(*rk808), GFP_KERNEL);
	if (!rk808)
		return -ENOMEM;
	rk808->dev = dev;
	rk808->variant = variant;
	rk808->regmap = regmap;
	dev_set_drvdata(dev, rk808);

	switch (rk808->variant) {
	case RK805_ID:
		rk808->regmap_irq_chip = &rk805_irq_chip;
		pre_init_reg = rk805_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk805_pre_init_reg);
		cells = rk805s;
		nr_cells = ARRAY_SIZE(rk805s);
		break;
	case RK806_ID:
		rk808->regmap_irq_chip = &rk806_irq_chip;
		pre_init_reg = rk806_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk806_pre_init_reg);
		cells = rk806s;
		nr_cells = ARRAY_SIZE(rk806s);
		dual_support = IRQF_SHARED;
		break;
	case RK808_ID:
		rk808->regmap_irq_chip = &rk808_irq_chip;
		pre_init_reg = rk808_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk808_pre_init_reg);
		cells = rk808s;
		nr_cells = ARRAY_SIZE(rk808s);
		break;
	case RK818_ID:
		rk808->regmap_irq_chip = &rk818_irq_chip;
		pre_init_reg = rk818_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk818_pre_init_reg);
		cells = rk818s;
		nr_cells = ARRAY_SIZE(rk818s);
		break;
	case RK809_ID:
	case RK817_ID:
		rk808->regmap_irq_chip = &rk817_irq_chip;
		pre_init_reg = rk817_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk817_pre_init_reg);
		cells = rk817s;
		nr_cells = ARRAY_SIZE(rk817s);
		break;
	default:
		dev_err(dev, "Unsupported RK8XX ID %lu\n", rk808->variant);
		return -EINVAL;
	}

	if (!irq)
		return dev_err_probe(dev, -EINVAL, "No interrupt support, no core IRQ\n");

	ret = devm_regmap_add_irq_chip(dev, rk808->regmap, irq,
				       IRQF_ONESHOT | dual_support, -1,
				       rk808->regmap_irq_chip, &rk808->irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add irq_chip\n");

	for (i = 0; i < nr_pre_init_regs; i++) {
		ret = regmap_update_bits(rk808->regmap,
					pre_init_reg[i].addr,
					pre_init_reg[i].mask,
					pre_init_reg[i].value);
		if (ret)
			return dev_err_probe(dev, ret, "0x%x write err\n",
					     pre_init_reg[i].addr);
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cells, nr_cells, NULL, 0,
			      regmap_irq_get_domain(rk808->irq_data));
	if (ret)
		return dev_err_probe(dev, ret, "failed to add MFD devices\n");

	if (device_property_read_bool(dev, "rockchip,system-power-controller") ||
	    device_property_read_bool(dev, "system-power-controller")) {
		ret = devm_register_sys_off_handler(dev,
				    SYS_OFF_MODE_POWER_OFF_PREPARE, SYS_OFF_PRIO_HIGH,
				    &rk808_power_off, rk808);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to register poweroff handler\n");

		switch (rk808->variant) {
		case RK809_ID:
		case RK817_ID:
			ret = devm_register_sys_off_handler(dev,
							    SYS_OFF_MODE_RESTART, SYS_OFF_PRIO_HIGH,
							    &rk808_restart, rk808);
			if (ret)
				dev_warn(dev, "failed to register rst handler, %d\n", ret);
			break;
		default:
			dev_dbg(dev, "pmic controlled board reset not supported\n");
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk8xx_probe);

int rk8xx_suspend(struct device *dev)
{
	struct rk808 *rk808 = dev_get_drvdata(dev);
	int ret = 0;

	switch (rk808->variant) {
	case RK805_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK805_GPIO_IO_POL_REG,
					 SLP_SD_MSK,
					 SLEEP_FUN);
		break;
	case RK809_ID:
	case RK817_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_SLP_FUN);
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rk8xx_suspend);

int rk8xx_resume(struct device *dev)
{
	struct rk808 *rk808 = dev_get_drvdata(dev);
	int ret = 0;

	switch (rk808->variant) {
	case RK809_ID:
	case RK817_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_NULL_FUN);
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(rk8xx_resume);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_DESCRIPTION("RK8xx PMIC core");
