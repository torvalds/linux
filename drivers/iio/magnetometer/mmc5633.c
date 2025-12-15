// SPDX-License-Identifier: GPL-2.0-only
/*
 * MMC5633 - MEMSIC 3-axis Magnetic Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 * Copyright (c) 2025, NXP
 *
 * IIO driver for MMC5633, base on mmc35240.c
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i3c/device.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#define MMC5633_REG_XOUT0	0x00
#define MMC5633_REG_XOUT1	0x01
#define MMC5633_REG_YOUT0	0x02
#define MMC5633_REG_YOUT1	0x03
#define MMC5633_REG_ZOUT0	0x04
#define MMC5633_REG_ZOUT1	0x05
#define MMC5633_REG_XOUT2	0x06
#define MMC5633_REG_YOUT2	0x07
#define MMC5633_REG_ZOUT2	0x08
#define MMC5633_REG_TOUT	0x09

#define MMC5633_REG_STATUS1	0x18
#define MMC5633_REG_STATUS0	0x19
#define MMC5633_REG_CTRL0	0x1b
#define MMC5633_REG_CTRL1	0x1c
#define MMC5633_REG_CTRL2	0x1d

#define MMC5633_REG_ID		0x39

#define MMC5633_STATUS1_MEAS_T_DONE_BIT	BIT(7)
#define MMC5633_STATUS1_MEAS_M_DONE_BIT	BIT(6)

#define MMC5633_CTRL0_CMM_FREQ_EN	BIT(7)
#define MMC5633_CTRL0_AUTO_ST_EN	BIT(6)
#define MMC5633_CTRL0_AUTO_SR_EN	BIT(5)
#define MMC5633_CTRL0_RESET		BIT(4)
#define MMC5633_CTRL0_SET		BIT(3)
#define MMC5633_CTRL0_MEAS_T		BIT(1)
#define MMC5633_CTRL0_MEAS_M		BIT(0)

#define MMC5633_CTRL1_BW_MASK		GENMASK(1, 0)

#define MMC5633_WAIT_SET_RESET_US	(1 * USEC_PER_MSEC)

#define MMC5633_HDR_CTRL0_MEAS_M	0x01
#define MMC5633_HDR_CTRL0_MEAS_T	0x03
#define MMC5633_HDR_CTRL0_SET		0x05
#define MMC5633_HDR_CTRL0_RESET		0x07

enum mmc5633_axis {
	MMC5633_AXIS_X,
	MMC5633_AXIS_Y,
	MMC5633_AXIS_Z,
	MMC5633_TEMPERATURE,
};

struct mmc5633_data {
	struct regmap *regmap;
	struct i3c_device *i3cdev;
	struct mutex mutex; /* protect to finish one whole measurement */
};

static int mmc5633_samp_freq[][2] = {
	{ 1, 200000 },
	{ 2, 0 },
	{ 3, 500000 },
	{ 6, 600000 },
};

#define MMC5633_CHANNEL(_axis) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _axis, \
	.address = MMC5633_AXIS_ ## _axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
				    BIT(IIO_CHAN_INFO_SCALE), \
}

static const struct iio_chan_spec mmc5633_channels[] = {
	MMC5633_CHANNEL(X),
	MMC5633_CHANNEL(Y),
	MMC5633_CHANNEL(Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.address = MMC5633_TEMPERATURE,
	},
};

static int mmc5633_get_samp_freq_index(struct mmc5633_data *data,
				       int val, int val2)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mmc5633_samp_freq); i++)
		if (mmc5633_samp_freq[i][0] == val &&
		    mmc5633_samp_freq[i][1] == val2)
			return i;
	return -EINVAL;
}

