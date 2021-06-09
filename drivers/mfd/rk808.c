// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFD core driver for Rockchip RK808/RK818
 *
 * Copyright (c) 2014-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/devinfo.h>

struct rk808_reg_data {
	int addr;
	int mask;
	int value;
};

static bool rk808_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Notes:
	 * - Technically the ROUND_30s bit makes RTC_CTRL_REG volatile, but
	 *   we don't use that feature.  It's better to cache.
	 * - It's unlikely we care that RK808_DEVCTRL_REG is volatile since
	 *   bits are cleared in case when we shutoff anyway, but better safe.
	 */

	switch (reg) {
	case RK808_SECONDS_REG ... RK808_WEEKS_REG:
	case RK808_RTC_STATUS_REG:
	case RK808_VB_MON_REG:
	case RK808_THERMAL_REG:
	case RK808_DCDC_UV_STS_REG:
	case RK808_LDO_UV_STS_REG:
	case RK808_DCDC_PG_REG:
	case RK808_LDO_PG_REG:
	case RK808_DEVCTRL_REG:
	case RK808_INT_STS_REG1:
	case RK808_INT_STS_REG2:
		return true;
	}

	return false;
}

static bool rk817_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Notes:
	 * - Technically the ROUND_30s bit makes RTC_CTRL_REG volatile, but
	 *   we don't use that feature.  It's better to cache.
	 */

	switch (reg) {
	case RK817_SECONDS_REG ... RK817_WEEKS_REG:
	case RK817_RTC_STATUS_REG:
	case RK817_ADC_CONFIG0 ... RK817_CURE_ADC_K0:
	case RK817_CHRG_STS:
	case RK817_CHRG_OUT:
	case RK817_CHRG_IN:
	case RK817_SYS_STS:
	case RK817_INT_STS_REG0:
	case RK817_INT_STS_REG1:
	case RK817_INT_STS_REG2:
		return true;
	}

	return false;
}

static bool rk818_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Notes:
	 * - Technically the ROUND_30s bit makes RTC_CTRL_REG volatile, but
	 *   we don't use that feature.  It's better to cache.
	 * - It's unlikely we care that RK808_DEVCTRL_REG is volatile since
	 *   bits are cleared in case when we shutoff anyway, but better safe.
	 */

	switch (reg) {
	case RK808_SECONDS_REG ... RK808_WEEKS_REG:
	case RK808_RTC_STATUS_REG:
	case RK808_VB_MON_REG:
	case RK808_THERMAL_REG:
	case RK808_DCDC_EN_REG:
	case RK808_LDO_EN_REG:
	case RK808_DCDC_UV_STS_REG:
	case RK808_LDO_UV_STS_REG:
	case RK808_DCDC_PG_REG:
	case RK808_LDO_PG_REG:
	case RK808_DEVCTRL_REG:
	case RK808_INT_STS_REG1:
	case RK808_INT_STS_REG2:
	case RK808_INT_STS_MSK_REG1:
	case RK808_INT_STS_MSK_REG2:
	case RK816_INT_STS_REG1:
	case RK816_INT_STS_MSK_REG1:
	case RK818_SUP_STS_REG ... RK818_SAVE_DATA19:
		return true;
	}

	return false;
}

static const struct regmap_config rk818_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK818_SAVE_DATA19,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk818_is_volatile_reg,
};

static const struct regmap_config rk805_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK805_OFF_SOURCE_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk808_is_volatile_reg,
};

static const struct regmap_config rk808_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK808_IO_POL_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk808_is_volatile_reg,
};

static const struct regmap_config rk816_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK816_DATA18_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk818_is_volatile_reg,
};

static const struct regmap_config rk817_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK817_GPIO_INT_CFG,
	.num_reg_defaults_raw = RK817_GPIO_INT_CFG + 1,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk817_is_volatile_reg,
};

static struct resource rtc_resources[] = {
	DEFINE_RES_IRQ(RK808_IRQ_RTC_ALARM),
};

static struct resource rk816_rtc_resources[] = {
	DEFINE_RES_IRQ(RK816_IRQ_RTC_ALARM),
};

static struct resource rk817_rtc_resources[] = {
	DEFINE_RES_IRQ(RK817_IRQ_RTC_ALARM),
};

static struct resource rk805_key_resources[] = {
	DEFINE_RES_IRQ(RK805_IRQ_PWRON_FALL),
	DEFINE_RES_IRQ(RK805_IRQ_PWRON_RISE),
};

static struct resource rk816_pwrkey_resources[] = {
	DEFINE_RES_IRQ(RK816_IRQ_PWRON_FALL),
	DEFINE_RES_IRQ(RK816_IRQ_PWRON_RISE),
};

static struct resource rk817_pwrkey_resources[] = {
	DEFINE_RES_IRQ(RK817_IRQ_PWRON_FALL),
	DEFINE_RES_IRQ(RK817_IRQ_PWRON_RISE),
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

static const struct mfd_cell rk808s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = rtc_resources,
	},
};

static const struct mfd_cell rk816s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk805-pinctrl", },
	{ .name = "rk816-battery", .of_compatible = "rk816-battery", },
	{
		.name = "rk805-pwrkey",
		.num_resources = ARRAY_SIZE(rk816_pwrkey_resources),
		.resources = &rk816_pwrkey_resources[0],
	},
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rk816_rtc_resources),
		.resources = &rk816_rtc_resources[0],
	},
};

