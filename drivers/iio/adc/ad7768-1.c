// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7768-1 SPI ADC driver
 *
 * Copyright 2017 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

/* AD7768 registers definition */
#define AD7768_REG_CHIP_TYPE		0x3
#define AD7768_REG_PROD_ID_L		0x4
#define AD7768_REG_PROD_ID_H		0x5
#define AD7768_REG_CHIP_GRADE		0x6
#define AD7768_REG_SCRATCH_PAD		0x0A
#define AD7768_REG_VENDOR_L		0x0C
#define AD7768_REG_VENDOR_H		0x0D
#define AD7768_REG_INTERFACE_FORMAT	0x14
#define AD7768_REG_POWER_CLOCK		0x15
#define AD7768_REG_ANALOG		0x16
#define AD7768_REG_ANALOG2		0x17
#define AD7768_REG_CONVERSION		0x18
#define AD7768_REG_DIGITAL_FILTER	0x19
#define AD7768_REG_SINC3_DEC_RATE_MSB	0x1A
#define AD7768_REG_SINC3_DEC_RATE_LSB	0x1B
#define AD7768_REG_DUTY_CYCLE_RATIO	0x1C
#define AD7768_REG_SYNC_RESET		0x1D
#define AD7768_REG_GPIO_CONTROL		0x1E
#define AD7768_REG_GPIO_WRITE		0x1F
#define AD7768_REG_GPIO_READ		0x20
#define AD7768_REG_OFFSET_HI		0x21
#define AD7768_REG_OFFSET_MID		0x22
#define AD7768_REG_OFFSET_LO		0x23
#define AD7768_REG_GAIN_HI		0x24
#define AD7768_REG_GAIN_MID		0x25
#define AD7768_REG_GAIN_LO		0x26
#define AD7768_REG_SPI_DIAG_ENABLE	0x28
#define AD7768_REG_ADC_DIAG_ENABLE	0x29
#define AD7768_REG_DIG_DIAG_ENABLE	0x2A
#define AD7768_REG_ADC_DATA		0x2C
#define AD7768_REG_MASTER_STATUS	0x2D
#define AD7768_REG_SPI_DIAG_STATUS	0x2E
#define AD7768_REG_ADC_DIAG_STATUS	0x2F
#define AD7768_REG_DIG_DIAG_STATUS	0x30
#define AD7768_REG_MCLK_COUNTER		0x31

/* AD7768_REG_POWER_CLOCK */
#define AD7768_PWR_MCLK_DIV_MSK		GENMASK(5, 4)
#define AD7768_PWR_MCLK_DIV(x)		FIELD_PREP(AD7768_PWR_MCLK_DIV_MSK, x)
#define AD7768_PWR_PWRMODE_MSK		GENMASK(1, 0)
#define AD7768_PWR_PWRMODE(x)		FIELD_PREP(AD7768_PWR_PWRMODE_MSK, x)

/* AD7768_REG_DIGITAL_FILTER */
#define AD7768_DIG_FIL_FIL_MSK		GENMASK(6, 4)
#define AD7768_DIG_FIL_FIL(x)		FIELD_PREP(AD7768_DIG_FIL_FIL_MSK, x)
#define AD7768_DIG_FIL_DEC_MSK		GENMASK(2, 0)
#define AD7768_DIG_FIL_DEC_RATE(x)	FIELD_PREP(AD7768_DIG_FIL_DEC_MSK, x)

/* AD7768_REG_CONVERSION */
#define AD7768_CONV_MODE_MSK		GENMASK(2, 0)
#define AD7768_CONV_MODE(x)		FIELD_PREP(AD7768_CONV_MODE_MSK, x)

#define AD7768_RD_FLAG_MSK(x)		(BIT(6) | ((x) & 0x3F))
#define AD7768_WR_FLAG_MSK(x)		((x) & 0x3F)

enum ad7768_conv_mode {
	AD7768_CONTINUOUS,
	AD7768_ONE_SHOT,
	AD7768_SINGLE,
	AD7768_PERIODIC,
	AD7768_STANDBY
};

enum ad7768_pwrmode {
	AD7768_ECO_MODE = 0,
	AD7768_MED_MODE = 2,
	AD7768_FAST_MODE = 3
};

enum ad7768_mclk_div {
	AD7768_MCLK_DIV_16,
	AD7768_MCLK_DIV_8,
	AD7768_MCLK_DIV_4,
	AD7768_MCLK_DIV_2
};

