// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2008, 2009 Provigent Ltd.
 *
 * Author: Baruch Siach <baruch@tkos.co.il>
 *
 * Driver for the ARM PrimeCell(tm) General Purpose Input/Output (PL061)
 *
 * Data sheet: ARM DDI 0190B, September 2000
 */
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
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

struct pl061 {
	raw_spinlock_t		lock;

	void __iomem		*base;
	struct gpio_chip	gc;
	struct irq_chip		irq_chip;
	int			parent_irq;

#ifdef CONFIG_PM
	struct pl061_context_save_regs csave_regs;
#endif
};

static int pl061_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct pl061 *pl061 = gpiochip_get_data(gc);

	return !(readb(pl061->base + GPIODIR) & BIT(offset));
}

static int pl061_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct pl061 *pl061 = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned char gpiodir;

	raw_spin_lock_irqsave(&pl061->lock, flags);
	gpiodir = readb(pl061->base + GPIODIR);
	gpiodir &= ~(BIT(offset));
	writeb(gpiodir, pl061->base + GPIODIR);
	raw_spin_unlock_irqrestore(&pl061->lock, flags);

	return 0;
}

static int pl061_direction_output(struct gpio_chip *gc, unsigned offset,
		int value)
{
	struct pl061 *pl061 = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned char gpiodir;

	raw_spin_lock_irqsave(&pl061->lock, flags);
	writeb(!!value << offset, pl061->base + (BIT(offset + 2)));
	gpiodir = readb(pl061->base + GPIODIR);
	gpiodir |= BIT(offset);
	writeb(gpiodir, pl061->base + GPIODIR);

	/*
	 * gpio value is set again, because pl061 doesn't allow to set value of
	 * a gpio pin before configuring it in OUT mode.
	 */
	writeb(!!value << offset, pl061->base + (BIT(offset + 2)));
	raw_spin_unlock_irqrestore(&pl061->lock, flags);

	return 0;
}

static int pl061_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct pl061 *pl061 = gpiochip_get_data(gc);

	return !!readb(pl061->base + (BIT(offset + 2)));
}

static void pl061_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	struct pl061 *pl061 = gpiochip_get_data(gc);

	writeb(!!value << offset, pl061->base + (BIT(offset + 2)));
}

static int pl061_irq_type(struct irq_data *d, unsigned trigger)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d);
	unsigned long flags;
	u8 gpiois, gpioibe, gpioiev;
	u8 bit = BIT(offset);

	if (offset < 0 || offset >= PL061_GPIO_NR)
		return -EINVAL;

	if ((trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) &&
	    (trigger & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)))
	{
		dev_err(gc->parent,
			"trying to configure line %d for both level and edge "
			"detection, choose one!\n",
			offset);
		return -EINVAL;
	}


	raw_spin_lock_irqsave(&pl061->lock, flags);

	gpioiev = readb(pl061->base + GPIOIEV);
	gpiois = readb(pl061->base + GPIOIS);
	gpioibe = readb(pl061->base + GPIOIBE);

	if (trigger & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		bool polarity = trigger & IRQ_TYPE_LEVEL_HIGH;

		/* Disable edge detection */
		gpioibe &= ~bit;
		/* Enable level detection */
		gpiois |= bit;
		/* Select polarity */
		if (polarity)
			gpioiev |= bit;
		else
			gpioiev &= ~bit;
		irq_set_handler_locked(d, handle_level_irq);
		dev_dbg(gc->parent, "line %d: IRQ on %s level\n",
			offset,
			polarity ? "HIGH" : "LOW");
	} else if ((trigger & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		/* Disable level detection */
		gpiois &= ~bit;
		/* Select both edges, setting this makes GPIOEV be ignored */
		gpioibe |= bit;
		irq_set_handler_locked(d, handle_edge_irq);
		dev_dbg(gc->parent, "line %d: IRQ on both edges\n", offset);
	} else if ((trigger & IRQ_TYPE_EDGE_RISING) ||
		   (trigger & IRQ_TYPE_EDGE_FALLING)) {
		bool rising = trigger & IRQ_TYPE_EDGE_RISING;

		/* Disable level detection */
		gpiois &= ~bit;
		/* Clear detection on both edges */
		gpioibe &= ~bit;
		/* Select edge */
		if (rising)
			gpioiev |= bit;
		else
			gpioiev &= ~bit;
		irq_set_handler_locked(d, handle_edge_irq);
		dev_dbg(gc->parent, "line %d: IRQ on %s edge\n",
			offset,
			rising ? "RISING" : "FALLING");
	} else {
		/* No trigger: disable everything */
		gpiois &= ~bit;
		gpioibe &= ~bit;
		gpioiev &= ~bit;
		irq_set_handler_locked(d, handle_bad_irq);
		dev_warn(gc->parent, "no trigger selected for line %d\n",
			 offset);
	}

	writeb(gpiois, pl061->base + GPIOIS);
	writeb(gpioibe, pl061->base + GPIOIBE);
	writeb(gpioiev, pl061->base + GPIOIEV);

	raw_spin_unlock_irqrestore(&pl061->lock, flags);

	return 0;
}

