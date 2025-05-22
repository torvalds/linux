// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom BCM590xx PMU
 *
 * Copyright 2014 Linaro Limited
 * Author: Matt Porter <mporter@linaro.org>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mfd/bcm590xx.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Under primary I2C address: */
#define BCM590XX_REG_PMUID		0x1e

#define BCM590XX_REG_PMUREV		0x1f
#define BCM590XX_PMUREV_DIG_MASK	0xF
#define BCM590XX_PMUREV_DIG_SHIFT	0
#define BCM590XX_PMUREV_ANA_MASK	0xF0
#define BCM590XX_PMUREV_ANA_SHIFT	4

static const struct mfd_cell bcm590xx_devs[] = {
	{
		.name = "bcm590xx-vregs",
	},
};

static const struct regmap_config bcm590xx_regmap_config_pri = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= BCM590XX_MAX_REGISTER_PRI,
	.cache_type	= REGCACHE_MAPLE,
};

static const struct regmap_config bcm590xx_regmap_config_sec = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= BCM590XX_MAX_REGISTER_SEC,
	.cache_type	= REGCACHE_MAPLE,
};

/* Map PMU ID value to model name string */
static const char * const bcm590xx_names[] = {
	[BCM590XX_PMUID_BCM59054] = "BCM59054",
	[BCM590XX_PMUID_BCM59056] = "BCM59056",
};

static int bcm590xx_parse_version(struct bcm590xx *bcm590xx)
{
	unsigned int id, rev;
	int ret;

	/* Get PMU ID and verify that it matches compatible */
	ret = regmap_read(bcm590xx->regmap_pri, BCM590XX_REG_PMUID, &id);
	if (ret) {
		dev_err(bcm590xx->dev, "failed to read PMU ID: %d\n", ret);
		return ret;
	}

	if (id != bcm590xx->pmu_id) {
		dev_err(bcm590xx->dev, "Incorrect ID for %s: expected %x, got %x.\n",
			bcm590xx_names[bcm590xx->pmu_id], bcm590xx->pmu_id, id);
		return -ENODEV;
	}

	/* Get PMU revision and store it in the info struct */
	ret = regmap_read(bcm590xx->regmap_pri, BCM590XX_REG_PMUREV, &rev);
	if (ret) {
		dev_err(bcm590xx->dev, "failed to read PMU revision: %d\n", ret);
		return ret;
	}

	bcm590xx->rev_digital = (rev & BCM590XX_PMUREV_DIG_MASK) >> BCM590XX_PMUREV_DIG_SHIFT;

	bcm590xx->rev_analog = (rev & BCM590XX_PMUREV_ANA_MASK) >> BCM590XX_PMUREV_ANA_SHIFT;

	dev_dbg(bcm590xx->dev, "PMU ID 0x%x (%s), revision: digital %d, analog %d",
		 id, bcm590xx_names[id], bcm590xx->rev_digital, bcm590xx->rev_analog);

	return 0;
}

static int bcm590xx_i2c_probe(struct i2c_client *i2c_pri)
{
	struct bcm590xx *bcm590xx;
	int ret;

	bcm590xx = devm_kzalloc(&i2c_pri->dev, sizeof(*bcm590xx), GFP_KERNEL);
	if (!bcm590xx)
		return -ENOMEM;

	i2c_set_clientdata(i2c_pri, bcm590xx);
	bcm590xx->dev = &i2c_pri->dev;
	bcm590xx->i2c_pri = i2c_pri;

	bcm590xx->pmu_id = (uintptr_t) of_device_get_match_data(bcm590xx->dev);

	bcm590xx->regmap_pri = devm_regmap_init_i2c(i2c_pri,
						 &bcm590xx_regmap_config_pri);
	if (IS_ERR(bcm590xx->regmap_pri)) {
		ret = PTR_ERR(bcm590xx->regmap_pri);
		dev_err(&i2c_pri->dev, "primary regmap init failed: %d\n", ret);
		return ret;
	}

	/* Secondary I2C slave address is the base address with A(2) asserted */
	bcm590xx->i2c_sec = i2c_new_dummy_device(i2c_pri->adapter,
					  i2c_pri->addr | BIT(2));
	if (IS_ERR(bcm590xx->i2c_sec)) {
		dev_err(&i2c_pri->dev, "failed to add secondary I2C device\n");
		return PTR_ERR(bcm590xx->i2c_sec);
	}
	i2c_set_clientdata(bcm590xx->i2c_sec, bcm590xx);

	bcm590xx->regmap_sec = devm_regmap_init_i2c(bcm590xx->i2c_sec,
						&bcm590xx_regmap_config_sec);
	if (IS_ERR(bcm590xx->regmap_sec)) {
		ret = PTR_ERR(bcm590xx->regmap_sec);
		dev_err(&bcm590xx->i2c_sec->dev,
			"secondary regmap init failed: %d\n", ret);
		goto err;
	}

	ret = bcm590xx_parse_version(bcm590xx);
	if (ret)
		goto err;

	ret = devm_mfd_add_devices(&i2c_pri->dev, -1, bcm590xx_devs,
				   ARRAY_SIZE(bcm590xx_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&i2c_pri->dev, "failed to add sub-devices: %d\n", ret);
		goto err;
	}

	return 0;

err:
	i2c_unregister_device(bcm590xx->i2c_sec);
	return ret;
}

static const struct of_device_id bcm590xx_of_match[] = {
	{
		.compatible = "brcm,bcm59054",
		.data = (void *)BCM590XX_PMUID_BCM59054,
	},
	{
		.compatible = "brcm,bcm59056",
		.data = (void *)BCM590XX_PMUID_BCM59056,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, bcm590xx_of_match);

static const struct i2c_device_id bcm590xx_i2c_id[] = {
	{ "bcm59054" },
	{ "bcm59056" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bcm590xx_i2c_id);

static struct i2c_driver bcm590xx_i2c_driver = {
	.driver = {
		   .name = "bcm590xx",
		   .of_match_table = bcm590xx_of_match,
	},
	.probe = bcm590xx_i2c_probe,
	.id_table = bcm590xx_i2c_id,
};
module_i2c_driver(bcm590xx_i2c_driver);

MODULE_AUTHOR("Matt Porter <mporter@linaro.org>");
MODULE_DESCRIPTION("BCM590xx multi-function driver");
MODULE_LICENSE("GPL v2");
