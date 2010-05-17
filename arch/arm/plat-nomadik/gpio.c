/*
 * Generic GPIO driver for logic cells found in the Nomadik SoC
 *
 * Copyright (C) 2008,2009 STMicroelectronics
 * Copyright (C) 2009 Alessandro Rubini <rubini@unipv.it>
 *   Rewritten based on work by Prafulla WADASKAR <prafulla.wadaskar@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <mach/gpio.h>

/*
 * The GPIO module in the Nomadik family of Systems-on-Chip is an
 * AMBA device, managing 32 pins and alternate functions.  The logic block
 * is currently only used in the Nomadik.
 *
 * Symbols in this file are called "nmk_gpio" for "nomadik gpio"
 */

#define NMK_GPIO_PER_CHIP 32
struct nmk_gpio_chip {
	struct gpio_chip chip;
	void __iomem *addr;
	struct clk *clk;
	unsigned int parent_irq;
	spinlock_t lock;
	/* Keep track of configured edges */
	u32 edge_rising;
	u32 edge_falling;
};

/* Mode functions */
int nmk_gpio_set_mode(int gpio, int gpio_mode)
{
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 afunc, bfunc, bit;

	nmk_chip = get_irq_chip_data(NOMADIK_GPIO_TO_IRQ(gpio));
	if (!nmk_chip)
		return -EINVAL;

	bit = 1 << (gpio - nmk_chip->chip.base);

	spin_lock_irqsave(&nmk_chip->lock, flags);
	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & ~bit;
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & ~bit;
	if (gpio_mode & NMK_GPIO_ALT_A)
		afunc |= bit;
	if (gpio_mode & NMK_GPIO_ALT_B)
		bfunc |= bit;
	writel(afunc, nmk_chip->addr + NMK_GPIO_AFSLA);
	writel(bfunc, nmk_chip->addr + NMK_GPIO_AFSLB);
	spin_unlock_irqrestore(&nmk_chip->lock, flags);

	return 0;
}
EXPORT_SYMBOL(nmk_gpio_set_mode);

int nmk_gpio_get_mode(int gpio)
{
	struct nmk_gpio_chip *nmk_chip;
	u32 afunc, bfunc, bit;

	nmk_chip = get_irq_chip_data(NOMADIK_GPIO_TO_IRQ(gpio));
	if (!nmk_chip)
		return -EINVAL;

	bit = 1 << (gpio - nmk_chip->chip.base);

	afunc = readl(nmk_chip->addr + NMK_GPIO_AFSLA) & bit;
	bfunc = readl(nmk_chip->addr + NMK_GPIO_AFSLB) & bit;

	return (afunc ? NMK_GPIO_ALT_A : 0) | (bfunc ? NMK_GPIO_ALT_B : 0);
}
EXPORT_SYMBOL(nmk_gpio_get_mode);


/* IRQ functions */
static inline int nmk_gpio_get_bitmask(int gpio)
{
	return 1 << (gpio % 32);
}

static void nmk_gpio_irq_ack(unsigned int irq)
{
	int gpio;
	struct nmk_gpio_chip *nmk_chip;

	gpio = NOMADIK_IRQ_TO_GPIO(irq);
	nmk_chip = get_irq_chip_data(irq);
	if (!nmk_chip)
		return;
	writel(nmk_gpio_get_bitmask(gpio), nmk_chip->addr + NMK_GPIO_IC);
}

static void __nmk_gpio_irq_modify(struct nmk_gpio_chip *nmk_chip,
				  int gpio, bool enable)
{
	u32 bitmask = nmk_gpio_get_bitmask(gpio);
	u32 reg;

	/* we must individually set/clear the two edges */
	if (nmk_chip->edge_rising & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_RIMSC);
		if (enable)
			reg |= bitmask;
		else
			reg &= ~bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_RIMSC);
	}
	if (nmk_chip->edge_falling & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_FIMSC);
		if (enable)
			reg |= bitmask;
		else
			reg &= ~bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_FIMSC);
	}
}

