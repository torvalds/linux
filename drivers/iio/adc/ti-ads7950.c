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
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 */
	u16 rx_buf[TI_ADS7950_MAX_CHAN + 2] __aligned(IIO_DMA_MINALIGN);
	u16 tx_buf[TI_ADS7950_MAX_CHAN + 2];
	u16 single_tx;
	u16 single_rx;

};

struct ti_ads7950_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
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

static const struct ti_ads7950_chip_info ti_ads7950_chip_info = {
	.channels	= ti_ads7950_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7950_channels),
};

static const struct ti_ads7950_chip_info ti_ads7951_chip_info = {
	.channels	= ti_ads7951_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7951_channels),
};

static const struct ti_ads7950_chip_info ti_ads7952_chip_info = {
	.channels	= ti_ads7952_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7952_channels),
};

static const struct ti_ads7950_chip_info ti_ads7953_chip_info = {
	.channels	= ti_ads7953_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7953_channels),
};

static const struct ti_ads7950_chip_info ti_ads7954_chip_info = {
	.channels	= ti_ads7954_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7954_channels),
};

static const struct ti_ads7950_chip_info ti_ads7955_chip_info = {
	.channels	= ti_ads7955_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7955_channels),
};

static const struct ti_ads7950_chip_info ti_ads7956_chip_info = {
	.channels	= ti_ads7956_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7956_channels),
};

static const struct ti_ads7950_chip_info ti_ads7957_chip_info = {
	.channels	= ti_ads7957_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7957_channels),
};

static const struct ti_ads7950_chip_info ti_ads7958_chip_info = {
	.channels	= ti_ads7958_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7958_channels),
};

static const struct ti_ads7950_chip_info ti_ads7959_chip_info = {
	.channels	= ti_ads7959_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7959_channels),
};

static const struct ti_ads7950_chip_info ti_ads7960_chip_info = {
	.channels	= ti_ads7960_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7960_channels),
};

static const struct ti_ads7950_chip_info ti_ads7961_chip_info = {
	.channels	= ti_ads7961_channels,
	.num_channels	= ARRAY_SIZE(ti_ads7961_channels),
};

static irqreturn_t ti_ads7950_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int ret;

	do {
		guard(mutex)(&st->slock);

		ret = spi_sync(st->spi, &st->ring_msg);
		if (ret)
			break;

		iio_push_to_buffers_with_ts_unaligned(indio_dev, &st->rx_buf[2],
						      sizeof(*st->rx_buf) *
						      TI_ADS7950_MAX_CHAN,
						      iio_get_time_ns(indio_dev));
	} while (0);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ti_ads7950_scan_direct(struct iio_dev *indio_dev, unsigned int ch)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	int ret, cmd;

	guard(mutex)(&st->slock);

	cmd = TI_ADS7950_MAN_CMD(TI_ADS7950_CR_CHAN(ch));
	st->single_tx = cmd;

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return st->single_rx;
}

static unsigned int ti_ads7950_get_range(struct ti_ads7950_state *st)
{
	unsigned int vref = st->vref_mv;

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
		*val = ti_ads7950_get_range(st);
		*val2 = (1 << chan->scan_type.realbits) - 1;

		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static const struct iio_info ti_ads7950_info = {
	.read_raw = &ti_ads7950_read_raw,
};

static int ti_ads7950_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);
	u32 len = 0;
	u32 i;
	u16 cmd;

	for_each_set_bit(i, indio_dev->active_scan_mask, indio_dev->num_channels) {
		cmd = TI_ADS7950_MAN_CMD(TI_ADS7950_CR_CHAN(i));
		st->tx_buf[len++] = cmd;
	}

	/* Data for the 1st channel is not returned until the 3rd transfer */
	st->tx_buf[len++] = 0;
	st->tx_buf[len++] = 0;

	st->ring_xfer.len = len * 2;

	return spi_optimize_message(st->spi, &st->ring_msg);
}

static int ti_ads7950_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ti_ads7950_state *st = iio_priv(indio_dev);

	spi_unoptimize_message(&st->ring_msg);

	return 0;
}

static const struct iio_buffer_setup_ops ti_ads7950_buffer_setup_ops = {
	.preenable = ti_ads7950_buffer_preenable,
	.postdisable = ti_ads7950_buffer_postdisable,
};

static int ti_ads7950_set(struct gpio_chip *chip, unsigned int offset,
			  int value)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);

	guard(mutex)(&st->slock);

	if (value)
		st->cmd_settings_bitmask |= BIT(offset);
	else
		st->cmd_settings_bitmask &= ~BIT(offset);

	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);

	return spi_sync(st->spi, &st->scan_single_msg);
}

