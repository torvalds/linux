// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD4030 and AD4630 ADC family driver.
 *
 * Copyright 2024 Analog Devices, Inc.
 * Copyright 2024 BayLibre, SAS
 *
 * based on code from:
 *	Analog Devices, Inc.
 *	  Sergiu Cuciurean <sergiu.cuciurean@analog.com>
 *	  Nuno Sa <nuno.sa@analog.com>
 *	  Marcelo Schmitt <marcelo.schmitt@analog.com>
 *	  Liviu Adace <liviu.adace@analog.com>
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/limits.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/types.h>

#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AD4030_REG_INTERFACE_CONFIG_A			0x00
#define     AD4030_REG_INTERFACE_CONFIG_A_SW_RESET	(BIT(0) | BIT(7))
#define AD4030_REG_INTERFACE_CONFIG_B			0x01
#define AD4030_REG_DEVICE_CONFIG			0x02
#define AD4030_REG_CHIP_TYPE				0x03
#define AD4030_REG_PRODUCT_ID_L				0x04
#define AD4030_REG_PRODUCT_ID_H				0x05
#define AD4030_REG_CHIP_GRADE				0x06
#define     AD4030_REG_CHIP_GRADE_AD4030_24_GRADE	0x10
#define     AD4030_REG_CHIP_GRADE_AD4630_16_GRADE	0x03
#define     AD4030_REG_CHIP_GRADE_AD4630_24_GRADE	0x00
#define     AD4030_REG_CHIP_GRADE_AD4632_16_GRADE	0x05
#define     AD4030_REG_CHIP_GRADE_AD4632_24_GRADE	0x02
#define     AD4030_REG_CHIP_GRADE_ADAQ4216_GRADE	0x1E
#define     AD4030_REG_CHIP_GRADE_ADAQ4224_GRADE	0x1C
#define     AD4030_REG_CHIP_GRADE_MASK_CHIP_GRADE	GENMASK(7, 3)
#define AD4030_REG_SCRATCH_PAD			0x0A
#define AD4030_REG_SPI_REVISION			0x0B
#define AD4030_REG_VENDOR_L			0x0C
#define AD4030_REG_VENDOR_H			0x0D
#define AD4030_REG_STREAM_MODE			0x0E
#define AD4030_REG_INTERFACE_CONFIG_C		0x10
#define AD4030_REG_INTERFACE_STATUS_A		0x11
#define AD4030_REG_EXIT_CFG_MODE		0x14
#define     AD4030_REG_EXIT_CFG_MODE_EXIT_MSK	BIT(0)
#define AD4030_REG_AVG				0x15
#define     AD4030_REG_AVG_MASK_AVG_SYNC	BIT(7)
#define     AD4030_REG_AVG_MASK_AVG_VAL		GENMASK(4, 0)
#define AD4030_REG_OFFSET_X0_0			0x16
#define AD4030_REG_OFFSET_X0_1			0x17
#define AD4030_REG_OFFSET_X0_2			0x18
#define AD4030_REG_OFFSET_X1_0			0x19
#define AD4030_REG_OFFSET_X1_1			0x1A
#define AD4030_REG_OFFSET_X1_2			0x1B
#define     AD4030_REG_OFFSET_BYTES_NB		3
#define     AD4030_REG_OFFSET_CHAN(ch)		\
	(AD4030_REG_OFFSET_X0_2 + (AD4030_REG_OFFSET_BYTES_NB * (ch)))
#define AD4030_REG_GAIN_X0_LSB			0x1C
#define AD4030_REG_GAIN_X0_MSB			0x1D
#define AD4030_REG_GAIN_X1_LSB			0x1E
#define AD4030_REG_GAIN_X1_MSB			0x1F
#define     AD4030_REG_GAIN_MAX_GAIN		1999970
#define     AD4030_REG_GAIN_BYTES_NB		2
#define     AD4030_REG_GAIN_CHAN(ch)		\
	(AD4030_REG_GAIN_X0_MSB + (AD4030_REG_GAIN_BYTES_NB * (ch)))
#define AD4030_REG_MODES			0x20
#define     AD4030_REG_MODES_MASK_OUT_DATA_MODE	GENMASK(2, 0)
#define     AD4030_REG_MODES_MASK_LANE_MODE	GENMASK(7, 6)
#define AD4030_REG_OSCILATOR			0x21
#define AD4030_REG_IO				0x22
#define     AD4030_REG_IO_MASK_IO2X		BIT(1)
#define AD4030_REG_PAT0				0x23
#define AD4030_REG_PAT1				0x24
#define AD4030_REG_PAT2				0x25
#define AD4030_REG_PAT3				0x26
#define AD4030_REG_DIG_DIAG			0x34
#define AD4030_REG_DIG_ERR			0x35

/* Sequence starting with "1 0 1" to enable reg access */
#define AD4030_REG_ACCESS			0xA0

#define AD4030_MAX_IIO_SAMPLE_SIZE_BUFFERED	BITS_TO_BYTES(64)
#define AD4030_MAX_HARDWARE_CHANNEL_NB		2
#define AD4030_MAX_IIO_CHANNEL_NB		5
#define AD4030_SINGLE_COMMON_BYTE_CHANNELS_MASK	0b10
#define AD4030_DUAL_COMMON_BYTE_CHANNELS_MASK	0b1100
#define AD4030_GAIN_MIDLE_POINT			0x8000
/*
 * This accounts for 1 sample per channel plus one s64 for the timestamp,
 * aligned on a s64 boundary
 */
#define AD4030_MAXIMUM_RX_BUFFER_SIZE			\
	(ALIGN(AD4030_MAX_IIO_SAMPLE_SIZE_BUFFERED *	\
	      AD4030_MAX_HARDWARE_CHANNEL_NB,		\
	      sizeof(s64)) + sizeof(s64))

#define AD4030_VREF_MIN_UV		(4096 * MILLI)
#define AD4030_VREF_MAX_UV		(5000 * MILLI)
#define AD4030_VIO_THRESHOLD_UV		(1400 * MILLI)
#define AD4030_SPI_MAX_XFER_LEN		8
#define AD4030_SPI_MAX_REG_XFER_SPEED	(80 * MEGA)
#define AD4030_TCNVH_NS			10
#define AD4030_TCNVL_NS			20
#define AD4030_TCYC_NS			500
#define AD4030_TCYC_ADJUSTED_NS		(AD4030_TCYC_NS - AD4030_TCNVL_NS)
#define AD4030_TRESET_PW_NS		50
#define AD4632_TCYC_NS			2000
#define AD4632_TCYC_ADJUSTED_NS		(AD4632_TCYC_NS - AD4030_TCNVL_NS)
#define AD4030_TRESET_COM_DELAY_MS	750
/* Datasheet says 9.8ns, so use the closest integer value */
#define AD4030_TQUIET_CNV_DELAY_NS	10

/* HARDWARE_GAIN */
#define ADAQ4616_PGA_PINS		2
#define ADAQ4616_PGA_GAIN_MAX_NANO	(NANO * 2 / 3)

enum ad4030_out_mode {
	AD4030_OUT_DATA_MD_DIFF,
	AD4030_OUT_DATA_MD_16_DIFF_8_COM,
	AD4030_OUT_DATA_MD_24_DIFF_8_COM,
	AD4030_OUT_DATA_MD_30_AVERAGED_DIFF,
	AD4030_OUT_DATA_MD_32_PATTERN,
};

