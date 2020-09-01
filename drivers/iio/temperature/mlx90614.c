// SPDX-License-Identifier: GPL-2.0-only
/*
 * mlx90614.c - Support for Melexis MLX90614 contactless IR temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 * Copyright (c) 2015 Essensium NV
 * Copyright (c) 2015 Melexis
 *
 * Driver for the Melexis MLX90614 I2C 16-bit IR thermopile sensor
 *
 * (7-bit I2C slave address 0x5a, 100KHz bus speed only!)
 *
 * To wake up from sleep mode, the SDA line must be held low while SCL is high
 * for at least 33ms.  This is achieved with an extra GPIO that can be connected
 * directly to the SDA line.  In normal operation, the GPIO is set as input and
 * will not interfere in I2C communication.  While the GPIO is driven low, the
 * i2c adapter is locked since it cannot be used by other clients.  The SCL line
 * always has a pull-up so we do not need an extra GPIO to drive it high.  If
 * the "wakeup" GPIO is not given, power management will be disabled.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MLX90614_OP_RAM		0x00
#define MLX90614_OP_EEPROM	0x20
#define MLX90614_OP_SLEEP	0xff

/* RAM offsets with 16-bit data, MSB first */
#define MLX90614_RAW1	(MLX90614_OP_RAM | 0x04) /* raw data IR channel 1 */
#define MLX90614_RAW2	(MLX90614_OP_RAM | 0x05) /* raw data IR channel 2 */
#define MLX90614_TA	(MLX90614_OP_RAM | 0x06) /* ambient temperature */
#define MLX90614_TOBJ1	(MLX90614_OP_RAM | 0x07) /* object 1 temperature */
#define MLX90614_TOBJ2	(MLX90614_OP_RAM | 0x08) /* object 2 temperature */

/* EEPROM offsets with 16-bit data, MSB first */
#define MLX90614_EMISSIVITY	(MLX90614_OP_EEPROM | 0x04) /* emissivity correction coefficient */
#define MLX90614_CONFIG		(MLX90614_OP_EEPROM | 0x05) /* configuration register */

/* Control bits in configuration register */
#define MLX90614_CONFIG_IIR_SHIFT 0 /* IIR coefficient */
#define MLX90614_CONFIG_IIR_MASK (0x7 << MLX90614_CONFIG_IIR_SHIFT)
#define MLX90614_CONFIG_DUAL_SHIFT 6 /* single (0) or dual (1) IR sensor */
#define MLX90614_CONFIG_DUAL_MASK (1 << MLX90614_CONFIG_DUAL_SHIFT)
#define MLX90614_CONFIG_FIR_SHIFT 8 /* FIR coefficient */
#define MLX90614_CONFIG_FIR_MASK (0x7 << MLX90614_CONFIG_FIR_SHIFT)
#define MLX90614_CONFIG_GAIN_SHIFT 11 /* gain */
#define MLX90614_CONFIG_GAIN_MASK (0x7 << MLX90614_CONFIG_GAIN_SHIFT)

/* Timings (in ms) */
#define MLX90614_TIMING_EEPROM 20 /* time for EEPROM write/erase to complete */
#define MLX90614_TIMING_WAKEUP 34 /* time to hold SDA low for wake-up */
#define MLX90614_TIMING_STARTUP 250 /* time before first data after wake-up */

#define MLX90614_AUTOSLEEP_DELAY 5000 /* default autosleep delay */

/* Magic constants */
#define MLX90614_CONST_OFFSET_DEC -13657 /* decimal part of the Kelvin offset */
#define MLX90614_CONST_OFFSET_REM 500000 /* remainder of offset (273.15*50) */
#define MLX90614_CONST_SCALE 20 /* Scale in milliKelvin (0.02 * 1000) */
#define MLX90614_CONST_RAW_EMISSIVITY_MAX 65535 /* max value for emissivity */
#define MLX90614_CONST_EMISSIVITY_RESOLUTION 15259 /* 1/65535 ~ 0.000015259 */
#define MLX90614_CONST_FIR 0x7 /* Fixed value for FIR part of low pass filter */

