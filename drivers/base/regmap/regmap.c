/*
 * Register map access API
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/err.h>

#define CREATE_TRACE_POINTS
#include <trace/events/regmap.h>

#include "internal.h"

bool regmap_writeable(struct regmap *map, unsigned int reg)
{
	if (map->max_register && reg > map->max_register)
		return false;

	if (map->writeable_reg)
		return map->writeable_reg(map->dev, reg);

	return true;
}

bool regmap_readable(struct regmap *map, unsigned int reg)
{
	if (map->max_register && reg > map->max_register)
		return false;

	if (map->readable_reg)
		return map->readable_reg(map->dev, reg);

	return true;
}

bool regmap_volatile(struct regmap *map, unsigned int reg)
{
	if (map->max_register && reg > map->max_register)
		return false;

	if (map->volatile_reg)
		return map->volatile_reg(map->dev, reg);

	return true;
}

bool regmap_precious(struct regmap *map, unsigned int reg)
{
	if (map->max_register && reg > map->max_register)
		return false;

	if (map->precious_reg)
		return map->precious_reg(map->dev, reg);

	return false;
}

static void regmap_format_4_12_write(struct regmap *map,
				     unsigned int reg, unsigned int val)
{
	__be16 *out = map->work_buf;
	*out = cpu_to_be16((reg << 12) | val);
}

static void regmap_format_7_9_write(struct regmap *map,
				    unsigned int reg, unsigned int val)
{
	__be16 *out = map->work_buf;
	*out = cpu_to_be16((reg << 9) | val);
}

static void regmap_format_8(void *buf, unsigned int val)
{
	u8 *b = buf;

	b[0] = val;
}

static void regmap_format_16(void *buf, unsigned int val)
{
	__be16 *b = buf;

	b[0] = cpu_to_be16(val);
}

static unsigned int regmap_parse_8(void *buf)
{
	u8 *b = buf;

	return b[0];
}

static unsigned int regmap_parse_16(void *buf)
{
	__be16 *b = buf;

	b[0] = be16_to_cpu(b[0]);

	return b[0];
}

/**
 * regmap_init(): Initialise register map
 *
 * @dev: Device that will be interacted with
 * @bus: Bus-specific callbacks to use with device
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.  This function should generally not be called
 * directly, it should be called by bus-specific init functions.
 */
