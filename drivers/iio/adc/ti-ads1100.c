// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADS1100 - Texas Instruments Analog-to-Digital Converter
 *
 * Copyright (c) 2023, Topic Embedded Products
 *
 * Datasheet: https://www.ti.com/lit/gpn/ads1100
 * IIO driver for ADS1100 and ADS1000 ADC 16-bit I2C
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

/* The ADS1100 has a single byte config register */

/* Conversion in progress bit */
#define ADS1100_CFG_ST_BSY	BIT(7)
/* Single conversion bit */
#define ADS1100_CFG_SC		BIT(4)
/* Data rate */
#define ADS1100_DR_MASK		GENMASK(3, 2)
/* Gain */
#define ADS1100_PGA_MASK	GENMASK(1, 0)

#define ADS1100_CONTINUOUS	0
#define	ADS1100_SINGLESHOT	ADS1100_CFG_SC

#define ADS1100_SLEEP_DELAY_MS	2000

static const int ads1100_data_rate[] = { 128, 32, 16, 8 };
static const int ads1100_data_rate_bits[] = { 12, 14, 15, 16 };

struct ads1100_data {
	struct i2c_client *client;
	struct regulator *reg_vdd;
	struct mutex lock;
	int scale_avail[2 * 4]; /* 4 gain settings */
	u8 config;
	bool supports_data_rate; /* Only the ADS1100 can select the rate */
};

static const struct iio_chan_spec ads1100_channel = {
	.type = IIO_VOLTAGE,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_all =
	    BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.info_mask_shared_by_all_available =
	    BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		      .sign = 's',
		      .realbits = 16,
		      .storagebits = 16,
		      .endianness = IIO_CPU,
		       },
	.datasheet_name = "AIN",
};

static int ads1100_set_config_bits(struct ads1100_data *data, u8 mask, u8 value)
{
	int ret;
	u8 config = (data->config & ~mask) | (value & mask);

	if (data->config == config)
		return 0;	/* Already done */

	ret = i2c_master_send(data->client, &config, 1);
	if (ret < 0)
		return ret;

	data->config = config;

	return 0;
};

static int ads1100_data_bits(struct ads1100_data *data)
{
	return ads1100_data_rate_bits[FIELD_GET(ADS1100_DR_MASK, data->config)];
}

static int ads1100_get_adc_result(struct ads1100_data *data, int chan, int *val)
{
	int ret;
	__be16 buffer;
	s16 value;

	if (chan != 0)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(&data->client->dev);
	if (ret < 0)
		return ret;

	ret = i2c_master_recv(data->client, (char *)&buffer, sizeof(buffer));

	pm_runtime_mark_last_busy(&data->client->dev);
	pm_runtime_put_autosuspend(&data->client->dev);

	if (ret < 0) {
		dev_err(&data->client->dev, "I2C read fail: %d\n", ret);
		return ret;
	}

	/* Value is always 16-bit 2's complement */
	value = be16_to_cpu(buffer);

	/* Shift result to compensate for bit resolution vs. sample rate */
	value <<= 16 - ads1100_data_bits(data);

	*val = sign_extend32(value, 15);

	return 0;
}

static int ads1100_set_scale(struct ads1100_data *data, int val, int val2)
{
	int microvolts;
	int gain;

	/* With Vdd between 2.7 and 5V, the scale is always below 1 */
	if (val)
		return -EINVAL;

	if (!val2)
		return -EINVAL;

	microvolts = regulator_get_voltage(data->reg_vdd);
	/*
	 * val2 is in 'micro' units, n = val2 / 1000000
	 * result must be millivolts, d = microvolts / 1000
	 * the full-scale value is d/n, corresponds to 2^15,
	 * hence the gain = (d / n) >> 15, factoring out the 1000 and moving the
	 * bitshift so everything fits in 32-bits yields this formula.
	 */
	gain = DIV_ROUND_CLOSEST(microvolts, BIT(15)) * MILLI / val2;
	if (gain < BIT(0) || gain > BIT(3))
		return -EINVAL;

	ads1100_set_config_bits(data, ADS1100_PGA_MASK, ffs(gain) - 1);

	return 0;
}

static int ads1100_set_data_rate(struct ads1100_data *data, int chan, int rate)
{
	unsigned int i;
	unsigned int size;

	size = data->supports_data_rate ? ARRAY_SIZE(ads1100_data_rate) : 1;
	for (i = 0; i < size; i++) {
		if (ads1100_data_rate[i] == rate)
			return ads1100_set_config_bits(data, ADS1100_DR_MASK,
						       FIELD_PREP(ADS1100_DR_MASK, i));
	}

	return -EINVAL;
}

static int ads1100_get_vdd_millivolts(struct ads1100_data *data)
{
	return regulator_get_voltage(data->reg_vdd) / (MICRO / MILLI);
}

static void ads1100_calc_scale_avail(struct ads1100_data *data)
{
	int millivolts = ads1100_get_vdd_millivolts(data);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(data->scale_avail) / 2; i++) {
		data->scale_avail[i * 2 + 0] = millivolts;
		data->scale_avail[i * 2 + 1] = 15 + i;
	}
}

