/*
 * sca3000_core.c -- support VTI sca3000 series accelerometers via SPI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Copyright (c) 2009 Jonathan Cameron <jic23@kernel.org>
 *
 * See industrialio/accels/sca3000.h for comments.
 */

#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#define SCA3000_WRITE_REG(a) (((a) << 2) | 0x02)
#define SCA3000_READ_REG(a) ((a) << 2)

#define SCA3000_REG_REVID_ADDR				0x00
#define   SCA3000_REG_REVID_MAJOR_MASK			GENMASK(8, 4)
#define   SCA3000_REG_REVID_MINOR_MASK			GENMASK(3, 0)

#define SCA3000_REG_STATUS_ADDR				0x02
#define   SCA3000_LOCKED				BIT(5)
#define   SCA3000_EEPROM_CS_ERROR			BIT(1)
#define   SCA3000_SPI_FRAME_ERROR			BIT(0)

/* All reads done using register decrement so no need to directly access LSBs */
#define SCA3000_REG_X_MSB_ADDR				0x05
#define SCA3000_REG_Y_MSB_ADDR				0x07
#define SCA3000_REG_Z_MSB_ADDR				0x09

#define SCA3000_REG_RING_OUT_ADDR			0x0f

/* Temp read untested - the e05 doesn't have the sensor */
#define SCA3000_REG_TEMP_MSB_ADDR			0x13

#define SCA3000_REG_MODE_ADDR				0x14
#define SCA3000_MODE_PROT_MASK				0x28
#define   SCA3000_REG_MODE_RING_BUF_ENABLE		BIT(7)
#define   SCA3000_REG_MODE_RING_BUF_8BIT		BIT(6)

/*
 * Free fall detection triggers an interrupt if the acceleration
 * is below a threshold for equivalent of 25cm drop
 */
#define   SCA3000_REG_MODE_FREE_FALL_DETECT		BIT(4)
#define   SCA3000_REG_MODE_MEAS_MODE_NORMAL		0x00
#define   SCA3000_REG_MODE_MEAS_MODE_OP_1		0x01
#define   SCA3000_REG_MODE_MEAS_MODE_OP_2		0x02

/*
 * In motion detection mode the accelerations are band pass filtered
 * (approx 1 - 25Hz) and then a programmable threshold used to trigger
 * and interrupt.
 */
#define   SCA3000_REG_MODE_MEAS_MODE_MOT_DET		0x03
#define   SCA3000_REG_MODE_MODE_MASK			0x03

#define SCA3000_REG_BUF_COUNT_ADDR			0x15

#define SCA3000_REG_INT_STATUS_ADDR			0x16
#define   SCA3000_REG_INT_STATUS_THREE_QUARTERS		BIT(7)
#define   SCA3000_REG_INT_STATUS_HALF			BIT(6)

#define SCA3000_INT_STATUS_FREE_FALL			BIT(3)
#define SCA3000_INT_STATUS_Y_TRIGGER			BIT(2)
#define SCA3000_INT_STATUS_X_TRIGGER			BIT(1)
#define SCA3000_INT_STATUS_Z_TRIGGER			BIT(0)

/* Used to allow access to multiplexed registers */
#define SCA3000_REG_CTRL_SEL_ADDR			0x18
/* Only available for SCA3000-D03 and SCA3000-D01 */
#define   SCA3000_REG_CTRL_SEL_I2C_DISABLE		0x01
#define   SCA3000_REG_CTRL_SEL_MD_CTRL			0x02
#define   SCA3000_REG_CTRL_SEL_MD_Y_TH			0x03
#define   SCA3000_REG_CTRL_SEL_MD_X_TH			0x04
#define   SCA3000_REG_CTRL_SEL_MD_Z_TH			0x05
/*
 * BE VERY CAREFUL WITH THIS, IF 3 BITS ARE NOT SET the device
 * will not function
 */
#define   SCA3000_REG_CTRL_SEL_OUT_CTRL			0x0B

#define     SCA3000_REG_OUT_CTRL_PROT_MASK		0xE0
#define     SCA3000_REG_OUT_CTRL_BUF_X_EN		0x10
#define     SCA3000_REG_OUT_CTRL_BUF_Y_EN		0x08
#define     SCA3000_REG_OUT_CTRL_BUF_Z_EN		0x04
#define     SCA3000_REG_OUT_CTRL_BUF_DIV_MASK		0x03
#define     SCA3000_REG_OUT_CTRL_BUF_DIV_4		0x02
#define     SCA3000_REG_OUT_CTRL_BUF_DIV_2		0x01


/*
 * Control which motion detector interrupts are on.
 * For now only OR combinations are supported.
 */
#define SCA3000_MD_CTRL_PROT_MASK			0xC0
#define SCA3000_MD_CTRL_OR_Y				BIT(0)
#define SCA3000_MD_CTRL_OR_X				BIT(1)
#define SCA3000_MD_CTRL_OR_Z				BIT(2)
/* Currently unsupported */
#define SCA3000_MD_CTRL_AND_Y				BIT(3)
#define SCA3000_MD_CTRL_AND_X				BIT(4)
#define SAC3000_MD_CTRL_AND_Z				BIT(5)

/*
 * Some control registers of complex access methods requiring this register to
 * be used to remove a lock.
 */
#define SCA3000_REG_UNLOCK_ADDR				0x1e

#define SCA3000_REG_INT_MASK_ADDR			0x21
#define   SCA3000_REG_INT_MASK_PROT_MASK		0x1C

#define   SCA3000_REG_INT_MASK_RING_THREE_QUARTER	BIT(7)
#define   SCA3000_REG_INT_MASK_RING_HALF		BIT(6)

#define SCA3000_REG_INT_MASK_ALL_INTS			0x02
#define SCA3000_REG_INT_MASK_ACTIVE_HIGH		0x01
#define SCA3000_REG_INT_MASK_ACTIVE_LOW			0x00
/* Values of multiplexed registers (write to ctrl_data after select) */
#define SCA3000_REG_CTRL_DATA_ADDR			0x22

