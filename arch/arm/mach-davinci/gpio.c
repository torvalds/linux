/*
 * TI DaVinci GPIO Support
 *
 * Copyright (c) 2006 David Brownell
 * Copyright (c) 2007, MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/bitops.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/gpio.h>

#include <asm/mach/irq.h>

static DEFINE_SPINLOCK(gpio_lock);
static DECLARE_BITMAP(gpio_in_use, DAVINCI_N_GPIO);

int gpio_request(unsigned gpio, const char *tag)
{
	if (gpio >= DAVINCI_N_GPIO)
		return -EINVAL;

	if (test_and_set_bit(gpio, gpio_in_use))
		return -EBUSY;

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	if (gpio >= DAVINCI_N_GPIO)
		return;

	clear_bit(gpio, gpio_in_use);
}
EXPORT_SYMBOL(gpio_free);

/* create a non-inlined version */
static struct gpio_controller *__iomem gpio2controller(unsigned gpio)
{
	return __gpio_to_controller(gpio);
}

/*
 * Assuming the pin is muxed as a gpio output, set its output value.
 */
void __gpio_set(unsigned gpio, int value)
{
	struct gpio_controller *__iomem g = gpio2controller(gpio);

	__raw_writel(__gpio_mask(gpio), value ? &g->set_data : &g->clr_data);
}
EXPORT_SYMBOL(__gpio_set);


/*
 * Read the pin's value (works even if it's set up as output);
 * returns zero/nonzero.
 *
 * Note that changes are synched to the GPIO clock, so reading values back
 * right after you've set them may give old values.
 */
int __gpio_get(unsigned gpio)
{
	struct gpio_controller *__iomem g = gpio2controller(gpio);

	return !!(__gpio_mask(gpio) & __raw_readl(&g->in_data));
}
EXPORT_SYMBOL(__gpio_get);


/*--------------------------------------------------------------------------*/

/*
 * board setup code *MUST* set PINMUX0 and PINMUX1 as
 * needed, and enable the GPIO clock.
 */

