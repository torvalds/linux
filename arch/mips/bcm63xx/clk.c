/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_reset.h>

struct clk {
	void		(*set)(struct clk *, int);
	unsigned int	rate;
	unsigned int	usage;
	int		id;
};

static DEFINE_MUTEX(clocks_mutex);


static void clk_enable_unlocked(struct clk *clk)
{
	if (clk->set && (clk->usage++) == 0)
		clk->set(clk, 1);
}

static void clk_disable_unlocked(struct clk *clk)
{
	if (clk->set && (--clk->usage) == 0)
		clk->set(clk, 0);
}

static void bcm_hwclock_set(u32 mask, int enable)
{
	u32 reg;

	reg = bcm_perf_readl(PERF_CKCTL_REG);
	if (enable)
		reg |= mask;
	else
		reg &= ~mask;
	bcm_perf_writel(reg, PERF_CKCTL_REG);
}

/*
 * Ethernet MAC "misc" clock: dma clocks and main clock on 6348
 */
static void enet_misc_set(struct clk *clk, int enable)
{
	u32 mask;

	if (BCMCPU_IS_6338())
		mask = CKCTL_6338_ENET_EN;
	else if (BCMCPU_IS_6345())
		mask = CKCTL_6345_ENET_EN;
	else if (BCMCPU_IS_6348())
		mask = CKCTL_6348_ENET_EN;
	else
		/* BCMCPU_IS_6358 */
		mask = CKCTL_6358_EMUSB_EN;
	bcm_hwclock_set(mask, enable);
}

static struct clk clk_enet_misc = {
	.set	= enet_misc_set,
};

/*
 * Ethernet MAC clocks: only revelant on 6358, silently enable misc
 * clocks
 */
static void enetx_set(struct clk *clk, int enable)
{
	if (enable)
		clk_enable_unlocked(&clk_enet_misc);
	else
		clk_disable_unlocked(&clk_enet_misc);

	if (BCMCPU_IS_3368() || BCMCPU_IS_6358()) {
		u32 mask;

		if (clk->id == 0)
			mask = CKCTL_6358_ENET0_EN;
		else
			mask = CKCTL_6358_ENET1_EN;
		bcm_hwclock_set(mask, enable);
	}
}

static struct clk clk_enet0 = {
	.id	= 0,
	.set	= enetx_set,
};

static struct clk clk_enet1 = {
	.id	= 1,
	.set	= enetx_set,
};

/*
 * Ethernet PHY clock
 */
static void ephy_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_3368() || BCMCPU_IS_6358())
		bcm_hwclock_set(CKCTL_6358_EPHY_EN, enable);
}


static struct clk clk_ephy = {
	.set	= ephy_set,
};

/*
 * Ethernet switch SAR clock
 */
static void swpkt_sar_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6368())
		bcm_hwclock_set(CKCTL_6368_SWPKT_SAR_EN, enable);
	else
		return;
}

static struct clk clk_swpkt_sar = {
	.set	= swpkt_sar_set,
};

/*
 * Ethernet switch USB clock
 */
static void swpkt_usb_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6368())
		bcm_hwclock_set(CKCTL_6368_SWPKT_USB_EN, enable);
	else
		return;
}

static struct clk clk_swpkt_usb = {
	.set	= swpkt_usb_set,
};

/*
 * Ethernet switch clock
 */
static void enetsw_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6328()) {
		bcm_hwclock_set(CKCTL_6328_ROBOSW_EN, enable);
	} else if (BCMCPU_IS_6362()) {
		bcm_hwclock_set(CKCTL_6362_ROBOSW_EN, enable);
	} else if (BCMCPU_IS_6368()) {
		if (enable) {
			clk_enable_unlocked(&clk_swpkt_sar);
			clk_enable_unlocked(&clk_swpkt_usb);
		} else {
			clk_disable_unlocked(&clk_swpkt_usb);
			clk_disable_unlocked(&clk_swpkt_sar);
		}
		bcm_hwclock_set(CKCTL_6368_ROBOSW_EN, enable);
	} else {
		return;
	}

	if (enable) {
		/* reset switch core afer clock change */
		bcm63xx_core_set_reset(BCM63XX_RESET_ENETSW, 1);
		msleep(10);
		bcm63xx_core_set_reset(BCM63XX_RESET_ENETSW, 0);
		msleep(10);
	}
}

static struct clk clk_enetsw = {
	.set	= enetsw_set,
};

/*
 * PCM clock
 */
static void pcm_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_3368())
		bcm_hwclock_set(CKCTL_3368_PCM_EN, enable);
	if (BCMCPU_IS_6358())
		bcm_hwclock_set(CKCTL_6358_PCM_EN, enable);
}

static struct clk clk_pcm = {
	.set	= pcm_set,
};

/*
 * USB host clock
 */
