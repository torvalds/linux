// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale MMA9551L Intelligent Motion-Sensing Platform driver
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/pm_runtime.h>
#include "mma9551_core.h"

#define MMA9551_DRV_NAME		"mma9551"
#define MMA9551_IRQ_NAME		"mma9551_event"
#define MMA9551_GPIO_COUNT		4

/* Tilt application (inclination in IIO terms). */
#define MMA9551_TILT_XZ_ANG_REG		0x00
#define MMA9551_TILT_YZ_ANG_REG		0x01
#define MMA9551_TILT_XY_ANG_REG		0x02
#define MMA9551_TILT_ANGFLG		BIT(7)
#define MMA9551_TILT_QUAD_REG		0x03
#define MMA9551_TILT_XY_QUAD_SHIFT	0
#define MMA9551_TILT_YZ_QUAD_SHIFT	2
#define MMA9551_TILT_XZ_QUAD_SHIFT	4
#define MMA9551_TILT_CFG_REG		0x01
#define MMA9551_TILT_ANG_THRESH_MASK	GENMASK(3, 0)

#define MMA9551_DEFAULT_SAMPLE_RATE	122	/* Hz */

/* Tilt events are mapped to the first three GPIO pins. */
enum mma9551_tilt_axis {
	mma9551_x = 0,
	mma9551_y,
	mma9551_z,
};

struct mma9551_data {
	struct i2c_client *client;
	struct mutex mutex;
	int event_enabled[3];
	int irqs[MMA9551_GPIO_COUNT];
};

static int mma9551_read_incli_chan(struct i2c_client *client,
				   const struct iio_chan_spec *chan,
				   int *val)
{
	u8 quad_shift, angle, quadrant;
	u16 reg_addr;
	int ret;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg_addr = MMA9551_TILT_YZ_ANG_REG;
		quad_shift = MMA9551_TILT_YZ_QUAD_SHIFT;
		break;
	case IIO_MOD_Y:
		reg_addr = MMA9551_TILT_XZ_ANG_REG;
		quad_shift = MMA9551_TILT_XZ_QUAD_SHIFT;
		break;
	case IIO_MOD_Z:
		reg_addr = MMA9551_TILT_XY_ANG_REG;
		quad_shift = MMA9551_TILT_XY_QUAD_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	ret = mma9551_set_power_state(client, true);
	if (ret < 0)
		return ret;

	ret = mma9551_read_status_byte(client, MMA9551_APPID_TILT,
				       reg_addr, &angle);
	if (ret < 0)
		goto out_poweroff;

	ret = mma9551_read_status_byte(client, MMA9551_APPID_TILT,
				       MMA9551_TILT_QUAD_REG, &quadrant);
	if (ret < 0)
		goto out_poweroff;

	angle &= ~MMA9551_TILT_ANGFLG;
	quadrant = (quadrant >> quad_shift) & 0x03;

	if (quadrant == 1 || quadrant == 3)
		*val = 90 * (quadrant + 1) - angle;
	else
		*val = angle + 90 * quadrant;

	ret = IIO_VAL_INT;

out_poweroff:
	mma9551_set_power_state(client, false);
	return ret;
}

static int mma9551_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_INCLI:
			mutex_lock(&data->mutex);
			ret = mma9551_read_incli_chan(data->client, chan, val);
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			mutex_lock(&data->mutex);
			ret = mma9551_read_accel_chan(data->client,
						      chan, val, val2);
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			return mma9551_read_accel_scale(val, val2);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int mma9551_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct mma9551_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_INCLI:
		/* IIO counts axes from 1, because IIO_NO_MOD is 0. */
		return data->event_enabled[chan->channel2 - 1];
	default:
		return -EINVAL;
	}
}

