/*
 * Atmel PIO2 Port Multiplexer support
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <asm/gpio.h>
#include <asm/io.h>

#include <asm/arch/portmux.h>

#include "pio.h"

#define MAX_NR_PIO_DEVICES		8

struct pio_device {
	void __iomem *regs;
	const struct platform_device *pdev;
	struct clk *clk;
	u32 pinmux_mask;
	u32 gpio_mask;
	char name[8];
};

static struct pio_device pio_dev[MAX_NR_PIO_DEVICES];

static struct pio_device *gpio_to_pio(unsigned int gpio)
{
	struct pio_device *pio;
	unsigned int index;

	index = gpio >> 5;
	if (index >= MAX_NR_PIO_DEVICES)
		return NULL;
	pio = &pio_dev[index];
	if (!pio->regs)
		return NULL;

	return pio;
}

/* Pin multiplexing API */

void __init at32_select_periph(unsigned int pin, unsigned int periph,
			       unsigned long flags)
{
	struct pio_device *pio;
	unsigned int pin_index = pin & 0x1f;
	u32 mask = 1 << pin_index;

	pio = gpio_to_pio(pin);
	if (unlikely(!pio)) {
		printk("pio: invalid pin %u\n", pin);
		goto fail;
	}

	if (unlikely(test_and_set_bit(pin_index, &pio->pinmux_mask))) {
		printk("%s: pin %u is busy\n", pio->name, pin_index);
		goto fail;
	}

	pio_writel(pio, PUER, mask);
	if (periph)
		pio_writel(pio, BSR, mask);
	else
		pio_writel(pio, ASR, mask);

	pio_writel(pio, PDR, mask);
	if (!(flags & AT32_GPIOF_PULLUP))
		pio_writel(pio, PUDR, mask);

	/* gpio_request NOT allowed */
	set_bit(pin_index, &pio->gpio_mask);

	return;

fail:
	dump_stack();
}

void __init at32_select_gpio(unsigned int pin, unsigned long flags)
{
	struct pio_device *pio;
	unsigned int pin_index = pin & 0x1f;
	u32 mask = 1 << pin_index;

	pio = gpio_to_pio(pin);
	if (unlikely(!pio)) {
		printk("pio: invalid pin %u\n", pin);
		goto fail;
	}

	if (unlikely(test_and_set_bit(pin_index, &pio->pinmux_mask))) {
		printk("%s: pin %u is busy\n", pio->name, pin_index);
		goto fail;
	}

	if (flags & AT32_GPIOF_OUTPUT) {
		if (flags & AT32_GPIOF_HIGH)
			pio_writel(pio, SODR, mask);
		else
			pio_writel(pio, CODR, mask);
		if (flags & AT32_GPIOF_MULTIDRV)
			pio_writel(pio, MDER, mask);
		else
			pio_writel(pio, MDDR, mask);
		pio_writel(pio, PUDR, mask);
		pio_writel(pio, OER, mask);
	} else {
		if (flags & AT32_GPIOF_PULLUP)
			pio_writel(pio, PUER, mask);
		else
			pio_writel(pio, PUDR, mask);
		if (flags & AT32_GPIOF_DEGLITCH)
			pio_writel(pio, IFER, mask);
		else
			pio_writel(pio, IFDR, mask);
		pio_writel(pio, ODR, mask);
	}

	pio_writel(pio, PER, mask);

	/* gpio_request now allowed */
	clear_bit(pin_index, &pio->gpio_mask);

	return;

fail:
	dump_stack();
}

/* Reserve a pin, preventing anyone else from changing its configuration. */
void __init at32_reserve_pin(unsigned int pin)
{
	struct pio_device *pio;
	unsigned int pin_index = pin & 0x1f;

	pio = gpio_to_pio(pin);
	if (unlikely(!pio)) {
		printk("pio: invalid pin %u\n", pin);
		goto fail;
	}

	if (unlikely(test_and_set_bit(pin_index, &pio->pinmux_mask))) {
		printk("%s: pin %u is busy\n", pio->name, pin_index);
		goto fail;
	}

	return;

fail:
	dump_stack();
}

/*--------------------------------------------------------------------------*/

/* GPIO API */

int gpio_request(unsigned int gpio, const char *label)
{
	struct pio_device *pio;
	unsigned int pin;

	pio = gpio_to_pio(gpio);
	if (!pio)
		return -ENODEV;

	pin = gpio & 0x1f;
	if (test_and_set_bit(pin, &pio->gpio_mask))
		return -EBUSY;

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned int gpio)
{
	struct pio_device *pio;
	unsigned int pin;

	pio = gpio_to_pio(gpio);
	if (!pio) {
		printk(KERN_ERR
		       "gpio: attempted to free invalid pin %u\n", gpio);
		return;
	}

	pin = gpio & 0x1f;
	if (!test_and_clear_bit(pin, &pio->gpio_mask))
		printk(KERN_ERR "gpio: freeing free or non-gpio pin %s-%u\n",
		       pio->name, pin);
}
EXPORT_SYMBOL(gpio_free);

