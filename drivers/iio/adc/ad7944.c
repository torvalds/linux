// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD7944/85/86 PulSAR ADC family driver.
 *
 * Copyright 2024 Analog Devices, Inc.
 * Copyright 2024 BayLibre, SAS
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/spi.h>
#include <linux/string_helpers.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AD7944_INTERNAL_REF_MV		4096

struct ad7944_timing_spec {
	/* Normal mode max conversion time (t_{CONV}). */
	unsigned int conv_ns;
	/* TURBO mode max conversion time (t_{CONV}). */
	unsigned int turbo_conv_ns;
};

enum ad7944_spi_mode {
	/* datasheet calls this "4-wire mode" */
	AD7944_SPI_MODE_DEFAULT,
	/* datasheet calls this "3-wire mode" (not related to SPI_3WIRE!) */
	AD7944_SPI_MODE_SINGLE,
	/* datasheet calls this "chain mode" */
	AD7944_SPI_MODE_CHAIN,
};

/* maps adi,spi-mode property value to enum */
static const char * const ad7944_spi_modes[] = {
	[AD7944_SPI_MODE_DEFAULT] = "",
	[AD7944_SPI_MODE_SINGLE] = "single",
	[AD7944_SPI_MODE_CHAIN] = "chain",
};

struct ad7944_adc {
	struct spi_device *spi;
	enum ad7944_spi_mode spi_mode;
	struct spi_transfer xfers[3];
	struct spi_message msg;
	struct spi_transfer offload_xfers[2];
	struct spi_message offload_msg;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	unsigned long offload_trigger_hz;
	int sample_freq_range[3];
	void *chain_mode_buf;
	/* Chip-specific timing specifications. */
	const struct ad7944_timing_spec *timing_spec;
	/* GPIO connected to CNV pin. */
	struct gpio_desc *cnv;
	/* Optional GPIO to enable turbo mode. */
	struct gpio_desc *turbo;
	/* Indicates TURBO is hard-wired to be always enabled. */
	bool always_turbo;
	/* Reference voltage (millivolts). */
	unsigned int ref_mv;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	struct {
		union {
			u16 u16;
			u32 u32;
		} raw;
		aligned_s64 timestamp;
	 } sample __aligned(IIO_DMA_MINALIGN);
};

/* quite time before CNV rising edge */
#define AD7944_T_QUIET_NS	20
/* minimum CNV high time to trigger conversion */
#define AD7944_T_CNVH_NS	10

static const struct ad7944_timing_spec ad7944_timing_spec = {
	.conv_ns = 420,
	.turbo_conv_ns = 320,
};

static const struct ad7944_timing_spec ad7986_timing_spec = {
	.conv_ns = 500,
	.turbo_conv_ns = 400,
};

struct ad7944_chip_info {
	const char *name;
	const struct ad7944_timing_spec *timing_spec;
	u32 max_sample_rate_hz;
	const struct iio_chan_spec channels[2];
	const struct iio_chan_spec offload_channels[1];
};

/* get number of bytes for SPI xfer */
#define AD7944_SPI_BYTES(scan_type) ((scan_type).realbits > 16 ? 4 : 2)

/*
 * AD7944_DEFINE_CHIP_INFO - Define a chip info structure for a specific chip
 * @_name: The name of the chip
 * @_ts: The timing specification for the chip
 * @_max: The maximum sample rate in Hz
 * @_bits: The number of bits in the conversion result
 * @_diff: Whether the chip is true differential or not
 */
