// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip RK808/RK818 Core (I2C) driver
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#include <linux/i2c.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

struct rk8xx_i2c_platform_data {
	const struct regmap_config *regmap_cfg;
	int variant;
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
	case RK817_CODEC_DTOP_LPT_SRST:
	case RK817_GAS_GAUGE_ADC_CONFIG0 ... RK817_GAS_GAUGE_CUR_ADC_K0:
	case RK817_PMIC_CHRG_STS:
	case RK817_PMIC_CHRG_OUT:
	case RK817_PMIC_CHRG_IN:
	case RK817_INT_STS_REG0:
	case RK817_INT_STS_REG1:
	case RK817_INT_STS_REG2:
	case RK817_SYS_STS:
		return true;
	}

	return false;
}


static const struct regmap_config rk818_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK818_USB_CTRL_REG,
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

static const struct regmap_config rk808_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK808_IO_POL_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk808_is_volatile_reg,
};

static const struct regmap_config rk817_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK817_GPIO_INT_CFG,
	.cache_type = REGCACHE_NONE,
	.volatile_reg = rk817_is_volatile_reg,
};

static const struct rk8xx_i2c_platform_data rk805_data = {
	.regmap_cfg = &rk805_regmap_config,
	.variant = RK805_ID,
};

static const struct rk8xx_i2c_platform_data rk808_data = {
	.regmap_cfg = &rk808_regmap_config,
	.variant = RK808_ID,
};

static const struct rk8xx_i2c_platform_data rk809_data = {
	.regmap_cfg = &rk817_regmap_config,
	.variant = RK809_ID,
};

static const struct rk8xx_i2c_platform_data rk817_data = {
	.regmap_cfg = &rk817_regmap_config,
	.variant = RK817_ID,
};

static const struct rk8xx_i2c_platform_data rk818_data = {
	.regmap_cfg = &rk818_regmap_config,
	.variant = RK818_ID,
};

static int rk8xx_i2c_probe(struct i2c_client *client)
{
	const struct rk8xx_i2c_platform_data *data;
	struct regmap *regmap;

	data = device_get_match_data(&client->dev);
	if (!data)
		return -ENODEV;

	regmap = devm_regmap_init_i2c(client, data->regmap_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "regmap initialization failed\n");

	return rk8xx_probe(&client->dev, data->variant, client->irq, regmap);
}

static void rk8xx_i2c_shutdown(struct i2c_client *client)
{
	rk8xx_shutdown(&client->dev);
}

static SIMPLE_DEV_PM_OPS(rk8xx_i2c_pm_ops, rk8xx_suspend, rk8xx_resume);

static const struct of_device_id rk8xx_i2c_of_match[] = {
	{ .compatible = "rockchip,rk805", .data = &rk805_data },
	{ .compatible = "rockchip,rk808", .data = &rk808_data },
	{ .compatible = "rockchip,rk809", .data = &rk809_data },
	{ .compatible = "rockchip,rk817", .data = &rk817_data },
	{ .compatible = "rockchip,rk818", .data = &rk818_data },
	{ },
};
MODULE_DEVICE_TABLE(of, rk8xx_i2c_of_match);

static struct i2c_driver rk8xx_i2c_driver = {
	.driver = {
		.name = "rk8xx-i2c",
		.of_match_table = rk8xx_i2c_of_match,
		.pm = &rk8xx_i2c_pm_ops,
	},
	.probe_new = rk8xx_i2c_probe,
	.shutdown  = rk8xx_i2c_shutdown,
};
module_i2c_driver(rk8xx_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_DESCRIPTION("RK8xx I2C PMIC driver");
