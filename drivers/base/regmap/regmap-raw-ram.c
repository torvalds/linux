// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - Memory region with raw access
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

static unsigned int decode_reg(enum regmap_endian endian, const void *reg)
{
	const u16 *r = reg;

	if (endian == REGMAP_ENDIAN_BIG)
		return be16_to_cpu(*r);
	else
		return le16_to_cpu(*r);
}

static int regmap_raw_ram_gather_write(void *context,
				       const void *reg, size_t reg_len,
				       const void *val, size_t val_len)
{
	struct regmap_ram_data *data = context;
	unsigned int r;
	u16 *our_buf = (u16 *)data->vals;
	int i;

	if (reg_len != 2)
		return -EINVAL;
	if (val_len % 2)
		return -EINVAL;

	r = decode_reg(data->reg_endian, reg);
	memcpy(&our_buf[r], val, val_len);

	for (i = 0; i < val_len / 2; i++)
		data->written[r + i] = true;
	
	return 0;
}

static int regmap_raw_ram_write(void *context, const void *data, size_t count)
{
	return regmap_raw_ram_gather_write(context, data, 2,
					   data + 2, count - 2);
}

static int regmap_raw_ram_read(void *context,
			       const void *reg, size_t reg_len,
			       void *val, size_t val_len)
{
	struct regmap_ram_data *data = context;
	unsigned int r;
	u16 *our_buf = (u16 *)data->vals;
	int i;

	if (reg_len != 2)
		return -EINVAL;
	if (val_len % 2)
		return -EINVAL;

	r = decode_reg(data->reg_endian, reg);
	memcpy(val, &our_buf[r], val_len);

	for (i = 0; i < val_len / 2; i++)
		data->read[r + i] = true;

	return 0;
}

static void regmap_raw_ram_free_context(void *context)
{
	struct regmap_ram_data *data = context;

	kfree(data->vals);
	kfree(data->read);
	kfree(data->written);
	kfree(data);
}

static const struct regmap_bus regmap_raw_ram = {
	.fast_io = true,
	.write = regmap_raw_ram_write,
	.gather_write = regmap_raw_ram_gather_write,
	.read = regmap_raw_ram_read,
	.free_context = regmap_raw_ram_free_context,
};

struct regmap *__regmap_init_raw_ram(const struct regmap_config *config,
				     struct regmap_ram_data *data,
				     struct lock_class_key *lock_key,
				     const char *lock_name)
{
	struct regmap *map;

	if (config->reg_bits != 16)
		return ERR_PTR(-EINVAL);

	if (!config->max_register) {
		pr_crit("No max_register specified for RAM regmap\n");
		return ERR_PTR(-EINVAL);
	}

	data->read = kcalloc(sizeof(bool), config->max_register + 1,
			     GFP_KERNEL);
	if (!data->read)
		return ERR_PTR(-ENOMEM);

	data->written = kcalloc(sizeof(bool), config->max_register + 1,
				GFP_KERNEL);
	if (!data->written)
		return ERR_PTR(-ENOMEM);

	data->reg_endian = config->reg_format_endian;

	map = __regmap_init(NULL, &regmap_raw_ram, data, config,
			    lock_key, lock_name);

	return map;
}
EXPORT_SYMBOL_GPL(__regmap_init_raw_ram);

MODULE_LICENSE("GPL v2");
