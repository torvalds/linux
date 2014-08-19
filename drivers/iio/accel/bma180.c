/*
 * bma180.c - IIO driver for Bosch BMA180 triaxial acceleration sensor
 *
 * Copyright 2013 Oleksandr Kravchenko <x0199363@ti.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define BMA180_DRV_NAME "bma180"
#define BMA180_IRQ_NAME "bma180_event"

/* Register set */
#define BMA180_CHIP_ID		0x00 /* Need to distinguish BMA180 from other */
#define BMA180_ACC_X_LSB	0x02 /* First of 6 registers of accel data */
#define BMA180_CTRL_REG0	0x0d
#define BMA180_RESET		0x10
#define BMA180_BW_TCS		0x20
#define BMA180_CTRL_REG3	0x21
#define BMA180_TCO_Z		0x30
#define BMA180_OFFSET_LSB1	0x35

/* BMA180_CTRL_REG0 bits */
#define BMA180_DIS_WAKE_UP	BIT(0) /* Disable wake up mode */
#define BMA180_SLEEP		BIT(1) /* 1 - chip will sleep */
#define BMA180_EE_W		BIT(4) /* Unlock writing to addr from 0x20 */
#define BMA180_RESET_INT	BIT(6) /* Reset pending interrupts */

/* BMA180_CTRL_REG3 bits */
#define BMA180_NEW_DATA_INT	BIT(1) /* Intr every new accel data is ready */

/* BMA180_OFFSET_LSB1 skipping mode bit */
#define BMA180_SMP_SKIP		BIT(0)

/* Bit masks for registers bit fields */
#define BMA180_RANGE		0x0e /* Range of measured accel values */
#define BMA180_BW		0xf0 /* Accel bandwidth */
#define BMA180_MODE_CONFIG	0x03 /* Config operation modes */

/* We have to write this value in reset register to do soft reset */
#define BMA180_RESET_VAL	0xb6

#define BMA180_ID_REG_VAL	0x03

/* Chip power modes */
#define BMA180_LOW_NOISE	0x00
#define BMA180_LOW_POWER	0x03

#define BMA180_LOW_NOISE_STR	"low_noise"
#define BMA180_LOW_POWER_STR	"low_power"

/* Defaults values */
#define BMA180_DEF_PMODE	0
#define BMA180_DEF_BW		20
#define BMA180_DEF_SCALE	2452

/* Available values for sysfs */
#define BMA180_FLP_FREQ_AVAILABLE \
	"10 20 40 75 150 300"
#define BMA180_SCALE_AVAILABLE \
	"0.001275 0.001863 0.002452 0.003727 0.004903 0.009709 0.019417"

struct bma180_data {
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct mutex mutex;
	int sleep_state;
	int scale;
	int bw;
	int pmode;
	char *buff;
};

enum bma180_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

static int bma180_bw_table[] = { 10, 20, 40, 75, 150, 300 }; /* Hz */
static int bma180_scale_table[] = { 1275, 1863, 2452, 3727, 4903, 9709, 19417 };

static int bma180_get_acc_reg(struct bma180_data *data, enum bma180_axis axis)
{
	u8 reg = BMA180_ACC_X_LSB + axis * 2;
	int ret;

	if (data->sleep_state)
		return -EBUSY;

	ret = i2c_smbus_read_word_data(data->client, reg);
	if (ret < 0)
		dev_err(&data->client->dev,
			"failed to read accel_%c register\n", 'x' + axis);

	return ret;
}

static int bma180_set_bits(struct bma180_data *data, u8 reg, u8 mask, u8 val)
{
	int ret = i2c_smbus_read_byte_data(data->client, reg);
	u8 reg_val = (ret & ~mask) | (val << (ffs(mask) - 1));

	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(data->client, reg, reg_val);
}

static int bma180_reset_intr(struct bma180_data *data)
{
	int ret = bma180_set_bits(data, BMA180_CTRL_REG0, BMA180_RESET_INT, 1);

	if (ret)
		dev_err(&data->client->dev, "failed to reset interrupt\n");

	return ret;
}

