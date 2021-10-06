// SPDX-License-Identifier: GPL-2.0-only
/*
 * RDA Micro GPIO driver
 *
 * Copyright (C) 2012 RDA Micro Inc.
 * Copyright (C) 2019 Manivannan Sadhasivam
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define RDA_GPIO_OEN_VAL		0x00
#define RDA_GPIO_OEN_SET_OUT		0x04
#define RDA_GPIO_OEN_SET_IN		0x08
#define RDA_GPIO_VAL			0x0c
#define RDA_GPIO_SET			0x10
#define RDA_GPIO_CLR			0x14
#define RDA_GPIO_INT_CTRL_SET		0x18
#define RDA_GPIO_INT_CTRL_CLR		0x1c
#define RDA_GPIO_INT_CLR		0x20
#define RDA_GPIO_INT_STATUS		0x24

#define RDA_GPIO_IRQ_RISE_SHIFT		0
#define RDA_GPIO_IRQ_FALL_SHIFT		8
#define RDA_GPIO_DEBOUCE_SHIFT		16
#define RDA_GPIO_LEVEL_SHIFT		24

#define RDA_GPIO_IRQ_MASK		0xff

/* Each bank consists of 32 GPIOs */
#define RDA_GPIO_BANK_NR	32

struct rda_gpio {
	struct gpio_chip chip;
	void __iomem *base;
	spinlock_t lock;
	struct irq_chip irq_chip;
	int irq;
};

static inline void rda_gpio_update(struct gpio_chip *chip, unsigned int offset,
				   u16 reg, int val)
{
	struct rda_gpio *rda_gpio = gpiochip_get_data(chip);
	void __iomem *base = rda_gpio->base;
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&rda_gpio->lock, flags);
	tmp = readl_relaxed(base + reg);

	if (val)
		tmp |= BIT(offset);
	else
		tmp &= ~BIT(offset);

	writel_relaxed(tmp, base + reg);
	spin_unlock_irqrestore(&rda_gpio->lock, flags);
}

static void rda_gpio_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct rda_gpio *rda_gpio = gpiochip_get_data(chip);
	void __iomem *base = rda_gpio->base;
	u32 offset = irqd_to_hwirq(data);
	u32 value;

	value = BIT(offset) << RDA_GPIO_IRQ_RISE_SHIFT;
	value |= BIT(offset) << RDA_GPIO_IRQ_FALL_SHIFT;

	writel_relaxed(value, base + RDA_GPIO_INT_CTRL_CLR);
}

static void rda_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);

	rda_gpio_update(chip, offset, RDA_GPIO_INT_CLR, 1);
}

static int rda_gpio_set_irq(struct gpio_chip *chip, u32 offset,
			    unsigned int flow_type)
{
	struct rda_gpio *rda_gpio = gpiochip_get_data(chip);
	void __iomem *base = rda_gpio->base;
	u32 value;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		/* Set rising edge trigger */
		value = BIT(offset) << RDA_GPIO_IRQ_RISE_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_SET);

		/* Switch to edge trigger interrupt */
		value = BIT(offset) << RDA_GPIO_LEVEL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_CLR);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		/* Set falling edge trigger */
		value = BIT(offset) << RDA_GPIO_IRQ_FALL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_SET);

		/* Switch to edge trigger interrupt */
		value = BIT(offset) << RDA_GPIO_LEVEL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_CLR);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		/* Set both edge trigger */
		value = BIT(offset) << RDA_GPIO_IRQ_RISE_SHIFT;
		value |= BIT(offset) << RDA_GPIO_IRQ_FALL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_SET);

		/* Switch to edge trigger interrupt */
		value = BIT(offset) << RDA_GPIO_LEVEL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_CLR);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		/* Set high level trigger */
		value = BIT(offset) << RDA_GPIO_IRQ_RISE_SHIFT;

		/* Switch to level trigger interrupt */
		value |= BIT(offset) << RDA_GPIO_LEVEL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_SET);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		/* Set low level trigger */
		value = BIT(offset) << RDA_GPIO_IRQ_FALL_SHIFT;

		/* Switch to level trigger interrupt */
		value |= BIT(offset) << RDA_GPIO_LEVEL_SHIFT;
		writel_relaxed(value, base + RDA_GPIO_INT_CTRL_SET);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void rda_gpio_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);
	u32 trigger = irqd_get_trigger_type(data);

	rda_gpio_set_irq(chip, offset, trigger);
}

