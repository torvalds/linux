/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_clk.h>

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

	if (BCMCPU_IS_6358()) {
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
	if (!BCMCPU_IS_6358())
		return;
	bcm_hwclock_set(CKCTL_6358_EPHY_EN, enable);
}


static struct clk clk_ephy = {
	.set	= ephy_set,
};

/*
 * Ethernet switch clock
 */
static void enetsw_set(struct clk *clk, int enable)
{
	if (!BCMCPU_IS_6368())
		return;
	bcm_hwclock_set(CKCTL_6368_ROBOSW_EN |
			CKCTL_6368_SWPKT_USB_EN |
			CKCTL_6368_SWPKT_SAR_EN, enable);
	if (enable) {
		u32 val;

		/* reset switch core afer clock change */
		val = bcm_perf_readl(PERF_SOFTRESET_6368_REG);
		val &= ~SOFTRESET_6368_ENETSW_MASK;
		bcm_perf_writel(val, PERF_SOFTRESET_6368_REG);
		msleep(10);
		val |= SOFTRESET_6368_ENETSW_MASK;
		bcm_perf_writel(val, PERF_SOFTRESET_6368_REG);
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
	if (!BCMCPU_IS_6358())
		return;
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
	else if (BCMCPU_IS_6358())
		mask = CKCTL_6358_SPI_EN;
	else
		/* BCMCPU_IS_6368 */
		mask = CKCTL_6368_SPI_EN;
	bcm_hwclock_set(mask, enable);
}

static struct clk clk_spi = {
	.set	= spi_set,
};

/*
 * XTM clock
 */
static void xtm_set(struct clk *clk, int enable)
{
	if (!BCMCPU_IS_6368())
		return;

	bcm_hwclock_set(CKCTL_6368_SAR_EN |
			CKCTL_6368_SWPKT_SAR_EN, enable);

	if (enable) {
		u32 val;

		/* reset sar core afer clock change */
		val = bcm_perf_readl(PERF_SOFTRESET_6368_REG);
		val &= ~SOFTRESET_6368_SAR_MASK;
		bcm_perf_writel(val, PERF_SOFTRESET_6368_REG);
		mdelay(1);
		val |= SOFTRESET_6368_SAR_MASK;
		bcm_perf_writel(val, PERF_SOFTRESET_6368_REG);
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
	bcm_hwclock_set(CKCTL_6368_IPSEC_EN, enable);
}

static struct clk clk_ipsec = {
	.set	= ipsec_set,
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
	mutex_lock(&clocks_mutex);
	clk_disable_unlocked(clk);
	mutex_unlock(&clocks_mutex);
}

EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}

EXPORT_SYMBOL(clk_get_rate);

struct clk *clk_get(struct device *dev, const char *id)
{
	if (!strcmp(id, "enet0"))
		return &clk_enet0;
	if (!strcmp(id, "enet1"))
		return &clk_enet1;
	if (!strcmp(id, "enetsw"))
		return &clk_enetsw;
	if (!strcmp(id, "ephy"))
		return &clk_ephy;
	if (!strcmp(id, "usbh"))
		return &clk_usbh;
	if (!strcmp(id, "usbd"))
		return &clk_usbd;
	if (!strcmp(id, "spi"))
		return &clk_spi;
	if (!strcmp(id, "xtm"))
		return &clk_xtm;
	if (!strcmp(id, "periph"))
		return &clk_periph;
	if (BCMCPU_IS_6358() && !strcmp(id, "pcm"))
		return &clk_pcm;
	if (BCMCPU_IS_6368() && !strcmp(id, "ipsec"))
		return &clk_ipsec;
	return ERR_PTR(-ENOENT);
}

EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}

EXPORT_SYMBOL(clk_put);
