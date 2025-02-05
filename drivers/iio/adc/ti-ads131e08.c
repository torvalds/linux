// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments ADS131E0x 4-, 6- and 8-Channel ADCs
 *
 * Copyright (c) 2020 AVL DiTEST GmbH
 *   Tomislav Denis <tomislav.denis@avl.com>
 *
 * Datasheet: https://www.ti.com/lit/ds/symlink/ads131e08.pdf
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/unaligned.h>

/* Commands */
#define ADS131E08_CMD_RESET		0x06
#define ADS131E08_CMD_START		0x08
#define ADS131E08_CMD_STOP		0x0A
#define ADS131E08_CMD_OFFSETCAL		0x1A
#define ADS131E08_CMD_SDATAC		0x11
#define ADS131E08_CMD_RDATA		0x12
#define ADS131E08_CMD_RREG(r)		(BIT(5) | (r & GENMASK(4, 0)))
#define ADS131E08_CMD_WREG(r)		(BIT(6) | (r & GENMASK(4, 0)))

/* Registers */
#define ADS131E08_ADR_CFG1R		0x01
#define ADS131E08_ADR_CFG3R		0x03
#define ADS131E08_ADR_CH0R		0x05

/* Configuration register 1 */
#define ADS131E08_CFG1R_DR_MASK		GENMASK(2, 0)

/* Configuration register 3 */
#define ADS131E08_CFG3R_PDB_REFBUF_MASK	BIT(7)
#define ADS131E08_CFG3R_VREF_4V_MASK	BIT(5)

/* Channel settings register */
#define ADS131E08_CHR_GAIN_MASK		GENMASK(6, 4)
#define ADS131E08_CHR_MUX_MASK		GENMASK(2, 0)
#define ADS131E08_CHR_PWD_MASK		BIT(7)

/* ADC  misc */
#define ADS131E08_DEFAULT_DATA_RATE	1
#define ADS131E08_DEFAULT_PGA_GAIN	1
#define ADS131E08_DEFAULT_MUX		0

#define ADS131E08_VREF_2V4_mV		2400
#define ADS131E08_VREF_4V_mV		4000

#define ADS131E08_WAIT_RESET_CYCLES	18
#define ADS131E08_WAIT_SDECODE_CYCLES	4
#define ADS131E08_WAIT_OFFSETCAL_MS	153
#define ADS131E08_MAX_SETTLING_TIME_MS	6

#define ADS131E08_NUM_STATUS_BYTES	3
#define ADS131E08_NUM_DATA_BYTES_MAX	24
#define ADS131E08_NUM_DATA_BYTES(dr)	(((dr) >= 32) ? 2 : 3)
#define ADS131E08_NUM_DATA_BITS(dr)	(ADS131E08_NUM_DATA_BYTES(dr) * 8)
#define ADS131E08_NUM_STORAGE_BYTES	4

enum ads131e08_ids {
	ads131e04,
	ads131e06,
	ads131e08,
};

struct ads131e08_info {
	unsigned int max_channels;
	const char *name;
};

struct ads131e08_channel_config {
	unsigned int pga_gain;
	unsigned int mux;
};

struct ads131e08_state {
	const struct ads131e08_info *info;
	struct spi_device *spi;
	struct iio_trigger *trig;
	struct clk *adc_clk;
	struct regulator *vref_reg;
	struct ads131e08_channel_config *channel_config;
	unsigned int data_rate;
	unsigned int vref_mv;
	unsigned int sdecode_delay_us;
	unsigned int reset_delay_us;
	unsigned int readback_len;
	struct completion completion;
	struct {
		u8 data[ADS131E08_NUM_DATA_BYTES_MAX];
		aligned_s64 ts;
	} tmp_buf;

	u8 tx_buf[3] __aligned(IIO_DMA_MINALIGN);
	/*
	 * Add extra one padding byte to be able to access the last channel
	 * value using u32 pointer
	 */
	u8 rx_buf[ADS131E08_NUM_STATUS_BYTES +
		ADS131E08_NUM_DATA_BYTES_MAX + 1];
};

