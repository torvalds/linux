// SPDX-License-Identifier: GPL-2.0
/* TI ADS1298 chip family driver
 * Copyright (C) 2023 - 2024 Topic Embedded Products
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#include <linux/unaligned.h>

/* Commands */
#define ADS1298_CMD_WAKEUP	0x02
#define ADS1298_CMD_STANDBY	0x04
#define ADS1298_CMD_RESET	0x06
#define ADS1298_CMD_START	0x08
#define ADS1298_CMD_STOP	0x0a
#define ADS1298_CMD_RDATAC	0x10
#define ADS1298_CMD_SDATAC	0x11
#define ADS1298_CMD_RDATA	0x12
#define ADS1298_CMD_RREG	0x20
#define ADS1298_CMD_WREG	0x40

/* Registers */
#define ADS1298_REG_ID		0x00
#define ADS1298_MASK_ID_FAMILY			GENMASK(7, 3)
#define ADS1298_MASK_ID_CHANNELS		GENMASK(2, 0)
#define ADS1298_ID_FAMILY_ADS129X		0x90
#define ADS1298_ID_FAMILY_ADS129XR		0xd0

#define ADS1298_REG_CONFIG1	0x01
#define ADS1298_MASK_CONFIG1_HR			BIT(7)
#define ADS1298_MASK_CONFIG1_DR			GENMASK(2, 0)
#define ADS1298_SHIFT_DR_HR			6
#define ADS1298_SHIFT_DR_LP			7
#define ADS1298_LOWEST_DR			0x06

#define ADS1298_REG_CONFIG2	0x02
#define ADS1298_MASK_CONFIG2_RESERVED		BIT(6)
#define ADS1298_MASK_CONFIG2_WCT_CHOP		BIT(5)
#define ADS1298_MASK_CONFIG2_INT_TEST		BIT(4)
#define ADS1298_MASK_CONFIG2_TEST_AMP		BIT(2)
#define ADS1298_MASK_CONFIG2_TEST_FREQ_DC	GENMASK(1, 0)
#define ADS1298_MASK_CONFIG2_TEST_FREQ_SLOW	0
#define ADS1298_MASK_CONFIG2_TEST_FREQ_FAST	BIT(0)

#define ADS1298_REG_CONFIG3	0x03
#define ADS1298_MASK_CONFIG3_PWR_REFBUF		BIT(7)
#define ADS1298_MASK_CONFIG3_RESERVED		BIT(6)
#define ADS1298_MASK_CONFIG3_VREF_4V		BIT(5)

#define ADS1298_REG_LOFF	0x04
#define ADS1298_REG_CHnSET(n)	(0x05 + n)
#define ADS1298_MASK_CH_PD		BIT(7)
#define ADS1298_MASK_CH_PGA		GENMASK(6, 4)
#define ADS1298_MASK_CH_MUX		GENMASK(2, 0)

#define ADS1298_REG_LOFF_STATP	0x12
#define ADS1298_REG_LOFF_STATN	0x13
#define ADS1298_REG_CONFIG4	0x17
#define ADS1298_MASK_CONFIG4_SINGLE_SHOT	BIT(3)

#define ADS1298_REG_WCT1	0x18
#define ADS1298_REG_WCT2	0x19

#define ADS1298_MAX_CHANNELS	8
#define ADS1298_BITS_PER_SAMPLE	24
#define ADS1298_CLK_RATE_HZ	2048000
#define ADS1298_CLOCKS_TO_USECS(x) \
		(DIV_ROUND_UP((x) * MICROHZ_PER_HZ, ADS1298_CLK_RATE_HZ))
/*
 * Read/write register commands require 4 clocks to decode, for speeds above
 * 2x the clock rate, this would require extra time between the command byte and
 * the data. Much simpler is to just limit the SPI transfer speed while doing
 * register access.
 */
#define ADS1298_SPI_BUS_SPEED_SLOW	ADS1298_CLK_RATE_HZ
/* For reading and writing registers, we need a 3-byte buffer */
#define ADS1298_SPI_CMD_BUFFER_SIZE	3
/* Outputs status word and 'n' 24-bit samples, plus the command byte */
#define ADS1298_SPI_RDATA_BUFFER_SIZE(n)	(((n) + 1) * 3 + 1)
#define ADS1298_SPI_RDATA_BUFFER_SIZE_MAX \
		ADS1298_SPI_RDATA_BUFFER_SIZE(ADS1298_MAX_CHANNELS)

