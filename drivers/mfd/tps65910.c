/*
 * tps65910.c  --  TI TPS6591x
 *
 * Copyright 2010 Texas Instruments Inc.
 *
 * Author: Graeme Gregory <gg@slimlogic.co.uk>
 * Author: Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>
#include <linux/mfd/tps65910.h>
#include <linux/of_device.h>

static struct resource rtc_resources[] = {
	{
		.start  = TPS65910_IRQ_RTC_ALARM,
		.end    = TPS65910_IRQ_RTC_ALARM,
		.flags  = IORESOURCE_IRQ,
	}
};

static struct mfd_cell tps65910s[] = {
	{
		.name = "tps65910-gpio",
	},
	{
		.name = "tps65910-pmic",
	},
	{
		.name = "tps65910-rtc",
		.num_resources = ARRAY_SIZE(rtc_resources),
		.resources = &rtc_resources[0],
	},
	{
		.name = "tps65910-power",
	},
};


static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	struct tps65910 *tps65910 = dev_get_drvdata(dev);

	/*
	 * Caching all regulator registers.
	 * All regualator register address range is same for
	 * TPS65910 and TPS65911
	 */
	if ((reg >= TPS65910_VIO) && (reg <= TPS65910_VDAC)) {
		/* Check for non-existing register */
		if (tps65910_chip_id(tps65910) == TPS65910)
			if ((reg == TPS65911_VDDCTRL_OP) ||
				(reg == TPS65911_VDDCTRL_SR))
				return true;
		return false;
	}
	return true;
}

static const struct regmap_config tps65910_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_reg = is_volatile_reg,
	.max_register = TPS65910_MAX_REGISTER - 1,
	.cache_type = REGCACHE_RBTREE,
};

static int __devinit tps65910_ck32k_init(struct tps65910 *tps65910,
					struct tps65910_board *pmic_pdata)
{
	int ret;

	if (!pmic_pdata->en_ck32k_xtal)
		return 0;

