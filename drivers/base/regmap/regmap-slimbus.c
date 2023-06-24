// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017, Linaro Ltd.

#include <linux/regmap.h>
#include <linux/slimbus.h>
#include <linux/module.h>

#include "internal.h"

static int regmap_slimbus_write(void *context, const void *data, size_t count)
{
	struct slim_device *sdev = context;

	return slim_write(sdev, *(u16 *)data, count - 2, (u8 *)data + 2);
}

static int regmap_slimbus_read(void *context, const void *reg, size_t reg_size,
			       void *val, size_t val_size)
{
	struct slim_device *sdev = context;

	return slim_read(sdev, *(u16 *)reg, val_size, val);
}

static const struct regmap_bus regmap_slimbus_bus = {
	.write = regmap_slimbus_write,
	.read = regmap_slimbus_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static const struct regmap_bus *regmap_get_slimbus(struct slim_device *slim,
					const struct regmap_config *config)
{
	if (config->val_bits == 8 && config->reg_bits == 16)
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
