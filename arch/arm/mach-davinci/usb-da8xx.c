// SPDX-License-Identifier: GPL-2.0
/*
 * DA8xx USB
 */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/platform_data/clk-da8xx-cfgchip.h>
#include <linux/platform_data/phy-da8xx-usb.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/platform_device.h>
#include <linux/usb/musb.h>

#include "common.h"
#include "cputype.h"
#include "da8xx.h"
#include "irqs.h"

#define DA8XX_USB0_BASE		0x01e00000
#define DA8XX_USB1_BASE		0x01e25000

#ifndef CONFIG_COMMON_CLK
static struct clk *usb20_clk;
#endif

static struct da8xx_usb_phy_platform_data da8xx_usb_phy_pdata;

static struct platform_device da8xx_usb_phy = {
	.name		= "da8xx-usb-phy",
	.id		= -1,
	.dev		= {
		/*
		 * Setting init_name so that clock lookup will work in
		 * da8xx_register_usb11_phy_clk() even if this device is not
		 * registered yet.
		 */
		.init_name	= "da8xx-usb-phy",
		.platform_data	= &da8xx_usb_phy_pdata,
	},
};

int __init da8xx_register_usb_phy(void)
{
	da8xx_usb_phy_pdata.cfgchip = da8xx_get_cfgchip();

	return platform_device_register(&da8xx_usb_phy);
}

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
		.start		= DAVINCI_INTC_IRQ(IRQ_DA8XX_USB_INT),
		.flags		= IORESOURCE_IRQ,
		.name		= "mc",
	},
};

static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device da8xx_usb20_dev = {
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

	return platform_device_register(&da8xx_usb20_dev);
}

static struct resource da8xx_usb11_resources[] = {
	[0] = {
		.start	= DA8XX_USB1_BASE,
		.end	= DA8XX_USB1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DAVINCI_INTC_IRQ(IRQ_DA8XX_IRQN),
		.end	= DAVINCI_INTC_IRQ(IRQ_DA8XX_IRQN),
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 da8xx_usb11_dma_mask = DMA_BIT_MASK(32);

static struct platform_device da8xx_usb11_device = {
	.name		= "ohci-da8xx",
	.id		= -1,
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

static struct platform_device da8xx_usb_phy_clks_device = {
	.name		= "da830-usb-phy-clks",
	.id		= -1,
};

int __init da8xx_register_usb_phy_clocks(void)
{
	struct da8xx_cfgchip_clk_platform_data pdata;

	pdata.cfgchip = da8xx_get_cfgchip();
	da8xx_usb_phy_clks_device.dev.platform_data = &pdata;

	return platform_device_register(&da8xx_usb_phy_clks_device);
}
