// SPDX-License-Identifier: GPL-2.0+
/*
 * HD44780 Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "charlcd.h"
#include "hd44780_common.h"

enum hd44780_pin {
	/* Order does matter due to writing to GPIO array subsets! */
	PIN_DATA0,	/* Optional */
	PIN_DATA1,	/* Optional */
	PIN_DATA2,	/* Optional */
	PIN_DATA3,	/* Optional */
	PIN_DATA4,
	PIN_DATA5,
	PIN_DATA6,
	PIN_DATA7,
	PIN_CTRL_RS,
	PIN_CTRL_RW,	/* Optional */
	PIN_CTRL_E,
	PIN_CTRL_BL,   /* Optional */
	PIN_NUM
};

struct hd44780 {
	struct gpio_desc *pins[PIN_NUM];
};

static void hd44780_backlight(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct hd44780_common *hdc = lcd->drvdata;
	struct hd44780 *hd = hdc->hd44780;

	if (hd->pins[PIN_CTRL_BL])
		gpiod_set_value_cansleep(hd->pins[PIN_CTRL_BL], on);
}

static void hd44780_strobe_gpio(struct hd44780 *hd)
{
	/* Maintain the data during 20 us before the strobe */
	udelay(20);

	gpiod_set_value_cansleep(hd->pins[PIN_CTRL_E], 1);

	/* Maintain the strobe during 40 us */
	udelay(40);

	gpiod_set_value_cansleep(hd->pins[PIN_CTRL_E], 0);
}

/* write to an LCD panel register in 8 bit GPIO mode */
static void hd44780_write_gpio8(struct hd44780 *hd, u8 val, unsigned int rs)
{
	DECLARE_BITMAP(values, 10); /* for DATA[0-7], RS, RW */
	unsigned int n;

	values[0] = val;
	__assign_bit(8, values, rs);
	n = hd->pins[PIN_CTRL_RW] ? 10 : 9;

	/* Present the data to the port */
	gpiod_set_array_value_cansleep(n, &hd->pins[PIN_DATA0], NULL, values);

	hd44780_strobe_gpio(hd);
}

/* write to an LCD panel register in 4 bit GPIO mode */
static void hd44780_write_gpio4(struct hd44780 *hd, u8 val, unsigned int rs)
{
	DECLARE_BITMAP(values, 6); /* for DATA[4-7], RS, RW */
	unsigned int n;

	/* High nibble + RS, RW */
	values[0] = val >> 4;
	__assign_bit(4, values, rs);
	n = hd->pins[PIN_CTRL_RW] ? 6 : 5;

	/* Present the data to the port */
	gpiod_set_array_value_cansleep(n, &hd->pins[PIN_DATA4], NULL, values);

	hd44780_strobe_gpio(hd);

	/* Low nibble */
	values[0] &= ~0x0fUL;
	values[0] |= val & 0x0f;

	/* Present the data to the port */
	gpiod_set_array_value_cansleep(n, &hd->pins[PIN_DATA4], NULL, values);

	hd44780_strobe_gpio(hd);
}

/* Send a command to the LCD panel in 8 bit GPIO mode */
static void hd44780_write_cmd_gpio8(struct hd44780_common *hdc, int cmd)
{
	struct hd44780 *hd = hdc->hd44780;

	hd44780_write_gpio8(hd, cmd, 0);

	/* The shortest command takes at least 120 us */
	udelay(120);
}

/* Send data to the LCD panel in 8 bit GPIO mode */
static void hd44780_write_data_gpio8(struct hd44780_common *hdc, int data)
{
	struct hd44780 *hd = hdc->hd44780;

	hd44780_write_gpio8(hd, data, 1);

	/* The shortest data takes at least 45 us */
	udelay(45);
}

static const struct charlcd_ops hd44780_ops_gpio8 = {
	.backlight	= hd44780_backlight,
	.print		= hd44780_common_print,
	.gotoxy		= hd44780_common_gotoxy,
	.home		= hd44780_common_home,
	.clear_display	= hd44780_common_clear_display,
	.init_display	= hd44780_common_init_display,
	.shift_cursor	= hd44780_common_shift_cursor,
	.shift_display	= hd44780_common_shift_display,
	.display	= hd44780_common_display,
	.cursor		= hd44780_common_cursor,
	.blink		= hd44780_common_blink,
	.fontsize	= hd44780_common_fontsize,
	.lines		= hd44780_common_lines,
	.redefine_char	= hd44780_common_redefine_char,
};

/* Send a command to the LCD panel in 4 bit GPIO mode */
static void hd44780_write_cmd_gpio4(struct hd44780_common *hdc, int cmd)
{
	struct hd44780 *hd = hdc->hd44780;

	hd44780_write_gpio4(hd, cmd, 0);

	/* The shortest command takes at least 120 us */
	udelay(120);
}

/* Send 4-bits of a command to the LCD panel in raw 4 bit GPIO mode */
static void hd44780_write_cmd_raw_gpio4(struct hd44780_common *hdc, int cmd)
{
	DECLARE_BITMAP(values, 6); /* for DATA[4-7], RS, RW */
	struct hd44780 *hd = hdc->hd44780;
	unsigned int n;

	/* Command nibble + RS, RW */
	values[0] = cmd & 0x0f;
	n = hd->pins[PIN_CTRL_RW] ? 6 : 5;

	/* Present the data to the port */
	gpiod_set_array_value_cansleep(n, &hd->pins[PIN_DATA4], NULL, values);

	hd44780_strobe_gpio(hd);
}

