/*
 * Freescale eSDHC i.MX controller driver for the platform bus.
 *
 * derived from the OF-version.
 *
 * Copyright (c) 2010 Pengutronix e.K.
 *   Author: Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci-pltfm.h>
#include <mach/hardware.h>
#include "sdhci.h"
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

static inline void esdhc_clrset_le(struct sdhci_host *host, u32 mask, u32 val, int reg)
{
	void __iomem *base = host->ioaddr + (reg & ~0x3);
	u32 shift = (reg & 0x3) * 8;

	writel(((readl(base) & ~(mask << shift)) | (val << shift)), base);
}

static u16 esdhc_readw_le(struct sdhci_host *host, int reg)
{
	if (unlikely(reg == SDHCI_HOST_VERSION))
		reg ^= 2;

	return readw(host->ioaddr + reg);
}

static void esdhc_writew_le(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	switch (reg) {
	case SDHCI_TRANSFER_MODE:
		/*
		 * Postpone this write, we must do it together with a
		 * command write that is down below.
		 */
		pltfm_host->scratchpad = val;
		return;
	case SDHCI_COMMAND:
		writel(val << 16 | pltfm_host->scratchpad,
			host->ioaddr + SDHCI_TRANSFER_MODE);
		return;
	case SDHCI_BLOCK_SIZE:
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
		break;
	}
	esdhc_clrset_le(host, 0xffff, val, reg);
}

static void esdhc_writeb_le(struct sdhci_host *host, u8 val, int reg)
{
	u32 new_val;

	switch (reg) {
	case SDHCI_POWER_CONTROL:
		/*
		 * FSL put some DMA bits here
		 * If your board has a regulator, code should be here
		 */
		return;
	case SDHCI_HOST_CONTROL:
		/* FSL messed up here, so we can just keep those two */
		new_val = val & (SDHCI_CTRL_LED | SDHCI_CTRL_4BITBUS);
		/* ensure the endianess */
		new_val |= ESDHC_HOST_CONTROL_LE;
		/* DMA mode bits are shifted */
		new_val |= (val & SDHCI_CTRL_DMA_MASK) << 5;

		esdhc_clrset_le(host, 0xffff, new_val, reg);
		return;
	}
	esdhc_clrset_le(host, 0xff, val, reg);
}

static unsigned int esdhc_pltfm_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk);
}

static unsigned int esdhc_pltfm_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk) / 256 / 16;
}

static int esdhc_pltfm_init(struct sdhci_host *host, struct sdhci_pltfm_data *pdata)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct clk *clk;

	clk = clk_get(mmc_dev(host->mmc), NULL);
	if (IS_ERR(clk)) {
		dev_err(mmc_dev(host->mmc), "clk err\n");
		return PTR_ERR(clk);
	}
	clk_enable(clk);
	pltfm_host->clk = clk;

	if (cpu_is_mx35() || cpu_is_mx51())
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	/* Fix errata ENGcm07207 which is present on i.MX25 and i.MX35 */
	if (cpu_is_mx25() || cpu_is_mx35())
		host->quirks |= SDHCI_QUIRK_NO_MULTIBLOCK;

	return 0;
}

static void esdhc_pltfm_exit(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	clk_disable(pltfm_host->clk);
	clk_put(pltfm_host->clk);
}

static struct sdhci_ops sdhci_esdhc_ops = {
	.read_w = esdhc_readw_le,
	.write_w = esdhc_writew_le,
	.write_b = esdhc_writeb_le,
	.set_clock = esdhc_set_clock,
	.get_max_clock = esdhc_pltfm_get_max_clock,
	.get_min_clock = esdhc_pltfm_get_min_clock,
};

struct sdhci_pltfm_data sdhci_esdhc_imx_pdata = {
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_BROKEN_ADMA,
	/* ADMA has issues. Might be fixable */
	.ops = &sdhci_esdhc_ops,
	.init = esdhc_pltfm_init,
	.exit = esdhc_pltfm_exit,
};