enum {
	AD4030_LANE_MD_1_PER_CH,
	AD4030_LANE_MD_2_PER_CH,
	AD4030_LANE_MD_4_PER_CH,
	AD4030_LANE_MD_INTERLEAVED,
};

enum {
	AD4030_SCAN_TYPE_NORMAL,
	AD4030_SCAN_TYPE_AVG,
};

/*
 * Gains computed as fractions of 1000 so they can be expressed by integers.
 */
static const int adaq4216_hw_gains_vpv[] = {
	1 * MILLI / 3,		/* 0.333 */
	5 * MILLI / 9,		/* 0.555 */
	20 * MILLI / 9,		/* 0.2222 */
	20 * MILLI / 3,		/* 0.6666 */
};

static const int adaq4216_hw_gains_frac[][2] = {
	{ 1, 3 },  /* 1/3 V/V gain */
	{ 5, 9 },  /* 5/9 V/V gain */
	{ 20, 9 }, /* 20/9 V/V gain */
	{ 20, 3 }, /* 20/3 V/V gain */
};

struct ad4030_chip_info {
	const char *name;
	const unsigned long *available_masks;
	const struct iio_chan_spec channels[AD4030_MAX_IIO_CHANNEL_NB];
	const struct iio_chan_spec offload_channels[AD4030_MAX_IIO_CHANNEL_NB];
	u8 grade;
	u8 precision_bits;
	bool has_pga;
	/* Number of hardware channels */
	int num_voltage_inputs;
	unsigned int tcyc_ns;
	unsigned int max_sample_rate_hz;
};

struct ad4030_state {
	struct spi_device *spi;
	struct regmap *regmap;
	const struct ad4030_chip_info *chip;
	struct gpio_desc *cnv_gpio;
	int vref_uv;
	int vio_uv;
	int offset_avail[3];
	unsigned int avg_log2;
	enum ad4030_out_mode mode;
	/* Offload sampling */
	struct spi_transfer offload_xfer;
	struct spi_message offload_msg;
	struct spi_offload *offload;
	struct spi_offload_trigger *offload_trigger;
	struct spi_offload_trigger_config offload_trigger_config;
	struct pwm_device *cnv_trigger;
	size_t scale_avail_size;
	struct pwm_waveform cnv_wf;
	unsigned int scale_avail[ARRAY_SIZE(adaq4216_hw_gains_vpv)][2];
	struct gpio_descs *pga_gpios;
	unsigned int pga_index;

	/*
	 * DMA (thus cache coherency maintenance) requires the transfer buffers
	 * to live in their own cache lines.
	 */
	u8 tx_data[AD4030_SPI_MAX_XFER_LEN] __aligned(IIO_DMA_MINALIGN);
	union {
		u8 raw[AD4030_MAXIMUM_RX_BUFFER_SIZE];
		struct {
			s32 diff;
			u8 common;
		} single;
		struct {
			s32 diff[2];
			u8 common[2];
		} dual;
	} rx_data;
};

/*
 * For a chip with 2 hardware channel this will be used to create 2 common-mode
 * channels:
 * - voltage4
 * - voltage5
 * As the common-mode channels are after the differential ones, we compute the
 * channel number like this:
 * - _idx is the scan_index (the order in the output buffer)
 * - _ch is the hardware channel number this common-mode channel is related
 * - _idx - _ch gives us the number of channel in the chip
 * - _idx - _ch * 2 is the starting number of the common-mode channels, since
 *   for each differential channel there is a common-mode channel
 * - _idx - _ch * 2 + _ch gives the channel number for this specific common-mode
 *   channel
 */
#define AD4030_CHAN_CMO(_idx, _ch)  {					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
		BIT(IIO_CHAN_INFO_SCALE),				\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.address = (_ch),						\
	.channel = ((_idx) - (_ch)) * 2 + (_ch),			\
	.scan_index = (_idx),						\
	.scan_type = {							\
		.sign = 'u',						\
		.storagebits = 8,					\
		.realbits = 8,						\
		.endianness = IIO_BE,					\
	},								\
}

/*
 * For a chip with 2 hardware channel this will be used to create 2 differential
 * channels:
 * - voltage0-voltage1
 * - voltage2-voltage3
 */
#define __AD4030_CHAN_DIFF(_idx, _scan_type, _offload, _pga) {		\
	.info_mask_shared_by_all =					\
		(_offload ? BIT(IIO_CHAN_INFO_SAMP_FREQ) : 0) |		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),			\
	.info_mask_shared_by_all_available =				\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |				\
		BIT(IIO_CHAN_INFO_CALIBBIAS) |				\
		BIT(IIO_CHAN_INFO_RAW),					\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_CALIBBIAS) |	\
		(_pga ? BIT(IIO_CHAN_INFO_SCALE) : 0) |			\
		BIT(IIO_CHAN_INFO_CALIBSCALE),				\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.address = (_idx),						\
	.channel = (_idx) * 2,						\
	.channel2 = (_idx) * 2 + 1,					\
	.scan_index = (_idx),						\
	.differential = true,						\
	.has_ext_scan_type = 1,						\
	.ext_scan_type = _scan_type,					\
	.num_ext_scan_type = ARRAY_SIZE(_scan_type),			\
}

#define AD4030_CHAN_DIFF(_idx, _scan_type)				\
	__AD4030_CHAN_DIFF(_idx, _scan_type, 0, 0)

#define AD4030_OFFLOAD_CHAN_DIFF(_idx, _scan_type)			\
	__AD4030_CHAN_DIFF(_idx, _scan_type, 1, 0)

#define ADAQ4216_CHAN_DIFF(_idx, _scan_type)				\
	__AD4030_CHAN_DIFF(_idx, _scan_type, 0, 1)

#define ADAQ4216_OFFLOAD_CHAN_DIFF(_idx, _scan_type)			\
	__AD4030_CHAN_DIFF(_idx, _scan_type, 1, 1)

/*
 * AD4030 can average over 2^N samples, where N = 1, 2, 3, ..., 16.
 * We use N = 0 to mean no sample averaging.
 */
static const int ad4030_average_modes[] = {
	BIT(0),					/* No sampling average */
	BIT(1), BIT(2), BIT(3), BIT(4),
	BIT(5), BIT(6), BIT(7), BIT(8),
	BIT(9), BIT(10), BIT(11), BIT(12),
	BIT(13), BIT(14), BIT(15), BIT(16),
};

static const struct spi_offload_config ad4030_offload_config = {
	.capability_flags = SPI_OFFLOAD_CAP_TRIGGER |
			    SPI_OFFLOAD_CAP_RX_STREAM_DMA,
};

static int ad4030_enter_config_mode(struct ad4030_state *st)
{
	st->tx_data[0] = AD4030_REG_ACCESS;

	struct spi_transfer xfer = {
		.tx_buf = st->tx_data,
		.len = 1,
		.speed_hz = AD4030_SPI_MAX_REG_XFER_SPEED,
	};

	return spi_sync_transfer(st->spi, &xfer, 1);
}

static int ad4030_exit_config_mode(struct ad4030_state *st)
{
	st->tx_data[0] = 0;
	st->tx_data[1] = AD4030_REG_EXIT_CFG_MODE;
	st->tx_data[2] = AD4030_REG_EXIT_CFG_MODE_EXIT_MSK;

	struct spi_transfer xfer = {
		.tx_buf = st->tx_data,
		.len = 3,
		.speed_hz = AD4030_SPI_MAX_REG_XFER_SPEED,
	};

	return spi_sync_transfer(st->spi, &xfer, 1);
}