int gpio_direction_input(unsigned int gpio)
{
	struct pio_device *pio;
	unsigned int pin;

	pio = gpio_to_pio(gpio);
	if (!pio)
		return -ENODEV;

	pin = gpio & 0x1f;
	pio_writel(pio, ODR, 1 << pin);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned int gpio, int value)
{
	struct pio_device *pio;
	unsigned int pin;

	pio = gpio_to_pio(gpio);
	if (!pio)
		return -ENODEV;

	gpio_set_value(gpio, value);

	pin = gpio & 0x1f;
	pio_writel(pio, OER, 1 << pin);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned int gpio)
{
	struct pio_device *pio = &pio_dev[gpio >> 5];

	return (pio_readl(pio, PDSR) >> (gpio & 0x1f)) & 1;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned int gpio, int value)
{
	struct pio_device *pio = &pio_dev[gpio >> 5];
	u32 mask;

	mask = 1 << (gpio & 0x1f);
	if (value)
		pio_writel(pio, SODR, mask);
	else
		pio_writel(pio, CODR, mask);
}
EXPORT_SYMBOL(gpio_set_value);

/*--------------------------------------------------------------------------*/

/* GPIO IRQ support */

static void gpio_irq_mask(unsigned irq)
{
	unsigned		gpio = irq_to_gpio(irq);
	struct pio_device	*pio = &pio_dev[gpio >> 5];

	pio_writel(pio, IDR, 1 << (gpio & 0x1f));
}

static void gpio_irq_unmask(unsigned irq)
{
	unsigned		gpio = irq_to_gpio(irq);
	struct pio_device	*pio = &pio_dev[gpio >> 5];

	pio_writel(pio, IER, 1 << (gpio & 0x1f));
}

static int gpio_irq_type(unsigned irq, unsigned type)
{
	if (type != IRQ_TYPE_EDGE_BOTH && type != IRQ_TYPE_NONE)
		return -EINVAL;

	return 0;
}

static struct irq_chip gpio_irqchip = {
	.name		= "gpio",
	.mask		= gpio_irq_mask,
	.unmask		= gpio_irq_unmask,
	.set_type	= gpio_irq_type,
};

static void gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct pio_device	*pio = get_irq_chip_data(irq);
	unsigned		gpio_irq;

	gpio_irq = (unsigned) get_irq_data(irq);
	for (;;) {
		u32		isr;
		struct irq_desc	*d;

		/* ack pending GPIO interrupts */
		isr = pio_readl(pio, ISR) & pio_readl(pio, IMR);
		if (!isr)
			break;
		do {
			int i;

			i = ffs(isr) - 1;
			isr &= ~(1 << i);

			i += gpio_irq;
			d = &irq_desc[i];

			d->handle_irq(i, d);
		} while (isr);
	}
}

static void __init
gpio_irq_setup(struct pio_device *pio, int irq, int gpio_irq)
{
	unsigned	i;

	set_irq_chip_data(irq, pio);
	set_irq_data(irq, (void *) gpio_irq);

	for (i = 0; i < 32; i++, gpio_irq++) {
		set_irq_chip_data(gpio_irq, pio);
		set_irq_chip_and_handler(gpio_irq, &gpio_irqchip,
				handle_simple_irq);
	}

	set_irq_chained_handler(irq, gpio_irq_handler);
}

/*--------------------------------------------------------------------------*/

static int __init pio_probe(struct platform_device *pdev)
{
	struct pio_device *pio = NULL;
	int irq = platform_get_irq(pdev, 0);
	int gpio_irq_base = GPIO_IRQ_BASE + pdev->id * 32;

	BUG_ON(pdev->id >= MAX_NR_PIO_DEVICES);
	pio = &pio_dev[pdev->id];
	BUG_ON(!pio->regs);

	gpio_irq_setup(pio, irq, gpio_irq_base);

	platform_set_drvdata(pdev, pio);

	printk(KERN_DEBUG "%s: base 0x%p, irq %d chains %d..%d\n",
	       pio->name, pio->regs, irq, gpio_irq_base, gpio_irq_base + 31);

	return 0;
}

static struct platform_driver pio_driver = {
	.probe		= pio_probe,
	.driver		= {
		.name		= "pio",
	},
};

static int __init pio_init(void)
{
	return platform_driver_register(&pio_driver);
}
postcore_initcall(pio_init);

void __init at32_init_pio(struct platform_device *pdev)
{
	struct resource *regs;
	struct pio_device *pio;

	if (pdev->id > MAX_NR_PIO_DEVICES) {
		dev_err(&pdev->dev, "only %d PIO devices supported\n",
			MAX_NR_PIO_DEVICES);
		return;
	}

	pio = &pio_dev[pdev->id];
	snprintf(pio->name, sizeof(pio->name), "pio%d", pdev->id);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "no mmio resource defined\n");
		return;
	}

	pio->clk = clk_get(&pdev->dev, "mck");
	if (IS_ERR(pio->clk))
		/*
		 * This is a fatal error, but if we continue we might
		 * be so lucky that we manage to initialize the
		 * console and display this message...
		 */
		dev_err(&pdev->dev, "no mck clock defined\n");
	else
		clk_enable(pio->clk);

	pio->pdev = pdev;
	pio->regs = ioremap(regs->start, regs->end - regs->start + 1);

	/*
	 * request_gpio() is only valid for pins that have been
	 * explicitly configured as GPIO and not previously requested
	 */
	pio->gpio_mask = ~0UL;

	/* start with irqs disabled and acked */
	pio_writel(pio, IDR, ~0UL);
	(void) pio_readl(pio, ISR);
}