struct ads1298_private {
	const struct ads1298_chip_info *chip_info;
	struct spi_device *spi;
	struct regulator *reg_avdd;
	struct regulator *reg_vref;
	struct clk *clk;
	struct regmap *regmap;
	struct completion completion;
	struct iio_trigger *trig;
	struct spi_transfer rdata_xfer;
	struct spi_message rdata_msg;
	spinlock_t irq_busy_lock; /* Handshake between SPI and DRDY irqs */
	/*
	 * rdata_xfer_busy increments when a DRDY occurs and decrements when SPI
	 * completion is reported. Hence its meaning is:
	 * 0 = Waiting for DRDY interrupt
	 * 1 = SPI transfer in progress
	 * 2 = DRDY during SPI transfer, start another transfer on completion
	 * >2 = Multiple DRDY during transfer, lost rdata_xfer_busy - 2 samples
	 */
	unsigned int rdata_xfer_busy;

	/* Temporary storage for demuxing data after SPI transfer */
	u32 bounce_buffer[ADS1298_MAX_CHANNELS];

	/* For synchronous SPI exchanges (read/write registers) */
	u8 cmd_buffer[ADS1298_SPI_CMD_BUFFER_SIZE] __aligned(IIO_DMA_MINALIGN);

	/* Buffer used for incoming SPI data */
	u8 rx_buffer[ADS1298_SPI_RDATA_BUFFER_SIZE_MAX];
	/* Contains the RDATA command and zeroes to clock out */
	u8 tx_buffer[ADS1298_SPI_RDATA_BUFFER_SIZE_MAX];
};

/* Three bytes per sample in RX buffer, starting at offset 4 */
#define ADS1298_OFFSET_IN_RX_BUFFER(index)	(3 * (index) + 4)

#define ADS1298_CHAN(index)				\
{							\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = index,				\
	.address = ADS1298_OFFSET_IN_RX_BUFFER(index),	\
	.info_mask_separate =				\
		BIT(IIO_CHAN_INFO_RAW) |		\
		BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all =			\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |		\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
	.scan_index = index,				\
	.scan_type = {					\
		.sign = 's',				\
		.realbits = ADS1298_BITS_PER_SAMPLE,	\
		.storagebits = 32,			\
		.endianness = IIO_CPU,			\
	},						\
}

static const struct iio_chan_spec ads1298_channels[] = {
	ADS1298_CHAN(0),
	ADS1298_CHAN(1),
	ADS1298_CHAN(2),
	ADS1298_CHAN(3),
	ADS1298_CHAN(4),
	ADS1298_CHAN(5),
	ADS1298_CHAN(6),
	ADS1298_CHAN(7),
};

static int ads1298_write_cmd(struct ads1298_private *priv, u8 command)
{
	struct spi_transfer xfer = {
		.tx_buf = priv->cmd_buffer,
		.rx_buf = priv->cmd_buffer,
		.len = 1,
		.speed_hz = ADS1298_SPI_BUS_SPEED_SLOW,
		.delay = {
			.value = 2,
			.unit = SPI_DELAY_UNIT_USECS,
		},
	};

	priv->cmd_buffer[0] = command;

	return spi_sync_transfer(priv->spi, &xfer, 1);
}

