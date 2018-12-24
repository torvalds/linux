// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Ziyuan Xu <xzy.xu@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>

/**
 * id:			the index of analog switch inputs
 * saradc_chan_id:	the index of analog switch 'x' output
 * gpio_mask:		set the value of switch-gpios with mask, that
 *			makes the 'id' connect to 'saradc_chan_id'.
 */
struct gpio_muxadc_chan_data {
	u32 id;
	u32 saradc_chan_id;
	u32 gpio_mask;
};

#define MUXADC_CHANNEL(_index, _id, _mask) {		\
	.id = _index,					\
	.saradc_chan_id = _id,				\
	.gpio_mask = _mask,				\
}

/**
 * nr_chans:		the number of analog switch 'x' output
 * saradc_nr_chans:	the number of analog switch inputs
 * chans:		pointer to get the muxadc channel information
 */
struct gpio_muxadc_data {
	u32 nr_chans;
	u32 saradc_nr_chans;
	const struct gpio_muxadc_chan_data *chans;
};

/**
 * gpios:		pointer of digital enable input gpios
 * adc_chans:		pointer of the 'saraadc' channel
 * muxchans:		specification of a single analog switch 'x'
 *			output channel
 * data:		pointer to get the muxadc channels information
 * nr_chans:		the number of analog switch 'x' output
 */
struct gpio_muxadc {
	struct gpio_descs *gpios;
	struct iio_channel *adc_chans;
	struct iio_chan_spec *muxchans;
	const struct gpio_muxadc_data *data;
	u32 nr_chans;
};

static int gpio_muxadc_chan_read_by_index(struct gpio_muxadc *muxadc,
					      int index, int *val)
{
	struct iio_channel *saradc_chan;
	const struct gpio_muxadc_chan_data *chan_data;
	u32 i, saradc_chan_id;

	chan_data = &muxadc->data->chans[index];
	for (i = 0; i < muxadc->gpios->ndescs; i++) {
		struct gpio_desc *gpiod = muxadc->gpios->desc[i];
		int gpio_val = chan_data->gpio_mask & BIT(i) ? 1 : 0;

		gpiod_set_value(gpiod, gpio_val);
	}

	saradc_chan_id = chan_data->saradc_chan_id;
	saradc_chan = &muxadc->adc_chans[saradc_chan_id];
	return iio_read_channel_raw(saradc_chan, val);
}

