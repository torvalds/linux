// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sensortek STK8312 3-Axis Accelerometer
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * IIO driver for STK8312; 7-bit I2C address: 0x3D.
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define STK8312_REG_XOUT		0x00
#define STK8312_REG_YOUT		0x01
#define STK8312_REG_ZOUT		0x02
#define STK8312_REG_INTSU		0x06
#define STK8312_REG_MODE		0x07
#define STK8312_REG_SR			0x08
#define STK8312_REG_STH			0x13
#define STK8312_REG_RESET		0x20
#define STK8312_REG_AFECTRL		0x24
#define STK8312_REG_OTPADDR		0x3D
#define STK8312_REG_OTPDATA		0x3E
#define STK8312_REG_OTPCTRL		0x3F

#define STK8312_MODE_ACTIVE		BIT(0)
#define STK8312_MODE_STANDBY		0x00
#define STK8312_MODE_INT_AH_PP		0xC0	/* active-high, push-pull */
#define STK8312_DREADY_BIT		BIT(4)
#define STK8312_RNG_6G			1
#define STK8312_RNG_SHIFT		6
#define STK8312_RNG_MASK		GENMASK(7, 6)
#define STK8312_SR_MASK			GENMASK(2, 0)
#define STK8312_SR_400HZ_IDX		0
#define STK8312_ALL_CHANNEL_MASK	GENMASK(2, 0)
#define STK8312_ALL_CHANNEL_SIZE	3

#define STK8312_DRIVER_NAME		"stk8312"
#define STK8312_IRQ_NAME		"stk8312_event"

/*
 * The accelerometer has two measurement ranges:
 *
 * -6g - +6g (8-bit, signed)
 * -16g - +16g (8-bit, signed)
 *
 * scale1 = (6 + 6) * 9.81 / (2^8 - 1)     = 0.4616
 * scale2 = (16 + 16) * 9.81 / (2^8 - 1)   = 1.2311
 */
#define STK8312_SCALE_AVAIL		"0.4616 1.2311"

static const int stk8312_scale_table[][2] = {
	{0, 461600}, {1, 231100}
};

static const struct {
	int val;
	int val2;
} stk8312_samp_freq_table[] = {
	{400, 0}, {200, 0}, {100, 0}, {50, 0}, {25, 0},
	{12, 500000}, {6, 250000}, {3, 125000}
};

#define STK8312_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				    BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = index,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 8,						\
		.storagebits = 8,					\
		.endianness = IIO_CPU,					\
	},								\
}