static void usbh_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6328())
		bcm_hwclock_set(CKCTL_6328_USBH_EN, enable);
	else if (BCMCPU_IS_6348())
		bcm_hwclock_set(CKCTL_6348_USBH_EN, enable);
	else if (BCMCPU_IS_6362())
		bcm_hwclock_set(CKCTL_6362_USBH_EN, enable);
	else if (BCMCPU_IS_6368())
		bcm_hwclock_set(CKCTL_6368_USBH_EN, enable);
}

static struct clk clk_usbh = {
	.set	= usbh_set,
};

/*
 * USB device clock
 */
static void usbd_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6328())
		bcm_hwclock_set(CKCTL_6328_USBD_EN, enable);
	else if (BCMCPU_IS_6362())
		bcm_hwclock_set(CKCTL_6362_USBD_EN, enable);
	else if (BCMCPU_IS_6368())
		bcm_hwclock_set(CKCTL_6368_USBD_EN, enable);
}

static struct clk clk_usbd = {
	.set	= usbd_set,
};

/*
 * SPI clock
 */
static void spi_set(struct clk *clk, int enable)
{
	u32 mask;

	if (BCMCPU_IS_6338())
		mask = CKCTL_6338_SPI_EN;
	else if (BCMCPU_IS_6348())
		mask = CKCTL_6348_SPI_EN;
	else if (BCMCPU_IS_3368() || BCMCPU_IS_6358())
		mask = CKCTL_6358_SPI_EN;
	else if (BCMCPU_IS_6362())
		mask = CKCTL_6362_SPI_EN;
	else
		/* BCMCPU_IS_6368 */
		mask = CKCTL_6368_SPI_EN;
	bcm_hwclock_set(mask, enable);
}

static struct clk clk_spi = {
	.set	= spi_set,
};

/*
 * HSSPI clock
 */
static void hsspi_set(struct clk *clk, int enable)
{
	u32 mask;

	if (BCMCPU_IS_6328())
		mask = CKCTL_6328_HSSPI_EN;
	else if (BCMCPU_IS_6362())
		mask = CKCTL_6362_HSSPI_EN;
	else
		return;

	bcm_hwclock_set(mask, enable);
}

static struct clk clk_hsspi = {
	.set	= hsspi_set,
};

/*
 * HSSPI PLL
 */
static struct clk clk_hsspi_pll;

/*
 * XTM clock
 */
static void xtm_set(struct clk *clk, int enable)
{
	if (!BCMCPU_IS_6368())
		return;

	if (enable)
		clk_enable_unlocked(&clk_swpkt_sar);
	else
		clk_disable_unlocked(&clk_swpkt_sar);

	bcm_hwclock_set(CKCTL_6368_SAR_EN, enable);

	if (enable) {
		/* reset sar core afer clock change */
		bcm63xx_core_set_reset(BCM63XX_RESET_SAR, 1);
		mdelay(1);
		bcm63xx_core_set_reset(BCM63XX_RESET_SAR, 0);
		mdelay(1);
	}
}


static struct clk clk_xtm = {
	.set	= xtm_set,
};

/*
 * IPsec clock
 */
static void ipsec_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6362())
		bcm_hwclock_set(CKCTL_6362_IPSEC_EN, enable);
	else if (BCMCPU_IS_6368())
		bcm_hwclock_set(CKCTL_6368_IPSEC_EN, enable);
}

static struct clk clk_ipsec = {
	.set	= ipsec_set,
};

/*
 * PCIe clock
 */

static void pcie_set(struct clk *clk, int enable)
{
	if (BCMCPU_IS_6328())
		bcm_hwclock_set(CKCTL_6328_PCIE_EN, enable);
	else if (BCMCPU_IS_6362())
		bcm_hwclock_set(CKCTL_6362_PCIE_EN, enable);
}

static struct clk clk_pcie = {
	.set	= pcie_set,
};

/*
 * Internal peripheral clock
 */
static struct clk clk_periph = {
	.rate	= (50 * 1000 * 1000),
};


/*
 * Linux clock API implementation
 */
int clk_enable(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	clk_enable_unlocked(clk);
	mutex_unlock(&clocks_mutex);
	return 0;
}

EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (!clk)
		return;

	mutex_lock(&clocks_mutex);
	clk_disable_unlocked(clk);
	mutex_unlock(&clocks_mutex);
}

EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (!clk)
		return 0;

	return clk->rate;
}

EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}
EXPORT_SYMBOL_GPL(clk_set_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}
EXPORT_SYMBOL_GPL(clk_round_rate);

static struct clk_lookup bcm3368_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.1", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enet0", &clk_enet0),
	CLKDEV_INIT(NULL, "enet1", &clk_enet1),
	CLKDEV_INIT(NULL, "ephy", &clk_ephy),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT(NULL, "pcm", &clk_pcm),
	CLKDEV_INIT("bcm63xx_enet.0", "enet", &clk_enet0),
	CLKDEV_INIT("bcm63xx_enet.1", "enet", &clk_enet1),
};

