/*
 * GPIOs on MPC8349/8572/8610 and compatible
 *
 * Copyright (C) 2008 Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/irq.h>

#define MPC8XXX_GPIO_PINS	32

#define GPIO_DIR		0x00
#define GPIO_ODR		0x04
#define GPIO_DAT		0x08
#define GPIO_IER		0x0c
#define GPIO_IMR		0x10
#define GPIO_ICR		0x14

struct mpc8xxx_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

	/*
	 * shadowed data register to be able to clear/set output pins in
	 * open drain mode safely
	 */
	u32 data;
	struct irq_host *irq;
};

static inline u32 mpc8xxx_gpio2mask(unsigned int gpio)
{
	return 1u << (MPC8XXX_GPIO_PINS - 1 - gpio);
}

static inline struct mpc8xxx_gpio_chip *
to_mpc8xxx_gpio_chip(struct of_mm_gpio_chip *mm)
{
	return container_of(mm, struct mpc8xxx_gpio_chip, mm_gc);
}

static void mpc8xxx_gpio_save_regs(struct of_mm_gpio_chip *mm)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);

	mpc8xxx_gc->data = in_be32(mm->regs + GPIO_DAT);
}

/* Workaround GPIO 1 errata on MPC8572/MPC8536. The status of GPIOs
 * defined as output cannot be determined by reading GPDAT register,
 * so we use shadow data register instead. The status of input pins
 * is determined by reading GPDAT register.
 */
static int mpc8572_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	u32 val;
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);

	val = in_be32(mm->regs + GPIO_DAT) & ~in_be32(mm->regs + GPIO_DIR);

	return (val | mpc8xxx_gc->data) & mpc8xxx_gpio2mask(gpio);
}

static int mpc8xxx_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);

	return in_be32(mm->regs + GPIO_DAT) & mpc8xxx_gpio2mask(gpio);
}

static void mpc8xxx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;

	spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	if (val)
		mpc8xxx_gc->data |= mpc8xxx_gpio2mask(gpio);
	else
		mpc8xxx_gc->data &= ~mpc8xxx_gpio2mask(gpio);

	out_be32(mm->regs + GPIO_DAT, mpc8xxx_gc->data);

	spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static int mpc8xxx_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;

	spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	clrbits32(mm->regs + GPIO_DIR, mpc8xxx_gpio2mask(gpio));

	spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);

	return 0;
}

static int mpc8xxx_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;

	mpc8xxx_gpio_set(gc, gpio, val);

	spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	setbits32(mm->regs + GPIO_DIR, mpc8xxx_gpio2mask(gpio));

	spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);

	return 0;
}

static int mpc8xxx_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);

	if (mpc8xxx_gc->irq && offset < MPC8XXX_GPIO_PINS)
		return irq_create_mapping(mpc8xxx_gc->irq, offset);
	else
		return -ENXIO;
}

static void mpc8xxx_gpio_irq_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = get_irq_desc_data(desc);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned int mask;

	mask = in_be32(mm->regs + GPIO_IER) & in_be32(mm->regs + GPIO_IMR);
	if (mask)
		generic_handle_irq(irq_linear_revmap(mpc8xxx_gc->irq,
						     32 - ffs(mask)));
}

static void mpc8xxx_irq_unmask(unsigned int virq)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = get_irq_chip_data(virq);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	setbits32(mm->regs + GPIO_IMR, mpc8xxx_gpio2mask(virq_to_hw(virq)));

	spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_irq_mask(unsigned int virq)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = get_irq_chip_data(virq);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	clrbits32(mm->regs + GPIO_IMR, mpc8xxx_gpio2mask(virq_to_hw(virq)));

	spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_irq_ack(unsigned int virq)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = get_irq_chip_data(virq);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;

	out_be32(mm->regs + GPIO_IER, mpc8xxx_gpio2mask(virq_to_hw(virq)));
}

