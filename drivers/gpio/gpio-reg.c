// SPDX-License-Identifier: GPL-2.0-only
/*
 * gpio-reg: single register individually fixed-direction GPIOs
 *
 * Copyright (C) 2016 Russell King
 */
#include <linux/gpio/driver.h>
#include <linux/gpio/gpio-reg.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct gpio_reg {
	struct gpio_chip gc;
	spinlock_t lock;
	u32 direction;
	u32 out;
	void __iomem *reg;
	struct irq_domain *irqdomain;
	const int *irqs;
};

#define to_gpio_reg(x) container_of(x, struct gpio_reg, gc)

static int gpio_reg_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_reg *r = to_gpio_reg(gc);

	return r->direction & BIT(offset) ? 1 : 0;
}

static int gpio_reg_direction_output(struct gpio_chip *gc, unsigned offset,
	int value)
{
	struct gpio_reg *r = to_gpio_reg(gc);

	if (r->direction & BIT(offset))
		return -ENOTSUPP;

	gc->set(gc, offset, value);
	return 0;
}

static int gpio_reg_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_reg *r = to_gpio_reg(gc);

	return r->direction & BIT(offset) ? 0 : -ENOTSUPP;
}

static void gpio_reg_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct gpio_reg *r = to_gpio_reg(gc);
	unsigned long flags;
	u32 val, mask = BIT(offset);

	spin_lock_irqsave(&r->lock, flags);
	val = r->out;
	if (value)
		val |= mask;
	else
		val &= ~mask;
	r->out = val;
	writel_relaxed(val, r->reg);
	spin_unlock_irqrestore(&r->lock, flags);
}

static int gpio_reg_get(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_reg *r = to_gpio_reg(gc);
	u32 val, mask = BIT(offset);

	if (r->direction & mask) {
		/*
		 * double-read the value, some registers latch after the
		 * first read.
		 */
		readl_relaxed(r->reg);
		val = readl_relaxed(r->reg);
	} else {
		val = r->out;
	}
	return !!(val & mask);
}

static void gpio_reg_set_multiple(struct gpio_chip *gc, unsigned long *mask,
	unsigned long *bits)
{
	struct gpio_reg *r = to_gpio_reg(gc);
	unsigned long flags;

	spin_lock_irqsave(&r->lock, flags);
	r->out = (r->out & ~*mask) | (*bits & *mask);
	writel_relaxed(r->out, r->reg);
	spin_unlock_irqrestore(&r->lock, flags);
}

static int gpio_reg_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_reg *r = to_gpio_reg(gc);
	int irq = r->irqs[offset];

	if (irq >= 0 && r->irqdomain)
		irq = irq_find_mapping(r->irqdomain, irq);

	return irq;
}

/**
 * gpio_reg_init - add a fixed in/out register as gpio
 * @dev: optional struct device associated with this register
 * @base: start gpio number, or -1 to allocate
 * @num: number of GPIOs, maximum 32
 * @label: GPIO chip label
 * @direction: bitmask of fixed direction, one per GPIO signal, 1 = in
 * @def_out: initial GPIO output value
 * @names: array of %num strings describing each GPIO signal or %NULL
 * @irqdom: irq domain or %NULL
 * @irqs: array of %num ints describing the interrupt mapping for each
 *        GPIO signal, or %NULL.  If @irqdom is %NULL, then this
 *        describes the Linux interrupt number, otherwise it describes
 *        the hardware interrupt number in the specified irq domain.
 *
 * Add a single-register GPIO device containing up to 32 GPIO signals,
 * where each GPIO has a fixed input or output configuration.  Only
 * input GPIOs are assumed to be readable from the register, and only
 * then after a double-read.  Output values are assumed not to be
 * readable.
 */
struct gpio_chip *gpio_reg_init(struct device *dev, void __iomem *reg,
	int base, int num, const char *label, u32 direction, u32 def_out,
	const char *const *names, struct irq_domain *irqdom, const int *irqs)
{
	struct gpio_reg *r;
	int ret;

	if (dev)
		r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	else
		r = kzalloc(sizeof(*r), GFP_KERNEL);

	if (!r)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&r->lock);

	r->gc.label = label;
	r->gc.get_direction = gpio_reg_get_direction;
	r->gc.direction_input = gpio_reg_direction_input;
	r->gc.direction_output = gpio_reg_direction_output;
	r->gc.set = gpio_reg_set;
	r->gc.get = gpio_reg_get;
	r->gc.set_multiple = gpio_reg_set_multiple;
	if (irqs)
		r->gc.to_irq = gpio_reg_to_irq;
	r->gc.base = base;
	r->gc.ngpio = num;
	r->gc.names = names;
	r->direction = direction;
	r->out = def_out;
	r->reg = reg;
	r->irqs = irqs;

	if (dev)
		ret = devm_gpiochip_add_data(dev, &r->gc, r);
	else
		ret = gpiochip_add_data(&r->gc, r);

	return ret ? ERR_PTR(ret) : &r->gc;
}

int gpio_reg_resume(struct gpio_chip *gc)
{
	struct gpio_reg *r = to_gpio_reg(gc);
	unsigned long flags;

	spin_lock_irqsave(&r->lock, flags);
	writel_relaxed(r->out, r->reg);
	spin_unlock_irqrestore(&r->lock, flags);

	return 0;
}