static void nmk_gpio_irq_modify(unsigned int irq, bool enable)
{
	int gpio;
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask;

	gpio = NOMADIK_IRQ_TO_GPIO(irq);
	nmk_chip = get_irq_chip_data(irq);
	bitmask = nmk_gpio_get_bitmask(gpio);
	if (!nmk_chip)
		return;

	spin_lock_irqsave(&nmk_chip->lock, flags);
	__nmk_gpio_irq_modify(nmk_chip, gpio, enable);
	spin_unlock_irqrestore(&nmk_chip->lock, flags);
}

static void nmk_gpio_irq_mask(unsigned int irq)
{
	nmk_gpio_irq_modify(irq, false);
};

static void nmk_gpio_irq_unmask(unsigned int irq)
{
	nmk_gpio_irq_modify(irq, true);
}

static int nmk_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	bool enabled = !(irq_to_desc(irq)->status & IRQ_DISABLED);
	int gpio;
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask;

	gpio = NOMADIK_IRQ_TO_GPIO(irq);
	nmk_chip = get_irq_chip_data(irq);
	bitmask = nmk_gpio_get_bitmask(gpio);
	if (!nmk_chip)
		return -EINVAL;

	if (type & IRQ_TYPE_LEVEL_HIGH)
		return -EINVAL;
	if (type & IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;

	spin_lock_irqsave(&nmk_chip->lock, flags);

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, gpio, false);

	nmk_chip->edge_rising &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_RISING)
		nmk_chip->edge_rising |= bitmask;

	nmk_chip->edge_falling &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		nmk_chip->edge_falling |= bitmask;

	if (enabled)
		__nmk_gpio_irq_modify(nmk_chip, gpio, true);

	spin_unlock_irqrestore(&nmk_chip->lock, flags);

	return 0;
}

static struct irq_chip nmk_gpio_irq_chip = {
	.name		= "Nomadik-GPIO",
	.ack		= nmk_gpio_irq_ack,
	.mask		= nmk_gpio_irq_mask,
	.unmask		= nmk_gpio_irq_unmask,
	.set_type	= nmk_gpio_irq_set_type,
};

static void nmk_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct nmk_gpio_chip *nmk_chip;
	struct irq_chip *host_chip = get_irq_chip(irq);
	unsigned int gpio_irq;
	u32 pending;
	unsigned int first_irq;

	if (host_chip->mask_ack)
		host_chip->mask_ack(irq);
	else {
		host_chip->mask(irq);
		if (host_chip->ack)
			host_chip->ack(irq);
	}

	nmk_chip = get_irq_data(irq);
	first_irq = NOMADIK_GPIO_TO_IRQ(nmk_chip->chip.base);
	while ( (pending = readl(nmk_chip->addr + NMK_GPIO_IS)) ) {
		gpio_irq = first_irq + __ffs(pending);
		generic_handle_irq(gpio_irq);
	}

	host_chip->unmask(irq);
}

static int nmk_gpio_init_irq(struct nmk_gpio_chip *nmk_chip)
{
	unsigned int first_irq;
	int i;

	first_irq = NOMADIK_GPIO_TO_IRQ(nmk_chip->chip.base);
	for (i = first_irq; i < first_irq + NMK_GPIO_PER_CHIP; i++) {
		set_irq_chip(i, &nmk_gpio_irq_chip);
		set_irq_handler(i, handle_edge_irq);
		set_irq_flags(i, IRQF_VALID);
		set_irq_chip_data(i, nmk_chip);
		set_irq_type(i, IRQ_TYPE_EDGE_FALLING);
	}
	set_irq_chained_handler(nmk_chip->parent_irq, nmk_gpio_irq_handler);
	set_irq_data(nmk_chip->parent_irq, nmk_chip);
	return 0;
}

/* I/O Functions */
static int nmk_gpio_make_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	writel(1 << offset, nmk_chip->addr + NMK_GPIO_DIRC);
	return 0;
}

static int nmk_gpio_make_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);

	writel(1 << offset, nmk_chip->addr + NMK_GPIO_DIRS);
	return 0;
}

static int nmk_gpio_get_input(struct gpio_chip *chip, unsigned offset)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);
	u32 bit = 1 << offset;

	return (readl(nmk_chip->addr + NMK_GPIO_DAT) & bit) != 0;
}

