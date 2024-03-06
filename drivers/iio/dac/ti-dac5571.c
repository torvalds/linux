// SPDX-License-Identifier: GPL-2.0-only
/*
 * ti-dac5571.c - Texas Instruments 8/10/12-bit 1/4-channel DAC driver
 *
 * Copyright (C) 2018 Prevas A/S
 *
 * https://www.ti.com/lit/ds/symlink/dac5571.pdf
 * https://www.ti.com/lit/ds/symlink/dac6571.pdf
 * https://www.ti.com/lit/ds/symlink/dac7571.pdf
 * https://www.ti.com/lit/ds/symlink/dac5574.pdf
 * https://www.ti.com/lit/ds/symlink/dac6574.pdf
 * https://www.ti.com/lit/ds/symlink/dac7574.pdf
 * https://www.ti.com/lit/ds/symlink/dac5573.pdf
 * https://www.ti.com/lit/ds/symlink/dac6573.pdf
 * https://www.ti.com/lit/ds/symlink/dac7573.pdf
 * https://www.ti.com/lit/ds/symlink/dac121c081.pdf
 */

#include <linux/iio/iio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

enum chip_id {
	single_8bit, single_10bit, single_12bit,
	quad_8bit, quad_10bit, quad_12bit
};

struct dac5571_spec {
	u8 num_channels;
	u8 resolution;
};

static const struct dac5571_spec dac5571_spec[] = {
	[single_8bit]  = {.num_channels = 1, .resolution =  8},
	[single_10bit] = {.num_channels = 1, .resolution = 10},
	[single_12bit] = {.num_channels = 1, .resolution = 12},
	[quad_8bit]    = {.num_channels = 4, .resolution =  8},
	[quad_10bit]   = {.num_channels = 4, .resolution = 10},
	[quad_12bit]   = {.num_channels = 4, .resolution = 12},
};

struct dac5571_data {
	struct i2c_client *client;
	int id;
	struct mutex lock;
	struct regulator *vref;
	u16 val[4];
	bool powerdown[4];
	u8 powerdown_mode[4];
	struct dac5571_spec const *spec;
	int (*dac5571_cmd)(struct dac5571_data *data, int channel, u16 val);
	int (*dac5571_pwrdwn)(struct dac5571_data *data, int channel, u8 pwrdwn);
	u8 buf[3] __aligned(IIO_DMA_MINALIGN);
};

#define DAC5571_POWERDOWN(mode)		((mode) + 1)
#define DAC5571_POWERDOWN_FLAG		BIT(0)
#define DAC5571_CHANNEL_SELECT		1
#define DAC5571_LOADMODE_DIRECT		BIT(4)
#define DAC5571_SINGLE_PWRDWN_BITS	4
#define DAC5571_QUAD_PWRDWN_BITS	6

static int dac5571_cmd_single(struct dac5571_data *data, int channel, u16 val)
{
	unsigned int shift;

	shift = 12 - data->spec->resolution;
	data->buf[1] = val << shift;
	data->buf[0] = val >> (8 - shift);

	if (i2c_master_send(data->client, data->buf, 2) != 2)
		return -EIO;

	return 0;
}

static int dac5571_cmd_quad(struct dac5571_data *data, int channel, u16 val)
{
	unsigned int shift;

	shift = 16 - data->spec->resolution;
	data->buf[2] = val << shift;
	data->buf[1] = (val >> (8 - shift));
	data->buf[0] = (channel << DAC5571_CHANNEL_SELECT) |
		       DAC5571_LOADMODE_DIRECT;

	if (i2c_master_send(data->client, data->buf, 3) != 3)
		return -EIO;

	return 0;
}

static int dac5571_pwrdwn_single(struct dac5571_data *data, int channel, u8 pwrdwn)
{
	data->buf[1] = 0;
	data->buf[0] = pwrdwn << DAC5571_SINGLE_PWRDWN_BITS;

	if (i2c_master_send(data->client, data->buf, 2) != 2)
		return -EIO;

	return 0;
}

