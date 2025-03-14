// SPDX-License-Identifier: GPL-2.0
/*
 * Silicon Labs Si7210 Hall Effect sensor driver
 *
 * Copyright (c) 2024 Antoni Pokusinski <apokusinski01@gmail.com>
 *
 * Datasheet:
 *  https://www.silabs.com/documents/public/data-sheets/si7210-datasheet.pdf
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/units.h>
#include <asm/byteorder.h>

/* Registers offsets and masks */
#define SI7210_REG_DSPSIGM	0xC1
#define SI7210_REG_DSPSIGL	0xC2

#define SI7210_MASK_DSPSIGSEL	GENMASK(2, 0)
#define SI7210_REG_DSPSIGSEL	0xC3

#define SI7210_MASK_STOP	BIT(1)
#define SI7210_MASK_ONEBURST	BIT(2)
#define SI7210_REG_POWER_CTRL	0xC4

#define SI7210_MASK_ARAUTOINC	BIT(0)
#define SI7210_REG_ARAUTOINC	0xC5

#define SI7210_REG_A0		0xCA
#define SI7210_REG_A1		0xCB
#define SI7210_REG_A2		0xCC
#define SI7210_REG_A3		0xCE
#define SI7210_REG_A4		0xCF
#define SI7210_REG_A5		0xD0

#define SI7210_REG_OTP_ADDR	0xE1
#define SI7210_REG_OTP_DATA	0xE2

#define SI7210_MASK_OTP_READ_EN	BIT(1)
#define SI7210_REG_OTP_CTRL	0xE3

/* OTP data registers offsets */
#define SI7210_OTPREG_TMP_OFF	0x1D
#define SI7210_OTPREG_TMP_GAIN	0x1E

#define SI7210_OTPREG_A0_20	0x21
#define SI7210_OTPREG_A1_20	0x22
#define SI7210_OTPREG_A2_20	0x23
#define SI7210_OTPREG_A3_20	0x24
#define SI7210_OTPREG_A4_20	0x25
#define SI7210_OTPREG_A5_20	0x26

#define SI7210_OTPREG_A0_200	0x27
#define SI7210_OTPREG_A1_200	0x28
#define SI7210_OTPREG_A2_200	0x29
#define SI7210_OTPREG_A3_200	0x2A
#define SI7210_OTPREG_A4_200	0x2B
#define SI7210_OTPREG_A5_200	0x2C

#define A_REGS_COUNT 6

static const unsigned int a20_otp_regs[A_REGS_COUNT] = {
	SI7210_OTPREG_A0_20, SI7210_OTPREG_A1_20, SI7210_OTPREG_A2_20,
	SI7210_OTPREG_A3_20, SI7210_OTPREG_A4_20, SI7210_OTPREG_A5_20,
};

static const unsigned int a200_otp_regs[A_REGS_COUNT] = {
	SI7210_OTPREG_A0_200, SI7210_OTPREG_A1_200, SI7210_OTPREG_A2_200,
	SI7210_OTPREG_A3_200, SI7210_OTPREG_A4_200, SI7210_OTPREG_A5_200,
};

static const struct regmap_range si7210_read_reg_ranges[] = {
	regmap_reg_range(SI7210_REG_DSPSIGM, SI7210_REG_ARAUTOINC),
	regmap_reg_range(SI7210_REG_A0, SI7210_REG_A2),
	regmap_reg_range(SI7210_REG_A3, SI7210_REG_A5),
	regmap_reg_range(SI7210_REG_OTP_ADDR, SI7210_REG_OTP_CTRL),
};

static const struct regmap_access_table si7210_readable_regs = {
	.yes_ranges = si7210_read_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(si7210_read_reg_ranges),
};

static const struct regmap_range si7210_write_reg_ranges[] = {
	regmap_reg_range(SI7210_REG_DSPSIGSEL, SI7210_REG_ARAUTOINC),
	regmap_reg_range(SI7210_REG_A0, SI7210_REG_A2),
	regmap_reg_range(SI7210_REG_A3, SI7210_REG_A5),
	regmap_reg_range(SI7210_REG_OTP_ADDR, SI7210_REG_OTP_CTRL),
};

