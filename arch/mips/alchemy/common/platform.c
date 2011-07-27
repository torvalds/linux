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

#include <asm/mach-au1x00/au1xxx.h>
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

/* OHCI (USB full speed host controller) */
static struct resource au1xxx_usb_ohci_resources[] = {
	[0] = {
		.start		= USB_OHCI_BASE,
		.end		= USB_OHCI_BASE + USB_OHCI_LEN - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= FOR_PLATFORM_C_USB_HOST_INT,
		.end		= FOR_PLATFORM_C_USB_HOST_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1xxx_usb_ohci_device = {
	.name		= "au1xxx-ohci",
	.id		= 0,
	.dev = {
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1xxx_usb_ohci_resources),
	.resource	= au1xxx_usb_ohci_resources,
};

/*** AU1100 LCD controller ***/

#ifdef CONFIG_FB_AU1100
static struct resource au1100_lcd_resources[] = {
	[0] = {
		.start          = LCD_PHYS_ADDR,
		.end            = LCD_PHYS_ADDR + 0x800 - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = AU1100_LCD_INT,
		.end            = AU1100_LCD_INT,
		.flags          = IORESOURCE_IRQ,
	}
};

static u64 au1100_lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1100_lcd_device = {
	.name           = "au1100-lcd",
	.id             = 0,
	.dev = {
		.dma_mask               = &au1100_lcd_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.num_resources  = ARRAY_SIZE(au1100_lcd_resources),
	.resource       = au1100_lcd_resources,
};
#endif

#ifdef CONFIG_SOC_AU1200
/* EHCI (USB high speed host controller) */
static struct resource au1xxx_usb_ehci_resources[] = {
	[0] = {
		.start		= USB_EHCI_BASE,
		.end		= USB_EHCI_BASE + USB_EHCI_LEN - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1200_USB_INT,
		.end		= AU1200_USB_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 ehci_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1xxx_usb_ehci_device = {
	.name		= "au1xxx-ehci",
	.id		= 0,
	.dev = {
		.dma_mask		= &ehci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1xxx_usb_ehci_resources),
	.resource	= au1xxx_usb_ehci_resources,
};

/* Au1200 UDC (USB gadget controller) */
static struct resource au1xxx_usb_gdt_resources[] = {
	[0] = {
		.start		= USB_UDC_BASE,
		.end		= USB_UDC_BASE + USB_UDC_LEN - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1200_USB_INT,
		.end		= AU1200_USB_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 udc_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1xxx_usb_gdt_device = {
	.name		= "au1xxx-udc",
	.id		= 0,
	.dev = {
		.dma_mask		= &udc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1xxx_usb_gdt_resources),
	.resource	= au1xxx_usb_gdt_resources,
};

/* Au1200 UOC (USB OTG controller) */
static struct resource au1xxx_usb_otg_resources[] = {
	[0] = {
		.start		= USB_UOC_BASE,
		.end		= USB_UOC_BASE + USB_UOC_LEN - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1200_USB_INT,
		.end		= AU1200_USB_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 uoc_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1xxx_usb_otg_device = {
	.name		= "au1xxx-uoc",
	.id		= 0,
	.dev = {
		.dma_mask		= &uoc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1xxx_usb_otg_resources),
	.resource	= au1xxx_usb_otg_resources,
};

static struct resource au1200_lcd_resources[] = {
	[0] = {
		.start          = LCD_PHYS_ADDR,
		.end            = LCD_PHYS_ADDR + 0x800 - 1,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start          = AU1200_LCD_INT,
		.end            = AU1200_LCD_INT,
		.flags          = IORESOURCE_IRQ,
	}
};

static u64 au1200_lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1200_lcd_device = {
	.name           = "au1200-lcd",
	.id             = 0,
	.dev = {
		.dma_mask               = &au1200_lcd_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.num_resources  = ARRAY_SIZE(au1200_lcd_resources),
	.resource       = au1200_lcd_resources,
};

static u64 au1xxx_mmc_dmamask =  DMA_BIT_MASK(32);

extern struct au1xmmc_platform_data au1xmmc_platdata[2];

static struct resource au1200_mmc0_resources[] = {
	[0] = {
		.start          = AU1100_SD0_PHYS_ADDR,
		.end            = AU1100_SD0_PHYS_ADDR + 0xfff,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1200_SD_INT,
		.end		= AU1200_SD_INT,
		.flags		= IORESOURCE_IRQ,
	},
	[2] = {
		.start		= DSCR_CMD0_SDMS_TX0,
		.end		= DSCR_CMD0_SDMS_TX0,
		.flags		= IORESOURCE_DMA,
	},
	[3] = {
		.start          = DSCR_CMD0_SDMS_RX0,
		.end		= DSCR_CMD0_SDMS_RX0,
		.flags          = IORESOURCE_DMA,
	}
};

static struct platform_device au1200_mmc0_device = {
	.name = "au1xxx-mmc",
	.id = 0,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &au1xmmc_platdata[0],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc0_resources),
	.resource	= au1200_mmc0_resources,
};

#ifndef CONFIG_MIPS_DB1200
static struct resource au1200_mmc1_resources[] = {
	[0] = {
		.start          = AU1100_SD1_PHYS_ADDR,
		.end            = AU1100_SD1_PHYS_ADDR + 0xfff,
		.flags          = IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1200_SD_INT,
		.end		= AU1200_SD_INT,
		.flags		= IORESOURCE_IRQ,
	},
	[2] = {
		.start		= DSCR_CMD0_SDMS_TX1,
		.end		= DSCR_CMD0_SDMS_TX1,
		.flags		= IORESOURCE_DMA,
	},
	[3] = {
		.start          = DSCR_CMD0_SDMS_RX1,
		.end		= DSCR_CMD0_SDMS_RX1,
		.flags          = IORESOURCE_DMA,
	}
};

static struct platform_device au1200_mmc1_device = {
	.name = "au1xxx-mmc",
	.id = 1,
	.dev = {
		.dma_mask		= &au1xxx_mmc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &au1xmmc_platdata[1],
	},
	.num_resources	= ARRAY_SIZE(au1200_mmc1_resources),
	.resource	= au1200_mmc1_resources,
};
#endif /* #ifndef CONFIG_MIPS_DB1200 */
#endif /* #ifdef CONFIG_SOC_AU1200 */

/* All Alchemy demoboards with I2C have this #define in their headers */
#ifdef SMBUS_PSC_BASE
static struct resource pbdb_smbus_resources[] = {
	{
		.start	= CPHYSADDR(SMBUS_PSC_BASE),
		.end	= CPHYSADDR(SMBUS_PSC_BASE + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device pbdb_smbus_device = {
	.name		= "au1xpsc_smbus",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(pbdb_smbus_resources),
	.resource	= pbdb_smbus_resources,
};
#endif

/* Macro to help defining the Ethernet MAC resources */
#define MAC_RES_COUNT	3	/* MAC regs base, MAC enable reg, MAC INT */
#define MAC_RES(_base, _enable, _irq)			\
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
	}

static struct resource au1xxx_eth0_resources[][MAC_RES_COUNT] __initdata = {
	[ALCHEMY_CPU_AU1000] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1000_MAC0_DMA_INT)
	},
	[ALCHEMY_CPU_AU1500] = {
		MAC_RES(AU1500_MAC0_PHYS_ADDR,
			AU1500_MACEN_PHYS_ADDR,
			AU1500_MAC0_DMA_INT)
	},
	[ALCHEMY_CPU_AU1100] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1100_MAC0_DMA_INT)
	},
	[ALCHEMY_CPU_AU1550] = {
		MAC_RES(AU1000_MAC0_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR,
			AU1550_MAC0_DMA_INT)
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
			AU1000_MAC1_DMA_INT)
	},
	[ALCHEMY_CPU_AU1500] = {
		MAC_RES(AU1500_MAC1_PHYS_ADDR,
			AU1500_MACEN_PHYS_ADDR + 4,
			AU1500_MAC1_DMA_INT)
	},
	[ALCHEMY_CPU_AU1550] = {
		MAC_RES(AU1000_MAC1_PHYS_ADDR,
			AU1000_MACEN_PHYS_ADDR + 4,
			AU1550_MAC1_DMA_INT)
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
	if (!ret)
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

static struct platform_device *au1xxx_platform_devices[] __initdata = {
	&au1xxx_usb_ohci_device,
#ifdef CONFIG_FB_AU1100
	&au1100_lcd_device,
#endif
#ifdef CONFIG_SOC_AU1200
	&au1xxx_usb_ehci_device,
	&au1xxx_usb_gdt_device,
	&au1xxx_usb_otg_device,
	&au1200_lcd_device,
	&au1200_mmc0_device,
#ifndef CONFIG_MIPS_DB1200
	&au1200_mmc1_device,
#endif
#endif
#ifdef SMBUS_PSC_BASE
	&pbdb_smbus_device,
#endif
};

static int __init au1xxx_platform_init(void)
{
	int err, ctype = alchemy_get_cputype();

	alchemy_setup_uarts(ctype);
	alchemy_setup_macs(ctype);

	err = platform_add_devices(au1xxx_platform_devices,
				   ARRAY_SIZE(au1xxx_platform_devices));
	return err;
}

arch_initcall(au1xxx_platform_init);
