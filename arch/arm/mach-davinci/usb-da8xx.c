// SPDX-License-Identifier: GPL-2.0
/*
 * DA8xx USB
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/phy/phy.h>
#include <linux/platform_data/phy-da8xx-usb.h>
#include <linux/platform_data/usb-davinci.h>
#include <linux/platform_device.h>
#include <linux/usb/musb.h>

#include <mach/clock.h>
#include <mach/common.h>
#include <mach/cputype.h>
#include <mach/da8xx.h>
#include <mach/irqs.h>

#include "clock.h"

#define DA8XX_USB0_BASE		0x01e00000
#define DA8XX_USB1_BASE		0x01e25000

static struct clk *usb20_clk;

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
	},
};

int __init da8xx_register_usb_phy(void)
{
	struct da8xx_usb_phy_platform_data pdata;

	pdata.cfgchip = da8xx_get_cfgchip();
	da8xx_usb_phy.dev.platform_data = &pdata;

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
		.start		= IRQ_DA8XX_USB_INT,
		.flags		= IORESOURCE_IRQ,
		.name		= "mc",
	},
};

static u64 usb_dmamask = DMA_BIT_MASK(32);

static struct platform_device da8xx_usb20_dev = {
	.name		= "musb-da8xx",
	.id             = -1,
	.dev = {
		/*
		 * Setting init_name so that clock lookup will work in
		 * usb20_phy_clk_enable() even if this device is not registered.
		 */
		.init_name		= "musb-da8xx",
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
		.start	= IRQ_DA8XX_IRQN,
		.end	= IRQ_DA8XX_IRQN,
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

static struct clk usb_refclkin = {
	.name		= "usb_refclkin",
	.set_rate	= davinci_simple_set_rate,
};

static struct clk_lookup usb_refclkin_lookup =
	CLK(NULL, "usb_refclkin", &usb_refclkin);

/**
 * da8xx_register_usb_refclkin - register USB_REFCLKIN clock
 *
 * @rate: The clock rate in Hz
 *
 * This clock is only needed if the board provides an external USB_REFCLKIN
 * signal, in which case it will be used as the parent of usb20_phy_clk and/or
 * usb11_phy_clk.
 */
int __init da8xx_register_usb_refclkin(int rate)
{
	int ret;

	usb_refclkin.rate = rate;
	ret = clk_register(&usb_refclkin);
	if (ret)
		return ret;

	clkdev_add(&usb_refclkin_lookup);

	return 0;
}

static void usb20_phy_clk_enable(struct clk *clk)
{
	u32 val;
	u32 timeout = 500000; /* 500 msec */

	val = readl(DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	/* The USB 2.O PLL requires that the USB 2.O PSC is enabled as well. */
	davinci_clk_enable(usb20_clk);

	/*
	 * Turn on the USB 2.0 PHY, but just the PLL, and not OTG. The USB 1.1
	 * host may use the PLL clock without USB 2.0 OTG being used.
	 */
	val &= ~(CFGCHIP2_RESET | CFGCHIP2_PHYPWRDN);
	val |= CFGCHIP2_PHY_PLLON;

	writel(val, DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	while (--timeout) {
		val = readl(DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));
		if (val & CFGCHIP2_PHYCLKGD)
			goto done;
		udelay(1);
	}

	pr_err("Timeout waiting for USB 2.0 PHY clock good\n");
done:
	davinci_clk_disable(usb20_clk);
}

static void usb20_phy_clk_disable(struct clk *clk)
{
	u32 val;

	val = readl(DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));
	val |= CFGCHIP2_PHYPWRDN;
	writel(val, DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));
}

