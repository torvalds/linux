/*
 * DA8xx USB
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/platform_device.h>
#include <linux/usb/musb.h>

#include <mach/common.h>
#include <mach/cputype.h>
#include <mach/da8xx.h>
#include <mach/irqs.h>

#define DA8XX_USB0_BASE		0x01e00000
#define DA8XX_USB1_BASE		0x01e25000

#if IS_ENABLED(CONFIG_USB_MUSB_HDRC)

static struct musb_hdrc_config musb_config = {
	.multipoint	= true,
	.num_eps	= 5,
	.ram_bits	= 10,
};

static struct musb_hdrc_platform_data usb_data = {
	/* OTG requires a Mini-AB connector */
	.mode           = MUSB_OTG,
	.clock		= "usb20",
	.config		= &musb_config,
};

static struct resource da8xx_usb20_resources[] = {
	{
		.start		= DA8XX_USB0_BASE,
		.end		= DA8XX_USB0_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IRQ_DA8XX_USB_INT,
		.flags		= IORESOURCE_IRQ,
		.name		= "mc",
	},
};

static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device usb_dev = {
	.name		= "musb-da8xx",
	.id             = -1,
	.dev = {
		.platform_data		= &usb_data,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
	},
	.resource	= da8xx_usb20_resources,
	.num_resources	= ARRAY_SIZE(da8xx_usb20_resources),
};

int __init da8xx_register_usb20(unsigned int mA, unsigned int potpgt)
{
	usb_data.power	= mA > 510 ? 255 : mA / 2;
	usb_data.potpgt = (potpgt + 1) / 2;

	return platform_device_register(&usb_dev);
}

#else

int __init da8xx_register_usb20(unsigned int mA, unsigned int potpgt)
{
	return 0;
}

#endif  /* CONFIG_USB_MUSB_HDRC */

static struct resource da8xx_usb11_resources[] = {
	[0] = {
		.start	= DA8XX_USB1_BASE,
		.end	= DA8XX_USB1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DA8XX_IRQN,
		.end	= IRQ_DA8XX_IRQN,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 da8xx_usb11_dma_mask = DMA_BIT_MASK(32);

static struct platform_device da8xx_usb11_device = {
	.name		= "ohci",
	.id		= 0,
	.dev = {
		.dma_mask		= &da8xx_usb11_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(da8xx_usb11_resources),
	.resource	= da8xx_usb11_resources,
};

int __init da8xx_register_usb11(struct da8xx_ohci_root_hub *pdata)
{
	da8xx_usb11_device.dev.platform_data = pdata;
	return platform_device_register(&da8xx_usb11_device);
}