/*
 * Measurement modes available on some sca3000 series chips. Code assumes others
 * may become available in the future.
 *
 * Bypass - Bypass the low-pass filter in the signal channel so as to increase
 *          signal bandwidth.
 *
 * Narrow - Narrow low-pass filtering of the signal channel and half output
 *          data rate by decimation.
 *
 * Wide - Widen low-pass filtering of signal channel to increase bandwidth
 */
#define SCA3000_OP_MODE_BYPASS				0x01
#define SCA3000_OP_MODE_NARROW				0x02
#define SCA3000_OP_MODE_WIDE				0x04
#define SCA3000_MAX_TX 6
#define SCA3000_MAX_RX 2

/**
 * struct sca3000_state - device instance state information
 * @us:			the associated spi device
 * @info:			chip variant information
 * @last_timestamp:		the timestamp of the last event
 * @mo_det_use_count:		reference counter for the motion detection unit
 * @lock:			lock used to protect elements of sca3000_state
 *				and the underlying device state.
 * @tx:			dma-able transmit buffer
 * @rx:			dma-able receive buffer
 **/
struct sca3000_state {
	struct spi_device		*us;
	const struct sca3000_chip_info	*info;
	s64				last_timestamp;
	int				mo_det_use_count;
	struct mutex			lock;
	/* Can these share a cacheline ? */
	u8				rx[384] ____cacheline_aligned;
	u8				tx[6] ____cacheline_aligned;
};

/**
 * struct sca3000_chip_info - model dependent parameters
 * @scale:			scale * 10^-6
 * @temp_output:		some devices have temperature sensors.
 * @measurement_mode_freq:	normal mode sampling frequency
 * @measurement_mode_3db_freq:	3db cutoff frequency of the low pass filter for
 * the normal measurement mode.
 * @option_mode_1:		first optional mode. Not all models have one
 * @option_mode_1_freq:		option mode 1 sampling frequency
 * @option_mode_1_3db_freq:	3db cutoff frequency of the low pass filter for
 * the first option mode.
 * @option_mode_2:		second optional mode. Not all chips have one
 * @option_mode_2_freq:		option mode 2 sampling frequency
 * @option_mode_2_3db_freq:	3db cutoff frequency of the low pass filter for
 * the second option mode.
 * @mod_det_mult_xz:		Bit wise multipliers to calculate the threshold
 * for motion detection in the x and z axis.
 * @mod_det_mult_y:		Bit wise multipliers to calculate the threshold
 * for motion detection in the y axis.
 *
 * This structure is used to hold information about the functionality of a given
 * sca3000 variant.
 **/
struct sca3000_chip_info {
	unsigned int		scale;
	bool			temp_output;
	int			measurement_mode_freq;
	int			measurement_mode_3db_freq;
	int			option_mode_1;
	int			option_mode_1_freq;
	int			option_mode_1_3db_freq;
	int			option_mode_2;
	int			option_mode_2_freq;
	int			option_mode_2_3db_freq;
	int			mot_det_mult_xz[6];
	int			mot_det_mult_y[7];
};

enum sca3000_variant {
	d01,
	e02,
	e04,
	e05,
};

/*
 * Note where option modes are not defined, the chip simply does not
 * support any.
 * Other chips in the sca3000 series use i2c and are not included here.
 *
 * Some of these devices are only listed in the family data sheet and
 * do not actually appear to be available.
 */
static const struct sca3000_chip_info sca3000_spi_chip_info_tbl[] = {
	[d01] = {
		.scale = 7357,
		.temp_output = true,
		.measurement_mode_freq = 250,
		.measurement_mode_3db_freq = 45,
		.option_mode_1 = SCA3000_OP_MODE_BYPASS,
		.option_mode_1_freq = 250,
		.option_mode_1_3db_freq = 70,
		.mot_det_mult_xz = {50, 100, 200, 350, 650, 1300},
		.mot_det_mult_y = {50, 100, 150, 250, 450, 850, 1750},
	},
	[e02] = {
		.scale = 9810,
		.measurement_mode_freq = 125,
		.measurement_mode_3db_freq = 40,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 63,
		.option_mode_1_3db_freq = 11,
		.mot_det_mult_xz = {100, 150, 300, 550, 1050, 2050},
		.mot_det_mult_y = {50, 100, 200, 350, 700, 1350, 2700},
	},
	[e04] = {
		.scale = 19620,
		.measurement_mode_freq = 100,
		.measurement_mode_3db_freq = 38,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 50,
		.option_mode_1_3db_freq = 9,
		.option_mode_2 = SCA3000_OP_MODE_WIDE,
		.option_mode_2_freq = 400,
		.option_mode_2_3db_freq = 70,
		.mot_det_mult_xz = {200, 300, 600, 1100, 2100, 4100},
		.mot_det_mult_y = {100, 200, 400, 7000, 1400, 2700, 54000},
	},
	[e05] = {
		.scale = 61313,
		.measurement_mode_freq = 200,
		.measurement_mode_3db_freq = 60,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 50,
		.option_mode_1_3db_freq = 9,
		.option_mode_2 = SCA3000_OP_MODE_WIDE,
		.option_mode_2_freq = 400,
		.option_mode_2_3db_freq = 75,
		.mot_det_mult_xz = {600, 900, 1700, 3200, 6100, 11900},
		.mot_det_mult_y = {300, 600, 1200, 2000, 4100, 7800, 15600},
	},
};

static int sca3000_write_reg(struct sca3000_state *st, u8 address, u8 val)
{
	st->tx[0] = SCA3000_WRITE_REG(address);
	st->tx[1] = val;
	return spi_write(st->us, st->tx, 2);
}

