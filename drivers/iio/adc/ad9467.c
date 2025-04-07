// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD9467 SPI ADC driver
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>


#include <linux/iio/backend.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/clk.h>

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

/*
 * Analog Devices AD9643 14-Bit, 170/210/250 MSPS ADC
 */

#define CHIPID_AD9643			0x82
#define AD9643_REG_VREF_MASK		0x1F

/*
 * Analog Devices AD9652 16-bit 310 MSPS ADC
 */

#define CHIPID_AD9652                   0xC1
#define AD9652_REG_VREF_MASK            0xC0

/*
 * Analog Devices AD9649 14-bit 20/40/65/80 MSPS ADC
 */

#define CHIPID_AD9649			0x6F
#define AD9649_TEST_POINTS		8

#define AD9647_MAX_TEST_POINTS		32
#define AD9467_CAN_INVERT(st)	\
	(!(st)->info->has_dco || (st)->info->has_dco_invert)

struct ad9467_chip_info {
	const char *name;
	unsigned int id;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	const unsigned int (*scale_table)[2];
	int num_scales;
	unsigned long test_mask;
	unsigned int test_mask_len;
	unsigned long max_rate;
	unsigned int default_output_mode;
	unsigned int vref_mask;
	unsigned int num_lanes;
	unsigned int dco_en;
	unsigned int test_points;
	/* data clock output */
	bool has_dco;
	bool has_dco_invert;
};

struct ad9467_chan_test_mode {
	struct ad9467_state *st;
	unsigned int idx;
	u8 mode;
};

struct ad9467_state {
	const struct ad9467_chip_info *info;
	struct iio_backend *back;
	struct spi_device *spi;
	struct clk *clk;
	/* used for debugfs */
	struct ad9467_chan_test_mode *chan_test;
	unsigned int output_mode;
	unsigned int (*scales)[2];
	/*
	 * Times 2 because we may also invert the signal polarity and run the
	 * calibration again. For some reference on the test points (ad9265) see:
	 * https://www.analog.com/media/en/technical-documentation/data-sheets/ad9265.pdf
	 * at page 38 for the dco output delay. On devices as ad9467, the
	 * calibration is done at the backend level. For the ADI axi-adc:
	 * https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
	 * at the io delay control section.
	 */
	DECLARE_BITMAP(calib_map, AD9647_MAX_TEST_POINTS * 2);
	/* number of bits of the map */
	unsigned int calib_map_size;
	struct gpio_desc *pwrdown_gpio;
	/* ensure consistent state obtained on multiple related accesses */
	struct mutex lock;
	u8 buf[3] __aligned(IIO_DMA_MINALIGN);
};

static int ad9467_spi_read(struct ad9467_state *st, unsigned int reg)
{
	unsigned char tbuf[2], rbuf[1];
	int ret;

	tbuf[0] = 0x80 | (reg >> 8);
	tbuf[1] = reg & 0xFF;

	ret = spi_write_then_read(st->spi,
				  tbuf, ARRAY_SIZE(tbuf),
				  rbuf, ARRAY_SIZE(rbuf));

	if (ret < 0)
		return ret;

	return rbuf[0];
}

static int ad9467_spi_write(struct ad9467_state *st, unsigned int reg,
			    unsigned int val)
{
	st->buf[0] = reg >> 8;
	st->buf[1] = reg & 0xFF;
	st->buf[2] = val;

	return spi_write(st->spi, st->buf, ARRAY_SIZE(st->buf));
}

static int ad9467_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad9467_state *st = iio_priv(indio_dev);
	int ret;

	if (!readval) {
		guard(mutex)(&st->lock);
		ret = ad9467_spi_write(st, reg, writeval);
		if (ret)
			return ret;
		return ad9467_spi_write(st, AN877_ADC_REG_TRANSFER,
					AN877_ADC_TRANSFER_SYNC);
	}

	ret = ad9467_spi_read(st, reg);
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

static const unsigned int ad9643_scale_table[][2] = {
	{2087, 0x0F}, {2065, 0x0E}, {2042, 0x0D}, {2020, 0x0C}, {1997, 0x0B},
	{1975, 0x0A}, {1952, 0x09}, {1930, 0x08}, {1907, 0x07}, {1885, 0x06},
	{1862, 0x05}, {1840, 0x04}, {1817, 0x03}, {1795, 0x02}, {1772, 0x01},
	{1750, 0x00}, {1727, 0x1F}, {1704, 0x1E}, {1681, 0x1D}, {1658, 0x1C},
	{1635, 0x1B}, {1612, 0x1A}, {1589, 0x19}, {1567, 0x18}, {1544, 0x17},
	{1521, 0x16}, {1498, 0x15}, {1475, 0x14}, {1452, 0x13}, {1429, 0x12},
	{1406, 0x11}, {1383, 0x10},
};

