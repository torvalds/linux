// SPDX-License-Identifier: GPL-2.0-only
/*
 * MMC5983 - MEMSIC 3-axis Magnetic Sensor
 *
 * Copyright (c) 2026, Vlad Kulikov <vlad.kulikov.c@gmail.com>
 *
 * IIO driver for MMC5983
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/types.h>

#define MMC5983_REG_XOUT0	0x00
#define MMC5983_REG_XOUT1	0x01
#define MMC5983_REG_YOUT0	0x02
#define MMC5983_REG_YOUT1	0x03
#define MMC5983_REG_ZOUT0	0x04
#define MMC5983_REG_ZOUT1	0x05
#define MMC5983_REG_XYZOUT2	0x06

#define MMC5983_REG_STATUS	0x08

#define MMC5983_REG_CTRL0	0x09
#define MMC5983_REG_CTRL1	0x0A
#define MMC5983_REG_CTRL2	0x0B
#define MMC5983_REG_CTRL3	0x0C

#define MMC5983_REG_ID		0x2F

#define MMC5983_PRODUCT_ID	0x30

#define MMC5983_STATUS_MEAS_M_DONE_BIT	BIT(0)
#define MMC5983_STATUS_OTP_RD_DONE_BIT	BIT(4)

#define MMC5983_CTRL0_TM_M_BIT		BIT(0)
#define MMC5983_CTRL0_SET_BIT		BIT(3)
#define MMC5983_CTRL0_RESET_BIT		BIT(4)
#define MMC5983_CTRL0_OTP_RD_BIT	BIT(6)

#define MMC5983_CTRL1_SW_RST_BIT	BIT(7)

enum mmc5983_axis {
	MMC5983_AXIS_X,
	MMC5983_AXIS_Y,
	MMC5983_AXIS_Z,
};

struct mmc5983_data {
	struct regmap *regmap;
	/* Protects chip access during SET/RESET measurement sequence */
	struct mutex mutex;
};

#define MMC5983_CHANNEL(_axis) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_##_axis, \
	.address = MMC5983_AXIS_##_axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
}

static const struct iio_chan_spec mmc5983_channels[] = {
	MMC5983_CHANNEL(X),
	MMC5983_CHANNEL(Y),
	MMC5983_CHANNEL(Z),
};

static int mmc5983_pulse_coil(struct mmc5983_data *data, unsigned int coil_bit)
{
	int ret;

	ret = regmap_write(data->regmap, MMC5983_REG_CTRL0, coil_bit);
	if (ret)
		return ret;

	/*
	 * Datasheet page 15: SET/RESET coil pulse is 500 ns.
	 * Vendor sample code waits 500 us before the next operation.
	 */
	fsleep(500);

	return 0;
}

static int mmc5983_take_measurement(struct mmc5983_data *data, int m[3])
{
	unsigned int status;
	u8 buf[7];
	int ret;

	ret = regmap_write(data->regmap, MMC5983_REG_CTRL0,
			   MMC5983_CTRL0_TM_M_BIT);
	if (ret)
		return ret;

	/*
	 * Datasheet page 15: measurement time is 8 ms at BW=00 (default,
	 * slowest setting). Use a 50 ms timeout for margin.
	 */
	ret = regmap_read_poll_timeout(data->regmap, MMC5983_REG_STATUS,
				       status,
				       status & MMC5983_STATUS_MEAS_M_DONE_BIT,
				       10 * USEC_PER_MSEC,
				       50 * USEC_PER_MSEC);
	if (ret)
		return ret;

	ret = regmap_bulk_read(data->regmap, MMC5983_REG_XOUT0, buf,
			       sizeof(buf));
	if (ret)
		return ret;

	m[0] = (buf[0] << 10) | (buf[1] << 2) | ((buf[6] >> 6) & 0x3);
	m[1] = (buf[2] << 10) | (buf[3] << 2) | ((buf[6] >> 4) & 0x3);
	m[2] = (buf[4] << 10) | (buf[5] << 2) | ((buf[6] >> 2) & 0x3);

	return 0;
}

static int mmc5983_read_raw(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan, int *val,
			     int *val2, long mask)
{
	struct mmc5983_data *data = iio_priv(indio_dev);
	int m1[3], m2[3];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		guard(mutex)(&data->mutex);

		/* SET: magnetize sensor elements in forward direction */
		ret = mmc5983_pulse_coil(data, MMC5983_CTRL0_SET_BIT);
		if (ret)
			return ret;

		ret = mmc5983_take_measurement(data, m1);
		if (ret)
			return ret;

		/* RESET: magnetize sensor elements in reverse direction */
		ret = mmc5983_pulse_coil(data, MMC5983_CTRL0_RESET_BIT);
		if (ret)
			return ret;

		ret = mmc5983_take_measurement(data, m2);
		if (ret)
			return ret;

		*val = (m1[chan->address] - m2[chan->address]) / 2;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 61035;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mmc5983_info = {
	.read_raw = mmc5983_read_raw,
};

static bool mmc5983_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5983_REG_CTRL0:
	case MMC5983_REG_CTRL1:
	case MMC5983_REG_CTRL2:
	case MMC5983_REG_CTRL3:
		return true;
	default:
		return false;
	}
}

