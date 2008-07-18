/*
 * QUICC Engine GPIOs
 *
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <asm/qe.h>

struct qe_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

	/* shadowed data register to clear/set bits safely */
	u32 cpdata;
};

static inline struct qe_gpio_chip *
to_qe_gpio_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct qe_gpio_chip, mm_gc);
}

static void qe_gpio_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct qe_gpio_chip *qe_gc = to_qe_gpio_chip(mm_gc);
	struct qe_pio_regs __iomem *regs = mm_gc->regs;

	qe_gc->cpdata = in_be32(&regs->cpdata);
}

static int qe_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct qe_pio_regs __iomem *regs = mm_gc->regs;
	u32 pin_mask = 1 << (QE_PIO_PINS - 1 - gpio);

	return in_be32(&regs->cpdata) & pin_mask;
}

static void qe_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct qe_gpio_chip *qe_gc = to_qe_gpio_chip(mm_gc);
	struct qe_pio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (QE_PIO_PINS - 1 - gpio);

	spin_lock_irqsave(&qe_gc->lock, flags);

	if (val)
		qe_gc->cpdata |= pin_mask;
	else
		qe_gc->cpdata &= ~pin_mask;

	out_be32(&regs->cpdata, qe_gc->cpdata);

	spin_unlock_irqrestore(&qe_gc->lock, flags);
}

static int qe_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct qe_gpio_chip *qe_gc = to_qe_gpio_chip(mm_gc);
	unsigned long flags;

	spin_lock_irqsave(&qe_gc->lock, flags);

	__par_io_config_pin(mm_gc->regs, gpio, QE_PIO_DIR_IN, 0, 0, 0);

	spin_unlock_irqrestore(&qe_gc->lock, flags);

	return 0;
}

static int qe_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct qe_gpio_chip *qe_gc = to_qe_gpio_chip(mm_gc);
	unsigned long flags;

	spin_lock_irqsave(&qe_gc->lock, flags);

	__par_io_config_pin(mm_gc->regs, gpio, QE_PIO_DIR_OUT, 0, 0, 0);

	spin_unlock_irqrestore(&qe_gc->lock, flags);

	qe_gpio_set(gc, gpio, val);

	return 0;
}

static int __init qe_add_gpiochips(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "fsl,mpc8323-qe-pario-bank") {
		int ret;
		struct qe_gpio_chip *qe_gc;
		struct of_mm_gpio_chip *mm_gc;
		struct of_gpio_chip *of_gc;
		struct gpio_chip *gc;

		qe_gc = kzalloc(sizeof(*qe_gc), GFP_KERNEL);
		if (!qe_gc) {
			ret = -ENOMEM;
			goto err;
		}

		spin_lock_init(&qe_gc->lock);

		mm_gc = &qe_gc->mm_gc;
		of_gc = &mm_gc->of_gc;
		gc = &of_gc->gc;

		mm_gc->save_regs = qe_gpio_save_regs;
		of_gc->gpio_cells = 2;
		gc->ngpio = QE_PIO_PINS;
		gc->direction_input = qe_gpio_dir_in;
		gc->direction_output = qe_gpio_dir_out;
		gc->get = qe_gpio_get;
		gc->set = qe_gpio_set;

		ret = of_mm_gpiochip_add(np, mm_gc);
		if (ret)
			goto err;
		continue;
err:
		pr_err("%s: registration failed with status %d\n",
		       np->full_name, ret);
		kfree(qe_gc);
		/* try others anyway */
	}
	return 0;
}
arch_initcall(qe_add_gpiochips);
