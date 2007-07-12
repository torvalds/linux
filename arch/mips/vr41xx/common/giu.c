/*
 *  NEC VR4100 series GIU platform device.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/cpu.h>
#include <asm/vr41xx/giu.h>
#include <asm/vr41xx/irq.h>

static struct resource giu_50pins_pullupdown_resource[] __initdata = {
	{
		.start	= 0x0b000100,
		.end	= 0x0b00011f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x0b0002e0,
		.end	= 0x0b0002e3,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= GIUINT_IRQ,
		.end	= GIUINT_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource giu_36pins_resource[] __initdata = {
	{
		.start	= 0x0f000140,
		.end	= 0x0f00015f,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= GIUINT_IRQ,
		.end	= GIUINT_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource giu_48pins_resource[] __initdata = {
	{
		.start	= 0x0f000140,
		.end	= 0x0f000167,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= GIUINT_IRQ,
		.end	= GIUINT_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static int __init vr41xx_giu_add(void)
{
	struct platform_device *pdev;
	struct resource *res;
	unsigned int num;
	int retval;

	pdev = platform_device_alloc("GIU", -1);
	if (!pdev)
		return -ENOMEM;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		pdev->id = GPIO_50PINS_PULLUPDOWN;
		res = giu_50pins_pullupdown_resource;
		num = ARRAY_SIZE(giu_50pins_pullupdown_resource);
		break;
	case CPU_VR4122:
	case CPU_VR4131:
		pdev->id = GPIO_36PINS;
		res = giu_36pins_resource;
		num = ARRAY_SIZE(giu_36pins_resource);
		break;
	case CPU_VR4133:
		pdev->id = GPIO_48PINS_EDGE_SELECT;
		res = giu_48pins_resource;
		num = ARRAY_SIZE(giu_48pins_resource);
		break;
	default:
		retval = -ENODEV;
		goto err_free_device;
	}

	retval = platform_device_add_resources(pdev, res, num);
	if (retval)
		goto err_free_device;

	retval = platform_device_add(pdev);
	if (retval)
		goto err_free_device;

	return 0;

err_free_device:
	platform_device_put(pdev);

	return retval;
}
device_initcall(vr41xx_giu_add);
