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

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mfd/rk808.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_device.h>

struct rk808_reg_data {
	int addr;
	int mask;
	int value;
};

struct rk8xx_power_data {
	char *name;
	const struct rk808_reg_data *rk8xx_pre_init_reg;
	int reg_num;
	const struct regmap_config *rk8xx_regmap_config;
	const struct mfd_cell *rk8xx_cell;
	int cell_num;
	struct regmap_irq_chip *rk8xx_irq_chip;
	int (*pm_shutdown)(struct regmap *regmap);
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

static int rk808_shutdown(struct regmap *regmap)
{
	int ret;

	ret = regmap_update_bits(regmap,
				 RK808_DEVCTRL_REG,
				 DEV_OFF_RST, DEV_OFF_RST);
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

static struct resource rtc_resources[] = {
	{
		.start  = RK808_IRQ_RTC_ALARM,
		.end    = RK808_IRQ_RTC_ALARM,
		.flags  = IORESOURCE_IRQ,
	}
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

static const struct rk808_reg_data pre_init_reg[] = {
	{ RK808_BUCK3_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_150MA },
	{ RK808_BUCK4_CONFIG_REG, BUCK_ILMIN_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BOOST_CONFIG_REG, BOOST_ILMIN_MASK, BOOST_ILMIN_100MA },
	{ RK808_BUCK1_CONFIG_REG, BUCK1_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_BUCK2_CONFIG_REG, BUCK2_RATE_MASK,  BUCK_ILMIN_200MA },
	{ RK808_DCDC_UV_ACT_REG,  BUCK_UV_ACT_MASK, BUCK_UV_ACT_DISABLE},
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

static const struct regmap_config rk818_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RK818_SAVE_DATA19,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = rk818_is_volatile_reg,
};

static const struct mfd_cell rk818s[] = {
	{ .name = "rk808-clkout", },
	{ .name = "rk818-regulator", },
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

static struct rk8xx_power_data rk808_power_data = {
	.name = "rk808",
	.rk8xx_pre_init_reg = pre_init_reg,
	.reg_num = ARRAY_SIZE(pre_init_reg),
	.rk8xx_regmap_config = &rk808_regmap_config,
	.rk8xx_cell = rk808s,
	.cell_num = ARRAY_SIZE(rk808s),
	.rk8xx_irq_chip = &rk808_irq_chip,
	.pm_shutdown = rk808_shutdown,
};

static struct rk8xx_power_data rk818_power_data = {
	.name = "rk818",
	.rk8xx_pre_init_reg = rk818_pre_init_reg,
	.reg_num = ARRAY_SIZE(rk818_pre_init_reg),
	.rk8xx_regmap_config = &rk818_regmap_config,
	.rk8xx_cell = rk818s,
	.cell_num = ARRAY_SIZE(rk818s),
	.rk8xx_irq_chip = &rk818_irq_chip,
	.pm_shutdown = rk818_shutdown,
};

static int (*pm_shutdown)(struct regmap *regmap);
static struct i2c_client *rk808_i2c_client;

static void rk808_device_shutdown(void)
{
	int ret;
	struct rk808 *rk808 = i2c_get_clientdata(rk808_i2c_client);

	if (!rk808) {
		dev_warn(&rk808_i2c_client->dev,
			 "have no rk808, so do nothing here\n");
		return;
	}

	ret = pm_shutdown(rk808->regmap);
	if (ret)
		dev_err(&rk808_i2c_client->dev, "power off error!\n");
}

static const struct of_device_id rk808_of_match[] = {
	{
		.compatible = "rockchip,rk808",
		.data = &rk808_power_data,
	},
	{
		.compatible = "rockchip,rk818",
		.data = &rk818_power_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rk808_of_match);

static int rk808_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	const struct of_device_id *of_id =
			of_match_device(rk808_of_match, &client->dev);
	const struct rk8xx_power_data *pdata = of_id->data;
	struct device_node *np = client->dev.of_node;
	struct rk808 *rk808;
	int pm_off = 0;
	int ret;
	int i;

	if (!of_id) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return -ENODEV;
	}

	if (!client->irq) {
		dev_err(&client->dev, "No interrupt support, no core IRQ\n");
		return -EINVAL;
	}

	rk808 = devm_kzalloc(&client->dev, sizeof(*rk808), GFP_KERNEL);
	if (!rk808)
		return -ENOMEM;

	rk808->regmap = devm_regmap_init_i2c(client,
					     pdata->rk8xx_regmap_config);
	if (IS_ERR(rk808->regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(rk808->regmap);
	}

	pm_shutdown = pdata->pm_shutdown;
	if (!pm_shutdown) {
		dev_err(&client->dev, "shutdown initialization failed\n");
		return -EINVAL;
	}

	for (i = 0; i < pdata->reg_num; i++) {
		ret = regmap_update_bits(rk808->regmap,
					 pdata->rk8xx_pre_init_reg[i].addr,
					 pdata->rk8xx_pre_init_reg[i].mask,
					 pdata->rk8xx_pre_init_reg[i].value);
		if (ret) {
			dev_err(&client->dev,
				"0x%x write err\n",
				pdata->rk8xx_pre_init_reg[i].addr);
			return ret;
		}
	}

	ret = regmap_add_irq_chip(rk808->regmap, client->irq,
				  IRQF_ONESHOT, -1,
				  pdata->rk8xx_irq_chip, &rk808->irq_data);
	if (ret) {
		dev_err(&client->dev, "Failed to add irq_chip %d\n", ret);
		return ret;
	}

	rk808->i2c = client;
	i2c_set_clientdata(client, rk808);

	ret = mfd_add_devices(&client->dev, -1,
			      pdata->rk8xx_cell, pdata->cell_num,
			      NULL, 0, regmap_irq_get_domain(rk808->irq_data));
	if (ret) {
		dev_err(&client->dev, "failed to add MFD devices %d\n", ret);
		goto err_irq;
	}

	pm_off = of_property_read_bool(np,
				"rockchip,system-power-controller");
	if (pm_off) {
		rk808_i2c_client = client;
		pm_power_off = rk808_device_shutdown;
	}

	return 0;

err_irq:
	regmap_del_irq_chip(client->irq, rk808->irq_data);
	return ret;
}

static int rk808_remove(struct i2c_client *client)
{
	struct rk808 *rk808 = i2c_get_clientdata(client);

	regmap_del_irq_chip(client->irq, rk808->irq_data);
	mfd_remove_devices(&client->dev);
	pm_power_off = NULL;

	return 0;
}

static const struct i2c_device_id rk808_ids[] = {
	{ "rk808" },
	{ "rk818" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rk808_ids);

static struct i2c_driver rk808_i2c_driver = {
	.driver = {
		.name = "rk808",
		.of_match_table = rk808_of_match,
	},
	.probe    = rk808_probe,
	.remove   = rk808_remove,
	.id_table = rk808_ids,
};

module_i2c_driver(rk808_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("RK808 PMIC driver");