static int mmc5633_init(struct mmc5633_data *data)
{
	unsigned int reg_id;
	int ret;

	ret = regmap_read(data->regmap, MMC5633_REG_ID, &reg_id);
	if (ret)
		return dev_err_probe(regmap_get_device(data->regmap), ret,
				     "Error reading product id\n");

	/*
	 * Make sure we restore sensor characteristics, by doing
	 * a SET/RESET sequence, the axis polarity being naturally
	 * aligned after RESET.
	 */
	ret = regmap_write(data->regmap, MMC5633_REG_CTRL0, MMC5633_CTRL0_SET);
	if (ret)
		return ret;

	/*
	 * Minimum time interval between SET or RESET to other operations is
	 * 1ms according to Operating Timing Diagram in datasheet.
	 */
	fsleep(MMC5633_WAIT_SET_RESET_US);

	ret = regmap_write(data->regmap, MMC5633_REG_CTRL0, MMC5633_CTRL0_RESET);
	if (ret)
		return ret;

	/* set default sampling frequency */
	return regmap_update_bits(data->regmap, MMC5633_REG_CTRL1,
				  MMC5633_CTRL1_BW_MASK,
				  FIELD_PREP(MMC5633_CTRL1_BW_MASK, 0));
}

static int mmc5633_take_measurement(struct mmc5633_data *data, int address)
{
	unsigned int reg_status, val;
	int ret;

	val = (address == MMC5633_TEMPERATURE) ? MMC5633_CTRL0_MEAS_T : MMC5633_CTRL0_MEAS_M;
	ret = regmap_write(data->regmap, MMC5633_REG_CTRL0, val);
	if (ret < 0)
		return ret;

	val = (address == MMC5633_TEMPERATURE) ?
	      MMC5633_STATUS1_MEAS_T_DONE_BIT : MMC5633_STATUS1_MEAS_M_DONE_BIT;
	ret = regmap_read_poll_timeout(data->regmap, MMC5633_REG_STATUS1, reg_status,
				       reg_status & val,
				       10 * USEC_PER_MSEC,
				       100 * 10 * USEC_PER_MSEC);
	if (ret) {
		dev_err(regmap_get_device(data->regmap), "data not ready\n");
		return ret;
	}

	return 0;
}

static bool mmc5633_is_support_hdr(struct mmc5633_data *data)
{
	if (!data->i3cdev)
		return false;

	return i3c_device_get_supported_xfer_mode(data->i3cdev) & BIT(I3C_HDR_DDR);
}

static int mmc5633_read_measurement(struct mmc5633_data *data, int address, void *buf, size_t sz)
{
	struct device *dev = regmap_get_device(data->regmap);
	u8 data_cmd[2], status[2];
	unsigned int val, ready;
	int ret;

	if (mmc5633_is_support_hdr(data)) {
		struct i3c_xfer xfers_wr_cmd[] = {
			{
				.cmd = 0x3b,
				.len = 2,
				.data.out = data_cmd,
			}
		};
		struct i3c_xfer xfers_rd_sta_cmd[] = {
			{
				.cmd = 0x23 | BIT(7), /* RDSTA CMD */
				.len = 2,
				.data.in = status,
			},
		};
		struct i3c_xfer xfers_rd_data_cmd[] = {
			{
				.cmd = 0x22 | BIT(7), /* RDLONG CMD */
				.len = sz,
				.data.in = buf,
			},
		};

		data_cmd[0] = 0;
		data_cmd[1] = (address == MMC5633_TEMPERATURE) ?
			      MMC5633_HDR_CTRL0_MEAS_T : MMC5633_HDR_CTRL0_MEAS_M;

		ret = i3c_device_do_xfers(data->i3cdev, xfers_wr_cmd,
					  ARRAY_SIZE(xfers_wr_cmd), I3C_HDR_DDR);
		if (ret < 0)
			return ret;

		ready = (address == MMC5633_TEMPERATURE) ?
			MMC5633_STATUS1_MEAS_T_DONE_BIT : MMC5633_STATUS1_MEAS_M_DONE_BIT;
		ret = read_poll_timeout(i3c_device_do_xfers, val,
					val || (status[0] & ready),
					10 * USEC_PER_MSEC,
					100 * 10 * USEC_PER_MSEC, 0,
					data->i3cdev, xfers_rd_sta_cmd,
					ARRAY_SIZE(xfers_rd_sta_cmd), I3C_HDR_DDR);
		if (ret) {
			dev_err(dev, "data not ready\n");
			return ret;
		}
		if (val) {
			dev_err(dev, "i3c transfer error\n");
			return val;
		}
		return i3c_device_do_xfers(data->i3cdev, xfers_rd_data_cmd,
					   ARRAY_SIZE(xfers_rd_data_cmd), I3C_HDR_DDR);
	}

	/* Fallback to use SDR/I2C mode */
	ret = mmc5633_take_measurement(data, address);
	if (ret < 0)
		return ret;

	if (address == MMC5633_TEMPERATURE)
		/*
		 * Put tempeature to last byte of buff to align HDR case.
		 * I3C will early terminate data read if previous data is not
		 * available.
		 */
		return regmap_bulk_read(data->regmap, MMC5633_REG_TOUT, buf + sz - 1, 1);

	return regmap_bulk_read(data->regmap, MMC5633_REG_XOUT0, buf, sz);
}