static int mma9551_config_incli_event(struct iio_dev *indio_dev,
				      enum iio_modifier axis,
				      int state)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	enum mma9551_tilt_axis mma_axis;
	int ret;

	/* IIO counts axes from 1, because IIO_NO_MOD is 0. */
	mma_axis = axis - 1;

	if (data->event_enabled[mma_axis] == state)
		return 0;

	if (state == 0) {
		ret = mma9551_gpio_config(data->client,
					  (enum mma9551_gpio_pin)mma_axis,
					  MMA9551_APPID_NONE, 0, 0);
		if (ret < 0)
			return ret;

		ret = mma9551_set_power_state(data->client, false);
		if (ret < 0)
			return ret;
	} else {
		int bitnum;

		/* Bit 7 of each angle register holds the angle flag. */
		switch (axis) {
		case IIO_MOD_X:
			bitnum = 7 + 8 * MMA9551_TILT_YZ_ANG_REG;
			break;
		case IIO_MOD_Y:
			bitnum = 7 + 8 * MMA9551_TILT_XZ_ANG_REG;
			break;
		case IIO_MOD_Z:
			bitnum = 7 + 8 * MMA9551_TILT_XY_ANG_REG;
			break;
		default:
			return -EINVAL;
		}


		ret = mma9551_set_power_state(data->client, true);
		if (ret < 0)
			return ret;

		ret = mma9551_gpio_config(data->client,
					  (enum mma9551_gpio_pin)mma_axis,
					  MMA9551_APPID_TILT, bitnum, 0);
		if (ret < 0) {
			mma9551_set_power_state(data->client, false);
			return ret;
		}
	}

	data->event_enabled[mma_axis] = state;

	return ret;
}

static int mma9551_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_INCLI:
		mutex_lock(&data->mutex);
		ret = mma9551_config_incli_event(indio_dev,
						 chan->channel2, state);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int mma9551_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_INCLI:
		if (val2 != 0 || val < 1 || val > 10)
			return -EINVAL;
		mutex_lock(&data->mutex);
		ret = mma9551_update_config_bits(data->client,
						 MMA9551_APPID_TILT,
						 MMA9551_TILT_CFG_REG,
						 MMA9551_TILT_ANG_THRESH_MASK,
						 val);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int mma9551_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;
	u8 tmp;

	switch (chan->type) {
	case IIO_INCLI:
		mutex_lock(&data->mutex);
		ret = mma9551_read_config_byte(data->client,
					       MMA9551_APPID_TILT,
					       MMA9551_TILT_CFG_REG, &tmp);
		mutex_unlock(&data->mutex);
		if (ret < 0)
			return ret;
		*val = tmp & MMA9551_TILT_ANG_THRESH_MASK;
		*val2 = 0;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_event_spec mma9551_incli_event = {
	.type = IIO_EV_TYPE_ROC,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE),
};

#define MMA9551_INCLI_CHANNEL(axis) {				\
	.type = IIO_INCLI,					\
	.modified = 1,						\
	.channel2 = axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	\
	.event_spec = &mma9551_incli_event,			\
	.num_event_specs = 1,					\
}

static const struct iio_chan_spec mma9551_channels[] = {
	MMA9551_ACCEL_CHANNEL(IIO_MOD_X),
	MMA9551_ACCEL_CHANNEL(IIO_MOD_Y),
	MMA9551_ACCEL_CHANNEL(IIO_MOD_Z),

	MMA9551_INCLI_CHANNEL(IIO_MOD_X),
	MMA9551_INCLI_CHANNEL(IIO_MOD_Y),
	MMA9551_INCLI_CHANNEL(IIO_MOD_Z),
};

static const struct iio_info mma9551_info = {
	.read_raw = mma9551_read_raw,
	.read_event_config = mma9551_read_event_config,
	.write_event_config = mma9551_write_event_config,
	.read_event_value = mma9551_read_event_value,
	.write_event_value = mma9551_write_event_value,
};

static irqreturn_t mma9551_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct mma9551_data *data = iio_priv(indio_dev);
	int i, ret, mma_axis = -1;
	u16 reg;
	u8 val;

	mutex_lock(&data->mutex);

	for (i = 0; i < 3; i++)
		if (irq == data->irqs[i]) {
			mma_axis = i;
			break;
		}

	if (mma_axis == -1) {
		/* IRQ was triggered on 4th line, which we don't use. */
		dev_warn(&data->client->dev,
			 "irq triggered on unused line %d\n", data->irqs[3]);
		goto out;
	}

	switch (mma_axis) {
	case mma9551_x:
		reg = MMA9551_TILT_YZ_ANG_REG;
		break;
	case mma9551_y:
		reg = MMA9551_TILT_XZ_ANG_REG;
		break;
	case mma9551_z:
		reg = MMA9551_TILT_XY_ANG_REG;
		break;
	}

	/*
	 * Read the angle even though we don't use it, otherwise we
	 * won't get any further interrupts.
	 */
	ret = mma9551_read_status_byte(data->client, MMA9551_APPID_TILT,
				       reg, &val);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"error %d reading tilt register in IRQ\n", ret);
		goto out;
	}

	iio_push_event(indio_dev,
		       IIO_MOD_EVENT_CODE(IIO_INCLI, 0, (mma_axis + 1),
					  IIO_EV_TYPE_ROC, IIO_EV_DIR_RISING),
		       iio_get_time_ns(indio_dev));

