// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Tangier GPIO driver
 *
 * Copyright (c) 2016, 2021, 2023 Intel Corporation.
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Pandith N <pandith.n@intel.com>
 *          Raag Jadav <raag.jadav@intel.com>
 */

#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <linux/string_helpers.h>
#include <linux/types.h>

#include <linux/gpio/driver.h>

#include "gpio-tangier.h"

#define GCCR		0x000	/* Controller configuration */
#define GPLR		0x004	/* Pin level r/o */
#define GPDR		0x01c	/* Pin direction */
#define GPSR		0x034	/* Pin set w/o */
#define GPCR		0x04c	/* Pin clear w/o */
#define GRER		0x064	/* Rising edge detect */
#define GFER		0x07c	/* Falling edge detect */
#define GFBR		0x094	/* Glitch filter bypass */
#define GIMR		0x0ac	/* Interrupt mask */
#define GISR		0x0c4	/* Interrupt source */
#define GITR		0x300	/* Input type */
#define GLPR		0x318	/* Level input polarity */

/**
 * struct tng_gpio_context - Context to be saved during suspend-resume
 * @level: Pin level
 * @gpdr: Pin direction
 * @grer: Rising edge detect enable
 * @gfer: Falling edge detect enable
 * @gimr: Interrupt mask
 * @gwmr: Wake mask
 */
struct tng_gpio_context {
	u32 level;
	u32 gpdr;
	u32 grer;
	u32 gfer;
	u32 gimr;
	u32 gwmr;
};

static void __iomem *gpio_reg(struct gpio_chip *chip, unsigned int offset,
			      unsigned int reg)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	u8 reg_offset = offset / 32;

	return priv->reg_base + reg + reg_offset * 4;
}

static void __iomem *gpio_reg_and_bit(struct gpio_chip *chip, unsigned int offset,
				      unsigned int reg, u8 *bit)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	u8 reg_offset = offset / 32;
	u8 shift = offset % 32;

	*bit = shift;
	return priv->reg_base + reg + reg_offset * 4;
}

static int tng_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	void __iomem *gplr;
	u8 shift;

	gplr = gpio_reg_and_bit(chip, offset, GPLR, &shift);

	return !!(readl(gplr) & BIT(shift));
}

static void tng_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	void __iomem *reg;
	u8 shift;

	reg = gpio_reg_and_bit(chip, offset, value ? GPSR : GPCR, &shift);

	guard(raw_spinlock_irqsave)(&priv->lock);

	writel(BIT(shift), reg);
}

static int tng_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gpdr;
	u32 value;
	u8 shift;

	gpdr = gpio_reg_and_bit(chip, offset, GPDR, &shift);

	guard(raw_spinlock_irqsave)(&priv->lock);

	value = readl(gpdr);
	value &= ~BIT(shift);
	writel(value, gpdr);

	return 0;
}

static int tng_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				     int value)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gpdr;
	u8 shift;

	gpdr = gpio_reg_and_bit(chip, offset, GPDR, &shift);
	tng_gpio_set(chip, offset, value);

	guard(raw_spinlock_irqsave)(&priv->lock);

	value = readl(gpdr);
	value |= BIT(shift);
	writel(value, gpdr);

	return 0;
}

static int tng_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	void __iomem *gpdr;
	u8 shift;

	gpdr = gpio_reg_and_bit(chip, offset, GPDR, &shift);

	if (readl(gpdr) & BIT(shift))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int tng_gpio_set_debounce(struct gpio_chip *chip, unsigned int offset,
				 unsigned int debounce)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gfbr;
	u32 value;
	u8 shift;

	gfbr = gpio_reg_and_bit(chip, offset, GFBR, &shift);

	guard(raw_spinlock_irqsave)(&priv->lock);

	value = readl(gfbr);
	if (debounce)
		value &= ~BIT(shift);
	else
		value |= BIT(shift);
	writel(value, gfbr);

	return 0;
}

static int tng_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	u32 debounce;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		return gpiochip_generic_config(chip, offset, config);
	case PIN_CONFIG_INPUT_DEBOUNCE:
		debounce = pinconf_to_config_argument(config);
		return tng_gpio_set_debounce(chip, offset, debounce);
	default:
		return -ENOTSUPP;
	}
}

static void tng_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *gisr;
	u8 shift;

	gisr = gpio_reg_and_bit(&priv->chip, gpio, GISR, &shift);

	guard(raw_spinlock_irqsave)(&priv->lock);

	writel(BIT(shift), gisr);
}

