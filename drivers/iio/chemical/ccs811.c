// SPDX-License-Identifier: GPL-2.0-only
/*
 * ccs811.c - Support for AMS CCS811 VOC Sensor
 *
 * Copyright (C) 2017 Narcisa Vasile <narcisaanamaria12@gmail.com>
 *
 * Datasheet: ams.com/content/download/951091/2269479/CCS811_DS000459_3-00.pdf
 *
 * IIO driver for AMS CCS811 (I2C address 0x5A/0x5B set by ADDR Low/High)
 *
 * TODO:
 * 1. Make the drive mode selectable form userspace
 * 2. Add support for interrupts
 * 3. Adjust time to wait for data to be ready based on selected operation mode
 * 4. Read error register and put the information in logs
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/module.h>

#define CCS811_STATUS		0x00
#define CCS811_MEAS_MODE	0x01
#define CCS811_ALG_RESULT_DATA	0x02
#define CCS811_RAW_DATA		0x03
#define CCS811_HW_ID		0x20
#define CCS811_HW_ID_VALUE	0x81
#define CCS811_HW_VERSION	0x21
#define CCS811_HW_VERSION_VALUE	0x10
#define CCS811_HW_VERSION_MASK	0xF0
#define CCS811_ERR		0xE0
/* Used to transition from boot to application mode */
#define CCS811_APP_START	0xF4
#define CCS811_SW_RESET		0xFF

/* Status register flags */
#define CCS811_STATUS_ERROR		BIT(0)
#define CCS811_STATUS_DATA_READY	BIT(3)
#define CCS811_STATUS_APP_VALID_MASK	BIT(4)
#define CCS811_STATUS_APP_VALID_LOADED	BIT(4)
/*
 * Value of FW_MODE bit of STATUS register describes the sensor's state:
 * 0: Firmware is in boot mode, this allows new firmware to be loaded
 * 1: Firmware is in application mode. CCS811 is ready to take ADC measurements
 */
#define CCS811_STATUS_FW_MODE_MASK	BIT(7)
#define CCS811_STATUS_FW_MODE_APPLICATION	BIT(7)

/* Measurement modes */
#define CCS811_MODE_IDLE	0x00
#define CCS811_MODE_IAQ_1SEC	0x10
#define CCS811_MODE_IAQ_10SEC	0x20
#define CCS811_MODE_IAQ_60SEC	0x30
#define CCS811_MODE_RAW_DATA	0x40

#define CCS811_MEAS_MODE_INTERRUPT	BIT(3)

#define CCS811_VOLTAGE_MASK	0x3FF

struct ccs811_reading {
	__be16 co2;
	__be16 voc;
	u8 status;
	u8 error;
	__be16 raw_data;
} __attribute__((__packed__));

struct ccs811_data {
	struct i2c_client *client;
	struct mutex lock; /* Protect readings */
	struct ccs811_reading buffer;
	struct iio_trigger *drdy_trig;
	struct gpio_desc *wakeup_gpio;
	bool drdy_trig_on;
};

static const struct iio_chan_spec ccs811_channels[] = {
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = -1,
	}, {
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = -1,
	}, {
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.modified = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	}, {
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_VOC,
		.modified = 1,
		.info_mask_separate =  BIT(IIO_CHAN_INFO_RAW) |
				       BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

/*
 * The CCS811 powers-up in boot mode. A setup write to CCS811_APP_START will
 * transition the sensor to application mode.
 */
static int ccs811_start_sensor_application(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, CCS811_STATUS);
	if (ret < 0)
		return ret;

	if ((ret & CCS811_STATUS_FW_MODE_APPLICATION))
		return 0;

	if ((ret & CCS811_STATUS_APP_VALID_MASK) !=
	    CCS811_STATUS_APP_VALID_LOADED)
		return -EIO;

	ret = i2c_smbus_write_byte(client, CCS811_APP_START);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, CCS811_STATUS);
	if (ret < 0)
		return ret;

	if ((ret & CCS811_STATUS_FW_MODE_MASK) !=
	    CCS811_STATUS_FW_MODE_APPLICATION) {
		dev_err(&client->dev, "Application failed to start. Sensor is still in boot mode.\n");
		return -EIO;
	}

	return 0;
}

static int ccs811_setup(struct i2c_client *client)
{
	int ret;

	ret = ccs811_start_sensor_application(client);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client, CCS811_MEAS_MODE,
					 CCS811_MODE_IAQ_1SEC);
}

