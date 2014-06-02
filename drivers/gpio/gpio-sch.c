/*
 * GPIO interface for Intel Poulsbo SCH
 *
 *  Copyright (c) 2010 CompuLab Ltd
 *  Author: Denis Turischev <denis@compulab.co.il>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/pci_ids.h>

#include <linux/gpio.h>

static DEFINE_SPINLOCK(gpio_lock);

#define CGEN	(0x00)
#define CGIO	(0x04)
#define CGLV	(0x08)

#define RGEN	(0x20)
#define RGIO	(0x24)
#define RGLV	(0x28)

static unsigned short gpio_ba;

static int sch_gpio_core_direction_in(struct gpio_chip *gc, unsigned  gpio_num)
{
	u8 curr_dirs;
	unsigned short offset, bit;

	spin_lock(&gpio_lock);

	offset = CGIO + gpio_num / 8;
	bit = gpio_num % 8;

	curr_dirs = inb(gpio_ba + offset);

	if (!(curr_dirs & (1 << bit)))
		outb(curr_dirs | (1 << bit), gpio_ba + offset);

	spin_unlock(&gpio_lock);
	return 0;
}

static int sch_gpio_core_get(struct gpio_chip *gc, unsigned gpio_num)
{
	int res;
	unsigned short offset, bit;

	offset = CGLV + gpio_num / 8;
	bit = gpio_num % 8;

	res = !!(inb(gpio_ba + offset) & (1 << bit));
	return res;
}

static void sch_gpio_core_set(struct gpio_chip *gc, unsigned gpio_num, int val)
{
	u8 curr_vals;
	unsigned short offset, bit;

	spin_lock(&gpio_lock);

	offset = CGLV + gpio_num / 8;
	bit = gpio_num % 8;

	curr_vals = inb(gpio_ba + offset);

	if (val)
		outb(curr_vals | (1 << bit), gpio_ba + offset);
	else
		outb((curr_vals & ~(1 << bit)), gpio_ba + offset);
	spin_unlock(&gpio_lock);
}

static int sch_gpio_core_direction_out(struct gpio_chip *gc,
					unsigned gpio_num, int val)
{
	u8 curr_dirs;
	unsigned short offset, bit;

	spin_lock(&gpio_lock);

	offset = CGIO + gpio_num / 8;
	bit = gpio_num % 8;

	curr_dirs = inb(gpio_ba + offset);
	if (curr_dirs & (1 << bit))
		outb(curr_dirs & ~(1 << bit), gpio_ba + offset);

	spin_unlock(&gpio_lock);

	/*
	 * according to the datasheet, writing to the level register has no
	 * effect when GPIO is programmed as input.
	 * Actually the the level register is read-only when configured as input.
	 * Thus presetting the output level before switching to output is _NOT_ possible.
	 * Hence we set the level after configuring the GPIO as output.
	 * But we cannot prevent a short low pulse if direction is set to high
	 * and an external pull-up is connected.
	 */
	sch_gpio_core_set(gc, gpio_num, val);
	return 0;
}

static struct gpio_chip sch_gpio_core = {
	.label			= "sch_gpio_core",
	.owner			= THIS_MODULE,
	.direction_input	= sch_gpio_core_direction_in,
	.get			= sch_gpio_core_get,
	.direction_output	= sch_gpio_core_direction_out,
	.set			= sch_gpio_core_set,
};

static int sch_gpio_resume_direction_in(struct gpio_chip *gc,
					unsigned gpio_num)
{
	u8 curr_dirs;
	unsigned short offset, bit;

	spin_lock(&gpio_lock);

	offset = RGIO + gpio_num / 8;
	bit = gpio_num % 8;

	curr_dirs = inb(gpio_ba + offset);

	if (!(curr_dirs & (1 << bit)))
		outb(curr_dirs | (1 << bit), gpio_ba + offset);

	spin_unlock(&gpio_lock);
	return 0;
}

static int sch_gpio_resume_get(struct gpio_chip *gc, unsigned gpio_num)
{
	unsigned short offset, bit;

	offset = RGLV + gpio_num / 8;
	bit = gpio_num % 8;

	return !!(inb(gpio_ba + offset) & (1 << bit));
}

static void sch_gpio_resume_set(struct gpio_chip *gc,
				unsigned gpio_num, int val)
{
	u8 curr_vals;
	unsigned short offset, bit;

	spin_lock(&gpio_lock);

	offset = RGLV + gpio_num / 8;
	bit = gpio_num % 8;

	curr_vals = inb(gpio_ba + offset);

	if (val)
		outb(curr_vals | (1 << bit), gpio_ba + offset);
	else
		outb((curr_vals & ~(1 << bit)), gpio_ba + offset);

	spin_unlock(&gpio_lock);
}

