// SPDX-License-Identifier: GPL-2.0+
//
// MXC GPIO support. (c) 2008 Daniel Mack <daniel@caiaq.de>
// Copyright 2008 Juergen Beisert, kernel@pengutronix.de
//
// Based on code from Freescale,
// Copyright (C) 2004-2010 Freescale Semiconductor, Inc. All Rights Reserved.

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
/* FIXME: for gpio_get_value(), replace this by direct register read */
#include <linux/gpio.h>
#include <linux/module.h>

#define MXS_SET		0x4
#define MXS_CLR		0x8

#define PINCTRL_DOUT(p)		((is_imx23_gpio(p) ? 0x0500 : 0x0700) + (p->id) * 0x10)
#define PINCTRL_DIN(p)		((is_imx23_gpio(p) ? 0x0600 : 0x0900) + (p->id) * 0x10)
#define PINCTRL_DOE(p)		((is_imx23_gpio(p) ? 0x0700 : 0x0b00) + (p->id) * 0x10)
#define PINCTRL_PIN2IRQ(p)	((is_imx23_gpio(p) ? 0x0800 : 0x1000) + (p->id) * 0x10)
#define PINCTRL_IRQEN(p)	((is_imx23_gpio(p) ? 0x0900 : 0x1100) + (p->id) * 0x10)
#define PINCTRL_IRQLEV(p)	((is_imx23_gpio(p) ? 0x0a00 : 0x1200) + (p->id) * 0x10)
#define PINCTRL_IRQPOL(p)	((is_imx23_gpio(p) ? 0x0b00 : 0x1300) + (p->id) * 0x10)
#define PINCTRL_IRQSTAT(p)	((is_imx23_gpio(p) ? 0x0c00 : 0x1400) + (p->id) * 0x10)

#define GPIO_INT_FALL_EDGE	0x0
#define GPIO_INT_LOW_LEV	0x1
#define GPIO_INT_RISE_EDGE	0x2
#define GPIO_INT_HIGH_LEV	0x3
#define GPIO_INT_LEV_MASK	(1 << 0)
#define GPIO_INT_POL_MASK	(1 << 1)

enum mxs_gpio_id {
	IMX23_GPIO,
	IMX28_GPIO,
};

struct mxs_gpio_port {
	void __iomem *base;
	int id;
	int irq;
	struct irq_domain *domain;
	struct gpio_chip gc;
	struct device *dev;
	enum mxs_gpio_id devid;
	u32 both_edges;
};

static inline int is_imx23_gpio(struct mxs_gpio_port *port)
{
	return port->devid == IMX23_GPIO;
}

static inline int is_imx28_gpio(struct mxs_gpio_port *port)
{
	return port->devid == IMX28_GPIO;
}

/* Note: This driver assumes 32 GPIOs are handled in one register */

static int mxs_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	u32 val;
	u32 pin_mask = 1 << d->hwirq;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct mxs_gpio_port *port = gc->private;
	void __iomem *pin_addr;
	int edge;

	if (!(ct->type & type))
		if (irq_setup_alt_chip(d, type))
			return -EINVAL;

	port->both_edges &= ~pin_mask;
	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		val = gpio_get_value(port->gc.base + d->hwirq);
		if (val)
			edge = GPIO_INT_FALL_EDGE;
		else
			edge = GPIO_INT_RISE_EDGE;
		port->both_edges |= pin_mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		edge = GPIO_INT_RISE_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		edge = GPIO_INT_FALL_EDGE;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		edge = GPIO_INT_LOW_LEV;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		edge = GPIO_INT_HIGH_LEV;
		break;
	default:
		return -EINVAL;
	}

	/* set level or edge */
	pin_addr = port->base + PINCTRL_IRQLEV(port);
	if (edge & GPIO_INT_LEV_MASK) {
		writel(pin_mask, pin_addr + MXS_SET);
		writel(pin_mask, port->base + PINCTRL_IRQEN(port) + MXS_SET);
	} else {
		writel(pin_mask, pin_addr + MXS_CLR);
		writel(pin_mask, port->base + PINCTRL_PIN2IRQ(port) + MXS_SET);
	}

	/* set polarity */
	pin_addr = port->base + PINCTRL_IRQPOL(port);
	if (edge & GPIO_INT_POL_MASK)
		writel(pin_mask, pin_addr + MXS_SET);
	else
		writel(pin_mask, pin_addr + MXS_CLR);

	writel(pin_mask,
	       port->base + PINCTRL_IRQSTAT(port) + MXS_CLR);

	return 0;
}