static const struct regmap_access_table si7210_writeable_regs = {
	.yes_ranges = si7210_write_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(si7210_write_reg_ranges),
};

static const struct regmap_range si7210_volatile_reg_ranges[] = {
	regmap_reg_range(SI7210_REG_DSPSIGM, SI7210_REG_DSPSIGL),
	regmap_reg_range(SI7210_REG_POWER_CTRL, SI7210_REG_POWER_CTRL),
};

static const struct regmap_access_table si7210_volatile_regs = {
	.yes_ranges = si7210_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(si7210_volatile_reg_ranges),
};

static const struct regmap_config si7210_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SI7210_REG_OTP_CTRL,

	.rd_table = &si7210_readable_regs,
	.wr_table = &si7210_writeable_regs,
	.volatile_table = &si7210_volatile_regs,
};

struct si7210_data {
	struct regmap *regmap;
	struct i2c_client *client;
	struct regulator *vdd;
	struct mutex fetch_lock; /* lock for a single measurement fetch */
	s8 temp_offset;
	s8 temp_gain;
	s8 scale_20_a[A_REGS_COUNT];
	s8 scale_200_a[A_REGS_COUNT];
	u8 curr_scale;
};

static const struct iio_chan_spec si7210_channels[] = {
	{
		.type = IIO_MAGN,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET),
	}, {
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	},
};

static int si7210_fetch_measurement(struct si7210_data *data,
				    struct iio_chan_spec const *chan,
				    u16 *buf)
{
	u8 dspsigsel = chan->type == IIO_MAGN ? 0 : 1;
	int ret;
	__be16 result;

	guard(mutex)(&data->fetch_lock);

	ret = regmap_update_bits(data->regmap, SI7210_REG_DSPSIGSEL,
				 SI7210_MASK_DSPSIGSEL, dspsigsel);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, SI7210_REG_POWER_CTRL,
				 SI7210_MASK_ONEBURST | SI7210_MASK_STOP,
				 SI7210_MASK_ONEBURST & ~SI7210_MASK_STOP);
	if (ret)
		return ret;

	/*
	 * Read the contents of the
	 * registers containing the result: DSPSIGM, DSPSIGL
	 */
	ret = regmap_bulk_read(data->regmap, SI7210_REG_DSPSIGM,
			       &result, sizeof(result));
	if (ret)
		return ret;

	*buf = be16_to_cpu(result);

	return 0;
}

static int si7210_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct si7210_data *data = iio_priv(indio_dev);
	long long temp;
	u16 dspsig;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = si7210_fetch_measurement(data, chan, &dspsig);
		if (ret)
			return ret;

		*val = dspsig & GENMASK(14, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		if (data->curr_scale == 20)
			*val2 = 12500;
		else /* data->curr_scale == 200 */
			*val2 = 125000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OFFSET:
		*val = -16384;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		ret = si7210_fetch_measurement(data, chan, &dspsig);
		if (ret)
			return ret;

		/* temp = 32 * Dspsigm[6:0] + (Dspsigl[7:0] >> 3) */
		temp = FIELD_GET(GENMASK(14, 3), dspsig);
		temp = div_s64(-383 * temp * temp, 100) + 160940 * temp - 279800000;
		temp *= (1 + (data->temp_gain / 2048));
		temp += (int)(MICRO / 16) * data->temp_offset;

		ret = regulator_get_voltage(data->vdd);
		if (ret < 0)
			return ret;

		/* temp -= 0.222 * VDD */
		temp -= 222 * div_s64(ret, MILLI);

		*val = div_s64(temp, MILLI);

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int si7210_set_scale(struct si7210_data *data, unsigned int scale)
{
	s8 *a_otp_values;
	int ret;

	if (scale == 20)
		a_otp_values = data->scale_20_a;
	else if (scale == 200)
		a_otp_values = data->scale_200_a;
	else
		return -EINVAL;

	guard(mutex)(&data->fetch_lock);

	/* Write the registers 0xCA - 0xCC */
	ret = regmap_bulk_write(data->regmap, SI7210_REG_A0, a_otp_values, 3);
	if (ret)
		return ret;

	/* Write the registers 0xCE - 0xD0 */
	ret = regmap_bulk_write(data->regmap, SI7210_REG_A3, &a_otp_values[3], 3);
	if (ret)
		return ret;

	data->curr_scale = scale;

	return 0;
}

static int si7210_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct si7210_data *data = iio_priv(indio_dev);
	unsigned int scale;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val == 0 && val2 == 12500)
			scale = 20;
		else if (val == 0 && val2 == 125000)
			scale = 200;
		else
			return -EINVAL;

		return si7210_set_scale(data, scale);
	default:
		return -EINVAL;
	}
}

