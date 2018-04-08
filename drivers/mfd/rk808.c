/*
 * MFD core driver for Rockchip RK808
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

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
	 * - It's unlikely we care that RK808_DEVCTRL_REG is volatile since
	 *   bits are cleared in case when we shutoff anyway, but better safe.
	 */

	switch (reg) {
	case RK817_SECONDS_REG ... RK817_WEEKS_REG:
	case RK817_RTC_STATUS_REG:
	case RK817_INT_STS_REG0:
	case RK817_INT_STS_REG1:
	case RK817_INT_STS_REG2:
	case RK817_SYS_STS:
		return true;
	}

	return true;
}

static int rk808_shutdown(struct regmap *regmap)
{
	int ret;

	ret = regmap_update_bits(regmap,
				 RK808_DEVCTRL_REG,
				 DEV_OFF_RST, DEV_OFF_RST);
	return ret;
}

static int rk816_shutdown(struct regmap *regmap)
{
	int ret;

	ret = regmap_update_bits(regmap,
				 RK816_DEV_CTRL_REG,
				 DEV_OFF, DEV_OFF);
	return ret;
}

static int rk818_shutdown(struct regmap *regmap)
{
	int ret;

	ret = regmap_update_bits(regmap,
				 RK818_DEVCTRL_REG,
				 DEV_OFF, DEV_OFF);
	return ret;
}

static int rk805_shutdown_prepare(struct rk808 *rk808)
{
	int ret;

	/* close rtc int when power off */
	regmap_update_bits(rk808->regmap,
			   RK808_INT_STS_MSK_REG1,
			   (0x3 << 5), (0x3 << 5));
	regmap_update_bits(rk808->regmap,
			   RK808_RTC_INT_REG,
			   (0x3 << 2), (0x0 << 2));

	/* pmic sleep shutdown function */
	ret = regmap_update_bits(rk808->regmap,
				 RK805_GPIO_IO_POL_REG,
				 SLP_SD_MSK, SHUTDOWN_FUN);
	return ret;
}

static int rk817_shutdown_prepare(struct rk808 *rk808)
{
	int ret;

	/* close rtc int when power off */
	regmap_update_bits(rk808->regmap,
			   RK817_INT_STS_MSK_REG1,
			   (0x3 << 5), (0x3 << 5));
	regmap_update_bits(rk808->regmap,
			   RK817_RTC_INT_REG,
			   (0x3 << 2), (0x0 << 2));

	if (rk808->pins && rk808->pins->p && rk808->pins->power_off) {
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_NULL_FUN);
		if (ret) {
			pr_err("shutdown: config SLPPIN_NULL_FUN error!\n");
			return 0;
		}

		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPOL_MSK,
					 RK817_SLPPOL_H);
		if (ret) {
			pr_err("shutdown: config RK817_SLPPOL_H error!\n");
			return 0;
		}
		ret = pinctrl_select_state(rk808->pins->p,
					   rk808->pins->power_off);
		if (ret)
			pr_info("%s:failed to activate pwroff state\n",
				__func__);
		else
			return ret;
	}

	/* pmic sleep shutdown function */
	ret = regmap_update_bits(rk808->regmap,
				 RK817_SYS_CFG(3),
				 RK817_SLPPIN_FUNC_MSK, SLPPIN_DN_FUN);
	return ret;
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

static const struct regmap_config rk808_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK808_IO_POL_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk808_is_volatile_reg,
};

static const struct regmap_config rk805_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK805_OFF_SOURCE_REG,
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

static const struct regmap_config rk818_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK818_SAVE_DATA19,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk818_is_volatile_reg,
};

static const struct regmap_config rk817_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK817_GPIO_INT_CFG,
	.cache_type = REGCACHE_NONE,
	.volatile_reg = rk817_is_volatile_reg,
};

