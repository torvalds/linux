// SPDX-License-Identifier: GPL-2.0+
/*
 * AD4000 SPI ADC driver
 *
 * Copyright 2024 Analog Devices Inc.
 */
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>
#include <linux/util_macros.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define AD4000_READ_COMMAND	0x54
#define AD4000_WRITE_COMMAND	0x14

#define AD4000_CONFIG_REG_DEFAULT	0xE1

/* AD4000 Configuration Register programmable bits */
#define AD4000_CFG_SPAN_COMP		BIT(3) /* Input span compression  */
#define AD4000_CFG_HIGHZ		BIT(2) /* High impedance mode  */
#define AD4000_CFG_TURBO		BIT(1) /* Turbo mode */

#define AD4000_SCALE_OPTIONS		2

#define __AD4000_DIFF_CHANNEL(_sign, _real_bits, _storage_bits, _reg_access, _offl)\
{										\
	.type = IIO_VOLTAGE,							\
	.indexed = 1,								\
	.differential = 1,							\
	.channel = 0,								\
	.channel2 = 1,								\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      (_offl ? BIT(IIO_CHAN_INFO_SAMP_FREQ) : 0),	\
	.info_mask_separate_available = _reg_access ? BIT(IIO_CHAN_INFO_SCALE) : 0,\
	.scan_index = 0,							\
	.scan_type = {								\
		.sign = _sign,							\
		.realbits = _real_bits,						\
		.storagebits = _storage_bits,					\
		.shift = (_offl ? 0 : _storage_bits - _real_bits),		\
		.endianness = _offl ? IIO_CPU : IIO_BE				\
	},									\
}

#define AD4000_DIFF_CHANNEL(_sign, _real_bits, _reg_access, _offl)		\
	__AD4000_DIFF_CHANNEL((_sign), (_real_bits),				\
			      (((_offl) || ((_real_bits) > 16)) ? 32 : 16),	\
			      (_reg_access), (_offl))

/*
 * When SPI offload is configured, transfers are executed without CPU
 * intervention so no soft timestamp can be recorded when transfers run.
 * Because of that, the macros that set timestamp channel are only used when
 * transfers are not offloaded.
 */
#define AD4000_DIFF_CHANNELS(_sign, _real_bits, _reg_access)			\
{										\
	AD4000_DIFF_CHANNEL(_sign, _real_bits, _reg_access, 0),			\
	IIO_CHAN_SOFT_TIMESTAMP(1),						\
}

#define __AD4000_PSEUDO_DIFF_CHANNEL(_sign, _real_bits, _storage_bits,		\
				     _reg_access, _offl)			\
{										\
	.type = IIO_VOLTAGE,							\
	.indexed = 1,								\
	.channel = 0,								\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      BIT(IIO_CHAN_INFO_OFFSET) |			\
			      (_offl ? BIT(IIO_CHAN_INFO_SAMP_FREQ) : 0),	\
	.info_mask_separate_available = _reg_access ? BIT(IIO_CHAN_INFO_SCALE) : 0,\
	.scan_index = 0,							\
	.scan_type = {								\
		.sign = _sign,							\
		.realbits = _real_bits,						\
		.storagebits = _storage_bits,					\
		.shift = (_offl ? 0 : _storage_bits - _real_bits),		\
		.endianness = _offl ? IIO_CPU : IIO_BE				\
	},									\
}

#define AD4000_PSEUDO_DIFF_CHANNEL(_sign, _real_bits, _reg_access, _offl)	\
	__AD4000_PSEUDO_DIFF_CHANNEL((_sign), (_real_bits),			\
				     (((_offl) || ((_real_bits) > 16)) ? 32 : 16),\
				     (_reg_access), (_offl))

#define AD4000_PSEUDO_DIFF_CHANNELS(_sign, _real_bits, _reg_access)		\
{										\
	AD4000_PSEUDO_DIFF_CHANNEL(_sign, _real_bits, _reg_access, 0),		\
	IIO_CHAN_SOFT_TIMESTAMP(1),						\
}

static const char * const ad4000_power_supplies[] = {
	"vdd", "vio"
};

enum ad4000_sdi {
	AD4000_SDI_MOSI,
	AD4000_SDI_VIO,
	AD4000_SDI_CS,
	AD4000_SDI_GND,
};

/* maps adi,sdi-pin property value to enum */
static const char * const ad4000_sdi_pin[] = {
	[AD4000_SDI_MOSI] = "sdi",
	[AD4000_SDI_VIO] = "high",
	[AD4000_SDI_CS] = "cs",
	[AD4000_SDI_GND] = "low",
};

