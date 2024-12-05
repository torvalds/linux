// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO driver for NXP LPC18xx/43xx.
 *
 * Copyright (C) 2018 Vladimir Zapolskiy <vz@mleia.com>
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 */

#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

/* LPC18xx GPIO register offsets */
#define LPC18XX_REG_DIR(n)	(0x2000 + n * sizeof(u32))

#define LPC18XX_MAX_PORTS	8
#define LPC18XX_PINS_PER_PORT	32

/* LPC18xx GPIO pin interrupt controller register offsets */
#define LPC18XX_GPIO_PIN_IC_ISEL	0x00
#define LPC18XX_GPIO_PIN_IC_IENR	0x04
#define LPC18XX_GPIO_PIN_IC_SIENR	0x08
#define LPC18XX_GPIO_PIN_IC_CIENR	0x0c
#define LPC18XX_GPIO_PIN_IC_IENF	0x10
#define LPC18XX_GPIO_PIN_IC_SIENF	0x14
#define LPC18XX_GPIO_PIN_IC_CIENF	0x18
#define LPC18XX_GPIO_PIN_IC_RISE	0x1c
#define LPC18XX_GPIO_PIN_IC_FALL	0x20
#define LPC18XX_GPIO_PIN_IC_IST		0x24

#define NR_LPC18XX_GPIO_PIN_IC_IRQS	8

struct lpc18xx_gpio_pin_ic {
	void __iomem *base;
	struct irq_domain *domain;
	struct raw_spinlock lock;
};

struct lpc18xx_gpio_chip {
	struct gpio_chip gpio;
	void __iomem *base;
	struct lpc18xx_gpio_pin_ic *pin_ic;
	spinlock_t lock;
};

static inline void lpc18xx_gpio_pin_ic_isel(struct lpc18xx_gpio_pin_ic *ic,
					    u32 pin, bool set)
{
	u32 val = readl_relaxed(ic->base + LPC18XX_GPIO_PIN_IC_ISEL);

	if (set)
		val &= ~BIT(pin);
	else
		val |= BIT(pin);

	writel_relaxed(val, ic->base + LPC18XX_GPIO_PIN_IC_ISEL);
}

static inline void lpc18xx_gpio_pin_ic_set(struct lpc18xx_gpio_pin_ic *ic,
					   u32 pin, u32 reg)
{
	writel_relaxed(BIT(pin), ic->base + reg);
}

static void lpc18xx_gpio_pin_ic_mask(struct irq_data *d)
{
	struct lpc18xx_gpio_pin_ic *ic = d->chip_data;
	u32 type = irqd_get_trigger_type(d);

	raw_spin_lock(&ic->lock);

	if (type & IRQ_TYPE_LEVEL_MASK || type & IRQ_TYPE_EDGE_RISING)
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_CIENR);

	if (type & IRQ_TYPE_EDGE_FALLING)
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_CIENF);

	raw_spin_unlock(&ic->lock);

	irq_chip_mask_parent(d);
}

static void lpc18xx_gpio_pin_ic_unmask(struct irq_data *d)
{
	struct lpc18xx_gpio_pin_ic *ic = d->chip_data;
	u32 type = irqd_get_trigger_type(d);

	raw_spin_lock(&ic->lock);

	if (type & IRQ_TYPE_LEVEL_MASK || type & IRQ_TYPE_EDGE_RISING)
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_SIENR);

	if (type & IRQ_TYPE_EDGE_FALLING)
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_SIENF);

	raw_spin_unlock(&ic->lock);

	irq_chip_unmask_parent(d);
}

static void lpc18xx_gpio_pin_ic_eoi(struct irq_data *d)
{
	struct lpc18xx_gpio_pin_ic *ic = d->chip_data;
	u32 type = irqd_get_trigger_type(d);

	raw_spin_lock(&ic->lock);

	if (type & IRQ_TYPE_EDGE_BOTH)
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_IST);

	raw_spin_unlock(&ic->lock);

	irq_chip_eoi_parent(d);
}

