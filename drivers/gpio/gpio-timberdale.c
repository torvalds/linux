// SPDX-License-Identifier: GPL-2.0-only
/*
 * Timberdale FPGA GPIO driver
 * Author: Mocean Laboratories
 * Copyright (c) 2009 Intel Corporation
 */

/* Supports:
 * Timberdale FPGA GPIO
 */

#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/timb_gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#define DRIVER_NAME "timb-gpio"

#define TGPIOVAL	0x00
#define TGPIODIR	0x04
#define TGPIO_IER	0x08
#define TGPIO_ISR	0x0c
#define TGPIO_IPR	0x10
#define TGPIO_ICR	0x14
#define TGPIO_FLR	0x18
#define TGPIO_LVR	0x1c
#define TGPIO_VER	0x20
#define TGPIO_BFLR	0x24

struct timbgpio {
	void __iomem		*membase;
	spinlock_t		lock; /* mutual exclusion */
	struct gpio_chip	gpio;
	int			irq_base;
	unsigned long		last_ier;
};

static int timbgpio_update_bit(struct gpio_chip *gpio, unsigned index,
	unsigned offset, bool enabled)
{
	struct timbgpio *tgpio = gpiochip_get_data(gpio);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&tgpio->lock, flags);
	reg = ioread32(tgpio->membase + offset);

	if (enabled)
		reg |= (1 << index);
	else
		reg &= ~(1 << index);

	iowrite32(reg, tgpio->membase + offset);
	spin_unlock_irqrestore(&tgpio->lock, flags);

	return 0;
}

static int timbgpio_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	return timbgpio_update_bit(gpio, nr, TGPIODIR, true);
}

static int timbgpio_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct timbgpio *tgpio = gpiochip_get_data(gpio);
	u32 value;

	value = ioread32(tgpio->membase + TGPIOVAL);
	return (value & (1 << nr)) ? 1 : 0;
}

static int timbgpio_gpio_direction_output(struct gpio_chip *gpio,
						unsigned nr, int val)
{
	return timbgpio_update_bit(gpio, nr, TGPIODIR, false);
}

static void timbgpio_gpio_set(struct gpio_chip *gpio,
				unsigned nr, int val)
{
	timbgpio_update_bit(gpio, nr, TGPIOVAL, val != 0);
}

static int timbgpio_to_irq(struct gpio_chip *gpio, unsigned offset)
{
	struct timbgpio *tgpio = gpiochip_get_data(gpio);

	if (tgpio->irq_base <= 0)
		return -EINVAL;

	return tgpio->irq_base + offset;
}

/*
 * GPIO IRQ
 */
static void timbgpio_irq_disable(struct irq_data *d)
{
	struct timbgpio *tgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - tgpio->irq_base;
	unsigned long flags;

	spin_lock_irqsave(&tgpio->lock, flags);
	tgpio->last_ier &= ~(1UL << offset);
	iowrite32(tgpio->last_ier, tgpio->membase + TGPIO_IER);
	spin_unlock_irqrestore(&tgpio->lock, flags);
}

static void timbgpio_irq_enable(struct irq_data *d)
{
	struct timbgpio *tgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - tgpio->irq_base;
	unsigned long flags;

	spin_lock_irqsave(&tgpio->lock, flags);
	tgpio->last_ier |= 1UL << offset;
	iowrite32(tgpio->last_ier, tgpio->membase + TGPIO_IER);
	spin_unlock_irqrestore(&tgpio->lock, flags);
}

