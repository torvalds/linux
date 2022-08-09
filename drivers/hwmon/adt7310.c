// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADT7310/ADT7310 digital temperature sensor driver
 *
 * Copyright 2012-2013 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <asm/unaligned.h>

#include "adt7x10.h"

#define ADT7310_STATUS			0
#define ADT7310_CONFIG			1
#define ADT7310_TEMPERATURE		2
#define ADT7310_ID			3
#define ADT7310_T_CRIT			4
#define ADT7310_T_HYST			5
#define ADT7310_T_ALARM_HIGH		6
#define ADT7310_T_ALARM_LOW		7

static const u8 adt7310_reg_table[] = {
	[ADT7X10_TEMPERATURE]   = ADT7310_TEMPERATURE,
	[ADT7X10_STATUS]	= ADT7310_STATUS,
	[ADT7X10_CONFIG]	= ADT7310_CONFIG,
	[ADT7X10_T_ALARM_HIGH]	= ADT7310_T_ALARM_HIGH,
	[ADT7X10_T_ALARM_LOW]	= ADT7310_T_ALARM_LOW,
	[ADT7X10_T_CRIT]	= ADT7310_T_CRIT,
	[ADT7X10_T_HYST]	= ADT7310_T_HYST,
	[ADT7X10_ID]		= ADT7310_ID,
};

#define ADT7310_CMD_REG_OFFSET	3
#define ADT7310_CMD_READ	0x40

#define AD7310_COMMAND(reg) (adt7310_reg_table[(reg)] << ADT7310_CMD_REG_OFFSET)

static int adt7310_spi_read_word(struct spi_device *spi, u8 reg)
{
	return spi_w8r16be(spi, AD7310_COMMAND(reg) | ADT7310_CMD_READ);
}

static int adt7310_spi_write_word(struct spi_device *spi, u8 reg, u16 data)
{
	u8 buf[3];

	buf[0] = AD7310_COMMAND(reg);
	put_unaligned_be16(data, &buf[1]);

	return spi_write(spi, buf, sizeof(buf));
}

static int adt7310_spi_read_byte(struct spi_device *spi, u8 reg)
{
	return spi_w8r8(spi, AD7310_COMMAND(reg) | ADT7310_CMD_READ);
}

static int adt7310_spi_write_byte(struct spi_device *spi, u8 reg, u8 data)
{
	u8 buf[2];

	buf[0] = AD7310_COMMAND(reg);
	buf[1] = data;

	return spi_write(spi, buf, sizeof(buf));
}

static bool adt7310_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_STATUS:
		return true;
	default:
		return false;
	}
}

static int adt7310_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct spi_device *spi = context;
	int regval;

	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_T_ALARM_HIGH:
	case ADT7X10_T_ALARM_LOW:
	case ADT7X10_T_CRIT:
		regval = adt7310_spi_read_word(spi, reg);
		break;
	default:
		regval = adt7310_spi_read_byte(spi, reg);
		break;
	}
	if (regval < 0)
		return regval;
	*val = regval;
	return 0;
}

static int adt7310_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct spi_device *spi = context;
	int ret;

	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_T_ALARM_HIGH:
	case ADT7X10_T_ALARM_LOW:
	case ADT7X10_T_CRIT:
		ret = adt7310_spi_write_word(spi, reg, val);
		break;
	default:
		ret = adt7310_spi_write_byte(spi, reg, val);
		break;
	}
	return ret;
}

static const struct regmap_config adt7310_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = adt7310_regmap_is_volatile,
	.reg_read = adt7310_reg_read,
	.reg_write = adt7310_reg_write,
};

static int adt7310_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init(&spi->dev, NULL, spi, &adt7310_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adt7x10_probe(&spi->dev, spi_get_device_id(spi)->name, spi->irq,
			     regmap);
}

static const struct spi_device_id adt7310_id[] = {
	{ "adt7310", 0 },
	{ "adt7320", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, adt7310_id);

static struct spi_driver adt7310_driver = {
	.driver = {
		.name	= "adt7310",
		.pm	= ADT7X10_DEV_PM_OPS,
	},
	.probe		= adt7310_spi_probe,
	.id_table	= adt7310_id,
};
module_spi_driver(adt7310_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADT7310/ADT7320 driver");
MODULE_LICENSE("GPL");