#define AD7944_DEFINE_CHIP_INFO(_name, _ts, _max, _bits, _diff)		\
static const struct ad7944_chip_info _name##_chip_info = {		\
	.name = #_name,							\
	.timing_spec = &_ts##_timing_spec,				\
	.max_sample_rate_hz = _max,					\
	.channels = {							\
		{							\
			.type = IIO_VOLTAGE,				\
			.indexed = 1,					\
			.differential = _diff,				\
			.channel = 0,					\
			.channel2 = _diff ? 1 : 0,			\
			.scan_index = 0,				\
			.scan_type.sign = _diff ? 's' : 'u',		\
			.scan_type.realbits = _bits,			\
			.scan_type.storagebits = _bits > 16 ? 32 : 16,	\
			.scan_type.endianness = IIO_CPU,		\
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)	\
					| BIT(IIO_CHAN_INFO_SCALE),	\
		},							\
		IIO_CHAN_SOFT_TIMESTAMP(1),				\
	},								\
	.offload_channels = {						\
		{							\
			.type = IIO_VOLTAGE,				\
			.indexed = 1,					\
			.differential = _diff,				\
			.channel = 0,					\
			.channel2 = _diff ? 1 : 0,			\
			.scan_index = 0,				\
			.scan_type.sign = _diff ? 's' : 'u',		\
			.scan_type.realbits = _bits,			\
			.scan_type.storagebits = 32,			\
			.scan_type.endianness = IIO_CPU,		\
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)	\
					| BIT(IIO_CHAN_INFO_SCALE)	\
					| BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
			.info_mask_separate_available =			\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),		\
		},							\
	},								\
}

/*
 * Notes on the offload channels:
 * - There is no soft timestamp since everything is done in hardware.
 * - There is a sampling frequency attribute added. This controls the SPI
 *   offload trigger.
 * - The storagebits value depends on the SPI offload provider. Currently there
 *   is only one supported provider, namely the ADI PULSAR ADC HDL project,
 *   which always uses 32-bit words for data values, even for <= 16-bit ADCs.
 *   So the value is just hardcoded to 32 for now.
 */

/* pseudo-differential with ground sense */
AD7944_DEFINE_CHIP_INFO(ad7944, ad7944, 2.5 * MEGA, 14, 0);
AD7944_DEFINE_CHIP_INFO(ad7985, ad7944, 2.5 * MEGA, 16, 0);
/* fully differential */
AD7944_DEFINE_CHIP_INFO(ad7986, ad7986, 2 * MEGA, 18, 1);

static int ad7944_3wire_cs_mode_init_msg(struct device *dev, struct ad7944_adc *adc,
					 const struct iio_chan_spec *chan)
{
	unsigned int t_conv_ns = adc->always_turbo ? adc->timing_spec->turbo_conv_ns
						   : adc->timing_spec->conv_ns;
	struct spi_transfer *xfers = adc->xfers;

	/*
	 * CS is tied to CNV and we need a low to high transition to start the
	 * conversion, so place CNV low for t_QUIET to prepare for this.
	 */
	xfers[0].delay.value = AD7944_T_QUIET_NS;
	xfers[0].delay.unit = SPI_DELAY_UNIT_NSECS;

	/*
	 * CS has to be high for full conversion time to avoid triggering the
	 * busy indication.
	 */
	xfers[1].cs_off = 1;
	xfers[1].delay.value = t_conv_ns;
	xfers[1].delay.unit = SPI_DELAY_UNIT_NSECS;

	/* Then we can read the data during the acquisition phase */
	xfers[2].rx_buf = &adc->sample.raw;
	xfers[2].len = AD7944_SPI_BYTES(chan->scan_type);
	xfers[2].bits_per_word = chan->scan_type.realbits;

	spi_message_init_with_transfers(&adc->msg, xfers, 3);

	return devm_spi_optimize_message(dev, adc->spi, &adc->msg);
}

