/*
 * Copyright (c) 2011-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/mtd/partitions.h>
#include <linux/sizes.h>
#include <linux/phy.h>
#include <linux/serial_8250.h>
#include <linux/stmmac.h>
#include <linux/usb/ehci_pdriver.h>

#include <loongson1.h>
#include <cpufreq.h>
#include <dma.h>
#include <nand.h>

/* 8250/16550 compatible UART */
#define LS1X_UART(_id)						\
	{							\
		.mapbase	= LS1X_UART ## _id ## _BASE,	\
		.irq		= LS1X_UART ## _id ## _IRQ,	\
		.iotype		= UPIO_MEM,			\
		.flags		= UPF_IOREMAP | UPF_FIXED_TYPE, \
		.type		= PORT_16550A,			\
	}

static struct plat_serial8250_port ls1x_serial8250_pdata[] = {
	LS1X_UART(0),
	LS1X_UART(1),
	LS1X_UART(2),
	LS1X_UART(3),
	{},
};

struct platform_device ls1x_uart_pdev = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data = ls1x_serial8250_pdata,
	},
};

void __init ls1x_serial_set_uartclk(struct platform_device *pdev)
{
	struct clk *clk;
	struct plat_serial8250_port *p;

	clk = clk_get(&pdev->dev, pdev->name);
	if (IS_ERR(clk)) {
		pr_err("unable to get %s clock, err=%ld",
		       pdev->name, PTR_ERR(clk));
		return;
	}
	clk_prepare_enable(clk);

	for (p = pdev->dev.platform_data; p->flags != 0; ++p)
		p->uartclk = clk_get_rate(clk);
}

/* CPUFreq */
static struct plat_ls1x_cpufreq ls1x_cpufreq_pdata = {
	.clk_name	= "cpu_clk",
	.osc_clk_name	= "osc_33m_clk",
	.max_freq	= 266 * 1000,
	.min_freq	= 33 * 1000,
};

struct platform_device ls1x_cpufreq_pdev = {
	.name		= "ls1x-cpufreq",
	.dev		= {
		.platform_data = &ls1x_cpufreq_pdata,
	},
};

