/*
 * GPIO controller driver for Intel Lynxpoint PCH chipset>
 * Copyright (c) 2012, Intel Corporation.
 *
 * Author: Mathias Nyman <mathias.nyman@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/irqdomain.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>

/* LynxPoint chipset has support for 94 gpio pins */

#define LP_NUM_GPIO	94

/* Bitmapped register offsets */
#define LP_ACPI_OWNED	0x00 /* Bitmap, set by bios, 0: pin reserved for ACPI */
#define LP_GC		0x7C /* set APIC IRQ to IRQ14 or IRQ15 for all pins */
#define LP_INT_STAT	0x80
#define LP_INT_ENABLE	0x90

/* Each pin has two 32 bit config registers, starting at 0x100 */
#define LP_CONFIG1	0x100
#define LP_CONFIG2	0x104

/* LP_CONFIG1 reg bits */
#define OUT_LVL_BIT	BIT(31)
#define IN_LVL_BIT	BIT(30)
#define TRIG_SEL_BIT	BIT(4) /* 0: Edge, 1: Level */
#define INT_INV_BIT	BIT(3) /* Invert interrupt triggering */
#define DIR_BIT		BIT(2) /* 0: Output, 1: Input */
#define USE_SEL_BIT	BIT(0) /* 0: Native, 1: GPIO */

/* LP_CONFIG2 reg bits */
#define GPINDIS_BIT	BIT(2) /* disable input sensing */
#define GPIWP_BIT	(BIT(0) | BIT(1)) /* weak pull options */

struct lp_gpio {
	struct gpio_chip	chip;
	struct irq_domain	*domain;
	struct platform_device	*pdev;
	spinlock_t		lock;
	unsigned long		reg_base;
};

/*
 * Lynxpoint gpios are controlled through both bitmapped registers and
 * per gpio specific registers. The bitmapped registers are in chunks of
 * 3 x 32bit registers to cover all 94 gpios
 *
 * per gpio specific registers consist of two 32bit registers per gpio
 * (LP_CONFIG1 and LP_CONFIG2), with 94 gpios there's a total of
 * 188 config registes.
 *
 * A simplified view of the register layout look like this:
 *
 * LP_ACPI_OWNED[31:0] gpio ownerships for gpios 0-31  (bitmapped registers)
 * LP_ACPI_OWNED[63:32] gpio ownerships for gpios 32-63
 * LP_ACPI_OWNED[94:64] gpio ownerships for gpios 63-94
 * ...
 * LP_INT_ENABLE[31:0] ...
 * LP_INT_ENABLE[63:31] ...
 * LP_INT_ENABLE[94:64] ...
 * LP0_CONFIG1 (gpio 0) config1 reg for gpio 0 (per gpio registers)
 * LP0_CONFIG2 (gpio 0) config2 reg for gpio 0
 * LP1_CONFIG1 (gpio 1) config1 reg for gpio 1
 * LP1_CONFIG2 (gpio 1) config2 reg for gpio 1
 * LP2_CONFIG1 (gpio 2) ...
 * LP2_CONFIG2 (gpio 2) ...
 * ...
 * LP94_CONFIG1 (gpio 94) ...
 * LP94_CONFIG2 (gpio 94) ...
 */

static unsigned long lp_gpio_reg(struct gpio_chip *chip, unsigned offset,
				 int reg)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	int reg_offset;

	if (reg == LP_CONFIG1 || reg == LP_CONFIG2)
		/* per gpio specific config registers */
		reg_offset = offset * 8;
	else
		/* bitmapped registers */
		reg_offset = (offset / 32) * 4;

	return lg->reg_base + reg + reg_offset;
}

static int lp_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	unsigned long reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long conf2 = lp_gpio_reg(chip, offset, LP_CONFIG2);
	unsigned long acpi_use = lp_gpio_reg(chip, offset, LP_ACPI_OWNED);

	pm_runtime_get(&lg->pdev->dev); /* should we put if failed */

	/* Fail if BIOS reserved pin for ACPI use */
	if (!(inl(acpi_use) & BIT(offset % 32))) {
		dev_err(&lg->pdev->dev, "gpio %d reserved for ACPI\n", offset);
		return -EBUSY;
	}
	/* Fail if pin is in alternate function mode (not GPIO mode) */
	if (!(inl(reg) & USE_SEL_BIT))
		return -ENODEV;

	/* enable input sensing */
	outl(inl(conf2) & ~GPINDIS_BIT, conf2);


	return 0;
}

static void lp_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	unsigned long conf2 = lp_gpio_reg(chip, offset, LP_CONFIG2);

	/* disable input sensing */
	outl(inl(conf2) | GPINDIS_BIT, conf2);

	pm_runtime_put(&lg->pdev->dev);
}