static int ad7944_4wire_mode_init_msg(struct device *dev, struct ad7944_adc *adc,
				      const struct iio_chan_spec *chan)
{
	unsigned int t_conv_ns = adc->always_turbo ? adc->timing_spec->turbo_conv_ns
						   : adc->timing_spec->conv_ns;
	struct spi_transfer *xfers = adc->xfers;

	/*
	 * CS has to be high for full conversion time to avoid triggering the
	 * busy indication.
	 */
	xfers[0].cs_off = 1;
	xfers[0].delay.value = t_conv_ns;
	xfers[0].delay.unit = SPI_DELAY_UNIT_NSECS;

	xfers[1].rx_buf = &adc->sample.raw;
	xfers[1].len = AD7944_SPI_BYTES(chan->scan_type);
	xfers[1].bits_per_word = chan->scan_type.realbits;

	spi_message_init_with_transfers(&adc->msg, xfers, 2);

	return devm_spi_optimize_message(dev, adc->spi, &adc->msg);
}

static int ad7944_chain_mode_init_msg(struct device *dev, struct ad7944_adc *adc,
				      const struct iio_chan_spec *chan,
				      u32 n_chain_dev)
{
	struct spi_transfer *xfers = adc->xfers;

	/*
	 * NB: SCLK has to be low before we toggle CS to avoid triggering the
	 * busy indication.
	 */
	if (adc->spi->mode & SPI_CPOL)
		return dev_err_probe(dev, -EINVAL,
				     "chain mode requires ~SPI_CPOL\n");

	/*
	 * We only support CNV connected to CS in chain mode and we need CNV
	 * to be high during the transfer to trigger the conversion.
	 */
	if (!(adc->spi->mode & SPI_CS_HIGH))
		return dev_err_probe(dev, -EINVAL,
				     "chain mode requires SPI_CS_HIGH\n");

	/* CNV has to be high for full conversion time before reading data. */
	xfers[0].delay.value = adc->timing_spec->conv_ns;
	xfers[0].delay.unit = SPI_DELAY_UNIT_NSECS;

	xfers[1].rx_buf = adc->chain_mode_buf;
	xfers[1].len = AD7944_SPI_BYTES(chan->scan_type) * n_chain_dev;
	xfers[1].bits_per_word = chan->scan_type.realbits;

	spi_message_init_with_transfers(&adc->msg, xfers, 2);

	return devm_spi_optimize_message(dev, adc->spi, &adc->msg);
}

/*
 * Unlike ad7944_3wire_cs_mode_init_msg(), this creates a message that reads
 * during the conversion phase instead of the acquisition phase when reading
 * a sample from the ADC. This is needed to be able to read at the maximum
 * sample rate. It requires the SPI controller to have offload support and a
 * high enough SCLK rate to read the sample during the conversion phase.
 */
static int ad7944_3wire_cs_mode_init_offload_msg(struct device *dev,
						 struct ad7944_adc *adc,
						 const struct iio_chan_spec *chan)
{
	struct spi_transfer *xfers = adc->offload_xfers;
	int ret;

	/*
	 * CS is tied to CNV and we need a low to high transition to start the
	 * conversion, so place CNV low for t_QUIET to prepare for this.
	 */
	xfers[0].delay.value = AD7944_T_QUIET_NS;
	xfers[0].delay.unit = SPI_DELAY_UNIT_NSECS;
	/* CNV has to be high for a minimum time to trigger conversion. */
	xfers[0].cs_change = 1;
	xfers[0].cs_change_delay.value = AD7944_T_CNVH_NS;
	xfers[0].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	/* Then we can read the previous sample during the conversion phase */
	xfers[1].offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	xfers[1].len = AD7944_SPI_BYTES(chan->scan_type);
	xfers[1].bits_per_word = chan->scan_type.realbits;

	spi_message_init_with_transfers(&adc->offload_msg, xfers,
					ARRAY_SIZE(adc->offload_xfers));

	adc->offload_msg.offload = adc->offload;

	ret = devm_spi_optimize_message(dev, adc->spi, &adc->offload_msg);
	if (ret)
		return dev_err_probe(dev, ret, "failed to prepare offload msg\n");

	return 0;
}

