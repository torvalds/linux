// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include "internal.h"

static int regmap_sdw_write(void *context, const void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	/* First word of buffer contains the destination address */
	u32 addr = le32_to_cpu(*(const __le32 *)val_buf);
	const u8 *val = val_buf;

	return sdw_nwrite_no_pm(slave, addr, val_size - sizeof(addr), val + sizeof(addr));
}

static int regmap_sdw_gather_write(void *context,
				   const void *reg_buf, size_t reg_size,
				   const void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	u32 addr = le32_to_cpu(*(const __le32 *)reg_buf);

	return sdw_nwrite_no_pm(slave, addr, val_size, val_buf);
}

static int regmap_sdw_read(void *context,
			   const void *reg_buf, size_t reg_size,
			   void *val_buf, size_t val_size)
{
	struct device *dev = context;
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	u32 addr = le32_to_cpu(*(const __le32 *)reg_buf);

	return sdw_nread_no_pm(slave, addr, val_size, val_buf);
}

static const struct regmap_bus regmap_sdw = {
	.write = regmap_sdw_write,
	.gather_write = regmap_sdw_gather_write,
	.read = regmap_sdw_read,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_LITTLE,
};

static int regmap_sdw_config_check(const struct regmap_config *config)
{
	/* Register addresses are 32 bits wide */
	if (config->reg_bits != 32)
		return -ENOTSUPP;

	if (config->pad_bits != 0)
		return -ENOTSUPP;

	/* Only bulk writes are supported not multi-register writes */
	if (config->can_multi_write)
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

MODULE_DESCRIPTION("regmap SoundWire Module");
MODULE_LICENSE("GPL v2");
