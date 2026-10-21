// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices LTC2378 ADC series driver
 *
 * Copyright (C) 2026 Analog Devices Inc.
 * Author: Marcelo Schmitt <marcelo.schmitt@analog.com>
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/device-id/spi.h>
#include <linux/device-id/of.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm.h>
#include <linux/spi/spi.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/types.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/types.h>

#define LTC2378_TDSDOBUSYL_NS		5
#define LTC2378_TBUSYLH_NS		13
#define LTC2378_TCNV_HIGH_NS		20
#define LTC2378_MAX_DATA_WAIT_US	4 /* max(TBUSYLH + TCONV + TDSDOBUSYL) */

#define LTC2378_CHANNEL(_sign, _real_bits, _storage_bits)			\
{										\
	.type = IIO_VOLTAGE,							\
	.indexed = 1,								\
	.differential = _sign,							\
	.channel = 0,								\
	.channel2 = _sign ? 1 : 0,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE),				\
	.scan_index = 0,							\
	.scan_type = {								\
		.format = _sign ? IIO_SCAN_FORMAT_SIGNED_INT :			\
				  IIO_SCAN_FORMAT_UNSIGNED_INT,			\
		.realbits = _real_bits,						\
		.storagebits = _storage_bits,					\
		.shift = _storage_bits - _real_bits,				\
		.endianness = IIO_BE,						\
	},									\
}

#define LTC2378_DIFF_CHANNEL(_real_bits)					\
	LTC2378_CHANNEL(1, _real_bits, (((_real_bits) > 16) ? 32 : 16))

#define LTC2378_PSEUDO_DIFF_CHANNEL(_real_bits)					\
	LTC2378_CHANNEL(0, _real_bits, (((_real_bits) > 16) ? 32 : 16))

#define LTC2378_OFFLOAD_CHANNEL(_sign, _real_bits, _storage_bits)		\
{										\
	.type = IIO_VOLTAGE,							\
	.indexed = 1,								\
	.differential = _sign,							\
	.channel = 0,								\
	.channel2 = _sign ? 1 : 0,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),		\
	.scan_index = 0,							\
	.scan_type = {								\
		.format = _sign ? IIO_SCAN_FORMAT_SIGNED_INT :			\
				  IIO_SCAN_FORMAT_UNSIGNED_INT,			\
		.realbits = _real_bits,						\
		.storagebits = _storage_bits,					\
		.shift = 0,							\
		.endianness = IIO_CPU,						\
	},									\
}

/*
 * Currently, the available offload hardware + DMA configuration only supports
 * pushing 32-bit data elements to DMA IIO buffers in CPU endianness. For 16-bit
 * precision parts, those 32-bit elements (in CPU endianness) contain 2 bytes
 * with data and 2 bytes always zeroed out. Nevertheless, for the offload use
 * case, the IIO buffer is configured for 32 storage bits in CPU endianness so
 * data is correctly aligned in user space despite 2 out of the 4 bytes being
 * zeros.
 */
#define LTC2378_OFFLOAD_DIFF_CHANNEL(_real_bits)			\
	LTC2378_OFFLOAD_CHANNEL(1, (_real_bits), 32)

#define LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(_real_bits)				\
	LTC2378_OFFLOAD_CHANNEL(0, (_real_bits), 32)

struct ltc2378_chip_info {
	const char *name;
	unsigned int internal_ref_uV;
	struct u32_fract internal_div;
	struct iio_chan_spec chan[2]; /* 1 physical chan + 1 timestamp chan */
	struct iio_chan_spec offload_chan;
	unsigned int max_sample_rate_Hz;
	unsigned int tconv_ns;
};

