// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2017-2018 Cadence
 *
 * Authors:
 *  Jan Kotas <jank@cadence.com>
 *  Boris Brezillon <boris.brezillon@free-electrons.com>
 */

#include <linux/gpio/driver.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define CDNS_GPIO_BYPASS_MODE		0x00
#define CDNS_GPIO_DIRECTION_MODE	0x04
#define CDNS_GPIO_OUTPUT_EN		0x08
#define CDNS_GPIO_OUTPUT_VALUE		0x0c
#define CDNS_GPIO_INPUT_VALUE		0x10
#define CDNS_GPIO_IRQ_MASK		0x14
#define CDNS_GPIO_IRQ_EN		0x18
#define CDNS_GPIO_IRQ_DIS		0x1c
#define CDNS_GPIO_IRQ_STATUS		0x20
#define CDNS_GPIO_IRQ_TYPE		0x24
#define CDNS_GPIO_IRQ_VALUE		0x28
#define CDNS_GPIO_IRQ_ANY_EDGE		0x2c

struct cdns_gpio_chip {
	struct gpio_chip gc;
	struct clk *pclk;
	void __iomem *regs;
	u32 bypass_orig;
};

static int cdns_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);
	unsigned long flags;

	spin_lock_irqsave(&chip->bgpio_lock, flags);

	iowrite32(ioread32(cgpio->regs + CDNS_GPIO_BYPASS_MODE) & ~BIT(offset),
		  cgpio->regs + CDNS_GPIO_BYPASS_MODE);

	spin_unlock_irqrestore(&chip->bgpio_lock, flags);
	return 0;
}

static void cdns_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);
	unsigned long flags;

	spin_lock_irqsave(&chip->bgpio_lock, flags);

	iowrite32(ioread32(cgpio->regs + CDNS_GPIO_BYPASS_MODE) |
		  (BIT(offset) & cgpio->bypass_orig),
		  cgpio->regs + CDNS_GPIO_BYPASS_MODE);

	spin_unlock_irqrestore(&chip->bgpio_lock, flags);
}

static void cdns_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);

	iowrite32(BIT(d->hwirq), cgpio->regs + CDNS_GPIO_IRQ_DIS);
}

static void cdns_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);

	iowrite32(BIT(d->hwirq), cgpio->regs + CDNS_GPIO_IRQ_EN);
}

static int cdns_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);
	unsigned long flags;
	u32 int_value;
	u32 int_type;
	u32 mask = BIT(d->hwirq);
	int ret = 0;

	spin_lock_irqsave(&chip->bgpio_lock, flags);

	int_value = ioread32(cgpio->regs + CDNS_GPIO_IRQ_VALUE) & ~mask;
	int_type = ioread32(cgpio->regs + CDNS_GPIO_IRQ_TYPE) & ~mask;

	/*
	 * The GPIO controller doesn't have an ACK register.
	 * All interrupt statuses are cleared on a status register read.
	 * Don't support edge interrupts for now.
	 */

	if (type == IRQ_TYPE_LEVEL_HIGH) {
		int_type |= mask;
		int_value |= mask;
	} else if (type == IRQ_TYPE_LEVEL_LOW) {
		int_type |= mask;
	} else {
		ret = -EINVAL;
		goto err_irq_type;
	}

	iowrite32(int_value, cgpio->regs + CDNS_GPIO_IRQ_VALUE);
	iowrite32(int_type, cgpio->regs + CDNS_GPIO_IRQ_TYPE);

err_irq_type:
	spin_unlock_irqrestore(&chip->bgpio_lock, flags);
	return ret;
}

static void cdns_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct cdns_gpio_chip *cgpio = gpiochip_get_data(chip);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long status;
	int hwirq;

	chained_irq_enter(irqchip, desc);

	status = ioread32(cgpio->regs + CDNS_GPIO_IRQ_STATUS) &
		~ioread32(cgpio->regs + CDNS_GPIO_IRQ_MASK);

	for_each_set_bit(hwirq, &status, chip->ngpio)
		generic_handle_irq(irq_find_mapping(chip->irq.domain, hwirq));

	chained_irq_exit(irqchip, desc);
}

static struct irq_chip cdns_gpio_irqchip = {
	.name		= "cdns-gpio",
	.irq_mask	= cdns_gpio_irq_mask,
	.irq_unmask	= cdns_gpio_irq_unmask,
	.irq_set_type	= cdns_gpio_irq_set_type
};

