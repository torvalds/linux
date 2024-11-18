/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BME680_H_
#define BME680_H_

#define BME680_REG_CHIP_ID			0xD0
#define   BME680_CHIP_ID_VAL			0x61
#define BME680_REG_SOFT_RESET			0xE0
#define   BME680_CMD_SOFTRESET			0xB6
#define BME680_REG_STATUS			0x73
#define   BME680_SPI_MEM_PAGE_BIT		BIT(4)
#define     BME680_SPI_MEM_PAGE_1_VAL		1

#define BME680_REG_TEMP_MSB			0x22
#define BME680_REG_PRESS_MSB			0x1F
#define BME680_REG_HUMIDITY_MSB			0x25
#define BME680_REG_GAS_MSB			0x2A
#define BME680_REG_GAS_R_LSB			0x2B
#define   BME680_GAS_STAB_BIT			BIT(4)
#define   BME680_GAS_RANGE_MASK			GENMASK(3, 0)

#define BME680_REG_CTRL_HUMIDITY		0x72
#define   BME680_OSRS_HUMIDITY_MASK		GENMASK(2, 0)

#define BME680_REG_CTRL_MEAS			0x74
#define   BME680_OSRS_TEMP_MASK			GENMASK(7, 5)
#define   BME680_OSRS_PRESS_MASK		GENMASK(4, 2)
#define   BME680_MODE_MASK			GENMASK(1, 0)
#define     BME680_MODE_FORCED			1
#define     BME680_MODE_SLEEP			0

#define BME680_REG_CONFIG			0x75
#define   BME680_FILTER_MASK			GENMASK(4, 2)
#define     BME680_FILTER_COEFF_VAL		BIT(1)

/* TEMP/PRESS/HUMID reading skipped */
#define BME680_MEAS_SKIPPED			0x8000

#define BME680_MAX_OVERFLOW_VAL			0x40000000
#define BME680_HUM_REG_SHIFT_VAL		4
#define BME680_BIT_H1_DATA_MASK			GENMASK(3, 0)

#define   BME680_RHRANGE_MASK			GENMASK(5, 4)
#define BME680_REG_RES_HEAT_VAL			0x00
#define   BME680_RSERROR_MASK			GENMASK(7, 4)
#define BME680_REG_RES_HEAT_0			0x5A
#define BME680_REG_GAS_WAIT_0			0x64
#define BME680_ADC_GAS_RES			GENMASK(15, 6)
#define BME680_AMB_TEMP				25

#define BME680_REG_CTRL_GAS_1			0x71
#define   BME680_RUN_GAS_MASK			BIT(4)
#define   BME680_NB_CONV_MASK			GENMASK(3, 0)

#define BME680_REG_MEAS_STAT_0			0x1D
#define   BME680_NEW_DATA_BIT			BIT(7)
#define   BME680_GAS_MEAS_BIT			BIT(6)
#define   BME680_MEAS_BIT			BIT(5)

#define BME680_TEMP_NUM_BYTES			3
#define BME680_PRESS_NUM_BYTES			3
#define BME680_HUMID_NUM_BYTES			2
#define BME680_GAS_NUM_BYTES			2

#define BME680_MEAS_TRIM_MASK			GENMASK(24, 4)

#define BME680_STARTUP_TIME_US			5000

/* Calibration Parameters */
#define BME680_T2_LSB_REG	0x8A
#define BME680_H2_MSB_REG	0xE1
#define BME680_GH3_REG		0xEE

#define BME680_CALIB_RANGE_1_LEN               23
#define BME680_CALIB_RANGE_2_LEN               14
#define BME680_CALIB_RANGE_3_LEN               5

extern const struct regmap_config bme680_regmap_config;

int bme680_core_probe(struct device *dev, struct regmap *regmap,
		      const char *name);

#endif  /* BME680_H_ */
