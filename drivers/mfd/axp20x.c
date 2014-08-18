/*
 * axp20x.c - MFD core driver for the X-Powers AXP202 and AXP209
 *
 * AXP20x comprises an adaptive USB-Compatible PWM charger, 2 BUCK DC-DC
 * converters, 5 LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as 4 configurable GPIOs.
 *
 * Author: Carlo Caione <carlo@caione.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/axp20x.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#define AXP20X_OFF	0x80

static const struct regmap_range axp20x_writeable_ranges[] = {
	regmap_reg_range(AXP20X_DATACACHE(0), AXP20X_IRQ5_STATE),
	regmap_reg_range(AXP20X_DCDC_MODE, AXP20X_FG_RES),
};

static const struct regmap_range axp20x_volatile_ranges[] = {
	regmap_reg_range(AXP20X_IRQ1_EN, AXP20X_IRQ5_STATE),
};

static const struct regmap_access_table axp20x_writeable_table = {
	.yes_ranges	= axp20x_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_writeable_ranges),
};

static const struct regmap_access_table axp20x_volatile_table = {
	.yes_ranges	= axp20x_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp20x_volatile_ranges),
};

static struct resource axp20x_pek_resources[] = {
	{
		.name	= "PEK_DBR",
		.start	= AXP20X_IRQ_PEK_RIS_EDGE,
		.end	= AXP20X_IRQ_PEK_RIS_EDGE,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "PEK_DBF",
		.start	= AXP20X_IRQ_PEK_FAL_EDGE,
		.end	= AXP20X_IRQ_PEK_FAL_EDGE,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct regmap_config axp20x_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.wr_table	= &axp20x_writeable_table,
	.volatile_table	= &axp20x_volatile_table,
	.max_register	= AXP20X_FG_RES,
	.cache_type	= REGCACHE_RBTREE,
};

#define AXP20X_IRQ(_irq, _off, _mask) \
	[AXP20X_IRQ_##_irq] = { .reg_offset = (_off), .mask = BIT(_mask) }

static const struct regmap_irq axp20x_regmap_irqs[] = {
	AXP20X_IRQ(ACIN_OVER_V,		0, 7),
	AXP20X_IRQ(ACIN_PLUGIN,		0, 6),
	AXP20X_IRQ(ACIN_REMOVAL,	0, 5),
	AXP20X_IRQ(VBUS_OVER_V,		0, 4),
	AXP20X_IRQ(VBUS_PLUGIN,		0, 3),
	AXP20X_IRQ(VBUS_REMOVAL,	0, 2),
	AXP20X_IRQ(VBUS_V_LOW,		0, 1),
	AXP20X_IRQ(BATT_PLUGIN,		1, 7),
	AXP20X_IRQ(BATT_REMOVAL,	1, 6),
	AXP20X_IRQ(BATT_ENT_ACT_MODE,	1, 5),
	AXP20X_IRQ(BATT_EXIT_ACT_MODE,	1, 4),
	AXP20X_IRQ(CHARG,		1, 3),
	AXP20X_IRQ(CHARG_DONE,		1, 2),
	AXP20X_IRQ(BATT_TEMP_HIGH,	1, 1),
	AXP20X_IRQ(BATT_TEMP_LOW,	1, 0),
	AXP20X_IRQ(DIE_TEMP_HIGH,	2, 7),
	AXP20X_IRQ(CHARG_I_LOW,		2, 6),
	AXP20X_IRQ(DCDC1_V_LONG,	2, 5),
	AXP20X_IRQ(DCDC2_V_LONG,	2, 4),
	AXP20X_IRQ(DCDC3_V_LONG,	2, 3),
	AXP20X_IRQ(PEK_SHORT,		2, 1),
	AXP20X_IRQ(PEK_LONG,		2, 0),
	AXP20X_IRQ(N_OE_PWR_ON,		3, 7),
	AXP20X_IRQ(N_OE_PWR_OFF,	3, 6),
	AXP20X_IRQ(VBUS_VALID,		3, 5),
	AXP20X_IRQ(VBUS_NOT_VALID,	3, 4),
	AXP20X_IRQ(VBUS_SESS_VALID,	3, 3),
	AXP20X_IRQ(VBUS_SESS_END,	3, 2),
	AXP20X_IRQ(LOW_PWR_LVL1,	3, 1),
	AXP20X_IRQ(LOW_PWR_LVL2,	3, 0),
	AXP20X_IRQ(TIMER,		4, 7),
	AXP20X_IRQ(PEK_RIS_EDGE,	4, 6),
	AXP20X_IRQ(PEK_FAL_EDGE,	4, 5),
	AXP20X_IRQ(GPIO3_INPUT,		4, 3),
	AXP20X_IRQ(GPIO2_INPUT,		4, 2),
	AXP20X_IRQ(GPIO1_INPUT,		4, 1),
	AXP20X_IRQ(GPIO0_INPUT,		4, 0),
};

static const struct of_device_id axp20x_of_match[] = {
	{ .compatible = "x-powers,axp202", .data = (void *) AXP202_ID },
	{ .compatible = "x-powers,axp209", .data = (void *) AXP209_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, axp20x_of_match);

/*
 * This is useless for OF-enabled devices, but it is needed by I2C subsystem
 */
