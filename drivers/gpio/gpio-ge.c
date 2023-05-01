/*
 * Driver for GE FPGA based GPIO
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 *
 * 2008 (c) GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/* TODO
 *
 * Configuration of output modes (totem-pole/open-drain)
 * Interrupt configuration - interrupts are always generated the FPGA relies on
 * the I/O interrupt controllers mask to stop them propergating
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>

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
	struct gpio_chip *gc;
	void __iomem *regs;
	int ret;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	regs = of_iomap(pdev->dev.of_node, 0);
	if (!regs)
		return -ENOMEM;

	ret = bgpio_init(gc, &pdev->dev, 4, regs + GEF_GPIO_IN,
			 regs + GEF_GPIO_OUT, NULL, NULL,
			 regs + GEF_GPIO_DIRECT, BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (ret) {
		dev_err(&pdev->dev, "bgpio_init failed\n");
		goto err0;
	}

	/* Setup pointers to chip functions */
	gc->label = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%pOF", pdev->dev.of_node);
	if (!gc->label) {
		ret = -ENOMEM;
		goto err0;
	}

	gc->base = -1;
	gc->ngpio = (u16)(uintptr_t)of_device_get_match_data(&pdev->dev);

	/* This function adds a memory mapped GPIO chip */
	ret = devm_gpiochip_add_data(&pdev->dev, gc, NULL);
	if (ret)
		goto err0;

	return 0;
err0:
	iounmap(regs);
	pr_err("%pOF: GPIO chip registration failed\n", pdev->dev.of_node);
	return ret;
};

static struct platform_driver gef_gpio_driver = {
	.driver = {
		.name		= "gef-gpio",
		.of_match_table	= gef_gpio_ids,
	},
};
module_platform_driver_probe(gef_gpio_driver, gef_gpio_probe);

MODULE_DESCRIPTION("GE I/O FPGA GPIO driver");
MODULE_AUTHOR("Martyn Welch <martyn.welch@ge.com");
MODULE_LICENSE("GPL");
