// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments ADS7950 SPI ADC driver
 *
 * Copyright 2016 David Lechner <david@lechnology.com>
 *
 * Based on iio/ad7923.c:
 * Copyright 2011 Analog Devices Inc
 * Copyright 2012 CS Systemes d'Information
 *
 * And also on hwmon/ads79xx.c
 * Copyright (C) 2013 Texas Instruments Incorporated - https://www.ti.com/
 *	Nishanth Menon
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/*
 * In case of ACPI, we use the 5000 mV as default for the reference pin.
 * Device tree users encode that via the vref-supply regulator.
 */
#define TI_ADS7950_VA_MV_ACPI_DEFAULT	5000

#define TI_ADS7950_CR_GPIO	BIT(14)
#define TI_ADS7950_CR_MANUAL	BIT(12)
#define TI_ADS7950_CR_WRITE	BIT(11)
#define TI_ADS7950_CR_CHAN(ch)	((ch) << 7)
#define TI_ADS7950_CR_RANGE_5V	BIT(6)
#define TI_ADS7950_CR_GPIO_DATA	BIT(4)

#define TI_ADS7950_MAX_CHAN	16
#define TI_ADS7950_NUM_GPIOS	4

#define TI_ADS7950_TIMESTAMP_SIZE (sizeof(int64_t) / sizeof(__be16))

/* val = value, dec = left shift, bits = number of bits of the mask */
#define TI_ADS7950_EXTRACT(val, dec, bits) \
	(((val) >> (dec)) & ((1 << (bits)) - 1))

#define TI_ADS7950_MAN_CMD(cmd)         (TI_ADS7950_CR_MANUAL | (cmd))
#define TI_ADS7950_GPIO_CMD(cmd)        (TI_ADS7950_CR_GPIO | (cmd))

/* Manual mode configuration */
#define TI_ADS7950_MAN_CMD_SETTINGS(st) \
	(TI_ADS7950_MAN_CMD(TI_ADS7950_CR_WRITE | st->cmd_settings_bitmask))
/* GPIO mode configuration */
#define TI_ADS7950_GPIO_CMD_SETTINGS(st) \
	(TI_ADS7950_GPIO_CMD(st->gpio_cmd_settings_bitmask))

struct ti_ads7950_state {
	struct spi_device	*spi;
	struct spi_transfer	ring_xfer;
	struct spi_transfer	scan_single_xfer[3];
	struct spi_message	ring_msg;
	struct spi_message	scan_single_msg;

	/* Lock to protect the spi xfer buffers */
	struct mutex		slock;
	struct gpio_chip	chip;

	struct regulator	*reg;
	unsigned int		vref_mv;

	/*
	 * Bitmask of lower 7 bits used for configuration
	 * These bits only can be written when TI_ADS7950_CR_WRITE
	 * is set, otherwise it retains its original state.
	 * [0-3] GPIO signal
	 * [4]   Set following frame to return GPIO signal values
	 * [5]   Powers down device
	 * [6]   Sets Vref range1(2.5v) or range2(5v)
	 *
	 * Bits present on Manual/Auto1/Auto2 commands
	 */
	unsigned int		cmd_settings_bitmask;

	/*
	 * Bitmask of GPIO command
	 * [0-3] GPIO direction
	 * [4-6] Different GPIO alarm mode configurations
	 * [7]   GPIO 2 as device range input
	 * [8]   GPIO 3 as device power down input
	 * [9]   Reset all registers
	 * [10-11] N/A
	 */
	unsigned int		gpio_cmd_settings_bitmask;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u16 rx_buf[TI_ADS7950_MAX_CHAN + 2 + TI_ADS7950_TIMESTAMP_SIZE]
							____cacheline_aligned;
	u16 tx_buf[TI_ADS7950_MAX_CHAN + 2];
	u16 single_tx;
	u16 single_rx;

};

struct ti_ads7950_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

enum ti_ads7950_id {
	TI_ADS7950,
	TI_ADS7951,
	TI_ADS7952,
	TI_ADS7953,
	TI_ADS7954,
	TI_ADS7955,
	TI_ADS7956,
	TI_ADS7957,
	TI_ADS7958,
	TI_ADS7959,
	TI_ADS7960,
	TI_ADS7961,
};

#define TI_ADS7950_V_CHAN(index, bits)				\
{								\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = index,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.address = index,					\
	.datasheet_name = "CH##index",				\
	.scan_index = index,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = bits,				\
		.storagebits = 16,				\
		.shift = 12 - (bits),				\
		.endianness = IIO_CPU,				\
	},							\
}

