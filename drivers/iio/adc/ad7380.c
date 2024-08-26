// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD738x Simultaneous Sampling SAR ADCs
 *
 * Copyright 2017 Analog Devices Inc.
 * Copyright 2024 BayLibre, SAS
 *
 * Datasheets of supported parts:
 * ad7380/1 : https://www.analog.com/media/en/technical-documentation/data-sheets/AD7380-7381.pdf
 * ad7383/4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7383-7384.pdf
 * ad7380-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7380-4.pdf
 * ad7381-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7381-4.pdf
 * ad7383/4-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7383-4-ad7384-4.pdf
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define MAX_NUM_CHANNELS		4
/* 2.5V internal reference voltage */
#define AD7380_INTERNAL_REF_MV		2500

/* reading and writing registers is more reliable at lower than max speed */
#define AD7380_REG_WR_SPEED_HZ		10000000

#define AD7380_REG_WR			BIT(15)
#define AD7380_REG_REGADDR		GENMASK(14, 12)
#define AD7380_REG_DATA			GENMASK(11, 0)

#define AD7380_REG_ADDR_NOP		0x0
#define AD7380_REG_ADDR_CONFIG1		0x1
#define AD7380_REG_ADDR_CONFIG2		0x2
#define AD7380_REG_ADDR_ALERT		0x3
#define AD7380_REG_ADDR_ALERT_LOW_TH	0x4
#define AD7380_REG_ADDR_ALERT_HIGH_TH	0x5

#define AD7380_CONFIG1_OS_MODE		BIT(9)
#define AD7380_CONFIG1_OSR		GENMASK(8, 6)
#define AD7380_CONFIG1_CRC_W		BIT(5)
#define AD7380_CONFIG1_CRC_R		BIT(4)
#define AD7380_CONFIG1_ALERTEN		BIT(3)
#define AD7380_CONFIG1_RES		BIT(2)
#define AD7380_CONFIG1_REFSEL		BIT(1)
#define AD7380_CONFIG1_PMODE		BIT(0)

#define AD7380_CONFIG2_SDO2		GENMASK(9, 8)
#define AD7380_CONFIG2_SDO		BIT(8)
#define AD7380_CONFIG2_RESET		GENMASK(7, 0)

#define AD7380_CONFIG2_RESET_SOFT	0x3C
#define AD7380_CONFIG2_RESET_HARD	0xFF

#define AD7380_ALERT_LOW_TH		GENMASK(11, 0)
#define AD7380_ALERT_HIGH_TH		GENMASK(11, 0)

#define T_CONVERT_NS 190		/* conversion time */
#define T_CONVERT_0_NS 10		/* 1st conversion start time (oversampling) */
#define T_CONVERT_X_NS 500		/* xth conversion start time (oversampling) */

struct ad7380_timing_specs {
	const unsigned int t_csh_ns;	/* CS minimum high time */
};

struct ad7380_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	const char * const *vcm_supplies;
	unsigned int num_vcm_supplies;
	const unsigned long *available_scan_masks;
	const struct ad7380_timing_specs *timing_specs;
};

enum {
	AD7380_SCAN_TYPE_NORMAL,
	AD7380_SCAN_TYPE_RESOLUTION_BOOST,
};

/* Extended scan types for 14-bit chips. */
static const struct iio_scan_type ad7380_scan_type_14[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 14,
		.storagebits = 16,
		.endianness = IIO_CPU
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU
	},
};

/* Extended scan types for 16-bit chips. */
static const struct iio_scan_type ad7380_scan_type_16[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 18,
		.storagebits = 32,
		.endianness = IIO_CPU
	},
};

#define AD7380_CHANNEL(index, bits, diff) {			\
	.type = IIO_VOLTAGE,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
		((diff) ? 0 : BIT(IIO_CHAN_INFO_OFFSET)),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),		\
	.info_mask_shared_by_type_available =			\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),		\
	.indexed = 1,						\
	.differential = (diff),					\
	.channel = (diff) ? (2 * (index)) : (index),		\
	.channel2 = (diff) ? (2 * (index) + 1) : 0,		\
	.scan_index = (index),					\
	.has_ext_scan_type = 1,					\
	.ext_scan_type = ad7380_scan_type_##bits,		\
	.num_ext_scan_type = ARRAY_SIZE(ad7380_scan_type_##bits),\
}

