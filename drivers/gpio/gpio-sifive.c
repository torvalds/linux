// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 SiFive
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>

#define SIFIVE_GPIO_INPUT_VAL	0x00
#define SIFIVE_GPIO_INPUT_EN	0x04
#define SIFIVE_GPIO_OUTPUT_EN	0x08
#define SIFIVE_GPIO_OUTPUT_VAL	0x0C
#define SIFIVE_GPIO_RISE_IE	0x18
#define SIFIVE_GPIO_RISE_IP	0x1C
#define SIFIVE_GPIO_FALL_IE	0x20
#define SIFIVE_GPIO_FALL_IP	0x24
#define SIFIVE_GPIO_HIGH_IE	0x28
#define SIFIVE_GPIO_HIGH_IP	0x2C
#define SIFIVE_GPIO_LOW_IE	0x30
#define SIFIVE_GPIO_LOW_IP	0x34
#define SIFIVE_GPIO_OUTPUT_XOR	0x40

#define SIFIVE_GPIO_MAX		32

struct sifive_gpio {
	void __iomem		*base;
	struct gpio_chip	gc;
	struct regmap		*regs;
	unsigned long		irq_state;
	unsigned int		trigger[SIFIVE_GPIO_MAX];
	unsigned int		irq_number[SIFIVE_GPIO_MAX];
};

static void sifive_gpio_set_ie(struct sifive_gpio *chip, unsigned int offset)
{
	unsigned long flags;
	unsigned int trigger;

	raw_spin_lock_irqsave(&chip->gc.bgpio_lock, flags);
	trigger = (chip->irq_state & BIT(offset)) ? chip->trigger[offset] : 0;
	regmap_update_bits(chip->regs, SIFIVE_GPIO_RISE_IE, BIT(offset),
			   (trigger & IRQ_TYPE_EDGE_RISING) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, SIFIVE_GPIO_FALL_IE, BIT(offset),
			   (trigger & IRQ_TYPE_EDGE_FALLING) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, SIFIVE_GPIO_HIGH_IE, BIT(offset),
			   (trigger & IRQ_TYPE_LEVEL_HIGH) ? BIT(offset) : 0);
	regmap_update_bits(chip->regs, SIFIVE_GPIO_LOW_IE, BIT(offset),
			   (trigger & IRQ_TYPE_LEVEL_LOW) ? BIT(offset) : 0);
	raw_spin_unlock_irqrestore(&chip->gc.bgpio_lock, flags);
}

static int sifive_gpio_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sifive_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d);

	if (offset < 0 || offset >= gc->ngpio)
		return -EINVAL;

	chip->trigger[offset] = trigger;
	sifive_gpio_set_ie(chip, offset);
	return 0;
}

static void sifive_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sifive_gpio *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	int offset = hwirq % SIFIVE_GPIO_MAX;
	u32 bit = BIT(offset);
	unsigned long flags;

	gpiochip_enable_irq(gc, hwirq);
	irq_chip_enable_parent(d);

	/* Switch to input */
	gc->direction_input(gc, offset);

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	/* Clear any sticky pending interrupts */
	regmap_write(chip->regs, SIFIVE_GPIO_RISE_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_FALL_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_HIGH_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_LOW_IP, bit);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	/* Enable interrupts */
	assign_bit(offset, &chip->irq_state, 1);
	sifive_gpio_set_ie(chip, offset);
}

static void sifive_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sifive_gpio *chip = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	int offset = hwirq % SIFIVE_GPIO_MAX;

	assign_bit(offset, &chip->irq_state, 0);
	sifive_gpio_set_ie(chip, offset);
	irq_chip_disable_parent(d);
	gpiochip_disable_irq(gc, hwirq);
}

static void sifive_gpio_irq_eoi(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sifive_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d) % SIFIVE_GPIO_MAX;
	u32 bit = BIT(offset);
	unsigned long flags;

	raw_spin_lock_irqsave(&gc->bgpio_lock, flags);
	/* Clear all pending interrupts */
	regmap_write(chip->regs, SIFIVE_GPIO_RISE_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_FALL_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_HIGH_IP, bit);
	regmap_write(chip->regs, SIFIVE_GPIO_LOW_IP, bit);
	raw_spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	irq_chip_eoi_parent(d);
}

static int sifive_gpio_irq_set_affinity(struct irq_data *data,
					const struct cpumask *dest,
					bool force)
{
	if (data->parent_data)
		return irq_chip_set_affinity_parent(data, dest, force);

	return -EINVAL;
}