/* Gains stored as fractions of 1000 so they can be expressed by integers. */
static const int ad4000_gains[] = {
	454, 909, 1000, 1900,
};

struct ad4000_time_spec {
	int t_conv_ns;
	int t_quiet2_ns;
};

/*
 * Same timing specifications for all of AD4000, AD4001, ..., AD4008, AD4010,
 * ADAQ4001, and ADAQ4003.
 */
static const struct ad4000_time_spec ad4000_t_spec = {
	.t_conv_ns = 320,
	.t_quiet2_ns = 60,
};

/* AD4020, AD4021, AD4022 */
static const struct ad4000_time_spec ad4020_t_spec = {
	.t_conv_ns = 350,
	.t_quiet2_ns = 60,
};

/* AD7983, AD7984 */
static const struct ad4000_time_spec ad7983_t_spec = {
	.t_conv_ns = 500,
	.t_quiet2_ns = 0,
};

/* AD7980, AD7982 */
static const struct ad4000_time_spec ad7980_t_spec = {
	.t_conv_ns = 800,
	.t_quiet2_ns = 0,
};

/* AD7946, AD7686, AD7688, AD7988-5, AD7693 */
static const struct ad4000_time_spec ad7686_t_spec = {
	.t_conv_ns = 1600,
	.t_quiet2_ns = 0,
};

/* AD7690 */
static const struct ad4000_time_spec ad7690_t_spec = {
	.t_conv_ns = 2100,
	.t_quiet2_ns = 0,
};

/* AD7942, AD7685, AD7687 */
static const struct ad4000_time_spec ad7687_t_spec = {
	.t_conv_ns = 3200,
	.t_quiet2_ns = 0,
};

/* AD7691 */
static const struct ad4000_time_spec ad7691_t_spec = {
	.t_conv_ns = 3700,
	.t_quiet2_ns = 0,
};

/* AD7988-1 */
static const struct ad4000_time_spec ad7988_1_t_spec = {
	.t_conv_ns = 9500,
	.t_quiet2_ns = 0,
};

struct ad4000_chip_info {
	const char *dev_name;
	struct iio_chan_spec chan_spec[2];
	struct iio_chan_spec reg_access_chan_spec[2];
	struct iio_chan_spec offload_chan_spec;
	struct iio_chan_spec reg_access_offload_chan_spec;
	const struct ad4000_time_spec *time_spec;
	bool has_hardware_gain;
	int max_rate_hz;
};

