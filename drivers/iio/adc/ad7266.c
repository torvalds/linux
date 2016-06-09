/*
 * AD7266/65 SPI ADC driver
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/module.h>

#include <linux/interrupt.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/platform_data/ad7266.h>

struct ad7266_state {
	struct spi_device	*spi;
	struct regulator	*reg;
	unsigned long		vref_mv;

	struct spi_transfer	single_xfer[3];
	struct spi_message	single_msg;

	enum ad7266_range	range;
	enum ad7266_mode	mode;
	bool			fixed_addr;
	struct gpio		gpios[3];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 * The buffer needs to be large enough to hold two samples (4 bytes) and
	 * the naturally aligned timestamp (8 bytes).
	 */
	struct {
		__be16 sample[2];
		s64 timestamp;
	} data ____cacheline_aligned;
};

static int ad7266_wakeup(struct ad7266_state *st)
{
	/* Any read with >= 2 bytes will wake the device */
	return spi_read(st->spi, &st->data.sample[0], 2);
}

static int ad7266_powerdown(struct ad7266_state *st)
{
	/* Any read with < 2 bytes will powerdown the device */
	return spi_read(st->spi, &st->data.sample[0], 1);
}

static int ad7266_preenable(struct iio_dev *indio_dev)
{
	struct ad7266_state *st = iio_priv(indio_dev);
	return ad7266_wakeup(st);
}

static int ad7266_postdisable(struct iio_dev *indio_dev)
{
	struct ad7266_state *st = iio_priv(indio_dev);
	return ad7266_powerdown(st);
}

static const struct iio_buffer_setup_ops iio_triggered_buffer_setup_ops = {
	.preenable = &ad7266_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &ad7266_postdisable,
};

static irqreturn_t ad7266_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7266_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_read(st->spi, st->data.sample, 4);
	if (ret == 0) {
		iio_push_to_buffers_with_timestamp(indio_dev, &st->data,
			    pf->timestamp);
	}

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void ad7266_select_input(struct ad7266_state *st, unsigned int nr)
{
	unsigned int i;

	if (st->fixed_addr)
		return;

	switch (st->mode) {
	case AD7266_MODE_SINGLE_ENDED:
		nr >>= 1;
		break;
	case AD7266_MODE_PSEUDO_DIFF:
		nr |= 1;
		break;
	case AD7266_MODE_DIFF:
		nr &= ~1;
		break;
	}

	for (i = 0; i < 3; ++i)
		gpio_set_value(st->gpios[i].gpio, (bool)(nr & BIT(i)));
}

static int ad7266_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct ad7266_state *st = iio_priv(indio_dev);
	unsigned int nr = find_first_bit(scan_mask, indio_dev->masklength);

	ad7266_select_input(st, nr);

	return 0;
}

static int ad7266_read_single(struct ad7266_state *st, int *val,
	unsigned int address)
{
	int ret;

	ad7266_select_input(st, address);

	ret = spi_sync(st->spi, &st->single_msg);
	*val = be16_to_cpu(st->data.sample[address % 2]);

	return ret;
}

static int ad7266_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long m)
{
	struct ad7266_state *st = iio_priv(indio_dev);
	unsigned long scale_mv;
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = ad7266_read_single(st, val, chan->address);
		iio_device_release_direct_mode(indio_dev);

		*val = (*val >> 2) & 0xfff;
		if (chan->scan_type.sign == 's')
			*val = sign_extend32(*val, 11);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		scale_mv = st->vref_mv;
		if (st->mode == AD7266_MODE_DIFF)
			scale_mv *= 2;
		if (st->range == AD7266_RANGE_2VREF)
			scale_mv *= 2;

		*val = scale_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		if (st->range == AD7266_RANGE_2VREF &&
			st->mode != AD7266_MODE_DIFF)
			*val = 2048;
		else
			*val = 0;
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

#define AD7266_CHAN(_chan, _sign) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = (_chan),				\
	.address = (_chan),				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) \
		| BIT(IIO_CHAN_INFO_OFFSET),			\
	.scan_index = (_chan),				\
	.scan_type = {					\
		.sign = (_sign),			\
		.realbits = 12,				\
		.storagebits = 16,			\
		.shift = 2,				\
		.endianness = IIO_BE,			\
	},						\
}

#define AD7266_DECLARE_SINGLE_ENDED_CHANNELS(_name, _sign) \
const struct iio_chan_spec ad7266_channels_##_name[] = { \
	AD7266_CHAN(0, (_sign)), \
	AD7266_CHAN(1, (_sign)), \
	AD7266_CHAN(2, (_sign)), \
	AD7266_CHAN(3, (_sign)), \
	AD7266_CHAN(4, (_sign)), \
	AD7266_CHAN(5, (_sign)), \
	AD7266_CHAN(6, (_sign)), \
	AD7266_CHAN(7, (_sign)), \
	AD7266_CHAN(8, (_sign)), \
	AD7266_CHAN(9, (_sign)), \
	AD7266_CHAN(10, (_sign)), \
	AD7266_CHAN(11, (_sign)), \
	IIO_CHAN_SOFT_TIMESTAMP(13), \
}

