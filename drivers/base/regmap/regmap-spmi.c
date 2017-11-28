/*
 * Register map access API - SPMI support
 *
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Based on regmap-i2c.c:
 * Copyright 2011 Wolfson Microelectronics plc
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/module.h>
#include <linux/init.h>

static int regmap_spmi_base_read(void *context,
				 const void *reg, size_t reg_size,
				 void *val, size_t val_size)
{
	u8 addr = *(u8 *)reg;
	int err = 0;

	BUG_ON(reg_size != 1);

	while (val_size-- && !err)
		err = spmi_register_read(context, addr++, val++);

	return err;
}

static int regmap_spmi_base_gather_write(void *context,
					 const void *reg, size_t reg_size,
					 const void *val, size_t val_size)
{
	const u8 *data = val;
	u8 addr = *(u8 *)reg;
	int err = 0;

	BUG_ON(reg_size != 1);

	/*
	 * SPMI defines a more bandwidth-efficient 'Register 0 Write' sequence,
	 * use it when possible.
	 */
	if (addr == 0 && val_size) {
		err = spmi_register_zero_write(context, *data);
		if (err)
			goto err_out;

		data++;
		addr++;
		val_size--;
	}

	while (val_size) {
		err = spmi_register_write(context, addr, *data);
		if (err)
			goto err_out;

		data++;
		addr++;
		val_size--;
	}

err_out:
	return err;
}

static int regmap_spmi_base_write(void *context, const void *data,
				  size_t count)
{
	BUG_ON(count < 1);
	return regmap_spmi_base_gather_write(context, data, 1, data + 1,
					     count - 1);
}

static const struct regmap_bus regmap_spmi_base = {
	.read				= regmap_spmi_base_read,
	.write				= regmap_spmi_base_write,
	.gather_write			= regmap_spmi_base_gather_write,
	.reg_format_endian_default	= REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default	= REGMAP_ENDIAN_NATIVE,
};

struct regmap *__regmap_init_spmi_base(struct spmi_device *sdev,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name)
{
	return __regmap_init(&sdev->dev, &regmap_spmi_base, sdev, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_spmi_base);

struct regmap *__devm_regmap_init_spmi_base(struct spmi_device *sdev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name)
{
	return __devm_regmap_init(&sdev->dev, &regmap_spmi_base, sdev, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_spmi_base);

static int regmap_spmi_ext_read(void *context,
				const void *reg, size_t reg_size,
				void *val, size_t val_size)
{
	int err = 0;
	size_t len;
	u16 addr;

	BUG_ON(reg_size != 2);

	addr = *(u16 *)reg;

	/*
	 * Split accesses into two to take advantage of the more
	 * bandwidth-efficient 'Extended Register Read' command when possible
	 */
	while (addr <= 0xFF && val_size) {
		len = min_t(size_t, val_size, 16);

		err = spmi_ext_register_read(context, addr, val, len);
		if (err)
			goto err_out;

		addr += len;
		val += len;
		val_size -= len;
	}

	while (val_size) {
		len = min_t(size_t, val_size, 8);

		err = spmi_ext_register_readl(context, addr, val, len);
		if (err)
			goto err_out;

		addr += len;
		val += len;
		val_size -= len;
	}

err_out:
	return err;
}

static int regmap_spmi_ext_gather_write(void *context,
					const void *reg, size_t reg_size,
					const void *val, size_t val_size)
{
	int err = 0;
	size_t len;
	u16 addr;

	BUG_ON(reg_size != 2);

	addr = *(u16 *)reg;

	while (addr <= 0xFF && val_size) {
		len = min_t(size_t, val_size, 16);

		err = spmi_ext_register_write(context, addr, val, len);
		if (err)
			goto err_out;

		addr += len;
		val += len;
		val_size -= len;
	}

	while (val_size) {
		len = min_t(size_t, val_size, 8);

		err = spmi_ext_register_writel(context, addr, val, len);
		if (err)
			goto err_out;

		addr += len;
		val += len;
		val_size -= len;
	}

err_out:
	return err;
}

static int regmap_spmi_ext_write(void *context, const void *data,
				 size_t count)
{
	BUG_ON(count < 2);
	return regmap_spmi_ext_gather_write(context, data, 2, data + 2,
					    count - 2);
}

static const struct regmap_bus regmap_spmi_ext = {
	.read				= regmap_spmi_ext_read,
	.write				= regmap_spmi_ext_write,
	.gather_write			= regmap_spmi_ext_gather_write,
	.reg_format_endian_default	= REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default	= REGMAP_ENDIAN_NATIVE,
};

struct regmap *__regmap_init_spmi_ext(struct spmi_device *sdev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	return __regmap_init(&sdev->dev, &regmap_spmi_ext, sdev, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_spmi_ext);

struct regmap *__devm_regmap_init_spmi_ext(struct spmi_device *sdev,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name)
{
	return __devm_regmap_init(&sdev->dev, &regmap_spmi_ext, sdev, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_spmi_ext);

MODULE_LICENSE("GPL");
