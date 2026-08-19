// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (C) 2018 ROHM Semiconductors
//
// ROHM BD71837MWV and BD71847MWV PMIC driver
//
// Datasheet for BD71837MWV available from
// https://www.rohm.com/datasheet/BD71837MWV/bd71837mwv-e

#include <linux/device/devres.h>
#include <linux/gfp_types.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/rohm-bd718x7.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

static struct mfd_cell bd71837_mfd_cells[] = {
	{ .name = "bd71837-clk", },
	{ .name = "bd71837-pmic", },
};

static struct mfd_cell bd71847_mfd_cells[] = {
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

static const struct regmap_irq_chip bd718xx_irq_chip = {
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

static const struct regmap_range pmic_status_range[] = {
	regmap_reg_range(BD718XX_REG_IRQ, BD718XX_REG_POW_STATE),
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &pmic_status_range[0],
	.n_yes_ranges = ARRAY_SIZE(pmic_status_range),
};

static const struct regmap_config bd718xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD718XX_MAX_REGISTER - 1,
	.cache_type = REGCACHE_MAPLE,
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

static const struct property_entry bd718xx_powerkey_parent_props[] = {
	PROPERTY_ENTRY_STRING("label", "bd718xx-pwrkey"),
	{ }
};

static const struct property_entry bd718xx_powerkey_props[] = {
	PROPERTY_ENTRY_U32("linux,code", KEY_POWER),
	{ }
};

static const struct resource bd718xx_powerkey_resources[] = {
	DEFINE_RES_IRQ_NAMED(BD718XX_INT_PWRBTN_S, "bd718xx-pwrkey"),
};

#define GPIO_KEYS  0	/* Node corresponding to gpio-keys device itself */
#define PWRON_KEY  1	/* Node describing power button in gpio-keys */

static int bd718xx_i2c_register_swnodes(const struct software_node *nodes)
{
	const struct software_node * const node_group[] = {
		&nodes[GPIO_KEYS], &nodes[PWRON_KEY], NULL
	};

	return software_node_register_node_group(node_group);
}

static void bd718xx_i2c_unregister_swnodes(void *data)
{
	const struct software_node *nodes = data;
	const struct software_node * const node_group[] = {
		&nodes[GPIO_KEYS], &nodes[PWRON_KEY], NULL
	};

	software_node_unregister_node_group(node_group);
}

static int bd718xx_i2c_register_pwrbutton(struct device *dev,
					  struct irq_domain *irq_domain)
{
	struct mfd_cell gpio_keys_cell = {
		.name = "gpio-keys",
		.resources = bd718xx_powerkey_resources,
		.num_resources = ARRAY_SIZE(bd718xx_powerkey_resources),
	};
	struct software_node *nodes;
	int ret;

	nodes = devm_kcalloc(dev, 2, sizeof(*nodes), GFP_KERNEL);
	if (!nodes)
		return -ENOMEM;

	nodes[GPIO_KEYS].name = devm_kasprintf(dev, GFP_KERNEL, "%s-power-key", dev_name(dev));
	if (!nodes[GPIO_KEYS].name)
		return -ENOMEM;

	nodes[GPIO_KEYS].properties = bd718xx_powerkey_parent_props;

	nodes[PWRON_KEY].parent = &nodes[GPIO_KEYS];
	nodes[PWRON_KEY].properties = bd718xx_powerkey_props;

	ret = bd718xx_i2c_register_swnodes(nodes);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, bd718xx_i2c_unregister_swnodes, nodes);
	if (ret)
		return ret;

	gpio_keys_cell.swnode = &nodes[GPIO_KEYS];

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &gpio_keys_cell, 1,
				   NULL, 0, irq_domain);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register power-button");

	return 0;
}

static int bd718xx_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;
	struct regmap_irq_chip_data *irq_data;
	struct irq_domain *irq_domain;
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

	irq_domain = regmap_irq_get_domain(irq_data);

	ret = devm_mfd_add_devices(&i2c->dev, PLATFORM_DEVID_AUTO,
				   mfd, cells, NULL, 0, irq_domain);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Failed to create subdevices\n");

	ret = bd718xx_i2c_register_pwrbutton(&i2c->dev, irq_domain);
	if (ret)
		return ret;

	return 0;
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