static int dac5571_pwrdwn_quad(struct dac5571_data *data, int channel, u8 pwrdwn)
{
	data->buf[2] = 0;
	data->buf[1] = pwrdwn << DAC5571_QUAD_PWRDWN_BITS;
	data->buf[0] = (channel << DAC5571_CHANNEL_SELECT) |
		       DAC5571_LOADMODE_DIRECT | DAC5571_POWERDOWN_FLAG;

	if (i2c_master_send(data->client, data->buf, 3) != 3)
		return -EIO;

	return 0;
}

static const char *const dac5571_powerdown_modes[] = {
	"1kohm_to_gnd", "100kohm_to_gnd", "three_state",
};

static int dac5571_get_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct dac5571_data *data = iio_priv(indio_dev);

	return data->powerdown_mode[chan->channel];
}

static int dac5571_set_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int mode)
{
	struct dac5571_data *data = iio_priv(indio_dev);
	int ret = 0;

	if (data->powerdown_mode[chan->channel] == mode)
		return 0;

	mutex_lock(&data->lock);
	if (data->powerdown[chan->channel]) {
		ret = data->dac5571_pwrdwn(data, chan->channel,
					   DAC5571_POWERDOWN(mode));
		if (ret)
			goto out;
	}
	data->powerdown_mode[chan->channel] = mode;

 out:
	mutex_unlock(&data->lock);

	return ret;
}

static const struct iio_enum dac5571_powerdown_mode = {
	.items = dac5571_powerdown_modes,
	.num_items = ARRAY_SIZE(dac5571_powerdown_modes),
	.get = dac5571_get_powerdown_mode,
	.set = dac5571_set_powerdown_mode,
};

static ssize_t dac5571_read_powerdown(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      char *buf)
{
	struct dac5571_data *data = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", data->powerdown[chan->channel]);
}

static ssize_t dac5571_write_powerdown(struct iio_dev *indio_dev,
				       uintptr_t private,
				       const struct iio_chan_spec *chan,
				       const char *buf, size_t len)
{
	struct dac5571_data *data = iio_priv(indio_dev);
	bool powerdown;
	int ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	if (data->powerdown[chan->channel] == powerdown)
		return len;

	mutex_lock(&data->lock);
	if (powerdown)
		ret = data->dac5571_pwrdwn(data, chan->channel,
			    DAC5571_POWERDOWN(data->powerdown_mode[chan->channel]));
	else
		ret = data->dac5571_cmd(data, chan->channel,
				data->val[chan->channel]);
	if (ret)
		goto out;

	data->powerdown[chan->channel] = powerdown;

 out:
	mutex_unlock(&data->lock);

	return ret ? ret : len;
}


static const struct iio_chan_spec_ext_info dac5571_ext_info[] = {
	{
		.name	   = "powerdown",
		.read	   = dac5571_read_powerdown,
		.write	   = dac5571_write_powerdown,
		.shared	   = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &dac5571_powerdown_mode),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE, &dac5571_powerdown_mode),
	{},
};

#define dac5571_CHANNEL(chan, name) {				\
	.type = IIO_VOLTAGE,					\
	.channel = (chan),					\
	.address = (chan),					\
	.indexed = true,					\
	.output = true,						\
	.datasheet_name = name,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = dac5571_ext_info,				\
}

static const struct iio_chan_spec dac5571_channels[] = {
	dac5571_CHANNEL(0, "A"),
	dac5571_CHANNEL(1, "B"),
	dac5571_CHANNEL(2, "C"),
	dac5571_CHANNEL(3, "D"),
};

static int dac5571_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct dac5571_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = data->val[chan->channel];
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(data->vref);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = data->spec->resolution;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static int dac5571_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct dac5571_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (data->val[chan->channel] == val)
			return 0;

		if (val >= (1 << data->spec->resolution) || val < 0)
			return -EINVAL;

		if (data->powerdown[chan->channel])
			return -EBUSY;

		mutex_lock(&data->lock);
		ret = data->dac5571_cmd(data, chan->channel, val);
		if (ret == 0)
			data->val[chan->channel] = val;
		mutex_unlock(&data->lock);
		return ret;

	default:
		return -EINVAL;
	}
}

static int dac5571_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	return IIO_VAL_INT;
}

