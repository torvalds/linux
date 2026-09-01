// SPDX-License-Identifier: GPL-2.0-only
/*
 * PPC44x gpio driver
 *
 * Copyright (c) 2008 Harris Corporation
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) MontaVista Software, Inc. 2008.
 *
 * Author: Steve Falco <sfalco@harris.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/gpio/generic.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#define GPIO_MASK(gpio)		(0x80000000 >> (gpio))
#define GPIO_MASK2(gpio)	(0xc0000000 >> (((gpio) % 16) * 2))

/* Physical GPIO register layout */
struct ppc44x_gpio {
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

struct ppc44x_gpio_chip {
	struct gpio_generic_chip chip;
	void __iomem *regs;
};

static inline void ppc44x_clrbits32(void __iomem *addr, u32 mask)
{
	u32 val = ioread32be(addr);

	val &= ~mask;
	iowrite32be(val, addr);
}

static inline void ppc44x_setbits32(void __iomem *addr, u32 mask)
{
	u32 val = ioread32be(addr);

	val |= mask;
	iowrite32be(val, addr);
}

/*
 * GPIO LIB API implementation for GPIOs
 *
 * There are a maximum of 32 gpios in each gpio controller.
 */

static inline void
__ppc44x_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ppc44x_gpio_chip *chip = gpiochip_get_data(gc);
	struct gpio_generic_chip *gen_gc = &chip->chip;

	if (val)
		gen_gc->sdata |= GPIO_MASK(gpio);
	else
		gen_gc->sdata &= ~GPIO_MASK(gpio);

	gpio_generic_write_reg(gen_gc, gen_gc->reg_set, gen_gc->sdata);
}

static int ppc44x_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct ppc44x_gpio_chip *chip = gpiochip_get_data(gc);
	struct gpio_generic_chip *gen_gc = &chip->chip;
	struct ppc44x_gpio __iomem *regs = chip->regs;

	guard(gpio_generic_lock_irqsave)(gen_gc);

	/* Disable open-drain function */
	ppc44x_clrbits32(&regs->odr, GPIO_MASK(gpio));

	/* Float the pin */
	ppc44x_clrbits32(&regs->tcr, GPIO_MASK(gpio));
	gen_gc->sdir &= ~GPIO_MASK(gpio);

	/* Bits 0-15 use TSRL/OSRL, bits 16-31 use TSRH/OSRH */
	if (gpio < 16) {
		ppc44x_clrbits32(&regs->osrl, GPIO_MASK2(gpio));
		ppc44x_clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
	} else {
		ppc44x_clrbits32(&regs->osrh, GPIO_MASK2(gpio));
		ppc44x_clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
	}

	return 0;
}

static int
ppc44x_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct ppc44x_gpio_chip *chip = gpiochip_get_data(gc);
	struct gpio_generic_chip *gen_gc = &chip->chip;
	struct ppc44x_gpio __iomem *regs = chip->regs;

	guard(gpio_generic_lock_irqsave)(gen_gc);

	/* First set initial value */
	__ppc44x_gpio_set(gc, gpio, val);

	/* Disable open-drain function */
	ppc44x_clrbits32(&regs->odr, GPIO_MASK(gpio));

	/* Drive the pin */
	ppc44x_setbits32(&regs->tcr, GPIO_MASK(gpio));
	gen_gc->sdir |= GPIO_MASK(gpio);

	/* Bits 0-15 use TSRL, bits 16-31 use TSRH */
	if (gpio < 16) {
		ppc44x_clrbits32(&regs->osrl, GPIO_MASK2(gpio));
		ppc44x_clrbits32(&regs->tsrl, GPIO_MASK2(gpio));
	} else {
		ppc44x_clrbits32(&regs->osrh, GPIO_MASK2(gpio));
		ppc44x_clrbits32(&regs->tsrh, GPIO_MASK2(gpio));
	}

	pr_debug("%s: gpio: %d val: %d\n", __func__, gpio, val);

	return 0;
}

static int ppc44x_gpio_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct ppc44x_gpio __iomem *regs;
	struct ppc44x_gpio_chip *chip;
	struct gpio_generic_chip_config config;
	struct gpio_chip *gc;
	int ret;

	regs = devm_platform_ioremap_resource(ofdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regs = regs;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = &regs->ir,
		.set = &regs->or,
		.dirout = &regs->tcr,
		.flags = GPIO_GENERIC_BIG_ENDIAN |
			 GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER,
	};

	ret = gpio_generic_chip_init(&chip->chip, &config);
	if (ret)
		return ret;

	gc = &chip->chip.gc;
	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->direction_input = ppc44x_gpio_dir_in;
	gc->direction_output = ppc44x_gpio_dir_out;

	return devm_gpiochip_add_data(dev, gc, chip);
}

static const struct of_device_id ppc44x_gpio_match[] = {
	{
		.compatible = "ibm,ppc4xx-gpio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ppc44x_gpio_match);

static struct platform_driver ppc44x_gpio_driver = {
	.probe		= ppc44x_gpio_probe,
	.driver		= {
		.name	= "ppc44x-gpio",
		.of_match_table	= ppc44x_gpio_match,
	},
};

MODULE_DESCRIPTION("PPC44x gpio driver");
MODULE_LICENSE("GPL");

module_platform_driver(ppc44x_gpio_driver);
