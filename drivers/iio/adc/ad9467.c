// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD9467 SPI ADC driver
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>


#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/clk.h>

#include <linux/iio/adc/adi-axi-adc.h>

/*
 * ADI High-Speed ADC common spi interface registers
 * See Application-Note AN-877:
 *   https://www.analog.com/media/en/technical-documentation/application-notes/AN-877.pdf
 */

#define AN877_ADC_REG_CHIP_PORT_CONF		0x00
#define AN877_ADC_REG_CHIP_ID			0x01
#define AN877_ADC_REG_CHIP_GRADE		0x02
#define AN877_ADC_REG_CHAN_INDEX		0x05
#define AN877_ADC_REG_TRANSFER			0xFF
#define AN877_ADC_REG_MODES			0x08
#define AN877_ADC_REG_TEST_IO			0x0D
#define AN877_ADC_REG_ADC_INPUT			0x0F
#define AN877_ADC_REG_OFFSET			0x10
#define AN877_ADC_REG_OUTPUT_MODE		0x14
#define AN877_ADC_REG_OUTPUT_ADJUST		0x15
#define AN877_ADC_REG_OUTPUT_PHASE		0x16
#define AN877_ADC_REG_OUTPUT_DELAY		0x17
#define AN877_ADC_REG_VREF			0x18
#define AN877_ADC_REG_ANALOG_INPUT		0x2C

/* AN877_ADC_REG_TEST_IO */
#define AN877_ADC_TESTMODE_OFF			0x0
#define AN877_ADC_TESTMODE_MIDSCALE_SHORT	0x1
#define AN877_ADC_TESTMODE_POS_FULLSCALE	0x2
#define AN877_ADC_TESTMODE_NEG_FULLSCALE	0x3
#define AN877_ADC_TESTMODE_ALT_CHECKERBOARD	0x4
#define AN877_ADC_TESTMODE_PN23_SEQ		0x5
#define AN877_ADC_TESTMODE_PN9_SEQ		0x6
#define AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE	0x7
#define AN877_ADC_TESTMODE_USER			0x8
#define AN877_ADC_TESTMODE_BIT_TOGGLE		0x9
#define AN877_ADC_TESTMODE_SYNC			0xA
#define AN877_ADC_TESTMODE_ONE_BIT_HIGH		0xB
#define AN877_ADC_TESTMODE_MIXED_BIT_FREQUENCY	0xC
#define AN877_ADC_TESTMODE_RAMP			0xF

/* AN877_ADC_REG_TRANSFER */
#define AN877_ADC_TRANSFER_SYNC			0x1

/* AN877_ADC_REG_OUTPUT_MODE */
#define AN877_ADC_OUTPUT_MODE_OFFSET_BINARY	0x0
#define AN877_ADC_OUTPUT_MODE_TWOS_COMPLEMENT	0x1
#define AN877_ADC_OUTPUT_MODE_GRAY_CODE		0x2

/* AN877_ADC_REG_OUTPUT_PHASE */
#define AN877_ADC_OUTPUT_EVEN_ODD_MODE_EN	0x20
#define AN877_ADC_INVERT_DCO_CLK		0x80

/* AN877_ADC_REG_OUTPUT_DELAY */
#define AN877_ADC_DCO_DELAY_ENABLE		0x80

/*
 * Analog Devices AD9265 16-Bit, 125/105/80 MSPS ADC
 */

#define CHIPID_AD9265			0x64
#define AD9265_DEF_OUTPUT_MODE		0x40
#define AD9265_REG_VREF_MASK		0xC0

/*
 * Analog Devices AD9434 12-Bit, 370/500 MSPS ADC
 */

#define CHIPID_AD9434			0x6A
#define AD9434_DEF_OUTPUT_MODE		0x00
#define AD9434_REG_VREF_MASK		0xC0

/*
 * Analog Devices AD9467 16-Bit, 200/250 MSPS ADC
 */

#define CHIPID_AD9467			0x50
#define AD9467_DEF_OUTPUT_MODE		0x08
#define AD9467_REG_VREF_MASK		0x0F

enum {
	ID_AD9265,
	ID_AD9434,
	ID_AD9467,
};

struct ad9467_chip_info {
	struct adi_axi_adc_chip_info	axi_adc_info;
	unsigned int			default_output_mode;
	unsigned int			vref_mask;
};

#define to_ad9467_chip_info(_info)	\
	container_of(_info, struct ad9467_chip_info, axi_adc_info)

struct ad9467_state {
	struct spi_device		*spi;
	struct clk			*clk;
	unsigned int			output_mode;

	struct gpio_desc		*pwrdown_gpio;
	struct gpio_desc		*reset_gpio;
};

static int ad9467_spi_read(struct spi_device *spi, unsigned int reg)
{
	unsigned char tbuf[2], rbuf[1];
	int ret;

	tbuf[0] = 0x80 | (reg >> 8);
	tbuf[1] = reg & 0xFF;

	ret = spi_write_then_read(spi,
				  tbuf, ARRAY_SIZE(tbuf),
				  rbuf, ARRAY_SIZE(rbuf));

	if (ret < 0)
		return ret;

	return rbuf[0];
}

