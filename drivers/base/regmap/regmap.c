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

#include <linux/regmap.h>

struct regmap;

struct regmap_format {
	size_t buf_size;
	size_t reg_bytes;
	size_t val_bytes;
	void (*format_write)(struct regmap *map,
			     unsigned int reg, unsigned int val);
	void (*format_reg)(void *buf, unsigned int reg);
	void (*format_val)(void *buf, unsigned int val);
	unsigned int (*parse_val)(void *buf);
};

struct regmap {
	struct mutex lock;

	struct device *dev; /* Device we do I/O on */
	void *work_buf;     /* Scratch buffer used to format I/O */
	struct regmap_format format;  /* Buffer format */
	const struct regmap_bus *bus;
};

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
		goto err_bus;
	}

	return map;

err_bus:
	module_put(map->bus->owner);
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
	kfree(map->work_buf);
	module_put(map->bus->owner);
	kfree(map);
}
EXPORT_SYMBOL_GPL(regmap_exit);

static int _regmap_raw_write(struct regmap *map, unsigned int reg,
			     const void *val, size_t val_len)
{
	void *buf;
	int ret = -ENOTSUPP;
	size_t len;

	map->format.format_reg(map->work_buf, reg);

	/* Try to do a gather write if we can */
	if (map->bus->gather_write)
		ret = map->bus->gather_write(map->dev, map->work_buf,
					     map->format.reg_bytes,
					     val, val_len);

	/* Otherwise fall back on linearising by hand. */
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

	return ret;
}

static int _regmap_write(struct regmap *map, unsigned int reg,
			 unsigned int val)
{
	BUG_ON(!map->format.format_write && !map->format.format_val);

	if (map->format.format_write) {
		map->format.format_write(map, reg, val);

		return map->bus->write(map->dev, map->work_buf,
				       map->format.buf_size);
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
	 * Some buses flag reads by setting the high bits in the
	 * register addresss; since it's always the high bits for all
	 * current formats we can do this here rather than in
	 * formatting.  This may break if we get interesting formats.
	 */
	if (map->bus->read_flag_mask)
		u8[0] |= map->bus->read_flag_mask;

	ret = map->bus->read(map->dev, map->work_buf, map->format.reg_bytes,
			     val, val_len);
	if (ret != 0)
		return ret;

	return 0;
}

static int _regmap_read(struct regmap *map, unsigned int reg,
			unsigned int *val)
{
	int ret;

	if (!map->format.parse_val)
		return -EINVAL;

	ret = _regmap_raw_read(map, reg, map->work_buf, map->format.val_bytes);
	if (ret == 0)
		*val = map->format.parse_val(map->work_buf);

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

	if (!map->format.parse_val)
		return -EINVAL;

	ret = regmap_raw_read(map, reg, val, val_bytes * val_count);
	if (ret != 0)
		return ret;

	for (i = 0; i < val_count * val_bytes; i += val_bytes)
		map->format.parse_val(val + i);

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
