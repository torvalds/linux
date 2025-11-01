// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM ADC driver for BD79112 signal monitoring hub.
 * Copyright (C) 2025, ROHM Semiconductor.
 *
 * SPI communication derived from ad7923.c and ti-ads7950.c
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#include <linux/iio/adc-helpers.h>
#include <linux/iio/iio.h>

#define BD79112_MAX_NUM_CHANNELS 32

struct bd79112_data {
	struct spi_device *spi;
	struct regmap *map;
	struct device *dev;
	struct gpio_chip gc;
	unsigned long gpio_valid_mask;
	unsigned int vref_mv;
	struct spi_transfer read_xfer[2];
	struct spi_transfer write_xfer;
	struct spi_message read_msg;
	struct spi_message write_msg;
	/* 16-bit TX, valid data in high byte */
	u8 read_tx[2] __aligned(IIO_DMA_MINALIGN);
	/* 8-bit address followed by 8-bit data */
	u8 reg_write_tx[2];
	/* 12-bit of ADC data or 8 bit of reg data */
	__be16 read_rx;
};

/*
 * The ADC data is read issuing SPI-command matching the channel number.
 * We treat this as a register address.
 */
#define BD79112_REG_AGIO0A		0x00
#define BD79112_REG_AGIO15B		0x1f

/*
 * ADC STATUS_FLAG appended to ADC data will be set, if the ADC result is being
 * read for a channel, which input pin is muxed to be a GPIO.
 */
#define BD79112_ADC_STATUS_FLAG BIT(14)

/*
 * The BD79112 requires "R/W bit" to be set for SPI register (not ADC data)
 * reads and an "IOSET bit" to be set for read/write operations (which aren't
 * reading the ADC data).
 */
#define BD79112_BIT_RW			BIT(4)
#define BD79112_BIT_IO			BIT(5)

#define BD79112_REG_GPI_VALUE_B8_15	(BD79112_BIT_IO | 0x0)
#define BD79112_REG_GPI_VALUE_B0_B7	(BD79112_BIT_IO | 0x1)
#define BD79112_REG_GPI_VALUE_A8_15	(BD79112_BIT_IO | 0x2)
#define BD79112_REG_GPI_VALUE_A0_A7	(BD79112_BIT_IO | 0x3)

#define BD79112_REG_GPI_EN_B7_B15	(BD79112_BIT_IO | 0x4)
#define BD79112_REG_GPI_EN_B0_B7	(BD79112_BIT_IO | 0x5)
#define BD79112_REG_GPI_EN_A8_A15	(BD79112_BIT_IO | 0x6)
#define BD79112_REG_GPI_EN_A0_A7	(BD79112_BIT_IO | 0x7)

#define BD79112_REG_GPO_EN_B7_B15	(BD79112_BIT_IO | 0x8)
#define BD79112_REG_GPO_EN_B0_B7	(BD79112_BIT_IO | 0x9)
#define BD79112_REG_GPO_EN_A8_A15	(BD79112_BIT_IO | 0xa)
#define BD79112_REG_GPO_EN_A0_A7	(BD79112_BIT_IO | 0xb)

#define BD79112_NUM_GPIO_EN_REGS	8
#define BD79112_FIRST_GPIO_EN_REG	BD79112_REG_GPI_EN_B7_B15

#define BD79112_REG_GPO_VALUE_B8_15	(BD79112_BIT_IO | 0xc)
#define BD79112_REG_GPO_VALUE_B0_B7	(BD79112_BIT_IO | 0xd)
#define BD79112_REG_GPO_VALUE_A8_15	(BD79112_BIT_IO | 0xe)
#define BD79112_REG_GPO_VALUE_A0_A7	(BD79112_BIT_IO | 0xf)

#define BD79112_REG_MAX BD79112_REG_GPO_VALUE_A0_A7

/*
 * Read transaction consists of two 16-bit sequences separated by CSB.
 * For register read, 'IOSET' bit must be set. For ADC read, IOSET is cleared
 * and ADDR equals the channel number (0 ... 31).
 *
 * First 16-bit sequence, MOSI as below, MISO data ignored:
 * - SCK: | 1 | 2 |   3   |    4   | 5 .. 8 | 9 .. 16 |
 * - MOSI:| 0 | 0 | IOSET | RW (1) |  ADDR  |  8'b0   |
 *
 * CSB released and re-acquired between these sequences
 *
 * Second 16-bit sequence, MISO as below, MOSI data ignored:
 *   For Register read data is 8 bits:
 *   - SCK: | 1 .. 8 |   9 .. 16   |
 *   - MISO:|  8'b0  | 8-bit data  |
 *
 *   For ADC read data is 12 bits:
 *   - SCK: | 1 |      2      | 3  4 |   4 .. 16   |
 *   - MISO:| 0 | STATUS_FLAG | 2'b0 | 12-bit data |
 *     The 'STATUS_FLAG' is set if the read input pin was configured as a GPIO.
 */
