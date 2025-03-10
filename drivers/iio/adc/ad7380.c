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
 * ad7386/7/8 : https://www.analog.com/media/en/technical-documentation/data-sheets/AD7386-7387-7388.pdf
 * ad7380-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7380-4.pdf
 * ad7381-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7381-4.pdf
 * ad7383/4-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7383-4-ad7384-4.pdf
 * ad7386/7/8-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/ad7386-4-7387-4-7388-4.pdf
 * adaq4370-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/adaq4370-4.pdf
 * adaq4380-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/adaq4380-4.pdf
 * adaq4381-4 : https://www.analog.com/media/en/technical-documentation/data-sheets/adaq4381-4.pdf
 *
 * HDL ad738x_fmc: https://analogdevicesinc.github.io/hdl/projects/ad738x_fmc/index.html
 *
 */

#include <linux/align.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>
#include <linux/util_macros.h>

#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define MAX_NUM_CHANNELS		8
/* 2.5V internal reference voltage */
#define AD7380_INTERNAL_REF_MV		2500
/* 3.3V internal reference voltage for ADAQ */
#define ADAQ4380_INTERNAL_REF_MV	3300

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

#define AD7380_CONFIG1_CH		BIT(11)
#define AD7380_CONFIG1_SEQ		BIT(10)
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
#define T_POWERUP_US 5000		/* Power up */

/*
 * AD738x support several SDO lines to increase throughput, but driver currently
 * supports only 1 SDO line (standard SPI transaction)
 */
#define AD7380_NUM_SDO_LINES		1
#define AD7380_DEFAULT_GAIN_MILLI	1000

/*
 * Using SPI offload, storagebits is always 32, so can't be used to compute struct
 * spi_transfer.len. Using realbits instead.
 */
#define AD7380_SPI_BYTES(scan_type)	((scan_type)->realbits > 16 ? 4 : 2)

struct ad7380_timing_specs {
	const unsigned int t_csh_ns;	/* CS minimum high time */
};

struct ad7380_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	const struct iio_chan_spec *offload_channels;
	unsigned int num_channels;
	unsigned int num_simult_channels;
	bool has_hardware_gain;
	bool has_mux;
	const char * const *supplies;
	unsigned int num_supplies;
	bool external_ref_only;
	bool adaq_internal_ref_only;
	const char * const *vcm_supplies;
	unsigned int num_vcm_supplies;
	const unsigned long *available_scan_masks;
	const struct ad7380_timing_specs *timing_specs;
	u32 max_conversion_rate_hz;
};

static const struct iio_event_spec ad7380_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_dir = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_dir = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_shared_by_all = BIT(IIO_EV_INFO_ENABLE),
	},
};

enum {
	AD7380_SCAN_TYPE_NORMAL,
	AD7380_SCAN_TYPE_RESOLUTION_BOOST,
};

