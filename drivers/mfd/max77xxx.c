/*
 * max77xxx.c - mfd core driver for the Maxim 77686 and 77802
 *
 * Copyright (C) 2013 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max8997.c
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77xxx.h>
#include <linux/mfd/max77xxx-private.h>
#include <linux/of_gpio.h>
#include <linux/err.h>

static struct mfd_cell max77xxx_devs[TYPE_COUNT][2] = {
	{
		{ .name = "max77686-pmic", },
		{ .name = "max77686-rtc", },
	},
	{
		{ .name = "max77802-pmic", },
		{ .name = "max77802-rtc", },
	},
};

static bool max77686_is_accessible_reg(struct device *dev, unsigned int reg)
{
	return (reg >= MAX77XXX_REG_DEVICE_ID && reg < MAX77686_REG_PMIC_END);
}

static bool max77802_is_accessible_reg(struct device *dev, unsigned int reg)
{
	return (reg >= MAX77XXX_REG_DEVICE_ID && reg < MAX77802_REG_PMIC_END);
}

static bool max77xxx_is_precious_reg(struct device *dev, unsigned int reg)
{
	return (reg == MAX77XXX_REG_INTSRC || reg == MAX77XXX_REG_INT1 ||
		reg == MAX77XXX_REG_INT2);
}

static bool max77xxx_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (max77xxx_is_precious_reg(dev, reg) ||
		reg == MAX77XXX_REG_STATUS1 || reg == MAX77XXX_REG_STATUS2 ||
		reg == MAX77XXX_REG_PWRON);
}

static const struct regmap_config max77686_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = max77686_is_accessible_reg,
	.readable_reg = max77686_is_accessible_reg,
	.precious_reg = max77xxx_is_precious_reg,
	.volatile_reg = max77xxx_is_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_config max77802_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = max77802_is_accessible_reg,
	.readable_reg = max77802_is_accessible_reg,
	.precious_reg = max77xxx_is_precious_reg,
	.volatile_reg = max77xxx_is_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

#ifdef CONFIG_OF
static const struct of_device_id max77xxx_pmic_dt_match[] = {
	{.compatible = "maxim,max77686", .data = (void *)TYPE_MAX77686},
	{.compatible = "maxim,max77802", .data = (void *)TYPE_MAX77802},
	{},
};

static void max77xxx_dt_parse_dvs_gpio(struct device *dev,
				struct max77xxx_platform_data *pd,
				struct device_node *np)
{
	int i;

	/*
	 * NOTE: we don't consder GPIO errors fatal; board may have some lines
	 * directly pulled high or low and thus doesn't specify them.
	 */
	for (i = 0; i < ARRAY_SIZE(pd->buck_gpio_dvs); i++)
		pd->buck_gpio_dvs[i] =
			of_get_named_gpio(np,
					  "max77xxx,pmic-buck-dvs-gpios", i);

	for (i = 0; i < ARRAY_SIZE(pd->buck_gpio_selb); i++)
		pd->buck_gpio_selb[i] =
			of_get_named_gpio(np,
					  "max77xxx,pmic-buck-selb-gpios", i);
}

static struct max77xxx_platform_data *max77xxx_i2c_parse_dt_pdata(struct device
								  *dev)
{
	struct device_node *np = dev->of_node;
	struct max77xxx_platform_data *pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		dev_err(dev, "could not allocate memory for pdata\n");
		return NULL;
	}

	/* Read default index and ignore errors, since default is 0 */
	of_property_read_u32(np, "max77xxx,pmic-buck-default-dvs-idx",
			     &pd->buck_default_idx);

	max77xxx_dt_parse_dvs_gpio(dev, pd, np);

	dev->platform_data = pd;
	return pd;
}
#else
static struct max77xxx_platform_data *max77xxx_i2c_parse_dt_pdata(struct device
								  *dev)
{
	return 0;
}
#endif