static void tng_irq_unmask_mask(struct tng_gpio *priv, u32 gpio, bool unmask)
{
	void __iomem *gimr;
	u32 value;
	u8 shift;

	gimr = gpio_reg_and_bit(&priv->chip, gpio, GIMR, &shift);

	guard(raw_spinlock_irqsave)(&priv->lock);

	value = readl(gimr);
	if (unmask)
		value |= BIT(shift);
	else
		value &= ~BIT(shift);
	writel(value, gimr);
}

static void tng_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);

	tng_irq_unmask_mask(priv, gpio, false);
	gpiochip_disable_irq(&priv->chip, gpio);
}

static void tng_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);

	gpiochip_enable_irq(&priv->chip, gpio);
	tng_irq_unmask_mask(priv, gpio, true);
}

static int tng_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *grer = gpio_reg(&priv->chip, gpio, GRER);
	void __iomem *gfer = gpio_reg(&priv->chip, gpio, GFER);
	void __iomem *gitr = gpio_reg(&priv->chip, gpio, GITR);
	void __iomem *glpr = gpio_reg(&priv->chip, gpio, GLPR);
	u8 shift = gpio % 32;
	u32 value;

	guard(raw_spinlock_irqsave)(&priv->lock);

	value = readl(grer);
	if (type & IRQ_TYPE_EDGE_RISING)
		value |= BIT(shift);
	else
		value &= ~BIT(shift);
	writel(value, grer);

	value = readl(gfer);
	if (type & IRQ_TYPE_EDGE_FALLING)
		value |= BIT(shift);
	else
		value &= ~BIT(shift);
	writel(value, gfer);

	/*
	 * To prevent glitches from triggering an unintended level interrupt,
	 * configure GLPR register first and then configure GITR.
	 */
	value = readl(glpr);
	if (type & IRQ_TYPE_LEVEL_LOW)
		value |= BIT(shift);
	else
		value &= ~BIT(shift);
	writel(value, glpr);

	if (type & IRQ_TYPE_LEVEL_MASK) {
		value = readl(gitr);
		value |= BIT(shift);
		writel(value, gitr);

		irq_set_handler_locked(d, handle_level_irq);
	} else if (type & IRQ_TYPE_EDGE_BOTH) {
		value = readl(gitr);
		value &= ~BIT(shift);
		writel(value, gitr);

		irq_set_handler_locked(d, handle_edge_irq);
	}

	return 0;
}

static int tng_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *gwmr = gpio_reg(&priv->chip, gpio, priv->wake_regs.gwmr);
	void __iomem *gwsr = gpio_reg(&priv->chip, gpio, priv->wake_regs.gwsr);
	u8 shift = gpio % 32;
	u32 value;

	dev_dbg(priv->dev, "%s wake for gpio %lu\n", str_enable_disable(on), gpio);

	guard(raw_spinlock_irqsave)(&priv->lock);

	/* Clear the existing wake status */
	writel(BIT(shift), gwsr);

	value = readl(gwmr);
	if (on)
		value |= BIT(shift);
	else
		value &= ~BIT(shift);
	writel(value, gwmr);

	return 0;
}

static const struct irq_chip tng_irqchip = {
	.name		= "gpio-tangier",
	.irq_ack	= tng_irq_ack,
	.irq_mask	= tng_irq_mask,
	.irq_unmask	= tng_irq_unmask,
	.irq_set_type	= tng_irq_set_type,
	.irq_set_wake	= tng_irq_set_wake,
	.flags          = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void tng_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct tng_gpio *priv = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long base, gpio;

	chained_irq_enter(irqchip, desc);

	/* Check GPIO controller to check which pin triggered the interrupt */
	for (base = 0; base < priv->chip.ngpio; base += 32) {
		void __iomem *gisr = gpio_reg(&priv->chip, base, GISR);
		void __iomem *gimr = gpio_reg(&priv->chip, base, GIMR);
		unsigned long pending, enabled;

		pending = readl(gisr);
		enabled = readl(gimr);

		/* Only interrupts that are enabled */
		pending &= enabled;

		for_each_set_bit(gpio, &pending, 32)
			generic_handle_domain_irq(gc->irq.domain, base + gpio);
	}

	chained_irq_exit(irqchip, desc);
}

static int tng_irq_init_hw(struct gpio_chip *chip)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int base;

	for (base = 0; base < priv->chip.ngpio; base += 32) {
		/* Clear the rising-edge detect register */
		reg = gpio_reg(&priv->chip, base, GRER);
		writel(0, reg);

		/* Clear the falling-edge detect register */
		reg = gpio_reg(&priv->chip, base, GFER);
		writel(0, reg);
	}

	return 0;
}