static int bma180_set_new_data_intr_state(struct bma180_data *data, int state)
{
	u8 reg_val = state ? BMA180_NEW_DATA_INT : 0x00;
	int ret = i2c_smbus_write_byte_data(data->client, BMA180_CTRL_REG3,
			reg_val);

	if (ret)
		goto err;
	ret = bma180_reset_intr(data);
	if (ret)
		goto err;

	return 0;

err:
	dev_err(&data->client->dev,
		"failed to set new data interrupt state %d\n", state);
	return ret;
}

static int bma180_set_sleep_state(struct bma180_data *data, int state)
{
	int ret = bma180_set_bits(data, BMA180_CTRL_REG0, BMA180_SLEEP, state);

	if (ret) {
		dev_err(&data->client->dev,
			"failed to set sleep state %d\n", state);
		return ret;
	}
	data->sleep_state = state;

	return 0;
}

static int bma180_set_ee_writing_state(struct bma180_data *data, int state)
{
	int ret = bma180_set_bits(data, BMA180_CTRL_REG0, BMA180_EE_W, state);

	if (ret)
		dev_err(&data->client->dev,
			"failed to set ee writing state %d\n", state);

	return ret;
}

static int bma180_set_bw(struct bma180_data *data, int val)
{
	int ret, i;

	if (data->sleep_state)
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(bma180_bw_table); ++i) {
		if (bma180_bw_table[i] == val) {
			ret = bma180_set_bits(data,
					BMA180_BW_TCS, BMA180_BW, i);
			if (ret) {
				dev_err(&data->client->dev,
					"failed to set bandwidth\n");
				return ret;
			}
			data->bw = val;
			return 0;
		}
	}

	return -EINVAL;
}

static int bma180_set_scale(struct bma180_data *data, int val)
{
	int ret, i;

	if (data->sleep_state)
		return -EBUSY;

	for (i = 0; i < ARRAY_SIZE(bma180_scale_table); ++i)
		if (bma180_scale_table[i] == val) {
			ret = bma180_set_bits(data,
					BMA180_OFFSET_LSB1, BMA180_RANGE, i);
			if (ret) {
				dev_err(&data->client->dev,
					"failed to set scale\n");
				return ret;
			}
			data->scale = val;
			return 0;
		}

	return -EINVAL;
}

static int bma180_set_pmode(struct bma180_data *data, int mode)
{
	u8 reg_val = mode ? BMA180_LOW_POWER : BMA180_LOW_NOISE;
	int ret = bma180_set_bits(data, BMA180_TCO_Z, BMA180_MODE_CONFIG,
			reg_val);

	if (ret) {
		dev_err(&data->client->dev, "failed to set power mode\n");
		return ret;
	}
	data->pmode = mode;

	return 0;
}

static int bma180_soft_reset(struct bma180_data *data)
{
	int ret = i2c_smbus_write_byte_data(data->client,
			BMA180_RESET, BMA180_RESET_VAL);

	if (ret)
		dev_err(&data->client->dev, "failed to reset the chip\n");

	return ret;
}

static int bma180_chip_init(struct bma180_data *data)
{
	/* Try to read chip_id register. It must return 0x03. */
	int ret = i2c_smbus_read_byte_data(data->client, BMA180_CHIP_ID);

	if (ret < 0)
		goto err;
	if (ret != BMA180_ID_REG_VAL) {
		ret = -ENODEV;
		goto err;
	}

	ret = bma180_soft_reset(data);
	if (ret)
		goto err;
	/*
	 * No serial transaction should occur within minimum 10 us
	 * after soft_reset command
	 */
	msleep(20);

	ret = bma180_set_bits(data, BMA180_CTRL_REG0, BMA180_DIS_WAKE_UP, 1);
	if (ret)
		goto err;
	ret = bma180_set_ee_writing_state(data, 1);
	if (ret)
		goto err;
	ret = bma180_set_new_data_intr_state(data, 0);
	if (ret)
		goto err;
	ret = bma180_set_bits(data, BMA180_OFFSET_LSB1, BMA180_SMP_SKIP, 1);
	if (ret)
		goto err;
	ret = bma180_set_pmode(data, BMA180_DEF_PMODE);
	if (ret)
		goto err;
	ret = bma180_set_bw(data, BMA180_DEF_BW);
	if (ret)
		goto err;
	ret = bma180_set_scale(data, BMA180_DEF_SCALE);
	if (ret)
		goto err;

	return 0;

err:
	dev_err(&data->client->dev, "failed to init the chip\n");
	return ret;
}

