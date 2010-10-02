/* linux/arch/arm/plat-s5p/irq-gpioint.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 * Author: Kyungmin Park <kyungmin.park@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/map.h>
#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>

#define S5P_GPIOREG(x)			(S5P_VA_GPIO + (x))

#define GPIOINT_CON_OFFSET		0x700
#define GPIOINT_MASK_OFFSET		0x900
#define GPIOINT_PEND_OFFSET		0xA00

static struct s3c_gpio_chip *irq_chips[S5P_GPIOINT_GROUP_MAXNR];

static int s5p_gpioint_get_group(unsigned int irq)
{
	struct gpio_chip *chip = get_irq_data(irq);
	struct s3c_gpio_chip *s3c_chip = container_of(chip,
			struct s3c_gpio_chip, chip);
	int group;

	for (group = 0; group < S5P_GPIOINT_GROUP_MAXNR; group++)
		if (s3c_chip == irq_chips[group])
			break;

	return group;
}

static int s5p_gpioint_get_offset(unsigned int irq)
{
	struct gpio_chip *chip = get_irq_data(irq);
	struct s3c_gpio_chip *s3c_chip = container_of(chip,
			struct s3c_gpio_chip, chip);

	return irq - s3c_chip->irq_base;
}

static void s5p_gpioint_ack(unsigned int irq)
{
	int group, offset, pend_offset;
	unsigned int value;

	group = s5p_gpioint_get_group(irq);
	offset = s5p_gpioint_get_offset(irq);
	pend_offset = group << 2;

	value = __raw_readl(S5P_GPIOREG(GPIOINT_PEND_OFFSET) + pend_offset);
	value |= 1 << offset;
	__raw_writel(value, S5P_GPIOREG(GPIOINT_PEND_OFFSET) + pend_offset);
}

static void s5p_gpioint_mask(unsigned int irq)
{
	int group, offset, mask_offset;
	unsigned int value;

	group = s5p_gpioint_get_group(irq);
	offset = s5p_gpioint_get_offset(irq);
	mask_offset = group << 2;

	value = __raw_readl(S5P_GPIOREG(GPIOINT_MASK_OFFSET) + mask_offset);
	value |= 1 << offset;
	__raw_writel(value, S5P_GPIOREG(GPIOINT_MASK_OFFSET) + mask_offset);
}

static void s5p_gpioint_unmask(unsigned int irq)
{
	int group, offset, mask_offset;
	unsigned int value;

	group = s5p_gpioint_get_group(irq);
	offset = s5p_gpioint_get_offset(irq);
	mask_offset = group << 2;

	value = __raw_readl(S5P_GPIOREG(GPIOINT_MASK_OFFSET) + mask_offset);
	value &= ~(1 << offset);
	__raw_writel(value, S5P_GPIOREG(GPIOINT_MASK_OFFSET) + mask_offset);
}

static void s5p_gpioint_mask_ack(unsigned int irq)
{
	s5p_gpioint_mask(irq);
	s5p_gpioint_ack(irq);
}

static int s5p_gpioint_set_type(unsigned int irq, unsigned int type)
{
	int group, offset, con_offset;
	unsigned int value;

	group = s5p_gpioint_get_group(irq);
	offset = s5p_gpioint_get_offset(irq);
	con_offset = group << 2;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		type = S5P_IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = S5P_IRQ_TYPE_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		type = S5P_IRQ_TYPE_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = S5P_IRQ_TYPE_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = S5P_IRQ_TYPE_LEVEL_LOW;
		break;
	case IRQ_TYPE_NONE:
	default:
		printk(KERN_WARNING "No irq type\n");
		return -EINVAL;
	}

	value = __raw_readl(S5P_GPIOREG(GPIOINT_CON_OFFSET) + con_offset);
	value &= ~(0x7 << (offset * 0x4));
	value |= (type << (offset * 0x4));
	__raw_writel(value, S5P_GPIOREG(GPIOINT_CON_OFFSET) + con_offset);

	return 0;
}

struct irq_chip s5p_gpioint = {
	.name		= "s5p_gpioint",
	.ack		= s5p_gpioint_ack,
	.mask		= s5p_gpioint_mask,
	.mask_ack	= s5p_gpioint_mask_ack,
	.unmask		= s5p_gpioint_unmask,
	.set_type	= s5p_gpioint_set_type,
};

static void s5p_gpioint_handler(unsigned int irq, struct irq_desc *desc)
{
	int group, offset, pend_offset, mask_offset;
	int real_irq;
	unsigned int pend, mask;

	for (group = 0; group < S5P_GPIOINT_GROUP_MAXNR; group++) {
		pend_offset = group << 2;
		pend = __raw_readl(S5P_GPIOREG(GPIOINT_PEND_OFFSET) +
				pend_offset);
		if (!pend)
			continue;

		mask_offset = group << 2;
		mask = __raw_readl(S5P_GPIOREG(GPIOINT_MASK_OFFSET) +
				mask_offset);
		pend &= ~mask;

		for (offset = 0; offset < 8; offset++) {
			if (pend & (1 << offset)) {
				struct s3c_gpio_chip *chip = irq_chips[group];
				if (chip) {
					real_irq = chip->irq_base + offset;
					generic_handle_irq(real_irq);
				}
			}
		}
	}
}

static __init int s5p_gpioint_add(struct s3c_gpio_chip *chip)
{
	static int used_gpioint_groups = 0;
	static bool handler_registered = 0;
	int irq, group = chip->group;
	int i;

	if (used_gpioint_groups >= S5P_GPIOINT_GROUP_COUNT)
		return -ENOMEM;

	chip->irq_base = S5P_GPIOINT_BASE +
			 used_gpioint_groups * S5P_GPIOINT_GROUP_SIZE;
	used_gpioint_groups++;

	if (!handler_registered) {
		set_irq_chained_handler(IRQ_GPIOINT, s5p_gpioint_handler);
		handler_registered = 1;
	}

	irq_chips[group] = chip;
	for (i = 0; i < chip->chip.ngpio; i++) {
		irq = chip->irq_base + i;
		set_irq_chip(irq, &s5p_gpioint);
		set_irq_data(irq, &chip->chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
	return 0;
}

int __init s5p_register_gpio_interrupt(int pin)
{
	struct s3c_gpio_chip *my_chip = s3c_gpiolib_getchip(pin);
	int offset, group;
	int ret;

	if (!my_chip)
		return -EINVAL;

	offset = pin - my_chip->chip.base;
	group = my_chip->group;

	/* check if the group has been already registered */
	if (my_chip->irq_base)
		return my_chip->irq_base + offset;

	/* register gpio group */
	ret = s5p_gpioint_add(my_chip);
	if (ret == 0) {
		my_chip->chip.to_irq = samsung_gpiolib_to_irq;
		printk(KERN_INFO "Registered interrupt support for gpio group %d.\n",
		       group);
		return my_chip->irq_base + offset;
	}
	return ret;
}
