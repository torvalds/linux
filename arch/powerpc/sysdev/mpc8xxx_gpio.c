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

static void __init mpc8xxx_add_controller(struct device_node *np)
{
	struct mpc8xxx_gpio_chip *mpc8xxx_gc;
	struct of_mm_gpio_chip *mm_gc;
	struct of_gpio_chip *of_gc;
	struct gpio_chip *gc;
	int ret;

	mpc8xxx_gc = kzalloc(sizeof(*mpc8xxx_gc), GFP_KERNEL);
	if (!mpc8xxx_gc) {
		ret = -ENOMEM;
		goto err;
	}

	spin_lock_init(&mpc8xxx_gc->lock);

	mm_gc = &mpc8xxx_gc->mm_gc;
	of_gc = &mm_gc->of_gc;
	gc = &of_gc->gc;

	mm_gc->save_regs = mpc8xxx_gpio_save_regs;
	of_gc->gpio_cells = 2;
	gc->ngpio = MPC8XXX_GPIO_PINS;
	gc->direction_input = mpc8xxx_gpio_dir_in;
	gc->direction_output = mpc8xxx_gpio_dir_out;
	if (of_device_is_compatible(np, "fsl,mpc8572-gpio"))
		gc->get = mpc8572_gpio_get;
	else
		gc->get = mpc8xxx_gpio_get;
	gc->set = mpc8xxx_gpio_set;

	ret = of_mm_gpiochip_add(np, mm_gc);
	if (ret)
		goto err;

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

	return 0;
}
arch_initcall(mpc8xxx_add_gpiochips);
