// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Broadcom
 */

#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define IPROC_CCA_INT_F_GPIOINT		BIT(0)
#define IPROC_CCA_INT_STS		0x20
#define IPROC_CCA_INT_MASK		0x24

#define IPROC_GPIO_CCA_DIN		0x0
#define IPROC_GPIO_CCA_DOUT		0x4
#define IPROC_GPIO_CCA_OUT_EN		0x8
#define IPROC_GPIO_CCA_INT_LEVEL	0x10
#define IPROC_GPIO_CCA_INT_LEVEL_MASK	0x14
#define IPROC_GPIO_CCA_INT_EVENT	0x18
#define IPROC_GPIO_CCA_INT_EVENT_MASK	0x1C
#define IPROC_GPIO_CCA_INT_EDGE		0x24

struct iproc_gpio_chip {
	struct irq_chip irqchip;
	struct gpio_chip gc;
	spinlock_t lock;
	struct device *dev;
	void __iomem *base;
	void __iomem *intr;
};

static inline struct iproc_gpio_chip *
to_iproc_gpio(struct gpio_chip *gc)
{
	return container_of(gc, struct iproc_gpio_chip, gc);
}

static void iproc_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio_chip *chip = to_iproc_gpio(gc);
	int pin = d->hwirq;
	unsigned long flags;
	u32 irq = d->irq;
	u32 irq_type, event_status = 0;

	spin_lock_irqsave(&chip->lock, flags);
	irq_type = irq_get_trigger_type(irq);
	if (irq_type & IRQ_TYPE_EDGE_BOTH) {
		event_status |= BIT(pin);
		writel_relaxed(event_status,
			       chip->base + IPROC_GPIO_CCA_INT_EVENT);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void iproc_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio_chip *chip = to_iproc_gpio(gc);
	int pin = d->hwirq;
	unsigned long flags;
	u32 irq = d->irq;
	u32 int_mask, irq_type, event_mask;

	spin_lock_irqsave(&chip->lock, flags);
	irq_type = irq_get_trigger_type(irq);
	event_mask = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EVENT_MASK);
	int_mask = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL_MASK);

	if (irq_type & IRQ_TYPE_EDGE_BOTH) {
		event_mask |= 1 << pin;
		writel_relaxed(event_mask,
			       chip->base + IPROC_GPIO_CCA_INT_EVENT_MASK);
	} else {
		int_mask |= 1 << pin;
		writel_relaxed(int_mask,
			       chip->base + IPROC_GPIO_CCA_INT_LEVEL_MASK);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void iproc_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio_chip *chip = to_iproc_gpio(gc);
	int pin = d->hwirq;
	unsigned long flags;
	u32 irq = d->irq;
	u32 irq_type, int_mask, event_mask;

	spin_lock_irqsave(&chip->lock, flags);
	irq_type = irq_get_trigger_type(irq);
	event_mask = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EVENT_MASK);
	int_mask = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL_MASK);

	if (irq_type & IRQ_TYPE_EDGE_BOTH) {
		event_mask &= ~BIT(pin);
		writel_relaxed(event_mask,
			       chip->base + IPROC_GPIO_CCA_INT_EVENT_MASK);
	} else {
		int_mask &= ~BIT(pin);
		writel_relaxed(int_mask,
			       chip->base + IPROC_GPIO_CCA_INT_LEVEL_MASK);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int iproc_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct iproc_gpio_chip *chip = to_iproc_gpio(gc);
	int pin = d->hwirq;
	unsigned long flags;
	u32 irq = d->irq;
	u32 event_pol, int_pol;
	int ret = 0;

	spin_lock_irqsave(&chip->lock, flags);
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		event_pol = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EDGE);
		event_pol &= ~BIT(pin);
		writel_relaxed(event_pol, chip->base + IPROC_GPIO_CCA_INT_EDGE);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		event_pol = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EDGE);
		event_pol |= BIT(pin);
		writel_relaxed(event_pol, chip->base + IPROC_GPIO_CCA_INT_EDGE);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_pol = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL);
		int_pol &= ~BIT(pin);
		writel_relaxed(int_pol, chip->base + IPROC_GPIO_CCA_INT_LEVEL);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_pol = readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL);
		int_pol |= BIT(pin);
		writel_relaxed(int_pol, chip->base + IPROC_GPIO_CCA_INT_LEVEL);
		break;
	default:
		/* should not come here */
		ret = -EINVAL;
		goto out_unlock;
	}

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(irq_get_irq_data(irq), handle_level_irq);
	else if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(irq_get_irq_data(irq), handle_edge_irq);

out_unlock:
	spin_unlock_irqrestore(&chip->lock, flags);

	return ret;
}

