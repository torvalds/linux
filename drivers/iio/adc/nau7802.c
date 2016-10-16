/*
 * Driver for the Nuvoton NAU7802 ADC
 *
 * Copyright 2013 Free Electrons
 *
 * Licensed under the GPLv2 or later.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/log2.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define NAU7802_REG_PUCTRL	0x00
#define NAU7802_PUCTRL_RR(x)		(x << 0)
#define NAU7802_PUCTRL_RR_BIT		NAU7802_PUCTRL_RR(1)
#define NAU7802_PUCTRL_PUD(x)		(x << 1)
#define NAU7802_PUCTRL_PUD_BIT		NAU7802_PUCTRL_PUD(1)
#define NAU7802_PUCTRL_PUA(x)		(x << 2)
#define NAU7802_PUCTRL_PUA_BIT		NAU7802_PUCTRL_PUA(1)
#define NAU7802_PUCTRL_PUR(x)		(x << 3)
#define NAU7802_PUCTRL_PUR_BIT		NAU7802_PUCTRL_PUR(1)
#define NAU7802_PUCTRL_CS(x)		(x << 4)
#define NAU7802_PUCTRL_CS_BIT		NAU7802_PUCTRL_CS(1)
#define NAU7802_PUCTRL_CR(x)		(x << 5)
#define NAU7802_PUCTRL_CR_BIT		NAU7802_PUCTRL_CR(1)
#define NAU7802_PUCTRL_AVDDS(x)		(x << 7)
#define NAU7802_PUCTRL_AVDDS_BIT	NAU7802_PUCTRL_AVDDS(1)
#define NAU7802_REG_CTRL1	0x01
#define NAU7802_CTRL1_VLDO(x)		(x << 3)
#define NAU7802_CTRL1_GAINS(x)		(x)
#define NAU7802_CTRL1_GAINS_BITS	0x07
#define NAU7802_REG_CTRL2	0x02
#define NAU7802_CTRL2_CHS(x)		(x << 7)
#define NAU7802_CTRL2_CRS(x)		(x << 4)
#define NAU7802_SAMP_FREQ_320	0x07
#define NAU7802_CTRL2_CHS_BIT		NAU7802_CTRL2_CHS(1)
#define NAU7802_REG_ADC_B2	0x12
#define NAU7802_REG_ADC_B1	0x13
#define NAU7802_REG_ADC_B0	0x14
#define NAU7802_REG_ADC_CTRL	0x15

#define NAU7802_MIN_CONVERSIONS 6

struct nau7802_state {
	struct i2c_client	*client;
	s32			last_value;
	struct mutex		lock;
	struct mutex		data_lock;
	u32			vref_mv;
	u32			conversion_count;
	u32			min_conversions;
	u8			sample_rate;
	u32			scale_avail[8];
	struct completion	value_ok;
};

#define NAU7802_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (chan),					\
	.scan_index = (chan),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ)	\
}

static const struct iio_chan_spec nau7802_chan_array[] = {
	NAU7802_CHANNEL(0),
	NAU7802_CHANNEL(1),
};

static const u16 nau7802_sample_freq_avail[] = {10, 20, 40, 80,
						10, 10, 10, 320};

static ssize_t nau7802_show_scales(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct nau7802_state *st = iio_priv(dev_to_iio_dev(dev));
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%09d ",
				 st->scale_avail[i]);

	buf[len-1] = '\n';

	return len;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("10 40 80 320");

static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO, nau7802_show_scales,
		       NULL, 0);

static struct attribute *nau7802_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group nau7802_attribute_group = {
	.attrs = nau7802_attributes,
};

static int nau7802_set_gain(struct nau7802_state *st, int gain)
{
	int ret;

	mutex_lock(&st->lock);
	st->conversion_count = 0;

	ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_CTRL1);
	if (ret < 0)
		goto nau7802_sysfs_set_gain_out;
	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_CTRL1,
					(ret & (~NAU7802_CTRL1_GAINS_BITS)) |
					gain);

nau7802_sysfs_set_gain_out:
	mutex_unlock(&st->lock);

	return ret;
}

static int nau7802_read_conversion(struct nau7802_state *st)
{
	int data;

	mutex_lock(&st->data_lock);
	data = i2c_smbus_read_byte_data(st->client, NAU7802_REG_ADC_B2);
	if (data < 0)
		goto nau7802_read_conversion_out;
	st->last_value = data << 16;

	data = i2c_smbus_read_byte_data(st->client, NAU7802_REG_ADC_B1);
	if (data < 0)
		goto nau7802_read_conversion_out;
	st->last_value |= data << 8;

	data = i2c_smbus_read_byte_data(st->client, NAU7802_REG_ADC_B0);
	if (data < 0)
		goto nau7802_read_conversion_out;
	st->last_value |= data;

	st->last_value = sign_extend32(st->last_value, 23);

nau7802_read_conversion_out:
	mutex_unlock(&st->data_lock);

	return data;
}

/*
 * Conversions are synchronised on the rising edge of NAU7802_PUCTRL_CS_BIT
 */