struct mlx90614_data {
	struct i2c_client *client;
	struct mutex lock; /* for EEPROM access only */
	struct gpio_desc *wakeup_gpio; /* NULL to disable sleep/wake-up */
	unsigned long ready_timestamp; /* in jiffies */
};

/* Bandwidth values for IIR filtering */
static const int mlx90614_iir_values[] = {77, 31, 20, 15, 723, 153, 110, 86};
static IIO_CONST_ATTR(in_temp_object_filter_low_pass_3db_frequency_available,
		      "0.15 0.20 0.31 0.77 0.86 1.10 1.53 7.23");

static struct attribute *mlx90614_attributes[] = {
	&iio_const_attr_in_temp_object_filter_low_pass_3db_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mlx90614_attr_group = {
	.attrs = mlx90614_attributes,
};

/*
 * Erase an address and write word.
 * The mutex must be locked before calling.
 */
static s32 mlx90614_write_word(const struct i2c_client *client, u8 command,
			       u16 value)
{
	/*
	 * Note: The mlx90614 requires a PEC on writing but does not send us a
	 * valid PEC on reading.  Hence, we cannot set I2C_CLIENT_PEC in
	 * i2c_client.flags.  As a workaround, we use i2c_smbus_xfer here.
	 */
	union i2c_smbus_data data;
	s32 ret;

	dev_dbg(&client->dev, "Writing 0x%x to address 0x%x", value, command);

	data.word = 0x0000; /* erase command */
	ret = i2c_smbus_xfer(client->adapter, client->addr,
			     client->flags | I2C_CLIENT_PEC,
			     I2C_SMBUS_WRITE, command,
			     I2C_SMBUS_WORD_DATA, &data);
	if (ret < 0)
		return ret;

	msleep(MLX90614_TIMING_EEPROM);

	data.word = value; /* actual write */
	ret = i2c_smbus_xfer(client->adapter, client->addr,
			     client->flags | I2C_CLIENT_PEC,
			     I2C_SMBUS_WRITE, command,
			     I2C_SMBUS_WORD_DATA, &data);

	msleep(MLX90614_TIMING_EEPROM);

	return ret;
}

/*
 * Find the IIR value inside mlx90614_iir_values array and return its position
 * which is equivalent to the bit value in sensor register
 */
static inline s32 mlx90614_iir_search(const struct i2c_client *client,
				      int value)
{
	int i;
	s32 ret;

	for (i = 0; i < ARRAY_SIZE(mlx90614_iir_values); ++i) {
		if (value == mlx90614_iir_values[i])
			break;
	}

	if (i == ARRAY_SIZE(mlx90614_iir_values))
		return -EINVAL;

	/*
	 * CONFIG register values must not be changed so
	 * we must read them before we actually write
	 * changes
	 */
	ret = i2c_smbus_read_word_data(client, MLX90614_CONFIG);
	if (ret < 0)
		return ret;

	ret &= ~MLX90614_CONFIG_FIR_MASK;
	ret |= MLX90614_CONST_FIR << MLX90614_CONFIG_FIR_SHIFT;
	ret &= ~MLX90614_CONFIG_IIR_MASK;
	ret |= i << MLX90614_CONFIG_IIR_SHIFT;

	/* Write changed values */
	ret = mlx90614_write_word(client, MLX90614_CONFIG, ret);
	return ret;
}

#ifdef CONFIG_PM
/*
 * If @startup is true, make sure MLX90614_TIMING_STARTUP ms have elapsed since
 * the last wake-up.  This is normally only needed to get a valid temperature
 * reading.  EEPROM access does not need such delay.
 * Return 0 on success, <0 on error.
 */
static int mlx90614_power_get(struct mlx90614_data *data, bool startup)
{
	unsigned long now;

	if (!data->wakeup_gpio)
		return 0;

	pm_runtime_get_sync(&data->client->dev);

	if (startup) {
		now = jiffies;
		if (time_before(now, data->ready_timestamp) &&
		    msleep_interruptible(jiffies_to_msecs(
				data->ready_timestamp - now)) != 0) {
			pm_runtime_put_autosuspend(&data->client->dev);
			return -EINTR;
		}
	}

	return 0;
}

static void mlx90614_power_put(struct mlx90614_data *data)
{
	if (!data->wakeup_gpio)
		return;

	pm_runtime_mark_last_busy(&data->client->dev);
	pm_runtime_put_autosuspend(&data->client->dev);
}
#else
static inline int mlx90614_power_get(struct mlx90614_data *data, bool startup)
{
	return 0;
}

static inline void mlx90614_power_put(struct mlx90614_data *data)
{
}
#endif

static int mlx90614_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mlx90614_data *data = iio_priv(indio_dev);
	u8 cmd;
	s32 ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: /* 0.02K / LSB */
		switch (channel->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			cmd = MLX90614_TA;
			break;
		case IIO_MOD_TEMP_OBJECT:
			switch (channel->channel) {
			case 0:
				cmd = MLX90614_TOBJ1;
				break;
			case 1:
				cmd = MLX90614_TOBJ2;
				break;
			default:
				return -EINVAL;
			}
			break;
		default:
			return -EINVAL;
		}

