/*
 * Copyright (C) 2008, 2009 Provigent Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the ARM PrimeCell(tm) General Purpose Input/Output (PL061)
 *
 * Data sheet: ARM DDI 0190B, September 2000
 */
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl061.h>
#include <linux/slab.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm.h>

#define GPIODIR 0x400
#define GPIOIS  0x404
#define GPIOIBE 0x408
#define GPIOIEV 0x40C
#define GPIOIE  0x410
#define GPIORIS 0x414
#define GPIOMIS 0x418
#define GPIOIC  0x41C

#define PL061_GPIO_NR	8

#ifdef CONFIG_PM
struct pl061_context_save_regs {
	u8 gpio_data;
	u8 gpio_dir;
	u8 gpio_is;
	u8 gpio_ibe;
	u8 gpio_iev;
	u8 gpio_ie;
};
#endif

struct pl061_gpio {
	spinlock_t		lock;

	void __iomem		*base;
	struct irq_domain	*domain;
	struct gpio_chip	gc;

#ifdef CONFIG_PM
	struct pl061_context_save_regs csave_regs;
#endif
};

static int pl061_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	/*
	 * Map back to global GPIO space and request muxing, the direction
	 * parameter does not matter for this controller.
	 */
	int gpio = chip->base + offset;

	return pinctrl_request_gpio(gpio);
}

static void pl061_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;

	pinctrl_free_gpio(gpio);
}

static int pl061_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct pl061_gpio *chip = container_of(gc, struct pl061_gpio, gc);
	unsigned long flags;
	unsigned char gpiodir;

	if (offset >= gc->ngpio)
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);
	gpiodir = readb(chip->base + GPIODIR);
	gpiodir &= ~(1 << offset);
	writeb(gpiodir, chip->base + GPIODIR);
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int pl061_direction_output(struct gpio_chip *gc, unsigned offset,
		int value)
{
	struct pl061_gpio *chip = container_of(gc, struct pl061_gpio, gc);
	unsigned long flags;
	unsigned char gpiodir;

	if (offset >= gc->ngpio)
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);
	writeb(!!value << offset, chip->base + (1 << (offset + 2)));
	gpiodir = readb(chip->base + GPIODIR);
	gpiodir |= 1 << offset;
	writeb(gpiodir, chip->base + GPIODIR);

	/*
	 * gpio value is set again, because pl061 doesn't allow to set value of
	 * a gpio pin before configuring it in OUT mode.
	 */
	writeb(!!value << offset, chip->base + (1 << (offset + 2)));
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int pl061_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct pl061_gpio *chip = container_of(gc, struct pl061_gpio, gc);

	return !!readb(chip->base + (1 << (offset + 2)));
}

static void pl061_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	struct pl061_gpio *chip = container_of(gc, struct pl061_gpio, gc);

	writeb(!!value << offset, chip->base + (1 << (offset + 2)));
}

static int pl061_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct pl061_gpio *chip = container_of(gc, struct pl061_gpio, gc);

	return irq_create_mapping(chip->domain, offset);
}

