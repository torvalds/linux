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
#include <linux/sysdev.h>
#include <linux/gpio.h>

#include <asm/system.h>
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

static int puv3_gpio_type(unsigned int irq, unsigned int type)
{
	unsigned int mask;

	if (irq < IRQ_GPIOHIGH)
		mask = 1 << irq;
	else
		mask = GPIO_MASK(irq);

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

	GPIO_GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
	GPIO_GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;

	return 0;
}

/*
 * GPIO IRQs must be acknowledged.  This is for IRQs from 0 to 7.
 */
static void puv3_low_gpio_ack(unsigned int irq)
{
	GPIO_GEDR = (1 << irq);
}

static void puv3_low_gpio_mask(unsigned int irq)
{
	INTC_ICMR &= ~(1 << irq);
}

static void puv3_low_gpio_unmask(unsigned int irq)
{
	INTC_ICMR |= 1 << irq;
}

static int puv3_low_gpio_wake(unsigned int irq, unsigned int on)
{
	if (on)
		PM_PWER |= 1 << irq;
	else
		PM_PWER &= ~(1 << irq);
	return 0;
}

static struct irq_chip puv3_low_gpio_chip = {
	.name		= "GPIO-low",
	.ack		= puv3_low_gpio_ack,
	.mask		= puv3_low_gpio_mask,
	.unmask		= puv3_low_gpio_unmask,
	.set_type	= puv3_gpio_type,
	.set_wake	= puv3_low_gpio_wake,
};

/*
 * IRQ8 (GPIO0 through 27) handler.  We enter here with the
 * irq_controller_lock held, and IRQs disabled.  Decode the IRQ
 * and call the handler.
 */
static void
puv3_gpio_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned int mask;

	mask = GPIO_GEDR;
	do {
		/*
		 * clear down all currently active IRQ sources.
		 * We will be processing them all.
		 */
		GPIO_GEDR = mask;

		irq = IRQ_GPIO0;
		do {
			if (mask & 1)
				generic_handle_irq(irq);
			mask >>= 1;
			irq++;
		} while (mask);
		mask = GPIO_GEDR;
	} while (mask);
}

/*
 * GPIO0-27 edge IRQs need to be handled specially.
 * In addition, the IRQs are all collected up into one bit in the
 * interrupt controller registers.
 */
static void puv3_high_gpio_ack(unsigned int irq)
{
	unsigned int mask = GPIO_MASK(irq);

	GPIO_GEDR = mask;
}

static void puv3_high_gpio_mask(unsigned int irq)
{
	unsigned int mask = GPIO_MASK(irq);

	GPIO_IRQ_mask &= ~mask;

	GPIO_GRER &= ~mask;
	GPIO_GFER &= ~mask;
}

static void puv3_high_gpio_unmask(unsigned int irq)
{
	unsigned int mask = GPIO_MASK(irq);

	GPIO_IRQ_mask |= mask;

	GPIO_GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
	GPIO_GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;
}

static int puv3_high_gpio_wake(unsigned int irq, unsigned int on)
{
	if (on)
		PM_PWER |= PM_PWER_GPIOHIGH;
	else
		PM_PWER &= ~PM_PWER_GPIOHIGH;
	return 0;
}

static struct irq_chip puv3_high_gpio_chip = {
	.name		= "GPIO-high",
	.ack		= puv3_high_gpio_ack,
	.mask		= puv3_high_gpio_mask,
	.unmask		= puv3_high_gpio_unmask,
	.set_type	= puv3_gpio_type,
	.set_wake	= puv3_high_gpio_wake,
};

/*
 * We don't need to ACK IRQs on the PKUnity unless they're GPIOs
 * this is for internal IRQs i.e. from 8 to 31.
 */
static void puv3_mask_irq(unsigned int irq)
{
	INTC_ICMR &= ~(1 << irq);
}

static void puv3_unmask_irq(unsigned int irq)
{
	INTC_ICMR |= (1 << irq);
}

/*
 * Apart form GPIOs, only the RTC alarm can be a wakeup event.
 */
static int puv3_set_wake(unsigned int irq, unsigned int on)
{
	if (irq == IRQ_RTCAlarm) {
		if (on)
			PM_PWER |= PM_PWER_RTC;
		else
			PM_PWER &= ~PM_PWER_RTC;
		return 0;
	}
	return -EINVAL;
}

static struct irq_chip puv3_normal_chip = {
	.name		= "PKUnity-v3",
	.ack		= puv3_mask_irq,
	.mask		= puv3_mask_irq,
	.unmask		= puv3_unmask_irq,
	.set_wake	= puv3_set_wake,
};

static struct resource irq_resource = {
	.name	= "irqs",
	.start	= PKUNITY_INTC_BASE,
	.end	= PKUNITY_INTC_BASE + 0xFFFFF,
};

