// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Microchip MCP3911, Two-channel Analog Front End
 *
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 * Copyright (C) 2018 Kent Gustavsson <kent@minoris.se>
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/trigger.h>

#include <asm/unaligned.h>

#define MCP3911_REG_CHANNEL0		0x00
#define MCP3911_REG_CHANNEL1		0x03
#define MCP3911_REG_MOD			0x06
#define MCP3911_REG_PHASE		0x07
#define MCP3911_REG_GAIN		0x09
#define MCP3911_GAIN_MASK(ch)		(GENMASK(2, 0) << 3 * (ch))
#define MCP3911_GAIN_VAL(ch, val)      ((val << 3 * (ch)) & MCP3911_GAIN_MASK(ch))

#define MCP3911_REG_STATUSCOM		0x0a
#define MCP3911_STATUSCOM_DRHIZ		BIT(12)
#define MCP3911_STATUSCOM_READ		GENMASK(7, 6)
#define MCP3911_STATUSCOM_CH1_24WIDTH	BIT(4)
#define MCP3911_STATUSCOM_CH0_24WIDTH	BIT(3)
#define MCP3911_STATUSCOM_EN_OFFCAL	BIT(2)
#define MCP3911_STATUSCOM_EN_GAINCAL	BIT(1)

#define MCP3911_REG_CONFIG		0x0c
#define MCP3911_CONFIG_CLKEXT		BIT(1)
#define MCP3911_CONFIG_VREFEXT		BIT(2)
#define MCP3911_CONFIG_OSR		GENMASK(13, 11)

#define MCP3911_REG_OFFCAL_CH0		0x0e
#define MCP3911_REG_GAINCAL_CH0		0x11
#define MCP3911_REG_OFFCAL_CH1		0x14
#define MCP3911_REG_GAINCAL_CH1		0x17
#define MCP3911_REG_VREFCAL		0x1a

#define MCP3911_CHANNEL(ch)		(MCP3911_REG_CHANNEL0 + (ch) * 3)
#define MCP3911_OFFCAL(ch)		(MCP3911_REG_OFFCAL_CH0 + (ch) * 6)

/* Internal voltage reference in mV */
#define MCP3911_INT_VREF_MV		1200

#define MCP3911_REG_READ(reg, id)	((((reg) << 1) | ((id) << 6) | (1 << 0)) & 0xff)
#define MCP3911_REG_WRITE(reg, id)	((((reg) << 1) | ((id) << 6) | (0 << 0)) & 0xff)
#define MCP3911_REG_MASK		GENMASK(4, 1)

#define MCP3911_NUM_SCALES		6

/* Registers compatible with MCP3910 */
#define MCP3910_REG_STATUSCOM		0x0c
#define MCP3910_STATUSCOM_READ		GENMASK(23, 22)
#define MCP3910_STATUSCOM_DRHIZ		BIT(20)

#define MCP3910_REG_GAIN		0x0b

#define MCP3910_REG_CONFIG0		0x0d
#define MCP3910_CONFIG0_EN_OFFCAL	BIT(23)
#define MCP3910_CONFIG0_OSR		GENMASK(15, 13)

#define MCP3910_REG_CONFIG1		0x0e
#define MCP3910_CONFIG1_CLKEXT		BIT(6)
#define MCP3910_CONFIG1_VREFEXT		BIT(7)

#define MCP3910_REG_OFFCAL_CH0		0x0f
#define MCP3910_OFFCAL(ch)		(MCP3910_REG_OFFCAL_CH0 + (ch) * 6)

/* Maximal number of channels used by the MCP39XX family */
#define MCP39XX_MAX_NUM_CHANNELS	8

static const int mcp3911_osr_table[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096 };
static u32 mcp3911_scale_table[MCP3911_NUM_SCALES][2];

enum mcp3911_id {
	MCP3910,
	MCP3911,
	MCP3912,
	MCP3913,
	MCP3914,
	MCP3918,
	MCP3919,
};