static int pl061_irq_type(struct irq_data *d, unsigned trigger)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	int offset = irqd_to_hwirq(d);
	unsigned long flags;
	u8 gpiois, gpioibe, gpioiev;
	u8 bit = BIT(offset);

	if (offset < 0 || offset >= PL061_GPIO_NR)
		return -EINVAL;

	spin_lock_irqsave(&chip->lock, flags);

	gpioiev = readb(chip->base + GPIOIEV);
	gpiois = readb(chip->base + GPIOIS);
	gpioibe = readb(chip->base + GPIOIBE);

	if (trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		gpiois |= bit;
		if (trigger & IRQ_TYPE_LEVEL_HIGH)
			gpioiev |= bit;
		else
			gpioiev &= ~bit;
	} else
		gpiois &= ~bit;

	if ((trigger & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		/* Setting this makes GPIOEV be ignored */
		gpioibe |= bit;
	else {
		gpioibe &= ~bit;
		if (trigger & IRQ_TYPE_EDGE_RISING)
			gpioiev |= bit;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			gpioiev &= ~bit;
	}

	writeb(gpiois, chip->base + GPIOIS);
	writeb(gpioibe, chip->base + GPIOIBE);
	writeb(gpioiev, chip->base + GPIOIEV);

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static void pl061_irq_handler(unsigned irq, struct irq_desc *desc)
{
	unsigned long pending;
	int offset;
	struct pl061_gpio *chip = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);

	chained_irq_enter(irqchip, desc);

	pending = readb(chip->base + GPIOMIS);
	writeb(pending, chip->base + GPIOIC);
	if (pending) {
		for_each_set_bit(offset, &pending, PL061_GPIO_NR)
			generic_handle_irq(pl061_to_irq(&chip->gc, offset));
	}

	chained_irq_exit(irqchip, desc);
}

static void pl061_irq_mask(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	u8 mask = 1 << (irqd_to_hwirq(d) % PL061_GPIO_NR);
	u8 gpioie;

	spin_lock(&chip->lock);
	gpioie = readb(chip->base + GPIOIE) & ~mask;
	writeb(gpioie, chip->base + GPIOIE);
	spin_unlock(&chip->lock);
}

static void pl061_irq_unmask(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	u8 mask = 1 << (irqd_to_hwirq(d) % PL061_GPIO_NR);
	u8 gpioie;

	spin_lock(&chip->lock);
	gpioie = readb(chip->base + GPIOIE) | mask;
	writeb(gpioie, chip->base + GPIOIE);
	spin_unlock(&chip->lock);
}

static unsigned int pl061_irq_startup(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);

	if (gpio_lock_as_irq(&chip->gc, irqd_to_hwirq(d)))
		dev_err(chip->gc.dev,
			"unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
	pl061_irq_unmask(d);
	return 0;
}

static void pl061_irq_shutdown(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);

	pl061_irq_mask(d);
	gpio_unlock_as_irq(&chip->gc, irqd_to_hwirq(d));
}

static struct irq_chip pl061_irqchip = {
	.name		= "pl061 gpio",
	.irq_mask	= pl061_irq_mask,
	.irq_unmask	= pl061_irq_unmask,
	.irq_set_type	= pl061_irq_type,
	.irq_startup	= pl061_irq_startup,
	.irq_shutdown	= pl061_irq_shutdown,
};

