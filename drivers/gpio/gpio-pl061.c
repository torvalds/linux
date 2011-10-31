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
#include <linux/list.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl061.h>
#include <linux/slab.h>

#define GPIODIR 0x400
#define GPIOIS  0x404
#define GPIOIBE 0x408
#define GPIOIEV 0x40C
#define GPIOIE  0x410
#define GPIORIS 0x414
#define GPIOMIS 0x418
#define GPIOIC  0x41C

#define PL061_GPIO_NR	8

struct pl061_gpio {
	/* We use a list of pl061_gpio structs for each trigger IRQ in the main
	 * interrupts controller of the system. We need this to support systems
	 * in which more that one PL061s are connected to the same IRQ. The ISR
	 * interates through this list to find the source of the interrupt.
	 */
	struct list_head	list;

	/* Each of the two spinlocks protects a different set of hardware
	 * regiters and data structurs. This decouples the code of the IRQ from
	 * the GPIO code. This also makes the case of a GPIO routine call from
	 * the IRQ code simpler.
	 */
	spinlock_t		lock;		/* GPIO registers */
	spinlock_t		irq_lock;	/* IRQ registers */

	void __iomem		*base;
	unsigned		irq_base;
	struct gpio_chip	gc;
};

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

	if (chip->irq_base == NO_IRQ)
		return -EINVAL;

	return chip->irq_base + offset;
}

/*
 * PL061 GPIO IRQ
 */
static void pl061_irq_disable(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	int offset = d->irq - chip->irq_base;
	unsigned long flags;
	u8 gpioie;

	spin_lock_irqsave(&chip->irq_lock, flags);
	gpioie = readb(chip->base + GPIOIE);
	gpioie &= ~(1 << offset);
	writeb(gpioie, chip->base + GPIOIE);
	spin_unlock_irqrestore(&chip->irq_lock, flags);
}

static void pl061_irq_enable(struct irq_data *d)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	int offset = d->irq - chip->irq_base;
	unsigned long flags;
	u8 gpioie;

	spin_lock_irqsave(&chip->irq_lock, flags);
	gpioie = readb(chip->base + GPIOIE);
	gpioie |= 1 << offset;
	writeb(gpioie, chip->base + GPIOIE);
	spin_unlock_irqrestore(&chip->irq_lock, flags);
}

