// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024-2026 Analog Devices, Inc.
 * Author: Radu Sabau <radu.sabau@analog.com>
 */
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kstrtox.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/string.h>
#include <linux/spi/spi.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/provider.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/unaligned.h>

#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dma.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define AD4691_VREF_uV_MIN			2400000
#define AD4691_VREF_uV_MAX			5250000
#define AD4691_VREF_2P5_uV_MAX			2750000
#define AD4691_VREF_3P0_uV_MAX			3250000
#define AD4691_VREF_3P3_uV_MAX			3750000
#define AD4691_VREF_4P096_uV_MAX		4500000

#define AD4691_CNV_DUTY_CYCLE_NS		380
#define AD4691_CNV_HIGH_TIME_NS			430
/*
 * Conservative default for the manual offload periodic trigger. Low enough
 * to work safely out of the box across all OSR and channel count combinations.
 */
#define AD4691_OFFLOAD_INITIAL_TRIGGER_HZ	(100 * HZ_PER_KHZ)

#define AD4691_SPI_CONFIG_A_REG			0x000
#define AD4691_SW_RESET				(BIT(7) | BIT(0))

#define AD4691_STATUS_REG			0x014
#define AD4691_CLAMP_STATUS1_REG		0x01A
#define AD4691_CLAMP_STATUS2_REG		0x01B
#define AD4691_DEVICE_SETUP			0x020
#define AD4691_MANUAL_MODE			BIT(2)
#define AD4691_LDO_EN				BIT(4)
#define AD4691_REF_CTRL				0x021
#define AD4691_REF_CTRL_MASK			GENMASK(4, 2)
#define AD4691_REFBUF_EN			BIT(0)
#define AD4691_OSC_FREQ_REG			0x023
#define AD4691_OSC_FREQ_MASK			GENMASK(3, 0)
#define AD4691_STD_SEQ_CONFIG			0x025
#define AD4691_SEQ_ALL_CHANNELS_OFF		0x00
#define AD4691_SPARE_CONTROL			0x02A

#define AD4691_MAX_CHANNELS			16

#define AD4691_NOOP				0x00
#define AD4691_ADC_CHAN(ch)			((0x10 + (ch)) << 3)
#define AD4691_EXIT_COMMAND			0x5000

#define AD4691_OSC_EN_REG			0x180
#define AD4691_STATE_RESET_REG			0x181
#define AD4691_STATE_RESET_ALL			BIT(0)
#define AD4691_ADC_SETUP			0x182
#define AD4691_ADC_MODE_MASK			GENMASK(1, 0)
#define AD4691_CNV_BURST_MODE			0x01
#define AD4691_AUTONOMOUS_MODE			0x02
/*
 * ACC_MASK_REG covers both mask bytes via ADDR_DESCENDING SPI: writing a
 * 16-bit BE value to 0x185 auto-decrements to 0x184 for the second byte.
 */
#define AD4691_ACC_MASK_REG			0x185
#define AD4691_ACC_DEPTH_IN(n)			(0x186 + (n))
#define AD4691_GPIO_MODE1_REG			0x196
#define AD4691_GPIO_MODE2_REG			0x197
#define AD4691_GP_MODE_MASK			GENMASK(3, 0)
#define AD4691_GP_MODE_DATA_READY		0x06
#define AD4691_GPIO_READ			0x1A0
#define AD4691_ACC_STATUS_FULL1_REG		0x1B0
#define AD4691_ACC_STATUS_FULL2_REG		0x1B1
#define AD4691_ACC_STATUS_OVERRUN1_REG		0x1B2
#define AD4691_ACC_STATUS_OVERRUN2_REG		0x1B3
#define AD4691_ACC_STATUS_SAT1_REG		0x1B4
#define AD4691_ACC_STATUS_SAT2_REG		0x1BE
#define AD4691_ACC_SAT_OVR_REG(n)		(0x1C0 + (n))
#define AD4691_AVG_IN(n)			(0x201 + (2 * (n)))
#define AD4691_AVG_STS_IN(n)			(0x222 + (3 * (n)))
#define AD4691_ACC_IN(n)			(0x252 + (3 * (n)))
#define AD4691_ACC_STS_DATA(n)			(0x283 + (4 * (n)))


static const char * const ad4691_supplies[] = { "avdd", "vio" };

enum ad4691_ref_ctrl {
	AD4691_VREF_2P5,
	AD4691_VREF_3P0,
	AD4691_VREF_3P3,
	AD4691_VREF_4P096,
	AD4691_VREF_5P0
};

struct ad4691_channel_info {
	const struct iio_chan_spec *channels __counted_by_ptr(num_channels);
	const struct iio_chan_spec *manual_channels __counted_by_ptr(num_channels);
	unsigned int num_channels;
};

struct ad4691_chip_info {
	const char *name;
	unsigned int max_rate;
	const struct ad4691_channel_info *sw_info;
	const struct ad4691_channel_info *offload_info;
};

/* CNV burst mode channel — exposes oversampling ratio. */
#define AD4691_CHANNEL(ch)						\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE)	\
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)	\
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.info_mask_shared_by_all_available =			\
				      BIT(IIO_CHAN_INFO_SAMP_FREQ)	\
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.channel = ch,						\
		.scan_index = ch,					\
		.scan_type = {						\
			.format = 'u',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

/*
 * Manual mode channel — no oversampling ratio attribute. OSR is not
 * supported in manual mode; ACC_DEPTH_IN is not configured during manual
 * buffer enable.
 */
#define AD4691_MANUAL_CHANNEL(ch)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE)	\
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.info_mask_shared_by_all_available =			\
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.channel = ch,						\
		.scan_index = ch,					\
		.scan_type = {						\
			.format = 'u',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

/*
 * Offload path (bits_per_word=16): the SPI Engine assembles received
 * bits into native 16-bit words before DMA, so samples are in
 * CPU-native byte order (IIO_CPU). storagebits=16 matches the 16-bit
 * DMA word size.
 *
 * CNV burst offload configures ACC_DEPTH_IN per channel, so the
 * oversampling_ratio attribute is exposed. Manual offload does not;
 * use AD4691_OFFLOAD_MANUAL_CHANNEL for that path.
 */
#define AD4691_OFFLOAD_CHANNEL(ch)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE)	\
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ)	\
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.info_mask_shared_by_all_available =			\
				      BIT(IIO_CHAN_INFO_SAMP_FREQ)	\
				    | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.channel = ch,						\
		.scan_index = ch,					\
		.scan_type = {						\
			.format = 'u',					\
			.realbits = 16,					\
			.storagebits = 16,				\
		},							\
	}

/* Manual offload — same IIO_CPU layout but no oversampling_ratio attribute. */
#define AD4691_OFFLOAD_MANUAL_CHANNEL(ch)				\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE)	\
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.info_mask_shared_by_all_available =			\
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.channel = ch,						\
		.scan_index = ch,					\
		.scan_type = {						\
			.format = 'u',					\
			.realbits = 16,					\
			.storagebits = 16,				\
		},							\
	}

static const struct iio_chan_spec ad4691_channels[] = {
	AD4691_CHANNEL(0),
	AD4691_CHANNEL(1),
	AD4691_CHANNEL(2),
	AD4691_CHANNEL(3),
	AD4691_CHANNEL(4),
	AD4691_CHANNEL(5),
	AD4691_CHANNEL(6),
	AD4691_CHANNEL(7),
	AD4691_CHANNEL(8),
	AD4691_CHANNEL(9),
	AD4691_CHANNEL(10),
	AD4691_CHANNEL(11),
	AD4691_CHANNEL(12),
	AD4691_CHANNEL(13),
	AD4691_CHANNEL(14),
	AD4691_CHANNEL(15),
	IIO_CHAN_SOFT_TIMESTAMP(16),
};