static void ccs811_set_wakeup(struct ccs811_data *data, bool enable)
{
	if (!data->wakeup_gpio)
		return;

	gpiod_set_value(data->wakeup_gpio, enable);

	if (enable)
		usleep_range(50, 60);
	else
		usleep_range(20, 30);
}

static int ccs811_get_measurement(struct ccs811_data *data)
{
	int ret, tries = 11;

	ccs811_set_wakeup(data, true);

	/* Maximum waiting time: 1s, as measurements are made every second */
	while (tries-- > 0) {
		ret = i2c_smbus_read_byte_data(data->client, CCS811_STATUS);
		if (ret < 0)
			return ret;

		if ((ret & CCS811_STATUS_DATA_READY) || tries == 0)
			break;
		msleep(100);
	}
	if (!(ret & CCS811_STATUS_DATA_READY))
		return -EIO;

	ret = i2c_smbus_read_i2c_block_data(data->client,
					    CCS811_ALG_RESULT_DATA, 8,
					    (char *)&data->buffer);
	ccs811_set_wakeup(data, false);

	return ret;
}

static int ccs811_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ccs811_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		mutex_lock(&data->lock);
		ret = ccs811_get_measurement(data);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}

		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = be16_to_cpu(data->buffer.raw_data) &
					   CCS811_VOLTAGE_MASK;
			ret = IIO_VAL_INT;
			break;
		case IIO_CURRENT:
			*val = be16_to_cpu(data->buffer.raw_data) >> 10;
			ret = IIO_VAL_INT;
			break;
		case IIO_CONCENTRATION:
			switch (chan->channel2) {
			case IIO_MOD_CO2:
				*val = be16_to_cpu(data->buffer.co2);
				ret =  IIO_VAL_INT;
				break;
			case IIO_MOD_VOC:
				*val = be16_to_cpu(data->buffer.voc);
				ret = IIO_VAL_INT;
				break;
			default:
				ret = -EINVAL;
			}
			break;
		default:
			ret = -EINVAL;
		}
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);

		return ret;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = 1;
			*val2 = 612903;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_CURRENT:
			*val = 0;
			*val2 = 1000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_CONCENTRATION:
			switch (chan->channel2) {
			case IIO_MOD_CO2:
				*val = 0;
				*val2 = 100;
				return IIO_VAL_INT_PLUS_MICRO;
			case IIO_MOD_VOC:
				*val = 0;
				*val2 = 100;
				return IIO_VAL_INT_PLUS_NANO;
			default:
				return -EINVAL;
			}
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info ccs811_info = {
	.read_raw = ccs811_read_raw,
};

static int ccs811_set_trigger_state(struct iio_trigger *trig,
				    bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct ccs811_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, CCS811_MEAS_MODE);
	if (ret < 0)
		return ret;

	if (state)
		ret |= CCS811_MEAS_MODE_INTERRUPT;
	else
		ret &= ~CCS811_MEAS_MODE_INTERRUPT;

	data->drdy_trig_on = state;

	return i2c_smbus_write_byte_data(data->client, CCS811_MEAS_MODE, ret);
}

static const struct iio_trigger_ops ccs811_trigger_ops = {
	.set_trigger_state = ccs811_set_trigger_state,
};

static irqreturn_t ccs811_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ccs811_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	s16 buf[8]; /* s16 eCO2 + s16 TVOC + padding + 8 byte timestamp */
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, CCS811_ALG_RESULT_DATA, 4,
					    (u8 *)&buf);
	if (ret != 4) {
		dev_err(&client->dev, "cannot read sensor data\n");
		goto err;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buf,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t ccs811_data_rdy_trigger_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ccs811_data *data = iio_priv(indio_dev);

	if (data->drdy_trig_on)
		iio_trigger_poll(data->drdy_trig);

	return IRQ_HANDLED;
}

static int ccs811_reset(struct i2c_client *client)
{
	struct gpio_desc *reset_gpio;
	int ret;

	reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
					     GPIOD_OUT_LOW);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	/* Try to reset using nRESET pin if available else do SW reset */
	if (reset_gpio) {
		gpiod_set_value(reset_gpio, 1);
		usleep_range(20, 30);
		gpiod_set_value(reset_gpio, 0);
	} else {
		/*
		 * As per the datasheet, this sequence of values needs to be
		 * written to the SW_RESET register for triggering the soft
		 * reset in the device and placing it in boot mode.
		 */
		static const u8 reset_seq[] = {
			0x11, 0xE5, 0x72, 0x8A,
		};

		ret = i2c_smbus_write_i2c_block_data(client, CCS811_SW_RESET,
					     sizeof(reset_seq), reset_seq);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to reset sensor\n");
			return ret;
		}
	}

	/* tSTART delay required after reset */
	usleep_range(1000, 2000);

	return 0;
}