static const struct ad4000_chip_info ad4000_chip_info = {
	.dev_name = "ad4000",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info ad4001_chip_info = {
	.dev_name = "ad4001",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 16, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info ad4002_chip_info = {
	.dev_name = "ad4002",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info ad4003_chip_info = {
	.dev_name = "ad4003",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 18, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info ad4004_chip_info = {
	.dev_name = "ad4004",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad4005_chip_info = {
	.dev_name = "ad4005",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 16, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad4006_chip_info = {
	.dev_name = "ad4006",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad4007_chip_info = {
	.dev_name = "ad4007",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 18, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad4008_chip_info = {
	.dev_name = "ad4008",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad4010_chip_info = {
	.dev_name = "ad4010",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 0),
	.reg_access_chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 18, 1),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad4011_chip_info = {
	.dev_name = "ad4011",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 18, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad4020_chip_info = {
	.dev_name = "ad4020",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 20, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 20, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 1, 1),
	.time_spec = &ad4020_t_spec,
	.max_rate_hz = 1800 * KILO,
};

static const struct ad4000_chip_info ad4021_chip_info = {
	.dev_name = "ad4021",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 20, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 20, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 1, 1),
	.time_spec = &ad4020_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad4022_chip_info = {
	.dev_name = "ad4022",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 20, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 20, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 20, 1, 1),
	.time_spec = &ad4020_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info adaq4001_chip_info = {
	.dev_name = "adaq4001",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 16, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 1, 1),
	.time_spec = &ad4000_t_spec,
	.has_hardware_gain = true,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info adaq4003_chip_info = {
	.dev_name = "adaq4003",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.reg_access_chan_spec = AD4000_DIFF_CHANNELS('s', 18, 1),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.reg_access_offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 1, 1),
	.time_spec = &ad4000_t_spec,
	.has_hardware_gain = true,
	.max_rate_hz = 2 * MEGA,
};

static const struct ad4000_chip_info ad7685_chip_info = {
	.dev_name = "ad7685",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7687_t_spec,
	.max_rate_hz = 250 * KILO,
};

static const struct ad4000_chip_info ad7686_chip_info = {
	.dev_name = "ad7686",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7686_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad7687_chip_info = {
	.dev_name = "ad7687",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.time_spec = &ad7687_t_spec,
	.max_rate_hz = 250 * KILO,
};

static const struct ad4000_chip_info ad7688_chip_info = {
	.dev_name = "ad7688",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.time_spec = &ad7686_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad7690_chip_info = {
	.dev_name = "ad7690",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.time_spec = &ad7690_t_spec,
	.max_rate_hz = 400 * KILO,
};

static const struct ad4000_chip_info ad7691_chip_info = {
	.dev_name = "ad7691",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.time_spec = &ad7691_t_spec,
	.max_rate_hz = 250 * KILO,
};

static const struct ad4000_chip_info ad7693_chip_info = {
	.dev_name = "ad7693",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 16, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 16, 0, 1),
	.time_spec = &ad7686_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad7942_chip_info = {
	.dev_name = "ad7942",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 14, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 14, 0, 1),
	.time_spec = &ad7687_t_spec,
	.max_rate_hz = 250 * KILO,
};

static const struct ad4000_chip_info ad7946_chip_info = {
	.dev_name = "ad7946",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 14, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 14, 0, 1),
	.time_spec = &ad7686_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct ad4000_chip_info ad7980_chip_info = {
	.dev_name = "ad7980",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7980_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad7982_chip_info = {
	.dev_name = "ad7982",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.time_spec = &ad7980_t_spec,
	.max_rate_hz = 1 * MEGA,
};

static const struct ad4000_chip_info ad7983_chip_info = {
	.dev_name = "ad7983",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7983_t_spec,
	.max_rate_hz = 1 * MEGA + 333 * KILO + 333,
};

static const struct ad4000_chip_info ad7984_chip_info = {
	.dev_name = "ad7984",
	.chan_spec = AD4000_DIFF_CHANNELS('s', 18, 0),
	.offload_chan_spec = AD4000_DIFF_CHANNEL('s', 18, 0, 1),
	.time_spec = &ad7983_t_spec,
	.max_rate_hz = 1 * MEGA + 333 * KILO + 333,
};

static const struct ad4000_chip_info ad7988_1_chip_info = {
	.dev_name = "ad7988-1",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7988_1_t_spec,
	.max_rate_hz = 100 * KILO,
};

static const struct ad4000_chip_info ad7988_5_chip_info = {
	.dev_name = "ad7988-5",
	.chan_spec = AD4000_PSEUDO_DIFF_CHANNELS('u', 16, 0),
	.offload_chan_spec = AD4000_PSEUDO_DIFF_CHANNEL('u', 16, 0, 1),
	.time_spec = &ad7686_t_spec,
	.max_rate_hz = 500 * KILO,
};

static const struct spi_offload_config ad4000_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

struct ad4000_state {
	struct spi_device *spi;
	struct gpio_desc *cnv_gpio;
	struct spi_transfer xfers[2];
	struct spi_message msg;
	struct spi_transfer offload_xfer;
	struct spi_message offload_msg;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	bool using_offload;
	unsigned long offload_trigger_hz;
	int max_rate_hz;
	struct mutex lock; /* Protect read modify write cycle */
	int vref_mv;
	enum ad4000_sdi sdi_pin;
	bool span_comp;
	u16 gain_milli;
	int scale_tbl[AD4000_SCALE_OPTIONS][2];
	const struct ad4000_time_spec *time_spec;

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
	u8 tx_buf[2];
	u8 rx_buf[2];
};

static void ad4000_fill_scale_tbl(struct ad4000_state *st,
				  struct iio_chan_spec const *chan)
{
	int val, tmp0, tmp1;
	int scale_bits;
	u64 tmp2;

	/*
	 * ADCs that output two's complement code have one less bit to express
	 * voltage magnitude.
	 */
	if (chan->scan_type.sign == 's')
		scale_bits = chan->scan_type.realbits - 1;
	else
		scale_bits = chan->scan_type.realbits;

	/*
	 * The gain is stored as a fraction of 1000 and, as we need to
	 * divide vref_mv by the gain, we invert the gain/1000 fraction.
	 * Also multiply by an extra MILLI to preserve precision.
	 * Thus, we have MILLI * MILLI equals MICRO as fraction numerator.
	 */
	val = mult_frac(st->vref_mv, MICRO, st->gain_milli);

	/* Would multiply by NANO here but we multiplied by extra MILLI */
	tmp2 = (u64)val * MICRO >> scale_bits;
	tmp0 = div_s64_rem(tmp2, NANO, &tmp1);

	/* Store scale for when span compression is disabled */
	st->scale_tbl[0][0] = tmp0; /* Integer part */
	st->scale_tbl[0][1] = abs(tmp1); /* Fractional part */

	/* Store scale for when span compression is enabled */
	st->scale_tbl[1][0] = tmp0;

	/* The integer part is always zero so don't bother to divide it. */
	if (chan->differential)
		st->scale_tbl[1][1] = DIV_ROUND_CLOSEST(abs(tmp1) * 4, 5);
	else
		st->scale_tbl[1][1] = DIV_ROUND_CLOSEST(abs(tmp1) * 9, 10);
}

static int ad4000_write_reg(struct ad4000_state *st, uint8_t val)
{
	st->tx_buf[0] = AD4000_WRITE_COMMAND;
	st->tx_buf[1] = val;
	return spi_write(st->spi, st->tx_buf, ARRAY_SIZE(st->tx_buf));
}

static int ad4000_read_reg(struct ad4000_state *st, unsigned int *val)
{
	struct spi_transfer t = {
		.tx_buf = st->tx_buf,
		.rx_buf = st->rx_buf,
		.len = 2,
	};
	int ret;

	st->tx_buf[0] = AD4000_READ_COMMAND;
	ret = spi_sync_transfer(st->spi, &t, 1);
	if (ret < 0)
		return ret;

	*val = st->rx_buf[1];
	return ret;
}

static int ad4000_set_sampling_freq(struct ad4000_state *st, int freq)
{
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = freq,
		},
	};
	int ret;

	ret = spi_offload_trigger_validate(st->offload_trigger, &config);
	if (ret)
		return ret;

	st->offload_trigger_hz = config.periodic.frequency_hz;

	return 0;
}

static int ad4000_convert_and_acquire(struct ad4000_state *st)
{
	int ret;

	/*
	 * In 4-wire mode, the CNV line is held high for the entire conversion
	 * and acquisition process. In other modes, the CNV GPIO is optional
	 * and, if provided, replaces controller CS. If CNV GPIO is not defined
	 * gpiod_set_value_cansleep() has no effect.
	 */
	gpiod_set_value_cansleep(st->cnv_gpio, 1);
	ret = spi_sync(st->spi, &st->msg);
	gpiod_set_value_cansleep(st->cnv_gpio, 0);

	return ret;
}

static int ad4000_single_conversion(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan, int *val)
{
	struct ad4000_state *st = iio_priv(indio_dev);
	u32 sample;
	int ret;

	ret = ad4000_convert_and_acquire(st);
	if (ret < 0)
		return ret;

	if (chan->scan_type.endianness == IIO_BE) {
		if (chan->scan_type.realbits > 16)
			sample = be32_to_cpu(st->scan.data.sample_buf32_be);
		else
			sample = be16_to_cpu(st->scan.data.sample_buf16_be);
	} else {
		if (chan->scan_type.realbits > 16)
			sample = st->scan.data.sample_buf32;
		else
			sample = st->scan.data.sample_buf16;
	}

	sample >>= chan->scan_type.shift;

	if (chan->scan_type.sign == 's')
		*val = sign_extend32(sample, chan->scan_type.realbits - 1);
	else
		*val = sample;

	return IIO_VAL_INT;
}

static int ad4000_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long info)
{
	struct ad4000_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad4000_single_conversion(indio_dev, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		*val = st->scale_tbl[st->span_comp][0];
		*val2 = st->scale_tbl[st->span_comp][1];
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		*val = 0;
		if (st->span_comp)
			*val = mult_frac(st->vref_mv, 1, 10);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->offload_trigger_hz;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad4000_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	struct ad4000_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)st->scale_tbl;
		*length = AD4000_SCALE_OPTIONS * 2;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ad4000_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int __ad4000_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val2)
{
	struct ad4000_state *st = iio_priv(indio_dev);
	unsigned int reg_val;
	bool span_comp_en;
	int ret;

	guard(mutex)(&st->lock);

	ret = ad4000_read_reg(st, &reg_val);
	if (ret < 0)
		return ret;

	span_comp_en = val2 == st->scale_tbl[1][1];
	reg_val &= ~AD4000_CFG_SPAN_COMP;
	reg_val |= FIELD_PREP(AD4000_CFG_SPAN_COMP, span_comp_en);

	ret = ad4000_write_reg(st, reg_val);
	if (ret < 0)
		return ret;

	st->span_comp = span_comp_en;
	return 0;
}

static int ad4000_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad4000_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = __ad4000_write_raw(indio_dev, chan, val2);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val < 1 || val > st->max_rate_hz)
			return -EINVAL;

		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = ad4000_set_sampling_freq(st, val);
		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static irqreturn_t ad4000_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad4000_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4000_convert_and_acquire(st);
	if (ret < 0)
		goto err_out;

	iio_push_to_buffers_with_ts(indio_dev, &st->scan, sizeof(st->scan),
				    pf->timestamp);

err_out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static const struct iio_info ad4000_reg_access_info = {
	.read_raw = &ad4000_read_raw,
	.read_avail = &ad4000_read_avail,
	.write_raw = &ad4000_write_raw,
	.write_raw_get_fmt = &ad4000_write_raw_get_fmt,
};

static const struct iio_info ad4000_offload_info = {
	.read_raw = &ad4000_read_raw,
	.write_raw = &ad4000_write_raw,
	.write_raw_get_fmt = &ad4000_write_raw_get_fmt,
};

static const struct iio_info ad4000_info = {
	.read_raw = &ad4000_read_raw,
};

static int ad4000_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4000_state *st = iio_priv(indio_dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
		.periodic = {
			.frequency_hz = st->offload_trigger_hz,
		},
	};

	return spi_offload_trigger_enable(st->offload, st->offload_trigger,
					  &config);
}

static int ad4000_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4000_state *st = iio_priv(indio_dev);

	spi_offload_trigger_disable(st->offload, st->offload_trigger);

	return 0;
}

static const struct iio_buffer_setup_ops ad4000_offload_buffer_setup_ops = {
	.postenable = &ad4000_offload_buffer_postenable,
	.predisable = &ad4000_offload_buffer_predisable,
};

static int ad4000_spi_offload_setup(struct iio_dev *indio_dev,
				    struct ad4000_state *st)
{
	struct spi_device *spi = st->spi;
	struct device *dev = &spi->dev;
	struct dma_chan *rx_dma;
	int ret;

	st->offload_trigger = devm_spi_offload_trigger_get(dev, st->offload,
							   SPI_OFFLOAD_TRIGGER_PERIODIC);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
				     "Failed to get offload trigger\n");

	ret = ad4000_set_sampling_freq(st, st->max_rate_hz);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to set sampling frequency\n");

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "Failed to get offload RX DMA\n");

	ret = devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev, rx_dma,
							  IIO_BUFFER_DIRECTION_IN);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to setup DMA buffer\n");

	return 0;
}

