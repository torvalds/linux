// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the TS-4800 board
 *
 * Copyright (c) 2016 - Savoir-faire Linux
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define DEFAULT_PIN_NUMBER      16
#define INPUT_REG_OFFSET        0x00
#define OUTPUT_REG_OFFSET       0x02
#define DIRECTION_REG_OFFSET    0x04

static int ts4800_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct gpio_generic_chip *chip;
	void __iomem *base_addr;
	int retval;
	u32 ngpios;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base_addr))
		return PTR_ERR(base_addr);

	retval = device_property_read_u32(dev, "ngpios", &ngpios);
	if (retval == -EINVAL)
		ngpios = DEFAULT_PIN_NUMBER;
	else if (retval)
		return retval;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 2,
		.dat = base_addr + INPUT_REG_OFFSET,
		.set = base_addr + OUTPUT_REG_OFFSET,
		.dirout = base_addr + DIRECTION_REG_OFFSET,
	};

	retval = gpio_generic_chip_init(chip, &config);
	if (retval)
		return dev_err_probe(dev, retval,
				     "failed to initialize the generic GPIO chip\n");

	chip->gc.ngpio = ngpios;

	return devm_gpiochip_add_data(dev, &chip->gc, NULL);
}

static const struct of_device_id ts4800_gpio_of_match[] = {
	{ .compatible = "technologic,ts4800-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, ts4800_gpio_of_match);

static struct platform_driver ts4800_gpio_driver = {
	.driver = {
		   .name = "ts4800-gpio",
		   .of_match_table = ts4800_gpio_of_match,
		   },
	.probe = ts4800_gpio_probe,
};

module_platform_driver_probe(ts4800_gpio_driver, ts4800_gpio_probe);

MODULE_AUTHOR("Julien Grossholtz <julien.grossholtz@savoirfairelinux.com>");
MODULE_DESCRIPTION("TS4800 FPGA GPIO driver");
MODULE_LICENSE("GPL v2");
