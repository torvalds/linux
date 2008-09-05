/*
 * linux/arch/arm/mach-ep93xx/gpio.c
 *
 * Generic EP93xx GPIO handling
 *
 * Copyright (c) 2008 Ryan Mallon <ryan@bluewatersys.com>
 *
 * Based on code originally from:
 *  linux/arch/arm/mach-ep93xx/core.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#include <mach/ep93xx-regs.h>
#include <asm/io.h>
#include <asm/gpio.h>

struct ep93xx_gpio_chip {
	struct gpio_chip	chip;

	unsigned int		data_reg;
	unsigned int		data_dir_reg;
};

#define to_ep93xx_gpio_chip(c) container_of(c, struct ep93xx_gpio_chip, chip)

/* From core.c */
extern void ep93xx_gpio_int_mask(unsigned line);
extern void ep93xx_gpio_update_int_params(unsigned port);

static int ep93xx_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct ep93xx_gpio_chip *ep93xx_chip = to_ep93xx_gpio_chip(chip);
	unsigned long flags;
	u8 v;

	local_irq_save(flags);
	v = __raw_readb(ep93xx_chip->data_dir_reg);
	v &= ~(1 << offset);
	__raw_writeb(v, ep93xx_chip->data_dir_reg);
	local_irq_restore(flags);

	return 0;
}

static int ep93xx_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int val)
{
	struct ep93xx_gpio_chip *ep93xx_chip = to_ep93xx_gpio_chip(chip);
	unsigned long flags;
	int line;
	u8 v;

	local_irq_save(flags);

	/* Set the value */
	v = __raw_readb(ep93xx_chip->data_reg);
	if (val)
		v |= (1 << offset);
	else
		v &= ~(1 << offset);
	__raw_writeb(v, ep93xx_chip->data_reg);

	/* Drive as an output */
	line = chip->base + offset;
	if (line <= EP93XX_GPIO_LINE_MAX_IRQ) {
		/* Ports A/B/F */
		ep93xx_gpio_int_mask(line);
		ep93xx_gpio_update_int_params(line >> 3);
	}

	v = __raw_readb(ep93xx_chip->data_dir_reg);
	v |= (1 << offset);
	__raw_writeb(v, ep93xx_chip->data_dir_reg);

	local_irq_restore(flags);

	return 0;
}

static int ep93xx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ep93xx_gpio_chip *ep93xx_chip = to_ep93xx_gpio_chip(chip);

	return !!(__raw_readb(ep93xx_chip->data_reg) & (1 << offset));
}

static void ep93xx_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct ep93xx_gpio_chip *ep93xx_chip = to_ep93xx_gpio_chip(chip);
	unsigned long flags;
	u8 v;

	local_irq_save(flags);
	v = __raw_readb(ep93xx_chip->data_reg);
	if (val)
		v |= (1 << offset);
	else
		v &= ~(1 << offset);
	__raw_writeb(v, ep93xx_chip->data_reg);
	local_irq_restore(flags);
}

static void ep93xx_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct ep93xx_gpio_chip *ep93xx_chip = to_ep93xx_gpio_chip(chip);
	u8 data_reg, data_dir_reg;
	int i;

	data_reg = __raw_readb(ep93xx_chip->data_reg);
	data_dir_reg = __raw_readb(ep93xx_chip->data_dir_reg);

	for (i = 0; i < chip->ngpio; i++)
		seq_printf(s, "GPIO %s%d: %s %s\n", chip->label, i,
			   (data_reg & (1 << i)) ? "set" : "clear",
			   (data_dir_reg & (1 << i)) ? "out" : "in");
}

#define EP93XX_GPIO_BANK(name, dr, ddr, base_gpio)			\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = ep93xx_gpio_direction_input, \
			.direction_output = ep93xx_gpio_direction_output, \
			.get		  = ep93xx_gpio_get,		\
			.set		  = ep93xx_gpio_set,		\
			.dbg_show	  = ep93xx_gpio_dbg_show,	\
			.base		  = base_gpio,			\
			.ngpio		  = 8,				\
		},							\
		.data_reg	= EP93XX_GPIO_REG(dr),			\
		.data_dir_reg	= EP93XX_GPIO_REG(ddr),			\
	}

static struct ep93xx_gpio_chip ep93xx_gpio_banks[] = {
	EP93XX_GPIO_BANK("A", 0x00, 0x10, 0),
	EP93XX_GPIO_BANK("B", 0x04, 0x14, 8),
	EP93XX_GPIO_BANK("C", 0x08, 0x18, 40),
	EP93XX_GPIO_BANK("D", 0x0c, 0x1c, 24),
	EP93XX_GPIO_BANK("E", 0x20, 0x24, 32),
	EP93XX_GPIO_BANK("F", 0x30, 0x34, 16),
	EP93XX_GPIO_BANK("G", 0x38, 0x3c, 48),
	EP93XX_GPIO_BANK("H", 0x40, 0x44, 56),
};

void __init ep93xx_gpio_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ep93xx_gpio_banks); i++)
		gpiochip_add(&ep93xx_gpio_banks[i].chip);
}