static void pl061_irq_handler(struct irq_desc *desc)
{
	unsigned long pending;
	int offset;
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);

	chained_irq_enter(irqchip, desc);

	pending = readb(pl061->base + GPIOMIS);
	if (pending) {
		for_each_set_bit(offset, &pending, PL061_GPIO_NR)
			generic_handle_irq(irq_find_mapping(gc->irq.domain,
							    offset));
	}

	chained_irq_exit(irqchip, desc);
}

static void pl061_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	u8 mask = BIT(irqd_to_hwirq(d) % PL061_GPIO_NR);
	u8 gpioie;

	raw_spin_lock(&pl061->lock);
	gpioie = readb(pl061->base + GPIOIE) & ~mask;
	writeb(gpioie, pl061->base + GPIOIE);
	raw_spin_unlock(&pl061->lock);
}

static void pl061_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	u8 mask = BIT(irqd_to_hwirq(d) % PL061_GPIO_NR);
	u8 gpioie;

	raw_spin_lock(&pl061->lock);
	gpioie = readb(pl061->base + GPIOIE) | mask;
	writeb(gpioie, pl061->base + GPIOIE);
	raw_spin_unlock(&pl061->lock);
}

/**
 * pl061_irq_ack() - ACK an edge IRQ
 * @d: IRQ data for this IRQ
 *
 * This gets called from the edge IRQ handler to ACK the edge IRQ
 * in the GPIOIC (interrupt-clear) register. For level IRQs this is
 * not needed: these go away when the level signal goes away.
 */
static void pl061_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pl061 *pl061 = gpiochip_get_data(gc);
	u8 mask = BIT(irqd_to_hwirq(d) % PL061_GPIO_NR);

	raw_spin_lock(&pl061->lock);
	writeb(mask, pl061->base + GPIOIC);
	raw_spin_unlock(&pl061->lock);
}

static int pl061_irq_set_wake(struct irq_data *d, unsigned int state)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct pl061 *pl061 = gpiochip_get_data(gc);

	return irq_set_irq_wake(pl061->parent_irq, state);
}