static const struct ads131e08_info ads131e08_info_tbl[] = {
	[ads131e04] = {
		.max_channels = 4,
		.name = "ads131e04",
	},
	[ads131e06] = {
		.max_channels = 6,
		.name = "ads131e06",
	},
	[ads131e08] = {
		.max_channels = 8,
		.name = "ads131e08",
	},
};

struct ads131e08_data_rate_desc {
	unsigned int rate;  /* data rate in kSPS */
	u8 reg;             /* reg value */
};

static const struct ads131e08_data_rate_desc ads131e08_data_rate_tbl[] = {
	{ .rate = 64,   .reg = 0x00 },
	{ .rate = 32,   .reg = 0x01 },
	{ .rate = 16,   .reg = 0x02 },
	{ .rate = 8,    .reg = 0x03 },
	{ .rate = 4,    .reg = 0x04 },
	{ .rate = 2,    .reg = 0x05 },
	{ .rate = 1,    .reg = 0x06 },
};

struct ads131e08_pga_gain_desc {
	unsigned int gain;  /* PGA gain value */
	u8 reg;             /* field value */
};

static const struct ads131e08_pga_gain_desc ads131e08_pga_gain_tbl[] = {
	{ .gain = 1,   .reg = 0x01 },
	{ .gain = 2,   .reg = 0x02 },
	{ .gain = 4,   .reg = 0x04 },
	{ .gain = 8,   .reg = 0x05 },
	{ .gain = 12,  .reg = 0x06 },
};

static const u8 ads131e08_valid_channel_mux_values[] = { 0, 1, 3, 4 };

static int ads131e08_exec_cmd(struct ads131e08_state *st, u8 cmd)
{
	int ret;

	ret = spi_write_then_read(st->spi, &cmd, 1, NULL, 0);
	if (ret)
		dev_err(&st->spi->dev, "Exec cmd(%02x) failed\n", cmd);

	return ret;
}

static int ads131e08_read_reg(struct ads131e08_state *st, u8 reg)
{
	int ret;
	struct spi_transfer transfer[] = {
		{
			.tx_buf = &st->tx_buf,
			.len = 2,
			.delay = {
				.value = st->sdecode_delay_us,
				.unit = SPI_DELAY_UNIT_USECS,
			},
		}, {
			.rx_buf = &st->rx_buf,
			.len = 1,
		},
	};

	st->tx_buf[0] = ADS131E08_CMD_RREG(reg);
	st->tx_buf[1] = 0;

	ret = spi_sync_transfer(st->spi, transfer, ARRAY_SIZE(transfer));
	if (ret) {
		dev_err(&st->spi->dev, "Read register failed\n");
		return ret;
	}

	return st->rx_buf[0];
}

static int ads131e08_write_reg(struct ads131e08_state *st, u8 reg, u8 value)
{
	int ret;
	struct spi_transfer transfer[] = {
		{
			.tx_buf = &st->tx_buf,
			.len = 3,
			.delay = {
				.value = st->sdecode_delay_us,
				.unit = SPI_DELAY_UNIT_USECS,
			},
		}
	};

	st->tx_buf[0] = ADS131E08_CMD_WREG(reg);
	st->tx_buf[1] = 0;
	st->tx_buf[2] = value;

	ret = spi_sync_transfer(st->spi, transfer, ARRAY_SIZE(transfer));
	if (ret)
		dev_err(&st->spi->dev, "Write register failed\n");

	return ret;
}

static int ads131e08_read_data(struct ads131e08_state *st, int rx_len)
{
	int ret;
	struct spi_transfer transfer[] = {
		{
			.tx_buf = &st->tx_buf,
			.len = 1,
		}, {
			.rx_buf = &st->rx_buf,
			.len = rx_len,
		},
	};

	st->tx_buf[0] = ADS131E08_CMD_RDATA;

	ret = spi_sync_transfer(st->spi, transfer, ARRAY_SIZE(transfer));
	if (ret)
		dev_err(&st->spi->dev, "Read data failed\n");

	return ret;
}