static int ad4030_spi_read(void *context, const void *reg, size_t reg_size,
			   void *val, size_t val_size)
{
	int ret;
	struct ad4030_state *st = context;
	struct spi_transfer xfer = {
		.tx_buf = st->tx_data,
		.rx_buf = st->rx_data.raw,
		.len = reg_size + val_size,
		.speed_hz = AD4030_SPI_MAX_REG_XFER_SPEED,
	};

	if (xfer.len > sizeof(st->tx_data) ||
	    xfer.len > sizeof(st->rx_data.raw))
		return  -EINVAL;

	ret = ad4030_enter_config_mode(st);
	if (ret)
		return ret;

	memset(st->tx_data, 0, sizeof(st->tx_data));
	memcpy(st->tx_data, reg, reg_size);

	ret = spi_sync_transfer(st->spi, &xfer, 1);
	if (ret)
		return ret;

	memcpy(val, &st->rx_data.raw[reg_size], val_size);

	return ad4030_exit_config_mode(st);
}

static int ad4030_spi_write(void *context, const void *data, size_t count)
{
	int ret;
	struct ad4030_state *st = context;
	bool is_reset = count >= 3 &&
			((u8 *)data)[0] == 0 &&
			((u8 *)data)[1] == 0 &&
			((u8 *)data)[2] == 0x81;
	struct spi_transfer xfer = {
		.tx_buf = st->tx_data,
		.len = count,
		.speed_hz = AD4030_SPI_MAX_REG_XFER_SPEED,
	};

	if (count > sizeof(st->tx_data))
		return  -EINVAL;

	ret = ad4030_enter_config_mode(st);
	if (ret)
		return ret;

	memcpy(st->tx_data, data, count);

	ret = spi_sync_transfer(st->spi, &xfer, 1);
	if (ret)
		return ret;

	/*
	 * From datasheet: "After a [...] reset, no SPI commands or conversions
	 * can be started for 750us"
	 *  After a reset we are in conversion mode, no need to exit config mode
	 */
	if (is_reset) {
		fsleep(750);
		return 0;
	}

	return ad4030_exit_config_mode(st);
}

static const struct regmap_bus ad4030_regmap_bus = {
	.read = ad4030_spi_read,
	.write = ad4030_spi_write,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
};

static const struct regmap_range ad4030_regmap_rd_range[] = {
	regmap_reg_range(AD4030_REG_INTERFACE_CONFIG_A, AD4030_REG_CHIP_GRADE),
	regmap_reg_range(AD4030_REG_SCRATCH_PAD, AD4030_REG_STREAM_MODE),
	regmap_reg_range(AD4030_REG_INTERFACE_CONFIG_C,
			 AD4030_REG_INTERFACE_STATUS_A),
	regmap_reg_range(AD4030_REG_EXIT_CFG_MODE, AD4030_REG_PAT3),
	regmap_reg_range(AD4030_REG_DIG_DIAG, AD4030_REG_DIG_ERR),
};

static const struct regmap_range ad4030_regmap_wr_range[] = {
	regmap_reg_range(AD4030_REG_CHIP_TYPE, AD4030_REG_CHIP_GRADE),
	regmap_reg_range(AD4030_REG_SPI_REVISION, AD4030_REG_VENDOR_H),
};

static const struct regmap_access_table ad4030_regmap_rd_table = {
	.yes_ranges = ad4030_regmap_rd_range,
	.n_yes_ranges = ARRAY_SIZE(ad4030_regmap_rd_range),
};

static const struct regmap_access_table ad4030_regmap_wr_table = {
	.no_ranges = ad4030_regmap_wr_range,
	.n_no_ranges = ARRAY_SIZE(ad4030_regmap_wr_range),
};

static const struct regmap_config ad4030_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.read_flag_mask = 0x80,
	.rd_table = &ad4030_regmap_rd_table,
	.wr_table = &ad4030_regmap_wr_table,
	.max_register = AD4030_REG_DIG_ERR,
};

static void ad4030_fill_scale_avail(struct ad4030_state *st)
{
	unsigned int mag_bits, int_part, fract_part;
	u64 range;

	/*
	 * The maximum precision of differential channels is retrieved from the
	 * chip properties. The output code of differential channels is in two's
	 * complement format (i.e. signed), so the MSB is the sign bit and only
	 * (precision_bits - 1) bits express voltage magnitude.
	 */
	mag_bits = st->chip->precision_bits - 1;

	for (unsigned int i = 0; i < ARRAY_SIZE(adaq4216_hw_gains_frac); i++) {
		range = mult_frac(st->vref_uv, adaq4216_hw_gains_frac[i][1],
				  adaq4216_hw_gains_frac[i][0]);
		/*
		 * If range were in mV, we would multiply it by NANO below.
		 * Though, range is in µV so multiply it by MICRO only so the
		 * result after right shift and division scales output codes to
		 * millivolts.
		 */
		int_part = div_u64_rem((range * MICRO) >> mag_bits, NANO, &fract_part);
		st->scale_avail[i][0] = int_part;
		st->scale_avail[i][1] = fract_part;
	}
}

static int ad4030_set_pga_gain(struct ad4030_state *st)
{
	DECLARE_BITMAP(bitmap, ADAQ4616_PGA_PINS) = { };

	bitmap_write(bitmap, st->pga_index, 0, ADAQ4616_PGA_PINS);

	return gpiod_multi_set_value_cansleep(st->pga_gpios, bitmap);
}

static int ad4030_set_pga(struct iio_dev *indio_dev, int gain_int, int gain_fract)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	unsigned int mag_bits = st->chip->precision_bits - 1;
	unsigned int tmp;
	u64 gain_nano;

	if (!st->pga_gpios)
		return -EINVAL;

	gain_nano = gain_int * NANO + gain_fract;
	if (!in_range(gain_nano, 1, ADAQ4616_PGA_GAIN_MAX_NANO))
		return -EINVAL;

	tmp = DIV_ROUND_CLOSEST_ULL(gain_nano << mag_bits, NANO);
	gain_nano = DIV_ROUND_CLOSEST(st->vref_uv, tmp);
	st->pga_index = find_closest(gain_nano, adaq4216_hw_gains_vpv,
				     ARRAY_SIZE(adaq4216_hw_gains_vpv));

	return ad4030_set_pga_gain(st);
}

static int ad4030_get_chan_scale(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val,
				 int *val2)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;

	scan_type = iio_get_current_scan_type(indio_dev, chan);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	/* The LSB of the 8-bit common-mode data is always vref/256. */
	if (st->chip->has_pga && scan_type->realbits != 8) {
		*val = st->scale_avail[st->pga_index][0];
		*val2 = st->scale_avail[st->pga_index][1];
		return IIO_VAL_INT_PLUS_NANO;
	}

	if (chan->differential)
		*val = (st->vref_uv * 2) / MILLI;
	else
		*val = st->vref_uv / MILLI;

	*val2 = scan_type->realbits;

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int ad4030_get_chan_calibscale(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int *val,
				      int *val2)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	u16 gain;
	int ret;

	ret = regmap_bulk_read(st->regmap, AD4030_REG_GAIN_CHAN(chan->address),
			       st->rx_data.raw, AD4030_REG_GAIN_BYTES_NB);
	if (ret)
		return ret;

	gain = get_unaligned_be16(st->rx_data.raw);

	/* From datasheet: multiplied output = input × gain word/0x8000 */
	*val = gain / AD4030_GAIN_MIDLE_POINT;
	*val2 = mul_u64_u32_div(gain % AD4030_GAIN_MIDLE_POINT, NANO,
				AD4030_GAIN_MIDLE_POINT);

	return IIO_VAL_INT_PLUS_NANO;
}