static const struct iio_chan_spec ad4693_channels[] = {
	AD4691_CHANNEL(0),
	AD4691_CHANNEL(1),
	AD4691_CHANNEL(2),
	AD4691_CHANNEL(3),
	AD4691_CHANNEL(4),
	AD4691_CHANNEL(5),
	AD4691_CHANNEL(6),
	AD4691_CHANNEL(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

/*
 * Offload channel arrays: no IIO_CHAN_SOFT_TIMESTAMP because DMA delivers
 * data directly to userspace without a software timestamp.
 */
static const struct iio_chan_spec ad4691_offload_channels[] = {
	AD4691_OFFLOAD_CHANNEL(0),
	AD4691_OFFLOAD_CHANNEL(1),
	AD4691_OFFLOAD_CHANNEL(2),
	AD4691_OFFLOAD_CHANNEL(3),
	AD4691_OFFLOAD_CHANNEL(4),
	AD4691_OFFLOAD_CHANNEL(5),
	AD4691_OFFLOAD_CHANNEL(6),
	AD4691_OFFLOAD_CHANNEL(7),
	AD4691_OFFLOAD_CHANNEL(8),
	AD4691_OFFLOAD_CHANNEL(9),
	AD4691_OFFLOAD_CHANNEL(10),
	AD4691_OFFLOAD_CHANNEL(11),
	AD4691_OFFLOAD_CHANNEL(12),
	AD4691_OFFLOAD_CHANNEL(13),
	AD4691_OFFLOAD_CHANNEL(14),
	AD4691_OFFLOAD_CHANNEL(15),
};

static const struct iio_chan_spec ad4693_offload_channels[] = {
	AD4691_OFFLOAD_CHANNEL(0),
	AD4691_OFFLOAD_CHANNEL(1),
	AD4691_OFFLOAD_CHANNEL(2),
	AD4691_OFFLOAD_CHANNEL(3),
	AD4691_OFFLOAD_CHANNEL(4),
	AD4691_OFFLOAD_CHANNEL(5),
	AD4691_OFFLOAD_CHANNEL(6),
	AD4691_OFFLOAD_CHANNEL(7),
};

static const struct iio_chan_spec ad4691_manual_channels[] = {
	AD4691_MANUAL_CHANNEL(0),
	AD4691_MANUAL_CHANNEL(1),
	AD4691_MANUAL_CHANNEL(2),
	AD4691_MANUAL_CHANNEL(3),
	AD4691_MANUAL_CHANNEL(4),
	AD4691_MANUAL_CHANNEL(5),
	AD4691_MANUAL_CHANNEL(6),
	AD4691_MANUAL_CHANNEL(7),
	AD4691_MANUAL_CHANNEL(8),
	AD4691_MANUAL_CHANNEL(9),
	AD4691_MANUAL_CHANNEL(10),
	AD4691_MANUAL_CHANNEL(11),
	AD4691_MANUAL_CHANNEL(12),
	AD4691_MANUAL_CHANNEL(13),
	AD4691_MANUAL_CHANNEL(14),
	AD4691_MANUAL_CHANNEL(15),
	IIO_CHAN_SOFT_TIMESTAMP(16),
};

static const struct iio_chan_spec ad4693_manual_channels[] = {
	AD4691_MANUAL_CHANNEL(0),
	AD4691_MANUAL_CHANNEL(1),
	AD4691_MANUAL_CHANNEL(2),
	AD4691_MANUAL_CHANNEL(3),
	AD4691_MANUAL_CHANNEL(4),
	AD4691_MANUAL_CHANNEL(5),
	AD4691_MANUAL_CHANNEL(6),
	AD4691_MANUAL_CHANNEL(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static const struct iio_chan_spec ad4691_offload_manual_channels[] = {
	AD4691_OFFLOAD_MANUAL_CHANNEL(0),
	AD4691_OFFLOAD_MANUAL_CHANNEL(1),
	AD4691_OFFLOAD_MANUAL_CHANNEL(2),
	AD4691_OFFLOAD_MANUAL_CHANNEL(3),
	AD4691_OFFLOAD_MANUAL_CHANNEL(4),
	AD4691_OFFLOAD_MANUAL_CHANNEL(5),
	AD4691_OFFLOAD_MANUAL_CHANNEL(6),
	AD4691_OFFLOAD_MANUAL_CHANNEL(7),
	AD4691_OFFLOAD_MANUAL_CHANNEL(8),
	AD4691_OFFLOAD_MANUAL_CHANNEL(9),
	AD4691_OFFLOAD_MANUAL_CHANNEL(10),
	AD4691_OFFLOAD_MANUAL_CHANNEL(11),
	AD4691_OFFLOAD_MANUAL_CHANNEL(12),
	AD4691_OFFLOAD_MANUAL_CHANNEL(13),
	AD4691_OFFLOAD_MANUAL_CHANNEL(14),
	AD4691_OFFLOAD_MANUAL_CHANNEL(15),
};

static const struct iio_chan_spec ad4693_offload_manual_channels[] = {
	AD4691_OFFLOAD_MANUAL_CHANNEL(0),
	AD4691_OFFLOAD_MANUAL_CHANNEL(1),
	AD4691_OFFLOAD_MANUAL_CHANNEL(2),
	AD4691_OFFLOAD_MANUAL_CHANNEL(3),
	AD4691_OFFLOAD_MANUAL_CHANNEL(4),
	AD4691_OFFLOAD_MANUAL_CHANNEL(5),
	AD4691_OFFLOAD_MANUAL_CHANNEL(6),
	AD4691_OFFLOAD_MANUAL_CHANNEL(7),
};

static const int ad4691_oversampling_ratios[] = { 1, 2, 4, 8, 16, 32 };

static const struct ad4691_channel_info ad4691_sw_info = {
	.channels = ad4691_channels,
	.manual_channels = ad4691_manual_channels,
	.num_channels = ARRAY_SIZE(ad4691_channels),
};

static const struct ad4691_channel_info ad4693_sw_info = {
	.channels = ad4693_channels,
	.manual_channels = ad4693_manual_channels,
	.num_channels = ARRAY_SIZE(ad4693_channels),
};

static const struct ad4691_channel_info ad4691_offload_info = {
	.channels = ad4691_offload_channels,
	.manual_channels = ad4691_offload_manual_channels,
	.num_channels = ARRAY_SIZE(ad4691_offload_channels),
};

static const struct ad4691_channel_info ad4693_offload_info = {
	.channels = ad4693_offload_channels,
	.manual_channels = ad4693_offload_manual_channels,
	.num_channels = ARRAY_SIZE(ad4693_offload_channels),
};

/*
 * Internal oscillator frequency table. Index is the OSC_FREQ_REG[3:0] value.
 * Index 0 (1 MHz) is only valid for AD4692/AD4694; AD4691/AD4693 support
 * up to 500 kHz and use index 1 as their highest valid rate.
 */
static const int ad4691_osc_freqs_Hz[] = {
	[0x0] = 1000000,
	[0x1] = 500000,
	[0x2] = 400000,
	[0x3] = 250000,
	[0x4] = 200000,
	[0x5] = 167000,
	[0x6] = 133000,
	[0x7] = 125000,
	[0x8] = 100000,
	[0x9] = 50000,
	[0xA] = 25000,
	[0xB] = 12500,
	[0xC] = 10000,
	[0xD] = 5000,
	[0xE] = 2500,
	[0xF] = 1250,
};

static const char * const ad4691_gp_names[] = { "gp0", "gp1", "gp2", "gp3" };

static const struct ad4691_chip_info ad4691_chip_info = {
	.name = "ad4691",
	.max_rate = 500 * HZ_PER_KHZ,
	.sw_info = &ad4691_sw_info,
	.offload_info = &ad4691_offload_info,
};

static const struct ad4691_chip_info ad4692_chip_info = {
	.name = "ad4692",
	.max_rate = 1 * HZ_PER_MHZ,
	.sw_info = &ad4691_sw_info,
	.offload_info = &ad4691_offload_info,
};

static const struct ad4691_chip_info ad4693_chip_info = {
	.name = "ad4693",
	.max_rate = 500 * HZ_PER_KHZ,
	.sw_info = &ad4693_sw_info,
	.offload_info = &ad4693_offload_info,
};

static const struct ad4691_chip_info ad4694_chip_info = {
	.name = "ad4694",
	.max_rate = 1 * HZ_PER_MHZ,
	.sw_info = &ad4693_sw_info,
	.offload_info = &ad4693_offload_info,
};

struct ad4691_state {
	const struct ad4691_chip_info *info;
	struct regmap *regmap;
	struct spi_device *spi;

	struct pwm_device *conv_trigger;
	int irq;
	int vref_uV;
	u32 cnv_period_ns;
	/*
	 * Snapped oscillator frequency (Hz) shared by all channels. Set when
	 * sampling_frequency or oversampling_ratio is written; written to
	 * OSC_FREQ_REG at buffer enable and single-shot time so both attributes
	 * can be set in any order. Reading in_voltage_sampling_frequency
	 * returns target_osc_freq_Hz / osr — the effective rate given the
	 * shared oversampling ratio.
	 */
	u32 target_osc_freq_Hz;
	/* Shared oversampling ratio across all channels; always 1 in manual mode. */
	unsigned int osr;
	/*
	 * Precomputed effective-rate lists, one row per entry in
	 * ad4691_oversampling_ratios[]. Populated at probe; read_avail picks
	 * the row for the current shared OSR. The tables are stable after
	 * probe so returning a pointer into them from read_avail is race-free.
	 */
	int samp_freq_avail[ARRAY_SIZE(ad4691_oversampling_ratios)][ARRAY_SIZE(ad4691_osc_freqs_Hz)];
	int samp_freq_avail_len[ARRAY_SIZE(ad4691_oversampling_ratios)];

	bool manual_mode;
	bool irq_enabled;
	bool refbuf_en;
	bool ldo_en;
	/*
	 * Synchronize access to members of the driver state, and ensure
	 * atomicity of consecutive SPI operations.
	 */
	struct mutex lock;
	/* NULL when no SPI offload hardware is present. */
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	u64 trigger_hz;
	/*
	 * Per-buffer-enable lifetime resources:
	 * Manual Mode - a pre-built SPI message that clocks out N+1
	 *		 transfers in one go.
	 * CNV Burst Mode - a pre-built SPI message that clocks out 2*N
	 *		    transfers in one go.
	 */
	struct spi_message scan_msg;
	/*
	 * max 16 + 1 NOOP (manual) or 2*16 + 1 state-reset (CNV burst).
	 */
	struct spi_transfer scan_xfers[34];
	/*
	 * CNV burst: 16 AVG_IN addresses = 16.  Manual: 16 channel cmds +
	 * 1 NOOP = 17.  Stored as native u16.  The non-offload path fills slots
	 * with put_unaligned_be16() (bits_per_word=8, bytes go out in memory
	 * order).  The offload path assigns native values directly
	 * (bits_per_word=bpw, SPI reads each slot as a native 16-bit word and
	 * shifts it out MSB-first).
	 */
	u16 scan_tx[17] __aligned(IIO_DMA_MINALIGN);
	/*
	 * CNV burst state-reset: 4-byte write [addr_hi, addr_lo,
	 * STATE_RESET_ALL, OSC_EN=1]. CS is asserted throughout, so
	 * ADDR_DESCENDING writes byte[3]=1 to OSC_EN_REG (0x180) as a
	 * deliberate side-write, keeping the oscillator enabled. Shared
	 * with the offload path (mutually exclusive at probe).
	 */
	u8 scan_tx_reset[4] __aligned(IIO_DMA_MINALIGN);
	/*
	 * Scan buffer: one BE16 slot per active channel, plus timestamp.
	 * DMA-aligned because scan_xfers point rx_buf directly into vals[].
	 */
	IIO_DECLARE_DMA_BUFFER_WITH_TS(__be16, vals, 16);
};

/*
 * Configure the given GP pin (0-3) as DATA_READY output.
 * GP0/GP1 → GPIO_MODE1_REG, GP2/GP3 → GPIO_MODE2_REG.
 * Even pins occupy bits [3:0], odd pins bits [7:4].
 */
static int ad4691_gpio_setup(struct ad4691_state *st, unsigned int gp_num)
{
	unsigned int bit_off = gp_num % 2;
	unsigned int reg_off = gp_num / 2;
	unsigned int shift = 4 * bit_off;

	return regmap_update_bits(st->regmap,
				  AD4691_GPIO_MODE1_REG + reg_off,
				  AD4691_GP_MODE_MASK << shift,
				  AD4691_GP_MODE_DATA_READY << shift);
}

static const struct spi_offload_config ad4691_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static bool ad4691_offload_trigger_match(struct spi_offload_trigger *trigger,
					 enum spi_offload_trigger_type type,
					 u64 *args, u32 nargs)
{
	return type == SPI_OFFLOAD_TRIGGER_DATA_READY && nargs == 1 && args[0] <= 3;
}

static int ad4691_offload_trigger_request(struct spi_offload_trigger *trigger,
					  enum spi_offload_trigger_type type,
					  u64 *args, u32 nargs)
{
	struct ad4691_state *st = spi_offload_trigger_get_priv(trigger);

	if (nargs != 1 || args[0] > 3)
		return -EINVAL;

	return ad4691_gpio_setup(st, args[0]);
}

static int ad4691_offload_trigger_validate(struct spi_offload_trigger *trigger,
					   struct spi_offload_trigger_config *config)
{
	if (config->type != SPI_OFFLOAD_TRIGGER_DATA_READY)
		return -EINVAL;

	return 0;
}

static const struct spi_offload_trigger_ops ad4691_offload_trigger_ops = {
	.match    = ad4691_offload_trigger_match,
	.request  = ad4691_offload_trigger_request,
	.validate = ad4691_offload_trigger_validate,
};

static int ad4691_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct spi_device *spi = context;
	u8 tx[2], rx[4];
	int ret;

	/* Set bit 15 to mark the operation as READ. */
	put_unaligned_be16(0x8000 | reg, tx);

	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_MASK_REG - 1:
	case AD4691_ACC_MASK_REG + 1 ... AD4691_ACC_SAT_OVR_REG(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 1);
		if (ret)
			return ret;
		*val = rx[0];
		return 0;
	case AD4691_ACC_MASK_REG:
	case AD4691_STD_SEQ_CONFIG:
	case AD4691_AVG_IN(0) ... AD4691_AVG_IN(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 2);
		if (ret)
			return ret;
		*val = get_unaligned_be16(rx);
		return 0;
	case AD4691_AVG_STS_IN(0) ... AD4691_AVG_STS_IN(15):
	case AD4691_ACC_IN(0) ... AD4691_ACC_IN(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 3);
		if (ret)
			return ret;
		*val = get_unaligned_be24(rx);
		return 0;
	case AD4691_ACC_STS_DATA(0) ... AD4691_ACC_STS_DATA(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 4);
		if (ret)
			return ret;
		*val = get_unaligned_be32(rx);
		return 0;
	default:
		return -EINVAL;
	}
}

static int ad4691_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct spi_device *spi = context;
	u8 tx[4];

	put_unaligned_be16(reg, tx);

	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_MASK_REG - 1:
	case AD4691_ACC_MASK_REG + 1 ... AD4691_GPIO_MODE2_REG:
		if (val > U8_MAX)
			return -EINVAL;
		tx[2] = val;
		return spi_write_then_read(spi, tx, 3, NULL, 0);
	case AD4691_ACC_MASK_REG:
	case AD4691_STD_SEQ_CONFIG:
		if (val > U16_MAX)
			return -EINVAL;
		put_unaligned_be16(val, &tx[2]);
		return spi_write_then_read(spi, tx, 4, NULL, 0);
	default:
		return -EINVAL;
	}
}

