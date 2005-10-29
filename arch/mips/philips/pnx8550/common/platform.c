/*
 * Platform device support for Philips PNX8550 SoCs
 *
 * Copyright 2005, Embedded Alley Solutions, Inc
 *
 * Based on arch/mips/au1000/common/platform.c
 * Platform device support for Au1x00 SoCs.
 *
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial.h>
#include <linux/serial_ip3106.h>

#include <int.h>
#include <usb.h>
#include <uart.h>

extern struct uart_ops ip3106_pops;

static struct resource pnx8550_usb_ohci_resources[] = {
	[0] = {
		.start		= PNX8550_USB_OHCI_OP_BASE,
		.end		= PNX8550_USB_OHCI_OP_BASE +
				  PNX8550_USB_OHCI_OP_LEN,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= PNX8550_INT_USB,
		.end		= PNX8550_INT_USB,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct resource pnx8550_uart_resources[] = {
	[0] = {
		.start		= PNX8550_UART_PORT0,
		.end		= PNX8550_UART_PORT0 + 0xfff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= PNX8550_UART_INT(0),
		.end		= PNX8550_UART_INT(0),
		.flags		= IORESOURCE_IRQ,
	},
	[2] = {
		.start		= PNX8550_UART_PORT1,
		.end		= PNX8550_UART_PORT1 + 0xfff,
		.flags		= IORESOURCE_MEM,
	},
	[3] = {
		.start		= PNX8550_UART_INT(1),
		.end		= PNX8550_UART_INT(1),
		.flags		= IORESOURCE_IRQ,
	},
};

struct ip3106_port ip3106_ports[] = {
	[0] = {
		.port   = {
			.type		= PORT_IP3106,
			.iotype		= SERIAL_IO_MEM,
			.membase	= (void __iomem *)PNX8550_UART_PORT0,
			.mapbase	= PNX8550_UART_PORT0,
			.irq		= PNX8550_UART_INT(0),
			.uartclk	= 3692300,
			.fifosize	= 16,
			.ops		= &ip3106_pops,
			.flags		= ASYNC_BOOT_AUTOCONF,
			.line		= 0,
		},
	},
	[1] = {
		.port   = {
			.type		= PORT_IP3106,
			.iotype		= SERIAL_IO_MEM,
			.membase	= (void __iomem *)PNX8550_UART_PORT1,
			.mapbase	= PNX8550_UART_PORT1,
			.irq		= PNX8550_UART_INT(1),
			.uartclk	= 3692300,
			.fifosize	= 16,
			.ops		= &ip3106_pops,
			.flags		= ASYNC_BOOT_AUTOCONF,
			.line		= 1,
		},
	},
};

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = ~(u32)0;

static u64 uart_dmamask = ~(u32)0;

static struct platform_device pnx8550_usb_ohci_device = {
	.name		= "pnx8550-ohci",
	.id		= -1,
	.dev = {
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(pnx8550_usb_ohci_resources),
	.resource	= pnx8550_usb_ohci_resources,
};

static struct platform_device pnx8550_uart_device = {
	.name		= "ip3106-uart",
	.id		= -1,
	.dev = {
		.dma_mask		= &uart_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data = ip3106_ports,
	},
	.num_resources	= ARRAY_SIZE(pnx8550_uart_resources),
	.resource	= pnx8550_uart_resources,
};

static struct platform_device *pnx8550_platform_devices[] __initdata = {
	&pnx8550_usb_ohci_device,
	&pnx8550_uart_device,
};

int pnx8550_platform_init(void)
{
	return platform_add_devices(pnx8550_platform_devices,
			            ARRAY_SIZE(pnx8550_platform_devices));
}

arch_initcall(pnx8550_platform_init);
