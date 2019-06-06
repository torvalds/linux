// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Synopsys, Inc. and/or its affiliates.

#include <linux/regmap.h>
#include <linux/i3c/device.h>
#include <linux/i3c/master.h>
#include <linux/module.h>

static int regmap_i3c_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i3c_device *i3c = dev_to_i3cdev(dev);
	struct i3c_priv_xfer xfers[] = {
		{
			.rnw = false,
			.len = count,
			.data.out = data,
		},
	};

	return i3c_device_do_priv_xfers(i3c, xfers, 1);
}

static int regmap_i3c_read(void *context,
			   const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct device *dev = context;
	struct i3c_device *i3c = dev_to_i3cdev(dev);
	struct i3c_priv_xfer xfers[2];

	xfers[0].rnw = false;
	xfers[0].len = reg_size;
	xfers[0].data.out = reg;

	xfers[1].rnw = true;
	xfers[1].len = val_size;
	xfers[1].data.in = val;

	return i3c_device_do_priv_xfers(i3c, xfers, 2);
}

static struct regmap_bus regmap_i3c = {
	.write = regmap_i3c_write,
	.read = regmap_i3c_read,
};

struct regmap *__devm_regmap_init_i3c(struct i3c_device *i3c,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	return __devm_regmap_init(&i3c->dev, &regmap_i3c, &i3c->dev, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_i3c);

MODULE_AUTHOR("Vitor Soares <vitor.soares@synopsys.com>");
MODULE_DESCRIPTION("Regmap I3C Module");
MODULE_LICENSE("GPL v2");