static int ads131e08_set_data_rate(struct ads131e08_state *st, int data_rate)
{
	int i, reg, ret;

	for (i = 0; i < ARRAY_SIZE(ads131e08_data_rate_tbl); i++) {
		if (ads131e08_data_rate_tbl[i].rate == data_rate)
			break;
	}

	if (i == ARRAY_SIZE(ads131e08_data_rate_tbl)) {
		dev_err(&st->spi->dev, "invalid data rate value\n");
		return -EINVAL;
	}

	reg = ads131e08_read_reg(st, ADS131E08_ADR_CFG1R);
	if (reg < 0)
		return reg;

	reg &= ~ADS131E08_CFG1R_DR_MASK;
	reg |= FIELD_PREP(ADS131E08_CFG1R_DR_MASK,
		ads131e08_data_rate_tbl[i].reg);

	ret = ads131e08_write_reg(st, ADS131E08_ADR_CFG1R, reg);
	if (ret)
		return ret;

	st->data_rate = data_rate;
	st->readback_len = ADS131E08_NUM_STATUS_BYTES +
		ADS131E08_NUM_DATA_BYTES(st->data_rate) *
		st->info->max_channels;

	return 0;
}

static int ads131e08_pga_gain_to_field_value(struct ads131e08_state *st,
	unsigned int pga_gain)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ads131e08_pga_gain_tbl); i++) {
		if (ads131e08_pga_gain_tbl[i].gain == pga_gain)
			break;
	}

	if (i == ARRAY_SIZE(ads131e08_pga_gain_tbl)) {
		dev_err(&st->spi->dev, "invalid PGA gain value\n");
		return -EINVAL;
	}

	return ads131e08_pga_gain_tbl[i].reg;
}

static int ads131e08_set_pga_gain(struct ads131e08_state *st,
	unsigned int channel, unsigned int pga_gain)
{
	int field_value, reg;

	field_value = ads131e08_pga_gain_to_field_value(st, pga_gain);
	if (field_value < 0)
		return field_value;

	reg = ads131e08_read_reg(st, ADS131E08_ADR_CH0R + channel);
	if (reg < 0)
		return reg;

	reg &= ~ADS131E08_CHR_GAIN_MASK;
	reg |= FIELD_PREP(ADS131E08_CHR_GAIN_MASK, field_value);

	return ads131e08_write_reg(st, ADS131E08_ADR_CH0R + channel, reg);
}

static int ads131e08_validate_channel_mux(struct ads131e08_state *st,
	unsigned int mux)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ads131e08_valid_channel_mux_values); i++) {
		if (ads131e08_valid_channel_mux_values[i] == mux)
			break;
	}

	if (i == ARRAY_SIZE(ads131e08_valid_channel_mux_values)) {
		dev_err(&st->spi->dev, "invalid channel mux value\n");
		return -EINVAL;
	}

	return 0;
}

static int ads131e08_set_channel_mux(struct ads131e08_state *st,
	unsigned int channel, unsigned int mux)
{
	int reg;

	reg = ads131e08_read_reg(st, ADS131E08_ADR_CH0R + channel);
	if (reg < 0)
		return reg;

	reg &= ~ADS131E08_CHR_MUX_MASK;
	reg |= FIELD_PREP(ADS131E08_CHR_MUX_MASK, mux);

	return ads131e08_write_reg(st, ADS131E08_ADR_CH0R + channel, reg);
}

static int ads131e08_power_down_channel(struct ads131e08_state *st,
	unsigned int channel, bool value)
{
	int reg;

	reg = ads131e08_read_reg(st, ADS131E08_ADR_CH0R + channel);
	if (reg < 0)
		return reg;

	reg &= ~ADS131E08_CHR_PWD_MASK;
	reg |= FIELD_PREP(ADS131E08_CHR_PWD_MASK, value);

	return ads131e08_write_reg(st, ADS131E08_ADR_CH0R + channel, reg);
}

