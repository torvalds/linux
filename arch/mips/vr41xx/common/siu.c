// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  NEC VR4100 series SIU platform device.
 *
 *  Copyright (C) 2007-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/irq.h>

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

void __init vr41xx_siu_setup(void)
{
	struct uart_port port;
	struct resource *res;
	unsigned int *type;
	int i;

	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121:
		type = siu_type1_ports;
		res = siu_type1_resource;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		type = siu_type2_ports;
		res = siu_type2_resource;
		break;
	default:
		return;
	}

	for (i = 0; i < SIU_PORTS_MAX; i++) {
		port.line = i;
		port.type = type[i];
		if (port.type == PORT_UNKNOWN)
			break;
		port.mapbase = res[i].start;
		port.membase = (unsigned char __iomem *)KSEG1ADDR(res[i].start);
		vr41xx_siu_early_setup(&port);
	}
}
