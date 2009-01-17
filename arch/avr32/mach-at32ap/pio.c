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

#include <mach/portmux.h>

#include "pio.h"

#define MAX_NR_PIO_DEVICES		8

struct pio_device {
	struct gpio_chip chip;
	void __iomem *regs;
	const struct platform_device *pdev;
	struct clk *clk;
	u32 pinmux_mask;
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
static DEFINE_SPINLOCK(pio_lock);

void __init at32_select_periph(unsigned int port, u32 pin_mask,
			       unsigned int periph, unsigned long flags)
{
	struct pio_device *pio;

	/* assign and verify pio */
	pio = gpio_to_pio(port);
	if (unlikely(!pio)) {
		printk(KERN_WARNING "pio: invalid port %u\n", port);
		goto fail;
	}

	/* Test if any of the requested pins is already muxed */
	spin_lock(&pio_lock);
	if (unlikely(pio->pinmux_mask & pin_mask)) {
		printk(KERN_WARNING "%s: pin(s) busy (requested 0x%x, busy 0x%x)\n",
		       pio->name, pin_mask, pio->pinmux_mask & pin_mask);
		spin_unlock(&pio_lock);
		goto fail;
	}

	pio->pinmux_mask |= pin_mask;

	/* enable pull ups */
	pio_writel(pio, PUER, pin_mask);

	/* select either peripheral A or B */
	if (periph)
		pio_writel(pio, BSR, pin_mask);
	else
		pio_writel(pio, ASR, pin_mask);

	/* enable peripheral control */
	pio_writel(pio, PDR, pin_mask);

	/* Disable pull ups if not requested. */
	if (!(flags & AT32_GPIOF_PULLUP))
		pio_writel(pio, PUDR, pin_mask);

	spin_unlock(&pio_lock);

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

	return;

fail:
	dump_stack();
}

/*
 * Undo a previous pin reservation. Will not affect the hardware
 * configuration.
 */
void at32_deselect_pin(unsigned int pin)
{
	struct pio_device *pio;
	unsigned int pin_index = pin & 0x1f;

	pio = gpio_to_pio(pin);
	if (unlikely(!pio)) {
		printk("pio: invalid pin %u\n", pin);
		dump_stack();
		return;
	}

	clear_bit(pin_index, &pio->pinmux_mask);
}

/* Reserve a pin, preventing anyone else from changing its configuration. */
void __init at32_reserve_pin(unsigned int port, u32 pin_mask)
{
	struct pio_device *pio;

	/* assign and verify pio */
	pio = gpio_to_pio(port);
	if (unlikely(!pio)) {
		printk(KERN_WARNING "pio: invalid port %u\n", port);
		goto fail;
	}

	/* Test if any of the requested pins is already muxed */
	spin_lock(&pio_lock);
	if (unlikely(pio->pinmux_mask & pin_mask)) {
		printk(KERN_WARNING "%s: pin(s) busy (req. 0x%x, busy 0x%x)\n",
		       pio->name, pin_mask, pio->pinmux_mask & pin_mask);
		spin_unlock(&pio_lock);
		goto fail;
	}

	/* Reserve pins */
	pio->pinmux_mask |= pin_mask;
	spin_unlock(&pio_lock);
	return;

fail:
	dump_stack();
}

/*--------------------------------------------------------------------------*/

/* GPIO API */

static int direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct pio_device *pio = container_of(chip, struct pio_device, chip);
	u32 mask = 1 << offset;

	if (!(pio_readl(pio, PSR) & mask))
		return -EINVAL;

	pio_writel(pio, ODR, mask);
	return 0;
}

static int gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pio_device *pio = container_of(chip, struct pio_device, chip);

	return (pio_readl(pio, PDSR) >> offset) & 1;
}

static void gpio_set(struct gpio_chip *chip, unsigned offset, int value);

static int direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pio_device *pio = container_of(chip, struct pio_device, chip);
	u32 mask = 1 << offset;

	if (!(pio_readl(pio, PSR) & mask))
		return -EINVAL;

	gpio_set(chip, offset, value);
	pio_writel(pio, OER, mask);
	return 0;
}

static void gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pio_device *pio = container_of(chip, struct pio_device, chip);
	u32 mask = 1 << offset;

	if (value)
		pio_writel(pio, SODR, mask);
	else
		pio_writel(pio, CODR, mask);
}

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

#ifdef CONFIG_DEBUG_FS

#include <linux/seq_file.h>

/*
 * This shows more info than the generic gpio dump code:
 * pullups, deglitching, open drain drive.
 */
static void pio_bank_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct pio_device *pio = container_of(chip, struct pio_device, chip);
	u32			psr, osr, imr, pdsr, pusr, ifsr, mdsr;
	unsigned		i;
	u32			mask;
	char			bank;

	psr = pio_readl(pio, PSR);
	osr = pio_readl(pio, OSR);
	imr = pio_readl(pio, IMR);
	pdsr = pio_readl(pio, PDSR);
	pusr = pio_readl(pio, PUSR);
	ifsr = pio_readl(pio, IFSR);
	mdsr = pio_readl(pio, MDSR);

	bank = 'A' + pio->pdev->id;

	for (i = 0, mask = 1; i < 32; i++, mask <<= 1) {
		const char *label;

		label = gpiochip_is_requested(chip, i);
		if (!label && (imr & mask))
			label = "[irq]";
		if (!label)
			continue;

		seq_printf(s, " gpio-%-3d P%c%-2d (%-12s) %s %s %s",
			chip->base + i, bank, i,
			label,
			(osr & mask) ? "out" : "in ",
			(mask & pdsr) ? "hi" : "lo",
			(mask & pusr) ? "  " : "up");
		if (ifsr & mask)
			seq_printf(s, " deglitch");
		if ((osr & mdsr) & mask)
			seq_printf(s, " open-drain");
		if (imr & mask)
			seq_printf(s, " irq-%d edge-both",
				gpio_to_irq(chip->base + i));
		seq_printf(s, "\n");
	}
}

#else
#define pio_bank_show	NULL
#endif


/*--------------------------------------------------------------------------*/

static int __init pio_probe(struct platform_device *pdev)
{
	struct pio_device *pio = NULL;
	int irq = platform_get_irq(pdev, 0);
	int gpio_irq_base = GPIO_IRQ_BASE + pdev->id * 32;

	BUG_ON(pdev->id >= MAX_NR_PIO_DEVICES);
	pio = &pio_dev[pdev->id];
	BUG_ON(!pio->regs);

	pio->chip.label = pio->name;
	pio->chip.base = pdev->id * 32;
	pio->chip.ngpio = 32;
	pio->chip.dev = &pdev->dev;
	pio->chip.owner = THIS_MODULE;

	pio->chip.direction_input = direction_input;
	pio->chip.get = gpio_get;
	pio->chip.direction_output = direction_output;
	pio->chip.set = gpio_set;
	pio->chip.dbg_show = pio_bank_show;

	gpiochip_add(&pio->chip);

	gpio_irq_setup(pio, irq, gpio_irq_base);

	platform_set_drvdata(pdev, pio);

	printk(KERN_DEBUG "%s: base 0x%p, irq %d chains %d..%d\n",
	       pio->name, pio->regs, irq, gpio_irq_base, gpio_irq_base + 31);

	return 0;
}

static struct platform_driver pio_driver = {
	.driver		= {
		.name		= "pio",
	},
};

static int __init pio_init(void)
{
	return platform_driver_probe(&pio_driver, pio_probe);
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

	/* start with irqs disabled and acked */
	pio_writel(pio, IDR, ~0UL);
	(void) pio_readl(pio, ISR);
}