/* Extended scan types for 12-bit unsigned chips. */
static const struct iio_scan_type ad7380_scan_type_12_u[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 12,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 14,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 14-bit signed chips. */
static const struct iio_scan_type ad7380_scan_type_14_s[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 14,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 14-bit unsigned chips. */
static const struct iio_scan_type ad7380_scan_type_14_u[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 14,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 16-bit signed_chips. */
static const struct iio_scan_type ad7380_scan_type_16_s[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 18,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 16-bit unsigned chips. */
static const struct iio_scan_type ad7380_scan_type_16_u[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 18,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/*
 * Defining here scan types for offload mode, since with current available HDL
 * only a value of 32 for storagebits is supported.
 */

/* Extended scan types for 12-bit unsigned chips, offload support. */
static const struct iio_scan_type ad7380_scan_type_12_u_offload[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 12,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 14,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 14-bit signed chips, offload support. */
static const struct iio_scan_type ad7380_scan_type_14_s_offload[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 14,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 14-bit unsigned chips, offload support. */
static const struct iio_scan_type ad7380_scan_type_14_u_offload[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 14,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 16-bit signed_chips, offload support. */
static const struct iio_scan_type ad7380_scan_type_16_s_offload[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 's',
		.realbits = 18,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

/* Extended scan types for 16-bit unsigned chips, offload support. */
static const struct iio_scan_type ad7380_scan_type_16_u_offload[] = {
	[AD7380_SCAN_TYPE_NORMAL] = {
		.sign = 'u',
		.realbits = 16,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
	[AD7380_SCAN_TYPE_RESOLUTION_BOOST] = {
		.sign = 'u',
		.realbits = 18,
		.storagebits = 32,
		.endianness = IIO_CPU,
	},
};

#define _AD7380_CHANNEL(index, bits, diff, sign, gain) {			\
	.type = IIO_VOLTAGE,							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
		((gain) ? BIT(IIO_CHAN_INFO_SCALE) : 0) |			\
		((diff) ? 0 : BIT(IIO_CHAN_INFO_OFFSET)),			\
	.info_mask_shared_by_type = ((gain) ? 0 : BIT(IIO_CHAN_INFO_SCALE)) |	\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),				\
	.info_mask_shared_by_type_available =					\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),				\
	.indexed = 1,								\
	.differential = (diff),							\
	.channel = (diff) ? (2 * (index)) : (index),				\
	.channel2 = (diff) ? (2 * (index) + 1) : 0,				\
	.scan_index = (index),							\
	.has_ext_scan_type = 1,							\
	.ext_scan_type = ad7380_scan_type_##bits##_##sign,			\
	.num_ext_scan_type = ARRAY_SIZE(ad7380_scan_type_##bits##_##sign),	\
	.event_spec = ad7380_events,						\
	.num_event_specs = ARRAY_SIZE(ad7380_events),				\
}

#define _AD7380_OFFLOAD_CHANNEL(index, bits, diff, sign, gain) {		\
	.type = IIO_VOLTAGE,							\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |                          \
		((gain) ? BIT(IIO_CHAN_INFO_SCALE) : 0) |			\
		((diff) ? 0 : BIT(IIO_CHAN_INFO_OFFSET)),			\
	.info_mask_shared_by_type = ((gain) ? 0 : BIT(IIO_CHAN_INFO_SCALE)) |   \
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),					\
	.info_mask_shared_by_type_available =					\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO) |				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),					\
	.indexed = 1,                                                           \
	.differential = (diff),                                                 \
	.channel = (diff) ? (2 * (index)) : (index),                            \
	.channel2 = (diff) ? (2 * (index) + 1) : 0,                             \
	.scan_index = (index),                                                  \
	.has_ext_scan_type = 1,                                                 \
	.ext_scan_type = ad7380_scan_type_##bits##_##sign##_offload,            \
	.num_ext_scan_type =                                                    \
		ARRAY_SIZE(ad7380_scan_type_##bits##_##sign##_offload),		\
	.event_spec = ad7380_events,                                            \
	.num_event_specs = ARRAY_SIZE(ad7380_events),                           \
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

#define AD7380_CHANNEL(index, bits, diff, sign)		\
	_AD7380_CHANNEL(index, bits, diff, sign, false)

#define ADAQ4380_CHANNEL(index, bits, diff, sign)	\
	_AD7380_CHANNEL(index, bits, diff, sign, true)

#define DEFINE_AD7380_2_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {	\
	AD7380_CHANNEL(0, bits, diff, sign),	\
	AD7380_CHANNEL(1, bits, diff, sign),	\
	IIO_CHAN_SOFT_TIMESTAMP(2),		\
}

#define DEFINE_AD7380_4_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {	\
	 AD7380_CHANNEL(0, bits, diff, sign),	\
	 AD7380_CHANNEL(1, bits, diff, sign),	\
	 AD7380_CHANNEL(2, bits, diff, sign),	\
	 AD7380_CHANNEL(3, bits, diff, sign),	\
	 IIO_CHAN_SOFT_TIMESTAMP(4),		\
}

#define DEFINE_ADAQ4380_4_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {	\
	 ADAQ4380_CHANNEL(0, bits, diff, sign),	\
	 ADAQ4380_CHANNEL(1, bits, diff, sign),	\
	 ADAQ4380_CHANNEL(2, bits, diff, sign),	\
	 ADAQ4380_CHANNEL(3, bits, diff, sign),	\
	 IIO_CHAN_SOFT_TIMESTAMP(4),		\
}

#define DEFINE_AD7380_8_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {	\
	 AD7380_CHANNEL(0, bits, diff, sign),	\
	 AD7380_CHANNEL(1, bits, diff, sign),	\
	 AD7380_CHANNEL(2, bits, diff, sign),	\
	 AD7380_CHANNEL(3, bits, diff, sign),	\
	 AD7380_CHANNEL(4, bits, diff, sign),	\
	 AD7380_CHANNEL(5, bits, diff, sign),	\
	 AD7380_CHANNEL(6, bits, diff, sign),	\
	 AD7380_CHANNEL(7, bits, diff, sign),	\
	 IIO_CHAN_SOFT_TIMESTAMP(8),		\
}

#define AD7380_OFFLOAD_CHANNEL(index, bits, diff, sign) \
_AD7380_OFFLOAD_CHANNEL(index, bits, diff, sign, false)

#define ADAQ4380_OFFLOAD_CHANNEL(index, bits, diff, sign) \
_AD7380_OFFLOAD_CHANNEL(index, bits, diff, sign, true)

#define DEFINE_AD7380_2_OFFLOAD_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {		\
	AD7380_OFFLOAD_CHANNEL(0, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(1, bits, diff, sign),	\
}

#define DEFINE_AD7380_4_OFFLOAD_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {		\
	AD7380_OFFLOAD_CHANNEL(0, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(1, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(2, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(3, bits, diff, sign),	\
}

#define DEFINE_ADAQ4380_4_OFFLOAD_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {		\
	AD7380_OFFLOAD_CHANNEL(0, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(1, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(2, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(3, bits, diff, sign),	\
}

#define DEFINE_AD7380_8_OFFLOAD_CHANNEL(name, bits, diff, sign) \
static const struct iio_chan_spec name[] = {		\
	AD7380_OFFLOAD_CHANNEL(0, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(1, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(2, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(3, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(4, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(5, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(6, bits, diff, sign),	\
	AD7380_OFFLOAD_CHANNEL(7, bits, diff, sign),	\
}

/* fully differential */
DEFINE_AD7380_2_CHANNEL(ad7380_channels, 16, 1, s);
DEFINE_AD7380_2_CHANNEL(ad7381_channels, 14, 1, s);
DEFINE_AD7380_4_CHANNEL(ad7380_4_channels, 16, 1, s);
DEFINE_AD7380_4_CHANNEL(ad7381_4_channels, 14, 1, s);
DEFINE_ADAQ4380_4_CHANNEL(adaq4380_4_channels, 16, 1, s);
DEFINE_ADAQ4380_4_CHANNEL(adaq4381_4_channels, 14, 1, s);
/* pseudo differential */
DEFINE_AD7380_2_CHANNEL(ad7383_channels, 16, 0, s);
DEFINE_AD7380_2_CHANNEL(ad7384_channels, 14, 0, s);
DEFINE_AD7380_4_CHANNEL(ad7383_4_channels, 16, 0, s);
DEFINE_AD7380_4_CHANNEL(ad7384_4_channels, 14, 0, s);

/* Single ended */
DEFINE_AD7380_4_CHANNEL(ad7386_channels, 16, 0, u);
DEFINE_AD7380_4_CHANNEL(ad7387_channels, 14, 0, u);
DEFINE_AD7380_4_CHANNEL(ad7388_channels, 12, 0, u);
DEFINE_AD7380_8_CHANNEL(ad7386_4_channels, 16, 0, u);
DEFINE_AD7380_8_CHANNEL(ad7387_4_channels, 14, 0, u);
DEFINE_AD7380_8_CHANNEL(ad7388_4_channels, 12, 0, u);

/* offload channels */
DEFINE_AD7380_2_OFFLOAD_CHANNEL(ad7380_offload_channels, 16, 1, s);
DEFINE_AD7380_2_OFFLOAD_CHANNEL(ad7381_offload_channels, 14, 1, s);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7380_4_offload_channels, 16, 1, s);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7381_4_offload_channels, 14, 1, s);
DEFINE_ADAQ4380_4_OFFLOAD_CHANNEL(adaq4380_4_offload_channels, 16, 1, s);
DEFINE_ADAQ4380_4_OFFLOAD_CHANNEL(adaq4381_4_offload_channels, 14, 1, s);

/* pseudo differential */
DEFINE_AD7380_2_OFFLOAD_CHANNEL(ad7383_offload_channels, 16, 0, s);
DEFINE_AD7380_2_OFFLOAD_CHANNEL(ad7384_offload_channels, 14, 0, s);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7383_4_offload_channels, 16, 0, s);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7384_4_offload_channels, 14, 0, s);

/* Single ended */
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7386_offload_channels, 16, 0, u);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7387_offload_channels, 14, 0, u);
DEFINE_AD7380_4_OFFLOAD_CHANNEL(ad7388_offload_channels, 12, 0, u);
DEFINE_AD7380_8_OFFLOAD_CHANNEL(ad7386_4_offload_channels, 16, 0, u);
DEFINE_AD7380_8_OFFLOAD_CHANNEL(ad7387_4_offload_channels, 14, 0, u);
DEFINE_AD7380_8_OFFLOAD_CHANNEL(ad7388_4_offload_channels, 12, 0, u);

static const char * const ad7380_supplies[] = {
	"vcc", "vlogic",
};

static const char * const adaq4380_supplies[] = {
	"ldo", "vcc", "vlogic", "vs-p", "vs-n", "refin",
};

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

/*
 * Single ended parts have a 2:1 multiplexer in front of each ADC.
 *
 * From an IIO point of view, all inputs are exported, i.e ad7386/7/8
 * export 4 channels and ad7386-4/7-4/8-4 export 8 channels.
 *
 * Inputs AinX0 of multiplexers correspond to the first half of IIO channels
 * (i.e 0-1 or 0-3) and inputs AinX1 correspond to second half (i.e 2-3 or
 * 4-7). Example for AD7386/7/8 (2 channels parts):
 *
 *           IIO   | AD7386/7/8
 *                 |         +----------------------------
 *                 |         |     _____        ______
 *                 |         |    |     |      |      |
 *        voltage0 | AinA0 --|--->|     |      |      |
 *                 |         |    | mux |----->| ADCA |---
 *        voltage2 | AinA1 --|--->|     |      |      |
 *                 |         |    |_____|      |_____ |
 *                 |         |     _____        ______
 *                 |         |    |     |      |      |
 *        voltage1 | AinB0 --|--->|     |      |      |
 *                 |         |    | mux |----->| ADCB |---
 *        voltage3 | AinB1 --|--->|     |      |      |
 *                 |         |    |_____|      |______|
 *                 |         |
 *                 |         +----------------------------
 *
 * Since this is simultaneous sampling for AinX0 OR AinX1 we have two separate
 * scan masks.
 * When sequencer mode is enabled, chip automatically cycles through
 * AinX0 and AinX1 channels. From an IIO point of view, we ca enable all
 * channels, at the cost of an extra read, thus dividing the maximum rate by
 * two.
 */
enum {
	AD7380_SCAN_MASK_CH_0,
	AD7380_SCAN_MASK_CH_1,
	AD7380_SCAN_MASK_SEQ,
};

static const unsigned long ad7380_2x2_channel_scan_masks[] = {
	[AD7380_SCAN_MASK_CH_0] = GENMASK(1, 0),
	[AD7380_SCAN_MASK_CH_1] = GENMASK(3, 2),
	[AD7380_SCAN_MASK_SEQ] = GENMASK(3, 0),
	0
};

static const unsigned long ad7380_2x4_channel_scan_masks[] = {
	[AD7380_SCAN_MASK_CH_0] = GENMASK(3, 0),
	[AD7380_SCAN_MASK_CH_1] = GENMASK(7, 4),
	[AD7380_SCAN_MASK_SEQ] = GENMASK(7, 0),
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

/* Gains stored as fractions of 1000 so they can be expressed by integers. */
static const int ad7380_gains[] = {
	300, 600, 1000, 1600,
};

static const struct ad7380_chip_info ad7380_chip_info = {
	.name = "ad7380",
	.channels = ad7380_channels,
	.offload_channels = ad7380_offload_channels,
	.num_channels = ARRAY_SIZE(ad7380_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7381_chip_info = {
	.name = "ad7381",
	.channels = ad7381_channels,
	.offload_channels = ad7381_offload_channels,
	.num_channels = ARRAY_SIZE(ad7381_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7383_chip_info = {
	.name = "ad7383",
	.channels = ad7383_channels,
	.offload_channels = ad7383_offload_channels,
	.num_channels = ARRAY_SIZE(ad7383_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.vcm_supplies = ad7380_2_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_2_channel_vcm_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7384_chip_info = {
	.name = "ad7384",
	.channels = ad7384_channels,
	.offload_channels = ad7384_offload_channels,
	.num_channels = ARRAY_SIZE(ad7384_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.vcm_supplies = ad7380_2_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_2_channel_vcm_supplies),
	.available_scan_masks = ad7380_2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7386_chip_info = {
	.name = "ad7386",
	.channels = ad7386_channels,
	.offload_channels = ad7386_offload_channels,
	.num_channels = ARRAY_SIZE(ad7386_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7387_chip_info = {
	.name = "ad7387",
	.channels = ad7387_channels,
	.offload_channels = ad7387_offload_channels,
	.num_channels = ARRAY_SIZE(ad7387_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7388_chip_info = {
	.name = "ad7388",
	.channels = ad7388_channels,
	.offload_channels = ad7388_offload_channels,
	.num_channels = ARRAY_SIZE(ad7388_channels),
	.num_simult_channels = 2,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x2_channel_scan_masks,
	.timing_specs = &ad7380_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7380_4_chip_info = {
	.name = "ad7380-4",
	.channels = ad7380_4_channels,
	.offload_channels = ad7380_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7380_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.external_ref_only = true,
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7381_4_chip_info = {
	.name = "ad7381-4",
	.channels = ad7381_4_channels,
	.offload_channels = ad7381_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7381_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7383_4_chip_info = {
	.name = "ad7383-4",
	.channels = ad7383_4_channels,
	.offload_channels = ad7383_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7383_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.vcm_supplies = ad7380_4_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_4_channel_vcm_supplies),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7384_4_chip_info = {
	.name = "ad7384-4",
	.channels = ad7384_4_channels,
	.offload_channels = ad7384_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7384_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.vcm_supplies = ad7380_4_channel_vcm_supplies,
	.num_vcm_supplies = ARRAY_SIZE(ad7380_4_channel_vcm_supplies),
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7386_4_chip_info = {
	.name = "ad7386-4",
	.channels = ad7386_4_channels,
	.offload_channels = ad7386_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7386_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7387_4_chip_info = {
	.name = "ad7387-4",
	.channels = ad7387_4_channels,
	.offload_channels = ad7387_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7387_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info ad7388_4_chip_info = {
	.name = "ad7388-4",
	.channels = ad7388_4_channels,
	.offload_channels = ad7388_4_offload_channels,
	.num_channels = ARRAY_SIZE(ad7388_4_channels),
	.num_simult_channels = 4,
	.supplies = ad7380_supplies,
	.num_supplies = ARRAY_SIZE(ad7380_supplies),
	.has_mux = true,
	.available_scan_masks = ad7380_2x4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info adaq4370_4_chip_info = {
	.name = "adaq4370-4",
	.channels = adaq4380_4_channels,
	.offload_channels = adaq4380_4_offload_channels,
	.num_channels = ARRAY_SIZE(adaq4380_4_channels),
	.num_simult_channels = 4,
	.supplies = adaq4380_supplies,
	.num_supplies = ARRAY_SIZE(adaq4380_supplies),
	.adaq_internal_ref_only = true,
	.has_hardware_gain = true,
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 2 * MEGA,
};

static const struct ad7380_chip_info adaq4380_4_chip_info = {
	.name = "adaq4380-4",
	.channels = adaq4380_4_channels,
	.offload_channels = adaq4380_4_offload_channels,
	.num_channels = ARRAY_SIZE(adaq4380_4_channels),
	.num_simult_channels = 4,
	.supplies = adaq4380_supplies,
	.num_supplies = ARRAY_SIZE(adaq4380_supplies),
	.adaq_internal_ref_only = true,
	.has_hardware_gain = true,
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
	.max_conversion_rate_hz = 4 * MEGA,
};

static const struct ad7380_chip_info adaq4381_4_chip_info = {
	.name = "adaq4381-4",
	.channels = adaq4381_4_channels,
	.offload_channels = adaq4381_4_offload_channels,
	.num_channels = ARRAY_SIZE(adaq4381_4_channels),
	.num_simult_channels = 4,
	.supplies = adaq4380_supplies,
	.num_supplies = ARRAY_SIZE(adaq4380_supplies),
	.adaq_internal_ref_only = true,
	.has_hardware_gain = true,
	.available_scan_masks = ad7380_4_channel_scan_masks,
	.timing_specs = &ad7380_4_timing,
};

static const struct spi_offload_config ad7380_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

struct ad7380_state {
	const struct ad7380_chip_info *chip_info;
	struct spi_device *spi;
	struct regmap *regmap;
	bool resolution_boost_enabled;
	unsigned int ch;
	bool seq;
	unsigned int vref_mv;
	unsigned int vcm_mv[MAX_NUM_CHANNELS];
	unsigned int gain_milli[MAX_NUM_CHANNELS];
	/* xfers, message an buffer for reading sample data */
	struct spi_transfer normal_xfer[2];
	struct spi_message normal_msg;
	struct spi_transfer seq_xfer[4];
	struct spi_message seq_msg;
	struct spi_transfer offload_xfer;
	struct spi_message offload_msg;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	unsigned long offload_trigger_hz;

	int sample_freq_range[3];
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

static const struct reg_default ad7380_reg_defaults[] = {
	{ AD7380_REG_ADDR_ALERT_LOW_TH, 0x800 },
	{ AD7380_REG_ADDR_ALERT_HIGH_TH, 0x7FF },
};

static const struct regmap_range ad7380_volatile_reg_ranges[] = {
	regmap_reg_range(AD7380_REG_ADDR_CONFIG2, AD7380_REG_ADDR_ALERT),
};

static const struct regmap_access_table ad7380_volatile_regs = {
	.yes_ranges = ad7380_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad7380_volatile_reg_ranges),
};

static const struct regmap_config ad7380_regmap_config = {
	.reg_bits = 3,
	.val_bits = 12,
	.reg_read = ad7380_regmap_reg_read,
	.reg_write = ad7380_regmap_reg_write,
	.max_register = AD7380_REG_ADDR_ALERT_HIGH_TH,
	.can_sleep = true,
	.reg_defaults = ad7380_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ad7380_reg_defaults),
	.volatile_table = &ad7380_volatile_regs,
	.cache_type = REGCACHE_MAPLE,
};

static int ad7380_debugfs_reg_access(struct iio_dev *indio_dev, u32 reg,
				     u32 writeval, u32 *readval)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	if (readval)
		ret = regmap_read(st->regmap, reg, readval);
	else
		ret = regmap_write(st->regmap, reg, writeval);

	iio_device_release_direct(indio_dev);

	return ret;
}

/**
 * ad7380_regval_to_osr - convert OSR register value to ratio
 * @regval: register value to check
 *
 * Returns: the ratio corresponding to the OSR register. If regval is not in
 * bound, return 1 (oversampling disabled)
 *
 */
static int ad7380_regval_to_osr(unsigned int regval)
{
	if (regval >= ARRAY_SIZE(ad7380_oversampling_ratios))
		return 1;

	return ad7380_oversampling_ratios[regval];
}

static int ad7380_get_osr(struct ad7380_state *st, int *val)
{
	u32 tmp;
	int ret;

	ret = regmap_read(st->regmap, AD7380_REG_ADDR_CONFIG1, &tmp);
	if (ret)
		return ret;

	*val = ad7380_regval_to_osr(FIELD_GET(AD7380_CONFIG1_OSR, tmp));

	return 0;
}

/*
 * When switching channel, the ADC require an additional settling time.
 * According to the datasheet, data is value on the third CS low. We already
 * have an extra toggle before each read (either direct reads or buffered reads)
 * to sample correct data, so we just add a single CS toggle at the end of the
 * register write.
 */
static int ad7380_set_ch(struct ad7380_state *st, unsigned int ch)
{
	struct spi_transfer xfer = {
		.delay = {
			.value = T_CONVERT_NS,
			.unit = SPI_DELAY_UNIT_NSECS,
		}
	};
	int oversampling_ratio, ret;

	if (st->ch == ch)
		return 0;

	ret = ad7380_get_osr(st, &oversampling_ratio);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap,
				 AD7380_REG_ADDR_CONFIG1,
				 AD7380_CONFIG1_CH,
				 FIELD_PREP(AD7380_CONFIG1_CH, ch));

	if (ret)
		return ret;

	st->ch = ch;

	if (oversampling_ratio > 1)
		xfer.delay.value = T_CONVERT_0_NS +
			T_CONVERT_X_NS * (oversampling_ratio - 1) *
			st->chip_info->num_simult_channels / AD7380_NUM_SDO_LINES;

	return spi_sync_transfer(st->spi, &xfer, 1);
}

/**
 * ad7380_update_xfers - update the SPI transfers base on the current scan type
 * @st:		device instance specific state
 * @scan_type:	current scan type
 */
static int ad7380_update_xfers(struct ad7380_state *st,
				const struct iio_scan_type *scan_type)
{
	struct spi_transfer *xfer = st->seq ? st->seq_xfer : st->normal_xfer;
	unsigned int t_convert = T_CONVERT_NS;
	int oversampling_ratio, ret;

	/*
	 * In the case of oversampling, conversion time is higher than in normal
	 * mode. Technically T_CONVERT_X_NS is lower for some chips, but we use
	 * the maximum value for simplicity for now.
	 */
	ret = ad7380_get_osr(st, &oversampling_ratio);
	if (ret)
		return ret;

	if (oversampling_ratio > 1)
		t_convert = T_CONVERT_0_NS + T_CONVERT_X_NS *
			(oversampling_ratio - 1) *
			st->chip_info->num_simult_channels / AD7380_NUM_SDO_LINES;

	if (st->seq) {
		xfer[0].delay.value = xfer[1].delay.value = t_convert;
		xfer[0].delay.unit = xfer[1].delay.unit = SPI_DELAY_UNIT_NSECS;
		xfer[2].bits_per_word = xfer[3].bits_per_word =
			scan_type->realbits;
		xfer[2].len = xfer[3].len =
			AD7380_SPI_BYTES(scan_type) *
			st->chip_info->num_simult_channels;
		xfer[3].rx_buf = xfer[2].rx_buf + xfer[2].len;
		/* Additional delay required here when oversampling is enabled */
		if (oversampling_ratio > 1)
			xfer[2].delay.value = t_convert;
		else
			xfer[2].delay.value = 0;
		xfer[2].delay.unit = SPI_DELAY_UNIT_NSECS;
	} else {
		xfer[0].delay.value = t_convert;
		xfer[0].delay.unit = SPI_DELAY_UNIT_NSECS;
		xfer[1].bits_per_word = scan_type->realbits;
		xfer[1].len = AD7380_SPI_BYTES(scan_type) *
			st->chip_info->num_simult_channels;
	}

	return 0;
}

static int ad7380_set_sample_freq(struct ad7380_state *st, int val)
{
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = val,
		},
	};
	int ret;

	ret = spi_offload_trigger_validate(st->offload_trigger, &config);
	if (ret)
		return ret;

	st->offload_trigger_hz = config.periodic.frequency_hz;

	return 0;
}

static int ad7380_init_offload_msg(struct ad7380_state *st,
				   struct iio_dev *indio_dev)
{
	struct spi_transfer *xfer = &st->offload_xfer;
	struct device *dev = &st->spi->dev;
	const struct iio_scan_type *scan_type;
	int oversampling_ratio;
	int ret;

	scan_type = iio_get_current_scan_type(indio_dev,
					      &indio_dev->channels[0]);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	if (st->chip_info->has_mux) {
		int index;

		ret = iio_active_scan_mask_index(indio_dev);
		if (ret < 0)
			return ret;

		index = ret;
		if (index == AD7380_SCAN_MASK_SEQ) {
			ret = regmap_set_bits(st->regmap, AD7380_REG_ADDR_CONFIG1,
					      AD7380_CONFIG1_SEQ);
			if (ret)
				return ret;

			st->seq = true;
		} else {
			ret = ad7380_set_ch(st, index);
			if (ret)
				return ret;
		}
	}

	ret = ad7380_get_osr(st, &oversampling_ratio);
	if (ret)
		return ret;

	xfer->bits_per_word = scan_type->realbits;
	xfer->offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	xfer->len = AD7380_SPI_BYTES(scan_type) * st->chip_info->num_simult_channels;

	spi_message_init_with_transfers(&st->offload_msg, xfer, 1);
	st->offload_msg.offload = st->offload;

	ret = spi_optimize_message(st->spi, &st->offload_msg);
	if (ret) {
		dev_err(dev, "failed to prepare offload msg, err: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int ad7380_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = st->offload_trigger_hz,
		},
	};
	int ret;

	ret = ad7380_init_offload_msg(st, indio_dev);
	if (ret)
		return ret;

	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger, &config);
	if (ret)
		spi_unoptimize_message(&st->offload_msg);

	return ret;
}

static int ad7380_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	if (st->seq) {
		ret = regmap_update_bits(st->regmap,
					 AD7380_REG_ADDR_CONFIG1,
					 AD7380_CONFIG1_SEQ,
					 FIELD_PREP(AD7380_CONFIG1_SEQ, 0));
		if (ret)
			return ret;

		st->seq = false;
	}

	spi_offload_trigger_disable(st->offload, st->offload_trigger);

	spi_unoptimize_message(&st->offload_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad7380_offload_buffer_setup_ops = {
	.postenable = ad7380_offload_buffer_postenable,
	.predisable = ad7380_offload_buffer_predisable,
};

static int ad7380_triggered_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	struct spi_message *msg = &st->normal_msg;
	int ret;

	/*
	 * Currently, we always read all channels at the same time. The scan_type
	 * is the same for all channels, so we just pass the first channel.
	 */
	scan_type = iio_get_current_scan_type(indio_dev, &indio_dev->channels[0]);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	if (st->chip_info->has_mux) {
		unsigned int index;

		/*
		 * Depending on the requested scan_mask and current state,
		 * we need to either change CH bit, or enable sequencer mode
		 * to sample correct data.
		 * Sequencer mode is enabled if active mask corresponds to all
		 * IIO channels enabled. Otherwise, CH bit is set.
		 */
		ret = iio_active_scan_mask_index(indio_dev);
		if (ret < 0)
			return ret;

		index = ret;
		if (index == AD7380_SCAN_MASK_SEQ) {
			ret = regmap_update_bits(st->regmap,
						 AD7380_REG_ADDR_CONFIG1,
						 AD7380_CONFIG1_SEQ,
						 FIELD_PREP(AD7380_CONFIG1_SEQ, 1));
			if (ret)
				return ret;
			msg = &st->seq_msg;
			st->seq = true;
		} else {
			ret = ad7380_set_ch(st, index);
			if (ret)
				return ret;
		}

	}

	ret = ad7380_update_xfers(st, scan_type);
	if (ret)
		return ret;

	return spi_optimize_message(st->spi, msg);
}

static int ad7380_triggered_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	struct spi_message *msg = &st->normal_msg;
	int ret;

	if (st->seq) {
		ret = regmap_update_bits(st->regmap,
					 AD7380_REG_ADDR_CONFIG1,
					 AD7380_CONFIG1_SEQ,
					 FIELD_PREP(AD7380_CONFIG1_SEQ, 0));
		if (ret)
			return ret;

		msg = &st->seq_msg;
		st->seq = false;
	}

	spi_unoptimize_message(msg);

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
	struct spi_message *msg = st->seq ? &st->seq_msg : &st->normal_msg;
	int ret;

	ret = spi_sync(st->spi, msg);
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
	unsigned int index = scan_index;
	int ret;

	if (st->chip_info->has_mux) {
		unsigned int ch = 0;

		if (index >= st->chip_info->num_simult_channels) {
			index -= st->chip_info->num_simult_channels;
			ch = 1;
		}

		ret = ad7380_set_ch(st, ch);
		if (ret)
			return ret;
	}

	ret = ad7380_update_xfers(st, scan_type);
	if (ret)
		return ret;

	ret = spi_sync(st->spi, &st->normal_msg);
	if (ret < 0)
		return ret;

	if (scan_type->realbits > 16) {
		if (scan_type->sign == 's')
			*val = sign_extend32(*(u32 *)(st->scan_data + 4 * index),
					     scan_type->realbits - 1);
		else
			*val = *(u32 *)(st->scan_data + 4 * index) &
				GENMASK(scan_type->realbits - 1, 0);
	} else {
		if (scan_type->sign == 's')
			*val = sign_extend32(*(u16 *)(st->scan_data + 2 * index),
					     scan_type->realbits - 1);
		else
			*val = *(u16 *)(st->scan_data + 2 * index) &
				GENMASK(scan_type->realbits - 1, 0);
	}

	return IIO_VAL_INT;
}

static int ad7380_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	int ret;

	scan_type = iio_get_current_scan_type(indio_dev, chan);

	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7380_read_direct(st, chan->scan_index,
					 scan_type, val);

		iio_device_release_direct(indio_dev);

		return ret;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * According to the datasheet, the LSB size is:
		 *    * (2 × VREF) / 2^N, for differential chips
		 *    * VREF / 2^N, for pseudo-differential chips
		 * where N is the ADC resolution (i.e realbits)
		 *
		 * The gain is stored as a fraction of 1000 and, as we need to
		 * divide vref_mv by the gain, we invert the gain/1000 fraction.
		 */
		if (st->chip_info->has_hardware_gain)
			*val = mult_frac(st->vref_mv, MILLI,
					 st->gain_milli[chan->scan_index]);
		else
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
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7380_get_osr(st, val);

		iio_device_release_direct(indio_dev);

		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->offload_trigger_hz;
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
	struct ad7380_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad7380_oversampling_ratios;
		*length = ARRAY_SIZE(ad7380_oversampling_ratios);
		*type = IIO_VAL_INT;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = st->sample_freq_range;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
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

static int ad7380_set_oversampling_ratio(struct ad7380_state *st, int val)
{
	int ret, osr, boost;

	osr = ad7380_osr_to_regval(val);
	if (osr < 0)
		return osr;

	/* always enable resolution boost when oversampling is enabled */
	boost = osr > 0 ? 1 : 0;

	ret = regmap_update_bits(st->regmap,
				 AD7380_REG_ADDR_CONFIG1,
				 AD7380_CONFIG1_OSR | AD7380_CONFIG1_RES,
				 FIELD_PREP(AD7380_CONFIG1_OSR, osr) |
				 FIELD_PREP(AD7380_CONFIG1_RES, boost));

	if (ret)
		return ret;

	st->resolution_boost_enabled = boost;

	/*
	 * Perform a soft reset. This will flush the oversampling
	 * block and FIFO but will maintain the content of the
	 * configurable registers.
	 */
	ret = regmap_update_bits(st->regmap,
				 AD7380_REG_ADDR_CONFIG2,
				 AD7380_CONFIG2_RESET,
				 FIELD_PREP(AD7380_CONFIG2_RESET,
					    AD7380_CONFIG2_RESET_SOFT));
	return ret;
}
static int ad7380_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long mask)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val < 1)
			return -EINVAL;
		return ad7380_set_sample_freq(st, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7380_set_oversampling_ratio(st, val);

		iio_device_release_direct(indio_dev);

		return ret;
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

static int ad7380_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int tmp, ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_read(st->regmap, AD7380_REG_ADDR_CONFIG1, &tmp);

	iio_device_release_direct(indio_dev);

	if (ret)
		return ret;

	return FIELD_GET(AD7380_CONFIG1_ALERTEN, tmp);
}

static int ad7380_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     bool state)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = regmap_update_bits(st->regmap,
				 AD7380_REG_ADDR_CONFIG1,
				 AD7380_CONFIG1_ALERTEN,
				 FIELD_PREP(AD7380_CONFIG1_ALERTEN, state));

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad7380_get_alert_th(struct ad7380_state *st,
			       enum iio_event_direction dir,
			       int *val)
{
	int ret, tmp;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		ret = regmap_read(st->regmap,
				  AD7380_REG_ADDR_ALERT_HIGH_TH,
				  &tmp);
		if (ret)
			return ret;

		*val = FIELD_GET(AD7380_ALERT_HIGH_TH, tmp);
		return IIO_VAL_INT;
	case IIO_EV_DIR_FALLING:
		ret = regmap_read(st->regmap,
				  AD7380_REG_ADDR_ALERT_LOW_TH,
				  &tmp);
		if (ret)
			return ret;

		*val = FIELD_GET(AD7380_ALERT_LOW_TH, tmp);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7380_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7380_get_alert_th(st, dir, val);

		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int ad7380_set_alert_th(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_direction dir,
			       int val)
{
	struct ad7380_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	u16 th;

	/*
	 * According to the datasheet,
	 * AD7380_REG_ADDR_ALERT_HIGH_TH[11:0] are the 12 MSB of the
	 * 16-bits internal alert high register. LSB are set to 0xf.
	 * AD7380_REG_ADDR_ALERT_LOW_TH[11:0] are the 12 MSB of the
	 * 16 bits internal alert low register. LSB are set to 0x0.
	 *
	 * When alert is enabled the conversion from the adc is compared
	 * immediately to the alert high/low thresholds, before any
	 * oversampling. This means that the thresholds are the same for
	 * normal mode and oversampling mode.
	 */

	/* Extract the 12 MSB of val */
	scan_type = iio_get_current_scan_type(indio_dev, chan);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	th = val >> (scan_type->realbits - 12);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		return regmap_write(st->regmap,
				    AD7380_REG_ADDR_ALERT_HIGH_TH,
				    th);
	case IIO_EV_DIR_FALLING:
		return regmap_write(st->regmap,
				    AD7380_REG_ADDR_ALERT_LOW_TH,
				    th);
	default:
		return -EINVAL;
	}
}

static int ad7380_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad7380_set_alert_th(indio_dev, chan, dir, val);

		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad7380_info = {
	.read_raw = &ad7380_read_raw,
	.read_avail = &ad7380_read_avail,
	.write_raw = &ad7380_write_raw,
	.get_current_scan_type = &ad7380_get_current_scan_type,
	.debugfs_reg_access = &ad7380_debugfs_reg_access,
	.read_event_config = &ad7380_read_event_config,
	.write_event_config = &ad7380_write_event_config,
	.read_event_value = &ad7380_read_event_value,
	.write_event_value = &ad7380_write_event_value,
};

static int ad7380_init(struct ad7380_state *st, bool external_ref_en)
{
	int ret;

	/* perform hard reset */
	ret = regmap_update_bits(st->regmap, AD7380_REG_ADDR_CONFIG2,
				 AD7380_CONFIG2_RESET,
				 FIELD_PREP(AD7380_CONFIG2_RESET,
					    AD7380_CONFIG2_RESET_HARD));
	if (ret < 0)
		return ret;

	if (external_ref_en) {
		/* select external reference voltage */
		ret = regmap_set_bits(st->regmap, AD7380_REG_ADDR_CONFIG1,
				      AD7380_CONFIG1_REFSEL);
		if (ret < 0)
			return ret;
	}

	/* This is the default value after reset. */
	st->ch = 0;
	st->seq = false;

	/* SPI 1-wire mode */
	return regmap_update_bits(st->regmap, AD7380_REG_ADDR_CONFIG2,
				  AD7380_CONFIG2_SDO,
				  FIELD_PREP(AD7380_CONFIG2_SDO,
					     AD7380_NUM_SDO_LINES));
}

static int ad7380_probe_spi_offload(struct iio_dev *indio_dev,
				    struct ad7380_state *st)
{
	struct spi_device *spi = st->spi;
	struct device *dev = &spi->dev;
	struct dma_chan *rx_dma;
	int sample_rate, ret;

	indio_dev->setup_ops = &ad7380_offload_buffer_setup_ops;
	indio_dev->channels = st->chip_info->offload_channels;
	/* Just removing the timestamp channel. */
	indio_dev->num_channels--;

	st->offload_trigger = devm_spi_offload_trigger_get(dev, st->offload,
		SPI_OFFLOAD_TRIGGER_PERIODIC);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
				     "failed to get offload trigger\n");

	sample_rate = st->chip_info->max_conversion_rate_hz *
		      AD7380_NUM_SDO_LINES / st->chip_info->num_simult_channels;

	st->sample_freq_range[0] = 1; /* min */
	st->sample_freq_range[1] = 1; /* step */
	st->sample_freq_range[2] = sample_rate; /* max */

	/*
	 * Starting with a quite low frequency, to allow oversampling x32,
	 * user is then reponsible to adjust the frequency for the specific case.
	 */
	ret = ad7380_set_sample_freq(st, sample_rate / 32);
	if (ret)
		return ret;

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "failed to get offload RX DMA\n");

	ret = devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev,
		rx_dma, IIO_BUFFER_DIRECTION_IN);
	if (ret)
		return dev_err_probe(dev, ret, "cannot setup dma buffer\n");

	return 0;
}

static int ad7380_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad7380_state *st;
	bool external_ref_en;
	int ret, i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return dev_err_probe(dev, -EINVAL, "missing match data\n");

	ret = devm_regulator_bulk_get_enable(dev, st->chip_info->num_supplies,
					     st->chip_info->supplies);

	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable power supplies\n");
	fsleep(T_POWERUP_US);

	if (st->chip_info->adaq_internal_ref_only) {
		/*
		 * ADAQ chips use fixed internal reference but still
		 * require a specific reference supply to power it.
		 * "refin" is already enabled with other power supplies
		 * in bulk_get_enable().
		 */

		st->vref_mv = ADAQ4380_INTERNAL_REF_MV;

		/* these chips don't have a register bit for this */
		external_ref_en = false;
	} else if (st->chip_info->external_ref_only) {
		ret = devm_regulator_get_enable_read_voltage(dev, "refin");
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to get refin regulator\n");

		st->vref_mv = ret / 1000;

		/* these chips don't have a register bit for this */
		external_ref_en = false;
	} else {
		/*
		 * If there is no REFIO supply, then it means that we are using
		 * the internal reference, otherwise REFIO is reference voltage.
		 */
		ret = devm_regulator_get_enable_read_voltage(dev, "refio");
		if (ret < 0 && ret != -ENODEV)
			return dev_err_probe(dev, ret,
					     "Failed to get refio regulator\n");

		external_ref_en = ret != -ENODEV;
		st->vref_mv = external_ref_en ? ret / 1000 : AD7380_INTERNAL_REF_MV;
	}

	if (st->chip_info->num_vcm_supplies > ARRAY_SIZE(st->vcm_mv))
		return dev_err_probe(dev, -EINVAL,
				     "invalid number of VCM supplies\n");

	/*
	 * pseudo-differential chips have common mode supplies for the negative
	 * input pin.
	 */
	for (i = 0; i < st->chip_info->num_vcm_supplies; i++) {
		const char *vcm = st->chip_info->vcm_supplies[i];

		ret = devm_regulator_get_enable_read_voltage(dev, vcm);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to get %s regulator\n",
					     vcm);

		st->vcm_mv[i] = ret / 1000;
	}

	for (i = 0; i < MAX_NUM_CHANNELS; i++)
		st->gain_milli[i] = AD7380_DEFAULT_GAIN_MILLI;

	if (st->chip_info->has_hardware_gain) {
		device_for_each_child_node_scoped(dev, node) {
			unsigned int channel, gain;
			int gain_idx;

			ret = fwnode_property_read_u32(node, "reg", &channel);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to read reg property\n");

			if (channel >= st->chip_info->num_channels - 1)
				return dev_err_probe(dev, -EINVAL,
						     "Invalid channel number %i\n",
						     channel);

			ret = fwnode_property_read_u32(node, "adi,gain-milli",
						       &gain);
			if (ret && ret != -EINVAL)
				return dev_err_probe(dev, ret,
						     "Failed to read gain for channel %i\n",
						     channel);
			if (ret != -EINVAL) {
				/*
				 * Match gain value from dt to one of supported
				 * gains
				 */
				gain_idx = find_closest(gain, ad7380_gains,
							ARRAY_SIZE(ad7380_gains));
				st->gain_milli[channel] = ad7380_gains[gain_idx];
			}
		}
	}

	st->regmap = devm_regmap_init(dev, NULL, st, &ad7380_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "failed to allocate register map\n");

	/*
	 * Setting up xfer structures for both normal and sequence mode. These
	 * struct are used for both direct read and triggered buffer. Additional
	 * fields will be set up in ad7380_update_xfers() based on the current
	 * state of the driver at the time of the read.
	 */

	/*
	 * In normal mode a read is composed of two steps:
	 *   - first, toggle CS (no data xfer) to trigger a conversion
	 *   - then, read data
	 */
	st->normal_xfer[0].cs_change = 1;
	st->normal_xfer[0].cs_change_delay.value = st->chip_info->timing_specs->t_csh_ns;
	st->normal_xfer[0].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
	st->normal_xfer[1].rx_buf = st->scan_data;

	spi_message_init_with_transfers(&st->normal_msg, st->normal_xfer,
					ARRAY_SIZE(st->normal_xfer));
	/*
	 * In sequencer mode a read is composed of four steps:
	 *   - CS toggle (no data xfer) to get the right point in the sequence
	 *   - CS toggle (no data xfer) to trigger a conversion of AinX0 and
	 *   acquisition of AinX1
	 *   - 2 data reads, to read AinX0 and AinX1
	 */
	st->seq_xfer[0].cs_change = 1;
	st->seq_xfer[0].cs_change_delay.value = st->chip_info->timing_specs->t_csh_ns;
	st->seq_xfer[0].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
	st->seq_xfer[1].cs_change = 1;
	st->seq_xfer[1].cs_change_delay.value = st->chip_info->timing_specs->t_csh_ns;
	st->seq_xfer[1].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	st->seq_xfer[2].rx_buf = st->scan_data;
	st->seq_xfer[2].cs_change = 1;
	st->seq_xfer[2].cs_change_delay.value = st->chip_info->timing_specs->t_csh_ns;
	st->seq_xfer[2].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	spi_message_init_with_transfers(&st->seq_msg, st->seq_xfer,
					ARRAY_SIZE(st->seq_xfer));

	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;
	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad7380_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = st->chip_info->available_scan_masks;

	st->offload = devm_spi_offload_get(dev, spi, &ad7380_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get offload\n");

	/* If no SPI offload, fall back to low speed usage. */
	if (ret == -ENODEV) {
		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      iio_pollfunc_store_time,
						      ad7380_trigger_handler,
						      &ad7380_buffer_setup_ops);
		if (ret)
			return ret;
	} else {
		ret = ad7380_probe_spi_offload(indio_dev, st);
		if (ret)
			return ret;
	}

	ret = ad7380_init(st, external_ref_en);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad7380_of_match_table[] = {
	{ .compatible = "adi,ad7380", .data = &ad7380_chip_info },
	{ .compatible = "adi,ad7381", .data = &ad7381_chip_info },
	{ .compatible = "adi,ad7383", .data = &ad7383_chip_info },
	{ .compatible = "adi,ad7384", .data = &ad7384_chip_info },
	{ .compatible = "adi,ad7386", .data = &ad7386_chip_info },
	{ .compatible = "adi,ad7387", .data = &ad7387_chip_info },
	{ .compatible = "adi,ad7388", .data = &ad7388_chip_info },
	{ .compatible = "adi,ad7380-4", .data = &ad7380_4_chip_info },
	{ .compatible = "adi,ad7381-4", .data = &ad7381_4_chip_info },
	{ .compatible = "adi,ad7383-4", .data = &ad7383_4_chip_info },
	{ .compatible = "adi,ad7384-4", .data = &ad7384_4_chip_info },
	{ .compatible = "adi,ad7386-4", .data = &ad7386_4_chip_info },
	{ .compatible = "adi,ad7387-4", .data = &ad7387_4_chip_info },
	{ .compatible = "adi,ad7388-4", .data = &ad7388_4_chip_info },
	{ .compatible = "adi,adaq4370-4", .data = &adaq4370_4_chip_info },
	{ .compatible = "adi,adaq4380-4", .data = &adaq4380_4_chip_info },
	{ .compatible = "adi,adaq4381-4", .data = &adaq4381_4_chip_info },
	{ }
};

static const struct spi_device_id ad7380_id_table[] = {
	{ "ad7380", (kernel_ulong_t)&ad7380_chip_info },
	{ "ad7381", (kernel_ulong_t)&ad7381_chip_info },
	{ "ad7383", (kernel_ulong_t)&ad7383_chip_info },
	{ "ad7384", (kernel_ulong_t)&ad7384_chip_info },
	{ "ad7386", (kernel_ulong_t)&ad7386_chip_info },
	{ "ad7387", (kernel_ulong_t)&ad7387_chip_info },
	{ "ad7388", (kernel_ulong_t)&ad7388_chip_info },
	{ "ad7380-4", (kernel_ulong_t)&ad7380_4_chip_info },
	{ "ad7381-4", (kernel_ulong_t)&ad7381_4_chip_info },
	{ "ad7383-4", (kernel_ulong_t)&ad7383_4_chip_info },
	{ "ad7384-4", (kernel_ulong_t)&ad7384_4_chip_info },
	{ "ad7386-4", (kernel_ulong_t)&ad7386_4_chip_info },
	{ "ad7387-4", (kernel_ulong_t)&ad7387_4_chip_info },
	{ "ad7388-4", (kernel_ulong_t)&ad7388_4_chip_info },
	{ "adaq4370-4", (kernel_ulong_t)&adaq4370_4_chip_info },
	{ "adaq4380-4", (kernel_ulong_t)&adaq4380_4_chip_info },
	{ "adaq4381-4", (kernel_ulong_t)&adaq4381_4_chip_info },
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
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