static int lp_irq_type(struct irq_data *d, unsigned type)
{
	struct lp_gpio *lg = irq_data_get_irq_chip_data(d);
	u32 hwirq = irqd_to_hwirq(d);
	unsigned long flags;
	u32 value;
	unsigned long reg = lp_gpio_reg(&lg->chip, hwirq, LP_CONFIG1);

	if (hwirq >= lg->chip.ngpio)
		return -EINVAL;

	spin_lock_irqsave(&lg->lock, flags);
	value = inl(reg);

	/* set both TRIG_SEL and INV bits to 0 for rising edge */
	if (type & IRQ_TYPE_EDGE_RISING)
		value &= ~(TRIG_SEL_BIT | INT_INV_BIT);

	/* TRIG_SEL bit 0, INV bit 1 for falling edge */
	if (type & IRQ_TYPE_EDGE_FALLING)
		value = (value | INT_INV_BIT) & ~TRIG_SEL_BIT;

	/* TRIG_SEL bit 1, INV bit 0 for level low */
	if (type & IRQ_TYPE_LEVEL_LOW)
		value = (value | TRIG_SEL_BIT) & ~INT_INV_BIT;

	/* TRIG_SEL bit 1, INV bit 1 for level high */
	if (type & IRQ_TYPE_LEVEL_HIGH)
		value |= TRIG_SEL_BIT | INT_INV_BIT;

	outl(value, reg);
	spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static int lp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	unsigned long reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	return inl(reg) & IN_LVL_BIT;
}

static void lp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	unsigned long reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	spin_lock_irqsave(&lg->lock, flags);

	if (value)
		outl(inl(reg) | OUT_LVL_BIT, reg);
	else
		outl(inl(reg) & ~OUT_LVL_BIT, reg);

	spin_unlock_irqrestore(&lg->lock, flags);
}

static int lp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	unsigned long reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	spin_lock_irqsave(&lg->lock, flags);
	outl(inl(reg) | DIR_BIT, reg);
	spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static int lp_gpio_direction_output(struct gpio_chip *chip,
				      unsigned offset, int value)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	unsigned long reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	lp_gpio_set(chip, offset, value);

	spin_lock_irqsave(&lg->lock, flags);
	outl(inl(reg) & ~DIR_BIT, reg);
	spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static int lp_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct lp_gpio *lg = container_of(chip, struct lp_gpio, chip);
	return irq_create_mapping(lg->domain, offset);
}

static void lp_gpio_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct lp_gpio *lg = irq_data_get_irq_handler_data(data);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	u32 base, pin, mask;
	unsigned long reg, pending;
	unsigned virq;

	/* check from GPIO controller which pin triggered the interrupt */
	for (base = 0; base < lg->chip.ngpio; base += 32) {
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_STAT);

		while ((pending = inl(reg))) {
			pin = __ffs(pending);
			mask = BIT(pin);
			/* Clear before handling so we don't lose an edge */
			outl(mask, reg);
			virq = irq_find_mapping(lg->domain, base + pin);
			generic_handle_irq(virq);
		}
	}
	chip->irq_eoi(data);
}

static void lp_irq_unmask(struct irq_data *d)
{
}

static void lp_irq_mask(struct irq_data *d)
{
}

static void lp_irq_enable(struct irq_data *d)
{
	struct lp_gpio *lg = irq_data_get_irq_chip_data(d);
	u32 hwirq = irqd_to_hwirq(d);
	unsigned long reg = lp_gpio_reg(&lg->chip, hwirq, LP_INT_ENABLE);
	unsigned long flags;

	spin_lock_irqsave(&lg->lock, flags);
	outl(inl(reg) | BIT(hwirq % 32), reg);
	spin_unlock_irqrestore(&lg->lock, flags);
}

static void lp_irq_disable(struct irq_data *d)
{
	struct lp_gpio *lg = irq_data_get_irq_chip_data(d);
	u32 hwirq = irqd_to_hwirq(d);
	unsigned long reg = lp_gpio_reg(&lg->chip, hwirq, LP_INT_ENABLE);
	unsigned long flags;

	spin_lock_irqsave(&lg->lock, flags);
	outl(inl(reg) & ~BIT(hwirq % 32), reg);
	spin_unlock_irqrestore(&lg->lock, flags);
}

