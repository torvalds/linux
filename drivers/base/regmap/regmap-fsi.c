// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - FSI support
//
// Copyright 2022 IBM Corp
//
// Author: Eddie James <eajames@linux.ibm.com>

#include <linux/fsi.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "internal.h"

static int regmap_fsi32_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	u32 v;
	int ret;

	ret = fsi_slave_read(context, reg, &v, sizeof(v));
	if (ret)
		return ret;

	*val = v;
	return 0;
}

static int regmap_fsi32_reg_write(void *context, unsigned int reg, unsigned int val)
{
	u32 v = val;

	return fsi_slave_write(context, reg, &v, sizeof(v));
}

static const struct regmap_bus regmap_fsi32 = {
	.reg_write = regmap_fsi32_reg_write,
	.reg_read = regmap_fsi32_reg_read,
};

static int regmap_fsi32le_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	__be32 v;
	int ret;

	ret = fsi_slave_read(context, reg, &v, sizeof(v));
	if (ret)
		return ret;

	*val = be32_to_cpu(v);
	return 0;
}

static int regmap_fsi32le_reg_write(void *context, unsigned int reg, unsigned int val)
{
	__be32 v = cpu_to_be32(val);

	return fsi_slave_write(context, reg, &v, sizeof(v));
}

static const struct regmap_bus regmap_fsi32le = {
	.reg_write = regmap_fsi32le_reg_write,
	.reg_read = regmap_fsi32le_reg_read,
};

static int regmap_fsi16_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	u16 v;
	int ret;

	ret = fsi_slave_read(context, reg, &v, sizeof(v));
	if (ret)
		return ret;

	*val = v;
	return 0;
}

static int regmap_fsi16_reg_write(void *context, unsigned int reg, unsigned int val)
{
	u16 v;

	if (val > 0xffff)
		return -EINVAL;

	v = val;
	return fsi_slave_write(context, reg, &v, sizeof(v));
}

static const struct regmap_bus regmap_fsi16 = {
	.reg_write = regmap_fsi16_reg_write,
	.reg_read = regmap_fsi16_reg_read,
};

static int regmap_fsi16le_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	__be16 v;
	int ret;

	ret = fsi_slave_read(context, reg, &v, sizeof(v));
	if (ret)
		return ret;

	*val = be16_to_cpu(v);
	return 0;
}

static int regmap_fsi16le_reg_write(void *context, unsigned int reg, unsigned int val)
{
	__be16 v;

	if (val > 0xffff)
		return -EINVAL;

	v = cpu_to_be16(val);
	return fsi_slave_write(context, reg, &v, sizeof(v));
}

static const struct regmap_bus regmap_fsi16le = {
	.reg_write = regmap_fsi16le_reg_write,
	.reg_read = regmap_fsi16le_reg_read,
};

static int regmap_fsi8_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	u8 v;
	int ret;

	ret = fsi_slave_read(context, reg, &v, sizeof(v));
	if (ret)
		return ret;

	*val = v;
	return 0;
}

static int regmap_fsi8_reg_write(void *context, unsigned int reg, unsigned int val)
{
	u8 v;

	if (val > 0xff)
		return -EINVAL;

	v = val;
	return fsi_slave_write(context, reg, &v, sizeof(v));
}

static const struct regmap_bus regmap_fsi8 = {
	.reg_write = regmap_fsi8_reg_write,
	.reg_read = regmap_fsi8_reg_read,
};

static const struct regmap_bus *regmap_get_fsi_bus(struct fsi_device *fsi_dev,
						   const struct regmap_config *config)
{
	const struct regmap_bus *bus = NULL;

	if (config->reg_bits == 8 || config->reg_bits == 16 || config->reg_bits == 32) {
		switch (config->val_bits) {
		case 8:
			bus = &regmap_fsi8;
			break;
		case 16:
			switch (regmap_get_val_endian(&fsi_dev->dev, NULL, config)) {
			case REGMAP_ENDIAN_LITTLE:
#ifdef __LITTLE_ENDIAN
			case REGMAP_ENDIAN_NATIVE:
#endif
				bus = &regmap_fsi16le;
				break;
			case REGMAP_ENDIAN_DEFAULT:
			case REGMAP_ENDIAN_BIG:
#ifdef __BIG_ENDIAN
			case REGMAP_ENDIAN_NATIVE:
#endif
				bus = &regmap_fsi16;
				break;
			default:
				break;
			}
			break;
		case 32:
			switch (regmap_get_val_endian(&fsi_dev->dev, NULL, config)) {
			case REGMAP_ENDIAN_LITTLE:
#ifdef __LITTLE_ENDIAN
			case REGMAP_ENDIAN_NATIVE:
#endif
				bus = &regmap_fsi32le;
				break;
			case REGMAP_ENDIAN_DEFAULT:
			case REGMAP_ENDIAN_BIG:
#ifdef __BIG_ENDIAN
			case REGMAP_ENDIAN_NATIVE:
#endif
				bus = &regmap_fsi32;
				break;
			default:
				break;
			}
			break;
		}
	}

	return bus ?: ERR_PTR(-EOPNOTSUPP);
}

struct regmap *__regmap_init_fsi(struct fsi_device *fsi_dev, const struct regmap_config *config,
				 struct lock_class_key *lock_key, const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_fsi_bus(fsi_dev, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __regmap_init(&fsi_dev->dev, bus, fsi_dev->slave, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_fsi);

struct regmap *__devm_regmap_init_fsi(struct fsi_device *fsi_dev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key, const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_fsi_bus(fsi_dev, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __devm_regmap_init(&fsi_dev->dev, bus, fsi_dev->slave, config, lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_fsi);

MODULE_LICENSE("GPL");
