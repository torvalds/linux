/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ADXL380 3-Axis Digital Accelerometer
 *
 * Copyright 2024 Analog Devices Inc.
 */

#ifndef _ADXL380_H_
#define _ADXL380_H_

struct adxl380_chip_info {
	const char *name;
	const int scale_tbl[3][2];
	const int samp_freq_tbl[3];
	const int temp_offset;
	const u16 chip_id;
};

extern const struct adxl380_chip_info adxl380_chip_info;
extern const struct adxl380_chip_info adxl382_chip_info;

int adxl380_probe(struct device *dev, struct regmap *regmap,
		  const struct adxl380_chip_info *chip_info);
bool adxl380_readable_noinc_reg(struct device *dev, unsigned int reg);

#endif /* _ADXL380_H_ */