static int sch_gpio_resume_direction_out(struct gpio_chip *gc,
					unsigned gpio_num, int val)
{
	u8 curr_dirs;
	unsigned short offset, bit;

	offset = RGIO + gpio_num / 8;
	bit = gpio_num % 8;

	spin_lock(&gpio_lock);

	curr_dirs = inb(gpio_ba + offset);
	if (curr_dirs & (1 << bit))
		outb(curr_dirs & ~(1 << bit), gpio_ba + offset);

	spin_unlock(&gpio_lock);

	/*
	* according to the datasheet, writing to the level register has no
	* effect when GPIO is programmed as input.
	* Actually the the level register is read-only when configured as input.
	* Thus presetting the output level before switching to output is _NOT_ possible.
	* Hence we set the level after configuring the GPIO as output.
	* But we cannot prevent a short low pulse if direction is set to high
	* and an external pull-up is connected.
	*/
	sch_gpio_resume_set(gc, gpio_num, val);
	return 0;
}

static struct gpio_chip sch_gpio_resume = {
	.label			= "sch_gpio_resume",
	.owner			= THIS_MODULE,
	.direction_input	= sch_gpio_resume_direction_in,
	.get			= sch_gpio_resume_get,
	.direction_output	= sch_gpio_resume_direction_out,
	.set			= sch_gpio_resume_set,
};

static int sch_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	int err, id;

	id = pdev->id;
	if (!id)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -EBUSY;

	if (!request_region(res->start, resource_size(res), pdev->name))
		return -EBUSY;

	gpio_ba = res->start;

	switch (id) {
	case PCI_DEVICE_ID_INTEL_SCH_LPC:
		sch_gpio_core.base = 0;
		sch_gpio_core.ngpio = 10;
		sch_gpio_resume.base = 10;
		sch_gpio_resume.ngpio = 4;
		/*
		 * GPIO[6:0] enabled by default
		 * GPIO7 is configured by the CMC as SLPIOVR
		 * Enable GPIO[9:8] core powered gpios explicitly
		 */
		outb(0x3, gpio_ba + CGEN + 1);
		/*
		 * SUS_GPIO[2:0] enabled by default
		 * Enable SUS_GPIO3 resume powered gpio explicitly
		 */
		outb(0x8, gpio_ba + RGEN);
		break;

	case PCI_DEVICE_ID_INTEL_ITC_LPC:
		sch_gpio_core.base = 0;
		sch_gpio_core.ngpio = 5;
		sch_gpio_resume.base = 5;
		sch_gpio_resume.ngpio = 9;
		break;

	case PCI_DEVICE_ID_INTEL_CENTERTON_ILB:
		sch_gpio_core.base = 0;
		sch_gpio_core.ngpio = 21;
		sch_gpio_resume.base = 21;
		sch_gpio_resume.ngpio = 9;
		break;

	default:
		err = -ENODEV;
		goto err_sch_gpio_core;
	}

	sch_gpio_core.dev = &pdev->dev;
	sch_gpio_resume.dev = &pdev->dev;

	err = gpiochip_add(&sch_gpio_core);
	if (err < 0)
		goto err_sch_gpio_core;

	err = gpiochip_add(&sch_gpio_resume);
	if (err < 0)
		goto err_sch_gpio_resume;

	return 0;

err_sch_gpio_resume:
	if (gpiochip_remove(&sch_gpio_core))
		dev_err(&pdev->dev, "%s gpiochip_remove failed\n", __func__);

err_sch_gpio_core:
	release_region(res->start, resource_size(res));
	gpio_ba = 0;

	return err;
}

static int sch_gpio_remove(struct platform_device *pdev)
{
	struct resource *res;
	if (gpio_ba) {
		int err;

		err  = gpiochip_remove(&sch_gpio_core);
		if (err)
			dev_err(&pdev->dev, "%s failed, %d\n",
				"gpiochip_remove()", err);
		err = gpiochip_remove(&sch_gpio_resume);
		if (err)
			dev_err(&pdev->dev, "%s failed, %d\n",
				"gpiochip_remove()", err);

		res = platform_get_resource(pdev, IORESOURCE_IO, 0);

		release_region(res->start, resource_size(res));
		gpio_ba = 0;

		return err;
	}

	return 0;
}

static struct platform_driver sch_gpio_driver = {
	.driver = {
		.name = "sch_gpio",
		.owner = THIS_MODULE,
	},
	.probe		= sch_gpio_probe,
	.remove		= sch_gpio_remove,
};

module_platform_driver(sch_gpio_driver);

MODULE_AUTHOR("Denis Turischev <denis@compulab.co.il>");
MODULE_DESCRIPTION("GPIO interface for Intel Poulsbo SCH");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sch_gpio");