static int si7210_read_otpreg_val(struct si7210_data *data, unsigned int otpreg, u8 *val)
{
	int ret;
	unsigned int otpdata;

	ret = regmap_write(data->regmap, SI7210_REG_OTP_ADDR, otpreg);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, SI7210_REG_OTP_CTRL,
				 SI7210_MASK_OTP_READ_EN, SI7210_MASK_OTP_READ_EN);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, SI7210_REG_OTP_DATA, &otpdata);
	if (ret)
		return ret;

	*val = otpdata;

	return 0;
}

/*
 * According to the datasheet, the primary method to wake up a
 * device is to send an empty write. However this is not feasible
 * using the current API so we use the other method i.e. read a single
 * byte. The device should respond with 0xFF.
 */
static int si7210_device_wake(struct si7210_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte(data->client);
	if (ret < 0)
		return ret;

	if (ret != 0xFF)
		return -EIO;

	return 0;
}

static int si7210_device_init(struct si7210_data *data)
{
	int ret;
	unsigned int i;

	ret = si7210_device_wake(data);
	if (ret)
		return ret;

	fsleep(1000);

	ret = si7210_read_otpreg_val(data, SI7210_OTPREG_TMP_GAIN, &data->temp_gain);
	if (ret)
		return ret;

	ret = si7210_read_otpreg_val(data, SI7210_OTPREG_TMP_OFF, &data->temp_offset);
	if (ret)
		return ret;

	for (i = 0; i < A_REGS_COUNT; i++) {
		ret = si7210_read_otpreg_val(data, a20_otp_regs[i], &data->scale_20_a[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < A_REGS_COUNT; i++) {
		ret = si7210_read_otpreg_val(data, a200_otp_regs[i], &data->scale_200_a[i]);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(data->regmap, SI7210_REG_ARAUTOINC,
				 SI7210_MASK_ARAUTOINC, SI7210_MASK_ARAUTOINC);
	if (ret)
		return ret;

	return si7210_set_scale(data, 20);
}

static const struct iio_info si7210_info = {
	.read_raw = si7210_read_raw,
	.write_raw = si7210_write_raw,
};

static int si7210_probe(struct i2c_client *client)
{
	struct si7210_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	ret = devm_mutex_init(&client->dev, &data->fetch_lock);
	if (ret)
		return ret;

	data->regmap = devm_regmap_init_i2c(client, &si7210_regmap_conf);
	if (IS_ERR(data->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(data->regmap),
				     "failed to register regmap\n");

	data->vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(data->vdd))
		return dev_err_probe(&client->dev, PTR_ERR(data->vdd),
				     "failed to get VDD regulator\n");

	ret = regulator_enable(data->vdd);
	if (ret)
		return ret;

	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &si7210_info;
	indio_dev->channels = si7210_channels;
	indio_dev->num_channels = ARRAY_SIZE(si7210_channels);

	ret = si7210_device_init(data);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "device initialization failed\n");

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id si7210_id[] = {
	{ "si7210" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si7210_id);

static const struct of_device_id si7210_dt_ids[] = {
	{ .compatible = "silabs,si7210" },
	{ }
};
MODULE_DEVICE_TABLE(of, si7210_dt_ids);

static struct i2c_driver si7210_driver = {
	.driver = {
		.name = "si7210",
		.of_match_table = si7210_dt_ids,
	},
	.probe = si7210_probe,
	.id_table = si7210_id,
};
module_i2c_driver(si7210_driver);

MODULE_AUTHOR("Antoni Pokusinski <apokusinski01@gmail.com>");
MODULE_DESCRIPTION("Silicon Labs Si7210 Hall Effect sensor I2C driver");
MODULE_LICENSE("GPL");
