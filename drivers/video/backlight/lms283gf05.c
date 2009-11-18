/*
 * lms283gf05.c -- support for Samsung LMS283GF05 LCD
 *
 * Copyright (c) 2009 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/lcd.h>

#include <linux/spi/spi.h>
#include <linux/spi/lms283gf05.h>

struct lms283gf05_state {
	struct spi_device	*spi;
	struct lcd_device	*ld;
};

struct lms283gf05_seq {
	unsigned char		reg;
	unsigned short		value;
	unsigned char		delay;
};

/* Magic sequences supplied by manufacturer, for details refer to datasheet */
static struct lms283gf05_seq disp_initseq[] = {
	/* REG, VALUE, DELAY */
	{ 0x07, 0x0000, 0 },
	{ 0x13, 0x0000, 10 },

	{ 0x11, 0x3004, 0 },
	{ 0x14, 0x200F, 0 },
	{ 0x10, 0x1a20, 0 },
	{ 0x13, 0x0040, 50 },

	{ 0x13, 0x0060, 0 },
	{ 0x13, 0x0070, 200 },

	{ 0x01, 0x0127, 0 },
	{ 0x02,	0x0700, 0 },
	{ 0x03, 0x1030, 0 },
	{ 0x08, 0x0208, 0 },
	{ 0x0B, 0x0620, 0 },
	{ 0x0C, 0x0110, 0 },
	{ 0x30, 0x0120, 0 },
	{ 0x31, 0x0127, 0 },
	{ 0x32, 0x0000, 0 },
	{ 0x33, 0x0503, 0 },
	{ 0x34, 0x0727, 0 },
	{ 0x35, 0x0124, 0 },
	{ 0x36, 0x0706, 0 },
	{ 0x37, 0x0701, 0 },
	{ 0x38, 0x0F00, 0 },
	{ 0x39, 0x0F00, 0 },
	{ 0x40, 0x0000, 0 },
	{ 0x41, 0x0000, 0 },
	{ 0x42, 0x013f, 0 },
	{ 0x43, 0x0000, 0 },
	{ 0x44, 0x013f, 0 },
	{ 0x45, 0x0000, 0 },
	{ 0x46, 0xef00, 0 },
	{ 0x47, 0x013f, 0 },
	{ 0x48, 0x0000, 0 },
	{ 0x07, 0x0015, 30 },

	{ 0x07, 0x0017, 0 },

	{ 0x20, 0x0000, 0 },
	{ 0x21, 0x0000, 0 },
	{ 0x22, 0x0000, 0 }
};

static struct lms283gf05_seq disp_pdwnseq[] = {
	{ 0x07, 0x0016, 30 },

	{ 0x07, 0x0004, 0 },
	{ 0x10, 0x0220, 20 },

	{ 0x13, 0x0060, 50 },

	{ 0x13, 0x0040, 50 },

	{ 0x13, 0x0000, 0 },
	{ 0x10, 0x0000, 0 }
};


static void lms283gf05_reset(unsigned long gpio, bool inverted)
{
	gpio_set_value(gpio, !inverted);
	mdelay(100);
	gpio_set_value(gpio, inverted);
	mdelay(20);
	gpio_set_value(gpio, !inverted);
	mdelay(20);
}

static void lms283gf05_toggle(struct spi_device *spi,
			struct lms283gf05_seq *seq, int sz)
{
	char buf[3];
	int i;

	for (i = 0; i < sz; i++) {
		buf[0] = 0x74;
		buf[1] = 0x00;
		buf[2] = seq[i].reg;
		spi_write(spi, buf, 3);

		buf[0] = 0x76;
		buf[1] = seq[i].value >> 8;
		buf[2] = seq[i].value & 0xff;
		spi_write(spi, buf, 3);

		mdelay(seq[i].delay);
	}
}

static int lms283gf05_power_set(struct lcd_device *ld, int power)
{
	struct lms283gf05_state *st = lcd_get_data(ld);
	struct spi_device *spi = st->spi;
	struct lms283gf05_pdata *pdata = spi->dev.platform_data;

	if (power) {
		if (pdata)
			lms283gf05_reset(pdata->reset_gpio,
					pdata->reset_inverted);
		lms283gf05_toggle(spi, disp_initseq, ARRAY_SIZE(disp_initseq));
	} else {
		lms283gf05_toggle(spi, disp_pdwnseq, ARRAY_SIZE(disp_pdwnseq));
		if (pdata)
			gpio_set_value(pdata->reset_gpio,
					pdata->reset_inverted);
	}

	return 0;
}

static struct lcd_ops lms_ops = {
	.set_power	= lms283gf05_power_set,
	.get_power	= NULL,
};

static int __devinit lms283gf05_probe(struct spi_device *spi)
{
	struct lms283gf05_state *st;
	struct lms283gf05_pdata *pdata = spi->dev.platform_data;
	struct lcd_device *ld;
	int ret = 0;

	if (pdata != NULL) {
		ret = gpio_request(pdata->reset_gpio, "LMS285GF05 RESET");
		if (ret)
			return ret;

		ret = gpio_direction_output(pdata->reset_gpio,
						!pdata->reset_inverted);
		if (ret)
			goto err;
	}

	st = kzalloc(sizeof(struct lms283gf05_state), GFP_KERNEL);
	if (st == NULL) {
		dev_err(&spi->dev, "No memory for device state\n");
		ret = -ENOMEM;
		goto err;
	}

	ld = lcd_device_register("lms283gf05", &spi->dev, st, &lms_ops);
	if (IS_ERR(ld)) {
		ret = PTR_ERR(ld);
		goto err2;
	}

	st->spi = spi;
	st->ld = ld;

	dev_set_drvdata(&spi->dev, st);

	/* kick in the LCD */
	if (pdata)
		lms283gf05_reset(pdata->reset_gpio, pdata->reset_inverted);
	lms283gf05_toggle(spi, disp_initseq, ARRAY_SIZE(disp_initseq));

	return 0;

err2:
	kfree(st);
err:
	if (pdata != NULL)
		gpio_free(pdata->reset_gpio);

	return ret;
}

static int __devexit lms283gf05_remove(struct spi_device *spi)
{
	struct lms283gf05_state *st = dev_get_drvdata(&spi->dev);
	struct lms283gf05_pdata *pdata = st->spi->dev.platform_data;

	lcd_device_unregister(st->ld);

	if (pdata != NULL)
		gpio_free(pdata->reset_gpio);

	kfree(st);

	return 0;
}

static struct spi_driver lms283gf05_driver = {
	.driver = {
		.name	= "lms283gf05",
		.owner	= THIS_MODULE,
	},
	.probe		= lms283gf05_probe,
	.remove		= __devexit_p(lms283gf05_remove),
};

static __init int lms283gf05_init(void)
{
	return spi_register_driver(&lms283gf05_driver);
}

static __exit void lms283gf05_exit(void)
{
	spi_unregister_driver(&lms283gf05_driver);
}

module_init(lms283gf05_init);
module_exit(lms283gf05_exit);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("LCD283GF05 LCD");
MODULE_LICENSE("GPL v2");
