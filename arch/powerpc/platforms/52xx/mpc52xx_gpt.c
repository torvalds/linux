/*
 * MPC5200 General Purpose Timer device driver
 *
 * Copyright (c) 2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This file is a driver for the the General Purpose Timer (gpt) devices
 * found on the MPC5200 SoC.  Each timer has an IO pin which can be used
 * for GPIO or can be used to raise interrupts.  The timer function can
 * be used independently from the IO pin, or it can be used to control
 * output signals or measure input signals.
 *
 * This driver supports the GPIO and IRQ controller functions of the GPT
 * device.  Timer functions are not yet supported, nor is the watchdog
 * timer.
 *
 * To use the GPIO function, the following two properties must be added
 * to the device tree node for the gpt device (typically in the .dts file
 * for the board):
 * 	gpio-controller;
 * 	#gpio-cells = < 2 >;
 * This driver will register the GPIO pin if it finds the gpio-controller
 * property in the device tree.
 *
 * To use the IRQ controller function, the following two properties must
 * be added to the device tree node for the gpt device:
 * 	interrupt-controller;
 * 	#interrupt-cells = < 1 >;
 * The IRQ controller binding only uses one cell to specify the interrupt,
 * and the IRQ flags are encoded in the cell.  A cell is not used to encode
 * the IRQ number because the GPT only has a single IRQ source.  For flags,
 * a value of '1' means rising edge sensitive and '2' means falling edge.
 *
 * The GPIO and the IRQ controller functions can be used at the same time,
 * but in this use case the IO line will only work as an input.  Trying to
 * use it as a GPIO output will not work.
 *
 * When using the GPIO line as an output, it can either be driven as normal
 * IO, or it can be an Open Collector (OC) output.  At the moment it is the
 * responsibility of either the bootloader or the platform setup code to set
 * the output mode.  This driver does not change the output mode setting.
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/kernel.h>
#include <asm/mpc52xx.h>

MODULE_DESCRIPTION("Freescale MPC52xx gpt driver");
MODULE_AUTHOR("Sascha Hauer, Grant Likely");
MODULE_LICENSE("GPL");

/**
 * struct mpc52xx_gpt - Private data structure for MPC52xx GPT driver
 * @dev: pointer to device structure
 * @regs: virtual address of GPT registers
 * @lock: spinlock to coordinate between different functions.
 * @of_gc: of_gpio_chip instance structure; used when GPIO is enabled
 * @irqhost: Pointer to irq_host instance; used when IRQ mode is supported
 */
struct mpc52xx_gpt_priv {
	struct device *dev;
	struct mpc52xx_gpt __iomem *regs;
	spinlock_t lock;
	struct irq_host *irqhost;

#if defined(CONFIG_GPIOLIB)
	struct of_gpio_chip of_gc;
#endif
};

#define MPC52xx_GPT_MODE_MS_MASK	(0x07)
#define MPC52xx_GPT_MODE_MS_IC		(0x01)
#define MPC52xx_GPT_MODE_MS_OC		(0x02)
#define MPC52xx_GPT_MODE_MS_PWM		(0x03)
#define MPC52xx_GPT_MODE_MS_GPIO	(0x04)

#define MPC52xx_GPT_MODE_GPIO_MASK	(0x30)
#define MPC52xx_GPT_MODE_GPIO_OUT_LOW	(0x20)
#define MPC52xx_GPT_MODE_GPIO_OUT_HIGH	(0x30)

#define MPC52xx_GPT_MODE_IRQ_EN		(0x0100)

#define MPC52xx_GPT_MODE_ICT_MASK	(0x030000)
#define MPC52xx_GPT_MODE_ICT_RISING	(0x010000)
#define MPC52xx_GPT_MODE_ICT_FALLING	(0x020000)
#define MPC52xx_GPT_MODE_ICT_TOGGLE	(0x030000)

