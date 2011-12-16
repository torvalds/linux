/*
 * Platform device support for Au1x00 SoCs.
 *
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * (C) Copyright Embedded Alley Solutions, Inc 2005
 * Author: Pantelis Antoniou <pantelis@embeddedalley.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/slab.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1100_mmc.h>
#include <asm/mach-au1x00/au1xxx_eth.h>

#include <prom.h>

static void alchemy_8250_pm(struct uart_port *port, unsigned int state,
			    unsigned int old_state)
{
#ifdef CONFIG_SERIAL_8250
	switch (state) {
	case 0:
		alchemy_uart_enable(CPHYSADDR(port->membase));
		serial8250_do_pm(port, state, old_state);
		break;
	case 3:		/* power off */
		serial8250_do_pm(port, state, old_state);
		alchemy_uart_disable(CPHYSADDR(port->membase));
		break;
	default:
		serial8250_do_pm(port, state, old_state);
		break;
	}
#endif
}

#define PORT(_base, _irq)					\
	{							\
		.mapbase	= _base,			\
		.irq		= _irq,				\
		.regshift	= 2,				\
		.iotype		= UPIO_AU,			\
		.flags		= UPF_SKIP_TEST | UPF_IOREMAP |	\
				  UPF_FIXED_TYPE,		\
		.type		= PORT_16550A,			\
		.pm		= alchemy_8250_pm,		\
	}

static struct plat_serial8250_port au1x00_uart_data[][4] __initdata = {
	[ALCHEMY_CPU_AU1000] = {
		PORT(AU1000_UART0_PHYS_ADDR, AU1000_UART0_INT),
		PORT(AU1000_UART1_PHYS_ADDR, AU1000_UART1_INT),
		PORT(AU1000_UART2_PHYS_ADDR, AU1000_UART2_INT),
		PORT(AU1000_UART3_PHYS_ADDR, AU1000_UART3_INT),
	},
	[ALCHEMY_CPU_AU1500] = {
		PORT(AU1000_UART0_PHYS_ADDR, AU1500_UART0_INT),
		PORT(AU1000_UART3_PHYS_ADDR, AU1500_UART3_INT),
	},
	[ALCHEMY_CPU_AU1100] = {
		PORT(AU1000_UART0_PHYS_ADDR, AU1100_UART0_INT),
		PORT(AU1000_UART1_PHYS_ADDR, AU1100_UART1_INT),
		PORT(AU1000_UART3_PHYS_ADDR, AU1100_UART3_INT),
	},
	[ALCHEMY_CPU_AU1550] = {
		PORT(AU1000_UART0_PHYS_ADDR, AU1550_UART0_INT),
		PORT(AU1000_UART1_PHYS_ADDR, AU1550_UART1_INT),
		PORT(AU1000_UART3_PHYS_ADDR, AU1550_UART3_INT),
	},
	[ALCHEMY_CPU_AU1200] = {
		PORT(AU1000_UART0_PHYS_ADDR, AU1200_UART0_INT),
		PORT(AU1000_UART1_PHYS_ADDR, AU1200_UART1_INT),
	},
};

static struct platform_device au1xx0_uart_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_AU1X00,
};

static void __init alchemy_setup_uarts(int ctype)
{
	unsigned int uartclk = get_au1x00_uart_baud_base() * 16;
	int s = sizeof(struct plat_serial8250_port);
	int c = alchemy_get_uarts(ctype);
	struct plat_serial8250_port *ports;

	ports = kzalloc(s * (c + 1), GFP_KERNEL);
	if (!ports) {
		printk(KERN_INFO "Alchemy: no memory for UART data\n");
		return;
	}
	memcpy(ports, au1x00_uart_data[ctype], s * c);
	au1xx0_uart_device.dev.platform_data = ports;

	/* Fill up uartclk. */
	for (s = 0; s < c; s++)
		ports[s].uartclk = uartclk;
	if (platform_device_register(&au1xx0_uart_device))
		printk(KERN_INFO "Alchemy: failed to register UARTs\n");
}


/* The dmamask must be set for OHCI/EHCI to work */
static u64 alchemy_ohci_dmamask = DMA_BIT_MASK(32);
static u64 __maybe_unused alchemy_ehci_dmamask = DMA_BIT_MASK(32);

