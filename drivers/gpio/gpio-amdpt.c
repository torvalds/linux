// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Promontory GPIO driver
 *
 * Copyright (C) 2015 ASMedia Technology Inc.
 * Author: YD Tseng <yd_tseng@asmedia.com.tw>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/spinlock.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

#define PT_TOTAL_GPIO 8
#define PT_TOTAL_GPIO_EX 24

/* PCI-E MMIO register offsets */
#define PT_DIRECTION_REG   0x00
#define PT_INPUTDATA_REG   0x04
#define PT_OUTPUTDATA_REG  0x08
#define PT_CLOCKRATE_REG   0x0C
#define PT_SYNC_REG        0x28

struct pt_gpio_chip {
	struct gpio_generic_chip chip;
	void __iomem             *reg_base;
};

static int pt_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_generic_chip *gen_gc = to_gpio_generic_chip(gc);
	struct pt_gpio_chip *pt_gpio = gpiochip_get_data(gc);
	u32 using_pins;

	dev_dbg(gc->parent, "pt_gpio_request offset=%x\n", offset);

	guard(gpio_generic_lock_irqsave)(gen_gc);

	using_pins = readl(pt_gpio->reg_base + PT_SYNC_REG);
	if (using_pins & BIT(offset)) {
		dev_warn(gc->parent, "PT GPIO pin %x reconfigured\n",
			 offset);
		return -EINVAL;
	}

	writel(using_pins | BIT(offset), pt_gpio->reg_base + PT_SYNC_REG);

	return 0;
}

static void pt_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct gpio_generic_chip *gen_gc = to_gpio_generic_chip(gc);
	struct pt_gpio_chip *pt_gpio = gpiochip_get_data(gc);
	u32 using_pins;

	guard(gpio_generic_lock_irqsave)(gen_gc);

	using_pins = readl(pt_gpio->reg_base + PT_SYNC_REG);
	using_pins &= ~BIT(offset);
	writel(using_pins, pt_gpio->reg_base + PT_SYNC_REG);

	dev_dbg(gc->parent, "pt_gpio_free offset=%x\n", offset);
}

static int pt_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct pt_gpio_chip *pt_gpio;
	int ret = 0;

	if (!ACPI_COMPANION(dev)) {
		dev_err(dev, "PT GPIO device node not found\n");
		return -ENODEV;
	}

	pt_gpio = devm_kzalloc(dev, sizeof(struct pt_gpio_chip), GFP_KERNEL);
	if (!pt_gpio)
		return -ENOMEM;

	pt_gpio->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pt_gpio->reg_base)) {
		dev_err(dev, "Failed to map MMIO resource for PT GPIO.\n");
		return PTR_ERR(pt_gpio->reg_base);
	}

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = pt_gpio->reg_base + PT_INPUTDATA_REG,
		.set = pt_gpio->reg_base + PT_OUTPUTDATA_REG,
		.dirout = pt_gpio->reg_base + PT_DIRECTION_REG,
		.flags = GPIO_GENERIC_READ_OUTPUT_REG_SET,
	};

	ret = gpio_generic_chip_init(&pt_gpio->chip, &config);
	if (ret) {
		dev_err(dev, "failed to initialize the generic GPIO chip\n");
		return ret;
	}

	pt_gpio->chip.gc.owner = THIS_MODULE;
	pt_gpio->chip.gc.request = pt_gpio_request;
	pt_gpio->chip.gc.free = pt_gpio_free;
	pt_gpio->chip.gc.ngpio = (uintptr_t)device_get_match_data(dev);

	ret = devm_gpiochip_add_data(dev, &pt_gpio->chip.gc, pt_gpio);
	if (ret) {
		dev_err(dev, "Failed to register GPIO lib\n");
		return ret;
	}

	platform_set_drvdata(pdev, pt_gpio);

	/* initialize register setting */
	writel(0, pt_gpio->reg_base + PT_SYNC_REG);
	writel(0, pt_gpio->reg_base + PT_CLOCKRATE_REG);

	dev_dbg(dev, "PT GPIO driver loaded\n");
	return ret;
}

static const struct acpi_device_id pt_gpio_acpi_match[] = {
	{ "AMDF030", PT_TOTAL_GPIO },
	{ "AMDIF030", PT_TOTAL_GPIO },
	{ "AMDIF031", PT_TOTAL_GPIO_EX },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pt_gpio_acpi_match);

static struct platform_driver pt_gpio_driver = {
	.driver = {
		.name = "pt-gpio",
		.acpi_match_table = ACPI_PTR(pt_gpio_acpi_match),
	},
	.probe = pt_gpio_probe,
};

module_platform_driver(pt_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YD Tseng <yd_tseng@asmedia.com.tw>");
MODULE_DESCRIPTION("AMD Promontory GPIO Driver");