/* Returns the offset where 1 LSB = (VREF/2^precision_bits - 1)/gain */
static int ad4030_get_chan_calibbias(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int *val)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	int ret;

	ret = regmap_bulk_read(st->regmap,
			       AD4030_REG_OFFSET_CHAN(chan->address),
			       st->rx_data.raw, AD4030_REG_OFFSET_BYTES_NB);
	if (ret)
		return ret;

	switch (st->chip->precision_bits) {
	case 16:
		*val = sign_extend32(get_unaligned_be16(st->rx_data.raw), 15);
		return IIO_VAL_INT;

	case 24:
		*val = sign_extend32(get_unaligned_be24(st->rx_data.raw), 23);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static void ad4030_get_sampling_freq(struct ad4030_state *st, int *freq)
{
	struct spi_offload_trigger_config *config = &st->offload_trigger_config;

	/*
	 * Conversion data is fetched from the device when the offload transfer
	 * is triggered. Thus, provide the SPI offload trigger frequency as the
	 * sampling frequency.
	 */
	*freq = config->periodic.frequency_hz;
}

static int ad4030_update_conversion_rate(struct ad4030_state *st,
					 unsigned int freq_hz, unsigned int avg_log2)
{
	struct spi_offload_trigger_config *config = &st->offload_trigger_config;
	unsigned int offload_period_ns, cnv_rate_hz;
	struct pwm_waveform cnv_wf = { };
	u64 target = AD4030_TCNVH_NS;
	u64 offload_offset_ns;
	int ret;

	/*
	 * When averaging/oversampling over N samples, we fire the offload
	 * trigger once at every N pulses of the CNV signal. Conversely, the CNV
	 * signal needs to be N times faster than the offload trigger. Take that
	 * into account to correctly re-evaluate both the PWM waveform connected
	 * to CNV and the SPI offload trigger.
	 */
	cnv_rate_hz = freq_hz << avg_log2;

	cnv_wf.period_length_ns = DIV_ROUND_CLOSEST(NSEC_PER_SEC, cnv_rate_hz);
	/*
	 * The datasheet lists a minimum time of 9.8 ns, but no maximum. If the
	 * rounded PWM's value is less than 10, increase the target value by 10
	 * and attempt to round the waveform again, until the value is at least
	 * 10 ns. Use a separate variable to represent the target in case the
	 * rounding is severe enough to keep putting the first few results under
	 * the minimum 10ns condition checked by the while loop.
	 */
	do {
		cnv_wf.duty_length_ns = target;
		ret = pwm_round_waveform_might_sleep(st->cnv_trigger, &cnv_wf);
		if (ret)
			return ret;
		target += AD4030_TCNVH_NS;
	} while (cnv_wf.duty_length_ns < AD4030_TCNVH_NS);

	/*
	 * The CNV waveform period (period_length_ns) might get rounded down by
	 * pwm_round_waveform_might_sleep(). Check the resultant PWM period
	 * is not smaller than the minimum data conversion cycle time.
	 */
	if (!in_range(cnv_wf.period_length_ns, AD4030_TCYC_NS, INT_MAX))
		return -EINVAL;

	offload_period_ns = DIV_ROUND_CLOSEST(NSEC_PER_SEC, freq_hz);

	config->periodic.frequency_hz = DIV_ROUND_UP(HZ_PER_GHZ, offload_period_ns);

	/*
	 * The hardware does the capture on zone 2 (when SPI trigger PWM
	 * is used). This means that the SPI trigger signal should happen at
	 * tsync + tquiet_con_delay being tsync the conversion signal period
	 * and tquiet_con_delay 9.8ns. Hence set the PWM phase accordingly.
	 *
	 * The PWM waveform API only supports nanosecond resolution right now,
	 * so round this setting to the closest available value.
	 */
	offload_offset_ns = AD4030_TQUIET_CNV_DELAY_NS;
	do {
		config->periodic.offset_ns = offload_offset_ns;
		ret = spi_offload_trigger_validate(st->offload_trigger, config);
		if (ret)
			return ret;
		offload_offset_ns += AD4030_TQUIET_CNV_DELAY_NS;
	} while (config->periodic.offset_ns < AD4030_TQUIET_CNV_DELAY_NS);

	st->cnv_wf = cnv_wf;

	return 0;
}

static int ad4030_set_sampling_freq(struct iio_dev *indio_dev, int freq_hz)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	if (freq_hz == 0)
		return -EINVAL;

	if (!in_range(freq_hz, 0, st->chip->max_sample_rate_hz))
		return -ERANGE;

	return ad4030_update_conversion_rate(st, freq_hz, st->avg_log2);
}

static int ad4030_set_chan_calibscale(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int gain_int,
				      int gain_frac)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	u64 gain;

	if (gain_int < 0 || gain_frac < 0)
		return -EINVAL;

	gain = mul_u32_u32(gain_int, MICRO) + gain_frac;

	if (gain > AD4030_REG_GAIN_MAX_GAIN)
		return -EINVAL;

	put_unaligned_be16(DIV_ROUND_CLOSEST_ULL(gain * AD4030_GAIN_MIDLE_POINT,
						 MICRO),
			   st->tx_data);

	return regmap_bulk_write(st->regmap,
				 AD4030_REG_GAIN_CHAN(chan->address),
				 st->tx_data, AD4030_REG_GAIN_BYTES_NB);
}

static int ad4030_set_chan_calibbias(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     int offset)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	if (offset < st->offset_avail[0] || offset > st->offset_avail[2])
		return -EINVAL;

	st->tx_data[2] = 0;

	switch (st->chip->precision_bits) {
	case 16:
		put_unaligned_be16(offset, st->tx_data);
		break;

	case 24:
		put_unaligned_be24(offset, st->tx_data);
		break;

	default:
		return -EINVAL;
	}

	return regmap_bulk_write(st->regmap,
				 AD4030_REG_OFFSET_CHAN(chan->address),
				 st->tx_data, AD4030_REG_OFFSET_BYTES_NB);
}

static int ad4030_set_avg_frame_len(struct iio_dev *dev, int avg_val)
{
	struct ad4030_state *st = iio_priv(dev);
	unsigned int avg_log2 = ilog2(avg_val);
	unsigned int last_avg_idx = ARRAY_SIZE(ad4030_average_modes) - 1;
	int freq_hz;
	int ret;

	if (avg_val < 0 || avg_val > ad4030_average_modes[last_avg_idx])
		return -EINVAL;

	if (st->offload_trigger) {
		/*
		 * The sample averaging and sampling frequency configurations
		 * are mutually dependent on each other. That's because the
		 * effective data sample rate is fCNV / 2^N, where N is the
		 * number of samples being averaged.
		 *
		 * When SPI offload is supported and we have control over the
		 * sample rate, the conversion start signal (CNV) and the SPI
		 * offload trigger frequencies must be re-evaluated so data is
		 * fetched only after 'avg_val' conversions.
		 */
		ad4030_get_sampling_freq(st, &freq_hz);
		ret = ad4030_update_conversion_rate(st, freq_hz, avg_log2);
		if (ret)
			return ret;
	}

	ret = regmap_write(st->regmap, AD4030_REG_AVG,
			   AD4030_REG_AVG_MASK_AVG_SYNC |
			   FIELD_PREP(AD4030_REG_AVG_MASK_AVG_VAL, avg_log2));
	if (ret)
		return ret;

	st->avg_log2 = avg_log2;

	return 0;
}

