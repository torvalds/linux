/*
 * GPIO driver for Exar XR17V35X chip
 *
 * Copyright (C) 2015 Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#define EXAR_OFFSET_MPIOLVL_LO 0x90
#define EXAR_OFFSET_MPIOSEL_LO 0x93
#define EXAR_OFFSET_MPIOLVL_HI 0x96
#define EXAR_OFFSET_MPIOSEL_HI 0x99

#define DRIVER_NAME "gpio_exar"

static DEFINE_IDA(ida_index);

struct exar_gpio_chip {
	struct gpio_chip gpio_chip;
	struct mutex lock;
	int index;
	void __iomem *regs;
	char name[20];
};

static void exar_update(struct gpio_chip *chip, unsigned int reg, int val,
			unsigned int offset)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	int temp;

	mutex_lock(&exar_gpio->lock);
	temp = readb(exar_gpio->regs + reg);
	temp &= ~BIT(offset);
	if (val)
		temp |= BIT(offset);
	writeb(temp, exar_gpio->regs + reg);
	mutex_unlock(&exar_gpio->lock);
}

static int exar_set_direction(struct gpio_chip *chip, int direction,
			      unsigned int offset)
{
	unsigned int bank = offset / 8;
	unsigned int addr;

	addr = bank ? EXAR_OFFSET_MPIOSEL_HI : EXAR_OFFSET_MPIOSEL_LO;
	exar_update(chip, addr, direction, offset % 8);
	return 0;
}

static int exar_direction_output(struct gpio_chip *chip, unsigned int offset,
				 int value)
{
	return exar_set_direction(chip, 0, offset);
}

static int exar_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	return exar_set_direction(chip, 1, offset);
}

static int exar_get(struct gpio_chip *chip, unsigned int reg)
{
	struct exar_gpio_chip *exar_gpio = gpiochip_get_data(chip);
	int value;

	mutex_lock(&exar_gpio->lock);
	value = readb(exar_gpio->regs + reg);
	mutex_unlock(&exar_gpio->lock);

	return !!value;
}

static int exar_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	unsigned int bank = offset / 8;
	unsigned int addr;
	int val;

	addr = bank ? EXAR_OFFSET_MPIOSEL_HI : EXAR_OFFSET_MPIOSEL_LO;
	val = exar_get(chip, addr) >> (offset % 8);

	return !!val;
}

static int exar_get_value(struct gpio_chip *chip, unsigned int offset)
{
	unsigned int bank = offset / 8;
	unsigned int addr;
	int val;

	addr = bank ? EXAR_OFFSET_MPIOLVL_LO : EXAR_OFFSET_MPIOLVL_HI;
	val = exar_get(chip, addr) >> (offset % 8);

	return !!val;
}

static void exar_set_value(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	unsigned int bank = offset / 8;
	unsigned int addr;

	addr = bank ? EXAR_OFFSET_MPIOLVL_HI : EXAR_OFFSET_MPIOLVL_LO;
	exar_update(chip, addr, value, offset % 8);
}

static int gpio_exar_probe(struct platform_device *pdev)
{
	struct pci_dev *pcidev = platform_get_drvdata(pdev);
	struct exar_gpio_chip *exar_gpio;
	void __iomem *p;
	int index, ret;

	if (pcidev->vendor != PCI_VENDOR_ID_EXAR)
		return -ENODEV;

	/*
	 * Map the pci device to get the register addresses.
	 * We will need to read and write those registers to control
	 * the GPIO pins.
	 * Using managed functions will save us from unmaping on exit.
	 * As the device is enabled using managed functions by the
	 * UART driver we can also use managed functions here.
	 */
	p = pcim_iomap(pcidev, 0, 0);
	if (!p)
		return -ENOMEM;

	exar_gpio = devm_kzalloc(&pcidev->dev, sizeof(*exar_gpio), GFP_KERNEL);
	if (!exar_gpio)
		return -ENOMEM;

	mutex_init(&exar_gpio->lock);

	index = ida_simple_get(&ida_index, 0, 0, GFP_KERNEL);

	sprintf(exar_gpio->name, "exar_gpio%d", index);
	exar_gpio->gpio_chip.label = exar_gpio->name;
	exar_gpio->gpio_chip.parent = &pcidev->dev;
	exar_gpio->gpio_chip.direction_output = exar_direction_output;
	exar_gpio->gpio_chip.direction_input = exar_direction_input;
	exar_gpio->gpio_chip.get_direction = exar_get_direction;
	exar_gpio->gpio_chip.get = exar_get_value;
	exar_gpio->gpio_chip.set = exar_set_value;
	exar_gpio->gpio_chip.base = -1;
	exar_gpio->gpio_chip.ngpio = 16;
	exar_gpio->regs = p;
	exar_gpio->index = index;

	ret = devm_gpiochip_add_data(&pcidev->dev,
				     &exar_gpio->gpio_chip, exar_gpio);
	if (ret)
		goto err_destroy;

	platform_set_drvdata(pdev, exar_gpio);

	return 0;

err_destroy:
	ida_simple_remove(&ida_index, index);
	mutex_destroy(&exar_gpio->lock);
	return ret;
}

static int gpio_exar_remove(struct platform_device *pdev)
{
	struct exar_gpio_chip *exar_gpio = platform_get_drvdata(pdev);

	ida_simple_remove(&ida_index, exar_gpio->index);
	mutex_destroy(&exar_gpio->lock);

	return 0;
}

static struct platform_driver gpio_exar_driver = {
	.probe	= gpio_exar_probe,
	.remove	= gpio_exar_remove,
	.driver	= {
		.name = DRIVER_NAME,
	},
};

module_platform_driver(gpio_exar_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_DESCRIPTION("Exar GPIO driver");
MODULE_AUTHOR("Sudip Mukherjee <sudip.mukherjee@codethink.co.uk>");
MODULE_LICENSE("GPL");
