// SPDX-License-Identifier: GPL-2.0-only
 /*
  * iio/dac/max5821.c
  * Copyright (C) 2014 Philippe Reynes
  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/regulator/consumer.h>

#define MAX5821_MAX_DAC_CHANNELS		2

/* command bytes */
#define MAX5821_LOAD_DAC_A_IN_REG_B		0x00
#define MAX5821_LOAD_DAC_B_IN_REG_A		0x10
#define MAX5821_EXTENDED_COMMAND_MODE		0xf0
#define MAX5821_READ_DAC_A_COMMAND		0xf1
#define MAX5821_READ_DAC_B_COMMAND		0xf2

#define MAX5821_EXTENDED_POWER_UP		0x00
#define MAX5821_EXTENDED_POWER_DOWN_MODE0	0x01
#define MAX5821_EXTENDED_POWER_DOWN_MODE1	0x02
#define MAX5821_EXTENDED_POWER_DOWN_MODE2	0x03
#define MAX5821_EXTENDED_DAC_A			0x04
#define MAX5821_EXTENDED_DAC_B			0x08

enum max5821_device_ids {
	ID_MAX5821,
};

struct max5821_data {
	struct i2c_client	*client;
	struct regulator	*vref_reg;
	unsigned short		vref_mv;
	bool			powerdown[MAX5821_MAX_DAC_CHANNELS];
	u8			powerdown_mode[MAX5821_MAX_DAC_CHANNELS];
	struct mutex		lock;
};

static const char * const max5821_powerdown_modes[] = {
	"three_state",
	"1kohm_to_gnd",
	"100kohm_to_gnd",
};

enum {
	MAX5821_THREE_STATE,
	MAX5821_1KOHM_TO_GND,
	MAX5821_100KOHM_TO_GND
};

static int max5821_get_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct max5821_data *st = iio_priv(indio_dev);

	return st->powerdown_mode[chan->channel];
}

static int max5821_set_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int mode)
{
	struct max5821_data *st = iio_priv(indio_dev);

	st->powerdown_mode[chan->channel] = mode;

	return 0;
}

static const struct iio_enum max5821_powerdown_mode_enum = {
	.items = max5821_powerdown_modes,
	.num_items = ARRAY_SIZE(max5821_powerdown_modes),
	.get = max5821_get_powerdown_mode,
	.set = max5821_set_powerdown_mode,
};

static ssize_t max5821_read_dac_powerdown(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct max5821_data *st = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", st->powerdown[chan->channel]);
}

static int max5821_sync_powerdown_mode(struct max5821_data *data,
				       const struct iio_chan_spec *chan)
{
	u8 outbuf[2];

	outbuf[0] = MAX5821_EXTENDED_COMMAND_MODE;

	if (chan->channel == 0)
		outbuf[1] = MAX5821_EXTENDED_DAC_A;
	else
		outbuf[1] = MAX5821_EXTENDED_DAC_B;

	if (data->powerdown[chan->channel])
		outbuf[1] |= data->powerdown_mode[chan->channel] + 1;
	else
		outbuf[1] |= MAX5821_EXTENDED_POWER_UP;

	return i2c_master_send(data->client, outbuf, 2);
}

static ssize_t max5821_write_dac_powerdown(struct iio_dev *indio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   const char *buf, size_t len)
{
	struct max5821_data *data = iio_priv(indio_dev);
	bool powerdown;
	int ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	data->powerdown[chan->channel] = powerdown;

	ret = max5821_sync_powerdown_mode(data, chan);
	if (ret < 0)
		return ret;

	return len;
}

static const struct iio_chan_spec_ext_info max5821_ext_info[] = {
	{
		.name = "powerdown",
		.read = max5821_read_dac_powerdown,
		.write = max5821_write_dac_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &max5821_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &max5821_powerdown_mode_enum),
	{ },
};

#define MAX5821_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = max5821_ext_info,				\
}

static const struct iio_chan_spec max5821_channels[] = {
	MAX5821_CHANNEL(0),
	MAX5821_CHANNEL(1)
};

static const u8 max5821_read_dac_command[] = {
	MAX5821_READ_DAC_A_COMMAND,
	MAX5821_READ_DAC_B_COMMAND
};

static const u8 max5821_load_dac_command[] = {
	MAX5821_LOAD_DAC_A_IN_REG_B,
	MAX5821_LOAD_DAC_B_IN_REG_A
};