enum ad7768_dec_rate {
	AD7768_DEC_RATE_32 = 0,
	AD7768_DEC_RATE_64 = 1,
	AD7768_DEC_RATE_128 = 2,
	AD7768_DEC_RATE_256 = 3,
	AD7768_DEC_RATE_512 = 4,
	AD7768_DEC_RATE_1024 = 5,
	AD7768_DEC_RATE_8 = 9,
	AD7768_DEC_RATE_16 = 10
};

struct ad7768_clk_configuration {
	enum ad7768_mclk_div mclk_div;
	enum ad7768_dec_rate dec_rate;
	unsigned int clk_div;
	enum ad7768_pwrmode pwrmode;
};

static const struct ad7768_clk_configuration ad7768_clk_config[] = {
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_8, 16,  AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_16, 32,  AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_32, 64, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_64, 128, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_2, AD7768_DEC_RATE_128, 256, AD7768_FAST_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_128, 512, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_256, 1024, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_512, 2048, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_4, AD7768_DEC_RATE_1024, 4096, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_8, AD7768_DEC_RATE_1024, 8192, AD7768_MED_MODE },
	{ AD7768_MCLK_DIV_16, AD7768_DEC_RATE_1024, 16384, AD7768_ECO_MODE },
};

static const struct iio_chan_spec ad7768_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.indexed = 1,
		.channel = 0,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.shift = 8,
			.endianness = IIO_BE,
		},
	},
};

struct ad7768_state {
	struct spi_device *spi;
	struct regulator *vref;
	struct mutex lock;
	struct clk *mclk;
	unsigned int mclk_freq;
	unsigned int samp_freq;
	struct completion completion;
	struct iio_trigger *trig;
	struct gpio_desc *gpio_sync_in;
	const char *labels[ARRAY_SIZE(ad7768_channels)];
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		__be32 d32;
		u8 d8[2];
	} data ____cacheline_aligned;
};

static int ad7768_spi_reg_read(struct ad7768_state *st, unsigned int addr,
			       unsigned int len)
{
	unsigned int shift;
	int ret;

	shift = 32 - (8 * len);
	st->data.d8[0] = AD7768_RD_FLAG_MSK(addr);

	ret = spi_write_then_read(st->spi, st->data.d8, 1,
				  &st->data.d32, len);
	if (ret < 0)
		return ret;

	return (be32_to_cpu(st->data.d32) >> shift);
}

static int ad7768_spi_reg_write(struct ad7768_state *st,
				unsigned int addr,
				unsigned int val)
{
	st->data.d8[0] = AD7768_WR_FLAG_MSK(addr);
	st->data.d8[1] = val & 0xFF;

	return spi_write(st->spi, st->data.d8, 2);
}

static int ad7768_set_mode(struct ad7768_state *st,
			   enum ad7768_conv_mode mode)
{
	int regval;

	regval = ad7768_spi_reg_read(st, AD7768_REG_CONVERSION, 1);
	if (regval < 0)
		return regval;

	regval &= ~AD7768_CONV_MODE_MSK;
	regval |= AD7768_CONV_MODE(mode);

	return ad7768_spi_reg_write(st, AD7768_REG_CONVERSION, regval);
}

static int ad7768_scan_direct(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int readval, ret;

	reinit_completion(&st->completion);

	ret = ad7768_set_mode(st, AD7768_ONE_SHOT);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_timeout(&st->completion,
					  msecs_to_jiffies(1000));
	if (!ret)
		return -ETIMEDOUT;

	readval = ad7768_spi_reg_read(st, AD7768_REG_ADC_DATA, 3);
	if (readval < 0)
		return readval;
	/*
	 * Any SPI configuration of the AD7768-1 can only be
	 * performed in continuous conversion mode.
	 */
	ret = ad7768_set_mode(st, AD7768_CONTINUOUS);
	if (ret < 0)
		return ret;

	return readval;
}

static int ad7768_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	if (readval) {
		ret = ad7768_spi_reg_read(st, reg, 1);
		if (ret < 0)
			goto err_unlock;
		*readval = ret;
		ret = 0;
	} else {
		ret = ad7768_spi_reg_write(st, reg, writeval);
	}
err_unlock:
	mutex_unlock(&st->lock);

	return ret;
}

static int ad7768_set_dig_fil(struct ad7768_state *st,
			      enum ad7768_dec_rate dec_rate)
{
	unsigned int mode;
	int ret;

