// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for GE FPGA based GPIO
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 *
 * 2008 (c) GE Intelligent Platforms Embedded Systems, Inc.
 */

/*
 * TODO:
 *
 * Configuration of output modes (totem-pole/open-drain).
 * Interrupt configuration - interrupts are always generated, the FPGA relies
 * on the I/O interrupt controllers mask to stop them from being propagated.
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

#define GEF_GPIO_DIRECT		0x00
#define GEF_GPIO_IN		0x04
#define GEF_GPIO_OUT		0x08
#define GEF_GPIO_TRIG		0x0C
#define GEF_GPIO_POLAR_A	0x10
#define GEF_GPIO_POLAR_B	0x14
#define GEF_GPIO_INT_STAT	0x18
#define GEF_GPIO_OVERRUN	0x1C
#define GEF_GPIO_MODE		0x20

static const struct of_device_id gef_gpio_ids[] = {
	{
		.compatible	= "gef,sbc610-gpio",
		.data		= (void *)19,
	}, {
		.compatible	= "gef,sbc310-gpio",
		.data		= (void *)6,
	}, {
		.compatible	= "ge,imp3a-gpio",
		.data		= (void *)16,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, gef_gpio_ids);

static int __init gef_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct gpio_generic_chip *chip;
	struct gpio_chip *gc;
	void __iomem *regs;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = regs + GEF_GPIO_IN,
		.set = regs + GEF_GPIO_OUT,
		.dirin = regs + GEF_GPIO_DIRECT,
		.flags = GPIO_GENERIC_BIG_ENDIAN_BYTE_ORDER,
	};

	ret = gpio_generic_chip_init(chip, &config);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to initialize the generic GPIO chip\n");

	gc = &chip->gc;

	/* Setup pointers to chip functions */
	gc->label = devm_kasprintf(dev, GFP_KERNEL, "%pfw", dev_fwnode(dev));
	if (!gc->label)
		return -ENOMEM;

	gc->base = -1;
	gc->ngpio = (uintptr_t)device_get_match_data(dev);

	/* This function adds a memory mapped GPIO chip */
	ret = devm_gpiochip_add_data(dev, gc, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "GPIO chip registration failed\n");

	return 0;
};

static struct platform_driver gef_gpio_driver = {
	.driver = {
		.name		= "gef-gpio",
		.of_match_table	= gef_gpio_ids,
	},
};
module_platform_driver_probe(gef_gpio_driver, gef_gpio_probe);

MODULE_DESCRIPTION("GE I/O FPGA GPIO driver");
MODULE_AUTHOR("Martyn Welch <martyn.welch@ge.com>");
MODULE_LICENSE("GPL");