static void bma180_chip_disable(struct bma180_data *data)
{
	if (bma180_set_new_data_intr_state(data, 0))
		goto err;
	if (bma180_set_ee_writing_state(data, 0))
		goto err;
	if (bma180_set_sleep_state(data, 1))
		goto err;

	return;

err:
	dev_err(&data->client->dev, "failed to disable the chip\n");
}

static IIO_CONST_ATTR(in_accel_filter_low_pass_3db_frequency_available,
		BMA180_FLP_FREQ_AVAILABLE);
static IIO_CONST_ATTR(in_accel_scale_available, BMA180_SCALE_AVAILABLE);

static struct attribute *bma180_attributes[] = {
	&iio_const_attr_in_accel_filter_low_pass_3db_frequency_available.dev_attr.attr,
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bma180_attrs_group = {
	.attrs = bma180_attributes,
};

static int bma180_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val, int *val2,
		long mask)
{
	struct bma180_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->mutex);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = bma180_get_acc_reg(data, chan->scan_index);
		mutex_unlock(&data->mutex);
		if (ret < 0)
			return ret;
		*val = (s16)ret >> chan->scan_type.shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*val = data->bw;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = data->scale;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int bma180_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct bma180_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = bma180_set_scale(data, val2);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (val2)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = bma180_set_bw(data, val);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bma180_update_scan_mode(struct iio_dev *indio_dev,
		const unsigned long *scan_mask)
{
	struct bma180_data *data = iio_priv(indio_dev);

	if (data->buff)
		devm_kfree(&indio_dev->dev, data->buff);
	data->buff = devm_kzalloc(&indio_dev->dev,
			indio_dev->scan_bytes, GFP_KERNEL);
	if (!data->buff)
		return -ENOMEM;

	return 0;
}

static const struct iio_info bma180_info = {
	.attrs			= &bma180_attrs_group,
	.read_raw		= bma180_read_raw,
	.write_raw		= bma180_write_raw,
	.update_scan_mode	= bma180_update_scan_mode,
	.driver_module		= THIS_MODULE,
};

static const char * const bma180_power_modes[] = {
	BMA180_LOW_NOISE_STR,
	BMA180_LOW_POWER_STR,
};

static int bma180_get_power_mode(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan)
{
	struct bma180_data *data = iio_priv(indio_dev);

	return data->pmode;
}

static int bma180_set_power_mode(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan, unsigned int mode)
{
	struct bma180_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bma180_set_pmode(data, mode);
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_enum bma180_power_mode_enum = {
	.items = bma180_power_modes,
	.num_items = ARRAY_SIZE(bma180_power_modes),
	.get = bma180_get_power_mode,
	.set = bma180_set_power_mode,
};

static const struct iio_chan_spec_ext_info bma180_ext_info[] = {
	IIO_ENUM("power_mode", true, &bma180_power_mode_enum),
	IIO_ENUM_AVAILABLE("power_mode", &bma180_power_mode_enum),
	{ },
};

#define BMA180_CHANNEL(_axis) {					\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 14,						\
		.storagebits = 16,					\
		.shift = 2,						\
	},								\
	.ext_info = bma180_ext_info,					\
}