static struct clk_lookup bcm6328_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.1", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx-hsspi.0", "pll", &clk_hsspi_pll),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enetsw", &clk_enetsw),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "hsspi", &clk_hsspi),
	CLKDEV_INIT(NULL, "pcie", &clk_pcie),
};

static struct clk_lookup bcm6338_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enet0", &clk_enet0),
	CLKDEV_INIT(NULL, "enet1", &clk_enet1),
	CLKDEV_INIT(NULL, "ephy", &clk_ephy),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT("bcm63xx_enet.0", "enet", &clk_enet_misc),
};

static struct clk_lookup bcm6345_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enet0", &clk_enet0),
	CLKDEV_INIT(NULL, "enet1", &clk_enet1),
	CLKDEV_INIT(NULL, "ephy", &clk_ephy),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT("bcm63xx_enet.0", "enet", &clk_enet_misc),
};

static struct clk_lookup bcm6348_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enet0", &clk_enet0),
	CLKDEV_INIT(NULL, "enet1", &clk_enet1),
	CLKDEV_INIT(NULL, "ephy", &clk_ephy),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT("bcm63xx_enet.0", "enet", &clk_enet_misc),
	CLKDEV_INIT("bcm63xx_enet.1", "enet", &clk_enet_misc),
};

static struct clk_lookup bcm6358_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.1", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enet0", &clk_enet0),
	CLKDEV_INIT(NULL, "enet1", &clk_enet1),
	CLKDEV_INIT(NULL, "ephy", &clk_ephy),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT(NULL, "pcm", &clk_pcm),
	CLKDEV_INIT(NULL, "swpkt_sar", &clk_swpkt_sar),
	CLKDEV_INIT(NULL, "swpkt_usb", &clk_swpkt_usb),
	CLKDEV_INIT("bcm63xx_enet.0", "enet", &clk_enet0),
	CLKDEV_INIT("bcm63xx_enet.1", "enet", &clk_enet1),
};

static struct clk_lookup bcm6362_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.1", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx-hsspi.0", "pll", &clk_hsspi_pll),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enetsw", &clk_enetsw),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT(NULL, "hsspi", &clk_hsspi),
	CLKDEV_INIT(NULL, "pcie", &clk_pcie),
	CLKDEV_INIT(NULL, "ipsec", &clk_ipsec),
};

static struct clk_lookup bcm6368_clks[] = {
	/* fixed rate clocks */
	CLKDEV_INIT(NULL, "periph", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.0", "refclk", &clk_periph),
	CLKDEV_INIT("bcm63xx_uart.1", "refclk", &clk_periph),
	/* gated clocks */
	CLKDEV_INIT(NULL, "enetsw", &clk_enetsw),
	CLKDEV_INIT(NULL, "usbh", &clk_usbh),
	CLKDEV_INIT(NULL, "usbd", &clk_usbd),
	CLKDEV_INIT(NULL, "spi", &clk_spi),
	CLKDEV_INIT(NULL, "xtm", &clk_xtm),
	CLKDEV_INIT(NULL, "ipsec", &clk_ipsec),
};

#define HSSPI_PLL_HZ_6328	133333333
#define HSSPI_PLL_HZ_6362	400000000

static int __init bcm63xx_clk_init(void)
{
	switch (bcm63xx_get_cpu_id()) {
	case BCM3368_CPU_ID:
		clkdev_add_table(bcm3368_clks, ARRAY_SIZE(bcm3368_clks));
		break;
	case BCM6328_CPU_ID:
		clk_hsspi_pll.rate = HSSPI_PLL_HZ_6328;
		clkdev_add_table(bcm6328_clks, ARRAY_SIZE(bcm6328_clks));
		break;
	case BCM6338_CPU_ID:
		clkdev_add_table(bcm6338_clks, ARRAY_SIZE(bcm6338_clks));
		break;
	case BCM6345_CPU_ID:
		clkdev_add_table(bcm6345_clks, ARRAY_SIZE(bcm6345_clks));
		break;
	case BCM6348_CPU_ID:
		clkdev_add_table(bcm6348_clks, ARRAY_SIZE(bcm6348_clks));
		break;
	case BCM6358_CPU_ID:
		clkdev_add_table(bcm6358_clks, ARRAY_SIZE(bcm6358_clks));
		break;
	case BCM6362_CPU_ID:
		clk_hsspi_pll.rate = HSSPI_PLL_HZ_6362;
		clkdev_add_table(bcm6362_clks, ARRAY_SIZE(bcm6362_clks));
		break;
	case BCM6368_CPU_ID:
		clkdev_add_table(bcm6368_clks, ARRAY_SIZE(bcm6368_clks));
		break;
	}

	return 0;
}
arch_initcall(bcm63xx_clk_init);