static const struct iio_chan_spec stk8312_channels[] = {
	STK8312_ACCEL_CHANNEL(0, STK8312_REG_XOUT, X),
	STK8312_ACCEL_CHANNEL(1, STK8312_REG_YOUT, Y),
	STK8312_ACCEL_CHANNEL(2, STK8312_REG_ZOUT, Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

struct stk8312_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 range;
	u8 sample_rate_idx;
	u8 mode;
	struct iio_trigger *dready_trig;
	bool dready_trigger_on;
	/* Ensure timestamp is naturally aligned */
	struct {
		s8 chans[3];
		s64 timestamp __aligned(8);
	} scan;
};

static IIO_CONST_ATTR(in_accel_scale_available, STK8312_SCALE_AVAIL);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("3.125 6.25 12.5 25 50 100 200 400");

static struct attribute *stk8312_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group stk8312_attribute_group = {
	.attrs = stk8312_attributes
};

static int stk8312_otp_init(struct stk8312_data *data)
{
	int ret;
	int count = 10;
	struct i2c_client *client = data->client;

	ret = i2c_smbus_write_byte_data(client, STK8312_REG_OTPADDR, 0x70);
	if (ret < 0)
		goto exit_err;
	ret = i2c_smbus_write_byte_data(client, STK8312_REG_OTPCTRL, 0x02);
	if (ret < 0)
		goto exit_err;

	do {
		usleep_range(1000, 5000);
		ret = i2c_smbus_read_byte_data(client, STK8312_REG_OTPCTRL);
		if (ret < 0)
			goto exit_err;
		count--;
	} while (!(ret & BIT(7)) && count > 0);

	if (count == 0) {
		ret = -ETIMEDOUT;
		goto exit_err;
	}

	ret = i2c_smbus_read_byte_data(client, STK8312_REG_OTPDATA);
	if (ret == 0)
		ret = -EINVAL;
	if (ret < 0)
		goto exit_err;

	ret = i2c_smbus_write_byte_data(data->client, STK8312_REG_AFECTRL, ret);
	if (ret < 0)
		goto exit_err;
	msleep(150);

	return 0;

exit_err:
	dev_err(&client->dev, "failed to initialize sensor\n");
	return ret;
}

static int stk8312_set_mode(struct stk8312_data *data, u8 mode)
{
	int ret;
	struct i2c_client *client = data->client;

	if (mode == data->mode)
		return 0;

	ret = i2c_smbus_write_byte_data(client, STK8312_REG_MODE, mode);
	if (ret < 0) {
		dev_err(&client->dev, "failed to change sensor mode\n");
		return ret;
	}

	data->mode = mode;
	if (mode & STK8312_MODE_ACTIVE) {
		/* Need to run OTP sequence before entering active mode */
		usleep_range(1000, 5000);
		ret = stk8312_otp_init(data);
	}

	return ret;
}

static int stk8312_set_interrupts(struct stk8312_data *data, u8 int_mask)
{
	int ret;
	u8 mode;
	struct i2c_client *client = data->client;

	mode = data->mode;
	/* We need to go in standby mode to modify registers */
	ret = stk8312_set_mode(data, STK8312_MODE_STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, STK8312_REG_INTSU, int_mask);
	if (ret < 0) {
		dev_err(&client->dev, "failed to set interrupts\n");
		stk8312_set_mode(data, mode);
		return ret;
	}

	return stk8312_set_mode(data, mode);
}

static int stk8312_data_rdy_trigger_set_state(struct iio_trigger *trig,
					      bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct stk8312_data *data = iio_priv(indio_dev);
	int ret;

	if (state)
		ret = stk8312_set_interrupts(data, STK8312_DREADY_BIT);
	else
		ret = stk8312_set_interrupts(data, 0x00);

	if (ret < 0) {
		dev_err(&data->client->dev, "failed to set trigger state\n");
		return ret;
	}

	data->dready_trigger_on = state;

	return 0;
}

static const struct iio_trigger_ops stk8312_trigger_ops = {
	.set_trigger_state = stk8312_data_rdy_trigger_set_state,
};

static int stk8312_set_sample_rate(struct stk8312_data *data, u8 rate)
{
	int ret;
	u8 masked_reg;
	u8 mode;
	struct i2c_client *client = data->client;

	if (rate == data->sample_rate_idx)
		return 0;

	mode = data->mode;
	/* We need to go in standby mode to modify registers */
	ret = stk8312_set_mode(data, STK8312_MODE_STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, STK8312_REG_SR);
	if (ret < 0)
		goto err_activate;

	masked_reg = (ret & (~STK8312_SR_MASK)) | rate;

	ret = i2c_smbus_write_byte_data(client, STK8312_REG_SR, masked_reg);
	if (ret < 0)
		goto err_activate;

	data->sample_rate_idx = rate;

	return stk8312_set_mode(data, mode);

err_activate:
	dev_err(&client->dev, "failed to set sampling rate\n");
	stk8312_set_mode(data, mode);

	return ret;
}

static int stk8312_set_range(struct stk8312_data *data, u8 range)
{
	int ret;
	u8 masked_reg;
	u8 mode;
	struct i2c_client *client = data->client;

	if (range != 1 && range != 2)
		return -EINVAL;
	else if (range == data->range)
		return 0;

	mode = data->mode;
	/* We need to go in standby mode to modify registers */
	ret = stk8312_set_mode(data, STK8312_MODE_STANDBY);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, STK8312_REG_STH);
	if (ret < 0)
		goto err_activate;

	masked_reg = ret & (~STK8312_RNG_MASK);
	masked_reg |= range << STK8312_RNG_SHIFT;

	ret = i2c_smbus_write_byte_data(client, STK8312_REG_STH, masked_reg);
	if (ret < 0)
		goto err_activate;

	data->range = range;

	return stk8312_set_mode(data, mode);

err_activate:
	dev_err(&client->dev, "failed to change sensor range\n");
	stk8312_set_mode(data, mode);

	return ret;
}