static int sca3000_read_data_short(struct sca3000_state *st,
				   u8 reg_address_high,
				   int len)
{
	struct spi_transfer xfer[2] = {
		{
			.len = 1,
			.tx_buf = st->tx,
		}, {
			.len = len,
			.rx_buf = st->rx,
		}
	};
	st->tx[0] = SCA3000_READ_REG(reg_address_high);

	return spi_sync_transfer(st->us, xfer, ARRAY_SIZE(xfer));
}

/**
 * sca3000_reg_lock_on() - test if the ctrl register lock is on
 * @st: Driver specific device instance data.
 *
 * Lock must be held.
 **/
static int sca3000_reg_lock_on(struct sca3000_state *st)
{
	int ret;

	ret = sca3000_read_data_short(st, SCA3000_REG_STATUS_ADDR, 1);
	if (ret < 0)
		return ret;

	return !(st->rx[0] & SCA3000_LOCKED);
}

/**
 * __sca3000_unlock_reg_lock() - unlock the control registers
 * @st: Driver specific device instance data.
 *
 * Note the device does not appear to support doing this in a single transfer.
 * This should only ever be used as part of ctrl reg read.
 * Lock must be held before calling this
 */
static int __sca3000_unlock_reg_lock(struct sca3000_state *st)
{
	struct spi_transfer xfer[3] = {
		{
			.len = 2,
			.cs_change = 1,
			.tx_buf = st->tx,
		}, {
			.len = 2,
			.cs_change = 1,
			.tx_buf = st->tx + 2,
		}, {
			.len = 2,
			.tx_buf = st->tx + 4,
		},
	};
	st->tx[0] = SCA3000_WRITE_REG(SCA3000_REG_UNLOCK_ADDR);
	st->tx[1] = 0x00;
	st->tx[2] = SCA3000_WRITE_REG(SCA3000_REG_UNLOCK_ADDR);
	st->tx[3] = 0x50;
	st->tx[4] = SCA3000_WRITE_REG(SCA3000_REG_UNLOCK_ADDR);
	st->tx[5] = 0xA0;

	return spi_sync_transfer(st->us, xfer, ARRAY_SIZE(xfer));
}

/**
 * sca3000_write_ctrl_reg() write to a lock protect ctrl register
 * @st: Driver specific device instance data.
 * @sel: selects which registers we wish to write to
 * @val: the value to be written
 *
 * Certain control registers are protected against overwriting by the lock
 * register and use a shared write address. This function allows writing of
 * these registers.
 * Lock must be held.
 */
static int sca3000_write_ctrl_reg(struct sca3000_state *st,
				  u8 sel,
				  uint8_t val)
{
	int ret;

	ret = sca3000_reg_lock_on(st);
	if (ret < 0)
		goto error_ret;
	if (ret) {
		ret = __sca3000_unlock_reg_lock(st);
		if (ret)
			goto error_ret;
	}

	/* Set the control select register */
	ret = sca3000_write_reg(st, SCA3000_REG_CTRL_SEL_ADDR, sel);
	if (ret)
		goto error_ret;

	/* Write the actual value into the register */
	ret = sca3000_write_reg(st, SCA3000_REG_CTRL_DATA_ADDR, val);

error_ret:
	return ret;
}

/**
 * sca3000_read_ctrl_reg() read from lock protected control register.
 * @st: Driver specific device instance data.
 * @ctrl_reg: Which ctrl register do we want to read.
 *
 * Lock must be held.
 */
static int sca3000_read_ctrl_reg(struct sca3000_state *st,
				 u8 ctrl_reg)
{
	int ret;

	ret = sca3000_reg_lock_on(st);
	if (ret < 0)
		goto error_ret;
	if (ret) {
		ret = __sca3000_unlock_reg_lock(st);
		if (ret)
			goto error_ret;
	}
	/* Set the control select register */
	ret = sca3000_write_reg(st, SCA3000_REG_CTRL_SEL_ADDR, ctrl_reg);
	if (ret)
		goto error_ret;
	ret = sca3000_read_data_short(st, SCA3000_REG_CTRL_DATA_ADDR, 1);
	if (ret)
		goto error_ret;
	return st->rx[0];
error_ret:
	return ret;
}

/**
 * sca3000_show_rev() - sysfs interface to read the chip revision number
 * @indio_dev: Device instance specific generic IIO data.
 * Driver specific device instance data can be obtained via
 * via iio_priv(indio_dev)
 */
static int sca3000_print_rev(struct iio_dev *indio_dev)
{
	int ret;
	struct sca3000_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_REVID_ADDR, 1);
	if (ret < 0)
		goto error_ret;
	dev_info(&indio_dev->dev,
		 "sca3000 revision major=%lu, minor=%lu\n",
		 st->rx[0] & SCA3000_REG_REVID_MAJOR_MASK,
		 st->rx[0] & SCA3000_REG_REVID_MINOR_MASK);
error_ret:
	mutex_unlock(&st->lock);

	return ret;
}

static ssize_t
sca3000_show_available_3db_freqs(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sca3000_state *st = iio_priv(indio_dev);
	int len;

	len = sprintf(buf, "%d", st->info->measurement_mode_3db_freq);
	if (st->info->option_mode_1)
		len += sprintf(buf + len, " %d",
			       st->info->option_mode_1_3db_freq);
	if (st->info->option_mode_2)
		len += sprintf(buf + len, " %d",
			       st->info->option_mode_2_3db_freq);
	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEVICE_ATTR(in_accel_filter_low_pass_3db_frequency_available,
		       S_IRUGO, sca3000_show_available_3db_freqs,
		       NULL, 0);

static const struct iio_event_spec sca3000_event = {
	.type = IIO_EV_TYPE_MAG,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
};

/*
 * Note the hack in the number of bits to pretend we have 2 more than
 * we do in the fifo.
 */
#define SCA3000_CHAN(index, mod)				\
	{							\
		.type = IIO_ACCEL,				\
		.modified = 1,					\
		.channel2 = mod,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |\
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.address = index,				\
		.scan_index = index,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 13,				\
			.storagebits = 16,			\
			.shift = 3,				\
			.endianness = IIO_BE,			\
		},						\
		.event_spec = &sca3000_event,			\
		.num_event_specs = 1,				\
	}