static void nmk_gpio_set_output(struct gpio_chip *chip, unsigned offset,
				int val)
{
	struct nmk_gpio_chip *nmk_chip =
		container_of(chip, struct nmk_gpio_chip, chip);
	u32 bit = 1 << offset;

	if (val)
		writel(bit, nmk_chip->addr + NMK_GPIO_DATS);
	else
		writel(bit, nmk_chip->addr + NMK_GPIO_DATC);
}

/* This structure is replicated for each GPIO block allocated at probe time */
static struct gpio_chip nmk_gpio_template = {
	.direction_input	= nmk_gpio_make_input,
	.get			= nmk_gpio_get_input,
	.direction_output	= nmk_gpio_make_output,
	.set			= nmk_gpio_set_output,
	.ngpio			= NMK_GPIO_PER_CHIP,
	.can_sleep		= 0,
};

static int __init nmk_gpio_probe(struct platform_device *dev)
{
	struct nmk_gpio_platform_data *pdata = dev->dev.platform_data;
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	struct resource *res;
	struct clk *clk;
	int irq;
	int ret;

	if (!pdata)
		return -ENODEV;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto out;
	}

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		ret = irq;
		goto out;
	}

	if (request_mem_region(res->start, resource_size(res),
			       dev_name(&dev->dev)) == NULL) {
		ret = -EBUSY;
		goto out;
	}

	clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_release;
	}

	clk_enable(clk);

	nmk_chip = kzalloc(sizeof(*nmk_chip), GFP_KERNEL);
	if (!nmk_chip) {
		ret = -ENOMEM;
		goto out_clk;
	}
	/*
	 * The virt address in nmk_chip->addr is in the nomadik register space,
	 * so we can simply convert the resource address, without remapping
	 */
	nmk_chip->clk = clk;
	nmk_chip->addr = io_p2v(res->start);
	nmk_chip->chip = nmk_gpio_template;
	nmk_chip->parent_irq = irq;
	spin_lock_init(&nmk_chip->lock);

	chip = &nmk_chip->chip;
	chip->base = pdata->first_gpio;
	chip->label = pdata->name;
	chip->dev = &dev->dev;
	chip->owner = THIS_MODULE;

	ret = gpiochip_add(&nmk_chip->chip);
	if (ret)
		goto out_free;

	platform_set_drvdata(dev, nmk_chip);

	nmk_gpio_init_irq(nmk_chip);

	dev_info(&dev->dev, "Bits %i-%i at address %p\n",
		 nmk_chip->chip.base, nmk_chip->chip.base+31, nmk_chip->addr);
	return 0;

out_free:
	kfree(nmk_chip);
out_clk:
	clk_disable(clk);
	clk_put(clk);
out_release:
	release_mem_region(res->start, resource_size(res));
out:
	dev_err(&dev->dev, "Failure %i for GPIO %i-%i\n", ret,
		  pdata->first_gpio, pdata->first_gpio+31);
	return ret;
}

static int __exit nmk_gpio_remove(struct platform_device *dev)
{
	struct nmk_gpio_chip *nmk_chip;
	struct resource *res;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);

	nmk_chip = platform_get_drvdata(dev);
	gpiochip_remove(&nmk_chip->chip);
	clk_disable(nmk_chip->clk);
	clk_put(nmk_chip->clk);
	kfree(nmk_chip);
	release_mem_region(res->start, resource_size(res));
	return 0;
}


static struct platform_driver nmk_gpio_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "gpio",
		},
	.probe = nmk_gpio_probe,
	.remove = __exit_p(nmk_gpio_remove),
	.suspend = NULL, /* to be done */
	.resume = NULL,
};

static int __init nmk_gpio_init(void)
{
	return platform_driver_register(&nmk_gpio_driver);
}

arch_initcall(nmk_gpio_init);

MODULE_AUTHOR("Prafulla WADASKAR and Alessandro Rubini");
MODULE_DESCRIPTION("Nomadik GPIO Driver");
MODULE_LICENSE("GPL");


