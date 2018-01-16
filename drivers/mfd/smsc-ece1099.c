/*
 * TI SMSC MFD Driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Sourav Poddar <sourav.poddar@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  GPL v2.
 *
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/mfd/smsc.h>
#include <linux/of_platform.h>

static const struct regmap_config smsc_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = SMSC_VEN_ID_H,
		.cache_type = REGCACHE_RBTREE,
};

static int smsc_i2c_probe(struct i2c_client *i2c,
			const struct i2c_device_id *id)
{
	struct smsc *smsc;
	int devid, rev, venid_l, venid_h;
	int ret;

	smsc = devm_kzalloc(&i2c->dev, sizeof(struct smsc),
				GFP_KERNEL);
	if (!smsc)
		return -ENOMEM;

	smsc->regmap = devm_regmap_init_i2c(i2c, &smsc_regmap_config);
	if (IS_ERR(smsc->regmap))
		return PTR_ERR(smsc->regmap);

	i2c_set_clientdata(i2c, smsc);
	smsc->dev = &i2c->dev;

#ifdef CONFIG_OF
	of_property_read_u32(i2c->dev.of_node, "clock", &smsc->clk);
#endif

	regmap_read(smsc->regmap, SMSC_DEV_ID, &devid);
	regmap_read(smsc->regmap, SMSC_DEV_REV, &rev);
	regmap_read(smsc->regmap, SMSC_VEN_ID_L, &venid_l);
	regmap_read(smsc->regmap, SMSC_VEN_ID_H, &venid_h);

	dev_info(&i2c->dev, "SMSCxxx devid: %02x rev: %02x venid: %02x\n",
		devid, rev, (venid_h << 8) | venid_l);

	ret = regmap_write(smsc->regmap, SMSC_CLK_CTRL, smsc->clk);
	if (ret)
		return ret;

#ifdef CONFIG_OF
	if (i2c->dev.of_node)
		ret = devm_of_platform_populate(&i2c->dev);
#endif

	return ret;
}

static const struct i2c_device_id smsc_i2c_id[] = {
	{ "smscece1099", 0},
	{},
};

static struct i2c_driver smsc_i2c_driver = {
	.driver = {
		   .name = "smsc",
	},
	.probe = smsc_i2c_probe,
	.id_table = smsc_i2c_id,
};
builtin_i2c_driver(smsc_i2c_driver);
