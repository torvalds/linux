// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - I2C support
//
// Copyright 2011 Wolfson Microelectronics plc
//
// Author: Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include "internal.h"

static int regmap_smbus_byte_reg_read(void *context, unsigned int reg,
				      unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	if (reg > 0xff)
		return -EINVAL;

	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int regmap_smbus_byte_reg_write(void *context, unsigned int reg,
				       unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	if (val > 0xff || reg > 0xff)
		return -EINVAL;

	return i2c_smbus_write_byte_data(i2c, reg, val);
}

static const struct regmap_bus regmap_smbus_byte = {
	.reg_write = regmap_smbus_byte_reg_write,
	.reg_read = regmap_smbus_byte_reg_read,
};

static int regmap_smbus_word_reg_read(void *context, unsigned int reg,
				      unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	if (reg > 0xff)
		return -EINVAL;

	ret = i2c_smbus_read_word_data(i2c, reg);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int regmap_smbus_word_reg_write(void *context, unsigned int reg,
				       unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	if (val > 0xffff || reg > 0xff)
		return -EINVAL;

	return i2c_smbus_write_word_data(i2c, reg, val);
}

static const struct regmap_bus regmap_smbus_word = {
	.reg_write = regmap_smbus_word_reg_write,
	.reg_read = regmap_smbus_word_reg_read,
};

static int regmap_smbus_word_read_swapped(void *context, unsigned int reg,
					  unsigned int *val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	if (reg > 0xff)
		return -EINVAL;

	ret = i2c_smbus_read_word_swapped(i2c, reg);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int regmap_smbus_word_write_swapped(void *context, unsigned int reg,
					   unsigned int val)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	if (val > 0xffff || reg > 0xff)
		return -EINVAL;

	return i2c_smbus_write_word_swapped(i2c, reg, val);
}

static const struct regmap_bus regmap_smbus_word_swapped = {
	.reg_write = regmap_smbus_word_write_swapped,
	.reg_read = regmap_smbus_word_read_swapped,
};

static int regmap_i2c_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	ret = i2c_master_send(i2c, data, count);
	if (ret == count)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int regmap_i2c_gather_write(void *context,
				   const void *reg, size_t reg_size,
				   const void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct i2c_msg xfer[2];
	int ret;

	/* If the I2C controller can't do a gather tell the core, it
	 * will substitute in a linear write for us.
	 */
	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_NOSTART))
		return -ENOTSUPP;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = reg_size;
	xfer[0].buf = (void *)reg;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_NOSTART;
	xfer[1].len = val_size;
	xfer[1].buf = (void *)val;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int regmap_i2c_read(void *context,
			   const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	struct i2c_msg xfer[2];
	int ret;

	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = reg_size;
	xfer[0].buf = (void *)reg;

	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = val_size;
	xfer[1].buf = val;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static const struct regmap_bus regmap_i2c = {
	.write = regmap_i2c_write,
	.gather_write = regmap_i2c_gather_write,
	.read = regmap_i2c_read,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int regmap_i2c_smbus_i2c_write(void *context, const void *data,
				      size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	if (count < 1)
		return -EINVAL;

	--count;
	return i2c_smbus_write_i2c_block_data(i2c, ((u8 *)data)[0], count,
					      ((u8 *)data + 1));
}

static int regmap_i2c_smbus_i2c_read(void *context, const void *reg,
				     size_t reg_size, void *val,
				     size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret;

	if (reg_size != 1 || val_size < 1)
		return -EINVAL;

	ret = i2c_smbus_read_i2c_block_data(i2c, ((u8 *)reg)[0], val_size, val);
	if (ret == val_size)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static const struct regmap_bus regmap_i2c_smbus_i2c_block = {
	.write = regmap_i2c_smbus_i2c_write,
	.read = regmap_i2c_smbus_i2c_read,
	.max_raw_read = I2C_SMBUS_BLOCK_MAX - 1,
	.max_raw_write = I2C_SMBUS_BLOCK_MAX - 1,
};

static int regmap_i2c_smbus_i2c_write_reg16(void *context, const void *data,
				      size_t count)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);

	if (count < 2)
		return -EINVAL;

	count--;
	return i2c_smbus_write_i2c_block_data(i2c, ((u8 *)data)[0], count,
					      (u8 *)data + 1);
}

static int regmap_i2c_smbus_i2c_read_reg16(void *context, const void *reg,
				     size_t reg_size, void *val,
				     size_t val_size)
{
	struct device *dev = context;
	struct i2c_client *i2c = to_i2c_client(dev);
	int ret, count, len = val_size;

	if (reg_size != 2)
		return -EINVAL;

	ret = i2c_smbus_write_byte_data(i2c, ((u16 *)reg)[0] & 0xff,
					((u16 *)reg)[0] >> 8);
	if (ret < 0)
		return ret;

	count = 0;
	do {
		/* Current Address Read */
		ret = i2c_smbus_read_byte(i2c);
		if (ret < 0)
			break;

		*((u8 *)val++) = ret;
		count++;
		len--;
	} while (len > 0);

	if (count == val_size)
		return 0;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static const struct regmap_bus regmap_i2c_smbus_i2c_block_reg16 = {
	.write = regmap_i2c_smbus_i2c_write_reg16,
	.read = regmap_i2c_smbus_i2c_read_reg16,
	.max_raw_read = I2C_SMBUS_BLOCK_MAX - 2,
	.max_raw_write = I2C_SMBUS_BLOCK_MAX - 2,
};

static const struct regmap_bus *regmap_get_i2c_bus(struct i2c_client *i2c,
					const struct regmap_config *config)
{
	const struct i2c_adapter_quirks *quirks;
	const struct regmap_bus *bus = NULL;
	struct regmap_bus *ret_bus;
	u16 max_read = 0, max_write = 0;

	if (i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C))
		bus = &regmap_i2c;
	else if (config->val_bits == 8 && config->reg_bits == 8 &&
		 i2c_check_functionality(i2c->adapter,
					 I2C_FUNC_SMBUS_I2C_BLOCK))
		bus = &regmap_i2c_smbus_i2c_block;
	else if (config->val_bits == 8 && config->reg_bits == 16 &&
		i2c_check_functionality(i2c->adapter,
					I2C_FUNC_SMBUS_I2C_BLOCK))
		bus = &regmap_i2c_smbus_i2c_block_reg16;
	else if (config->val_bits == 16 && config->reg_bits == 8 &&
		 i2c_check_functionality(i2c->adapter,
					 I2C_FUNC_SMBUS_WORD_DATA))
		switch (regmap_get_val_endian(&i2c->dev, NULL, config)) {
		case REGMAP_ENDIAN_LITTLE:
			bus = &regmap_smbus_word;
			break;
		case REGMAP_ENDIAN_BIG:
			bus = &regmap_smbus_word_swapped;
			break;
		default:		/* everything else is not supported */
			break;
		}
	else if (config->val_bits == 8 && config->reg_bits == 8 &&
		 i2c_check_functionality(i2c->adapter,
					 I2C_FUNC_SMBUS_BYTE_DATA))
		bus = &regmap_smbus_byte;

	if (!bus)
		return ERR_PTR(-ENOTSUPP);

	quirks = i2c->adapter->quirks;
	if (quirks) {
		if (quirks->max_read_len &&
		    (bus->max_raw_read == 0 || bus->max_raw_read > quirks->max_read_len))
			max_read = quirks->max_read_len;

		if (quirks->max_write_len &&
		    (bus->max_raw_write == 0 || bus->max_raw_write > quirks->max_write_len))
			max_write = quirks->max_write_len;

		if (max_read || max_write) {
			ret_bus = kmemdup(bus, sizeof(*bus), GFP_KERNEL);
			if (!ret_bus)
				return ERR_PTR(-ENOMEM);
			ret_bus->free_on_exit = true;
			ret_bus->max_raw_read = max_read;
			ret_bus->max_raw_write = max_write;
			bus = ret_bus;
		}
	}

	return bus;
}

struct regmap *__regmap_init_i2c(struct i2c_client *i2c,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_i2c_bus(i2c, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __regmap_init(&i2c->dev, bus, &i2c->dev, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_i2c);

struct regmap *__devm_regmap_init_i2c(struct i2c_client *i2c,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name)
{
	const struct regmap_bus *bus = regmap_get_i2c_bus(i2c, config);

	if (IS_ERR(bus))
		return ERR_CAST(bus);

	return __devm_regmap_init(&i2c->dev, bus, &i2c->dev, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_i2c);

MODULE_LICENSE("GPL");
