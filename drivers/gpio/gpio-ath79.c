// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Atheros AR71XX/AR724X/AR913X GPIO API support
 *
 *  Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 *  Copyright (C) 2010-2011 Jaiganesh Narayanan <jnarayanan@atheros.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define AR71XX_GPIO_REG_OE		0x00
#define AR71XX_GPIO_REG_IN		0x04
#define AR71XX_GPIO_REG_SET		0x0c
#define AR71XX_GPIO_REG_CLEAR		0x10

#define AR71XX_GPIO_REG_INT_ENABLE	0x14
#define AR71XX_GPIO_REG_INT_TYPE	0x18
#define AR71XX_GPIO_REG_INT_POLARITY	0x1c
#define AR71XX_GPIO_REG_INT_PENDING	0x20
#define AR71XX_GPIO_REG_INT_MASK	0x24

struct ath79_gpio_ctrl {
	struct gpio_generic_chip chip;
	void __iomem *base;
	unsigned long both_edges;
};

static struct ath79_gpio_ctrl *irq_data_to_ath79_gpio(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct gpio_generic_chip *gen_gc = to_gpio_generic_chip(gc);

	return container_of(gen_gc, struct ath79_gpio_ctrl, chip);
}

static u32 ath79_gpio_read(struct ath79_gpio_ctrl *ctrl, unsigned reg)
{
	return readl(ctrl->base + reg);
}

static void ath79_gpio_write(struct ath79_gpio_ctrl *ctrl,
			unsigned reg, u32 val)
{
	writel(val, ctrl->base + reg);
}

static bool ath79_gpio_update_bits(
	struct ath79_gpio_ctrl *ctrl, unsigned reg, u32 mask, u32 bits)
{
	u32 old_val, new_val;

	old_val = ath79_gpio_read(ctrl, reg);
	new_val = (old_val & ~mask) | (bits & mask);

	if (new_val != old_val)
		ath79_gpio_write(ctrl, reg, new_val);

	return new_val != old_val;
}

static void ath79_gpio_irq_unmask(struct irq_data *data)
{
	struct ath79_gpio_ctrl *ctrl = irq_data_to_ath79_gpio(data);
	u32 mask = BIT(irqd_to_hwirq(data));

	gpiochip_enable_irq(&ctrl->chip.gc, irqd_to_hwirq(data));

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);

	ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_MASK, mask, mask);
}

static void ath79_gpio_irq_mask(struct irq_data *data)
{
	struct ath79_gpio_ctrl *ctrl = irq_data_to_ath79_gpio(data);
	u32 mask = BIT(irqd_to_hwirq(data));

	scoped_guard(gpio_generic_lock_irqsave, &ctrl->chip)
		ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_MASK, mask, 0);

	gpiochip_disable_irq(&ctrl->chip.gc, irqd_to_hwirq(data));
}

static void ath79_gpio_irq_enable(struct irq_data *data)
{
	struct ath79_gpio_ctrl *ctrl = irq_data_to_ath79_gpio(data);
	u32 mask = BIT(irqd_to_hwirq(data));

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);
	ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_ENABLE, mask, mask);
	ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_MASK, mask, mask);
}

static void ath79_gpio_irq_disable(struct irq_data *data)
{
	struct ath79_gpio_ctrl *ctrl = irq_data_to_ath79_gpio(data);
	u32 mask = BIT(irqd_to_hwirq(data));

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);
	ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_MASK, mask, 0);
	ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_ENABLE, mask, 0);
}

static int ath79_gpio_irq_set_type(struct irq_data *data,
				unsigned int flow_type)
{
	struct ath79_gpio_ctrl *ctrl = irq_data_to_ath79_gpio(data);
	u32 mask = BIT(irqd_to_hwirq(data));
	u32 type = 0, polarity = 0;
	bool disabled;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		polarity |= mask;
		fallthrough;
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		polarity |= mask;
		fallthrough;
	case IRQ_TYPE_LEVEL_LOW:
		type |= mask;
		break;

	default:
		return -EINVAL;
	}

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);

	if (flow_type == IRQ_TYPE_EDGE_BOTH) {
		ctrl->both_edges |= mask;
		polarity = ~ath79_gpio_read(ctrl, AR71XX_GPIO_REG_IN);
	} else {
		ctrl->both_edges &= ~mask;
	}

	/* As the IRQ configuration can't be loaded atomically we
	 * have to disable the interrupt while the configuration state
	 * is invalid.
	 */
	disabled = ath79_gpio_update_bits(
		ctrl, AR71XX_GPIO_REG_INT_ENABLE, mask, 0);

	ath79_gpio_update_bits(
		ctrl, AR71XX_GPIO_REG_INT_TYPE, mask, type);
	ath79_gpio_update_bits(
		ctrl, AR71XX_GPIO_REG_INT_POLARITY, mask, polarity);

	if (disabled)
		ath79_gpio_update_bits(
			ctrl, AR71XX_GPIO_REG_INT_ENABLE, mask, mask);

	return 0;
}

