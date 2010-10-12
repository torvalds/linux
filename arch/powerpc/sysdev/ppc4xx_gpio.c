/*
 * PPC4xx gpio driver
 *
 * Copyright (c) 2008 Harris Corporation
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Steve Falco <sfalco@harris.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/slab.h>

#define GPIO_MASK(gpio)		(0x80000000 >> (gpio))
#define GPIO_MASK2(gpio)	(0xc0000000 >> ((gpio) * 2))

/* Physical GPIO register layout */
struct ppc4xx_gpio {
	__be32 or;
	__be32 tcr;
	__be32 osrl;
	__be32 osrh;
	__be32 tsrl;
	__be32 tsrh;
	__be32 odr;
	__be32 ir;
	__be32 rr1;
	__be32 rr2;
	__be32 rr3;
	__be32 reserved1;
	__be32 isr1l;
	__be32 isr1h;
	__be32 isr2l;
	__be32 isr2h;
	__be32 isr3l;
	__be32 isr3h;
};

struct ppc4xx_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;
};

/*
 * GPIO LIB API implementation for GPIOs
 *
 * There are a maximum of 32 gpios in each gpio controller.
 */

static inline struct ppc4xx_gpio_chip *
to_ppc4xx_gpiochip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct ppc4xx_gpio_chip, mm_gc);
}

static int ppc4xx_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct ppc4xx_gpio __iomem *regs = mm_gc->regs;

	return in_be32(&regs->ir) & GPIO_MASK(gpio);
}

static inline void
__ppc4xx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct ppc4xx_gpio __iomem *regs = mm_gc->regs;

	if (val)
		setbits32(&regs->or, GPIO_MASK(gpio));
	else
		clrbits32(&regs->or, GPIO_MASK(gpio));
}

static void
ppc4xx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct ppc4xx_gpio_chip *chip = to_ppc4xx_gpiochip(mm_gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	__ppc4xx_gpio_set(gc, gpio, val);

	spin_unlock_irqrestore(&chip->lock, flags);

	pr_debug("%s: gpio: %d val: %d\n", __func__, gpio, val);
}

static int ppc4xx_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct ppc4xx_gpio_chip *chip = to_ppc4xx_gpiochip(mm_gc);
	struct ppc4xx_gpio __iomem *regs = mm_gc->regs;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	/* Disable open-drain function */
	clrbits32(&regs->odr, GPIO_MASK(gpio));

	/* Float the pin */
	clrbits32(&regs->tcr, GPIO_MASK(gpio));

	/* Bits 0-15 use TSRL/OSRL, bits 16-31 use TSRH/OSRH */
	if (gpio < 16) {
		clrbits32(&regs->osrl, GPIO_MASK2(gpio));
		clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
	} else {
		clrbits32(&regs->osrh, GPIO_MASK2(gpio));
		clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
	}

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int
ppc4xx_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct ppc4xx_gpio_chip *chip = to_ppc4xx_gpiochip(mm_gc);
	struct ppc4xx_gpio __iomem *regs = mm_gc->regs;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	/* First set initial value */
	__ppc4xx_gpio_set(gc, gpio, val);

	/* Disable open-drain function */
	clrbits32(&regs->odr, GPIO_MASK(gpio));

	/* Drive the pin */
	setbits32(&regs->tcr, GPIO_MASK(gpio));

	/* Bits 0-15 use TSRL, bits 16-31 use TSRH */
	if (gpio < 16) {
		clrbits32(&regs->osrl, GPIO_MASK2(gpio));
		clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
	} else {
		clrbits32(&regs->osrh, GPIO_MASK2(gpio));
		clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
	}

	spin_unlock_irqrestore(&chip->lock, flags);

	pr_debug("%s: gpio: %d val: %d\n", __func__, gpio, val);

	return 0;
}

static int __init ppc4xx_add_gpiochips(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "ibm,ppc4xx-gpio") {
		int ret;
		struct ppc4xx_gpio_chip *ppc4xx_gc;
		struct of_mm_gpio_chip *mm_gc;
		struct gpio_chip *gc;

		ppc4xx_gc = kzalloc(sizeof(*ppc4xx_gc), GFP_KERNEL);
		if (!ppc4xx_gc) {
			ret = -ENOMEM;
			goto err;
		}

		spin_lock_init(&ppc4xx_gc->lock);

		mm_gc = &ppc4xx_gc->mm_gc;
		gc = &mm_gc->gc;

		gc->ngpio = 32;
		gc->direction_input = ppc4xx_gpio_dir_in;
		gc->direction_output = ppc4xx_gpio_dir_out;
		gc->get = ppc4xx_gpio_get;
		gc->set = ppc4xx_gpio_set;

		ret = of_mm_gpiochip_add(np, mm_gc);
		if (ret)
			goto err;
		continue;
err:
		pr_err("%s: registration failed with status %d\n",
		       np->full_name, ret);
		kfree(ppc4xx_gc);
		/* try others anyway */
	}
	return 0;
}
arch_initcall(ppc4xx_add_gpiochips);