static bool ad4691_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD4691_STATUS_REG:
	case AD4691_CLAMP_STATUS1_REG:
	case AD4691_CLAMP_STATUS2_REG:
	case AD4691_GPIO_READ:
	case AD4691_ACC_STATUS_FULL1_REG ... AD4691_ACC_STATUS_SAT2_REG:
	case AD4691_ACC_SAT_OVR_REG(0) ... AD4691_ACC_SAT_OVR_REG(15):
	case AD4691_AVG_IN(0) ... AD4691_AVG_IN(15):
	case AD4691_AVG_STS_IN(0) ... AD4691_AVG_STS_IN(15):
	case AD4691_ACC_IN(0) ... AD4691_ACC_IN(15):
	case AD4691_ACC_STS_DATA(0) ... AD4691_ACC_STS_DATA(15):
		return true;
	default:
		return false;
	}
}

static bool ad4691_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_SAT_OVR_REG(15):
	case AD4691_STD_SEQ_CONFIG:
		return true;
	default:
		break;
	}

	/*
	 * Multi-byte result registers have non-unit strides; only the base
	 * address of each entry is a valid single-register read.
	 */
	if (reg >= AD4691_AVG_IN(0) && reg <= AD4691_AVG_IN(15))
		return (reg - AD4691_AVG_IN(0)) % 2 == 0;
	if (reg >= AD4691_AVG_STS_IN(0) && reg <= AD4691_AVG_STS_IN(15))
		return (reg - AD4691_AVG_STS_IN(0)) % 3 == 0;
	if (reg >= AD4691_ACC_IN(0) && reg <= AD4691_ACC_IN(15))
		return (reg - AD4691_ACC_IN(0)) % 3 == 0;
	if (reg >= AD4691_ACC_STS_DATA(0) && reg <= AD4691_ACC_STS_DATA(15))
		return (reg - AD4691_ACC_STS_DATA(0)) % 4 == 0;

	return false;
}

static bool ad4691_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_STD_SEQ_CONFIG:
	case AD4691_SPARE_CONTROL ... AD4691_GPIO_MODE2_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ad4691_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_read = ad4691_reg_read,
	.reg_write = ad4691_reg_write,
	.volatile_reg = ad4691_volatile_reg,
	.readable_reg = ad4691_readable_reg,
	.writeable_reg = ad4691_writeable_reg,
	.max_register = AD4691_ACC_STS_DATA(15),
	.cache_type = REGCACHE_MAPLE,
};

/*
 * Index 0 in ad4691_osc_freqs_Hz is 1 MHz — valid only for AD4692/AD4694
 * (max_rate == 1 MHz). AD4691/AD4693 cap at 500 kHz so their valid range
 * starts at index 1.
 */