#define DECLARE_TI_ADS7950_4_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	TI_ADS7950_V_CHAN(0, bits), \
	TI_ADS7950_V_CHAN(1, bits), \
	TI_ADS7950_V_CHAN(2, bits), \
	TI_ADS7950_V_CHAN(3, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(4), \
}

#define DECLARE_TI_ADS7950_8_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	TI_ADS7950_V_CHAN(0, bits), \
	TI_ADS7950_V_CHAN(1, bits), \
	TI_ADS7950_V_CHAN(2, bits), \
	TI_ADS7950_V_CHAN(3, bits), \
	TI_ADS7950_V_CHAN(4, bits), \
	TI_ADS7950_V_CHAN(5, bits), \
	TI_ADS7950_V_CHAN(6, bits), \
	TI_ADS7950_V_CHAN(7, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(8), \
}

#define DECLARE_TI_ADS7950_12_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	TI_ADS7950_V_CHAN(0, bits), \
	TI_ADS7950_V_CHAN(1, bits), \
	TI_ADS7950_V_CHAN(2, bits), \
	TI_ADS7950_V_CHAN(3, bits), \
	TI_ADS7950_V_CHAN(4, bits), \
	TI_ADS7950_V_CHAN(5, bits), \
	TI_ADS7950_V_CHAN(6, bits), \
	TI_ADS7950_V_CHAN(7, bits), \
	TI_ADS7950_V_CHAN(8, bits), \
	TI_ADS7950_V_CHAN(9, bits), \
	TI_ADS7950_V_CHAN(10, bits), \
	TI_ADS7950_V_CHAN(11, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(12), \
}

#define DECLARE_TI_ADS7950_16_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	TI_ADS7950_V_CHAN(0, bits), \
	TI_ADS7950_V_CHAN(1, bits), \
	TI_ADS7950_V_CHAN(2, bits), \
	TI_ADS7950_V_CHAN(3, bits), \
	TI_ADS7950_V_CHAN(4, bits), \
	TI_ADS7950_V_CHAN(5, bits), \
	TI_ADS7950_V_CHAN(6, bits), \
	TI_ADS7950_V_CHAN(7, bits), \
	TI_ADS7950_V_CHAN(8, bits), \
	TI_ADS7950_V_CHAN(9, bits), \
	TI_ADS7950_V_CHAN(10, bits), \
	TI_ADS7950_V_CHAN(11, bits), \
	TI_ADS7950_V_CHAN(12, bits), \
	TI_ADS7950_V_CHAN(13, bits), \
	TI_ADS7950_V_CHAN(14, bits), \
	TI_ADS7950_V_CHAN(15, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(16), \
}

static DECLARE_TI_ADS7950_4_CHANNELS(ti_ads7950, 12);
static DECLARE_TI_ADS7950_8_CHANNELS(ti_ads7951, 12);
static DECLARE_TI_ADS7950_12_CHANNELS(ti_ads7952, 12);
static DECLARE_TI_ADS7950_16_CHANNELS(ti_ads7953, 12);
static DECLARE_TI_ADS7950_4_CHANNELS(ti_ads7954, 10);
static DECLARE_TI_ADS7950_8_CHANNELS(ti_ads7955, 10);
static DECLARE_TI_ADS7950_12_CHANNELS(ti_ads7956, 10);
static DECLARE_TI_ADS7950_16_CHANNELS(ti_ads7957, 10);
static DECLARE_TI_ADS7950_4_CHANNELS(ti_ads7958, 8);
static DECLARE_TI_ADS7950_8_CHANNELS(ti_ads7959, 8);
static DECLARE_TI_ADS7950_12_CHANNELS(ti_ads7960, 8);
static DECLARE_TI_ADS7950_16_CHANNELS(ti_ads7961, 8);