#define MPC52xx_GPT_STATUS_IRQMASK	(0x000f)

/* ---------------------------------------------------------------------
 * Cascaded interrupt controller hooks
 */

static void mpc52xx_gpt_irq_unmask(unsigned int virq)
{
	struct mpc52xx_gpt_priv *gpt = get_irq_chip_data(virq);
	unsigned long flags;

	spin_lock_irqsave(&gpt->lock, flags);
	setbits32(&gpt->regs->mode, MPC52xx_GPT_MODE_IRQ_EN);
	spin_unlock_irqrestore(&gpt->lock, flags);
}

static void mpc52xx_gpt_irq_mask(unsigned int virq)
{
	struct mpc52xx_gpt_priv *gpt = get_irq_chip_data(virq);
	unsigned long flags;

	spin_lock_irqsave(&gpt->lock, flags);
	clrbits32(&gpt->regs->mode, MPC52xx_GPT_MODE_IRQ_EN);
	spin_unlock_irqrestore(&gpt->lock, flags);
}

static void mpc52xx_gpt_irq_ack(unsigned int virq)
{
	struct mpc52xx_gpt_priv *gpt = get_irq_chip_data(virq);

	out_be32(&gpt->regs->status, MPC52xx_GPT_STATUS_IRQMASK);
}

static int mpc52xx_gpt_irq_set_type(unsigned int virq, unsigned int flow_type)
{
	struct mpc52xx_gpt_priv *gpt = get_irq_chip_data(virq);
	unsigned long flags;
	u32 reg;

	dev_dbg(gpt->dev, "%s: virq=%i type=%x\n", __func__, virq, flow_type);

	spin_lock_irqsave(&gpt->lock, flags);
	reg = in_be32(&gpt->regs->mode) & ~MPC52xx_GPT_MODE_ICT_MASK;
	if (flow_type & IRQF_TRIGGER_RISING)
		reg |= MPC52xx_GPT_MODE_ICT_RISING;
	if (flow_type & IRQF_TRIGGER_FALLING)
		reg |= MPC52xx_GPT_MODE_ICT_FALLING;
	out_be32(&gpt->regs->mode, reg);
	spin_unlock_irqrestore(&gpt->lock, flags);

	return 0;
}

static struct irq_chip mpc52xx_gpt_irq_chip = {
	.typename = "MPC52xx GPT",
	.unmask = mpc52xx_gpt_irq_unmask,
	.mask = mpc52xx_gpt_irq_mask,
	.ack = mpc52xx_gpt_irq_ack,
	.set_type = mpc52xx_gpt_irq_set_type,
};

void mpc52xx_gpt_irq_cascade(unsigned int virq, struct irq_desc *desc)
{
	struct mpc52xx_gpt_priv *gpt = get_irq_data(virq);
	int sub_virq;
	u32 status;

	status = in_be32(&gpt->regs->status) & MPC52xx_GPT_STATUS_IRQMASK;
	if (status) {
		sub_virq = irq_linear_revmap(gpt->irqhost, 0);
		generic_handle_irq(sub_virq);
	}
}

static int mpc52xx_gpt_irq_map(struct irq_host *h, unsigned int virq,
			       irq_hw_number_t hw)
{
	struct mpc52xx_gpt_priv *gpt = h->host_data;

	dev_dbg(gpt->dev, "%s: h=%p, virq=%i\n", __func__, h, virq);
	set_irq_chip_data(virq, gpt);
	set_irq_chip_and_handler(virq, &mpc52xx_gpt_irq_chip, handle_edge_irq);

	return 0;
}

