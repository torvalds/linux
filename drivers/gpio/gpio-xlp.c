// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2003-2015 Broadcom Corporation
 * All Rights Reserved
 */

#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/acpi.h>

/*
 * XLP GPIO has multiple 32 bit registers for each feature where each register
 * controls 32 pins. So, pins up to 64 require 2 32-bit registers and up to 96
 * require 3 32-bit registers for each feature.
 * Here we only define offset of the first register for each feature. Offset of
 * the registers for pins greater than 32 can be calculated as following(Use
 * GPIO_INT_STAT as example):
 *
 * offset = (gpio / XLP_GPIO_REGSZ) * 4;
 * reg_addr = addr + offset;
 *
 * where addr is base address of the that feature register and gpio is the pin.
 */
#define GPIO_9XX_BYTESWAP	0X00
#define GPIO_9XX_CTRL		0X04
#define GPIO_9XX_OUTPUT_EN	0x14
#define GPIO_9XX_PADDRV		0x24
/*
 * Only for 4 interrupt enable reg are defined for now,
 * total reg available are 12.
 */
#define GPIO_9XX_INT_EN00	0x44
#define GPIO_9XX_INT_EN10	0x54
#define GPIO_9XX_INT_EN20	0x64
#define GPIO_9XX_INT_EN30	0x74
#define GPIO_9XX_INT_POL	0x104
#define GPIO_9XX_INT_TYPE	0x114
#define GPIO_9XX_INT_STAT	0x124

/* Interrupt type register mask */
#define XLP_GPIO_IRQ_TYPE_LVL	0x0
#define XLP_GPIO_IRQ_TYPE_EDGE	0x1

/* Interrupt polarity register mask */
#define XLP_GPIO_IRQ_POL_HIGH	0x0
#define XLP_GPIO_IRQ_POL_LOW	0x1

#define XLP_GPIO_REGSZ		32
#define XLP_GPIO_IRQ_BASE	768
#define XLP_MAX_NR_GPIO		96

struct xlp_gpio_priv {
	struct gpio_chip chip;
	DECLARE_BITMAP(gpio_enabled_mask, XLP_MAX_NR_GPIO);
	void __iomem *gpio_intr_en;	/* pointer to first intr enable reg */
	void __iomem *gpio_intr_stat;	/* pointer to first intr status reg */
	void __iomem *gpio_intr_type;	/* pointer to first intr type reg */
	void __iomem *gpio_intr_pol;	/* pointer to first intr polarity reg */
	void __iomem *gpio_out_en;	/* pointer to first output enable reg */
	void __iomem *gpio_paddrv;	/* pointer to first pad drive reg */
	spinlock_t lock;
};

static int xlp_gpio_get_reg(void __iomem *addr, unsigned gpio)
{
	u32 pos, regset;

	pos = gpio % XLP_GPIO_REGSZ;
	regset = (gpio / XLP_GPIO_REGSZ) * 4;
	return !!(readl(addr + regset) & BIT(pos));
}

static void xlp_gpio_set_reg(void __iomem *addr, unsigned gpio, int state)
{
	u32 value, pos, regset;

	pos = gpio % XLP_GPIO_REGSZ;
	regset = (gpio / XLP_GPIO_REGSZ) * 4;
	value = readl(addr + regset);

	if (state)
		value |= BIT(pos);
	else
		value &= ~BIT(pos);

	writel(value, addr + regset);
}

static void xlp_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc  = irq_data_get_irq_chip_data(d);
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	xlp_gpio_set_reg(priv->gpio_intr_en, d->hwirq, 0x0);
	__clear_bit(d->hwirq, priv->gpio_enabled_mask);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void xlp_gpio_irq_mask_ack(struct irq_data *d)
{
	struct gpio_chip *gc  = irq_data_get_irq_chip_data(d);
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	xlp_gpio_set_reg(priv->gpio_intr_en, d->hwirq, 0x0);
	xlp_gpio_set_reg(priv->gpio_intr_stat, d->hwirq, 0x1);
	__clear_bit(d->hwirq, priv->gpio_enabled_mask);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void xlp_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc  = irq_data_get_irq_chip_data(d);
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	xlp_gpio_set_reg(priv->gpio_intr_en, d->hwirq, 0x1);
	__set_bit(d->hwirq, priv->gpio_enabled_mask);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int xlp_gpio_set_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc  = irq_data_get_irq_chip_data(d);
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);
	int pol, irq_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type = XLP_GPIO_IRQ_TYPE_EDGE;
		pol = XLP_GPIO_IRQ_POL_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type = XLP_GPIO_IRQ_TYPE_EDGE;
		pol = XLP_GPIO_IRQ_POL_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type = XLP_GPIO_IRQ_TYPE_LVL;
		pol = XLP_GPIO_IRQ_POL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type = XLP_GPIO_IRQ_TYPE_LVL;
		pol = XLP_GPIO_IRQ_POL_LOW;
		break;
	default:
		return -EINVAL;
	}

	xlp_gpio_set_reg(priv->gpio_intr_type, d->hwirq, irq_type);
	xlp_gpio_set_reg(priv->gpio_intr_pol, d->hwirq, pol);

	return 0;
}

