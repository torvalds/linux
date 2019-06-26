/*
 * AMD Promontory GPIO driver
 *
 * Copyright (C) 2015 ASMedia Technology Inc.
 * Author: YD Tseng <yd_tseng@asmedia.com.tw>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/spinlock.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>

#define PT_TOTAL_GPIO 8

/* PCI-E MMIO register offsets */
#define PT_DIRECTION_REG   0x00
#define PT_INPUTDATA_REG   0x04
#define PT_OUTPUTDATA_REG  0x08
#define PT_CLOCKRATE_REG   0x0C
#define PT_SYNC_REG        0x28

struct pt_gpio_chip {
	struct gpio_chip         gc;
	void __iomem             *reg_base;
};

static int pt_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	struct pt_gpio_chip *pt_gpio = gpiochip_get_data(gc);
	unsigned long flags;
	u32 using_pins;

	dev_dbg(gc->parent, "pt_gpio_request offset=%x\n", offset);

	spin_lock_irqsave(&gc->bgpio_lock, flags);

	using_pins = readl(pt_gpio->reg_base + PT_SYNC_REG);
	if (using_pins & BIT(offset)) {
		dev_warn(gc->parent, "PT GPIO pin %x reconfigured\n",
			 offset);
		spin_unlock_irqrestore(&gc->bgpio_lock, flags);
		return -EINVAL;
	}

	writel(using_pins | BIT(offset), pt_gpio->reg_base + PT_SYNC_REG);

	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	return 0;
}

static void pt_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	struct pt_gpio_chip *pt_gpio = gpiochip_get_data(gc);
	unsigned long flags;
	u32 using_pins;

	spin_lock_irqsave(&gc->bgpio_lock, flags);

	using_pins = readl(pt_gpio->reg_base + PT_SYNC_REG);
	using_pins &= ~BIT(offset);
	writel(using_pins, pt_gpio->reg_base + PT_SYNC_REG);

	spin_unlock_irqrestore(&gc->bgpio_lock, flags);

	dev_dbg(gc->parent, "pt_gpio_free offset=%x\n", offset);
}

static int pt_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acpi_device *acpi_dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	struct pt_gpio_chip *pt_gpio;
	int ret = 0;

	if (acpi_bus_get_device(handle, &acpi_dev)) {
		dev_err(dev, "PT GPIO device node not found\n");
		return -ENODEV;
	}

	pt_gpio = devm_kzalloc(dev, sizeof(struct pt_gpio_chip), GFP_KERNEL);
	if (!pt_gpio)
		return -ENOMEM;

	pt_gpio->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pt_gpio->reg_base)) {
		dev_err(&pdev->dev, "Failed to map MMIO resource for PT GPIO.\n");
		return PTR_ERR(pt_gpio->reg_base);
	}

	ret = bgpio_init(&pt_gpio->gc, dev, 4,
			 pt_gpio->reg_base + PT_INPUTDATA_REG,
			 pt_gpio->reg_base + PT_OUTPUTDATA_REG, NULL,
			 pt_gpio->reg_base + PT_DIRECTION_REG, NULL,
			 BGPIOF_READ_OUTPUT_REG_SET);
	if (ret) {
		dev_err(&pdev->dev, "bgpio_init failed\n");
		return ret;
	}

	pt_gpio->gc.owner            = THIS_MODULE;
	pt_gpio->gc.request          = pt_gpio_request;
	pt_gpio->gc.free             = pt_gpio_free;
	pt_gpio->gc.ngpio            = PT_TOTAL_GPIO;
#if defined(CONFIG_OF_GPIO)
	pt_gpio->gc.of_node          = pdev->dev.of_node;
#endif
	ret = gpiochip_add_data(&pt_gpio->gc, pt_gpio);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPIO lib\n");
		return ret;
	}

	platform_set_drvdata(pdev, pt_gpio);

	/* initialize register setting */
	writel(0, pt_gpio->reg_base + PT_SYNC_REG);
	writel(0, pt_gpio->reg_base + PT_CLOCKRATE_REG);

	dev_dbg(&pdev->dev, "PT GPIO driver loaded\n");
	return ret;
}

static int pt_gpio_remove(struct platform_device *pdev)
{
	struct pt_gpio_chip *pt_gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&pt_gpio->gc);

	return 0;
}

static const struct acpi_device_id pt_gpio_acpi_match[] = {
	{ "AMDF030", 0 },
	{ "AMDIF030", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pt_gpio_acpi_match);

static struct platform_driver pt_gpio_driver = {
	.driver = {
		.name = "pt-gpio",
		.acpi_match_table = ACPI_PTR(pt_gpio_acpi_match),
	},
	.probe = pt_gpio_probe,
	.remove = pt_gpio_remove,
};

module_platform_driver(pt_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YD Tseng <yd_tseng@asmedia.com.tw>");
MODULE_DESCRIPTION("AMD Promontory GPIO Driver");