/* X,Y,Z 3 channels, each channel has 3 byte and TEMP */
#define MMC5633_ALL_SIZE (3 * 3 + 1)

static int mmc5633_get_raw(struct mmc5633_data *data, int index, unsigned char *buf, int *val)
{
	if (index == MMC5633_TEMPERATURE) {
		*val = buf[MMC5633_ALL_SIZE - 1];
		return 0;
	}
	/*
	 * X[19..12] X[11..4] Y[19..12] Y[11..4] Z[19..12] Z[11..4] X[3..0] Y[3..0] Z[3..0]
	 */
	*val = get_unaligned_be16(buf + 2 * index) << 4;
	*val |= buf[index + 6] >> 4;

	return 0;
}

static int mmc5633_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct mmc5633_data *data = iio_priv(indio_dev);
	char buf[MMC5633_ALL_SIZE];
	unsigned int reg, i;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		scoped_guard(mutex, &data->mutex) {
			ret = mmc5633_read_measurement(data, chan->address, buf, MMC5633_ALL_SIZE);
			if (ret < 0)
				return ret;
		}

		ret = mmc5633_get_raw(data, chan->address, buf, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_MAGN) {
			*val = 0;
			*val2 = 62500;
		} else {
			*val = 0;
			*val2 = 800000000; /* 0.8C */
		}
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = -75;
			return IIO_VAL_INT;
		}
		return -EINVAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		scoped_guard(mutex, &data->mutex) {
			ret = regmap_read(data->regmap, MMC5633_REG_CTRL1, &reg);
			if (ret < 0)
				return ret;
		}

		i = FIELD_GET(MMC5633_CTRL1_BW_MASK, reg);
		if (i >= ARRAY_SIZE(mmc5633_samp_freq))
			return -EINVAL;

		*val = mmc5633_samp_freq[i][0];
		*val2 = mmc5633_samp_freq[i][1];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mmc5633_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct mmc5633_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		ret = mmc5633_get_samp_freq_index(data, val, val2);
		if (ret < 0)
			return ret;

		guard(mutex)(&data->mutex);

		return regmap_update_bits(data->regmap, MMC5633_REG_CTRL1,
					  MMC5633_CTRL1_BW_MASK,
					  FIELD_PREP(MMC5633_CTRL1_BW_MASK, ret));
	}
	default:
		return -EINVAL;
	}
}

static int mmc5633_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)mmc5633_samp_freq;
		*length = ARRAY_SIZE(mmc5633_samp_freq) * 2;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mmc5633_info = {
	.read_raw	= mmc5633_read_raw,
	.write_raw	= mmc5633_write_raw,
	.read_avail	= mmc5633_read_avail,
};

static bool mmc5633_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5633_REG_CTRL0:
	case MMC5633_REG_CTRL1:
		return true;
	default:
		return false;
	}
}

static bool mmc5633_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5633_REG_XOUT0:
	case MMC5633_REG_XOUT1:
	case MMC5633_REG_YOUT0:
	case MMC5633_REG_YOUT1:
	case MMC5633_REG_ZOUT0:
	case MMC5633_REG_ZOUT1:
	case MMC5633_REG_XOUT2:
	case MMC5633_REG_YOUT2:
	case MMC5633_REG_ZOUT2:
	case MMC5633_REG_TOUT:
	case MMC5633_REG_STATUS1:
	case MMC5633_REG_ID:
		return true;
	default:
		return false;
	}
}

static bool mmc5633_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC5633_REG_CTRL0:
	case MMC5633_REG_CTRL1:
		return false;
	default:
		return true;
	}
}