static const struct iio_event_spec sca3000_freefall_event_spec = {
	.type = IIO_EV_TYPE_MAG,
	.dir = IIO_EV_DIR_FALLING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
		BIT(IIO_EV_INFO_PERIOD),
};

static const struct iio_chan_spec sca3000_channels[] = {
	SCA3000_CHAN(0, IIO_MOD_X),
	SCA3000_CHAN(1, IIO_MOD_Y),
	SCA3000_CHAN(2, IIO_MOD_Z),
	{
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X_AND_Y_AND_Z,
		.scan_index = -1, /* Fake channel */
		.event_spec = &sca3000_freefall_event_spec,
		.num_event_specs = 1,
	},
};

static const struct iio_chan_spec sca3000_channels_with_temp[] = {
	SCA3000_CHAN(0, IIO_MOD_X),
	SCA3000_CHAN(1, IIO_MOD_Y),
	SCA3000_CHAN(2, IIO_MOD_Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		/* No buffer support */
		.scan_index = -1,
	},
	{
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X_AND_Y_AND_Z,
		.scan_index = -1, /* Fake channel */
		.event_spec = &sca3000_freefall_event_spec,
		.num_event_specs = 1,
	},
};

static u8 sca3000_addresses[3][3] = {
	[0] = {SCA3000_REG_X_MSB_ADDR, SCA3000_REG_CTRL_SEL_MD_X_TH,
	       SCA3000_MD_CTRL_OR_X},
	[1] = {SCA3000_REG_Y_MSB_ADDR, SCA3000_REG_CTRL_SEL_MD_Y_TH,
	       SCA3000_MD_CTRL_OR_Y},
	[2] = {SCA3000_REG_Z_MSB_ADDR, SCA3000_REG_CTRL_SEL_MD_Z_TH,
	       SCA3000_MD_CTRL_OR_Z},
};

/**
 * __sca3000_get_base_freq() - obtain mode specific base frequency
 * @st: Private driver specific device instance specific state.
 * @info: chip type specific information.
 * @base_freq: Base frequency for the current measurement mode.
 *
 * lock must be held
 */
static inline int __sca3000_get_base_freq(struct sca3000_state *st,
					  const struct sca3000_chip_info *info,
					  int *base_freq)
{
	int ret;

	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		goto error_ret;
	switch (SCA3000_REG_MODE_MODE_MASK & st->rx[0]) {
	case SCA3000_REG_MODE_MEAS_MODE_NORMAL:
		*base_freq = info->measurement_mode_freq;
		break;
	case SCA3000_REG_MODE_MEAS_MODE_OP_1:
		*base_freq = info->option_mode_1_freq;
		break;
	case SCA3000_REG_MODE_MEAS_MODE_OP_2:
		*base_freq = info->option_mode_2_freq;
		break;
	default:
		ret = -EINVAL;
	}
error_ret:
	return ret;
}

/**
 * sca3000_read_raw_samp_freq() - read_raw handler for IIO_CHAN_INFO_SAMP_FREQ
 * @st: Private driver specific device instance specific state.
 * @val: The frequency read back.
 *
 * lock must be held
 **/
static int sca3000_read_raw_samp_freq(struct sca3000_state *st, int *val)
{
	int ret;

	ret = __sca3000_get_base_freq(st, st->info, val);
	if (ret)
		return ret;

	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL);
	if (ret < 0)
		return ret;

	if (*val > 0) {
		ret &= SCA3000_REG_OUT_CTRL_BUF_DIV_MASK;
		switch (ret) {
		case SCA3000_REG_OUT_CTRL_BUF_DIV_2:
			*val /= 2;
			break;
		case SCA3000_REG_OUT_CTRL_BUF_DIV_4:
			*val /= 4;
			break;
		}
	}

	return 0;
}

/**
 * sca3000_write_raw_samp_freq() - write_raw handler for IIO_CHAN_INFO_SAMP_FREQ
 * @st: Private driver specific device instance specific state.
 * @val: The frequency desired.
 *
 * lock must be held
 */
static int sca3000_write_raw_samp_freq(struct sca3000_state *st, int val)
{
	int ret, base_freq, ctrlval;

	ret = __sca3000_get_base_freq(st, st->info, &base_freq);
	if (ret)
		return ret;

	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL);
	if (ret < 0)
		return ret;

	ctrlval = ret & ~SCA3000_REG_OUT_CTRL_BUF_DIV_MASK;

	if (val == base_freq / 2)
		ctrlval |= SCA3000_REG_OUT_CTRL_BUF_DIV_2;
	if (val == base_freq / 4)
		ctrlval |= SCA3000_REG_OUT_CTRL_BUF_DIV_4;
	else if (val != base_freq)
		return -EINVAL;

	return sca3000_write_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL,
				     ctrlval);
}

