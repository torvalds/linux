/*
 * linux/arch/unicore32/kernel/irq.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <linux/proc_fs.h>
#include <linux/syscore_ops.h>
#include <linux/gpio.h>

#include <mach/hardware.h>

#include "setup.h"

/*
 * PKUnity GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * Use this instead of directly setting GRER/GFER.
 */
static int GPIO_IRQ_rising_edge;
static int GPIO_IRQ_falling_edge;
static int GPIO_IRQ_mask = 0;

#define GPIO_MASK(irq)		(1 << (irq - IRQ_GPIO0))

static int puv3_gpio_type(struct irq_data *d, unsigned int type)
{
	unsigned int mask;

	if (d->irq < IRQ_GPIOHIGH)
		mask = 1 << d->irq;
	else
		mask = GPIO_MASK(d->irq);

	if (type == IRQ_TYPE_PROBE) {
		if ((GPIO_IRQ_rising_edge | GPIO_IRQ_falling_edge) & mask)
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & IRQ_TYPE_EDGE_RISING)
		GPIO_IRQ_rising_edge |= mask;
	else
		GPIO_IRQ_rising_edge &= ~mask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		GPIO_IRQ_falling_edge |= mask;
	else
		GPIO_IRQ_falling_edge &= ~mask;

	writel(GPIO_IRQ_rising_edge & GPIO_IRQ_mask, GPIO_GRER);
	writel(GPIO_IRQ_falling_edge & GPIO_IRQ_mask, GPIO_GFER);

	return 0;
}

/*
 * GPIO IRQs must be acknowledged.  This is for IRQs from 0 to 7.
 */
static void puv3_low_gpio_ack(struct irq_data *d)
{
	writel((1 << d->irq), GPIO_GEDR);
}

static void puv3_low_gpio_mask(struct irq_data *d)
{
	writel(readl(INTC_ICMR) & ~(1 << d->irq), INTC_ICMR);
}

static void puv3_low_gpio_unmask(struct irq_data *d)
{
	writel(readl(INTC_ICMR) | (1 << d->irq), INTC_ICMR);
}

static int puv3_low_gpio_wake(struct irq_data *d, unsigned int on)
{
	if (on)
		writel(readl(PM_PWER) | (1 << d->irq), PM_PWER);
	else
		writel(readl(PM_PWER) & ~(1 << d->irq), PM_PWER);
	return 0;
}

static struct irq_chip puv3_low_gpio_chip = {
	.name		= "GPIO-low",
	.irq_ack	= puv3_low_gpio_ack,
	.irq_mask	= puv3_low_gpio_mask,
	.irq_unmask	= puv3_low_gpio_unmask,
	.irq_set_type	= puv3_gpio_type,
	.irq_set_wake	= puv3_low_gpio_wake,
};

/*
 * IRQ8 (GPIO0 through 27) handler.  We enter here with the
 * irq_controller_lock held, and IRQs disabled.  Decode the IRQ
 * and call the handler.
 */
static void puv3_gpio_handler(struct irq_desc *desc)
{
	unsigned int mask, irq;

	mask = readl(GPIO_GEDR);
	do {
		/*
		 * clear down all currently active IRQ sources.
		 * We will be processing them all.
		 */
		writel(mask, GPIO_GEDR);

		irq = IRQ_GPIO0;
		do {
			if (mask & 1)
				generic_handle_irq(irq);
			mask >>= 1;
			irq++;
		} while (mask);
		mask = readl(GPIO_GEDR);
	} while (mask);
}

/*
 * GPIO0-27 edge IRQs need to be handled specially.
 * In addition, the IRQs are all collected up into one bit in the
 * interrupt controller registers.
 */
static void puv3_high_gpio_ack(struct irq_data *d)
{
	unsigned int mask = GPIO_MASK(d->irq);

	writel(mask, GPIO_GEDR);
}

static void puv3_high_gpio_mask(struct irq_data *d)
{
	unsigned int mask = GPIO_MASK(d->irq);

	GPIO_IRQ_mask &= ~mask;

	writel(readl(GPIO_GRER) & ~mask, GPIO_GRER);
	writel(readl(GPIO_GFER) & ~mask, GPIO_GFER);
}

static void puv3_high_gpio_unmask(struct irq_data *d)
{
	unsigned int mask = GPIO_MASK(d->irq);

	GPIO_IRQ_mask |= mask;

	writel(GPIO_IRQ_rising_edge & GPIO_IRQ_mask, GPIO_GRER);
	writel(GPIO_IRQ_falling_edge & GPIO_IRQ_mask, GPIO_GFER);
}

static int puv3_high_gpio_wake(struct irq_data *d, unsigned int on)
{
	if (on)
		writel(readl(PM_PWER) | PM_PWER_GPIOHIGH, PM_PWER);
	else
		writel(readl(PM_PWER) & ~PM_PWER_GPIOHIGH, PM_PWER);
	return 0;
}

static struct irq_chip puv3_high_gpio_chip = {
	.name		= "GPIO-high",
	.irq_ack	= puv3_high_gpio_ack,
	.irq_mask	= puv3_high_gpio_mask,
	.irq_unmask	= puv3_high_gpio_unmask,
	.irq_set_type	= puv3_gpio_type,
	.irq_set_wake	= puv3_high_gpio_wake,
};

/*
 * We don't need to ACK IRQs on the PKUnity unless they're GPIOs
 * this is for internal IRQs i.e. from 8 to 31.
 */
