/*
 * USB
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <linux/usb/musb.h>
#include <linux/usb/otg.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
static struct musb_hdrc_eps_bits musb_eps[] = {
	{ "ep1_tx", 8, },
	{ "ep1_rx", 8, },
	{ "ep2_tx", 8, },
	{ "ep2_rx", 8, },
	{ "ep3_tx", 5, },
	{ "ep3_rx", 5, },
	{ "ep4_tx", 5, },
	{ "ep4_rx", 5, },
};

static struct musb_hdrc_config musb_config = {
	.multipoint	= true,
	.dyn_fifo	= true,
	.soft_con	= true,
	.dma		= true,

	.num_eps	= 5,
	.dma_channels	= 8,
	.ram_bits	= 10,
	.eps_bits	= musb_eps,
};

static struct musb_hdrc_platform_data usb_data = {
#if defined(CONFIG_USB_MUSB_OTG)
	/* OTG requires a Mini-AB connector */
	.mode           = MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_PERIPHERAL)
	.mode           = MUSB_PERIPHERAL,
#elif defined(CONFIG_USB_MUSB_HOST)
	.mode           = MUSB_HOST,
#endif
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
		.start          = IRQ_USBINT,
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 usb_dmamask = DMA_32BIT_MASK;

static struct platform_device usb_dev = {
	.name           = "musb_hdrc",
	.id             = -1,
	.dev = {
		.platform_data		= &usb_data,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask      = DMA_32BIT_MASK,
	},
	.resource       = usb_resources,
	.num_resources  = ARRAY_SIZE(usb_resources),
};

void __init setup_usb(unsigned mA, unsigned potpgt_msec)
{
	usb_data.power = mA / 2;
	usb_data.potpgt = potpgt_msec / 2;
	platform_device_register(&usb_dev);
}

#else

void __init setup_usb(unsigned mA, unsigned potpgt_msec)
{
}

#endif  /* CONFIG_USB_MUSB_HDRC */