static int sca3000_read_3db_freq(struct sca3000_state *st, int *val)
{
	int ret;

	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		return ret;

	/* mask bottom 2 bits - only ones that are relevant */
	st->rx[0] &= SCA3000_REG_MODE_MODE_MASK;
	switch (st->rx[0]) {
	case SCA3000_REG_MODE_MEAS_MODE_NORMAL:
		*val = st->info->measurement_mode_3db_freq;
		return IIO_VAL_INT;
	case SCA3000_REG_MODE_MEAS_MODE_MOT_DET:
		return -EBUSY;
	case SCA3000_REG_MODE_MEAS_MODE_OP_1:
		*val = st->info->option_mode_1_3db_freq;
		return IIO_VAL_INT;
	case SCA3000_REG_MODE_MEAS_MODE_OP_2:
		*val = st->info->option_mode_2_3db_freq;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int sca3000_write_3db_freq(struct sca3000_state *st, int val)
{
	int ret;
	int mode;

	if (val == st->info->measurement_mode_3db_freq)
		mode = SCA3000_REG_MODE_MEAS_MODE_NORMAL;
	else if (st->info->option_mode_1 &&
		 (val == st->info->option_mode_1_3db_freq))
		mode = SCA3000_REG_MODE_MEAS_MODE_OP_1;
	else if (st->info->option_mode_2 &&
		 (val == st->info->option_mode_2_3db_freq))
		mode = SCA3000_REG_MODE_MEAS_MODE_OP_2;
	else
		return -EINVAL;
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		return ret;

	st->rx[0] &= ~SCA3000_REG_MODE_MODE_MASK;
	st->rx[0] |= (mode & SCA3000_REG_MODE_MODE_MASK);

	return sca3000_write_reg(st, SCA3000_REG_MODE_ADDR, st->rx[0]);
}

static int sca3000_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;
	u8 address;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		if (chan->type == IIO_ACCEL) {
			if (st->mo_det_use_count) {
				mutex_unlock(&st->lock);
				return -EBUSY;
			}
			address = sca3000_addresses[chan->address][0];
			ret = sca3000_read_data_short(st, address, 2);
			if (ret < 0) {
				mutex_unlock(&st->lock);
				return ret;
			}
			*val = (be16_to_cpup((__be16 *)st->rx) >> 3) & 0x1FFF;
			*val = ((*val) << (sizeof(*val) * 8 - 13)) >>
				(sizeof(*val) * 8 - 13);
		} else {
			/* get the temperature when available */
			ret = sca3000_read_data_short(st,
						      SCA3000_REG_TEMP_MSB_ADDR,
						      2);
			if (ret < 0) {
				mutex_unlock(&st->lock);
				return ret;
			}
			*val = ((st->rx[0] & 0x3F) << 3) |
			       ((st->rx[1] & 0xE0) >> 5);
		}
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		if (chan->type == IIO_ACCEL)
			*val2 = st->info->scale;
		else /* temperature */
			*val2 = 555556;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = -214;
		*val2 = 600000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&st->lock);
		ret = sca3000_read_raw_samp_freq(st, val);
		mutex_unlock(&st->lock);
		return ret ? ret : IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		mutex_lock(&st->lock);
		ret = sca3000_read_3db_freq(st, val);
		mutex_unlock(&st->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int sca3000_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2)
			return -EINVAL;
		mutex_lock(&st->lock);
		ret = sca3000_write_raw_samp_freq(st, val);
		mutex_unlock(&st->lock);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (val2)
			return -EINVAL;
		mutex_lock(&st->lock);
		ret = sca3000_write_3db_freq(st, val);
		mutex_unlock(&st->lock);
		return ret;
	default:
		return -EINVAL;
	}

	return ret;
}

/**
 * sca3000_read_av_freq() - sysfs function to get available frequencies
 * @dev: Device structure for this device.
 * @attr: Description of the attribute.
 * @buf: Incoming string
 *
 * The later modes are only relevant to the ring buffer - and depend on current
 * mode. Note that data sheet gives rather wide tolerances for these so integer
 * division will give good enough answer and not all chips have them specified
 * at all.
 **/
static ssize_t sca3000_read_av_freq(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sca3000_state *st = iio_priv(indio_dev);
	int len = 0, ret, val;

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	val = st->rx[0];
	mutex_unlock(&st->lock);
	if (ret)
		goto error_ret;

	switch (val & SCA3000_REG_MODE_MODE_MASK) {
	case SCA3000_REG_MODE_MEAS_MODE_NORMAL:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->measurement_mode_freq,
			       st->info->measurement_mode_freq / 2,
			       st->info->measurement_mode_freq / 4);
		break;
	case SCA3000_REG_MODE_MEAS_MODE_OP_1:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->option_mode_1_freq,
			       st->info->option_mode_1_freq / 2,
			       st->info->option_mode_1_freq / 4);
		break;
	case SCA3000_REG_MODE_MEAS_MODE_OP_2:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->option_mode_2_freq,
			       st->info->option_mode_2_freq / 2,
			       st->info->option_mode_2_freq / 4);
		break;
	}
	return len;
error_ret:
	return ret;
}

/*
 * Should only really be registered if ring buffer support is compiled in.
 * Does no harm however and doing it right would add a fair bit of complexity
 */
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(sca3000_read_av_freq);

/**
 * sca3000_read_event_value() - query of a threshold or period
 **/
static int sca3000_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	int ret, i;
	struct sca3000_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		mutex_lock(&st->lock);
		ret = sca3000_read_ctrl_reg(st,
					    sca3000_addresses[chan->address][1]);
		mutex_unlock(&st->lock);
		if (ret < 0)
			return ret;
		*val = 0;
		if (chan->channel2 == IIO_MOD_Y)
			for_each_set_bit(i, (unsigned long *)&ret,
					 ARRAY_SIZE(st->info->mot_det_mult_y))
				*val += st->info->mot_det_mult_y[i];
		else
			for_each_set_bit(i, (unsigned long *)&ret,
					 ARRAY_SIZE(st->info->mot_det_mult_xz))
				*val += st->info->mot_det_mult_xz[i];

		return IIO_VAL_INT;
	case IIO_EV_INFO_PERIOD:
		*val = 0;
		*val2 = 226000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

/**
 * sca3000_write_value() - control of threshold and period
 * @indio_dev: Device instance specific IIO information.
 * @chan: Description of the channel for which the event is being
 * configured.
 * @type: The type of event being configured, here magnitude rising
 * as everything else is read only.
 * @dir: Direction of the event (here rising)
 * @info: What information about the event are we configuring.
 * Here the threshold only.
 * @val: Integer part of the value being written..
 * @val2: Non integer part of the value being written. Here always 0.
 */
