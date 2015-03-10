/*
 * Toumaz Xenif TZ1090 PDC GPIO handling.
 *
 * Copyright (C) 2012-2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <asm/global_lock.h>

/* Register offsets from SOC_GPIO_CONTROL0 */
#define REG_SOC_GPIO_CONTROL0	0x00
#define REG_SOC_GPIO_CONTROL1	0x04
#define REG_SOC_GPIO_CONTROL2	0x08
#define REG_SOC_GPIO_CONTROL3	0x0c
#define REG_SOC_GPIO_STATUS	0x80

/* PDC GPIOs go after normal GPIOs */
#define GPIO_PDC_BASE		90
#define GPIO_PDC_NGPIO		7

/* Out of PDC gpios, only syswakes have irqs */
#define GPIO_PDC_IRQ_FIRST	2
#define GPIO_PDC_NIRQ		3

/**
 * struct tz1090_pdc_gpio - GPIO bank private data
 * @chip:	Generic GPIO chip for GPIO bank
 * @reg:	Base of registers, offset for this GPIO bank
 * @irq:	IRQ numbers for Syswake GPIOs
 *
 * This is the main private data for the PDC GPIO driver. It encapsulates a
 * gpio_chip, and the callbacks for the gpio_chip can access the private data
 * with the to_pdc() macro below.
 */
struct tz1090_pdc_gpio {
	struct gpio_chip chip;
	void __iomem *reg;
	int irq[GPIO_PDC_NIRQ];
};
#define to_pdc(c)	container_of(c, struct tz1090_pdc_gpio, chip)

/* Register accesses into the PDC MMIO area */

static inline void pdc_write(struct tz1090_pdc_gpio *priv, unsigned int reg_offs,
		      unsigned int data)
{
	writel(data, priv->reg + reg_offs);
}

static inline unsigned int pdc_read(struct tz1090_pdc_gpio *priv,
			     unsigned int reg_offs)
{
	return readl(priv->reg + reg_offs);
}

/* Generic GPIO interface */

static int tz1090_pdc_gpio_direction_input(struct gpio_chip *chip,
					   unsigned int offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	__global_lock2(lstat);
	value = pdc_read(priv, REG_SOC_GPIO_CONTROL1);
	value |= BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL1, value);
	__global_unlock2(lstat);

	return 0;
}

static int tz1090_pdc_gpio_direction_output(struct gpio_chip *chip,
					    unsigned int offset,
					    int output_value)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	__global_lock2(lstat);
	/* EXT_POWER doesn't seem to have an output value bit */
	if (offset < 6) {
		value = pdc_read(priv, REG_SOC_GPIO_CONTROL0);
		if (output_value)
			value |= BIT(offset);
		else
			value &= ~BIT(offset);
		pdc_write(priv, REG_SOC_GPIO_CONTROL0, value);
	}

	value = pdc_read(priv, REG_SOC_GPIO_CONTROL1);
	value &= ~BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL1, value);
	__global_unlock2(lstat);

	return 0;
}

static int tz1090_pdc_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	return pdc_read(priv, REG_SOC_GPIO_STATUS) & BIT(offset);
}

static void tz1090_pdc_gpio_set(struct gpio_chip *chip, unsigned int offset,
				int output_value)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	u32 value;
	int lstat;

	/* EXT_POWER doesn't seem to have an output value bit */
	if (offset >= 6)
		return;

	__global_lock2(lstat);
	value = pdc_read(priv, REG_SOC_GPIO_CONTROL0);
	if (output_value)
		value |= BIT(offset);
	else
		value &= ~BIT(offset);
	pdc_write(priv, REG_SOC_GPIO_CONTROL0, value);
	__global_unlock2(lstat);
}

static int tz1090_pdc_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void tz1090_pdc_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int tz1090_pdc_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_pdc_gpio *priv = to_pdc(chip);
	unsigned int syswake = offset - GPIO_PDC_IRQ_FIRST;
	int irq;

	/* only syswakes have irqs */
	if (syswake >= GPIO_PDC_NIRQ)
		return -EINVAL;

	irq = priv->irq[syswake];
	if (!irq)
		return -EINVAL;

	return irq;
}

static int tz1090_pdc_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res_regs;
	struct tz1090_pdc_gpio *priv;
	unsigned int i;

	if (!np) {
		dev_err(&pdev->dev, "must be instantiated via devicetree\n");
		return -ENOENT;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_regs) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	/* Ioremap the registers */
	priv->reg = devm_ioremap(&pdev->dev, res_regs->start,
				 resource_size(res_regs));
	if (!priv->reg) {
		dev_err(&pdev->dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}

	/* Set up GPIO chip */
	priv->chip.label		= "tz1090-pdc-gpio";
	priv->chip.dev			= &pdev->dev;
	priv->chip.direction_input	= tz1090_pdc_gpio_direction_input;
	priv->chip.direction_output	= tz1090_pdc_gpio_direction_output;
	priv->chip.get			= tz1090_pdc_gpio_get;
	priv->chip.set			= tz1090_pdc_gpio_set;
	priv->chip.free			= tz1090_pdc_gpio_free;
	priv->chip.request		= tz1090_pdc_gpio_request;
	priv->chip.to_irq		= tz1090_pdc_gpio_to_irq;
	priv->chip.of_node		= np;

	/* GPIO numbering */
	priv->chip.base			= GPIO_PDC_BASE;
	priv->chip.ngpio		= GPIO_PDC_NGPIO;

	/* Map the syswake irqs */
	for (i = 0; i < GPIO_PDC_NIRQ; ++i)
		priv->irq[i] = irq_of_parse_and_map(np, i);

	/* Add the GPIO bank */
	gpiochip_add(&priv->chip);

	return 0;
}

static struct of_device_id tz1090_pdc_gpio_of_match[] = {
	{ .compatible = "img,tz1090-pdc-gpio" },
	{ },
};

static struct platform_driver tz1090_pdc_gpio_driver = {
	.driver = {
		.name		= "tz1090-pdc-gpio",
		.of_match_table	= tz1090_pdc_gpio_of_match,
	},
	.probe		= tz1090_pdc_gpio_probe,
};

static int __init tz1090_pdc_gpio_init(void)
{
	return platform_driver_register(&tz1090_pdc_gpio_driver);
}
subsys_initcall(tz1090_pdc_gpio_init);