static bool ad4030_is_common_byte_asked(struct ad4030_state *st,
					unsigned int mask)
{
	return mask & (st->chip->num_voltage_inputs == 1 ?
		AD4030_SINGLE_COMMON_BYTE_CHANNELS_MASK :
		AD4030_DUAL_COMMON_BYTE_CHANNELS_MASK);
}

static int ad4030_set_mode(struct iio_dev *indio_dev, unsigned long mask)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	if (st->avg_log2 > 0) {
		st->mode = AD4030_OUT_DATA_MD_30_AVERAGED_DIFF;
	} else if (ad4030_is_common_byte_asked(st, mask)) {
		switch (st->chip->precision_bits) {
		case 16:
			st->mode = AD4030_OUT_DATA_MD_16_DIFF_8_COM;
			break;

		case 24:
			st->mode = AD4030_OUT_DATA_MD_24_DIFF_8_COM;
			break;

		default:
			return -EINVAL;
		}
	} else {
		st->mode = AD4030_OUT_DATA_MD_DIFF;
	}

	return regmap_update_bits(st->regmap, AD4030_REG_MODES,
				  AD4030_REG_MODES_MASK_OUT_DATA_MODE,
				  st->mode);
}

/*
 * Descramble 2 32bits numbers out of a 64bits. The bits are interleaved:
 * 1 bit for first number, 1 bit for the second, and so on...
 */
static void ad4030_extract_interleaved(u8 *src, u32 *ch0, u32 *ch1)
{
	u8 h0, h1, l0, l1;
	u32 out0, out1;
	u8 *out0_raw = (u8 *)&out0;
	u8 *out1_raw = (u8 *)&out1;

	for (int i = 0; i < 4; i++) {
		h0 = src[i * 2];
		l1 = src[i * 2 + 1];
		h1 = h0 << 1;
		l0 = l1 >> 1;

		h0 &= 0xAA;
		l0 &= 0x55;
		h1 &= 0xAA;
		l1 &= 0x55;

		h0 = (h0 | h0 << 001) & 0xCC;
		h1 = (h1 | h1 << 001) & 0xCC;
		l0 = (l0 | l0 >> 001) & 0x33;
		l1 = (l1 | l1 >> 001) & 0x33;
		h0 = (h0 | h0 << 002) & 0xF0;
		h1 = (h1 | h1 << 002) & 0xF0;
		l0 = (l0 | l0 >> 002) & 0x0F;
		l1 = (l1 | l1 >> 002) & 0x0F;

		out0_raw[i] = h0 | l0;
		out1_raw[i] = h1 | l1;
	}

	*ch0 = out0;
	*ch1 = out1;
}

static int ad4030_conversion(struct iio_dev *indio_dev)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	const struct iio_scan_type *scan_type;
	unsigned char diff_realbytes, diff_storagebytes;
	unsigned int bytes_to_read;
	unsigned long cnv_nb = BIT(st->avg_log2);
	unsigned int i;
	int ret;

	scan_type = iio_get_current_scan_type(indio_dev, st->chip->channels);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	diff_realbytes = BITS_TO_BYTES(scan_type->realbits);
	diff_storagebytes = BITS_TO_BYTES(scan_type->storagebits);

	/* Number of bytes for one differential channel */
	bytes_to_read = diff_realbytes;
	/* Add one byte if we are using a differential + common byte mode */
	bytes_to_read += (st->mode == AD4030_OUT_DATA_MD_24_DIFF_8_COM ||
			st->mode == AD4030_OUT_DATA_MD_16_DIFF_8_COM) ? 1 : 0;
	/* Multiply by the number of hardware channels */
	bytes_to_read *= st->chip->num_voltage_inputs;

	for (i = 0; i < cnv_nb; i++) {
		gpiod_set_value_cansleep(st->cnv_gpio, 1);
		ndelay(AD4030_TCNVH_NS);
		gpiod_set_value_cansleep(st->cnv_gpio, 0);
		ndelay(st->chip->tcyc_ns);
	}

	ret = spi_read(st->spi, st->rx_data.raw, bytes_to_read);
	if (ret)
		return ret;

	if (st->chip->num_voltage_inputs == 2)
		ad4030_extract_interleaved(st->rx_data.raw,
					   &st->rx_data.dual.diff[0],
					   &st->rx_data.dual.diff[1]);

	/*
	 * If no common mode voltage channel is enabled, we can use the raw
	 * data as is. Otherwise, we need to rearrange the data a bit to match
	 * the natural alignment of the IIO buffer.
	 */

	if (st->mode != AD4030_OUT_DATA_MD_16_DIFF_8_COM &&
	    st->mode != AD4030_OUT_DATA_MD_24_DIFF_8_COM)
		return 0;

	if (st->chip->num_voltage_inputs == 1) {
		st->rx_data.single.common = st->rx_data.raw[diff_realbytes];
		return 0;
	}

	for (i = 0; i < st->chip->num_voltage_inputs; i++)
		st->rx_data.dual.common[i] =
			st->rx_data.raw[diff_storagebytes * i + diff_realbytes];

	return 0;
}

static int ad4030_single_conversion(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan, int *val)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4030_set_mode(indio_dev, BIT(chan->scan_index));
	if (ret)
		return ret;

	ret = ad4030_conversion(indio_dev);
	if (ret)
		return ret;

	if (chan->differential)
		if (st->chip->num_voltage_inputs == 1)
			*val = st->rx_data.single.diff;
		else
			*val = st->rx_data.dual.diff[chan->address];
	else
		if (st->chip->num_voltage_inputs == 1)
			*val = st->rx_data.single.common;
		else
			*val = st->rx_data.dual.common[chan->address];

	return IIO_VAL_INT;
}

static irqreturn_t ad4030_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad4030_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4030_conversion(indio_dev);
	if (ret)
		goto out;

	iio_push_to_buffers_with_ts(indio_dev, &st->rx_data, sizeof(st->rx_data),
				    pf->timestamp);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const int ad4030_gain_avail[3][2] = {
	{ 0, 0 },
	{ 0, 30518 },
	{ 1, 999969482 },
};

static int ad4030_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel,
			     const int **vals, int *type,
			     int *length, long mask)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		*vals = st->offset_avail;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;

	case IIO_CHAN_INFO_CALIBSCALE:
		*vals = (void *)ad4030_gain_avail;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_RANGE;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*vals = ad4030_average_modes;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(ad4030_average_modes);
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_SCALE:
		if (st->scale_avail_size == 1)
			*vals = (int *)st->scale_avail[st->pga_index];
		else
			*vals = (int *)st->scale_avail;
		*length = st->scale_avail_size * 2; /* print int and nano part */
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static int ad4030_read_raw_dispatch(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, int *val,
				    int *val2, long info)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return ad4030_single_conversion(indio_dev, chan, val);

	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4030_get_chan_calibscale(indio_dev, chan, val, val2);

	case IIO_CHAN_INFO_CALIBBIAS:
		return ad4030_get_chan_calibbias(indio_dev, chan, val);

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = BIT(st->avg_log2);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SAMP_FREQ:
		ad4030_get_sampling_freq(st, val);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ad4030_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long info)
{
	int ret;

	if (info == IIO_CHAN_INFO_SCALE)
		return ad4030_get_chan_scale(indio_dev, chan, val, val2);

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = ad4030_read_raw_dispatch(indio_dev, chan, val, val2, info);

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4030_write_raw_dispatch(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan, int val,
				     int val2, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4030_set_chan_calibscale(indio_dev, chan, val, val2);

	case IIO_CHAN_INFO_CALIBBIAS:
		if (val2 != 0)
			return -EINVAL;
		return ad4030_set_chan_calibbias(indio_dev, chan, val);

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return ad4030_set_avg_frame_len(indio_dev, val);

	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4030_set_sampling_freq(indio_dev, val);

	case IIO_CHAN_INFO_SCALE:
		return ad4030_set_pga(indio_dev, val, val2);

	default:
		return -EINVAL;
	}
}

