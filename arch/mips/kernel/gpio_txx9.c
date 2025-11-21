// SPDX-License-Identifier: GPL-2.0-only
/*
 * A gpio chip driver for TXx9 SoCs
 *
 * Copyright (C) 2008 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/gpio/driver.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <asm/txx9pio.h>

static DEFINE_SPINLOCK(txx9_gpio_lock);

static struct txx9_pio_reg __iomem *txx9_pioptr;

static int txx9_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return !!(__raw_readl(&txx9_pioptr->din) & (1 << offset));
}

static void txx9_gpio_set_raw(unsigned int offset, int value)
{
	u32 val;
	val = __raw_readl(&txx9_pioptr->dout);
	if (value)
		val |= 1 << offset;
	else
		val &= ~(1 << offset);
	__raw_writel(val, &txx9_pioptr->dout);
}

static int txx9_gpio_set(struct gpio_chip *chip, unsigned int offset,
			 int value)
{
	unsigned long flags;
	spin_lock_irqsave(&txx9_gpio_lock, flags);
	txx9_gpio_set_raw(offset, value);
	mmiowb();
	spin_unlock_irqrestore(&txx9_gpio_lock, flags);

	return 0;
}

static int txx9_gpio_dir_in(struct gpio_chip *chip, unsigned int offset)
{
	unsigned long flags;
	spin_lock_irqsave(&txx9_gpio_lock, flags);
	__raw_writel(__raw_readl(&txx9_pioptr->dir) & ~(1 << offset),
		     &txx9_pioptr->dir);
	mmiowb();
	spin_unlock_irqrestore(&txx9_gpio_lock, flags);
	return 0;
}

static int txx9_gpio_dir_out(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	unsigned long flags;
	spin_lock_irqsave(&txx9_gpio_lock, flags);
	txx9_gpio_set_raw(offset, value);
	__raw_writel(__raw_readl(&txx9_pioptr->dir) | (1 << offset),
		     &txx9_pioptr->dir);
	mmiowb();
	spin_unlock_irqrestore(&txx9_gpio_lock, flags);
	return 0;
}

static struct gpio_chip txx9_gpio_chip = {
	.get = txx9_gpio_get,
	.set = txx9_gpio_set,
	.direction_input = txx9_gpio_dir_in,
	.direction_output = txx9_gpio_dir_out,
	.label = "TXx9",
};

int __init txx9_gpio_init(unsigned long baseaddr,
			  unsigned int base, unsigned int num)
{
	txx9_pioptr = ioremap(baseaddr, sizeof(struct txx9_pio_reg));
	if (!txx9_pioptr)
		return -ENODEV;
	txx9_gpio_chip.base = base;
	txx9_gpio_chip.ngpio = num;
	return gpiochip_add_data(&txx9_gpio_chip, NULL);
}