static int bd79112_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct bd79112_data *data = context;
	int ret;

	if (reg & BD79112_BIT_IO)
		reg |= BD79112_BIT_RW;

	data->read_tx[0] = reg;

	ret = spi_sync(data->spi, &data->read_msg);
	if (!ret)
		*val = be16_to_cpu(data->read_rx);

	return ret;
}

/*
 * Write, single 16-bit sequence (broken down below):
 *
 * First 8-bit, MOSI as below, MISO data ignored:
 * - SCK: | 1 | 2 | 3   | 4     | 5 .. 8 |
 * - MOSI:| 0 | 0 |IOSET| RW(0) | ADDR   |
 *
 * Last 8 SCK cycles (b8 ... b15), MISO contains register data, MOSI ignored.
 * - SCK: | 9 .. 16 |
 * - MISO:|  data   |
 */
static int bd79112_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct bd79112_data *data = context;

	data->reg_write_tx[0] = reg;
	data->reg_write_tx[1] = val;

	return spi_sync(data->spi, &data->write_msg);
}

static int _get_gpio_reg(unsigned int offset, unsigned int base)
{
	int regoffset = offset / 8;

	if (offset > 31)
		return -EINVAL;

	return base - regoffset;
}

#define GET_GPIO_BIT(offset) BIT((offset) % 8)
#define GET_GPO_EN_REG(offset)  _get_gpio_reg((offset), BD79112_REG_GPO_EN_A0_A7)
#define GET_GPI_EN_REG(offset)  _get_gpio_reg((offset), BD79112_REG_GPI_EN_A0_A7)
#define GET_GPO_VAL_REG(offset)  _get_gpio_reg((offset), BD79112_REG_GPO_VALUE_A0_A7)
#define GET_GPI_VAL_REG(offset)  _get_gpio_reg((offset), BD79112_REG_GPI_VALUE_A0_A7)

static const struct regmap_range bd71815_volatile_ro_ranges[] = {
	{
		/* Read ADC data */
		.range_min = BD79112_REG_AGIO0A,
		.range_max = BD79112_REG_AGIO15B,
	}, {
		/* GPI state */
		.range_min = BD79112_REG_GPI_VALUE_B8_15,
		.range_max = BD79112_REG_GPI_VALUE_A0_A7,
	},
};

static const struct regmap_access_table bd79112_volatile_regs = {
	.yes_ranges = &bd71815_volatile_ro_ranges[0],
	.n_yes_ranges = ARRAY_SIZE(bd71815_volatile_ro_ranges),
};

static const struct regmap_access_table bd79112_ro_regs = {
	.no_ranges = &bd71815_volatile_ro_ranges[0],
	.n_no_ranges = ARRAY_SIZE(bd71815_volatile_ro_ranges),
};

static const struct regmap_config bd79112_regmap = {
	.reg_read = bd79112_reg_read,
	.reg_write = bd79112_reg_write,
	.volatile_table = &bd79112_volatile_regs,
	.wr_table = &bd79112_ro_regs,
	.cache_type = REGCACHE_MAPLE,
	.max_register = BD79112_REG_MAX,
};