static const struct ti_ads7950_chip_info ti_ads7950_chip_info[] = {
	[TI_ADS7950] = {
		.channels	= ti_ads7950_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7950_channels),
	},
	[TI_ADS7951] = {
		.channels	= ti_ads7951_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7951_channels),
	},
	[TI_ADS7952] = {
		.channels	= ti_ads7952_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7952_channels),
	},
	[TI_ADS7953] = {
		.channels	= ti_ads7953_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7953_channels),
	},
	[TI_ADS7954] = {
		.channels	= ti_ads7954_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7954_channels),
	},
	[TI_ADS7955] = {
		.channels	= ti_ads7955_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7955_channels),
	},
	[TI_ADS7956] = {
		.channels	= ti_ads7956_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7956_channels),
	},
	[TI_ADS7957] = {
		.channels	= ti_ads7957_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7957_channels),
	},
	[TI_ADS7958] = {
		.channels	= ti_ads7958_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7958_channels),
	},
	[TI_ADS7959] = {
		.channels	= ti_ads7959_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7959_channels),
	},
	[TI_ADS7960] = {
		.channels	= ti_ads7960_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7960_channels),
	},
	[TI_ADS7961] = {
		.channels	= ti_ads7961_channels,
		.num_channels	= ARRAY_SIZE(ti_ads7961_channels),
	},
};

/*
 * ti_ads7950_update_scan_mode() setup the spi transfer buffer for the new
 * scan mask
 */
static int ti_ads7950_update_scan_mode(struct iio_dev *indio_dev,
				       const unsigned long *active_scan_mask)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int i, cmd, len;

	len = 0;
	for_each_set_bit(i, active_scan_mask, indio_dev->num_channels) {
		cmd = TI_ADS7950_MAN_CMD(TI_ADS7950_CR_CHAN(i));
		st->tx_buf[len++] = cmd;
	}

	/* Data for the 1st channel is not returned until the 3rd transfer */
	st->tx_buf[len++] = 0;
	st->tx_buf[len++] = 0;

	st->ring_xfer.len = len * 2;

	return 0;
}

static irqreturn_t ti_ads7950_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->slock);
	ret = spi_sync(st->spi, &st->ring_msg);
	if (ret < 0)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, &st->rx_buf[2],
					   iio_get_time_ns(indio_dev));

out:
	mutex_unlock(&st->slock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ti_ads7950_scan_direct(struct iio_dev *indio_dev, unsigned int ch)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int ret, cmd;

	mutex_lock(&st->slock);
	cmd = TI_ADS7950_MAN_CMD(TI_ADS7950_CR_CHAN(ch));
	st->single_tx = cmd;

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		goto out;

	ret = st->single_rx;

out:
	mutex_unlock(&st->slock);

	return ret;
}

static int ti_ads7950_get_range(struct ti_ads7950_state *st)
{
	int vref;

	if (st->vref_mv) {
		vref = st->vref_mv;
	} else {
		vref = regulator_get_voltage(st->reg);
		if (vref < 0)
			return vref;

		vref /= 1000;
	}

	if (st->cmd_settings_bitmask & TI_ADS7950_CR_RANGE_5V)
		vref *= 2;

	return vref;
}

static int ti_ads7950_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long m)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ti_ads7950_scan_direct(indio_dev, chan->address);
		if (ret < 0)
			return ret;

		if (chan->address != TI_ADS7950_EXTRACT(ret, 12, 4))
			return -EIO;

		*val = TI_ADS7950_EXTRACT(ret, chan->scan_type.shift,
					  chan->scan_type.realbits);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = ti_ads7950_get_range(st);
		if (ret < 0)
			return ret;

		*val = ret;
		*val2 = (1 << chan->scan_type.realbits) - 1;

		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static const struct iio_info ti_ads7950_info = {
	.read_raw		= &ti_ads7950_read_raw,
	.update_scan_mode	= ti_ads7950_update_scan_mode,
};

static void ti_ads7950_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);

	mutex_lock(&st->slock);

	if (value)
		st->cmd_settings_bitmask |= BIT(offset);
	else
		st->cmd_settings_bitmask &= ~BIT(offset);

	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	spi_sync(st->spi, &st->scan_single_msg);

	mutex_unlock(&st->slock);
}

static int ti_ads7950_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);
	int ret;

	mutex_lock(&st->slock);

	/* If set as output, return the output */
	if (st->gpio_cmd_settings_bitmask & BIT(offset)) {
		ret = st->cmd_settings_bitmask & BIT(offset);
		goto out;
	}

	/* GPIO data bit sets SDO bits 12-15 to GPIO input */
	st->cmd_settings_bitmask |= TI_ADS7950_CR_GPIO_DATA;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		goto out;

	ret = ((st->single_rx >> 12) & BIT(offset)) ? 1 : 0;

	/* Revert back to original settings */
	st->cmd_settings_bitmask &= ~TI_ADS7950_CR_GPIO_DATA;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		goto out;

