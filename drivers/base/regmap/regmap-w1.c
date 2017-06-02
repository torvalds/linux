/*
 * Register map access API - W1 (1-Wire) support
 *
 * Copyright (C) 2017 OAO Radioavionica
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#include <linux/regmap.h>
#include <linux/module.h>
#include "../../w1/w1.h"

#include "internal.h"

#define W1_CMD_READ_DATA	0x69
#define W1_CMD_WRITE_DATA	0x6C

/*
 * 1-Wire slaves registers with addess 8 bit and data 8 bit
 */

static int w1_reg_a8_v8_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 255)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_READ_DATA);
		w1_write_8(sl->master, reg);
		*val = w1_read_8(sl->master);
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static int w1_reg_a8_v8_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 255)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_WRITE_DATA);
		w1_write_8(sl->master, reg);
		w1_write_8(sl->master, val);
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

/*
 * 1-Wire slaves registers with addess 8 bit and data 16 bit
 */

static int w1_reg_a8_v16_read(void *context, unsigned int reg,
				unsigned int *val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 255)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_READ_DATA);
		w1_write_8(sl->master, reg);
		*val = w1_read_8(sl->master);
		*val |= w1_read_8(sl->master)<<8;
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static int w1_reg_a8_v16_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 255)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_WRITE_DATA);
		w1_write_8(sl->master, reg);
		w1_write_8(sl->master, val & 0x00FF);
		w1_write_8(sl->master, val>>8 & 0x00FF);
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

/*
 * 1-Wire slaves registers with addess 16 bit and data 16 bit
 */

static int w1_reg_a16_v16_read(void *context, unsigned int reg,
				unsigned int *val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 65535)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_READ_DATA);
		w1_write_8(sl->master, reg & 0x00FF);
		w1_write_8(sl->master, reg>>8 & 0x00FF);
		*val = w1_read_8(sl->master);
		*val |= w1_read_8(sl->master)<<8;
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static int w1_reg_a16_v16_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct device *dev = context;
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);
	int ret = 0;

	if (reg > 65535)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);
	if (!w1_reset_select_slave(sl)) {
		w1_write_8(sl->master, W1_CMD_WRITE_DATA);
		w1_write_8(sl->master, reg & 0x00FF);
		w1_write_8(sl->master, reg>>8 & 0x00FF);
		w1_write_8(sl->master, val & 0x00FF);
		w1_write_8(sl->master, val>>8 & 0x00FF);
	} else {
		ret = -ENODEV;
	}
	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

/*
 * Various types of supported bus addressing
 */

static struct regmap_bus regmap_w1_bus_a8_v8 = {
	.reg_read = w1_reg_a8_v8_read,
	.reg_write = w1_reg_a8_v8_write,
};

static struct regmap_bus regmap_w1_bus_a8_v16 = {
	.reg_read = w1_reg_a8_v16_read,
	.reg_write = w1_reg_a8_v16_write,
};

static struct regmap_bus regmap_w1_bus_a16_v16 = {
	.reg_read = w1_reg_a16_v16_read,
	.reg_write = w1_reg_a16_v16_write,
};

static const struct regmap_bus *regmap_get_w1_bus(struct device *w1_dev,
					const struct regmap_config *config)
{
	if (config->reg_bits == 8 && config->val_bits == 8)
		return &regmap_w1_bus_a8_v8;

	if (config->reg_bits == 8 && config->val_bits == 16)
		return &regmap_w1_bus_a8_v16;

	if (config->reg_bits == 16 && config->val_bits == 16)
		return &regmap_w1_bus_a16_v16;

	return ERR_PTR(-ENOTSUPP);
}

struct regmap *__regmap_init_w1(struct device *w1_dev,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{

	const struct regmap_bus *bus = regmap_get_w1_bus(w1_dev, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __regmap_init(w1_dev, bus, w1_dev, config,
			 lock_key, lock_name);

	return NULL;
}
EXPORT_SYMBOL_GPL(__regmap_init_w1);

struct regmap *__devm_regmap_init_w1(struct device *w1_dev,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{

	const struct regmap_bus *bus = regmap_get_w1_bus(w1_dev, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __devm_regmap_init(w1_dev, bus, w1_dev, config,
				 lock_key, lock_name);

	return NULL;
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_w1);

MODULE_LICENSE("GPL");
