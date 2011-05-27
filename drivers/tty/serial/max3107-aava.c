/*
 *  max3107.c - spi uart protocol driver for Maxim 3107
 *  Based on max3100.c
 *	by Christian Pellegrin <chripell@evolware.org>
 *  and	max3110.c
 *	by Feng Tang <feng.tang@intel.com>
 *
 *  Copyright (C) Aavamobile 2009
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/spi/spi.h>
#include <linux/freezer.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/sfi.h>
#include <linux/module.h>
#include <asm/mrst.h>
#include "max3107.h"

/* GPIO direction to input function */
static int max3107_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct max3107_port *s = container_of(chip, struct max3107_port, chip);
	u16 buf[1];		/* Buffer for SPI transfer */

	if (offset >= MAX3107_GPIO_COUNT) {
		dev_err(&s->spi->dev, "Invalid GPIO\n");
		return -EINVAL;
	}

	/* Read current GPIO configuration register */
	buf[0] = MAX3107_GPIOCFG_REG;
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, (u8 *)buf, 2)) {
		dev_err(&s->spi->dev, "SPI transfer GPIO read failed\n");
		return -EIO;
	}
	buf[0] &= MAX3107_SPI_RX_DATA_MASK;

	/* Set GPIO to input */
	buf[0] &= ~(0x0001 << offset);

	/* Write new GPIO configuration register value */
	buf[0] |= (MAX3107_WRITE_BIT | MAX3107_GPIOCFG_REG);
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, NULL, 2)) {
		dev_err(&s->spi->dev, "SPI transfer GPIO write failed\n");
		return -EIO;
	}
	return 0;
}

/* GPIO direction to output function */
static int max3107_gpio_direction_out(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct max3107_port *s = container_of(chip, struct max3107_port, chip);
	u16 buf[2];	/* Buffer for SPI transfers */

	if (offset >= MAX3107_GPIO_COUNT) {
		dev_err(&s->spi->dev, "Invalid GPIO\n");
		return -EINVAL;
	}

	/* Read current GPIO configuration and data registers */
	buf[0] = MAX3107_GPIOCFG_REG;
	buf[1] = MAX3107_GPIODATA_REG;
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, (u8 *)buf, 4)) {
		dev_err(&s->spi->dev, "SPI transfer gpio failed\n");
		return -EIO;
	}
	buf[0] &= MAX3107_SPI_RX_DATA_MASK;
	buf[1] &= MAX3107_SPI_RX_DATA_MASK;

	/* Set GPIO to output */
	buf[0] |= (0x0001 << offset);
	/* Set value */
	if (value)
		buf[1] |= (0x0001 << offset);
	else
		buf[1] &= ~(0x0001 << offset);

	/* Write new GPIO configuration and data register values */
	buf[0] |= (MAX3107_WRITE_BIT | MAX3107_GPIOCFG_REG);
	buf[1] |= (MAX3107_WRITE_BIT | MAX3107_GPIODATA_REG);
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, NULL, 4)) {
		dev_err(&s->spi->dev,
			"SPI transfer for GPIO conf data w failed\n");
		return -EIO;
	}
	return 0;
}

/* GPIO value query function */
static int max3107_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct max3107_port *s = container_of(chip, struct max3107_port, chip);
	u16 buf[1];	/* Buffer for SPI transfer */

	if (offset >= MAX3107_GPIO_COUNT) {
		dev_err(&s->spi->dev, "Invalid GPIO\n");
		return -EINVAL;
	}

	/* Read current GPIO data register */
	buf[0] = MAX3107_GPIODATA_REG;
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, (u8 *)buf, 2)) {
		dev_err(&s->spi->dev, "SPI transfer GPIO data r failed\n");
		return -EIO;
	}
	buf[0] &= MAX3107_SPI_RX_DATA_MASK;

	/* Return value */
	return buf[0] & (0x0001 << offset);
}

/* GPIO value set function */
static void max3107_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct max3107_port *s = container_of(chip, struct max3107_port, chip);
	u16 buf[2];	/* Buffer for SPI transfers */

	if (offset >= MAX3107_GPIO_COUNT) {
		dev_err(&s->spi->dev, "Invalid GPIO\n");
		return;
	}

	/* Read current GPIO configuration registers*/
	buf[0] = MAX3107_GPIODATA_REG;
	buf[1] = MAX3107_GPIOCFG_REG;
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, (u8 *)buf, 4)) {
		dev_err(&s->spi->dev,
			"SPI transfer for GPIO data and config read failed\n");
		return;
	}
	buf[0] &= MAX3107_SPI_RX_DATA_MASK;
	buf[1] &= MAX3107_SPI_RX_DATA_MASK;

	if (!(buf[1] & (0x0001 << offset))) {
		/* Configured as input, can't set value */
		dev_warn(&s->spi->dev,
				"Trying to set value for input GPIO\n");
		return;
	}

	/* Set value */
	if (value)
		buf[0] |= (0x0001 << offset);
	else
		buf[0] &= ~(0x0001 << offset);

	/* Write new GPIO data register value */
	buf[0] |= (MAX3107_WRITE_BIT | MAX3107_GPIODATA_REG);
	/* Perform SPI transfer */
	if (max3107_rw(s, (u8 *)buf, NULL, 2))
		dev_err(&s->spi->dev, "SPI transfer GPIO data w failed\n");
}

