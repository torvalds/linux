/* drivers/video/backlight/ili9320.c
 *
 * ILI9320 LCD controller driver core.
 *
 * Copyright 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>

#include <video/ili9320.h>

#include "ili9320.h"


static inline int ili9320_write_spi(struct ili9320 *ili,
				    unsigned int reg,
				    unsigned int value)
{
	struct ili9320_spi *spi = &ili->access.spi;
	unsigned char *addr = spi->buffer_addr;
	unsigned char *data = spi->buffer_data;

	/* spi message consits of:
	 * first byte: ID and operation
	 */

	addr[0] = spi->id | ILI9320_SPI_INDEX | ILI9320_SPI_WRITE;
	addr[1] = reg >> 8;
	addr[2] = reg;

	/* second message is the data to transfer */

	data[0] = spi->id | ILI9320_SPI_DATA  | ILI9320_SPI_WRITE;
 	data[1] = value >> 8;
	data[2] = value;

	return spi_sync(spi->dev, &spi->message);
}

int ili9320_write(struct ili9320 *ili, unsigned int reg, unsigned int value)
{
	dev_dbg(ili->dev, "write: reg=%02x, val=%04x\n", reg, value);
	return ili->write(ili, reg, value);
}

EXPORT_SYMBOL_GPL(ili9320_write);