static const struct irq_chip sifive_gpio_irqchip = {
	.name		= "sifive-gpio",
	.irq_set_type	= sifive_gpio_irq_set_type,
	.irq_mask	= irq_chip_mask_parent,
	.irq_unmask	= irq_chip_unmask_parent,
	.irq_enable	= sifive_gpio_irq_enable,
	.irq_disable	= sifive_gpio_irq_disable,
	.irq_eoi	= sifive_gpio_irq_eoi,
	.irq_set_affinity = sifive_gpio_irq_set_affinity,
	.irq_set_wake	= irq_chip_set_wake_parent,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int sifive_gpio_child_to_parent_hwirq(struct gpio_chip *gc,
					     unsigned int child,
					     unsigned int child_type,
					     unsigned int *parent,
					     unsigned int *parent_type)
{
	struct sifive_gpio *chip = gpiochip_get_data(gc);
	struct irq_data *d = irq_get_irq_data(chip->irq_number[child]);

	*parent_type = IRQ_TYPE_NONE;
	*parent = irqd_to_hwirq(d);

	return 0;
}

static const struct regmap_config sifive_gpio_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.disable_locking = true,
};

static int sifive_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct irq_domain *parent;
	struct gpio_irq_chip *girq;
	struct sifive_gpio *chip;
	int ret, ngpio;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base)) {
		dev_err(dev, "failed to allocate device memory\n");
		return PTR_ERR(chip->base);
	}

	chip->regs = devm_regmap_init_mmio(dev, chip->base,
					   &sifive_gpio_regmap_config);
	if (IS_ERR(chip->regs))
		return PTR_ERR(chip->regs);

	for (ngpio = 0; ngpio < SIFIVE_GPIO_MAX; ngpio++) {
		ret = platform_get_irq_optional(pdev, ngpio);
		if (ret < 0)
			break;
		chip->irq_number[ngpio] = ret;
	}
	if (!ngpio) {
		dev_err(dev, "no IRQ found\n");
		return -ENODEV;
	}

	/*
	 * The check above ensures at least one parent IRQ is valid.
	 * Assume all parent IRQs belong to the same domain.
	 */
	parent = irq_get_irq_data(chip->irq_number[0])->domain;

	ret = bgpio_init(&chip->gc, dev, 4,
			 chip->base + SIFIVE_GPIO_INPUT_VAL,
			 chip->base + SIFIVE_GPIO_OUTPUT_VAL,
			 NULL,
			 chip->base + SIFIVE_GPIO_OUTPUT_EN,
			 chip->base + SIFIVE_GPIO_INPUT_EN,
			 BGPIOF_READ_OUTPUT_REG_SET);
	if (ret) {
		dev_err(dev, "unable to init generic GPIO\n");
		return ret;
	}

	/* Disable all GPIO interrupts before enabling parent interrupts */
	regmap_write(chip->regs, SIFIVE_GPIO_RISE_IE, 0);
	regmap_write(chip->regs, SIFIVE_GPIO_FALL_IE, 0);
	regmap_write(chip->regs, SIFIVE_GPIO_HIGH_IE, 0);
	regmap_write(chip->regs, SIFIVE_GPIO_LOW_IE, 0);
	chip->irq_state = 0;

	chip->gc.base = -1;
	chip->gc.ngpio = ngpio;
	chip->gc.label = dev_name(dev);
	chip->gc.parent = dev;
	chip->gc.owner = THIS_MODULE;
	girq = &chip->gc.irq;
	gpio_irq_chip_set_chip(girq, &sifive_gpio_irqchip);
	girq->fwnode = dev_fwnode(dev);
	girq->parent_domain = parent;
	girq->child_to_parent_hwirq = sifive_gpio_child_to_parent_hwirq;
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;

	return gpiochip_add_data(&chip->gc, chip);
}

static const struct of_device_id sifive_gpio_match[] = {
	{ .compatible = "sifive,gpio0" },
	{ .compatible = "sifive,fu540-c000-gpio" },
	{ },
};

static struct platform_driver sifive_gpio_driver = {
	.probe		= sifive_gpio_probe,
	.driver = {
		.name	= "sifive_gpio",
		.of_match_table = sifive_gpio_match,
	},
};
module_platform_driver(sifive_gpio_driver)

MODULE_AUTHOR("Yash Shah <yash.shah@sifive.com>");
MODULE_DESCRIPTION("SiFive GPIO driver");
MODULE_LICENSE("GPL");