static int lpc18xx_gpio_pin_ic_set_type(struct irq_data *d, unsigned int type)
{
	struct lpc18xx_gpio_pin_ic *ic = d->chip_data;

	raw_spin_lock(&ic->lock);

	if (type & IRQ_TYPE_LEVEL_HIGH) {
		lpc18xx_gpio_pin_ic_isel(ic, d->hwirq, true);
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_SIENF);
	} else if (type & IRQ_TYPE_LEVEL_LOW) {
		lpc18xx_gpio_pin_ic_isel(ic, d->hwirq, true);
		lpc18xx_gpio_pin_ic_set(ic, d->hwirq,
					LPC18XX_GPIO_PIN_IC_CIENF);
	} else {
		lpc18xx_gpio_pin_ic_isel(ic, d->hwirq, false);
	}

	raw_spin_unlock(&ic->lock);

	return 0;
}

static struct irq_chip lpc18xx_gpio_pin_ic = {
	.name		= "LPC18xx GPIO pin",
	.irq_mask	= lpc18xx_gpio_pin_ic_mask,
	.irq_unmask	= lpc18xx_gpio_pin_ic_unmask,
	.irq_eoi	= lpc18xx_gpio_pin_ic_eoi,
	.irq_set_type	= lpc18xx_gpio_pin_ic_set_type,
	.flags		= IRQCHIP_SET_TYPE_MASKED,
};

static int lpc18xx_gpio_pin_ic_domain_alloc(struct irq_domain *domain,
					    unsigned int virq,
					    unsigned int nr_irqs, void *data)
{
	struct irq_fwspec parent_fwspec, *fwspec = data;
	struct lpc18xx_gpio_pin_ic *ic = domain->host_data;
	irq_hw_number_t hwirq;
	int ret;

	if (nr_irqs != 1)
		return -EINVAL;

	hwirq = fwspec->param[0];
	if (hwirq >= NR_LPC18XX_GPIO_PIN_IC_IRQS)
		return -EINVAL;

	/*
	 * All LPC18xx/LPC43xx GPIO pin hardware interrupts are translated
	 * into edge interrupts 32...39 on parent Cortex-M3/M4 NVIC
	 */
	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 1;
	parent_fwspec.param[0] = hwirq + 32;

	ret = irq_domain_alloc_irqs_parent(domain, virq, 1, &parent_fwspec);
	if (ret < 0) {
		pr_err("failed to allocate parent irq %u: %d\n",
		       parent_fwspec.param[0], ret);
		return ret;
	}

	return irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					     &lpc18xx_gpio_pin_ic, ic);
}

static const struct irq_domain_ops lpc18xx_gpio_pin_ic_domain_ops = {
	.alloc	= lpc18xx_gpio_pin_ic_domain_alloc,
	.xlate	= irq_domain_xlate_twocell,
	.free	= irq_domain_free_irqs_common,
};

static int lpc18xx_gpio_pin_ic_probe(struct lpc18xx_gpio_chip *gc)
{
	struct device *dev = gc->gpio.parent;
	struct irq_domain *parent_domain;
	struct device_node *parent_node;
	struct lpc18xx_gpio_pin_ic *ic;
	struct resource res;
	int ret, index;

	parent_node = of_irq_find_parent(dev->of_node);
	if (!parent_node)
		return -ENXIO;

	parent_domain = irq_find_host(parent_node);
	of_node_put(parent_node);
	if (!parent_domain)
		return -ENXIO;

	ic = devm_kzalloc(dev, sizeof(*ic), GFP_KERNEL);
	if (!ic)
		return -ENOMEM;

	index = of_property_match_string(dev->of_node, "reg-names",
					 "gpio-pin-ic");
	if (index < 0) {
		ret = -ENODEV;
		goto free_ic;
	}

	ret = of_address_to_resource(dev->of_node, index, &res);
	if (ret < 0)
		goto free_ic;

	ic->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(ic->base)) {
		ret = PTR_ERR(ic->base);
		goto free_ic;
	}

	raw_spin_lock_init(&ic->lock);

	ic->domain = irq_domain_add_hierarchy(parent_domain, 0,
					      NR_LPC18XX_GPIO_PIN_IC_IRQS,
					      dev->of_node,
					      &lpc18xx_gpio_pin_ic_domain_ops,
					      ic);
	if (!ic->domain) {
		pr_err("unable to add irq domain\n");
		ret = -ENODEV;
		goto free_iomap;
	}

	gc->pin_ic = ic;

	return 0;