struct regmap *regmap_init(struct device *dev,
			   const struct regmap_bus *bus,
			   const struct regmap_config *config)
{
	struct regmap *map;
	int ret = -EINVAL;

	if (!bus || !config)
		return NULL;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_init(&map->lock);
	map->format.buf_size = (config->reg_bits + config->val_bits) / 8;
	map->format.reg_bytes = config->reg_bits / 8;
	map->format.val_bytes = config->val_bits / 8;
	map->dev = dev;
	map->bus = bus;
	map->max_register = config->max_register;
	map->writeable_reg = config->writeable_reg;
	map->readable_reg = config->readable_reg;
	map->volatile_reg = config->volatile_reg;
	map->precious_reg = config->precious_reg;
	map->cache_type = config->cache_type;
	map->reg_defaults = config->reg_defaults;
	map->num_reg_defaults = config->num_reg_defaults;
	map->num_reg_defaults_raw = config->num_reg_defaults_raw;
	map->reg_defaults_raw = config->reg_defaults_raw;
	map->cache_size_raw = (config->val_bits / 8) * config->num_reg_defaults_raw;
	map->cache_word_size = config->val_bits / 8;

	if (config->read_flag_mask || config->write_flag_mask) {
		map->read_flag_mask = config->read_flag_mask;
		map->write_flag_mask = config->write_flag_mask;
	} else {
		map->read_flag_mask = bus->read_flag_mask;
	}

	switch (config->reg_bits) {
	case 4:
		switch (config->val_bits) {
		case 12:
			map->format.format_write = regmap_format_4_12_write;
			break;
		default:
			goto err_map;
		}
		break;

	case 7:
		switch (config->val_bits) {
		case 9:
			map->format.format_write = regmap_format_7_9_write;
			break;
		default:
			goto err_map;
		}
		break;

	case 8:
		map->format.format_reg = regmap_format_8;
		break;

	case 16:
		map->format.format_reg = regmap_format_16;
		break;

	default:
		goto err_map;
	}

	switch (config->val_bits) {
	case 8:
		map->format.format_val = regmap_format_8;
		map->format.parse_val = regmap_parse_8;
		break;
	case 16:
		map->format.format_val = regmap_format_16;
		map->format.parse_val = regmap_parse_16;
		break;
	}

	if (!map->format.format_write &&
	    !(map->format.format_reg && map->format.format_val))
		goto err_map;

	map->work_buf = kmalloc(map->format.buf_size, GFP_KERNEL);
	if (map->work_buf == NULL) {
		ret = -ENOMEM;
		goto err_map;
	}

	ret = regcache_init(map);
	if (ret < 0)
		goto err_map;

	regmap_debugfs_init(map);

	return map;

err_map:
	kfree(map);
err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(regmap_init);

/**
 * regmap_exit(): Free a previously allocated register map
 */
void regmap_exit(struct regmap *map)
{
	regcache_exit(map);
	regmap_debugfs_exit(map);
	kfree(map->work_buf);
	kfree(map);
}
EXPORT_SYMBOL_GPL(regmap_exit);

static int _regmap_raw_write(struct regmap *map, unsigned int reg,
			     const void *val, size_t val_len)
{
	u8 *u8 = map->work_buf;
	void *buf;
	int ret = -ENOTSUPP;
	size_t len;
	int i;

	/* Check for unwritable registers before we start */
	if (map->writeable_reg)
		for (i = 0; i < val_len / map->format.val_bytes; i++)
			if (!map->writeable_reg(map->dev, reg + i))
				return -EINVAL;

	map->format.format_reg(map->work_buf, reg);

	u8[0] |= map->write_flag_mask;

	trace_regmap_hw_write_start(map->dev, reg,
				    val_len / map->format.val_bytes);

	/* If we're doing a single register write we can probably just
	 * send the work_buf directly, otherwise try to do a gather
	 * write.
	 */
	if (val == map->work_buf + map->format.reg_bytes)
		ret = map->bus->write(map->dev, map->work_buf,
				      map->format.reg_bytes + val_len);
	else if (map->bus->gather_write)
		ret = map->bus->gather_write(map->dev, map->work_buf,
					     map->format.reg_bytes,
					     val, val_len);

	/* If that didn't work fall back on linearising by hand. */
	if (ret == -ENOTSUPP) {
		len = map->format.reg_bytes + val_len;
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		memcpy(buf, map->work_buf, map->format.reg_bytes);
		memcpy(buf + map->format.reg_bytes, val, val_len);
		ret = map->bus->write(map->dev, buf, len);

		kfree(buf);
	}

	trace_regmap_hw_write_done(map->dev, reg,
				   val_len / map->format.val_bytes);

	return ret;
}

int _regmap_write(struct regmap *map, unsigned int reg,
		  unsigned int val)
{
	int ret;
	BUG_ON(!map->format.format_write && !map->format.format_val);

	if (!map->cache_bypass) {
		ret = regcache_write(map, reg, val);
		if (ret != 0)
			return ret;
		if (map->cache_only)
			return 0;
	}

	trace_regmap_reg_write(map->dev, reg, val);

	if (map->format.format_write) {
		map->format.format_write(map, reg, val);

		trace_regmap_hw_write_start(map->dev, reg, 1);

		ret = map->bus->write(map->dev, map->work_buf,
				      map->format.buf_size);

		trace_regmap_hw_write_done(map->dev, reg, 1);

		return ret;
	} else {
		map->format.format_val(map->work_buf + map->format.reg_bytes,
				       val);
		return _regmap_raw_write(map, reg,
					 map->work_buf + map->format.reg_bytes,
					 map->format.val_bytes);
	}
}

/**
 * regmap_write(): Write a value to a single register
 *
 * @map: Register map to write to
 * @reg: Register to write to
 * @val: Value to be written
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val)
{
	int ret;

	mutex_lock(&map->lock);

	ret = _regmap_write(map, reg, val);

	mutex_unlock(&map->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_write);

/**
 * regmap_raw_write(): Write raw values to one or more registers
 *
 * @map: Register map to write to
 * @reg: Initial register to write to
 * @val: Block of data to be written, laid out for direct transmission to the
 *       device
 * @val_len: Length of data pointed to by val.
 *
 * This function is intended to be used for things like firmware
 * download where a large block of data needs to be transferred to the
 * device.  No formatting will be done on the data provided.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_raw_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len)
{
	int ret;

	WARN_ON(map->cache_type != REGCACHE_NONE);

	mutex_lock(&map->lock);

	ret = _regmap_raw_write(map, reg, val, val_len);

	mutex_unlock(&map->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_raw_write);

static int _regmap_raw_read(struct regmap *map, unsigned int reg, void *val,
			    unsigned int val_len)
{
	u8 *u8 = map->work_buf;
	int ret;

	map->format.format_reg(map->work_buf, reg);

	/*
	 * Some buses or devices flag reads by setting the high bits in the
	 * register addresss; since it's always the high bits for all
	 * current formats we can do this here rather than in
	 * formatting.  This may break if we get interesting formats.
	 */
	u8[0] |= map->read_flag_mask;

	trace_regmap_hw_read_start(map->dev, reg,
				   val_len / map->format.val_bytes);

	ret = map->bus->read(map->dev, map->work_buf, map->format.reg_bytes,
			     val, val_len);

	trace_regmap_hw_read_done(map->dev, reg,
				  val_len / map->format.val_bytes);

	return ret;
}