static const struct reg_default mmc5633_reg_defaults[] = {
	{ MMC5633_REG_CTRL0,  0x00 },
	{ MMC5633_REG_CTRL1,  0x00 },
};

static const struct regmap_config mmc5633_regmap_config = {
	.name = "mmc5633_regmap",

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MMC5633_REG_ID,
	.cache_type = REGCACHE_MAPLE,

	.writeable_reg = mmc5633_is_writeable_reg,
	.readable_reg = mmc5633_is_readable_reg,
	.volatile_reg = mmc5633_is_volatile_reg,

	.reg_defaults = mmc5633_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(mmc5633_reg_defaults),
};

static int mmc5633_common_probe(struct regmap *regmap, char *name,
				struct i3c_device *i3cdev)
{
	struct device *dev = regmap_get_device(regmap);
	struct mmc5633_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	data->regmap = regmap;
	data->i3cdev = i3cdev;

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret)
		return ret;

	indio_dev->info = &mmc5633_info;
	indio_dev->name = name;
	indio_dev->channels = mmc5633_channels;
	indio_dev->num_channels = ARRAY_SIZE(mmc5633_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = mmc5633_init(data);
	if (ret < 0)
		return dev_err_probe(dev, ret, "mmc5633 chip init failed\n");

	return devm_iio_device_register(dev, indio_dev);
}

static int mmc5633_suspend(struct device *dev)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);

	regcache_cache_only(regmap, true);

	return 0;
}

static int mmc5633_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_regmap(dev, NULL);
	int ret;

	regcache_mark_dirty(regmap);
	ret = regcache_sync_region(regmap, MMC5633_REG_CTRL0, MMC5633_REG_CTRL1);
	if (ret)
		dev_err(dev, "Failed to restore control registers\n");

	regcache_cache_only(regmap, false);

	return 0;
}

static int mmc5633_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &mmc5633_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "regmap init failed\n");

	return mmc5633_common_probe(regmap, client->name, NULL);
}

static DEFINE_SIMPLE_DEV_PM_OPS(mmc5633_pm_ops, mmc5633_suspend, mmc5633_resume);

static const struct of_device_id mmc5633_of_match[] = {
	{ .compatible = "memsic,mmc5603" },
	{ .compatible = "memsic,mmc5633" },
	{ }
};
MODULE_DEVICE_TABLE(of, mmc5633_of_match);

static const struct i2c_device_id mmc5633_i2c_id[] = {
	{ "mmc5603" },
	{ "mmc5633" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mmc5633_i2c_id);

static struct i2c_driver mmc5633_i2c_driver = {
	.driver = {
		.name = "mmc5633_i2c",
		.of_match_table = mmc5633_of_match,
		.pm = pm_sleep_ptr(&mmc5633_pm_ops),
	},
	.probe = mmc5633_i2c_probe,
	.id_table = mmc5633_i2c_id,
};

static const struct i3c_device_id mmc5633_i3c_ids[] = {
	I3C_DEVICE(0x0251, 0x0000, NULL),
	{ }
};
MODULE_DEVICE_TABLE(i3c, mmc5633_i3c_ids);

static int mmc5633_i3c_probe(struct i3c_device *i3cdev)
{
	struct device *dev = i3cdev_to_dev(i3cdev);
	struct regmap *regmap;
	char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, "mmc5633_%s", dev_name(dev));
	if (!name)
		return -ENOMEM;

	regmap = devm_regmap_init_i3c(i3cdev, &mmc5633_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to register i3c regmap\n");

	return mmc5633_common_probe(regmap, name, i3cdev);
}

static struct i3c_driver mmc5633_i3c_driver = {
	.driver = {
		.name = "mmc5633_i3c",
	},
	.probe = mmc5633_i3c_probe,
	.id_table = mmc5633_i3c_ids,
};
module_i3c_i2c_driver(mmc5633_i3c_driver, &mmc5633_i2c_driver)

MODULE_AUTHOR("Frank Li <Frank.li@nxp.com>");
MODULE_DESCRIPTION("MEMSIC MMC5633 magnetic sensor driver");
MODULE_LICENSE("GPL");
