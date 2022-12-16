// SPDX-License-Identifier: GPL-2.0-only
/*
 * sl28cpld GPIO driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* GPIO flavor */
#define GPIO_REG_DIR	0x00
#define GPIO_REG_OUT	0x01
#define GPIO_REG_IN	0x02
#define GPIO_REG_IE	0x03
#define GPIO_REG_IP	0x04

/* input-only flavor */
#define GPI_REG_IN	0x00

/* output-only flavor */
#define GPO_REG_OUT	0x00

enum sl28cpld_gpio_type {
	SL28CPLD_GPIO = 1,
	SL28CPLD_GPI,
	SL28CPLD_GPO,
};

static const struct regmap_irq sl28cpld_gpio_irqs[] = {
	REGMAP_IRQ_REG_LINE(0, 8),
	REGMAP_IRQ_REG_LINE(1, 8),
	REGMAP_IRQ_REG_LINE(2, 8),
	REGMAP_IRQ_REG_LINE(3, 8),
	REGMAP_IRQ_REG_LINE(4, 8),
	REGMAP_IRQ_REG_LINE(5, 8),
	REGMAP_IRQ_REG_LINE(6, 8),
	REGMAP_IRQ_REG_LINE(7, 8),
};

static int sl28cpld_gpio_irq_init(struct platform_device *pdev,
				  unsigned int base,
				  struct gpio_regmap_config *config)
{
	struct regmap_irq_chip_data *irq_data;
	struct regmap_irq_chip *irq_chip;
	struct device *dev = &pdev->dev;
	int irq, ret;

	if (!device_property_read_bool(dev, "interrupt-controller"))
		return 0;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	irq_chip = devm_kzalloc(dev, sizeof(*irq_chip), GFP_KERNEL);
	if (!irq_chip)
		return -ENOMEM;

	irq_chip->name = "sl28cpld-gpio-irq";
	irq_chip->irqs = sl28cpld_gpio_irqs;
	irq_chip->num_irqs = ARRAY_SIZE(sl28cpld_gpio_irqs);
	irq_chip->num_regs = 1;
	irq_chip->status_base = base + GPIO_REG_IP;
	irq_chip->unmask_base = base + GPIO_REG_IE;
	irq_chip->ack_base = base + GPIO_REG_IP;

	ret = devm_regmap_add_irq_chip_fwnode(dev, dev_fwnode(dev),
					      config->regmap, irq,
					      IRQF_SHARED | IRQF_ONESHOT,
					      0, irq_chip, &irq_data);
	if (ret)
		return ret;

	config->irq_domain = regmap_irq_get_domain(irq_data);

	return 0;
}

static int sl28cpld_gpio_probe(struct platform_device *pdev)
{
	struct gpio_regmap_config config = {0};
	enum sl28cpld_gpio_type type;
	struct regmap *regmap;
	u32 base;
	int ret;

	if (!pdev->dev.parent)
		return -ENODEV;

	type = (uintptr_t)device_get_match_data(&pdev->dev);
	if (!type)
		return -ENODEV;

	ret = device_property_read_u32(&pdev->dev, "reg", &base);
	if (ret)
		return -EINVAL;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap)
		return -ENODEV;

	config.regmap = regmap;
	config.parent = &pdev->dev;
	config.ngpio = 8;

	switch (type) {
	case SL28CPLD_GPIO:
		config.reg_dat_base = base + GPIO_REG_IN;
		config.reg_set_base = base + GPIO_REG_OUT;
		/* reg_dir_out_base might be zero */
		config.reg_dir_out_base = GPIO_REGMAP_ADDR(base + GPIO_REG_DIR);

		/* This type supports interrupts */
		ret = sl28cpld_gpio_irq_init(pdev, base, &config);
		if (ret)
			return ret;
		break;
	case SL28CPLD_GPO:
		config.reg_set_base = base + GPO_REG_OUT;
		break;
	case SL28CPLD_GPI:
		config.reg_dat_base = base + GPI_REG_IN;
		break;
	default:
		dev_err(&pdev->dev, "unknown type %d\n", type);
		return -ENODEV;
	}

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(&pdev->dev, &config));
}

static const struct of_device_id sl28cpld_gpio_of_match[] = {
	{ .compatible = "kontron,sl28cpld-gpio", .data = (void *)SL28CPLD_GPIO },
	{ .compatible = "kontron,sl28cpld-gpi", .data = (void *)SL28CPLD_GPI },
	{ .compatible = "kontron,sl28cpld-gpo", .data = (void *)SL28CPLD_GPO },
	{}
};
MODULE_DEVICE_TABLE(of, sl28cpld_gpio_of_match);

static struct platform_driver sl28cpld_gpio_driver = {
	.probe = sl28cpld_gpio_probe,
	.driver = {
		.name = "sl28cpld-gpio",
		.of_match_table = sl28cpld_gpio_of_match,
	},
};
module_platform_driver(sl28cpld_gpio_driver);

MODULE_DESCRIPTION("sl28cpld GPIO Driver");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_LICENSE("GPL");