static bool mmc5983_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5983_REG_XOUT0:
	case MMC5983_REG_XOUT1:
	case MMC5983_REG_YOUT0:
	case MMC5983_REG_YOUT1:
	case MMC5983_REG_ZOUT0:
	case MMC5983_REG_ZOUT1:
	case MMC5983_REG_XYZOUT2:
	case MMC5983_REG_STATUS:
	case MMC5983_REG_CTRL0:
	case MMC5983_REG_CTRL1:
	case MMC5983_REG_CTRL2:
	case MMC5983_REG_CTRL3:
	case MMC5983_REG_ID:
		return true;
	default:
		return false;
	}
}

static bool mmc5983_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5983_REG_XOUT0:
	case MMC5983_REG_XOUT1:
	case MMC5983_REG_YOUT0:
	case MMC5983_REG_YOUT1:
	case MMC5983_REG_ZOUT0:
	case MMC5983_REG_ZOUT1:
	case MMC5983_REG_XYZOUT2:
	case MMC5983_REG_STATUS:
	case MMC5983_REG_CTRL0:
	case MMC5983_REG_CTRL1:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mmc5983_regmap_config = {
	.name = "mmc5983_regmap",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MMC5983_REG_ID,
	.writeable_reg = mmc5983_is_writeable_reg,
	.readable_reg = mmc5983_is_readable_reg,
	.volatile_reg = mmc5983_is_volatile_reg,
};

static int mmc5983_init(struct mmc5983_data *data)
{
	struct regmap *regmap = data->regmap;
	struct device *dev = regmap_get_device(regmap);
	unsigned int reg_id, status;
	int ret;

	ret = regmap_read(regmap, MMC5983_REG_ID, &reg_id);
	if (ret)
		return dev_err_probe(dev, ret, "Error reading product id\n");

	if (reg_id != MMC5983_PRODUCT_ID)
		dev_info(dev, "unexpected product id 0x%02x\n", reg_id);

	ret = regmap_write(regmap, MMC5983_REG_CTRL1, MMC5983_CTRL1_SW_RST_BIT);
	if (ret)
		return ret;

	/* Datasheet page 15: power-on time after SW_RST is 10 ms */
	fsleep(10 * USEC_PER_MSEC);

	ret = regmap_write(regmap, MMC5983_REG_CTRL0, MMC5983_CTRL0_OTP_RD_BIT);
	if (ret)
		return ret;

	/*
	 * Datasheet page 15: OTP read completes and self-clears. No separate
	 * OTP refresh timeout is specified, so use the 10 ms power-on time as
	 * a conservative upper bound.
	 */
	ret = regmap_read_poll_timeout(regmap, MMC5983_REG_STATUS, status,
				       status & MMC5983_STATUS_OTP_RD_DONE_BIT,
				       USEC_PER_MSEC,
				       10 * USEC_PER_MSEC);
	if (ret)
		return ret;

	/* SET: magnetize sensor elements in forward direction */
	ret = mmc5983_pulse_coil(data, MMC5983_CTRL0_SET_BIT);
	if (ret)
		return ret;

	/* RESET: magnetize sensor elements in reverse direction */
	return mmc5983_pulse_coil(data, MMC5983_CTRL0_RESET_BIT);
}

static int mmc5983_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct mmc5983_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret)
		return ret;

	data->regmap = devm_regmap_init_i2c(i2c, &mmc5983_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to allocate register map\n");

	indio_dev->info = &mmc5983_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = "mmc5983";
	indio_dev->channels = mmc5983_channels;
	indio_dev->num_channels = ARRAY_SIZE(mmc5983_channels);

	ret = mmc5983_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "mmc5983 chip init failed\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id mmc5983_of_match[] = {
	{ .compatible = "memsic,mmc5983" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmc5983_of_match);

static const struct i2c_device_id mmc5983_id[] = {
	{ "mmc5983" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mmc5983_id);

static struct i2c_driver mmc5983_driver = {
	.driver = {
		.name = "mmc5983",
		.of_match_table = mmc5983_of_match,
	},
	.probe = mmc5983_probe,
	.id_table = mmc5983_id,
};
module_i2c_driver(mmc5983_driver);

MODULE_AUTHOR("Vladislav Kulikov <vlad.kulikov.c@gmail.com>");
MODULE_DESCRIPTION("MEMSIC MMC5983 magnetic sensor driver");
MODULE_LICENSE("GPL");
