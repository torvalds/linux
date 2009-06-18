/**
 * drivers/gpio/max7301.c
 *
 * Copyright (C) 2006 Juergen Beisert, Pengutronix
 * Copyright (C) 2008 Guennadi Liakhovetski, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The Maxim's MAX7301 device is an SPI driven GPIO expander. There are
 * 28 GPIOs. 8 of them can trigger an interrupt. See datasheet for more
 * details
 * Note:
 * - DIN must be stable at the rising edge of clock.
 * - when writing:
 *   - always clock in 16 clocks at once
 *   - at DIN: D15 first, D0 last
 *   - D0..D7 = databyte, D8..D14 = commandbyte
 *   - D15 = low -> write command
 * - when reading
 *   - always clock in 16 clocks at once
 *   - at DIN: D15 first, D0 last
 *   - D0..D7 = dummy, D8..D14 = register address
 *   - D15 = high -> read command
 *   - raise CS and assert it again
 *   - always clock in 16 clocks at once
 *   - at DOUT: D15 first, D0 last
 *   - D0..D7 contains the data from the first cycle
 *
 * The driver exports a standard gpiochip interface
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/spi/max7301.h>
#include <linux/gpio.h>

#define DRIVER_NAME "max7301"

/*
 * Pin configurations, see MAX7301 datasheet page 6
 */
#define PIN_CONFIG_MASK 0x03
#define PIN_CONFIG_IN_PULLUP 0x03
#define PIN_CONFIG_IN_WO_PULLUP 0x02
#define PIN_CONFIG_OUT 0x01

#define PIN_NUMBER 28


/*
 * Some registers must be read back to modify.
 * To save time we cache them here in memory
 */
struct max7301 {
	struct mutex	lock;
	u8		port_config[8];	/* field 0 is unused */
	u32		out_level;	/* cached output levels */
	struct gpio_chip chip;
	struct spi_device *spi;
};

/**
 * max7301_write - Write a new register content
 * @spi: The SPI device
 * @reg: Register offset
 * @val: Value to write
 *
 * A write to the MAX7301 means one message with one transfer
 *
 * Returns 0 if successful or a negative value on error
 */
static int max7301_write(struct spi_device *spi, unsigned int reg, unsigned int val)
{
	u16 word = ((reg & 0x7F) << 8) | (val & 0xFF);
	return spi_write(spi, (const u8 *)&word, sizeof(word));
}

/**
 * max7301_read - Read back register content
 * @spi: The SPI device
 * @reg: Register offset
 *
 * A read from the MAX7301 means two transfers; here, one message each
 *
 * Returns positive 8 bit value from device if successful or a
 * negative value on error
 */
static int max7301_read(struct spi_device *spi, unsigned int reg)
{
	int ret;
	u16 word;

	word = 0x8000 | (reg << 8);
	ret = spi_write(spi, (const u8 *)&word, sizeof(word));
	if (ret)
		return ret;
	/*
	 * This relies on the fact, that a transfer with NULL tx_buf shifts out
	 * zero bytes (=NOOP for MAX7301)
	 */
	ret = spi_read(spi, (u8 *)&word, sizeof(word));
	if (ret)
		return ret;
	return word & 0xff;
}

static int max7301_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct max7301 *ts = container_of(chip, struct max7301, chip);
	u8 *config;
	int ret;

	/* First 4 pins are unused in the controller */
	offset += 4;

	config = &ts->port_config[offset >> 2];

	mutex_lock(&ts->lock);

	/* Standard GPIO API doesn't support pull-ups, has to be extended.
	 * Hard-coding no pollup for now. */
	*config = (*config & ~(3 << (offset & 3))) | (1 << (offset & 3));

	ret = max7301_write(ts->spi, 0x08 + (offset >> 2), *config);

	mutex_unlock(&ts->lock);

	return ret;
}

static int __max7301_set(struct max7301 *ts, unsigned offset, int value)
{
	if (value) {
		ts->out_level |= 1 << offset;
		return max7301_write(ts->spi, 0x20 + offset, 0x01);
	} else {
		ts->out_level &= ~(1 << offset);
		return max7301_write(ts->spi, 0x20 + offset, 0x00);
	}
}