static int sca3000_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;
	int i;
	u8 nonlinear = 0;

	if (chan->channel2 == IIO_MOD_Y) {
		i = ARRAY_SIZE(st->info->mot_det_mult_y);
		while (i > 0)
			if (val >= st->info->mot_det_mult_y[--i]) {
				nonlinear |= (1 << i);
				val -= st->info->mot_det_mult_y[i];
			}
	} else {
		i = ARRAY_SIZE(st->info->mot_det_mult_xz);
		while (i > 0)
			if (val >= st->info->mot_det_mult_xz[--i]) {
				nonlinear |= (1 << i);
				val -= st->info->mot_det_mult_xz[i];
			}
	}

	mutex_lock(&st->lock);
	ret = sca3000_write_ctrl_reg(st,
				     sca3000_addresses[chan->address][1],
				     nonlinear);
	mutex_unlock(&st->lock);

	return ret;
}

static struct attribute *sca3000_attributes[] = {
	&iio_dev_attr_in_accel_filter_low_pass_3db_frequency_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group sca3000_attribute_group = {
	.attrs = sca3000_attributes,
};

static int sca3000_read_data(struct sca3000_state *st,
			     u8 reg_address_high,
			     u8 *rx,
			     int len)
{
	int ret;
	struct spi_transfer xfer[2] = {
		{
			.len = 1,
			.tx_buf = st->tx,
		}, {
			.len = len,
			.rx_buf = rx,
		}
	};

	st->tx[0] = SCA3000_READ_REG(reg_address_high);
	ret = spi_sync_transfer(st->us, xfer, ARRAY_SIZE(xfer));
	if (ret) {
		dev_err(&st->us->dev, "problem reading register\n");
		return ret;
	}

	return 0;
}

/**
 * sca3000_ring_int_process() - ring specific interrupt handling.
 * @val: Value of the interrupt status register.
 * @indio_dev: Device instance specific IIO device structure.
 */
static void sca3000_ring_int_process(u8 val, struct iio_dev *indio_dev)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret, i, num_available;

	mutex_lock(&st->lock);

	if (val & SCA3000_REG_INT_STATUS_HALF) {
		ret = sca3000_read_data_short(st, SCA3000_REG_BUF_COUNT_ADDR,
					      1);
		if (ret)
			goto error_ret;
		num_available = st->rx[0];
		/*
		 * num_available is the total number of samples available
		 * i.e. number of time points * number of channels.
		 */
		ret = sca3000_read_data(st, SCA3000_REG_RING_OUT_ADDR, st->rx,
					num_available * 2);
		if (ret)
			goto error_ret;
		for (i = 0; i < num_available / 3; i++) {
			/*
			 * Dirty hack to cover for 11 bit in fifo, 13 bit
			 * direct reading.
			 *
			 * In theory the bottom two bits are undefined.
			 * In reality they appear to always be 0.
			 */
			iio_push_to_buffers(indio_dev, st->rx + i * 3 * 2);
		}
	}
error_ret:
	mutex_unlock(&st->lock);
}

/**
 * sca3000_event_handler() - handling ring and non ring events
 * @irq: The irq being handled.
 * @private: struct iio_device pointer for the device.
 *
 * Ring related interrupt handler. Depending on event, push to
 * the ring buffer event chrdev or the event one.
 *
 * This function is complicated by the fact that the devices can signify ring
 * and non ring events via the same interrupt line and they can only
 * be distinguished via a read of the relevant status register.
 */
static irqreturn_t sca3000_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret, val;
	s64 last_timestamp = iio_get_time_ns(indio_dev);

	/*
	 * Could lead if badly timed to an extra read of status reg,
	 * but ensures no interrupt is missed.
	 */
	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_INT_STATUS_ADDR, 1);
	val = st->rx[0];
	mutex_unlock(&st->lock);
	if (ret)
		goto done;

	sca3000_ring_int_process(val, indio_dev);

	if (val & SCA3000_INT_STATUS_FREE_FALL)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X_AND_Y_AND_Z,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_FALLING),
			       last_timestamp);

	if (val & SCA3000_INT_STATUS_Y_TRIGGER)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Y,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       last_timestamp);

	if (val & SCA3000_INT_STATUS_X_TRIGGER)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       last_timestamp);

	if (val & SCA3000_INT_STATUS_Z_TRIGGER)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Z,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       last_timestamp);

done:
	return IRQ_HANDLED;
}

/**
 * sca3000_read_event_config() what events are enabled
 **/
static int sca3000_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;
	/* read current value of mode register */
	mutex_lock(&st->lock);

	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		goto error_ret;

	switch (chan->channel2) {
	case IIO_MOD_X_AND_Y_AND_Z:
		ret = !!(st->rx[0] & SCA3000_REG_MODE_FREE_FALL_DETECT);
		break;
	case IIO_MOD_X:
	case IIO_MOD_Y:
	case IIO_MOD_Z:
		/*
		 * Motion detection mode cannot run at the same time as
		 * acceleration data being read.
		 */
		if ((st->rx[0] & SCA3000_REG_MODE_MODE_MASK)
		    != SCA3000_REG_MODE_MEAS_MODE_MOT_DET) {
			ret = 0;
		} else {
			ret = sca3000_read_ctrl_reg(st,
						SCA3000_REG_CTRL_SEL_MD_CTRL);
			if (ret < 0)
				goto error_ret;
			/* only supporting logical or's for now */
			ret = !!(ret & sca3000_addresses[chan->address][2]);
		}
		break;
	default:
		ret = -EINVAL;
	}

error_ret:
	mutex_unlock(&st->lock);

	return ret;
}

static int sca3000_freefall_set_state(struct iio_dev *indio_dev, int state)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;

	/* read current value of mode register */
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		return ret;

	/* if off and should be on */
	if (state && !(st->rx[0] & SCA3000_REG_MODE_FREE_FALL_DETECT))
		return sca3000_write_reg(st, SCA3000_REG_MODE_ADDR,
					 st->rx[0] | SCA3000_REG_MODE_FREE_FALL_DETECT);
	/* if on and should be off */
	else if (!state && (st->rx[0] & SCA3000_REG_MODE_FREE_FALL_DETECT))
		return sca3000_write_reg(st, SCA3000_REG_MODE_ADDR,
					 st->rx[0] & ~SCA3000_REG_MODE_FREE_FALL_DETECT);
	else
		return 0;
}