static const struct iio_chan_spec bma180_channels[] = {
	BMA180_CHANNEL(X),
	BMA180_CHANNEL(Y),
	BMA180_CHANNEL(Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static irqreturn_t bma180_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bma180_data *data = iio_priv(indio_dev);
	int64_t time_ns = iio_get_time_ns();
	int bit, ret, i = 0;

	mutex_lock(&data->mutex);

	for_each_set_bit(bit, indio_dev->buffer->scan_mask,
			 indio_dev->masklength) {
		ret = bma180_get_acc_reg(data, bit);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			goto err;
		}
		((s16 *)data->buff)[i++] = ret;
	}
	mutex_unlock(&data->mutex);

	iio_push_to_buffers_with_timestamp(indio_dev, data->buff, time_ns);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bma180_data_rdy_trigger_set_state(struct iio_trigger *trig,
		bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bma180_data *data = iio_priv(indio_dev);

	return bma180_set_new_data_intr_state(data, state);
}

static int bma180_trig_try_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bma180_data *data = iio_priv(indio_dev);

	return bma180_reset_intr(data);
}

static const struct iio_trigger_ops bma180_trigger_ops = {
	.set_trigger_state = bma180_data_rdy_trigger_set_state,
	.try_reenable = bma180_trig_try_reen,
	.owner = THIS_MODULE,
};

static int bma180_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct bma180_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	ret = bma180_chip_init(data);
	if (ret < 0)
		goto err_chip_disable;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = bma180_channels;
	indio_dev->num_channels = ARRAY_SIZE(bma180_channels);
	indio_dev->name = BMA180_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bma180_info;

	if (client->irq > 0) {
		data->trig = iio_trigger_alloc("%s-dev%d", indio_dev->name,
			indio_dev->id);
		if (!data->trig) {
			ret = -ENOMEM;
			goto err_chip_disable;
		}

		ret = devm_request_irq(&client->dev, client->irq,
			iio_trigger_generic_data_rdy_poll, IRQF_TRIGGER_RISING,
			BMA180_IRQ_NAME, data->trig);
		if (ret) {
			dev_err(&client->dev, "unable to request IRQ\n");
			goto err_trigger_free;
		}

		data->trig->dev.parent = &client->dev;
		data->trig->ops = &bma180_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);
		indio_dev->trig = data->trig;

		ret = iio_trigger_register(data->trig);
		if (ret)
			goto err_trigger_free;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
			bma180_trigger_handler, NULL);
	if (ret < 0) {
		dev_err(&client->dev, "unable to setup iio triggered buffer\n");
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
	if (data->trig)
		iio_trigger_unregister(data->trig);
err_trigger_free:
	iio_trigger_free(data->trig);
err_chip_disable:
	bma180_chip_disable(data);

	return ret;
}

static int bma180_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bma180_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (data->trig) {
		iio_trigger_unregister(data->trig);
		iio_trigger_free(data->trig);
	}

	mutex_lock(&data->mutex);
	bma180_chip_disable(data);
	mutex_unlock(&data->mutex);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bma180_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bma180_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bma180_set_sleep_state(data, 1);
	mutex_unlock(&data->mutex);

	return ret;
}

static int bma180_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bma180_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = bma180_set_sleep_state(data, 0);
	mutex_unlock(&data->mutex);

	return ret;
}

static SIMPLE_DEV_PM_OPS(bma180_pm_ops, bma180_suspend, bma180_resume);
#define BMA180_PM_OPS (&bma180_pm_ops)
#else
#define BMA180_PM_OPS NULL
#endif

static struct i2c_device_id bma180_id[] = {
	{ BMA180_DRV_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma180_id);

static struct i2c_driver bma180_driver = {
	.driver = {
		.name	= BMA180_DRV_NAME,
		.owner	= THIS_MODULE,
		.pm	= BMA180_PM_OPS,
	},
	.probe		= bma180_probe,
	.remove		= bma180_remove,
	.id_table	= bma180_id,
};

module_i2c_driver(bma180_driver);

MODULE_AUTHOR("Kravchenko Oleksandr <x0199363@ti.com>");
MODULE_AUTHOR("Texas Instruments, Inc.");
MODULE_DESCRIPTION("Bosch BMA180 triaxial acceleration sensor");
MODULE_LICENSE("GPL");
