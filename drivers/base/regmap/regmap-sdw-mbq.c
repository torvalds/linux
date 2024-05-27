// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2020 Intel Corporation.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include "internal.h"

static int regmap_sdw_mbq_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret;

	ret = sdw_write_no_pm(slave, SDW_SDCA_MBQ_CTL(reg), (val >> 8) & 0xff);
	if (ret < 0)
		return ret;

	return sdw_write_no_pm(slave, reg, val & 0xff);
}

static int regmap_sdw_mbq_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int read0;
	int read1;

	read0 = sdw_read_no_pm(slave, reg);
	if (read0 < 0)
		return read0;

	read1 = sdw_read_no_pm(slave, SDW_SDCA_MBQ_CTL(reg));
	if (read1 < 0)
		return read1;

	*val = (read1 << 8) | read0;

	return 0;
}

static const struct regmap_bus regmap_sdw_mbq = {
	.reg_read = regmap_sdw_mbq_read,
	.reg_write = regmap_sdw_mbq_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int regmap_sdw_mbq_config_check(const struct regmap_config *config)
{
	/* MBQ-based controls are only 16-bits for now */
	if (config->val_bits != 16)
		return -ENOTSUPP;

	/* Registers are 32 bits wide */
	if (config->reg_bits != 32)
		return -ENOTSUPP;

	if (config->pad_bits != 0)
		return -ENOTSUPP;

	return 0;
}

struct regmap *__regmap_init_sdw_mbq(struct sdw_slave *sdw,
				     const struct regmap_config *config,
				     struct lock_class_key *lock_key,
				     const char *lock_name)
{
	int ret;

	ret = regmap_sdw_mbq_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __regmap_init(&sdw->dev, &regmap_sdw_mbq,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_sdw_mbq);

struct regmap *__devm_regmap_init_sdw_mbq(struct sdw_slave *sdw,
					  const struct regmap_config *config,
					  struct lock_class_key *lock_key,
					  const char *lock_name)
{
	int ret;

	ret = regmap_sdw_mbq_config_check(config);
	if (ret)
		return ERR_PTR(ret);

	return __devm_regmap_init(&sdw->dev, &regmap_sdw_mbq,
			&sdw->dev, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_sdw_mbq);

MODULE_DESCRIPTION("regmap SoundWire MBQ Module");
MODULE_LICENSE("GPL");