static unsigned long alchemy_ohci_data[][2] __initdata = {
	[ALCHEMY_CPU_AU1000] = { AU1000_USB_OHCI_PHYS_ADDR, AU1000_USB_HOST_INT },
	[ALCHEMY_CPU_AU1500] = { AU1000_USB_OHCI_PHYS_ADDR, AU1500_USB_HOST_INT },
	[ALCHEMY_CPU_AU1100] = { AU1000_USB_OHCI_PHYS_ADDR, AU1100_USB_HOST_INT },
	[ALCHEMY_CPU_AU1550] = { AU1550_USB_OHCI_PHYS_ADDR, AU1550_USB_HOST_INT },
	[ALCHEMY_CPU_AU1200] = { AU1200_USB_OHCI_PHYS_ADDR, AU1200_USB_INT },
};

static unsigned long alchemy_ehci_data[][2] __initdata = {
	[ALCHEMY_CPU_AU1200] = { AU1200_USB_EHCI_PHYS_ADDR, AU1200_USB_INT },
};

static int __init _new_usbres(struct resource **r, struct platform_device **d)
{
	*r = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!*r)
		return -ENOMEM;
	*d = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!*d) {
		kfree(*r);
		return -ENOMEM;
	}

	(*d)->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	(*d)->num_resources = 2;
	(*d)->resource = *r;

	return 0;
}

static void __init alchemy_setup_usb(int ctype)
{
	struct resource *res;
	struct platform_device *pdev;

	/* setup OHCI0.  Every variant has one */
	if (_new_usbres(&res, &pdev))
		return;

	res[0].start = alchemy_ohci_data[ctype][0];
	res[0].end = res[0].start + 0x100 - 1;
	res[0].flags = IORESOURCE_MEM;
	res[1].start = alchemy_ohci_data[ctype][1];
	res[1].end = res[1].start;
	res[1].flags = IORESOURCE_IRQ;
	pdev->name = "au1xxx-ohci";
	pdev->id = 0;
	pdev->dev.dma_mask = &alchemy_ohci_dmamask;

	if (platform_device_register(pdev))
		printk(KERN_INFO "Alchemy USB: cannot add OHCI0\n");


	/* setup EHCI0: Au1200 */
	if (ctype == ALCHEMY_CPU_AU1200) {
		if (_new_usbres(&res, &pdev))
			return;

		res[0].start = alchemy_ehci_data[ctype][0];
		res[0].end = res[0].start + 0x100 - 1;
		res[0].flags = IORESOURCE_MEM;
		res[1].start = alchemy_ehci_data[ctype][1];
		res[1].end = res[1].start;
		res[1].flags = IORESOURCE_IRQ;
		pdev->name = "au1xxx-ehci";
		pdev->id = 0;
		pdev->dev.dma_mask = &alchemy_ehci_dmamask;

		if (platform_device_register(pdev))
			printk(KERN_INFO "Alchemy USB: cannot add EHCI0\n");
	}
}

/* Macro to help defining the Ethernet MAC resources */
#define MAC_RES_COUNT	4	/* MAC regs, MAC en, MAC INT, MACDMA regs */
#define MAC_RES(_base, _enable, _irq, _macdma)		\
	{						\
		.start	= _base,			\
		.end	= _base + 0xffff,		\
		.flags	= IORESOURCE_MEM,		\
	},						\
	{						\
		.start	= _enable,			\
		.end	= _enable + 0x3,		\
		.flags	= IORESOURCE_MEM,		\
	},						\
	{						\
		.start	= _irq,				\
		.end	= _irq,				\
		.flags	= IORESOURCE_IRQ		\
	},						\
	{						\
		.start	= _macdma,			\
		.end	= _macdma + 0x1ff,		\
		.flags	= IORESOURCE_MEM,		\
	}

static struct resource au1xxx_eth0_resources[][MAC_RES_COUNT] __initdata = {
	[ALCHEMY_CPU_AU1000] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1000_MAC0_DMA_INT,
			AU1000_MACDMA0_PHYS_ADDR)
	},
	[ALCHEMY_CPU_AU1500] = {
		MAC_RES(AU1500_MAC0_PHYS_ADDR,
			AU1500_MACEN_PHYS_ADDR,
			AU1500_MAC0_DMA_INT,
			AU1000_MACDMA0_PHYS_ADDR)
	},
	[ALCHEMY_CPU_AU1100] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1100_MAC0_DMA_INT,
			AU1000_MACDMA0_PHYS_ADDR)
	},
	[ALCHEMY_CPU_AU1550] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1550_MAC0_DMA_INT,
			AU1000_MACDMA0_PHYS_ADDR)
	},
};

