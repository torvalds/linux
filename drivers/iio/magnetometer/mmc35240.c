/*
 * MMC35240 - MEMSIC 3-axis Magnetic Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for MMC35240 (7-bit I2C slave address 0x30).
 *
 * TODO: offset, ACPI, continuous measurement mode, PM
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/acpi.h>
#include <linux/pm.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MMC35240_DRV_NAME "mmc35240"
#define MMC35240_REGMAP_NAME "mmc35240_regmap"

#define MMC35240_REG_XOUT_L	0x00
#define MMC35240_REG_XOUT_H	0x01
#define MMC35240_REG_YOUT_L	0x02
#define MMC35240_REG_YOUT_H	0x03
#define MMC35240_REG_ZOUT_L	0x04
#define MMC35240_REG_ZOUT_H	0x05

#define MMC35240_REG_STATUS	0x06
#define MMC35240_REG_CTRL0	0x07
#define MMC35240_REG_CTRL1	0x08

#define MMC35240_REG_ID		0x20

#define MMC35240_STATUS_MEAS_DONE_BIT	BIT(0)

#define MMC35240_CTRL0_REFILL_BIT	BIT(7)
#define MMC35240_CTRL0_RESET_BIT	BIT(6)
#define MMC35240_CTRL0_SET_BIT		BIT(5)
#define MMC35240_CTRL0_CMM_BIT		BIT(1)
#define MMC35240_CTRL0_TM_BIT		BIT(0)

/* output resolution bits */
#define MMC35240_CTRL1_BW0_BIT		BIT(0)
#define MMC35240_CTRL1_BW1_BIT		BIT(1)

#define MMC35240_CTRL1_BW_MASK	 (MMC35240_CTRL1_BW0_BIT | \
		 MMC35240_CTRL1_BW1_BIT)
#define MMC35240_CTRL1_BW_SHIFT		0

#define MMC35240_WAIT_CHARGE_PUMP	50000	/* us */
#define MMC53240_WAIT_SET_RESET		1000	/* us */

/*
 * Memsic OTP process code piece is put here for reference:
 *
 * #define OTP_CONVERT(REG)  ((float)((REG) >=32 ? (32 - (REG)) : (REG)) * 0.006
 * 1) For X axis, the COEFFICIENT is always 1.
 * 2) For Y axis, the COEFFICIENT is as below:
 *    f_OTP_matrix[4] = OTP_CONVERT(((reg_data[1] & 0x03) << 4) |
 *                                   (reg_data[2] >> 4)) + 1.0;
 * 3) For Z axis, the COEFFICIENT is as below:
 *    f_OTP_matrix[8] = (OTP_CONVERT(reg_data[3] & 0x3f) + 1) * 1.35;
 * We implemented the OTP logic into driver.
 */

/* scale = 1000 here for Y otp */
#define MMC35240_OTP_CONVERT_Y(REG) (((REG) >= 32 ? (32 - (REG)) : (REG)) * 6)

/* 0.6 * 1.35 = 0.81, scale 10000 for Z otp */
#define MMC35240_OTP_CONVERT_Z(REG) (((REG) >= 32 ? (32 - (REG)) : (REG)) * 81)

#define MMC35240_X_COEFF(x)	(x)
#define MMC35240_Y_COEFF(y)	(y + 1000)
#define MMC35240_Z_COEFF(z)	(z + 13500)

#define MMC35240_OTP_START_ADDR		0x1B

enum mmc35240_resolution {
	MMC35240_16_BITS_SLOW = 0, /* 7.92 ms */
	MMC35240_16_BITS_FAST,     /* 4.08 ms */
	MMC35240_14_BITS,          /* 2.16 ms */
	MMC35240_12_BITS,          /* 1.20 ms */
};

enum mmc35240_axis {
	AXIS_X = 0,
	AXIS_Y,
	AXIS_Z,
};