		ret = mlx90614_power_get(data, true);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_read_word_data(data->client, cmd);
		mlx90614_power_put(data);

		if (ret < 0)
			return ret;

		/* MSB is an error flag */
		if (ret & 0x8000)
			return -EIO;

		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = MLX90614_CONST_OFFSET_DEC;
		*val2 = MLX90614_CONST_OFFSET_REM;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		*val = MLX90614_CONST_SCALE;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBEMISSIVITY: /* 1/65535 / LSB */
		mlx90614_power_get(data, false);
		mutex_lock(&data->lock);
		ret = i2c_smbus_read_word_data(data->client,
					       MLX90614_EMISSIVITY);
		mutex_unlock(&data->lock);
		mlx90614_power_put(data);

		if (ret < 0)
			return ret;

		if (ret == MLX90614_CONST_RAW_EMISSIVITY_MAX) {
			*val = 1;
			*val2 = 0;
		} else {
			*val = 0;
			*val2 = ret * MLX90614_CONST_EMISSIVITY_RESOLUTION;
		}
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY: /* IIR setting with
							     FIR = 1024 */
		mlx90614_power_get(data, false);
		mutex_lock(&data->lock);
		ret = i2c_smbus_read_word_data(data->client, MLX90614_CONFIG);
		mutex_unlock(&data->lock);
		mlx90614_power_put(data);

		if (ret < 0)
			return ret;

		*val = mlx90614_iir_values[ret & MLX90614_CONFIG_IIR_MASK] / 100;
		*val2 = (mlx90614_iir_values[ret & MLX90614_CONFIG_IIR_MASK] % 100) *
			10000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mlx90614_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int val,
			     int val2, long mask)
{
	struct mlx90614_data *data = iio_priv(indio_dev);
	s32 ret;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBEMISSIVITY: /* 1/65535 / LSB */
		if (val < 0 || val2 < 0 || val > 1 || (val == 1 && val2 != 0))
			return -EINVAL;
		val = val * MLX90614_CONST_RAW_EMISSIVITY_MAX +
			val2 / MLX90614_CONST_EMISSIVITY_RESOLUTION;

		mlx90614_power_get(data, false);
		mutex_lock(&data->lock);
		ret = mlx90614_write_word(data->client, MLX90614_EMISSIVITY,
					  val);
		mutex_unlock(&data->lock);
		mlx90614_power_put(data);

		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY: /* IIR Filter setting */
		if (val < 0 || val2 < 0)
			return -EINVAL;

		mlx90614_power_get(data, false);
		mutex_lock(&data->lock);
		ret = mlx90614_iir_search(data->client,
					  val * 100 + val2 / 10000);
		mutex_unlock(&data->lock);
		mlx90614_power_put(data);

		return ret;
	default:
		return -EINVAL;
	}
}

