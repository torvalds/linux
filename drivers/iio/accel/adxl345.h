/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADXL345 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 */

#ifndef _ADXL345_H_
#define _ADXL345_H_

#define ADXL345_REG_DEVID		0x00
#define ADXL345_REG_THRESH_TAP		0x1D
#define ADXL345_REG_OFSX		0x1E
#define ADXL345_REG_OFSY		0x1F
#define ADXL345_REG_OFSZ		0x20
#define ADXL345_REG_OFS_AXIS(index)	(ADXL345_REG_OFSX + (index))

/* Tap duration */
#define ADXL345_REG_DUR		0x21
/* Tap latency */
#define ADXL345_REG_LATENT		0x22
/* Tap window */
#define ADXL345_REG_WINDOW		0x23
/* Activity threshold */
#define ADXL345_REG_THRESH_ACT		0x24
/* Inactivity threshold */
#define ADXL345_REG_THRESH_INACT	0x25
/* Inactivity time */
#define ADXL345_REG_TIME_INACT		0x26
/* Axis enable control for activity and inactivity detection */
#define ADXL345_REG_ACT_INACT_CTRL	0x27
/* Free-fall threshold */
#define ADXL345_REG_THRESH_FF		0x28
/* Free-fall time */
#define ADXL345_REG_TIME_FF		0x29
/* Axis control for single tap or double tap */
#define ADXL345_REG_TAP_AXIS		0x2A
/* Source of single tap or double tap */
#define ADXL345_REG_ACT_TAP_STATUS	0x2B
/* Data rate and power mode control */
#define ADXL345_REG_BW_RATE		0x2C
#define ADXL345_REG_POWER_CTL		0x2D
#define ADXL345_REG_INT_ENABLE		0x2E
#define ADXL345_REG_INT_MAP		0x2F
#define ADXL345_REG_INT_SOURCE		0x30
#define ADXL345_REG_INT_SOURCE_MSK	0xFF
#define ADXL345_REG_DATA_FORMAT		0x31
#define ADXL345_REG_XYZ_BASE		0x32
#define ADXL345_REG_DATA_AXIS(index)				\
	(ADXL345_REG_XYZ_BASE + (index) * sizeof(__le16))

#define ADXL345_REG_FIFO_CTL		0x38
#define ADXL345_FIFO_CTL_SAMPLES_MSK	GENMASK(4, 0)
/* 0: INT1, 1: INT2 */
#define ADXL345_FIFO_CTL_TRIGGER_MSK	BIT(5)
#define ADXL345_FIFO_CTL_MODE_MSK	GENMASK(7, 6)
#define ADXL345_REG_FIFO_STATUS	0x39
#define ADXL345_REG_FIFO_STATUS_MSK	0x3F

#define ADXL345_INT_OVERRUN		BIT(0)
#define ADXL345_INT_WATERMARK		BIT(1)
#define ADXL345_INT_FREE_FALL		BIT(2)
#define ADXL345_INT_INACTIVITY		BIT(3)
#define ADXL345_INT_ACTIVITY		BIT(4)
#define ADXL345_INT_DOUBLE_TAP		BIT(5)
#define ADXL345_INT_SINGLE_TAP		BIT(6)
#define ADXL345_INT_DATA_READY		BIT(7)

/*
 * BW_RATE bits - Bandwidth and output data rate. The default value is
 * 0x0A, which translates to a 100 Hz output data rate
 */
#define ADXL345_BW_RATE			GENMASK(3, 0)
#define ADXL345_BW_LOW_POWER		BIT(4)
#define ADXL345_BASE_RATE_NANO_HZ	97656250LL

#define ADXL345_POWER_CTL_STANDBY	0x00
#define ADXL345_POWER_CTL_WAKEUP	GENMASK(1, 0)
#define ADXL345_POWER_CTL_SLEEP	BIT(2)
#define ADXL345_POWER_CTL_MEASURE	BIT(3)
#define ADXL345_POWER_CTL_AUTO_SLEEP	BIT(4)
#define ADXL345_POWER_CTL_LINK		BIT(5)

/* Set the g range */
#define ADXL345_DATA_FORMAT_RANGE	GENMASK(1, 0)
/* Data is left justified */
#define ADXL345_DATA_FORMAT_JUSTIFY	BIT(2)
/* Up to 13-bits resolution */
#define ADXL345_DATA_FORMAT_FULL_RES	BIT(3)
#define ADXL345_DATA_FORMAT_SPI_3WIRE	BIT(6)
#define ADXL345_DATA_FORMAT_SELF_TEST	BIT(7)
#define ADXL345_DATA_FORMAT_2G		0
#define ADXL345_DATA_FORMAT_4G		1
#define ADXL345_DATA_FORMAT_8G		2
#define ADXL345_DATA_FORMAT_16G		3

#define ADXL345_DEVID			0xE5
#define ADXL345_FIFO_SIZE		32

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
		       bool fifo_delay_default,
		       int (*setup)(struct device*, struct regmap*));

#endif /* _ADXL345_H_ */
