/*
 * Freescale eSDHC controller driver.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include "sdhci-of.h"
#include "sdhci.h"

/*
 * Ops and quirks for the Freescale eSDHC controller.
 */

#define ESDHC_DMA_SYSCTL	0x40c
#define ESDHC_DMA_SNOOP		0x00000040

#define ESDHC_SYSTEM_CONTROL	0x2c
#define ESDHC_CLOCK_MASK	0x0000fff0
#define ESDHC_PREDIV_SHIFT	8
#define ESDHC_DIVIDER_SHIFT	4
#define ESDHC_CLOCK_PEREN	0x00000004
#define ESDHC_CLOCK_HCKEN	0x00000002
#define ESDHC_CLOCK_IPGEN	0x00000001

#define ESDHC_HOST_CONTROL_RES	0x05

static u16 esdhc_readw(struct sdhci_host *host, int reg)
{
	u16 ret;

	if (unlikely(reg == SDHCI_HOST_VERSION))
		ret = in_be16(host->ioaddr + reg);
	else
		ret = sdhci_be32bs_readw(host, reg);
	return ret;
}

static void esdhc_writew(struct sdhci_host *host, u16 val, int reg)
{
	if (reg == SDHCI_BLOCK_SIZE) {
		/*
		 * Two last DMA bits are reserved, and first one is used for
		 * non-standard blksz of 4096 bytes that we don't support
		 * yet. So clear the DMA boundary bits.
		 */
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
	}
	sdhci_be32bs_writew(host, val, reg);
}

static void esdhc_writeb(struct sdhci_host *host, u8 val, int reg)
{
	/* Prevent SDHCI core from writing reserved bits (e.g. HISPD). */
	if (reg == SDHCI_HOST_CONTROL)
		val &= ~ESDHC_HOST_CONTROL_RES;
	sdhci_be32bs_writeb(host, val, reg);
}

static void esdhc_set_clock(struct sdhci_host *host, unsigned int clock)
{
	int pre_div = 2;
	int div = 1;

	clrbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL, ESDHC_CLOCK_IPGEN |
		  ESDHC_CLOCK_HCKEN | ESDHC_CLOCK_PEREN | ESDHC_CLOCK_MASK);

	if (clock == 0)
		goto out;

	while (host->max_clk / pre_div / 16 > clock && pre_div < 256)
		pre_div *= 2;

	while (host->max_clk / pre_div / div > clock && div < 16)
		div++;

	dev_dbg(mmc_dev(host->mmc), "desired SD clock: %d, actual: %d\n",
		clock, host->max_clk / pre_div / div);

	pre_div >>= 1;
	div--;

	setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL, ESDHC_CLOCK_IPGEN |
		  ESDHC_CLOCK_HCKEN | ESDHC_CLOCK_PEREN |
		  div << ESDHC_DIVIDER_SHIFT | pre_div << ESDHC_PREDIV_SHIFT);
	mdelay(100);
out:
	host->clock = clock;
}

static int esdhc_enable_dma(struct sdhci_host *host)
{
	setbits32(host->ioaddr + ESDHC_DMA_SYSCTL, ESDHC_DMA_SNOOP);
	return 0;
}

static unsigned int esdhc_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_of_host *of_host = sdhci_priv(host);

	return of_host->clock;
}

static unsigned int esdhc_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_of_host *of_host = sdhci_priv(host);

	return of_host->clock / 256 / 16;
}

struct sdhci_of_data sdhci_esdhc = {
	.quirks = SDHCI_QUIRK_FORCE_BLK_SZ_2048 |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_NO_BUSY_IRQ |
		  SDHCI_QUIRK_NONSTANDARD_CLOCK |
		  SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_PIO_NEEDS_DELAY |
		  SDHCI_QUIRK_RESTORE_IRQS_AFTER_RESET |
		  SDHCI_QUIRK_NO_CARD_NO_RESET,
	.ops = {
		.readl = sdhci_be32bs_readl,
		.readw = esdhc_readw,
		.readb = sdhci_be32bs_readb,
		.writel = sdhci_be32bs_writel,
		.writew = esdhc_writew,
		.writeb = esdhc_writeb,
		.set_clock = esdhc_set_clock,
		.enable_dma = esdhc_enable_dma,
		.get_max_clock = esdhc_get_max_clock,
		.get_min_clock = esdhc_get_min_clock,
	},
};
