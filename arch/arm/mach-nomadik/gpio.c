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
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

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
	unsigned int parent_irq;
	spinlock_t *lock;
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

static void nmk_gpio_irq_mask(unsigned int irq)
{
	int gpio;
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask, reg;

	gpio = NOMADIK_IRQ_TO_GPIO(irq);
	nmk_chip = get_irq_chip_data(irq);
	bitmask = nmk_gpio_get_bitmask(gpio);
	if (!nmk_chip)
		return;

	/* we must individually clear the two edges */
	spin_lock_irqsave(&nmk_chip->lock, flags);
	if (nmk_chip->edge_rising & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_RWIMSC);
		reg &= ~bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_RWIMSC);
	}
	if (nmk_chip->edge_falling & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_FWIMSC);
		reg &= ~bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_FWIMSC);
	}
	spin_unlock_irqrestore(&nmk_chip->lock, flags);
};

static void nmk_gpio_irq_unmask(unsigned int irq)
{
	int gpio;
	struct nmk_gpio_chip *nmk_chip;
	unsigned long flags;
	u32 bitmask, reg;

	gpio = NOMADIK_IRQ_TO_GPIO(irq);
	nmk_chip = get_irq_chip_data(irq);
	bitmask = nmk_gpio_get_bitmask(gpio);
	if (!nmk_chip)
		return;

	/* we must individually set the two edges */
	spin_lock_irqsave(&nmk_chip->lock, flags);
	if (nmk_chip->edge_rising & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_RWIMSC);
		reg |= bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_RWIMSC);
	}
	if (nmk_chip->edge_falling & bitmask) {
		reg = readl(nmk_chip->addr + NMK_GPIO_FWIMSC);
		reg |= bitmask;
		writel(reg, nmk_chip->addr + NMK_GPIO_FWIMSC);
	}
	spin_unlock_irqrestore(&nmk_chip->lock, flags);
}

static int nmk_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
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

	nmk_chip->edge_rising &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_RISING)
		nmk_chip->edge_rising |= bitmask;
	writel(nmk_chip->edge_rising, nmk_chip->addr + NMK_GPIO_RIMSC);

	nmk_chip->edge_falling &= ~bitmask;
	if (type & IRQ_TYPE_EDGE_FALLING)
		nmk_chip->edge_falling |= bitmask;
	writel(nmk_chip->edge_falling, nmk_chip->addr + NMK_GPIO_FIMSC);

	spin_unlock_irqrestore(&nmk_chip->lock, flags);

	nmk_gpio_irq_unmask(irq);

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
	struct irq_chip *host_chip;
	unsigned int gpio_irq;
	u32 pending;
	unsigned int first_irq;

	nmk_chip = get_irq_data(irq);
	first_irq = NOMADIK_GPIO_TO_IRQ(nmk_chip->chip.base);
	while ( (pending = readl(nmk_chip->addr + NMK_GPIO_IS)) ) {
		gpio_irq = first_irq + __ffs(pending);
		generic_handle_irq(gpio_irq);
	}
	if (0) {/* don't ack parent irq, as ack == disable */
		host_chip = get_irq_chip(irq);
		host_chip->ack(irq);
	}
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

static int __init nmk_gpio_probe(struct amba_device *dev, struct amba_id *id)
{
	struct nmk_gpio_platform_data *pdata;
	struct nmk_gpio_chip *nmk_chip;
	struct gpio_chip *chip;
	int ret;

	pdata = dev->dev.platform_data;
	ret = amba_request_regions(dev, pdata->name);
	if (ret)
		return ret;

	nmk_chip = kzalloc(sizeof(*nmk_chip), GFP_KERNEL);
	if (!nmk_chip) {
		ret = -ENOMEM;
		goto out_amba;
	}
	/*
	 * The virt address in nmk_chip->addr is in the nomadik register space,
	 * so we can simply convert the resource address, without remapping
	 */
	nmk_chip->addr = io_p2v(dev->res.start);
	nmk_chip->chip = nmk_gpio_template;
	nmk_chip->parent_irq = pdata->parent_irq;

	chip = &nmk_chip->chip;
	chip->base = pdata->first_gpio;
	chip->label = pdata->name;
	chip->dev = &dev->dev;
	chip->owner = THIS_MODULE;

	ret = gpiochip_add(&nmk_chip->chip);
	if (ret)
		goto out_free;

	amba_set_drvdata(dev, nmk_chip);

	nmk_gpio_init_irq(nmk_chip);

	dev_info(&dev->dev, "Bits %i-%i at address %p\n",
		 nmk_chip->chip.base, nmk_chip->chip.base+31, nmk_chip->addr);
	return 0;

 out_free:
	kfree(nmk_chip);
 out_amba:
	amba_release_regions(dev);
	dev_err(&dev->dev, "Failure %i for GPIO %i-%i\n", ret,
		  pdata->first_gpio, pdata->first_gpio+31);
	return ret;
}

static int nmk_gpio_remove(struct amba_device *dev)
{
	struct nmk_gpio_chip *nmk_chip;

	nmk_chip = amba_get_drvdata(dev);
	gpiochip_remove(&nmk_chip->chip);
	kfree(nmk_chip);
	amba_release_regions(dev);
	return 0;
}


/* We have 0x1f080060 and 0x1f180060, accept both using the mask */
static struct amba_id nmk_gpio_ids[] = {
	{
		.id	= 0x1f080060,
		.mask	= 0xffefffff,
	},
	{0, 0},
};

static struct amba_driver nmk_gpio_driver = {
	.drv = {
		.owner = THIS_MODULE,
		.name = "gpio",
		},
	.probe = nmk_gpio_probe,
	.remove = nmk_gpio_remove,
	.suspend = NULL, /* to be done */
	.resume = NULL,
	.id_table = nmk_gpio_ids,
};

static int __init nmk_gpio_init(void)
{
	return amba_driver_register(&nmk_gpio_driver);
}

arch_initcall(nmk_gpio_init);

MODULE_AUTHOR("Prafulla WADASKAR and Alessandro Rubini");
MODULE_DESCRIPTION("Nomadik GPIO Driver");
MODULE_LICENSE("GPL");