struct mcp3911;
struct mcp3911_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;

	int (*config)(struct mcp3911 *adc, bool external_vref);
	int (*get_osr)(struct mcp3911 *adc, u32 *val);
	int (*set_osr)(struct mcp3911 *adc, u32 val);
	int (*enable_offset)(struct mcp3911 *adc, bool enable);
	int (*get_offset)(struct mcp3911 *adc, int channel, int *val);
	int (*set_offset)(struct mcp3911 *adc, int channel, int val);
	int (*set_scale)(struct mcp3911 *adc, int channel, u32 val);
};

struct mcp3911 {
	struct spi_device *spi;
	struct mutex lock;
	struct clk *clki;
	u32 dev_addr;
	struct iio_trigger *trig;
	u32 gain[MCP39XX_MAX_NUM_CHANNELS];
	const struct mcp3911_chip_info *chip;
	struct {
		u32 channels[MCP39XX_MAX_NUM_CHANNELS];
		s64 ts __aligned(8);
	} scan;

	u8 tx_buf __aligned(IIO_DMA_MINALIGN);
	u8 rx_buf[MCP39XX_MAX_NUM_CHANNELS * 3];
};

static int mcp3911_read(struct mcp3911 *adc, u8 reg, u32 *val, u8 len)
{
	int ret;

	reg = MCP3911_REG_READ(reg, adc->dev_addr);
	ret = spi_write_then_read(adc->spi, &reg, 1, val, len);
	if (ret < 0)
		return ret;

	be32_to_cpus(val);
	*val >>= ((4 - len) * 8);
	dev_dbg(&adc->spi->dev, "reading 0x%x from register 0x%lx\n", *val,
		FIELD_GET(MCP3911_REG_MASK, reg));
	return ret;
}

static int mcp3911_write(struct mcp3911 *adc, u8 reg, u32 val, u8 len)
{
	dev_dbg(&adc->spi->dev, "writing 0x%x to register 0x%x\n", val, reg);

	val <<= (3 - len) * 8;
	cpu_to_be32s(&val);
	val |= MCP3911_REG_WRITE(reg, adc->dev_addr);

	return spi_write(adc->spi, &val, len + 1);
}

static int mcp3911_update(struct mcp3911 *adc, u8 reg, u32 mask, u32 val, u8 len)
{
	u32 tmp;
	int ret;

	ret = mcp3911_read(adc, reg, &tmp, len);
	if (ret)
		return ret;

	val &= mask;
	val |= tmp & ~mask;
	return mcp3911_write(adc, reg, val, len);
}

static int mcp3910_enable_offset(struct mcp3911 *adc, bool enable)
{
	unsigned int mask = MCP3910_CONFIG0_EN_OFFCAL;
	unsigned int value = enable ? mask : 0;

	return mcp3911_update(adc, MCP3910_REG_CONFIG0, mask, value, 3);
}

static int mcp3910_get_offset(struct mcp3911 *adc, int channel, int *val)
{
	return mcp3911_read(adc, MCP3910_OFFCAL(channel), val, 3);
}

static int mcp3910_set_offset(struct mcp3911 *adc, int channel, int val)
{
	int ret;

	ret = mcp3911_write(adc, MCP3910_OFFCAL(channel), val, 3);
	if (ret)
		return ret;

	return adc->chip->enable_offset(adc, 1);
}

static int mcp3911_enable_offset(struct mcp3911 *adc, bool enable)
{
	unsigned int mask = MCP3911_STATUSCOM_EN_OFFCAL;
	unsigned int value = enable ? mask : 0;

	return mcp3911_update(adc, MCP3911_REG_STATUSCOM, mask, value, 2);
}

static int mcp3911_get_offset(struct mcp3911 *adc, int channel, int *val)
{
	return mcp3911_read(adc, MCP3911_OFFCAL(channel), val, 3);
}