static struct irq_chip xlp_gpio_irq_chip = {
	.name		= "XLP-GPIO",
	.irq_mask_ack	= xlp_gpio_irq_mask_ack,
	.irq_disable	= xlp_gpio_irq_disable,
	.irq_set_type	= xlp_gpio_set_irq_type,
	.irq_unmask	= xlp_gpio_irq_unmask,
	.flags		= IRQCHIP_ONESHOT_SAFE,
};

static void xlp_gpio_generic_handler(struct irq_desc *desc)
{
	struct xlp_gpio_priv *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	int gpio, regoff;
	u32 gpio_stat;

	regoff = -1;
	gpio_stat = 0;

	chained_irq_enter(irqchip, desc);
	for_each_set_bit(gpio, priv->gpio_enabled_mask, XLP_MAX_NR_GPIO) {
		if (regoff != gpio / XLP_GPIO_REGSZ) {
			regoff = gpio / XLP_GPIO_REGSZ;
			gpio_stat = readl(priv->gpio_intr_stat + regoff * 4);
		}

		if (gpio_stat & BIT(gpio % XLP_GPIO_REGSZ))
			generic_handle_domain_irq(priv->chip.irq.domain, gpio);
	}
	chained_irq_exit(irqchip, desc);
}

static int xlp_gpio_dir_output(struct gpio_chip *gc, unsigned gpio, int state)
{
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);

	BUG_ON(gpio >= gc->ngpio);
	xlp_gpio_set_reg(priv->gpio_out_en, gpio, 0x1);

	return 0;
}

static int xlp_gpio_dir_input(struct gpio_chip *gc, unsigned gpio)
{
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);

	BUG_ON(gpio >= gc->ngpio);
	xlp_gpio_set_reg(priv->gpio_out_en, gpio, 0x0);

	return 0;
}

static int xlp_gpio_get(struct gpio_chip *gc, unsigned gpio)
{
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);

	BUG_ON(gpio >= gc->ngpio);
	return xlp_gpio_get_reg(priv->gpio_paddrv, gpio);
}

static void xlp_gpio_set(struct gpio_chip *gc, unsigned gpio, int state)
{
	struct xlp_gpio_priv *priv = gpiochip_get_data(gc);

	BUG_ON(gpio >= gc->ngpio);
	xlp_gpio_set_reg(priv->gpio_paddrv, gpio, state);
}

static int xlp_gpio_probe(struct platform_device *pdev)
{
	struct gpio_chip *gc;
	struct gpio_irq_chip *girq;
	struct xlp_gpio_priv *priv;
	void __iomem *gpio_base;
	int irq, err;

	priv = devm_kzalloc(&pdev->dev,	sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	gpio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio_base))
		return PTR_ERR(gpio_base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->gpio_out_en = gpio_base + GPIO_9XX_OUTPUT_EN;
	priv->gpio_paddrv = gpio_base + GPIO_9XX_PADDRV;
	priv->gpio_intr_stat = gpio_base + GPIO_9XX_INT_STAT;
	priv->gpio_intr_type = gpio_base + GPIO_9XX_INT_TYPE;
	priv->gpio_intr_pol = gpio_base + GPIO_9XX_INT_POL;
	priv->gpio_intr_en = gpio_base + GPIO_9XX_INT_EN00;

	bitmap_zero(priv->gpio_enabled_mask, XLP_MAX_NR_GPIO);

	gc = &priv->chip;

	gc->owner = THIS_MODULE;
	gc->label = dev_name(&pdev->dev);
	gc->base = 0;
	gc->parent = &pdev->dev;
	gc->ngpio = 70;
	gc->direction_output = xlp_gpio_dir_output;
	gc->direction_input = xlp_gpio_dir_input;
	gc->set = xlp_gpio_set;
	gc->get = xlp_gpio_get;

	spin_lock_init(&priv->lock);

	girq = &gc->irq;
	girq->chip = &xlp_gpio_irq_chip;
	girq->parent_handler = xlp_gpio_generic_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = irq;
	girq->first = 0;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	err = gpiochip_add_data(gc, priv);
	if (err < 0)
		return err;

	dev_info(&pdev->dev, "registered %d GPIOs\n", gc->ngpio);

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id xlp_gpio_acpi_match[] = {
	{ "BRCM9006" },
	{ "CAV9006" },
	{},
};
MODULE_DEVICE_TABLE(acpi, xlp_gpio_acpi_match);
#endif

static struct platform_driver xlp_gpio_driver = {
	.driver		= {
		.name	= "xlp-gpio",
		.acpi_match_table = ACPI_PTR(xlp_gpio_acpi_match),
	},
	.probe		= xlp_gpio_probe,
};
module_platform_driver(xlp_gpio_driver);

MODULE_AUTHOR("Kamlakant Patel <kamlakant.patel@broadcom.com>");
MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@broadcom.com>");
MODULE_DESCRIPTION("Netlogic XLP GPIO Driver");
MODULE_LICENSE("GPL v2");
