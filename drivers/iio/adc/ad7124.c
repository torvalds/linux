// SPDX-License-Identifier: GPL-2.0+
/*
 * AD7124 SPI ADC driver
 *
 * Copyright 2018 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/adc/ad_sigma_delta.h>
#include <linux/iio/sysfs.h>

/* AD7124 registers */
#define AD7124_COMMS			0x00
#define AD7124_STATUS			0x00
#define AD7124_ADC_CONTROL		0x01
#define AD7124_DATA			0x02
#define AD7124_IO_CONTROL_1		0x03
#define AD7124_IO_CONTROL_2		0x04
#define AD7124_ID			0x05
#define AD7124_ERROR			0x06
#define AD7124_ERROR_EN		0x07
#define AD7124_MCLK_COUNT		0x08
#define AD7124_CHANNEL(x)		(0x09 + (x))
#define AD7124_CONFIG(x)		(0x19 + (x))
#define AD7124_FILTER(x)		(0x21 + (x))
#define AD7124_OFFSET(x)		(0x29 + (x))
#define AD7124_GAIN(x)			(0x31 + (x))

/* AD7124_STATUS */
#define AD7124_STATUS_POR_FLAG_MSK	BIT(4)

/* AD7124_ADC_CONTROL */
#define AD7124_ADC_CTRL_REF_EN_MSK	BIT(8)
#define AD7124_ADC_CTRL_REF_EN(x)	FIELD_PREP(AD7124_ADC_CTRL_REF_EN_MSK, x)
#define AD7124_ADC_CTRL_PWR_MSK	GENMASK(7, 6)
#define AD7124_ADC_CTRL_PWR(x)		FIELD_PREP(AD7124_ADC_CTRL_PWR_MSK, x)
#define AD7124_ADC_CTRL_MODE_MSK	GENMASK(5, 2)
#define AD7124_ADC_CTRL_MODE(x)	FIELD_PREP(AD7124_ADC_CTRL_MODE_MSK, x)

/* AD7124_CHANNEL_X */
#define AD7124_CHANNEL_EN_MSK		BIT(15)
#define AD7124_CHANNEL_EN(x)		FIELD_PREP(AD7124_CHANNEL_EN_MSK, x)
#define AD7124_CHANNEL_SETUP_MSK	GENMASK(14, 12)
#define AD7124_CHANNEL_SETUP(x)	FIELD_PREP(AD7124_CHANNEL_SETUP_MSK, x)
#define AD7124_CHANNEL_AINP_MSK	GENMASK(9, 5)
#define AD7124_CHANNEL_AINP(x)		FIELD_PREP(AD7124_CHANNEL_AINP_MSK, x)
#define AD7124_CHANNEL_AINM_MSK	GENMASK(4, 0)
#define AD7124_CHANNEL_AINM(x)		FIELD_PREP(AD7124_CHANNEL_AINM_MSK, x)

/* AD7124_CONFIG_X */
#define AD7124_CONFIG_BIPOLAR_MSK	BIT(11)
#define AD7124_CONFIG_BIPOLAR(x)	FIELD_PREP(AD7124_CONFIG_BIPOLAR_MSK, x)
#define AD7124_CONFIG_REF_SEL_MSK	GENMASK(4, 3)
#define AD7124_CONFIG_REF_SEL(x)	FIELD_PREP(AD7124_CONFIG_REF_SEL_MSK, x)
#define AD7124_CONFIG_PGA_MSK		GENMASK(2, 0)
#define AD7124_CONFIG_PGA(x)		FIELD_PREP(AD7124_CONFIG_PGA_MSK, x)
#define AD7124_CONFIG_IN_BUFF_MSK	GENMASK(7, 6)
#define AD7124_CONFIG_IN_BUFF(x)	FIELD_PREP(AD7124_CONFIG_IN_BUFF_MSK, x)

/* AD7124_FILTER_X */
#define AD7124_FILTER_FS_MSK		GENMASK(10, 0)
#define AD7124_FILTER_FS(x)		FIELD_PREP(AD7124_FILTER_FS_MSK, x)

enum ad7124_ids {
	ID_AD7124_4,
	ID_AD7124_8,
};