	ret = tps65910_reg_clear_bits(tps65910, TPS65910_DEVCTRL,
						DEVCTRL_CK32K_CTRL_MASK);
	if (ret < 0) {
		dev_err(tps65910->dev, "clear ck32k_ctrl failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __devinit tps65910_sleepinit(struct tps65910 *tps65910,
		struct tps65910_board *pmic_pdata)
{
	struct device *dev = NULL;
	int ret = 0;

	dev = tps65910->dev;

	if (!pmic_pdata->en_dev_slp)
		return 0;

	/* enabling SLEEP device state */
	ret = tps65910_reg_set_bits(tps65910, TPS65910_DEVCTRL,
				DEVCTRL_DEV_SLP_MASK);
	if (ret < 0) {
		dev_err(dev, "set dev_slp failed: %d\n", ret);
		goto err_sleep_init;
	}

	/* Return if there is no sleep keepon data. */
	if (!pmic_pdata->slp_keepon)
		return 0;

	if (pmic_pdata->slp_keepon->therm_keepon) {
		ret = tps65910_reg_set_bits(tps65910,
				TPS65910_SLEEP_KEEP_RES_ON,
				SLEEP_KEEP_RES_ON_THERM_KEEPON_MASK);
		if (ret < 0) {
			dev_err(dev, "set therm_keepon failed: %d\n", ret);
			goto disable_dev_slp;
		}
	}

	if (pmic_pdata->slp_keepon->clkout32k_keepon) {
		ret = tps65910_reg_set_bits(tps65910,
				TPS65910_SLEEP_KEEP_RES_ON,
				SLEEP_KEEP_RES_ON_CLKOUT32K_KEEPON_MASK);
		if (ret < 0) {
			dev_err(dev, "set clkout32k_keepon failed: %d\n", ret);
			goto disable_dev_slp;
		}
	}

	if (pmic_pdata->slp_keepon->i2chs_keepon) {
		ret = tps65910_reg_set_bits(tps65910,
				TPS65910_SLEEP_KEEP_RES_ON,
				SLEEP_KEEP_RES_ON_I2CHS_KEEPON_MASK);
		if (ret < 0) {
			dev_err(dev, "set i2chs_keepon failed: %d\n", ret);
			goto disable_dev_slp;
		}
	}

	return 0;

disable_dev_slp:
	tps65910_reg_clear_bits(tps65910, TPS65910_DEVCTRL,
				DEVCTRL_DEV_SLP_MASK);

err_sleep_init:
	return ret;
}

#ifdef CONFIG_OF
static struct of_device_id tps65910_of_match[] = {
	{ .compatible = "ti,tps65910", .data = (void *)TPS65910},
	{ .compatible = "ti,tps65911", .data = (void *)TPS65911},
	{ },
};
MODULE_DEVICE_TABLE(of, tps65910_of_match);

static struct tps65910_board *tps65910_parse_dt(struct i2c_client *client,
						int *chip_id)
{
	struct device_node *np = client->dev.of_node;
	struct tps65910_board *board_info;
	unsigned int prop;
	const struct of_device_id *match;
	int ret = 0;

	match = of_match_device(tps65910_of_match, &client->dev);
	if (!match) {
		dev_err(&client->dev, "Failed to find matching dt id\n");
		return NULL;
	}

	*chip_id  = (int)match->data;

	board_info = devm_kzalloc(&client->dev, sizeof(*board_info),
			GFP_KERNEL);
	if (!board_info) {
		dev_err(&client->dev, "Failed to allocate pdata\n");
		return NULL;
	}

	ret = of_property_read_u32(np, "ti,vmbch-threshold", &prop);
	if (!ret)
		board_info->vmbch_threshold = prop;
	else if (*chip_id == TPS65911)
		dev_warn(&client->dev, "VMBCH-Threshold not specified");

	ret = of_property_read_u32(np, "ti,vmbch2-threshold", &prop);
	if (!ret)
		board_info->vmbch2_threshold = prop;
	else if (*chip_id == TPS65911)
		dev_warn(&client->dev, "VMBCH2-Threshold not specified");

	prop = of_property_read_bool(np, "ti,en-ck32k-xtal");
	board_info->en_ck32k_xtal = prop;

	board_info->irq = client->irq;
	board_info->irq_base = -1;
	board_info->pm_off = of_property_read_bool(np,
			"ti,system-power-controller");

	return board_info;
}
#else
static inline
struct tps65910_board *tps65910_parse_dt(struct i2c_client *client,
					 int *chip_id)
{
	return NULL;
}
#endif

static struct i2c_client *tps65910_i2c_client;
static void tps65910_power_off(void)
{
	struct tps65910 *tps65910;

	tps65910 = dev_get_drvdata(&tps65910_i2c_client->dev);

	if (tps65910_reg_set_bits(tps65910, TPS65910_DEVCTRL,
			DEVCTRL_PWR_OFF_MASK) < 0)
		return;

	tps65910_reg_clear_bits(tps65910, TPS65910_DEVCTRL,
			DEVCTRL_DEV_ON_MASK);
}

static __devinit int tps65910_i2c_probe(struct i2c_client *i2c,
					const struct i2c_device_id *id)
{
	struct tps65910 *tps65910;
	struct tps65910_board *pmic_plat_data;
	struct tps65910_board *of_pmic_plat_data = NULL;
	struct tps65910_platform_data *init_data;
	int ret = 0;
	int chip_id = id->driver_data;

	pmic_plat_data = dev_get_platdata(&i2c->dev);

	if (!pmic_plat_data && i2c->dev.of_node) {
		pmic_plat_data = tps65910_parse_dt(i2c, &chip_id);
		of_pmic_plat_data = pmic_plat_data;
	}

	if (!pmic_plat_data)
		return -EINVAL;

	init_data = devm_kzalloc(&i2c->dev, sizeof(*init_data), GFP_KERNEL);
	if (init_data == NULL)
		return -ENOMEM;

	tps65910 = devm_kzalloc(&i2c->dev, sizeof(*tps65910), GFP_KERNEL);
	if (tps65910 == NULL)
		return -ENOMEM;

	tps65910->of_plat_data = of_pmic_plat_data;
	i2c_set_clientdata(i2c, tps65910);
	tps65910->dev = &i2c->dev;
	tps65910->i2c_client = i2c;
	tps65910->id = chip_id;
	mutex_init(&tps65910->io_mutex);

	tps65910->regmap = devm_regmap_init_i2c(i2c, &tps65910_regmap_config);
	if (IS_ERR(tps65910->regmap)) {
		ret = PTR_ERR(tps65910->regmap);
		dev_err(&i2c->dev, "regmap initialization failed: %d\n", ret);
		return ret;
	}

	init_data->irq = pmic_plat_data->irq;
	init_data->irq_base = pmic_plat_data->irq_base;

	tps65910_irq_init(tps65910, init_data->irq, init_data);
	tps65910_ck32k_init(tps65910, pmic_plat_data);
	tps65910_sleepinit(tps65910, pmic_plat_data);

	if (pmic_plat_data->pm_off && !pm_power_off) {
		tps65910_i2c_client = i2c;
		pm_power_off = tps65910_power_off;
	}

	ret = mfd_add_devices(tps65910->dev, -1,
			      tps65910s, ARRAY_SIZE(tps65910s),
			      NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&i2c->dev, "mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static __devexit int tps65910_i2c_remove(struct i2c_client *i2c)
{
	struct tps65910 *tps65910 = i2c_get_clientdata(i2c);

	tps65910_irq_exit(tps65910);
	mfd_remove_devices(tps65910->dev);

	return 0;
}

static const struct i2c_device_id tps65910_i2c_id[] = {
       { "tps65910", TPS65910 },
       { "tps65911", TPS65911 },
       { }
};
MODULE_DEVICE_TABLE(i2c, tps65910_i2c_id);


static struct i2c_driver tps65910_i2c_driver = {
	.driver = {
		   .name = "tps65910",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(tps65910_of_match),
	},
	.probe = tps65910_i2c_probe,
	.remove = __devexit_p(tps65910_i2c_remove),
	.id_table = tps65910_i2c_id,
};

static int __init tps65910_i2c_init(void)
{
	return i2c_add_driver(&tps65910_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(tps65910_i2c_init);

static void __exit tps65910_i2c_exit(void)
{
	i2c_del_driver(&tps65910_i2c_driver);
}
module_exit(tps65910_i2c_exit);

MODULE_AUTHOR("Graeme Gregory <gg@slimlogic.co.uk>");
MODULE_AUTHOR("Jorge Eduardo Candelaria <jedu@slimlogic.co.uk>");
MODULE_DESCRIPTION("TPS6591x chip family multi-function driver");
MODULE_LICENSE("GPL");
