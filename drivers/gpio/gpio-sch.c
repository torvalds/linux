// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO interface for Intel Poulsbo SCH
 *
 *  Copyright (c) 2010 CompuLab Ltd
 *  Author: Denis Turischev <denis@compulab.co.il>
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>

#define GEN	0x00
#define GIO	0x04
#define GLV	0x08

struct sch_gpio {
	struct gpio_chip chip;
	spinlock_t lock;
	unsigned short iobase;
	unsigned short resume_base;
};

static unsigned sch_gpio_offset(struct sch_gpio *sch, unsigned gpio,
				unsigned reg)
{
	unsigned base = 0;

	if (gpio >= sch->resume_base) {
		gpio -= sch->resume_base;
		base += 0x20;
	}

	return base + reg + gpio / 8;
}

static unsigned sch_gpio_bit(struct sch_gpio *sch, unsigned gpio)
{
	if (gpio >= sch->resume_base)
		gpio -= sch->resume_base;
	return gpio % 8;
}

static int sch_gpio_reg_get(struct sch_gpio *sch, unsigned gpio, unsigned reg)
{
	unsigned short offset, bit;
	u8 reg_val;

	offset = sch_gpio_offset(sch, gpio, reg);
	bit = sch_gpio_bit(sch, gpio);

	reg_val = !!(inb(sch->iobase + offset) & BIT(bit));

	return reg_val;
}

static void sch_gpio_reg_set(struct sch_gpio *sch, unsigned gpio, unsigned reg,
			     int val)
{
	unsigned short offset, bit;
	u8 reg_val;

	offset = sch_gpio_offset(sch, gpio, reg);
	bit = sch_gpio_bit(sch, gpio);

	reg_val = inb(sch->iobase + offset);

	if (val)
		outb(reg_val | BIT(bit), sch->iobase + offset);
	else
		outb((reg_val & ~BIT(bit)), sch->iobase + offset);
}

static int sch_gpio_direction_in(struct gpio_chip *gc, unsigned gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	spin_lock(&sch->lock);
	sch_gpio_reg_set(sch, gpio_num, GIO, 1);
	spin_unlock(&sch->lock);
	return 0;
}

static int sch_gpio_get(struct gpio_chip *gc, unsigned gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);
	return sch_gpio_reg_get(sch, gpio_num, GLV);
}

static void sch_gpio_set(struct gpio_chip *gc, unsigned gpio_num, int val)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	spin_lock(&sch->lock);
	sch_gpio_reg_set(sch, gpio_num, GLV, val);
	spin_unlock(&sch->lock);
}

static int sch_gpio_direction_out(struct gpio_chip *gc, unsigned gpio_num,
				  int val)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	spin_lock(&sch->lock);
	sch_gpio_reg_set(sch, gpio_num, GIO, 0);
	spin_unlock(&sch->lock);

	/*
	 * according to the datasheet, writing to the level register has no
	 * effect when GPIO is programmed as input.
	 * Actually the the level register is read-only when configured as input.
	 * Thus presetting the output level before switching to output is _NOT_ possible.
	 * Hence we set the level after configuring the GPIO as output.
	 * But we cannot prevent a short low pulse if direction is set to high
	 * and an external pull-up is connected.
	 */
	sch_gpio_set(gc, gpio_num, val);
	return 0;
}

static int sch_gpio_get_direction(struct gpio_chip *gc, unsigned gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	return sch_gpio_reg_get(sch, gpio_num, GIO);
}

static const struct gpio_chip sch_gpio_chip = {
	.label			= "sch_gpio",
	.owner			= THIS_MODULE,
	.direction_input	= sch_gpio_direction_in,
	.get			= sch_gpio_get,
	.direction_output	= sch_gpio_direction_out,
	.set			= sch_gpio_set,
	.get_direction		= sch_gpio_get_direction,
};

static int sch_gpio_probe(struct platform_device *pdev)
{
	struct sch_gpio *sch;
	struct resource *res;

	sch = devm_kzalloc(&pdev->dev, sizeof(*sch), GFP_KERNEL);
	if (!sch)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -EBUSY;

	if (!devm_request_region(&pdev->dev, res->start, resource_size(res),
				 pdev->name))
		return -EBUSY;

	spin_lock_init(&sch->lock);
	sch->iobase = res->start;
	sch->chip = sch_gpio_chip;
	sch->chip.label = dev_name(&pdev->dev);
	sch->chip.parent = &pdev->dev;

	switch (pdev->id) {
	case PCI_DEVICE_ID_INTEL_SCH_LPC:
		sch->resume_base = 10;
		sch->chip.ngpio = 14;

		/*
		 * GPIO[6:0] enabled by default
		 * GPIO7 is configured by the CMC as SLPIOVR
		 * Enable GPIO[9:8] core powered gpios explicitly
		 */
		sch_gpio_reg_set(sch, 8, GEN, 1);
		sch_gpio_reg_set(sch, 9, GEN, 1);
		/*
		 * SUS_GPIO[2:0] enabled by default
		 * Enable SUS_GPIO3 resume powered gpio explicitly
		 */
		sch_gpio_reg_set(sch, 13, GEN, 1);
		break;

	case PCI_DEVICE_ID_INTEL_ITC_LPC:
		sch->resume_base = 5;
		sch->chip.ngpio = 14;
		break;

	case PCI_DEVICE_ID_INTEL_CENTERTON_ILB:
		sch->resume_base = 21;
		sch->chip.ngpio = 30;
		break;

	case PCI_DEVICE_ID_INTEL_QUARK_X1000_ILB:
		sch->resume_base = 2;
		sch->chip.ngpio = 8;
		break;

	default:
		return -ENODEV;
	}

	platform_set_drvdata(pdev, sch);

	return devm_gpiochip_add_data(&pdev->dev, &sch->chip, sch);
}

static struct platform_driver sch_gpio_driver = {
	.driver = {
		.name = "sch_gpio",
	},
	.probe		= sch_gpio_probe,
};

module_platform_driver(sch_gpio_driver);

MODULE_AUTHOR("Denis Turischev <denis@compulab.co.il>");
MODULE_DESCRIPTION("GPIO interface for Intel Poulsbo SCH");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sch_gpio");