static struct resource rtc_resources[] = {
	{
		.start  = RK808_IRQ_RTC_ALARM,
		.end    = RK808_IRQ_RTC_ALARM,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct resource rk817_rtc_resources[] = {
	{
		.start  = RK817_IRQ_RTC_ALARM,
		.end    = RK817_IRQ_RTC_ALARM,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct resource rk816_rtc_resources[] = {
	{
		.start  = RK816_IRQ_RTC_ALARM,
		.end    = RK816_IRQ_RTC_ALARM,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct resource rk805_pwrkey_resources[] = {
	{
		.start  = RK805_IRQ_PWRON_RISE,
		.end    = RK805_IRQ_PWRON_RISE,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = RK805_IRQ_PWRON_FALL,
		.end    = RK805_IRQ_PWRON_FALL,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rk817_pwrkey_resources[] = {
	{
		.start  = RK817_IRQ_PWRON_RISE,
		.end    = RK817_IRQ_PWRON_RISE,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = RK817_IRQ_PWRON_FALL,
		.end    = RK817_IRQ_PWRON_FALL,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rk816_pwrkey_resources[] = {
	{
		.start  = RK816_IRQ_PWRON_RISE,
		.end    = RK816_IRQ_PWRON_RISE,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = RK816_IRQ_PWRON_FALL,
		.end    = RK816_IRQ_PWRON_FALL,
		.flags  = IORESOURCE_IRQ,
	},
};

static const struct mfd_cell rk808s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = &rtc_resources[0],
	},
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

static struct regmap_irq_chip rk808_irq_chip = {
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

static const struct mfd_cell rk816s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk8xx-gpio", },
	{ .name = "rk816-battery", .of_compatible = "rk816-battery", },
	{
		.name = "rk8xx-pwrkey",
		.num_resources = ARRAY_SIZE(rk816_pwrkey_resources),
		.resources = &rk816_pwrkey_resources[0],
	},
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rk816_rtc_resources),
		.resources = &rk816_rtc_resources[0],
	},
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
};

static struct rk808_reg_data rk816_suspend_reg[] = {
	/* set bat 3.4v low and act irq */
	{ RK816_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK816_VBAT_LOW_3V4 | EN_VBAT_LOW_IRQ },
};

static struct rk808_reg_data rk816_resume_reg[] = {
	/* set bat 3.0v low and act shutdown*/
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

static const struct mfd_cell rk818s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk808-regulator", },
	{ .name = "rk818-battery", .of_compatible = "rk818-battery", },
	{ .name = "rk818-charger", },
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = &rtc_resources[0],
	},
};

static const struct rk808_reg_data rk818_pre_init_reg[] = {
	{ RK818_H5V_EN_REG, REF_RDY_CTRL_ENABLE | H5V_EN_MASK,
					REF_RDY_CTRL_ENABLE | H5V_EN_ENABLE },
	{ RK818_DCDC_EN_REG, BOOST_EN_MASK | SWITCH_EN_MASK,
					BOOST_EN_ENABLE | SWITCH_EN_ENABLE },
	{ RK818_SLEEP_SET_OFF_REG1, OTG_SLP_SET_MASK, OTG_SLP_SET_OFF },
	{ RK818_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_250MA },
	{ RK808_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
};

static struct rk808_reg_data rk818_suspend_reg[] = {
	/* set bat 3.4v low and act irq */
	{ RK808_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK808_VBAT_LOW_3V4 | EN_VBAT_LOW_IRQ },
};

static struct rk808_reg_data rk818_resume_reg[] = {
	/* set bat 3.0v low and act shutdown*/
	{ RK808_VB_MON_REG, VBAT_LOW_VOL_MASK | VBAT_LOW_ACT_MASK,
	  RK808_VBAT_LOW_3V0 | EN_VABT_LOW_SHUT_DOWN },
};

static const struct regmap_irq rk818_irqs[] = {
	/* INT_STS */
	[RK818_IRQ_VOUT_LO] = {
		.mask = VOUT_LO_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_VB_LO] = {
		.mask = VB_LO_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_PWRON] = {
		.mask = PWRON_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_PWRON_LP] = {
		.mask = PWRON_LP_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_HOTDIE] = {
		.mask = HOTDIE_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_RTC_ALARM] = {
		.mask = RTC_ALARM_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_RTC_PERIOD] = {
		.mask = RTC_PERIOD_MASK,
		.reg_offset = 0,
	},
	[RK818_IRQ_USB_OV] = {
		.mask = USB_OV_MASK,
		.reg_offset = 0,
	},
	/* INT_STS2 */
	[RK818_IRQ_PLUG_IN] = {
		.mask = PLUG_IN_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_PLUG_OUT] = {
		.mask = PLUG_OUT_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_OK] = {
		.mask = CHGOK_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_TE] = {
		.mask = CHGTE_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_TS1] = {
		.mask = CHGTS1_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_TS2] = {
		.mask = TS2_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_CHG_CVTLIM] = {
		.mask = CHG_CVTLIM_MASK,
		.reg_offset = 1,
	},
	[RK818_IRQ_DISCHG_ILIM] = {
		.mask = DISCHG_ILIM_MASK,
		.reg_offset = 1,
	},
};

static struct regmap_irq_chip rk818_irq_chip = {
	.name = "rk818",
	.irqs = rk818_irqs,
	.num_irqs = ARRAY_SIZE(rk818_irqs),
	.num_regs = 2,
	.irq_reg_stride = 2,
	.status_base = RK808_INT_STS_REG1,
	.mask_base = RK808_INT_STS_MSK_REG1,
	.ack_base = RK808_INT_STS_REG1,
	.init_ack_masked = true,
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

static const struct mfd_cell rk805s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk818-regulator", },
	{ .name = "rk8xx-gpio", },
	{
		.name = "rk8xx-pwrkey",
		.num_resources = ARRAY_SIZE(rk805_pwrkey_resources),
		.resources = &rk805_pwrkey_resources[0],
	},
	{
		.name = "rk808-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = &rtc_resources[0],
	},
};

static const struct rk808_reg_data rk805_pre_init_reg[] = {
	{RK805_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK, BUCK_ILMIN_400MA},
	{RK805_GPIO_IO_POL_REG, SLP_SD_MSK, SLEEP_FUN},
	{RK808_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	{RK805_THERMAL_REG, TEMP_HOTDIE_MSK, TEMP115C},
	{RK805_DCDC_VRP_REG, REG_WRITE_MSK, BUCK4_VRP_3PERCENT},
};

static struct rk808_reg_data rk805_suspend_reg[] = {
	{RK805_BUCK3_CONFIG_REG, PWM_MODE_MSK, AUTO_PWM_MODE},
};

static struct rk808_reg_data rk805_resume_reg[] = {
	{RK805_BUCK3_CONFIG_REG, PWM_MODE_MSK, FPWM_MODE},
};

#define REGMAP_IRQ_M(_id) \
	[_id] = {				\
		.mask = BIT(((_id) % 8)),	\
		.reg_offset = ((_id) / 8),	\
	}

static const struct regmap_irq rk817_irqs[RK817_IRQ_END] = {
	REGMAP_IRQ_M(0),
	REGMAP_IRQ_M(1),
	REGMAP_IRQ_M(2),
	REGMAP_IRQ_M(3),
	REGMAP_IRQ_M(4),
	REGMAP_IRQ_M(5),
	REGMAP_IRQ_M(6),
	REGMAP_IRQ_M(7),
	REGMAP_IRQ_M(8),
	REGMAP_IRQ_M(9),
	REGMAP_IRQ_M(10),
	REGMAP_IRQ_M(11),
	REGMAP_IRQ_M(12),
	REGMAP_IRQ_M(13),
	REGMAP_IRQ_M(14),
	REGMAP_IRQ_M(15),
	REGMAP_IRQ_M(16),
	REGMAP_IRQ_M(17),
	REGMAP_IRQ_M(18),
	REGMAP_IRQ_M(19),
	REGMAP_IRQ_M(20),
	REGMAP_IRQ_M(21),
	REGMAP_IRQ_M(22),
	REGMAP_IRQ_M(23)
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

static const struct mfd_cell rk817s[] = {
	{ .name = "rk808-clkout",},
	{ .name = "rk808-regulator",},
	{ .name = "rk817-battery", .of_compatible = "rk817,battery", },
	{ .name = "rk817-charger", .of_compatible = "rk817,charger", },
	{
		.name = "rk8xx-pwrkey",
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

static const struct rk808_reg_data rk817_pre_init_reg[] = {
	{RK817_RTC_CTRL_REG, RTC_STOP, RTC_STOP},
	{RK817_GPIO_INT_CFG, RK817_INT_POL_MSK, RK817_INT_POL_L},
	{RK817_SYS_CFG(1), RK817_HOTDIE_TEMP_MSK | RK817_TSD_TEMP_MSK,
					   RK817_HOTDIE_105 | RK817_TSD_140},
};

static int (*pm_shutdown)(struct regmap *regmap);
static int (*pm_shutdown_prepare)(struct rk808 *rk808);
static struct i2c_client *rk808_i2c_client;
static struct rk808_reg_data *suspend_reg, *resume_reg;
static int suspend_reg_num, resume_reg_num;

static void rk808_device_shutdown_prepare(void)
{
	int ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	if (!rk808) {
		dev_warn(&rk808_i2c_client->dev,
			 "have no rk808, so do nothing here\n");
		return;
	}

	if (pm_shutdown_prepare) {
		ret = pm_shutdown_prepare(rk808);
		if (ret)
			dev_err(&rk808_i2c_client->dev,
				"power off prepare error!\n");
	}
}

static void rk808_syscore_shutdown(void)
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
		/* power off supplies ! */
		if (pm_shutdown) {
			dev_info(&rk808_i2c_client->dev, "System power off\n");
			ret = pm_shutdown(rk808->regmap);
			if (ret)
				dev_err(&rk808_i2c_client->dev,
					"System power off error!\n");
			mdelay(10);
			dev_info(&rk808_i2c_client->dev,
				 "Cpu should never reach here, stop!\n");
			while (1)
				;
		}
	}
}

static struct syscore_ops rk808_syscore_ops = {
	.shutdown = rk808_syscore_shutdown,
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
	switch (cmd) {
	case 'w':
		ret = sscanf(buf, "%c %x %x ", &cmd, &input[0], &input[1]);
		if (ret != 3) {
			pr_err("erro! cmd format: echo w [addr] [value]\n");
			goto out;
		}
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
			pr_err("erro! cmd format: echo r [addr]\n");
			goto out;
		}
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

	ret = regmap_update_bits(rk808->regmap,
				 RK817_SYS_CFG(3),
				 RK817_SLPPOL_MSK,
				 RK817_SLPPOL_L);
	if (ret) {
		dev_err(dev, "init: config RK817_SLPPOL_L error!\n");
		return -1;
	}

	ret = pinctrl_select_state(rk808->pins->p, rk808->pins->reset);

	if (ret)
		dev_dbg(dev, "failed to activate reset-setting pinctrl state\n");

	return 0;
}

static void rk817_of_property_prepare(struct regmap *regmap, struct device *dev)
{
	u32 inner;
	int ret, func, msk, val;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32_index(np, "fb-inner-reg-idxs", 0, &inner);
	if (!ret && inner == RK817_ID_DCDC3)
		regmap_update_bits(regmap, RK817_POWER_CONFIG,
				   RK817_BUCK3_FB_RES_MSK,
				   RK817_BUCK3_FB_RES_INTER);
	else
		regmap_update_bits(regmap, RK817_POWER_CONFIG,
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

	regmap_update_bits(regmap, RK817_SYS_CFG(3), msk, val);

	dev_info(dev, "support pmic reset mode:%d,%d\n", ret, func);
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
	int (*pm_shutdown_fn)(struct regmap *regmap) = NULL;
	int (*pm_shutdown_prepare_fn)(struct rk808 *rk808) = NULL;
	const struct rk808_reg_data *pre_init_reg;
	const struct regmap_config *regmap_config;
	const struct regmap_irq_chip *irq_chip, *battery_irq_chip = NULL;
	const struct mfd_cell *cell;
	u8 on_source = 0, off_source = 0;
	int msb, lsb, reg_num, cell_num;
	int ret, i, pm_off = 0;
	unsigned int on, off;
	u8 pmic_id_msb = RK808_ID_MSB, pmic_id_lsb = RK808_ID_LSB;
	void (*of_property_prepare_fn)(struct regmap *regmap,
				       struct device *dev) = NULL;
	int (*pinctrl_init)(struct device *dev, struct rk808 *rk808) = NULL;

	if (!client->irq) {
		dev_err(&client->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	rk808 = devm_kzalloc(&client->dev, sizeof(*rk808), GFP_KERNEL);
	if (!rk808)
		return -ENOMEM;

	if (of_device_is_compatible(np, "rockchip,rk817") ||
	    of_device_is_compatible(np, "rockchip,rk809")) {
		pmic_id_msb = RK817_ID_MSB;
		pmic_id_lsb = RK817_ID_LSB;
	}
	/* read Chip variant */
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
	dev_info(&client->dev, "Pmic Chip id: 0x%lx\n", rk808->variant);

	/* set Chip platform init data*/
	switch (rk808->variant) {
	case RK818_ID:
		cell = rk818s;
		cell_num = ARRAY_SIZE(rk818s);
		pre_init_reg = rk818_pre_init_reg;
		reg_num = ARRAY_SIZE(rk818_pre_init_reg);
		regmap_config = &rk818_regmap_config;
		irq_chip = &rk818_irq_chip;
		pm_shutdown_fn = rk818_shutdown;
		on_source = RK818_ON_SOURCE_REG;
		off_source = RK818_OFF_SOURCE_REG;
		suspend_reg = rk818_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk818_suspend_reg);
		resume_reg = rk818_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk818_resume_reg);
		break;
	case RK816_ID:
		cell = rk816s;
		cell_num = ARRAY_SIZE(rk816s);
		pre_init_reg = rk816_pre_init_reg;
		reg_num = ARRAY_SIZE(rk816_pre_init_reg);
		regmap_config = &rk816_regmap_config;
		irq_chip = &rk816_irq_chip;
		battery_irq_chip = &rk816_battery_irq_chip;
		pm_shutdown_fn = rk816_shutdown;
		on_source = RK816_ON_SOURCE_REG;
		off_source = RK816_OFF_SOURCE_REG;
		suspend_reg = rk816_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk816_suspend_reg);
		resume_reg = rk816_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk816_resume_reg);
		break;
	case RK808_ID:
		cell = rk808s;
		cell_num = ARRAY_SIZE(rk808s);
		pre_init_reg = rk808_pre_init_reg;
		reg_num = ARRAY_SIZE(rk808_pre_init_reg);
		regmap_config = &rk808_regmap_config;
		irq_chip = &rk808_irq_chip;
		pm_shutdown_fn = rk808_shutdown;
		break;
	case RK805_ID:
		cell = rk805s;
		cell_num = ARRAY_SIZE(rk805s);
		pre_init_reg = rk805_pre_init_reg;
		reg_num = ARRAY_SIZE(rk805_pre_init_reg);
		regmap_config = &rk805_regmap_config;
		irq_chip = &rk805_irq_chip;
		pm_shutdown_prepare_fn = rk805_shutdown_prepare;
		on_source = RK805_ON_SOURCE_REG;
		off_source = RK805_OFF_SOURCE_REG;
		suspend_reg = rk805_suspend_reg;
		suspend_reg_num = ARRAY_SIZE(rk805_suspend_reg);
		resume_reg = rk805_resume_reg;
		resume_reg_num = ARRAY_SIZE(rk805_resume_reg);
		break;
	case RK809_ID:
	case RK817_ID:
		cell = rk817s;
		cell_num = ARRAY_SIZE(rk817s);
		pre_init_reg = rk817_pre_init_reg;
		reg_num = ARRAY_SIZE(rk817_pre_init_reg);
		regmap_config = &rk817_regmap_config;
		irq_chip = &rk817_irq_chip;
		pm_shutdown_prepare_fn = rk817_shutdown_prepare;
		on_source = RK817_ON_SOURCE_REG;
		off_source = RK817_OFF_SOURCE_REG;
		of_property_prepare_fn = rk817_of_property_prepare;
		pinctrl_init = rk817_pinctrl_init;
		break;
	default:
		dev_err(&client->dev, "unsupported RK8XX ID 0x%lx\n",
			rk808->variant);
		return -EINVAL;
	}

	rk808->regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(rk808->regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk808->regmap);
	}

	/* on & off source */
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

	for (i = 0; i < reg_num; i++) {
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

	if (of_property_prepare_fn)
		of_property_prepare_fn(rk808->regmap, &client->dev);

	i2c_set_clientdata(client, rk808);
	rk808->i2c = client;
	rk808_i2c_client = client;

	if (pinctrl_init) {
		ret = pinctrl_init(&client->dev, rk808);
		if (ret)
			return ret;
	}

	ret = regmap_add_irq_chip(rk808->regmap, client->irq,
				  IRQF_ONESHOT | IRQF_SHARED, -1,
				  irq_chip, &rk808->irq_data);
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

	ret = mfd_add_devices(&client->dev, -1,
			      cell, cell_num,
			      NULL, 0, regmap_irq_get_domain(rk808->irq_data));
	if (ret) {
		dev_err(&client->dev, "failed to add MFD devices %d\n", ret);
		goto err_irq;
	}

	pm_off = of_property_read_bool(np,
				       "rockchip,system-power-controller");
	if (pm_off) {
		if (pm_shutdown_prepare_fn) {
			pm_shutdown_prepare = pm_shutdown_prepare_fn;
			pm_power_off_prepare = rk808_device_shutdown_prepare;
		}
		if (pm_shutdown_fn) {
			pm_shutdown = pm_shutdown_fn;
			register_syscore_ops(&rk808_syscore_ops);
		}

		/*
		 * If not assigned(e.g. PSCI is not enable), we provide a
		 * dummy for it to avoid halt in Reboot system call.
		 */
		if (!pm_power_off)
			pm_power_off = rk808_pm_power_off_dummy;
	}

	rk8xx_kobj = kobject_create_and_add("rk8xx", NULL);
	if (rk8xx_kobj) {
		ret = sysfs_create_file(rk8xx_kobj, &rk8xx_attrs.attr);
		if (ret)
			dev_err(&client->dev, "create rk8xx sysfs error\n");
	}

	return 0;

err_irq:
	regmap_del_irq_chip(client->irq, rk808->irq_data);
	if (battery_irq_chip)
		regmap_del_irq_chip(client->irq, rk808->battery_irq_data);
	return ret;
}

static int rk808_suspend(struct device *dev)
{
	int i, ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

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

	if (rk808->pins && rk808->pins->p && rk808->pins->sleep) {
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_NULL_FUN);
		if (ret) {
			dev_err(dev, "suspend: config SLPPIN_NULL_FUN error!\n");
			return -1;
		}

		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPOL_MSK,
					 RK817_SLPPOL_H);
		if (ret) {
			dev_err(dev, "suspend: config RK817_SLPPOL_H error!\n");
			return -1;
		}

		ret = pinctrl_select_state(rk808->pins->p, rk808->pins->sleep);
		if (ret) {
			dev_err(dev, "failed to act slp pinctrl state\n");
			return -1;
		}
	}

	return 0;
}

static int rk808_resume(struct device *dev)
{
	int i, ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

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

	if (rk808->pins && rk808->pins->p && rk808->pins->reset) {
		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPIN_FUNC_MSK,
					 SLPPIN_NULL_FUN);
		if (ret) {
			dev_err(dev, "resume: config SLPPIN_NULL_FUN error!\n");
			return -1;
		}

		ret = regmap_update_bits(rk808->regmap,
					 RK817_SYS_CFG(3),
					 RK817_SLPPOL_MSK,
					 RK817_SLPPOL_L);
		if (ret) {
			dev_err(dev, "resume: config RK817_SLPPOL_L error!\n");
			return -1;
		}

		ret = pinctrl_select_state(rk808->pins->p, rk808->pins->reset);
		if (ret)
			dev_dbg(dev, "failed to act reset pinctrl state\n");
	}

	return 0;
}

static int rk808_remove(struct i2c_client *client)
{
	struct rk808 *rk808 = i2c_get_clientdata(client);

	regmap_del_irq_chip(client->irq, rk808->irq_data);
	mfd_remove_devices(&client->dev);

	if (pm_power_off == rk808_pm_power_off_dummy)
		pm_power_off = NULL;
	if (pm_power_off_prepare == rk808_device_shutdown_prepare)
		pm_power_off_prepare = NULL;
	if (pm_shutdown)
		unregister_syscore_ops(&rk808_syscore_ops);

	return 0;
}

static const struct dev_pm_ops rk808_pm_ops = {
	.suspend = rk808_suspend,
	.resume =  rk808_resume,
};

static const struct i2c_device_id rk808_ids[] = {
	{ "rk805" },
	{ "rk808" },
	{ "rk809" },
	{ "rk816" },
	{ "rk817" },
	{ "rk818" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rk808_ids);

static struct i2c_driver rk808_i2c_driver = {
	.driver = {
		.name = "rk808",
		.of_match_table = rk808_of_match,
		.pm = &rk808_pm_ops,
	},
	.probe    = rk808_probe,
	.remove   = rk808_remove,
	.id_table = rk808_ids,
};

module_i2c_driver(rk808_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Chen jianhong <chenjh@rock-chips.com>");
MODULE_DESCRIPTION("RK808 PMIC driver");