static const struct iio_info dac5571_info = {
	.read_raw = dac5571_read_raw,
	.write_raw = dac5571_write_raw,
	.write_raw_get_fmt = dac5571_write_raw_get_fmt,
};

static int dac5571_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	const struct dac5571_spec *spec;
	struct dac5571_data *data;
	struct iio_dev *indio_dev;
	int ret, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &dac5571_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dac5571_channels;

	spec = i2c_get_match_data(client);

	indio_dev->num_channels = spec->num_channels;
	data->spec = spec;

	data->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(data->vref))
		return PTR_ERR(data->vref);

	ret = regulator_enable(data->vref);
	if (ret < 0)
		return ret;

	mutex_init(&data->lock);

	switch (spec->num_channels) {
	case 1:
		data->dac5571_cmd = dac5571_cmd_single;
		data->dac5571_pwrdwn = dac5571_pwrdwn_single;
		break;
	case 4:
		data->dac5571_cmd = dac5571_cmd_quad;
		data->dac5571_pwrdwn = dac5571_pwrdwn_quad;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	for (i = 0; i < spec->num_channels; i++) {
		ret = data->dac5571_cmd(data, i, 0);
		if (ret) {
			dev_err(dev, "failed to initialize channel %d to 0\n", i);
			goto err;
		}
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err;

	return 0;

 err:
	regulator_disable(data->vref);
	return ret;
}

static void dac5571_remove(struct i2c_client *i2c)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(i2c);
	struct dac5571_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(data->vref);
}

static const struct of_device_id dac5571_of_id[] = {
	{.compatible = "ti,dac121c081", .data = &dac5571_spec[single_12bit] },
	{.compatible = "ti,dac5571", .data = &dac5571_spec[single_8bit] },
	{.compatible = "ti,dac6571", .data = &dac5571_spec[single_10bit] },
	{.compatible = "ti,dac7571", .data = &dac5571_spec[single_12bit] },
	{.compatible = "ti,dac5574", .data = &dac5571_spec[quad_8bit] },
	{.compatible = "ti,dac6574", .data = &dac5571_spec[quad_10bit] },
	{.compatible = "ti,dac7574", .data = &dac5571_spec[quad_12bit] },
	{.compatible = "ti,dac5573", .data = &dac5571_spec[quad_8bit] },
	{.compatible = "ti,dac6573", .data = &dac5571_spec[quad_10bit] },
	{.compatible = "ti,dac7573", .data = &dac5571_spec[quad_12bit] },
	{}
};
MODULE_DEVICE_TABLE(of, dac5571_of_id);

static const struct i2c_device_id dac5571_id[] = {
	{"dac121c081", (kernel_ulong_t)&dac5571_spec[single_12bit] },
	{"dac5571", (kernel_ulong_t)&dac5571_spec[single_8bit] },
	{"dac6571", (kernel_ulong_t)&dac5571_spec[single_10bit] },
	{"dac7571", (kernel_ulong_t)&dac5571_spec[single_12bit] },
	{"dac5574", (kernel_ulong_t)&dac5571_spec[quad_8bit] },
	{"dac6574", (kernel_ulong_t)&dac5571_spec[quad_10bit] },
	{"dac7574", (kernel_ulong_t)&dac5571_spec[quad_12bit] },
	{"dac5573", (kernel_ulong_t)&dac5571_spec[quad_8bit] },
	{"dac6573", (kernel_ulong_t)&dac5571_spec[quad_10bit] },
	{"dac7573", (kernel_ulong_t)&dac5571_spec[quad_12bit] },
	{}
};
MODULE_DEVICE_TABLE(i2c, dac5571_id);

static struct i2c_driver dac5571_driver = {
	.driver = {
		   .name = "ti-dac5571",
		   .of_match_table = dac5571_of_id,
	},
	.probe = dac5571_probe,
	.remove   = dac5571_remove,
	.id_table = dac5571_id,
};
module_i2c_driver(dac5571_driver);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.dk>");
MODULE_DESCRIPTION("Texas Instruments 8/10/12-bit 1/4-channel DAC driver");
MODULE_LICENSE("GPL v2");