static int ad4030_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long info)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = ad4030_write_raw_dispatch(indio_dev, chan, val, val2, info);

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ad4030_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int ad4030_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	const struct ad4030_state *st = iio_priv(indio_dev);
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

static int ad4030_read_label(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     char *label)
{
	if (chan->differential)
		return sysfs_emit(label, "differential%lu\n", chan->address);
	return sysfs_emit(label, "common-mode%lu\n", chan->address);
}

static int ad4030_get_current_scan_type(const struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	return st->avg_log2 ? AD4030_SCAN_TYPE_AVG : AD4030_SCAN_TYPE_NORMAL;
}

static int ad4030_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	return ad4030_set_mode(indio_dev, *scan_mask);
}

static const struct iio_info ad4030_iio_info = {
	.read_avail = ad4030_read_avail,
	.read_raw = ad4030_read_raw,
	.write_raw = ad4030_write_raw,
	.write_raw_get_fmt = &ad4030_write_raw_get_fmt,
	.debugfs_reg_access = ad4030_reg_access,
	.read_label = ad4030_read_label,
	.get_current_scan_type = ad4030_get_current_scan_type,
	.update_scan_mode  = ad4030_update_scan_mode,
};

static bool ad4030_validate_scan_mask(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	/* Asking for both common channels and averaging */
	if (st->avg_log2 && ad4030_is_common_byte_asked(st, *scan_mask))
		return false;

	return true;
}

static const struct iio_buffer_setup_ops ad4030_buffer_setup_ops = {
	.validate_scan_mask = ad4030_validate_scan_mask,
};

static void ad4030_prepare_offload_msg(struct iio_dev *indio_dev)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	u8 offload_bpw;

	if (st->mode == AD4030_OUT_DATA_MD_30_AVERAGED_DIFF)
		offload_bpw = 32;
	else
		offload_bpw = st->chip->precision_bits;

	st->offload_xfer.bits_per_word = offload_bpw;
	st->offload_xfer.len = spi_bpw_to_bytes(offload_bpw);
	st->offload_xfer.offload_flags = SPI_OFFLOAD_XFER_RX_STREAM;
	spi_message_init_with_transfers(&st->offload_msg, &st->offload_xfer, 1);
}

static int ad4030_offload_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	unsigned int reg_modes;
	int ret;

	/*
	 * When data from 2 analog input channels is output through a single
	 * bus line (interleaved mode (LANE_MD == 0b11)) and gets pushed through
	 * DMA, extra hardware is required to do the de-interleaving. While we
	 * don't support such hardware configurations, disallow interleaved mode
	 * when using SPI offload.
	 */
	ret = regmap_read(st->regmap, AD4030_REG_MODES, &reg_modes);
	if (ret)
		return ret;

	if (st->chip->num_voltage_inputs > 1 &&
	    FIELD_GET(AD4030_REG_MODES_MASK_LANE_MODE, reg_modes) == AD4030_LANE_MD_INTERLEAVED)
		return -EINVAL;

	ad4030_prepare_offload_msg(indio_dev);
	st->offload_msg.offload = st->offload;
	ret = spi_optimize_message(st->spi, &st->offload_msg);
	if (ret)
		return ret;

	ret = pwm_set_waveform_might_sleep(st->cnv_trigger, &st->cnv_wf, false);
	if (ret)
		goto out_unoptimize;

	ret = spi_offload_trigger_enable(st->offload, st->offload_trigger,
					 &st->offload_trigger_config);
	if (ret)
		goto out_pwm_disable;

	return 0;

out_pwm_disable:
	pwm_disable(st->cnv_trigger);
out_unoptimize:
	spi_unoptimize_message(&st->offload_msg);

	return ret;
}

static int ad4030_offload_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	spi_offload_trigger_disable(st->offload, st->offload_trigger);

	pwm_disable(st->cnv_trigger);

	spi_unoptimize_message(&st->offload_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ad4030_offload_buffer_setup_ops = {
	.postenable = &ad4030_offload_buffer_postenable,
	.predisable = &ad4030_offload_buffer_predisable,
};

static int ad4030_regulators_get(struct ad4030_state *st)
{
	struct device *dev = &st->spi->dev;
	static const char * const ids[] = { "vdd-5v", "vdd-1v8" };
	int ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ids), ids);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	st->vio_uv = devm_regulator_get_enable_read_voltage(dev, "vio");
	if (st->vio_uv < 0)
		return dev_err_probe(dev, st->vio_uv,
				     "Failed to enable and read vio voltage\n");

	st->vref_uv = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (st->vref_uv < 0) {
		if (st->vref_uv != -ENODEV)
			return dev_err_probe(dev, st->vref_uv,
					     "Failed to read ref voltage\n");

		/* if not using optional REF, the REFIN must be used */
		st->vref_uv = devm_regulator_get_enable_read_voltage(dev,
								     "refin");
		if (st->vref_uv < 0)
			return dev_err_probe(dev, st->vref_uv,
					     "Failed to read refin voltage\n");
	}

	return 0;
}

static int ad4030_reset(struct ad4030_state *st)
{
	struct device *dev = &st->spi->dev;
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "Failed to get reset GPIO\n");

	if (reset) {
		ndelay(50);
		gpiod_set_value_cansleep(reset, 0);
		return 0;
	}

	return regmap_write(st->regmap, AD4030_REG_INTERFACE_CONFIG_A,
			   AD4030_REG_INTERFACE_CONFIG_A_SW_RESET);
}

static int ad4030_detect_chip_info(const struct ad4030_state *st)
{
	unsigned int grade;
	int ret;

	ret = regmap_read(st->regmap, AD4030_REG_CHIP_GRADE, &grade);
	if (ret)
		return ret;

	grade = FIELD_GET(AD4030_REG_CHIP_GRADE_MASK_CHIP_GRADE, grade);
	if (grade != st->chip->grade)
		dev_warn(&st->spi->dev, "Unknown grade(0x%x) for %s\n", grade,
			 st->chip->name);

	return 0;
}

static int ad4030_pwm_get(struct ad4030_state *st)
{
	struct device *dev = &st->spi->dev;

	st->cnv_trigger = devm_pwm_get(dev, NULL);
	if (IS_ERR(st->cnv_trigger))
		return dev_err_probe(dev, PTR_ERR(st->cnv_trigger),
				     "Failed to get CNV PWM\n");

	/*
	 * Preemptively disable the PWM, since we only want to enable it with
	 * the buffer.
	 */
	pwm_disable(st->cnv_trigger);

	return 0;
}

