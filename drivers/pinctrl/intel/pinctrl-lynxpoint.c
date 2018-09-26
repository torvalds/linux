// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO controller driver for Intel Lynxpoint PCH chipset>
 * Copyright (c) 2012, Intel Corporation.
 *
 * Author: Mathias Nyman <mathias.nyman@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>

/* LynxPoint chipset has support for 95 GPIO pins */

#define LP_NUM_GPIO	95

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
#define USE_SEL_MASK	GENMASK(1, 0)	/* 0: Native, 1: GPIO, ... */
#define USE_SEL_NATIVE	(0 << 0)
#define USE_SEL_GPIO	(1 << 0)

/* LP_CONFIG2 reg bits */
#define GPINDIS_BIT	BIT(2) /* disable input sensing */
#define GPIWP_BIT	(BIT(0) | BIT(1)) /* weak pull options */

struct lp_gpio {
	struct gpio_chip	chip;
	struct device		*dev;
	raw_spinlock_t		lock;
	void __iomem		*regs;
};

/*
 * Lynxpoint gpios are controlled through both bitmapped registers and
 * per gpio specific registers. The bitmapped registers are in chunks of
 * 3 x 32bit registers to cover all 95 GPIOs
 *
 * per gpio specific registers consist of two 32bit registers per gpio
 * (LP_CONFIG1 and LP_CONFIG2), with 95 GPIOs there's a total of
 * 190 config registers.
 *
 * A simplified view of the register layout look like this:
 *
 * LP_ACPI_OWNED[31:0] gpio ownerships for gpios 0-31  (bitmapped registers)
 * LP_ACPI_OWNED[63:32] gpio ownerships for gpios 32-63
 * LP_ACPI_OWNED[94:64] gpio ownerships for gpios 63-94
 * ...
 * LP_INT_ENABLE[31:0] ...
 * LP_INT_ENABLE[63:32] ...
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

static void __iomem *lp_gpio_reg(struct gpio_chip *chip, unsigned int offset,
				 int reg)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	int reg_offset;

	if (reg == LP_CONFIG1 || reg == LP_CONFIG2)
		/* per gpio specific config registers */
		reg_offset = offset * 8;
	else
		/* bitmapped registers */
		reg_offset = (offset / 32) * 4;

	return lg->regs + reg + reg_offset;
}

static int lp_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	void __iomem *conf2 = lp_gpio_reg(chip, offset, LP_CONFIG2);
	void __iomem *acpi_use = lp_gpio_reg(chip, offset, LP_ACPI_OWNED);
	u32 value;

	pm_runtime_get(lg->dev); /* should we put if failed */

	/* Fail if BIOS reserved pin for ACPI use */
	if (!(ioread32(acpi_use) & BIT(offset % 32))) {
		dev_err(lg->dev, "gpio %d reserved for ACPI\n", offset);
		return -EBUSY;
	}

	/*
	 * Reconfigure pin to GPIO mode if needed and issue a warning,
	 * since we expect firmware to configure it properly.
	 */
	value = ioread32(reg);
	if ((value & USE_SEL_MASK) != USE_SEL_GPIO) {
		iowrite32((value & USE_SEL_MASK) | USE_SEL_GPIO, reg);
		dev_warn(lg->dev, FW_BUG "pin %u forcibly reconfigured as GPIO\n", offset);
	}

	/* enable input sensing */
	iowrite32(ioread32(conf2) & ~GPINDIS_BIT, conf2);


	return 0;
}

static void lp_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *conf2 = lp_gpio_reg(chip, offset, LP_CONFIG2);

	/* disable input sensing */
	iowrite32(ioread32(conf2) | GPINDIS_BIT, conf2);

	pm_runtime_put(lg->dev);
}

static int lp_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct lp_gpio *lg = gpiochip_get_data(gc);
	u32 hwirq = irqd_to_hwirq(d);
	void __iomem *reg = lp_gpio_reg(&lg->chip, hwirq, LP_CONFIG1);
	unsigned long flags;
	u32 value;

	if (hwirq >= lg->chip.ngpio)
		return -EINVAL;

	raw_spin_lock_irqsave(&lg->lock, flags);
	value = ioread32(reg);

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

	iowrite32(value, reg);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static int lp_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	void __iomem *reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	return !!(ioread32(reg) & IN_LVL_BIT);
}

static void lp_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	raw_spin_lock_irqsave(&lg->lock, flags);

	if (value)
		iowrite32(ioread32(reg) | OUT_LVL_BIT, reg);
	else
		iowrite32(ioread32(reg) & ~OUT_LVL_BIT, reg);

	raw_spin_unlock_irqrestore(&lg->lock, flags);
}

static int lp_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	raw_spin_lock_irqsave(&lg->lock, flags);
	iowrite32(ioread32(reg) | DIR_BIT, reg);
	raw_spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static int lp_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *reg = lp_gpio_reg(chip, offset, LP_CONFIG1);
	unsigned long flags;

	lp_gpio_set(chip, offset, value);

	raw_spin_lock_irqsave(&lg->lock, flags);
	iowrite32(ioread32(reg) & ~DIR_BIT, reg);
	raw_spin_unlock_irqrestore(&lg->lock, flags);

	return 0;
}

