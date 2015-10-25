/*
 * GPIOs on MPC512x/8349/8572/8610 and compatible
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
#include <linux/of_irq.h>
#include <linux/of_platform.h>
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
#define GPIO_ICR2		0x18

struct mpc8xxx_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	raw_spinlock_t lock;

	/*
	 * shadowed data register to be able to clear/set output pins in
	 * open drain mode safely
	 */
	u32 data;
	struct irq_domain *irq;
	unsigned int irqn;
	const void *of_dev_id_data;
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
	u32 out_mask, out_shadow;

	out_mask = in_be32(mm->regs + GPIO_DIR);

	val = in_be32(mm->regs + GPIO_DAT) & ~out_mask;
	out_shadow = mpc8xxx_gc->data & out_mask;

	return (val | out_shadow) & mpc8xxx_gpio2mask(gpio);
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

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	if (val)
		mpc8xxx_gc->data |= mpc8xxx_gpio2mask(gpio);
	else
		mpc8xxx_gc->data &= ~mpc8xxx_gpio2mask(gpio);

	out_be32(mm->regs + GPIO_DAT, mpc8xxx_gc->data);

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_gpio_set_multiple(struct gpio_chip *gc,
				      unsigned long *mask, unsigned long *bits)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	for (i = 0; i < gc->ngpio; i++) {
		if (*mask == 0)
			break;
		if (__test_and_clear_bit(i, mask)) {
			if (test_bit(i, bits))
				mpc8xxx_gc->data |= mpc8xxx_gpio2mask(i);
			else
				mpc8xxx_gc->data &= ~mpc8xxx_gpio2mask(i);
		}
	}

	out_be32(mm->regs + GPIO_DAT, mpc8xxx_gc->data);

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static int mpc8xxx_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	clrbits32(mm->regs + GPIO_DIR, mpc8xxx_gpio2mask(gpio));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);

	return 0;
}

static int mpc8xxx_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm = to_of_mm_gpio_chip(gc);
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = to_mpc8xxx_gpio_chip(mm);
	unsigned long flags;

	mpc8xxx_gpio_set(gc, gpio, val);

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	setbits32(mm->regs + GPIO_DIR, mpc8xxx_gpio2mask(gpio));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);

	return 0;
}

static int mpc5121_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	/* GPIO 28..31 are input only on MPC5121 */
	if (gpio >= 28)
		return -EINVAL;

	return mpc8xxx_gpio_dir_out(gc, gpio, val);
}

static int mpc5125_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	/* GPIO 0..3 are input only on MPC5125 */
	if (gpio <= 3)
		return -EINVAL;

	return mpc8xxx_gpio_dir_out(gc, gpio, val);
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

static void mpc8xxx_gpio_irq_cascade(struct irq_desc *desc)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned int mask;

	mask = in_be32(mm->regs + GPIO_IER) & in_be32(mm->regs + GPIO_IMR);
	if (mask)
		generic_handle_irq(irq_linear_revmap(mpc8xxx_gc->irq,
						     32 - ffs(mask)));
	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

static void mpc8xxx_irq_unmask(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	setbits32(mm->regs + GPIO_IMR, mpc8xxx_gpio2mask(irqd_to_hwirq(d)));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_irq_mask(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);

	clrbits32(mm->regs + GPIO_IMR, mpc8xxx_gpio2mask(irqd_to_hwirq(d)));

	raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
}

static void mpc8xxx_irq_ack(struct irq_data *d)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;

	out_be32(mm->regs + GPIO_IER, mpc8xxx_gpio2mask(irqd_to_hwirq(d)));
}

static int mpc8xxx_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long flags;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		setbits32(mm->regs + GPIO_ICR,
			  mpc8xxx_gpio2mask(irqd_to_hwirq(d)));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		clrbits32(mm->regs + GPIO_ICR,
			  mpc8xxx_gpio2mask(irqd_to_hwirq(d)));
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int mpc512x_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = irq_data_get_irq_chip_data(d);
	struct of_mm_gpio_chip *mm = &mpc8xxx_gc->mm_gc;
	unsigned long gpio = irqd_to_hwirq(d);
	void __iomem *reg;
	unsigned int shift;
	unsigned long flags;

	if (gpio < 16) {
		reg = mm->regs + GPIO_ICR;
		shift = (15 - gpio) * 2;
	} else {
		reg = mm->regs + GPIO_ICR2;
		shift = (15 - (gpio % 16)) * 2;
	}

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_LEVEL_LOW:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		clrsetbits_be32(reg, 3 << shift, 2 << shift);
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_LEVEL_HIGH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		clrsetbits_be32(reg, 3 << shift, 1 << shift);
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		raw_spin_lock_irqsave(&mpc8xxx_gc->lock, flags);
		clrbits32(reg, 3 << shift);
		raw_spin_unlock_irqrestore(&mpc8xxx_gc->lock, flags);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip mpc8xxx_irq_chip = {
	.name		= "mpc8xxx-gpio",
	.irq_unmask	= mpc8xxx_irq_unmask,
	.irq_mask	= mpc8xxx_irq_mask,
	.irq_ack	= mpc8xxx_irq_ack,
	/* this might get overwritten in mpc8xxx_probe() */
	.irq_set_type	= mpc8xxx_irq_set_type,
};