static int ads1100_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct ads1100_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_VOLTAGE)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = ads1100_data_rate;
		if (data->supports_data_rate)
			*length = ARRAY_SIZE(ads1100_data_rate);
		else
			*length = 1;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_FRACTIONAL_LOG2;
		*vals = data->scale_avail;
		*length = ARRAY_SIZE(data->scale_avail);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ads1100_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret;
	struct ads1100_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			break;

		ret = ads1100_get_adc_result(data, chan->address, val);
		if (ret >= 0)
			ret = IIO_VAL_INT;
		iio_device_release_direct_mode(indio_dev);
		break;
	case IIO_CHAN_INFO_SCALE:
		/* full-scale is the supply voltage in millivolts */
		*val = ads1100_get_vdd_millivolts(data);
		*val2 = 15 + FIELD_GET(ADS1100_PGA_MASK, data->config);
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = ads1100_data_rate[FIELD_GET(ADS1100_DR_MASK,
						   data->config)];
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static int ads1100_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ads1100_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = ads1100_set_scale(data, val, val2);
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = ads1100_set_data_rate(data, chan->address, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static const struct iio_info ads1100_info = {
	.read_avail = ads1100_read_avail,
	.read_raw = ads1100_read_raw,
	.write_raw = ads1100_write_raw,
};

static int ads1100_setup(struct ads1100_data *data)
{
	int ret;
	u8 buffer[3];

	/* Setup continuous sampling mode at 8sps */
	buffer[0] = ADS1100_DR_MASK | ADS1100_CONTINUOUS;
	ret = i2c_master_send(data->client, buffer, 1);
	if (ret < 0)
		return ret;

	ret = i2c_master_recv(data->client, buffer, sizeof(buffer));
	if (ret < 0)
		return ret;

	/* Config register returned in third byte, strip away the busy status */
	data->config = buffer[2] & ~ADS1100_CFG_ST_BSY;

	/* Detect the sample rate capability by checking the DR bits */
	data->supports_data_rate = FIELD_GET(ADS1100_DR_MASK, buffer[2]) != 0;

	return 0;
}

static void ads1100_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static void ads1100_disable_continuous(void *data)
{
	ads1100_set_config_bits(data, ADS1100_CFG_SC, ADS1100_SINGLESHOT);
}

static int ads1100_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct ads1100_data *data;
	struct device *dev = &client->dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, data);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->name = "ads1100";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &ads1100_channel;
	indio_dev->num_channels = 1;
	indio_dev->info = &ads1100_info;

	data->reg_vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(data->reg_vdd))
		return dev_err_probe(dev, PTR_ERR(data->reg_vdd),
				     "Failed to get vdd regulator\n");

	ret = regulator_enable(data->reg_vdd);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to enable vdd regulator\n");

	ret = devm_add_action_or_reset(dev, ads1100_reg_disable, data->reg_vdd);
	if (ret)
		return ret;

	ret = ads1100_setup(data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to communicate with device\n");

	ret = devm_add_action_or_reset(dev, ads1100_disable_continuous, data);
	if (ret)
		return ret;

	ads1100_calc_scale_avail(data);

	pm_runtime_set_autosuspend_delay(dev, ADS1100_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable pm_runtime\n");

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register IIO device\n");

	return 0;
}

static int ads1100_runtime_suspend(struct device *dev)
{
	struct ads1100_data *data = dev_get_drvdata(dev);

	ads1100_set_config_bits(data, ADS1100_CFG_SC, ADS1100_SINGLESHOT);
	regulator_disable(data->reg_vdd);

	return 0;
}

static int ads1100_runtime_resume(struct device *dev)
{
	struct ads1100_data *data = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(data->reg_vdd);
	if (ret) {
		dev_err(&data->client->dev, "Failed to enable Vdd\n");
		return ret;
	}

	/*
	 * We'll always change the mode bit in the config register, so there is
	 * no need here to "force" a write to the config register. If the device
	 * has been power-cycled, we'll re-write its config register now.
	 */
	return ads1100_set_config_bits(data, ADS1100_CFG_SC,
				       ADS1100_CONTINUOUS);
}

static DEFINE_RUNTIME_DEV_PM_OPS(ads1100_pm_ops,
				 ads1100_runtime_suspend,
				 ads1100_runtime_resume,
				 NULL);

static const struct i2c_device_id ads1100_id[] = {
	{ "ads1100" },
	{ "ads1000" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ads1100_id);

static const struct of_device_id ads1100_of_match[] = {
	{.compatible = "ti,ads1100" },
	{.compatible = "ti,ads1000" },
	{ }
};

MODULE_DEVICE_TABLE(of, ads1100_of_match);

static struct i2c_driver ads1100_driver = {
	.driver = {
		   .name = "ads1100",
		   .of_match_table = ads1100_of_match,
		   .pm = pm_ptr(&ads1100_pm_ops),
	},
	.probe_new = ads1100_probe,
	.id_table = ads1100_id,
};

module_i2c_driver(ads1100_driver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("Texas Instruments ADS1100 ADC driver");
MODULE_LICENSE("GPL");