static irqreturn_t iproc_gpio_irq_handler(int irq, void *data)
{
	struct gpio_chip *gc = (struct gpio_chip *)data;
	struct iproc_gpio_chip *chip = to_iproc_gpio(gc);
	int bit;
	unsigned long int_bits = 0;
	u32 int_status;

	/* go through the entire GPIOs and handle all interrupts */
	int_status = readl_relaxed(chip->intr + IPROC_CCA_INT_STS);
	if (int_status & IPROC_CCA_INT_F_GPIOINT) {
		u32 event, level;

		/* Get level and edge interrupts */
		event =
		    readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EVENT_MASK);
		event &= readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_EVENT);
		level = readl_relaxed(chip->base + IPROC_GPIO_CCA_DIN);
		level ^= readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL);
		level &=
		    readl_relaxed(chip->base + IPROC_GPIO_CCA_INT_LEVEL_MASK);
		int_bits = level | event;

		for_each_set_bit(bit, &int_bits, gc->ngpio)
			generic_handle_domain_irq(gc->irq.domain, bit);
	}

	return int_bits ? IRQ_HANDLED : IRQ_NONE;
}

static int iproc_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = pdev->dev.of_node;
	struct iproc_gpio_chip *chip;
	u32 num_gpios;
	int irq, ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	platform_set_drvdata(pdev, chip);
	spin_lock_init(&chip->lock);

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	ret = bgpio_init(&chip->gc, dev, 4,
			 chip->base + IPROC_GPIO_CCA_DIN,
			 chip->base + IPROC_GPIO_CCA_DOUT,
			 NULL,
			 chip->base + IPROC_GPIO_CCA_OUT_EN,
			 NULL,
			 0);
	if (ret) {
		dev_err(dev, "unable to init GPIO chip\n");
		return ret;
	}

	chip->gc.label = dev_name(dev);
	if (!of_property_read_u32(dn, "ngpios", &num_gpios))
		chip->gc.ngpio = num_gpios;

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		struct gpio_irq_chip *girq;
		struct irq_chip *irqc;
		u32 val;

		irqc = &chip->irqchip;
		irqc->name = dev_name(dev);
		irqc->irq_ack = iproc_gpio_irq_ack;
		irqc->irq_mask = iproc_gpio_irq_mask;
		irqc->irq_unmask = iproc_gpio_irq_unmask;
		irqc->irq_set_type = iproc_gpio_irq_set_type;

		chip->intr = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(chip->intr))
			return PTR_ERR(chip->intr);

		/* Enable GPIO interrupts for CCA GPIO */
		val = readl_relaxed(chip->intr + IPROC_CCA_INT_MASK);
		val |= IPROC_CCA_INT_F_GPIOINT;
		writel_relaxed(val, chip->intr + IPROC_CCA_INT_MASK);

		/*
		 * Directly request the irq here instead of passing
		 * a flow-handler because the irq is shared.
		 */
		ret = devm_request_irq(dev, irq, iproc_gpio_irq_handler,
				       IRQF_SHARED, chip->gc.label, &chip->gc);
		if (ret) {
			dev_err(dev, "Fail to request IRQ%d: %d\n", irq, ret);
			return ret;
		}

		girq = &chip->gc.irq;
		girq->chip = irqc;
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
	}

	ret = devm_gpiochip_add_data(dev, &chip->gc, chip);
	if (ret) {
		dev_err(dev, "unable to add GPIO chip\n");
		return ret;
	}

	return 0;
}

static int iproc_gpio_remove(struct platform_device *pdev)
{
	struct iproc_gpio_chip *chip = platform_get_drvdata(pdev);

	if (chip->intr) {
		u32 val;

		val = readl_relaxed(chip->intr + IPROC_CCA_INT_MASK);
		val &= ~IPROC_CCA_INT_F_GPIOINT;
		writel_relaxed(val, chip->intr + IPROC_CCA_INT_MASK);
	}

	return 0;
}

static const struct of_device_id bcm_iproc_gpio_of_match[] = {
	{ .compatible = "brcm,iproc-gpio-cca" },
	{}
};
MODULE_DEVICE_TABLE(of, bcm_iproc_gpio_of_match);

static struct platform_driver bcm_iproc_gpio_driver = {
	.driver = {
		.name = "iproc-xgs-gpio",
		.of_match_table = bcm_iproc_gpio_of_match,
	},
	.probe = iproc_gpio_probe,
	.remove = iproc_gpio_remove,
};

module_platform_driver(bcm_iproc_gpio_driver);

MODULE_DESCRIPTION("XGS IPROC GPIO driver");
MODULE_LICENSE("GPL v2");