static void mxs_flip_edge(struct mxs_gpio_port *port, u32 gpio)
{
	u32 bit, val, edge;
	void __iomem *pin_addr;

	bit = 1 << gpio;

	pin_addr = port->base + PINCTRL_IRQPOL(port);
	val = readl(pin_addr);
	edge = val & bit;

	if (edge)
		writel(bit, pin_addr + MXS_CLR);
	else
		writel(bit, pin_addr + MXS_SET);
}

/* MXS has one interrupt *per* gpio port */
static void mxs_gpio_irq_handler(struct irq_desc *desc)
{
	u32 irq_stat;
	struct mxs_gpio_port *port = irq_desc_get_handler_data(desc);

	desc->irq_data.chip->irq_ack(&desc->irq_data);

	irq_stat = readl(port->base + PINCTRL_IRQSTAT(port)) &
			readl(port->base + PINCTRL_IRQEN(port));

	while (irq_stat != 0) {
		int irqoffset = fls(irq_stat) - 1;
		if (port->both_edges & (1 << irqoffset))
			mxs_flip_edge(port, irqoffset);

		generic_handle_irq(irq_find_mapping(port->domain, irqoffset));
		irq_stat &= ~(1 << irqoffset);
	}
}

/*
 * Set interrupt number "irq" in the GPIO as a wake-up source.
 * While system is running, all registered GPIO interrupts need to have
 * wake-up enabled. When system is suspended, only selected GPIO interrupts
 * need to have wake-up enabled.
 * @param  irq          interrupt source number
 * @param  enable       enable as wake-up if equal to non-zero
 * @return       This function returns 0 on success.
 */
static int mxs_gpio_set_wake_irq(struct irq_data *d, unsigned int enable)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct mxs_gpio_port *port = gc->private;

	if (enable)
		enable_irq_wake(port->irq);
	else
		disable_irq_wake(port->irq);

	return 0;
}

static int mxs_gpio_init_gc(struct mxs_gpio_port *port, int irq_base)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int rv;

	gc = devm_irq_alloc_generic_chip(port->dev, "gpio-mxs", 2, irq_base,
					 port->base, handle_level_irq);
	if (!gc)
		return -ENOMEM;

	gc->private = port;

	ct = &gc->chip_types[0];
	ct->type = IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_set_type = mxs_gpio_set_irq_type;
	ct->chip.irq_set_wake = mxs_gpio_set_wake_irq;
	ct->chip.flags = IRQCHIP_SET_TYPE_MASKED;
	ct->regs.ack = PINCTRL_IRQSTAT(port) + MXS_CLR;
	ct->regs.enable = PINCTRL_PIN2IRQ(port) + MXS_SET;
	ct->regs.disable = PINCTRL_PIN2IRQ(port) + MXS_CLR;

	ct = &gc->chip_types[1];
	ct->type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	ct->chip.irq_ack = irq_gc_ack_set_bit;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_set_type = mxs_gpio_set_irq_type;
	ct->chip.irq_set_wake = mxs_gpio_set_wake_irq;
	ct->chip.flags = IRQCHIP_SET_TYPE_MASKED;
	ct->regs.ack = PINCTRL_IRQSTAT(port) + MXS_CLR;
	ct->regs.enable = PINCTRL_IRQEN(port) + MXS_SET;
	ct->regs.disable = PINCTRL_IRQEN(port) + MXS_CLR;
	ct->handler = handle_level_irq;

	rv = devm_irq_setup_generic_chip(port->dev, gc, IRQ_MSK(32),
					 IRQ_GC_INIT_NESTED_LOCK,
					 IRQ_NOREQUEST, 0);

	return rv;
}

static int mxs_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct mxs_gpio_port *port = gpiochip_get_data(gc);

	return irq_find_mapping(port->domain, offset);
}