#define DEFINE_AD7380_2_CHANNEL(name, bits, diff)	\
static const struct iio_chan_spec name[] = {		\
	AD7380_CHANNEL(0, bits, diff),			\
	AD7380_CHANNEL(1, bits, diff),			\
	IIO_CHAN_SOFT_TIMESTAMP(2),			\
}

#define DEFINE_AD7380_4_CHANNEL(name, bits, diff)	\
static const struct iio_chan_spec name[] = {		\
	AD7380_CHANNEL(0, bits, diff),			\
	AD7380_CHANNEL(1, bits, diff),			\
	AD7380_CHANNEL(2, bits, diff),			\
	AD7380_CHANNEL(3, bits, diff),			\
	IIO_CHAN_SOFT_TIMESTAMP(4),			\
}

/* fully differential */
DEFINE_AD7380_2_CHANNEL(ad7380_channels, 16, 1);
DEFINE_AD7380_2_CHANNEL(ad7381_channels, 14, 1);
DEFINE_AD7380_4_CHANNEL(ad7380_4_channels, 16, 1);
DEFINE_AD7380_4_CHANNEL(ad7381_4_channels, 14, 1);
/* pseudo differential */
DEFINE_AD7380_2_CHANNEL(ad7383_channels, 16, 0);
DEFINE_AD7380_2_CHANNEL(ad7384_channels, 14, 0);
DEFINE_AD7380_4_CHANNEL(ad7383_4_channels, 16, 0);
DEFINE_AD7380_4_CHANNEL(ad7384_4_channels, 14, 0);

static const char * const ad7380_2_channel_vcm_supplies[] = {
	"aina", "ainb",
};

static const char * const ad7380_4_channel_vcm_supplies[] = {
	"aina", "ainb", "ainc", "aind",
};

/* Since this is simultaneous sampling, we don't allow individual channels. */
static const unsigned long ad7380_2_channel_scan_masks[] = {
	GENMASK(1, 0),
	0
};

static const unsigned long ad7380_4_channel_scan_masks[] = {
	GENMASK(3, 0),
	0
};

static const struct ad7380_timing_specs ad7380_timing = {
	.t_csh_ns = 10,
};

static const struct ad7380_timing_specs ad7380_4_timing = {
	.t_csh_ns = 20,
};

/*
 * Available oversampling ratios. The indices correspond with the bit value
 * expected by the chip.  The available ratios depend on the averaging mode,
 * only normal averaging is supported for now.
 */
static const int ad7380_oversampling_ratios[] = {
	1, 2, 4, 8, 16, 32,
};

static const struct ad7380_chip_info ad7380_chip_info = {
	.name = "ad7380",
	.channels = ad7380_channels,
	.num_channels = ARRAY_SIZE(ad7380_channels),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
};

static const struct ad7380_chip_info ad7381_chip_info = {
	.name = "ad7381",
	.channels = ad7381_channels,
	.num_channels = ARRAY_SIZE(ad7381_channels),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
};

static const struct ad7380_chip_info ad7383_chip_info = {
	.name = "ad7383",
	.channels = ad7383_channels,
	.num_channels = ARRAY_SIZE(ad7383_channels),
	.vcm_supplies = ad7380_2_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_2_channel_vcm_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
};

static const struct ad7380_chip_info ad7384_chip_info = {
	.name = "ad7384",
	.channels = ad7384_channels,
	.num_channels = ARRAY_SIZE(ad7384_channels),
	.vcm_supplies = ad7380_2_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_2_channel_vcm_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
};

static const struct ad7380_chip_info ad7380_4_chip_info = {
	.name = "ad7380-4",
	.channels = ad7380_4_channels,
	.num_channels = ARRAY_SIZE(ad7380_4_channels),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
};

static const struct ad7380_chip_info ad7381_4_chip_info = {
	.name = "ad7381-4",
	.channels = ad7381_4_channels,
	.num_channels = ARRAY_SIZE(ad7381_4_channels),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
};

