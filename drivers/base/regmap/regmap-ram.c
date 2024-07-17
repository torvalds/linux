// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - Memory region
//
// This is intended for testing only
//
// Copyright (c) 2023, Arm Ltd

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/swab.h>

#include "internal.h"

static int regmap_ram_write(void *context, unsigned int reg, unsigned int val)
{
	struct regmap_ram_data *data = context;

	data->vals[reg] = val;
	data->written[reg] = true;

	return 0;
}

static int regmap_ram_read(void *context, unsigned int reg, unsigned int *val)
{
	struct regmap_ram_data *data = context;

	*val = data->vals[reg];
	data->read[reg] = true;

	return 0;
}

static void regmap_ram_free_context(void *context)
{
	struct regmap_ram_data *data = context;

	kfree(data->vals);
	kfree(data->read);
	kfree(data->written);
	kfree(data);
}

static const struct regmap_bus regmap_ram = {
	.fast_io = true,
	.reg_write = regmap_ram_write,
	.reg_read = regmap_ram_read,
	.free_context = regmap_ram_free_context,
};

struct regmap *__regmap_init_ram(struct device *dev,
				 const struct regmap_config *config,
				 struct regmap_ram_data *data,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	struct regmap *map;

	if (!config->max_register) {
		pr_crit("No max_register specified for RAM regmap\n");
		return ERR_PTR(-EINVAL);
	}

	data->read = kcalloc(config->max_register + 1, sizeof(bool),
			     GFP_KERNEL);
	if (!data->read)
		return ERR_PTR(-ENOMEM);

	data->written = kcalloc(config->max_register + 1, sizeof(bool),
				GFP_KERNEL);
	if (!data->written)
		return ERR_PTR(-ENOMEM);

	map = __regmap_init(dev, &regmap_ram, data, config,
			    lock_key, lock_name);

	return map;
}
EXPORT_SYMBOL_GPL(__regmap_init_ram);

MODULE_DESCRIPTION("Register map access API - Memory region");
MODULE_LICENSE("GPL v2");