static int ads1298_read_one(struct ads1298_private *priv, int chan_index)
{
	int ret;

	/* Enable the channel */
	ret = regmap_update_bits(priv->regmap, ADS1298_REG_CHnSET(chan_index),
				 ADS1298_MASK_CH_PD, 0);
	if (ret)
		return ret;

	/* Enable single-shot mode, so we don't need to send a STOP */
	ret = regmap_update_bits(priv->regmap, ADS1298_REG_CONFIG4,
				 ADS1298_MASK_CONFIG4_SINGLE_SHOT,
				 ADS1298_MASK_CONFIG4_SINGLE_SHOT);
	if (ret)
		return ret;

	reinit_completion(&priv->completion);

	ret = ads1298_write_cmd(priv, ADS1298_CMD_START);
	if (ret < 0) {
		dev_err(&priv->spi->dev, "CMD_START error: %d\n", ret);
		return ret;
	}

	/* Cannot take longer than 40ms (250Hz) */
	ret = wait_for_completion_timeout(&priv->completion, msecs_to_jiffies(50));
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static int ads1298_get_samp_freq(struct ads1298_private *priv, int *val)
{
	unsigned long rate;
	unsigned int cfg;
	int ret;

	ret = regmap_read(priv->regmap, ADS1298_REG_CONFIG1, &cfg);
	if (ret)
		return ret;

	if (priv->clk)
		rate = clk_get_rate(priv->clk);
	else
		rate = ADS1298_CLK_RATE_HZ;
	if (!rate)
		return -EINVAL;

	/* Data rate shift depends on HR/LP mode */
	if (cfg & ADS1298_MASK_CONFIG1_HR)
		rate >>= ADS1298_SHIFT_DR_HR;
	else
		rate >>= ADS1298_SHIFT_DR_LP;

	*val = rate >> (cfg & ADS1298_MASK_CONFIG1_DR);

	return IIO_VAL_INT;
}

static int ads1298_set_samp_freq(struct ads1298_private *priv, int val)
{
	unsigned long rate;
	unsigned int factor;
	unsigned int cfg;

	if (priv->clk)
		rate = clk_get_rate(priv->clk);
	else
		rate = ADS1298_CLK_RATE_HZ;
	if (!rate)
		return -EINVAL;
	if (val <= 0)
		return -EINVAL;

	factor = (rate >> ADS1298_SHIFT_DR_HR) / val;
	if (factor >= BIT(ADS1298_SHIFT_DR_LP))
		cfg = ADS1298_LOWEST_DR;
	else if (factor)
		cfg = ADS1298_MASK_CONFIG1_HR | ilog2(factor); /* Use HR mode */
	else
		cfg = ADS1298_MASK_CONFIG1_HR; /* Fastest possible */

	return regmap_update_bits(priv->regmap, ADS1298_REG_CONFIG1,
				  ADS1298_MASK_CONFIG1_HR | ADS1298_MASK_CONFIG1_DR,
				  cfg);
}

static const u8 ads1298_pga_settings[] = { 6, 1, 2, 3, 4, 8, 12 };

static int ads1298_get_scale(struct ads1298_private *priv,
			     int channel, int *val, int *val2)
{
	int ret;
	unsigned int regval;
	u8 gain;

	if (priv->reg_vref) {
		ret = regulator_get_voltage(priv->reg_vref);
		if (ret < 0)
			return ret;

		*val = ret / MILLI; /* Convert to millivolts */
	} else {
		ret = regmap_read(priv->regmap, ADS1298_REG_CONFIG3, &regval);
		if (ret)
			return ret;

		/* Reference in millivolts */
		*val = regval & ADS1298_MASK_CONFIG3_VREF_4V ? 4000 : 2400;
	}

	ret = regmap_read(priv->regmap, ADS1298_REG_CHnSET(channel), &regval);
	if (ret)
		return ret;

	gain = ads1298_pga_settings[FIELD_GET(ADS1298_MASK_CH_PGA, regval)];
	*val /= gain; /* Full scale is VREF / gain */

	*val2 = ADS1298_BITS_PER_SAMPLE - 1; /* Signed, hence the -1 */

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int ads1298_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ads1298_private *priv = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ads1298_read_one(priv, chan->scan_index);

		iio_device_release_direct(indio_dev);

		if (ret)
			return ret;

		*val = sign_extend32(get_unaligned_be24(priv->rx_buffer + chan->address),
				     ADS1298_BITS_PER_SAMPLE - 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		return ads1298_get_scale(priv, chan->channel, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ads1298_get_samp_freq(priv, val);
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = regmap_read(priv->regmap, ADS1298_REG_CONFIG1, val);
		if (ret)
			return ret;

		*val = 16 << (*val & ADS1298_MASK_CONFIG1_DR);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ads1298_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct ads1298_private *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ads1298_set_samp_freq(priv, val);
	default:
		return -EINVAL;
	}
}

static int ads1298_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct ads1298_private *priv = context;
	struct spi_transfer reg_write_xfer = {
		.tx_buf = priv->cmd_buffer,
		.rx_buf = priv->cmd_buffer,
		.len = 3,
		.speed_hz = ADS1298_SPI_BUS_SPEED_SLOW,
		.delay = {
			.value = 2,
			.unit = SPI_DELAY_UNIT_USECS,
		},
	};

	priv->cmd_buffer[0] = ADS1298_CMD_WREG | reg;
	priv->cmd_buffer[1] = 0; /* Number of registers to be written - 1 */
	priv->cmd_buffer[2] = val;

	return spi_sync_transfer(priv->spi, &reg_write_xfer, 1);
}

static int ads1298_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct ads1298_private *priv = context;
	struct spi_transfer reg_read_xfer = {
		.tx_buf = priv->cmd_buffer,
		.rx_buf = priv->cmd_buffer,
		.len = 3,
		.speed_hz = ADS1298_SPI_BUS_SPEED_SLOW,
		.delay = {
			.value = 2,
			.unit = SPI_DELAY_UNIT_USECS,
		},
	};
	int ret;

	priv->cmd_buffer[0] = ADS1298_CMD_RREG | reg;
	priv->cmd_buffer[1] = 0; /* Number of registers to be read - 1 */
	priv->cmd_buffer[2] = 0;

	ret = spi_sync_transfer(priv->spi, &reg_read_xfer, 1);
	if (ret)
		return ret;

	*val = priv->cmd_buffer[2];

	return 0;
}

