/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/setup.h>
#include <mach/irqs.h>

static struct map_desc u5500_io_desc[] __initdata = {
	__IO_DEV_DESC(U5500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO3_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_GPIO4_BASE, SZ_4K),
	__IO_DEV_DESC(U5500_PRCMU_BASE, SZ_4K),
};

static struct resource mbox0_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX0_PEER_START,
		.end = U5500_MBOX0_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX0_LOCAL_START,
		.end = U5500_MBOX0_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR0_VIRT_IRQ,
		.end = MBOX_PAIR0_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource mbox1_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX1_PEER_START,
		.end = U5500_MBOX1_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX1_LOCAL_START,
		.end = U5500_MBOX1_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR1_VIRT_IRQ,
		.end = MBOX_PAIR1_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource mbox2_resources[] = {
	{
		.name = "mbox_peer",
		.start = U5500_MBOX2_PEER_START,
		.end = U5500_MBOX2_PEER_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_local",
		.start = U5500_MBOX2_LOCAL_START,
		.end = U5500_MBOX2_LOCAL_END,
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "mbox_irq",
		.start = MBOX_PAIR2_VIRT_IRQ,
		.end = MBOX_PAIR2_VIRT_IRQ,
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device mbox0_device = {
	.id = 0,
	.name = "mbox",
	.resource = mbox0_resources,
	.num_resources = ARRAY_SIZE(mbox0_resources),
};

static struct platform_device mbox1_device = {
	.id = 1,
	.name = "mbox",
	.resource = mbox1_resources,
	.num_resources = ARRAY_SIZE(mbox1_resources),
};

static struct platform_device mbox2_device = {
	.id = 2,
	.name = "mbox",
	.resource = mbox2_resources,
	.num_resources = ARRAY_SIZE(mbox2_resources),
};

static struct platform_device *u5500_platform_devs[] __initdata = {
	&u5500_gpio_devs[0],
	&u5500_gpio_devs[1],
	&u5500_gpio_devs[2],
	&u5500_gpio_devs[3],
	&u5500_gpio_devs[4],
	&u5500_gpio_devs[5],
	&u5500_gpio_devs[6],
	&u5500_gpio_devs[7],
	&mbox0_device,
	&mbox1_device,
	&mbox2_device,
};

void __init u5500_map_io(void)
{
	ux500_map_io();

	iotable_init(u5500_io_desc, ARRAY_SIZE(u5500_io_desc));
}

void __init u5500_init_devices(void)
{
	ux500_init_devices();

	platform_add_devices(u5500_platform_devs,
			     ARRAY_SIZE(u5500_platform_devs));
}