static int gpio_muxadc_read_raw(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    int *val, int *val2, long mask)
{
	struct gpio_muxadc *muxadc = iio_priv(indio_dev);
	int ret = IIO_VAL_INT;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = gpio_muxadc_chan_read_by_index(muxadc,
						     chan->channel, val);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct iio_info gpio_muxadc_iio_info = {
	.read_raw = gpio_muxadc_read_raw,
};

static const struct gpio_muxadc_chan_data mux_sgm3699_chans_data[] = {
	MUXADC_CHANNEL(0, 0, 0b00),
	MUXADC_CHANNEL(1, 1, 0b00),
	MUXADC_CHANNEL(2, 2, 0b00),
	MUXADC_CHANNEL(3, 3, 0b00),
	MUXADC_CHANNEL(4, 0, 0b01),
	MUXADC_CHANNEL(5, 1, 0b01),
	MUXADC_CHANNEL(6, 2, 0b11),
	MUXADC_CHANNEL(7, 3, 0b11),
};

static const struct gpio_muxadc_data mux_sgm3699_data = {
	.saradc_nr_chans = 4,
	.nr_chans = ARRAY_SIZE(mux_sgm3699_chans_data),
	.chans = mux_sgm3699_chans_data,
};

static const struct gpio_muxadc_chan_data mux_sgm48752_chans_data[] = {
	MUXADC_CHANNEL(0, 0, 0b00),
	MUXADC_CHANNEL(1, 1, 0b00),
	MUXADC_CHANNEL(2, 0, 0b10),
	MUXADC_CHANNEL(3, 1, 0b10),
	MUXADC_CHANNEL(4, 0, 0b01),
	MUXADC_CHANNEL(5, 1, 0b01),
	MUXADC_CHANNEL(6, 0, 0b11),
	MUXADC_CHANNEL(7, 1, 0b11),
};

static const struct gpio_muxadc_data mux_sgm48752_data = {
	.saradc_nr_chans = 2,
	.nr_chans = ARRAY_SIZE(mux_sgm48752_chans_data),
	.chans = mux_sgm48752_chans_data,
};

static const struct of_device_id of_gpio_muxadc_match[] = {
	{
		.compatible = "sgm3699",
		.data = &mux_sgm3699_data,
	},
	{
		.compatible = "sgm48752",
		.data = &mux_sgm48752_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_muxadc_match);

static int gpio_muxadc_probe(struct platform_device *pdev)
{
	struct gpio_muxadc *muxadc;
	struct iio_dev *indio_dev;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	u32 i, nr_adc_chans = 0;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*muxadc));
	if (!indio_dev)
		return -ENOMEM;

	muxadc = iio_priv(indio_dev);

	match = of_match_device(of_gpio_muxadc_match, dev);
	if (!match) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}
	muxadc->data = match->data;

	muxadc->gpios = devm_gpiod_get_array(dev, "switch", GPIOD_OUT_LOW);
	if (IS_ERR(muxadc->gpios)) {
		dev_err(dev, "property of switch-gpios not specified\n");
		return PTR_ERR(muxadc->gpios);
	}

	muxadc->adc_chans = iio_channel_get_all(dev);
	if (IS_ERR(muxadc->adc_chans))
		return PTR_ERR(muxadc->adc_chans);
	/*
	 * It's necessary to get the number of input ADC, and make a
	 * comparison with chan_data->saradc_nr_chans. Otherwise it
	 * might fall in to trap.
	 */
	while (muxadc->adc_chans[nr_adc_chans].indio_dev)
		nr_adc_chans++;
	if (muxadc->data->saradc_nr_chans != nr_adc_chans) {
		dev_err(dev, "the number of io-channels is mismatch\n");
		return -EINVAL;
	}

	muxadc->nr_chans = of_property_count_strings(np, "labels");
	if (muxadc->nr_chans != muxadc->data->nr_chans) {
		dev_err(dev, "should provide %d label\n",
			muxadc->nr_chans);
		return -EINVAL;
	}

	muxadc->muxchans = devm_kcalloc(dev, muxadc->nr_chans,
					sizeof(struct iio_chan_spec),
					GFP_KERNEL);
	if (!muxadc->muxchans)
		return -ENOMEM;

	for (i = 0; i < muxadc->nr_chans; i++) {
		/*
		 * The specification of each muxadc channel will be
		 * in_voltage_<label> without been indexed.
		 */
		muxadc->muxchans[i].type = IIO_VOLTAGE;
		muxadc->muxchans[i].channel = i;
		muxadc->muxchans[i].info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		of_property_read_string_index(np, "labels", i,
					      &muxadc->muxchans[i].extend_name);
	}

	indio_dev->name = dev_name(dev);
	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = dev->of_node;
	indio_dev->info = &gpio_muxadc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = muxadc->muxchans;
	indio_dev->num_channels = muxadc->nr_chans;

	return iio_device_register(indio_dev);
}

static int gpio_muxadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	return 0;
}

static struct platform_driver gpio_muxadc_driver = {
	.probe		= gpio_muxadc_probe,
	.remove		= gpio_muxadc_remove,
	.driver		= {
		.name	= "gpio-muxadc",
		.of_match_table = of_gpio_muxadc_match,
	},
};

module_platform_driver(gpio_muxadc_driver);

MODULE_AUTHOR("Ziyuan Xu <xzy.xu@rock-chips.com>");
MODULE_DESCRIPTION("GPIO MUX ADC driver");
MODULE_LICENSE("GPL v2");
