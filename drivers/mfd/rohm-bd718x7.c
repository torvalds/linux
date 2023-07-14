// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (C) 2018 ROHM Semiconductors
//
// ROHM BD71837MWV and BD71847MWV PMIC driver
//
// Datasheet for BD71837MWV available from
// https://www.rohm.com/datasheet/BD71837MWV/bd71837mwv-e

#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/rohm-bd718x7.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

static struct gpio_keys_button button = {
	.code = KEY_POWER,
	.gpio = -1,
	.type = EV_KEY,
};

static struct gpio_keys_platform_data bd718xx_powerkey_data = {
	.buttons = &button,
	.nbuttons = 1,
	.name = "bd718xx-pwrkey",
};

static struct mfd_cell bd71837_mfd_cells[] = {
	{
		.name = "gpio-keys",
		.platform_data = &bd718xx_powerkey_data,
		.pdata_size = sizeof(bd718xx_powerkey_data),
	},
	{ .name = "bd71837-clk", },
	{ .name = "bd71837-pmic", },
};

static struct mfd_cell bd71847_mfd_cells[] = {
	{
		.name = "gpio-keys",
		.platform_data = &bd718xx_powerkey_data,
		.pdata_size = sizeof(bd718xx_powerkey_data),
	},
	{ .name = "bd71847-clk", },
	{ .name = "bd71847-pmic", },
};

static const struct regmap_irq bd718xx_irqs[] = {
	REGMAP_IRQ_REG(BD718XX_INT_SWRST, 0, BD718XX_INT_SWRST_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_PWRBTN_S, 0, BD718XX_INT_PWRBTN_S_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_PWRBTN_L, 0, BD718XX_INT_PWRBTN_L_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_PWRBTN, 0, BD718XX_INT_PWRBTN_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_WDOG, 0, BD718XX_INT_WDOG_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_ON_REQ, 0, BD718XX_INT_ON_REQ_MASK),
	REGMAP_IRQ_REG(BD718XX_INT_STBY_REQ, 0, BD718XX_INT_STBY_REQ_MASK),
};

static struct regmap_irq_chip bd718xx_irq_chip = {
	.name = "bd718xx-irq",
	.irqs = bd718xx_irqs,
	.num_irqs = ARRAY_SIZE(bd718xx_irqs),
	.num_regs = 1,
	.irq_reg_stride = 1,
	.status_base = BD718XX_REG_IRQ,
	.mask_base = BD718XX_REG_MIRQ,
	.ack_base = BD718XX_REG_IRQ,
	.init_ack_masked = true,
};

static const struct regmap_range pmic_status_range = {
	.range_min = BD718XX_REG_IRQ,
	.range_max = BD718XX_REG_POW_STATE,
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &pmic_status_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config bd718xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD718XX_MAX_REGISTER - 1,
	.cache_type = REGCACHE_RBTREE,
};

static int bd718xx_init_press_duration(struct regmap *regmap,
				       struct device *dev)
{
	u32 short_press_ms, long_press_ms;
	u32 short_press_value, long_press_value;
	int ret;

	ret = of_property_read_u32(dev->of_node, "rohm,short-press-ms",
				   &short_press_ms);
	if (!ret) {
		short_press_value = min(15u, (short_press_ms + 250) / 500);
		ret = regmap_update_bits(regmap, BD718XX_REG_PWRONCONFIG0,
					 BD718XX_PWRBTN_PRESS_DURATION_MASK,
					 short_press_value);
		if (ret) {
			dev_err(dev, "Failed to init pwron short press\n");
			return ret;
		}
	}

	ret = of_property_read_u32(dev->of_node, "rohm,long-press-ms",
				   &long_press_ms);
	if (!ret) {
		long_press_value = min(15u, (long_press_ms + 500) / 1000);
		ret = regmap_update_bits(regmap, BD718XX_REG_PWRONCONFIG1,
					 BD718XX_PWRBTN_PRESS_DURATION_MASK,
					 long_press_value);
		if (ret) {
			dev_err(dev, "Failed to init pwron long press\n");
			return ret;
		}
	}

	return 0;
}

static int bd718xx_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	int ret;
	unsigned int chip_type;
	struct mfd_cell *mfd;
	int cells;

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}
	chip_type = (unsigned int)(uintptr_t)
		    of_device_get_match_data(&i2c->dev);
	switch (chip_type) {
	case ROHM_CHIP_TYPE_BD71837:
		mfd = bd71837_mfd_cells;
		cells = ARRAY_SIZE(bd71837_mfd_cells);
		break;
	case ROHM_CHIP_TYPE_BD71847:
		mfd = bd71847_mfd_cells;
		cells = ARRAY_SIZE(bd71847_mfd_cells);
		break;
	default:
		dev_err(&i2c->dev, "Unknown device type");
		return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(i2c, &bd718xx_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&i2c->dev, PTR_ERR(regmap),
				     "regmap initialization failed\n");

	ret = devm_regmap_add_irq_chip(&i2c->dev, regmap, i2c->irq,
				       IRQF_ONESHOT, 0, &bd718xx_irq_chip,
				       &irq_data);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to add irq_chip\n");

	ret = bd718xx_init_press_duration(regmap, &i2c->dev);
	if (ret)
		return ret;

	ret = regmap_irq_get_virq(irq_data, BD718XX_INT_PWRBTN_S);

	if (ret < 0)
		return dev_err_probe(&i2c->dev, ret, "Failed to get the IRQ\n");

	button.irq = ret;

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				   mfd, cells, NULL, 0,
				   regmap_irq_get_domain(irq_data));
	if (ret)
		dev_err_probe(&i2c->dev, ret, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd718xx_of_match[] = {
	{
		.compatible = "rohm,bd71837",
		.data = (void *)ROHM_CHIP_TYPE_BD71837,
	},
	{
		.compatible = "rohm,bd71847",
		.data = (void *)ROHM_CHIP_TYPE_BD71847,
	},
	{
		.compatible = "rohm,bd71850",
		.data = (void *)ROHM_CHIP_TYPE_BD71847,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, bd718xx_of_match);

static struct i2c_driver bd718xx_i2c_driver = {
	.driver = {
		.name = "rohm-bd718x7",
		.of_match_table = bd718xx_of_match,
	},
	.probe = bd718xx_i2c_probe,
};

static int __init bd718xx_i2c_init(void)
{
	return i2c_add_driver(&bd718xx_i2c_driver);
}

/* Initialise early so consumer devices can complete system boot */
subsys_initcall(bd718xx_i2c_init);

static void __exit bd718xx_i2c_exit(void)
{
	i2c_del_driver(&bd718xx_i2c_driver);
}
module_exit(bd718xx_i2c_exit);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD71837/BD71847 Power Management IC driver");
MODULE_LICENSE("GPL");