static unsigned int ad4691_samp_freq_start(const struct ad4691_chip_info *info)
{
	return (info->max_rate == 1 * HZ_PER_MHZ) ? 0 : 1;
}

/*
 * Find the largest oscillator table entry that is both <= needed_osc and
 * evenly divisible by osr (guaranteeing an integer effective rate on
 * read-back). Returns 0 if no such entry exists in the chip's valid range.
 */
static unsigned int ad4691_find_osc_freq(struct ad4691_state *st,
					 unsigned int needed_osc,
					 unsigned int osr)
{
	unsigned int start = ad4691_samp_freq_start(st->info);

	for (unsigned int i = start; i < ARRAY_SIZE(ad4691_osc_freqs_Hz); i++) {
		if ((unsigned int)ad4691_osc_freqs_Hz[i] > needed_osc)
			continue;
		if (ad4691_osc_freqs_Hz[i] % osr)
			continue;
		return ad4691_osc_freqs_Hz[i];
	}
	return 0;
}

/* Write target_osc_freq_Hz to OSC_FREQ_REG. Called at use time. */
static int ad4691_write_osc_freq(struct ad4691_state *st)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(ad4691_osc_freqs_Hz); i++) {
		if (ad4691_osc_freqs_Hz[i] == st->target_osc_freq_Hz)
			return regmap_write(st->regmap, AD4691_OSC_FREQ_REG, i);
	}
	return -EINVAL;
}

/* Return the index of osr in ad4691_oversampling_ratios[], defaulting to 0. */
static unsigned int ad4691_osr_index(unsigned int osr)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(ad4691_oversampling_ratios) - 1; i++) {
		if ((unsigned int)ad4691_oversampling_ratios[i] == osr)
			return i;
	}
	return ARRAY_SIZE(ad4691_oversampling_ratios) - 1;
}

/*
 * Precompute samp_freq_avail[][]: for each OSR value, list the oscillator
 * table entries that divide evenly by that OSR, expressed as effective rates
 * (osc_freq / osr). Called once at probe after st->info is set.
 */
static void ad4691_precompute_samp_freq_avail(struct ad4691_state *st)
{
	unsigned int start = ad4691_samp_freq_start(st->info);

	for (unsigned int i = 0; i < ARRAY_SIZE(ad4691_oversampling_ratios); i++) {
		unsigned int osr = ad4691_oversampling_ratios[i];
		int n = 0;

		for (unsigned int j = start; j < ARRAY_SIZE(ad4691_osc_freqs_Hz); j++) {
			if (ad4691_osc_freqs_Hz[j] % osr)
				continue;
			st->samp_freq_avail[i][n++] = ad4691_osc_freqs_Hz[j] / osr;
		}
		st->samp_freq_avail_len[i] = n;
	}
}

static int ad4691_set_sampling_freq(struct ad4691_state *st, int freq)
{
	unsigned int osr, found;

	/*
	 * Read osr under st->lock: osr and target_osc_freq_Hz are modified
	 * together under the lock; reading after acquiring it ensures we see
	 * a consistent snapshot with no concurrent write racing us.
	 */
	guard(mutex)(&st->lock);
	osr = st->osr;

	if (freq <= 0 || (unsigned int)freq > st->info->max_rate / osr)
		return -EINVAL;

	found = ad4691_find_osc_freq(st, (unsigned int)freq * osr, osr);
	if (!found)
		return -EINVAL;

	/*
	 * Store the snapped oscillator frequency; OSC_FREQ_REG is written at
	 * buffer enable and single-shot time so that sampling_frequency and
	 * oversampling_ratio can be set in any order.
	 */
	st->target_osc_freq_Hz = found;
	return 0;
}

static int ad4691_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type,
			     int *length, long mask)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		unsigned int osr_idx;

		/*
		 * The precomputed tables are stable after probe; only the
		 * current OSR needs to be read under the lock to pick the
		 * right row atomically.
		 */
		guard(mutex)(&st->lock);
		osr_idx = ad4691_osr_index(st->osr);
		*vals = st->samp_freq_avail[osr_idx];
		*type = IIO_VAL_INT;
		*length = st->samp_freq_avail_len[osr_idx];
		return IIO_AVAIL_LIST;
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad4691_oversampling_ratios;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(ad4691_oversampling_ratios);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ad4691_single_shot_read(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan, int *val)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int reg_val, period_us;
	int ret;

	guard(mutex)(&st->lock);

	/* Use AUTONOMOUS mode for single-shot reads. */
	ret = regmap_write(st->regmap, AD4691_STATE_RESET_REG, AD4691_STATE_RESET_ALL);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_STD_SEQ_CONFIG,
			   BIT(chan->channel));
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_ACC_MASK_REG,
			   ~BIT(chan->channel) & GENMASK(15, 0));
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_ACC_DEPTH_IN(0), st->osr);
	if (ret)
		return ret;

	ret = ad4691_write_osc_freq(st);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_OSC_EN_REG, 1);
	if (ret)
		return ret;

	/*
	 * Wait osr + 1 oscillator periods: osr for accumulation, +1 for the
	 * pipeline margin (one extra period ensures the final result is ready).
	 */
	period_us = DIV_ROUND_UP((st->osr + 1) * USEC_PER_SEC,
				 st->target_osc_freq_Hz);
	fsleep(period_us);

	ret = regmap_write(st->regmap, AD4691_OSC_EN_REG, 0);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4691_AVG_IN(chan->channel), &reg_val);
	if (ret)
		return ret;

	*val = reg_val;

	ret = regmap_write(st->regmap, AD4691_STATE_RESET_REG, AD4691_STATE_RESET_ALL);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}