static const struct {
	int sens[3]; /* sensitivity per X, Y, Z axis */
	int nfo; /* null field output */
} mmc35240_props_table[] = {
	/* 16 bits, 125Hz ODR */
	{
		{1024, 1024, 1024},
		32768,
	},
	/* 16 bits, 250Hz ODR */
	{
		{1024, 1024, 770},
		32768,
	},
	/* 14 bits, 450Hz ODR */
	{
		{256, 256, 193},
		8192,
	},
	/* 12 bits, 800Hz ODR */
	{
		{64, 64, 48},
		2048,
	},
};

struct mmc35240_data {
	struct i2c_client *client;
	struct mutex mutex;
	struct regmap *regmap;
	enum mmc35240_resolution res;

	/* OTP compensation */
	int axis_coef[3];
	int axis_scale[3];
};

static const struct {
	int val;
	int val2;
} mmc35240_samp_freq[] = { {1, 500000},
			   {13, 0},
			   {25, 0},
			   {50, 0} };

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("1.5 13 25 50");

#define MMC35240_CHANNEL(_axis) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _axis, \
	.address = AXIS_ ## _axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_SCALE), \
}

static const struct iio_chan_spec mmc35240_channels[] = {
	MMC35240_CHANNEL(X),
	MMC35240_CHANNEL(Y),
	MMC35240_CHANNEL(Z),
};

static struct attribute *mmc35240_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mmc35240_attribute_group = {
	.attrs = mmc35240_attributes,
};

static int mmc35240_get_samp_freq_index(struct mmc35240_data *data,
					int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mmc35240_samp_freq); i++)
		if (mmc35240_samp_freq[i].val == val &&
		    mmc35240_samp_freq[i].val2 == val2)
			return i;
	return -EINVAL;
}

static int mmc35240_hw_set(struct mmc35240_data *data, bool set)
{
	int ret;
	u8 coil_bit;

	/*
	 * Recharge the capacitor at VCAP pin, requested to be issued
	 * before a SET/RESET command.
	 */
	ret = regmap_update_bits(data->regmap, MMC35240_REG_CTRL0,
				 MMC35240_CTRL0_REFILL_BIT,
				 MMC35240_CTRL0_REFILL_BIT);
	if (ret < 0)
		return ret;
	usleep_range(MMC35240_WAIT_CHARGE_PUMP, MMC35240_WAIT_CHARGE_PUMP + 1);

	if (set)
		coil_bit = MMC35240_CTRL0_SET_BIT;
	else
		coil_bit = MMC35240_CTRL0_RESET_BIT;

	return regmap_update_bits(data->regmap, MMC35240_REG_CTRL0,
				  coil_bit, coil_bit);

}

static int mmc35240_init(struct mmc35240_data *data)
{
	int ret, y_convert, z_convert;
	unsigned int reg_id;
	u8 otp_data[6];

	ret = regmap_read(data->regmap, MMC35240_REG_ID, &reg_id);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading product id\n");
		return ret;
	}

	dev_dbg(&data->client->dev, "MMC35240 chip id %x\n", reg_id);

	/*
	 * make sure we restore sensor characteristics, by doing
	 * a SET/RESET sequence, the axis polarity being naturally
	 * aligned after RESET
	 */
	ret = mmc35240_hw_set(data, true);
	if (ret < 0)
		return ret;
	usleep_range(MMC53240_WAIT_SET_RESET, MMC53240_WAIT_SET_RESET + 1);

	ret = mmc35240_hw_set(data, false);
	if (ret < 0)
		return ret;

	/* set default sampling frequency */
	ret = regmap_update_bits(data->regmap, MMC35240_REG_CTRL1,
				 MMC35240_CTRL1_BW_MASK,
				 data->res << MMC35240_CTRL1_BW_SHIFT);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, MMC35240_OTP_START_ADDR,
			       (u8 *)otp_data, sizeof(otp_data));
	if (ret < 0)
		return ret;

	y_convert = MMC35240_OTP_CONVERT_Y(((otp_data[1] & 0x03) << 4) |
					   (otp_data[2] >> 4));
	z_convert = MMC35240_OTP_CONVERT_Z(otp_data[3] & 0x3f);

	data->axis_coef[0] = MMC35240_X_COEFF(1);
	data->axis_coef[1] = MMC35240_Y_COEFF(y_convert);
	data->axis_coef[2] = MMC35240_Z_COEFF(z_convert);

	data->axis_scale[0] = 1;
	data->axis_scale[1] = 1000;
	data->axis_scale[2] = 10000;

	return 0;
}