static int bd79112_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long m)
{
	struct bd79112_data *data = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(data->map, chan->channel, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = data->vref_mv;
		*val2 = 12;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bd79112_info = {
	.read_raw = bd79112_read_raw,
};

static const struct iio_chan_spec bd79112_chan_template = {
	.type = IIO_VOLTAGE,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	.indexed = 1,
};

static int bd79112_gpio_init_valid_mask(struct gpio_chip *gc,
					unsigned long *valid_mask,
					unsigned int ngpios)
{
	struct bd79112_data *data = gpiochip_get_data(gc);

	*valid_mask = data->gpio_valid_mask;

	return 0;
}

static int bd79112_gpio_dir_get(struct gpio_chip *gc, unsigned int offset)
{
	struct bd79112_data *data = gpiochip_get_data(gc);
	unsigned int reg, bit, val;
	int ret;

	bit = GET_GPIO_BIT(offset);
	reg = GET_GPO_EN_REG(offset);

	ret = regmap_read(data->map, reg, &val);
	if (ret)
		return ret;

	if (bit & val)
		return GPIO_LINE_DIRECTION_OUT;

	reg = GET_GPI_EN_REG(offset);
	ret = regmap_read(data->map, reg, &val);
	if (ret)
		return ret;

	if (bit & val)
		return GPIO_LINE_DIRECTION_IN;

	/*
	 * Ouch. Seems the pin is ADC input - shouldn't happen as changing mux
	 * at runtime is not supported and non GPIO pins should be invalidated
	 * by the valid_mask at probe. Maybe someone wrote a register bypassing
	 * the driver?
	 */
	dev_err(data->dev, "Pin not a GPIO\n");

	return -EINVAL;
}

static int bd79112_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct bd79112_data *data = gpiochip_get_data(gc);
	unsigned int reg, bit, val;
	int ret;

	bit = GET_GPIO_BIT(offset);
	reg = GET_GPI_VAL_REG(offset);

	ret = regmap_read(data->map, reg, &val);
	if (ret)
		return ret;

	return !!(val & bit);
}

static int bd79112_gpio_set(struct gpio_chip *gc, unsigned int offset,
			    int value)
{
	struct bd79112_data *data = gpiochip_get_data(gc);
	unsigned int reg, bit;

	bit = GET_GPIO_BIT(offset);
	reg = GET_GPO_VAL_REG(offset);

	return regmap_assign_bits(data->map, reg, bit, value);
}

static int bd79112_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				     unsigned long *bits)
{
	struct bd79112_data *data = gpiochip_get_data(gc);
	unsigned long i, bank_mask;

	for_each_set_clump8(i, bank_mask, mask, gc->ngpio) {
		unsigned long bank_bits;
		unsigned int reg;
		int ret;

		bank_bits = bitmap_get_value8(bits, i);
		reg = BD79112_REG_GPO_VALUE_A0_A7 - i / 8;
		ret = regmap_update_bits(data->map, reg, bank_mask, bank_bits);
		if (ret)
			return ret;
	}

	return 0;
}

static int bd79112_gpio_dir_set(struct bd79112_data *data, unsigned int offset,
				int dir)
{
	unsigned int gpi_reg, gpo_reg, bit;
	int ret;

	bit = GET_GPIO_BIT(offset);
	gpi_reg = GET_GPI_EN_REG(offset);
	gpo_reg =  GET_GPO_EN_REG(offset);

	if (dir == GPIO_LINE_DIRECTION_OUT) {
		ret = regmap_clear_bits(data->map, gpi_reg, bit);
		if (ret)
			return ret;

		return regmap_set_bits(data->map, gpo_reg, bit);
	}

	ret = regmap_set_bits(data->map, gpi_reg, bit);
	if (ret)
		return ret;

	return regmap_clear_bits(data->map, gpo_reg, bit);
}

static int bd79112_gpio_input(struct gpio_chip *gc, unsigned int offset)
{
	struct bd79112_data *data = gpiochip_get_data(gc);

	return bd79112_gpio_dir_set(data, offset, GPIO_LINE_DIRECTION_IN);
}

static int bd79112_gpio_output(struct gpio_chip *gc, unsigned int offset,
			       int value)
{
	struct bd79112_data *data = gpiochip_get_data(gc);
	int ret;

	ret = bd79112_gpio_set(gc, offset, value);
	if (ret)
		return ret;

	return bd79112_gpio_dir_set(data, offset, GPIO_LINE_DIRECTION_OUT);
}

static const struct gpio_chip bd79112_gpio_chip = {
	.label			= "bd79112-gpio",
	.get_direction		= bd79112_gpio_dir_get,
	.direction_input	= bd79112_gpio_input,
	.direction_output	= bd79112_gpio_output,
	.get			= bd79112_gpio_get,
	.set			= bd79112_gpio_set,
	.set_multiple		= bd79112_gpio_set_multiple,
	.init_valid_mask	= bd79112_gpio_init_valid_mask,
	.can_sleep		= true,
	.ngpio			= 32,
	.base			= -1,
};

static unsigned int bd79112_get_gpio_pins(const struct iio_chan_spec *cs, int num_channels)
{
	unsigned int i, gpio_channels;

	/*
	 * Let's initialize the mux config to say that all 32 channels are
	 * GPIOs. Then we can just loop through the iio_chan_spec and clear the
	 * bits for found ADC channels.
	 */
	gpio_channels = GENMASK(31, 0);
	for (i = 0; i < num_channels; i++)
		gpio_channels &= ~BIT(cs[i].channel);

	return gpio_channels;
}

/* ADC channels as named in the data-sheet */
static const char * const bd79112_chan_names[] = {
	"AGIO0A", "AGIO1A", "AGIO2A", "AGIO3A",		/* 0 - 3 */
	"AGIO4A", "AGIO5A", "AGIO6A", "AGIO7A",		/* 4 - 7 */
	"AGIO8A", "AGIO9A", "AGIO10A", "AGIO11A",	/* 8 - 11 */
	"AGIO12A", "AGIO13A", "AGIO14A", "AGIO15A",	/* 12 - 15 */
	"AGIO0B", "AGIO1B", "AGIO2B", "AGIO3B",		/* 16 - 19 */
	"AGIO4B", "AGIO5B", "AGIO6B", "AGIO7B",		/* 20 - 23 */
	"AGIO8B", "AGIO9B", "AGIO10B", "AGIO11B",	/* 24 - 27 */
	"AGIO12B", "AGIO13B", "AGIO14B", "AGIO15B",	/* 28 - 31 */
};

