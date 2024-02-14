// SPDX-License-Identifier: GPL-2.0
/*
 * The LTC2309 is an 8-Channel, 12-Bit SAR ADC with an I2C Interface.
 *
 * Datasheet:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/2309fd.pdf
 *
 * Copyright (c) 2023, Liam Beguin <liambeguin@gmail.com>
 */
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>

#define LTC2309_ADC_RESOLUTION	12

#define LTC2309_DIN_CH_MASK	GENMASK(7, 4)
#define LTC2309_DIN_SDN		BIT(7)
#define LTC2309_DIN_OSN		BIT(6)
#define LTC2309_DIN_S1		BIT(5)
#define LTC2309_DIN_S0		BIT(4)
#define LTC2309_DIN_UNI		BIT(3)
#define LTC2309_DIN_SLEEP	BIT(2)

/**
 * struct ltc2309 - internal device data structure
 * @dev:	Device reference
 * @client:	I2C reference
 * @vref:	External reference source
 * @lock:	Lock to serialize data access
 * @vref_mv:	Internal voltage reference
 */
struct ltc2309 {
	struct device		*dev;
	struct i2c_client	*client;
	struct regulator	*vref;
	struct mutex		lock; /* serialize data access */
	int			vref_mv;
};

/* Order matches expected channel address, See datasheet Table 1. */
enum ltc2309_channels {
	LTC2309_CH0_CH1 = 0,
	LTC2309_CH2_CH3,
	LTC2309_CH4_CH5,
	LTC2309_CH6_CH7,
	LTC2309_CH1_CH0,
	LTC2309_CH3_CH2,
	LTC2309_CH5_CH4,
	LTC2309_CH7_CH6,
	LTC2309_CH0,
	LTC2309_CH2,
	LTC2309_CH4,
	LTC2309_CH6,
	LTC2309_CH1,
	LTC2309_CH3,
	LTC2309_CH5,
	LTC2309_CH7,
};

#define LTC2309_CHAN(_chan, _addr) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

#define LTC2309_DIFF_CHAN(_chan, _chan2, _addr) {		\
	.type = IIO_VOLTAGE,					\
	.differential = 1,					\
	.indexed = 1,						\
	.address = _addr,					\
	.channel = _chan,					\
	.channel2 = _chan2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec ltc2309_channels[] = {
	LTC2309_CHAN(0, LTC2309_CH0),
	LTC2309_CHAN(1, LTC2309_CH1),
	LTC2309_CHAN(2, LTC2309_CH2),
	LTC2309_CHAN(3, LTC2309_CH3),
	LTC2309_CHAN(4, LTC2309_CH4),
	LTC2309_CHAN(5, LTC2309_CH5),
	LTC2309_CHAN(6, LTC2309_CH6),
	LTC2309_CHAN(7, LTC2309_CH7),
	LTC2309_DIFF_CHAN(0, 1, LTC2309_CH0_CH1),
	LTC2309_DIFF_CHAN(2, 3, LTC2309_CH2_CH3),
	LTC2309_DIFF_CHAN(4, 5, LTC2309_CH4_CH5),
	LTC2309_DIFF_CHAN(6, 7, LTC2309_CH6_CH7),
	LTC2309_DIFF_CHAN(1, 0, LTC2309_CH1_CH0),
	LTC2309_DIFF_CHAN(3, 2, LTC2309_CH3_CH2),
	LTC2309_DIFF_CHAN(5, 4, LTC2309_CH5_CH4),
	LTC2309_DIFF_CHAN(7, 6, LTC2309_CH7_CH6),
};

static int ltc2309_read_raw_channel(struct ltc2309 *ltc2309,
				    unsigned long address, int *val)
{
	int ret;
	u16 buf;
	u8 din;

	din = FIELD_PREP(LTC2309_DIN_CH_MASK, address & 0x0f) |
		FIELD_PREP(LTC2309_DIN_UNI, 1) |
		FIELD_PREP(LTC2309_DIN_SLEEP, 0);

	ret = i2c_smbus_write_byte(ltc2309->client, din);
	if (ret < 0) {
		dev_err(ltc2309->dev, "i2c command failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	ret = i2c_master_recv(ltc2309->client, (char *)&buf, 2);
	if (ret < 0) {
		dev_err(ltc2309->dev, "i2c read failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	*val = be16_to_cpu(buf) >> 4;

	return ret;
}

static int ltc2309_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct ltc2309 *ltc2309 = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&ltc2309->lock);
		ret = ltc2309_read_raw_channel(ltc2309, chan->address, val);
		mutex_unlock(&ltc2309->lock);
		if (ret < 0)
			return -EINVAL;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = ltc2309->vref_mv;
		*val2 = LTC2309_ADC_RESOLUTION;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ltc2309_info = {
	.read_raw = ltc2309_read_raw,
};

static void ltc2309_regulator_disable(void *regulator)
{
	regulator_disable(regulator);
}

static int ltc2309_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct ltc2309 *ltc2309;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*ltc2309));
	if (!indio_dev)
		return -ENOMEM;

	ltc2309 = iio_priv(indio_dev);
	ltc2309->dev = &indio_dev->dev;
	ltc2309->client = client;
	ltc2309->vref_mv = 4096; /* Default to the internal ref */

	indio_dev->name = "ltc2309";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ltc2309_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltc2309_channels);
	indio_dev->info = &ltc2309_info;

	ltc2309->vref = devm_regulator_get_optional(&client->dev, "vref");
	if (IS_ERR(ltc2309->vref)) {
		ret = PTR_ERR(ltc2309->vref);
		if (ret == -ENODEV)
			ltc2309->vref = NULL;
		else
			return ret;
	}

	if (ltc2309->vref) {
		ret = regulator_enable(ltc2309->vref);
		if (ret)
			return dev_err_probe(ltc2309->dev, ret,
					     "failed to enable vref\n");

		ret = devm_add_action_or_reset(ltc2309->dev,
					       ltc2309_regulator_disable,
					       ltc2309->vref);
		if (ret) {
			return dev_err_probe(ltc2309->dev, ret,
					     "failed to add regulator_disable action: %d\n",
					     ret);
		}

		ret = regulator_get_voltage(ltc2309->vref);
		if (ret < 0)
			return ret;

		ltc2309->vref_mv = ret / 1000;
	}

	mutex_init(&ltc2309->lock);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id ltc2309_of_match[] = {
	{ .compatible = "lltc,ltc2309" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2309_of_match);

static const struct i2c_device_id ltc2309_id[] = {
	{ "ltc2309" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc2309_id);

static struct i2c_driver ltc2309_driver = {
	.driver = {
		.name = "ltc2309",
		.of_match_table = ltc2309_of_match,
	},
	.probe		= ltc2309_probe,
	.id_table	= ltc2309_id,
};
module_i2c_driver(ltc2309_driver);

MODULE_AUTHOR("Liam Beguin <liambeguin@gmail.com>");
MODULE_DESCRIPTION("Linear Technology LTC2309 ADC");
MODULE_LICENSE("GPL v2");