static int tng_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct tng_gpio *priv = gpiochip_get_data(chip);
	const struct tng_gpio_pinrange *range;
	unsigned int i;
	int ret;

	for (i = 0; i < priv->pin_info.nranges; i++) {
		range = &priv->pin_info.pin_ranges[i];
		ret = gpiochip_add_pin_range(&priv->chip,
					     priv->pin_info.name,
					     range->gpio_base,
					     range->pin_base,
					     range->npins);
		if (ret) {
			dev_err(priv->dev, "failed to add GPIO pin range\n");
			return ret;
		}
	}

	return 0;
}

int devm_tng_gpio_probe(struct device *dev, struct tng_gpio *gpio)
{
	const struct tng_gpio_info *info = &gpio->info;
	size_t nctx = DIV_ROUND_UP(info->ngpio, 32);
	struct gpio_irq_chip *girq;
	int ret;

	gpio->ctx = devm_kcalloc(dev, nctx, sizeof(*gpio->ctx), GFP_KERNEL);
	if (!gpio->ctx)
		return -ENOMEM;

	gpio->chip.label = dev_name(dev);
	gpio->chip.parent = dev;
	gpio->chip.request = gpiochip_generic_request;
	gpio->chip.free = gpiochip_generic_free;
	gpio->chip.direction_input = tng_gpio_direction_input;
	gpio->chip.direction_output = tng_gpio_direction_output;
	gpio->chip.get = tng_gpio_get;
	gpio->chip.set = tng_gpio_set;
	gpio->chip.get_direction = tng_gpio_get_direction;
	gpio->chip.set_config = tng_gpio_set_config;
	gpio->chip.base = info->base;
	gpio->chip.ngpio = info->ngpio;
	gpio->chip.can_sleep = false;
	gpio->chip.add_pin_ranges = tng_gpio_add_pin_ranges;

	raw_spin_lock_init(&gpio->lock);

	girq = &gpio->chip.irq;
	gpio_irq_chip_set_chip(girq, &tng_irqchip);
	girq->init_hw = tng_irq_init_hw;
	girq->parent_handler = tng_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, girq->num_parents,
				     sizeof(*girq->parents), GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;

	girq->parents[0] = gpio->irq;
	girq->first = info->first;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	ret = devm_gpiochip_add_data(dev, &gpio->chip, gpio);
	if (ret)
		return dev_err_probe(dev, ret, "gpiochip_add error\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(devm_tng_gpio_probe, "GPIO_TANGIER");

static int tng_gpio_suspend(struct device *dev)
{
	struct tng_gpio *priv = dev_get_drvdata(dev);
	struct tng_gpio_context *ctx = priv->ctx;
	unsigned int base;

	guard(raw_spinlock_irqsave)(&priv->lock);

	for (base = 0; base < priv->chip.ngpio; base += 32, ctx++) {
		/* GPLR is RO, values read will be restored using GPSR */
		ctx->level = readl(gpio_reg(&priv->chip, base, GPLR));

		ctx->gpdr = readl(gpio_reg(&priv->chip, base, GPDR));
		ctx->grer = readl(gpio_reg(&priv->chip, base, GRER));
		ctx->gfer = readl(gpio_reg(&priv->chip, base, GFER));
		ctx->gimr = readl(gpio_reg(&priv->chip, base, GIMR));

		ctx->gwmr = readl(gpio_reg(&priv->chip, base, priv->wake_regs.gwmr));
	}

	return 0;
}

static int tng_gpio_resume(struct device *dev)
{
	struct tng_gpio *priv = dev_get_drvdata(dev);
	struct tng_gpio_context *ctx = priv->ctx;
	unsigned int base;

	guard(raw_spinlock_irqsave)(&priv->lock);

	for (base = 0; base < priv->chip.ngpio; base += 32, ctx++) {
		/* GPLR is RO, values read will be restored using GPSR */
		writel(ctx->level, gpio_reg(&priv->chip, base, GPSR));

		writel(ctx->gpdr, gpio_reg(&priv->chip, base, GPDR));
		writel(ctx->grer, gpio_reg(&priv->chip, base, GRER));
		writel(ctx->gfer, gpio_reg(&priv->chip, base, GFER));
		writel(ctx->gimr, gpio_reg(&priv->chip, base, GIMR));

		writel(ctx->gwmr, gpio_reg(&priv->chip, base, priv->wake_regs.gwmr));
	}

	return 0;
}

EXPORT_NS_GPL_SIMPLE_DEV_PM_OPS(tng_gpio_pm_ops, tng_gpio_suspend, tng_gpio_resume, GPIO_TANGIER);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Pandith N <pandith.n@intel.com>");
MODULE_AUTHOR("Raag Jadav <raag.jadav@intel.com>");
MODULE_DESCRIPTION("Intel Tangier GPIO driver");
MODULE_LICENSE("GPL");