struct ltc2378_state {
	const struct ltc2378_chip_info *info;
	struct gpio_desc *cnv_gpio;
	struct spi_device *spi;
	struct mutex lock; /* Protect data acquisition cycle */
	int ref_uV;
	struct spi_transfer xfer;
	struct spi_transfer offload_xfer;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	struct pwm_waveform cnv_wf;
	struct spi_message offload_msg;
	struct spi_offload_trigger_config offload_trigger_config;
	struct pwm_device *cnv_trigger;
	unsigned int cnv_Hz;
	unsigned int sample_freq_range[3];

	/*
	 * DMA (thus cache coherency maintenance) requires the transfer buffers
	 * to live in their own cache lines.
	 */
	struct {
		union {
			__be16 sample_buf16_be;
			__be32 sample_buf32_be;
			u16 sample_buf16;
			u32 sample_buf32;
		} data;
		aligned_s64 timestamp;
	} scan __aligned(IIO_DMA_MINALIGN);
};

static const struct ltc2378_chip_info ltc2338_18_chip_info = {
	.name = "ltc2338-18",
	.internal_ref_uV = 2048000,
	.internal_div = { .numerator = 5, .denominator = 2 },
	.chan = { LTC2378_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 527,
};

static const struct ltc2378_chip_info ltc2364_16_chip_info = {
	.name = "ltc2364-16",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 250 * HZ_PER_KHZ,
	.tconv_ns = 3000,
};

static const struct ltc2378_chip_info ltc2364_18_chip_info = {
	.name = "ltc2364-18",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 250 * HZ_PER_KHZ,
	.tconv_ns = 3000,
};

static const struct ltc2378_chip_info ltc2367_16_chip_info = {
	.name = "ltc2367-16",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 500 * HZ_PER_KHZ,
	.tconv_ns = 1500,
};

static const struct ltc2378_chip_info ltc2367_18_chip_info = {
	.name = "ltc2367-18",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 500 * HZ_PER_KHZ,
	.tconv_ns = 1500,
};

static const struct ltc2378_chip_info ltc2368_16_chip_info = {
	.name = "ltc2368-16",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 527,
};

static const struct ltc2378_chip_info ltc2368_18_chip_info = {
	.name = "ltc2368-18",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 527,
};

static const struct ltc2378_chip_info ltc2369_18_chip_info = {
	.name = "ltc2369-18",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 1600 * HZ_PER_KHZ,
	.tconv_ns = 412,
};

static const struct ltc2378_chip_info ltc2370_16_chip_info = {
	.name = "ltc2370-16",
	.chan = { LTC2378_PSEUDO_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_PSEUDO_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 2 * HZ_PER_MHZ,
	.tconv_ns = 322,
};

static const struct ltc2378_chip_info ltc2376_16_chip_info = {
	.name = "ltc2376-16",
	.chan = { LTC2378_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 250 * HZ_PER_KHZ,
	.tconv_ns = 3000,
};

static const struct ltc2378_chip_info ltc2376_18_chip_info = {
	.name = "ltc2376-18",
	.chan = { LTC2378_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 250 * HZ_PER_KHZ,
	.tconv_ns = 3000,
};

static const struct ltc2378_chip_info ltc2376_20_chip_info = {
	.name = "ltc2376-20",
	.chan = { LTC2378_DIFF_CHANNEL(20), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(20),
	.max_sample_rate_Hz = 250 * HZ_PER_KHZ,
	.tconv_ns = 3000,
};

static const struct ltc2378_chip_info ltc2377_16_chip_info = {
	.name = "ltc2377-16",
	.chan = { LTC2378_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 500 * HZ_PER_KHZ,
	.tconv_ns = 1500,
};

static const struct ltc2378_chip_info ltc2377_18_chip_info = {
	.name = "ltc2377-18",
	.chan = { LTC2378_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 500 * HZ_PER_KHZ,
	.tconv_ns = 1500,
};

static const struct ltc2378_chip_info ltc2377_20_chip_info = {
	.name = "ltc2377-20",
	.chan = { LTC2378_DIFF_CHANNEL(20), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(20),
	.max_sample_rate_Hz = 500 * HZ_PER_KHZ,
	.tconv_ns = 1500,
};

static const struct ltc2378_chip_info ltc2378_16_chip_info = {
	.name = "ltc2378-16",
	.chan = { LTC2378_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 527,
};

static const struct ltc2378_chip_info ltc2378_18_chip_info = {
	.name = "ltc2378-18",
	.chan = { LTC2378_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 527,
};

static const struct ltc2378_chip_info ltc2378_20_chip_info = {
	.name = "ltc2378-20",
	.chan = { LTC2378_DIFF_CHANNEL(20), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(20),
	.max_sample_rate_Hz = 1 * HZ_PER_MHZ,
	.tconv_ns = 675,
};

static const struct ltc2378_chip_info ltc2379_18_chip_info = {
	.name = "ltc2379-18",
	.chan = { LTC2378_DIFF_CHANNEL(18), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(18),
	.max_sample_rate_Hz = 1600 * HZ_PER_KHZ,
	.tconv_ns = 412,
};

static const struct ltc2378_chip_info ltc2380_16_chip_info = {
	.name = "ltc2380-16",
	.chan = { LTC2378_DIFF_CHANNEL(16), IIO_CHAN_SOFT_TIMESTAMP(1) },
	.offload_chan = LTC2378_OFFLOAD_DIFF_CHANNEL(16),
	.max_sample_rate_Hz = 2 * HZ_PER_MHZ,
	.tconv_ns = 322,
};

static int ltc2378_convert_and_acquire(struct ltc2378_state *st)
{
	int ret;

	/* Cause a rising edge of CNV to initiate a new ADC conversion */
	gpiod_set_value_cansleep(st->cnv_gpio, 1);
	fsleep(LTC2378_MAX_DATA_WAIT_US);
	ret = spi_sync_transfer(st->spi, &st->xfer, 1);
	gpiod_set_value_cansleep(st->cnv_gpio, 0);

	return ret;
}

static irqreturn_t ltc2378_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ltc2378_state *st = iio_priv(indio_dev);
	int ret;

	ret = ltc2378_convert_and_acquire(st);
	if (ret < 0)
		goto err_out;

	iio_push_to_buffers_with_ts(indio_dev, &st->scan, sizeof(st->scan),
				    pf->timestamp);

err_out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int ltc2378_channel_single_read(const struct iio_chan_spec *chan,
				       struct ltc2378_state *st, int *val)
{
	const struct iio_scan_type *scan_type = &chan->scan_type;
	u32 sample;
	int ret;

	guard(mutex)(&st->lock);
	ret = ltc2378_convert_and_acquire(st);
	if (ret)
		return ret;

	if (chan->scan_type.endianness == IIO_BE) {
		if (chan->scan_type.realbits > 16)
			sample = be32_to_cpu(st->scan.data.sample_buf32_be);
		else
			sample = be16_to_cpu(st->scan.data.sample_buf16_be);
	} else { /* IIO_CPU */
		if (chan->scan_type.realbits > 16)
			sample = st->scan.data.sample_buf32;
		else
			sample = st->scan.data.sample_buf16;
	}

	sample >>= chan->scan_type.shift;

	if (scan_type->format == IIO_SCAN_FORMAT_SIGNED_INT)
		*val = sign_extend32(sample, scan_type->realbits - 1);
	else
		*val = sample;

	return 0;
}

static int ltc2378_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct ltc2378_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		ret = ltc2378_channel_single_read(chan, st, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE: {
		struct u32_fract fract = st->info->internal_div;
		*val = st->ref_uV / MILLI;
		if (fract.numerator && fract.denominator)
			*val = mult_frac(*val, fract.numerator, fract.denominator);
		/*
		 * For all LTC2378-like devices, the amount of bits that express
		 * voltage magnitude depend on the polarity / output code format:
		 * - straight binary: All precision/resolution bits are used.
		 * - 2's complement: One of the precision bits is used for sign.
		 */
		if (chan->scan_type.format == IIO_SCAN_FORMAT_SIGNED_INT)
			*val2 = chan->scan_type.realbits - 1;
		else
			*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->cnv_Hz;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ltc2378_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length, long mask)
{
	struct ltc2378_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = st->sample_freq_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

/*
 * SPI offload wiring schema
 *
 *     +-------------+         +-------------+
 *     |         CNV |<-----+--| GPIO        |
 *     |             |      +--| PWM0        |
 *     |             |         |             |
 *     |             |      +--| PWM1        |
 *     |             |      |  +-------------+
 *     |             |      +->| TRIGGER     |
 *     |             |         |             |
 *     |     ADC     |         |    SPI      |
 *     |             |         | controller  |
 *     |             |         |             |
 *     |         SDI |<--------| SDO         |
 *     |         SDO |-------->| SDI         |
 *     |        SCLK |<--------| SCLK        |
 *     +-------------+         +-------------+
 *
 */
static int ltc2378_update_conversion_rate(struct ltc2378_state *st, int freq_Hz)
{
	struct spi_offload_trigger_config config = st->offload_trigger_config;
	unsigned int min_read_offset, offload_period_ns;
	struct pwm_waveform cnv_wf = { };
	u64 target = LTC2378_TCNV_HIGH_NS;
	unsigned int count;
	u64 offload_offset_ns;
	int ret;

	if (freq_Hz == 0)
		return -EINVAL;

	if (!in_range(freq_Hz, 1, st->info->max_sample_rate_Hz))
		return -ERANGE;

	/* Configure CNV PWM waveform */
	cnv_wf.period_length_ns = DIV_ROUND_CLOSEST(NSEC_PER_SEC, freq_Hz);

	/*
	 * Ensure CNV high time meets minimum requirement (20ns). The PWM
	 * hardware may round the duty cycle, so iterate until we get at least
	 * the minimum required high time (or reach a try count limit).
	 */
	count = 100;
	do {
		cnv_wf.duty_length_ns = target;
		ret = pwm_round_waveform_might_sleep(st->cnv_trigger, &cnv_wf);
		if (ret)
			return ret;
		target += 10;  /* Increment by PWM duty cycle period */
	} while (count-- && cnv_wf.duty_length_ns < LTC2378_TCNV_HIGH_NS);

	/* Check the minimum CNV high time is met */
	if (cnv_wf.duty_length_ns < LTC2378_TCNV_HIGH_NS)
		return -EDOM;

	/*
	 * Configure SPI offload PWM trigger.
	 * The trigger should fire after tBUSYLH + tCONV + tDSDOBUSYL.
	 * Minimum time needed: TBUSYLH (13ns) + TCONV (part-specific) + TDSDOBUSYL (5ns)
	 *
	 * Use the same period as CNV PWM to avoid timing issues.
	 * Convert back from period to frequency for the SPI offload API.
	 */
	offload_period_ns = cnv_wf.period_length_ns;
	config.periodic.frequency_hz = div_u64(HZ_PER_GHZ, offload_period_ns);
	min_read_offset = LTC2378_TBUSYLH_NS + st->info->tconv_ns + LTC2378_TDSDOBUSYL_NS;
	offload_offset_ns = min_read_offset;
	count = 100;
	do {
		config.periodic.offset_ns = offload_offset_ns;
		ret = spi_offload_trigger_validate(st->offload_trigger, &config);
		if (ret)
			return ret;
		offload_offset_ns += 10;
	} while (count-- && config.periodic.offset_ns < min_read_offset);

	/* Check the minimum CNV to SCLK delay is met */
	if (config.periodic.offset_ns < min_read_offset)
		return -EDOM;

	/* Check the PWM periods remain the same */
	offload_period_ns = div64_u64(HZ_PER_GHZ, config.periodic.frequency_hz);
	if (cnv_wf.period_length_ns != offload_period_ns)
		return -EDOM;

	st->offload_trigger_config = config;
	st->cnv_wf = cnv_wf;
	st->cnv_Hz = DIV_ROUND_CLOSEST_ULL(HZ_PER_GHZ, cnv_wf.period_length_ns);

	return 0;
}

static int ltc2378_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct ltc2378_state *st = iio_priv(indio_dev);

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ltc2378_update_conversion_rate(st, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info ltc2378_iio_info = {
	.read_raw = &ltc2378_read_raw,
};

static const struct iio_info ltc2378_offload_iio_info = {
	.read_raw = &ltc2378_read_raw,
	.read_avail = &ltc2378_read_avail,
	.write_raw = &ltc2378_write_raw,
};

static int ltc2378_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ltc2378_state *st = iio_priv(indio_dev);
	int ret;

	ret = pwm_set_waveform_might_sleep(st->cnv_trigger, &st->cnv_wf, true);
	if (ret)
		return ret;

	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger,
					 &st->offload_trigger_config);
	if (ret)
		goto out_pwm_disable;

	return 0;

out_pwm_disable:
	pwm_disable(st->cnv_trigger);
	return ret;
}

static int ltc2378_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ltc2378_state *st = iio_priv(indio_dev);

	spi_offload_trigger_disable(st->offload, st->offload_trigger);
	pwm_disable(st->cnv_trigger);

	return 0;
}

static const struct iio_buffer_setup_ops ltc2378_offload_buffer_ops = {
	.postenable = &ltc2378_offload_buffer_postenable,
	.predisable = &ltc2378_offload_buffer_predisable,
};

static int ltc2378_prepare_offload_message(struct device *dev,
					   struct ltc2378_state *st)
{
	unsigned int resolution = st->info->offload_chan.scan_type.realbits;

	st->offload_xfer.bits_per_word = resolution;
	st->offload_xfer.len = spi_bpw_to_bytes(resolution);
	st->offload_xfer.offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;

	/* Initialize message with offload */
	spi_message_init_with_transfers(&st->offload_msg, &st->offload_xfer, 1);
	st->offload_msg.offload = st->offload;

	return devm_spi_optimize_message(dev, st->spi, &st->offload_msg);
}

static int ltc2378_spi_offload_setup(struct iio_dev *indio_dev,
				     struct ltc2378_state *st)
{
	struct device *dev = &st->spi->dev;
	struct dma_chan *rx_dma;

	indio_dev->setup_ops = &ltc2378_offload_buffer_ops;

	st->offload_trigger = devm_spi_offload_trigger_get(dev, st->offload,
							   SPI_OFFLOAD_TRIGGER_PERIODIC);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
				     "failed to get offload trigger\n");

	st->offload_trigger_config.type = SPI_OFFLOAD_TRIGGER_PERIODIC;

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma), "failed to get offload RX DMA\n");

	return devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev, rx_dma,
							   IIO_BUFFER_DIRECTION_IN);
}

static int ltc2378_pwm_get(struct ltc2378_state *st)
{
	struct device *dev = &st->spi->dev;

	st->cnv_trigger = devm_pwm_get(dev, NULL);
	if (IS_ERR(st->cnv_trigger))
		return dev_err_probe(dev, PTR_ERR(st->cnv_trigger),
				     "failed to get cnv pwm\n");

	/*
	 * Disable the PWM connected to CNV in case it was left running by
	 * something else.
	 */
	pwm_disable(st->cnv_trigger);

	return 0;
}

static const struct spi_offload_config ltc2378_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static int ltc2378_refin_setup(struct device *dev, struct ltc2378_state *st)
{
	int ret;

	/*
	 * The internal reference buffer amplifies both the internal reference
	 * and REFIN by a factor of 2.
	 */
	ret = devm_regulator_get_enable_read_voltage(dev, "refin");
	if (ret == -ENODEV) { /* refin is optional */
		st->ref_uV = st->info->internal_ref_uV * 2;
		return 0;
	}

	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read refin regulator\n");

	st->ref_uV = ret * 2;

	return 0;
}

static int ltc2378_ref_setup(struct device *dev, struct ltc2378_state *st)
{
	int ret;

	ret = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read ref regulator\n");

	st->ref_uV = ret;

	return 0;
}

static int ltc2378_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ltc2378_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -EINVAL;

	if (st->info->internal_ref_uV)
		ret = ltc2378_refin_setup(dev, st);
	else
		ret = ltc2378_ref_setup(dev, st);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	st->cnv_gpio = devm_gpiod_get(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "failed to get CNV GPIO");

	st->offload = devm_spi_offload_get(dev, spi, &ltc2378_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	/* Fall back to low speed usage when no SPI offload is available. */
	if (ret == -ENODEV) {
		indio_dev->info = &ltc2378_iio_info;
		indio_dev->channels = st->info->chan;
		indio_dev->num_channels = ARRAY_SIZE(st->info->chan);

		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      iio_pollfunc_store_time,
						      ltc2378_trigger_handler,
						      NULL);
		if (ret)
			return ret;
	} else if (ret) {
		return dev_err_probe(dev, ret, "failed to get offload\n");
	} else {
		indio_dev->info = &ltc2378_offload_iio_info;
		indio_dev->channels = &st->info->offload_chan;
		indio_dev->num_channels = 1;
		ret = ltc2378_spi_offload_setup(indio_dev, st);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to setup SPI offload\n");

		ret = ltc2378_pwm_get(st);
		if (ret)
			return dev_err_probe(dev, ret, "failed to get PWM\n");

		st->sample_freq_range[0] = 1; /* min */
		st->sample_freq_range[1] = 1; /* step */
		st->sample_freq_range[2] = st->info->max_sample_rate_Hz; /* max */

		/*
		 * Start with a slower sampling rate so there is some room for
		 * adjusting the sample averaging and the sampling frequency
		 * without hitting the maximum conversion rate.
		 */
		ret = ltc2378_update_conversion_rate(st, st->info->max_sample_rate_Hz >> 4);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set offload samp freq\n");

		ret = ltc2378_prepare_offload_message(&spi->dev, st);
		if (ret)
			return dev_err_probe(dev, ret, "failed to optimize SPI message\n");

		/*
		 * Set single-read transfer bits_per_word so the SPI subsystem
		 * rearranges data to CPU endianness, enabling us to reuse
		 * offload_chan specifications for single-shot reads.
		 */
		st->xfer.bits_per_word = st->info->offload_chan.scan_type.realbits;
	}

	st->xfer.rx_buf = &st->scan.data;
	st->xfer.len = spi_bpw_to_bytes(indio_dev->channels[0].scan_type.realbits);

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ltc2378_of_match[] = {
	{ .compatible = "adi,ltc2338-18", .data = &ltc2338_18_chip_info },
	{ .compatible = "adi,ltc2364-16", .data = &ltc2364_16_chip_info },
	{ .compatible = "adi,ltc2364-18", .data = &ltc2364_18_chip_info },
	{ .compatible = "adi,ltc2367-16", .data = &ltc2367_16_chip_info },
	{ .compatible = "adi,ltc2367-18", .data = &ltc2367_18_chip_info },
	{ .compatible = "adi,ltc2368-16", .data = &ltc2368_16_chip_info },
	{ .compatible = "adi,ltc2368-18", .data = &ltc2368_18_chip_info },
	{ .compatible = "adi,ltc2369-18", .data = &ltc2369_18_chip_info },
	{ .compatible = "adi,ltc2370-16", .data = &ltc2370_16_chip_info },
	{ .compatible = "adi,ltc2376-16", .data = &ltc2376_16_chip_info },
	{ .compatible = "adi,ltc2376-18", .data = &ltc2376_18_chip_info },
	{ .compatible = "adi,ltc2376-20", .data = &ltc2376_20_chip_info },
	{ .compatible = "adi,ltc2377-16", .data = &ltc2377_16_chip_info },
	{ .compatible = "adi,ltc2377-18", .data = &ltc2377_18_chip_info },
	{ .compatible = "adi,ltc2377-20", .data = &ltc2377_20_chip_info },
	{ .compatible = "adi,ltc2378-16", .data = &ltc2378_16_chip_info },
	{ .compatible = "adi,ltc2378-18", .data = &ltc2378_18_chip_info },
	{ .compatible = "adi,ltc2378-20", .data = &ltc2378_20_chip_info },
	{ .compatible = "adi,ltc2379-18", .data = &ltc2379_18_chip_info },
	{ .compatible = "adi,ltc2380-16", .data = &ltc2380_16_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2378_of_match);

static const struct spi_device_id ltc2378_spi_id[] = {
	{ .name = "ltc2338-18", .driver_data = (kernel_ulong_t)&ltc2338_18_chip_info },
	{ .name = "ltc2364-16", .driver_data = (kernel_ulong_t)&ltc2364_16_chip_info },
	{ .name = "ltc2364-18", .driver_data = (kernel_ulong_t)&ltc2364_18_chip_info },
	{ .name = "ltc2367-16", .driver_data = (kernel_ulong_t)&ltc2367_16_chip_info },
	{ .name = "ltc2367-18", .driver_data = (kernel_ulong_t)&ltc2367_18_chip_info },
	{ .name = "ltc2368-16", .driver_data = (kernel_ulong_t)&ltc2368_16_chip_info },
	{ .name = "ltc2368-18", .driver_data = (kernel_ulong_t)&ltc2368_18_chip_info },
	{ .name = "ltc2369-18", .driver_data = (kernel_ulong_t)&ltc2369_18_chip_info },
	{ .name = "ltc2370-16", .driver_data = (kernel_ulong_t)&ltc2370_16_chip_info },
	{ .name = "ltc2376-16", .driver_data = (kernel_ulong_t)&ltc2376_16_chip_info },
	{ .name = "ltc2376-18", .driver_data = (kernel_ulong_t)&ltc2376_18_chip_info },
	{ .name = "ltc2376-20", .driver_data = (kernel_ulong_t)&ltc2376_20_chip_info },
	{ .name = "ltc2377-16", .driver_data = (kernel_ulong_t)&ltc2377_16_chip_info },
	{ .name = "ltc2377-18", .driver_data = (kernel_ulong_t)&ltc2377_18_chip_info },
	{ .name = "ltc2377-20", .driver_data = (kernel_ulong_t)&ltc2377_20_chip_info },
	{ .name = "ltc2378-16", .driver_data = (kernel_ulong_t)&ltc2378_16_chip_info },
	{ .name = "ltc2378-18", .driver_data = (kernel_ulong_t)&ltc2378_18_chip_info },
	{ .name = "ltc2378-20", .driver_data = (kernel_ulong_t)&ltc2378_20_chip_info },
	{ .name = "ltc2379-18", .driver_data = (kernel_ulong_t)&ltc2379_18_chip_info },
	{ .name = "ltc2380-16", .driver_data = (kernel_ulong_t)&ltc2380_16_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ltc2378_spi_id);

static struct spi_driver ltc2378_driver = {
	.driver = {
		.name = "ltc2378",
		.of_match_table = ltc2378_of_match
	},
	.probe = ltc2378_probe,
	.id_table = ltc2378_spi_id,
};
module_spi_driver(ltc2378_driver);

MODULE_AUTHOR("Marcelo Schmitt <marcelo.schmitt@analog.com>");
MODULE_DESCRIPTION("Analog Devices LTC2378 ADC series driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
MODULE_IMPORT_NS("SPI_OFFLOAD");