/**
 * ad7944_convert_and_acquire - Perform a single conversion and acquisition
 * @adc: The ADC device structure
 * Return: 0 on success, a negative error code on failure
 *
 * Perform a conversion and acquisition of a single sample using the
 * pre-optimized adc->msg.
 *
 * Upon successful return adc->sample.raw will contain the conversion result
 * (or adc->chain_mode_buf if the device is using chain mode).
 */
static int ad7944_convert_and_acquire(struct ad7944_adc *adc)
{
	int ret;

	/*
	 * In 4-wire mode, the CNV line is held high for the entire conversion
	 * and acquisition process. In other modes adc->cnv is NULL and is
	 * ignored (CS is wired to CNV in those cases).
	 */
	gpiod_set_value_cansleep(adc->cnv, 1);
	ret = spi_sync(adc->spi, &adc->msg);
	gpiod_set_value_cansleep(adc->cnv, 0);

	return ret;
}

static int ad7944_single_conversion(struct ad7944_adc *adc,
				    const struct iio_chan_spec *chan,
				    int *val)
{
	int ret;

	ret = ad7944_convert_and_acquire(adc);
	if (ret)
		return ret;

	if (adc->spi_mode == AD7944_SPI_MODE_CHAIN) {
		if (chan->scan_type.realbits > 16)
			*val = ((u32 *)adc->chain_mode_buf)[chan->scan_index];
		else
			*val = ((u16 *)adc->chain_mode_buf)[chan->scan_index];
	} else {
		if (chan->scan_type.realbits > 16)
			*val = adc->sample.raw.u32;
		else
			*val = adc->sample.raw.u16;
	}

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(*val, chan->scan_type.realbits - 1);
	else
		*val &= GENMASK(chan->scan_type.realbits - 1, 0);

	return IIO_VAL_INT;
}

static int ad7944_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct ad7944_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = adc->sample_freq_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int ad7944_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long info)
{
	struct ad7944_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7944_single_conversion(adc, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = adc->ref_mv;

			if (chan->scan_type.sign == 's')
				*val2 = chan->scan_type.realbits - 1;
			else
				*val2 = chan->scan_type.realbits;

			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = adc->offload_trigger_hz;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ad7944_set_sample_freq(struct ad7944_adc *adc, int val)
{
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = val,
		},
	};
	int ret;

	ret = spi_offload_trigger_validate(adc->offload_trigger, &config);
	if (ret)
		return ret;

	adc->offload_trigger_hz = config.periodic.frequency_hz;

	return 0;
}

static int ad7944_write_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int val, int val2, long info)
{
	struct ad7944_adc *adc = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val < 1 || val > adc->sample_freq_range[2])
			return -EINVAL;

		return ad7944_set_sample_freq(adc, val);
	default:
		return -EINVAL;
	}
}

static int ad7944_write_raw_get_fmt(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static const struct iio_info ad7944_iio_info = {
	.read_avail = &ad7944_read_avail,
	.read_raw = &ad7944_read_raw,
	.write_raw = &ad7944_write_raw,
	.write_raw_get_fmt = &ad7944_write_raw_get_fmt,
};

static int ad7944_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad7944_adc *adc = iio_priv(indio_dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = adc->offload_trigger_hz,
		},
	};
	int ret;

	gpiod_set_value_cansleep(adc->turbo, 1);

	ret = spi_offload_trigger_enable(adc->offload, adc->offload_trigger,
					 &config);
	if (ret)
		gpiod_set_value_cansleep(adc->turbo, 0);

	return ret;
}

static int ad7944_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7944_adc *adc = iio_priv(indio_dev);

	spi_offload_trigger_disable(adc->offload, adc->offload_trigger);
	gpiod_set_value_cansleep(adc->turbo, 0);

	return 0;
}

static const struct iio_buffer_setup_ops ad7944_offload_buffer_setup_ops = {
	.postenable = &ad7944_offload_buffer_postenable,
	.predisable = &ad7944_offload_buffer_predisable,
};

