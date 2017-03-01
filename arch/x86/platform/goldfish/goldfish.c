/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2011 Intel, Inc.
 * Copyright (C) 2013 Intel, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/platform_device.h>

/*
 * Where in virtual device memory the IO devices (timers, system controllers
 * and so on)
 */

#define GOLDFISH_PDEV_BUS_BASE	(0xff001000)
#define GOLDFISH_PDEV_BUS_END	(0xff7fffff)
#define GOLDFISH_PDEV_BUS_IRQ	(4)

#define GOLDFISH_TTY_BASE	(0x2000)

static struct resource goldfish_pdev_bus_resources[] = {
	{
		.start  = GOLDFISH_PDEV_BUS_BASE,
		.end    = GOLDFISH_PDEV_BUS_END,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start	= GOLDFISH_PDEV_BUS_IRQ,
		.end	= GOLDFISH_PDEV_BUS_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static bool goldfish_enable __initdata;

static int __init goldfish_setup(char *str)
{
	goldfish_enable = true;
	return 0;
}
__setup("goldfish", goldfish_setup);

static int __init goldfish_init(void)
{
	if (!goldfish_enable)
		return -ENODEV;

	platform_device_register_simple("goldfish_pdev_bus", -1,
					goldfish_pdev_bus_resources, 2);
	return 0;
}
device_initcall(goldfish_init);
