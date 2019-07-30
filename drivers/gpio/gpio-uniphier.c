// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2017 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <dt-bindings/gpio/uniphier-gpio.h>

#define UNIPHIER_GPIO_BANK_MASK		\
				GENMASK((UNIPHIER_GPIO_LINES_PER_BANK) - 1, 0)

#define UNIPHIER_GPIO_IRQ_MAX_NUM	24

#define UNIPHIER_GPIO_PORT_DATA		0x0	/* data */
#define UNIPHIER_GPIO_PORT_DIR		0x4	/* direction (1:in, 0:out) */
#define UNIPHIER_GPIO_IRQ_EN		0x90	/* irq enable */
#define UNIPHIER_GPIO_IRQ_MODE		0x94	/* irq mode (1: both edge) */
#define UNIPHIER_GPIO_IRQ_FLT_EN	0x98	/* noise filter enable */
#define UNIPHIER_GPIO_IRQ_FLT_CYC	0x9c	/* noise filter clock cycle */

struct uniphier_gpio_priv {
	struct gpio_chip chip;
	struct irq_chip irq_chip;
	struct irq_domain *domain;
	void __iomem *regs;
	spinlock_t lock;
	u32 saved_vals[0];
};

static unsigned int uniphier_gpio_bank_to_reg(unsigned int bank)
{
	unsigned int reg;

	reg = (bank + 1) * 8;

	/*
	 * Unfortunately, the GPIO port registers are not contiguous because
	 * offset 0x90-0x9f is used for IRQ.  Add 0x10 when crossing the region.
	 */
	if (reg >= UNIPHIER_GPIO_IRQ_EN)
		reg += 0x10;

	return reg;
}

static void uniphier_gpio_get_bank_and_mask(unsigned int offset,
					    unsigned int *bank, u32 *mask)
{
	*bank = offset / UNIPHIER_GPIO_LINES_PER_BANK;
	*mask = BIT(offset % UNIPHIER_GPIO_LINES_PER_BANK);
}

static void uniphier_gpio_reg_update(struct uniphier_gpio_priv *priv,
				     unsigned int reg, u32 mask, u32 val)
{
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&priv->lock, flags);
	tmp = readl(priv->regs + reg);
	tmp &= ~mask;
	tmp |= mask & val;
	writel(tmp, priv->regs + reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void uniphier_gpio_bank_write(struct gpio_chip *chip, unsigned int bank,
				     unsigned int reg, u32 mask, u32 val)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);

	if (!mask)
		return;

	uniphier_gpio_reg_update(priv, uniphier_gpio_bank_to_reg(bank) + reg,
				 mask, val);
}

static void uniphier_gpio_offset_write(struct gpio_chip *chip,
				       unsigned int offset, unsigned int reg,
				       int val)
{
	unsigned int bank;
	u32 mask;

	uniphier_gpio_get_bank_and_mask(offset, &bank, &mask);

	uniphier_gpio_bank_write(chip, bank, reg, mask, val ? mask : 0);
}

static int uniphier_gpio_offset_read(struct gpio_chip *chip,
				     unsigned int offset, unsigned int reg)
{
	struct uniphier_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int bank, reg_offset;
	u32 mask;

	uniphier_gpio_get_bank_and_mask(offset, &bank, &mask);
	reg_offset = uniphier_gpio_bank_to_reg(bank) + reg;

	return !!(readl(priv->regs + reg_offset) & mask);
}

static int uniphier_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_PORT_DIR);
}

static int uniphier_gpio_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_PORT_DIR, 1);

	return 0;
}

static int uniphier_gpio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int val)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_PORT_DATA, val);
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_PORT_DIR, 0);

	return 0;
}

static int uniphier_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return uniphier_gpio_offset_read(chip, offset, UNIPHIER_GPIO_PORT_DATA);
}

static void uniphier_gpio_set(struct gpio_chip *chip,
			      unsigned int offset, int val)
{
	uniphier_gpio_offset_write(chip, offset, UNIPHIER_GPIO_PORT_DATA, val);
}