int ili9320_write_regs(struct ili9320 *ili,
		       struct ili9320_reg *values,
		       int nr_values)
{
	int index;
	int ret;

	for (index = 0; index < nr_values; index++, values++) {
		ret = ili9320_write(ili, values->address, values->value);
		if (ret != 0)
			return ret;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(ili9320_write_regs);

static void ili9320_reset(struct ili9320 *lcd)
{
	struct ili9320_platdata *cfg = lcd->platdata;

	cfg->reset(1);
	mdelay(50);

	cfg->reset(0);
	mdelay(50);

	cfg->reset(1);
	mdelay(100);
}

static inline int ili9320_init_chip(struct ili9320 *lcd)
{
	int ret;

	ili9320_reset(lcd);

	ret = lcd->client->init(lcd, lcd->platdata);
	if (ret != 0) {
		dev_err(lcd->dev, "failed to initialise display\n");
		return ret;
	}

	lcd->initialised = 1;
	return 0;
}

static inline int ili9320_power_on(struct ili9320 *lcd)
{
	if (!lcd->initialised)
		ili9320_init_chip(lcd);

	lcd->display1 |= (ILI9320_DISPLAY1_D(3) | ILI9320_DISPLAY1_BASEE);
	ili9320_write(lcd, ILI9320_DISPLAY1, lcd->display1);

	return 0;
}

static inline int ili9320_power_off(struct ili9320 *lcd)
{
	lcd->display1 &= ~(ILI9320_DISPLAY1_D(3) | ILI9320_DISPLAY1_BASEE);
	ili9320_write(lcd, ILI9320_DISPLAY1, lcd->display1);

	return 0;
}

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

static int ili9320_power(struct ili9320 *lcd, int power)
{
	int ret = 0;

	dev_dbg(lcd->dev, "power %d => %d\n", lcd->power, power);

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = ili9320_power_on(lcd);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = ili9320_power_off(lcd);

	if (ret == 0)
		lcd->power = power;
	else
		dev_warn(lcd->dev, "failed to set power mode %d\n", power);

	return ret;
}

static inline struct ili9320 *to_our_lcd(struct lcd_device *lcd)
{
	return lcd_get_data(lcd);
}

static int ili9320_set_power(struct lcd_device *ld, int power)
{
	struct ili9320 *lcd = to_our_lcd(ld);

	return ili9320_power(lcd, power);
}

static int ili9320_get_power(struct lcd_device *ld)
{
	struct ili9320 *lcd = to_our_lcd(ld);

	return lcd->power;
}

static struct lcd_ops ili9320_ops = {
	.get_power	= ili9320_get_power,
	.set_power	= ili9320_set_power,
};

static void __devinit ili9320_setup_spi(struct ili9320 *ili,
					struct spi_device *dev)
{
	struct ili9320_spi *spi = &ili->access.spi;

	ili->write = ili9320_write_spi;
	spi->dev = dev;

	/* fill the two messages we are going to use to send the data
	 * with, the first the address followed by the data. The datasheet
	 * says they should be done as two distinct cycles of the SPI CS line.
	 */

	spi->xfer[0].tx_buf = spi->buffer_addr;
	spi->xfer[1].tx_buf = spi->buffer_data;
	spi->xfer[0].len = 3;
	spi->xfer[1].len = 3;
	spi->xfer[0].bits_per_word = 8;
	spi->xfer[1].bits_per_word = 8;
	spi->xfer[0].cs_change = 1;

	spi_message_init(&spi->message);
	spi_message_add_tail(&spi->xfer[0], &spi->message);
	spi_message_add_tail(&spi->xfer[1], &spi->message);
}

int __devinit ili9320_probe_spi(struct spi_device *spi,
				struct ili9320_client *client)
{
	struct ili9320_platdata *cfg = spi->dev.platform_data;
	struct device *dev = &spi->dev;
	struct ili9320 *ili;
	struct lcd_device *lcd;
	int ret = 0;

	/* verify we where given some information */

	if (cfg == NULL) {
		dev_err(dev, "no platform data supplied\n");
		return -EINVAL;
	}

	if (cfg->hsize <= 0 || cfg->vsize <= 0 || cfg->reset == NULL) {
		dev_err(dev, "invalid platform data supplied\n");
		return -EINVAL;
	}

	/* allocate and initialse our state */

	ili = devm_kzalloc(&spi->dev, sizeof(struct ili9320), GFP_KERNEL);
	if (ili == NULL) {
		dev_err(dev, "no memory for device\n");
		return -ENOMEM;
	}

	ili->access.spi.id = ILI9320_SPI_IDCODE | ILI9320_SPI_ID(1);

	ili->dev = dev;
	ili->client = client;
	ili->power = FB_BLANK_POWERDOWN;
	ili->platdata = cfg;

	dev_set_drvdata(&spi->dev, ili);

	ili9320_setup_spi(ili, spi);

	lcd = lcd_device_register("ili9320", dev, ili, &ili9320_ops);
	if (IS_ERR(lcd)) {
		dev_err(dev, "failed to register lcd device\n");
		return PTR_ERR(lcd);
	}

	ili->lcd = lcd;

	dev_info(dev, "initialising %s\n", client->name);

	ret = ili9320_power(ili, FB_BLANK_UNBLANK);
	if (ret != 0) {
		dev_err(dev, "failed to set lcd power state\n");
		goto err_unregister;
	}

	return 0;

 err_unregister:
	lcd_device_unregister(lcd);

	return ret;
}

EXPORT_SYMBOL_GPL(ili9320_probe_spi);

int __devexit ili9320_remove(struct ili9320 *ili)
{
	ili9320_power(ili, FB_BLANK_POWERDOWN);

	lcd_device_unregister(ili->lcd);

	return 0;
}

EXPORT_SYMBOL_GPL(ili9320_remove);

#ifdef CONFIG_PM
int ili9320_suspend(struct ili9320 *lcd, pm_message_t state)
{
	int ret;

	dev_dbg(lcd->dev, "%s: event %d\n", __func__, state.event);

	if (state.event == PM_EVENT_SUSPEND) {
		ret = ili9320_power(lcd, FB_BLANK_POWERDOWN);

		if (lcd->platdata->suspend == ILI9320_SUSPEND_DEEP) {
			ili9320_write(lcd, ILI9320_POWER1, lcd->power1 |
				      ILI9320_POWER1_SLP |
				      ILI9320_POWER1_DSTB);
			lcd->initialised = 0;
		}

		return ret;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(ili9320_suspend);

int ili9320_resume(struct ili9320 *lcd)
{
	dev_info(lcd->dev, "resuming from power state %d\n", lcd->power);

	if (lcd->platdata->suspend == ILI9320_SUSPEND_DEEP) {
		ili9320_write(lcd, ILI9320_POWER1, 0x00);
	}

	return ili9320_power(lcd, FB_BLANK_UNBLANK);
}

EXPORT_SYMBOL_GPL(ili9320_resume);
#endif

/* Power down all displays on reboot, poweroff or halt */
void ili9320_shutdown(struct ili9320 *lcd)
{
	ili9320_power(lcd, FB_BLANK_POWERDOWN);
}

EXPORT_SYMBOL_GPL(ili9320_shutdown);

MODULE_AUTHOR("Ben Dooks <ben-linux@fluff.org>");
MODULE_DESCRIPTION("ILI9320 LCD Driver");
MODULE_LICENSE("GPL v2");