static int mcp3911_set_offset(struct mcp3911 *adc, int channel, int val)
{
	int ret;

	ret = mcp3911_write(adc, MCP3911_OFFCAL(channel), val, 3);
	if (ret)
		return ret;

	return adc->chip->enable_offset(adc, 1);
}

static int mcp3910_get_osr(struct mcp3911 *adc, u32 *val)
{
	int ret;
	unsigned int osr;

	ret = mcp3911_read(adc, MCP3910_REG_CONFIG0, val, 3);
	if (ret)
		return ret;

	osr = FIELD_GET(MCP3910_CONFIG0_OSR, *val);
	*val = 32 << osr;
	return 0;
}

static int mcp3910_set_osr(struct mcp3911 *adc, u32 val)
{
	unsigned int osr = FIELD_PREP(MCP3910_CONFIG0_OSR, val);
	unsigned int mask = MCP3910_CONFIG0_OSR;

	return mcp3911_update(adc, MCP3910_REG_CONFIG0, mask, osr, 3);
}

static int mcp3911_set_osr(struct mcp3911 *adc, u32 val)
{
	unsigned int osr = FIELD_PREP(MCP3911_CONFIG_OSR, val);
	unsigned int mask = MCP3911_CONFIG_OSR;

	return mcp3911_update(adc, MCP3911_REG_CONFIG, mask, osr, 2);
}

static int mcp3911_get_osr(struct mcp3911 *adc, u32 *val)
{
	int ret;
	unsigned int osr;

	ret = mcp3911_read(adc, MCP3911_REG_CONFIG, val, 2);
	if (ret)
		return ret;

	osr = FIELD_GET(MCP3911_CONFIG_OSR, *val);
	*val = 32 << osr;
	return ret;
}

static int mcp3910_set_scale(struct mcp3911 *adc, int channel, u32 val)
{
	return mcp3911_update(adc, MCP3910_REG_GAIN,
			      MCP3911_GAIN_MASK(channel),
			      MCP3911_GAIN_VAL(channel, val), 3);
}

static int mcp3911_set_scale(struct mcp3911 *adc, int channel, u32 val)
{
	return mcp3911_update(adc, MCP3911_REG_GAIN,
			      MCP3911_GAIN_MASK(channel),
			      MCP3911_GAIN_VAL(channel, val), 1);
}

static int mcp3911_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return IIO_VAL_INT;
	default:
		return IIO_VAL_INT_PLUS_NANO;
	}
}

