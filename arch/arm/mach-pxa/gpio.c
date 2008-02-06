/*
 *  linux/arch/arm/mach-pxa/gpio.c
 *
 *  Generic PXA GPIO handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>

#include <asm/gpio.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/pxa-regs.h>

#include "generic.h"


struct pxa_gpio_chip {
	struct gpio_chip chip;
	void __iomem     *regbase;
};

int pxa_last_gpio;

/*
 * Configure pins for GPIO or other functions
 */
int pxa_gpio_mode(int gpio_mode)
{
	unsigned long flags;
	int gpio = gpio_mode & GPIO_MD_MASK_NR;
	int fn = (gpio_mode & GPIO_MD_MASK_FN) >> 8;
	int gafr;

	if (gpio > pxa_last_gpio)
		return -EINVAL;

	local_irq_save(flags);
	if (gpio_mode & GPIO_DFLT_LOW)
		GPCR(gpio) = GPIO_bit(gpio);
	else if (gpio_mode & GPIO_DFLT_HIGH)
		GPSR(gpio) = GPIO_bit(gpio);
	if (gpio_mode & GPIO_MD_MASK_DIR)
		GPDR(gpio) |= GPIO_bit(gpio);
	else
		GPDR(gpio) &= ~GPIO_bit(gpio);
	gafr = GAFR(gpio) & ~(0x3 << (((gpio) & 0xf)*2));
	GAFR(gpio) = gafr |  (fn  << (((gpio) & 0xf)*2));
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(pxa_gpio_mode);

static int pxa_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long        flags;
	u32                  mask = 1 << offset;
	u32                  value;
	struct pxa_gpio_chip *pxa;
	void __iomem         *gpdr;

	pxa = container_of(chip, struct pxa_gpio_chip, chip);
	gpdr = pxa->regbase + GPDR_OFFSET;
	local_irq_save(flags);
	value = __raw_readl(gpdr);
	value &= ~mask;
	__raw_writel(value, gpdr);
	local_irq_restore(flags);

	return 0;
}

static int pxa_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	unsigned long        flags;
	u32                  mask = 1 << offset;
	u32                  tmp;
	struct pxa_gpio_chip *pxa;
	void __iomem         *gpdr;

	pxa = container_of(chip, struct pxa_gpio_chip, chip);
	__raw_writel(mask,
			pxa->regbase + (value ? GPSR_OFFSET : GPCR_OFFSET));
	gpdr = pxa->regbase + GPDR_OFFSET;
	local_irq_save(flags);
	tmp = __raw_readl(gpdr);
	tmp |= mask;
	__raw_writel(tmp, gpdr);
	local_irq_restore(flags);

	return 0;
}

/*
 * Return GPIO level
 */
static int pxa_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	u32                  mask = 1 << offset;
	struct pxa_gpio_chip *pxa;

	pxa = container_of(chip, struct pxa_gpio_chip, chip);
	return __raw_readl(pxa->regbase + GPLR_OFFSET) & mask;
}

/*
 * Set output GPIO level
 */
static void pxa_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	u32                  mask = 1 << offset;
	struct pxa_gpio_chip *pxa;

	pxa = container_of(chip, struct pxa_gpio_chip, chip);

	if (value)
		__raw_writel(mask, pxa->regbase + GPSR_OFFSET);
	else
		__raw_writel(mask, pxa->regbase + GPCR_OFFSET);
}

static struct pxa_gpio_chip pxa_gpio_chip[] = {
	[0] = {
		.regbase = GPIO0_BASE,
		.chip = {
			.label            = "gpio-0",
			.direction_input  = pxa_gpio_direction_input,
			.direction_output = pxa_gpio_direction_output,
			.get              = pxa_gpio_get,
			.set              = pxa_gpio_set,
			.base             = 0,
			.ngpio            = 32,
		},
	},
	[1] = {
		.regbase = GPIO1_BASE,
		.chip = {
			.label            = "gpio-1",
			.direction_input  = pxa_gpio_direction_input,
			.direction_output = pxa_gpio_direction_output,
			.get              = pxa_gpio_get,
			.set              = pxa_gpio_set,
			.base             = 32,
			.ngpio            = 32,
		},
	},
	[2] = {
		.regbase = GPIO2_BASE,
		.chip = {
			.label            = "gpio-2",
			.direction_input  = pxa_gpio_direction_input,
			.direction_output = pxa_gpio_direction_output,
			.get              = pxa_gpio_get,
			.set              = pxa_gpio_set,
			.base             = 64,
			.ngpio            = 32, /* 21 for PXA25x */
		},
	},
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	[3] = {
		.regbase = GPIO3_BASE,
		.chip = {
			.label            = "gpio-3",
			.direction_input  = pxa_gpio_direction_input,
			.direction_output = pxa_gpio_direction_output,
			.get              = pxa_gpio_get,
			.set              = pxa_gpio_set,
			.base             = 96,
			.ngpio            = 32,
		},
	},
#endif
};

void __init pxa_init_gpio(int gpio_nr)
{
	int i;

	/* add a GPIO chip for each register bank.
	 * the last PXA25x register only contains 21 GPIOs
	 */
	for (i = 0; i < gpio_nr; i += 32) {
		if (i+32 > gpio_nr)
			pxa_gpio_chip[i/32].chip.ngpio = gpio_nr - i;
		gpiochip_add(&pxa_gpio_chip[i/32].chip);
	}
}