static struct puv3_irq_state {
	unsigned int	saved;
	unsigned int	icmr;
	unsigned int	iclr;
	unsigned int	iccr;
} puv3_irq_state;

static int puv3_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	struct puv3_irq_state *st = &puv3_irq_state;

	st->saved = 1;
	st->icmr = INTC_ICMR;
	st->iclr = INTC_ICLR;
	st->iccr = INTC_ICCR;

	/*
	 * Disable all GPIO-based interrupts.
	 */
	INTC_ICMR &= ~(0x1ff);

	/*
	 * Set the appropriate edges for wakeup.
	 */
	GPIO_GRER = PM_PWER & GPIO_IRQ_rising_edge;
	GPIO_GFER = PM_PWER & GPIO_IRQ_falling_edge;

	/*
	 * Clear any pending GPIO interrupts.
	 */
	GPIO_GEDR = GPIO_GEDR;

	return 0;
}

static int puv3_irq_resume(struct sys_device *dev)
{
	struct puv3_irq_state *st = &puv3_irq_state;

	if (st->saved) {
		INTC_ICCR = st->iccr;
		INTC_ICLR = st->iclr;

		GPIO_GRER = GPIO_IRQ_rising_edge & GPIO_IRQ_mask;
		GPIO_GFER = GPIO_IRQ_falling_edge & GPIO_IRQ_mask;

		INTC_ICMR = st->icmr;
	}
	return 0;
}

static struct sysdev_class puv3_irq_sysclass = {
	.name		= "pkunity-irq",
	.suspend	= puv3_irq_suspend,
	.resume		= puv3_irq_resume,
};

static struct sys_device puv3_irq_device = {
	.id		= 0,
	.cls		= &puv3_irq_sysclass,
};

static int __init puv3_irq_init_devicefs(void)
{
	sysdev_class_register(&puv3_irq_sysclass);
	return sysdev_register(&puv3_irq_device);
}

device_initcall(puv3_irq_init_devicefs);

void __init init_IRQ(void)
{
	unsigned int irq;

	request_resource(&iomem_resource, &irq_resource);

	/* disable all IRQs */
	INTC_ICMR = 0;

	/* all IRQs are IRQ, not REAL */
	INTC_ICLR = 0;

	/* clear all GPIO edge detects */
	GPIO_GPIR = FMASK(8, 0) & ~FIELD(1, 1, GPI_SOFF_REQ);
	GPIO_GFER = 0;
	GPIO_GRER = 0;
	GPIO_GEDR = 0x0FFFFFFF;

	INTC_ICCR = 1;

	for (irq = 0; irq < IRQ_GPIOHIGH; irq++) {
		set_irq_chip(irq, &puv3_low_gpio_chip);
		set_irq_handler(irq, handle_edge_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN,
			0);
	}

	for (irq = IRQ_GPIOHIGH + 1; irq < IRQ_GPIO0; irq++) {
		set_irq_chip(irq, &puv3_normal_chip);
		set_irq_handler(irq, handle_level_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOAUTOEN,
			IRQ_NOPROBE);
	}

	for (irq = IRQ_GPIO0; irq <= IRQ_GPIO27; irq++) {
		set_irq_chip(irq, &puv3_high_gpio_chip);
		set_irq_handler(irq, handle_edge_irq);
		irq_modify_status(irq,
			IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN,
			0);
	}

	/*
	 * Install handler for GPIO 0-27 edge detect interrupts
	 */
	set_irq_chip(IRQ_GPIOHIGH, &puv3_normal_chip);
	set_irq_chained_handler(IRQ_GPIOHIGH, puv3_gpio_handler);

#ifdef CONFIG_PUV3_GPIO
	puv3_init_gpio();
#endif
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, cpu;
	struct irq_desc *desc;
	struct irqaction *action;
	unsigned long flags;

	if (i == 0) {
		char cpuname[12];

		seq_printf(p, "    ");
		for_each_present_cpu(cpu) {
			sprintf(cpuname, "CPU%d", cpu);
			seq_printf(p, " %10s", cpuname);
		}
		seq_putc(p, '\n');
	}

	if (i < nr_irqs) {
		desc = irq_to_desc(i);
		raw_spin_lock_irqsave(&desc->lock, flags);
		action = desc->action;
		if (!action)
			goto unlock;

		seq_printf(p, "%3d: ", i);
		for_each_present_cpu(cpu)
			seq_printf(p, "%10u ", kstat_irqs_cpu(i, cpu));
		seq_printf(p, " %10s", desc->chip->name ? : "-");
		seq_printf(p, "  %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
unlock:
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	} else if (i == nr_irqs) {
		seq_printf(p, "Error in interrupt!\n");
	}
	return 0;
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