/*
 * This executes a data sample transfer when using SPI offloading. The device
 * connections should be in "3-wire" mode, selected either when the adi,sdi-pin
 * device tree property is absent or set to "high". Also, the ADC CNV pin must
 * be connected to a SPI controller CS (it can't be connected to a GPIO).
 *
 * In order to achieve the maximum sample rate, we only do one transfer per
 * SPI offload trigger. Because the ADC output has a one sample latency (delay)
 * when the device is wired in "3-wire" mode and only one transfer per sample is
 * being made in turbo mode, the first data sample is not valid because it
 * contains the output of an earlier conversion result. We also set transfer
 * `bits_per_word` to achieve higher throughput by using the minimum number of
 * SCLK cycles. Also, a delay is added to make sure we meet the minimum quiet
 * time before releasing the CS line.
 *
 * Note that, with `bits_per_word` set to the number of ADC precision bits,
 * transfers use larger word sizes that get stored in 'in-memory wordsizes' that
 * are always in native CPU byte order. Because of that, IIO buffer elements
 * ought to be read in CPU endianness which requires setting IIO scan_type
 * endianness accordingly (i.e. IIO_CPU).
 */
static int ad4000_prepare_offload_message(struct ad4000_state *st,
					  const struct iio_chan_spec *chan)
{
	struct spi_transfer *xfer = &st->offload_xfer;

	xfer->bits_per_word = chan->scan_type.realbits;
	xfer->len = chan->scan_type.realbits > 16 ? 4 : 2;
	xfer->delay.value = st->time_spec->t_quiet2_ns;
	xfer->delay.unit = SPI_DELAY_UNIT_NSECS;
	xfer->offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;

	spi_message_init_with_transfers(&st->offload_msg, xfer, 1);
	st->offload_msg.offload = st->offload;

	return devm_spi_optimize_message(&st->spi->dev, st->spi, &st->offload_msg);
}