static const struct ad7380_chip_info ad7383_4_chip_info = {
	.name = "ad7383-4",
	.channels = ad7383_4_channels,
	.num_channels = ARRAY_SIZE(ad7383_4_channels),
	.vcm_supplies = ad7380_4_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_4_channel_vcm_supplies),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
};

static const struct ad7380_chip_info ad7384_4_chip_info = {
	.name = "ad7384-4",
	.channels = ad7384_4_channels,
	.num_channels = ARRAY_SIZE(ad7384_4_channels),
	.vcm_supplies = ad7380_4_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_4_channel_vcm_supplies),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
};

struct ad7380_state {
	const struct ad7380_chip_info *chip_info;
	struct spi_device *spi;
	struct regmap *regmap;
	unsigned int oversampling_ratio;
	bool resolution_boost_enabled;
	unsigned int vref_mv;
	unsigned int vcm_mv[MAX_NUM_CHANNELS];
	/* xfers, message an buffer for reading sample data */
	struct spi_transfer xfer[2];
	struct spi_message msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the transfer buffers
	 * to live in their own cache lines.
	 *
	 * Make the buffer large enough for MAX_NUM_CHANNELS 32-bit samples and
	 * one 64-bit aligned 64-bit timestamp.
	 */
	u8 scan_data[ALIGN(MAX_NUM_CHANNELS * sizeof(u32), sizeof(s64))
			   + sizeof(s64)] __aligned(IIO_DMA_MINALIGN);
	/* buffers for reading/writing registers */
	u16 tx;
	u16 rx;
};

static int ad7380_regmap_reg_write(void *context, unsigned int reg,
				   unsigned int val)
{
	struct ad7380_state *st = context;
	struct spi_transfer xfer = {
		.speed_hz = AD7380_REG_WR_SPEED_HZ,
		.bits_per_word = 16,
		.len = 2,
		.tx_buf = &st->tx,
	};

	st->tx = FIELD_PREP(AD7380_REG_WR, 1) |
		 FIELD_PREP(AD7380_REG_REGADDR, reg) |
		 FIELD_PREP(AD7380_REG_DATA, val);

	return spi_sync_transfer(st->spi, &xfer, 1);
}

static int ad7380_regmap_reg_read(void *context, unsigned int reg,
				  unsigned int *val)
{
	struct ad7380_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.speed_hz = AD7380_REG_WR_SPEED_HZ,
			.bits_per_word = 16,
			.len = 2,
			.tx_buf = &st->tx,
			.cs_change = 1,
			.cs_change_delay = {
				.value = st->chip_info->timing_specs->t_csh_ns,
				.unit = SPI_DELAY_UNIT_NSECS,
			},
		}, {
			.speed_hz = AD7380_REG_WR_SPEED_HZ,
			.bits_per_word = 16,
			.len = 2,
			.rx_buf = &st->rx,
		},
	};
	int ret;

	st->tx = FIELD_PREP(AD7380_REG_WR, 0) |
		 FIELD_PREP(AD7380_REG_REGADDR, reg) |
		 FIELD_PREP(AD7380_REG_DATA, 0);

	ret = spi_sync_transfer(st->spi, xfers, ARRAY_SIZE(xfers));
	if (ret < 0)
		return ret;

	*val = FIELD_GET(AD7380_REG_DATA, st->rx);

	return 0;
}

static const struct regmap_config ad7380_regmap_config = {
	.reg_bits = 3,
	.val_bits = 12,
	.reg_read = ad7380_regmap_reg_read,
	.reg_write = ad7380_regmap_reg_write,
	.max_register = AD7380_REG_ADDR_ALERT_HIGH_TH,
	.can_sleep = true,
};

static int ad7380_debugfs_reg_access(struct iio_dev *indio_dev, u32 reg,
				     u32 writeval, u32 *readval)
{
	iio_device_claim_direct_scoped(return  -EBUSY, indio_dev) {
		struct ad7380_state *st = iio_priv(indio_dev);

		if (readval)
			return regmap_read(st->regmap, reg, readval);
		else
			return regmap_write(st->regmap, reg, writeval);
	}
	unreachable();
}