static int mpc8xxx_gpio_irq_map(struct irq_domain *h, unsigned int irq,
				irq_hw_number_t hwirq)
{
	irq_set_chip_data(irq, h->host_data);
	irq_set_chip_and_handler(irq, &mpc8xxx_irq_chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops mpc8xxx_gpio_irq_ops = {
	.map	= mpc8xxx_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

struct mpc8xxx_gpio_devtype {
	int (*gpio_dir_out)(struct gpio_chip *, unsigned int, int);
	int (*gpio_get)(struct gpio_chip *, unsigned int);
	int (*irq_set_type)(struct irq_data *, unsigned int);
};

static const struct mpc8xxx_gpio_devtype mpc512x_gpio_devtype = {
	.gpio_dir_out = mpc5121_gpio_dir_out,
	.irq_set_type = mpc512x_irq_set_type,
};

static const struct mpc8xxx_gpio_devtype mpc5125_gpio_devtype = {
	.gpio_dir_out = mpc5125_gpio_dir_out,
	.irq_set_type = mpc512x_irq_set_type,
};

static const struct mpc8xxx_gpio_devtype mpc8572_gpio_devtype = {
	.gpio_get = mpc8572_gpio_get,
};

static const struct mpc8xxx_gpio_devtype mpc8xxx_gpio_devtype_default = {
	.gpio_dir_out = mpc8xxx_gpio_dir_out,
	.gpio_get = mpc8xxx_gpio_get,
	.irq_set_type = mpc8xxx_irq_set_type,
};

static const struct of_device_id mpc8xxx_gpio_ids[] = {
	{ .compatible = "fsl,mpc8349-gpio", },
	{ .compatible = "fsl,mpc8572-gpio", .data = &mpc8572_gpio_devtype, },
	{ .compatible = "fsl,mpc8610-gpio", },
	{ .compatible = "fsl,mpc5121-gpio", .data = &mpc512x_gpio_devtype, },
	{ .compatible = "fsl,mpc5125-gpio", .data = &mpc5125_gpio_devtype, },
	{ .compatible = "fsl,pq3-gpio",     },
	{ .compatible = "fsl,qoriq-gpio",   },
	{}
};

static int mpc8xxx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mpc8xxx_gpio_chip *mpc8xxx_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct gpio_chip *gc;
	const struct of_device_id *id;
	const struct mpc8xxx_gpio_devtype *devtype =
		of_device_get_match_data(&pdev->dev);
	int ret;

	mpc8xxx_gc = devm_kzalloc(&pdev->dev, sizeof(*mpc8xxx_gc), GFP_KERNEL);
	if (!mpc8xxx_gc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mpc8xxx_gc);

	raw_spin_lock_init(&mpc8xxx_gc->lock);

	mm_gc = &mpc8xxx_gc->mm_gc;
	gc = &mm_gc->gc;

	mm_gc->save_regs = mpc8xxx_gpio_save_regs;
	gc->ngpio = MPC8XXX_GPIO_PINS;
	gc->direction_input = mpc8xxx_gpio_dir_in;

	if (!devtype)
		devtype = &mpc8xxx_gpio_devtype_default;

	/*
	 * It's assumed that only a single type of gpio controller is available
	 * on the current machine, so overwriting global data is fine.
	 */
	mpc8xxx_irq_chip.irq_set_type = devtype->irq_set_type;

	gc->direction_output = devtype->gpio_dir_out ?: mpc8xxx_gpio_dir_out;
	gc->get = devtype->gpio_get ?: mpc8xxx_gpio_get;
	gc->set = mpc8xxx_gpio_set;
	gc->set_multiple = mpc8xxx_gpio_set_multiple;
	gc->to_irq = mpc8xxx_gpio_to_irq;

	ret = of_mm_gpiochip_add(np, mm_gc);
	if (ret)
		return ret;

	mpc8xxx_gc->irqn = irq_of_parse_and_map(np, 0);
	if (mpc8xxx_gc->irqn == NO_IRQ)
		return 0;

	mpc8xxx_gc->irq = irq_domain_add_linear(np, MPC8XXX_GPIO_PINS,
					&mpc8xxx_gpio_irq_ops, mpc8xxx_gc);
	if (!mpc8xxx_gc->irq)
		return 0;

	id = of_match_node(mpc8xxx_gpio_ids, np);
	if (id)
		mpc8xxx_gc->of_dev_id_data = id->data;

	/* ack and mask all irqs */
	out_be32(mm_gc->regs + GPIO_IER, 0xffffffff);
	out_be32(mm_gc->regs + GPIO_IMR, 0);

	irq_set_chained_handler_and_data(mpc8xxx_gc->irqn,
					 mpc8xxx_gpio_irq_cascade, mpc8xxx_gc);

	return 0;
}

static int mpc8xxx_remove(struct platform_device *pdev)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc = platform_get_drvdata(pdev);

	if (mpc8xxx_gc->irq) {
		irq_set_chained_handler_and_data(mpc8xxx_gc->irqn, NULL, NULL);
		irq_domain_remove(mpc8xxx_gc->irq);
	}

	of_mm_gpiochip_remove(&mpc8xxx_gc->mm_gc);

	return 0;
}

static struct platform_driver mpc8xxx_plat_driver = {
	.probe		= mpc8xxx_probe,
	.remove		= mpc8xxx_remove,
	.driver		= {
		.name = "gpio-mpc8xxx",
		.of_match_table	= mpc8xxx_gpio_ids,
	},
};

static int __init mpc8xxx_init(void)
{
	return platform_driver_register(&mpc8xxx_plat_driver);
}

arch_initcall(mpc8xxx_init);
