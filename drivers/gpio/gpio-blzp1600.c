// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 VeriSilicon Limited.
 * Copyright (C) 2025 Blaize, Inc.
 */

#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define GPIO_DIR_REG	0x00
#define GPIO_CTRL_REG	0x04
#define GPIO_SET_REG	0x08
#define GPIO_CLR_REG	0x0C
#define GPIO_ODATA_REG	0x10
#define GPIO_IDATA_REG	0x14
#define GPIO_IEN_REG	0x18
#define GPIO_IS_REG	0x1C
#define GPIO_IBE_REG	0x20
#define GPIO_IEV_REG	0x24
#define GPIO_RIS_REG	0x28
#define GPIO_IM_REG	0x2C
#define GPIO_MIS_REG	0x30
#define GPIO_IC_REG	0x34
#define GPIO_DB_REG	0x38
#define GPIO_DFG_REG	0x3C

#define DRIVER_NAME "blzp1600-gpio"

struct blzp1600_gpio {
	void __iomem *base;
	struct gpio_generic_chip gen_gc;
	int irq;
};

static inline struct blzp1600_gpio *get_blzp1600_gpio_from_irq_data(struct irq_data *d)
{
	return gpiochip_get_data(irq_data_get_irq_chip_data(d));
}

static inline struct blzp1600_gpio *get_blzp1600_gpio_from_irq_desc(struct irq_desc *d)
{
	return gpiochip_get_data(irq_desc_get_handler_data(d));
}

static inline u32 blzp1600_gpio_read(struct blzp1600_gpio *chip, unsigned int offset)
{
	return readl_relaxed(chip->base + offset);
}

static inline void blzp1600_gpio_write(struct blzp1600_gpio *chip, unsigned int offset, u32 val)
{
	writel_relaxed(val, chip->base + offset);
}

static inline void blzp1600_gpio_rmw(void __iomem *reg, u32 mask, bool set)
{
	u32 val = readl_relaxed(reg);

	if (set)
		val |= mask;
	else
		val &= ~mask;

	writel_relaxed(val, reg);
}

static void blzp1600_gpio_irq_mask(struct irq_data *d)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	blzp1600_gpio_rmw(chip->base + GPIO_IM_REG, BIT(d->hwirq), 1);
}

static void blzp1600_gpio_irq_unmask(struct irq_data *d)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	blzp1600_gpio_rmw(chip->base + GPIO_IM_REG, BIT(d->hwirq), 0);
}

static void blzp1600_gpio_irq_ack(struct irq_data *d)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);

	blzp1600_gpio_write(chip, GPIO_IC_REG, BIT(d->hwirq));
}

static void blzp1600_gpio_irq_enable(struct irq_data *d)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);

	gpiochip_enable_irq(&chip->gen_gc.gc, irqd_to_hwirq(d));

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	blzp1600_gpio_rmw(chip->base + GPIO_DIR_REG, BIT(d->hwirq), 0);
	blzp1600_gpio_rmw(chip->base + GPIO_IEN_REG, BIT(d->hwirq), 1);
}

static void blzp1600_gpio_irq_disable(struct irq_data *d)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	blzp1600_gpio_rmw(chip->base + GPIO_IEN_REG, BIT(d->hwirq), 0);
	gpiochip_disable_irq(&chip->gen_gc.gc, irqd_to_hwirq(d));
}

