/*
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

#include <asm/mach-au1x00/au1000.h>

static struct resource au1xxx_usb_ohci_resources[] = {
	[0] = {
		.start		= USB_OHCI_BASE,
		.end		= USB_OHCI_BASE + USB_OHCI_LEN,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= AU1000_USB_HOST_INT,
		.end		= AU1000_USB_HOST_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

/* The dmamask must be set for OHCI to work */
static u64 ohci_dmamask = ~(u32)0;

static struct platform_device au1xxx_usb_ohci_device = {
	.name		= "au1xxx-ohci",
	.id		= 0,
	.dev = {
		.dma_mask		= &ohci_dmamask,
		.coherent_dma_mask	= 0xffffffff,
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

static u64 au1100_lcd_dmamask = ~(u32)0;

static struct platform_device au1100_lcd_device = {
	.name           = "au1100-lcd",
	.id             = 0,
	.dev = {
		.dma_mask               = &au1100_lcd_dmamask,
		.coherent_dma_mask      = 0xffffffff,
	},
	.num_resources  = ARRAY_SIZE(au1100_lcd_resources),
	.resource       = au1100_lcd_resources,
};
#endif


static struct platform_device *au1xxx_platform_devices[] __initdata = {
	&au1xxx_usb_ohci_device,
#ifdef CONFIG_FB_AU1100
	&au1100_lcd_device,
#endif
};

int au1xxx_platform_init(void)
{
	return platform_add_devices(au1xxx_platform_devices, ARRAY_SIZE(au1xxx_platform_devices));
}

arch_initcall(au1xxx_platform_init);