static int mmc35240_take_measurement(struct mmc35240_data *data)
{
	int ret, tries = 100;
	unsigned int reg_status;

	ret = regmap_write(data->regmap, MMC35240_REG_CTRL0,
			   MMC35240_CTRL0_TM_BIT);
	if (ret < 0)
		return ret;

	while (tries-- > 0) {
		ret = regmap_read(data->regmap, MMC35240_REG_STATUS,
				  &reg_status);
		if (ret < 0)
			return ret;
		if (reg_status & MMC35240_STATUS_MEAS_DONE_BIT)
			break;
		/* minimum wait time to complete measurement is 10 ms */
		usleep_range(10000, 11000);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

static int mmc35240_read_measurement(struct mmc35240_data *data, __le16 buf[3])
{
	int ret;

	ret = mmc35240_take_measurement(data);
	if (ret < 0)
		return ret;

	return regmap_bulk_read(data->regmap, MMC35240_REG_XOUT_L, (u8 *)buf,
				3 * sizeof(__le16));
}

/**
 * mmc35240_raw_to_mgauss - convert raw readings to milli gauss. Also apply
			    compensation for output value.
 *
 * @data: device private data
 * @index: axis index for which we want the conversion
 * @buf: raw data to be converted, 2 bytes in little endian format
 * @val: compensated output reading (unit is milli gauss)
 *
 * Returns: 0 in case of success, -EINVAL when @index is not valid
 */
static int mmc35240_raw_to_mgauss(struct mmc35240_data *data, int index,
				  __le16 buf[], int *val)
{
	int raw[3];
	int sens[3];
	int nfo;

	raw[AXIS_X] = le16_to_cpu(buf[AXIS_X]);
	raw[AXIS_Y] = le16_to_cpu(buf[AXIS_Y]);
	raw[AXIS_Z] = le16_to_cpu(buf[AXIS_Z]);

	sens[AXIS_X] = mmc35240_props_table[data->res].sens[AXIS_X];
	sens[AXIS_Y] = mmc35240_props_table[data->res].sens[AXIS_Y];
	sens[AXIS_Z] = mmc35240_props_table[data->res].sens[AXIS_Z];

	nfo = mmc35240_props_table[data->res].nfo;

	switch (index) {
	case AXIS_X:
		*val = (raw[AXIS_X] - nfo) * 1000 / sens[AXIS_X];
		break;
	case AXIS_Y:
		*val = (raw[AXIS_Y] - nfo) * 1000 / sens[AXIS_Y] -
			(raw[AXIS_Z] - nfo)  * 1000 / sens[AXIS_Z];
		break;
	case AXIS_Z:
		*val = (raw[AXIS_Y] - nfo) * 1000 / sens[AXIS_Y] +
			(raw[AXIS_Z] - nfo) * 1000 / sens[AXIS_Z];
		break;
	default:
		return -EINVAL;
	}
	/* apply OTP compensation */
	*val = (*val) * data->axis_coef[index] / data->axis_scale[index];

	return 0;
}

static int mmc35240_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	struct mmc35240_data *data = iio_priv(indio_dev);
	int ret, i;
	unsigned int reg;
	__le16 buf[3];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->mutex);
		ret = mmc35240_read_measurement(data, buf);
		mutex_unlock(&data->mutex);
		if (ret < 0)
			return ret;
		ret = mmc35240_raw_to_mgauss(data, chan->address, buf, val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = regmap_read(data->regmap, MMC35240_REG_CTRL1, &reg);
		mutex_unlock(&data->mutex);
		if (ret < 0)
			return ret;

		i = (reg & MMC35240_CTRL1_BW_MASK) >> MMC35240_CTRL1_BW_SHIFT;
		if (i < 0 || i >= ARRAY_SIZE(mmc35240_samp_freq))
			return -EINVAL;

		*val = mmc35240_samp_freq[i].val;
		*val2 = mmc35240_samp_freq[i].val2;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mmc35240_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val,
			      int val2, long mask)
{
	struct mmc35240_data *data = iio_priv(indio_dev);
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = mmc35240_get_samp_freq_index(data, val, val2);
		if (i < 0)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = regmap_update_bits(data->regmap, MMC35240_REG_CTRL1,
					 MMC35240_CTRL1_BW_MASK,
					 i << MMC35240_CTRL1_BW_SHIFT);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mmc35240_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= mmc35240_read_raw,
	.write_raw	= mmc35240_write_raw,
	.attrs		= &mmc35240_attribute_group,
};

static bool mmc35240_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC35240_REG_CTRL0:
	case MMC35240_REG_CTRL1:
		return true;
	default:
		return false;
	}
}