static int ad4691_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long info)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		return ad4691_single_shot_read(indio_dev, chan, val);
	}
	case IIO_CHAN_INFO_SAMP_FREQ: {
		/*
		 * Read target_osc_freq_Hz and osr under st->lock to get a
		 * consistent snapshot: write_raw for SAMP_FREQ or OSR modifies
		 * both fields under the lock, so a concurrent read without the
		 * lock could observe a new oscillator frequency with the old OSR.
		 */
		guard(mutex)(&st->lock);
		*val = st->target_osc_freq_Hz / st->osr;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		guard(mutex)(&st->lock);
		*val = st->osr;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_uV / (MICRO / MILLI);
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int ad4691_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4691_set_sampling_freq(st, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO: {
		unsigned int old_effective, found, osr_idx;

		osr_idx = ad4691_osr_index(val);
		if (ad4691_oversampling_ratios[osr_idx] != val)
			return -EINVAL;

		/*
		 * Hold st->lock while computing the new oscillator frequency
		 * and updating both target_osc_freq_Hz and osr atomically:
		 * read_raw for SAMP_FREQ reads both fields under the lock and
		 * must see a consistent pair (new osc ↔ new osr).
		 *
		 * Snap target_osc_freq_Hz to the largest table entry that is
		 * both <= old_effective * new_osr and evenly divisible by
		 * new_osr, preserving an integer read-back of
		 * in_voltage_sampling_frequency after the OSR change.
		 */
		guard(mutex)(&st->lock);
		old_effective = st->target_osc_freq_Hz / st->osr;
		found = ad4691_find_osc_freq(st, old_effective * (unsigned int)val, val);
		if (!found)
			return -EINVAL;
		st->target_osc_freq_Hz = found;
		st->osr = val;
		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int ad4691_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int ad4691_set_pwm_freq(struct ad4691_state *st, unsigned int freq)
{
	if (!freq)
		return -EINVAL;

	st->cnv_period_ns = DIV_ROUND_UP(NSEC_PER_SEC, freq);
	return 0;
}

static int ad4691_sampling_enable(struct ad4691_state *st, bool enable)
{
	struct pwm_state conv_state = {
		.period     = st->cnv_period_ns,
		.duty_cycle = AD4691_CNV_DUTY_CYCLE_NS,
		.polarity   = PWM_POLARITY_NORMAL,
		.enabled    = enable,
	};

	return pwm_apply_might_sleep(st->conv_trigger, &conv_state);
}

/*
 * ad4691_enter_conversion_mode - Switch the chip to its buffer conversion mode.
 *
 * Configures the ADC hardware registers for the mode selected at probe
 * (CNV_BURST or MANUAL). Called from buffer preenable before starting
 * sampling. The chip is in AUTONOMOUS mode during idle (for read_raw).
 */
static int ad4691_enter_conversion_mode(struct ad4691_state *st)
{
	int ret;

	if (st->manual_mode)
		return regmap_update_bits(st->regmap, AD4691_DEVICE_SETUP,
					  AD4691_MANUAL_MODE, AD4691_MANUAL_MODE);

	ret = ad4691_write_osc_freq(st);
	if (ret)
		return ret;

	ret = regmap_update_bits(st->regmap, AD4691_ADC_SETUP,
				 AD4691_ADC_MODE_MASK, AD4691_CNV_BURST_MODE);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD4691_STATE_RESET_REG,
			    AD4691_STATE_RESET_ALL);
}

static int ad4691_transfer(struct ad4691_state *st, u16 cmd)
{
	u8 buf[2];

	put_unaligned_be16(cmd, buf);

	return spi_write_then_read(st->spi, buf, sizeof(buf), NULL, 0);
}

/*
 * ad4691_exit_conversion_mode - Return the chip to AUTONOMOUS mode.
 *
 * Called from buffer postdisable to restore the chip to the
 * idle state used by read_raw. Clears the sequencer and resets state.
 */
static int ad4691_exit_conversion_mode(struct ad4691_state *st)
{
	if (st->manual_mode)
		return ad4691_transfer(st, AD4691_EXIT_COMMAND);

	return regmap_update_bits(st->regmap, AD4691_ADC_SETUP,
				  AD4691_ADC_MODE_MASK, AD4691_AUTONOMOUS_MODE);
}

static int ad4691_manual_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int k, i;
	int ret;

	memset(st->scan_xfers, 0, sizeof(st->scan_xfers));
	memset(st->scan_tx, 0, sizeof(st->scan_tx));

	spi_message_init(&st->scan_msg);

	k = 0;
	iio_for_each_active_channel(indio_dev, i) {
		/*
		 * Channel-select command occupies the first (high) byte of the
		 * 16-bit DIN frame; the second byte is a don't-care zero pad.
		 * put_unaligned_be16() writes [cmd, 0x00] in memory so the
		 * SPI controller sends the command byte first on the wire.
		 */
		put_unaligned_be16((u16)(AD4691_ADC_CHAN(i) << 8), &st->scan_tx[k]);
		st->scan_xfers[k].tx_buf = &st->scan_tx[k];
		/*
		 * The pipeline means xfer[0] receives the residual from the
		 * previous sequence, not a valid sample. Discard it (rx_buf=NULL)
		 * to avoid aliasing vals[0] across two concurrent DMA mappings.
		 * xfer[1] (or the NOOP when only one channel is active) writes
		 * the real ch[0] result to vals[0]. Subsequent transfers write
		 * into vals[k-1] so each result lands at the next dense slot.
		 */
		st->scan_xfers[k].rx_buf = (k == 0) ? NULL : &st->vals[k - 1];
		st->scan_xfers[k].len = sizeof(*st->scan_tx);
		st->scan_xfers[k].cs_change = 1;
		st->scan_xfers[k].cs_change_delay.value = AD4691_CNV_HIGH_TIME_NS;
		st->scan_xfers[k].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
		spi_message_add_tail(&st->scan_xfers[k], &st->scan_msg);
		k++;
	}

	/* Final NOOP transfer retrieves the last channel's result. */
	st->scan_xfers[k].tx_buf = &st->scan_tx[k]; /* scan_tx[k] == 0 == NOOP */
	st->scan_xfers[k].rx_buf = &st->vals[k - 1];
	st->scan_xfers[k].len = sizeof(*st->scan_tx);
	spi_message_add_tail(&st->scan_xfers[k], &st->scan_msg);

	ret = spi_optimize_message(st->spi, &st->scan_msg);
	if (ret)
		return ret;

	ret = ad4691_enter_conversion_mode(st);
	if (ret) {
		spi_unoptimize_message(&st->scan_msg);
		return ret;
	}

	return 0;
}

static int ad4691_manual_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4691_exit_conversion_mode(st);
	spi_unoptimize_message(&st->scan_msg);
	return ret;
}

static const struct iio_buffer_setup_ops ad4691_manual_buffer_setup_ops = {
	.preenable = ad4691_manual_buffer_preenable,
	.postdisable = ad4691_manual_buffer_postdisable,
};

static int ad4691_cnv_burst_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int acc_mask, std_seq_config;
	unsigned int k, i;
	int ret;

	memset(st->scan_xfers, 0, sizeof(st->scan_xfers));
	memset(st->scan_tx, 0, sizeof(st->scan_tx));

	spi_message_init(&st->scan_msg);

	/*
	 * Each AVG_IN read needs two transfers: a 2-byte address write phase
	 * followed by a 2-byte data read phase. CS toggles between channels
	 * (cs_change=1 on the read phase of all but the last channel).
	 */
	k = 0;
	iio_for_each_active_channel(indio_dev, i) {
		put_unaligned_be16(0x8000 | AD4691_AVG_IN(i), &st->scan_tx[k]);
		st->scan_xfers[2 * k].tx_buf = &st->scan_tx[k];
		st->scan_xfers[2 * k].len = sizeof(*st->scan_tx);
		spi_message_add_tail(&st->scan_xfers[2 * k], &st->scan_msg);
		st->scan_xfers[2 * k + 1].rx_buf = &st->vals[k];
		st->scan_xfers[2 * k + 1].len = sizeof(*st->scan_tx);
		st->scan_xfers[2 * k + 1].cs_change = 1;
		spi_message_add_tail(&st->scan_xfers[2 * k + 1], &st->scan_msg);
		k++;
	}

	/*
	 * Append a 4-byte state-reset transfer [addr_hi, addr_lo,
	 * STATE_RESET_ALL, OSC_EN=1]. CS is asserted throughout, so
	 * ADDR_DESCENDING writes byte[3]=1 to OSC_EN_REG (0x180) as a
	 * deliberate side-write, keeping the oscillator enabled.
	 * STATE_RESET_ALL starts the next burst; the hardware does not
	 * accumulate new conversions until after a STATE_RESET pulse, so
	 * no in-progress data is lost.  No cs_change here — CS must
	 * deassert normally at end of message to frame the next command.
	 */
	put_unaligned_be16(AD4691_STATE_RESET_REG, st->scan_tx_reset);
	st->scan_tx_reset[2] = AD4691_STATE_RESET_ALL;
	st->scan_tx_reset[3] = 1;
	st->scan_xfers[2 * k].tx_buf = st->scan_tx_reset;
	st->scan_xfers[2 * k].len = sizeof(st->scan_tx_reset);
	spi_message_add_tail(&st->scan_xfers[2 * k], &st->scan_msg);

	ret = spi_optimize_message(st->spi, &st->scan_msg);
	if (ret)
		return ret;

	std_seq_config = bitmap_read(indio_dev->active_scan_mask, 0,
				     iio_get_masklength(indio_dev)) & GENMASK(15, 0);
	ret = regmap_write(st->regmap, AD4691_STD_SEQ_CONFIG, std_seq_config);
	if (ret)
		goto err_unoptimize;

	acc_mask = ~std_seq_config & GENMASK(15, 0);
	ret = regmap_write(st->regmap, AD4691_ACC_MASK_REG, acc_mask);
	if (ret)
		goto err_unoptimize;

	ret = regmap_write(st->regmap, AD4691_ACC_DEPTH_IN(0), st->osr);
	if (ret)
		goto err_unoptimize;

	ret = ad4691_enter_conversion_mode(st);
	if (ret)
		goto err_unoptimize;

	return 0;

err_unoptimize:
	spi_unoptimize_message(&st->scan_msg);
	return ret;
}

static int ad4691_cnv_burst_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	int ret;

	/*
	 * Start the PWM and unmask the IRQ here in postenable, not in
	 * preenable. The IIO core attaches the trigger poll function between
	 * preenable and postenable; enabling sampling or unmasking the IRQ
	 * before that point risks a DATA_READY assertion landing before the
	 * poll function is registered. iio_trigger_poll() would drop the
	 * event, disable_irq_nosync() would fire, and enable_irq() would
	 * never be called, leaving the IRQ permanently masked.
	 */
	ret = ad4691_sampling_enable(st, true);
	if (ret)
		return ret;

	enable_irq(st->irq);
	st->irq_enabled = true;
	return 0;
}

static int ad4691_cnv_burst_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	if (st->irq_enabled) {
		disable_irq(st->irq);
		st->irq_enabled = false;
	}
	return ad4691_sampling_enable(st, false);
}

static int ad4691_cnv_burst_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4691_exit_conversion_mode(st);
	spi_unoptimize_message(&st->scan_msg);
	return ret;
}

static const struct iio_buffer_setup_ops ad4691_cnv_burst_buffer_setup_ops = {
	.preenable = ad4691_cnv_burst_buffer_preenable,
	.postenable = ad4691_cnv_burst_buffer_postenable,
	.predisable = ad4691_cnv_burst_buffer_predisable,
	.postdisable = ad4691_cnv_burst_buffer_postdisable,
};

