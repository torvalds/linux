/*
 *  Copyright (C) 2007, OpenWrt.org, Florian Fainelli <florian@openwrt.org>
 *  	RDC321x architecture specific GPIO support
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/autoconf.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <asm/mach-rdc321x/rdc321x_defs.h>

static inline int rdc_gpio_is_valid(unsigned gpio)
{
	return (gpio <= RDC_MAX_GPIO);
}

static unsigned int rdc_gpio_read(unsigned gpio)
{
	unsigned int val;

	val = 0x80000000 | (7 << 11) | ((gpio&0x20?0x84:0x48));
	outl(val, RDC3210_CFGREG_ADDR);
	udelay(10);
	val = inl(RDC3210_CFGREG_DATA);
	val |= (0x1 << (gpio & 0x1F));
	outl(val, RDC3210_CFGREG_DATA);
	udelay(10);
	val = 0x80000000 | (7 << 11) | ((gpio&0x20?0x88:0x4C));
	outl(val, RDC3210_CFGREG_ADDR);
	udelay(10);
	val = inl(RDC3210_CFGREG_DATA);

	return val;
}

static void rdc_gpio_write(unsigned int val)
{
	if (val) {
		outl(val, RDC3210_CFGREG_DATA);
		udelay(10);
	}
}

int rdc_gpio_get_value(unsigned gpio)
{
	if (rdc_gpio_is_valid(gpio))
		return (int)rdc_gpio_read(gpio);
	else
		return -EINVAL;
}
EXPORT_SYMBOL(rdc_gpio_get_value);

void rdc_gpio_set_value(unsigned gpio, int value)
{
	unsigned int val;

	if (!rdc_gpio_is_valid(gpio))
		return;

	val = rdc_gpio_read(gpio);

	if (value)
		val &= ~(0x1 << (gpio & 0x1F));
	else
		val |= (0x1 << (gpio & 0x1F));

	rdc_gpio_write(val);
}
EXPORT_SYMBOL(rdc_gpio_set_value);

int rdc_gpio_direction_input(unsigned gpio)
{
	return 0;
}
EXPORT_SYMBOL(rdc_gpio_direction_input);

int rdc_gpio_direction_output(unsigned gpio, int value)
{
	return 0;
}
EXPORT_SYMBOL(rdc_gpio_direction_output);


