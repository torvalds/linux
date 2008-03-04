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
#include <linux/irq.h>

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

#define GPIO_CHIP(_n)							\
	[_n] = {							\
		.regbase = GPIO##_n##_BASE,				\
		.chip = {						\
			.label		  = "gpio-" #_n,		\
			.direction_input  = pxa_gpio_direction_input,	\
			.direction_output = pxa_gpio_direction_output,	\
			.get		  = pxa_gpio_get,		\
			.set		  = pxa_gpio_set,		\
			.base		  = (_n) * 32,			\
			.ngpio		  = 32,				\
		},							\
	}

static struct pxa_gpio_chip pxa_gpio_chip[] = {
	GPIO_CHIP(0),
	GPIO_CHIP(1),
	GPIO_CHIP(2),
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	GPIO_CHIP(3),
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

/*
 * PXA GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * Use this instead of directly setting GRER/GFER.
 */

static long GPIO_IRQ_rising_edge[4];
static long GPIO_IRQ_falling_edge[4];
static long GPIO_IRQ_mask[4];

static int pxa_gpio_irq_type(unsigned int irq, unsigned int type)
{
	int gpio, idx;

	gpio = IRQ_TO_GPIO(irq);
	idx = gpio >> 5;

	if (type == IRQ_TYPE_PROBE) {
		/* Don't mess with enabled GPIOs using preconfigured edges or
		 * GPIOs set to alternate function or to output during probe
		 */
		if ((GPIO_IRQ_rising_edge[idx] |
		     GPIO_IRQ_falling_edge[idx] |
		     GPDR(gpio)) & GPIO_bit(gpio))
			return 0;
		if (GAFR(gpio) & (0x3 << (((gpio) & 0xf)*2)))
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	pxa_gpio_mode(gpio | GPIO_IN);

	if (type & IRQ_TYPE_EDGE_RISING)
		__set_bit(gpio, GPIO_IRQ_rising_edge);
	else
		__clear_bit(gpio, GPIO_IRQ_rising_edge);

	if (type & IRQ_TYPE_EDGE_FALLING)
		__set_bit(gpio, GPIO_IRQ_falling_edge);
	else
		__clear_bit(gpio, GPIO_IRQ_falling_edge);

	GRER(gpio) = GPIO_IRQ_rising_edge[idx] & GPIO_IRQ_mask[idx];
	GFER(gpio) = GPIO_IRQ_falling_edge[idx] & GPIO_IRQ_mask[idx];

	pr_debug("%s: IRQ%d (GPIO%d) - edge%s%s\n", __func__, irq, gpio,
		((type & IRQ_TYPE_EDGE_RISING)  ? " rising"  : ""),
		((type & IRQ_TYPE_EDGE_FALLING) ? " falling" : ""));
	return 0;
}

/*
 * GPIO IRQs must be acknowledged.  This is for GPIO 0 and 1.
 */

static void pxa_ack_low_gpio(unsigned int irq)
{
	GEDR0 = (1 << (irq - IRQ_GPIO0));
}

static void pxa_mask_low_gpio(unsigned int irq)
{
	ICMR &= ~(1 << (irq - PXA_IRQ(0)));
}

static void pxa_unmask_low_gpio(unsigned int irq)
{
	ICMR |= 1 << (irq - PXA_IRQ(0));
}

static struct irq_chip pxa_low_gpio_chip = {
	.name		= "GPIO-l",
	.ack		= pxa_ack_low_gpio,
	.mask		= pxa_mask_low_gpio,
	.unmask		= pxa_unmask_low_gpio,
	.set_type	= pxa_gpio_irq_type,
};

/*
 * Demux handler for GPIO>=2 edge detect interrupts
 */

#define GEDR_BITS	(sizeof(gedr) * BITS_PER_BYTE)

static void pxa_gpio_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	int loop, bit, n;
	unsigned long gedr[4];

	do {
		gedr[0] = GEDR0 & GPIO_IRQ_mask[0] & ~3;
		gedr[1] = GEDR1 & GPIO_IRQ_mask[1];
		gedr[2] = GEDR2 & GPIO_IRQ_mask[2];
		gedr[3] = GEDR3 & GPIO_IRQ_mask[3];

		GEDR0 = gedr[0]; GEDR1 = gedr[1];
		GEDR2 = gedr[2]; GEDR3 = gedr[3];

		loop = 0;
		bit = find_first_bit(gedr, GEDR_BITS);
		while (bit < GEDR_BITS) {
			loop = 1;

			n = PXA_GPIO_IRQ_BASE + bit;
			desc_handle_irq(n, irq_desc + n);

			bit = find_next_bit(gedr, GEDR_BITS, bit + 1);
		}
	} while (loop);
}

static void pxa_ack_muxed_gpio(unsigned int irq)
{
	int gpio = irq - IRQ_GPIO(2) + 2;
	GEDR(gpio) = GPIO_bit(gpio);
}

static void pxa_mask_muxed_gpio(unsigned int irq)
{
	int gpio = irq - IRQ_GPIO(2) + 2;
	__clear_bit(gpio, GPIO_IRQ_mask);
	GRER(gpio) &= ~GPIO_bit(gpio);
	GFER(gpio) &= ~GPIO_bit(gpio);
}

static void pxa_unmask_muxed_gpio(unsigned int irq)
{
	int gpio = irq - IRQ_GPIO(2) + 2;
	int idx = gpio >> 5;
	__set_bit(gpio, GPIO_IRQ_mask);
	GRER(gpio) = GPIO_IRQ_rising_edge[idx] & GPIO_IRQ_mask[idx];
	GFER(gpio) = GPIO_IRQ_falling_edge[idx] & GPIO_IRQ_mask[idx];
}

static struct irq_chip pxa_muxed_gpio_chip = {
	.name		= "GPIO",
	.ack		= pxa_ack_muxed_gpio,
	.mask		= pxa_mask_muxed_gpio,
	.unmask		= pxa_unmask_muxed_gpio,
	.set_type	= pxa_gpio_irq_type,
};

void __init pxa_init_irq_gpio(int gpio_nr)
{
	int irq, i;

	pxa_last_gpio = gpio_nr - 1;

	/* clear all GPIO edge detects */
	for (i = 0; i < gpio_nr; i += 32) {
		GFER(i) = 0;
		GRER(i) = 0;
		GEDR(i) = GEDR(i);
	}

	/* GPIO 0 and 1 must have their mask bit always set */
	GPIO_IRQ_mask[0] = 3;

	for (irq = IRQ_GPIO0; irq <= IRQ_GPIO1; irq++) {
		set_irq_chip(irq, &pxa_low_gpio_chip);
		set_irq_handler(irq, handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	for (irq = IRQ_GPIO(2); irq < IRQ_GPIO(gpio_nr); irq++) {
		set_irq_chip(irq, &pxa_muxed_gpio_chip);
		set_irq_handler(irq, handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/* Install handler for GPIO>=2 edge detect interrupts */
	set_irq_chained_handler(IRQ_GPIO_2_x, pxa_gpio_demux_handler);

	pxa_init_gpio(gpio_nr);
}

void __init pxa_init_gpio_set_wake(int (*set_wake)(unsigned int, unsigned int))
{
	pxa_low_gpio_chip.set_wake = set_wake;
	pxa_muxed_gpio_chip.set_wake = set_wake;
}