#define AD7266_DECLARE_SINGLE_ENDED_CHANNELS_FIXED(_name, _sign) \
const struct iio_chan_spec ad7266_channels_##_name##_fixed[] = { \
	AD7266_CHAN(0, (_sign)), \
	AD7266_CHAN(1, (_sign)), \
	IIO_CHAN_SOFT_TIMESTAMP(2), \
}

static AD7266_DECLARE_SINGLE_ENDED_CHANNELS(u, 'u');
static AD7266_DECLARE_SINGLE_ENDED_CHANNELS(s, 's');
static AD7266_DECLARE_SINGLE_ENDED_CHANNELS_FIXED(u, 'u');
static AD7266_DECLARE_SINGLE_ENDED_CHANNELS_FIXED(s, 's');

#define AD7266_CHAN_DIFF(_chan, _sign) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = (_chan) * 2,				\
	.channel2 = (_chan) * 2 + 1,			\
	.address = (_chan),				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE)	\
		| BIT(IIO_CHAN_INFO_OFFSET),			\
	.scan_index = (_chan),				\
	.scan_type = {					\
		.sign = _sign,			\
		.realbits = 12,				\
		.storagebits = 16,			\
		.shift = 2,				\
		.endianness = IIO_BE,			\
	},						\
	.differential = 1,				\
}

#define AD7266_DECLARE_DIFF_CHANNELS(_name, _sign) \
const struct iio_chan_spec ad7266_channels_diff_##_name[] = { \
	AD7266_CHAN_DIFF(0, (_sign)), \
	AD7266_CHAN_DIFF(1, (_sign)), \
	AD7266_CHAN_DIFF(2, (_sign)), \
	AD7266_CHAN_DIFF(3, (_sign)), \
	AD7266_CHAN_DIFF(4, (_sign)), \
	AD7266_CHAN_DIFF(5, (_sign)), \
	IIO_CHAN_SOFT_TIMESTAMP(6), \
}

static AD7266_DECLARE_DIFF_CHANNELS(s, 's');
static AD7266_DECLARE_DIFF_CHANNELS(u, 'u');

#define AD7266_DECLARE_DIFF_CHANNELS_FIXED(_name, _sign) \
const struct iio_chan_spec ad7266_channels_diff_fixed_##_name[] = { \
	AD7266_CHAN_DIFF(0, (_sign)), \
	AD7266_CHAN_DIFF(1, (_sign)), \
	IIO_CHAN_SOFT_TIMESTAMP(2), \
}

static AD7266_DECLARE_DIFF_CHANNELS_FIXED(s, 's');
static AD7266_DECLARE_DIFF_CHANNELS_FIXED(u, 'u');

static const struct iio_info ad7266_info = {
	.read_raw = &ad7266_read_raw,
	.update_scan_mode = &ad7266_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static const unsigned long ad7266_available_scan_masks[] = {
	0x003,
	0x00c,
	0x030,
	0x0c0,
	0x300,
	0xc00,
	0x000,
};

static const unsigned long ad7266_available_scan_masks_diff[] = {
	0x003,
	0x00c,
	0x030,
	0x000,
};

static const unsigned long ad7266_available_scan_masks_fixed[] = {
	0x003,
	0x000,
};

struct ad7266_chan_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	const unsigned long *scan_masks;
};

#define AD7266_CHAN_INFO_INDEX(_differential, _signed, _fixed) \
	(((_differential) << 2) | ((_signed) << 1) | ((_fixed) << 0))

static const struct ad7266_chan_info ad7266_chan_infos[] = {
	[AD7266_CHAN_INFO_INDEX(0, 0, 0)] = {
		.channels = ad7266_channels_u,
		.num_channels = ARRAY_SIZE(ad7266_channels_u),
		.scan_masks = ad7266_available_scan_masks,
	},
	[AD7266_CHAN_INFO_INDEX(0, 0, 1)] = {
		.channels = ad7266_channels_u_fixed,
		.num_channels = ARRAY_SIZE(ad7266_channels_u_fixed),
		.scan_masks = ad7266_available_scan_masks_fixed,
	},
	[AD7266_CHAN_INFO_INDEX(0, 1, 0)] = {
		.channels = ad7266_channels_s,
		.num_channels = ARRAY_SIZE(ad7266_channels_s),
		.scan_masks = ad7266_available_scan_masks,
	},
	[AD7266_CHAN_INFO_INDEX(0, 1, 1)] = {
		.channels = ad7266_channels_s_fixed,
		.num_channels = ARRAY_SIZE(ad7266_channels_s_fixed),
		.scan_masks = ad7266_available_scan_masks_fixed,
	},
	[AD7266_CHAN_INFO_INDEX(1, 0, 0)] = {
		.channels = ad7266_channels_diff_u,
		.num_channels = ARRAY_SIZE(ad7266_channels_diff_u),
		.scan_masks = ad7266_available_scan_masks_diff,
	},
	[AD7266_CHAN_INFO_INDEX(1, 0, 1)] = {
		.channels = ad7266_channels_diff_fixed_u,
		.num_channels = ARRAY_SIZE(ad7266_channels_diff_fixed_u),
		.scan_masks = ad7266_available_scan_masks_fixed,
	},
	[AD7266_CHAN_INFO_INDEX(1, 1, 0)] = {
		.channels = ad7266_channels_diff_s,
		.num_channels = ARRAY_SIZE(ad7266_channels_diff_s),
		.scan_masks = ad7266_available_scan_masks_diff,
	},
	[AD7266_CHAN_INFO_INDEX(1, 1, 1)] = {
		.channels = ad7266_channels_diff_fixed_s,
		.num_channels = ARRAY_SIZE(ad7266_channels_diff_fixed_s),
		.scan_masks = ad7266_available_scan_masks_fixed,
	},
};

