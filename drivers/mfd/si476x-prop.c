/*
 * drivers/mfd/si476x-prop.c -- Subroutines to access
 * properties of si476x chips
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/module.h>

#include <linux/mfd/si476x-core.h>

struct si476x_property_range {
	u16 low, high;
};

static bool si476x_core_element_is_in_array(u16 element,
					    const u16 array[],
					    size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		if (element == array[i])
			return true;

	return false;
}

static bool si476x_core_element_is_in_range(u16 element,
					    const struct si476x_property_range range[],
					    size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		if (element <= range[i].high && element >= range[i].low)
			return true;

	return false;
}

static bool si476x_core_is_valid_property_a10(struct si476x_core *core,
					      u16 property)
{
	static const u16 valid_properties[] = {
		0x0000,
		0x0500, 0x0501,
		0x0600,
		0x0709, 0x070C, 0x070D, 0x70E, 0x710,
		0x0718,
		0x1207, 0x1208,
		0x2007,
		0x2300,
	};

	static const struct si476x_property_range valid_ranges[] = {
		{ 0x0200, 0x0203 },
		{ 0x0300, 0x0303 },
		{ 0x0400, 0x0404 },
		{ 0x0700, 0x0707 },
		{ 0x1100, 0x1102 },
		{ 0x1200, 0x1204 },
		{ 0x1300, 0x1306 },
		{ 0x2000, 0x2005 },
		{ 0x2100, 0x2104 },
		{ 0x2106, 0x2106 },
		{ 0x2200, 0x220E },
		{ 0x3100, 0x3104 },
		{ 0x3207, 0x320F },
		{ 0x3300, 0x3304 },
		{ 0x3500, 0x3517 },
		{ 0x3600, 0x3617 },
		{ 0x3700, 0x3717 },
		{ 0x4000, 0x4003 },
	};

	return	si476x_core_element_is_in_range(property, valid_ranges,
						ARRAY_SIZE(valid_ranges)) ||
		si476x_core_element_is_in_array(property, valid_properties,
						ARRAY_SIZE(valid_properties));
}

static bool si476x_core_is_valid_property_a20(struct si476x_core *core,
					      u16 property)
{
	static const u16 valid_properties[] = {
		0x071B,
		0x1006,
		0x2210,
		0x3401,
	};

	static const struct si476x_property_range valid_ranges[] = {
		{ 0x2215, 0x2219 },
	};

	return	si476x_core_is_valid_property_a10(core, property) ||
		si476x_core_element_is_in_range(property, valid_ranges,
						ARRAY_SIZE(valid_ranges))  ||
		si476x_core_element_is_in_array(property, valid_properties,
						ARRAY_SIZE(valid_properties));
}

static bool si476x_core_is_valid_property_a30(struct si476x_core *core,
					      u16 property)
{
	static const u16 valid_properties[] = {
		0x071C, 0x071D,
		0x1007, 0x1008,
		0x220F, 0x2214,
		0x2301,
		0x3105, 0x3106,
		0x3402,
	};

	static const struct si476x_property_range valid_ranges[] = {
		{ 0x0405, 0x0411 },
		{ 0x2008, 0x200B },
		{ 0x2220, 0x2223 },
		{ 0x3100, 0x3106 },
	};

	return	si476x_core_is_valid_property_a20(core, property) ||
		si476x_core_element_is_in_range(property, valid_ranges,
						ARRAY_SIZE(valid_ranges)) ||
		si476x_core_element_is_in_array(property, valid_properties,
						ARRAY_SIZE(valid_properties));
}

typedef bool (*valid_property_pred_t) (struct si476x_core *, u16);

static bool si476x_core_is_valid_property(struct si476x_core *core,
					  u16 property)
{
	static const valid_property_pred_t is_valid_property[] = {
		[SI476X_REVISION_A10] = si476x_core_is_valid_property_a10,
		[SI476X_REVISION_A20] = si476x_core_is_valid_property_a20,
		[SI476X_REVISION_A30] = si476x_core_is_valid_property_a30,
	};

	BUG_ON(core->revision > SI476X_REVISION_A30 ||
	       core->revision == -1);
	return is_valid_property[core->revision](core, property);
}


static bool si476x_core_is_readonly_property(struct si476x_core *core,
					     u16 property)
{
	BUG_ON(core->revision > SI476X_REVISION_A30 ||
	       core->revision == -1);

	switch (core->revision) {
	case SI476X_REVISION_A10:
		return (property == 0x3200);
	case SI476X_REVISION_A20:
		return (property == 0x1006 ||
			property == 0x2210 ||
			property == 0x3200);
	case SI476X_REVISION_A30:
		return false;
	}

	return false;
}

static bool si476x_core_regmap_readable_register(struct device *dev,
						 unsigned int reg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct si476x_core *core = i2c_get_clientdata(client);

	return si476x_core_is_valid_property(core, (u16) reg);

}

static bool si476x_core_regmap_writable_register(struct device *dev,
						 unsigned int reg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct si476x_core *core = i2c_get_clientdata(client);

	return si476x_core_is_valid_property(core, (u16) reg) &&
		!si476x_core_is_readonly_property(core, (u16) reg);
}


static int si476x_core_regmap_write(void *context, unsigned int reg,
				    unsigned int val)
{
	return si476x_core_cmd_set_property(context, reg, val);
}

static int si476x_core_regmap_read(void *context, unsigned int reg,
				   unsigned *val)
{
	struct si476x_core *core = context;
	int err;

	err = si476x_core_cmd_get_property(core, reg);
	if (err < 0)
		return err;

	*val = err;

	return 0;
}


static const struct regmap_config si476x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = 0x4003,

	.writeable_reg = si476x_core_regmap_writable_register,
	.readable_reg = si476x_core_regmap_readable_register,

	.reg_read = si476x_core_regmap_read,
	.reg_write = si476x_core_regmap_write,

	.cache_type = REGCACHE_RBTREE,
};

struct regmap *devm_regmap_init_si476x(struct si476x_core *core)
{
	return devm_regmap_init(&core->client->dev, NULL,
				core, &si476x_regmap_config);
}
EXPORT_SYMBOL_GPL(devm_regmap_init_si476x);