static int sca3000_motion_detect_set_state(struct iio_dev *indio_dev, int axis,
					   int state)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret, ctrlval;

	/*
	 * First read the motion detector config to find out if
	 * this axis is on
	 */
	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_MD_CTRL);
	if (ret < 0)
		return ret;
	ctrlval = ret;
	/* if off and should be on */
	if (state && !(ctrlval & sca3000_addresses[axis][2])) {
		ret = sca3000_write_ctrl_reg(st,
					     SCA3000_REG_CTRL_SEL_MD_CTRL,
					     ctrlval |
					     sca3000_addresses[axis][2]);
		if (ret)
			return ret;
		st->mo_det_use_count++;
	} else if (!state && (ctrlval & sca3000_addresses[axis][2])) {
		ret = sca3000_write_ctrl_reg(st,
					     SCA3000_REG_CTRL_SEL_MD_CTRL,
					     ctrlval &
					     ~(sca3000_addresses[axis][2]));
		if (ret)
			return ret;
		st->mo_det_use_count--;
	}

	/* read current value of mode register */
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		return ret;
	/* if off and should be on */
	if ((st->mo_det_use_count) &&
	    ((st->rx[0] & SCA3000_REG_MODE_MODE_MASK)
	     != SCA3000_REG_MODE_MEAS_MODE_MOT_DET))
		return sca3000_write_reg(st, SCA3000_REG_MODE_ADDR,
			(st->rx[0] & ~SCA3000_REG_MODE_MODE_MASK)
			| SCA3000_REG_MODE_MEAS_MODE_MOT_DET);
	/* if on and should be off */
	else if (!(st->mo_det_use_count) &&
		 ((st->rx[0] & SCA3000_REG_MODE_MODE_MASK)
		  == SCA3000_REG_MODE_MEAS_MODE_MOT_DET))
		return sca3000_write_reg(st, SCA3000_REG_MODE_ADDR,
			st->rx[0] & SCA3000_REG_MODE_MODE_MASK);
	else
		return 0;
}

/**
 * sca3000_write_event_config() - simple on off control for motion detector
 * @indio_dev: IIO device instance specific structure. Data specific to this
 * particular driver may be accessed via iio_priv(indio_dev).
 * @chan: Description of the channel whose event we are configuring.
 * @type: The type of event.
 * @dir: The direction of the event.
 * @state: Desired state of event being configured.
 *
 * This is a per axis control, but enabling any will result in the
 * motion detector unit being enabled.
 * N.B. enabling motion detector stops normal data acquisition.
 * There is a complexity in knowing which mode to return to when
 * this mode is disabled.  Currently normal mode is assumed.
 **/
static int sca3000_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	switch (chan->channel2) {
	case IIO_MOD_X_AND_Y_AND_Z:
		ret = sca3000_freefall_set_state(indio_dev, state);
		break;

	case IIO_MOD_X:
	case IIO_MOD_Y:
	case IIO_MOD_Z:
		ret = sca3000_motion_detect_set_state(indio_dev,
						      chan->address,
						      state);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int sca3000_configure_ring(struct iio_dev *indio_dev)
{
	struct iio_buffer *buffer;

	buffer = devm_iio_kfifo_allocate(&indio_dev->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);
	indio_dev->modes |= INDIO_BUFFER_SOFTWARE;

	return 0;
}

static inline
int __sca3000_hw_ring_state_set(struct iio_dev *indio_dev, bool state)
{
	struct sca3000_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		goto error_ret;
	if (state) {
		dev_info(&indio_dev->dev, "supposedly enabling ring buffer\n");
		ret = sca3000_write_reg(st,
			SCA3000_REG_MODE_ADDR,
			(st->rx[0] | SCA3000_REG_MODE_RING_BUF_ENABLE));
	} else
		ret = sca3000_write_reg(st,
			SCA3000_REG_MODE_ADDR,
			(st->rx[0] & ~SCA3000_REG_MODE_RING_BUF_ENABLE));
error_ret:
	mutex_unlock(&st->lock);

	return ret;
}

/**
 * sca3000_hw_ring_preenable() - hw ring buffer preenable function
 * @indio_dev: structure representing the IIO device. Device instance
 * specific state can be accessed via iio_priv(indio_dev).
 *
 * Very simple enable function as the chip will allows normal reads
 * during ring buffer operation so as long as it is indeed running
 * before we notify the core, the precise ordering does not matter.
 */
static int sca3000_hw_ring_preenable(struct iio_dev *indio_dev)
{
	int ret;
	struct sca3000_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);

	/* Enable the 50% full interrupt */
	ret = sca3000_read_data_short(st, SCA3000_REG_INT_MASK_ADDR, 1);
	if (ret)
		goto error_unlock;
	ret = sca3000_write_reg(st,
				SCA3000_REG_INT_MASK_ADDR,
				st->rx[0] | SCA3000_REG_INT_MASK_RING_HALF);
	if (ret)
		goto error_unlock;

	mutex_unlock(&st->lock);

	return __sca3000_hw_ring_state_set(indio_dev, 1);

error_unlock:
	mutex_unlock(&st->lock);

	return ret;
}