/**
 * ad7380_update_xfers - update the SPI transfers base on the current scan type
 * @st:		device instance specific state
 * @scan_type:	current scan type
 */
static void ad7380_update_xfers(struct ad7380_state *st,
				const struct iio_scan_type *scan_type)
{
	/*
	 * First xfer only triggers conversion and has to be long enough for
	 * all conversions to complete, which can be multiple conversion in the
	 * case of oversampling. Technically T_CONVERT_X_NS is lower for some
	 * chips, but we use the maximum value for simplicity for now.
	 */
	if (st->oversampling_ratio > 1)
		st->xfer[0].delay.value = T_CONVERT_0_NS + T_CONVERT_X_NS *
						(st->oversampling_ratio - 1);
	else
		st->xfer[0].delay.value = T_CONVERT_NS;

	st->xfer[0].delay.unit = SPI_DELAY_UNIT_NSECS;

	/*
	 * Second xfer reads all channels. Data size depends on if resolution
	 * boost is enabled or not.
	 */
	st->xfer[1].bits_per_word = scan_type->realbits;
	st->xfer[1].len = BITS_TO_BYTES(scan_type->storagebits) *
			  (st->chip_info->num_channels - 1);
}

static int ad7380_triggered_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;

	/*
	 * Currently, we always read all channels at the same time. The scan_type
	 * is the same for all channels, so we just pass the first channel.
	 */
	scan_type = iio_get_current_scan_type(indio_dev, &indio_dev->channels[0]);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	ad7380_update_xfers(st, scan_type);

	return spi_optimize_message(st->spi, &st->msg);
}

static int ad7380_triggered_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);

	spi_unoptimize_message(&st->msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad7380_buffer_setup_ops = {
	.preenable = ad7380_triggered_buffer_preenable,
	.postdisable = ad7380_triggered_buffer_postdisable,
};

static irqreturn_t ad7380_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_sync(st->spi, &st->msg);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, &st->scan_data,
					   pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ad7380_read_direct(struct ad7380_state *st, unsigned int scan_index,
			      const struct iio_scan_type *scan_type, int *val)
{
	int ret;

	ad7380_update_xfers(st, scan_type);

	ret = spi_sync(st->spi, &st->msg);
	if (ret < 0)
		return ret;

	if (scan_type->storagebits > 16)
		*val = sign_extend32(*(u32 *)(st->scan_data + 4 * scan_index),
				     scan_type->realbits - 1);
	else
		*val = sign_extend32(*(u16 *)(st->scan_data + 2 * scan_index),
				     scan_type->realbits - 1);

	return IIO_VAL_INT;
}

static int ad7380_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;

	scan_type = iio_get_current_scan_type(indio_dev, chan);

	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		iio_device_claim_direct_scoped(return -EBUSY, indio_dev) {
			return ad7380_read_direct(st, chan->scan_index,
						  scan_type, val);
		}
		unreachable();
	case IIO_CHAN_INFO_SCALE:
		/*
		 * According to the datasheet, the LSB size is:
		 *    * (2 × VREF) / 2^N, for differential chips
		 *    * VREF / 2^N, for pseudo-differential chips
		 * where N is the ADC resolution (i.e realbits)
		 */
		*val = st->vref_mv;
		*val2 = scan_type->realbits - chan->differential;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * According to IIO ABI, offset is applied before scale,
		 * so offset is: vcm_mv / scale
		 */
		*val = st->vcm_mv[chan->channel] * (1 << scan_type->realbits)
			/ st->vref_mv;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = st->oversampling_ratio;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7380_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad7380_oversampling_ratios;
		*length = ARRAY_SIZE(ad7380_oversampling_ratios);
		*type = IIO_VAL_INT;

		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

/**
 * ad7380_osr_to_regval - convert ratio to OSR register value
 * @ratio: ratio to check
 *
 * Check if ratio is present in the list of available ratios and return the
 * corresponding value that needs to be written to the register to select that
 * ratio.
 *
 * Returns: register value (0 to 7) or -EINVAL if there is not an exact match
 */
static int ad7380_osr_to_regval(int ratio)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ad7380_oversampling_ratios); i++) {
		if (ratio == ad7380_oversampling_ratios[i])
			return i;
	}

	return -EINVAL;
}

