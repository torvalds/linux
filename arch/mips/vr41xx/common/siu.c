/*
 *  NEC VR4100 series SIU platform device.
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
#include <linux/serial_core.h>

#include <asm/cpu.h>
#include <asm/vr41xx/siu.h>

static unsigned int siu_type1_ports[SIU_PORTS_MAX] __initdata = {
	PORT_VR41XX_SIU,
	PORT_UNKNOWN,
};

static struct resource siu_type1_resource[] __initdata = {
	{
		.start	= 0x0c000000,
		.end	= 0x0c00000a,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= SIU_IRQ,
		.end	= SIU_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static unsigned int siu_type2_ports[SIU_PORTS_MAX] __initdata = {
	PORT_VR41XX_SIU,
	PORT_VR41XX_DSIU,
};

static struct resource siu_type2_resource[] __initdata = {
	{
		.start	= 0x0f000800,
		.end	= 0x0f00080a,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x0f000820,
		.end	= 0x0f000829,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= SIU_IRQ,
		.end	= SIU_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= DSIU_IRQ,
		.end	= DSIU_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static int __init vr41xx_siu_add(void)
{
	struct platform_device *pdev;
	struct resource *res;
	unsigned int num;
	int retval;

	pdev = platform_device_alloc("SIU", -1);
	if (!pdev)
		return -ENOMEM;

	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121:
		pdev->dev.platform_data = siu_type1_ports;
		res = siu_type1_resource;
		num = ARRAY_SIZE(siu_type1_resource);
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		pdev->dev.platform_data = siu_type2_ports;
		res = siu_type2_resource;
		num = ARRAY_SIZE(siu_type2_resource);
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
device_initcall(vr41xx_siu_add);
