/*
 * timbgpio.c timberdale FPGA GPIO driver
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA GPIO
 */

#include <linux/module.h>
#include <linux/gpio.h>
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
};

static int timbgpio_update_bit(struct gpio_chip *gpio, unsigned index,
	unsigned offset, bool enabled)
{
	struct timbgpio *tgpio = container_of(gpio, struct timbgpio, gpio);
	u32 reg;

	spin_lock(&tgpio->lock);
	reg = ioread32(tgpio->membase + offset);

	if (enabled)
		reg |= (1 << index);
	else
		reg &= ~(1 << index);

	iowrite32(reg, tgpio->membase + offset);
	spin_unlock(&tgpio->lock);

	return 0;
}

static int timbgpio_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	return timbgpio_update_bit(gpio, nr, TGPIODIR, true);
}

static int timbgpio_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct timbgpio *tgpio = container_of(gpio, struct timbgpio, gpio);
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
	struct timbgpio *tgpio = container_of(gpio, struct timbgpio, gpio);

	if (tgpio->irq_base <= 0)
		return -EINVAL;

	return tgpio->irq_base + offset;
}

/*
 * GPIO IRQ
 */
static void timbgpio_irq_disable(unsigned irq)
{
	struct timbgpio *tgpio = get_irq_chip_data(irq);
	int offset = irq - tgpio->irq_base;

	timbgpio_update_bit(&tgpio->gpio, offset, TGPIO_IER, 0);
}

static void timbgpio_irq_enable(unsigned irq)
{
	struct timbgpio *tgpio = get_irq_chip_data(irq);
	int offset = irq - tgpio->irq_base;

	timbgpio_update_bit(&tgpio->gpio, offset, TGPIO_IER, 1);
}

static int timbgpio_irq_type(unsigned irq, unsigned trigger)
{
	struct timbgpio *tgpio = get_irq_chip_data(irq);
	int offset = irq - tgpio->irq_base;
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
		}
		else {
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

static void timbgpio_irq(unsigned int irq, struct irq_desc *desc)
{
	struct timbgpio *tgpio = get_irq_data(irq);
	unsigned long ipr;
	int offset;

	desc->chip->ack(irq);
	ipr = ioread32(tgpio->membase + TGPIO_IPR);
	iowrite32(ipr, tgpio->membase + TGPIO_ICR);

	for_each_set_bit(offset, &ipr, tgpio->gpio.ngpio)
		generic_handle_irq(timbgpio_to_irq(&tgpio->gpio, offset));
}

static struct irq_chip timbgpio_irqchip = {
	.name		= "GPIO",
	.enable		= timbgpio_irq_enable,
	.disable	= timbgpio_irq_disable,
	.set_type	= timbgpio_irq_type,
};

static int __devinit timbgpio_probe(struct platform_device *pdev)
{
	int err, i;
	struct gpio_chip *gc;
	struct timbgpio *tgpio;
	struct resource *iomem;
	struct timbgpio_platform_data *pdata = pdev->dev.platform_data;
	int irq = platform_get_irq(pdev, 0);

	if (!pdata || pdata->nr_pins > 32) {
		err = -EINVAL;
		goto err_mem;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		err = -EINVAL;
		goto err_mem;
	}

	tgpio = kzalloc(sizeof(*tgpio), GFP_KERNEL);
	if (!tgpio) {
		err = -EINVAL;
		goto err_mem;
	}
	tgpio->irq_base = pdata->irq_base;

	spin_lock_init(&tgpio->lock);

	if (!request_mem_region(iomem->start, resource_size(iomem),
		DRIVER_NAME)) {
		err = -EBUSY;
		goto err_request;
	}

	tgpio->membase = ioremap(iomem->start, resource_size(iomem));
	if (!tgpio->membase) {
		err = -ENOMEM;
		goto err_ioremap;
	}

	gc = &tgpio->gpio;

	gc->label = dev_name(&pdev->dev);
	gc->owner = THIS_MODULE;
	gc->dev = &pdev->dev;
	gc->direction_input = timbgpio_gpio_direction_input;
	gc->get = timbgpio_gpio_get;
	gc->direction_output = timbgpio_gpio_direction_output;
	gc->set = timbgpio_gpio_set;
	gc->to_irq = (irq >= 0 && tgpio->irq_base > 0) ? timbgpio_to_irq : NULL;
	gc->dbg_show = NULL;
	gc->base = pdata->gpio_base;
	gc->ngpio = pdata->nr_pins;
	gc->can_sleep = 0;

	err = gpiochip_add(gc);
	if (err)
		goto err_chipadd;

	platform_set_drvdata(pdev, tgpio);

	/* make sure to disable interrupts */
	iowrite32(0x0, tgpio->membase + TGPIO_IER);

	if (irq < 0 || tgpio->irq_base <= 0)
		return 0;

	for (i = 0; i < pdata->nr_pins; i++) {
		set_irq_chip_and_handler_name(tgpio->irq_base + i,
			&timbgpio_irqchip, handle_simple_irq, "mux");
		set_irq_chip_data(tgpio->irq_base + i, tgpio);
#ifdef CONFIG_ARM
		set_irq_flags(tgpio->irq_base + i, IRQF_VALID | IRQF_PROBE);
#endif
	}

	set_irq_data(irq, tgpio);
	set_irq_chained_handler(irq, timbgpio_irq);

	return 0;

err_chipadd:
	iounmap(tgpio->membase);
err_ioremap:
	release_mem_region(iomem->start, resource_size(iomem));
err_request:
	kfree(tgpio);
err_mem:
	printk(KERN_ERR DRIVER_NAME": Failed to register GPIOs: %d\n", err);

	return err;
}

static int __devexit timbgpio_remove(struct platform_device *pdev)
{
	int err;
	struct timbgpio_platform_data *pdata = pdev->dev.platform_data;
	struct timbgpio *tgpio = platform_get_drvdata(pdev);
	struct resource *iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int irq = platform_get_irq(pdev, 0);

	if (irq >= 0 && tgpio->irq_base > 0) {
		int i;
		for (i = 0; i < pdata->nr_pins; i++) {
			set_irq_chip(tgpio->irq_base + i, NULL);
			set_irq_chip_data(tgpio->irq_base + i, NULL);
		}

		set_irq_handler(irq, NULL);
		set_irq_data(irq, NULL);
	}

	err = gpiochip_remove(&tgpio->gpio);
	if (err)
		printk(KERN_ERR DRIVER_NAME": failed to remove gpio_chip\n");

	iounmap(tgpio->membase);
	release_mem_region(iomem->start, resource_size(iomem));
	kfree(tgpio);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver timbgpio_platform_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= timbgpio_probe,
	.remove		= timbgpio_remove,
};

/*--------------------------------------------------------------------------*/

static int __init timbgpio_init(void)
{
	return platform_driver_register(&timbgpio_platform_driver);
}

static void __exit timbgpio_exit(void)
{
	platform_driver_unregister(&timbgpio_platform_driver);
}

module_init(timbgpio_init);
module_exit(timbgpio_exit);

MODULE_DESCRIPTION("Timberdale GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mocean Laboratories");
MODULE_ALIAS("platform:"DRIVER_NAME);