static int ad4030_config(struct ad4030_state *st)
{
	int ret;
	u8 reg_modes;

	st->offset_avail[0] = (int)BIT(st->chip->precision_bits - 1) * -1;
	st->offset_avail[1] = 1;
	st->offset_avail[2] = BIT(st->chip->precision_bits - 1) - 1;

	if (st->chip->num_voltage_inputs > 1)
		reg_modes = FIELD_PREP(AD4030_REG_MODES_MASK_LANE_MODE,
				       AD4030_LANE_MD_INTERLEAVED);
	else
		reg_modes = FIELD_PREP(AD4030_REG_MODES_MASK_LANE_MODE,
				       AD4030_LANE_MD_1_PER_CH);

	ret = regmap_write(st->regmap, AD4030_REG_MODES, reg_modes);
	if (ret)
		return ret;

	if (st->vio_uv < AD4030_VIO_THRESHOLD_UV)
		return regmap_write(st->regmap, AD4030_REG_IO,
				    AD4030_REG_IO_MASK_IO2X);

	return 0;
}

static int ad4030_spi_offload_setup(struct iio_dev *indio_dev,
				    struct ad4030_state *st)
{
	struct device *dev = &st->spi->dev;
	struct dma_chan *rx_dma;

	indio_dev->setup_ops = &ad4030_offload_buffer_setup_ops;

	st->offload_trigger = devm_spi_offload_trigger_get(dev, st->offload,
							   SPI_OFFLOAD_TRIGGER_PERIODIC);
	if (IS_ERR(st->offload_trigger))
		return dev_err_probe(dev, PTR_ERR(st->offload_trigger),
				     "failed to get offload trigger\n");

	st->offload_trigger_config.type = SPI_OFFLOAD_TRIGGER_PERIODIC;

	rx_dma = devm_spi_offload_rx_stream_request_dma_chan(dev, st->offload);
	if (IS_ERR(rx_dma))
		return dev_err_probe(dev, PTR_ERR(rx_dma),
				     "failed to get offload RX DMA\n");

	return devm_iio_dmaengine_buffer_setup_with_handle(dev, indio_dev, rx_dma,
							   IIO_BUFFER_DIRECTION_IN);
}

static int ad4030_setup_pga(struct device *dev, struct iio_dev *indio_dev,
			    struct ad4030_state *st)
{
	/* Setup GPIOs for PGA control */
	st->pga_gpios = devm_gpiod_get_array(dev, "pga", GPIOD_OUT_LOW);
	if (IS_ERR(st->pga_gpios))
		return dev_err_probe(dev, PTR_ERR(st->pga_gpios),
				     "Failed to get PGA gpios.\n");

	if (st->pga_gpios->ndescs != ADAQ4616_PGA_PINS)
		return dev_err_probe(dev, -EINVAL,
				     "Expected %d GPIOs for PGA control.\n",
				     ADAQ4616_PGA_PINS);

	st->scale_avail_size = ARRAY_SIZE(adaq4216_hw_gains_vpv);
	st->pga_index = 0;

	return 0;
}

static int ad4030_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad4030_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	st->regmap = devm_regmap_init(dev, &ad4030_regmap_bus, st,
				      &ad4030_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	st->chip = spi_get_device_match_data(spi);
	if (!st->chip)
		return -EINVAL;

	ret = ad4030_regulators_get(st);
	if (ret)
		return ret;

	/*
	 * From datasheet: "Perform a reset no sooner than 3ms after the power
	 * supplies are valid and stable"
	 */
	fsleep(3000);

	ret = ad4030_reset(st);
	if (ret)
		return ret;

	ret = ad4030_detect_chip_info(st);
	if (ret)
		return ret;

	if (st->chip->has_pga) {
		ret = ad4030_setup_pga(dev, indio_dev, st);
		if (ret)
			return ret;

		ad4030_fill_scale_avail(st);
	}

	ret = ad4030_config(st);
	if (ret)
		return ret;

	st->cnv_gpio = devm_gpiod_get(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "Failed to get cnv gpio\n");

	indio_dev->name = st->chip->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad4030_iio_info;
	indio_dev->available_scan_masks = st->chip->available_masks;

	st->offload = devm_spi_offload_get(dev, spi, &ad4030_offload_config);
	ret = PTR_ERR_OR_ZERO(st->offload);
	/* Fall back to low speed usage when no SPI offload is available. */
	if (ret == -ENODEV) {
		/*
		 * One hardware channel is split in two software channels when
		 * using common byte mode. Add one more channel for the timestamp.
		 */
		indio_dev->num_channels = 2 * st->chip->num_voltage_inputs + 1;
		indio_dev->channels = st->chip->channels;

		ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
						      iio_pollfunc_store_time,
						      ad4030_trigger_handler,
						      &ad4030_buffer_setup_ops);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to setup triggered buffer\n");
	} else if (ret) {
		return dev_err_probe(dev, ret, "failed to get offload\n");
	} else {
		/*
		 * Offloaded SPI transfers can't support software timestamp so
		 * no additional timestamp channel is added.
		 */
		indio_dev->num_channels = st->chip->num_voltage_inputs;
		indio_dev->channels = st->chip->offload_channels;
		ret = ad4030_spi_offload_setup(indio_dev, st);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to setup SPI offload\n");

		ret = ad4030_pwm_get(st);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get PWM\n");

		/*
		 * Start with a slower sampling rate so there is some room for
		 * adjusting the sample averaging and the sampling frequency
		 * without hitting the maximum conversion rate.
		 */
		ret = ad4030_update_conversion_rate(st, st->chip->max_sample_rate_hz >> 4,
						    st->avg_log2);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to set offload samp freq\n");
	}

	return devm_iio_device_register(dev, indio_dev);
}

static const unsigned long ad4030_channel_masks[] = {
	/* Differential only */
	BIT(0),
	/* Differential and common-mode voltage */
	GENMASK(1, 0),
	0,
};

static const unsigned long ad4630_channel_masks[] = {
	/* Differential only */
	BIT(1) | BIT(0),
	/* Differential with common byte */
	GENMASK(3, 0),
	0,
};

static const struct iio_scan_type ad4030_24_scan_types[] = {
	[AD4030_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.storagebits = 32,
		.realbits = 24,
		.shift = 8,
		.endianness = IIO_BE,
	},
	[AD4030_SCAN_TYPE_AVG] = {
		.sign = 's',
		.storagebits = 32,
		.realbits = 30,
		.shift = 2,
		.endianness = IIO_BE,
	},
};

static const struct iio_scan_type ad4030_24_offload_scan_types[] = {
	[AD4030_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 24,
		.storagebits = 32,
		.shift = 0,
		.endianness = IIO_CPU,
	},
	[AD4030_SCAN_TYPE_AVG] = {
		.sign = 's',
		.realbits = 30,
		.storagebits = 32,
		.shift = 2,
		.endianness = IIO_CPU,
	},
};

static const struct iio_scan_type ad4030_16_scan_types[] = {
	[AD4030_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 32,
		.shift = 16,
		.endianness = IIO_BE,
	},
	[AD4030_SCAN_TYPE_AVG] = {
		.sign = 's',
		.storagebits = 32,
		.realbits = 30,
		.shift = 2,
		.endianness = IIO_BE,
	}
};