/*
 * This executes a data sample transfer for when the device connections are
 * in "3-wire" mode, selected when the adi,sdi-pin device tree property is
 * absent or set to "high". In this connection mode, the ADC SDI pin is
 * connected to MOSI or to VIO and ADC CNV pin is connected either to a SPI
 * controller CS or to a GPIO.
 * AD4000 series of devices initiate conversions on the rising edge of CNV pin.
 *
 * If the CNV pin is connected to an SPI controller CS line (which is by default
 * active low), the ADC readings would have a latency (delay) of one read.
 * Moreover, since we also do ADC sampling for filling the buffer on triggered
 * buffer mode, the timestamps of buffer readings would be disarranged.
 * To prevent the read latency and reduce the time discrepancy between the
 * sample read request and the time of actual sampling by the ADC, do a
 * preparatory transfer to pulse the CS/CNV line.
 */
static int ad4000_prepare_3wire_mode_message(struct ad4000_state *st,
					     const struct iio_chan_spec *chan)
{
	struct spi_transfer *xfers = st->xfers;

	xfers[0].cs_change = 1;
	xfers[0].cs_change_delay.value = st->time_spec->t_conv_ns;
	xfers[0].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	xfers[1].rx_buf = &st->scan.data;
	xfers[1].len = chan->scan_type.realbits > 16 ? 4 : 2;

	/*
	 * If the device is set up for SPI offloading, IIO channel scan_type is
	 * set to IIO_CPU. When that is the case, use larger SPI word sizes for
	 * single-shot reads too. Thus, sample data can be correctly handled in
	 * ad4000_single_conversion() according to scan_type endianness.
	 */
	if (chan->scan_type.endianness != IIO_BE)
		xfers[1].bits_per_word = chan->scan_type.realbits;
	xfers[1].delay.value = st->time_spec->t_quiet2_ns;
	xfers[1].delay.unit = SPI_DELAY_UNIT_NSECS;

	spi_message_init_with_transfers(&st->msg, st->xfers, 2);

	return devm_spi_optimize_message(&st->spi->dev, st->spi, &st->msg);
}