static int ad4691_manual_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->regmap);
	struct spi_device *spi = to_spi_device(dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
	};
	unsigned int bpw = indio_dev->channels[0].scan_type.realbits;
	unsigned int bit, k;
	int ret;

	ret = ad4691_enter_conversion_mode(st);
	if (ret)
		return ret;

	memset(st->scan_xfers, 0, sizeof(st->scan_xfers));
	memset(st->scan_tx, 0, sizeof(st->scan_tx));

	/*
	 * N+1 transfers for N channels. Each CS-low period triggers
	 * a conversion AND returns the previous result (pipelined).
	 *   TX: [AD4691_ADC_CHAN(n), 0x00]
	 *   RX: [data_hi, data_lo]     (storagebits=16, shift=0)
	 * Transfer 0 RX is garbage; transfers 1..N carry real data.
	 * scan_tx is reused for TX commands (mutually exclusive with the
	 * non-offload triggered-buffer path).
	 *
	 * bits_per_word=bpw: the SPI controller reads tx_buf as a native
	 * 16-bit word and shifts it out MSB-first.  Store the exact 16-bit
	 * value we want on the wire as a plain native u16 — no endianness
	 * macro — so the wire bytes are correct on both LE and BE hosts.
	 * The channel-select command is a single byte; shift it to the MSB
	 * position so SPI sends it first, with a zero pad in the LSB.
	 */
	k = 0;
	iio_for_each_active_channel(indio_dev, bit) {
		st->scan_tx[k] = AD4691_ADC_CHAN(bit) << 8;
		st->scan_xfers[k].tx_buf = &st->scan_tx[k];
		st->scan_xfers[k].len = sizeof(*st->scan_tx);
		st->scan_xfers[k].bits_per_word = bpw;
		st->scan_xfers[k].cs_change = 1;
		st->scan_xfers[k].cs_change_delay.value = AD4691_CNV_HIGH_TIME_NS;
		st->scan_xfers[k].cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
		/* First transfer RX is garbage — skip it. */
		if (k > 0)
			st->scan_xfers[k].offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
		k++;
	}

	/* Final NOOP transfer retrieves the last channel's result. */
	st->scan_xfers[k].tx_buf = &st->scan_tx[k]; /* scan_tx[k] == 0 == NOOP */
	st->scan_xfers[k].len = sizeof(*st->scan_tx);
	st->scan_xfers[k].bits_per_word = bpw;
	st->scan_xfers[k].offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	k++;

	spi_message_init_with_transfers(&st->scan_msg, st->scan_xfers, k);
	st->scan_msg.offload = st->offload;

	ret = spi_optimize_message(spi, &st->scan_msg);
	if (ret)
		goto err_exit_conversion;

	config.periodic.frequency_hz = st->trigger_hz;
	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger, &config);
	if (ret)
		goto err_unoptimize;

	return 0;

err_unoptimize:
	spi_unoptimize_message(&st->scan_msg);
err_exit_conversion:
	ad4691_exit_conversion_mode(st);
	return ret;
}

static int ad4691_manual_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	spi_offload_trigger_disable(st->offload, st->offload_trigger);
	spi_unoptimize_message(&st->scan_msg);

	return ad4691_exit_conversion_mode(st);
}

static const struct iio_buffer_setup_ops ad4691_manual_offload_buffer_setup_ops = {
	.postenable = ad4691_manual_offload_buffer_postenable,
	.predisable = ad4691_manual_offload_buffer_predisable,
};

static int ad4691_cnv_burst_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->regmap);
	struct spi_device *spi = to_spi_device(dev);
	struct spi_offload_trigger_config config = {
		.type = SPI_OFFLOAD_TRIGGER_DATA_READY,
	};
	unsigned int bpw = indio_dev->channels[0].scan_type.realbits;
	unsigned int acc_mask, std_seq_config;
	unsigned int bit, k;
	int ret;

	std_seq_config = bitmap_read(indio_dev->active_scan_mask, 0,
				     iio_get_masklength(indio_dev)) & GENMASK(15, 0);
	ret = regmap_write(st->regmap, AD4691_STD_SEQ_CONFIG, std_seq_config);
	if (ret)
		return ret;

	acc_mask = ~std_seq_config & GENMASK(15, 0);
	ret = regmap_write(st->regmap, AD4691_ACC_MASK_REG, acc_mask);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_ACC_DEPTH_IN(0), st->osr);
	if (ret)
		return ret;

	ret = ad4691_enter_conversion_mode(st);
	if (ret)
		return ret;

	memset(st->scan_xfers, 0, sizeof(st->scan_xfers));
	memset(st->scan_tx, 0, sizeof(st->scan_tx));

	/*
	 * Each AVG_IN register read uses two transfers:
	 *   TX: [reg_hi | 0x80, reg_lo]  (address phase, CS stays asserted)
	 *   RX: [data_hi, data_lo]       (bpw-wide data phase, storagebits=16)
	 * Both TX and RX use bits_per_word=bpw: the SPI controller reads tx_buf
	 * as a native 16-bit word and shifts it out MSB-first.  Store the exact
	 * 16-bit wire value as a plain native u16 — no endianness macro — so the
	 * wire bytes are correct on both LE and BE hosts.  The read-address
	 * (0x8000 | reg) is already the 16-bit value we want on the wire.
	 * scan_tx is reused for TX addresses (mutually exclusive with the
	 * non-offload triggered-buffer path).
	 */
	k = 0;
	iio_for_each_active_channel(indio_dev, bit) {
		st->scan_tx[k] = 0x8000 | AD4691_AVG_IN(bit);

		/* TX: address phase, CS stays asserted into data phase */
		st->scan_xfers[2 * k].tx_buf = &st->scan_tx[k];
		st->scan_xfers[2 * k].len = sizeof(*st->scan_tx);
		st->scan_xfers[2 * k].bits_per_word = bpw;

		/* RX: data phase, CS toggles after to delimit the next register op */
		st->scan_xfers[2 * k + 1].len = sizeof(*st->scan_tx);
		st->scan_xfers[2 * k + 1].bits_per_word = bpw;
		st->scan_xfers[2 * k + 1].offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
		st->scan_xfers[2 * k + 1].cs_change = 1;
		k++;
	}

	/*
	 * State reset: single 4-byte write [addr_hi, addr_lo, STATE_RESET_ALL,
	 * OSC_EN=1]. ADDR_DESCENDING writes byte[3]=1 to OSC_EN_REG (0x180) as
	 * a deliberate side-write, keeping the oscillator enabled.
	 * scan_tx_reset is shared with the non-offload path (len=4 here vs
	 * len=3 there) since the two paths are mutually exclusive at probe.
	 */
	put_unaligned_be16(AD4691_STATE_RESET_REG, st->scan_tx_reset);
	st->scan_tx_reset[2] = AD4691_STATE_RESET_ALL;
	st->scan_tx_reset[3] = 1;
	st->scan_xfers[2 * k].tx_buf = st->scan_tx_reset;
	st->scan_xfers[2 * k].len = sizeof(st->scan_tx_reset);
	/*
	 * 4-byte u8 buffer assembled with put_unaligned_be16(); leave
	 * bits_per_word at the default (8) so bytes go out in memory order.
	 */

	spi_message_init_with_transfers(&st->scan_msg, st->scan_xfers, 2 * k + 1);
	st->scan_msg.offload = st->offload;

	ret = spi_optimize_message(spi, &st->scan_msg);
	if (ret)
		goto err_exit_conversion;

	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger, &config);
	if (ret)
		goto err_unoptimize;

	ret = ad4691_sampling_enable(st, true);
	if (ret)
		goto err_disable_trigger;

	return 0;

err_disable_trigger:
	spi_offload_trigger_disable(st->offload, st->offload_trigger);
err_unoptimize:
	spi_unoptimize_message(&st->scan_msg);
err_exit_conversion:
	ad4691_exit_conversion_mode(st);
	return ret;
}

static int ad4691_cnv_burst_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	ad4691_sampling_enable(st, false);
	spi_offload_trigger_disable(st->offload, st->offload_trigger);
	spi_unoptimize_message(&st->scan_msg);

	return ad4691_exit_conversion_mode(st);
}

static const struct iio_buffer_setup_ops ad4691_cnv_burst_offload_buffer_setup_ops = {
	.postenable = ad4691_cnv_burst_offload_buffer_postenable,
	.predisable = ad4691_cnv_burst_offload_buffer_predisable,
};

static ssize_t sampling_frequency_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad4691_state *st = iio_priv(indio_dev);

	if (st->manual_mode && st->offload)
		return sysfs_emit(buf, "%llu\n", READ_ONCE(st->trigger_hz));

	return sysfs_emit(buf, "%lu\n", NSEC_PER_SEC / st->cnv_period_ns);
}