static int blzp1600_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct blzp1600_gpio *chip = get_blzp1600_gpio_from_irq_data(d);
	u32 edge_level, single_both, fall_rise;
	int mask = BIT(d->hwirq);

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	edge_level = blzp1600_gpio_read(chip, GPIO_IS_REG);
	single_both = blzp1600_gpio_read(chip, GPIO_IBE_REG);
	fall_rise = blzp1600_gpio_read(chip, GPIO_IEV_REG);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		edge_level &= ~mask;
		single_both |= mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		edge_level &= ~mask;
		single_both &= ~mask;
		fall_rise |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		edge_level &= ~mask;
		single_both &= ~mask;
		fall_rise &= ~mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		edge_level |= mask;
		fall_rise |= mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		edge_level |= mask;
		fall_rise &= ~mask;
		break;
	default:
		return -EINVAL;
	}

	blzp1600_gpio_write(chip, GPIO_IS_REG, edge_level);
	blzp1600_gpio_write(chip, GPIO_IBE_REG, single_both);
	blzp1600_gpio_write(chip, GPIO_IEV_REG, fall_rise);

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static const struct irq_chip blzp1600_gpio_irqchip = {
	.name = DRIVER_NAME,
	.irq_ack = blzp1600_gpio_irq_ack,
	.irq_mask = blzp1600_gpio_irq_mask,
	.irq_unmask = blzp1600_gpio_irq_unmask,
	.irq_set_type = blzp1600_gpio_irq_set_type,
	.irq_enable = blzp1600_gpio_irq_enable,
	.irq_disable = blzp1600_gpio_irq_disable,
	.flags = IRQCHIP_IMMUTABLE | IRQCHIP_MASK_ON_SUSPEND,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void blzp1600_gpio_irqhandler(struct irq_desc *desc)
{
	struct blzp1600_gpio *gpio = get_blzp1600_gpio_from_irq_desc(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long irq_status;
	int hwirq = 0;

	chained_irq_enter(irqchip, desc);
	irq_status = blzp1600_gpio_read(gpio, GPIO_RIS_REG);
	for_each_set_bit(hwirq, &irq_status, gpio->gen_gc.gc.ngpio)
		generic_handle_domain_irq(gpio->gen_gc.gc.irq.domain, hwirq);

	chained_irq_exit(irqchip, desc);
}

static int blzp1600_gpio_set_debounce(struct gpio_chip *gc, unsigned int offset,
				      unsigned int debounce)
{
	struct blzp1600_gpio *chip = gpiochip_get_data(gc);

	guard(gpio_generic_lock_irqsave)(&chip->gen_gc);
	blzp1600_gpio_rmw(chip->base + GPIO_DB_REG, BIT(offset), debounce);

	return 0;
}

static int blzp1600_gpio_set_config(struct gpio_chip *gc, unsigned int offset, unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return blzp1600_gpio_set_debounce(gc, offset, debounce);
}

static int blzp1600_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct blzp1600_gpio *chip;
	struct gpio_chip *gc;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	config = (struct gpio_generic_chip_config) {
		.dev = &pdev->dev,
		.sz = 4,
		.dat = chip->base + GPIO_IDATA_REG,
		.set = chip->base + GPIO_SET_REG,
		.clr = chip->base + GPIO_CLR_REG,
		.dirout = chip->base + GPIO_DIR_REG,
	};

	ret = gpio_generic_chip_init(&chip->gen_gc, &config);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to register generic gpio\n");

	/* configure the gpio chip */
	gc = &chip->gen_gc.gc;
	gc->set_config = blzp1600_gpio_set_config;

	if (device_property_present(&pdev->dev, "interrupt-controller")) {
		struct gpio_irq_chip *girq;

		chip->irq = platform_get_irq(pdev, 0);
		if (chip->irq < 0)
			return chip->irq;

		girq = &gc->irq;
		gpio_irq_chip_set_chip(girq, &blzp1600_gpio_irqchip);
		girq->parent_handler = blzp1600_gpio_irqhandler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1, sizeof(*girq->parents), GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;

		girq->parents[0] = chip->irq;
		girq->default_type = IRQ_TYPE_NONE;
	}

	return devm_gpiochip_add_data(&pdev->dev, gc, chip);
}

static const struct of_device_id blzp1600_gpio_of_match[] = {
	{ .compatible = "blaize,blzp1600-gpio", },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, blzp1600_gpio_of_match);

static struct platform_driver blzp1600_gpio_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = blzp1600_gpio_of_match,
	},
	.probe		= blzp1600_gpio_probe,
};

module_platform_driver(blzp1600_gpio_driver);

MODULE_AUTHOR("Nikolaos Pasaloukos <nikolaos.pasaloukos@blaize.com>");
MODULE_DESCRIPTION("Blaize BLZP1600 GPIO driver");
MODULE_LICENSE("GPL");