static const struct mfd_cell rk817s[] = {
	{ .name = "rk808-clkout",},
	{ .name = "rk808-regulator",},
	{ .name = "rk817-battery", .of_compatible = "rk817,battery", },
	{ .name = "rk817-charger", .of_compatible = "rk817,charger", },
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
	{
		.name = "rk817-codec",
		.of_compatible = "rockchip,rk817-codec",
	},
};

static const struct mfd_cell rk818s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk818-battery", .of_compatible = "rk818-battery", },
	{ .name = "rk818-charger", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = rtc_resources,
	},
};

static const struct rk808_reg_data rk805_pre_init_reg[] = {
	{RK805_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK, BUCK_ILMIN_400MA},
	{RK805_GPIO_IO_POL_REG, SLP_SD_MSK, SLEEP_FUN},
	{RK805_THERMAL_REG, TEMP_HOTDIE_MSK, TEMP115C},
	{RK808_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
};

static struct rk808_reg_data rk805_suspend_reg[] = {
	{RK805_BUCK3_CONFIG_REG, PWM_MODE_MSK, AUTO_PWM_MODE},
};

static struct rk808_reg_data rk805_resume_reg[] = {
	{RK805_BUCK3_CONFIG_REG, PWM_MODE_MSK, FPWM_MODE},
};

static const struct rk808_reg_data rk808_pre_init_reg[] = {
	{ RK808_BUCK3_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_150MA },
	{ RK808_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK808_BUCK1_CONFIG_REG, BUCK1_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_DCDC_UV_ACT_REG,  BUCK_UV_ACT_MASK, BUCK_UV_ACT_DISABLE},
	{ RK808_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	{ RK808_VB_MON_REG,       MASK_ALL,         VB_LO_ACT |
						    VB_LO_SEL_3500MV },
};

static const struct rk808_reg_data rk816_pre_init_reg[] = {
	/* buck4 Max ILMIT*/
	{ RK816_BUCK4_CONFIG_REG, REG_WRITE_MSK, BUCK4_MAX_ILIMIT },
	/* hotdie temperature: 105c*/
	{ RK816_THERMAL_REG, REG_WRITE_MSK, TEMP105C },
	/* set buck 12.5mv/us */
	{ RK816_BUCK1_CONFIG_REG, BUCK_RATE_MSK, BUCK_RATE_12_5MV_US },
	{ RK816_BUCK2_CONFIG_REG, BUCK_RATE_MSK, BUCK_RATE_12_5MV_US },
	/* enable RTC_PERIOD & RTC_ALARM int */
	{ RK816_INT_STS_MSK_REG2, REG_WRITE_MSK, RTC_PERIOD_ALARM_INT_EN },
	/* set bat 3.0 low and act shutdown */
	{ RK816_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK816_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN },
	/* enable PWRON rising/faling int */
	{ RK816_INT_STS_MSK_REG1, REG_WRITE_MSK, RK816_PWRON_FALL_RISE_INT_EN },
	/* enable PLUG IN/OUT int */
	{ RK816_INT_STS_MSK_REG3, REG_WRITE_MSK, PLUGIN_OUT_INT_EN },
	/* clear int flags */
	{ RK816_INT_STS_REG1, REG_WRITE_MSK, ALL_INT_FLAGS_ST },
	{ RK816_INT_STS_REG2, REG_WRITE_MSK, ALL_INT_FLAGS_ST },
	{ RK816_INT_STS_REG3, REG_WRITE_MSK, ALL_INT_FLAGS_ST },
	{ RK816_DCDC_EN_REG2, BOOST_EN_MASK, BOOST_DISABLE },
	/* set write mask bit 1, otherwise 'is_enabled()' get wrong status */
	{ RK816_LDO_EN_REG1, REGS_WMSK, REGS_WMSK },
	{ RK816_LDO_EN_REG2, REGS_WMSK, REGS_WMSK },
};

static const struct rk808_reg_data rk817_pre_init_reg[] = {
	{RK817_SYS_CFG(3), RK817_SLPPOL_MSK, RK817_SLPPOL_L},
	{RK817_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
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
	{ RK808_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	{ RK808_VB_MON_REG,	  MASK_ALL,	    VB_LO_ACT |
						    VB_LO_SEL_3500MV },
	{RK808_CLK32OUT_REG, CLK32KOUT2_FUNC_MASK, CLK32KOUT2_FUNC},
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

static struct rk808_reg_data rk816_suspend_reg[] = {
	/* set bat 3.4v low and act irq */
	{ RK816_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK816_VBAT_LOW_3V4 | EN_VBAT_LOW_IRQ },
};

static struct rk808_reg_data rk816_resume_reg[] = {
	/* set bat 3.0v low and act shutdown */
	{ RK816_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK816_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN },
};

static const struct regmap_irq rk816_irqs[] = {
	/* INT_STS */
	[RK816_IRQ_PWRON_FALL] = {
		.mask = RK816_IRQ_PWRON_FALL_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_PWRON_RISE] = {
		.mask = RK816_IRQ_PWRON_RISE_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_VB_LOW] = {
		.mask = RK816_IRQ_VB_LOW_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_PWRON] = {
		.mask = RK816_IRQ_PWRON_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_PWRON_LP] = {
		.mask = RK816_IRQ_PWRON_LP_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_HOTDIE] = {
		.mask = RK816_IRQ_HOTDIE_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_RTC_ALARM] = {
		.mask = RK816_IRQ_RTC_ALARM_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_RTC_PERIOD] = {
		.mask = RK816_IRQ_RTC_PERIOD_MSK,
		.reg_offset = 1,
	},
	[RK816_IRQ_USB_OV] = {
		.mask = RK816_IRQ_USB_OV_MSK,
		.reg_offset = 1,
	},
};

static struct rk808_reg_data rk818_suspend_reg[] = {
	/* set bat 3.4v low and act irq */
	{ RK808_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK808_VBAT_LOW_3V4 | EN_VBAT_LOW_IRQ },
};

static struct rk808_reg_data rk818_resume_reg[] = {
	/* set bat 3.0v low and act shutdown */
	{ RK808_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK808_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN },
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

static const struct regmap_irq rk816_battery_irqs[] = {
	/* INT_STS */
	[RK816_IRQ_PLUG_IN] = {
		.mask = RK816_IRQ_PLUG_IN_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_PLUG_OUT] = {
		.mask = RK816_IRQ_PLUG_OUT_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_CHG_OK] = {
		.mask = RK816_IRQ_CHG_OK_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_CHG_TE] = {
		.mask = RK816_IRQ_CHG_TE_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_CHG_TS] = {
		.mask = RK816_IRQ_CHG_TS_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_CHG_CVTLIM] = {
		.mask = RK816_IRQ_CHG_CVTLIM_MSK,
		.reg_offset = 0,
	},
	[RK816_IRQ_DISCHG_ILIM] = {
		.mask = RK816_IRQ_DISCHG_ILIM_MSK,
		.reg_offset = 0,
	},
};

static struct regmap_irq_chip rk816_irq_chip = {
	.name = "rk816",
	.irqs = rk816_irqs,
	.num_irqs = ARRAY_SIZE(rk816_irqs),
	.num_regs = 2,
	.irq_reg_stride = 3,
	.status_base = RK816_INT_STS_REG1,
	.mask_base = RK816_INT_STS_MSK_REG1,
	.ack_base = RK816_INT_STS_REG1,
	.init_ack_masked = true,
};

static struct regmap_irq_chip rk816_battery_irq_chip = {
	.name = "rk816_battery",
	.irqs = rk816_battery_irqs,
	.num_irqs = ARRAY_SIZE(rk816_battery_irqs),
	.num_regs = 1,
	.status_base = RK816_INT_STS_REG3,
	.mask_base = RK816_INT_STS_MSK_REG3,
	.ack_base = RK816_INT_STS_REG3,
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

static struct i2c_client *rk808_i2c_client;
static struct rk808_reg_data *suspend_reg, *resume_reg;
static int suspend_reg_num, resume_reg_num;

static void rk805_device_shutdown_prepare(void)
{
	int ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	if (!rk808)
		return;

	ret = regmap_update_bits(rk808->regmap,
				 RK805_GPIO_IO_POL_REG,
				 SLP_SD_MSK, SHUTDOWN_FUN);
	if (ret)
		dev_err(&rk808_i2c_client->dev, "Failed to shutdown device!\n");
}

static void rk817_shutdown_prepare(void)
{
	int ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	/* close rtc int when power off */
	regmap_update_bits(rk808->regmap,
			   RK817_INT_STS_MSK_REG0,
			   (0x3 << 5), (0x3 << 5));
	regmap_update_bits(rk808->regmap,
			   RK817_RTC_INT_REG,
			   (0x3 << 2), (0x0 << 2));

	if (rk808->pins && rk808->pins->p && rk808->pins->power_off) {
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_NULL_FUN);
		if (ret)
			pr_err("shutdown: config SLPPIN_NULL_FUN error!\n");

		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPOL_MSK,
					 RK817_SLPPOL_H);
		if (ret)
			pr_err("shutdown: config RK817_SLPPOL_H error!\n");

		ret = pinctrl_select_state(rk808->pins->p,
					   rk808->pins->power_off);
		if (ret)
			pr_info("%s:failed to activate pwroff state\n",
				__func__);
	}

	/* pmic sleep shutdown function */
	ret = regmap_update_bits(rk808->regmap,
				 RK817_SYS_CFG(3),
				 RK817_SLPPIN_FUNC_MSK, SLPPIN_DN_FUN);
	if (ret)
		dev_err(&rk808_i2c_client->dev, "Failed to shutdown device!\n");
	/* pmic need the SCL clock to synchronize register */
	mdelay(2);
}

static void rk8xx_device_shutdown(void)
{
	int ret;
	unsigned int reg, bit;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	switch (rk808->variant) {
	case RK805_ID:
		reg = RK805_DEV_CTRL_REG;
		bit = DEV_OFF;
		break;
	case RK808_ID:
		reg = RK808_DEVCTRL_REG,
		bit = DEV_OFF_RST;
		break;
	case RK816_ID:
		reg = RK816_DEV_CTRL_REG;
		bit = DEV_OFF;
		break;
	case RK818_ID:
		reg = RK818_DEVCTRL_REG;
		bit = DEV_OFF;
		break;
	default:
		return;
	}

	ret = regmap_update_bits(rk808->regmap, reg, bit, bit);
	if (ret)
		dev_err(&rk808_i2c_client->dev, "Failed to shutdown device!\n");
}

/* Called in syscore shutdown */
static void (*pm_shutdown)(void);

static void rk8xx_syscore_shutdown(void)
{
	int ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	if (!rk808) {
		dev_warn(&rk808_i2c_client->dev,
			 "have no rk808, so do nothing here\n");
		return;
	}

	/* close rtc int when power off */
	regmap_update_bits(rk808->regmap,
			   RK808_INT_STS_MSK_REG1,
			   (0x3 << 5), (0x3 << 5));
	regmap_update_bits(rk808->regmap,
			   RK808_RTC_INT_REG,
			   (0x3 << 2), (0x0 << 2));
	/*
	 * For PMIC that power off supplies by write register via i2c bus,
	 * it's better to do power off at syscore shutdown here.
	 *
	 * Because when run to kernel's "pm_power_off" call, i2c may has
	 * been stopped or PMIC may not be able to get i2c transfer while
	 * there are too many devices are competiting.
	 */
	if (system_state == SYSTEM_POWER_OFF) {
		if (rk808->variant == RK809_ID || rk808->variant == RK817_ID) {
			ret = regmap_update_bits(rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_SLPPIN_FUNC_MSK,
						 SLPPIN_DN_FUN);
			if (ret) {
				dev_warn(&rk808_i2c_client->dev,
					 "Cannot switch to power down function\n");
			}
		}

		if (pm_shutdown) {
			dev_info(&rk808_i2c_client->dev, "System power off\n");
			pm_shutdown();
			mdelay(10);
			dev_info(&rk808_i2c_client->dev,
				 "Power off failed !\n");
			while (1)
				;
		}
	}
}

static struct syscore_ops rk808_syscore_ops = {
	.shutdown = rk8xx_syscore_shutdown,
};

/*
 * RK8xx PMICs would do real power off in syscore shutdown, if "pm_power_off"
 * is not assigned(e.g. PSCI is not enabled), we have to provide a dummy
 * callback for it, otherwise there comes a halt in Reboot system call:
 *
 * if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !pm_power_off)
 *		cmd = LINUX_REBOOT_CMD_HALT;
 */
static void rk808_pm_power_off_dummy(void)
{
	pr_info("Dummy power off for RK8xx PMICs, should never reach here!\n");

	while (1)
		;
}

static ssize_t rk8xx_dbg_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int ret;
	char cmd;
	u32 input[2], addr, data;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	ret = sscanf(buf, "%c ", &cmd);
	if (ret != 1) {
		pr_err("Unknown command\n");
		goto out;
	}
	switch (cmd) {
	case 'w':
		ret = sscanf(buf, "%c %x %x ", &cmd, &input[0], &input[1]);
		if (ret != 3) {
			pr_err("error! cmd format: echo w [addr] [value]\n");
			goto out;
		};
		addr = input[0] & 0xff;
		data = input[1] & 0xff;
		pr_info("cmd : %c %x %x\n\n", cmd, input[0], input[1]);
		regmap_write(rk808->regmap, addr, data);
		regmap_read(rk808->regmap, addr, &data);
		pr_info("new: %x %x\n", addr, data);
		break;
	case 'r':
		ret = sscanf(buf, "%c %x ", &cmd, &input[0]);
		if (ret != 2) {
			pr_err("error! cmd format: echo r [addr]\n");
			goto out;
		};
		pr_info("cmd : %c %x\n\n", cmd, input[0]);
		addr = input[0] & 0xff;
		regmap_read(rk808->regmap, addr, &data);
		pr_info("%x %x\n", input[0], data);
		break;
	default:
		pr_err("Unknown command\n");
		break;
	}

out:
	return count;
}

static int rk817_pinctrl_init(struct device *dev, struct rk808 *rk808)
{
	int ret;
	struct platform_device	*pinctrl_dev;
	struct pinctrl_state *default_st;

	pinctrl_dev = platform_device_alloc("rk805-pinctrl", -1);
	if (!pinctrl_dev) {
		dev_err(dev, "Alloc pinctrl dev failed!\n");
		return -ENOMEM;
	}

	pinctrl_dev->dev.parent = dev;

	ret = platform_device_add(pinctrl_dev);

	if (ret) {
		platform_device_put(pinctrl_dev);
		dev_err(dev, "Add rk805-pinctrl dev failed!\n");
		return ret;
	}
	if (dev->pins && !IS_ERR(dev->pins->p)) {
		dev_info(dev, "had get a pinctrl!\n");
		return 0;
	}

	rk808->pins = devm_kzalloc(dev, sizeof(struct rk808_pin_info),
				   GFP_KERNEL);
	if (!rk808->pins)
		return -ENOMEM;

	rk808->pins->p = devm_pinctrl_get(dev);
	if (IS_ERR(rk808->pins->p)) {
		rk808->pins->p = NULL;
		dev_err(dev, "no pinctrl handle\n");
		return 0;
	}

	default_st = pinctrl_lookup_state(rk808->pins->p,
					  PINCTRL_STATE_DEFAULT);

	if (IS_ERR(default_st)) {
		dev_dbg(dev, "no default pinctrl state\n");
			return -EINVAL;
	}

	ret = pinctrl_select_state(rk808->pins->p, default_st);
	if (ret) {
		dev_dbg(dev, "failed to activate default pinctrl state\n");
		return -EINVAL;
	}

	rk808->pins->power_off = pinctrl_lookup_state(rk808->pins->p,
						      "pmic-power-off");
	if (IS_ERR(rk808->pins->power_off)) {
		rk808->pins->power_off = NULL;
		dev_dbg(dev, "no power-off pinctrl state\n");
	}

	rk808->pins->sleep = pinctrl_lookup_state(rk808->pins->p,
						  "pmic-sleep");
	if (IS_ERR(rk808->pins->sleep)) {
		rk808->pins->sleep = NULL;
		dev_dbg(dev, "no sleep-setting state\n");
	}

	rk808->pins->reset = pinctrl_lookup_state(rk808->pins->p,
						  "pmic-reset");
	if (IS_ERR(rk808->pins->reset)) {
		rk808->pins->reset = NULL;
		dev_dbg(dev, "no reset-setting pinctrl state\n");
		return 0;
	}

	ret = pinctrl_select_state(rk808->pins->p, rk808->pins->reset);

	if (ret)
		dev_dbg(dev, "failed to activate reset-setting pinctrl state\n");

	return 0;
}

struct rk817_reboot_data_t {
	struct rk808 *rk808;
	struct notifier_block reboot_notifier;
};

static struct rk817_reboot_data_t rk817_reboot_data;

static int rk817_reboot_notifier_handler(struct notifier_block *nb,
					 unsigned long action, void *cmd)
{
	struct rk817_reboot_data_t *data;
	struct device *dev;
	int value, power_en_active0, power_en_active1;
	int ret, i;
	static const char * const pmic_rst_reg_only_cmd[] = {
		"loader", "bootloader", "fastboot", "recovery",
		"ums", "panic", "watchdog", "charge",
	};

	data = container_of(nb, struct rk817_reboot_data_t, reboot_notifier);
	dev = &data->rk808->i2c->dev;

	regmap_read(data->rk808->regmap, RK817_POWER_EN_SAVE0,
		    &power_en_active0);
	if (power_en_active0 != 0) {
		regmap_read(data->rk808->regmap, RK817_POWER_EN_SAVE1,
			    &power_en_active1);
		value = power_en_active0 & 0x0f;
		regmap_write(data->rk808->regmap,
			     RK817_POWER_EN_REG(0),
			     value | 0xf0);
		value = (power_en_active0 & 0xf0) >> 4;
		regmap_write(data->rk808->regmap,
			     RK817_POWER_EN_REG(1),
			     value | 0xf0);
		value = power_en_active1 & 0x0f;
		regmap_write(data->rk808->regmap,
			     RK817_POWER_EN_REG(2),
			     value | 0xf0);
		value = (power_en_active1 & 0xf0) >> 4;
		regmap_write(data->rk808->regmap,
			     RK817_POWER_EN_REG(3),
			     value | 0xf0);
	} else {
		dev_info(dev, "reboot: not restore POWER_EN\n");
	}

	if (action != SYS_RESTART || !cmd)
		return NOTIFY_OK;

	/*
	 * When system restart, there are two rst actions of PMIC sleep if
	 * board hardware support:
	 *
	 *	0b'00: reset the PMIC itself completely.
	 *	0b'01: reset the 'RST' related register only.
	 *
	 * In the case of 0b'00, PMIC reset itself which triggers SoC NPOR-reset
	 * at the same time, so the command: reboot load/bootload/recovery, etc
	 * is not effect any more.
	 *
	 * Here we check if this reboot cmd is what we expect for 0b'01.
	 */
	for (i = 0; i < ARRAY_SIZE(pmic_rst_reg_only_cmd); i++) {
		if (!strcmp(cmd, pmic_rst_reg_only_cmd[i])) {
			ret = regmap_update_bits(data->rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_RST_FUNC_MSK,
						 RK817_RST_FUNC_REG);
			if (ret)
				dev_err(dev, "reboot: force RK817_RST_FUNC_REG error!\n");
			else
				dev_info(dev, "reboot: force RK817_RST_FUNC_REG ok!\n");
			break;
		}
	}

	return NOTIFY_OK;
}

static void rk817_of_property_prepare(struct rk808 *rk808, struct device *dev)
{
	u32 inner;
	int ret, func, msk, val;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32_index(np, "fb-inner-reg-idxs", 0, &inner);
	if (!ret && inner == RK817_ID_DCDC3)
		regmap_update_bits(rk808->regmap, RK817_POWER_CONFIG,
				   RK817_BUCK3_FB_RES_MSK,
				   RK817_BUCK3_FB_RES_INTER);
	else
		regmap_update_bits(rk808->regmap, RK817_POWER_CONFIG,
				   RK817_BUCK3_FB_RES_MSK,
				   RK817_BUCK3_FB_RES_EXT);
	dev_info(dev, "support dcdc3 fb mode:%d, %d\n", ret, inner);

	ret = of_property_read_u32(np, "pmic-reset-func", &func);

	msk = RK817_SLPPIN_FUNC_MSK | RK817_RST_FUNC_MSK;
	val = SLPPIN_NULL_FUN;

	if (!ret && func < RK817_RST_FUNC_CNT) {
		val |= RK817_RST_FUNC_MSK &
		       (func << RK817_RST_FUNC_SFT);
	} else {
		val |= RK817_RST_FUNC_REG;
	}

	regmap_update_bits(rk808->regmap, RK817_SYS_CFG(3), msk, val);

	dev_info(dev, "support pmic reset mode:%d,%d\n", ret, func);

	rk817_reboot_data.rk808 = rk808;
	rk817_reboot_data.reboot_notifier.notifier_call =
		rk817_reboot_notifier_handler;
	ret = register_reboot_notifier(&rk817_reboot_data.reboot_notifier);
	if (ret)
		dev_err(dev, "failed to register reboot nb\n");
}

static struct kobject *rk8xx_kobj;
static struct device_attribute rk8xx_attrs =
		__ATTR(rk8xx_dbg, 0200, NULL, rk8xx_dbg_store);

static const struct of_device_id rk808_of_match[] = {
	{ .compatible = "rockchip,rk805" },
	{ .compatible = "rockchip,rk808" },
	{ .compatible = "rockchip,rk809" },
	{ .compatible = "rockchip,rk816" },
	{ .compatible = "rockchip,rk817" },
	{ .compatible = "rockchip,rk818" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk808_of_match);

static int rk808_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct rk808 *rk808;
	const struct rk808_reg_data *pre_init_reg;
	const struct regmap_irq_chip *battery_irq_chip = NULL;
	const struct mfd_cell *cells;
	unsigned char pmic_id_msb, pmic_id_lsb;
	u8 on_source = 0, off_source = 0;
	unsigned int on, off;
	int pm_off = 0, msb, lsb;
	int nr_pre_init_regs;
	int nr_cells;
	int ret;
	int i;
	void (*of_property_prepare_fn)(struct rk808 *rk808,
				       struct device *dev) = NULL;
	int (*pinctrl_init)(struct device *dev, struct rk808 *rk808) = NULL;
	void (*device_shutdown_fn)(void) = NULL;

	rk808 = devm_kzalloc(&client->dev, sizeof(*rk808), GFP_KERNEL);
	if (!rk808)
		return -ENOMEM;

	if (of_device_is_compatible(np, "rockchip,rk817") ||
	    of_device_is_compatible(np, "rockchip,rk809")) {
		pmic_id_msb = RK817_ID_MSB;
		pmic_id_lsb = RK817_ID_LSB;
	} else {
		pmic_id_msb = RK808_ID_MSB;
		pmic_id_lsb = RK808_ID_LSB;
	}

	/* Read chip variant */
	msb = i2c_smbus_read_byte_data(client, pmic_id_msb);
	if (msb < 0) {
		dev_err(&client->dev, "failed to read the chip id at 0x%x\n",
			RK808_ID_MSB);
		return msb;
	}

	lsb = i2c_smbus_read_byte_data(client, pmic_id_lsb);
	if (lsb < 0) {
		dev_err(&client->dev, "failed to read the chip id at 0x%x\n",
			RK808_ID_LSB);
		return lsb;
	}

	rk808->variant = ((msb << 8) | lsb) & RK8XX_ID_MSK;
	dev_info(&client->dev, "chip id: 0x%x\n", (unsigned int)rk808->variant);

	switch (rk808->variant) {
	case RK805_ID:
		rk808->regmap_cfg = &rk805_regmap_config;
		rk808->regmap_irq_chip = &rk805_irq_chip;
		pre_init_reg = rk805_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk805_pre_init_reg);
		cells = rk805s;
		nr_cells = ARRAY_SIZE(rk805s);
		on_source = RK805_ON_SOURCE_REG;
		off_source = RK805_OFF_SOURCE_REG;
		suspend_reg = rk805_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk805_suspend_reg);
		resume_reg = rk805_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk805_resume_reg);
		device_shutdown_fn = rk8xx_device_shutdown;
		rk808->pm_pwroff_prep_fn = rk805_device_shutdown_prepare;
		break;
	case RK808_ID:
		rk808->regmap_cfg = &rk808_regmap_config;
		rk808->regmap_irq_chip = &rk808_irq_chip;
		pre_init_reg = rk808_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk808_pre_init_reg);
		cells = rk808s;
		nr_cells = ARRAY_SIZE(rk808s);
		device_shutdown_fn = rk8xx_device_shutdown;
		break;
	case RK816_ID:
		rk808->regmap_cfg = &rk816_regmap_config;
		rk808->regmap_irq_chip = &rk816_irq_chip;
		battery_irq_chip = &rk816_battery_irq_chip;
		pre_init_reg = rk816_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk816_pre_init_reg);
		cells = rk816s;
		nr_cells = ARRAY_SIZE(rk816s);
		on_source = RK816_ON_SOURCE_REG;
		off_source = RK816_OFF_SOURCE_REG;
		suspend_reg = rk816_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk816_suspend_reg);
		resume_reg = rk816_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk816_resume_reg);
		device_shutdown_fn = rk8xx_device_shutdown;
		break;
	case RK818_ID:
		rk808->regmap_cfg = &rk818_regmap_config;
		rk808->regmap_irq_chip = &rk818_irq_chip;
		pre_init_reg = rk818_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk818_pre_init_reg);
		cells = rk818s;
		nr_cells = ARRAY_SIZE(rk818s);
		on_source = RK818_ON_SOURCE_REG;
		off_source = RK818_OFF_SOURCE_REG;
		suspend_reg = rk818_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk818_suspend_reg);
		resume_reg = rk818_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk818_resume_reg);
		device_shutdown_fn = rk8xx_device_shutdown;
		break;
	case RK809_ID:
	case RK817_ID:
		rk808->regmap_cfg = &rk817_regmap_config;
		rk808->regmap_irq_chip = &rk817_irq_chip;
		pre_init_reg = rk817_pre_init_reg;
		nr_pre_init_regs = ARRAY_SIZE(rk817_pre_init_reg);
		cells = rk817s;
		nr_cells = ARRAY_SIZE(rk817s);
		on_source = RK817_ON_SOURCE_REG;
		off_source = RK817_OFF_SOURCE_REG;
		rk808->pm_pwroff_prep_fn = rk817_shutdown_prepare;
		of_property_prepare_fn = rk817_of_property_prepare;
		pinctrl_init = rk817_pinctrl_init;
		break;
	default:
		dev_err(&client->dev, "Unsupported RK8XX ID %lu\n",
			rk808->variant);
		return -EINVAL;
	}

	rk808->i2c = client;
	rk808_i2c_client = client;
	i2c_set_clientdata(client, rk808);

	rk808->regmap = devm_regmap_init_i2c(client, rk808->regmap_cfg);
	if (IS_ERR(rk808->regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk808->regmap);
	}

	if (on_source && off_source) {
		ret = regmap_read(rk808->regmap, on_source, &on);
		if (ret) {
			dev_err(&client->dev, "read 0x%x failed\n", on_source);
			return ret;
		}

		ret = regmap_read(rk808->regmap, off_source, &off);
		if (ret) {
			dev_err(&client->dev, "read 0x%x failed\n", off_source);
			return ret;
		}

		dev_info(&client->dev, "source: on=0x%02x, off=0x%02x\n",
			 on, off);
	}

	if (!client->irq) {
		dev_err(&client->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	if (of_property_prepare_fn)
		of_property_prepare_fn(rk808, &client->dev);

	for (i = 0; i < nr_pre_init_regs; i++) {
		ret = regmap_update_bits(rk808->regmap,
					 pre_init_reg[i].addr,
					 pre_init_reg[i].mask,
					 pre_init_reg[i].value);
		if (ret) {
			dev_err(&client->dev,
				"0x%x write err\n",
				pre_init_reg[i].addr);
			return ret;
		}
	}

	if (pinctrl_init) {
		ret = pinctrl_init(&client->dev, rk808);
		if (ret)
			return ret;
	}

	ret = regmap_add_irq_chip(rk808->regmap, client->irq,
				  IRQF_ONESHOT, -1,
				  rk808->regmap_irq_chip, &rk808->irq_data);
	if (ret) {
		dev_err(&client->dev, "Failed to add irq_chip %d\n", ret);
		return ret;
	}

	if (battery_irq_chip) {
		ret = regmap_add_irq_chip(rk808->regmap, client->irq,
					  IRQF_ONESHOT | IRQF_SHARED, -1,
					  battery_irq_chip,
					  &rk808->battery_irq_data);
		if (ret) {
			dev_err(&client->dev,
				"Failed to add batterry irq_chip %d\n", ret);
			regmap_del_irq_chip(client->irq, rk808->irq_data);
			return ret;
		}
	}

	ret = devm_mfd_add_devices(&client->dev, PLATFORM_DEVID_NONE,
			      cells, nr_cells, NULL, 0,
			      regmap_irq_get_domain(rk808->irq_data));
	if (ret) {
		dev_err(&client->dev, "failed to add MFD devices %d\n", ret);
		goto err_irq;
	}

	pm_off = of_property_read_bool(np, "rockchip,system-power-controller");
	if (pm_off) {
		if (!pm_power_off_prepare)
			pm_power_off_prepare = rk808->pm_pwroff_prep_fn;

		if (device_shutdown_fn) {
			register_syscore_ops(&rk808_syscore_ops);
			/* power off system in the syscore shutdown ! */
			pm_shutdown = device_shutdown_fn;
		}
	}

	rk8xx_kobj = kobject_create_and_add("rk8xx", NULL);
	if (rk8xx_kobj) {
		ret = sysfs_create_file(rk8xx_kobj, &rk8xx_attrs.attr);
		if (ret)
			dev_err(&client->dev, "create rk8xx sysfs error\n");
	}

	if (!pm_power_off)
		pm_power_off = rk808_pm_power_off_dummy;

	return 0;

err_irq:
	regmap_del_irq_chip(client->irq, rk808->irq_data);
	if (battery_irq_chip)
		regmap_del_irq_chip(client->irq, rk808->battery_irq_data);
	return ret;
}

static int rk808_remove(struct i2c_client *client)
{
	struct rk808 *rk808 = i2c_get_clientdata(client);

	regmap_del_irq_chip(client->irq, rk808->irq_data);
	mfd_remove_devices(&client->dev);

	/**
	 * pm_power_off may points to a function from another module.
	 * Check if the pointer is set by us and only then overwrite it.
	 */
	if (pm_power_off == rk808_pm_power_off_dummy)
		pm_power_off = NULL;

	/**
	 * As above, check if the pointer is set by us before overwrite.
	 */
	if (rk808->pm_pwroff_prep_fn &&
	    pm_power_off_prepare == rk808->pm_pwroff_prep_fn)
		pm_power_off_prepare = NULL;

	if (pm_shutdown)
		unregister_syscore_ops(&rk808_syscore_ops);

	return 0;
}

static int __maybe_unused rk8xx_suspend(struct device *dev)
{
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);
	int i, ret = 0;
	int value;

	for (i = 0; i < suspend_reg_num; i++) {
		ret = regmap_update_bits(rk808->regmap,
					 suspend_reg[i].addr,
					 suspend_reg[i].mask,
					 suspend_reg[i].value);
		if (ret) {
			dev_err(dev, "0x%x write err\n",
				suspend_reg[i].addr);
			return ret;
		}
	}

	switch (rk808->variant) {
	case RK805_ID:
		ret = regmap_update_bits(rk808->regmap,
					 RK805_GPIO_IO_POL_REG,
					 SLP_SD_MSK,
					 SLEEP_FUN);
		break;
	case RK809_ID:
	case RK817_ID:
		if (rk808->pins && rk808->pins->p && rk808->pins->sleep) {
			ret = regmap_update_bits(rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_SLPPIN_FUNC_MSK,
						 SLPPIN_NULL_FUN);
			if (ret) {
				dev_err(dev, "suspend: config SLPPIN_NULL_FUN error!\n");
				return ret;
			}

			ret = regmap_update_bits(rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_SLPPOL_MSK,
						 RK817_SLPPOL_H);
			if (ret) {
				dev_err(dev, "suspend: config RK817_SLPPOL_H error!\n");
				return ret;
			}

			/* pmic need the SCL clock to synchronize register */
			regmap_read(rk808->regmap, RK817_SYS_STS, &value);
			mdelay(2);
			ret = pinctrl_select_state(rk808->pins->p, rk808->pins->sleep);
			if (ret) {
				dev_err(dev, "failed to act slp pinctrl state\n");
				return ret;
			}
		}
		break;
	default:
		break;
	}

	return ret;
}