static int pl061_irq_map(struct irq_domain *d, unsigned int irq,
			 irq_hw_number_t hwirq)
{
	struct pl061_gpio *chip = d->host_data;

	irq_set_chip_and_handler_name(irq, &pl061_irqchip, handle_simple_irq,
				      "pl061");
	irq_set_chip_data(irq, chip);
	irq_set_irq_type(irq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops pl061_domain_ops = {
	.map	= pl061_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int pl061_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct pl061_platform_data *pdata = dev_get_platdata(dev);
	struct pl061_gpio *chip;
	int ret, irq, i, irq_base;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	if (pdata) {
		chip->gc.base = pdata->gpio_base;
		irq_base = pdata->irq_base;
		if (irq_base <= 0) {
			dev_err(&adev->dev, "invalid IRQ base in pdata\n");
			return -ENODEV;
		}
	} else {
		chip->gc.base = -1;
		irq_base = 0;
	}

	if (!devm_request_mem_region(dev, adev->res.start,
				     resource_size(&adev->res), "pl061")) {
		dev_err(&adev->dev, "no memory region\n");
		return -EBUSY;
	}

	chip->base = devm_ioremap(dev, adev->res.start,
				  resource_size(&adev->res));
	if (!chip->base) {
		dev_err(&adev->dev, "could not remap memory\n");
		return -ENOMEM;
	}

	spin_lock_init(&chip->lock);

	chip->gc.request = pl061_gpio_request;
	chip->gc.free = pl061_gpio_free;
	chip->gc.direction_input = pl061_direction_input;
	chip->gc.direction_output = pl061_direction_output;
	chip->gc.get = pl061_get_value;
	chip->gc.set = pl061_set_value;
	chip->gc.to_irq = pl061_to_irq;
	chip->gc.ngpio = PL061_GPIO_NR;
	chip->gc.label = dev_name(dev);
	chip->gc.dev = dev;
	chip->gc.owner = THIS_MODULE;

	ret = gpiochip_add(&chip->gc);
	if (ret)
		return ret;

	/*
	 * irq_chip support
	 */
	writeb(0, chip->base + GPIOIE); /* disable irqs */
	irq = adev->irq[0];
	if (irq < 0) {
		dev_err(&adev->dev, "invalid IRQ\n");
		return -ENODEV;
	}

	irq_set_chained_handler(irq, pl061_irq_handler);
	irq_set_handler_data(irq, chip);

	chip->domain = irq_domain_add_simple(adev->dev.of_node, PL061_GPIO_NR,
					     irq_base, &pl061_domain_ops, chip);
	if (!chip->domain) {
		dev_err(&adev->dev, "no irq domain\n");
		return -ENODEV;
	}

	for (i = 0; i < PL061_GPIO_NR; i++) {
		if (pdata) {
			if (pdata->directions & (1 << i))
				pl061_direction_output(&chip->gc, i,
						pdata->values & (1 << i));
			else
				pl061_direction_input(&chip->gc, i);
		}
	}

	amba_set_drvdata(adev, chip);
	dev_info(&adev->dev, "PL061 GPIO chip @%08x registered\n",
		adev->res.start);

	return 0;
}

#ifdef CONFIG_PM
static int pl061_suspend(struct device *dev)
{
	struct pl061_gpio *chip = dev_get_drvdata(dev);
	int offset;

	chip->csave_regs.gpio_data = 0;
	chip->csave_regs.gpio_dir = readb(chip->base + GPIODIR);
	chip->csave_regs.gpio_is = readb(chip->base + GPIOIS);
	chip->csave_regs.gpio_ibe = readb(chip->base + GPIOIBE);
	chip->csave_regs.gpio_iev = readb(chip->base + GPIOIEV);
	chip->csave_regs.gpio_ie = readb(chip->base + GPIOIE);

	for (offset = 0; offset < PL061_GPIO_NR; offset++) {
		if (chip->csave_regs.gpio_dir & (1 << offset))
			chip->csave_regs.gpio_data |=
				pl061_get_value(&chip->gc, offset) << offset;
	}

	return 0;
}

static int pl061_resume(struct device *dev)
{
	struct pl061_gpio *chip = dev_get_drvdata(dev);
	int offset;

	for (offset = 0; offset < PL061_GPIO_NR; offset++) {
		if (chip->csave_regs.gpio_dir & (1 << offset))
			pl061_direction_output(&chip->gc, offset,
					chip->csave_regs.gpio_data &
					(1 << offset));
		else
			pl061_direction_input(&chip->gc, offset);
	}

	writeb(chip->csave_regs.gpio_is, chip->base + GPIOIS);
	writeb(chip->csave_regs.gpio_ibe, chip->base + GPIOIBE);
	writeb(chip->csave_regs.gpio_iev, chip->base + GPIOIEV);
	writeb(chip->csave_regs.gpio_ie, chip->base + GPIOIE);

	return 0;
}

static const struct dev_pm_ops pl061_dev_pm_ops = {
	.suspend = pl061_suspend,
	.resume = pl061_resume,
	.freeze = pl061_suspend,
	.restore = pl061_resume,
};
#endif

static struct amba_id pl061_ids[] = {
	{
		.id	= 0x00041061,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, pl061_ids);

static struct amba_driver pl061_gpio_driver = {
	.drv = {
		.name	= "pl061_gpio",
#ifdef CONFIG_PM
		.pm	= &pl061_dev_pm_ops,
#endif
	},
	.id_table	= pl061_ids,
	.probe		= pl061_probe,
};

static int __init pl061_gpio_init(void)
{
	return amba_driver_register(&pl061_gpio_driver);
}
module_init(pl061_gpio_init);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("PL061 GPIO driver");
MODULE_LICENSE("GPL");