out:
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static int mma9551_init(struct mma9551_data *data)
{
	int ret;

	ret = mma9551_read_version(data->client);
	if (ret)
		return ret;

	return mma9551_set_device_state(data->client, true);
}

static int mma9551_gpio_probe(struct iio_dev *indio_dev)
{
	struct gpio_desc *gpio;
	int i, ret;
	struct mma9551_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;

	for (i = 0; i < MMA9551_GPIO_COUNT; i++) {
		gpio = devm_gpiod_get_index(dev, NULL, i, GPIOD_IN);
		if (IS_ERR(gpio)) {
			dev_err(dev, "acpi gpio get index failed\n");
			return PTR_ERR(gpio);
		}

		ret = gpiod_to_irq(gpio);
		if (ret < 0)
			return ret;

		data->irqs[i] = ret;
		ret = devm_request_threaded_irq(dev, data->irqs[i],
				NULL, mma9551_event_handler,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				MMA9551_IRQ_NAME, indio_dev);
		if (ret < 0) {
			dev_err(dev, "request irq %d failed\n", data->irqs[i]);
			return ret;
		}

		dev_dbg(dev, "gpio resource, no:%d irq:%d\n",
			desc_to_gpio(gpio), data->irqs[i]);
	}

	return 0;
}

static const char *mma9551_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	return dev_name(dev);
}

static int mma9551_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mma9551_data *data;
	struct iio_dev *indio_dev;
	const char *name = NULL;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	if (id)
		name = id->name;
	else if (ACPI_HANDLE(&client->dev))
		name = mma9551_match_acpi_device(&client->dev);

	ret = mma9551_init(data);
	if (ret < 0)
		return ret;

	mutex_init(&data->mutex);

	indio_dev->channels = mma9551_channels;
	indio_dev->num_channels = ARRAY_SIZE(mma9551_channels);
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mma9551_info;

	ret = mma9551_gpio_probe(indio_dev);
	if (ret < 0)
		goto out_poweroff;

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto out_poweroff;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 MMA9551_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "unable to register iio device\n");
		goto out_poweroff;
	}

	return 0;

out_poweroff:
	mma9551_set_device_state(client, false);

	return ret;
}

static int mma9551_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mma9551_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_lock(&data->mutex);
	mma9551_set_device_state(data->client, false);
	mutex_unlock(&data->mutex);

	return 0;
}

#ifdef CONFIG_PM
static int mma9551_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = mma9551_set_device_state(data->client, false);
	mutex_unlock(&data->mutex);
	if (ret < 0) {
		dev_err(&data->client->dev, "powering off device failed\n");
		return -EAGAIN;
	}

	return 0;
}

static int mma9551_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	ret = mma9551_set_device_state(data->client, true);
	if (ret < 0)
		return ret;

	mma9551_sleep(MMA9551_DEFAULT_SAMPLE_RATE);

	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int mma9551_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = mma9551_set_device_state(data->client, false);
	mutex_unlock(&data->mutex);

	return ret;
}

static int mma9551_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma9551_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = mma9551_set_device_state(data->client, true);
	mutex_unlock(&data->mutex);

	return ret;
}
#endif

static const struct dev_pm_ops mma9551_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mma9551_suspend, mma9551_resume)
	SET_RUNTIME_PM_OPS(mma9551_runtime_suspend,
			   mma9551_runtime_resume, NULL)
};

static const struct acpi_device_id mma9551_acpi_match[] = {
	{"MMA9551", 0},
	{},
};

MODULE_DEVICE_TABLE(acpi, mma9551_acpi_match);

static const struct i2c_device_id mma9551_id[] = {
	{"mma9551", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, mma9551_id);

static struct i2c_driver mma9551_driver = {
	.driver = {
		   .name = MMA9551_DRV_NAME,
		   .acpi_match_table = ACPI_PTR(mma9551_acpi_match),
		   .pm = &mma9551_pm_ops,
		   },
	.probe = mma9551_probe,
	.remove = mma9551_remove,
	.id_table = mma9551_id,
};

module_i2c_driver(mma9551_driver);

MODULE_AUTHOR("Irina Tirdea <irina.tirdea@intel.com>");
MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MMA9551L motion-sensing platform driver");