/* Send data to the LCD panel in 4 bit GPIO mode */
static void hd44780_write_data_gpio4(struct hd44780_common *hdc, int data)
{
	struct hd44780 *hd = hdc->hd44780;

	hd44780_write_gpio4(hd, data, 1);

	/* The shortest data takes at least 45 us */
	udelay(45);
}

static const struct charlcd_ops hd44780_ops_gpio4 = {
	.backlight	= hd44780_backlight,
	.print		= hd44780_common_print,
	.gotoxy		= hd44780_common_gotoxy,
	.home		= hd44780_common_home,
	.clear_display	= hd44780_common_clear_display,
	.init_display	= hd44780_common_init_display,
	.shift_cursor	= hd44780_common_shift_cursor,
	.shift_display	= hd44780_common_shift_display,
	.display	= hd44780_common_display,
	.cursor		= hd44780_common_cursor,
	.blink		= hd44780_common_blink,
	.fontsize	= hd44780_common_fontsize,
	.lines		= hd44780_common_lines,
	.redefine_char	= hd44780_common_redefine_char,
};

static int hd44780_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int i, base;
	struct charlcd *lcd;
	struct hd44780_common *hdc;
	struct hd44780 *hd;
	int ifwidth, ret = -ENOMEM;

	/* Required pins */
	ifwidth = gpiod_count(dev, "data");
	if (ifwidth < 0)
		return ifwidth;

	switch (ifwidth) {
	case 4:
		base = PIN_DATA4;
		break;
	case 8:
		base = PIN_DATA0;
		break;
	default:
		return -EINVAL;
	}

	hdc = hd44780_common_alloc();
	if (!hdc)
		return -ENOMEM;

	lcd = charlcd_alloc();
	if (!lcd)
		goto fail1;

	hd = kzalloc(sizeof(struct hd44780), GFP_KERNEL);
	if (!hd)
		goto fail2;

	hdc->hd44780 = hd;
	lcd->drvdata = hdc;
	for (i = 0; i < ifwidth; i++) {
		hd->pins[base + i] = devm_gpiod_get_index(dev, "data", i,
							  GPIOD_OUT_LOW);
		if (IS_ERR(hd->pins[base + i])) {
			ret = PTR_ERR(hd->pins[base + i]);
			goto fail3;
		}
	}

	hd->pins[PIN_CTRL_E] = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(hd->pins[PIN_CTRL_E])) {
		ret = PTR_ERR(hd->pins[PIN_CTRL_E]);
		goto fail3;
	}

	hd->pins[PIN_CTRL_RS] = devm_gpiod_get(dev, "rs", GPIOD_OUT_HIGH);
	if (IS_ERR(hd->pins[PIN_CTRL_RS])) {
		ret = PTR_ERR(hd->pins[PIN_CTRL_RS]);
		goto fail3;
	}

	/* Optional pins */
	hd->pins[PIN_CTRL_RW] = devm_gpiod_get_optional(dev, "rw",
							GPIOD_OUT_LOW);
	if (IS_ERR(hd->pins[PIN_CTRL_RW])) {
		ret = PTR_ERR(hd->pins[PIN_CTRL_RW]);
		goto fail3;
	}

	hd->pins[PIN_CTRL_BL] = devm_gpiod_get_optional(dev, "backlight",
							GPIOD_OUT_LOW);
	if (IS_ERR(hd->pins[PIN_CTRL_BL])) {
		ret = PTR_ERR(hd->pins[PIN_CTRL_BL]);
		goto fail3;
	}

	/* Required properties */
	ret = device_property_read_u32(dev, "display-height-chars",
				       &lcd->height);
	if (ret)
		goto fail3;
	ret = device_property_read_u32(dev, "display-width-chars", &lcd->width);
	if (ret)
		goto fail3;

	/*
	 * On displays with more than two rows, the internal buffer width is
	 * usually equal to the display width
	 */
	if (lcd->height > 2)
		hdc->bwidth = lcd->width;

	/* Optional properties */
	device_property_read_u32(dev, "internal-buffer-width", &hdc->bwidth);

	hdc->ifwidth = ifwidth;
	if (ifwidth == 8) {
		lcd->ops = &hd44780_ops_gpio8;
		hdc->write_data = hd44780_write_data_gpio8;
		hdc->write_cmd = hd44780_write_cmd_gpio8;
	} else {
		lcd->ops = &hd44780_ops_gpio4;
		hdc->write_data = hd44780_write_data_gpio4;
		hdc->write_cmd = hd44780_write_cmd_gpio4;
		hdc->write_cmd_raw4 = hd44780_write_cmd_raw_gpio4;
	}

	ret = charlcd_register(lcd);
	if (ret)
		goto fail3;

	platform_set_drvdata(pdev, lcd);
	return 0;

fail3:
	kfree(hd);
fail2:
	kfree(lcd);
fail1:
	kfree(hdc);
	return ret;
}

static int hd44780_remove(struct platform_device *pdev)
{
	struct charlcd *lcd = platform_get_drvdata(pdev);

	charlcd_unregister(lcd);
	kfree(lcd->drvdata);

	kfree(lcd);
	return 0;
}

static const struct of_device_id hd44780_of_match[] = {
	{ .compatible = "hit,hd44780" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hd44780_of_match);

static struct platform_driver hd44780_driver = {
	.probe = hd44780_probe,
	.remove = hd44780_remove,
	.driver		= {
		.name	= "hd44780",
		.of_match_table = hd44780_of_match,
	},
};

module_platform_driver(hd44780_driver);
MODULE_DESCRIPTION("HD44780 Character LCD driver");
MODULE_AUTHOR("Geert Uytterhoeven <geert@linux-m68k.org>");
MODULE_LICENSE("GPL");
