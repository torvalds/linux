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
	struct device *dev = &pdev->dev;
	struct gpio_chip *gc;
	void __iomem *regs;
	int ret;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ret = bgpio_init(gc, dev, 4, regs + GEF_GPIO_IN, regs + GEF_GPIO_OUT,
			 NULL, NULL, regs + GEF_GPIO_DIRECT,
			 BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (ret)
		return dev_err_probe(dev, ret, "bgpio_init failed\n");

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