static int stk8312_read_accel(struct stk8312_data *data, u8 address)
{
	int ret;
	struct i2c_client *client = data->client;

	if (address > 2)
		return -EINVAL;

	ret = i2c_smbus_read_byte_data(client, address);
	if (ret < 0)
		dev_err(&client->dev, "register read failed\n");

	return ret;
}

static int stk8312_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct stk8312_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;
		mutex_lock(&data->lock);
		ret = stk8312_set_mode(data, data->mode | STK8312_MODE_ACTIVE);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			return ret;
		}
		ret = stk8312_read_accel(data, chan->address);
		if (ret < 0) {
			stk8312_set_mode(data,
					 data->mode & (~STK8312_MODE_ACTIVE));
			mutex_unlock(&data->lock);
			return ret;
		}
		*val = sign_extend32(ret, chan->scan_type.realbits - 1);
		ret = stk8312_set_mode(data,
				       data->mode & (~STK8312_MODE_ACTIVE));
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = stk8312_scale_table[data->range - 1][0];
		*val2 = stk8312_scale_table[data->range - 1][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = stk8312_samp_freq_table[data->sample_rate_idx].val;
		*val2 = stk8312_samp_freq_table[data->sample_rate_idx].val2;
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static int stk8312_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	int i;
	int index = -1;
	int ret;
	struct stk8312_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ARRAY_SIZE(stk8312_scale_table); i++)
			if (val == stk8312_scale_table[i][0] &&
			    val2 == stk8312_scale_table[i][1]) {
				index = i + 1;
				break;
			}
		if (index < 0)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = stk8312_set_range(data, index);
		mutex_unlock(&data->lock);

		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < ARRAY_SIZE(stk8312_samp_freq_table); i++)
			if (val == stk8312_samp_freq_table[i].val &&
			    val2 == stk8312_samp_freq_table[i].val2) {
				index = i;
				break;
			}
		if (index < 0)
			return -EINVAL;
		mutex_lock(&data->lock);
		ret = stk8312_set_sample_rate(data, index);
		mutex_unlock(&data->lock);

		return ret;
	}

	return -EINVAL;
}

static const struct iio_info stk8312_info = {
	.read_raw		= stk8312_read_raw,
	.write_raw		= stk8312_write_raw,
	.attrs			= &stk8312_attribute_group,
};

static irqreturn_t stk8312_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct stk8312_data *data = iio_priv(indio_dev);
	int bit, ret, i = 0;

	mutex_lock(&data->lock);
	/*
	 * Do a bulk read if all channels are requested,
	 * from 0x00 (XOUT) to 0x02 (ZOUT)
	 */
	if (*(indio_dev->active_scan_mask) == STK8312_ALL_CHANNEL_MASK) {
		ret = i2c_smbus_read_i2c_block_data(data->client,
						    STK8312_REG_XOUT,
						    STK8312_ALL_CHANNEL_SIZE,
						    data->scan.chans);
		if (ret < STK8312_ALL_CHANNEL_SIZE) {
			dev_err(&data->client->dev, "register read failed\n");
			mutex_unlock(&data->lock);
			goto err;
		}
	} else {
		for_each_set_bit(bit, indio_dev->active_scan_mask,
				 indio_dev->masklength) {
			ret = stk8312_read_accel(data, bit);
			if (ret < 0) {
				mutex_unlock(&data->lock);
				goto err;
			}
			data->scan.chans[i++] = ret;
		}
	}
	mutex_unlock(&data->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   pf->timestamp);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t stk8312_data_rdy_trig_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct stk8312_data *data = iio_priv(indio_dev);

	if (data->dready_trigger_on)
		iio_trigger_poll(data->dready_trig);

	return IRQ_HANDLED;
}