static int nau7802_sync(struct nau7802_state *st)
{
	int ret;

	ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_PUCTRL);
	if (ret < 0)
		return ret;
	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_PUCTRL,
				ret | NAU7802_PUCTRL_CS_BIT);

	return ret;
}

static irqreturn_t nau7802_eoc_trigger(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct nau7802_state *st = iio_priv(indio_dev);
	int status;

	status = i2c_smbus_read_byte_data(st->client, NAU7802_REG_PUCTRL);
	if (status < 0)
		return IRQ_HANDLED;

	if (!(status & NAU7802_PUCTRL_CR_BIT))
		return IRQ_NONE;

	if (nau7802_read_conversion(st) < 0)
		return IRQ_HANDLED;

	/*
	 * Because there is actually only one ADC for both channels, we have to
	 * wait for enough conversions to happen before getting a significant
	 * value when changing channels and the values are far apart.
	 */
	if (st->conversion_count < NAU7802_MIN_CONVERSIONS)
		st->conversion_count++;
	if (st->conversion_count >= NAU7802_MIN_CONVERSIONS)
		complete(&st->value_ok);

	return IRQ_HANDLED;
}

static int nau7802_read_irq(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val)
{
	struct nau7802_state *st = iio_priv(indio_dev);
	int ret;

	reinit_completion(&st->value_ok);
	enable_irq(st->client->irq);

	nau7802_sync(st);

	/* read registers to ensure we flush everything */
	ret = nau7802_read_conversion(st);
	if (ret < 0)
		goto read_chan_info_failure;

	/* Wait for a conversion to finish */
	ret = wait_for_completion_interruptible_timeout(&st->value_ok,
			msecs_to_jiffies(1000));
	if (ret == 0)
		ret = -ETIMEDOUT;

	if (ret < 0)
		goto read_chan_info_failure;

	disable_irq(st->client->irq);

	*val = st->last_value;

	return IIO_VAL_INT;

read_chan_info_failure:
	disable_irq(st->client->irq);

	return ret;
}

static int nau7802_read_poll(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val)
{
	struct nau7802_state *st = iio_priv(indio_dev);
	int ret;

	nau7802_sync(st);

	/* read registers to ensure we flush everything */
	ret = nau7802_read_conversion(st);
	if (ret < 0)
		return ret;

	/*
	 * Because there is actually only one ADC for both channels, we have to
	 * wait for enough conversions to happen before getting a significant
	 * value when changing channels and the values are far appart.
	 */
	do {
		ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_PUCTRL);
		if (ret < 0)
			return ret;

		while (!(ret & NAU7802_PUCTRL_CR_BIT)) {
			if (st->sample_rate != NAU7802_SAMP_FREQ_320)
				msleep(20);
			else
				mdelay(4);
			ret = i2c_smbus_read_byte_data(st->client,
							NAU7802_REG_PUCTRL);
			if (ret < 0)
				return ret;
		}

		ret = nau7802_read_conversion(st);
		if (ret < 0)
			return ret;
		if (st->conversion_count < NAU7802_MIN_CONVERSIONS)
			st->conversion_count++;
	} while (st->conversion_count < NAU7802_MIN_CONVERSIONS);

	*val = st->last_value;

	return IIO_VAL_INT;
}

static int nau7802_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct nau7802_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		/*
		 * Select the channel to use
		 *   - Channel 1 is value 0 in the CHS register
		 *   - Channel 2 is value 1 in the CHS register
		 */
		ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_CTRL2);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}

		if (((ret & NAU7802_CTRL2_CHS_BIT) && !chan->channel) ||
				(!(ret & NAU7802_CTRL2_CHS_BIT) &&
				 chan->channel)) {
			st->conversion_count = 0;
			ret = i2c_smbus_write_byte_data(st->client,
					NAU7802_REG_CTRL2,
					NAU7802_CTRL2_CHS(chan->channel) |
					NAU7802_CTRL2_CRS(st->sample_rate));

			if (ret < 0) {
				mutex_unlock(&st->lock);
				return ret;
			}
		}

		if (st->client->irq)
			ret = nau7802_read_irq(indio_dev, chan, val);
		else
			ret = nau7802_read_poll(indio_dev, chan, val);

		mutex_unlock(&st->lock);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_CTRL1);
		if (ret < 0)
			return ret;

		/*
		 * We have 24 bits of signed data, that means 23 bits of data
		 * plus the sign bit
		 */
		*val = st->vref_mv;
		*val2 = 23 + (ret & NAU7802_CTRL1_GAINS_BITS);

		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val =  nau7802_sample_freq_avail[st->sample_rate];
		*val2 = 0;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int nau7802_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct nau7802_state *st = iio_priv(indio_dev);
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
			if (val2 == st->scale_avail[i])
				return nau7802_set_gain(st, i);

		break;

	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < ARRAY_SIZE(nau7802_sample_freq_avail); i++)
			if (val == nau7802_sample_freq_avail[i]) {
				mutex_lock(&st->lock);
				st->sample_rate = i;
				st->conversion_count = 0;
				ret = i2c_smbus_write_byte_data(st->client,
					NAU7802_REG_CTRL2,
					NAU7802_CTRL2_CRS(st->sample_rate));
				mutex_unlock(&st->lock);
				return ret;
			}

		break;

	default:
		break;
	}

	return -EINVAL;
}