static const struct iio_scan_type ad4030_16_offload_scan_types[] = {
	[AD4030_SCAN_TYPE_NORMAL] = {
		.sign = 's',
		.realbits = 16,
		.storagebits = 32,
		.shift = 0,
		.endianness = IIO_CPU,
	},
	[AD4030_SCAN_TYPE_AVG] = {
		.sign = 's',
		.realbits = 30,
		.storagebits = 32,
		.shift = 2,
		.endianness = IIO_CPU,
	},
};

static const struct ad4030_chip_info ad4030_24_chip_info = {
	.name = "ad4030-24",
	.available_masks = ad4030_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, ad4030_24_scan_types),
		AD4030_CHAN_CMO(1, 0),
		IIO_CHAN_SOFT_TIMESTAMP(2),
	},
	.offload_channels = {
		AD4030_OFFLOAD_CHAN_DIFF(0, ad4030_24_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4030_24_GRADE,
	.precision_bits = 24,
	.num_voltage_inputs = 1,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 2 * HZ_PER_MHZ,
};

static const struct ad4030_chip_info ad4630_16_chip_info = {
	.name = "ad4630-16",
	.available_masks = ad4630_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, ad4030_16_scan_types),
		AD4030_CHAN_DIFF(1, ad4030_16_scan_types),
		AD4030_CHAN_CMO(2, 0),
		AD4030_CHAN_CMO(3, 1),
		IIO_CHAN_SOFT_TIMESTAMP(4),
	},
	.offload_channels = {
		AD4030_OFFLOAD_CHAN_DIFF(0, ad4030_16_offload_scan_types),
		AD4030_OFFLOAD_CHAN_DIFF(1, ad4030_16_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4630_16_GRADE,
	.precision_bits = 16,
	.num_voltage_inputs = 2,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 2 * HZ_PER_MHZ,
};

static const struct ad4030_chip_info ad4630_24_chip_info = {
	.name = "ad4630-24",
	.available_masks = ad4630_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, ad4030_24_scan_types),
		AD4030_CHAN_DIFF(1, ad4030_24_scan_types),
		AD4030_CHAN_CMO(2, 0),
		AD4030_CHAN_CMO(3, 1),
		IIO_CHAN_SOFT_TIMESTAMP(4),
	},
	.offload_channels = {
		AD4030_OFFLOAD_CHAN_DIFF(0, ad4030_24_offload_scan_types),
		AD4030_OFFLOAD_CHAN_DIFF(1, ad4030_24_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4630_24_GRADE,
	.precision_bits = 24,
	.num_voltage_inputs = 2,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 2 * HZ_PER_MHZ,
};

static const struct ad4030_chip_info ad4632_16_chip_info = {
	.name = "ad4632-16",
	.available_masks = ad4630_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, ad4030_16_scan_types),
		AD4030_CHAN_DIFF(1, ad4030_16_scan_types),
		AD4030_CHAN_CMO(2, 0),
		AD4030_CHAN_CMO(3, 1),
		IIO_CHAN_SOFT_TIMESTAMP(4),
	},
	.offload_channels = {
		AD4030_OFFLOAD_CHAN_DIFF(0, ad4030_16_offload_scan_types),
		AD4030_OFFLOAD_CHAN_DIFF(1, ad4030_16_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4632_16_GRADE,
	.precision_bits = 16,
	.num_voltage_inputs = 2,
	.tcyc_ns = AD4632_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 500 * HZ_PER_KHZ,
};

static const struct ad4030_chip_info ad4632_24_chip_info = {
	.name = "ad4632-24",
	.available_masks = ad4630_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, ad4030_24_scan_types),
		AD4030_CHAN_DIFF(1, ad4030_24_scan_types),
		AD4030_CHAN_CMO(2, 0),
		AD4030_CHAN_CMO(3, 1),
		IIO_CHAN_SOFT_TIMESTAMP(4),
	},
	.offload_channels = {
		AD4030_OFFLOAD_CHAN_DIFF(0, ad4030_24_offload_scan_types),
		AD4030_OFFLOAD_CHAN_DIFF(1, ad4030_24_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4632_24_GRADE,
	.precision_bits = 24,
	.num_voltage_inputs = 2,
	.tcyc_ns = AD4632_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 500 * HZ_PER_KHZ,
};

static const struct ad4030_chip_info adaq4216_chip_info = {
	.name = "adaq4216",
	.available_masks = ad4030_channel_masks,
	.channels = {
		ADAQ4216_CHAN_DIFF(0, ad4030_16_scan_types),
		AD4030_CHAN_CMO(1, 0),
		IIO_CHAN_SOFT_TIMESTAMP(2),
	},
	.offload_channels = {
		ADAQ4216_OFFLOAD_CHAN_DIFF(0, ad4030_16_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_ADAQ4216_GRADE,
	.precision_bits = 16,
	.has_pga = true,
	.num_voltage_inputs = 1,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 2 * HZ_PER_MHZ,
};

static const struct ad4030_chip_info adaq4224_chip_info = {
	.name = "adaq4224",
	.available_masks = ad4030_channel_masks,
	.channels = {
		ADAQ4216_CHAN_DIFF(0, ad4030_24_scan_types),
		AD4030_CHAN_CMO(1, 0),
		IIO_CHAN_SOFT_TIMESTAMP(2),
	},
	.offload_channels = {
		ADAQ4216_OFFLOAD_CHAN_DIFF(0, ad4030_24_offload_scan_types),
	},
	.grade = AD4030_REG_CHIP_GRADE_ADAQ4224_GRADE,
	.precision_bits = 24,
	.has_pga = true,
	.num_voltage_inputs = 1,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
	.max_sample_rate_hz = 2 * HZ_PER_MHZ,
};

static const struct spi_device_id ad4030_id_table[] = {
	{ .name = "ad4030-24", .driver_data = (kernel_ulong_t)&ad4030_24_chip_info },
	{ .name = "ad4630-16", .driver_data = (kernel_ulong_t)&ad4630_16_chip_info },
	{ .name = "ad4630-24", .driver_data = (kernel_ulong_t)&ad4630_24_chip_info },
	{ .name = "ad4632-16", .driver_data = (kernel_ulong_t)&ad4632_16_chip_info },
	{ .name = "ad4632-24", .driver_data = (kernel_ulong_t)&ad4632_24_chip_info },
	{ .name = "adaq4216", .driver_data = (kernel_ulong_t)&adaq4216_chip_info },
	{ .name = "adaq4224", .driver_data = (kernel_ulong_t)&adaq4224_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4030_id_table);

static const struct of_device_id ad4030_of_match[] = {
	{ .compatible = "adi,ad4030-24", .data = &ad4030_24_chip_info },
	{ .compatible = "adi,ad4630-16", .data = &ad4630_16_chip_info },
	{ .compatible = "adi,ad4630-24", .data = &ad4630_24_chip_info },
	{ .compatible = "adi,ad4632-16", .data = &ad4632_16_chip_info },
	{ .compatible = "adi,ad4632-24", .data = &ad4632_24_chip_info },
	{ .compatible = "adi,adaq4216", .data = &adaq4216_chip_info },
	{ .compatible = "adi,adaq4224", .data = &adaq4224_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4030_of_match);

static struct spi_driver ad4030_driver = {
	.driver = {
		.name = "ad4030",
		.of_match_table = ad4030_of_match,
	},
	.probe = ad4030_probe,
	.id_table = ad4030_id_table,
};
module_spi_driver(ad4030_driver);

MODULE_AUTHOR("Esteban Blanc <eblanc@baylibre.com>");
MODULE_DESCRIPTION("Analog Devices AD4630 ADC family driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