static int mpc52xx_gpt_irq_xlate(struct irq_host *h, struct device_node *ct,
				 u32 *intspec, unsigned int intsize,
				 irq_hw_number_t *out_hwirq,
				 unsigned int *out_flags)
{
	struct mpc52xx_gpt_priv *gpt = h->host_data;

	dev_dbg(gpt->dev, "%s: flags=%i\n", __func__, intspec[0]);

	if ((intsize < 1) || (intspec[0] < 1) || (intspec[0] > 3)) {
		dev_err(gpt->dev, "bad irq specifier in %s\n", ct->full_name);
		return -EINVAL;
	}

	*out_hwirq = 0; /* The GPT only has 1 IRQ line */
	*out_flags = intspec[0];

	return 0;
}

static struct irq_host_ops mpc52xx_gpt_irq_ops = {
	.map = mpc52xx_gpt_irq_map,
	.xlate = mpc52xx_gpt_irq_xlate,
};

static void
mpc52xx_gpt_irq_setup(struct mpc52xx_gpt_priv *gpt, struct device_node *node)
{
	int cascade_virq;
	unsigned long flags;

	/* Only setup cascaded IRQ if device tree claims the GPT is
	 * an interrupt controller */
	if (!of_find_property(node, "interrupt-controller", NULL))
		return;

	cascade_virq = irq_of_parse_and_map(node, 0);

	gpt->irqhost = irq_alloc_host(node, IRQ_HOST_MAP_LINEAR, 1,
				      &mpc52xx_gpt_irq_ops, -1);
	if (!gpt->irqhost) {
		dev_err(gpt->dev, "irq_alloc_host() failed\n");
		return;
	}

	gpt->irqhost->host_data = gpt;

	set_irq_data(cascade_virq, gpt);
	set_irq_chained_handler(cascade_virq, mpc52xx_gpt_irq_cascade);

	/* Set to Input Capture mode */
	spin_lock_irqsave(&gpt->lock, flags);
	clrsetbits_be32(&gpt->regs->mode, MPC52xx_GPT_MODE_MS_MASK,
			MPC52xx_GPT_MODE_MS_IC);
	spin_unlock_irqrestore(&gpt->lock, flags);

	dev_dbg(gpt->dev, "%s() complete. virq=%i\n", __func__, cascade_virq);
}


/* ---------------------------------------------------------------------
 * GPIOLIB hooks
 */
#if defined(CONFIG_GPIOLIB)
static inline struct mpc52xx_gpt_priv *gc_to_mpc52xx_gpt(struct gpio_chip *gc)
{
	return container_of(to_of_gpio_chip(gc), struct mpc52xx_gpt_priv,of_gc);
}

static int mpc52xx_gpt_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct mpc52xx_gpt_priv *gpt = gc_to_mpc52xx_gpt(gc);

	return (in_be32(&gpt->regs->status) >> 8) & 1;
}

static void
mpc52xx_gpt_gpio_set(struct gpio_chip *gc, unsigned int gpio, int v)
{
	struct mpc52xx_gpt_priv *gpt = gc_to_mpc52xx_gpt(gc);
	unsigned long flags;
	u32 r;

	dev_dbg(gpt->dev, "%s: gpio:%d v:%d\n", __func__, gpio, v);
	r = v ? MPC52xx_GPT_MODE_GPIO_OUT_HIGH : MPC52xx_GPT_MODE_GPIO_OUT_LOW;

	spin_lock_irqsave(&gpt->lock, flags);
	clrsetbits_be32(&gpt->regs->mode, MPC52xx_GPT_MODE_GPIO_MASK, r);
	spin_unlock_irqrestore(&gpt->lock, flags);
}

static int mpc52xx_gpt_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct mpc52xx_gpt_priv *gpt = gc_to_mpc52xx_gpt(gc);
	unsigned long flags;

	dev_dbg(gpt->dev, "%s: gpio:%d\n", __func__, gpio);

	spin_lock_irqsave(&gpt->lock, flags);
	clrbits32(&gpt->regs->mode, MPC52xx_GPT_MODE_GPIO_MASK);
	spin_unlock_irqrestore(&gpt->lock, flags);

	return 0;
}