static int _regmap_read(struct regmap *map, unsigned int reg,
			unsigned int *val)
{
	int ret;

	if (!map->format.parse_val)
		return -EINVAL;

	if (!map->cache_bypass) {
		ret = regcache_read(map, reg, val);
		if (ret == 0)
			return 0;
	}

	if (map->cache_only)
		return -EBUSY;

	ret = _regmap_raw_read(map, reg, map->work_buf, map->format.val_bytes);
	if (ret == 0) {
		*val = map->format.parse_val(map->work_buf);
		trace_regmap_reg_read(map->dev, reg, *val);
	}

	return ret;
}

/**
 * regmap_read(): Read a value from a single register
 *
 * @map: Register map to write to
 * @reg: Register to be read from
 * @val: Pointer to store read value
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val)
{
	int ret;

	mutex_lock(&map->lock);

	ret = _regmap_read(map, reg, val);

	mutex_unlock(&map->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_read);

/**
 * regmap_raw_read(): Read raw data from the device
 *
 * @map: Register map to write to
 * @reg: First register to be read from
 * @val: Pointer to store read value
 * @val_len: Size of data to read
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_raw_read(struct regmap *map, unsigned int reg, void *val,
		    size_t val_len)
{
	int ret;
	int i;
	bool vol = true;

	for (i = 0; i < val_len / map->format.val_bytes; i++)
		if (!regmap_volatile(map, reg + i))
			vol = false;

	WARN_ON(!vol && map->cache_type != REGCACHE_NONE);

	mutex_lock(&map->lock);

	ret = _regmap_raw_read(map, reg, val, val_len);

	mutex_unlock(&map->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_raw_read);

/**
 * regmap_bulk_read(): Read multiple registers from the device
 *
 * @map: Register map to write to
 * @reg: First register to be read from
 * @val: Pointer to store read value, in native register size for device
 * @val_count: Number of registers to read
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int regmap_bulk_read(struct regmap *map, unsigned int reg, void *val,
		     size_t val_count)
{
	int ret, i;
	size_t val_bytes = map->format.val_bytes;
	bool vol = true;

	if (!map->format.parse_val)
		return -EINVAL;

	/* Is this a block of volatile registers? */
	for (i = 0; i < val_count; i++)
		if (!regmap_volatile(map, reg + i))
			vol = false;

	if (vol || map->cache_type == REGCACHE_NONE) {
		ret = regmap_raw_read(map, reg, val, val_bytes * val_count);
		if (ret != 0)
			return ret;

		for (i = 0; i < val_count * val_bytes; i += val_bytes)
			map->format.parse_val(val + i);
	} else {
		for (i = 0; i < val_count; i++) {
			ret = regmap_read(map, reg + i, val + (i * val_bytes));
			if (ret != 0)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regmap_bulk_read);

/**
 * remap_update_bits: Perform a read/modify/write cycle on the register map
 *
 * @map: Register map to update
 * @reg: Register to update
 * @mask: Bitmask to change
 * @val: New value for bitmask
 *
 * Returns zero for success, a negative number on error.
 */
int regmap_update_bits(struct regmap *map, unsigned int reg,
		       unsigned int mask, unsigned int val)
{
	int ret;
	unsigned int tmp;

	mutex_lock(&map->lock);

	ret = _regmap_read(map, reg, &tmp);
	if (ret != 0)
		goto out;

	tmp &= ~mask;
	tmp |= val & mask;

	ret = _regmap_write(map, reg, tmp);

out:
	mutex_unlock(&map->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(regmap_update_bits);

static int __init regmap_initcall(void)
{
	regmap_debugfs_initcall();

	return 0;
}
postcore_initcall(regmap_initcall);