static struct au1000_eth_platform_data au1xxx_eth0_platform_data = {
	.phy1_search_mac0 = 1,
};

static struct platform_device au1xxx_eth0_device = {
	.name		= "au1000-eth",
	.id		= 0,
	.num_resources	= MAC_RES_COUNT,
	.dev.platform_data = &au1xxx_eth0_platform_data,
};

static struct resource au1xxx_eth1_resources[][MAC_RES_COUNT] __initdata = {
	[ALCHEMY_CPU_AU1000] = {
		MAC_RES(AU1000_MAC1_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR + 4,
			AU1000_MAC1_DMA_INT,
			AU1000_MACDMA1_PHYS_ADDR)
	},
	[ALCHEMY_CPU_AU1500] = {
		MAC_RES(AU1500_MAC1_PHYS_ADDR,
			AU1500_MACEN_PHYS_ADDR + 4,
			AU1500_MAC1_DMA_INT,
			AU1000_MACDMA1_PHYS_ADDR)
	},
	[ALCHEMY_CPU_AU1550] = {
		MAC_RES(AU1000_MAC1_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR + 4,
			AU1550_MAC1_DMA_INT,
			AU1000_MACDMA1_PHYS_ADDR)
	},
};

static struct au1000_eth_platform_data au1xxx_eth1_platform_data = {
	.phy1_search_mac0 = 1,
};

static struct platform_device au1xxx_eth1_device = {
	.name		= "au1000-eth",
	.id		= 1,
	.num_resources	= MAC_RES_COUNT,
	.dev.platform_data = &au1xxx_eth1_platform_data,
};

void __init au1xxx_override_eth_cfg(unsigned int port,
			struct au1000_eth_platform_data *eth_data)
{
	if (!eth_data || port > 1)
		return;

	if (port == 0)
		memcpy(&au1xxx_eth0_platform_data, eth_data,
			sizeof(struct au1000_eth_platform_data));
	else
		memcpy(&au1xxx_eth1_platform_data, eth_data,
			sizeof(struct au1000_eth_platform_data));
}

static void __init alchemy_setup_macs(int ctype)
{
	int ret, i;
	unsigned char ethaddr[6];
	struct resource *macres;

	/* Handle 1st MAC */
	if (alchemy_get_macs(ctype) < 1)
		return;

	macres = kmalloc(sizeof(struct resource) * MAC_RES_COUNT, GFP_KERNEL);
	if (!macres) {
		printk(KERN_INFO "Alchemy: no memory for MAC0 resources\n");
		return;
	}
	memcpy(macres, au1xxx_eth0_resources[ctype],
	       sizeof(struct resource) * MAC_RES_COUNT);
	au1xxx_eth0_device.resource = macres;

	i = prom_get_ethernet_addr(ethaddr);
	if (!i && !is_valid_ether_addr(au1xxx_eth0_platform_data.mac))
		memcpy(au1xxx_eth0_platform_data.mac, ethaddr, 6);

	ret = platform_device_register(&au1xxx_eth0_device);
	if (ret)
		printk(KERN_INFO "Alchemy: failed to register MAC0\n");


	/* Handle 2nd MAC */
	if (alchemy_get_macs(ctype) < 2)
		return;

	macres = kmalloc(sizeof(struct resource) * MAC_RES_COUNT, GFP_KERNEL);
	if (!macres) {
		printk(KERN_INFO "Alchemy: no memory for MAC1 resources\n");
		return;
	}
	memcpy(macres, au1xxx_eth1_resources[ctype],
	       sizeof(struct resource) * MAC_RES_COUNT);
	au1xxx_eth1_device.resource = macres;

	ethaddr[5] += 1;	/* next addr for 2nd MAC */
	if (!i && !is_valid_ether_addr(au1xxx_eth1_platform_data.mac))
		memcpy(au1xxx_eth1_platform_data.mac, ethaddr, 6);

	/* Register second MAC if enabled in pinfunc */
	if (!(au_readl(SYS_PINFUNC) & (u32)SYS_PF_NI2)) {
		ret = platform_device_register(&au1xxx_eth1_device);
		if (ret)
			printk(KERN_INFO "Alchemy: failed to register MAC1\n");
	}
}

static int __init au1xxx_platform_init(void)
{
	int ctype = alchemy_get_cputype();

	alchemy_setup_uarts(ctype);
	alchemy_setup_macs(ctype);
	alchemy_setup_usb(ctype);

	return 0;
}

arch_initcall(au1xxx_platform_init);
