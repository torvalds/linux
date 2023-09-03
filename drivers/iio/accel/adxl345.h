/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADXL345 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 */

#ifndef _ADXL345_H_
#define _ADXL345_H_

enum adxl345_device_type {
	ADXL345	= 1,
	ADXL375 = 2,
};

struct adxl345_chip_info {
	const char *name;
	unsigned int type;
};

int adxl345_core_probe(struct device *dev, struct regmap *regmap);

#endif /* _ADXL345_H_ */