static int usb20_phy_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 val;

	val = readl(DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	/* Set the mux depending on the parent clock. */
	if (parent == &usb_refclkin) {
		val &= ~CFGCHIP2_USB2PHYCLKMUX;
	} else if (strcmp(parent->name, "pll0_aux_clk") == 0) {
		val |= CFGCHIP2_USB2PHYCLKMUX;
	} else {
		pr_err("Bad parent on USB 2.0 PHY clock\n");
		return -EINVAL;
	}

	/* reference frequency also comes from parent clock */
	val &= ~CFGCHIP2_REFFREQ_MASK;
	switch (clk_get_rate(parent)) {
	case 12000000:
		val |= CFGCHIP2_REFFREQ_12MHZ;
		break;
	case 13000000:
		val |= CFGCHIP2_REFFREQ_13MHZ;
		break;
	case 19200000:
		val |= CFGCHIP2_REFFREQ_19_2MHZ;
		break;
	case 20000000:
		val |= CFGCHIP2_REFFREQ_20MHZ;
		break;
	case 24000000:
		val |= CFGCHIP2_REFFREQ_24MHZ;
		break;
	case 26000000:
		val |= CFGCHIP2_REFFREQ_26MHZ;
		break;
	case 38400000:
		val |= CFGCHIP2_REFFREQ_38_4MHZ;
		break;
	case 40000000:
		val |= CFGCHIP2_REFFREQ_40MHZ;
		break;
	case 48000000:
		val |= CFGCHIP2_REFFREQ_48MHZ;
		break;
	default:
		pr_err("Bad parent clock rate on USB 2.0 PHY clock\n");
		return -EINVAL;
	}

	writel(val, DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	return 0;
}

static struct clk usb20_phy_clk = {
	.name		= "usb0_clk48",
	.clk_enable	= usb20_phy_clk_enable,
	.clk_disable	= usb20_phy_clk_disable,
	.set_parent	= usb20_phy_clk_set_parent,
};

static struct clk_lookup usb20_phy_clk_lookup =
	CLK("da8xx-usb-phy", "usb0_clk48", &usb20_phy_clk);

/**
 * da8xx_register_usb20_phy_clk - register USB0PHYCLKMUX clock
 *
 * @use_usb_refclkin: Selects the parent clock - either "usb_refclkin" if true
 *	or "pll0_aux" if false.
 */
int __init da8xx_register_usb20_phy_clk(bool use_usb_refclkin)
{
	struct clk *parent;
	int ret;

	usb20_clk = clk_get(&da8xx_usb20_dev.dev, "usb20");
	ret = PTR_ERR_OR_ZERO(usb20_clk);
	if (ret)
		return ret;

	parent = clk_get(NULL, use_usb_refclkin ? "usb_refclkin" : "pll0_aux");
	ret = PTR_ERR_OR_ZERO(parent);
	if (ret) {
		clk_put(usb20_clk);
		return ret;
	}

	usb20_phy_clk.parent = parent;
	ret = clk_register(&usb20_phy_clk);
	if (!ret)
		clkdev_add(&usb20_phy_clk_lookup);

	clk_put(parent);

	return ret;
}

static int usb11_phy_clk_set_parent(struct clk *clk, struct clk *parent)
{
	u32 val;

	val = readl(DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	/* Set the USB 1.1 PHY clock mux based on the parent clock. */
	if (parent == &usb20_phy_clk) {
		val &= ~CFGCHIP2_USB1PHYCLKMUX;
	} else if (parent == &usb_refclkin) {
		val |= CFGCHIP2_USB1PHYCLKMUX;
	} else {
		pr_err("Bad parent on USB 1.1 PHY clock\n");
		return -EINVAL;
	}

	writel(val, DA8XX_SYSCFG0_VIRT(DA8XX_CFGCHIP2_REG));

	return 0;
}

static struct clk usb11_phy_clk = {
	.name		= "usb1_clk48",
	.set_parent	= usb11_phy_clk_set_parent,
};

static struct clk_lookup usb11_phy_clk_lookup =
	CLK("da8xx-usb-phy", "usb1_clk48", &usb11_phy_clk);

/**
 * da8xx_register_usb11_phy_clk - register USB1PHYCLKMUX clock
 *
 * @use_usb_refclkin: Selects the parent clock - either "usb_refclkin" if true
 *	or "usb0_clk48" if false.
 */
int __init da8xx_register_usb11_phy_clk(bool use_usb_refclkin)
{
	struct clk *parent;
	int ret = 0;

	if (use_usb_refclkin)
		parent = clk_get(NULL, "usb_refclkin");
	else
		parent = clk_get(&da8xx_usb_phy.dev, "usb0_clk48");
	if (IS_ERR(parent))
		return PTR_ERR(parent);

	usb11_phy_clk.parent = parent;
	ret = clk_register(&usb11_phy_clk);
	if (!ret)
		clkdev_add(&usb11_phy_clk_lookup);

	clk_put(parent);

	return ret;
}
