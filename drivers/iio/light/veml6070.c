// SPDX-License-Identifier: GPL-2.0-only
/*
 * veml6070.c - Support for Vishay VEML6070 UV A light sensor
 *
 * Copyright 2016 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 *
 * IIO driver for VEML6070 (7-bit I2C slave addresses 0x38 and 0x39)
 *
 * TODO: ACK signal
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VEML6070_DRV_NAME "veml6070"

#define VEML6070_ADDR_CONFIG_DATA_MSB 0x38 /* read: MSB data, write: config */
#define VEML6070_ADDR_DATA_LSB	0x39 /* LSB data */

#define VEML6070_COMMAND_ACK	BIT(5) /* raise interrupt when over threshold */
#define VEML6070_COMMAND_IT	GENMASK(3, 2) /* bit mask integration time */
#define VEML6070_COMMAND_RSRVD	BIT(1) /* reserved, set to 1 */
#define VEML6070_COMMAND_SD	BIT(0) /* shutdown mode when set */

#define VEML6070_IT_05		0x00
#define VEML6070_IT_10		0x01
#define VEML6070_IT_20		0x02
#define VEML6070_IT_40		0x03

#define VEML6070_MIN_RSET_KOHM	75
#define VEML6070_MIN_IT_US	15625 /* Rset = 75 kohm, IT = 1/2 */

struct veml6070_data {
	struct i2c_client *client1;
	struct i2c_client *client2;
	u8 config;
	struct mutex lock;
	u32 rset;
	int it[4][2];
};

static int veml6070_calc_it(struct device *dev, struct veml6070_data *data)
{
	int i, tmp_it;

	data->rset = 270000;
	device_property_read_u32(dev, "vishay,rset-ohms", &data->rset);

	if (data->rset < 75000 || data->rset > 1200000)
		return dev_err_probe(dev, -EINVAL, "Rset out of range\n");

	/*
	 * convert to kohm to avoid overflows and work with the same units as
	 * in the datasheet and simplify UVI operations.
	 */
	data->rset /= KILO;

	tmp_it = VEML6070_MIN_IT_US * data->rset / VEML6070_MIN_RSET_KOHM;
	for (i = 0; i < ARRAY_SIZE(data->it); i++) {
		data->it[i][0] = (tmp_it << i) / MICRO;
		data->it[i][1] = (tmp_it << i) % MICRO;
	}

	return 0;
}

static int veml6070_get_it(struct veml6070_data *data, int *val, int *val2)
{
	int it_idx = FIELD_GET(VEML6070_COMMAND_IT, data->config);

	*val = data->it[it_idx][0];
	*val2 = data->it[it_idx][1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6070_set_it(struct veml6070_data *data, int val, int val2)
{
	int it_idx;

	for (it_idx = 0; it_idx < ARRAY_SIZE(data->it); it_idx++) {
		if (data->it[it_idx][0] == val && data->it[it_idx][1] == val2)
			break;
	}

	if (it_idx >= ARRAY_SIZE(data->it))
		return -EINVAL;

	data->config = (data->config & ~VEML6070_COMMAND_IT) |
		FIELD_PREP(VEML6070_COMMAND_IT, it_idx);

	return i2c_smbus_write_byte(data->client1, data->config);
}

static int veml6070_read(struct veml6070_data *data)
{
	int ret, it_ms, val, val2;
	u8 msb, lsb;

	guard(mutex)(&data->lock);

	/* disable shutdown */
	ret = i2c_smbus_write_byte(data->client1,
	    data->config & ~VEML6070_COMMAND_SD);
	if (ret < 0)
		return ret;

	veml6070_get_it(data, &val, &val2);
	it_ms = val * MILLI + val2 / (MICRO / MILLI);
	msleep(it_ms + 10);

	ret = i2c_smbus_read_byte(data->client2); /* read MSB, address 0x39 */
	if (ret < 0)
		return ret;

	msb = ret;

	ret = i2c_smbus_read_byte(data->client1); /* read LSB, address 0x38 */
	if (ret < 0)
		return ret;

	lsb = ret;

	/* shutdown again */
	ret = i2c_smbus_write_byte(data->client1, data->config);
	if (ret < 0)
		return ret;

	ret = (msb << 8) | lsb;

	return 0;
}

static const struct iio_chan_spec veml6070_channels[] = {
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_UV,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	},
	{
		.type = IIO_UVINDEX,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_INT_TIME),
		.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_INT_TIME),
	}
};