static const unsigned int ad9649_scale_table[][2] = {
	 {2000, 0},
};

static const unsigned int ad9652_scale_table[][2] = {
	 {1250, 0}, {1125, 1}, {1200, 2}, {1250, 3}, {1000, 5},
};

static void __ad9467_get_scale(struct ad9467_state *st, int index,
			       unsigned int *val, unsigned int *val2)
{
	const struct ad9467_chip_info *info = st->info;
	const struct iio_chan_spec *chan = &info->channels[0];
	unsigned int tmp;

	tmp = (info->scale_table[index][0] * 1000000ULL) >>
			chan->scan_type.realbits;
	*val = tmp / 1000000;
	*val2 = tmp % 1000000;
}

#define AD9467_CHAN(_chan, avai_mask, _si, _bits, _sign)		\
{									\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.channel = _chan,						\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.info_mask_shared_by_type_available = avai_mask,		\
	.scan_index = _si,						\
	.scan_type = {							\
		.sign = _sign,						\
		.realbits = _bits,					\
		.storagebits = 16,					\
	},								\
}

static const struct iio_chan_spec ad9434_channels[] = {
	AD9467_CHAN(0, BIT(IIO_CHAN_INFO_SCALE), 0, 12, 's'),
};

static const struct iio_chan_spec ad9467_channels[] = {
	AD9467_CHAN(0, BIT(IIO_CHAN_INFO_SCALE), 0, 16, 's'),
};

static const struct iio_chan_spec ad9643_channels[] = {
	AD9467_CHAN(0, BIT(IIO_CHAN_INFO_SCALE), 0, 14, 's'),
	AD9467_CHAN(1, BIT(IIO_CHAN_INFO_SCALE), 1, 14, 's'),
};

static const struct iio_chan_spec ad9649_channels[] = {
	AD9467_CHAN(0, 0, 0, 14, 's'),
};

static const struct iio_chan_spec ad9652_channels[] = {
	AD9467_CHAN(0, BIT(IIO_CHAN_INFO_SCALE), 0, 16, 's'),
	AD9467_CHAN(1, BIT(IIO_CHAN_INFO_SCALE), 1, 16, 's'),
};

static const char * const ad9467_test_modes[] = {
	[AN877_ADC_TESTMODE_OFF] = "off",
	[AN877_ADC_TESTMODE_MIDSCALE_SHORT] = "midscale_short",
	[AN877_ADC_TESTMODE_POS_FULLSCALE] = "pos_fullscale",
	[AN877_ADC_TESTMODE_NEG_FULLSCALE] = "neg_fullscale",
	[AN877_ADC_TESTMODE_ALT_CHECKERBOARD] = "checkerboard",
	[AN877_ADC_TESTMODE_PN23_SEQ] = "prbs23",
	[AN877_ADC_TESTMODE_PN9_SEQ] = "prbs9",
	[AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE] = "one_zero_toggle",
	[AN877_ADC_TESTMODE_USER] = "user",
	[AN877_ADC_TESTMODE_BIT_TOGGLE] = "bit_toggle",
	[AN877_ADC_TESTMODE_SYNC] = "sync",
	[AN877_ADC_TESTMODE_ONE_BIT_HIGH] = "one_bit_high",
	[AN877_ADC_TESTMODE_MIXED_BIT_FREQUENCY] = "mixed_bit_frequency",
	[AN877_ADC_TESTMODE_RAMP] = "ramp",
};

