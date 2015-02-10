/*
 * MOXA ART SoCs GPIO driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/bitops.h>

#define GPIO_DATA_OUT		0x00
#define GPIO_DATA_IN		0x04
#define GPIO_PIN_DIRECTION	0x08

struct moxart_gpio_chip {
	struct gpio_chip gpio;
	void __iomem *base;
};

static inline struct moxart_gpio_chip *to_moxart_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct moxart_gpio_chip, gpio);
}

static int moxart_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(offset);
}

static void moxart_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(offset);
}

static void moxart_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct moxart_gpio_chip *gc = to_moxart_gpio(chip);
	void __iomem *ioaddr = gc->base + GPIO_DATA_OUT;
	u32 reg = readl(ioaddr);

	if (value)
		reg = reg | BIT(offset);
	else
		reg = reg & ~BIT(offset);

	writel(reg, ioaddr);
}

static int moxart_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct moxart_gpio_chip *gc = to_moxart_gpio(chip);
	u32 ret = readl(gc->base + GPIO_PIN_DIRECTION);

	if (ret & BIT(offset))
		return !!(readl(gc->base + GPIO_DATA_OUT) & BIT(offset));
	else
		return !!(readl(gc->base + GPIO_DATA_IN) & BIT(offset));
}

static int moxart_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct moxart_gpio_chip *gc = to_moxart_gpio(chip);
	void __iomem *ioaddr = gc->base + GPIO_PIN_DIRECTION;

	writel(readl(ioaddr) & ~BIT(offset), ioaddr);
	return 0;
}

static int moxart_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct moxart_gpio_chip *gc = to_moxart_gpio(chip);
	void __iomem *ioaddr = gc->base + GPIO_PIN_DIRECTION;

	moxart_gpio_set(chip, offset, value);
	writel(readl(ioaddr) | BIT(offset), ioaddr);
	return 0;
}

static struct gpio_chip moxart_template_chip = {
	.label			= "moxart-gpio",
	.request		= moxart_gpio_request,
	.free			= moxart_gpio_free,
	.direction_input	= moxart_gpio_direction_input,
	.direction_output	= moxart_gpio_direction_output,
	.set			= moxart_gpio_set,
	.get			= moxart_gpio_get,
	.ngpio			= 32,
	.owner			= THIS_MODULE,
};

static int moxart_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct moxart_gpio_chip *mgc;
	int ret;

	mgc = devm_kzalloc(dev, sizeof(*mgc), GFP_KERNEL);
	if (!mgc)
		return -ENOMEM;
	mgc->gpio = moxart_template_chip;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mgc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mgc->base))
		return PTR_ERR(mgc->base);

	mgc->gpio.dev = dev;

	ret = gpiochip_add(&mgc->gpio);
	if (ret) {
		dev_err(dev, "%s: gpiochip_add failed\n",
			dev->of_node->full_name);
		return ret;
	}

	return 0;
}

static const struct of_device_id moxart_gpio_match[] = {
	{ .compatible = "moxa,moxart-gpio" },
	{ }
};

static struct platform_driver moxart_gpio_driver = {
	.driver	= {
		.name		= "moxart-gpio",
		.of_match_table	= moxart_gpio_match,
	},
	.probe	= moxart_gpio_probe,
};
module_platform_driver(moxart_gpio_driver);

MODULE_DESCRIPTION("MOXART GPIO chip driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas Jensen <jonas.jensen@gmail.com>");
