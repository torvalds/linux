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
#include <linux/clk.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#define AD4030_REG_INTERFACE_CONFIG_A			0x00
#define     AD4030_REG_INTERFACE_CONFIG_A_SW_RESET	(BIT(0) | BIT(7))
#define AD4030_REG_INTERFACE_CONFIG_B			0x01
#define AD4030_REG_DEVICE_CONFIG			0x02
#define AD4030_REG_CHIP_TYPE				0x03
#define AD4030_REG_PRODUCT_ID_L				0x04
#define AD4030_REG_PRODUCT_ID_H				0x05
#define AD4030_REG_CHIP_GRADE				0x06
#define     AD4030_REG_CHIP_GRADE_AD4030_24_GRADE	0x10
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

enum ad4030_out_mode {
	AD4030_OUT_DATA_MD_DIFF,
	AD4030_OUT_DATA_MD_16_DIFF_8_COM,
	AD4030_OUT_DATA_MD_24_DIFF_8_COM,
	AD4030_OUT_DATA_MD_30_AVERAGED_DIFF,
	AD4030_OUT_DATA_MD_32_PATTERN,
};

struct ad4030_chip_info {
	const char *name;
	const unsigned long *available_masks;
	const struct iio_chan_spec channels[AD4030_MAX_IIO_CHANNEL_NB];
	u8 grade;
	u8 precision_bits;
	/* Number of hardware channels */
	int num_voltage_inputs;
	unsigned int tcyc_ns;
};

struct ad4030_state {
	struct spi_device *spi;
	struct regmap *regmap;
	const struct ad4030_chip_info *chip;
	struct gpio_desc *cnv_gpio;
	int vref_uv;
	int vio_uv;
	int offset_avail[3];
	enum ad4030_out_mode mode;

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
		};
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
#define AD4030_CHAN_DIFF(_idx, _storage, _real, _shift) {		\
	.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_CALIBSCALE) |				\
		BIT(IIO_CHAN_INFO_CALIBBIAS) |				\
		BIT(IIO_CHAN_INFO_RAW),					\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_CALIBBIAS) |	\
		BIT(IIO_CHAN_INFO_CALIBSCALE),				\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.address = (_idx),						\
	.channel = (_idx) * 2,						\
	.channel2 = (_idx) * 2 + 1,					\
	.scan_index = (_idx),						\
	.differential = true,						\
	.scan_type = {							\
		.sign = 's',						\
		.storagebits = _storage,				\
		.realbits = _real,					\
		.shift = _shift,					\
		.endianness = IIO_BE,					\
	},								\
}

