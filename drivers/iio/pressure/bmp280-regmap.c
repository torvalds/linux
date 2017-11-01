// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bmp280.h"

static bool bmp180_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP280_REG_CTRL_MEAS:
	case BMP280_REG_RESET:
		return true;
	default:
		return false;
	};
}

static bool bmp180_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP180_REG_OUT_XLSB:
	case BMP180_REG_OUT_LSB:
	case BMP180_REG_OUT_MSB:
	case BMP280_REG_CTRL_MEAS:
		return true;
	default:
		return false;
	}
}

const struct regmap_config bmp180_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMP180_REG_OUT_XLSB,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmp180_is_writeable_reg,
	.volatile_reg = bmp180_is_volatile_reg,
};
EXPORT_SYMBOL(bmp180_regmap_config);

static bool bmp280_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP280_REG_CONFIG:
	case BMP280_REG_CTRL_HUMIDITY:
	case BMP280_REG_CTRL_MEAS:
	case BMP280_REG_RESET:
		return true;
	default:
		return false;
	};
}

static bool bmp280_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP280_REG_HUMIDITY_LSB:
	case BMP280_REG_HUMIDITY_MSB:
	case BMP280_REG_TEMP_XLSB:
	case BMP280_REG_TEMP_LSB:
	case BMP280_REG_TEMP_MSB:
	case BMP280_REG_PRESS_XLSB:
	case BMP280_REG_PRESS_LSB:
	case BMP280_REG_PRESS_MSB:
	case BMP280_REG_STATUS:
		return true;
	default:
		return false;
	}
}

const struct regmap_config bmp280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMP280_REG_HUMIDITY_LSB,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmp280_is_writeable_reg,
	.volatile_reg = bmp280_is_volatile_reg,
};
EXPORT_SYMBOL(bmp280_regmap_config);
