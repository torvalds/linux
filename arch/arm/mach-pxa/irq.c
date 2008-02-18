/*
 *  linux/arch/arm/mach-pxa/irq.c
 *
 *  Generic PXA IRQ handling, GPIO IRQ demultiplexing, etc.
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
#include <linux/interrupt.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/pxa-regs.h>

#include "generic.h"


/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static void pxa_mask_low_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void pxa_unmask_low_irq(unsigned int irq)
{
	ICMR |= (1 << irq);
}

static struct irq_chip pxa_internal_chip_low = {
	.name		= "SC",
	.ack		= pxa_mask_low_irq,
	.mask		= pxa_mask_low_irq,
	.unmask		= pxa_unmask_low_irq,
};

void __init pxa_init_irq_low(void)
{
	int irq;

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/* only unmasked interrupts kick us out of idle */
	ICCR = 1;

	for (irq = PXA_IRQ(0); irq <= PXA_IRQ(31); irq++) {
		set_irq_chip(irq, &pxa_internal_chip_low);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)

/*
 * This is for the second set of internal IRQs as found on the PXA27x.
 */

static void pxa_mask_high_irq(unsigned int irq)
{
	ICMR2 &= ~(1 << (irq - 32));
}

static void pxa_unmask_high_irq(unsigned int irq)
{
	ICMR2 |= (1 << (irq - 32));
}

static struct irq_chip pxa_internal_chip_high = {
	.name		= "SC-hi",
	.ack		= pxa_mask_high_irq,
	.mask		= pxa_mask_high_irq,
	.unmask		= pxa_unmask_high_irq,
};

void __init pxa_init_irq_high(void)
{
	int irq;

	ICMR2 = 0;
	ICLR2 = 0;

	for (irq = PXA_IRQ(32); irq < PXA_IRQ(64); irq++) {
		set_irq_chip(irq, &pxa_internal_chip_high);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}
#endif

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

	if (type == IRQT_PROBE) {
	    /* Don't mess with enabled GPIOs using preconfigured edges or
	       GPIOs set to alternate function or to output during probe */
		if ((GPIO_IRQ_rising_edge[idx] | GPIO_IRQ_falling_edge[idx] | GPDR(gpio)) &
		    GPIO_bit(gpio))
			return 0;
		if (GAFR(gpio) & (0x3 << (((gpio) & 0xf)*2)))
			return 0;
		type = __IRQT_RISEDGE | __IRQT_FALEDGE;
	}

	/* printk(KERN_DEBUG "IRQ%d (GPIO%d): ", irq, gpio); */

	pxa_gpio_mode(gpio | GPIO_IN);

	if (type & __IRQT_RISEDGE) {
		/* printk("rising "); */
		__set_bit (gpio, GPIO_IRQ_rising_edge);
	} else {
		__clear_bit (gpio, GPIO_IRQ_rising_edge);
	}

	if (type & __IRQT_FALEDGE) {
		/* printk("falling "); */
		__set_bit (gpio, GPIO_IRQ_falling_edge);
	} else {
		__clear_bit (gpio, GPIO_IRQ_falling_edge);
	}

	/* printk("edges\n"); */

	GRER(gpio) = GPIO_IRQ_rising_edge[idx] & GPIO_IRQ_mask[idx];
	GFER(gpio) = GPIO_IRQ_falling_edge[idx] & GPIO_IRQ_mask[idx];
	return 0;
}

/*
 * GPIO IRQs must be acknowledged.  This is for GPIO 0 and 1.
 */

static void pxa_ack_low_gpio(unsigned int irq)
{
	GEDR0 = (1 << (irq - IRQ_GPIO0));
}

static struct irq_chip pxa_low_gpio_chip = {
	.name		= "GPIO-l",
	.ack		= pxa_ack_low_gpio,
	.mask		= pxa_mask_low_irq,
	.unmask		= pxa_unmask_low_irq,
	.set_type	= pxa_gpio_irq_type,
};

/*
 * Demux handler for GPIO>=2 edge detect interrupts
 */

static void pxa_gpio_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned int mask;
	int loop;

	do {
		loop = 0;

		mask = GEDR0 & GPIO_IRQ_mask[0] & ~3;
		if (mask) {
			GEDR0 = mask;
			irq = IRQ_GPIO(2);
			desc = irq_desc + irq;
			mask >>= 2;
			do {
				if (mask & 1)
					desc_handle_irq(irq, desc);
				irq++;
				desc++;
				mask >>= 1;
			} while (mask);
			loop = 1;
		}

		mask = GEDR1 & GPIO_IRQ_mask[1];
		if (mask) {
			GEDR1 = mask;
			irq = IRQ_GPIO(32);
			desc = irq_desc + irq;
			do {
				if (mask & 1)
					desc_handle_irq(irq, desc);
				irq++;
				desc++;
				mask >>= 1;
			} while (mask);
			loop = 1;
		}

		mask = GEDR2 & GPIO_IRQ_mask[2];
		if (mask) {
			GEDR2 = mask;
			irq = IRQ_GPIO(64);
			desc = irq_desc + irq;
			do {
				if (mask & 1)
					desc_handle_irq(irq, desc);
				irq++;
				desc++;
				mask >>= 1;
			} while (mask);
			loop = 1;
		}

		mask = GEDR3 & GPIO_IRQ_mask[3];
		if (mask) {
			GEDR3 = mask;
			irq = IRQ_GPIO(96);
			desc = irq_desc + irq;
			do {
				if (mask & 1)
					desc_handle_irq(irq, desc);
				irq++;
				desc++;
				mask >>= 1;
			} while (mask);
			loop = 1;
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
	set_irq_chip(IRQ_GPIO_2_x, &pxa_internal_chip_low);
	set_irq_chained_handler(IRQ_GPIO_2_x, pxa_gpio_demux_handler);

	pxa_init_gpio(gpio_nr);
}

void __init pxa_init_irq_set_wake(int (*set_wake)(unsigned int, unsigned int))
{
	pxa_internal_chip_low.set_wake = set_wake;
#ifdef CONFIG_PXA27x
	pxa_internal_chip_high.set_wake = set_wake;
#endif
	pxa_low_gpio_chip.set_wake = set_wake;
	pxa_muxed_gpio_chip.set_wake = set_wake;
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[2];

static int pxa_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	switch (dev->id) {
	case 0:
		saved_icmr[0] = ICMR;
		ICMR = 0;
		break;
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	case 1:
		saved_icmr[1] = ICMR2;
		ICMR2 = 0;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static int pxa_irq_resume(struct sys_device *dev)
{
	switch (dev->id) {
	case 0:
		ICMR = saved_icmr[0];
		ICLR = 0;
		ICCR = 1;
		break;
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	case 1:
		ICMR2 = saved_icmr[1];
		ICLR2 = 0;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}
#else
#define pxa_irq_suspend		NULL
#define pxa_irq_resume		NULL
#endif

struct sysdev_class pxa_irq_sysclass = {
	.name		= "irq",
	.suspend	= pxa_irq_suspend,
	.resume		= pxa_irq_resume,
};

static int __init pxa_irq_init(void)
{
	return sysdev_class_register(&pxa_irq_sysclass);
}

core_initcall(pxa_irq_init);