static int mlx90614_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *channel,
				     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_CALIBEMISSIVITY:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec mlx90614_channels[] = {
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.modified = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		    BIT(IIO_CHAN_INFO_CALIBEMISSIVITY) |
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.modified = 1,
		.channel = 1,
		.channel2 = IIO_MOD_TEMP_OBJECT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		    BIT(IIO_CHAN_INFO_CALIBEMISSIVITY) |
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		    BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_info mlx90614_info = {
	.read_raw = mlx90614_read_raw,
	.write_raw = mlx90614_write_raw,
	.write_raw_get_fmt = mlx90614_write_raw_get_fmt,
	.attrs = &mlx90614_attr_group,
};

#ifdef CONFIG_PM
static int mlx90614_sleep(struct mlx90614_data *data)
{
	s32 ret;

	if (!data->wakeup_gpio) {
		dev_dbg(&data->client->dev, "Sleep disabled");
		return -ENOSYS;
	}

	dev_dbg(&data->client->dev, "Requesting sleep");

	mutex_lock(&data->lock);
	ret = i2c_smbus_xfer(data->client->adapter, data->client->addr,
			     data->client->flags | I2C_CLIENT_PEC,
			     I2C_SMBUS_WRITE, MLX90614_OP_SLEEP,
			     I2C_SMBUS_BYTE, NULL);
	mutex_unlock(&data->lock);

	return ret;
}

static int mlx90614_wakeup(struct mlx90614_data *data)
{
	if (!data->wakeup_gpio) {
		dev_dbg(&data->client->dev, "Wake-up disabled");
		return -ENOSYS;
	}

	dev_dbg(&data->client->dev, "Requesting wake-up");

	i2c_lock_bus(data->client->adapter, I2C_LOCK_ROOT_ADAPTER);
	gpiod_direction_output(data->wakeup_gpio, 0);
	msleep(MLX90614_TIMING_WAKEUP);
	gpiod_direction_input(data->wakeup_gpio);
	i2c_unlock_bus(data->client->adapter, I2C_LOCK_ROOT_ADAPTER);

	data->ready_timestamp = jiffies +
			msecs_to_jiffies(MLX90614_TIMING_STARTUP);

	/*
	 * Quirk: the i2c controller may get confused right after the
	 * wake-up signal has been sent.  As a workaround, do a dummy read.
	 * If the read fails, the controller will probably be reset so that
	 * further reads will work.
	 */
	i2c_smbus_read_word_data(data->client, MLX90614_CONFIG);

	return 0;
}

/* Return wake-up GPIO or NULL if sleep functionality should be disabled. */
static struct gpio_desc *mlx90614_probe_wakeup(struct i2c_client *client)
{
	struct gpio_desc *gpio;

	if (!i2c_check_functionality(client->adapter,
						I2C_FUNC_SMBUS_WRITE_BYTE)) {
		dev_info(&client->dev,
			 "i2c adapter does not support SMBUS_WRITE_BYTE, sleep disabled");
		return NULL;
	}

	gpio = devm_gpiod_get_optional(&client->dev, "wakeup", GPIOD_IN);

	if (IS_ERR(gpio)) {
		dev_warn(&client->dev,
			 "gpio acquisition failed with error %ld, sleep disabled",
			 PTR_ERR(gpio));
		return NULL;
	} else if (!gpio) {
		dev_info(&client->dev,
			 "wakeup-gpio not found, sleep disabled");
	}

	return gpio;
}
#else
static inline int mlx90614_sleep(struct mlx90614_data *data)
{
	return -ENOSYS;
}
static inline int mlx90614_wakeup(struct mlx90614_data *data)
{
	return -ENOSYS;
}
static inline struct gpio_desc *mlx90614_probe_wakeup(struct i2c_client *client)
{
	return NULL;
}
#endif

/* Return 0 for single sensor, 1 for dual sensor, <0 on error. */
static int mlx90614_probe_num_ir_sensors(struct i2c_client *client)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(client, MLX90614_CONFIG);

	if (ret < 0)
		return ret;

	return (ret & MLX90614_CONFIG_DUAL_MASK) ? 1 : 0;
}