static const struct irq_chip ath79_gpio_irqchip = {
	.name = "gpio-ath79",
	.irq_enable = ath79_gpio_irq_enable,
	.irq_disable = ath79_gpio_irq_disable,
	.irq_mask = ath79_gpio_irq_mask,
	.irq_unmask = ath79_gpio_irq_unmask,
	.irq_set_type = ath79_gpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void ath79_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	struct gpio_generic_chip *gen_gc = to_gpio_generic_chip(gc);
	struct ath79_gpio_ctrl *ctrl =
		container_of(gen_gc, struct ath79_gpio_ctrl, chip);
	unsigned long pending;
	u32 both_edges, state;
	int irq;

	chained_irq_enter(irqchip, desc);

	scoped_guard(gpio_generic_lock_irqsave, &ctrl->chip) {
		pending = ath79_gpio_read(ctrl, AR71XX_GPIO_REG_INT_PENDING);

		/* Update the polarity of the both edges irqs */
		both_edges = ctrl->both_edges & pending;
		if (both_edges) {
			state = ath79_gpio_read(ctrl, AR71XX_GPIO_REG_IN);
			ath79_gpio_update_bits(ctrl, AR71XX_GPIO_REG_INT_POLARITY,
					       both_edges, ~state);
		}
	}

	for_each_set_bit(irq, &pending, gc->ngpio)
		generic_handle_domain_irq(gc->irq.domain, irq);

	chained_irq_exit(irqchip, desc);
}

static const struct of_device_id ath79_gpio_of_match[] = {
	{ .compatible = "qca,ar7100-gpio" },
	{ .compatible = "qca,ar9340-gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, ath79_gpio_of_match);

static int ath79_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct ath79_gpio_ctrl *ctrl;
	struct gpio_irq_chip *girq;
	u32 ath79_gpio_count;
	bool oe_inverted;
	int err;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	err = device_property_read_u32(dev, "ngpios", &ath79_gpio_count);
	if (err) {
		dev_err(dev, "ngpios property is not valid\n");
		return err;
	}

	oe_inverted = device_is_compatible(dev, "qca,ar9340-gpio");

	if (ath79_gpio_count >= 32) {
		dev_err(dev, "ngpios must be less than 32\n");
		return -EINVAL;
	}

	ctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = ctrl->base + AR71XX_GPIO_REG_IN,
		.set = ctrl->base + AR71XX_GPIO_REG_SET,
		.clr = ctrl->base + AR71XX_GPIO_REG_CLEAR,
		.dirout = oe_inverted ? NULL : ctrl->base + AR71XX_GPIO_REG_OE,
		.dirin = oe_inverted ? ctrl->base + AR71XX_GPIO_REG_OE : NULL,
	};

	err = gpio_generic_chip_init(&ctrl->chip, &config);
	if (err) {
		dev_err(dev, "failed to initialize generic GPIO chip\n");
		return err;
	}

	/* Optional interrupt setup */
	if (device_property_read_bool(dev, "interrupt-controller")) {
		girq = &ctrl->chip.gc.irq;
		gpio_irq_chip_set_chip(girq, &ath79_gpio_irqchip);
		girq->parent_handler = ath79_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = platform_get_irq(pdev, 0);
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
	}

	return devm_gpiochip_add_data(dev, &ctrl->chip.gc, ctrl);
}

static struct platform_driver ath79_gpio_driver = {
	.driver = {
		.name = "ath79-gpio",
		.of_match_table	= ath79_gpio_of_match,
	},
	.probe = ath79_gpio_probe,
};

module_platform_driver(ath79_gpio_driver);

MODULE_DESCRIPTION("Atheros AR71XX/AR724X/AR913X GPIO API support");
MODULE_LICENSE("GPL v2");