static int ads131e08_config_reference_voltage(struct ads131e08_state *st)
{
	int reg;

	reg = ads131e08_read_reg(st, ADS131E08_ADR_CFG3R);
	if (reg < 0)
		return reg;

	reg &= ~ADS131E08_CFG3R_PDB_REFBUF_MASK;
	if (!st->vref_reg) {
		reg |= FIELD_PREP(ADS131E08_CFG3R_PDB_REFBUF_MASK, 1);
		reg &= ~ADS131E08_CFG3R_VREF_4V_MASK;
		reg |= FIELD_PREP(ADS131E08_CFG3R_VREF_4V_MASK,
			st->vref_mv == ADS131E08_VREF_4V_mV);
	}

	return ads131e08_write_reg(st, ADS131E08_ADR_CFG3R, reg);
}

static int ads131e08_initial_config(struct iio_dev *indio_dev)
{
	const struct iio_chan_spec *channel = indio_dev->channels;
	struct ads131e08_state *st = iio_priv(indio_dev);
	unsigned long active_channels = 0;
	int ret, i;

	ret = ads131e08_exec_cmd(st, ADS131E08_CMD_RESET);
	if (ret)
		return ret;

	udelay(st->reset_delay_us);

	/* Disable read data in continuous mode (enabled by default) */
	ret = ads131e08_exec_cmd(st, ADS131E08_CMD_SDATAC);
	if (ret)
		return ret;

	ret = ads131e08_set_data_rate(st, ADS131E08_DEFAULT_DATA_RATE);
	if (ret)
		return ret;

	ret = ads131e08_config_reference_voltage(st);
	if (ret)
		return ret;

	for (i = 0;  i < indio_dev->num_channels; i++) {
		ret = ads131e08_set_pga_gain(st, channel->channel,
			st->channel_config[i].pga_gain);
		if (ret)
			return ret;

		ret = ads131e08_set_channel_mux(st, channel->channel,
			st->channel_config[i].mux);
		if (ret)
			return ret;

		active_channels |= BIT(channel->channel);
		channel++;
	}

	/* Power down unused channels */
	for_each_clear_bit(i, &active_channels, st->info->max_channels) {
		ret = ads131e08_power_down_channel(st, i, true);
		if (ret)
			return ret;
	}

	/* Request channel offset calibration */
	ret = ads131e08_exec_cmd(st, ADS131E08_CMD_OFFSETCAL);
	if (ret)
		return ret;

	/*
	 * Channel offset calibration is triggered with the first START
	 * command. Since calibration takes more time than settling operation,
	 * this causes timeout error when command START is sent first
	 * time (e.g. first call of the ads131e08_read_direct method).
	 * To avoid this problem offset calibration is triggered here.
	 */
	ret = ads131e08_exec_cmd(st, ADS131E08_CMD_START);
	if (ret)
		return ret;

	msleep(ADS131E08_WAIT_OFFSETCAL_MS);

	return ads131e08_exec_cmd(st, ADS131E08_CMD_STOP);
}

static int ads131e08_pool_data(struct ads131e08_state *st)
{
	unsigned long timeout;
	int ret;

	reinit_completion(&st->completion);

	ret = ads131e08_exec_cmd(st, ADS131E08_CMD_START);
	if (ret)
		return ret;

	timeout = msecs_to_jiffies(ADS131E08_MAX_SETTLING_TIME_MS);
	ret = wait_for_completion_timeout(&st->completion, timeout);
	if (!ret)
		return -ETIMEDOUT;

	ret = ads131e08_read_data(st, st->readback_len);
	if (ret)
		return ret;

	return ads131e08_exec_cmd(st, ADS131E08_CMD_STOP);
}

static int ads131e08_read_direct(struct iio_dev *indio_dev,
	struct iio_chan_spec const *channel, int *value)
{
	struct ads131e08_state *st = iio_priv(indio_dev);
	u8 num_bits, *src;
	int ret;

	ret = ads131e08_pool_data(st);
	if (ret)
		return ret;

	src = st->rx_buf + ADS131E08_NUM_STATUS_BYTES +
		channel->channel * ADS131E08_NUM_DATA_BYTES(st->data_rate);

	num_bits = ADS131E08_NUM_DATA_BITS(st->data_rate);
	*value = sign_extend32(get_unaligned_be32(src) >> (32 - num_bits), num_bits - 1);