static int max77xxx_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max77xxx_dev *max77xxx = NULL;
	struct max77xxx_platform_data *pdata = i2c->dev.platform_data;
	struct device_node *np;
	unsigned int data;
	int num_devs;
	int ret = 0;
	const struct regmap_config *config;

	if (i2c->dev.of_node)
		pdata = max77xxx_i2c_parse_dt_pdata(&i2c->dev);

	if (!pdata) {
		ret = -EIO;
		dev_err(&i2c->dev, "No platform data found.\n");
		goto err;
	}

	max77xxx = devm_kzalloc(&i2c->dev, sizeof(struct max77xxx_dev),
				GFP_KERNEL);
	if (max77xxx == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77xxx);
	max77xxx->dev = &i2c->dev;
	max77xxx->i2c = i2c;
	max77xxx->type = id->driver_data;

	max77xxx->wakeup = pdata->wakeup;
	max77xxx->irq_gpio = pdata->irq_gpio;
	max77xxx->irq = i2c->irq;

	if (max77xxx->type == TYPE_MAX77686)
		config = &max77686_regmap_config;
	else
		config = &max77802_regmap_config;
	max77xxx->regmap = devm_regmap_init_i2c(i2c, config);
	if (IS_ERR(max77xxx->regmap)) {
		ret = PTR_ERR(max77xxx->regmap);
		dev_err(max77xxx->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (regmap_read(max77xxx->regmap,
			 MAX77XXX_REG_DEVICE_ID, &data) < 0) {
		dev_err(max77xxx->dev,
			"device not found on this channel (this is not an error)\n");
		ret = -ENODEV;
		goto err;
	} else
		dev_info(max77xxx->dev, "device found\n");

	/* If there is an rtc, use it */
	num_devs = 1;
	np = of_find_node_by_name(i2c->dev.of_node, "rtc");
	if (np) {
		u32 addr;

		/* Just to be safe, add some checks on this */
		if (!of_property_read_u32(np, "reg", &addr)) {
			if (max77xxx->type == TYPE_MAX77802) {
				dev_err(max77xxx->dev,
					"Do not specify slave address for MAX77802 RTC\n");
				ret = -ENODEV;
				goto err;
			}
			max77xxx->rtc = i2c_new_dummy(i2c->adapter, addr);
			i2c_set_clientdata(max77xxx->rtc, max77xxx);
		} else if (max77xxx->type == TYPE_MAX77686) {
			dev_err(max77xxx->dev, "Must specify slave address for MAX77686 RTC\n");
			ret = -ENODEV;
			goto err;
		}
		num_devs++;
	}

	ret = max77xxx_irq_init(max77xxx);
	if (ret) {
		dev_err(max77xxx->dev, "Failed to init IRQs\n");
		return ret;
	}

	ret = mfd_add_devices(max77xxx->dev, -1, max77xxx_devs[max77xxx->type],
			      num_devs, NULL, 0, NULL);

	if (ret < 0)
		goto err_mfd;

	return 0;

err_mfd:
	mfd_remove_devices(max77xxx->dev);
	i2c_unregister_device(max77xxx->rtc);
err:
	return ret;
}

static int max77xxx_i2c_remove(struct i2c_client *i2c)
{
	struct max77xxx_dev *max77xxx = i2c_get_clientdata(i2c);

	mfd_remove_devices(max77xxx->dev);
	i2c_unregister_device(max77xxx->rtc);

	return 0;
}

static const struct i2c_device_id max77xxx_i2c_id[] = {
	{ "max77686", TYPE_MAX77686 },
	{ "max77802", TYPE_MAX77802 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77xxx_i2c_id);

#ifdef CONFIG_PM_SLEEP
static int max77xxx_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct max77xxx_dev *max77xxx = i2c_get_clientdata(i2c);

	/* TODO: Shouldn't max77xxx-irq do this itself? */
	max77xxx_irq_resume(max77xxx);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(max77xxx_pm_ops, NULL, max77xxx_resume);

static struct i2c_driver max77xxx_i2c_driver = {
	.driver = {
		   .name = "max77xxx",
		   .owner = THIS_MODULE,
		   .pm = &max77xxx_pm_ops,
		   .of_match_table = of_match_ptr(max77xxx_pmic_dt_match),
	},
	.probe = max77xxx_i2c_probe,
	.remove = max77xxx_i2c_remove,
	.id_table = max77xxx_i2c_id,
};

static int __init max77xxx_i2c_init(void)
{
	return i2c_add_driver(&max77xxx_i2c_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(max77xxx_i2c_init);

static void __exit max77xxx_i2c_exit(void)
{
	i2c_del_driver(&max77xxx_i2c_driver);
}
module_exit(max77xxx_i2c_exit);

MODULE_DESCRIPTION("MAXIM 77xxx multi-function core driver");
MODULE_AUTHOR("Simon Glass <sjg@chromium.org>");
MODULE_LICENSE("GPL");
