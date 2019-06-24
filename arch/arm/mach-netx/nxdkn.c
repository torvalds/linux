// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-netx/nxdkn.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mtd/plat-ram.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/netx-regs.h>
#include <linux/platform_data/eth-netx.h>

#include "generic.h"

static struct netxeth_platform_data eth0_platform_data = {
	.xcno = 0,
};

static struct platform_device nxdkn_eth0_device = {
	.name		= "netx-eth",
	.id		= 0,
	.num_resources	= 0,
	.resource	= NULL,
	.dev = {
		.platform_data = &eth0_platform_data,
	}
};

static struct netxeth_platform_data eth1_platform_data = {
	.xcno = 1,
};

static struct platform_device nxdkn_eth1_device = {
	.name		= "netx-eth",
	.id		= 1,
	.num_resources	= 0,
	.resource	= NULL,
	.dev = {
		.platform_data = &eth1_platform_data,
	}
};

static struct resource netx_uart0_resources[] = {
	[0] = {
		.start	= 0x00100A00,
		.end	= 0x00100A3F,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= (NETX_IRQ_UART0),
		.end	= (NETX_IRQ_UART0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device netx_uart0_device = {
	.name		= "netx-uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(netx_uart0_resources),
	.resource	= netx_uart0_resources,
};

static struct platform_device *devices[] __initdata = {
	&nxdkn_eth0_device,
	&nxdkn_eth1_device,
	&netx_uart0_device,
};

static void __init nxdkn_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(NXDKN, "Hilscher nxdkn")
	.atag_offset	= 0x100,
	.map_io		= netx_map_io,
	.init_irq	= netx_init_irq,
	.init_time	= netx_timer_init,
	.init_machine	= nxdkn_init,
	.restart	= netx_restart,
MACHINE_END