static int ad9467_spi_write(struct spi_device *spi, unsigned int reg,
			    unsigned int val)
{
	unsigned char buf[3];

	buf[0] = reg >> 8;
	buf[1] = reg & 0xFF;
	buf[2] = val;

	return spi_write(spi, buf, ARRAY_SIZE(buf));
}

static int ad9467_reg_access(struct adi_axi_adc_conv *conv, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);
	struct spi_device *spi = st->spi;
	int ret;

	if (readval == NULL) {
		ret = ad9467_spi_write(spi, reg, writeval);
		ad9467_spi_write(spi, AN877_ADC_REG_TRANSFER,
				 AN877_ADC_TRANSFER_SYNC);
		return ret;
	}

	ret = ad9467_spi_read(spi, reg);
	if (ret < 0)
		return ret;
	*readval = ret;

	return 0;
}

static const unsigned int ad9265_scale_table[][2] = {
	{1250, 0x00}, {1500, 0x40}, {1750, 0x80}, {2000, 0xC0},
};

static const unsigned int ad9434_scale_table[][2] = {
	{1600, 0x1C}, {1580, 0x1D}, {1550, 0x1E}, {1520, 0x1F}, {1500, 0x00},
	{1470, 0x01}, {1440, 0x02}, {1420, 0x03}, {1390, 0x04}, {1360, 0x05},
	{1340, 0x06}, {1310, 0x07}, {1280, 0x08}, {1260, 0x09}, {1230, 0x0A},
	{1200, 0x0B}, {1180, 0x0C},
};

static const unsigned int ad9467_scale_table[][2] = {
	{2000, 0}, {2100, 6}, {2200, 7},
	{2300, 8}, {2400, 9}, {2500, 10},
};

static void __ad9467_get_scale(struct adi_axi_adc_conv *conv, int index,
			       unsigned int *val, unsigned int *val2)
{
	const struct adi_axi_adc_chip_info *info = conv->chip_info;
	const struct iio_chan_spec *chan = &info->channels[0];
	unsigned int tmp;

	tmp = (info->scale_table[index][0] * 1000000ULL) >>
			chan->scan_type.realbits;
	*val = tmp / 1000000;
	*val2 = tmp % 1000000;
}

#define AD9467_CHAN(_chan, _si, _bits, _sign)				\
{									\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.channel = _chan,						\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_index = _si,						\
	.scan_type = {							\
		.sign = _sign,						\
		.realbits = _bits,					\
		.storagebits = 16,					\
	},								\
}

static const struct iio_chan_spec ad9434_channels[] = {
	AD9467_CHAN(0, 0, 12, 'S'),
};

static const struct iio_chan_spec ad9467_channels[] = {
	AD9467_CHAN(0, 0, 16, 'S'),
};

static const struct ad9467_chip_info ad9467_chip_tbl[] = {
	[ID_AD9265] = {
		.axi_adc_info = {
			.id = CHIPID_AD9265,
			.max_rate = 125000000UL,
			.scale_table = ad9265_scale_table,
			.num_scales = ARRAY_SIZE(ad9265_scale_table),
			.channels = ad9467_channels,
			.num_channels = ARRAY_SIZE(ad9467_channels),
		},
		.default_output_mode = AD9265_DEF_OUTPUT_MODE,
		.vref_mask = AD9265_REG_VREF_MASK,
	},
	[ID_AD9434] = {
		.axi_adc_info = {
			.id = CHIPID_AD9434,
			.max_rate = 500000000UL,
			.scale_table = ad9434_scale_table,
			.num_scales = ARRAY_SIZE(ad9434_scale_table),
			.channels = ad9434_channels,
			.num_channels = ARRAY_SIZE(ad9434_channels),
		},
		.default_output_mode = AD9434_DEF_OUTPUT_MODE,
		.vref_mask = AD9434_REG_VREF_MASK,
	},
	[ID_AD9467] = {
		.axi_adc_info = {
			.id = CHIPID_AD9467,
			.max_rate = 250000000UL,
			.scale_table = ad9467_scale_table,
			.num_scales = ARRAY_SIZE(ad9467_scale_table),
			.channels = ad9467_channels,
			.num_channels = ARRAY_SIZE(ad9467_channels),
		},
		.default_output_mode = AD9467_DEF_OUTPUT_MODE,
		.vref_mask = AD9467_REG_VREF_MASK,
	},
};