/* GPIO chip data */
static struct gpio_chip max3107_gpio_chip = {
	.owner			= THIS_MODULE,
	.direction_input	= max3107_gpio_direction_in,
	.direction_output	= max3107_gpio_direction_out,
	.get			= max3107_gpio_get,
	.set			= max3107_gpio_set,
	.can_sleep		= 1,
	.base			= MAX3107_GPIO_BASE,
	.ngpio			= MAX3107_GPIO_COUNT,
};

/**
 *	max3107_aava_reset	-	reset on AAVA systems
 *	@spi: The SPI device we are probing
 *
 *	Reset the device ready for probing.
 */

static int max3107_aava_reset(struct spi_device *spi)
{
	/* Reset the chip */
	if (gpio_request(MAX3107_RESET_GPIO, "max3107")) {
		pr_err("Requesting RESET GPIO failed\n");
		return -EIO;
	}
	if (gpio_direction_output(MAX3107_RESET_GPIO, 0)) {
		pr_err("Setting RESET GPIO to 0 failed\n");
		gpio_free(MAX3107_RESET_GPIO);
		return -EIO;
	}
	msleep(MAX3107_RESET_DELAY);
	if (gpio_direction_output(MAX3107_RESET_GPIO, 1)) {
		pr_err("Setting RESET GPIO to 1 failed\n");
		gpio_free(MAX3107_RESET_GPIO);
		return -EIO;
	}
	gpio_free(MAX3107_RESET_GPIO);
	msleep(MAX3107_WAKEUP_DELAY);
	return 0;
}

static int max3107_aava_configure(struct max3107_port *s)
{
	int retval;

	/* Initialize GPIO chip data */
	s->chip = max3107_gpio_chip;
	s->chip.label = s->spi->modalias;
	s->chip.dev = &s->spi->dev;

	/* Add GPIO chip */
	retval = gpiochip_add(&s->chip);
	if (retval) {
		dev_err(&s->spi->dev, "Adding GPIO chip failed\n");
		return retval;
	}

	/* Temporary fix for EV2 boot problems, set modem reset to 0 */
	max3107_gpio_direction_out(&s->chip, 3, 0);
	return 0;
}

#if 0
/* This will get enabled once we have the board stuff merged for this
   specific case */

static const struct baud_table brg13_ext[] = {
	{ 300,    MAX3107_BRG13_B300 },
	{ 600,    MAX3107_BRG13_B600 },
	{ 1200,   MAX3107_BRG13_B1200 },
	{ 2400,   MAX3107_BRG13_B2400 },
	{ 4800,   MAX3107_BRG13_B4800 },
	{ 9600,   MAX3107_BRG13_B9600 },
	{ 19200,  MAX3107_BRG13_B19200 },
	{ 57600,  MAX3107_BRG13_B57600 },
	{ 115200, MAX3107_BRG13_B115200 },
	{ 230400, MAX3107_BRG13_B230400 },
	{ 460800, MAX3107_BRG13_B460800 },
	{ 921600, MAX3107_BRG13_B921600 },
	{ 0, 0 }
};

static void max3107_aava_init(struct max3107_port *s)
{
	/*override for AAVA SC specific*/
	if (mrst_platform_id() == MRST_PLATFORM_AAVA_SC) {
		if (get_koski_build_id() <= KOSKI_EV2)
			if (s->ext_clk) {
				s->brg_cfg = MAX3107_BRG13_B9600;
				s->baud_tbl = (struct baud_table *)brg13_ext;
			}
	}
}
#endif

static int __devexit max3107_aava_remove(struct spi_device *spi)
{
	struct max3107_port *s = dev_get_drvdata(&spi->dev);

	/* Remove GPIO chip */
	if (gpiochip_remove(&s->chip))
		dev_warn(&spi->dev, "Removing GPIO chip failed\n");

	/* Then do the default remove */
	return max3107_remove(spi);
}

/* Platform data */
static struct max3107_plat aava_plat_data = {
	.loopback               = 0,
	.ext_clk                = 1,
/*	.init			= max3107_aava_init, */
	.configure		= max3107_aava_configure,
	.hw_suspend		= max3107_hw_susp,
	.polled_mode            = 0,
	.poll_time              = 0,
};


static int __devinit max3107_probe_aava(struct spi_device *spi)
{
	int err = max3107_aava_reset(spi);
	if (err < 0)
		return err;
	return max3107_probe(spi, &aava_plat_data);
}

/* Spi driver data */
static struct spi_driver max3107_driver = {
	.driver = {
		.name		= "aava-max3107",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},
	.probe		= max3107_probe_aava,
	.remove		= __devexit_p(max3107_aava_remove),
	.suspend	= max3107_suspend,
	.resume		= max3107_resume,
};

/* Driver init function */
static int __init max3107_init(void)
{
	return spi_register_driver(&max3107_driver);
}

/* Driver exit function */
static void __exit max3107_exit(void)
{
	spi_unregister_driver(&max3107_driver);
}

module_init(max3107_init);
module_exit(max3107_exit);

MODULE_DESCRIPTION("MAX3107 driver");
MODULE_AUTHOR("Aavamobile");
MODULE_ALIAS("spi:aava-max3107");
MODULE_LICENSE("GPL v2");
