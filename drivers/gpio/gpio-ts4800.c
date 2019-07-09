/*
 * GPIO driver for the TS-4800 board
 *
 * Copyright (c) 2016 - Savoir-faire Linux
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define DEFAULT_PIN_NUMBER      16
#define INPUT_REG_OFFSET        0x00
#define OUTPUT_REG_OFFSET       0x02
#define DIRECTION_REG_OFFSET    0x04

static int ts4800_gpio_probe(struct platform_device *pdev)
{
	struct device_node *node;
	struct gpio_chip *chip;
	void __iomem *base_addr;
	int retval;
	u32 ngpios;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct gpio_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base_addr))
		return PTR_ERR(base_addr);

	node = pdev->dev.of_node;
	if (!node)
		return -EINVAL;

	retval = of_property_read_u32(node, "ngpios", &ngpios);
	if (retval == -EINVAL)
		ngpios = DEFAULT_PIN_NUMBER;
	else if (retval)
		return retval;

	retval = bgpio_init(chip, &pdev->dev, 2, base_addr + INPUT_REG_OFFSET,
			    base_addr + OUTPUT_REG_OFFSET, NULL,
			    base_addr + DIRECTION_REG_OFFSET, NULL, 0);
	if (retval) {
		dev_err(&pdev->dev, "bgpio_init failed\n");
		return retval;
	}

	chip->ngpio = ngpios;

	platform_set_drvdata(pdev, chip);

	return devm_gpiochip_add_data(&pdev->dev, chip, NULL);
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