static int mxs_gpio_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct mxs_gpio_port *port = gpiochip_get_data(gc);
	u32 mask = 1 << offset;
	u32 dir;

	dir = readl(port->base + PINCTRL_DOE(port));
	return !(dir & mask);
}

static const struct platform_device_id mxs_gpio_ids[] = {
	{
		.name = "imx23-gpio",
		.driver_data = IMX23_GPIO,
	}, {
		.name = "imx28-gpio",
		.driver_data = IMX28_GPIO,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, mxs_gpio_ids);

static const struct of_device_id mxs_gpio_dt_ids[] = {
	{ .compatible = "fsl,imx23-gpio", .data = (void *) IMX23_GPIO, },
	{ .compatible = "fsl,imx28-gpio", .data = (void *) IMX28_GPIO, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_gpio_dt_ids);

static int mxs_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *parent;
	static void __iomem *base;
	struct mxs_gpio_port *port;
	int irq_base;
	int err;

	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->id = of_alias_get_id(np, "gpio");
	if (port->id < 0)
		return port->id;
	port->devid = (enum mxs_gpio_id)of_device_get_match_data(&pdev->dev);
	port->dev = &pdev->dev;
	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return port->irq;

	/*
	 * map memory region only once, as all the gpio ports
	 * share the same one
	 */
	if (!base) {
		parent = of_get_parent(np);
		base = of_iomap(parent, 0);
		of_node_put(parent);
		if (!base)
			return -EADDRNOTAVAIL;
	}
	port->base = base;

	/* initially disable the interrupts */
	writel(0, port->base + PINCTRL_PIN2IRQ(port));
	writel(0, port->base + PINCTRL_IRQEN(port));

	/* clear address has to be used to clear IRQSTAT bits */
	writel(~0U, port->base + PINCTRL_IRQSTAT(port) + MXS_CLR);

	irq_base = devm_irq_alloc_descs(&pdev->dev, -1, 0, 32, numa_node_id());
	if (irq_base < 0) {
		err = irq_base;
		goto out_iounmap;
	}

	port->domain = irq_domain_add_legacy(np, 32, irq_base, 0,
					     &irq_domain_simple_ops, NULL);
	if (!port->domain) {
		err = -ENODEV;
		goto out_iounmap;
	}

	/* gpio-mxs can be a generic irq chip */
	err = mxs_gpio_init_gc(port, irq_base);
	if (err < 0)
		goto out_irqdomain_remove;

	/* setup one handler for each entry */
	irq_set_chained_handler_and_data(port->irq, mxs_gpio_irq_handler,
					 port);

	err = bgpio_init(&port->gc, &pdev->dev, 4,
			 port->base + PINCTRL_DIN(port),
			 port->base + PINCTRL_DOUT(port) + MXS_SET,
			 port->base + PINCTRL_DOUT(port) + MXS_CLR,
			 port->base + PINCTRL_DOE(port), NULL, 0);
	if (err)
		goto out_irqdomain_remove;

	port->gc.to_irq = mxs_gpio_to_irq;
	port->gc.get_direction = mxs_gpio_get_direction;
	port->gc.base = port->id * 32;

	err = gpiochip_add_data(&port->gc, port);
	if (err)
		goto out_irqdomain_remove;

	return 0;

out_irqdomain_remove:
	irq_domain_remove(port->domain);
out_iounmap:
	iounmap(port->base);
	return err;
}

static struct platform_driver mxs_gpio_driver = {
	.driver		= {
		.name	= "gpio-mxs",
		.of_match_table = mxs_gpio_dt_ids,
		.suppress_bind_attrs = true,
	},
	.probe		= mxs_gpio_probe,
	.id_table	= mxs_gpio_ids,
};

static int __init mxs_gpio_init(void)
{
	return platform_driver_register(&mxs_gpio_driver);
}
postcore_initcall(mxs_gpio_init);

MODULE_AUTHOR("Freescale Semiconductor, "
	      "Daniel Mack <danielncaiaq.de>, "
	      "Juergen Beisert <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Freescale MXS GPIO");
MODULE_LICENSE("GPL");