static irqreturn_t ad7944_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7944_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = ad7944_convert_and_acquire(adc);
	if (ret)
		goto out;

	if (adc->spi_mode == AD7944_SPI_MODE_CHAIN)
		iio_push_to_buffers_with_timestamp(indio_dev, adc->chain_mode_buf,
						   pf->timestamp);
	else
		iio_push_to_buffers_with_timestamp(indio_dev, &adc->sample.raw,
						   pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/**
 * ad7944_chain_mode_alloc - allocate and initialize channel specs and buffers
 *                           for daisy-chained devices
 * @dev: The device for devm_ functions
 * @chan_template: The channel template for the devices (array of 2 channels
 *                 voltage and timestamp)
 * @n_chain_dev: The number of devices in the chain
 * @chain_chan: Pointer to receive the allocated channel specs
 * @chain_mode_buf: Pointer to receive the allocated rx buffer
 * @chain_scan_masks: Pointer to receive the allocated scan masks
 * Return: 0 on success, a negative error code on failure
 */
static int ad7944_chain_mode_alloc(struct device *dev,
				   const struct iio_chan_spec *chan_template,
				   u32 n_chain_dev,
				   struct iio_chan_spec **chain_chan,
				   void **chain_mode_buf,
				   unsigned long **chain_scan_masks)
{
	struct iio_chan_spec *chan;
	size_t chain_mode_buf_size;
	unsigned long *scan_masks;
	void *buf;
	int i;

	/* 1 channel for each device in chain plus 1 for soft timestamp */

	chan = devm_kcalloc(dev, n_chain_dev + 1, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	for (i = 0; i < n_chain_dev; i++) {
		chan[i] = chan_template[0];

		if (chan_template[0].differential) {
			chan[i].channel = 2 * i;
			chan[i].channel2 = 2 * i + 1;
		} else {
			chan[i].channel = i;
		}

		chan[i].scan_index = i;
	}

	/* soft timestamp */
	chan[i] = chan_template[1];
	chan[i].scan_index = i;

	*chain_chan = chan;

	/* 1 word for each voltage channel + aligned u64 for timestamp */

	chain_mode_buf_size = ALIGN(n_chain_dev *
		AD7944_SPI_BYTES(chan[0].scan_type), sizeof(u64)) + sizeof(u64);
	buf = devm_kzalloc(dev, chain_mode_buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*chain_mode_buf = buf;

	/*
	 * Have to limit n_chain_dev due to current implementation of
	 * available_scan_masks.
	 */
	if (n_chain_dev > BITS_PER_LONG)
		return dev_err_probe(dev, -EINVAL,
				     "chain is limited to 32 devices\n");

	scan_masks = devm_kcalloc(dev, 2, sizeof(*scan_masks), GFP_KERNEL);
	if (!scan_masks)
		return -ENOMEM;

	/*
	 * Scan mask is needed since we always have to read all devices in the
	 * chain in one SPI transfer.
	 */
	scan_masks[0] = GENMASK(n_chain_dev - 1, 0);

	*chain_scan_masks = scan_masks;

	return 0;
}

static const char * const ad7944_power_supplies[] = {
	"avdd",	"dvdd",	"bvdd", "vio"
};

static const struct spi_offload_config ad7944_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static int ad7944_probe(struct spi_device *spi)
{
	const struct ad7944_chip_info *chip_info;
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad7944_adc *adc;
	bool have_refin;
	struct iio_chan_spec *chain_chan;
	unsigned long *chain_scan_masks;
	u32 n_chain_dev;
	int ret, ref_mv;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	chip_info = spi_get_device_match_data(spi);
	if (!chip_info)
		return dev_err_probe(dev, -EINVAL, "no chip info\n");

	adc->timing_spec = chip_info->timing_spec;

	adc->sample_freq_range[0] = 1; /* min */
	adc->sample_freq_range[1] = 1; /* step */
	adc->sample_freq_range[2] = chip_info->max_sample_rate_hz; /* max */

	ret = device_property_match_property_string(dev, "adi,spi-mode",
						    ad7944_spi_modes,
						    ARRAY_SIZE(ad7944_spi_modes));
	/* absence of adi,spi-mode property means default mode */
	if (ret == -EINVAL)
		adc->spi_mode = AD7944_SPI_MODE_DEFAULT;
	else if (ret < 0)
		return dev_err_probe(dev, ret,
				     "getting adi,spi-mode property failed\n");
	else
		adc->spi_mode = ret;

	/*
	 * Some chips use unusual word sizes, so check now instead of waiting
	 * for the first xfer.
	 */
	if (!spi_is_bpw_supported(spi, chip_info->channels[0].scan_type.realbits))
		return dev_err_probe(dev, -EINVAL,
				"SPI host does not support %d bits per word\n",
				chip_info->channels[0].scan_type.realbits);

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(ad7944_power_supplies),
					     ad7944_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get and enable supplies\n");

	/*
	 * Sort out what is being used for the reference voltage. Options are:
	 * - internal reference: neither REF or REFIN is connected
	 * - internal reference with external buffer: REF not connected, REFIN
	 *   is connected
	 * - external reference: REF is connected, REFIN is not connected
	 */

	ret = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get REF voltage\n");

	ref_mv = ret == -ENODEV ? 0 : ret / 1000;

	ret = devm_regulator_get_enable_optional(dev, "refin");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get REFIN voltage\n");

	have_refin = ret != -ENODEV;

	if (have_refin && ref_mv)
		return dev_err_probe(dev, -EINVAL,
				     "cannot have both refin and ref supplies\n");

	adc->ref_mv = ref_mv ?: AD7944_INTERNAL_REF_MV;

	adc->cnv = devm_gpiod_get_optional(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(adc->cnv))
		return dev_err_probe(dev, PTR_ERR(adc->cnv),
				     "failed to get CNV GPIO\n");

	if (!adc->cnv && adc->spi_mode == AD7944_SPI_MODE_DEFAULT)
		return dev_err_probe(&spi->dev, -EINVAL, "CNV GPIO is required\n");
	if (adc->cnv && adc->spi_mode != AD7944_SPI_MODE_DEFAULT)
		return dev_err_probe(&spi->dev, -EINVAL,
				     "CNV GPIO in single and chain mode is not currently supported\n");

	adc->turbo = devm_gpiod_get_optional(dev, "turbo", GPIOD_OUT_LOW);
	if (IS_ERR(adc->turbo))
		return dev_err_probe(dev, PTR_ERR(adc->turbo),
				     "failed to get TURBO GPIO\n");

	adc->always_turbo = device_property_present(dev, "adi,always-turbo");

	if (adc->turbo && adc->always_turbo)
		return dev_err_probe(dev, -EINVAL,
			"cannot have both turbo-gpios and adi,always-turbo\n");

	if (adc->spi_mode == AD7944_SPI_MODE_CHAIN && adc->always_turbo)
		return dev_err_probe(dev, -EINVAL,
			"cannot have both chain mode and always turbo\n");

	switch (adc->spi_mode) {
	case AD7944_SPI_MODE_DEFAULT:
		ret = ad7944_4wire_mode_init_msg(dev, adc, &chip_info->channels[0]);
		if (ret)
			return ret;

		break;
	case AD7944_SPI_MODE_SINGLE:
		ret = ad7944_3wire_cs_mode_init_msg(dev, adc, &chip_info->channels[0]);
		if (ret)
			return ret;

		break;
	case AD7944_SPI_MODE_CHAIN:
		ret = device_property_read_u32(dev, "#daisy-chained-devices",
					       &n_chain_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					"failed to get #daisy-chained-devices\n");

		ret = ad7944_chain_mode_alloc(dev, chip_info->channels,
					      n_chain_dev, &chain_chan,
					      &adc->chain_mode_buf,
					      &chain_scan_masks);
		if (ret)
			return ret;

		ret = ad7944_chain_mode_init_msg(dev, adc, &chain_chan[0],
						 n_chain_dev);
		if (ret)
			return ret;

		break;
	}

	indio_dev->name = chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad7944_iio_info;

	adc->offload = devm_spi_offload_get(dev, spi, &ad7944_offload_config);
	ret = PTR_ERR_OR_ZERO(adc->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get offload\n");

	/* Fall back to low speed usage when no SPI offload available. */
	if (ret == -ENODEV) {
		if (adc->spi_mode == AD7944_SPI_MODE_CHAIN) {
			indio_dev->available_scan_masks = chain_scan_masks;
			indio_dev->channels = chain_chan;
			indio_dev->num_channels = n_chain_dev + 1;
		} else {
			indio_dev->channels = chip_info->channels;
			indio_dev->num_channels = ARRAY_SIZE(chip_info->channels);
		}

		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      iio_pollfunc_store_time,
						      ad7944_trigger_handler,
						      NULL);
		if (ret)
			return ret;
	} else {
		struct dma_chan *rx_dma;

		if (adc->spi_mode != AD7944_SPI_MODE_SINGLE)
			return dev_err_probe(dev, -EINVAL,
				"offload only supported in single mode\n");

		indio_dev->setup_ops = &ad7944_offload_buffer_setup_ops;
		indio_dev->channels = chip_info->offload_channels;
		indio_dev->num_channels = ARRAY_SIZE(chip_info->offload_channels);

		adc->offload_trigger = devm_spi_offload_trigger_get(dev,
			adc->offload, SPI_OFFLOAD_TRIGGER_PERIODIC);
		if (IS_ERR(adc->offload_trigger))
			return dev_err_probe(dev, PTR_ERR(adc->offload_trigger),
					     "failed to get offload trigger\n");

		ret = ad7944_set_sample_freq(adc, 2 * MEGA);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to init sample rate\n");

		rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev,
								     adc->offload);
		if (IS_ERR(rx_dma))
			return dev_err_probe(dev, PTR_ERR(rx_dma),
					     "failed to get offload RX DMA\n");

		/*
		 * REVISIT: ideally, we would confirm that the offload RX DMA
		 * buffer layout is the same as what is hard-coded in
		 * offload_channels. Right now, the only supported offload
		 * is the pulsar_adc project which always uses 32-bit word
		 * size for data values, regardless of the SPI bits per word.
		 */

		ret = devm_iio_dmaengine_buffer_setup_with_handle(dev,
			indio_dev, rx_dma, IIO_BUFFER_DIRECTION_IN);
		if (ret)
			return ret;

		ret = ad7944_3wire_cs_mode_init_offload_msg(dev, adc,
			&chip_info->offload_channels[0]);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad7944_of_match[] = {
	{ .compatible = "adi,ad7944", .data = &ad7944_chip_info },
	{ .compatible = "adi,ad7985", .data = &ad7985_chip_info },
	{ .compatible = "adi,ad7986", .data = &ad7986_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7944_of_match);

static const struct spi_device_id ad7944_spi_id[] = {
	{ "ad7944", (kernel_ulong_t)&ad7944_chip_info },
	{ "ad7985", (kernel_ulong_t)&ad7985_chip_info },
	{ "ad7986", (kernel_ulong_t)&ad7986_chip_info },
	{ }

};
MODULE_DEVICE_TABLE(spi, ad7944_spi_id);

static struct spi_driver ad7944_driver = {
	.driver = {
		.name = "ad7944",
		.of_match_table = ad7944_of_match,
	},
	.probe = ad7944_probe,
	.id_table = ad7944_spi_id,
};
module_spi_driver(ad7944_driver);

MODULE_AUTHOR("David Lechner <dlechner@baylibre.com>");
MODULE_DESCRIPTION("Analog Devices AD7944 PulSAR ADC family driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