static void lp_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct lp_gpio *lg = gpiochip_get_data(gc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	void __iomem *reg, *ena;
	unsigned long pending;
	u32 base, pin;

	/* check from GPIO controller which pin triggered the interrupt */
	for (base = 0; base < lg->chip.ngpio; base += 32) {
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_STAT);
		ena = lp_gpio_reg(&lg->chip, base, LP_INT_ENABLE);

		/* Only interrupts that are enabled */
		pending = ioread32(reg) & ioread32(ena);

		for_each_set_bit(pin, &pending, 32) {
			unsigned int irq;

			/* Clear before handling so we don't lose an edge */
			iowrite32(BIT(pin), reg);

			irq = irq_find_mapping(lg->chip.irq.domain, base + pin);
			generic_handle_irq(irq);
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
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct lp_gpio *lg = gpiochip_get_data(gc);
	u32 hwirq = irqd_to_hwirq(d);
	void __iomem *reg = lp_gpio_reg(&lg->chip, hwirq, LP_INT_ENABLE);
	unsigned long flags;

	raw_spin_lock_irqsave(&lg->lock, flags);
	iowrite32(ioread32(reg) | BIT(hwirq % 32), reg);
	raw_spin_unlock_irqrestore(&lg->lock, flags);
}

static void lp_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct lp_gpio *lg = gpiochip_get_data(gc);
	u32 hwirq = irqd_to_hwirq(d);
	void __iomem *reg = lp_gpio_reg(&lg->chip, hwirq, LP_INT_ENABLE);
	unsigned long flags;

	raw_spin_lock_irqsave(&lg->lock, flags);
	iowrite32(ioread32(reg) & ~BIT(hwirq % 32), reg);
	raw_spin_unlock_irqrestore(&lg->lock, flags);
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

static int lp_gpio_irq_init_hw(struct gpio_chip *chip)
{
	struct lp_gpio *lg = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int base;

	for (base = 0; base < lg->chip.ngpio; base += 32) {
		/* disable gpio pin interrupts */
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_ENABLE);
		iowrite32(0, reg);
		/* Clear interrupt status register */
		reg = lp_gpio_reg(&lg->chip, base, LP_INT_STAT);
		iowrite32(0xffffffff, reg);
	}

	return 0;
}

static int lp_gpio_probe(struct platform_device *pdev)
{
	struct lp_gpio *lg;
	struct gpio_chip *gc;
	struct resource *io_rc, *irq_rc;
	struct device *dev = &pdev->dev;
	void __iomem *regs;
	int ret;

	lg = devm_kzalloc(dev, sizeof(*lg), GFP_KERNEL);
	if (!lg)
		return -ENOMEM;

	lg->dev = dev;
	platform_set_drvdata(pdev, lg);

	io_rc = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!io_rc) {
		dev_err(dev, "missing IO resources\n");
		return -EINVAL;
	}

	regs = devm_ioport_map(dev, io_rc->start, resource_size(io_rc));
	if (!regs) {
		dev_err(dev, "failed mapping IO region %pR\n", &io_rc);
		return -EBUSY;
	}

	lg->regs = regs;

	raw_spin_lock_init(&lg->lock);

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
	gc->can_sleep = false;
	gc->parent = dev;

	/* set up interrupts  */
	irq_rc = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_rc && irq_rc->start) {
		struct gpio_irq_chip *girq;

		girq = &gc->irq;
		girq->chip = &lp_irqchip;
		girq->init_hw = lp_gpio_irq_init_hw;
		girq->parent_handler = lp_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, girq->num_parents,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = (unsigned int)irq_rc->start;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
	}

	ret = devm_gpiochip_add_data(dev, gc, lg);
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

static int lp_gpio_resume(struct device *dev)
{
	struct lp_gpio *lg = dev_get_drvdata(dev);
	void __iomem *reg;
	int i;

	/* on some hardware suspend clears input sensing, re-enable it here */
	for (i = 0; i < lg->chip.ngpio; i++) {
		if (gpiochip_is_requested(&lg->chip, i) != NULL) {
			reg = lp_gpio_reg(&lg->chip, i, LP_CONFIG2);
			iowrite32(ioread32(reg) & ~GPINDIS_BIT, reg);
		}
	}
	return 0;
}

static const struct dev_pm_ops lp_gpio_pm_ops = {
	.runtime_suspend = lp_gpio_runtime_suspend,
	.runtime_resume = lp_gpio_runtime_resume,
	.resume = lp_gpio_resume,
};

static const struct acpi_device_id lynxpoint_gpio_acpi_match[] = {
	{ "INT33C7", 0 },
	{ "INT3437", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, lynxpoint_gpio_acpi_match);

static int lp_gpio_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct platform_driver lp_gpio_driver = {
	.probe          = lp_gpio_probe,
	.remove         = lp_gpio_remove,
	.driver         = {
		.name   = "lp_gpio",
		.pm	= &lp_gpio_pm_ops,
		.acpi_match_table = ACPI_PTR(lynxpoint_gpio_acpi_match),
	},
};

static int __init lp_gpio_init(void)
{
	return platform_driver_register(&lp_gpio_driver);
}

static void __exit lp_gpio_exit(void)
{
	platform_driver_unregister(&lp_gpio_driver);
}

subsys_initcall(lp_gpio_init);
module_exit(lp_gpio_exit);

MODULE_AUTHOR("Mathias Nyman (Intel)");
MODULE_DESCRIPTION("GPIO interface for Intel Lynxpoint");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:lp_gpio");