static int bd79112_probe(struct spi_device *spi)
{
	struct bd79112_data *data;
	struct iio_dev *iio_dev;
	struct iio_chan_spec *cs;
	struct device *dev = &spi->dev;
	unsigned long gpio_pins, pin;
	unsigned int i;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!iio_dev)
		return -ENOMEM;

	data = iio_priv(iio_dev);
	data->spi = spi;
	data->dev = dev;
	data->map = devm_regmap_init(dev, NULL, data, &bd79112_regmap);
	if (IS_ERR(data->map))
		return dev_err_probe(dev, PTR_ERR(data->map),
				     "Failed to initialize Regmap\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "vdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get the Vdd\n");

	data->vref_mv = ret / 1000;

	ret = devm_regulator_get_enable(dev, "iovdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable I/O voltage\n");

	data->read_xfer[0].tx_buf = &data->read_tx[0];
	data->read_xfer[0].len = sizeof(data->read_tx);
	data->read_xfer[0].cs_change = 1;
	data->read_xfer[1].rx_buf = &data->read_rx;
	data->read_xfer[1].len = sizeof(data->read_rx);
	spi_message_init_with_transfers(&data->read_msg, data->read_xfer, 2);
	ret = devm_spi_optimize_message(dev, spi, &data->read_msg);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to optimize SPI read message\n");

	data->write_xfer.tx_buf = &data->reg_write_tx[0];
	data->write_xfer.len = sizeof(data->reg_write_tx);
	spi_message_init_with_transfers(&data->write_msg, &data->write_xfer, 1);
	ret = devm_spi_optimize_message(dev, spi, &data->write_msg);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to optimize SPI write message\n");

	ret = devm_iio_adc_device_alloc_chaninfo_se(dev, &bd79112_chan_template,
						    BD79112_MAX_NUM_CHANNELS - 1,
						    &cs);

	/* Register all pins as GPIOs if there are no ADC channels */
	if (ret == -ENOENT)
		goto register_gpios;

	if (ret < 0)
		return ret;

	iio_dev->num_channels = ret;
	iio_dev->channels = cs;

	for (i = 0; i < iio_dev->num_channels; i++)
		cs[i].datasheet_name = bd79112_chan_names[cs[i].channel];

	iio_dev->info = &bd79112_info;
	iio_dev->name = "bd79112";
	iio_dev->modes = INDIO_DIRECT_MODE;

	/*
	 * Ensure all channels are ADCs. This allows us to register the IIO
	 * device early (before checking which pins are to be used for GPIO)
	 * without having to worry about some pins being initially used for
	 * GPIO.
	 */
	for (i = 0; i < BD79112_NUM_GPIO_EN_REGS; i++) {
		ret = regmap_write(data->map, BD79112_FIRST_GPIO_EN_REG + i, 0);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to initialize channels\n");
	}

	ret = devm_iio_device_register(data->dev, iio_dev);
	if (ret)
		return dev_err_probe(data->dev, ret, "Failed to register ADC\n");

register_gpios:
	gpio_pins = bd79112_get_gpio_pins(iio_dev->channels,
					  iio_dev->num_channels);

	/* If all channels are reserved for ADC, then we're done. */
	if (!gpio_pins)
		return 0;

	/* Default all the GPIO pins to GPI */
	for_each_set_bit(pin, &gpio_pins, BD79112_MAX_NUM_CHANNELS) {
		ret = bd79112_gpio_dir_set(data, pin, GPIO_LINE_DIRECTION_IN);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to mark pin as GPI\n");
	}

	data->gpio_valid_mask = gpio_pins;
	data->gc = bd79112_gpio_chip;
	data->gc.parent = dev;

	return devm_gpiochip_add_data(dev, &data->gc, data);
}

static const struct of_device_id bd79112_of_match[] = {
	{ .compatible = "rohm,bd79112" },
	{ }
};
MODULE_DEVICE_TABLE(of, bd79112_of_match);

static const struct spi_device_id bd79112_id[] = {
	{ "bd79112" },
	{ }
};
MODULE_DEVICE_TABLE(spi, bd79112_id);

static struct spi_driver bd79112_driver = {
	.driver = {
		.name = "bd79112",
		.of_match_table = bd79112_of_match,
	},
	.probe = bd79112_probe,
	.id_table = bd79112_id,
};
module_spi_driver(bd79112_driver);

MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("Driver for ROHM BD79112 ADC/GPIO");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");
