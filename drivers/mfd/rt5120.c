// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>

#define RT5120_REG_INTENABLE	0x1D
#define RT5120_REG_INTSTAT	0x1E
#define RT5120_REG_FZCMODE	0x44

#define RT5120_INT_HOTDIE	0
#define RT5120_INT_PWRKEY_REL	5
#define RT5120_INT_PWRKEY_PRESS	6

static const struct regmap_range rt5120_rd_yes_ranges[] = {
	regmap_reg_range(0x03, 0x13),
	regmap_reg_range(0x1c, 0x20),
	regmap_reg_range(0x44, 0x44),
};

static const struct regmap_range rt5120_wr_yes_ranges[] = {
	regmap_reg_range(0x06, 0x13),
	regmap_reg_range(0x1c, 0x20),
	regmap_reg_range(0x44, 0x44),
};

static const struct regmap_access_table rt5120_rd_table = {
	.yes_ranges = rt5120_rd_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(rt5120_rd_yes_ranges),
};

static const struct regmap_access_table rt5120_wr_table = {
	.yes_ranges = rt5120_wr_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(rt5120_wr_yes_ranges),
};

static const struct regmap_config rt5120_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT5120_REG_FZCMODE,

	.wr_table = &rt5120_wr_table,
	.rd_table = &rt5120_rd_table,
};

static const struct regmap_irq rt5120_irqs[] = {
	REGMAP_IRQ_REG_LINE(RT5120_INT_HOTDIE, 8),
	REGMAP_IRQ_REG_LINE(RT5120_INT_PWRKEY_REL, 8),
	REGMAP_IRQ_REG_LINE(RT5120_INT_PWRKEY_PRESS, 8),
};

static const struct regmap_irq_chip rt5120_irq_chip = {
	.name = "rt5120-pmic",
	.status_base = RT5120_REG_INTSTAT,
	.mask_base = RT5120_REG_INTENABLE,
	.ack_base = RT5120_REG_INTSTAT,
	.mask_invert = true,
	.use_ack = true,
	.num_regs = 1,
	.irqs = rt5120_irqs,
	.num_irqs = ARRAY_SIZE(rt5120_irqs),
};

static const struct resource rt5120_regulator_resources[] = {
	DEFINE_RES_IRQ(RT5120_INT_HOTDIE),
};

static const struct resource rt5120_pwrkey_resources[] = {
	DEFINE_RES_IRQ_NAMED(RT5120_INT_PWRKEY_PRESS, "pwrkey-press"),
	DEFINE_RES_IRQ_NAMED(RT5120_INT_PWRKEY_REL, "pwrkey-release"),
};

static const struct mfd_cell rt5120_devs[] = {
	MFD_CELL_RES("rt5120-regulator", rt5120_regulator_resources),
	MFD_CELL_OF("rt5120-pwrkey", rt5120_pwrkey_resources, NULL, 0, 0, "richtek,rt5120-pwrkey"),
};

static int rt5120_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	int ret;

	regmap = devm_regmap_init_i2c(i2c, &rt5120_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init regmap\n");

	ret = devm_regmap_add_irq_chip(dev, regmap, i2c->irq, IRQF_ONESHOT, 0,
				       &rt5120_irq_chip, &irq_data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add IRQ chip\n");

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, rt5120_devs,
				    ARRAY_SIZE(rt5120_devs), NULL, 0,
				    regmap_irq_get_domain(irq_data));
}

static const struct of_device_id rt5120_device_match_table[] = {
	{ .compatible = "richtek,rt5120" },
	{}
};
MODULE_DEVICE_TABLE(of, rt5120_device_match_table);

static struct i2c_driver rt5120_driver = {
	.driver = {
		.name = "rt5120",
		.of_match_table = rt5120_device_match_table,
	},
	.probe_new = rt5120_probe,
};
module_i2c_driver(rt5120_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT5120 I2C driver");
MODULE_LICENSE("GPL v2");