static int ads1298_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct ads1298_private *priv = iio_priv(indio_dev);

	if (readval)
		return regmap_read(priv->regmap, reg, readval);

	return regmap_write(priv->regmap, reg, writeval);
}

static void ads1298_rdata_unmark_busy(struct ads1298_private *priv)
{
	/* Notify we're no longer waiting for the SPI transfer to complete */
	guard(spinlock_irqsave)(&priv->irq_busy_lock);
	priv->rdata_xfer_busy = 0;
}

static int ads1298_update_scan_mode(struct iio_dev *indio_dev,
				    const unsigned long *scan_mask)
{
	struct ads1298_private *priv = iio_priv(indio_dev);
	unsigned int val;
	int ret;
	int i;

	/* Make the interrupt routines start with a clean slate */
	ads1298_rdata_unmark_busy(priv);

	/* Configure power-down bits to match scan mask */
	for (i = 0; i < indio_dev->num_channels; i++) {
		val = test_bit(i, scan_mask) ? 0 : ADS1298_MASK_CH_PD;
		ret = regmap_update_bits(priv->regmap, ADS1298_REG_CHnSET(i),
					 ADS1298_MASK_CH_PD, val);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct iio_info ads1298_info = {
	.read_raw = &ads1298_read_raw,
	.write_raw = &ads1298_write_raw,
	.update_scan_mode = &ads1298_update_scan_mode,
	.debugfs_reg_access = &ads1298_reg_access,
};

static void ads1298_rdata_release_busy_or_restart(struct ads1298_private *priv)
{
	guard(spinlock_irqsave)(&priv->irq_busy_lock);

	if (priv->rdata_xfer_busy > 1) {
		/*
		 * DRDY interrupt occurred before SPI completion. Start a new
		 * SPI transaction now to retrieve the data that wasn't latched
		 * into the ADS1298 chip's transfer buffer yet.
		 */
		spi_async(priv->spi, &priv->rdata_msg);
		/*
		 * If more than one DRDY took place, there was an overrun. Since
		 * the sample is already lost, reset the counter to 1 so that
		 * we will wait for a DRDY interrupt after this SPI transaction.
		 */
		priv->rdata_xfer_busy = 1;
	} else {
		/* No pending data, wait for DRDY */
		priv->rdata_xfer_busy = 0;
	}
}

/* Called from SPI completion interrupt handler */
static void ads1298_rdata_complete(void *context)
{
	struct iio_dev *indio_dev = context;
	struct ads1298_private *priv = iio_priv(indio_dev);
	int scan_index;
	u32 *bounce = priv->bounce_buffer;

	if (!iio_buffer_enabled(indio_dev)) {
		/*
		 * for a single transfer mode we're kept in direct_mode until
		 * completion, avoiding a race with buffered IO.
		 */
		ads1298_rdata_unmark_busy(priv);
		complete(&priv->completion);
		return;
	}

	/* Demux the channel data into our bounce buffer */
	iio_for_each_active_channel(indio_dev, scan_index) {
		const struct iio_chan_spec *scan_chan =
					&indio_dev->channels[scan_index];
		const u8 *data = priv->rx_buffer + scan_chan->address;

		*bounce++ = get_unaligned_be24(data);
	}

	/* rx_buffer can be overwritten from this point on */
	ads1298_rdata_release_busy_or_restart(priv);

	iio_push_to_buffers(indio_dev, priv->bounce_buffer);
}

static irqreturn_t ads1298_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ads1298_private *priv = iio_priv(indio_dev);
	unsigned int wasbusy;

	guard(spinlock_irqsave)(&priv->irq_busy_lock);

	wasbusy = priv->rdata_xfer_busy++;
	/* When no SPI transfer in transit, start one now */
	if (!wasbusy)
		spi_async(priv->spi, &priv->rdata_msg);

	return IRQ_HANDLED;
};

static int ads1298_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ads1298_private *priv = iio_priv(indio_dev);
	int ret;

	/* Disable single-shot mode */
	ret = regmap_update_bits(priv->regmap, ADS1298_REG_CONFIG4,
				 ADS1298_MASK_CONFIG4_SINGLE_SHOT, 0);
	if (ret)
		return ret;

	return ads1298_write_cmd(priv, ADS1298_CMD_START);
}