static int ccs811_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ccs811_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WRITE_BYTE
				     | I2C_FUNC_SMBUS_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_I2C_BLOCK))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	data->wakeup_gpio = devm_gpiod_get_optional(&client->dev, "wakeup",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(data->wakeup_gpio))
		return PTR_ERR(data->wakeup_gpio);

	ccs811_set_wakeup(data, true);

	ret = ccs811_reset(client);
	if (ret) {
		ccs811_set_wakeup(data, false);
		return ret;
	}

	/* Check hardware id (should be 0x81 for this family of devices) */
	ret = i2c_smbus_read_byte_data(client, CCS811_HW_ID);
	if (ret < 0) {
		ccs811_set_wakeup(data, false);
		return ret;
	}

	if (ret != CCS811_HW_ID_VALUE) {
		dev_err(&client->dev, "hardware id doesn't match CCS81x\n");
		ccs811_set_wakeup(data, false);
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(client, CCS811_HW_VERSION);
	if (ret < 0) {
		ccs811_set_wakeup(data, false);
		return ret;
	}

	if ((ret & CCS811_HW_VERSION_MASK) != CCS811_HW_VERSION_VALUE) {
		dev_err(&client->dev, "no CCS811 sensor\n");
		ccs811_set_wakeup(data, false);
		return -ENODEV;
	}

	ret = ccs811_setup(client);
	if (ret < 0) {
		ccs811_set_wakeup(data, false);
		return ret;
	}

	ccs811_set_wakeup(data, false);

	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ccs811_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = ccs811_channels;
	indio_dev->num_channels = ARRAY_SIZE(ccs811_channels);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						ccs811_data_rdy_trigger_poll,
						NULL,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"ccs811_irq", indio_dev);
		if (ret) {
			dev_err(&client->dev, "irq request error %d\n", -ret);
			goto err_poweroff;
		}

		data->drdy_trig = devm_iio_trigger_alloc(&client->dev,
							 "%s-dev%d",
							 indio_dev->name,
							 indio_dev->id);
		if (!data->drdy_trig) {
			ret = -ENOMEM;
			goto err_poweroff;
		}

		data->drdy_trig->dev.parent = &client->dev;
		data->drdy_trig->ops = &ccs811_trigger_ops;
		iio_trigger_set_drvdata(data->drdy_trig, indio_dev);
		indio_dev->trig = data->drdy_trig;
		iio_trigger_get(indio_dev->trig);
		ret = iio_trigger_register(data->drdy_trig);
		if (ret)
			goto err_poweroff;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ccs811_trigger_handler, NULL);

	if (ret < 0) {
		dev_err(&client->dev, "triggered buffer setup failed\n");
		goto err_trigger_unregister;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "unable to register iio device\n");
		goto err_buffer_cleanup;
	}
	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_trigger_unregister:
	if (data->drdy_trig)
		iio_trigger_unregister(data->drdy_trig);
err_poweroff:
	i2c_smbus_write_byte_data(client, CCS811_MEAS_MODE, CCS811_MODE_IDLE);

	return ret;
}

static int ccs811_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ccs811_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (data->drdy_trig)
		iio_trigger_unregister(data->drdy_trig);

	return i2c_smbus_write_byte_data(client, CCS811_MEAS_MODE,
					 CCS811_MODE_IDLE);
}

static const struct i2c_device_id ccs811_id[] = {
	{"ccs811", 0},
	{	}
};
MODULE_DEVICE_TABLE(i2c, ccs811_id);

static const struct of_device_id ccs811_dt_ids[] = {
	{ .compatible = "ams,ccs811" },
	{ }
};
MODULE_DEVICE_TABLE(of, ccs811_dt_ids);

static struct i2c_driver ccs811_driver = {
	.driver = {
		.name = "ccs811",
		.of_match_table = ccs811_dt_ids,
	},
	.probe = ccs811_probe,
	.remove = ccs811_remove,
	.id_table = ccs811_id,
};
module_i2c_driver(ccs811_driver);

MODULE_AUTHOR("Narcisa Vasile <narcisaanamaria12@gmail.com>");
MODULE_DESCRIPTION("CCS811 volatile organic compounds sensor");
MODULE_LICENSE("GPL v2");
