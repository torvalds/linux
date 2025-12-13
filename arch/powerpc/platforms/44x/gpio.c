// SPDX-License-Identifier: GPL-2.0-only
/*
 * PPC4xx gpio driver
 *
 * Copyright (c) 2008 Harris Corporation
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Steve Falco <sfalco@harris.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/gpio/driver.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

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
	struct gpio_chip gc;
	void __iomem *regs;
	spinlock_t lock;
};

/*
 * GPIO LIB API implementation for GPIOs
 *
 * There are a maximum of 32 gpios in each gpio controller.
 */

static int ppc4xx_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct ppc4xx_gpio_chip *chip = gpiochip_get_data(gc);
	struct ppc4xx_gpio __iomem *regs = chip->regs;

	return !!(in_be32(&regs->ir) & GPIO_MASK(gpio));
}

static inline void
__ppc4xx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ppc4xx_gpio_chip *chip = gpiochip_get_data(gc);
	struct ppc4xx_gpio __iomem *regs = chip->regs;

	if (val)
		setbits32(&regs->or, GPIO_MASK(gpio));
	else
		clrbits32(&regs->or, GPIO_MASK(gpio));
}

static int ppc4xx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ppc4xx_gpio_chip *chip = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	__ppc4xx_gpio_set(gc, gpio, val);

	spin_unlock_irqrestore(&chip->lock, flags);

	pr_debug("%s: gpio: %d val: %d\n", __func__, gpio, val);

	return 0;
}

static int ppc4xx_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct ppc4xx_gpio_chip *chip = gpiochip_get_data(gc);
	struct ppc4xx_gpio __iomem *regs = chip->regs;
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
	struct ppc4xx_gpio_chip *chip = gpiochip_get_data(gc);
	struct ppc4xx_gpio __iomem *regs = chip->regs;
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

static int ppc4xx_gpio_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = dev->of_node;
	struct ppc4xx_gpio_chip *chip;
	struct gpio_chip *gc;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	spin_lock_init(&chip->lock);

	gc = &chip->gc;

	gc->base = -1;
	gc->ngpio = 32;
	gc->direction_input = ppc4xx_gpio_dir_in;
	gc->direction_output = ppc4xx_gpio_dir_out;
	gc->get = ppc4xx_gpio_get;
	gc->set = ppc4xx_gpio_set;

	gc->label = devm_kasprintf(dev, GFP_KERNEL, "%pOF", np);
	if (!gc->label)
		return -ENOMEM;

	chip->regs = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	return devm_gpiochip_add_data(dev, gc, chip);
}

static const struct of_device_id ppc4xx_gpio_match[] = {
	{
		.compatible = "ibm,ppc4xx-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ppc4xx_gpio_match);

static struct platform_driver ppc4xx_gpio_driver = {
	.probe		= ppc4xx_gpio_probe,
	.driver		= {
		.name	= "ppc4xx-gpio",
		.of_match_table	= ppc4xx_gpio_match,
	},
};

static int __init ppc4xx_gpio_init(void)
{
	return platform_driver_register(&ppc4xx_gpio_driver);
}
arch_initcall(ppc4xx_gpio_init);
