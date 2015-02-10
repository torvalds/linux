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
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/module.h>

#define GEF_GPIO_DIRECT		0x00
#define GEF_GPIO_IN		0x04
#define GEF_GPIO_OUT		0x08
#define GEF_GPIO_TRIG		0x0C
#define GEF_GPIO_POLAR_A	0x10
#define GEF_GPIO_POLAR_B	0x14
#define GEF_GPIO_INT_STAT	0x18
#define GEF_GPIO_OVERRUN	0x1C
#define GEF_GPIO_MODE		0x20

static void gef_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);
	unsigned int data;

	data = ioread32be(mmchip->regs + GEF_GPIO_OUT);
	if (value)
		data = data | BIT(offset);
	else
		data = data & ~BIT(offset);
	iowrite32be(data, mmchip->regs + GEF_GPIO_OUT);
}

static int gef_gpio_dir_in(struct gpio_chip *chip, unsigned offset)
{
	unsigned int data;
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);

	data = ioread32be(mmchip->regs + GEF_GPIO_DIRECT);
	data = data | BIT(offset);
	iowrite32be(data, mmchip->regs + GEF_GPIO_DIRECT);

	return 0;
}

static int gef_gpio_dir_out(struct gpio_chip *chip, unsigned offset, int value)
{
	unsigned int data;
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);

	/* Set value before switching to output */
	gef_gpio_set(mmchip->regs + GEF_GPIO_OUT, offset, value);

	data = ioread32be(mmchip->regs + GEF_GPIO_DIRECT);
	data = data & ~BIT(offset);
	iowrite32be(data, mmchip->regs + GEF_GPIO_DIRECT);

	return 0;
}

static int gef_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct of_mm_gpio_chip *mmchip = to_of_mm_gpio_chip(chip);

	return !!(ioread32be(mmchip->regs + GEF_GPIO_IN) & BIT(offset));
}

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
	const struct of_device_id *of_id =
		of_match_device(gef_gpio_ids, &pdev->dev);
	struct of_mm_gpio_chip *mmchip;

	mmchip = devm_kzalloc(&pdev->dev, sizeof(*mmchip), GFP_KERNEL);
	if (!mmchip)
		return -ENOMEM;

	/* Setup pointers to chip functions */
	mmchip->gc.ngpio = (u16)(uintptr_t)of_id->data;
	mmchip->gc.of_gpio_n_cells = 2;
	mmchip->gc.direction_input = gef_gpio_dir_in;
	mmchip->gc.direction_output = gef_gpio_dir_out;
	mmchip->gc.get = gef_gpio_get;
	mmchip->gc.set = gef_gpio_set;

	/* This function adds a memory mapped GPIO chip */
	return of_mm_gpiochip_add(pdev->dev.of_node, mmchip);
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