static int ads1298_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ads1298_private *priv = iio_priv(indio_dev);

	return ads1298_write_cmd(priv, ADS1298_CMD_STOP);
}

static const struct iio_buffer_setup_ops ads1298_setup_ops = {
	.postenable = &ads1298_buffer_postenable,
	.predisable = &ads1298_buffer_predisable,
};

static void ads1298_reg_disable(void *reg)
{
	regulator_disable(reg);
}

static const struct regmap_range ads1298_regmap_volatile_range[] = {
	regmap_reg_range(ADS1298_REG_LOFF_STATP, ADS1298_REG_LOFF_STATN),
};

static const struct regmap_access_table ads1298_regmap_volatile = {
	.yes_ranges = ads1298_regmap_volatile_range,
	.n_yes_ranges = ARRAY_SIZE(ads1298_regmap_volatile_range),
};

static const struct regmap_config ads1298_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_read = ads1298_reg_read,
	.reg_write = ads1298_reg_write,
	.max_register = ADS1298_REG_WCT2,
	.volatile_table = &ads1298_regmap_volatile,
	.cache_type = REGCACHE_MAPLE,
};

static int ads1298_init(struct iio_dev *indio_dev)
{
	struct ads1298_private *priv = iio_priv(indio_dev);
	struct device *dev = &priv->spi->dev;
	const char *suffix;
	unsigned int val;
	int ret;

	/* Device initializes into RDATAC mode, which we don't want */
	ret = ads1298_write_cmd(priv, ADS1298_CMD_SDATAC);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, ADS1298_REG_ID, &val);
	if (ret)
		return ret;

	/* Fill in name and channel count based on what the chip told us */
	indio_dev->num_channels = 4 + 2 * (val & ADS1298_MASK_ID_CHANNELS);
	switch (val & ADS1298_MASK_ID_FAMILY) {
	case ADS1298_ID_FAMILY_ADS129X:
		suffix = "";
		break;
	case ADS1298_ID_FAMILY_ADS129XR:
		suffix = "r";
		break;
	default:
		return dev_err_probe(dev, -ENODEV, "Unknown ID: 0x%x\n", val);
	}
	indio_dev->name = devm_kasprintf(dev, GFP_KERNEL, "ads129%u%s",
					 indio_dev->num_channels, suffix);
	if (!indio_dev->name)
		return -ENOMEM;

	/* Enable internal test signal, double amplitude, double frequency */
	ret = regmap_write(priv->regmap, ADS1298_REG_CONFIG2,
			   ADS1298_MASK_CONFIG2_RESERVED |
			   ADS1298_MASK_CONFIG2_INT_TEST |
			   ADS1298_MASK_CONFIG2_TEST_AMP |
			   ADS1298_MASK_CONFIG2_TEST_FREQ_FAST);
	if (ret)
		return ret;

	val = ADS1298_MASK_CONFIG3_RESERVED; /* Must write 1 always */
	if (!priv->reg_vref) {
		/* Enable internal reference */
		val |= ADS1298_MASK_CONFIG3_PWR_REFBUF;
		/* Use 4V VREF when power supply is at least 4.4V */
		if (regulator_get_voltage(priv->reg_avdd) >= 4400000)
			val |= ADS1298_MASK_CONFIG3_VREF_4V;
	}
	return regmap_write(priv->regmap, ADS1298_REG_CONFIG3, val);
}