static int pl061_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device *dev = &adev->dev;
	struct pl061 *pl061;
	int ret, irq;

	pl061 = devm_kzalloc(dev, sizeof(*pl061), GFP_KERNEL);
	if (pl061 == NULL)
		return -ENOMEM;

	pl061->base = devm_ioremap_resource(dev, &adev->res);
	if (IS_ERR(pl061->base))
		return PTR_ERR(pl061->base);

	raw_spin_lock_init(&pl061->lock);
	if (of_property_read_bool(dev->of_node, "gpio-ranges")) {
		pl061->gc.request = gpiochip_generic_request;
		pl061->gc.free = gpiochip_generic_free;
	}

	pl061->gc.base = -1;
	pl061->gc.get_direction = pl061_get_direction;
	pl061->gc.direction_input = pl061_direction_input;
	pl061->gc.direction_output = pl061_direction_output;
	pl061->gc.get = pl061_get_value;
	pl061->gc.set = pl061_set_value;
	pl061->gc.ngpio = PL061_GPIO_NR;
	pl061->gc.label = dev_name(dev);
	pl061->gc.parent = dev;
	pl061->gc.owner = THIS_MODULE;

	ret = gpiochip_add_data(&pl061->gc, pl061);
	if (ret)
		return ret;

	/*
	 * irq_chip support
	 */
	pl061->irq_chip.name = dev_name(dev);
	pl061->irq_chip.irq_ack	= pl061_irq_ack;
	pl061->irq_chip.irq_mask = pl061_irq_mask;
	pl061->irq_chip.irq_unmask = pl061_irq_unmask;
	pl061->irq_chip.irq_set_type = pl061_irq_type;
	pl061->irq_chip.irq_set_wake = pl061_irq_set_wake;

	writeb(0, pl061->base + GPIOIE); /* disable irqs */
	irq = adev->irq[0];
	if (irq < 0) {
		dev_err(&adev->dev, "invalid IRQ\n");
		return -ENODEV;
	}
	pl061->parent_irq = irq;

	ret = gpiochip_irqchip_add(&pl061->gc, &pl061->irq_chip,
				   0, handle_bad_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_info(&adev->dev, "could not add irqchip\n");
		return ret;
	}
	gpiochip_set_chained_irqchip(&pl061->gc, &pl061->irq_chip,
				     irq, pl061_irq_handler);

	amba_set_drvdata(adev, pl061);
	dev_info(&adev->dev, "PL061 GPIO chip @%pa registered\n",
		 &adev->res.start);

	return 0;
}

#ifdef CONFIG_PM
static int pl061_suspend(struct device *dev)
{
	struct pl061 *pl061 = dev_get_drvdata(dev);
	int offset;

	pl061->csave_regs.gpio_data = 0;
	pl061->csave_regs.gpio_dir = readb(pl061->base + GPIODIR);
	pl061->csave_regs.gpio_is = readb(pl061->base + GPIOIS);
	pl061->csave_regs.gpio_ibe = readb(pl061->base + GPIOIBE);
	pl061->csave_regs.gpio_iev = readb(pl061->base + GPIOIEV);
	pl061->csave_regs.gpio_ie = readb(pl061->base + GPIOIE);

	for (offset = 0; offset < PL061_GPIO_NR; offset++) {
		if (pl061->csave_regs.gpio_dir & (BIT(offset)))
			pl061->csave_regs.gpio_data |=
				pl061_get_value(&pl061->gc, offset) << offset;
	}

	return 0;
}

static int pl061_resume(struct device *dev)
{
	struct pl061 *pl061 = dev_get_drvdata(dev);
	int offset;

	for (offset = 0; offset < PL061_GPIO_NR; offset++) {
		if (pl061->csave_regs.gpio_dir & (BIT(offset)))
			pl061_direction_output(&pl061->gc, offset,
					pl061->csave_regs.gpio_data &
					(BIT(offset)));
		else
			pl061_direction_input(&pl061->gc, offset);
	}

	writeb(pl061->csave_regs.gpio_is, pl061->base + GPIOIS);
	writeb(pl061->csave_regs.gpio_ibe, pl061->base + GPIOIBE);
	writeb(pl061->csave_regs.gpio_iev, pl061->base + GPIOIEV);
	writeb(pl061->csave_regs.gpio_ie, pl061->base + GPIOIE);

	return 0;
}

static const struct dev_pm_ops pl061_dev_pm_ops = {
	.suspend = pl061_suspend,
	.resume = pl061_resume,
	.freeze = pl061_suspend,
	.restore = pl061_resume,
};
#endif

static const struct amba_id pl061_ids[] = {
	{
		.id	= 0x00041061,
		.mask	= 0x000fffff,
	},
	{ 0, 0 },
};

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
device_initcall(pl061_gpio_init);