static int mpc8xxx_irq_set_type(unsigned int virq, unsigned int flow_type)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = get_irq_chip_data(virq);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
		spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		setbits32(mm->regs + GPIO_ICR,
			  mpc8xxx_gpio2mask(virq_to_hw(virq)));
		spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		clrbits32(mm->regs + GPIO_ICR,
			  mpc8xxx_gpio2mask(virq_to_hw(virq)));
		spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip mpc8xxx_irq_chip = {
	.name		= "mpc8xxx-gpio",
	.unmask		= mpc8xxx_irq_unmask,
	.mask		= mpc8xxx_irq_mask,
	.ack		= mpc8xxx_irq_ack,
	.set_type	= mpc8xxx_irq_set_type,
};

static int mpc8xxx_gpio_irq_map(struct irq_host *h, unsigned int virq,
				irq_hw_number_t hw)
{
	set_irq_chip_data(virq, h->host_data);
	set_irq_chip_and_handler(virq, &mpc8xxx_irq_chip, handle_level_irq);
	set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int mpc8xxx_gpio_irq_xlate(struct irq_host *h, struct device_node *ct,
				  const u32 *intspec, unsigned int intsize,
				  irq_hw_number_t *out_hwirq,
				  unsigned int *out_flags)

{
	/* interrupt sense values coming from the device tree equal either
	 * EDGE_FALLING or EDGE_BOTH
	 */
	*out_hwirq = intspec[0];
	*out_flags = intspec[1];

	return 0;
}

static struct irq_host_ops mpc8xxx_gpio_irq_ops = {
	.map	= mpc8xxx_gpio_irq_map,
	.xlate	= mpc8xxx_gpio_irq_xlate,
};

static void __init mpc8xxx_add_controller(struct device_node *np)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct gpio_chip *gc;
	unsigned hwirq;
	int ret;

	mpc8xxx_gc = kzalloc(sizeof(*mpc8xxx_gc), GFP_KERNEL);
	if (!mpc8xxx_gc) {
		ret = -ENOMEM;
		goto err;
	}

	spin_lock_init(&mpc8xxx_gc->lock);

	mm_gc = &mpc8xxx_gc->mm_gc;
	gc = &mm_gc->gc;

	mm_gc->save_regs = mpc8xxx_gpio_save_regs;
	gc->ngpio = MPC8XXX_GPIO_PINS;
	gc->direction_input = mpc8xxx_gpio_dir_in;
	gc->direction_output = mpc8xxx_gpio_dir_out;
	if (of_device_is_compatible(np, "fsl,mpc8572-gpio"))
		gc->get = mpc8572_gpio_get;
	else
		gc->get = mpc8xxx_gpio_get;
	gc->set = mpc8xxx_gpio_set;
	gc->to_irq = mpc8xxx_gpio_to_irq;

	ret = of_mm_gpiochip_add(np, mm_gc);
	if (ret)
		goto err;

	hwirq = irq_of_parse_and_map(np, 0);
	if (hwirq == NO_IRQ)
		goto skip_irq;

	mpc8xxx_gc->irq =
		irq_alloc_host(np, IRQ_HOST_MAP_LINEAR, MPC8XXX_GPIO_PINS,
			       &mpc8xxx_gpio_irq_ops, MPC8XXX_GPIO_PINS);
	if (!mpc8xxx_gc->irq)
		goto skip_irq;

	mpc8xxx_gc->irq->host_data = mpc8xxx_gc;

	/* ack and mask all irqs */
	out_be32(mm_gc->regs + GPIO_IER, 0xffffffff);
	out_be32(mm_gc->regs + GPIO_IMR, 0);

	set_irq_data(hwirq, mpc8xxx_gc);
	set_irq_chained_handler(hwirq, mpc8xxx_gpio_irq_cascade);

skip_irq:
	return;

err:
	pr_err("%s: registration failed with status %d\n",
	       np->full_name, ret);
	kfree(mpc8xxx_gc);

	return;
}

static int __init mpc8xxx_add_gpiochips(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "fsl,mpc8349-gpio")
		mpc8xxx_add_controller(np);

	for_each_compatible_node(np, NULL, "fsl,mpc8572-gpio")
		mpc8xxx_add_controller(np);

	for_each_compatible_node(np, NULL, "fsl,mpc8610-gpio")
		mpc8xxx_add_controller(np);

	for_each_compatible_node(np, NULL, "fsl,qoriq-gpio")
		mpc8xxx_add_controller(np);

	return 0;
}
arch_initcall(mpc8xxx_add_gpiochips);