static int ads1298_probe(struct spi_device *spi)
{
	struct ads1298_private *priv;
	struct iio_dev *indio_dev;
	struct device *dev = &spi->dev;
	struct gpio_desc *reset_gpio;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);

	/* Reset to be asserted before enabling clock and power */
	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "Cannot get reset GPIO\n");

	/* VREF can be supplied externally, otherwise use internal reference */
	priv->reg_vref = devm_regulator_get_optional(dev, "vref");
	if (IS_ERR(priv->reg_vref)) {
		if (PTR_ERR(priv->reg_vref) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(priv->reg_vref),
					     "Failed to get vref regulator\n");

		priv->reg_vref = NULL;
	} else {
		ret = regulator_enable(priv->reg_vref);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, ads1298_reg_disable, priv->reg_vref);
		if (ret)
			return ret;
	}

	priv->clk = devm_clk_get_optional_enabled(dev, "clk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Failed to get clk\n");

	priv->reg_avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(priv->reg_avdd))
		return dev_err_probe(dev, PTR_ERR(priv->reg_avdd),
				     "Failed to get avdd regulator\n");

	ret = regulator_enable(priv->reg_avdd);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable avdd regulator\n");

	ret = devm_add_action_or_reset(dev, ads1298_reg_disable, priv->reg_avdd);
	if (ret)
		return ret;

	priv->spi = spi;
	init_completion(&priv->completion);
	spin_lock_init(&priv->irq_busy_lock);
	priv->regmap = devm_regmap_init(dev, NULL, priv, &ads1298_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;
	indio_dev->channels = ads1298_channels;
	indio_dev->info = &ads1298_info;

	if (reset_gpio) {
		/*
		 * Deassert reset now that clock and power are active.
		 * Minimum reset pulsewidth is 2 clock cycles.
		 */
		fsleep(ADS1298_CLOCKS_TO_USECS(2));
		gpiod_set_value_cansleep(reset_gpio, 0);
	} else {
		ret = ads1298_write_cmd(priv, ADS1298_CMD_RESET);
		if (ret)
			return dev_err_probe(dev, ret, "RESET failed\n");
	}
	/* Wait 18 clock cycles for reset command to complete */
	fsleep(ADS1298_CLOCKS_TO_USECS(18));

	ret = ads1298_init(indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Init failed\n");

	priv->tx_buffer[0] = ADS1298_CMD_RDATA;
	priv->rdata_xfer.tx_buf = priv->tx_buffer;
	priv->rdata_xfer.rx_buf = priv->rx_buffer;
	priv->rdata_xfer.len = ADS1298_SPI_RDATA_BUFFER_SIZE(indio_dev->num_channels);
	/* Must keep CS low for 4 clocks */
	priv->rdata_xfer.delay.value = 2;
	priv->rdata_xfer.delay.unit = SPI_DELAY_UNIT_USECS;
	spi_message_init_with_transfers(&priv->rdata_msg, &priv->rdata_xfer, 1);
	priv->rdata_msg.complete = &ads1298_rdata_complete;
	priv->rdata_msg.context = indio_dev;

	ret = devm_request_irq(dev, spi->irq, &ads1298_interrupt,
			       IRQF_TRIGGER_FALLING, indio_dev->name,
			       indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_kfifo_buffer_setup(dev, indio_dev, &ads1298_setup_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct spi_device_id ads1298_id[] = {
	{ "ads1298" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ads1298_id);

static const struct of_device_id ads1298_of_table[] = {
	{ .compatible = "ti,ads1298" },
	{ }
};
MODULE_DEVICE_TABLE(of, ads1298_of_table);

static struct spi_driver ads1298_driver = {
	.driver = {
		.name	= "ads1298",
		.of_match_table = ads1298_of_table,
	},
	.probe		= ads1298_probe,
	.id_table	= ads1298_id,
};
module_spi_driver(ads1298_driver);

MODULE_AUTHOR("Mike Looijmans <mike.looijmans@topic.nl>");
MODULE_DESCRIPTION("TI ADS1298 ADC");
MODULE_LICENSE("GPL");