out:
	mutex_unlock(&st->slock);

	return ret;
}

static int ti_ads7950_get_direction(struct gpio_chip *chip,
				    unsigned int offset)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);

	/* Bitmask is inverted from GPIO framework 0=input/1=output */
	return !(st->gpio_cmd_settings_bitmask & BIT(offset));
}

static int _ti_ads7950_set_direction(struct gpio_chip *chip, int offset,
				     int input)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);
	int ret = 0;

	mutex_lock(&st->slock);

	/* Only change direction if needed */
	if (input && (st->gpio_cmd_settings_bitmask & BIT(offset)))
		st->gpio_cmd_settings_bitmask &= ~BIT(offset);
	else if (!input && !(st->gpio_cmd_settings_bitmask & BIT(offset)))
		st->gpio_cmd_settings_bitmask |= BIT(offset);
	else
		goto out;

	st->single_tx = TI_ADS7950_GPIO_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);

out:
	mutex_unlock(&st->slock);

	return ret;
}

static int ti_ads7950_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	return _ti_ads7950_set_direction(chip, offset, 1);
}

static int ti_ads7950_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	ti_ads7950_set(chip, offset, value);

	return _ti_ads7950_set_direction(chip, offset, 0);
}

static int ti_ads7950_init_hw(struct ti_ads7950_state *st)
{
	int ret = 0;

	mutex_lock(&st->slock);

	/* Settings for Manual/Auto1/Auto2 commands */
	/* Default to 5v ref */
	st->cmd_settings_bitmask = TI_ADS7950_CR_RANGE_5V;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		goto out;

	/* Settings for GPIO command */
	st->gpio_cmd_settings_bitmask = 0x0;
	st->single_tx = TI_ADS7950_GPIO_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);

out:
	mutex_unlock(&st->slock);

	return ret;
}

static int ti_ads7950_probe(struct spi_device *spi)
{
	struct ti_ads7950_state *st;
	struct iio_dev *indio_dev;
	const struct ti_ads7950_chip_info *info;
	int ret;

	spi->bits_per_word = 16;
	spi->mode |= SPI_CS_WORD;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "Error in spi setup\n");
		return ret;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	info = &ti_ads7950_chip_info[spi_get_device_id(spi)->driver_data];

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = info->channels;
	indio_dev->num_channels = info->num_channels;
	indio_dev->info = &ti_ads7950_info;

	/* build spi ring message */
	spi_message_init(&st->ring_msg);

	st->ring_xfer.tx_buf = &st->tx_buf[0];
	st->ring_xfer.rx_buf = &st->rx_buf[0];
	/* len will be set later */

	spi_message_add_tail(&st->ring_xfer, &st->ring_msg);

	/*
	 * Setup default message. The sample is read at the end of the first
	 * transfer, then it takes one full cycle to convert the sample and one
	 * more cycle to send the value. The conversion process is driven by
	 * the SPI clock, which is why we have 3 transfers. The middle one is
	 * just dummy data sent while the chip is converting the sample that
	 * was read at the end of the first transfer.
	 */

	st->scan_single_xfer[0].tx_buf = &st->single_tx;
	st->scan_single_xfer[0].len = 2;
	st->scan_single_xfer[0].cs_change = 1;
	st->scan_single_xfer[1].tx_buf = &st->single_tx;
	st->scan_single_xfer[1].len = 2;
	st->scan_single_xfer[1].cs_change = 1;
	st->scan_single_xfer[2].rx_buf = &st->single_rx;
	st->scan_single_xfer[2].len = 2;

	spi_message_init_with_transfers(&st->scan_single_msg,
					st->scan_single_xfer, 3);

	/* Use hard coded value for reference voltage in ACPI case */
	if (ACPI_COMPANION(&spi->dev))
		st->vref_mv = TI_ADS7950_VA_MV_ACPI_DEFAULT;

	mutex_init(&st->slock);

	st->reg = devm_regulator_get(&spi->dev, "vref");
	if (IS_ERR(st->reg)) {
		dev_err(&spi->dev, "Failed to get regulator \"vref\"\n");
		ret = PTR_ERR(st->reg);
		goto error_destroy_mutex;
	}

	ret = regulator_enable(st->reg);
	if (ret) {
		dev_err(&spi->dev, "Failed to enable regulator \"vref\"\n");
		goto error_destroy_mutex;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 &ti_ads7950_trigger_handler, NULL);
	if (ret) {
		dev_err(&spi->dev, "Failed to setup triggered buffer\n");
		goto error_disable_reg;
	}

	ret = ti_ads7950_init_hw(st);
	if (ret) {
		dev_err(&spi->dev, "Failed to init adc chip\n");
		goto error_cleanup_ring;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&spi->dev, "Failed to register iio device\n");
		goto error_cleanup_ring;
	}

	/* Add GPIO chip */
	st->chip.label = dev_name(&st->spi->dev);
	st->chip.parent = &st->spi->dev;
	st->chip.owner = THIS_MODULE;
	st->chip.can_sleep = true;
	st->chip.base = -1;
	st->chip.ngpio = TI_ADS7950_NUM_GPIOS;
	st->chip.get_direction = ti_ads7950_get_direction;
	st->chip.direction_input = ti_ads7950_direction_input;
	st->chip.direction_output = ti_ads7950_direction_output;
	st->chip.get = ti_ads7950_get;
	st->chip.set = ti_ads7950_set;

	ret = gpiochip_add_data(&st->chip, st);
	if (ret) {
		dev_err(&spi->dev, "Failed to init GPIOs\n");
		goto error_iio_device;
	}

	return 0;