static int mlx90614_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct mlx90614_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->wakeup_gpio = mlx90614_probe_wakeup(client);

	mlx90614_wakeup(data);

	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mlx90614_info;

	ret = mlx90614_probe_num_ir_sensors(client);
	switch (ret) {
	case 0:
		dev_dbg(&client->dev, "Found single sensor");
		indio_dev->channels = mlx90614_channels;
		indio_dev->num_channels = 2;
		break;
	case 1:
		dev_dbg(&client->dev, "Found dual sensor");
		indio_dev->channels = mlx90614_channels;
		indio_dev->num_channels = 3;
		break;
	default:
		return ret;
	}

	if (data->wakeup_gpio) {
		pm_runtime_set_autosuspend_delay(&client->dev,
						 MLX90614_AUTOSLEEP_DELAY);
		pm_runtime_use_autosuspend(&client->dev);
		pm_runtime_set_active(&client->dev);
		pm_runtime_enable(&client->dev);
	}

	return iio_device_register(indio_dev);
}

static int mlx90614_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mlx90614_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (data->wakeup_gpio) {
		pm_runtime_disable(&client->dev);
		if (!pm_runtime_status_suspended(&client->dev))
			mlx90614_sleep(data);
		pm_runtime_set_suspended(&client->dev);
	}

	return 0;
}

static const struct i2c_device_id mlx90614_id[] = {
	{ "mlx90614", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mlx90614_id);

static const struct of_device_id mlx90614_of_match[] = {
	{ .compatible = "melexis,mlx90614" },
	{ }
};
MODULE_DEVICE_TABLE(of, mlx90614_of_match);

#ifdef CONFIG_PM_SLEEP
static int mlx90614_pm_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90614_data *data = iio_priv(indio_dev);

	if (data->wakeup_gpio && pm_runtime_active(dev))
		return mlx90614_sleep(data);

	return 0;
}

static int mlx90614_pm_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90614_data *data = iio_priv(indio_dev);
	int err;

	if (data->wakeup_gpio) {
		err = mlx90614_wakeup(data);
		if (err < 0)
			return err;

		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int mlx90614_pm_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90614_data *data = iio_priv(indio_dev);

	return mlx90614_sleep(data);
}

static int mlx90614_pm_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mlx90614_data *data = iio_priv(indio_dev);

	return mlx90614_wakeup(data);
}
#endif

static const struct dev_pm_ops mlx90614_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mlx90614_pm_suspend, mlx90614_pm_resume)
	SET_RUNTIME_PM_OPS(mlx90614_pm_runtime_suspend,
			   mlx90614_pm_runtime_resume, NULL)
};

static struct i2c_driver mlx90614_driver = {
	.driver = {
		.name	= "mlx90614",
		.of_match_table = mlx90614_of_match,
		.pm	= &mlx90614_pm_ops,
	},
	.probe = mlx90614_probe,
	.remove = mlx90614_remove,
	.id_table = mlx90614_id,
};
module_i2c_driver(mlx90614_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_AUTHOR("Vianney le Clément de Saint-Marcq <vianney.leclement@essensium.com>");
MODULE_AUTHOR("Crt Mori <cmo@melexis.com>");
MODULE_DESCRIPTION("Melexis MLX90614 contactless IR temperature sensor driver");
MODULE_LICENSE("GPL");