static const struct ad9467_chip_info ad9467_chip_tbl = {
	.name = "ad9467",
	.id = CHIPID_AD9467,
	.max_rate = 250000000UL,
	.scale_table = ad9467_scale_table,
	.num_scales = ARRAY_SIZE(ad9467_scale_table),
	.channels = ad9467_channels,
	.num_channels = ARRAY_SIZE(ad9467_channels),
	.test_points = AD9647_MAX_TEST_POINTS,
	.test_mask = GENMASK(AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE,
			     AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE + 1,
	.default_output_mode = AD9467_DEF_OUTPUT_MODE,
	.vref_mask = AD9467_REG_VREF_MASK,
	.num_lanes = 8,
};

static const struct ad9467_chip_info ad9434_chip_tbl = {
	.name = "ad9434",
	.id = CHIPID_AD9434,
	.max_rate = 500000000UL,
	.scale_table = ad9434_scale_table,
	.num_scales = ARRAY_SIZE(ad9434_scale_table),
	.channels = ad9434_channels,
	.num_channels = ARRAY_SIZE(ad9434_channels),
	.test_points = AD9647_MAX_TEST_POINTS,
	.test_mask = GENMASK(AN877_ADC_TESTMODE_USER, AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_USER + 1,
	.default_output_mode = AD9434_DEF_OUTPUT_MODE,
	.vref_mask = AD9434_REG_VREF_MASK,
	.num_lanes = 6,
};

static const struct ad9467_chip_info ad9265_chip_tbl = {
	.name = "ad9265",
	.id = CHIPID_AD9265,
	.max_rate = 125000000UL,
	.scale_table = ad9265_scale_table,
	.num_scales = ARRAY_SIZE(ad9265_scale_table),
	.channels = ad9467_channels,
	.num_channels = ARRAY_SIZE(ad9467_channels),
	.test_points = AD9647_MAX_TEST_POINTS,
	.test_mask = GENMASK(AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE,
			     AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE + 1,
	.default_output_mode = AD9265_DEF_OUTPUT_MODE,
	.vref_mask = AD9265_REG_VREF_MASK,
	.has_dco = true,
	.has_dco_invert = true,
};

static const struct ad9467_chip_info ad9643_chip_tbl = {
	.name = "ad9643",
	.id = CHIPID_AD9643,
	.max_rate = 250000000UL,
	.scale_table = ad9643_scale_table,
	.num_scales = ARRAY_SIZE(ad9643_scale_table),
	.channels = ad9643_channels,
	.num_channels = ARRAY_SIZE(ad9643_channels),
	.test_points = AD9647_MAX_TEST_POINTS,
	.test_mask = BIT(AN877_ADC_TESTMODE_RAMP) |
		GENMASK(AN877_ADC_TESTMODE_MIXED_BIT_FREQUENCY, AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_RAMP + 1,
	.vref_mask = AD9643_REG_VREF_MASK,
	.has_dco = true,
	.has_dco_invert = true,
	.dco_en = AN877_ADC_DCO_DELAY_ENABLE,
};

static const struct ad9467_chip_info ad9649_chip_tbl = {
	.name = "ad9649",
	.id = CHIPID_AD9649,
	.max_rate = 80000000UL,
	.scale_table = ad9649_scale_table,
	.num_scales = ARRAY_SIZE(ad9649_scale_table),
	.channels = ad9649_channels,
	.num_channels = ARRAY_SIZE(ad9649_channels),
	.test_points = AD9649_TEST_POINTS,
	.test_mask = GENMASK(AN877_ADC_TESTMODE_MIXED_BIT_FREQUENCY,
			     AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_MIXED_BIT_FREQUENCY + 1,
	.has_dco = true,
	.has_dco_invert = true,
	.dco_en = AN877_ADC_DCO_DELAY_ENABLE,
};

static const struct ad9467_chip_info ad9652_chip_tbl = {
	.name = "ad9652",
	.id = CHIPID_AD9652,
	.max_rate = 310000000UL,
	.scale_table = ad9652_scale_table,
	.num_scales = ARRAY_SIZE(ad9652_scale_table),
	.channels = ad9652_channels,
	.num_channels = ARRAY_SIZE(ad9652_channels),
	.test_points = AD9647_MAX_TEST_POINTS,
	.test_mask = GENMASK(AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE,
			     AN877_ADC_TESTMODE_OFF),
	.test_mask_len = AN877_ADC_TESTMODE_ONE_ZERO_TOGGLE + 1,
	.vref_mask = AD9652_REG_VREF_MASK,
	.has_dco = true,
};

static int ad9467_get_scale(struct ad9467_state *st, int *val, int *val2)
{
	const struct ad9467_chip_info *info = st->info;
	unsigned int vref_val;
	unsigned int i = 0;
	int ret;

	/* nothing to read if we only have one possible scale */
	if (info->num_scales == 1)
		goto out_get_scale;

	ret = ad9467_spi_read(st, AN877_ADC_REG_VREF);
	if (ret < 0)
		return ret;

	vref_val = ret & info->vref_mask;

	for (i = 0; i < info->num_scales; i++) {
		if (vref_val == info->scale_table[i][1])
			break;
	}

	if (i == info->num_scales)
		return -ERANGE;

out_get_scale:
	__ad9467_get_scale(st, i, val, val2);

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad9467_set_scale(struct ad9467_state *st, int val, int val2)
{
	const struct ad9467_chip_info *info = st->info;
	unsigned int scale_val[2];
	unsigned int i;
	int ret;

	if (val != 0)
		return -EINVAL;
	if (info->num_scales == 1)
		return -EOPNOTSUPP;

	for (i = 0; i < info->num_scales; i++) {
		__ad9467_get_scale(st, i, &scale_val[0], &scale_val[1]);
		if (scale_val[0] != val || scale_val[1] != val2)
			continue;

		guard(mutex)(&st->lock);
		ret = ad9467_spi_write(st, AN877_ADC_REG_VREF,
				       info->scale_table[i][1]);
		if (ret < 0)
			return ret;

		return ad9467_spi_write(st, AN877_ADC_REG_TRANSFER,
					AN877_ADC_TRANSFER_SYNC);
	}

	return -EINVAL;
}

static int ad9467_outputmode_set(struct ad9467_state *st, unsigned int mode)
{
	int ret;

	ret = ad9467_spi_write(st, AN877_ADC_REG_OUTPUT_MODE, mode);
	if (ret < 0)
		return ret;

	return ad9467_spi_write(st, AN877_ADC_REG_TRANSFER,
				AN877_ADC_TRANSFER_SYNC);
}

static int ad9467_testmode_set(struct ad9467_state *st, unsigned int chan,
			       unsigned int test_mode)
{
	int ret;

	if (st->info->num_channels > 1) {
		/* so that the test mode is only applied to one channel */
		ret = ad9467_spi_write(st, AN877_ADC_REG_CHAN_INDEX, BIT(chan));
		if (ret)
			return ret;
	}

	ret = ad9467_spi_write(st, AN877_ADC_REG_TEST_IO, test_mode);
	if (ret)
		return ret;

	if (st->info->num_channels > 1) {
		/* go to default state where all channels get write commands */
		ret = ad9467_spi_write(st, AN877_ADC_REG_CHAN_INDEX,
				       GENMASK(st->info->num_channels - 1, 0));
		if (ret)
			return ret;
	}

	return ad9467_spi_write(st, AN877_ADC_REG_TRANSFER,
				AN877_ADC_TRANSFER_SYNC);
}

static int ad9467_backend_testmode_on(struct ad9467_state *st,
				      unsigned int chan,
				      enum iio_backend_test_pattern pattern)
{
	struct iio_backend_data_fmt data = {
		.enable = false,
	};
	int ret;

	ret = iio_backend_data_format_set(st->back, chan, &data);
	if (ret)
		return ret;

	ret = iio_backend_test_pattern_set(st->back, chan, pattern);
	if (ret)
		return ret;

	return iio_backend_chan_enable(st->back, chan);
}

static int ad9467_backend_testmode_off(struct ad9467_state *st,
				       unsigned int chan)
{
	struct iio_backend_data_fmt data = {
		.enable = true,
		.sign_extend = true,
	};
	int ret;

	ret = iio_backend_chan_disable(st->back, chan);
	if (ret)
		return ret;

	ret = iio_backend_test_pattern_set(st->back, chan,
					   IIO_BACKEND_NO_TEST_PATTERN);
	if (ret)
		return ret;

	return iio_backend_data_format_set(st->back, chan, &data);
}

static int ad9647_calibrate_prepare(struct ad9467_state *st)
{
	unsigned int c;
	int ret;

	ret = ad9467_outputmode_set(st, st->info->default_output_mode);
	if (ret)
		return ret;

	for (c = 0; c < st->info->num_channels; c++) {
		ret = ad9467_testmode_set(st, c, AN877_ADC_TESTMODE_PN9_SEQ);
		if (ret)
			return ret;

		ret = ad9467_backend_testmode_on(st, c,
						 IIO_BACKEND_ADI_PRBS_9A);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad9647_calibrate_polarity_set(struct ad9467_state *st,
					 bool invert)
{
	enum iio_backend_sample_trigger trigger;

	if (st->info->has_dco) {
		unsigned int phase = AN877_ADC_OUTPUT_EVEN_ODD_MODE_EN;

		if (invert)
			phase |= AN877_ADC_INVERT_DCO_CLK;

		return ad9467_spi_write(st, AN877_ADC_REG_OUTPUT_PHASE,
					phase);
	}

	if (invert)
		trigger = IIO_BACKEND_SAMPLE_TRIGGER_EDGE_FALLING;
	else
		trigger = IIO_BACKEND_SAMPLE_TRIGGER_EDGE_RISING;

	return iio_backend_data_sample_trigger(st->back, trigger);
}

/*
 * The idea is pretty simple. Find the max number of successful points in a row
 * and get the one in the middle.
 */
static unsigned int ad9467_find_optimal_point(const unsigned long *calib_map,
					      unsigned int start,
					      unsigned int nbits,
					      unsigned int *val)
{
	unsigned int bit = start, end, start_cnt, cnt = 0;

	for_each_clear_bitrange_from(bit, end, calib_map, nbits + start) {
		if (end - bit > cnt) {
			cnt = end - bit;
			start_cnt = bit;
		}
	}

	if (cnt)
		*val = start_cnt + cnt / 2;

	return cnt;
}

static int ad9467_calibrate_apply(struct ad9467_state *st, unsigned int val)
{
	unsigned int lane;
	int ret;

	if (st->info->has_dco) {
		ret = ad9467_spi_write(st, AN877_ADC_REG_OUTPUT_DELAY,
				       val | st->info->dco_en);
		if (ret)
			return ret;

		return ad9467_spi_write(st, AN877_ADC_REG_TRANSFER,
					AN877_ADC_TRANSFER_SYNC);
	}

	for (lane = 0; lane < st->info->num_lanes; lane++) {
		ret = iio_backend_iodelay_set(st->back, lane, val);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad9647_calibrate_stop(struct ad9467_state *st)
{
	unsigned int c, mode;
	int ret;

	for (c = 0; c < st->info->num_channels; c++) {
		ret = ad9467_backend_testmode_off(st, c);
		if (ret)
			return ret;

		ret = ad9467_testmode_set(st, c, AN877_ADC_TESTMODE_OFF);
		if (ret)
			return ret;
	}

	mode = st->info->default_output_mode | AN877_ADC_OUTPUT_MODE_TWOS_COMPLEMENT;
	return ad9467_outputmode_set(st, mode);
}

static int ad9467_calibrate(struct ad9467_state *st)
{
	unsigned int point, val, inv_val, cnt, inv_cnt = 0, c;
	/*
	 * Half of the bitmap is for the inverted signal. The number of test
	 * points is the same though...
	 */
	unsigned int test_points = st->info->test_points;
	unsigned long sample_rate = clk_get_rate(st->clk);
	struct device *dev = &st->spi->dev;
	bool invert = false, stat;
	int ret;

	/* all points invalid */
	bitmap_fill(st->calib_map, st->calib_map_size);

	ret = ad9647_calibrate_prepare(st);
	if (ret)
		return ret;
retune:
	ret = ad9647_calibrate_polarity_set(st, invert);
	if (ret)
		return ret;

	for (point = 0; point < st->info->test_points; point++) {
		ret = ad9467_calibrate_apply(st, point);
		if (ret)
			return ret;

		for (c = 0; c < st->info->num_channels; c++) {
			ret = iio_backend_chan_status(st->back, c, &stat);
			if (ret)
				return ret;

			/*
			 * A point is considered valid if all channels report no
			 * error. If one reports an error, then we consider the
			 * point as invalid and we can break the loop right away.
			 */
			if (stat) {
				dev_dbg(dev, "Invalid point(%u, inv:%u) for CH:%u\n",
					point, invert, c);
				break;
			}

			if (c == st->info->num_channels - 1)
				__clear_bit(point + invert * test_points,
					    st->calib_map);
		}
	}

	if (!invert) {
		cnt = ad9467_find_optimal_point(st->calib_map, 0, test_points,
						&val);
		/*
		 * We're happy if we find, at least, three good test points in
		 * a row.
		 */
		if (cnt < 3) {
			if (AD9467_CAN_INVERT(st)) {
				invert = true;
				goto retune;
			}

			if (!cnt)
				return -EIO;
		}
	} else {
		inv_cnt = ad9467_find_optimal_point(st->calib_map, test_points,
						    test_points, &inv_val);
		if (!inv_cnt && !cnt)
			return -EIO;
	}

	if (inv_cnt < cnt) {
		ret = ad9647_calibrate_polarity_set(st, false);
		if (ret)
			return ret;
	} else {
		/*
		 * polarity inverted is the last test to run. Hence, there's no
		 * need to re-do any configuration. We just need to "normalize"
		 * the selected value.
		 */
		val = inv_val - test_points;
	}

	if (st->info->has_dco)
		dev_dbg(dev, "%sDCO 0x%X CLK %lu Hz\n", inv_cnt >= cnt ? "INVERT " : "",
			val, sample_rate);
	else
		dev_dbg(dev, "%sIDELAY 0x%x\n", inv_cnt >= cnt ? "INVERT " : "",
			val);

	ret = ad9467_calibrate_apply(st, val);
	if (ret)
		return ret;

	/* finally apply the optimal value */
	return ad9647_calibrate_stop(st);
}

static int ad9467_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long m)
{
	struct ad9467_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		return ad9467_get_scale(st, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(st->clk);

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int __ad9467_update_clock(struct ad9467_state *st, long r_clk)
{
	int ret;

	ret = clk_set_rate(st->clk, r_clk);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);
	return ad9467_calibrate(st);
}

static int ad9467_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad9467_state *st = iio_priv(indio_dev);
	const struct ad9467_chip_info *info = st->info;
	unsigned long sample_rate;
	long r_clk;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return ad9467_set_scale(st, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		r_clk = clk_round_rate(st->clk, val);
		if (r_clk < 0 || r_clk > info->max_rate) {
			dev_warn(&st->spi->dev,
				 "Error setting ADC sample rate %ld", r_clk);
			return -EINVAL;
		}

		sample_rate = clk_get_rate(st->clk);
		/*
		 * clk_set_rate() would also do this but since we would still
		 * need it for avoiding an unnecessary calibration, do it now.
		 */
		if (sample_rate == r_clk)
			return 0;

		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = __ad9467_update_clock(st, r_clk);
		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int ad9467_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct ad9467_state *st = iio_priv(indio_dev);
	const struct ad9467_chip_info *info = st->info;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (const int *)st->scales;
		*type = IIO_VAL_INT_PLUS_MICRO;
		/* Values are stored in a 2D matrix */
		*length = info->num_scales * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ad9467_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad9467_state *st = iio_priv(indio_dev);
	unsigned int c;
	int ret;

	for (c = 0; c < st->info->num_channels; c++) {
		if (test_bit(c, scan_mask))
			ret = iio_backend_chan_enable(st->back, c);
		else
			ret = iio_backend_chan_disable(st->back, c);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct iio_info ad9467_info = {
	.read_raw = ad9467_read_raw,
	.write_raw = ad9467_write_raw,
	.update_scan_mode = ad9467_update_scan_mode,
	.debugfs_reg_access = ad9467_reg_access,
	.read_avail = ad9467_read_avail,
};

/* Same as above, but without .read_avail */
static const struct iio_info ad9467_info_no_read_avail = {
	.read_raw = ad9467_read_raw,
	.write_raw = ad9467_write_raw,
	.update_scan_mode = ad9467_update_scan_mode,
	.debugfs_reg_access = ad9467_reg_access,
};

static int ad9467_scale_fill(struct ad9467_state *st)
{
	const struct ad9467_chip_info *info = st->info;
	unsigned int i, val1, val2;

	st->scales = devm_kmalloc_array(&st->spi->dev, info->num_scales,
					sizeof(*st->scales), GFP_KERNEL);
	if (!st->scales)
		return -ENOMEM;

	for (i = 0; i < info->num_scales; i++) {
		__ad9467_get_scale(st, i, &val1, &val2);
		st->scales[i][0] = val1;
		st->scales[i][1] = val2;
	}

	return 0;
}

static int ad9467_reset(struct device *dev)
{
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR_OR_NULL(gpio))
		return PTR_ERR_OR_ZERO(gpio);

	fsleep(1);
	gpiod_set_value_cansleep(gpio, 0);
	fsleep(10 * USEC_PER_MSEC);

	return 0;
}

static int ad9467_iio_backend_get(struct ad9467_state *st)
{
	struct device *dev = &st->spi->dev;
	struct device_node *__back;

	st->back = devm_iio_backend_get(dev, NULL);
	if (!IS_ERR(st->back))
		return 0;
	/* If not found, don't error out as we might have legacy DT property */
	if (PTR_ERR(st->back) != -ENOENT)
		return PTR_ERR(st->back);

	/*
	 * if we don't get the backend using the normal API's, use the legacy
	 * 'adi,adc-dev' property. So we get all nodes with that property, and
	 * look for the one pointing at us. Then we directly lookup that fwnode
	 * on the backend list of registered devices. This is done so we don't
	 * make io-backends mandatory which would break DT ABI.
	 */
	for_each_node_with_property(__back, "adi,adc-dev") {
		struct device_node *__me;

		__me = of_parse_phandle(__back, "adi,adc-dev", 0);
		if (!__me)
			continue;

		if (!device_match_of_node(dev, __me)) {
			of_node_put(__me);
			continue;
		}

		of_node_put(__me);
		st->back = __devm_iio_backend_get_from_fwnode_lookup(dev,
								     of_fwnode_handle(__back));
		of_node_put(__back);
		return PTR_ERR_OR_ZERO(st->back);
	}

	return -ENODEV;
}

static int ad9467_test_mode_available_show(struct seq_file *s, void *ignored)
{
	struct ad9467_state *st = s->private;
	unsigned int bit;

	for_each_set_bit(bit, &st->info->test_mask, st->info->test_mask_len)
		seq_printf(s, "%s\n", ad9467_test_modes[bit]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ad9467_test_mode_available);

static ssize_t ad9467_chan_test_mode_read(struct file *file,
					  char __user *userbuf, size_t count,
					  loff_t *ppos)
{
	struct ad9467_chan_test_mode *chan = file->private_data;
	struct ad9467_state *st = chan->st;
	char buf[128] = {0};
	size_t len;
	int ret;

	if (chan->mode == AN877_ADC_TESTMODE_PN9_SEQ ||
	    chan->mode == AN877_ADC_TESTMODE_PN23_SEQ) {
		len = scnprintf(buf, sizeof(buf), "Running \"%s\" Test:\n\t",
				ad9467_test_modes[chan->mode]);

		ret = iio_backend_debugfs_print_chan_status(st->back, chan->idx,
							    buf + len,
							    sizeof(buf) - len);
		if (ret < 0)
			return ret;
		len += ret;
	} else if (chan->mode == AN877_ADC_TESTMODE_OFF) {
		len = scnprintf(buf, sizeof(buf), "No test Running...\n");
	} else {
		len = scnprintf(buf, sizeof(buf), "Running \"%s\" Test on CH:%u\n",
				ad9467_test_modes[chan->mode], chan->idx);
	}

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t ad9467_chan_test_mode_write(struct file *file,
					   const char __user *userbuf,
					   size_t count, loff_t *ppos)
{
	struct ad9467_chan_test_mode *chan = file->private_data;
	struct ad9467_state *st = chan->st;
	char test_mode[32] = {0};
	unsigned int mode;
	int ret;

	ret = simple_write_to_buffer(test_mode, sizeof(test_mode) - 1, ppos,
				     userbuf, count);
	if (ret < 0)
		return ret;

	for_each_set_bit(mode, &st->info->test_mask, st->info->test_mask_len) {
		if (sysfs_streq(test_mode, ad9467_test_modes[mode]))
			break;
	}

	if (mode == st->info->test_mask_len)
		return -EINVAL;

	guard(mutex)(&st->lock);

	if (mode == AN877_ADC_TESTMODE_OFF) {
		unsigned int out_mode;

		if (chan->mode == AN877_ADC_TESTMODE_PN9_SEQ ||
		    chan->mode == AN877_ADC_TESTMODE_PN23_SEQ) {
			ret = ad9467_backend_testmode_off(st, chan->idx);
			if (ret)
				return ret;
		}

		ret = ad9467_testmode_set(st, chan->idx, mode);
		if (ret)
			return ret;

		out_mode = st->info->default_output_mode | AN877_ADC_OUTPUT_MODE_TWOS_COMPLEMENT;
		ret = ad9467_outputmode_set(st, out_mode);
		if (ret)
			return ret;
	} else {
		ret = ad9467_outputmode_set(st, st->info->default_output_mode);
		if (ret)
			return ret;

		ret = ad9467_testmode_set(st, chan->idx, mode);
		if (ret)
			return ret;

		/*  some patterns have a backend matching monitoring block */
		if (mode == AN877_ADC_TESTMODE_PN9_SEQ) {
			ret = ad9467_backend_testmode_on(st, chan->idx,
							 IIO_BACKEND_ADI_PRBS_9A);
			if (ret)
				return ret;
		} else if (mode == AN877_ADC_TESTMODE_PN23_SEQ) {
			ret = ad9467_backend_testmode_on(st, chan->idx,
							 IIO_BACKEND_ADI_PRBS_23A);
			if (ret)
				return ret;
		}
	}

	chan->mode = mode;

	return count;
}

static const struct file_operations ad9467_chan_test_mode_fops = {
	.open = simple_open,
	.read = ad9467_chan_test_mode_read,
	.write = ad9467_chan_test_mode_write,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static ssize_t ad9467_dump_calib_table(struct file *file,
				       char __user *userbuf,
				       size_t count, loff_t *ppos)
{
	struct ad9467_state *st = file->private_data;
	unsigned int bit;
	/* +2 for the newline and +1 for the string termination */
	unsigned char map[AD9647_MAX_TEST_POINTS * 2 + 3];
	ssize_t len = 0;

	guard(mutex)(&st->lock);
	if (*ppos)
		goto out_read;

	for (bit = 0; bit < st->calib_map_size; bit++) {
		if (AD9467_CAN_INVERT(st) && bit == st->calib_map_size / 2)
			len += scnprintf(map + len, sizeof(map) - len, "\n");

		len += scnprintf(map + len, sizeof(map) - len, "%c",
				 test_bit(bit, st->calib_map) ? 'x' : 'o');
	}

	len += scnprintf(map + len, sizeof(map) - len, "\n");
out_read:
	return simple_read_from_buffer(userbuf, count, ppos, map, len);
}

static const struct file_operations ad9467_calib_table_fops = {
	.open = simple_open,
	.read = ad9467_dump_calib_table,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static void ad9467_debugfs_init(struct iio_dev *indio_dev)
{
	struct dentry *d = iio_get_debugfs_dentry(indio_dev);
	struct ad9467_state *st = iio_priv(indio_dev);
	char attr_name[32];
	unsigned int chan;

	if (!IS_ENABLED(CONFIG_DEBUG_FS))
		return;

	st->chan_test = devm_kcalloc(&st->spi->dev, st->info->num_channels,
				     sizeof(*st->chan_test), GFP_KERNEL);
	if (!st->chan_test)
		return;

	debugfs_create_file("calibration_table_dump", 0400, d, st,
			    &ad9467_calib_table_fops);

	for (chan = 0; chan < st->info->num_channels; chan++) {
		snprintf(attr_name, sizeof(attr_name), "in_voltage%u_test_mode",
			 chan);
		st->chan_test[chan].idx = chan;
		st->chan_test[chan].st = st;
		debugfs_create_file(attr_name, 0600, d, &st->chan_test[chan],
				    &ad9467_chan_test_mode_fops);
	}

	debugfs_create_file("in_voltage_test_mode_available", 0400, d, st,
			    &ad9467_test_mode_available_fops);

	iio_backend_debugfs_add(st->back, indio_dev);
}

static int ad9467_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad9467_state *st;
	unsigned int id;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -ENODEV;

	st->calib_map_size = st->info->test_points;
	if (AD9467_CAN_INVERT(st))
		st->calib_map_size *= 2;

	st->clk = devm_clk_get_enabled(&spi->dev, "adc-clk");
	if (IS_ERR(st->clk))
		return PTR_ERR(st->clk);

	st->pwrdown_gpio = devm_gpiod_get_optional(&spi->dev, "powerdown",
						   GPIOD_OUT_LOW);
	if (IS_ERR(st->pwrdown_gpio))
		return PTR_ERR(st->pwrdown_gpio);

	ret = ad9467_reset(&spi->dev);
	if (ret)
		return ret;

	ret = ad9467_scale_fill(st);
	if (ret)
		return ret;

	id = ad9467_spi_read(st, AN877_ADC_REG_CHIP_ID);
	if (id != st->info->id) {
		dev_err(&spi->dev, "Mismatch CHIP_ID, got 0x%X, expected 0x%X\n",
			id, st->info->id);
		return -ENODEV;
	}

	if (st->info->num_scales > 1)
		indio_dev->info = &ad9467_info;
	else
		indio_dev->info = &ad9467_info_no_read_avail;
	indio_dev->name = st->info->name;
	indio_dev->channels = st->info->channels;
	indio_dev->num_channels = st->info->num_channels;

	ret = ad9467_iio_backend_get(st);
	if (ret)
		return ret;

	ret = devm_iio_backend_request_buffer(&spi->dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(&spi->dev, st->back);
	if (ret)
		return ret;

	ret = ad9467_calibrate(st);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return ret;

	ad9467_debugfs_init(indio_dev);

	return 0;
}

static const struct of_device_id ad9467_of_match[] = {
	{ .compatible = "adi,ad9265", .data = &ad9265_chip_tbl, },
	{ .compatible = "adi,ad9434", .data = &ad9434_chip_tbl, },
	{ .compatible = "adi,ad9467", .data = &ad9467_chip_tbl, },
	{ .compatible = "adi,ad9643", .data = &ad9643_chip_tbl, },
	{ .compatible = "adi,ad9649", .data = &ad9649_chip_tbl, },
	{ .compatible = "adi,ad9652", .data = &ad9652_chip_tbl, },
	{ }
};
MODULE_DEVICE_TABLE(of, ad9467_of_match);

static const struct spi_device_id ad9467_ids[] = {
	{ "ad9265", (kernel_ulong_t)&ad9265_chip_tbl },
	{ "ad9434", (kernel_ulong_t)&ad9434_chip_tbl },
	{ "ad9467", (kernel_ulong_t)&ad9467_chip_tbl },
	{ "ad9643", (kernel_ulong_t)&ad9643_chip_tbl },
	{ "ad9649", (kernel_ulong_t)&ad9649_chip_tbl, },
	{ "ad9652", (kernel_ulong_t)&ad9652_chip_tbl, },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad9467_ids);

static struct spi_driver ad9467_driver = {
	.driver = {
		.name = "ad9467",
		.of_match_table = ad9467_of_match,
	},
	.probe = ad9467_probe,
	.id_table = ad9467_ids,
};
module_spi_driver(ad9467_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9467 ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_BACKEND");