static int ad9467_get_scale(struct adi_axi_adc_conv *conv, int *val, int *val2)
{
	const struct adi_axi_adc_chip_info *info = conv->chip_info;
	const struct ad9467_chip_info *info1 = to_ad9467_chip_info(info);
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);
	unsigned int i, vref_val;

	vref_val = ad9467_spi_read(st->spi, AN877_ADC_REG_VREF);

	vref_val &= info1->vref_mask;

	for (i = 0; i < info->num_scales; i++) {
		if (vref_val == info->scale_table[i][1])
			break;
	}

	if (i == info->num_scales)
		return -ERANGE;

	__ad9467_get_scale(conv, i, val, val2);

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad9467_set_scale(struct adi_axi_adc_conv *conv, int val, int val2)
{
	const struct adi_axi_adc_chip_info *info = conv->chip_info;
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);
	unsigned int scale_val[2];
	unsigned int i;

	if (val != 0)
		return -EINVAL;

	for (i = 0; i < info->num_scales; i++) {
		__ad9467_get_scale(conv, i, &scale_val[0], &scale_val[1]);
		if (scale_val[0] != val || scale_val[1] != val2)
			continue;

		ad9467_spi_write(st->spi, AN877_ADC_REG_VREF,
				 info->scale_table[i][1]);
		ad9467_spi_write(st->spi, AN877_ADC_REG_TRANSFER,
				 AN877_ADC_TRANSFER_SYNC);
		return 0;
	}

	return -EINVAL;
}

static int ad9467_read_raw(struct adi_axi_adc_conv *conv,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long m)
{
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		return ad9467_get_scale(conv, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(st->clk);

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad9467_write_raw(struct adi_axi_adc_conv *conv,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	const struct adi_axi_adc_chip_info *info = conv->chip_info;
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);
	long r_clk;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return ad9467_set_scale(conv, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		r_clk = clk_round_rate(st->clk, val);
		if (r_clk < 0 || r_clk > info->max_rate) {
			dev_warn(&st->spi->dev,
				 "Error setting ADC sample rate %ld", r_clk);
			return -EINVAL;
		}

		return clk_set_rate(st->clk, r_clk);
	default:
		return -EINVAL;
	}
}

static int ad9467_outputmode_set(struct spi_device *spi, unsigned int mode)
{
	int ret;

	ret = ad9467_spi_write(spi, AN877_ADC_REG_OUTPUT_MODE, mode);
	if (ret < 0)
		return ret;

	return ad9467_spi_write(spi, AN877_ADC_REG_TRANSFER,
				AN877_ADC_TRANSFER_SYNC);
}

static int ad9467_preenable_setup(struct adi_axi_adc_conv *conv)
{
	struct ad9467_state *st = adi_axi_adc_conv_priv(conv);

	return ad9467_outputmode_set(st->spi, st->output_mode);
}

static void ad9467_clk_disable(void *data)
{
	struct ad9467_state *st = data;

	clk_disable_unprepare(st->clk);
}

static int ad9467_probe(struct spi_device *spi)
{
	const struct ad9467_chip_info *info;
	struct adi_axi_adc_conv *conv;
	struct ad9467_state *st;
	unsigned int id;
	int ret;

	info = of_device_get_match_data(&spi->dev);
	if (!info)
		return -ENODEV;

	conv = devm_adi_axi_adc_conv_register(&spi->dev, sizeof(*st));
	if (IS_ERR(conv))
		return PTR_ERR(conv);

	st = adi_axi_adc_conv_priv(conv);
	st->spi = spi;

	st->clk = devm_clk_get(&spi->dev, "adc-clk");
	if (IS_ERR(st->clk))
		return PTR_ERR(st->clk);

	ret = clk_prepare_enable(st->clk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ad9467_clk_disable, st);
	if (ret)
		return ret;

	st->pwrdown_gpio = devm_gpiod_get_optional(&spi->dev, "powerdown",
						   GPIOD_OUT_LOW);
	if (IS_ERR(st->pwrdown_gpio))
		return PTR_ERR(st->pwrdown_gpio);

	st->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						 GPIOD_OUT_LOW);
	if (IS_ERR(st->reset_gpio))
		return PTR_ERR(st->reset_gpio);

	if (st->reset_gpio) {
		udelay(1);
		ret = gpiod_direction_output(st->reset_gpio, 1);
		if (ret)
			return ret;
		mdelay(10);
	}

	conv->chip_info = &info->axi_adc_info;

	id = ad9467_spi_read(spi, AN877_ADC_REG_CHIP_ID);
	if (id != conv->chip_info->id) {
		dev_err(&spi->dev, "Mismatch CHIP_ID, got 0x%X, expected 0x%X\n",
			id, conv->chip_info->id);
		return -ENODEV;
	}

	conv->reg_access = ad9467_reg_access;
	conv->write_raw = ad9467_write_raw;
	conv->read_raw = ad9467_read_raw;
	conv->preenable_setup = ad9467_preenable_setup;

	st->output_mode = info->default_output_mode |
			  AN877_ADC_OUTPUT_MODE_TWOS_COMPLEMENT;

	return 0;
}

static const struct of_device_id ad9467_of_match[] = {
	{ .compatible = "adi,ad9265", .data = &ad9467_chip_tbl[ID_AD9265], },
	{ .compatible = "adi,ad9434", .data = &ad9467_chip_tbl[ID_AD9434], },
	{ .compatible = "adi,ad9467", .data = &ad9467_chip_tbl[ID_AD9467], },
	{}
};
MODULE_DEVICE_TABLE(of, ad9467_of_match);

static struct spi_driver ad9467_driver = {
	.driver = {
		.name = "ad9467",
		.of_match_table = ad9467_of_match,
	},
	.probe = ad9467_probe,
};
module_spi_driver(ad9467_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9467 ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ADI_AXI);