static int
mpc52xx_gpt_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	mpc52xx_gpt_gpio_set(gc, gpio, val);
	return 0;
}

static void
mpc52xx_gpt_gpio_setup(struct mpc52xx_gpt_priv *gpt, struct device_node *node)
{
	int rc;

	/* Only setup GPIO if the device tree claims the GPT is
	 * a GPIO controller */
	if (!of_find_property(node, "gpio-controller", NULL))
		return;

	gpt->of_gc.gc.label = kstrdup(node->full_name, GFP_KERNEL);
	if (!gpt->of_gc.gc.label) {
		dev_err(gpt->dev, "out of memory\n");
		return;
	}

	gpt->of_gc.gpio_cells = 2;
	gpt->of_gc.gc.ngpio = 1;
	gpt->of_gc.gc.direction_input  = mpc52xx_gpt_gpio_dir_in;
	gpt->of_gc.gc.direction_output = mpc52xx_gpt_gpio_dir_out;
	gpt->of_gc.gc.get = mpc52xx_gpt_gpio_get;
	gpt->of_gc.gc.set = mpc52xx_gpt_gpio_set;
	gpt->of_gc.gc.base = -1;
	gpt->of_gc.xlate = of_gpio_simple_xlate;
	node->data = &gpt->of_gc;
	of_node_get(node);

	/* Setup external pin in GPIO mode */
	clrsetbits_be32(&gpt->regs->mode, MPC52xx_GPT_MODE_MS_MASK,
			MPC52xx_GPT_MODE_MS_GPIO);

	rc = gpiochip_add(&gpt->of_gc.gc);
	if (rc)
		dev_err(gpt->dev, "gpiochip_add() failed; rc=%i\n", rc);

	dev_dbg(gpt->dev, "%s() complete.\n", __func__);
}
#else /* defined(CONFIG_GPIOLIB) */
static void
mpc52xx_gpt_gpio_setup(struct mpc52xx_gpt_priv *p, struct device_node *np) { }
#endif /* defined(CONFIG_GPIOLIB) */

/* ---------------------------------------------------------------------
 * of_platform bus binding code
 */
static int __devinit mpc52xx_gpt_probe(struct of_device *ofdev,
				       const struct of_device_id *match)
{
	struct mpc52xx_gpt_priv *gpt;

	gpt = kzalloc(sizeof *gpt, GFP_KERNEL);
	if (!gpt)
		return -ENOMEM;

	spin_lock_init(&gpt->lock);
	gpt->dev = &ofdev->dev;
	gpt->regs = of_iomap(ofdev->node, 0);
	if (!gpt->regs) {
		kfree(gpt);
		return -ENOMEM;
	}

	dev_set_drvdata(&ofdev->dev, gpt);

	mpc52xx_gpt_gpio_setup(gpt, ofdev->node);
	mpc52xx_gpt_irq_setup(gpt, ofdev->node);

	return 0;
}

static int mpc52xx_gpt_remove(struct of_device *ofdev)
{
	return -EBUSY;
}

static const struct of_device_id mpc52xx_gpt_match[] = {
	{ .compatible = "fsl,mpc5200-gpt", },

	/* Depreciated compatible values; don't use for new dts files */
	{ .compatible = "fsl,mpc5200-gpt-gpio", },
	{ .compatible = "mpc5200-gpt", },
	{}
};

static struct of_platform_driver mpc52xx_gpt_driver = {
	.name = "mpc52xx-gpt",
	.match_table = mpc52xx_gpt_match,
	.probe = mpc52xx_gpt_probe,
	.remove = mpc52xx_gpt_remove,
};

static int __init mpc52xx_gpt_init(void)
{
	if (of_register_platform_driver(&mpc52xx_gpt_driver))
		pr_err("error registering MPC52xx GPT driver\n");

	return 0;
}

/* Make sure GPIOs and IRQs get set up before anyone tries to use them */
subsys_initcall(mpc52xx_gpt_init);