enum ad7124_ref_sel {
	AD7124_REFIN1,
	AD7124_REFIN2,
	AD7124_INT_REF,
	AD7124_AVDD_REF,
};

enum ad7124_power_mode {
	AD7124_LOW_POWER,
	AD7124_MID_POWER,
	AD7124_FULL_POWER,
};

static const unsigned int ad7124_gain[8] = {
	1, 2, 4, 8, 16, 32, 64, 128
};

static const int ad7124_master_clk_freq_hz[3] = {
	[AD7124_LOW_POWER] = 76800,
	[AD7124_MID_POWER] = 153600,
	[AD7124_FULL_POWER] = 614400,
};

static const char * const ad7124_ref_names[] = {
	[AD7124_REFIN1] = "refin1",
	[AD7124_REFIN2] = "refin2",
	[AD7124_INT_REF] = "int",
	[AD7124_AVDD_REF] = "avdd",
};

struct ad7124_chip_info {
	unsigned int num_inputs;
};

struct ad7124_channel_config {
	enum ad7124_ref_sel refsel;
	bool bipolar;
	bool buf_positive;
	bool buf_negative;
	unsigned int ain;
	unsigned int vref_mv;
	unsigned int pga_bits;
	unsigned int odr;
};

struct ad7124_state {
	const struct ad7124_chip_info *chip_info;
	struct ad_sigma_delta sd;
	struct ad7124_channel_config *channel_config;
	struct regulator *vref[4];
	struct clk *mclk;
	unsigned int adc_control;
	unsigned int num_channels;
};

static const struct iio_chan_spec ad7124_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.differential = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		.sign = 'u',
		.realbits = 24,
		.storagebits = 32,
		.shift = 8,
		.endianness = IIO_BE,
	},
};

static struct ad7124_chip_info ad7124_chip_info_tbl[] = {
	[ID_AD7124_4] = {
		.num_inputs = 8,
	},
	[ID_AD7124_8] = {
		.num_inputs = 16,
	},
};

static int ad7124_find_closest_match(const int *array,
				     unsigned int size, int val)
{
	int i, idx;
	unsigned int diff_new, diff_old;

	diff_old = U32_MAX;
	idx = 0;

	for (i = 0; i < size; i++) {
		diff_new = abs(val - array[i]);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	return idx;
}

static int ad7124_spi_write_mask(struct ad7124_state *st,
				 unsigned int addr,
				 unsigned long mask,
				 unsigned int val,
				 unsigned int bytes)
{
	unsigned int readval;
	int ret;

	ret = ad_sd_read_reg(&st->sd, addr, bytes, &readval);
	if (ret < 0)
		return ret;

	readval &= ~mask;
	readval |= val;

	return ad_sd_write_reg(&st->sd, addr, bytes, readval);
}

static int ad7124_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);

	st->adc_control &= ~AD7124_ADC_CTRL_MODE_MSK;
	st->adc_control |= AD7124_ADC_CTRL_MODE(mode);

	return ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, st->adc_control);
}

static int ad7124_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);
	unsigned int val;

	val = st->channel_config[channel].ain | AD7124_CHANNEL_EN(1) |
	      AD7124_CHANNEL_SETUP(channel);

	return ad_sd_write_reg(&st->sd, AD7124_CHANNEL(channel), 2, val);
}

static const struct ad_sigma_delta_info ad7124_sigma_delta_info = {
	.set_channel = ad7124_set_channel,
	.set_mode = ad7124_set_mode,
	.has_registers = true,
	.addr_shift = 0,
	.read_mask = BIT(6),
	.data_reg = AD7124_DATA,
};

static int ad7124_set_channel_odr(struct ad7124_state *st,
				  unsigned int channel,
				  unsigned int odr)
{
	unsigned int fclk, odr_sel_bits;
	int ret;

	fclk = clk_get_rate(st->mclk);
	/*
	 * FS[10:0] = fCLK / (fADC x 32) where:
	 * fADC is the output data rate
	 * fCLK is the master clock frequency
	 * FS[10:0] are the bits in the filter register
	 * FS[10:0] can have a value from 1 to 2047
	 */
	odr_sel_bits = DIV_ROUND_CLOSEST(fclk, odr * 32);
	if (odr_sel_bits < 1)
		odr_sel_bits = 1;
	else if (odr_sel_bits > 2047)
		odr_sel_bits = 2047;

	ret = ad7124_spi_write_mask(st, AD7124_FILTER(channel),
				    AD7124_FILTER_FS_MSK,
				    AD7124_FILTER_FS(odr_sel_bits), 3);
	if (ret < 0)
		return ret;
	/* fADC = fCLK / (FS[10:0] x 32) */
	st->channel_config[channel].odr =
		DIV_ROUND_CLOSEST(fclk, odr_sel_bits * 32);

	return 0;
}