static int ad7380_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret, osr, boost;

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		osr = ad7380_osr_to_regval(val);
		if (osr < 0)
			return osr;

		/* always enable resolution boost when oversampling is enabled */
		boost = osr > 0 ? 1 : 0;

		iio_device_claim_direct_scoped(return -EBUSY, indio_dev) {
			ret = regmap_update_bits(st->regmap,
					AD7380_REG_ADDR_CONFIG1,
					AD7380_CONFIG1_OSR | AD7380_CONFIG1_RES,
					FIELD_PREP(AD7380_CONFIG1_OSR, osr) |
					FIELD_PREP(AD7380_CONFIG1_RES, boost));

			if (ret)
				return ret;

			st->oversampling_ratio = val;
			st->resolution_boost_enabled = boost;

			/*
			 * Perform a soft reset. This will flush the oversampling
			 * block and FIFO but will maintain the content of the
			 * configurable registers.
			 */
			return regmap_update_bits(st->regmap,
					AD7380_REG_ADDR_CONFIG2,
					AD7380_CONFIG2_RESET,
					FIELD_PREP(AD7380_CONFIG2_RESET,
						   AD7380_CONFIG2_RESET_SOFT));
		}
		unreachable();
	default:
		return -EINVAL;
	}
}

static int ad7380_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad7380_state *st = iio_priv(indio_dev);

	return st->resolution_boost_enabled ? AD7380_SCAN_TYPE_RESOLUTION_BOOST
					    : AD7380_SCAN_TYPE_NORMAL;
}

static const struct iio_info ad7380_info = {
	.read_raw = &ad7380_read_raw,
	.read_avail = &ad7380_read_avail,
	.write_raw = &ad7380_write_raw,
	.get_current_scan_type = &ad7380_get_current_scan_type,
	.debugfs_reg_access = &ad7380_debugfs_reg_access,
};

static int ad7380_init(struct ad7380_state *st, struct regulator *vref)
{
	int ret;

	/* perform hard reset */
	ret = regmap_update_bits(st->regmap, AD7380_REG_ADDR_CONFIG2,
				 AD7380_CONFIG2_RESET,
				 FIELD_PREP(AD7380_CONFIG2_RESET,
					    AD7380_CONFIG2_RESET_HARD));
	if (ret < 0)
		return ret;

	/* select internal or external reference voltage */
	ret = regmap_update_bits(st->regmap, AD7380_REG_ADDR_CONFIG1,
				 AD7380_CONFIG1_REFSEL,
				 FIELD_PREP(AD7380_CONFIG1_REFSEL,
					    vref ? 1 : 0));
	if (ret < 0)
		return ret;

	/* This is the default value after reset. */
	st->oversampling_ratio = 1;

	/* SPI 1-wire mode */
	return regmap_update_bits(st->regmap, AD7380_REG_ADDR_CONFIG2,
				  AD7380_CONFIG2_SDO,
				  FIELD_PREP(AD7380_CONFIG2_SDO, 1));
}

static void ad7380_regulator_disable(void *p)
{
	regulator_disable(p);
}