static int stk8312_buffer_preenable(struct iio_dev *indio_dev)
{
	struct stk8312_data *data = iio_priv(indio_dev);

	return stk8312_set_mode(data, data->mode | STK8312_MODE_ACTIVE);
}

static int stk8312_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct stk8312_data *data = iio_priv(indio_dev);

	return stk8312_set_mode(data, data->mode & (~STK8312_MODE_ACTIVE));
}

static const struct iio_buffer_setup_ops stk8312_buffer_setup_ops = {
	.preenable   = stk8312_buffer_preenable,
	.postdisable = stk8312_buffer_postdisable,
};

static int stk8312_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct stk8312_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);
	mutex_init(&data->lock);

	indio_dev->info = &stk8312_info;
	indio_dev->name = STK8312_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = stk8312_channels;
	indio_dev->num_channels = ARRAY_SIZE(stk8312_channels);

	/* A software reset is recommended at power-on */
	ret = i2c_smbus_write_byte_data(data->client, STK8312_REG_RESET, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to reset sensor\n");
		return ret;
	}
	data->sample_rate_idx = STK8312_SR_400HZ_IDX;
	ret = stk8312_set_range(data, STK8312_RNG_6G);
	if (ret < 0)
		return ret;

	ret = stk8312_set_mode(data,
			       STK8312_MODE_INT_AH_PP | STK8312_MODE_ACTIVE);
	if (ret < 0)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						stk8312_data_rdy_trig_poll,
						NULL,
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT,
						STK8312_IRQ_NAME,
						indio_dev);
		if (ret < 0) {
			dev_err(&client->dev, "request irq %d failed\n",
				client->irq);
			goto err_power_off;
		}

		data->dready_trig = devm_iio_trigger_alloc(&client->dev,
							   "%s-dev%d",
							   indio_dev->name,
							   iio_device_id(indio_dev));
		if (!data->dready_trig) {
			ret = -ENOMEM;
			goto err_power_off;
		}

		data->dready_trig->ops = &stk8312_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		ret = iio_trigger_register(data->dready_trig);
		if (ret) {
			dev_err(&client->dev, "iio trigger register failed\n");
			goto err_power_off;
		}
	}

	ret = iio_triggered_buffer_setup(indio_dev,
					 iio_pollfunc_store_time,
					 stk8312_trigger_handler,
					 &stk8312_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		goto err_trigger_unregister;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		goto err_buffer_cleanup;
	}

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_trigger_unregister:
	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);
err_power_off:
	stk8312_set_mode(data, STK8312_MODE_STANDBY);
	return ret;
}

static void stk8312_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct stk8312_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);

	stk8312_set_mode(data, STK8312_MODE_STANDBY);
}

static int stk8312_suspend(struct device *dev)
{
	struct stk8312_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return stk8312_set_mode(data, data->mode & (~STK8312_MODE_ACTIVE));
}

static int stk8312_resume(struct device *dev)
{
	struct stk8312_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return stk8312_set_mode(data, data->mode | STK8312_MODE_ACTIVE);
}

static DEFINE_SIMPLE_DEV_PM_OPS(stk8312_pm_ops, stk8312_suspend,
				stk8312_resume);

static const struct i2c_device_id stk8312_i2c_id[] = {
	/* Deprecated in favour of lowercase form */
	{ "STK8312", 0 },
	{ "stk8312", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, stk8312_i2c_id);

static struct i2c_driver stk8312_driver = {
	.driver = {
		.name = STK8312_DRIVER_NAME,
		.pm = pm_sleep_ptr(&stk8312_pm_ops),
	},
	.probe =            stk8312_probe,
	.remove =           stk8312_remove,
	.id_table =         stk8312_i2c_id,
};

module_i2c_driver(stk8312_driver);

MODULE_AUTHOR("Tiberiu Breana <tiberiu.a.breana@intel.com>");
MODULE_DESCRIPTION("STK8312 3-Axis Accelerometer driver");
MODULE_LICENSE("GPL v2");
