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

#include <linux/device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/kernel.h>
#include <asm/div64.h>
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
	struct list_head list;		/* List of all GPT devices */
	struct device *dev;
	struct mpc52xx_gpt __iomem *regs;
	spinlock_t lock;
	struct irq_host *irqhost;
	u32 ipb_freq;

#if defined(CONFIG_GPIOLIB)
	struct of_gpio_chip of_gc;
#endif
};

LIST_HEAD(mpc52xx_gpt_list);
DEFINE_MUTEX(mpc52xx_gpt_list_mutex);

#define MPC52xx_GPT_MODE_MS_MASK	(0x07)
#define MPC52xx_GPT_MODE_MS_IC		(0x01)
#define MPC52xx_GPT_MODE_MS_OC		(0x02)
#define MPC52xx_GPT_MODE_MS_PWM		(0x03)
#define MPC52xx_GPT_MODE_MS_GPIO	(0x04)

#define MPC52xx_GPT_MODE_GPIO_MASK	(0x30)
#define MPC52xx_GPT_MODE_GPIO_OUT_LOW	(0x20)
#define MPC52xx_GPT_MODE_GPIO_OUT_HIGH	(0x30)

#define MPC52xx_GPT_MODE_COUNTER_ENABLE	(0x1000)
#define MPC52xx_GPT_MODE_CONTINUOUS	(0x0400)
#define MPC52xx_GPT_MODE_OPEN_DRAIN	(0x0200)
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

	if ((intsize < 1) || (intspec[0] > 3)) {
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
	u32 mode;

	cascade_virq = irq_of_parse_and_map(node, 0);
	if (!cascade_virq)
		return;

	gpt->irqhost = irq_alloc_host(node, IRQ_HOST_MAP_LINEAR, 1,
				      &mpc52xx_gpt_irq_ops, -1);
	if (!gpt->irqhost) {
		dev_err(gpt->dev, "irq_alloc_host() failed\n");
		return;
	}

	gpt->irqhost->host_data = gpt;
	set_irq_data(cascade_virq, gpt);
	set_irq_chained_handler(cascade_virq, mpc52xx_gpt_irq_cascade);

	/* If the GPT is currently disabled, then change it to be in Input
	 * Capture mode.  If the mode is non-zero, then the pin could be
	 * already in use for something. */
	spin_lock_irqsave(&gpt->lock, flags);
	mode = in_be32(&gpt->regs->mode);
	if ((mode & MPC52xx_GPT_MODE_MS_MASK) == 0)
		out_be32(&gpt->regs->mode, mode | MPC52xx_GPT_MODE_MS_IC);
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

/***********************************************************************
 * Timer API
 */

/**
 * mpc52xx_gpt_from_irq - Return the GPT device associated with an IRQ number
 * @irq: irq of timer.
 */
struct mpc52xx_gpt_priv *mpc52xx_gpt_from_irq(int irq)
{
	struct mpc52xx_gpt_priv *gpt;
	struct list_head *pos;

	/* Iterate over the list of timers looking for a matching device */
	mutex_lock(&mpc52xx_gpt_list_mutex);
	list_for_each(pos, &mpc52xx_gpt_list) {
		gpt = container_of(pos, struct mpc52xx_gpt_priv, list);
		if (gpt->irqhost && irq == irq_linear_revmap(gpt->irqhost, 0)) {
			mutex_unlock(&mpc52xx_gpt_list_mutex);
			return gpt;
		}
	}
	mutex_unlock(&mpc52xx_gpt_list_mutex);

	return NULL;
}
EXPORT_SYMBOL(mpc52xx_gpt_from_irq);

/**
 * mpc52xx_gpt_start_timer - Set and enable the GPT timer
 * @gpt: Pointer to gpt private data structure
 * @period: period of timer
 * @continuous: set to 1 to make timer continuous free running
 *
 * An interrupt will be generated every time the timer fires
 */
int mpc52xx_gpt_start_timer(struct mpc52xx_gpt_priv *gpt, int period,
                            int continuous)
{
	u32 clear, set;
	u64 clocks;
	u32 prescale;
	unsigned long flags;

	clear = MPC52xx_GPT_MODE_MS_MASK | MPC52xx_GPT_MODE_CONTINUOUS;
	set = MPC52xx_GPT_MODE_MS_GPIO | MPC52xx_GPT_MODE_COUNTER_ENABLE;
	if (continuous)
		set |= MPC52xx_GPT_MODE_CONTINUOUS;

	/* Determine the number of clocks in the requested period.  64 bit
	 * arithmatic is done here to preserve the precision until the value
	 * is scaled back down into the u32 range.  Period is in 'ns', bus
	 * frequency is in Hz. */
	clocks = (u64)period * (u64)gpt->ipb_freq;
	do_div(clocks, 1000000000); /* Scale it down to ns range */

	/* This device cannot handle a clock count greater than 32 bits */
	if (clocks > 0xffffffff)
		return -EINVAL;

	/* Calculate the prescaler and count values from the clocks value.
	 * 'clocks' is the number of clock ticks in the period.  The timer
	 * has 16 bit precision and a 16 bit prescaler.  Prescaler is
	 * calculated by integer dividing the clocks by 0x10000 (shifting
	 * down 16 bits) to obtain the smallest possible divisor for clocks
	 * to get a 16 bit count value.
	 *
	 * Note: the prescale register is '1' based, not '0' based.  ie. a
	 * value of '1' means divide the clock by one.  0xffff divides the
	 * clock by 0xffff.  '0x0000' does not divide by zero, but wraps
	 * around and divides by 0x10000.  That is why prescale must be
	 * a u32 variable, not a u16, for this calculation. */
	prescale = (clocks >> 16) + 1;
	do_div(clocks, prescale);
	if (clocks > 0xffff) {
		pr_err("calculation error; prescale:%x clocks:%llx\n",
		       prescale, clocks);
		return -EINVAL;
	}

	/* Set and enable the timer */
	spin_lock_irqsave(&gpt->lock, flags);
	out_be32(&gpt->regs->count, prescale << 16 | clocks);
	clrsetbits_be32(&gpt->regs->mode, clear, set);
	spin_unlock_irqrestore(&gpt->lock, flags);

	return 0;
}
EXPORT_SYMBOL(mpc52xx_gpt_start_timer);

void mpc52xx_gpt_stop_timer(struct mpc52xx_gpt_priv *gpt)
{
	clrbits32(&gpt->regs->mode, MPC52xx_GPT_MODE_COUNTER_ENABLE);
}
EXPORT_SYMBOL(mpc52xx_gpt_stop_timer);

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
	gpt->ipb_freq = mpc5xxx_get_bus_frequency(ofdev->node);
	gpt->regs = of_iomap(ofdev->node, 0);
	if (!gpt->regs) {
		kfree(gpt);
		return -ENOMEM;
	}

	dev_set_drvdata(&ofdev->dev, gpt);

	mpc52xx_gpt_gpio_setup(gpt, ofdev->node);
	mpc52xx_gpt_irq_setup(gpt, ofdev->node);

	mutex_lock(&mpc52xx_gpt_list_mutex);
	list_add(&gpt->list, &mpc52xx_gpt_list);
	mutex_unlock(&mpc52xx_gpt_list_mutex);

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
