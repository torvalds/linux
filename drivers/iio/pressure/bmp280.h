/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/regmap.h>

/* BMP280 specific registers */
#define BMP280_REG_HUMIDITY_LSB		0xFE
#define BMP280_REG_HUMIDITY_MSB		0xFD
#define BMP280_REG_TEMP_XLSB		0xFC
#define BMP280_REG_TEMP_LSB		0xFB
#define BMP280_REG_TEMP_MSB		0xFA
#define BMP280_REG_PRESS_XLSB		0xF9
#define BMP280_REG_PRESS_LSB		0xF8
#define BMP280_REG_PRESS_MSB		0xF7

/* Helper mask to truncate excess 12 bits on pressure and temp readings */
#define BMP280_MEAS_TRIM_MASK		GENMASK(31, 12)

#define BMP280_REG_CONFIG		0xF5
#define BMP280_REG_CTRL_MEAS		0xF4
#define BMP280_REG_STATUS		0xF3
#define BMP280_REG_CTRL_HUMIDITY	0xF2

/* Due to non linear mapping, and data sizes we can't do a bulk read */
#define BMP280_REG_COMP_H1		0xA1
#define BMP280_REG_COMP_H2		0xE1
#define BMP280_REG_COMP_H3		0xE3
#define BMP280_REG_COMP_H4		0xE4
#define BMP280_REG_COMP_H5		0xE5
#define BMP280_REG_COMP_H6		0xE7

#define BMP280_REG_COMP_TEMP_START	0x88
#define BMP280_COMP_TEMP_REG_COUNT	6

#define BMP280_REG_COMP_PRESS_START	0x8E
#define BMP280_COMP_PRESS_REG_COUNT	18

#define BMP280_COMP_H5_MASK		GENMASK(15, 4)

#define BMP280_FILTER_MASK		GENMASK(4, 2)
#define BMP280_FILTER_OFF		0
#define BMP280_FILTER_2X		1
#define BMP280_FILTER_4X		2
#define BMP280_FILTER_8X		3
#define BMP280_FILTER_16X		4

#define BMP280_OSRS_HUMIDITY_MASK	GENMASK(2, 0)
#define BMP280_OSRS_HUMIDITY_SKIP	0
#define BMP280_OSRS_HUMIDITY_1X		1
#define BMP280_OSRS_HUMIDITY_2X		2
#define BMP280_OSRS_HUMIDITY_4X		3
#define BMP280_OSRS_HUMIDITY_8X		4
#define BMP280_OSRS_HUMIDITY_16X	5

#define BMP280_OSRS_TEMP_MASK		GENMASK(7, 5)
#define BMP280_OSRS_TEMP_SKIP		0
#define BMP280_OSRS_TEMP_1X		1
#define BMP280_OSRS_TEMP_2X		2
#define BMP280_OSRS_TEMP_4X		3
#define BMP280_OSRS_TEMP_8X		4
#define BMP280_OSRS_TEMP_16X		5

#define BMP280_OSRS_PRESS_MASK		GENMASK(4, 2)
#define BMP280_OSRS_PRESS_SKIP		0
#define BMP280_OSRS_PRESS_1X		1
#define BMP280_OSRS_PRESS_2X		2
#define BMP280_OSRS_PRESS_4X		3
#define BMP280_OSRS_PRESS_8X		4
#define BMP280_OSRS_PRESS_16X		5

#define BMP280_MODE_MASK		GENMASK(1, 0)
#define BMP280_MODE_SLEEP		0
#define BMP280_MODE_FORCED		1
#define BMP280_MODE_NORMAL		3

/* BMP180 specific registers */
#define BMP180_REG_OUT_XLSB		0xF8
#define BMP180_REG_OUT_LSB		0xF7
#define BMP180_REG_OUT_MSB		0xF6

#define BMP180_REG_CALIB_START		0xAA
#define BMP180_REG_CALIB_COUNT		22

#define BMP180_MEAS_CTRL_MASK		GENMASK(4, 0)
#define BMP180_MEAS_TEMP		0x0E
#define BMP180_MEAS_PRESS		0x14
#define BMP180_MEAS_SCO			BIT(5)
#define BMP180_OSRS_PRESS_MASK		GENMASK(7, 6)
#define BMP180_MEAS_PRESS_1X		0
#define BMP180_MEAS_PRESS_2X		1
#define BMP180_MEAS_PRESS_4X		2
#define BMP180_MEAS_PRESS_8X		3

/* BMP180 and BMP280 common registers */
#define BMP280_REG_CTRL_MEAS		0xF4
#define BMP280_REG_RESET		0xE0
#define BMP280_REG_ID			0xD0

#define BMP180_CHIP_ID			0x55
#define BMP280_CHIP_ID			0x58
#define BME280_CHIP_ID			0x60
#define BMP280_SOFT_RESET_VAL		0xB6

/* BMP280 register skipped special values */
#define BMP280_TEMP_SKIPPED		0x80000
#define BMP280_PRESS_SKIPPED		0x80000
#define BMP280_HUMIDITY_SKIPPED		0x8000

/* Regmap configurations */
extern const struct regmap_config bmp180_regmap_config;
extern const struct regmap_config bmp280_regmap_config;

/* Probe called from different transports */
int bmp280_common_probe(struct device *dev,
			struct regmap *regmap,
			unsigned int chip,
			const char *name,
			int irq);

/* PM ops */
extern const struct dev_pm_ops bmp280_dev_pm_ops;