static int ad7124_set_channel_gain(struct ad7124_state *st,
				   unsigned int channel,
				   unsigned int gain)
{
	unsigned int res;
	int ret;

	res = ad7124_find_closest_match(ad7124_gain,
					ARRAY_SIZE(ad7124_gain), gain);
	ret = ad7124_spi_write_mask(st, AD7124_CONFIG(channel),
				    AD7124_CONFIG_PGA_MSK,
				    AD7124_CONFIG_PGA(res), 2);
	if (ret < 0)
		return ret;

	st->channel_config[channel].pga_bits = res;

	return 0;
}

static int ad7124_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	int idx, ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = ad_sigma_delta_single_conversion(indio_dev, chan, val);
		if (ret < 0)
			return ret;

		/* After the conversion is performed, disable the channel */
		ret = ad_sd_write_reg(&st->sd,
				      AD7124_CHANNEL(chan->address), 2,
				      st->channel_config[chan->address].ain |
				      AD7124_CHANNEL_EN(0));
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		idx = st->channel_config[chan->address].pga_bits;
		*val = st->channel_config[chan->address].vref_mv;
		if (st->channel_config[chan->address].bipolar)
			*val2 = chan->scan_type.realbits - 1 + idx;
		else
			*val2 = chan->scan_type.realbits + idx;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		if (st->channel_config[chan->address].bipolar)
			*val = -(1 << (chan->scan_type.realbits - 1));
		else
			*val = 0;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->channel_config[chan->address].odr;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7124_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	unsigned int res, gain, full_scale, vref;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2 != 0)
			return -EINVAL;

		return ad7124_set_channel_odr(st, chan->address, val);
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		if (st->channel_config[chan->address].bipolar)
			full_scale = 1 << (chan->scan_type.realbits - 1);
		else
			full_scale = 1 << chan->scan_type.realbits;

		vref = st->channel_config[chan->address].vref_mv * 1000000LL;
		res = DIV_ROUND_CLOSEST(vref, full_scale);
		gain = DIV_ROUND_CLOSEST(res, val2);

		return ad7124_set_channel_gain(st, chan->address, gain);
	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR(in_voltage_scale_available,
	"0.000001164 0.000002328 0.000004656 0.000009313 0.000018626 0.000037252 0.000074505 0.000149011 0.000298023");

static struct attribute *ad7124_attributes[] = {
	&iio_const_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7124_attrs_group = {
	.attrs = ad7124_attributes,
};

static const struct iio_info ad7124_info = {
	.read_raw = ad7124_read_raw,
	.write_raw = ad7124_write_raw,
	.validate_trigger = ad_sd_validate_trigger,
	.attrs = &ad7124_attrs_group,
};

static int ad7124_soft_reset(struct ad7124_state *st)
{
	unsigned int readval, timeout;
	int ret;

	ret = ad_sd_reset(&st->sd, 64);
	if (ret < 0)
		return ret;

	timeout = 100;
	do {
		ret = ad_sd_read_reg(&st->sd, AD7124_STATUS, 1, &readval);
		if (ret < 0)
			return ret;

		if (!(readval & AD7124_STATUS_POR_FLAG_MSK))
			return 0;

		/* The AD7124 requires typically 2ms to power up and settle */
		usleep_range(100, 2000);
	} while (--timeout);

	dev_err(&st->sd.spi->dev, "Soft reset failed\n");

	return -EIO;
}

static int ad7124_init_channel_vref(struct ad7124_state *st,
				    unsigned int channel_number)
{
	unsigned int refsel = st->channel_config[channel_number].refsel;

	switch (refsel) {
	case AD7124_REFIN1:
	case AD7124_REFIN2:
	case AD7124_AVDD_REF:
		if (IS_ERR(st->vref[refsel])) {
			dev_err(&st->sd.spi->dev,
				"Error, trying to use external voltage reference without a %s regulator.\n",
				ad7124_ref_names[refsel]);
			return PTR_ERR(st->vref[refsel]);
		}
		st->channel_config[channel_number].vref_mv =
			regulator_get_voltage(st->vref[refsel]);
		/* Conversion from uV to mV */
		st->channel_config[channel_number].vref_mv /= 1000;
		break;
	case AD7124_INT_REF:
		st->channel_config[channel_number].vref_mv = 2500;
		st->adc_control &= ~AD7124_ADC_CTRL_REF_EN_MSK;
		st->adc_control |= AD7124_ADC_CTRL_REF_EN(1);
		return ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL,
				      2, st->adc_control);
	default:
		dev_err(&st->sd.spi->dev, "Invalid reference %d\n", refsel);
		return -EINVAL;
	}