static int timbgpio_irq_type(struct irq_data *d, unsigned trigger)
{
	struct timbgpio *tgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - tgpio->irq_base;
	unsigned long flags;
	u32 lvr, flr, bflr = 0;
	u32 ver;
	int ret = 0;

	if (offset < 0 || offset > tgpio->gpio.ngpio)
		return -EINVAL;

	ver = ioread32(tgpio->membase + TGPIO_VER);

	spin_lock_irqsave(&tgpio->lock, flags);

	lvr = ioread32(tgpio->membase + TGPIO_LVR);
	flr = ioread32(tgpio->membase + TGPIO_FLR);
	if (ver > 2)
		bflr = ioread32(tgpio->membase + TGPIO_BFLR);

	if (trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		bflr &= ~(1 << offset);
		flr &= ~(1 << offset);
		if (trigger & IRQ_TYPE_LEVEL_HIGH)
			lvr |= 1 << offset;
		else
			lvr &= ~(1 << offset);
	}

	if ((trigger & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		if (ver < 3) {
			ret = -EINVAL;
			goto out;
		} else {
			flr |= 1 << offset;
			bflr |= 1 << offset;
		}
	} else {
		bflr &= ~(1 << offset);
		flr |= 1 << offset;
		if (trigger & IRQ_TYPE_EDGE_FALLING)
			lvr &= ~(1 << offset);
		else
			lvr |= 1 << offset;
	}

	iowrite32(lvr, tgpio->membase + TGPIO_LVR);
	iowrite32(flr, tgpio->membase + TGPIO_FLR);
	if (ver > 2)
		iowrite32(bflr, tgpio->membase + TGPIO_BFLR);

	iowrite32(1 << offset, tgpio->membase + TGPIO_ICR);

out:
	spin_unlock_irqrestore(&tgpio->lock, flags);
	return ret;
}

static void timbgpio_irq(struct irq_desc *desc)
{
	struct timbgpio *tgpio = irq_desc_get_handler_data(desc);
	struct irq_data *data = irq_desc_get_irq_data(desc);
	unsigned long ipr;
	int offset;

	data->chip->irq_ack(data);
	ipr = ioread32(tgpio->membase + TGPIO_IPR);
	iowrite32(ipr, tgpio->membase + TGPIO_ICR);

	/*
	 * Some versions of the hardware trash the IER register if more than
	 * one interrupt is received simultaneously.
	 */
	iowrite32(0, tgpio->membase + TGPIO_IER);

	for_each_set_bit(offset, &ipr, tgpio->gpio.ngpio)
		generic_handle_irq(timbgpio_to_irq(&tgpio->gpio, offset));

	iowrite32(tgpio->last_ier, tgpio->membase + TGPIO_IER);
}

static struct irq_chip timbgpio_irqchip = {
	.name		= "GPIO",
	.irq_enable	= timbgpio_irq_enable,
	.irq_disable	= timbgpio_irq_disable,
	.irq_set_type	= timbgpio_irq_type,
};

static int timbgpio_probe(struct platform_device *pdev)
{
	int err, i;
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	struct timbgpio *tgpio;
	struct timbgpio_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int irq = platform_get_irq(pdev, 0);

	if (!pdata || pdata->nr_pins > 32) {
		dev_err(dev, "Invalid platform data\n");
		return -EINVAL;
	}

	tgpio = devm_kzalloc(dev, sizeof(*tgpio), GFP_KERNEL);
	if (!tgpio)
		return -EINVAL;

	tgpio->irq_base = pdata->irq_base;

	spin_lock_init(&tgpio->lock);

	tgpio->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tgpio->membase))
		return PTR_ERR(tgpio->membase);

	gc = &tgpio->gpio;

	gc->label = dev_name(&pdev->dev);
	gc->owner = THIS_MODULE;
	gc->parent = &pdev->dev;
	gc->direction_input = timbgpio_gpio_direction_input;
	gc->get = timbgpio_gpio_get;
	gc->direction_output = timbgpio_gpio_direction_output;
	gc->set = timbgpio_gpio_set;
	gc->to_irq = (irq >= 0 && tgpio->irq_base > 0) ? timbgpio_to_irq : NULL;
	gc->dbg_show = NULL;
	gc->base = pdata->gpio_base;
	gc->ngpio = pdata->nr_pins;
	gc->can_sleep = false;

	err = devm_gpiochip_add_data(&pdev->dev, gc, tgpio);
	if (err)
		return err;

	/* make sure to disable interrupts */
	iowrite32(0x0, tgpio->membase + TGPIO_IER);

	if (irq < 0 || tgpio->irq_base <= 0)
		return 0;

	for (i = 0; i < pdata->nr_pins; i++) {
		irq_set_chip_and_handler(tgpio->irq_base + i,
			&timbgpio_irqchip, handle_simple_irq);
		irq_set_chip_data(tgpio->irq_base + i, tgpio);
		irq_clear_status_flags(tgpio->irq_base + i, IRQ_NOREQUEST | IRQ_NOPROBE);
	}

	irq_set_chained_handler_and_data(irq, timbgpio_irq, tgpio);

	return 0;
}

static struct platform_driver timbgpio_platform_driver = {
	.driver = {
		.name			= DRIVER_NAME,
		.suppress_bind_attrs	= true,
	},
	.probe		= timbgpio_probe,
};

/*--------------------------------------------------------------------------*/

builtin_platform_driver(timbgpio_platform_driver);