int gpio_direction_input(unsigned gpio)
{
	struct gpio_controller *__iomem g = gpio2controller(gpio);
	u32 temp;
	u32 mask;

	if (!g)
		return -EINVAL;

	spin_lock(&gpio_lock);
	mask = __gpio_mask(gpio);
	temp = __raw_readl(&g->dir);
	temp |= mask;
	__raw_writel(temp, &g->dir);
	spin_unlock(&gpio_lock);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	struct gpio_controller *__iomem g = gpio2controller(gpio);
	u32 temp;
	u32 mask;

	if (!g)
		return -EINVAL;

	spin_lock(&gpio_lock);
	mask = __gpio_mask(gpio);
	temp = __raw_readl(&g->dir);
	temp &= ~mask;
	__raw_writel(mask, value ? &g->set_data : &g->clr_data);
	__raw_writel(temp, &g->dir);
	spin_unlock(&gpio_lock);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

/*
 * We expect irqs will normally be set up as input pins, but they can also be
 * used as output pins ... which is convenient for testing.
 *
 * NOTE:  GPIO0..GPIO7 also have direct INTC hookups, which work in addition
 * to their GPIOBNK0 irq (but with a bit less overhead).  But we don't have
 * a good way to hook those up ...
 *
 * All those INTC hookups (GPIO0..GPIO7 plus five IRQ banks) can also
 * serve as EDMA event triggers.
 */

static void gpio_irq_disable(unsigned irq)
{
	struct gpio_controller *__iomem g = get_irq_chip_data(irq);
	u32 mask = __gpio_mask(irq_to_gpio(irq));

	__raw_writel(mask, &g->clr_falling);
	__raw_writel(mask, &g->clr_rising);
}

static void gpio_irq_enable(unsigned irq)
{
	struct gpio_controller *__iomem g = get_irq_chip_data(irq);
	u32 mask = __gpio_mask(irq_to_gpio(irq));

	if (irq_desc[irq].status & IRQ_TYPE_EDGE_FALLING)
		__raw_writel(mask, &g->set_falling);
	if (irq_desc[irq].status & IRQ_TYPE_EDGE_RISING)
		__raw_writel(mask, &g->set_rising);
}

static int gpio_irq_type(unsigned irq, unsigned trigger)
{
	struct gpio_controller *__iomem g = get_irq_chip_data(irq);
	u32 mask = __gpio_mask(irq_to_gpio(irq));

	if (trigger & ~(IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		return -EINVAL;

	irq_desc[irq].status &= ~IRQ_TYPE_SENSE_MASK;
	irq_desc[irq].status |= trigger;

	__raw_writel(mask, (trigger & IRQ_TYPE_EDGE_FALLING)
		     ? &g->set_falling : &g->clr_falling);
	__raw_writel(mask, (trigger & IRQ_TYPE_EDGE_RISING)
		     ? &g->set_rising : &g->clr_rising);
	return 0;
}

static struct irq_chip gpio_irqchip = {
	.name		= "GPIO",
	.enable		= gpio_irq_enable,
	.disable	= gpio_irq_disable,
	.set_type	= gpio_irq_type,
};

static void
gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct gpio_controller *__iomem g = get_irq_chip_data(irq);
	u32 mask = 0xffff;

	/* we only care about one bank */
	if (irq & 1)
		mask <<= 16;

	/* temporarily mask (level sensitive) parent IRQ */
	desc->chip->ack(irq);
	while (1) {
		u32		status;
		int		n;
		int		res;

		/* ack any irqs */
		status = __raw_readl(&g->intstat) & mask;
		if (!status)
			break;
		__raw_writel(status, &g->intstat);
		if (irq & 1)
			status >>= 16;

		/* now demux them to the right lowlevel handler */
		n = (int)get_irq_data(irq);
		while (status) {
			res = ffs(status);
			n += res;
			generic_handle_irq(n - 1);
			status >>= res;
		}
	}
	desc->chip->unmask(irq);
	/* now it may re-trigger */
}

/*
 * NOTE:  for suspend/resume, probably best to make a sysdev (and class)
 * with its suspend/resume calls hooking into the results of the set_wake()
 * calls ... so if no gpios are wakeup events the clock can be disabled,
 * with outputs left at previously set levels, and so that VDD3P3V.IOPWDN0
 * can be set appropriately for GPIOV33 pins.
 */

static int __init davinci_gpio_irq_setup(void)
{
	unsigned	gpio, irq, bank;
	struct clk	*clk;

	clk = clk_get(NULL, "gpio");
	if (IS_ERR(clk)) {
		printk(KERN_ERR "Error %ld getting gpio clock?\n",
		       PTR_ERR(clk));
		return 0;
	}

	clk_enable(clk);

	for (gpio = 0, irq = gpio_to_irq(0), bank = IRQ_GPIOBNK0;
	     gpio < DAVINCI_N_GPIO; bank++) {
		struct gpio_controller	*__iomem g = gpio2controller(gpio);
		unsigned		i;

		__raw_writel(~0, &g->clr_falling);
		__raw_writel(~0, &g->clr_rising);

		/* set up all irqs in this bank */
		set_irq_chained_handler(bank, gpio_irq_handler);
		set_irq_chip_data(bank, g);
		set_irq_data(bank, (void *)irq);

		for (i = 0; i < 16 && gpio < DAVINCI_N_GPIO;
		     i++, irq++, gpio++) {
			set_irq_chip(irq, &gpio_irqchip);
			set_irq_chip_data(irq, g);
			set_irq_handler(irq, handle_simple_irq);
			set_irq_flags(irq, IRQF_VALID);
		}
	}

	/* BINTEN -- per-bank interrupt enable. genirq would also let these
	 * bits be set/cleared dynamically.
	 */
	__raw_writel(0x1f, (void *__iomem)
		     IO_ADDRESS(DAVINCI_GPIO_BASE + 0x08));

	printk(KERN_INFO "DaVinci: %d gpio irqs\n", irq - gpio_to_irq(0));

	return 0;
}

arch_initcall(davinci_gpio_irq_setup);