/*
 * This executes a data sample transfer for when the device connections are
 * in "4-wire" mode, selected when the adi,sdi-pin device tree property is
 * set to "cs". In this connection mode, the controller CS pin is connected to
 * ADC SDI pin and a GPIO is connected to ADC CNV pin.
 * The GPIO connected to ADC CNV pin is set outside of the SPI transfer.
 */
static int ad4000_prepare_4wire_mode_message(struct ad4000_state *st,
					     const struct iio_chan_spec *chan)
{
	struct spi_transfer *xfers = st->xfers;

	/*
	 * Dummy transfer to cause enough delay between CNV going high and SDI
	 * going low.
	 */
	xfers[0].cs_off = 1;
	xfers[0].delay.value = st->time_spec->t_conv_ns;
	xfers[0].delay.unit = SPI_DELAY_UNIT_NSECS;

	xfers[1].rx_buf = &st->scan.data;
	xfers[1].len = BITS_TO_BYTES(chan->scan_type.storagebits);

	spi_message_init_with_transfers(&st->msg, st->xfers, 2);

	return devm_spi_optimize_message(&st->spi->dev, st->spi, &st->msg);
}

static int ad4000_config(struct ad4000_state *st)
{
	unsigned int reg_val = AD4000_CONFIG_REG_DEFAULT;

	if (device_property_present(&st->spi->dev, "adi,high-z-input"))
		reg_val |= FIELD_PREP(AD4000_CFG_HIGHZ, 1);

	if (st->using_offload)
		reg_val |= FIELD_PREP(AD4000_CFG_TURBO, 1);

	return ad4000_write_reg(st, reg_val);
}