static void uniphier_gpio_set_multiple(struct gpio_chip *chip,
				       unsigned long *mask, unsigned long *bits)
{
	unsigned int bank, shift, bank_mask, bank_bits;
	int i;

	for (i = 0; i < chip->ngpio; i += UNIPHIER_GPIO_LINES_PER_BANK) {
		bank = i / UNIPHIER_GPIO_LINES_PER_BANK;
		shift = i % BITS_PER_LONG;
		bank_mask = (mask[BIT_WORD(i)] >> shift) &
						UNIPHIER_GPIO_BANK_MASK;
		bank_bits = bits[BIT_WORD(i)] >> shift;

		uniphier_gpio_bank_write(chip, bank, UNIPHIER_GPIO_PORT_DATA,
					 bank_mask, bank_bits);
	}
}

static int uniphier_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct irq_fwspec fwspec;

	if (offset < UNIPHIER_GPIO_IRQ_OFFSET)
		return -ENXIO;

	fwspec.fwnode = of_node_to_fwnode(chip->parent->of_node);
	fwspec.param_count = 2;
	fwspec.param[0] = offset - UNIPHIER_GPIO_IRQ_OFFSET;
	/*
	 * IRQ_TYPE_NONE is rejected by the parent irq domain. Set LEVEL_HIGH
	 * temporarily. Anyway, ->irq_set_type() will override it later.
	 */
	fwspec.param[1] = IRQ_TYPE_LEVEL_HIGH;

	return irq_create_fwspec_mapping(&fwspec);
}

static void uniphier_gpio_irq_mask(struct irq_data *data)
{
	struct uniphier_gpio_priv *priv = data->chip_data;
	u32 mask = BIT(data->hwirq);

	uniphier_gpio_reg_update(priv, UNIPHIER_GPIO_IRQ_EN, mask, 0);

	return irq_chip_mask_parent(data);
}

static void uniphier_gpio_irq_unmask(struct irq_data *data)
{
	struct uniphier_gpio_priv *priv = data->chip_data;
	u32 mask = BIT(data->hwirq);

	uniphier_gpio_reg_update(priv, UNIPHIER_GPIO_IRQ_EN, mask, mask);

	return irq_chip_unmask_parent(data);
}

static int uniphier_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct uniphier_gpio_priv *priv = data->chip_data;
	u32 mask = BIT(data->hwirq);
	u32 val = 0;

	if (type == IRQ_TYPE_EDGE_BOTH) {
		val = mask;
		type = IRQ_TYPE_EDGE_FALLING;
	}

	uniphier_gpio_reg_update(priv, UNIPHIER_GPIO_IRQ_MODE, mask, val);
	/* To enable both edge detection, the noise filter must be enabled. */
	uniphier_gpio_reg_update(priv, UNIPHIER_GPIO_IRQ_FLT_EN, mask, val);

	return irq_chip_set_type_parent(data, type);
}

static int uniphier_gpio_irq_get_parent_hwirq(struct uniphier_gpio_priv *priv,
					      unsigned int hwirq)
{
	struct device_node *np = priv->chip.parent->of_node;
	const __be32 *range;
	u32 base, parent_base, size;
	int len;

	range = of_get_property(np, "socionext,interrupt-ranges", &len);
	if (!range)
		return -EINVAL;

	len /= sizeof(*range);

	for (; len >= 3; len -= 3) {
		base = be32_to_cpu(*range++);
		parent_base = be32_to_cpu(*range++);
		size = be32_to_cpu(*range++);

		if (base <= hwirq && hwirq < base + size)
			return hwirq - base + parent_base;
	}

	return -ENOENT;
}