static int rda_gpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);
	int ret;

	ret = rda_gpio_set_irq(chip, offset, flow_type);
	if (ret)
		return ret;

	if (flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(data, handle_level_irq);
	else if (flow_type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		irq_set_handler_locked(data, handle_edge_irq);

	return 0;
}

static void rda_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct rda_gpio *rda_gpio = gpiochip_get_data(chip);
	unsigned long status;
	u32 n;

	chained_irq_enter(ic, desc);

	status = readl_relaxed(rda_gpio->base + RDA_GPIO_INT_STATUS);
	/* Only lower 8 bits are capable of generating interrupts */
	status &= RDA_GPIO_IRQ_MASK;

	for_each_set_bit(n, &status, RDA_GPIO_BANK_NR)
		generic_handle_domain_irq(chip->irq.domain, n);

	chained_irq_exit(ic, desc);
}

static int rda_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct rda_gpio *rda_gpio;
	u32 ngpios;
	int ret;

	rda_gpio = devm_kzalloc(dev, sizeof(*rda_gpio), GFP_KERNEL);
	if (!rda_gpio)
		return -ENOMEM;

	ret = device_property_read_u32(dev, "ngpios", &ngpios);
	if (ret < 0)
		return ret;

	/*
	 * Not all ports have interrupt capability. For instance, on
	 * RDA8810PL, GPIOC doesn't support interrupt. So we must handle
	 * those also.
	 */
	rda_gpio->irq = platform_get_irq(pdev, 0);

	rda_gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rda_gpio->base))
		return PTR_ERR(rda_gpio->base);

	spin_lock_init(&rda_gpio->lock);

	ret = bgpio_init(&rda_gpio->chip, dev, 4,
			 rda_gpio->base + RDA_GPIO_VAL,
			 rda_gpio->base + RDA_GPIO_SET,
			 rda_gpio->base + RDA_GPIO_CLR,
			 rda_gpio->base + RDA_GPIO_OEN_SET_OUT,
			 rda_gpio->base + RDA_GPIO_OEN_SET_IN,
			 BGPIOF_READ_OUTPUT_REG_SET);
	if (ret) {
		dev_err(dev, "bgpio_init failed\n");
		return ret;
	}

	rda_gpio->chip.label = dev_name(dev);
	rda_gpio->chip.ngpio = ngpios;
	rda_gpio->chip.base = -1;
	rda_gpio->chip.parent = dev;
	rda_gpio->chip.of_node = np;

	if (rda_gpio->irq >= 0) {
		rda_gpio->irq_chip.name = "rda-gpio",
		rda_gpio->irq_chip.irq_ack = rda_gpio_irq_ack,
		rda_gpio->irq_chip.irq_mask = rda_gpio_irq_mask,
		rda_gpio->irq_chip.irq_unmask = rda_gpio_irq_unmask,
		rda_gpio->irq_chip.irq_set_type = rda_gpio_irq_set_type,
		rda_gpio->irq_chip.flags = IRQCHIP_SKIP_SET_WAKE,

		girq = &rda_gpio->chip.irq;
		girq->chip = &rda_gpio->irq_chip;
		girq->handler = handle_bad_irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->parent_handler = rda_gpio_irq_handler;
		girq->parent_handler_data = rda_gpio;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = rda_gpio->irq;
	}

	platform_set_drvdata(pdev, rda_gpio);

	return devm_gpiochip_add_data(dev, &rda_gpio->chip, rda_gpio);
}

static const struct of_device_id rda_gpio_of_match[] = {
	{ .compatible = "rda,8810pl-gpio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rda_gpio_of_match);

static struct platform_driver rda_gpio_driver = {
	.probe = rda_gpio_probe,
	.driver = {
		.name = "rda-gpio",
		.of_match_table	= rda_gpio_of_match,
	},
};

module_platform_driver_probe(rda_gpio_driver, rda_gpio_probe);

MODULE_DESCRIPTION("RDA Micro GPIO driver");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_LICENSE("GPL v2");
