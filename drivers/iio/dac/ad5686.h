/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of AD5686 DAC driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#ifndef __DRIVERS_IIO_DAC_AD5686_H__
#define __DRIVERS_IIO_DAC_AD5686_H__

#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#define AD5310_CMD_MSK				GENMASK(15, 12)
#define AD5310_DATA_MSK				GENMASK(11, 0)

#define AD5683_DATA_MSK				GENMASK(19, 4)

#define AD5686_CMD_MSK				GENMASK(23, 20)
#define AD5686_ADDR_MSK				GENMASK(19, 16)
#define AD5686_DATA_MSK				GENMASK(15, 0)

#define AD5686_ADDR_DAC(chan)			(0x1 << (chan))
#define AD5686_ADDR_ALL_DAC			0xF
#define AD5686_MAX_CHANNELS			16

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

#define AD5686_CMD_CONTROL_REG			0x4
#define AD5686_CMD_READBACK_ENABLE_V2		0x5

#define AD5310_DATA_GAIN_MSK			BIT(7)
#define AD5310_DATA_REF_MSK			BIT(8)
#define AD5310_DATA_PD_MSK			GENMASK(10, 9)

#define AD5683_DATA_GAIN_MSK			BIT(11) /* DB15 */
#define AD5683_DATA_REF_MSK			BIT(12) /* DB16 */
#define AD5683_DATA_PD_MSK			GENMASK(14, 13) /* DB18:DB17 */

#define AD5686_DATA_REF_MSK			BIT(0)

#define AD5686_PD_MSK				GENMASK(1, 0)
#define AD5686_PD_MODE_1K_TO_GND		0x1
#define AD5686_PD_MODE_100K_TO_GND		0x2
#define AD5686_PD_MODE_THREE_STATE		0x3

#define AD5686_PD_MSK_PWR_UP			0x0
#define AD5686_PD_MSK_PWR_DOWN			0x3

enum ad5686_regmap_type {
	AD5310_REGMAP,
	AD5683_REGMAP,
	AD5686_REGMAP,
};

struct gpio_desc;

struct ad5686_state;

/**
 * struct ad5686_bus_ops - bus specific read/write operations
 * @read: read a register value at the given address
 * @write: write a command, address and value to the device
 * @sync: ensure the completion of the write operation (optional)
 */
struct ad5686_bus_ops {
	int (*read)(struct ad5686_state *st, u8 addr);
	int (*write)(struct ad5686_state *st, u8 cmd, u8 addr, u16 val);
	int (*sync)(struct ad5686_state *st);
};

/**
 * struct ad5686_chip_info - chip specific information
 * @int_vref_mv:	the internal reference voltage
 * @num_channels:	number of channels
 * @channel:		channel specification
 * @regmap_type:	register map layout variant
 */
struct ad5686_chip_info {
	u16				int_vref_mv;
	unsigned int			num_channels;
	const struct iio_chan_spec	*channels;
	enum ad5686_regmap_type		regmap_type;
};

/* single-channel instances */
extern const struct ad5686_chip_info ad5310r_chip_info;
extern const struct ad5686_chip_info ad5311r_chip_info;
extern const struct ad5686_chip_info ad5681r_chip_info;
extern const struct ad5686_chip_info ad5682r_chip_info;
extern const struct ad5686_chip_info ad5683_chip_info;
extern const struct ad5686_chip_info ad5683r_chip_info;

/* dual-channel instances */
extern const struct ad5686_chip_info ad5337r_chip_info;
extern const struct ad5686_chip_info ad5338r_chip_info;

/* quad-channel instances */
extern const struct ad5686_chip_info ad5684_chip_info;
extern const struct ad5686_chip_info ad5684r_chip_info;
extern const struct ad5686_chip_info ad5685r_chip_info;
extern const struct ad5686_chip_info ad5686_chip_info;
extern const struct ad5686_chip_info ad5686r_chip_info;

/* 8-channel instances */
extern const struct ad5686_chip_info ad5672r_chip_info;
extern const struct ad5686_chip_info ad5676_chip_info;
extern const struct ad5686_chip_info ad5676r_chip_info;

/* 16-channel instances */
extern const struct ad5686_chip_info ad5674r_chip_info;
extern const struct ad5686_chip_info ad5679r_chip_info;

/**
 * struct ad5686_state - driver instance specific data
 * @dev:		device instance
 * @chip_info:		chip model specific constants, available modes etc
 * @ops:		bus specific operations
 * @ldac_gpio:		LDAC pin GPIO descriptor
 * @gain_gpio:		GAIN pin GPIO descriptor
 * @pwr_down_mask:	power down mask
 * @pwr_down_mode:	current power down mode
 * @scale_avail:	pre-calculated available scale values
 * @vref_mv:		actual reference voltage used
 * @double_scale:	flag to indicate the gain multiplier is applied
 * @use_internal_vref:	set to true if the internal reference voltage is used
 * @lock:		lock to protect access to state fields, which includes
 *			the data buffer during regmap ops
 * @bus_data:		bus specific data
 * @data:		transfer buffers
 */
struct ad5686_state {
	struct device			*dev;
	const struct ad5686_chip_info	*chip_info;
	const struct ad5686_bus_ops	*ops;
	struct gpio_desc		*ldac_gpio;
	struct gpio_desc		*gain_gpio;
	unsigned int			pwr_down_mask;
	unsigned int			pwr_down_mode;
	int				scale_avail[4];
	unsigned short			vref_mv;
	bool				double_scale;
	bool				use_internal_vref;
	struct mutex			lock;
	void				*bus_data;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */

	union {
		__be32 d32;
		__be16 d16;
		u8 d8[4];
	} data[AD5686_MAX_CHANNELS] __aligned(IIO_DMA_MINALIGN);
};


int ad5686_probe(struct device *dev,
		 const struct ad5686_chip_info *chip_info,
		 const char *name, const struct ad5686_bus_ops *ops,
		 void *bus_data);

static inline int ad5686_write(struct ad5686_state *st, u8 cmd, u8 addr, u16 val)
{
	int ret;

	ret = st->ops->write(st, cmd, addr, val);
	if (ret)
		return ret;

	return st->ops->sync ? st->ops->sync(st) : 0;
}

static inline int ad5686_read(struct ad5686_state *st, u8 addr)
{
	return st->ops->read(st, addr);
}

#endif /* __DRIVERS_IIO_DAC_AD5686_H__ */
