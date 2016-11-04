/*
 * TI ADC081C/ADC101C/ADC121C 8/10/12-bit ADC driver
 *
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2016 Intel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheets:
 *	http://www.ti.com/lit/ds/symlink/adc081c021.pdf
 *	http://www.ti.com/lit/ds/symlink/adc101c021.pdf
 *	http://www.ti.com/lit/ds/symlink/adc121c021.pdf
 *
 * The devices have a very similar interface and differ mostly in the number of
 * bits handled. For the 8-bit and 10-bit models the least-significant 4 or 2
 * bits of value registers are reserved.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/acpi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regulator/consumer.h>

struct adc081c {
	struct i2c_client *i2c;
	struct regulator *ref;

	/* 8, 10 or 12 */
	int bits;
};

#define REG_CONV_RES 0x00

static int adc081c_read_raw(struct iio_dev *iio,
			    struct iio_chan_spec const *channel, int *value,
			    int *shift, long mask)
{
	struct adc081c *adc = iio_priv(iio);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = i2c_smbus_read_word_swapped(adc->i2c, REG_CONV_RES);
		if (err < 0)
			return err;

		*value = (err & 0xFFF) >> (12 - adc->bits);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		err = regulator_get_voltage(adc->ref);
		if (err < 0)
			return err;

		*value = err / 1000;
		*shift = adc->bits;

		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		break;
	}

	return -EINVAL;
}

#define ADCxx1C_CHAN(_bits) {					\
	.type = IIO_VOLTAGE,					\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = (_bits),				\
		.storagebits = 16,				\
		.shift = 12 - (_bits),				\
		.endianness = IIO_CPU,				\
	},							\
}

#define DEFINE_ADCxx1C_CHANNELS(_name, _bits)				\
	static const struct iio_chan_spec _name ## _channels[] = {	\
		ADCxx1C_CHAN((_bits)),					\
		IIO_CHAN_SOFT_TIMESTAMP(1),				\
	};								\

#define ADC081C_NUM_CHANNELS 2

struct adcxx1c_model {
	const struct iio_chan_spec* channels;
	int bits;
};

#define ADCxx1C_MODEL(_name, _bits)					\
	{								\
		.channels = _name ## _channels,				\
		.bits = (_bits),					\
	}

DEFINE_ADCxx1C_CHANNELS(adc081c,  8);
DEFINE_ADCxx1C_CHANNELS(adc101c, 10);
DEFINE_ADCxx1C_CHANNELS(adc121c, 12);

/* Model ids are indexes in _models array */
enum adcxx1c_model_id {
	ADC081C = 0,
	ADC101C = 1,
	ADC121C = 2,
};

static struct adcxx1c_model adcxx1c_models[] = {
	ADCxx1C_MODEL(adc081c,  8),
	ADCxx1C_MODEL(adc101c, 10),
	ADCxx1C_MODEL(adc121c, 12),
};

static const struct iio_info adc081c_info = {
	.read_raw = adc081c_read_raw,
	.driver_module = THIS_MODULE,
};

static irqreturn_t adc081c_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adc081c *data = iio_priv(indio_dev);
	u16 buf[8]; /* 2 bytes data + 6 bytes padding + 8 bytes timestamp */
	int ret;

	ret = i2c_smbus_read_word_swapped(data->i2c, REG_CONV_RES);
	if (ret < 0)
		goto out;
	buf[0] = ret;
	iio_push_to_buffers_with_timestamp(indio_dev, buf,
					   iio_get_time_ns(indio_dev));
out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int adc081c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *iio;
	struct adc081c *adc;
	struct adcxx1c_model *model;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	if (ACPI_COMPANION(&client->dev)) {
		const struct acpi_device_id *ad_id;

		ad_id = acpi_match_device(client->dev.driver->acpi_match_table,
					  &client->dev);
		if (!ad_id)
			return -ENODEV;
		model = &adcxx1c_models[ad_id->driver_data];
	} else {
		model = &adcxx1c_models[id->driver_data];
	}

	iio = devm_iio_device_alloc(&client->dev, sizeof(*adc));
	if (!iio)
		return -ENOMEM;

	adc = iio_priv(iio);
	adc->i2c = client;
	adc->bits = model->bits;

	adc->ref = devm_regulator_get(&client->dev, "vref");
	if (IS_ERR(adc->ref))
		return PTR_ERR(adc->ref);

	err = regulator_enable(adc->ref);
	if (err < 0)
		return err;

	iio->dev.parent = &client->dev;
	iio->dev.of_node = client->dev.of_node;
	iio->name = dev_name(&client->dev);
	iio->modes = INDIO_DIRECT_MODE;
	iio->info = &adc081c_info;

	iio->channels = model->channels;
	iio->num_channels = ADC081C_NUM_CHANNELS;

	err = iio_triggered_buffer_setup(iio, NULL, adc081c_trigger_handler, NULL);
	if (err < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		goto err_regulator_disable;
	}

	err = iio_device_register(iio);
	if (err < 0)
		goto err_buffer_cleanup;

	i2c_set_clientdata(client, iio);

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(iio);
err_regulator_disable:
	regulator_disable(adc->ref);

	return err;
}

static int adc081c_remove(struct i2c_client *client)
{
	struct iio_dev *iio = i2c_get_clientdata(client);
	struct adc081c *adc = iio_priv(iio);

	iio_device_unregister(iio);
	iio_triggered_buffer_cleanup(iio);
	regulator_disable(adc->ref);

	return 0;
}

static const struct i2c_device_id adc081c_id[] = {
	{ "adc081c", ADC081C },
	{ "adc101c", ADC101C },
	{ "adc121c", ADC121C },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adc081c_id);

#ifdef CONFIG_OF
static const struct of_device_id adc081c_of_match[] = {
	{ .compatible = "ti,adc081c" },
	{ .compatible = "ti,adc101c" },
	{ .compatible = "ti,adc121c" },
	{ }
};
MODULE_DEVICE_TABLE(of, adc081c_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id adc081c_acpi_match[] = {
	{ "ADC081C", ADC081C },
	{ "ADC101C", ADC101C },
	{ "ADC121C", ADC121C },
	{ }
};
MODULE_DEVICE_TABLE(acpi, adc081c_acpi_match);
#endif

static struct i2c_driver adc081c_driver = {
	.driver = {
		.name = "adc081c",
		.of_match_table = of_match_ptr(adc081c_of_match),
		.acpi_match_table = ACPI_PTR(adc081c_acpi_match),
	},
	.probe = adc081c_probe,
	.remove = adc081c_remove,
	.id_table = adc081c_id,
};
module_i2c_driver(adc081c_driver);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("Texas Instruments ADC081C/ADC101C/ADC121C driver");
MODULE_LICENSE("GPL v2");