static int ad7380_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad7380_state *st;
	struct regulator *vref;
	int ret, i;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return dev_err_probe(&spi->dev, -EINVAL, "missing match data\n");

	vref = devm_regulator_get_optional(&spi->dev, "refio");
	if (IS_ERR(vref)) {
		if (PTR_ERR(vref) != -ENODEV)
			return dev_err_probe(&spi->dev, PTR_ERR(vref),
					     "Failed to get refio regulator\n");

		vref = NULL;
	}

	/*
	 * If there is no REFIO supply, then it means that we are using
	 * the internal 2.5V reference, otherwise REFIO is reference voltage.
	 */
	if (vref) {
		ret = regulator_enable(vref);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev,
					       ad7380_regulator_disable, vref);
		if (ret)
			return ret;

		ret = regulator_get_voltage(vref);
		if (ret < 0)
			return ret;

		st->vref_mv = ret / 1000;
	} else {
		st->vref_mv = AD7380_INTERNAL_REF_MV;
	}

	if (st->chip_info->num_vcm_supplies > ARRAY_SIZE(st->vcm_mv))
		return dev_err_probe(&spi->dev, -EINVAL,
				     "invalid number of VCM supplies\n");

	/*
	 * pseudo-differential chips have common mode supplies for the negative
	 * input pin.
	 */
	for (i = 0; i < st->chip_info->num_vcm_supplies; i++) {
		struct regulator *vcm;

		vcm = devm_regulator_get(&spi->dev,
					 st->chip_info->vcm_supplies[i]);
		if (IS_ERR(vcm))
			return dev_err_probe(&spi->dev, PTR_ERR(vcm),
					     "Failed to get %s regulator\n",
					     st->chip_info->vcm_supplies[i]);

		ret = regulator_enable(vcm);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(&spi->dev,
					       ad7380_regulator_disable, vcm);
		if (ret)
			return ret;

		ret = regulator_get_voltage(vcm);
		if (ret < 0)
			return ret;

		st->vcm_mv[i] = ret / 1000;
	}

	st->regmap = devm_regmap_init(&spi->dev, NULL, st, &ad7380_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(st->regmap),
				     "failed to allocate register map\n");

	/*
	 * Setting up a low latency read for getting sample data. Used for both
	 * direct read an triggered buffer. Additional fields will be set up in
	 * ad7380_update_xfers() based on the current state of the driver at the
	 * time of the read.
	 */

	/* toggle CS (no data xfer) to trigger a conversion */
	st->xfer[0].cs_change = 1;
	st->xfer[0].cs_change_delay.value = st->chip_info->timing_specs->t_csh_ns;
	st->xfer[0].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	/* then do a second xfer to read the data */
	st->xfer[1].rx_buf = st->scan_data;

	spi_message_init_with_transfers(&st->msg, st->xfer, ARRAY_SIZE(st->xfer));

	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad7380_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = st->chip_info->available_scan_masks;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					      iio_pollfunc_store_time,
					      ad7380_trigger_handler,
					      &ad7380_buffer_setup_ops);
	if (ret)
		return ret;

	ret = ad7380_init(st, vref);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad7380_of_match_table[] = {
	{ .compatible = "adi,ad7380", .data = &ad7380_chip_info },
	{ .compatible = "adi,ad7381", .data = &ad7381_chip_info },
	{ .compatible = "adi,ad7383", .data = &ad7383_chip_info },
	{ .compatible = "adi,ad7384", .data = &ad7384_chip_info },
	{ .compatible = "adi,ad7380-4", .data = &ad7380_4_chip_info },
	{ .compatible = "adi,ad7381-4", .data = &ad7381_4_chip_info },
	{ .compatible = "adi,ad7383-4", .data = &ad7383_4_chip_info },
	{ .compatible = "adi,ad7384-4", .data = &ad7384_4_chip_info },
	{ }
};

static const struct spi_device_id ad7380_id_table[] = {
	{ "ad7380", (kernel_ulong_t)&ad7380_chip_info },
	{ "ad7381", (kernel_ulong_t)&ad7381_chip_info },
	{ "ad7383", (kernel_ulong_t)&ad7383_chip_info },
	{ "ad7384", (kernel_ulong_t)&ad7384_chip_info },
	{ "ad7380-4", (kernel_ulong_t)&ad7380_4_chip_info },
	{ "ad7381-4", (kernel_ulong_t)&ad7381_4_chip_info },
	{ "ad7383-4", (kernel_ulong_t)&ad7383_4_chip_info },
	{ "ad7384-4", (kernel_ulong_t)&ad7384_4_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7380_id_table);

static struct spi_driver ad7380_driver = {
	.driver = {
		.name = "ad7380",
		.of_match_table = ad7380_of_match_table,
	},
	.probe = ad7380_probe,
	.id_table = ad7380_id_table,
};
module_spi_driver(ad7380_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD738x ADC driver");
MODULE_LICENSE("GPL");
