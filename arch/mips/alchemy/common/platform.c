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
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/init.h>

#include <asm/mach-au1x00/au1xxx.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1100_mmc.h>

#define PORT(_base, _irq)					\
	{							\
		.mapbase	= _base,			\
		.irq		= _irq,				\
		.regshift	= 2,				\
		.iotype		= UPIO_AU,			\
		.flags		= UPF_SKIP_TEST | UPF_IOREMAP	\
	}

static struct plat_serial8250_port au1x00_uart_data[] = {
#if defined(CONFIG_SERIAL_8250_AU1X00)
#if defined(CONFIG_SOC_AU1000)
	PORT(UART0_PHYS_ADDR, AU1000_UART0_INT),
	PORT(UART1_PHYS_ADDR, AU1000_UART1_INT),
	PORT(UART2_PHYS_ADDR, AU1000_UART2_INT),
	PORT(UART3_PHYS_ADDR, AU1000_UART3_INT),
#elif defined(CONFIG_SOC_AU1500)
	PORT(UART0_PHYS_ADDR, AU1500_UART0_INT),
	PORT(UART3_PHYS_ADDR, AU1500_UART3_INT),
#elif defined(CONFIG_SOC_AU1100)
	PORT(UART0_PHYS_ADDR, AU1100_UART0_INT),
	PORT(UART1_PHYS_ADDR, AU1100_UART1_INT),
	PORT(UART3_PHYS_ADDR, AU1100_UART3_INT),
#elif defined(CONFIG_SOC_AU1550)
	PORT(UART0_PHYS_ADDR, AU1550_UART0_INT),
	PORT(UART1_PHYS_ADDR, AU1550_UART1_INT),
	PORT(UART3_PHYS_ADDR, AU1550_UART3_INT),
#elif defined(CONFIG_SOC_AU1200)
	PORT(UART0_PHYS_ADDR, AU1200_UART0_INT),
	PORT(UART1_PHYS_ADDR, AU1200_UART1_INT),
#endif
#endif	/* CONFIG_SERIAL_8250_AU1X00 */
	{ },
};

static struct platform_device au1xx0_uart_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_AU1X00,
	.dev			= {
		.platform_data	= au1x00_uart_data,
	},
};

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
		.start          = SD0_PHYS_ADDR,
		.end            = SD0_PHYS_ADDR + 0x7ffff,
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
		.start          = SD1_PHYS_ADDR,
		.end            = SD1_PHYS_ADDR + 0x7ffff,
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

static struct platform_device *au1xxx_platform_devices[] __initdata = {
	&au1xx0_uart_device,
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
	unsigned int uartclk = get_au1x00_uart_baud_base() * 16;
	int i;

	/* Fill up uartclk. */
	for (i = 0; au1x00_uart_data[i].flags; i++)
		au1x00_uart_data[i].uartclk = uartclk;

	return platform_add_devices(au1xxx_platform_devices,
				    ARRAY_SIZE(au1xxx_platform_devices));
}

arch_initcall(au1xxx_platform_init);
