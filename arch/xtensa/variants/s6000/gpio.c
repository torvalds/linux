/*
 * s6000 gpio driver
 *
 * Copyright (c) 2009 emlix GmbH
 * Authors:	Oskar Schirmer <os@emlix.com>
 *		Johannes Weiner <jw@emlix.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <variant/hardware.h>

#define S6_GPIO_DATA		0x000
#define S6_GPIO_IS		0x404
#define S6_GPIO_IBE		0x408
#define S6_GPIO_IEV		0x40C
#define S6_GPIO_IE		0x410
#define S6_GPIO_RIS		0x414
#define S6_GPIO_MIS		0x418
#define S6_GPIO_IC		0x41C
#define S6_GPIO_AFSEL		0x420
#define S6_GPIO_DIR		0x800
#define S6_GPIO_BANK(nr)	((nr) * 0x1000)
#define S6_GPIO_MASK(nr)	(4 << (nr))
#define S6_GPIO_OFFSET(nr) \
		(S6_GPIO_BANK((nr) >> 3) + S6_GPIO_MASK((nr) & 7))

static int direction_input(struct gpio_chip *chip, unsigned int off)
{
	writeb(0, S6_REG_GPIO + S6_GPIO_DIR + S6_GPIO_OFFSET(off));
	return 0;
}

static int get(struct gpio_chip *chip, unsigned int off)
{
	return readb(S6_REG_GPIO + S6_GPIO_DATA + S6_GPIO_OFFSET(off));
}

static int direction_output(struct gpio_chip *chip, unsigned int off, int val)
{
	unsigned rel = S6_GPIO_OFFSET(off);
	writeb(~0, S6_REG_GPIO + S6_GPIO_DIR + rel);
	writeb(val ? ~0 : 0, S6_REG_GPIO + S6_GPIO_DATA + rel);
	return 0;
}

static void set(struct gpio_chip *chip, unsigned int off, int val)
{
	writeb(val ? ~0 : 0, S6_REG_GPIO + S6_GPIO_DATA + S6_GPIO_OFFSET(off));
}

static struct gpio_chip gpiochip = {
	.owner = THIS_MODULE,
	.direction_input = direction_input,
	.get = get,
	.direction_output = direction_output,
	.set = set,
	.base = 0,
	.ngpio = 24,
	.can_sleep = 0, /* no blocking io needed */
	.exported = 0, /* no exporting to userspace */
};

static int gpio_init(void)
{
	return gpiochip_add(&gpiochip);
}
device_initcall(gpio_init);