	return 0;
}

static int ads131e08_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *channel, int *value,
	int *value2, long mask)
{
	struct ads131e08_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = ads131e08_read_direct(indio_dev, channel, value);
		iio_device_release_direct_mode(indio_dev);
		if (ret)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		if (st->vref_reg) {
			ret = regulator_get_voltage(st->vref_reg);
			if (ret < 0)
				return ret;

			*value = ret / 1000;
		} else {
			*value = st->vref_mv;
		}

		*value /= st->channel_config[channel->address].pga_gain;
		*value2 = ADS131E08_NUM_DATA_BITS(st->data_rate) - 1;

		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*value = st->data_rate;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ads131e08_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *channel, int value,
	int value2, long mask)
{
	struct ads131e08_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = ads131e08_set_data_rate(st, value);
		iio_device_release_direct_mode(indio_dev);
		return ret;

	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("1 2 4 8 16 32 64");

static struct attribute *ads131e08_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ads131e08_attribute_group = {
	.attrs = ads131e08_attributes,
};

static int ads131e08_debugfs_reg_access(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int writeval, unsigned int *readval)
{
	struct ads131e08_state *st = iio_priv(indio_dev);

	if (readval) {
		int ret = ads131e08_read_reg(st, reg);
		*readval = ret;
		return ret;
	}

	return ads131e08_write_reg(st, reg, writeval);
}

static const struct iio_info ads131e08_iio_info = {
	.read_raw = ads131e08_read_raw,
	.write_raw = ads131e08_write_raw,
	.attrs = &ads131e08_attribute_group,
	.debugfs_reg_access = &ads131e08_debugfs_reg_access,
};

static int ads131e08_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct ads131e08_state *st = iio_priv(indio_dev);
	u8 cmd = state ? ADS131E08_CMD_START : ADS131E08_CMD_STOP;

	return ads131e08_exec_cmd(st, cmd);
}

static const struct iio_trigger_ops ads131e08_trigger_ops = {
	.set_trigger_state = &ads131e08_set_trigger_state,
	.validate_device = &iio_trigger_validate_own_device,
};

static irqreturn_t ads131e08_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads131e08_state *st = iio_priv(indio_dev);
	unsigned int chn, i = 0;
	u8 *src, *dest;
	int ret;

	/*
	 * The number of data bits per channel depends on the data rate.
	 * For 32 and 64 ksps data rates, number of data bits per channel
	 * is 16. This case is not compliant with used (fixed) scan element
	 * type (be:s24/32>>8). So we use a little tweak to pack properly
	 * 16 bits of data into the buffer.
	 */
	unsigned int num_bytes = ADS131E08_NUM_DATA_BYTES(st->data_rate);
	u8 tweek_offset = num_bytes == 2 ? 1 : 0;

	if (iio_trigger_using_own(indio_dev))
		ret = ads131e08_read_data(st, st->readback_len);
	else
		ret = ads131e08_pool_data(st);

	if (ret)
		goto out;

	iio_for_each_active_channel(indio_dev, chn) {
		src = st->rx_buf + ADS131E08_NUM_STATUS_BYTES + chn * num_bytes;
		dest = st->tmp_buf.data + i * ADS131E08_NUM_STORAGE_BYTES;

		/*
		 * Tweek offset is 0:
		 * +---+---+---+---+
		 * |D0 |D1 |D2 | X | (3 data bytes)
		 * +---+---+---+---+
		 *  a+0 a+1 a+2 a+3
		 *
		 * Tweek offset is 1:
		 * +---+---+---+---+
		 * |P0 |D0 |D1 | X | (one padding byte and 2 data bytes)
		 * +---+---+---+---+
		 *  a+0 a+1 a+2 a+3
		 */
		memcpy(dest + tweek_offset, src, num_bytes);

		/*
		 * Data conversion from 16 bits of data to 24 bits of data
		 * is done by sign extension (properly filling padding byte).
		 */
		if (tweek_offset)
			*dest = *src & BIT(7) ? 0xff : 0x00;

		i++;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, st->tmp_buf.data,
		iio_get_time_ns(indio_dev));

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t ads131e08_interrupt(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ads131e08_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev) && iio_trigger_using_own(indio_dev))
		iio_trigger_poll(st->trig);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
}