free_iomap:
	devm_iounmap(dev, ic->base);
free_ic:
	devm_kfree(dev, ic);

	return ret;
}

static void lpc18xx_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct lpc18xx_gpio_chip *gc = gpiochip_get_data(chip);
	writeb(value ? 1 : 0, gc->base + offset);
}

static int lpc18xx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct lpc18xx_gpio_chip *gc = gpiochip_get_data(chip);
	return !!readb(gc->base + offset);
}

static int lpc18xx_gpio_direction(struct gpio_chip *chip, unsigned offset,
				  bool out)
{
	struct lpc18xx_gpio_chip *gc = gpiochip_get_data(chip);
	unsigned long flags;
	u32 port, pin, dir;

	port = offset / LPC18XX_PINS_PER_PORT;
	pin  = offset % LPC18XX_PINS_PER_PORT;

	spin_lock_irqsave(&gc->lock, flags);
	dir = readl(gc->base + LPC18XX_REG_DIR(port));
	if (out)
		dir |= BIT(pin);
	else
		dir &= ~BIT(pin);
	writel(dir, gc->base + LPC18XX_REG_DIR(port));
	spin_unlock_irqrestore(&gc->lock, flags);

	return 0;
}

static int lpc18xx_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	return lpc18xx_gpio_direction(chip, offset, false);
}

static int lpc18xx_gpio_direction_output(struct gpio_chip *chip,
					 unsigned offset, int value)
{
	lpc18xx_gpio_set(chip, offset, value);
	return lpc18xx_gpio_direction(chip, offset, true);
}

static const struct gpio_chip lpc18xx_chip = {
	.label			= "lpc18xx/43xx-gpio",
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.direction_input	= lpc18xx_gpio_direction_input,
	.direction_output	= lpc18xx_gpio_direction_output,
	.set			= lpc18xx_gpio_set,
	.get			= lpc18xx_gpio_get,
	.ngpio			= LPC18XX_MAX_PORTS * LPC18XX_PINS_PER_PORT,
	.owner			= THIS_MODULE,
};

static int lpc18xx_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpc18xx_gpio_chip *gc;
	int index, ret;
	struct clk *clk;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gc->gpio = lpc18xx_chip;
	platform_set_drvdata(pdev, gc);

	index = of_property_match_string(dev->of_node, "reg-names", "gpio");
	if (index < 0) {
		/* To support backward compatibility take the first resource */
		gc->base = devm_platform_ioremap_resource(pdev, 0);
	} else {
		struct resource res;

		ret = of_address_to_resource(dev->of_node, index, &res);
		if (ret < 0)
			return ret;

		gc->base = devm_ioremap_resource(dev, &res);
	}
	if (IS_ERR(gc->base))
		return PTR_ERR(gc->base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "input clock not found\n");
		return PTR_ERR(clk);
	}

	spin_lock_init(&gc->lock);

	gc->gpio.parent = dev;

	ret = devm_gpiochip_add_data(dev, &gc->gpio, gc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add gpio chip\n");

	/* On error GPIO pin interrupt controller just won't be registered */
	lpc18xx_gpio_pin_ic_probe(gc);

	return 0;
}

static void lpc18xx_gpio_remove(struct platform_device *pdev)
{
	struct lpc18xx_gpio_chip *gc = platform_get_drvdata(pdev);

	if (gc->pin_ic)
		irq_domain_remove(gc->pin_ic->domain);
}

static const struct of_device_id lpc18xx_gpio_match[] = {
	{ .compatible = "nxp,lpc1850-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, lpc18xx_gpio_match);

static struct platform_driver lpc18xx_gpio_driver = {
	.probe	= lpc18xx_gpio_probe,
	.remove	= lpc18xx_gpio_remove,
	.driver	= {
		.name		= "lpc18xx-gpio",
		.of_match_table	= lpc18xx_gpio_match,
	},
};
module_platform_driver(lpc18xx_gpio_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_AUTHOR("Vladimir Zapolskiy <vz@mleia.com>");
MODULE_DESCRIPTION("GPIO driver for LPC18xx/43xx");
MODULE_LICENSE("GPL v2");