static struct irq_chip lp_irqchip = {
	.name = "LP-GPIO",
	.irq_mask = lp_irq_mask,
	.irq_unmask = lp_irq_unmask,
	.irq_enable = lp_irq_enable,
	.irq_disable = lp_irq_disable,
	.irq_set_type = lp_irq_type,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static void lp_gpio_irq_init_hw(struct lp_gpio *lg)
{
	unsigned long reg;
	unsigned base;

	for (base = 0; base < lg->chip.ngpio; base += 32) {
		/* disable gpio pin interrupts */
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_ENABLE);
		outl(0, reg);
		/* Clear interrupt status register */
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_STAT);
		outl(0xffffffff, reg);
	}
}

static int lp_gpio_irq_map(struct irq_domain *d, unsigned int virq,
			    irq_hw_number_t hw)
{
	struct lp_gpio *lg = d->host_data;

	irq_set_chip_and_handler_name(virq, &lp_irqchip, handle_simple_irq,
				      "demux");
	irq_set_chip_data(virq, lg);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops lp_gpio_irq_ops = {
	.map = lp_gpio_irq_map,
};

static int lp_gpio_probe(struct platform_device *pdev)
{
	struct lp_gpio *lg;
	struct gpio_chip *gc;
	struct resource *io_rc, *irq_rc;
	struct device *dev = &pdev->dev;
	unsigned long reg_len;
	unsigned hwirq;
	int ret = -ENODEV;

	lg = devm_kzalloc(dev, sizeof(struct lp_gpio), GFP_KERNEL);
	if (!lg) {
		dev_err(dev, "can't allocate lp_gpio chip data\n");
		return -ENOMEM;
	}

	lg->pdev = pdev;
	platform_set_drvdata(pdev, lg);

	io_rc = platform_get_resource(pdev, IORESOURCE_IO, 0);
	irq_rc = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!io_rc) {
		dev_err(dev, "missing IO resources\n");
		return -EINVAL;
	}

	lg->reg_base = io_rc->start;
	reg_len = resource_size(io_rc);

	if (!devm_request_region(dev, lg->reg_base, reg_len, "lp-gpio")) {
		dev_err(dev, "failed requesting IO region 0x%x\n",
			(unsigned int)lg->reg_base);
		return -EBUSY;
	}

	spin_lock_init(&lg->lock);

	gc = &lg->chip;
	gc->label = dev_name(dev);
	gc->owner = THIS_MODULE;
	gc->request = lp_gpio_request;
	gc->free = lp_gpio_free;
	gc->direction_input = lp_gpio_direction_input;
	gc->direction_output = lp_gpio_direction_output;
	gc->get = lp_gpio_get;
	gc->set = lp_gpio_set;
	gc->base = -1;
	gc->ngpio = LP_NUM_GPIO;
	gc->can_sleep = 0;
	gc->dev = dev;

	/* set up interrupts  */
	if (irq_rc && irq_rc->start) {
		hwirq = irq_rc->start;
		gc->to_irq = lp_gpio_to_irq;

		lg->domain = irq_domain_add_linear(NULL, LP_NUM_GPIO,
						   &lp_gpio_irq_ops, lg);
		if (!lg->domain)
			return -ENXIO;

		lp_gpio_irq_init_hw(lg);

		irq_set_handler_data(hwirq, lg);
		irq_set_chained_handler(hwirq, lp_gpio_irq_handler);
	}

	ret = gpiochip_add(gc);
	if (ret) {
		dev_err(dev, "failed adding lp-gpio chip\n");
		return ret;
	}
	pm_runtime_enable(dev);

	return 0;
}

static int lp_gpio_runtime_suspend(struct device *dev)
{
	return 0;
}

static int lp_gpio_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops lp_gpio_pm_ops = {
	.runtime_suspend = lp_gpio_runtime_suspend,
	.runtime_resume = lp_gpio_runtime_resume,
};

static const struct acpi_device_id lynxpoint_gpio_acpi_match[] = {
	{ "INT33C7", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, lynxpoint_gpio_acpi_match);

static int lp_gpio_remove(struct platform_device *pdev)
{
	struct lp_gpio *lg = platform_get_drvdata(pdev);
	int err;
	err = gpiochip_remove(&lg->chip);
	if (err)
		dev_warn(&pdev->dev, "failed to remove gpio_chip.\n");
	return 0;
}

static struct platform_driver lp_gpio_driver = {
	.probe          = lp_gpio_probe,
	.remove         = lp_gpio_remove,
	.driver         = {
		.name   = "lp_gpio",
		.owner  = THIS_MODULE,
		.pm	= &lp_gpio_pm_ops,
		.acpi_match_table = ACPI_PTR(lynxpoint_gpio_acpi_match),
	},
};

static int __init lp_gpio_init(void)
{
	return platform_driver_register(&lp_gpio_driver);
}

subsys_initcall(lp_gpio_init);
