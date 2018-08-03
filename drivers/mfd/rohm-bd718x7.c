// SPDX-License-Identifier: GPL-2.0-or-later
//
// Copyright (C) 2018 ROHM Semiconductors
//
// ROHM BD71837MWV PMIC driver
//
// Datasheet available from
// https://www.rohm.com/datasheet/BD71837MWV/bd71837mwv-e

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mfd/rohm-bd718x7.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

/*
 * gpio_keys.h requires definiton of bool. It is brought in
 * by above includes. Keep this as last until gpio_keys.h gets fixed.
 */
#include <linux/gpio_keys.h>

static const u8 supported_revisions[] = { 0xA2 /* BD71837 */ };

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

static const struct regmap_irq bd71837_irqs[] = {
	REGMAP_IRQ_REG(BD71837_INT_SWRST, 0, BD71837_INT_SWRST_MASK),
	REGMAP_IRQ_REG(BD71837_INT_PWRBTN_S, 0, BD71837_INT_PWRBTN_S_MASK),
	REGMAP_IRQ_REG(BD71837_INT_PWRBTN_L, 0, BD71837_INT_PWRBTN_L_MASK),
	REGMAP_IRQ_REG(BD71837_INT_PWRBTN, 0, BD71837_INT_PWRBTN_MASK),
	REGMAP_IRQ_REG(BD71837_INT_WDOG, 0, BD71837_INT_WDOG_MASK),
	REGMAP_IRQ_REG(BD71837_INT_ON_REQ, 0, BD71837_INT_ON_REQ_MASK),
	REGMAP_IRQ_REG(BD71837_INT_STBY_REQ, 0, BD71837_INT_STBY_REQ_MASK),
};

static struct regmap_irq_chip bd71837_irq_chip = {
	.name = "bd71837-irq",
	.irqs = bd71837_irqs,
	.num_irqs = ARRAY_SIZE(bd71837_irqs),
	.num_regs = 1,
	.irq_reg_stride = 1,
	.status_base = BD71837_REG_IRQ,
	.mask_base = BD71837_REG_MIRQ,
	.ack_base = BD71837_REG_IRQ,
	.init_ack_masked = true,
	.mask_invert = false,
};

static const struct regmap_range pmic_status_range = {
	.range_min = BD71837_REG_IRQ,
	.range_max = BD71837_REG_POW_STATE,
};

static const struct regmap_access_table volatile_regs = {
	.yes_ranges = &pmic_status_range,
	.n_yes_ranges = 1,
};

static const struct regmap_config bd71837_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &volatile_regs,
	.max_register = BD71837_MAX_REGISTER - 1,
	.cache_type = REGCACHE_RBTREE,
};

static int bd71837_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct bd71837 *bd71837;
	int ret, i;
	unsigned int val;

	bd71837 = devm_kzalloc(&i2c->dev, sizeof(struct bd71837), GFP_KERNEL);

	if (!bd71837)
		return -ENOMEM;

	bd71837->chip_irq = i2c->irq;

	if (!bd71837->chip_irq) {
		dev_err(&i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	bd71837->dev = &i2c->dev;
	dev_set_drvdata(&i2c->dev, bd71837);

	bd71837->regmap = devm_regmap_init_i2c(i2c, &bd71837_regmap_config);
	if (IS_ERR(bd71837->regmap)) {
		dev_err(&i2c->dev, "regmap initialization failed\n");
		return PTR_ERR(bd71837->regmap);
	}

	ret = regmap_read(bd71837->regmap, BD71837_REG_REV, &val);
	if (ret) {
		dev_err(&i2c->dev, "Read BD71837_REG_DEVICE failed\n");
		return ret;
	}
	for (i = 0; i < ARRAY_SIZE(supported_revisions); i++)
		if (supported_revisions[i] == val)
			break;

	if (i == ARRAY_SIZE(supported_revisions)) {
		dev_err(&i2c->dev, "Unsupported chip revision\n");
		return -ENODEV;
	}

	ret = devm_regmap_add_irq_chip(&i2c->dev, bd71837->regmap,
				       bd71837->chip_irq, IRQF_ONESHOT, 0,
				       &bd71837_irq_chip, &bd71837->irq_data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add irq_chip\n");
		return ret;
	}

	/* Configure short press to 10 milliseconds */
	ret = regmap_update_bits(bd71837->regmap,
				 BD71837_REG_PWRONCONFIG0,
				 BD718XX_PWRBTN_PRESS_DURATION_MASK,
				 BD718XX_PWRBTN_SHORT_PRESS_10MS);
	if (ret) {
		dev_err(&i2c->dev,
			"Failed to configure button short press timeout\n");
		return ret;
	}

	/* Configure long press to 10 seconds */
	ret = regmap_update_bits(bd71837->regmap,
				 BD71837_REG_PWRONCONFIG1,
				 BD718XX_PWRBTN_PRESS_DURATION_MASK,
				 BD718XX_PWRBTN_LONG_PRESS_10S);

	if (ret) {
		dev_err(&i2c->dev,
			"Failed to configure button long press timeout\n");
		return ret;
	}

	ret = regmap_irq_get_virq(bd71837->irq_data, BD71837_INT_PWRBTN_S);

	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to get the IRQ\n");
		return ret;
	}

	button.irq = ret;

	ret = devm_mfd_add_devices(bd71837->dev, PLATFORM_DEVID_AUTO,
				   bd71837_mfd_cells,
				   ARRAY_SIZE(bd71837_mfd_cells), NULL, 0,
				   regmap_irq_get_domain(bd71837->irq_data));
	if (ret)
		dev_err(&i2c->dev, "Failed to create subdevices\n");

	return ret;
}

static const struct of_device_id bd71837_of_match[] = {
	{ .compatible = "rohm,bd71837", },
	{ }
};
MODULE_DEVICE_TABLE(of, bd71837_of_match);

static struct i2c_driver bd71837_i2c_driver = {
	.driver = {
		.name = "rohm-bd718x7",
		.of_match_table = bd71837_of_match,
	},
	.probe = bd71837_i2c_probe,
};

static int __init bd71837_i2c_init(void)
{
	return i2c_add_driver(&bd71837_i2c_driver);
}

/* Initialise early so consumer devices can complete system boot */
subsys_initcall(bd71837_i2c_init);

static void __exit bd71837_i2c_exit(void)
{
	i2c_del_driver(&bd71837_i2c_driver);
}
module_exit(bd71837_i2c_exit);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD71837 Power Management IC driver");
MODULE_LICENSE("GPL");