/* DMA */
static struct resource ls1x_dma_resources[] = {
	[0] = {
		.start = LS1X_DMAC_BASE,
		.end = LS1X_DMAC_BASE + SZ_4 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = LS1X_DMA0_IRQ,
		.end = LS1X_DMA0_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
		.start = LS1X_DMA1_IRQ,
		.end = LS1X_DMA1_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	[3] = {
		.start = LS1X_DMA2_IRQ,
		.end = LS1X_DMA2_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device ls1x_dma_pdev = {
	.name		= "ls1x-dma",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ls1x_dma_resources),
	.resource	= ls1x_dma_resources,
};

void __init ls1x_dma_set_platdata(struct plat_ls1x_dma *pdata)
{
	ls1x_dma_pdev.dev.platform_data = pdata;
}

/* Synopsys Ethernet GMAC */
static struct stmmac_mdio_bus_data ls1x_mdio_bus_data = {
	.phy_mask	= 0,
};

static struct stmmac_dma_cfg ls1x_eth_dma_cfg = {
	.pbl		= 1,
};

int ls1x_eth_mux_init(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat = NULL;
	u32 val;

	val = __raw_readl(LS1X_MUX_CTRL1);

	plat_dat = dev_get_platdata(&pdev->dev);
	if (plat_dat->bus_id) {
		__raw_writel(__raw_readl(LS1X_MUX_CTRL0) | GMAC1_USE_UART1 |
			     GMAC1_USE_UART0, LS1X_MUX_CTRL0);
		switch (plat_dat->interface) {
		case PHY_INTERFACE_MODE_RGMII:
			val &= ~(GMAC1_USE_TXCLK | GMAC1_USE_PWM23);
			break;
		case PHY_INTERFACE_MODE_MII:
			val |= (GMAC1_USE_TXCLK | GMAC1_USE_PWM23);
			break;
		default:
			pr_err("unsupported mii mode %d\n",
			       plat_dat->interface);
			return -ENOTSUPP;
		}
		val &= ~GMAC1_SHUT;
	} else {
		switch (plat_dat->interface) {
		case PHY_INTERFACE_MODE_RGMII:
			val &= ~(GMAC0_USE_TXCLK | GMAC0_USE_PWM01);
			break;
		case PHY_INTERFACE_MODE_MII:
			val |= (GMAC0_USE_TXCLK | GMAC0_USE_PWM01);
			break;
		default:
			pr_err("unsupported mii mode %d\n",
			       plat_dat->interface);
			return -ENOTSUPP;
		}
		val &= ~GMAC0_SHUT;
	}
	__raw_writel(val, LS1X_MUX_CTRL1);

	return 0;
}

static struct plat_stmmacenet_data ls1x_eth0_pdata = {
	.bus_id		= 0,
	.phy_addr	= -1,
	.interface	= PHY_INTERFACE_MODE_MII,
	.mdio_bus_data	= &ls1x_mdio_bus_data,
	.dma_cfg	= &ls1x_eth_dma_cfg,
	.has_gmac	= 1,
	.tx_coe		= 1,
	.init		= ls1x_eth_mux_init,
};

static struct resource ls1x_eth0_resources[] = {
	[0] = {
		.start	= LS1X_GMAC0_BASE,
		.end	= LS1X_GMAC0_BASE + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "macirq",
		.start	= LS1X_GMAC0_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ls1x_eth0_pdev = {
	.name		= "stmmaceth",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ls1x_eth0_resources),
	.resource	= ls1x_eth0_resources,
	.dev		= {
		.platform_data = &ls1x_eth0_pdata,
	},
};

static struct plat_stmmacenet_data ls1x_eth1_pdata = {
	.bus_id		= 1,
	.phy_addr	= -1,
	.interface	= PHY_INTERFACE_MODE_MII,
	.mdio_bus_data	= &ls1x_mdio_bus_data,
	.dma_cfg	= &ls1x_eth_dma_cfg,
	.has_gmac	= 1,
	.tx_coe		= 1,
	.init		= ls1x_eth_mux_init,
};

static struct resource ls1x_eth1_resources[] = {
	[0] = {
		.start	= LS1X_GMAC1_BASE,
		.end	= LS1X_GMAC1_BASE + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "macirq",
		.start	= LS1X_GMAC1_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ls1x_eth1_pdev = {
	.name		= "stmmaceth",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(ls1x_eth1_resources),
	.resource	= ls1x_eth1_resources,
	.dev		= {
		.platform_data = &ls1x_eth1_pdata,
	},
};

/* GPIO */
static struct resource ls1x_gpio0_resources[] = {
	[0] = {
		.start	= LS1X_GPIO0_BASE,
		.end	= LS1X_GPIO0_BASE + SZ_4 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device ls1x_gpio0_pdev = {
	.name		= "ls1x-gpio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ls1x_gpio0_resources),
	.resource	= ls1x_gpio0_resources,
};

static struct resource ls1x_gpio1_resources[] = {
	[0] = {
		.start	= LS1X_GPIO1_BASE,
		.end	= LS1X_GPIO1_BASE + SZ_4 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device ls1x_gpio1_pdev = {
	.name		= "ls1x-gpio",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(ls1x_gpio1_resources),
	.resource	= ls1x_gpio1_resources,
};

/* NAND Flash */
static struct resource ls1x_nand_resources[] = {
	[0] = {
		.start	= LS1X_NAND_BASE,
		.end	= LS1X_NAND_BASE + SZ_32 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* DMA channel 0 is dedicated to NAND */
		.start	= LS1X_DMA_CHANNEL0,
		.end	= LS1X_DMA_CHANNEL0,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device ls1x_nand_pdev = {
	.name		= "ls1x-nand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ls1x_nand_resources),
	.resource	= ls1x_nand_resources,
};

void __init ls1x_nand_set_platdata(struct plat_ls1x_nand *pdata)
{
	ls1x_nand_pdev.dev.platform_data = pdata;
}

/* USB EHCI */
static u64 ls1x_ehci_dmamask = DMA_BIT_MASK(32);

static struct resource ls1x_ehci_resources[] = {
	[0] = {
		.start	= LS1X_EHCI_BASE,
		.end	= LS1X_EHCI_BASE + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LS1X_EHCI_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct usb_ehci_pdata ls1x_ehci_pdata = {
};

struct platform_device ls1x_ehci_pdev = {
	.name		= "ehci-platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ls1x_ehci_resources),
	.resource	= ls1x_ehci_resources,
	.dev		= {
		.dma_mask = &ls1x_ehci_dmamask,
		.platform_data = &ls1x_ehci_pdata,
	},
};

/* Real Time Clock */
struct platform_device ls1x_rtc_pdev = {
	.name		= "ls1x-rtc",
	.id		= -1,
};