static int ads131e08_alloc_channels(struct iio_dev *indio_dev)
{
	struct ads131e08_state *st = iio_priv(indio_dev);
	struct ads131e08_channel_config *channel_config;
	struct device *dev = &st->spi->dev;
	struct iio_chan_spec *channels;
	unsigned int channel, tmp;
	int num_channels, i, ret;

	ret = device_property_read_u32(dev, "ti,vref-internal", &tmp);
	if (ret)
		tmp = 0;

	switch (tmp) {
	case 0:
		st->vref_mv = ADS131E08_VREF_2V4_mV;
		break;
	case 1:
		st->vref_mv = ADS131E08_VREF_4V_mV;
		break;
	default:
		dev_err(&st->spi->dev, "invalid internal voltage reference\n");
		return -EINVAL;
	}

	num_channels = device_get_child_node_count(dev);
	if (num_channels == 0) {
		dev_err(&st->spi->dev, "no channel children\n");
		return -ENODEV;
	}

	if (num_channels > st->info->max_channels) {
		dev_err(&st->spi->dev, "num of channel children out of range\n");
		return -EINVAL;
	}

	channels = devm_kcalloc(&st->spi->dev, num_channels,
		sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	channel_config = devm_kcalloc(&st->spi->dev, num_channels,
		sizeof(*channel_config), GFP_KERNEL);
	if (!channel_config)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node_scoped(dev, node) {
		ret = fwnode_property_read_u32(node, "reg", &channel);
		if (ret)
			return ret;

		ret = fwnode_property_read_u32(node, "ti,gain", &tmp);
		if (ret) {
			channel_config[i].pga_gain = ADS131E08_DEFAULT_PGA_GAIN;
		} else {
			ret = ads131e08_pga_gain_to_field_value(st, tmp);
			if (ret < 0)
				return ret;

			channel_config[i].pga_gain = tmp;
		}

		ret = fwnode_property_read_u32(node, "ti,mux", &tmp);
		if (ret) {
			channel_config[i].mux = ADS131E08_DEFAULT_MUX;
		} else {
			ret = ads131e08_validate_channel_mux(st, tmp);
			if (ret)
				return ret;

			channel_config[i].mux = tmp;
		}

		channels[i].type = IIO_VOLTAGE;
		channels[i].indexed = 1;
		channels[i].channel = channel;
		channels[i].address = i;
		channels[i].info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
						BIT(IIO_CHAN_INFO_SCALE);
		channels[i].info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ);
		channels[i].scan_index = channel;
		channels[i].scan_type.sign = 's';
		channels[i].scan_type.realbits = 24;
		channels[i].scan_type.storagebits = 32;
		channels[i].scan_type.shift = 8;
		channels[i].scan_type.endianness = IIO_BE;
		i++;
	}

	indio_dev->channels = channels;
	indio_dev->num_channels = num_channels;
	st->channel_config = channel_config;

	return 0;

}

static void ads131e08_regulator_disable(void *data)
{
	struct ads131e08_state *st = data;

	regulator_disable(st->vref_reg);
}

