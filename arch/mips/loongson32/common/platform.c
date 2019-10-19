// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011-2016 Zhang, Keguang <keguang.zhang@gmail.com>
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

#include <platform.h>
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
	.osc_clk_name	= "osc_clk",
	.max_freq	= 266 * 1000,
	.min_freq	= 33 * 1000,
};

struct platform_device ls1x_cpufreq_pdev = {
	.name		= "ls1x-cpufreq",
	.dev		= {
		.platform_data = &ls1x_cpufreq_pdata,
	},
};

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

#if defined(CONFIG_LOONGSON1_LS1B)
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
#elif defined(CONFIG_LOONGSON1_LS1C)
	plat_dat = dev_get_platdata(&pdev->dev);

	val &= ~PHY_INTF_SELI;
	if (plat_dat->interface == PHY_INTERFACE_MODE_RMII)
		val |= 0x4 << PHY_INTF_SELI_SHIFT;
	__raw_writel(val, LS1X_MUX_CTRL1);

	val = __raw_readl(LS1X_MUX_CTRL0);
	__raw_writel(val & (~GMAC_SHUT), LS1X_MUX_CTRL0);
#endif

	return 0;
}

static struct plat_stmmacenet_data ls1x_eth0_pdata = {
	.bus_id			= 0,
	.phy_addr		= -1,
#if defined(CONFIG_LOONGSON1_LS1B)
	.interface		= PHY_INTERFACE_MODE_MII,
#elif defined(CONFIG_LOONGSON1_LS1C)
	.interface		= PHY_INTERFACE_MODE_RMII,
#endif
	.mdio_bus_data		= &ls1x_mdio_bus_data,
	.dma_cfg		= &ls1x_eth_dma_cfg,
	.has_gmac		= 1,
	.tx_coe			= 1,
	.rx_queues_to_use	= 1,
	.tx_queues_to_use	= 1,
	.init			= ls1x_eth_mux_init,
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

#ifdef CONFIG_LOONGSON1_LS1B
static struct plat_stmmacenet_data ls1x_eth1_pdata = {
	.bus_id			= 1,
	.phy_addr		= -1,
	.interface		= PHY_INTERFACE_MODE_MII,
	.mdio_bus_data		= &ls1x_mdio_bus_data,
	.dma_cfg		= &ls1x_eth_dma_cfg,
	.has_gmac		= 1,
	.tx_coe			= 1,
	.rx_queues_to_use	= 1,
	.tx_queues_to_use	= 1,
	.init			= ls1x_eth_mux_init,
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
#endif	/* CONFIG_LOONGSON1_LS1B */

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
void __init ls1x_rtc_set_extclk(struct platform_device *pdev)
{
	u32 val = __raw_readl(LS1X_RTC_CTRL);

	if (!(val & RTC_EXTCLK_OK))
		__raw_writel(val | RTC_EXTCLK_EN, LS1X_RTC_CTRL);
}

struct platform_device ls1x_rtc_pdev = {
	.name		= "ls1x-rtc",
	.id		= -1,
};

/* Watchdog */
static struct resource ls1x_wdt_resources[] = {
	{
		.start	= LS1X_WDT_BASE,
		.end	= LS1X_WDT_BASE + SZ_16 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device ls1x_wdt_pdev = {
	.name		= "ls1x-wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ls1x_wdt_resources),
	.resource	= ls1x_wdt_resources,
};
