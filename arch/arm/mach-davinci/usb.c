// SPDX-License-Identifier: GPL-2.0
/*
 * USB
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/usb/musb.h>

#include "common.h"
#include "cputype.h"
#include "irqs.h"

#define DAVINCI_USB_OTG_BASE	0x01c64000

#if IS_ENABLED(CONFIG_USB_MUSB_HDRC)
static struct musb_hdrc_config musb_config = {
	.multipoint	= true,

	.num_eps	= 5,
	.ram_bits	= 10,
};

static struct musb_hdrc_platform_data usb_data = {
	/* OTG requires a Mini-AB connector */
	.mode           = MUSB_OTG,
	.clock		= "usb",
	.config		= &musb_config,
};

static struct resource usb_resources[] = {
	{
		/* physical address */
		.start          = DAVINCI_USB_OTG_BASE,
		.end            = DAVINCI_USB_OTG_BASE + 0x5ff,
		.flags          = IORESOURCE_MEM,
	},
	{
		.start          = DAVINCI_INTC_IRQ(IRQ_USBINT),
		.flags          = IORESOURCE_IRQ,
		.name		= "mc"
	},
	{
		/* placeholder for the dedicated CPPI IRQ */
		.flags          = IORESOURCE_IRQ,
		.name		= "dma"
	},
};

static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device usb_dev = {
	.name           = "musb-davinci",
	.id             = -1,
	.dev = {
		.platform_data		= &usb_data,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.resource       = usb_resources,
	.num_resources  = ARRAY_SIZE(usb_resources),
};

void __init davinci_setup_usb(unsigned mA, unsigned potpgt_ms)
{
	usb_data.power = mA > 510 ? 255 : mA / 2;
	usb_data.potpgt = (potpgt_ms + 1) / 2;

	if (cpu_is_davinci_dm646x()) {
		/* Override the defaults as DM6467 uses different IRQs. */
		usb_dev.resource[1].start = DAVINCI_INTC_IRQ(IRQ_DM646X_USBINT);
		usb_dev.resource[2].start = DAVINCI_INTC_IRQ(
							IRQ_DM646X_USBDMAINT);
	} else	/* other devices don't have dedicated CPPI IRQ */
		usb_dev.num_resources = 2;

	platform_device_register(&usb_dev);
}

#else

void __init davinci_setup_usb(unsigned mA, unsigned potpgt_ms)
{
}

#endif  /* CONFIG_USB_MUSB_HDRC */