error_iio_device:
	iio_device_unregister(indio_dev);
error_cleanup_ring:
	iio_triggered_buffer_cleanup(indio_dev);
error_disable_reg:
	regulator_disable(st->reg);
error_destroy_mutex:
	mutex_destroy(&st->slock);

	return ret;
}

static int ti_ads7950_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ti_ads7950_state *st = iio_priv(indio_dev);

	gpiochip_remove(&st->chip);
	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	regulator_disable(st->reg);
	mutex_destroy(&st->slock);

	return 0;
}

static const struct spi_device_id ti_ads7950_id[] = {
	{ "ads7950", TI_ADS7950 },
	{ "ads7951", TI_ADS7951 },
	{ "ads7952", TI_ADS7952 },
	{ "ads7953", TI_ADS7953 },
	{ "ads7954", TI_ADS7954 },
	{ "ads7955", TI_ADS7955 },
	{ "ads7956", TI_ADS7956 },
	{ "ads7957", TI_ADS7957 },
	{ "ads7958", TI_ADS7958 },
	{ "ads7959", TI_ADS7959 },
	{ "ads7960", TI_ADS7960 },
	{ "ads7961", TI_ADS7961 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ti_ads7950_id);

static const struct of_device_id ads7950_of_table[] = {
	{ .compatible = "ti,ads7950", .data = &ti_ads7950_chip_info[TI_ADS7950] },
	{ .compatible = "ti,ads7951", .data = &ti_ads7950_chip_info[TI_ADS7951] },
	{ .compatible = "ti,ads7952", .data = &ti_ads7950_chip_info[TI_ADS7952] },
	{ .compatible = "ti,ads7953", .data = &ti_ads7950_chip_info[TI_ADS7953] },
	{ .compatible = "ti,ads7954", .data = &ti_ads7950_chip_info[TI_ADS7954] },
	{ .compatible = "ti,ads7955", .data = &ti_ads7950_chip_info[TI_ADS7955] },
	{ .compatible = "ti,ads7956", .data = &ti_ads7950_chip_info[TI_ADS7956] },
	{ .compatible = "ti,ads7957", .data = &ti_ads7950_chip_info[TI_ADS7957] },
	{ .compatible = "ti,ads7958", .data = &ti_ads7950_chip_info[TI_ADS7958] },
	{ .compatible = "ti,ads7959", .data = &ti_ads7950_chip_info[TI_ADS7959] },
	{ .compatible = "ti,ads7960", .data = &ti_ads7950_chip_info[TI_ADS7960] },
	{ .compatible = "ti,ads7961", .data = &ti_ads7950_chip_info[TI_ADS7961] },
	{ },
};
MODULE_DEVICE_TABLE(of, ads7950_of_table);

static struct spi_driver ti_ads7950_driver = {
	.driver = {
		.name	= "ads7950",
		.of_match_table = ads7950_of_table,
	},
	.probe		= ti_ads7950_probe,
	.remove		= ti_ads7950_remove,
	.id_table	= ti_ads7950_id,
};
module_spi_driver(ti_ads7950_driver);

MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("TI TI_ADS7950 ADC");
MODULE_LICENSE("GPL v2");