static void ad7266_init_channels(struct iio_dev *indio_dev)
{
	struct ad7266_state *st = iio_priv(indio_dev);
	bool is_differential, is_signed;
	const struct ad7266_chan_info *chan_info;
	int i;

	is_differential = st->mode != AD7266_MODE_SINGLE_ENDED;
	is_signed = (st->range == AD7266_RANGE_2VREF) |
		    (st->mode == AD7266_MODE_DIFF);

	i = AD7266_CHAN_INFO_INDEX(is_differential, is_signed, st->fixed_addr);
	chan_info = &ad7266_chan_infos[i];

	indio_dev->channels = chan_info->channels;
	indio_dev->num_channels = chan_info->num_channels;
	indio_dev->available_scan_masks = chan_info->scan_masks;
	indio_dev->masklength = chan_info->num_channels - 1;
}

static const char * const ad7266_gpio_labels[] = {
	"AD0", "AD1", "AD2",
};

static int ad7266_probe(struct spi_device *spi)
{
	struct ad7266_platform_data *pdata = spi->dev.platform_data;
	struct iio_dev *indio_dev;
	struct ad7266_state *st;
	unsigned int i;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = devm_regulator_get(&spi->dev, "vref");
	if (!IS_ERR_OR_NULL(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			return ret;

		ret = regulator_get_voltage(st->reg);
		if (ret < 0)
			goto error_disable_reg;

		st->vref_mv = ret / 1000;
	} else {
		/* Use internal reference */
		st->vref_mv = 2500;
	}

	if (pdata) {
		st->fixed_addr = pdata->fixed_addr;
		st->mode = pdata->mode;
		st->range = pdata->range;

		if (!st->fixed_addr) {
			for (i = 0; i < ARRAY_SIZE(st->gpios); ++i) {
				st->gpios[i].gpio = pdata->addr_gpios[i];
				st->gpios[i].flags = GPIOF_OUT_INIT_LOW;
				st->gpios[i].label = ad7266_gpio_labels[i];
			}
			ret = gpio_request_array(st->gpios,
				ARRAY_SIZE(st->gpios));
			if (ret)
				goto error_disable_reg;
		}
	} else {
		st->fixed_addr = true;
		st->range = AD7266_RANGE_VREF;
		st->mode = AD7266_MODE_DIFF;
	}

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad7266_info;

	ad7266_init_channels(indio_dev);

	/* wakeup */
	st->single_xfer[0].rx_buf = &st->data.sample[0];
	st->single_xfer[0].len = 2;
	st->single_xfer[0].cs_change = 1;
	/* conversion */
	st->single_xfer[1].rx_buf = st->data.sample;
	st->single_xfer[1].len = 4;
	st->single_xfer[1].cs_change = 1;
	/* powerdown */
	st->single_xfer[2].tx_buf = &st->data.sample[0];
	st->single_xfer[2].len = 1;

	spi_message_init(&st->single_msg);
	spi_message_add_tail(&st->single_xfer[0], &st->single_msg);
	spi_message_add_tail(&st->single_xfer[1], &st->single_msg);
	spi_message_add_tail(&st->single_xfer[2], &st->single_msg);

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
		&ad7266_trigger_handler, &iio_triggered_buffer_setup_ops);
	if (ret)
		goto error_free_gpios;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_buffer_cleanup;

	return 0;

error_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
error_free_gpios:
	if (!st->fixed_addr)
		gpio_free_array(st->gpios, ARRAY_SIZE(st->gpios));
error_disable_reg:
	if (!IS_ERR_OR_NULL(st->reg))
		regulator_disable(st->reg);

	return ret;
}

static int ad7266_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7266_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (!st->fixed_addr)
		gpio_free_array(st->gpios, ARRAY_SIZE(st->gpios));
	if (!IS_ERR_OR_NULL(st->reg))
		regulator_disable(st->reg);

	return 0;
}

static const struct spi_device_id ad7266_id[] = {
	{"ad7265", 0},
	{"ad7266", 0},
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7266_id);

static struct spi_driver ad7266_driver = {
	.driver = {
		.name	= "ad7266",
	},
	.probe		= ad7266_probe,
	.remove		= ad7266_remove,
	.id_table	= ad7266_id,
};
module_spi_driver(ad7266_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD7266/65 ADC");
MODULE_LICENSE("GPL v2");
