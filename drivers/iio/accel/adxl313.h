/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ADXL313 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Lucas Stankus <lucas.p.stankus@gmail.com>
 */

#ifndef _ADXL313_H_
#define _ADXL313_H_

#include <linux/iio/iio.h>

/* ADXL313 register definitions */
#define ADXL313_REG_DEVID0		0x00
#define ADXL313_REG_DEVID1		0x01
#define ADXL313_REG_PARTID		0x02
#define ADXL313_REG_XID			0x04
#define ADXL313_REG_SOFT_RESET		0x18
#define ADXL313_REG_OFS_AXIS(index)	(0x1E + (index))
#define ADXL313_REG_THRESH_ACT		0x24
#define ADXL313_REG_THRESH_INACT	0x25
#define ADXL313_REG_TIME_INACT		0x26
#define ADXL313_REG_ACT_INACT_CTL	0x27
#define ADXL313_REG_BW_RATE		0x2C
#define ADXL313_REG_POWER_CTL		0x2D
#define ADXL313_REG_INT_ENABLE		0x2E
#define ADXL313_REG_INT_MAP		0x2F
#define ADXL313_REG_INT_SOURCE		0x30
#define ADXL313_REG_DATA_FORMAT		0x31
#define ADXL313_REG_DATA_AXIS(index)	(0x32 + ((index) * 2))
#define ADXL313_REG_FIFO_CTL		0x38
#define ADXL313_REG_FIFO_STATUS		0x39

#define ADXL313_DEVID0			0xAD
#define ADXL313_DEVID0_ADXL312_314	0xE5
#define ADXL313_DEVID1			0x1D
#define ADXL313_PARTID			0xCB
#define ADXL313_SOFT_RESET		0x52

#define ADXL313_RATE_MSK		GENMASK(3, 0)
#define ADXL313_RATE_BASE		6

#define ADXL313_POWER_CTL_MSK		BIT(3)
#define ADXL313_POWER_CTL_INACT_MSK	GENMASK(5, 4)
#define ADXL313_POWER_CTL_LINK		BIT(5)
#define ADXL313_POWER_CTL_AUTO_SLEEP	BIT(4)

#define ADXL313_RANGE_MSK		GENMASK(1, 0)
#define ADXL313_RANGE_MAX		3

#define ADXL313_FULL_RES		BIT(3)
#define ADXL313_SPI_3WIRE		BIT(6)
#define ADXL313_I2C_DISABLE		BIT(6)

#define ADXL313_INT_OVERRUN		BIT(0)
#define ADXL313_INT_WATERMARK		BIT(1)
#define ADXL313_INT_INACTIVITY		BIT(3)
#define ADXL313_INT_ACTIVITY		BIT(4)
#define ADXL313_INT_DREADY		BIT(7)

/* FIFO entries: how many values are stored in the FIFO */
#define ADXL313_REG_FIFO_STATUS_ENTRIES_MSK	GENMASK(5, 0)
/* FIFO samples: number of samples needed for watermark (FIFO mode) */
#define ADXL313_REG_FIFO_CTL_SAMPLES_MSK	GENMASK(4, 0)
#define ADXL313_REG_FIFO_CTL_MODE_MSK		GENMASK(7, 6)

#define ADXL313_FIFO_BYPASS			0
#define ADXL313_FIFO_STREAM			2

#define ADXL313_FIFO_SIZE			32

#define ADXL313_NUM_AXIS			3

extern const struct regmap_access_table adxl312_readable_regs_table;
extern const struct regmap_access_table adxl313_readable_regs_table;
extern const struct regmap_access_table adxl314_readable_regs_table;

extern const struct regmap_access_table adxl312_writable_regs_table;
extern const struct regmap_access_table adxl313_writable_regs_table;
extern const struct regmap_access_table adxl314_writable_regs_table;

bool adxl313_is_volatile_reg(struct device *dev, unsigned int reg);

enum adxl313_device_type {
	ADXL312,
	ADXL313,
	ADXL314,
};

struct adxl313_data {
	struct regmap	*regmap;
	const struct adxl313_chip_info *chip_info;
	struct mutex	lock; /* lock to protect transf_buf */
	u8 watermark;
	__le16		transf_buf __aligned(IIO_DMA_MINALIGN);
	__le16		fifo_buf[ADXL313_NUM_AXIS * ADXL313_FIFO_SIZE + 1];
};

struct adxl313_chip_info {
	const char			*name;
	enum adxl313_device_type	type;
	int				scale_factor;
	bool				variable_range;
	bool				soft_reset;
	int (*check_id)(struct device *dev, struct adxl313_data *data);
};

extern const struct adxl313_chip_info adxl31x_chip_info[];

int adxl313_core_probe(struct device *dev,
		       struct regmap *regmap,
		       const struct adxl313_chip_info *chip_info,
		       int (*setup)(struct device *, struct regmap *));
#endif /* _ADXL313_H_ */
