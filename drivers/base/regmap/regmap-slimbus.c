// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017, Linaro Ltd.

#include <linux/regmap.h>
#include <linux/slimbus.h>
#include <linux/module.h>

#include "internal.h"

static int regmap_slimbus_byte_reg_read(void *context, unsigned int reg,
					unsigned int *val)
{
	struct slim_device *sdev = context;
	int v;

	v = slim_readb(sdev, reg);

	if (v < 0)
		return v;

	*val = v;

	return 0;
}

static int regmap_slimbus_byte_reg_write(void *context, unsigned int reg,
					 unsigned int val)
{
	struct slim_device *sdev = context;

	return slim_writeb(sdev, reg, val);
}

static struct regmap_bus regmap_slimbus_bus = {
	.reg_write = regmap_slimbus_byte_reg_write,
	.reg_read = regmap_slimbus_byte_reg_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static const struct regmap_bus *regmap_get_slimbus(struct slim_device *slim,
					const struct regmap_config *config)
{
	if (config->val_bits == 8 && config->reg_bits == 8)
		return &regmap_slimbus_bus;

	return ERR_PTR(-ENOTSUPP);
}

struct regmap *__regmap_init_slimbus(struct slim_device *slimbus,
				     const struct regmap_config *config,
				     struct lock_class_key *lock_key,
				     const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_slimbus(slimbus, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __regmap_init(&slimbus->dev, bus, &slimbus->dev, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_slimbus);

struct regmap *__devm_regmap_init_slimbus(struct slim_device *slimbus,
					  const struct regmap_config *config,
					  struct lock_class_key *lock_key,
					  const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_slimbus(slimbus, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __devm_regmap_init(&slimbus->dev, bus, &slimbus, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_slimbus);

MODULE_LICENSE("GPL v2");
