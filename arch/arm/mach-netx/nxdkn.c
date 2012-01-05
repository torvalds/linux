/*
 * arch/arm/mach-netx/nxdkn.c
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <asm/hardware/vic.h>
#include <mach/netx-regs.h>
#include <mach/eth.h>

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
	.handle_irq	= vic_handle_irq,
	.timer		= &netx_timer,
	.init_machine	= nxdkn_init,
MACHINE_END