static int ad4000_probe(struct spi_device *spi)
{
	const struct ad4000_chip_info *chip;
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad4000_state *st;
	int gain_idx, ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	chip = spi_get_device_match_data(spi);
	if (!chip)
		return -EINVAL;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->time_spec = chip->time_spec;
	st->max_rate_hz = chip->max_rate_hz;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad4000_power_supplies),
					     ad4000_power_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable power supplies\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get ref regulator reference\n");
	st->vref_mv = ret / 1000;

	st->cnv_gpio = devm_gpiod_get_optional(dev, "cnv", GPIOD_OUT_HIGH);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "Failed to get CNV GPIO");

	st->offload = devm_spi_offload_get(dev, spi, &ad4000_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get offload\n");

	st->using_offload = !IS_ERR(st->offload);
	if (st->using_offload) {
		indio_dev->setup_ops = &ad4000_offload_buffer_setup_ops;
		ret = ad4000_spi_offload_setup(indio_dev, st);
		if (ret)
			return ret;
	} else {
		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      &iio_pollfunc_store_time,
						      &ad4000_trigger_handler,
						      NULL);
		if (ret)
			return ret;
	}

	ret = device_property_match_property_string(dev, "adi,sdi-pin",
						    ad4000_sdi_pin,
						    ARRAY_SIZE(ad4000_sdi_pin));
	if (ret < 0 && ret != -EINVAL)
		return dev_err_probe(dev, ret,
				     "getting adi,sdi-pin property failed\n");

	/* Default to usual SPI connections if pin properties are not present */
	st->sdi_pin = ret == -EINVAL ? AD4000_SDI_MOSI : ret;
	switch (st->sdi_pin) {
	case AD4000_SDI_MOSI:
		indio_dev->info = &ad4000_reg_access_info;

		/*
		 * In "3-wire mode", the ADC SDI line must be kept high when
		 * data is not being clocked out of the controller.
		 * Request the SPI controller to make MOSI idle high.
		 */
		spi->mode |= SPI_MOSI_IDLE_HIGH;
		ret = spi_setup(spi);
		if (ret < 0)
			return ret;

		if (st->using_offload) {
			indio_dev->channels = &chip->reg_access_offload_chan_spec;
			indio_dev->num_channels = 1;
			ret = ad4000_prepare_offload_message(st, indio_dev->channels);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to optimize SPI msg\n");
		} else {
			indio_dev->channels = chip->reg_access_chan_spec;
			indio_dev->num_channels = ARRAY_SIZE(chip->reg_access_chan_spec);
		}

		/*
		 * Call ad4000_prepare_3wire_mode_message() so single-shot read
		 * SPI messages are always initialized.
		 */
		ret = ad4000_prepare_3wire_mode_message(st, &indio_dev->channels[0]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to optimize SPI msg\n");

		ret = ad4000_config(st);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Failed to config device\n");

		break;
	case AD4000_SDI_VIO:
		if (st->using_offload) {
			indio_dev->info = &ad4000_offload_info;
			indio_dev->channels = &chip->offload_chan_spec;
			indio_dev->num_channels = 1;

			ret = ad4000_prepare_offload_message(st, indio_dev->channels);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to optimize SPI msg\n");
		} else {
			indio_dev->info = &ad4000_info;
			indio_dev->channels = chip->chan_spec;
			indio_dev->num_channels = ARRAY_SIZE(chip->chan_spec);
		}

		ret = ad4000_prepare_3wire_mode_message(st, &indio_dev->channels[0]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to optimize SPI msg\n");

		break;
	case AD4000_SDI_CS:
		if (st->using_offload)
			return dev_err_probe(dev, -EPROTONOSUPPORT,
					     "Unsupported sdi-pin + offload config\n");
		indio_dev->info = &ad4000_info;
		indio_dev->channels = chip->chan_spec;
		indio_dev->num_channels = ARRAY_SIZE(chip->chan_spec);
		ret = ad4000_prepare_4wire_mode_message(st, &indio_dev->channels[0]);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to optimize SPI msg\n");

		break;
	case AD4000_SDI_GND:
		return dev_err_probe(dev, -EPROTONOSUPPORT,
				     "Unsupported connection mode\n");

	default:
		return dev_err_probe(dev, -EINVAL, "Unrecognized connection mode\n");
	}

	indio_dev->name = chip->dev_name;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->gain_milli = 1000;
	if (chip->has_hardware_gain) {
		ret = device_property_read_u16(dev, "adi,gain-milli",
					       &st->gain_milli);
		if (!ret) {
			/* Match gain value from dt to one of supported gains */
			gain_idx = find_closest(st->gain_milli, ad4000_gains,
						ARRAY_SIZE(ad4000_gains));
			st->gain_milli = ad4000_gains[gain_idx];
		} else {
			return dev_err_probe(dev, ret,
					     "Failed to read gain property\n");
		}
	}

	ad4000_fill_scale_tbl(st, &indio_dev->channels[0]);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id ad4000_id[] = {
	{ "ad4000", (kernel_ulong_t)&ad4000_chip_info },
	{ "ad4001", (kernel_ulong_t)&ad4001_chip_info },
	{ "ad4002", (kernel_ulong_t)&ad4002_chip_info },
	{ "ad4003", (kernel_ulong_t)&ad4003_chip_info },
	{ "ad4004", (kernel_ulong_t)&ad4004_chip_info },
	{ "ad4005", (kernel_ulong_t)&ad4005_chip_info },
	{ "ad4006", (kernel_ulong_t)&ad4006_chip_info },
	{ "ad4007", (kernel_ulong_t)&ad4007_chip_info },
	{ "ad4008", (kernel_ulong_t)&ad4008_chip_info },
	{ "ad4010", (kernel_ulong_t)&ad4010_chip_info },
	{ "ad4011", (kernel_ulong_t)&ad4011_chip_info },
	{ "ad4020", (kernel_ulong_t)&ad4020_chip_info },
	{ "ad4021", (kernel_ulong_t)&ad4021_chip_info },
	{ "ad4022", (kernel_ulong_t)&ad4022_chip_info },
	{ "adaq4001", (kernel_ulong_t)&adaq4001_chip_info },
	{ "adaq4003", (kernel_ulong_t)&adaq4003_chip_info },
	{ "ad7685", (kernel_ulong_t)&ad7685_chip_info },
	{ "ad7686", (kernel_ulong_t)&ad7686_chip_info },
	{ "ad7687", (kernel_ulong_t)&ad7687_chip_info },
	{ "ad7688", (kernel_ulong_t)&ad7688_chip_info },
	{ "ad7690", (kernel_ulong_t)&ad7690_chip_info },
	{ "ad7691", (kernel_ulong_t)&ad7691_chip_info },
	{ "ad7693", (kernel_ulong_t)&ad7693_chip_info },
	{ "ad7942", (kernel_ulong_t)&ad7942_chip_info },
	{ "ad7946", (kernel_ulong_t)&ad7946_chip_info },
	{ "ad7980", (kernel_ulong_t)&ad7980_chip_info },
	{ "ad7982", (kernel_ulong_t)&ad7982_chip_info },
	{ "ad7983", (kernel_ulong_t)&ad7983_chip_info },
	{ "ad7984", (kernel_ulong_t)&ad7984_chip_info },
	{ "ad7988-1", (kernel_ulong_t)&ad7988_1_chip_info },
	{ "ad7988-5", (kernel_ulong_t)&ad7988_5_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4000_id);

static const struct of_device_id ad4000_of_match[] = {
	{ .compatible = "adi,ad4000", .data = &ad4000_chip_info },
	{ .compatible = "adi,ad4001", .data = &ad4001_chip_info },
	{ .compatible = "adi,ad4002", .data = &ad4002_chip_info },
	{ .compatible = "adi,ad4003", .data = &ad4003_chip_info },
	{ .compatible = "adi,ad4004", .data = &ad4004_chip_info },
	{ .compatible = "adi,ad4005", .data = &ad4005_chip_info },
	{ .compatible = "adi,ad4006", .data = &ad4006_chip_info },
	{ .compatible = "adi,ad4007", .data = &ad4007_chip_info },
	{ .compatible = "adi,ad4008", .data = &ad4008_chip_info },
	{ .compatible = "adi,ad4010", .data = &ad4010_chip_info },
	{ .compatible = "adi,ad4011", .data = &ad4011_chip_info },
	{ .compatible = "adi,ad4020", .data = &ad4020_chip_info },
	{ .compatible = "adi,ad4021", .data = &ad4021_chip_info },
	{ .compatible = "adi,ad4022", .data = &ad4022_chip_info },
	{ .compatible = "adi,adaq4001", .data = &adaq4001_chip_info },
	{ .compatible = "adi,adaq4003", .data = &adaq4003_chip_info },
	{ .compatible = "adi,ad7685", .data = &ad7685_chip_info },
	{ .compatible = "adi,ad7686", .data = &ad7686_chip_info },
	{ .compatible = "adi,ad7687", .data = &ad7687_chip_info },
	{ .compatible = "adi,ad7688", .data = &ad7688_chip_info },
	{ .compatible = "adi,ad7690", .data = &ad7690_chip_info },
	{ .compatible = "adi,ad7691", .data = &ad7691_chip_info },
	{ .compatible = "adi,ad7693", .data = &ad7693_chip_info },
	{ .compatible = "adi,ad7942", .data = &ad7942_chip_info },
	{ .compatible = "adi,ad7946", .data = &ad7946_chip_info },
	{ .compatible = "adi,ad7980", .data = &ad7980_chip_info },
	{ .compatible = "adi,ad7982", .data = &ad7982_chip_info },
	{ .compatible = "adi,ad7983", .data = &ad7983_chip_info },
	{ .compatible = "adi,ad7984", .data = &ad7984_chip_info },
	{ .compatible = "adi,ad7988-1", .data = &ad7988_1_chip_info },
	{ .compatible = "adi,ad7988-5", .data = &ad7988_5_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4000_of_match);

static struct spi_driver ad4000_driver = {
	.driver = {
		.name   = "ad4000",
		.of_match_table = ad4000_of_match,
	},
	.probe          = ad4000_probe,
	.id_table       = ad4000_id,
};
module_spi_driver(ad4000_driver);

MODULE_AUTHOR("Marcelo Schmitt <marcelo.schmitt@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4000 ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