static ssize_t sampling_frequency_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int freq;
	int ret;

	ret = kstrtouint(buf, 10, &freq);
	if (ret)
		return ret;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	if (st->manual_mode && st->offload) {
		struct spi_offload_trigger_config config = {
			.type = SPI_OFFLOAD_TRIGGER_PERIODIC,
			.periodic = { .frequency_hz = freq },
		};

		ret = spi_offload_trigger_validate(st->offload_trigger, &config);
		if (ret)
			return ret;

		WRITE_ONCE(st->trigger_hz, config.periodic.frequency_hz);
		return len;
	}

	ret = ad4691_set_pwm_freq(st, freq);
	if (ret)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR_RW(sampling_frequency, 0);

static const struct iio_dev_attr *ad4691_buffer_attrs[] = {
	&iio_dev_attr_sampling_frequency,
	NULL
};

static irqreturn_t ad4691_irq(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ad4691_state *st = iio_priv(indio_dev);

	/*
	 * Disable the IRQ before calling iio_trigger_poll(). The IRQ is
	 * re-enabled via the trigger .reenable callback, which the IIO core
	 * calls inside iio_trigger_notify_done() once use_count reaches zero.
	 * Re-enabling here (before notify_done) would race: a DATA_READY
	 * between enable_irq() and notify_done() calls iio_trigger_poll()
	 * while use_count > 0, dropping the event and permanently masking
	 * the IRQ.
	 */
	disable_irq_nosync(st->irq);
	iio_trigger_poll(indio_dev->trig);

	return IRQ_HANDLED;
}

static void ad4691_trigger_reenable(struct iio_trigger *trig)
{
	struct ad4691_state *st = iio_trigger_get_drvdata(trig);

	enable_irq(st->irq);
}

static const struct iio_trigger_ops ad4691_trigger_ops = {
	.reenable = ad4691_trigger_reenable,
	.validate_device = iio_trigger_validate_own_device,
};

static void ad4691_read_scan(struct iio_dev *indio_dev, s64 ts)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&st->lock);

	ret = spi_sync(st->spi, &st->scan_msg);
	if (ret) {
		dev_err_ratelimited(regmap_get_device(st->regmap),
				    "SPI scan failed: %d\n", ret);
		return;
	}

	/*
	 * rx_buf pointers in scan_xfers point directly into scan.vals, so no
	 * copy is needed. The scan_msg already includes a STATE_RESET at the
	 * end (appended in preenable), so no explicit reset is needed here.
	 */
	iio_push_to_buffers_with_ts(indio_dev, st->vals, sizeof(st->vals), ts);
}

static irqreturn_t ad4691_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;

	ad4691_read_scan(indio_dev, pf->timestamp);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

/*
 * CNV burst mode: only allow our own trigger (driven by DATA_READY IRQ).
 * Manual mode: external triggers (e.g. iio-trig-hrtimer) must be allowed
 * because manual mode has no DATA_READY IRQ to fire the internal trigger.
 * iio_trigger_ops.validate_device = iio_trigger_validate_own_device is
 * correct in both modes — it prevents other devices from hijacking our
 * internal trigger; the distinction here is only for iio_info.validate_trigger.
 */
static const struct iio_info ad4691_cnv_burst_info = {
	.read_raw = ad4691_read_raw,
	.write_raw = ad4691_write_raw,
	.read_avail = ad4691_read_avail,
	.debugfs_reg_access = ad4691_reg_access,
	.validate_trigger = iio_validate_own_trigger,
};

static const struct iio_info ad4691_manual_info = {
	.read_raw = ad4691_read_raw,
	.write_raw = ad4691_write_raw,
	.read_avail = ad4691_read_avail,
	.debugfs_reg_access = ad4691_reg_access,
};

static int ad4691_pwm_setup(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);

	st->conv_trigger = devm_pwm_get(dev, "cnv");
	if (IS_ERR(st->conv_trigger))
		return dev_err_probe(dev, PTR_ERR(st->conv_trigger),
				     "Failed to get CNV PWM\n");

	return ad4691_set_pwm_freq(st, st->info->max_rate);
}

static int ad4691_regulator_setup(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	int ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad4691_supplies),
					     ad4691_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get and enable supplies\n");

	/*
	 * vdd-supply and ldo-in-supply are mutually exclusive:
	 *   vdd-supply present  → external 1.8V VDD; disable internal LDO.
	 *   vdd-supply absent   → enable internal LDO fed from ldo-in-supply.
	 * Having both simultaneously is strongly inadvisable per the datasheet.
	 */
	if (device_property_present(dev, "vdd-supply")) {
		ret = devm_regulator_get_enable(dev, "vdd");
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get and enable VDD\n");
	} else if (device_property_present(dev, "ldo-in-supply")) {
		ret = devm_regulator_get_enable(dev, "ldo-in");
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get and enable LDO-IN\n");
		st->ldo_en = true;
	} else {
		return dev_err_probe(dev, -EINVAL,
				     "missing one of vdd-supply, ldo-in-supply\n");
	}

	if (device_property_present(dev, "ref-supply")) {
		st->vref_uV = devm_regulator_get_enable_read_voltage(dev, "ref");
		if (st->vref_uV < 0)
			return dev_err_probe(dev, st->vref_uV,
					     "Failed to get REF supply voltage\n");
	} else if (device_property_present(dev, "refin-supply")) {
		st->vref_uV = devm_regulator_get_enable_read_voltage(dev, "refin");
		if (st->vref_uV < 0)
			return dev_err_probe(dev, st->vref_uV,
					     "Failed to get REFIN supply voltage\n");
		st->refbuf_en = true;
	} else {
		return dev_err_probe(dev, -EINVAL,
				     "missing one of ref-supply, refin-supply\n");
	}

	if (st->vref_uV < AD4691_VREF_uV_MIN || st->vref_uV > AD4691_VREF_uV_MAX)
		return dev_err_probe(dev, -EINVAL,
				     "vref(%d) must be in the range [%u...%u]\n",
				     st->vref_uV, AD4691_VREF_uV_MIN,
				     AD4691_VREF_uV_MAX);

	return 0;
}

static int ad4691_reset(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct reset_control *rst;
	int ret;

	rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "Failed to get reset\n");

	if (rst) {
		/*
		 * Assert the reset line to guarantee a clean reset pulse on
		 * every probe, including driver reloads where the line may
		 * already be deasserted (reset_control_put() does not
		 * re-assert on release). tRESETL (minimum pulse width) = 10 ns
		 * (Table 5); kernel function-call overhead alone exceeds this,
		 * so no explicit delay is needed between assert and deassert.
		 */
		reset_control_assert(rst);
		ret = reset_control_deassert(rst);
		if (ret)
			return ret;
	} else {
		/* No hardware reset available, fall back to software reset. */
		ret = regmap_write(st->regmap, AD4691_SPI_CONFIG_A_REG,
				   AD4691_SW_RESET);
		if (ret)
			return ret;
	}

	/*
	 * Wait 300 µs (Table 5) for the device to complete its internal reset
	 * sequence before accepting SPI commands.
	 */
	fsleep(300);
	return 0;
}

static int ad4691_config(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	enum ad4691_ref_ctrl ref_val;
	unsigned int val;
	int ret;

	/*
	 * Determine buffer conversion mode from DT: if a PWM is provided it
	 * drives the CNV pin (CNV_BURST_MODE); otherwise CNV is tied to CS
	 * and each SPI transfer triggers a conversion (MANUAL_MODE).
	 * Both modes idle in AUTONOMOUS mode so that read_raw can use the
	 * internal oscillator without disturbing the hardware configuration.
	 */
	if (device_property_present(dev, "pwms")) {
		st->manual_mode = false;
		ret = ad4691_pwm_setup(st);
		if (ret)
			return ret;
	} else {
		st->manual_mode = true;
	}

	switch (st->vref_uV) {
	case AD4691_VREF_uV_MIN ... AD4691_VREF_2P5_uV_MAX:
		ref_val = AD4691_VREF_2P5;
		break;
	case AD4691_VREF_2P5_uV_MAX + 1 ... AD4691_VREF_3P0_uV_MAX:
		ref_val = AD4691_VREF_3P0;
		break;
	case AD4691_VREF_3P0_uV_MAX + 1 ... AD4691_VREF_3P3_uV_MAX:
		ref_val = AD4691_VREF_3P3;
		break;
	case AD4691_VREF_3P3_uV_MAX + 1 ... AD4691_VREF_4P096_uV_MAX:
		ref_val = AD4691_VREF_4P096;
		break;
	case AD4691_VREF_4P096_uV_MAX + 1 ... AD4691_VREF_uV_MAX:
		ref_val = AD4691_VREF_5P0;
		break;
	default:
		return dev_err_probe(dev, -EINVAL,
				     "Unsupported vref voltage: %d uV\n",
				     st->vref_uV);
	}

	val = FIELD_PREP(AD4691_REF_CTRL_MASK, ref_val);
	if (st->refbuf_en)
		val |= AD4691_REFBUF_EN;

	ret = regmap_write(st->regmap, AD4691_REF_CTRL, val);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write REF_CTRL\n");

	ret = regmap_assign_bits(st->regmap, AD4691_DEVICE_SETUP,
				 AD4691_LDO_EN, st->ldo_en);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write DEVICE_SETUP\n");

	/*
	 * Set the internal oscillator to the highest rate this chip supports.
	 * Index 0 (1 MHz) exceeds the 500 kHz max of AD4691/AD4693, so those
	 * chips start at index 1 (500 kHz).
	 */
	ret = regmap_write(st->regmap, AD4691_OSC_FREQ_REG,
			   ad4691_samp_freq_start(st->info));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write OSC_FREQ\n");

	st->target_osc_freq_Hz = ad4691_osc_freqs_Hz[ad4691_samp_freq_start(st->info)];

	ret = regmap_update_bits(st->regmap, AD4691_ADC_SETUP,
				 AD4691_ADC_MODE_MASK, AD4691_AUTONOMOUS_MODE);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write ADC_SETUP\n");

	ad4691_precompute_samp_freq_avail(st);

	return 0;
}