static int max5821_get_value(struct iio_dev *indio_dev,
			     int *val, int channel)
{
	struct max5821_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u8 outbuf[1];
	u8 inbuf[2];
	int ret;

	if ((channel != 0) && (channel != 1))
		return -EINVAL;

	outbuf[0] = max5821_read_dac_command[channel];

	mutex_lock(&data->lock);

	ret = i2c_master_send(client, outbuf, 1);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret != 1) {
		mutex_unlock(&data->lock);
		return -EIO;
	}

	ret = i2c_master_recv(client, inbuf, 2);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret != 2) {
		mutex_unlock(&data->lock);
		return -EIO;
	}

	mutex_unlock(&data->lock);

	*val = ((inbuf[0] & 0x0f) << 6) | (inbuf[1] >> 2);

	return IIO_VAL_INT;
}

static int max5821_set_value(struct iio_dev *indio_dev,
			     int val, int channel)
{
	struct max5821_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u8 outbuf[2];
	int ret;

	if ((val < 0) || (val > 1023))
		return -EINVAL;

	if ((channel != 0) && (channel != 1))
		return -EINVAL;

	outbuf[0] = max5821_load_dac_command[channel];
	outbuf[0] |= val >> 6;
	outbuf[1] = (val & 0x3f) << 2;

	ret = i2c_master_send(client, outbuf, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;
	else
		return 0;
}

static int max5821_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct max5821_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return max5821_get_value(indio_dev, val, chan->channel);
	case IIO_CHAN_INFO_SCALE:
		*val = data->vref_mv;
		*val2 = 10;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int max5821_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	if (val2 != 0)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return max5821_set_value(indio_dev, val, chan->channel);
	default:
		return -EINVAL;
	}
}

static int max5821_suspend(struct device *dev)
{
	u8 outbuf[2] = { MAX5821_EXTENDED_COMMAND_MODE,
			 MAX5821_EXTENDED_DAC_A |
			 MAX5821_EXTENDED_DAC_B |
			 MAX5821_EXTENDED_POWER_DOWN_MODE2 };

	return i2c_master_send(to_i2c_client(dev), outbuf, 2);
}

static int max5821_resume(struct device *dev)
{
	u8 outbuf[2] = { MAX5821_EXTENDED_COMMAND_MODE,
			 MAX5821_EXTENDED_DAC_A |
			 MAX5821_EXTENDED_DAC_B |
			 MAX5821_EXTENDED_POWER_UP };

	return i2c_master_send(to_i2c_client(dev), outbuf, 2);
}

static DEFINE_SIMPLE_DEV_PM_OPS(max5821_pm_ops, max5821_suspend,
				max5821_resume);

static const struct iio_info max5821_info = {
	.read_raw = max5821_read_raw,
	.write_raw = max5821_write_raw,
};

static void max5821_regulator_disable(void *reg)
{
	regulator_disable(reg);
}

static int max5821_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct max5821_data *data;
	struct iio_dev *indio_dev;
	u32 tmp;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	/* max5821 start in powerdown mode 100Kohm to ground */
	for (tmp = 0; tmp < MAX5821_MAX_DAC_CHANNELS; tmp++) {
		data->powerdown[tmp] = true;
		data->powerdown_mode[tmp] = MAX5821_100KOHM_TO_GND;
	}

	data->vref_reg = devm_regulator_get(&client->dev, "vref");
	if (IS_ERR(data->vref_reg))
		return dev_err_probe(&client->dev, PTR_ERR(data->vref_reg),
				     "Failed to get vref regulator\n");

	ret = regulator_enable(data->vref_reg);
	if (ret) {
		dev_err(&client->dev,
			"Failed to enable vref regulator: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&client->dev, max5821_regulator_disable,
				       data->vref_reg);
	if (ret) {
		dev_err(&client->dev,
			"Failed to add action to managed regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_get_voltage(data->vref_reg);
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to get voltage on regulator: %d\n", ret);
		return ret;
	}

	data->vref_mv = ret / 1000;

	indio_dev->name = id->name;
	indio_dev->num_channels = ARRAY_SIZE(max5821_channels);
	indio_dev->channels = max5821_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &max5821_info;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id max5821_id[] = {
	{ "max5821", ID_MAX5821 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max5821_id);

static const struct of_device_id max5821_of_match[] = {
	{ .compatible = "maxim,max5821" },
	{ }
};
MODULE_DEVICE_TABLE(of, max5821_of_match);

static struct i2c_driver max5821_driver = {
	.driver = {
		.name	= "max5821",
		.of_match_table = max5821_of_match,
		.pm     = pm_sleep_ptr(&max5821_pm_ops),
	},
	.probe_new	= max5821_probe,
	.id_table	= max5821_id,
};
module_i2c_driver(max5821_driver);

MODULE_AUTHOR("Philippe Reynes <tremyfr@yahoo.fr>");
MODULE_DESCRIPTION("MAX5821 DAC");
MODULE_LICENSE("GPL v2");