	if (dec_rate == AD7768_DEC_RATE_8 || dec_rate == AD7768_DEC_RATE_16)
		mode = AD7768_DIG_FIL_FIL(dec_rate);
	else
		mode = AD7768_DIG_FIL_DEC_RATE(dec_rate);

	ret = ad7768_spi_reg_write(st, AD7768_REG_DIGITAL_FILTER, mode);
	if (ret < 0)
		return ret;

	/* A sync-in pulse is required every time the filter dec rate changes */
	gpiod_set_value(st->gpio_sync_in, 1);
	gpiod_set_value(st->gpio_sync_in, 0);

	return 0;
}

static int ad7768_set_freq(struct ad7768_state *st,
			   unsigned int freq)
{
	unsigned int diff_new, diff_old, pwr_mode, i, idx;
	int res, ret;

	diff_old = U32_MAX;
	idx = 0;

	res = DIV_ROUND_CLOSEST(st->mclk_freq, freq);

	/* Find the closest match for the desired sampling frequency */
	for (i = 0; i < ARRAY_SIZE(ad7768_clk_config); i++) {
		diff_new = abs(res - ad7768_clk_config[i].clk_div);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	/*
	 * Set both the mclk_div and pwrmode with a single write to the
	 * POWER_CLOCK register
	 */
	pwr_mode = AD7768_PWR_MCLK_DIV(ad7768_clk_config[idx].mclk_div) |
		   AD7768_PWR_PWRMODE(ad7768_clk_config[idx].pwrmode);
	ret = ad7768_spi_reg_write(st, AD7768_REG_POWER_CLOCK, pwr_mode);
	if (ret < 0)
		return ret;

	ret =  ad7768_set_dig_fil(st, ad7768_clk_config[idx].dec_rate);
	if (ret < 0)
		return ret;

	st->samp_freq = DIV_ROUND_CLOSEST(st->mclk_freq,
					  ad7768_clk_config[idx].clk_div);

	return 0;
}

static ssize_t ad7768_sampling_freq_avail(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int freq;
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(ad7768_clk_config); i++) {
		freq = DIV_ROUND_CLOSEST(st->mclk_freq,
					 ad7768_clk_config[i].clk_div);
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", freq);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(ad7768_sampling_freq_avail);

static int ad7768_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int scale_uv, ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = ad7768_scan_direct(indio_dev);
		if (ret >= 0)
			*val = ret;

		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		scale_uv = regulator_get_voltage(st->vref);
		if (scale_uv < 0)
			return scale_uv;

		*val = (scale_uv * 2) / 1000;
		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->samp_freq;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad7768_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad7768_set_freq(st, val);
	default:
		return -EINVAL;
	}
}

static int ad7768_read_label(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, char *label)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return sprintf(label, "%s\n", st->labels[chan->channel]);
}

static struct attribute *ad7768_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7768_group = {
	.attrs = ad7768_attributes,
};

static const struct iio_info ad7768_info = {
	.attrs = &ad7768_group,
	.read_raw = &ad7768_read_raw,
	.write_raw = &ad7768_write_raw,
	.read_label = ad7768_read_label,
	.debugfs_reg_access = &ad7768_reg_access,
};

static int ad7768_setup(struct ad7768_state *st)
{
	int ret;

	/*
	 * Two writes to the SPI_RESET[1:0] bits are required to initiate
	 * a software reset. The bits must first be set to 11, and then
	 * to 10. When the sequence is detected, the reset occurs.
	 * See the datasheet, page 70.
	 */
	ret = ad7768_spi_reg_write(st, AD7768_REG_SYNC_RESET, 0x3);
	if (ret)
		return ret;

	ret = ad7768_spi_reg_write(st, AD7768_REG_SYNC_RESET, 0x2);
	if (ret)
		return ret;

	st->gpio_sync_in = devm_gpiod_get(&st->spi->dev, "adi,sync-in",
					  GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_sync_in))
		return PTR_ERR(st->gpio_sync_in);

	/* Set the default sampling frequency to 32000 kSPS */
	return ad7768_set_freq(st, 32000);
}

static irqreturn_t ad7768_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	ret = spi_read(st->spi, &st->data.d32, 3);
	if (ret < 0)
		goto err_unlock;

	iio_push_to_buffers_with_timestamp(indio_dev, &st->data.d32,
					   iio_get_time_ns(indio_dev));

	iio_trigger_notify_done(indio_dev->trig);