static void puv3_mask_irq(struct irq_data *d)
{
	writel(readl(INTC_ICMR) & ~(1 << d->irq), INTC_ICMR);
}

static void puv3_unmask_irq(struct irq_data *d)
{
	writel(readl(INTC_ICMR) | (1 << d->irq), INTC_ICMR);
}

/*
 * Apart form GPIOs, only the RTC alarm can be a wakeup event.
 */
static int puv3_set_wake(struct irq_data *d, unsigned int on)
{
	if (d->irq == IRQ_RTCAlarm) {
		if (on)
			writel(readl(PM_PWER) | PM_PWER_RTC, PM_PWER);
		else
			writel(readl(PM_PWER) & ~PM_PWER_RTC, PM_PWER);
		return 0;
	}
	return -EINVAL;
}

static struct irq_chip puv3_normal_chip = {
	.name		= "PKUnity-v3",
	.irq_ack	= puv3_mask_irq,
	.irq_mask	= puv3_mask_irq,
	.irq_unmask	= puv3_unmask_irq,
	.irq_set_wake	= puv3_set_wake,
};

static struct resource irq_resource = {
	.name	= "irqs",
	.start	= io_v2p(PKUNITY_INTC_BASE),
	.end	= io_v2p(PKUNITY_INTC_BASE) + 0xFFFFF,
};

static struct puv3_irq_state {
	unsigned int	saved;
	unsigned int	icmr;
	unsigned int	iclr;
	unsigned int	iccr;
} puv3_irq_state;

static int puv3_irq_suspend(void)
{
	struct puv3_irq_state *st = &puv3_irq_state;

	st->saved = 1;
	st->icmr = readl(INTC_ICMR);
	st->iclr = readl(INTC_ICLR);
	st->iccr = readl(INTC_ICCR);

	/*
	 * Disable all GPIO-based interrupts.
	 */
	writel(readl(INTC_ICMR) & ~(0x1ff), INTC_ICMR);

	/*
	 * Set the appropriate edges for wakeup.
	 */
	writel(readl(PM_PWER) & GPIO_IRQ_rising_edge, GPIO_GRER);
	writel(readl(PM_PWER) & GPIO_IRQ_falling_edge, GPIO_GFER);

	/*
	 * Clear any pending GPIO interrupts.
	 */
	writel(readl(GPIO_GEDR), GPIO_GEDR);

	return 0;
}

static void puv3_irq_resume(void)
{
	struct puv3_irq_state *st = &puv3_irq_state;

	if (st->saved) {
		writel(st->iccr, INTC_ICCR);
		writel(st->iclr, INTC_ICLR);

		writel(GPIO_IRQ_rising_edge & GPIO_IRQ_mask, GPIO_GRER);
		writel(GPIO_IRQ_falling_edge & GPIO_IRQ_mask, GPIO_GFER);

		writel(st->icmr, INTC_ICMR);
	}
}

static struct syscore_ops puv3_irq_syscore_ops = {
	.suspend	= puv3_irq_suspend,
	.resume		= puv3_irq_resume,
};

static int __init puv3_irq_init_syscore(void)
{
	register_syscore_ops(&puv3_irq_syscore_ops);
	return 0;
}

device_initcall(puv3_irq_init_syscore);

void __init init_IRQ(void)
{
	unsigned int irq;

	request_resource(&iomem_resource, &irq_resource);

	/* disable all IRQs */
	writel(0, INTC_ICMR);

	/* all IRQs are IRQ, not REAL */
	writel(0, INTC_ICLR);

	/* clear all GPIO edge detects */
	writel(FMASK(8, 0) & ~FIELD(1, 1, GPI_SOFF_REQ), GPIO_GPIR);
	writel(0, GPIO_GFER);
	writel(0, GPIO_GRER);
	writel(0x0FFFFFFF, GPIO_GEDR);

	writel(1, INTC_ICCR);

	for (irq = 0; irq < IRQ_GPIOHIGH; irq++) {
		irq_set_chip(irq, &puv3_low_gpio_chip);
		irq_set_handler(irq, handle_edge_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN,
			0);
	}

	for (irq = IRQ_GPIOHIGH + 1; irq < IRQ_GPIO0; irq++) {
		irq_set_chip(irq, &puv3_normal_chip);
		irq_set_handler(irq, handle_level_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOAUTOEN,
			IRQ_NOPROBE);
	}

	for (irq = IRQ_GPIO0; irq <= IRQ_GPIO27; irq++) {
		irq_set_chip(irq, &puv3_high_gpio_chip);
		irq_set_handler(irq, handle_edge_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN,
			0);
	}

	/*
	 * Install handler for GPIO 0-27 edge detect interrupts
	 */
	irq_set_chip(IRQ_GPIOHIGH, &puv3_normal_chip);
	irq_set_chained_handler(IRQ_GPIOHIGH, puv3_gpio_handler);

#ifdef CONFIG_PUV3_GPIO
	puv3_init_gpio();
#endif
}

/*
 * do_IRQ handles all hardware IRQ's.  Decoded IRQs should not
 * come via this function.  Instead, they should provide their
 * own 'handler'
 */
asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (unlikely(irq >= nr_irqs)) {
		if (printk_ratelimit())
			printk(KERN_WARNING "Bad IRQ%u\n", irq);
		ack_bad_irq(irq);
	} else {
		generic_handle_irq(irq);
	}

	irq_exit();
	set_irq_regs(old_regs);
}

