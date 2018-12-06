/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * This file is part of AD5686 DAC driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#ifndef __DRIVERS_IIO_DAC_AD5686_H__
#define __DRIVERS_IIO_DAC_AD5686_H__

#include <linux/types.h>
#include <linux/cache.h>
#include <linux/mutex.h>
#include <linux/kernel.h>

#define AD5310_CMD(x)				((x) << 12)

#define AD5683_DATA(x)				((x) << 4)

#define AD5686_ADDR(x)				((x) << 16)
#define AD5686_CMD(x)				((x) << 20)

#define AD5686_ADDR_DAC(chan)			(0x1 << (chan))
#define AD5686_ADDR_ALL_DAC			0xF

#define AD5686_CMD_NOOP				0x0
#define AD5686_CMD_WRITE_INPUT_N		0x1
#define AD5686_CMD_UPDATE_DAC_N			0x2
#define AD5686_CMD_WRITE_INPUT_N_UPDATE_N	0x3
#define AD5686_CMD_POWERDOWN_DAC		0x4
#define AD5686_CMD_LDAC_MASK			0x5
#define AD5686_CMD_RESET			0x6
#define AD5686_CMD_INTERNAL_REFER_SETUP		0x7
#define AD5686_CMD_DAISY_CHAIN_ENABLE		0x8
#define AD5686_CMD_READBACK_ENABLE		0x9

#define AD5686_LDAC_PWRDN_NONE			0x0
#define AD5686_LDAC_PWRDN_1K			0x1
#define AD5686_LDAC_PWRDN_100K			0x2
#define AD5686_LDAC_PWRDN_3STATE		0x3

#define AD5686_CMD_CONTROL_REG			0x4
#define AD5686_CMD_READBACK_ENABLE_V2		0x5

#define AD5310_REF_BIT_MSK			BIT(8)
#define AD5683_REF_BIT_MSK			BIT(12)
#define AD5693_REF_BIT_MSK			BIT(12)

/**
 * ad5686_supported_device_ids:
 */
enum ad5686_supported_device_ids {
	ID_AD5310R,
	ID_AD5311R,
	ID_AD5671R,
	ID_AD5672R,
	ID_AD5675R,
	ID_AD5676,
	ID_AD5676R,
	ID_AD5681R,
	ID_AD5682R,
	ID_AD5683,
	ID_AD5683R,
	ID_AD5684,
	ID_AD5684R,
	ID_AD5685R,
	ID_AD5686,
	ID_AD5686R,
	ID_AD5691R,
	ID_AD5692R,
	ID_AD5693,
	ID_AD5693R,
	ID_AD5694,
	ID_AD5694R,
	ID_AD5695R,
	ID_AD5696,
	ID_AD5696R,
};

enum ad5686_regmap_type {
	AD5310_REGMAP,
	AD5683_REGMAP,
	AD5686_REGMAP,
	AD5693_REGMAP
};

struct ad5686_state;

typedef int (*ad5686_write_func)(struct ad5686_state *st,
				 u8 cmd, u8 addr, u16 val);

typedef int (*ad5686_read_func)(struct ad5686_state *st, u8 addr);

/**
 * struct ad5686_chip_info - chip specific information
 * @int_vref_mv:	AD5620/40/60: the internal reference voltage
 * @num_channels:	number of channels
 * @channel:		channel specification
 * @regmap_type:	register map layout variant
 */

struct ad5686_chip_info {
	u16				int_vref_mv;
	unsigned int			num_channels;
	struct iio_chan_spec		*channels;
	enum ad5686_regmap_type		regmap_type;
};

/**
 * struct ad5446_state - driver instance specific data
 * @spi:		spi_device
 * @chip_info:		chip model specific constants, available modes etc
 * @reg:		supply regulator
 * @vref_mv:		actual reference voltage used
 * @pwr_down_mask:	power down mask
 * @pwr_down_mode:	current power down mode
 * @use_internal_vref:	set to true if the internal reference voltage is used
 * @data:		spi transfer buffers
 */

struct ad5686_state {
	struct device			*dev;
	const struct ad5686_chip_info	*chip_info;
	struct regulator		*reg;
	unsigned short			vref_mv;
	unsigned int			pwr_down_mask;
	unsigned int			pwr_down_mode;
	ad5686_write_func		write;
	ad5686_read_func		read;
	bool				use_internal_vref;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */

	union {
		__be32 d32;
		__be16 d16;
		u8 d8[4];
	} data[3] ____cacheline_aligned;
};


int ad5686_probe(struct device *dev,
		 enum ad5686_supported_device_ids chip_type,
		 const char *name, ad5686_write_func write,
		 ad5686_read_func read);

int ad5686_remove(struct device *dev);


#endif /* __DRIVERS_IIO_DAC_AD5686_H__ */