static int veml6070_to_uv_index(struct veml6070_data *data, unsigned int val)
{
	/*
	 * conversion of raw UV intensity values to UV index depends on
	 * integration time (IT) and value of the resistor connected to
	 * the RSET pin.
	 */
	unsigned int uvi[11] = {
		187, 373, 560, /* low */
		746, 933, 1120, /* moderate */
		1308, 1494, /* high */
		1681, 1868, 2054}; /* very high */
	int i, it_idx;

	it_idx = FIELD_GET(VEML6070_COMMAND_IT, data->config);

	if (!it_idx)
		val = (val * 270  / data->rset) << 1;
	else
		val = (val * 270 / data->rset) >> (it_idx - 1);

	for (i = 0; i < ARRAY_SIZE(uvi); i++)
		if (val <= uvi[i])
			return i;

	return 11; /* extreme */
}

static int veml6070_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct veml6070_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		ret = veml6070_read(data);
		if (ret < 0)
			return ret;
		if (mask == IIO_CHAN_INFO_PROCESSED)
			*val = veml6070_to_uv_index(data, ret);
		else
			*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		return veml6070_get_it(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6070_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	struct veml6070_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		*vals = (int *)data->it;
		*length = 2 * ARRAY_SIZE(data->it);
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int veml6070_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct veml6070_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml6070_set_it(data, val, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info veml6070_info = {
	.read_raw = veml6070_read_raw,
	.read_avail  = veml6070_read_avail,
	.write_raw = veml6070_write_raw,
};

static void veml6070_i2c_unreg(void *p)
{
	struct veml6070_data *data = p;

	i2c_unregister_device(data->client2);
}

static int veml6070_probe(struct i2c_client *client)
{
	struct veml6070_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client1 = client;
	mutex_init(&data->lock);

	indio_dev->info = &veml6070_info;
	indio_dev->channels = veml6070_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml6070_channels);
	indio_dev->name = VEML6070_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = veml6070_calc_it(&client->dev, data);
	if (ret < 0)
		return ret;

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret < 0)
		return ret;

	data->client2 = i2c_new_dummy_device(client->adapter, VEML6070_ADDR_DATA_LSB);
	if (IS_ERR(data->client2))
		return dev_err_probe(&client->dev, PTR_ERR(data->client2),
				     "i2c device for second chip address failed\n");

	data->config = FIELD_PREP(VEML6070_COMMAND_IT, VEML6070_IT_10) |
		VEML6070_COMMAND_RSRVD | VEML6070_COMMAND_SD;
	ret = i2c_smbus_write_byte(data->client1, data->config);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&client->dev, veml6070_i2c_unreg, data);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id veml6070_id[] = {
	{ "veml6070" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6070_id);

static const struct of_device_id veml6070_of_match[] = {
	{ .compatible = "vishay,veml6070" },
	{ }
};
MODULE_DEVICE_TABLE(of, veml6070_of_match);

static struct i2c_driver veml6070_driver = {
	.driver = {
		.name   = VEML6070_DRV_NAME,
		.of_match_table = veml6070_of_match,
	},
	.probe = veml6070_probe,
	.id_table = veml6070_id,
};

module_i2c_driver(veml6070_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Vishay VEML6070 UV A light sensor driver");
MODULE_LICENSE("GPL");