	return 0;
}

static int ad7124_of_parse_channel_config(struct iio_dev *indio_dev,
					  struct device_node *np)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	struct device_node *child;
	struct iio_chan_spec *chan;
	struct ad7124_channel_config *chan_config;
	unsigned int ain[2], channel = 0, tmp;
	int ret;

	st->num_channels = of_get_available_child_count(np);
	if (!st->num_channels) {
		dev_err(indio_dev->dev.parent, "no channel children\n");
		return -ENODEV;
	}

	chan = devm_kcalloc(indio_dev->dev.parent, st->num_channels,
			    sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan_config = devm_kcalloc(indio_dev->dev.parent, st->num_channels,
				   sizeof(*chan_config), GFP_KERNEL);
	if (!chan_config)
		return -ENOMEM;

	indio_dev->channels = chan;
	indio_dev->num_channels = st->num_channels;
	st->channel_config = chan_config;

	for_each_available_child_of_node(np, child) {
		ret = of_property_read_u32(child, "reg", &channel);
		if (ret)
			goto err;

		ret = of_property_read_u32_array(child, "diff-channels",
						 ain, 2);
		if (ret)
			goto err;

		st->channel_config[channel].ain = AD7124_CHANNEL_AINP(ain[0]) |
						  AD7124_CHANNEL_AINM(ain[1]);
		st->channel_config[channel].bipolar =
			of_property_read_bool(child, "bipolar");

		ret = of_property_read_u32(child, "adi,reference-select", &tmp);
		if (ret)
			st->channel_config[channel].refsel = AD7124_INT_REF;
		else
			st->channel_config[channel].refsel = tmp;

		st->channel_config[channel].buf_positive =
			of_property_read_bool(child, "adi,buffered-positive");
		st->channel_config[channel].buf_negative =
			of_property_read_bool(child, "adi,buffered-negative");

		*chan = ad7124_channel_template;
		chan->address = channel;
		chan->scan_index = channel;
		chan->channel = ain[0];
		chan->channel2 = ain[1];

		chan++;
	}

	return 0;
err:
	of_node_put(child);

	return ret;
}

static int ad7124_setup(struct ad7124_state *st)
{
	unsigned int val, fclk, power_mode;
	int i, ret, tmp;

	fclk = clk_get_rate(st->mclk);
	if (!fclk)
		return -EINVAL;

	/* The power mode changes the master clock frequency */
	power_mode = ad7124_find_closest_match(ad7124_master_clk_freq_hz,
					ARRAY_SIZE(ad7124_master_clk_freq_hz),
					fclk);
	if (fclk != ad7124_master_clk_freq_hz[power_mode]) {
		ret = clk_set_rate(st->mclk, fclk);
		if (ret)
			return ret;
	}

	/* Set the power mode */
	st->adc_control &= ~AD7124_ADC_CTRL_PWR_MSK;
	st->adc_control |= AD7124_ADC_CTRL_PWR(power_mode);
	ret = ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, st->adc_control);
	if (ret < 0)
		return ret;

	for (i = 0; i < st->num_channels; i++) {
		val = st->channel_config[i].ain | AD7124_CHANNEL_SETUP(i);
		ret = ad_sd_write_reg(&st->sd, AD7124_CHANNEL(i), 2, val);
		if (ret < 0)
			return ret;

		ret = ad7124_init_channel_vref(st, i);
		if (ret < 0)
			return ret;

		tmp = (st->channel_config[i].buf_positive << 1)  +
			st->channel_config[i].buf_negative;

		val = AD7124_CONFIG_BIPOLAR(st->channel_config[i].bipolar) |
		      AD7124_CONFIG_REF_SEL(st->channel_config[i].refsel) |
		      AD7124_CONFIG_IN_BUFF(tmp);
		ret = ad_sd_write_reg(&st->sd, AD7124_CONFIG(i), 2, val);
		if (ret < 0)
			return ret;
		/*
		 * 9.38 SPS is the minimum output data rate supported
		 * regardless of the selected power mode. Round it up to 10 and
		 * set all the enabled channels to this default value.
		 */
		ret = ad7124_set_channel_odr(st, i, 10);
	}

	return ret;
}

