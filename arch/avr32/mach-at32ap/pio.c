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

#include <asm/io.h>

#include <asm/arch/portmux.h>

#include "pio.h"

#define MAX_NR_PIO_DEVICES		8

struct pio_device {
	void __iomem *regs;
	const struct platform_device *pdev;
	struct clk *clk;
	u32 pinmux_mask;
	char name[32];
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

	pio_writel(pio, PUER, mask);
	if (flags & AT32_GPIOF_HIGH)
		pio_writel(pio, SODR, mask);
	else
		pio_writel(pio, CODR, mask);
	if (flags & AT32_GPIOF_OUTPUT)
		pio_writel(pio, OER, mask);
	else
		pio_writel(pio, ODR, mask);

	pio_writel(pio, PER, mask);
	if (!(flags & AT32_GPIOF_PULLUP))
		pio_writel(pio, PUDR, mask);

	return;

fail:
	dump_stack();
}

static int __init pio_probe(struct platform_device *pdev)
{
	struct pio_device *pio = NULL;

	BUG_ON(pdev->id >= MAX_NR_PIO_DEVICES);
	pio = &pio_dev[pdev->id];
	BUG_ON(!pio->regs);

	/* TODO: Interrupts */

	platform_set_drvdata(pdev, pio);

	printk(KERN_INFO "%s: Atmel Port Multiplexer at 0x%p (irq %d)\n",
	       pio->name, pio->regs, platform_get_irq(pdev, 0));

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
subsys_initcall(pio_init);

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

	pio_writel(pio, ODR, ~0UL);
	pio_writel(pio, PER, ~0UL);
}
