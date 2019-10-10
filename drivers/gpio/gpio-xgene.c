// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppliedMicro X-Gene SoC GPIO Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>.
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/types.h>
#include <linux/bitops.h>

#define GPIO_SET_DR_OFFSET	0x0C
#define GPIO_DATA_OFFSET	0x14
#define GPIO_BANK_STRIDE	0x0C

#define XGENE_GPIOS_PER_BANK	16
#define XGENE_MAX_GPIO_BANKS	3
#define XGENE_MAX_GPIOS		(XGENE_GPIOS_PER_BANK * XGENE_MAX_GPIO_BANKS)

#define GPIO_BIT_OFFSET(x)	(x % XGENE_GPIOS_PER_BANK)
#define GPIO_BANK_OFFSET(x)	((x / XGENE_GPIOS_PER_BANK) * GPIO_BANK_STRIDE)

struct xgene_gpio {
	struct gpio_chip	chip;
	void __iomem		*base;
	spinlock_t		lock;
	u32			set_dr_val[XGENE_MAX_GPIO_BANKS];
};

static int xgene_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long bank_offset;
	u32 bit_offset;

	bank_offset = GPIO_DATA_OFFSET + GPIO_BANK_OFFSET(offset);
	bit_offset = GPIO_BIT_OFFSET(offset);
	return !!(ioread32(chip->base + bank_offset) & BIT(bit_offset));
}

static void __xgene_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long bank_offset;
	u32 setval, bit_offset;

	bank_offset = GPIO_SET_DR_OFFSET + GPIO_BANK_OFFSET(offset);
	bit_offset = GPIO_BIT_OFFSET(offset) + XGENE_GPIOS_PER_BANK;

	setval = ioread32(chip->base + bank_offset);
	if (val)
		setval |= BIT(bit_offset);
	else
		setval &= ~BIT(bit_offset);
	iowrite32(setval, chip->base + bank_offset);
}

static void xgene_gpio_set(struct gpio_chip *gc, unsigned int offset, int val)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	__xgene_gpio_set(gc, offset, val);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int xgene_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long bank_offset, bit_offset;

	bank_offset = GPIO_SET_DR_OFFSET + GPIO_BANK_OFFSET(offset);
	bit_offset = GPIO_BIT_OFFSET(offset);

	return !!(ioread32(chip->base + bank_offset) & BIT(bit_offset));
}

static int xgene_gpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags, bank_offset;
	u32 dirval, bit_offset;

	bank_offset = GPIO_SET_DR_OFFSET + GPIO_BANK_OFFSET(offset);
	bit_offset = GPIO_BIT_OFFSET(offset);

	spin_lock_irqsave(&chip->lock, flags);

	dirval = ioread32(chip->base + bank_offset);
	dirval |= BIT(bit_offset);
	iowrite32(dirval, chip->base + bank_offset);

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int xgene_gpio_dir_out(struct gpio_chip *gc,
					unsigned int offset, int val)
{
	struct xgene_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags, bank_offset;
	u32 dirval, bit_offset;

	bank_offset = GPIO_SET_DR_OFFSET + GPIO_BANK_OFFSET(offset);
	bit_offset = GPIO_BIT_OFFSET(offset);

	spin_lock_irqsave(&chip->lock, flags);

	dirval = ioread32(chip->base + bank_offset);
	dirval &= ~BIT(bit_offset);
	iowrite32(dirval, chip->base + bank_offset);
	__xgene_gpio_set(gc, offset, val);

	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static __maybe_unused int xgene_gpio_suspend(struct device *dev)
{
	struct xgene_gpio *gpio = dev_get_drvdata(dev);
	unsigned long bank_offset;
	unsigned int bank;

	for (bank = 0; bank < XGENE_MAX_GPIO_BANKS; bank++) {
		bank_offset = GPIO_SET_DR_OFFSET + bank * GPIO_BANK_STRIDE;
		gpio->set_dr_val[bank] = ioread32(gpio->base + bank_offset);
	}
	return 0;
}

static __maybe_unused int xgene_gpio_resume(struct device *dev)
{
	struct xgene_gpio *gpio = dev_get_drvdata(dev);
	unsigned long bank_offset;
	unsigned int bank;

	for (bank = 0; bank < XGENE_MAX_GPIO_BANKS; bank++) {
		bank_offset = GPIO_SET_DR_OFFSET + bank * GPIO_BANK_STRIDE;
		iowrite32(gpio->set_dr_val[bank], gpio->base + bank_offset);
	}
	return 0;
}

static SIMPLE_DEV_PM_OPS(xgene_gpio_pm, xgene_gpio_suspend, xgene_gpio_resume);

static int xgene_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct xgene_gpio *gpio;
	int err = 0;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio) {
		err = -ENOMEM;
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -EINVAL;
		goto err;
	}

	gpio->base = devm_ioremap_nocache(&pdev->dev, res->start,
							resource_size(res));
	if (!gpio->base) {
		err = -ENOMEM;
		goto err;
	}

	gpio->chip.ngpio = XGENE_MAX_GPIOS;

	spin_lock_init(&gpio->lock);
	gpio->chip.parent = &pdev->dev;
	gpio->chip.get_direction = xgene_gpio_get_direction;
	gpio->chip.direction_input = xgene_gpio_dir_in;
	gpio->chip.direction_output = xgene_gpio_dir_out;
	gpio->chip.get = xgene_gpio_get;
	gpio->chip.set = xgene_gpio_set;
	gpio->chip.label = dev_name(&pdev->dev);
	gpio->chip.base = -1;

	platform_set_drvdata(pdev, gpio);

	err = devm_gpiochip_add_data(&pdev->dev, &gpio->chip, gpio);
	if (err) {
		dev_err(&pdev->dev,
			"failed to register gpiochip.\n");
		goto err;
	}

	dev_info(&pdev->dev, "X-Gene GPIO driver registered.\n");
	return 0;
err:
	dev_err(&pdev->dev, "X-Gene GPIO driver registration failed.\n");
	return err;
}

static const struct of_device_id xgene_gpio_of_match[] = {
	{ .compatible = "apm,xgene-gpio", },
	{},
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_gpio_acpi_match[] = {
	{ "APMC0D14", 0 },
	{ },
};
#endif

static struct platform_driver xgene_gpio_driver = {
	.driver = {
		.name = "xgene-gpio",
		.of_match_table = xgene_gpio_of_match,
		.acpi_match_table = ACPI_PTR(xgene_gpio_acpi_match),
		.pm     = &xgene_gpio_pm,
	},
	.probe = xgene_gpio_probe,
};
builtin_platform_driver(xgene_gpio_driver);