static int __maybe_unused rk8xx_resume(struct device *dev)
{
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);
	int i, ret = 0;
	int value;

	for (i = 0; i < resume_reg_num; i++) {
		ret = regmap_update_bits(rk808->regmap,
					 resume_reg[i].addr,
					 resume_reg[i].mask,
					 resume_reg[i].value);
		if (ret) {
			dev_err(dev, "0x%x write err\n",
				resume_reg[i].addr);
			return ret;
		}
	}

	switch (rk808->variant) {
	case RK809_ID:
	case RK817_ID:
		if (rk808->pins && rk808->pins->p && rk808->pins->reset) {
			ret = regmap_update_bits(rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_SLPPIN_FUNC_MSK,
						 SLPPIN_NULL_FUN);
			if (ret) {
				dev_err(dev, "resume: config SLPPIN_NULL_FUN error!\n");
				return ret;
			}

			ret = regmap_update_bits(rk808->regmap,
						 RK817_SYS_CFG(3),
						 RK817_SLPPOL_MSK,
						 RK817_SLPPOL_L);
			if (ret) {
				dev_err(dev, "resume: config RK817_SLPPOL_L error!\n");
				return ret;
			}

			/* pmic need the SCL clock to synchronize register */
			regmap_read(rk808->regmap, RK817_SYS_STS, &value);
			mdelay(2);
			ret = pinctrl_select_state(rk808->pins->p, rk808->pins->reset);
			if (ret)
				dev_dbg(dev, "failed to act reset pinctrl state\n");
		}
		break;
	default:
		break;
	}

	return ret;
}
SIMPLE_DEV_PM_OPS(rk8xx_pm_ops, rk8xx_suspend, rk8xx_resume);

static struct i2c_driver rk808_i2c_driver = {
	.driver = {
		.name = "rk808",
		.of_match_table = rk808_of_match,
		.pm = &rk8xx_pm_ops,
	},
	.probe    = rk808_probe,
	.remove   = rk808_remove,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
static int __init rk808_i2c_driver_init(void)
{
	return i2c_add_driver(&rk808_i2c_driver);
}
subsys_initcall(rk808_i2c_driver_init);

static void __exit rk808_i2c_driver_exit(void)
{
	i2c_del_driver(&rk808_i2c_driver);
}
module_exit(rk808_i2c_driver_exit);
#else
module_i2c_driver(rk808_i2c_driver);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_DESCRIPTION("RK808/RK818 PMIC driver");