static int ads131e08_probe(struct spi_device *spi)
{
	const struct ads131e08_info *info;
	struct ads131e08_state *st;
	struct iio_dev *indio_dev;
	unsigned long adc_clk_hz;
	unsigned long adc_clk_ns;
	int ret;

	info = spi_get_device_match_data(spi);
	if (!info) {
		dev_err(&spi->dev, "failed to get match data\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev) {
		dev_err(&spi->dev, "failed to allocate IIO device\n");
		return -ENOMEM;
	}

	st = iio_priv(indio_dev);
	st->info = info;
	st->spi = spi;

	ret = ads131e08_alloc_channels(indio_dev);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->info = &ads131e08_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	init_completion(&st->completion);

	if (spi->irq) {
		ret = devm_request_irq(&spi->dev, spi->irq,
			ads131e08_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			spi->dev.driver->name, indio_dev);
		if (ret)
			return dev_err_probe(&spi->dev, ret,
					     "request irq failed\n");
	} else {
		dev_err(&spi->dev, "data ready IRQ missing\n");
		return -ENODEV;
	}

	st->trig = devm_iio_trigger_alloc(&spi->dev, "%s-dev%d",
		indio_dev->name, iio_device_id(indio_dev));
	if (!st->trig) {
		dev_err(&spi->dev, "failed to allocate IIO trigger\n");
		return -ENOMEM;
	}

	st->trig->ops = &ads131e08_trigger_ops;
	st->trig->dev.parent = &spi->dev;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = devm_iio_trigger_register(&spi->dev, st->trig);
	if (ret) {
		dev_err(&spi->dev, "failed to register IIO trigger\n");
		return -ENOMEM;
	}

	indio_dev->trig = iio_trigger_get(st->trig);

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
		NULL, &ads131e08_trigger_handler, NULL);
	if (ret) {
		dev_err(&spi->dev, "failed to setup IIO buffer\n");
		return ret;
	}

	st->vref_reg = devm_regulator_get_optional(&spi->dev, "vref");
	if (!IS_ERR(st->vref_reg)) {
		ret = regulator_enable(st->vref_reg);
		if (ret) {
			dev_err(&spi->dev,
				"failed to enable external vref supply\n");
			return ret;
		}

		ret = devm_add_action_or_reset(&spi->dev, ads131e08_regulator_disable, st);
		if (ret)
			return ret;
	} else {
		if (PTR_ERR(st->vref_reg) != -ENODEV)
			return PTR_ERR(st->vref_reg);

		st->vref_reg = NULL;
	}

	st->adc_clk = devm_clk_get_enabled(&spi->dev, "adc-clk");
	if (IS_ERR(st->adc_clk))
		return dev_err_probe(&spi->dev, PTR_ERR(st->adc_clk),
				     "failed to get the ADC clock\n");

	adc_clk_hz = clk_get_rate(st->adc_clk);
	if (!adc_clk_hz) {
		dev_err(&spi->dev, "failed to get the ADC clock rate\n");
		return  -EINVAL;
	}

	adc_clk_ns = NSEC_PER_SEC / adc_clk_hz;
	st->sdecode_delay_us = DIV_ROUND_UP(
		ADS131E08_WAIT_SDECODE_CYCLES * adc_clk_ns, NSEC_PER_USEC);
	st->reset_delay_us = DIV_ROUND_UP(
		ADS131E08_WAIT_RESET_CYCLES * adc_clk_ns, NSEC_PER_USEC);

	ret = ads131e08_initial_config(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "initial configuration failed\n");
		return ret;
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ads131e08_of_match[] = {
	{ .compatible = "ti,ads131e04",
	  .data = &ads131e08_info_tbl[ads131e04], },
	{ .compatible = "ti,ads131e06",
	  .data = &ads131e08_info_tbl[ads131e06], },
	{ .compatible = "ti,ads131e08",
	  .data = &ads131e08_info_tbl[ads131e08], },
	{ }
};
MODULE_DEVICE_TABLE(of, ads131e08_of_match);

static const struct spi_device_id ads131e08_ids[] = {
	{ "ads131e04", (kernel_ulong_t)&ads131e08_info_tbl[ads131e04] },
	{ "ads131e06", (kernel_ulong_t)&ads131e08_info_tbl[ads131e06] },
	{ "ads131e08", (kernel_ulong_t)&ads131e08_info_tbl[ads131e08] },
	{ }
};
MODULE_DEVICE_TABLE(spi, ads131e08_ids);

static struct spi_driver ads131e08_driver = {
	.driver = {
		.name = "ads131e08",
		.of_match_table = ads131e08_of_match,
	},
	.probe = ads131e08_probe,
	.id_table = ads131e08_ids,
};
module_spi_driver(ads131e08_driver);

MODULE_AUTHOR("Tomislav Denis <tomislav.denis@avl.com>");
MODULE_DESCRIPTION("Driver for ADS131E0x ADC family");
MODULE_LICENSE("GPL v2");