static int ad4030_enter_config_mode(struct ad4030_state *st)
{
	st->tx_data[0] = AD4030_REG_ACCESS;

	struct spi_transfer xfer = {
		.tx_buf = st->tx_data,
		.bits_per_word = 8,
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
		.bits_per_word = 8,
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
		.bits_per_word = 8,
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
		.bits_per_word = 8,
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

static int ad4030_get_chan_scale(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val,
				 int *val2)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	if (chan->differential) {
		*val = (st->vref_uv * 2) / MILLI;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	*val = st->vref_uv / MILLI;
	*val2 = chan->scan_type.realbits;
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

static bool ad4030_is_common_byte_asked(struct ad4030_state *st,
					unsigned int mask)
{
	return mask & AD4030_SINGLE_COMMON_BYTE_CHANNELS_MASK;
}

static int ad4030_set_mode(struct iio_dev *indio_dev, unsigned long mask)
{
	struct ad4030_state *st = iio_priv(indio_dev);

	if (ad4030_is_common_byte_asked(st, mask))
		st->mode = AD4030_OUT_DATA_MD_24_DIFF_8_COM;
	else
		st->mode = AD4030_OUT_DATA_MD_DIFF;

	return regmap_update_bits(st->regmap, AD4030_REG_MODES,
				  AD4030_REG_MODES_MASK_OUT_DATA_MODE,
				  st->mode);
}

static int ad4030_conversion(struct iio_dev *indio_dev)
{
	struct ad4030_state *st = iio_priv(indio_dev);
	const struct iio_scan_type scan_type = indio_dev->channels->scan_type;
	unsigned char diff_realbytes = BITS_TO_BYTES(scan_type.realbits);
	unsigned int bytes_to_read;
	int ret;

	/* Number of bytes for one differential channel */
	bytes_to_read = diff_realbytes;
	/* Add one byte if we are using a differential + common byte mode */
	bytes_to_read += (st->mode == AD4030_OUT_DATA_MD_24_DIFF_8_COM ||
			st->mode == AD4030_OUT_DATA_MD_16_DIFF_8_COM) ? 1 : 0;
	/* Mulitiply by the number of hardware channels */
	bytes_to_read *= st->chip->num_voltage_inputs;

	gpiod_set_value_cansleep(st->cnv_gpio, 1);
	ndelay(AD4030_TCNVH_NS);
	gpiod_set_value_cansleep(st->cnv_gpio, 0);
	ndelay(st->chip->tcyc_ns);

	ret = spi_read(st->spi, st->rx_data.raw, bytes_to_read);
	if (ret)
		return ret;

	if (st->mode != AD4030_OUT_DATA_MD_24_DIFF_8_COM)
		return 0;

	st->rx_data.common = st->rx_data.raw[diff_realbytes];

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
		*val = st->rx_data.diff;
	else
		*val = st->rx_data.common;

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

	iio_push_to_buffers_with_timestamp(indio_dev, st->rx_data.raw,
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

	default:
		return -EINVAL;
	}
}

static int ad4030_read_raw_dispatch(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, int *val,
				    int *val2, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return ad4030_single_conversion(indio_dev, chan, val);

	case IIO_CHAN_INFO_CALIBSCALE:
		return ad4030_get_chan_calibscale(indio_dev, chan, val, val2);

	case IIO_CHAN_INFO_CALIBBIAS:
		return ad4030_get_chan_calibbias(indio_dev, chan, val);

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

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	ret = ad4030_read_raw_dispatch(indio_dev, chan, val, val2, info);

	iio_device_release_direct_mode(indio_dev);

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

	default:
		return -EINVAL;
	}
}

static int ad4030_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val,
			    int val2, long info)
{
	int ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	ret = ad4030_write_raw_dispatch(indio_dev, chan, val, val2, info);

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int ad4030_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	const struct ad4030_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	if (readval)
		ret = regmap_read(st->regmap, reg, readval);
	else
		ret = regmap_write(st->regmap, reg, writeval);

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int ad4030_read_label(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     char *label)
{
	if (chan->differential)
		return sprintf(label, "differential%lu\n", chan->address);
	return sprintf(label, "common-mode%lu\n", chan->address);
}

static const struct iio_info ad4030_iio_info = {
	.read_avail = ad4030_read_avail,
	.read_raw = ad4030_read_raw,
	.write_raw = ad4030_write_raw,
	.debugfs_reg_access = ad4030_reg_access,
	.read_label = ad4030_read_label,
};

static int ad4030_buffer_preenable(struct iio_dev *indio_dev)
{
	return ad4030_set_mode(indio_dev, *indio_dev->active_scan_mask);
}

static const struct iio_buffer_setup_ops ad4030_buffer_setup_ops = {
	.preenable = ad4030_buffer_preenable,
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

static int ad4030_config(struct ad4030_state *st)
{
	st->offset_avail[0] = (int)BIT(st->chip->precision_bits - 1) * -1;
	st->offset_avail[1] = 1;
	st->offset_avail[2] = BIT(st->chip->precision_bits - 1) - 1;

	if (st->vio_uv < AD4030_VIO_THRESHOLD_UV)
		return regmap_write(st->regmap, AD4030_REG_IO,
				    AD4030_REG_IO_MASK_IO2X);

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
		dev_err_probe(dev, PTR_ERR(st->regmap),
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

	ret = ad4030_config(st);
	if (ret)
		return ret;

	st->cnv_gpio = devm_gpiod_get(dev, "cnv", GPIOD_OUT_LOW);
	if (IS_ERR(st->cnv_gpio))
		return dev_err_probe(dev, PTR_ERR(st->cnv_gpio),
				     "Failed to get cnv gpio\n");

	/*
	 * One hardware channel is split in two software channels when using
	 * common byte mode. Add one more channel for the timestamp.
	 */
	indio_dev->num_channels = 2 * st->chip->num_voltage_inputs + 1;
	indio_dev->name = st->chip->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad4030_iio_info;
	indio_dev->channels = st->chip->channels;
	indio_dev->available_scan_masks = st->chip->available_masks;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      ad4030_trigger_handler,
					      &ad4030_buffer_setup_ops);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to setup triggered buffer\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const unsigned long ad4030_channel_masks[] = {
	/* Differential only */
	BIT(0),
	/* Differential and common-mode voltage */
	GENMASK(1, 0),
	0,
};

static const struct ad4030_chip_info ad4030_24_chip_info = {
	.name = "ad4030-24",
	.available_masks = ad4030_channel_masks,
	.channels = {
		AD4030_CHAN_DIFF(0, 32, 24, 8),
		AD4030_CHAN_CMO(1, 0),
		IIO_CHAN_SOFT_TIMESTAMP(2),
	},
	.grade = AD4030_REG_CHIP_GRADE_AD4030_24_GRADE,
	.precision_bits = 24,
	.num_voltage_inputs = 1,
	.tcyc_ns = AD4030_TCYC_ADJUSTED_NS,
};

static const struct spi_device_id ad4030_id_table[] = {
	{ "ad4030-24", (kernel_ulong_t)&ad4030_24_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4030_id_table);

static const struct of_device_id ad4030_of_match[] = {
	{ .compatible = "adi,ad4030-24", .data = &ad4030_24_chip_info },
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
