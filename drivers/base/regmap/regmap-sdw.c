// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/device.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include "internal.h"

static int regmap_sdw_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	return sdw_write(slave, reg, val);
}

static int regmap_sdw_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int read;

	read = sdw_read(slave, reg);
	if (read < 0)
		return read;

	*val = read;
	return 0;
}

static struct regmap_bus regmap_sdw = {
	.reg_read = regmap_sdw_read,
	.reg_write = regmap_sdw_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int regmap_sdw_config_check(const struct regmap_config *config)
{
	/* All register are 8-bits wide as per MIPI Soundwire 1.0 Spec */
	if (config->val_bits != 8)
		return -ENOTSUPP;

	/* Registers are 32 bits wide */
	if (config->reg_bits != 32)
		return -ENOTSUPP;

	if (config->pad_bits != 0)
		return -ENOTSUPP;

	return 0;
}

struct regmap *__regmap_init_sdw(struct sdw_slave *sdw,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	int ret;

	ret = regmap_sdw_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __regmap_init(&sdw->dev, &regmap_sdw,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_sdw);

struct regmap *__devm_regmap_init_sdw(struct sdw_slave *sdw,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	int ret;

	ret = regmap_sdw_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __devm_regmap_init(&sdw->dev, &regmap_sdw,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_sdw);

MODULE_DESCRIPTION("Regmap SoundWire Module");
MODULE_LICENSE("GPL v2");