static int pl061_irq_type(struct irq_data *d, unsigned trigger)
{
	struct pl061_gpio *chip = irq_data_get_irq_chip_data(d);
	int offset = d->irq - chip->irq_base;
	unsigned long flags;
	u8 gpiois, gpioibe, gpioiev;

	if (offset < 0 || offset >= PL061_GPIO_NR)
		return -EINVAL;

	spin_lock_irqsave(&chip->irq_lock, flags);

	gpioiev = readb(chip->base + GPIOIEV);

	gpiois = readb(chip->base + GPIOIS);
	if (trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		gpiois |= 1 << offset;
		if (trigger & IRQ_TYPE_LEVEL_HIGH)
			gpioiev |= 1 << offset;
		else
			gpioiev &= ~(1 << offset);
	} else
		gpiois &= ~(1 << offset);
	writeb(gpiois, chip->base + GPIOIS);

	gpioibe = readb(chip->base + GPIOIBE);
	if ((trigger & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		gpioibe |= 1 << offset;
	else {
		gpioibe &= ~(1 << offset);
		if (trigger & IRQ_TYPE_EDGE_RISING)
			gpioiev |= 1 << offset;
		else if (trigger & IRQ_TYPE_EDGE_FALLING)
			gpioiev &= ~(1 << offset);
	}
	writeb(gpioibe, chip->base + GPIOIBE);

	writeb(gpioiev, chip->base + GPIOIEV);

	spin_unlock_irqrestore(&chip->irq_lock, flags);

	return 0;
}

static struct irq_chip pl061_irqchip = {
	.name		= "GPIO",
	.irq_enable	= pl061_irq_enable,
	.irq_disable	= pl061_irq_disable,
	.irq_set_type	= pl061_irq_type,
};

static void pl061_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct list_head *chip_list = irq_get_handler_data(irq);
	struct list_head *ptr;
	struct pl061_gpio *chip;

	desc->irq_data.chip->irq_ack(&desc->irq_data);
	list_for_each(ptr, chip_list) {
		unsigned long pending;
		int offset;

		chip = list_entry(ptr, struct pl061_gpio, list);
		pending = readb(chip->base + GPIOMIS);
		writeb(pending, chip->base + GPIOIC);

		if (pending == 0)
			continue;

		for_each_set_bit(offset, &pending, PL061_GPIO_NR)
			generic_handle_irq(pl061_to_irq(&chip->gc, offset));
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

static int pl061_probe(struct amba_device *dev, const struct amba_id *id)
{
	struct pl061_platform_data *pdata;
	struct pl061_gpio *chip;
	struct list_head *chip_list;
	int ret, irq, i;
	static DECLARE_BITMAP(init_irq, NR_IRQS);

	pdata = dev->dev.platform_data;
	if (pdata == NULL)
		return -ENODEV;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	pdata = dev->dev.platform_data;
	if (pdata) {
		chip->gc.base = pdata->gpio_base;
		chip->irq_base = pdata->irq_base;
	} else if (dev->dev.of_node) {
		chip->gc.base = -1;
		chip->irq_base = NO_IRQ;
	} else {
		ret = -ENODEV;
		goto free_mem;
	}

	if (!request_mem_region(dev->res.start,
				resource_size(&dev->res), "pl061")) {
		ret = -EBUSY;
		goto free_mem;
	}

	chip->base = ioremap(dev->res.start, resource_size(&dev->res));
	if (chip->base == NULL) {
		ret = -ENOMEM;
		goto release_region;
	}

	spin_lock_init(&chip->lock);
	spin_lock_init(&chip->irq_lock);
	INIT_LIST_HEAD(&chip->list);

	chip->gc.direction_input = pl061_direction_input;
	chip->gc.direction_output = pl061_direction_output;
	chip->gc.get = pl061_get_value;
	chip->gc.set = pl061_set_value;
	chip->gc.to_irq = pl061_to_irq;
	chip->gc.ngpio = PL061_GPIO_NR;
	chip->gc.label = dev_name(&dev->dev);
	chip->gc.dev = &dev->dev;
	chip->gc.owner = THIS_MODULE;

	ret = gpiochip_add(&chip->gc);
	if (ret)
		goto iounmap;

	/*
	 * irq_chip support
	 */

	if (chip->irq_base == NO_IRQ)
		return 0;

	writeb(0, chip->base + GPIOIE); /* disable irqs */
	irq = dev->irq[0];
	if (irq < 0) {
		ret = -ENODEV;
		goto iounmap;
	}
	irq_set_chained_handler(irq, pl061_irq_handler);
	if (!test_and_set_bit(irq, init_irq)) { /* list initialized? */
		chip_list = kmalloc(sizeof(*chip_list), GFP_KERNEL);
		if (chip_list == NULL) {
			clear_bit(irq, init_irq);
			ret = -ENOMEM;
			goto iounmap;
		}
		INIT_LIST_HEAD(chip_list);
		irq_set_handler_data(irq, chip_list);
	} else
		chip_list = irq_get_handler_data(irq);
	list_add(&chip->list, chip_list);

	for (i = 0; i < PL061_GPIO_NR; i++) {
		if (pdata) {
			if (pdata->directions & (1 << i))
				pl061_direction_output(&chip->gc, i,
						pdata->values & (1 << i));
			else
				pl061_direction_input(&chip->gc, i);
		}

		irq_set_chip_and_handler(i + chip->irq_base, &pl061_irqchip,
					 handle_simple_irq);
		set_irq_flags(i+chip->irq_base, IRQF_VALID);
		irq_set_chip_data(i + chip->irq_base, chip);
	}

	return 0;

iounmap:
	iounmap(chip->base);
release_region:
	release_mem_region(dev->res.start, resource_size(&dev->res));
free_mem:
	kfree(chip);

	return ret;
}

static struct amba_id pl061_ids[] = {
	{
		.id	= 0x00041061,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

static struct amba_driver pl061_gpio_driver = {
	.drv = {
		.name	= "pl061_gpio",
	},
	.id_table	= pl061_ids,
	.probe		= pl061_probe,
};

static int __init pl061_gpio_init(void)
{
	return amba_driver_register(&pl061_gpio_driver);
}
subsys_initcall(pl061_gpio_init);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("PL061 GPIO driver");
MODULE_LICENSE("GPL");