static int mcp3911_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long info)
{
	switch (info) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*type = IIO_VAL_INT;
		*vals = mcp3911_osr_table;
		*length = ARRAY_SIZE(mcp3911_osr_table);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_NANO;
		*vals = (int *)mcp3911_scale_table;
		*length = ARRAY_SIZE(mcp3911_scale_table) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int mcp3911_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mcp3911 *adc = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&adc->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp3911_read(adc,
				   MCP3911_CHANNEL(channel->channel), val, 3);
		if (ret)
			return ret;

		*val = sign_extend32(*val, 23);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		ret = adc->chip->get_offset(adc, channel->channel, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = adc->chip->get_osr(adc, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = mcp3911_scale_table[ilog2(adc->gain[channel->channel])][0];
		*val2 = mcp3911_scale_table[ilog2(adc->gain[channel->channel])][1];
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int mcp3911_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *channel, int val,
			     int val2, long mask)
{
	struct mcp3911 *adc = iio_priv(indio_dev);

	guard(mutex)(&adc->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (int i = 0; i < MCP3911_NUM_SCALES; i++) {
			if (val == mcp3911_scale_table[i][0] &&
			    val2 == mcp3911_scale_table[i][1]) {

				adc->gain[channel->channel] = BIT(i);
				return adc->chip->set_scale(adc, channel->channel, i);
			}
		}
		return -EINVAL;
	case IIO_CHAN_INFO_OFFSET:
		if (val2 != 0)
			return -EINVAL;

		return adc->chip->set_offset(adc, channel->channel, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		for (int i = 0; i < ARRAY_SIZE(mcp3911_osr_table); i++) {
			if (val == mcp3911_osr_table[i]) {
				return adc->chip->set_osr(adc, i);
			}
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int mcp3911_calc_scale_table(u32 vref_mv)
{
	u32 div;
	u64 tmp;

	/*
	 * For 24-bit Conversion
	 * Raw = ((Voltage)/(Vref) * 2^23 * Gain * 1.5
	 * Voltage = Raw * (Vref)/(2^23 * Gain * 1.5)
	 *
	 * ref = Reference voltage
	 * div = (2^23 * 1.5 * gain) = 12582912 * gain
	 */
	for (int i = 0; i < MCP3911_NUM_SCALES; i++) {
		div = 12582912 * BIT(i);
		tmp = div_s64((s64)vref_mv * 1000000000LL, div);

		mcp3911_scale_table[i][0] = 0;
		mcp3911_scale_table[i][1] = tmp;
	}

	return 0;
}

#define MCP3911_CHAN(idx) {					\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = idx,					\
		.scan_index = idx,				\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(IIO_CHAN_INFO_OFFSET) |		\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.info_mask_shared_by_type_available =           \
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		.info_mask_separate_available =			\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 24,				\
			.storagebits = 32,			\
			.endianness = IIO_BE,			\
		},						\
}

static const struct iio_chan_spec mcp3910_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_chan_spec mcp3911_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_chan_spec mcp3912_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	MCP3911_CHAN(2),
	MCP3911_CHAN(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct iio_chan_spec mcp3913_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	MCP3911_CHAN(2),
	MCP3911_CHAN(3),
	MCP3911_CHAN(4),
	MCP3911_CHAN(5),
	IIO_CHAN_SOFT_TIMESTAMP(6),
};

static const struct iio_chan_spec mcp3914_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	MCP3911_CHAN(2),
	MCP3911_CHAN(3),
	MCP3911_CHAN(4),
	MCP3911_CHAN(5),
	MCP3911_CHAN(6),
	MCP3911_CHAN(7),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static const struct iio_chan_spec mcp3918_channels[] = {
	MCP3911_CHAN(0),
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec mcp3919_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	MCP3911_CHAN(2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static irqreturn_t mcp3911_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mcp3911 *adc = iio_priv(indio_dev);
	struct device *dev = &adc->spi->dev;
	struct spi_transfer xfer[] = {
		{
			.tx_buf = &adc->tx_buf,
			.len = 1,
		}, {
			.rx_buf = adc->rx_buf,
			.len = (adc->chip->num_channels - 1) * 3,
		},
	};
	int scan_index;
	int i = 0;
	int ret;

	guard(mutex)(&adc->lock);
	adc->tx_buf = MCP3911_REG_READ(MCP3911_CHANNEL(0), adc->dev_addr);
	ret = spi_sync_transfer(adc->spi, xfer, ARRAY_SIZE(xfer));
	if (ret < 0) {
		dev_warn(dev, "failed to get conversion data\n");
		goto out;
	}

	iio_for_each_active_channel(indio_dev, scan_index) {
		const struct iio_chan_spec *scan_chan = &indio_dev->channels[scan_index];

		adc->scan.channels[i] = get_unaligned_be24(&adc->rx_buf[scan_chan->channel * 3]);
		i++;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, &adc->scan,
					   iio_get_time_ns(indio_dev));
out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info mcp3911_info = {
	.read_raw = mcp3911_read_raw,
	.write_raw = mcp3911_write_raw,
	.read_avail = mcp3911_read_avail,
	.write_raw_get_fmt = mcp3911_write_raw_get_fmt,
};

static int mcp3911_config(struct mcp3911 *adc, bool external_vref)
{
	struct device *dev = &adc->spi->dev;
	u32 regval;
	int ret;

	ret = mcp3911_read(adc, MCP3911_REG_CONFIG, &regval, 2);
	if (ret)
		return ret;

	regval &= ~MCP3911_CONFIG_VREFEXT;
	if (external_vref) {
		dev_dbg(dev, "use external voltage reference\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_VREFEXT, 1);
	} else {
		dev_dbg(dev, "use internal voltage reference (1.2V)\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_VREFEXT, 0);
	}

	regval &= ~MCP3911_CONFIG_CLKEXT;
	if (adc->clki) {
		dev_dbg(dev, "use external clock as clocksource\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_CLKEXT, 1);
	} else {
		dev_dbg(dev, "use crystal oscillator as clocksource\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_CLKEXT, 0);
	}

	ret = mcp3911_write(adc, MCP3911_REG_CONFIG, regval, 2);
	if (ret)
		return ret;

	ret = mcp3911_read(adc, MCP3911_REG_STATUSCOM, &regval, 2);
	if (ret)
		return ret;

	/* Address counter incremented, cycle through register types */
	regval &= ~MCP3911_STATUSCOM_READ;
	regval |= FIELD_PREP(MCP3911_STATUSCOM_READ, 0x02);

	regval &= ~MCP3911_STATUSCOM_DRHIZ;
	if (device_property_read_bool(dev, "microchip,data-ready-hiz"))
		regval |= FIELD_PREP(MCP3911_STATUSCOM_DRHIZ, 0);
	else
		regval |= FIELD_PREP(MCP3911_STATUSCOM_DRHIZ, 1);

	/* Disable offset to ignore any old values in offset register */
	regval &= ~MCP3911_STATUSCOM_EN_OFFCAL;

	ret =  mcp3911_write(adc, MCP3911_REG_STATUSCOM, regval, 2);
	if (ret)
		return ret;

	/* Set gain to 1 for all channels */
	ret = mcp3911_read(adc, MCP3911_REG_GAIN, &regval, 1);
	if (ret)
		return ret;

	for (int i = 0; i < adc->chip->num_channels - 1; i++) {
		adc->gain[i] = 1;
		regval &= ~MCP3911_GAIN_MASK(i);
	}

	return mcp3911_write(adc, MCP3911_REG_GAIN, regval, 1);
}

static int mcp3910_config(struct mcp3911 *adc, bool external_vref)
{
	struct device *dev = &adc->spi->dev;
	u32 regval;
	int ret;

	ret = mcp3911_read(adc, MCP3910_REG_CONFIG1, &regval, 3);
	if (ret)
		return ret;

	regval &= ~MCP3910_CONFIG1_VREFEXT;
	if (external_vref) {
		dev_dbg(dev, "use external voltage reference\n");
		regval |= FIELD_PREP(MCP3910_CONFIG1_VREFEXT, 1);
	} else {
		dev_dbg(dev, "use internal voltage reference (1.2V)\n");
		regval |= FIELD_PREP(MCP3910_CONFIG1_VREFEXT, 0);
	}

	regval &= ~MCP3910_CONFIG1_CLKEXT;
	if (adc->clki) {
		dev_dbg(dev, "use external clock as clocksource\n");
		regval |= FIELD_PREP(MCP3910_CONFIG1_CLKEXT, 1);
	} else {
		dev_dbg(dev, "use crystal oscillator as clocksource\n");
		regval |= FIELD_PREP(MCP3910_CONFIG1_CLKEXT, 0);
	}

	ret = mcp3911_write(adc, MCP3910_REG_CONFIG1, regval, 3);
	if (ret)
		return ret;

	ret = mcp3911_read(adc, MCP3910_REG_STATUSCOM, &regval, 3);
	if (ret)
		return ret;

	/* Address counter incremented, cycle through register types */
	regval &= ~MCP3910_STATUSCOM_READ;
	regval |= FIELD_PREP(MCP3910_STATUSCOM_READ, 0x02);

	regval &= ~MCP3910_STATUSCOM_DRHIZ;
	if (device_property_read_bool(dev, "microchip,data-ready-hiz"))
		regval |= FIELD_PREP(MCP3910_STATUSCOM_DRHIZ, 0);
	else
		regval |= FIELD_PREP(MCP3910_STATUSCOM_DRHIZ, 1);

	ret = mcp3911_write(adc, MCP3910_REG_STATUSCOM, regval, 3);
	if (ret)
		return ret;

	/* Set gain to 1 for all channels */
	ret = mcp3911_read(adc, MCP3910_REG_GAIN, &regval, 3);
	if (ret)
		return ret;

	for (int i = 0; i < adc->chip->num_channels - 1; i++) {
		adc->gain[i] = 1;
		regval &= ~MCP3911_GAIN_MASK(i);
	}
	ret = mcp3911_write(adc, MCP3910_REG_GAIN, regval, 3);
	if (ret)
		return ret;

	/* Disable offset to ignore any old values in offset register */
	return adc->chip->enable_offset(adc, 0);
}

static int mcp3911_set_trigger_state(struct iio_trigger *trig, bool enable)
{
	struct mcp3911 *adc = iio_trigger_get_drvdata(trig);

	if (enable)
		enable_irq(adc->spi->irq);
	else
		disable_irq(adc->spi->irq);

	return 0;
}

static const struct iio_trigger_ops mcp3911_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
	.set_trigger_state = mcp3911_set_trigger_state,
};

static int mcp3911_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct mcp3911 *adc;
	bool external_vref;
	u32 vref_mv;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;
	adc->chip = spi_get_device_match_data(spi);

	ret = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "failed to get vref voltage\n");

	external_vref = ret != -ENODEV;
	vref_mv = external_vref ? ret / 1000 : MCP3911_INT_VREF_MV;

	adc->clki = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(adc->clki)) {
		if (PTR_ERR(adc->clki) == -ENOENT) {
			adc->clki = NULL;
		} else {
			return dev_err_probe(dev, PTR_ERR(adc->clki), "failed to get adc clk\n");
		}
	}

	/*
	 * Fallback to "device-addr" due to historical mismatch between
	 * dt-bindings and implementation.
	 */
	ret = device_property_read_u32(dev, "microchip,device-addr", &adc->dev_addr);
	if (ret)
		device_property_read_u32(dev, "device-addr", &adc->dev_addr);
	if (adc->dev_addr > 3) {
		return dev_err_probe(dev, -EINVAL,
				     "invalid device address (%i). Must be in range 0-3.\n",
				     adc->dev_addr);
	}
	dev_dbg(dev, "use device address %i\n", adc->dev_addr);

	ret = adc->chip->config(adc, external_vref);
	if (ret)
		return ret;

	ret = mcp3911_calc_scale_table(vref_mv);
	if (ret)
		return ret;

	/* Set gain to 1 for all channels */
	for (int i = 0; i < adc->chip->num_channels - 1; i++) {
		adc->gain[i] = 1;
		ret = mcp3911_update(adc, MCP3911_REG_GAIN,
				     MCP3911_GAIN_MASK(i),
				     MCP3911_GAIN_VAL(i, 0), 1);
		if (ret)
			return ret;
	}

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp3911_info;
	spi_set_drvdata(spi, indio_dev);

	indio_dev->channels = adc->chip->channels;
	indio_dev->num_channels = adc->chip->num_channels;

	mutex_init(&adc->lock);

	if (spi->irq > 0) {
		adc->trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
						   iio_device_id(indio_dev));
		if (!adc->trig)
			return -ENOMEM;

		adc->trig->ops = &mcp3911_trigger_ops;
		iio_trigger_set_drvdata(adc->trig, adc);
		ret = devm_iio_trigger_register(dev, adc->trig);
		if (ret)
			return ret;

		/*
		 * The device generates interrupts as long as it is powered up.
		 * Some platforms might not allow the option to power it down so
		 * don't enable the interrupt to avoid extra load on the system.
		 */
		ret = devm_request_irq(dev, spi->irq, &iio_trigger_generic_data_rdy_poll,
				       IRQF_NO_AUTOEN | IRQF_ONESHOT,
				       indio_dev->name, adc->trig);
		if (ret)
			return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      mcp3911_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct mcp3911_chip_info mcp3911_chip_info[] = {
	[MCP3910] = {
		.channels = mcp3910_channels,
		.num_channels = ARRAY_SIZE(mcp3910_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
	[MCP3911] = {
		.channels = mcp3911_channels,
		.num_channels = ARRAY_SIZE(mcp3911_channels),
		.config = mcp3911_config,
		.get_osr = mcp3911_get_osr,
		.set_osr = mcp3911_set_osr,
		.enable_offset = mcp3911_enable_offset,
		.get_offset = mcp3911_get_offset,
		.set_offset = mcp3911_set_offset,
		.set_scale = mcp3911_set_scale,
	},
	[MCP3912] = {
		.channels = mcp3912_channels,
		.num_channels = ARRAY_SIZE(mcp3912_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
	[MCP3913] = {
		.channels = mcp3913_channels,
		.num_channels = ARRAY_SIZE(mcp3913_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
	[MCP3914] = {
		.channels = mcp3914_channels,
		.num_channels = ARRAY_SIZE(mcp3914_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
	[MCP3918] = {
		.channels = mcp3918_channels,
		.num_channels = ARRAY_SIZE(mcp3918_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
	[MCP3919] = {
		.channels = mcp3919_channels,
		.num_channels = ARRAY_SIZE(mcp3919_channels),
		.config = mcp3910_config,
		.get_osr = mcp3910_get_osr,
		.set_osr = mcp3910_set_osr,
		.enable_offset = mcp3910_enable_offset,
		.get_offset = mcp3910_get_offset,
		.set_offset = mcp3910_set_offset,
		.set_scale = mcp3910_set_scale,
	},
};
static const struct of_device_id mcp3911_dt_ids[] = {
	{ .compatible = "microchip,mcp3910", .data = &mcp3911_chip_info[MCP3910] },
	{ .compatible = "microchip,mcp3911", .data = &mcp3911_chip_info[MCP3911] },
	{ .compatible = "microchip,mcp3912", .data = &mcp3911_chip_info[MCP3912] },
	{ .compatible = "microchip,mcp3913", .data = &mcp3911_chip_info[MCP3913] },
	{ .compatible = "microchip,mcp3914", .data = &mcp3911_chip_info[MCP3914] },
	{ .compatible = "microchip,mcp3918", .data = &mcp3911_chip_info[MCP3918] },
	{ .compatible = "microchip,mcp3919", .data = &mcp3911_chip_info[MCP3919] },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3911_dt_ids);

static const struct spi_device_id mcp3911_id[] = {
	{ "mcp3910", (kernel_ulong_t)&mcp3911_chip_info[MCP3910] },
	{ "mcp3911", (kernel_ulong_t)&mcp3911_chip_info[MCP3911] },
	{ "mcp3912", (kernel_ulong_t)&mcp3911_chip_info[MCP3912] },
	{ "mcp3913", (kernel_ulong_t)&mcp3911_chip_info[MCP3913] },
	{ "mcp3914", (kernel_ulong_t)&mcp3911_chip_info[MCP3914] },
	{ "mcp3918", (kernel_ulong_t)&mcp3911_chip_info[MCP3918] },
	{ "mcp3919", (kernel_ulong_t)&mcp3911_chip_info[MCP3919] },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp3911_id);

static struct spi_driver mcp3911_driver = {
	.driver = {
		.name = "mcp3911",
		.of_match_table = mcp3911_dt_ids,
	},
	.probe = mcp3911_probe,
	.id_table = mcp3911_id,
};
module_spi_driver(mcp3911_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_AUTHOR("Kent Gustavsson <kent@minoris.se>");
MODULE_DESCRIPTION("Microchip Technology MCP3911");
MODULE_LICENSE("GPL v2");
