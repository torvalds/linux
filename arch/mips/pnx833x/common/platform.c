/*
 *  platform.c: platform support for PNX833X.
 *
 *  Copyright 2008 NXP Semiconductors
 *	  Chris Steel <chris.steel@nxp.com>
 *    Daniel Laird <daniel.j.laird@nxp.com>
 *
 *  Based on software written by:
 *	Nikita Youshchenko <yoush@debian.org>, based on PNX8550 code.
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial.h>
#include <linux/serial_pnx8xxx.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>

#include <irq.h>
#include <irq-mapping.h>
#include <pnx833x.h>

static u64 uart_dmamask	    = DMA_BIT_MASK(32);

static struct resource pnx833x_uart_resources[] = {
	[0] = {
		.start		= PNX833X_UART0_PORTS_START,
		.end		= PNX833X_UART0_PORTS_END,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= PNX833X_PIC_UART0_INT,
		.end		= PNX833X_PIC_UART0_INT,
		.flags		= IORESOURCE_IRQ,
	},
	[2] = {
		.start		= PNX833X_UART1_PORTS_START,
		.end		= PNX833X_UART1_PORTS_END,
		.flags		= IORESOURCE_MEM,
	},
	[3] = {
		.start		= PNX833X_PIC_UART1_INT,
		.end		= PNX833X_PIC_UART1_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

struct pnx8xxx_port pnx8xxx_ports[] = {
	[0] = {
		.port	= {
			.type		= PORT_PNX8XXX,
			.iotype		= UPIO_MEM,
			.membase	= (void __iomem *)PNX833X_UART0_PORTS_START,
			.mapbase	= PNX833X_UART0_PORTS_START,
			.irq		= PNX833X_PIC_UART0_INT,
			.uartclk	= 3692300,
			.fifosize	= 16,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 0,
		},
	},
	[1] = {
		.port	= {
			.type		= PORT_PNX8XXX,
			.iotype		= UPIO_MEM,
			.membase	= (void __iomem *)PNX833X_UART1_PORTS_START,
			.mapbase	= PNX833X_UART1_PORTS_START,
			.irq		= PNX833X_PIC_UART1_INT,
			.uartclk	= 3692300,
			.fifosize	= 16,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 1,
		},
	},
};

static struct platform_device pnx833x_uart_device = {
	.name		= "pnx8xxx-uart",
	.id		= -1,
	.dev = {
		.dma_mask		= &uart_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= pnx8xxx_ports,
	},
	.num_resources	= ARRAY_SIZE(pnx833x_uart_resources),
	.resource	= pnx833x_uart_resources,
};

static u64 ehci_dmamask	    = DMA_BIT_MASK(32);

static struct resource pnx833x_usb_ehci_resources[] = {
	[0] = {
		.start		= PNX833X_USB_PORTS_START,
		.end		= PNX833X_USB_PORTS_END,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= PNX833X_PIC_USB_INT,
		.end		= PNX833X_PIC_USB_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device pnx833x_usb_ehci_device = {
	.name		= "pnx833x-ehci",
	.id		= -1,
	.dev = {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(pnx833x_usb_ehci_resources),
	.resource	= pnx833x_usb_ehci_resources,
};

static u64 ethernet_dmamask = DMA_BIT_MASK(32);

static struct resource pnx833x_ethernet_resources[] = {
	[0] = {
		.start = PNX8335_IP3902_PORTS_START,
		.end   = PNX8335_IP3902_PORTS_END,
		.flags = IORESOURCE_MEM,
	},
#ifdef CONFIG_SOC_PNX8335
	[1] = {
		.start = PNX8335_PIC_ETHERNET_INT,
		.end   = PNX8335_PIC_ETHERNET_INT,
		.flags = IORESOURCE_IRQ,
	},
#endif
};

static struct platform_device pnx833x_ethernet_device = {
	.name = "ip3902-eth",
	.id   = -1,
	.dev  = {
		.dma_mask	   = &ethernet_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = ARRAY_SIZE(pnx833x_ethernet_resources),
	.resource      = pnx833x_ethernet_resources,
};

static struct resource pnx833x_sata_resources[] = {
	[0] = {
		.start = PNX8335_SATA_PORTS_START,
		.end   = PNX8335_SATA_PORTS_END,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PNX8335_PIC_SATA_INT,
		.end   = PNX8335_PIC_SATA_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device pnx833x_sata_device = {
	.name	       = "pnx833x-sata",
	.id	       = -1,
	.num_resources = ARRAY_SIZE(pnx833x_sata_resources),
	.resource      = pnx833x_sata_resources,
};

static void
pnx833x_flash_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd_to_nand(mtd);
	unsigned long nandaddr = (unsigned long)this->IO_ADDR_W;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, (void __iomem *)(nandaddr + PNX8335_NAND_CLE_MASK));
	else
		writeb(cmd, (void __iomem *)(nandaddr + PNX8335_NAND_ALE_MASK));
}

static struct platform_nand_data pnx833x_flash_nand_data = {
	.chip = {
		.nr_chips		= 1,
		.chip_delay		= 25,
	},
	.ctrl = {
		.cmd_ctrl		= pnx833x_flash_nand_cmd_ctrl
	}
};

/*
 * Set start to be the correct address (PNX8335_NAND_BASE with no 0xb!!),
 * 12 bytes more seems to be the standard that allows for NAND access.
 */
static struct resource pnx833x_flash_nand_resource = {
	.start	= PNX8335_NAND_BASE,
	.end	= PNX8335_NAND_BASE + 12,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device pnx833x_flash_nand = {
	.name		= "gen_nand",
	.id			= -1,
	.num_resources	= 1,
	.resource	    = &pnx833x_flash_nand_resource,
	.dev		= {
		.platform_data = &pnx833x_flash_nand_data,
	},
};

static struct platform_device *pnx833x_platform_devices[] __initdata = {
	&pnx833x_uart_device,
	&pnx833x_usb_ehci_device,
	&pnx833x_ethernet_device,
	&pnx833x_sata_device,
	&pnx833x_flash_nand,
};

static int __init pnx833x_platform_init(void)
{
	return platform_add_devices(pnx833x_platform_devices,
				    ARRAY_SIZE(pnx833x_platform_devices));
}

arch_initcall(pnx833x_platform_init);