static int uniphier_gpio_irq_domain_translate(struct irq_domain *domain,
					      struct irq_fwspec *fwspec,
					      unsigned long *out_hwirq,
					      unsigned int *out_type)
{
	if (WARN_ON(fwspec->param_count < 2))
		return -EINVAL;

	*out_hwirq = fwspec->param[0];
	*out_type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static int uniphier_gpio_irq_domain_alloc(struct irq_domain *domain,
					  unsigned int virq,
					  unsigned int nr_irqs, void *arg)
{
	struct uniphier_gpio_priv *priv = domain->host_data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	ret = uniphier_gpio_irq_domain_translate(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	ret = uniphier_gpio_irq_get_parent_hwirq(priv, hwirq);
	if (ret < 0)
		return ret;

	/* parent is UniPhier AIDET */
	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 2;
	parent_fwspec.param[0] = ret;
	parent_fwspec.param[1] = (type == IRQ_TYPE_EDGE_BOTH) ?
						IRQ_TYPE_EDGE_FALLING : type;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    &priv->irq_chip, priv);
	if (ret)
		return ret;

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
}

static int uniphier_gpio_irq_domain_activate(struct irq_domain *domain,
					     struct irq_data *data, bool early)
{
	struct uniphier_gpio_priv *priv = domain->host_data;
	struct gpio_chip *chip = &priv->chip;

	return gpiochip_lock_as_irq(chip, data->hwirq + UNIPHIER_GPIO_IRQ_OFFSET);
}

static void uniphier_gpio_irq_domain_deactivate(struct irq_domain *domain,
						struct irq_data *data)
{
	struct uniphier_gpio_priv *priv = domain->host_data;
	struct gpio_chip *chip = &priv->chip;

	gpiochip_unlock_as_irq(chip, data->hwirq + UNIPHIER_GPIO_IRQ_OFFSET);
}

static const struct irq_domain_ops uniphier_gpio_irq_domain_ops = {
	.alloc = uniphier_gpio_irq_domain_alloc,
	.free = irq_domain_free_irqs_common,
	.activate = uniphier_gpio_irq_domain_activate,
	.deactivate = uniphier_gpio_irq_domain_deactivate,
	.translate = uniphier_gpio_irq_domain_translate,
};

static void uniphier_gpio_hw_init(struct uniphier_gpio_priv *priv)
{
	/*
	 * Due to the hardware design, the noise filter must be enabled to
	 * detect both edge interrupts.  This filter is intended to remove the
	 * noise from the irq lines.  It does not work for GPIO input, so GPIO
	 * debounce is not supported.  Unfortunately, the filter period is
	 * shared among all irq lines.  Just choose a sensible period here.
	 */
	writel(0xff, priv->regs + UNIPHIER_GPIO_IRQ_FLT_CYC);
}

static unsigned int uniphier_gpio_get_nbanks(unsigned int ngpio)
{
	return DIV_ROUND_UP(ngpio, UNIPHIER_GPIO_LINES_PER_BANK);
}

static int uniphier_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *parent_np;
	struct irq_domain *parent_domain;
	struct uniphier_gpio_priv *priv;
	struct gpio_chip *chip;
	struct irq_chip *irq_chip;
	unsigned int nregs;
	u32 ngpios;
	int ret;

	parent_np = of_irq_find_parent(dev->of_node);
	if (!parent_np)
		return -ENXIO;

	parent_domain = irq_find_host(parent_np);
	of_node_put(parent_np);
	if (!parent_domain)
		return -EPROBE_DEFER;

	ret = of_property_read_u32(dev->of_node, "ngpios", &ngpios);
	if (ret)
		return ret;

	nregs = uniphier_gpio_get_nbanks(ngpios) * 2 + 3;
	priv = devm_kzalloc(dev, struct_size(priv, saved_vals, nregs),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	spin_lock_init(&priv->lock);

	chip = &priv->chip;
	chip->label = dev_name(dev);
	chip->parent = dev;
	chip->request = gpiochip_generic_request;
	chip->free = gpiochip_generic_free;
	chip->get_direction = uniphier_gpio_get_direction;
	chip->direction_input = uniphier_gpio_direction_input;
	chip->direction_output = uniphier_gpio_direction_output;
	chip->get = uniphier_gpio_get;
	chip->set = uniphier_gpio_set;
	chip->set_multiple = uniphier_gpio_set_multiple;
	chip->to_irq = uniphier_gpio_to_irq;
	chip->base = -1;
	chip->ngpio = ngpios;

	irq_chip = &priv->irq_chip;
	irq_chip->name = dev_name(dev);
	irq_chip->irq_mask = uniphier_gpio_irq_mask;
	irq_chip->irq_unmask = uniphier_gpio_irq_unmask;
	irq_chip->irq_eoi = irq_chip_eoi_parent;
	irq_chip->irq_set_affinity = irq_chip_set_affinity_parent;
	irq_chip->irq_set_type = uniphier_gpio_irq_set_type;

	uniphier_gpio_hw_init(priv);

	ret = devm_gpiochip_add_data(dev, chip, priv);
	if (ret)
		return ret;

	priv->domain = irq_domain_create_hierarchy(
					parent_domain, 0,
					UNIPHIER_GPIO_IRQ_MAX_NUM,
					of_node_to_fwnode(dev->of_node),
					&uniphier_gpio_irq_domain_ops, priv);
	if (!priv->domain)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int uniphier_gpio_remove(struct platform_device *pdev)
{
	struct uniphier_gpio_priv *priv = platform_get_drvdata(pdev);

	irq_domain_remove(priv->domain);

	return 0;
}

static int __maybe_unused uniphier_gpio_suspend(struct device *dev)
{
	struct uniphier_gpio_priv *priv = dev_get_drvdata(dev);
	unsigned int nbanks = uniphier_gpio_get_nbanks(priv->chip.ngpio);
	u32 *val = priv->saved_vals;
	unsigned int reg;
	int i;

	for (i = 0; i < nbanks; i++) {
		reg = uniphier_gpio_bank_to_reg(i);

		*val++ = readl(priv->regs + reg + UNIPHIER_GPIO_PORT_DATA);
		*val++ = readl(priv->regs + reg + UNIPHIER_GPIO_PORT_DIR);
	}

	*val++ = readl(priv->regs + UNIPHIER_GPIO_IRQ_EN);
	*val++ = readl(priv->regs + UNIPHIER_GPIO_IRQ_MODE);
	*val++ = readl(priv->regs + UNIPHIER_GPIO_IRQ_FLT_EN);

	return 0;
}

static int __maybe_unused uniphier_gpio_resume(struct device *dev)
{
	struct uniphier_gpio_priv *priv = dev_get_drvdata(dev);
	unsigned int nbanks = uniphier_gpio_get_nbanks(priv->chip.ngpio);
	const u32 *val = priv->saved_vals;
	unsigned int reg;
	int i;

	for (i = 0; i < nbanks; i++) {
		reg = uniphier_gpio_bank_to_reg(i);

		writel(*val++, priv->regs + reg + UNIPHIER_GPIO_PORT_DATA);
		writel(*val++, priv->regs + reg + UNIPHIER_GPIO_PORT_DIR);
	}

	writel(*val++, priv->regs + UNIPHIER_GPIO_IRQ_EN);
	writel(*val++, priv->regs + UNIPHIER_GPIO_IRQ_MODE);
	writel(*val++, priv->regs + UNIPHIER_GPIO_IRQ_FLT_EN);

	uniphier_gpio_hw_init(priv);

	return 0;
}

static const struct dev_pm_ops uniphier_gpio_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(uniphier_gpio_suspend,
				     uniphier_gpio_resume)
};

static const struct of_device_id uniphier_gpio_match[] = {
	{ .compatible = "socionext,uniphier-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_gpio_match);

static struct platform_driver uniphier_gpio_driver = {
	.probe = uniphier_gpio_probe,
	.remove = uniphier_gpio_remove,
	.driver = {
		.name = "uniphier-gpio",
		.of_match_table = uniphier_gpio_match,
		.pm = &uniphier_gpio_pm_ops,
	},
};
module_platform_driver(uniphier_gpio_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("UniPhier GPIO driver");
MODULE_LICENSE("GPL v2");