static int nau7802_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static const struct iio_info nau7802_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &nau7802_read_raw,
	.write_raw = &nau7802_write_raw,
	.write_raw_get_fmt = nau7802_write_raw_get_fmt,
	.attrs = &nau7802_attribute_group,
};

static int nau7802_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct nau7802_state *st;
	struct device_node *np = client->dev.of_node;
	int i, ret;
	u8 data;
	u32 tmp = 0;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "No device tree node available.\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->dev.of_node = client->dev.of_node;
	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &nau7802_info;

	st->client = client;

	/* Reset the device */
	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_PUCTRL,
				  NAU7802_PUCTRL_RR_BIT);
	if (ret < 0)
		return ret;

	/* Enter normal operation mode */
	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_PUCTRL,
				  NAU7802_PUCTRL_PUD_BIT);
	if (ret < 0)
		return ret;

	/*
	 * After about 200 usecs, the device should be ready and then
	 * the Power Up bit will be set to 1. If not, wait for it.
	 */
	udelay(210);
	ret = i2c_smbus_read_byte_data(st->client, NAU7802_REG_PUCTRL);
	if (ret < 0)
		return ret;
	if (!(ret & NAU7802_PUCTRL_PUR_BIT))
		return ret;

	of_property_read_u32(np, "nuvoton,vldo", &tmp);
	st->vref_mv = tmp;

	data = NAU7802_PUCTRL_PUD_BIT | NAU7802_PUCTRL_PUA_BIT |
		NAU7802_PUCTRL_CS_BIT;
	if (tmp >= 2400)
		data |= NAU7802_PUCTRL_AVDDS_BIT;

	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_PUCTRL, data);
	if (ret < 0)
		return ret;
	ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_ADC_CTRL, 0x30);
	if (ret < 0)
		return ret;

	if (tmp >= 2400) {
		data = NAU7802_CTRL1_VLDO((4500 - tmp) / 300);
		ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_CTRL1,
						data);
		if (ret < 0)
			return ret;
	}

	/* Populate available ADC input ranges */
	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
		st->scale_avail[i] = (((u64)st->vref_mv) * 1000000000ULL)
					   >> (23 + i);

	init_completion(&st->value_ok);

	/*
	 * The ADC fires continuously and we can't do anything about
	 * it. So we need to have the IRQ disabled by default, and we
	 * will enable them back when we will need them..
	 */
	if (client->irq) {
		ret = request_threaded_irq(client->irq,
				NULL,
				nau7802_eoc_trigger,
				IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				client->dev.driver->name,
				indio_dev);
		if (ret) {
			/*
			 * What may happen here is that our IRQ controller is
			 * not able to get level interrupt but this is required
			 * by this ADC as when going over 40 sample per second,
			 * the interrupt line may stay high between conversions.
			 * So, we continue no matter what but we switch to
			 * polling mode.
			 */
			dev_info(&client->dev,
				"Failed to allocate IRQ, using polling mode\n");
			client->irq = 0;
		} else
			disable_irq(client->irq);
	}

	if (!client->irq) {
		/*
		 * We are polling, use the fastest sample rate by
		 * default
		 */
		st->sample_rate = NAU7802_SAMP_FREQ_320;
		ret = i2c_smbus_write_byte_data(st->client, NAU7802_REG_CTRL2,
					  NAU7802_CTRL2_CRS(st->sample_rate));
		if (ret)
			goto error_free_irq;
	}

	/* Setup the ADC channels available on the board */
	indio_dev->num_channels = ARRAY_SIZE(nau7802_chan_array);
	indio_dev->channels = nau7802_chan_array;

	mutex_init(&st->lock);
	mutex_init(&st->data_lock);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Couldn't register the device.\n");
		goto error_device_register;
	}

	return 0;

error_device_register:
	mutex_destroy(&st->lock);
	mutex_destroy(&st->data_lock);
error_free_irq:
	if (client->irq)
		free_irq(client->irq, indio_dev);

	return ret;
}

static int nau7802_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct nau7802_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	mutex_destroy(&st->lock);
	mutex_destroy(&st->data_lock);
	if (client->irq)
		free_irq(client->irq, indio_dev);

	return 0;
}

static const struct i2c_device_id nau7802_i2c_id[] = {
	{ "nau7802", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nau7802_i2c_id);

static const struct of_device_id nau7802_dt_ids[] = {
	{ .compatible = "nuvoton,nau7802" },
	{},
};
MODULE_DEVICE_TABLE(of, nau7802_dt_ids);

static struct i2c_driver nau7802_driver = {
	.probe = nau7802_probe,
	.remove = nau7802_remove,
	.id_table = nau7802_i2c_id,
	.driver = {
		   .name = "nau7802",
		   .of_match_table = nau7802_dt_ids,
	},
};

module_i2c_driver(nau7802_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nuvoton NAU7802 ADC Driver");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@free-electrons.com>");