static int sca3000_hw_ring_postdisable(struct iio_dev *indio_dev)
{
	int ret;
	struct sca3000_state *st = iio_priv(indio_dev);

	ret = __sca3000_hw_ring_state_set(indio_dev, 0);
	if (ret)
		return ret;

	/* Disable the 50% full interrupt */
	mutex_lock(&st->lock);

	ret = sca3000_read_data_short(st, SCA3000_REG_INT_MASK_ADDR, 1);
	if (ret)
		goto unlock;
	ret = sca3000_write_reg(st,
				SCA3000_REG_INT_MASK_ADDR,
				st->rx[0] & ~SCA3000_REG_INT_MASK_RING_HALF);
unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static const struct iio_buffer_setup_ops sca3000_ring_setup_ops = {
	.preenable = &sca3000_hw_ring_preenable,
	.postdisable = &sca3000_hw_ring_postdisable,
};

/**
 * sca3000_clean_setup() - get the device into a predictable state
 * @st: Device instance specific private data structure
 *
 * Devices use flash memory to store many of the register values
 * and hence can come up in somewhat unpredictable states.
 * Hence reset everything on driver load.
 */
static int sca3000_clean_setup(struct sca3000_state *st)
{
	int ret;

	mutex_lock(&st->lock);
	/* Ensure all interrupts have been acknowledged */
	ret = sca3000_read_data_short(st, SCA3000_REG_INT_STATUS_ADDR, 1);
	if (ret)
		goto error_ret;

	/* Turn off all motion detection channels */
	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_MD_CTRL);
	if (ret < 0)
		goto error_ret;
	ret = sca3000_write_ctrl_reg(st, SCA3000_REG_CTRL_SEL_MD_CTRL,
				     ret & SCA3000_MD_CTRL_PROT_MASK);
	if (ret)
		goto error_ret;

	/* Disable ring buffer */
	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL);
	if (ret < 0)
		goto error_ret;
	ret = sca3000_write_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL,
				     (ret & SCA3000_REG_OUT_CTRL_PROT_MASK)
				     | SCA3000_REG_OUT_CTRL_BUF_X_EN
				     | SCA3000_REG_OUT_CTRL_BUF_Y_EN
				     | SCA3000_REG_OUT_CTRL_BUF_Z_EN
				     | SCA3000_REG_OUT_CTRL_BUF_DIV_4);
	if (ret)
		goto error_ret;
	/* Enable interrupts, relevant to mode and set up as active low */
	ret = sca3000_read_data_short(st, SCA3000_REG_INT_MASK_ADDR, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st,
				SCA3000_REG_INT_MASK_ADDR,
				(ret & SCA3000_REG_INT_MASK_PROT_MASK)
				| SCA3000_REG_INT_MASK_ACTIVE_LOW);
	if (ret)
		goto error_ret;
	/*
	 * Select normal measurement mode, free fall off, ring off
	 * Ring in 12 bit mode - it is fine to overwrite reserved bits 3,5
	 * as that occurs in one of the example on the datasheet
	 */
	ret = sca3000_read_data_short(st, SCA3000_REG_MODE_ADDR, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st, SCA3000_REG_MODE_ADDR,
				(st->rx[0] & SCA3000_MODE_PROT_MASK));

error_ret:
	mutex_unlock(&st->lock);
	return ret;
}

static const struct iio_info sca3000_info = {
	.attrs = &sca3000_attribute_group,
	.read_raw = &sca3000_read_raw,
	.write_raw = &sca3000_write_raw,
	.read_event_value = &sca3000_read_event_value,
	.write_event_value = &sca3000_write_event_value,
	.read_event_config = &sca3000_read_event_config,
	.write_event_config = &sca3000_write_event_config,
};

static int sca3000_probe(struct spi_device *spi)
{
	int ret;
	struct sca3000_state *st;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	st->us = spi;
	mutex_init(&st->lock);
	st->info = &sca3000_spi_chip_info_tbl[spi_get_device_id(spi)
					      ->driver_data];

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &sca3000_info;
	if (st->info->temp_output) {
		indio_dev->channels = sca3000_channels_with_temp;
		indio_dev->num_channels =
			ARRAY_SIZE(sca3000_channels_with_temp);
	} else {
		indio_dev->channels = sca3000_channels;
		indio_dev->num_channels = ARRAY_SIZE(sca3000_channels);
	}
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = sca3000_configure_ring(indio_dev);
	if (ret)
		return ret;

	if (spi->irq) {
		ret = request_threaded_irq(spi->irq,
					   NULL,
					   &sca3000_event_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "sca3000",
					   indio_dev);
		if (ret)
			return ret;
	}
	indio_dev->setup_ops = &sca3000_ring_setup_ops;
	ret = sca3000_clean_setup(st);
	if (ret)
		goto error_free_irq;

	ret = sca3000_print_rev(indio_dev);
	if (ret)
		goto error_free_irq;

	return iio_device_register(indio_dev);

error_free_irq:
	if (spi->irq)
		free_irq(spi->irq, indio_dev);

	return ret;
}

static int sca3000_stop_all_interrupts(struct sca3000_state *st)
{
	int ret;

	mutex_lock(&st->lock);
	ret = sca3000_read_data_short(st, SCA3000_REG_INT_MASK_ADDR, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st, SCA3000_REG_INT_MASK_ADDR,
				(st->rx[0] &
				 ~(SCA3000_REG_INT_MASK_RING_THREE_QUARTER |
				   SCA3000_REG_INT_MASK_RING_HALF |
				   SCA3000_REG_INT_MASK_ALL_INTS)));
error_ret:
	mutex_unlock(&st->lock);
	return ret;
}

static int sca3000_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct sca3000_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	/* Must ensure no interrupts can be generated after this! */
	sca3000_stop_all_interrupts(st);
	if (spi->irq)
		free_irq(spi->irq, indio_dev);

	return 0;
}

static const struct spi_device_id sca3000_id[] = {
	{"sca3000_d01", d01},
	{"sca3000_e02", e02},
	{"sca3000_e04", e04},
	{"sca3000_e05", e05},
	{}
};
MODULE_DEVICE_TABLE(spi, sca3000_id);

static struct spi_driver sca3000_driver = {
	.driver = {
		.name = "sca3000",
	},
	.probe = sca3000_probe,
	.remove = sca3000_remove,
	.id_table = sca3000_id,
};
module_spi_driver(sca3000_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("VTI SCA3000 Series Accelerometers SPI driver");
MODULE_LICENSE("GPL v2");