static int ad4691_setup_triggered_buffer(struct iio_dev *indio_dev,
					 struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct iio_trigger *trig;
	unsigned int i;
	int irq, ret;

	/*
	 * Manual mode exposes channels without the oversampling_ratio attribute
	 * because ACC_DEPTH_IN is not configured in manual mode.
	 */
	if (st->manual_mode)
		indio_dev->channels = st->info->sw_info->manual_channels;
	else
		indio_dev->channels = st->info->sw_info->channels;
	indio_dev->num_channels = st->info->sw_info->num_channels;
	indio_dev->info = st->manual_mode ? &ad4691_manual_info : &ad4691_cnv_burst_info;

	/*
	 * Manual mode relies on an external trigger (e.g. iio-trig-hrtimer);
	 * no internal trigger is needed or registered.
	 */
	if (st->manual_mode)
		return devm_iio_triggered_buffer_setup(dev, indio_dev,
						       iio_pollfunc_store_time,
						       ad4691_trigger_handler,
						       &ad4691_manual_buffer_setup_ops);

	/*
	 * CNV burst mode: allocate an internal trigger driven by the
	 * DATA_READY IRQ on the GP pin.
	 */
	trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
				      iio_device_id(indio_dev));
	if (!trig)
		return -ENOMEM;

	trig->ops = &ad4691_trigger_ops;
	iio_trigger_set_drvdata(trig, st);

	ret = devm_iio_trigger_register(dev, trig);
	if (ret)
		return dev_err_probe(dev, ret, "IIO trigger register failed\n");

	indio_dev->trig = iio_trigger_get(trig);

	/*
	 * The GP pin named in interrupt-names asserts at end-of-conversion.
	 * The IRQ handler fires the IIO trigger so the trigger handler can
	 * read and push the sample to the buffer. The IRQ is kept disabled
	 * until the buffer is enabled.
	 */
	irq = -ENXIO;
	for (i = 0; i < ARRAY_SIZE(ad4691_gp_names); i++) {
		irq = fwnode_irq_get_byname(dev_fwnode(dev),
					    ad4691_gp_names[i]);
		if (irq > 0 || irq == -EPROBE_DEFER)
			break;
	}
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to get GP interrupt\n");

	st->irq = irq;

	ret = ad4691_gpio_setup(st, i);
	if (ret)
		return ret;

	/*
	 * The handler only calls disable_irq_nosync() and iio_trigger_poll(),
	 * both safe in hardirq context, so register as a hard IRQ handler.
	 * IRQF_NO_AUTOEN keeps it disabled until the buffer is enabled.
	 */
	ret = devm_request_irq(dev, irq, ad4691_irq, IRQF_NO_AUTOEN,
			       indio_dev->name, indio_dev);
	if (ret)
		return ret;

	return devm_iio_triggered_buffer_setup_ext(dev, indio_dev,
						   iio_pollfunc_store_time,
						   ad4691_trigger_handler,
						   IIO_BUFFER_DIRECTION_IN,
						   &ad4691_cnv_burst_buffer_setup_ops,
						   ad4691_buffer_attrs);
}

static int ad4691_setup_offload(struct iio_dev *indio_dev,
				struct ad4691_state *st,
				struct spi_offload *spi_offload)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct dma_chan *rx_dma;
	int ret;

	st->offload = spi_offload;

	/*
	 * CNV burst offload exposes oversampling_ratio (ACC_DEPTH_IN is
	 * configured per channel at buffer enable). Manual offload does not
	 * configure ACC_DEPTH_IN, so it uses a separate channel array
	 * without the oversampling_ratio attribute. Both paths use IIO_CPU
	 * (no .endianness annotation) because bits_per_word=16 causes the
	 * SPI Engine to produce native 16-bit DMA words.
	 */
	if (st->manual_mode)
		indio_dev->channels = st->info->offload_info->manual_channels;
	else
		indio_dev->channels = st->info->offload_info->channels;
	indio_dev->num_channels = st->info->offload_info->num_channels;
	/*
	 * Offload path uses DMA directly; no IIO trigger is involved, so
	 * external triggers are not restricted (no validate_trigger).
	 */
	indio_dev->info = &ad4691_manual_info;

	if (st->manual_mode) {
		st->offload_trigger =
			devm_spi_offload_trigger_get(dev, st->offload,
						     SPI_OFFLOAD_TRIGGER_PERIODIC);
		if (IS_ERR(st->offload_trigger))
			return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
					     "Failed to get periodic offload trigger\n");

		st->trigger_hz = AD4691_OFFLOAD_INITIAL_TRIGGER_HZ;
	} else {
		struct spi_offload_trigger_info trigger_info = {
			.fwnode = dev_fwnode(dev),
			.ops    = &ad4691_offload_trigger_ops,
			.priv   = st,
		};

		ret = devm_spi_offload_trigger_register(dev, &trigger_info);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to register offload trigger\n");

		st->offload_trigger =
			devm_spi_offload_trigger_get(dev, st->offload,
						     SPI_OFFLOAD_TRIGGER_DATA_READY);
		if (IS_ERR(st->offload_trigger))
			return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
					     "Failed to get DATA_READY offload trigger\n");
	}

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "Failed to get offload RX DMA channel\n");

	if (st->manual_mode)
		indio_dev->setup_ops = &ad4691_manual_offload_buffer_setup_ops;
	else
		indio_dev->setup_ops = &ad4691_cnv_burst_offload_buffer_setup_ops;

	ret = devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev, rx_dma,
							  IIO_BUFFER_DIRECTION_IN);
	if (ret)
		return ret;

	indio_dev->buffer->attrs = ad4691_buffer_attrs;

	return 0;
}

static int ad4691_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct spi_offload *spi_offload;
	struct iio_dev *indio_dev;
	struct ad4691_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -ENODEV;
	st->osr = 1;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->regmap = devm_regmap_init(dev, NULL, spi, &ad4691_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	ret = ad4691_regulator_setup(st);
	if (ret)
		return ret;

	ret = ad4691_reset(st);
	if (ret)
		return ret;

	ret = ad4691_config(st);
	if (ret)
		return ret;

	spi_offload = devm_spi_offload_get(dev, spi, &ad4691_offload_config);
	ret = PTR_ERR_OR_ZERO(spi_offload);
	if (ret == -ENODEV)
		spi_offload = NULL;
	else if (ret)
		return dev_err_probe(dev, ret, "Failed to get SPI offload\n");

	indio_dev->name = st->info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (spi_offload)
		ret = ad4691_setup_offload(indio_dev, st, spi_offload);
	else
		ret = ad4691_setup_triggered_buffer(indio_dev, st);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad4691_of_match[] = {
	{ .compatible = "adi,ad4691", .data = &ad4691_chip_info },
	{ .compatible = "adi,ad4692", .data = &ad4692_chip_info },
	{ .compatible = "adi,ad4693", .data = &ad4693_chip_info },
	{ .compatible = "adi,ad4694", .data = &ad4694_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4691_of_match);

static const struct spi_device_id ad4691_id[] = {
	{ .name = "ad4691", .driver_data = (kernel_ulong_t)&ad4691_chip_info },
	{ .name = "ad4692", .driver_data = (kernel_ulong_t)&ad4692_chip_info },
	{ .name = "ad4693", .driver_data = (kernel_ulong_t)&ad4693_chip_info },
	{ .name = "ad4694", .driver_data = (kernel_ulong_t)&ad4694_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4691_id);

static struct spi_driver ad4691_driver = {
	.driver = {
		.name = "ad4691",
		.of_match_table = ad4691_of_match,
	},
	.probe = ad4691_probe,
	.id_table = ad4691_id,
};
module_spi_driver(ad4691_driver);

MODULE_AUTHOR("Radu Sabau <radu.sabau@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4691 Family ADC Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMA_BUFFER");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
