/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ADXL380 3-Axis Digital Accelerometer
 *
 * Copyright 2024 Analog Devices Inc.
 */

#ifndef _ADXL380_H_
#define _ADXL380_H_

enum adxl380_odr {
	ADXL380_ODR_VLP,
	ADXL380_ODR_DSM,
	ADXL380_ODR_DSM_2X,
	ADXL380_ODR_DSM_4X,
	ADXL380_ODR_MAX
};

struct adxl380_chip_info {
	const char *name;
	const int scale_tbl[3][2];
	const int samp_freq_tbl[ADXL380_ODR_MAX];
	const struct iio_info *info;
	const int temp_offset;
	const u16 chip_id;
	const bool has_low_power;
};

extern const struct adxl380_chip_info adxl318_chip_info;
extern const struct adxl380_chip_info adxl319_chip_info;
extern const struct adxl380_chip_info adxl380_chip_info;
extern const struct adxl380_chip_info adxl382_chip_info;

int adxl380_probe(struct device *dev, struct regmap *regmap,
		  const struct adxl380_chip_info *chip_info);
bool adxl380_readable_noinc_reg(struct device *dev, unsigned int reg);

#endif /* _ADXL380_H_ */
