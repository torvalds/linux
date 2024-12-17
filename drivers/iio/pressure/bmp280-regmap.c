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
	}
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
EXPORT_SYMBOL_NS(bmp180_regmap_config, "IIO_BMP280");

static bool bme280_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP280_REG_CONFIG:
	case BME280_REG_CTRL_HUMIDITY:
	case BMP280_REG_CTRL_MEAS:
	case BMP280_REG_RESET:
		return true;
	default:
		return false;
	}
}

static bool bmp280_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP280_REG_CONFIG:
	case BMP280_REG_CTRL_MEAS:
	case BMP280_REG_RESET:
		return true;
	default:
		return false;
	}
}

static bool bmp280_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
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

static bool bme280_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BME280_REG_HUMIDITY_LSB:
	case BME280_REG_HUMIDITY_MSB:
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
static bool bmp380_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP380_REG_CMD:
	case BMP380_REG_CONFIG:
	case BMP380_REG_FIFO_CONFIG_1:
	case BMP380_REG_FIFO_CONFIG_2:
	case BMP380_REG_FIFO_WATERMARK_LSB:
	case BMP380_REG_FIFO_WATERMARK_MSB:
	case BMP380_REG_POWER_CONTROL:
	case BMP380_REG_INT_CONTROL:
	case BMP380_REG_IF_CONFIG:
	case BMP380_REG_ODR:
	case BMP380_REG_OSR:
		return true;
	default:
		return false;
	}
}

static bool bmp380_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP380_REG_TEMP_XLSB:
	case BMP380_REG_TEMP_LSB:
	case BMP380_REG_TEMP_MSB:
	case BMP380_REG_PRESS_XLSB:
	case BMP380_REG_PRESS_LSB:
	case BMP380_REG_PRESS_MSB:
	case BMP380_REG_SENSOR_TIME_XLSB:
	case BMP380_REG_SENSOR_TIME_LSB:
	case BMP380_REG_SENSOR_TIME_MSB:
	case BMP380_REG_INT_STATUS:
	case BMP380_REG_FIFO_DATA:
	case BMP380_REG_STATUS:
	case BMP380_REG_ERROR:
	case BMP380_REG_EVENT:
		return true;
	default:
		return false;
	}
}

static bool bmp580_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP580_REG_NVM_DATA_MSB:
	case BMP580_REG_NVM_DATA_LSB:
	case BMP580_REG_NVM_ADDR:
	case BMP580_REG_ODR_CONFIG:
	case BMP580_REG_OSR_CONFIG:
	case BMP580_REG_INT_SOURCE:
	case BMP580_REG_INT_CONFIG:
	case BMP580_REG_OOR_THR_MSB:
	case BMP580_REG_OOR_THR_LSB:
	case BMP580_REG_OOR_CONFIG:
	case BMP580_REG_OOR_RANGE:
	case BMP580_REG_IF_CONFIG:
	case BMP580_REG_FIFO_CONFIG:
	case BMP580_REG_FIFO_SEL:
	case BMP580_REG_DSP_CONFIG:
	case BMP580_REG_DSP_IIR:
	case BMP580_REG_CMD:
		return true;
	default:
		return false;
	}
}

static bool bmp580_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMP580_REG_NVM_DATA_MSB:
	case BMP580_REG_NVM_DATA_LSB:
	case BMP580_REG_FIFO_COUNT:
	case BMP580_REG_INT_STATUS:
	case BMP580_REG_PRESS_XLSB:
	case BMP580_REG_PRESS_LSB:
	case BMP580_REG_PRESS_MSB:
	case BMP580_REG_FIFO_DATA:
	case BMP580_REG_TEMP_XLSB:
	case BMP580_REG_TEMP_LSB:
	case BMP580_REG_TEMP_MSB:
	case BMP580_REG_EFF_OSR:
	case BMP580_REG_STATUS:
		return true;
	default:
		return false;
	}
}

const struct regmap_config bmp280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMP280_REG_TEMP_XLSB,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmp280_is_writeable_reg,
	.volatile_reg = bmp280_is_volatile_reg,
};
EXPORT_SYMBOL_NS(bmp280_regmap_config, "IIO_BMP280");

const struct regmap_config bme280_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BME280_REG_HUMIDITY_LSB,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bme280_is_writeable_reg,
	.volatile_reg = bme280_is_volatile_reg,
};
EXPORT_SYMBOL_NS(bme280_regmap_config, "IIO_BMP280");

const struct regmap_config bmp380_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMP380_REG_CMD,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmp380_is_writeable_reg,
	.volatile_reg = bmp380_is_volatile_reg,
};
EXPORT_SYMBOL_NS(bmp380_regmap_config, "IIO_BMP280");

const struct regmap_config bmp580_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = BMP580_REG_CMD,
	.cache_type = REGCACHE_RBTREE,

	.writeable_reg = bmp580_is_writeable_reg,
	.volatile_reg = bmp580_is_volatile_reg,
};
EXPORT_SYMBOL_NS(bmp580_regmap_config, "IIO_BMP280");