static int ad7124_probe(struct spi_device *spi)
{
	const struct spi_device_id *id;
	struct ad7124_state *st;
	struct iio_dev *indio_dev;
	int i, ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	id = spi_get_device_id(spi);
	st->chip_info = &ad7124_chip_info_tbl[id->driver_data];

	ad_sd_init(&st->sd, indio_dev, spi, &ad7124_sigma_delta_info);

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad7124_info;

	ret = ad7124_of_parse_channel_config(indio_dev, spi->dev.of_node);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(st->vref); i++) {
		if (i == AD7124_INT_REF)
			continue;

		st->vref[i] = devm_regulator_get_optional(&spi->dev,
						ad7124_ref_names[i]);
		if (PTR_ERR(st->vref[i]) == -ENODEV)
			continue;
		else if (IS_ERR(st->vref[i]))
			return PTR_ERR(st->vref[i]);

		ret = regulator_enable(st->vref[i]);
		if (ret)
			return ret;
	}

	st->mclk = devm_clk_get(&spi->dev, "mclk");
	if (IS_ERR(st->mclk)) {
		ret = PTR_ERR(st->mclk);
		goto error_regulator_disable;
	}

	ret = clk_prepare_enable(st->mclk);
	if (ret < 0)
		goto error_regulator_disable;

	ret = ad7124_soft_reset(st);
	if (ret < 0)
		goto error_clk_disable_unprepare;

	ret = ad7124_setup(st);
	if (ret < 0)
		goto error_clk_disable_unprepare;

	ret = ad_sd_setup_buffer_and_trigger(indio_dev);
	if (ret < 0)
		goto error_clk_disable_unprepare;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to register iio device\n");
		goto error_remove_trigger;
	}

	return 0;

error_remove_trigger:
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
error_clk_disable_unprepare:
	clk_disable_unprepare(st->mclk);
error_regulator_disable:
	for (i = ARRAY_SIZE(st->vref) - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(st->vref[i]))
			regulator_disable(st->vref[i]);
	}

	return ret;
}

static int ad7124_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7124_state *st = iio_priv(indio_dev);
	int i;

	iio_device_unregister(indio_dev);
	ad_sd_cleanup_buffer_and_trigger(indio_dev);
	clk_disable_unprepare(st->mclk);

	for (i = ARRAY_SIZE(st->vref) - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(st->vref[i]))
			regulator_disable(st->vref[i]);
	}

	return 0;
}

static const struct spi_device_id ad7124_id_table[] = {
	{ "ad7124-4", ID_AD7124_4 },
	{ "ad7124-8", ID_AD7124_8 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad7124_id_table);

static const struct of_device_id ad7124_of_match[] = {
	{ .compatible = "adi,ad7124-4" },
	{ .compatible = "adi,ad7124-8" },
	{ },
};
MODULE_DEVICE_TABLE(of, ad7124_of_match);

static struct spi_driver ad71124_driver = {
	.driver = {
		.name = "ad7124",
		.of_match_table = ad7124_of_match,
	},
	.probe = ad7124_probe,
	.remove	= ad7124_remove,
	.id_table = ad7124_id_table,
};
module_spi_driver(ad71124_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7124 SPI driver");
MODULE_LICENSE("GPL");