static int ti_ads7950_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_ads7950_state *st = gpiochip_get_data(chip);
	bool state;
	int ret;

	guard(mutex)(&st->slock);

	/* If set as output, return the output */
	if (st->gpio_cmd_settings_bitmask & BIT(offset)) {
		state = st->cmd_settings_bitmask & BIT(offset);
		return state;
	}

	/* GPIO data bit sets SDO bits 12-15 to GPIO input */
	st->cmd_settings_bitmask |= TI_ADS7950_CR_GPIO_DATA;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	state = (st->single_rx >> 12) & BIT(offset);

	/* Revert back to original settings */
	st->cmd_settings_bitmask &= ~TI_ADS7950_CR_GPIO_DATA;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return state;
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

	guard(mutex)(&st->slock);

	/* Only change direction if needed */
	if (input && (st->gpio_cmd_settings_bitmask & BIT(offset)))
		st->gpio_cmd_settings_bitmask &= ~BIT(offset);
	else if (!input && !(st->gpio_cmd_settings_bitmask & BIT(offset)))
		st->gpio_cmd_settings_bitmask |= BIT(offset);
	else
		return 0;

	st->single_tx = TI_ADS7950_GPIO_CMD_SETTINGS(st);

	return spi_sync(st->spi, &st->scan_single_msg);
}

static int ti_ads7950_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	return _ti_ads7950_set_direction(chip, offset, 1);
}

static int ti_ads7950_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	int ret;

	ret = ti_ads7950_set(chip, offset, value);
	if (ret)
		return ret;

	return _ti_ads7950_set_direction(chip, offset, 0);
}

static int ti_ads7950_init_hw(struct ti_ads7950_state *st)
{
	int ret;

	guard(mutex)(&st->slock);

	/* Settings for Manual/Auto1/Auto2 commands */
	/* Default to 5v ref */
	st->cmd_settings_bitmask = TI_ADS7950_CR_RANGE_5V;
	st->single_tx = TI_ADS7950_MAN_CMD_SETTINGS(st);
	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	/* Settings for GPIO command */
	st->gpio_cmd_settings_bitmask = 0x0;
	st->single_tx = TI_ADS7950_GPIO_CMD_SETTINGS(st);
	return spi_sync(st->spi, &st->scan_single_msg);
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
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Error in spi setup\n");

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->spi = spi;

	info = spi_get_device_match_data(spi);

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

	ret = devm_mutex_init(&spi->dev, &st->slock);
	if (ret)
		return ret;

	/* Use hard coded value for reference voltage in ACPI case */
	if (ACPI_COMPANION(&spi->dev)) {
		st->vref_mv = TI_ADS7950_VA_MV_ACPI_DEFAULT;
	} else {
		ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
		if (ret < 0)
			return dev_err_probe(&spi->dev, ret,
					     "Failed to get regulator \"vref\"\n");

		st->vref_mv = ret / 1000;
	}

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      &ti_ads7950_trigger_handler,
					      &ti_ads7950_buffer_setup_ops);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to setup triggered buffer\n");

	ret = ti_ads7950_init_hw(st);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to init adc chip\n");

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to register iio device\n");

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

	ret = devm_gpiochip_add_data(&spi->dev, &st->chip, st);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "Failed to init GPIOs\n");

	return 0;
}

static const struct spi_device_id ti_ads7950_id[] = {
	{ "ads7950", (kernel_ulong_t)&ti_ads7950_chip_info },
	{ "ads7951", (kernel_ulong_t)&ti_ads7951_chip_info },
	{ "ads7952", (kernel_ulong_t)&ti_ads7952_chip_info },
	{ "ads7953", (kernel_ulong_t)&ti_ads7953_chip_info },
	{ "ads7954", (kernel_ulong_t)&ti_ads7954_chip_info },
	{ "ads7955", (kernel_ulong_t)&ti_ads7955_chip_info },
	{ "ads7956", (kernel_ulong_t)&ti_ads7956_chip_info },
	{ "ads7957", (kernel_ulong_t)&ti_ads7957_chip_info },
	{ "ads7958", (kernel_ulong_t)&ti_ads7958_chip_info },
	{ "ads7959", (kernel_ulong_t)&ti_ads7959_chip_info },
	{ "ads7960", (kernel_ulong_t)&ti_ads7960_chip_info },
	{ "ads7961", (kernel_ulong_t)&ti_ads7961_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ti_ads7950_id);

static const struct of_device_id ads7950_of_table[] = {
	{ .compatible = "ti,ads7950", .data = &ti_ads7950_chip_info },
	{ .compatible = "ti,ads7951", .data = &ti_ads7951_chip_info },
	{ .compatible = "ti,ads7952", .data = &ti_ads7952_chip_info },
	{ .compatible = "ti,ads7953", .data = &ti_ads7953_chip_info },
	{ .compatible = "ti,ads7954", .data = &ti_ads7954_chip_info },
	{ .compatible = "ti,ads7955", .data = &ti_ads7955_chip_info },
	{ .compatible = "ti,ads7956", .data = &ti_ads7956_chip_info },
	{ .compatible = "ti,ads7957", .data = &ti_ads7957_chip_info },
	{ .compatible = "ti,ads7958", .data = &ti_ads7958_chip_info },
	{ .compatible = "ti,ads7959", .data = &ti_ads7959_chip_info },
	{ .compatible = "ti,ads7960", .data = &ti_ads7960_chip_info },
	{ .compatible = "ti,ads7961", .data = &ti_ads7961_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ads7950_of_table);

static struct spi_driver ti_ads7950_driver = {
	.driver = {
		.name	= "ads7950",
		.of_match_table = ads7950_of_table,
	},
	.probe		= ti_ads7950_probe,
	.id_table	= ti_ads7950_id,
};
module_spi_driver(ti_ads7950_driver);

MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_DESCRIPTION("TI TI_ADS7950 ADC");
MODULE_LICENSE("GPL v2");