static bool mmc35240_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC35240_REG_XOUT_L:
	case MMC35240_REG_XOUT_H:
	case MMC35240_REG_YOUT_L:
	case MMC35240_REG_YOUT_H:
	case MMC35240_REG_ZOUT_L:
	case MMC35240_REG_ZOUT_H:
	case MMC35240_REG_STATUS:
	case MMC35240_REG_ID:
		return true;
	default:
		return false;
	}
}

static bool mmc35240_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MMC35240_REG_CTRL0:
	case MMC35240_REG_CTRL1:
		return false;
	default:
		return true;
	}
}

static struct reg_default mmc35240_reg_defaults[] = {
	{ MMC35240_REG_CTRL0,  0x00 },
	{ MMC35240_REG_CTRL1,  0x00 },
};

static const struct regmap_config mmc35240_regmap_config = {
	.name = MMC35240_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MMC35240_REG_ID,
	.cache_type = REGCACHE_FLAT,

	.writeable_reg = mmc35240_is_writeable_reg,
	.readable_reg = mmc35240_is_readable_reg,
	.volatile_reg = mmc35240_is_volatile_reg,

	.reg_defaults = mmc35240_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(mmc35240_reg_defaults),
};

static int mmc35240_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct mmc35240_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &mmc35240_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap initialization failed\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;
	data->res = MMC35240_16_BITS_SLOW;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &mmc35240_info;
	indio_dev->name = MMC35240_DRV_NAME;
	indio_dev->channels = mmc35240_channels;
	indio_dev->num_channels = ARRAY_SIZE(mmc35240_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = mmc35240_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "mmc35240 chip init failed\n");
		return ret;
	}
	return devm_iio_device_register(&client->dev, indio_dev);
}

#ifdef CONFIG_PM_SLEEP
static int mmc35240_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mmc35240_data *data = iio_priv(indio_dev);

	regcache_cache_only(data->regmap, true);

	return 0;
}

static int mmc35240_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mmc35240_data *data = iio_priv(indio_dev);
	int ret;

	regcache_mark_dirty(data->regmap);
	ret = regcache_sync_region(data->regmap, MMC35240_REG_CTRL0,
				   MMC35240_REG_CTRL1);
	if (ret < 0)
		dev_err(dev, "Failed to restore control registers\n");

	regcache_cache_only(data->regmap, false);

	return 0;
}
#endif

static const struct dev_pm_ops mmc35240_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mmc35240_suspend, mmc35240_resume)
};

static const struct of_device_id mmc35240_of_match[] = {
	{ .compatible = "memsic,mmc35240", },
	{ }
};
MODULE_DEVICE_TABLE(of, mmc35240_of_match);

static const struct acpi_device_id mmc35240_acpi_match[] = {
	{"MMC35240", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, mmc35240_acpi_match);

static const struct i2c_device_id mmc35240_id[] = {
	{"mmc35240", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mmc35240_id);

static struct i2c_driver mmc35240_driver = {
	.driver = {
		.name = MMC35240_DRV_NAME,
		.of_match_table = mmc35240_of_match,
		.pm = &mmc35240_pm_ops,
		.acpi_match_table = ACPI_PTR(mmc35240_acpi_match),
	},
	.probe		= mmc35240_probe,
	.id_table	= mmc35240_id,
};

module_i2c_driver(mmc35240_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("MEMSIC MMC35240 magnetic sensor driver");
MODULE_LICENSE("GPL v2");
