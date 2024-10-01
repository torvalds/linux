/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADXL345 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 */

#ifndef _ADXL345_H_
#define _ADXL345_H_

#define ADXL345_REG_DEVID		0x00
#define ADXL345_REG_OFSX		0x1E
#define ADXL345_REG_OFSY		0x1F
#define ADXL345_REG_OFSZ		0x20
#define ADXL345_REG_OFS_AXIS(index)	(ADXL345_REG_OFSX + (index))
#define ADXL345_REG_BW_RATE		0x2C
#define ADXL345_REG_POWER_CTL		0x2D
#define ADXL345_REG_DATA_FORMAT		0x31
#define ADXL345_REG_DATAX0		0x32
#define ADXL345_REG_DATAY0		0x34
#define ADXL345_REG_DATAZ0		0x36
#define ADXL345_REG_DATA_AXIS(index)	\
	(ADXL345_REG_DATAX0 + (index) * sizeof(__le16))

#define ADXL345_BW_RATE			GENMASK(3, 0)
#define ADXL345_BASE_RATE_NANO_HZ	97656250LL

#define ADXL345_POWER_CTL_MEASURE	BIT(3)
#define ADXL345_POWER_CTL_STANDBY	0x00

#define ADXL345_DATA_FORMAT_RANGE	GENMASK(1, 0)	/* Set the g range */
#define ADXL345_DATA_FORMAT_JUSTIFY	BIT(2)	/* Left-justified (MSB) mode */
#define ADXL345_DATA_FORMAT_FULL_RES	BIT(3)	/* Up to 13-bits resolution */
#define ADXL345_DATA_FORMAT_SPI_3WIRE	BIT(6)	/* 3-wire SPI mode */
#define ADXL345_DATA_FORMAT_SELF_TEST	BIT(7)	/* Enable a self test */

#define ADXL345_DATA_FORMAT_2G		0
#define ADXL345_DATA_FORMAT_4G		1
#define ADXL345_DATA_FORMAT_8G		2
#define ADXL345_DATA_FORMAT_16G		3

#define ADXL345_DEVID			0xE5

/*
 * In full-resolution mode, scale factor is maintained at ~4 mg/LSB
 * in all g ranges.
 *
 * At +/- 16g with 13-bit resolution, scale is computed as:
 * (16 + 16) * 9.81 / (2^13 - 1) = 0.0383
 */
#define ADXL345_USCALE	38300

/*
 * The Datasheet lists a resolution of Resolution is ~49 mg per LSB. That's
 * ~480mm/s**2 per LSB.
 */
#define ADXL375_USCALE	480000

struct adxl345_chip_info {
	const char *name;
	int uscale;
};

int adxl345_core_probe(struct device *dev, struct regmap *regmap,
		       int (*setup)(struct device*, struct regmap*));

#endif /* _ADXL345_H_ */