static int cdns_gpio_probe(struct platform_device *pdev)
{
	struct cdns_gpio_chip *cgpio;
	struct resource *res;
	int ret, irq;
	u32 dir_prev;
	u32 num_gpios = 32;

	cgpio = devm_kzalloc(&pdev->dev, sizeof(*cgpio), GFP_KERNEL);
	if (!cgpio)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cgpio->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(cgpio->regs))
		return PTR_ERR(cgpio->regs);

	of_property_read_u32(pdev->dev.of_node, "ngpios", &num_gpios);

	if (num_gpios > 32) {
		dev_err(&pdev->dev, "ngpios must be less or equal 32\n");
		return -EINVAL;
	}

	/*
	 * Set all pins as inputs by default, otherwise:
	 * gpiochip_lock_as_irq:
	 * tried to flag a GPIO set as output for IRQ
	 * Generic GPIO driver stores the direction value internally,
	 * so it needs to be changed before bgpio_init() is called.
	 */
	dir_prev = ioread32(cgpio->regs + CDNS_GPIO_DIRECTION_MODE);
	iowrite32(GENMASK(num_gpios - 1, 0),
		  cgpio->regs + CDNS_GPIO_DIRECTION_MODE);

	ret = bgpio_init(&cgpio->gc, &pdev->dev, 4,
			 cgpio->regs + CDNS_GPIO_INPUT_VALUE,
			 cgpio->regs + CDNS_GPIO_OUTPUT_VALUE,
			 NULL,
			 NULL,
			 cgpio->regs + CDNS_GPIO_DIRECTION_MODE,
			 BGPIOF_READ_OUTPUT_REG_SET);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register generic gpio, %d\n",
			ret);
		goto err_revert_dir;
	}

	cgpio->gc.label = dev_name(&pdev->dev);
	cgpio->gc.ngpio = num_gpios;
	cgpio->gc.parent = &pdev->dev;
	cgpio->gc.base = -1;
	cgpio->gc.owner = THIS_MODULE;
	cgpio->gc.request = cdns_gpio_request;
	cgpio->gc.free = cdns_gpio_free;

	cgpio->pclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(cgpio->pclk)) {
		ret = PTR_ERR(cgpio->pclk);
		dev_err(&pdev->dev,
			"Failed to retrieve peripheral clock, %d\n", ret);
		goto err_revert_dir;
	}

	ret = clk_prepare_enable(cgpio->pclk);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to enable the peripheral clock, %d\n", ret);
		goto err_revert_dir;
	}

	ret = devm_gpiochip_add_data(&pdev->dev, &cgpio->gc, cgpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n", ret);
		goto err_disable_clk;
	}

	/*
	 * irq_chip support
	 */
	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		ret = gpiochip_irqchip_add(&cgpio->gc, &cdns_gpio_irqchip,
					   0, handle_level_irq,
					   IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&pdev->dev, "Could not add irqchip, %d\n",
				ret);
			goto err_disable_clk;
		}
		gpiochip_set_chained_irqchip(&cgpio->gc, &cdns_gpio_irqchip,
					     irq, cdns_gpio_irq_handler);
	}

	cgpio->bypass_orig = ioread32(cgpio->regs + CDNS_GPIO_BYPASS_MODE);

	/*
	 * Enable gpio outputs, ignored for input direction
	 */
	iowrite32(GENMASK(num_gpios - 1, 0),
		  cgpio->regs + CDNS_GPIO_OUTPUT_EN);
	iowrite32(0, cgpio->regs + CDNS_GPIO_BYPASS_MODE);

	platform_set_drvdata(pdev, cgpio);
	return 0;

err_disable_clk:
	clk_disable_unprepare(cgpio->pclk);

err_revert_dir:
	iowrite32(dir_prev, cgpio->regs + CDNS_GPIO_DIRECTION_MODE);

	return ret;
}

static int cdns_gpio_remove(struct platform_device *pdev)
{
	struct cdns_gpio_chip *cgpio = platform_get_drvdata(pdev);

	iowrite32(cgpio->bypass_orig, cgpio->regs + CDNS_GPIO_BYPASS_MODE);
	clk_disable_unprepare(cgpio->pclk);

	return 0;
}

static const struct of_device_id cdns_of_ids[] = {
	{ .compatible = "cdns,gpio-r1p02" },
	{ /* sentinel */ },
};

static struct platform_driver cdns_gpio_driver = {
	.driver = {
		.name = "cdns-gpio",
		.of_match_table = cdns_of_ids,
	},
	.probe = cdns_gpio_probe,
	.remove = cdns_gpio_remove,
};
module_platform_driver(cdns_gpio_driver);

MODULE_AUTHOR("Jan Kotas <jank@cadence.com>");
MODULE_DESCRIPTION("Cadence GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cdns-gpio");