static int max7301_direction_output(struct gpio_chip *chip, unsigned offset,
				    int value)
{
	struct max7301 *ts = container_of(chip, struct max7301, chip);
	u8 *config;
	int ret;

	/* First 4 pins are unused in the controller */
	offset += 4;

	config = &ts->port_config[offset >> 2];

	mutex_lock(&ts->lock);

	*config = (*config & ~(3 << (offset & 3))) | (1 << (offset & 3));

	ret = __max7301_set(ts, offset, value);

	if (!ret)
		ret = max7301_write(ts->spi, 0x08 + (offset >> 2), *config);

	mutex_unlock(&ts->lock);

	return ret;
}

static int max7301_get(struct gpio_chip *chip, unsigned offset)
{
	struct max7301 *ts = container_of(chip, struct max7301, chip);
	int config, level = -EINVAL;

	/* First 4 pins are unused in the controller */
	offset += 4;

	mutex_lock(&ts->lock);

	config = (ts->port_config[offset >> 2] >> ((offset & 3) * 2)) & 3;

	switch (config) {
	case 1:
		/* Output: return cached level */
		level =  !!(ts->out_level & (1 << offset));
		break;
	case 2:
	case 3:
		/* Input: read out */
		level = max7301_read(ts->spi, 0x20 + offset) & 0x01;
	}
	mutex_unlock(&ts->lock);

	return level;
}

static void max7301_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct max7301 *ts = container_of(chip, struct max7301, chip);

	/* First 4 pins are unused in the controller */
	offset += 4;

	mutex_lock(&ts->lock);

	__max7301_set(ts, offset, value);

	mutex_unlock(&ts->lock);
}

static int __devinit max7301_probe(struct spi_device *spi)
{
	struct max7301 *ts;
	struct max7301_platform_data *pdata;
	int i, ret;

	pdata = spi->dev.platform_data;
	if (!pdata || !pdata->base) {
		dev_dbg(&spi->dev, "incorrect or missing platform data\n");
		return -EINVAL;
	}

	/*
	 * bits_per_word cannot be configured in platform data
	 */
	spi->bits_per_word = 16;

	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ts = kzalloc(sizeof(struct max7301), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	mutex_init(&ts->lock);

	dev_set_drvdata(&spi->dev, ts);

	/* Power up the chip and disable IRQ output */
	max7301_write(spi, 0x04, 0x01);

	ts->spi = spi;

	ts->chip.label = DRIVER_NAME,

	ts->chip.direction_input = max7301_direction_input;
	ts->chip.get = max7301_get;
	ts->chip.direction_output = max7301_direction_output;
	ts->chip.set = max7301_set;

	ts->chip.base = pdata->base;
	ts->chip.ngpio = PIN_NUMBER;
	ts->chip.can_sleep = 1;
	ts->chip.dev = &spi->dev;
	ts->chip.owner = THIS_MODULE;

	/*
	 * tristate all pins in hardware and cache the
	 * register values for later use.
	 */
	for (i = 1; i < 8; i++) {
		int j;
		/* 0xAA means input with internal pullup disabled */
		max7301_write(spi, 0x08 + i, 0xAA);
		ts->port_config[i] = 0xAA;
		for (j = 0; j < 4; j++) {
			int offset = (i - 1) * 4 + j;
			ret = max7301_direction_input(&ts->chip, offset);
			if (ret)
				goto exit_destroy;
		}
	}

	ret = gpiochip_add(&ts->chip);
	if (ret)
		goto exit_destroy;

	return ret;

exit_destroy:
	dev_set_drvdata(&spi->dev, NULL);
	mutex_destroy(&ts->lock);
	kfree(ts);
	return ret;
}

static int __devexit max7301_remove(struct spi_device *spi)
{
	struct max7301 *ts;
	int ret;

	ts = dev_get_drvdata(&spi->dev);
	if (ts == NULL)
		return -ENODEV;

	dev_set_drvdata(&spi->dev, NULL);

	/* Power down the chip and disable IRQ output */
	max7301_write(spi, 0x04, 0x00);

	ret = gpiochip_remove(&ts->chip);
	if (!ret) {
		mutex_destroy(&ts->lock);
		kfree(ts);
	} else
		dev_err(&spi->dev, "Failed to remove the GPIO controller: %d\n",
			ret);

	return ret;
}

static struct spi_driver max7301_driver = {
	.driver = {
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
	.probe		= max7301_probe,
	.remove		= __devexit_p(max7301_remove),
};

static int __init max7301_init(void)
{
	return spi_register_driver(&max7301_driver);
}
/* register after spi postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(max7301_init);

static void __exit max7301_exit(void)
{
	spi_unregister_driver(&max7301_driver);
}
module_exit(max7301_exit);

MODULE_AUTHOR("Juergen Beisert");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MAX7301 SPI based GPIO-Expander");