err_unlock:
	mutex_unlock(&st->lock);

	return IRQ_HANDLED;
}

static irqreturn_t ad7768_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad7768_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev))
		iio_trigger_poll(st->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
};

static int ad7768_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	/*
	 * Write a 1 to the LSB of the INTERFACE_FORMAT register to enter
	 * continuous read mode. Subsequent data reads do not require an
	 * initial 8-bit write to query the ADC_DATA register.
	 */
	return ad7768_spi_reg_write(st, AD7768_REG_INTERFACE_FORMAT, 0x01);
}

static int ad7768_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	/*
	 * To exit continuous read mode, perform a single read of the ADC_DATA
	 * reg (0x2C), which allows further configuration of the device.
	 */
	return ad7768_spi_reg_read(st, AD7768_REG_ADC_DATA, 3);
}

static const struct iio_buffer_setup_ops ad7768_buffer_ops = {
	.postenable = &ad7768_buffer_postenable,
	.predisable = &ad7768_buffer_predisable,
};

static const struct iio_trigger_ops ad7768_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static void ad7768_regulator_disable(void *data)
{
	struct ad7768_state *st = data;

	regulator_disable(st->vref);
}

static void ad7768_clk_disable(void *data)
{
	struct ad7768_state *st = data;

	clk_disable_unprepare(st->mclk);
}

static int ad7768_set_channel_label(struct iio_dev *indio_dev,
						int num_channels)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	struct device *device = indio_dev->dev.parent;
	struct fwnode_handle *fwnode;
	struct fwnode_handle *child;
	const char *label;
	int crt_ch = 0;

	fwnode = dev_fwnode(device);
	fwnode_for_each_child_node(fwnode, child) {
		if (fwnode_property_read_u32(child, "reg", &crt_ch))
			continue;

		if (crt_ch >= num_channels)
			continue;

		if (fwnode_property_read_string(child, "label", &label))
			continue;

		st->labels[crt_ch] = label;
	}

	return 0;
}

static int ad7768_probe(struct spi_device *spi)
{
	struct ad7768_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	st->vref = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(st->vref))
		return PTR_ERR(st->vref);

	ret = regulator_enable(st->vref);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable specified vref supply\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&spi->dev, ad7768_regulator_disable, st);
	if (ret)
		return ret;

	st->mclk = devm_clk_get(&spi->dev, "mclk");
	if (IS_ERR(st->mclk))
		return PTR_ERR(st->mclk);

	ret = clk_prepare_enable(st->mclk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ad7768_clk_disable, st);
	if (ret)
		return ret;

	st->mclk_freq = clk_get_rate(st->mclk);

	spi_set_drvdata(spi, indio_dev);
	mutex_init(&st->lock);

	indio_dev->channels = ad7768_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7768_channels);
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad7768_info;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;

	ret = ad7768_setup(st);
	if (ret < 0) {
		dev_err(&spi->dev, "AD7768 setup failed\n");
		return ret;
	}

	st->trig = devm_iio_trigger_alloc(&spi->dev, "%s-dev%d",
					  indio_dev->name, indio_dev->id);
	if (!st->trig)
		return -ENOMEM;

	st->trig->ops = &ad7768_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = devm_iio_trigger_register(&spi->dev, st->trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(st->trig);

	init_completion(&st->completion);

	ret = ad7768_set_channel_label(indio_dev, ARRAY_SIZE(ad7768_channels));
	if (ret)
		return ret;

	ret = devm_request_irq(&spi->dev, spi->irq,
			       &ad7768_interrupt,
			       IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			       indio_dev->name, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					      &iio_pollfunc_store_time,
					      &ad7768_trigger_handler,
					      &ad7768_buffer_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7768_id_table[] = {
	{ "ad7768-1", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad7768_id_table);

static const struct of_device_id ad7768_of_match[] = {
	{ .compatible = "adi,ad7768-1" },
	{ },
};
MODULE_DEVICE_TABLE(of, ad7768_of_match);

static struct spi_driver ad7768_driver = {
	.driver = {
		.name = "ad7768-1",
		.of_match_table = ad7768_of_match,
	},
	.probe = ad7768_probe,
	.id_table = ad7768_id_table,
};
module_spi_driver(ad7768_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7768-1 ADC driver");
MODULE_LICENSE("GPL v2");