static const struct i2c_device_id axp20x_i2c_id[] = {
	{ },
};
MODULE_DEVICE_TABLE(i2c, axp20x_i2c_id);

static const struct regmap_irq_chip axp20x_regmap_irq_chip = {
	.name			= "axp20x_irq_chip",
	.status_base		= AXP20X_IRQ1_STATE,
	.ack_base		= AXP20X_IRQ1_STATE,
	.mask_base		= AXP20X_IRQ1_EN,
	.num_regs		= 5,
	.irqs			= axp20x_regmap_irqs,
	.num_irqs		= ARRAY_SIZE(axp20x_regmap_irqs),
	.mask_invert		= true,
	.init_ack_masked	= true,
};

static const char * const axp20x_supplies[] = {
	"acin",
	"vin2",
	"vin3",
	"ldo24in",
	"ldo3in",
	"ldo5in",
};

static struct mfd_cell axp20x_cells[] = {
	{
		.name			= "axp20x-pek",
		.num_resources		= ARRAY_SIZE(axp20x_pek_resources),
		.resources		= axp20x_pek_resources,
	}, {
		.name			= "axp20x-regulator",
		.parent_supplies	= axp20x_supplies,
		.num_parent_supplies	= ARRAY_SIZE(axp20x_supplies),
	},
};

static struct axp20x_dev *axp20x_pm_power_off;
static void axp20x_power_off(void)
{
	regmap_write(axp20x_pm_power_off->regmap, AXP20X_OFF_CTRL,
		     AXP20X_OFF);
}

static int axp20x_i2c_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct axp20x_dev *axp20x;
	const struct of_device_id *of_id;
	int ret;

	axp20x = devm_kzalloc(&i2c->dev, sizeof(*axp20x), GFP_KERNEL);
	if (!axp20x)
		return -ENOMEM;

	of_id = of_match_device(axp20x_of_match, &i2c->dev);
	if (!of_id) {
		dev_err(&i2c->dev, "Unable to setup AXP20X data\n");
		return -ENODEV;
	}
	axp20x->variant = (long) of_id->data;

	axp20x->i2c_client = i2c;
	axp20x->dev = &i2c->dev;
	dev_set_drvdata(axp20x->dev, axp20x);

	axp20x->regmap = devm_regmap_init_i2c(i2c, &axp20x_regmap_config);
	if (IS_ERR(axp20x->regmap)) {
		ret = PTR_ERR(axp20x->regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	ret = regmap_add_irq_chip(axp20x->regmap, i2c->irq,
				  IRQF_ONESHOT | IRQF_SHARED, -1,
				  &axp20x_regmap_irq_chip,
				  &axp20x->regmap_irqc);
	if (ret) {
		dev_err(&i2c->dev, "failed to add irq chip: %d\n", ret);
		return ret;
	}

	ret = mfd_add_devices(axp20x->dev, -1, axp20x_cells,
			      ARRAY_SIZE(axp20x_cells), NULL, 0, NULL);

	if (ret) {
		dev_err(&i2c->dev, "failed to add MFD devices: %d\n", ret);
		regmap_del_irq_chip(i2c->irq, axp20x->regmap_irqc);
		return ret;
	}

	if (!pm_power_off) {
		axp20x_pm_power_off = axp20x;
		pm_power_off = axp20x_power_off;
	}

	dev_info(&i2c->dev, "AXP20X driver loaded\n");

	return 0;
}

static int axp20x_i2c_remove(struct i2c_client *i2c)
{
	struct axp20x_dev *axp20x = i2c_get_clientdata(i2c);

	if (axp20x == axp20x_pm_power_off) {
		axp20x_pm_power_off = NULL;
		pm_power_off = NULL;
	}

	mfd_remove_devices(axp20x->dev);
	regmap_del_irq_chip(axp20x->i2c_client->irq, axp20x->regmap_irqc);

	return 0;
}

static struct i2c_driver axp20x_i2c_driver = {
	.driver = {
		.name	= "axp20x",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(axp20x_of_match),
	},
	.probe		= axp20x_i2c_probe,
	.remove		= axp20x_i2c_remove,
	.id_table	= axp20x_i2c_id,
};

module_i2c_driver(axp20x_i2c_driver);

MODULE_DESCRIPTION("PMIC MFD core driver for AXP20X");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_LICENSE("GPL");
