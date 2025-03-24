// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/mmc/host/sdhci-of-hlwd.c
 *
 * Nintendo Wii Secure Digital Host Controller Interface.
 * Copyright (C) 2009 The GameCube Linux Team
 * Copyright (C) 2009 Albert Herranz
 *
 * Based on sdhci-of-esdhc.c
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"

/*
 * Ops and quirks for the Nintendo Wii SDHCI controllers.
 */

/*
 * We need a small delay after each write, or things go horribly wrong.
 */
#define SDHCI_HLWD_WRITE_DELAY	5 /* usecs */

static void sdhci_hlwd_writel(struct sdhci_host *host, u32 val, int reg)
{
	sdhci_be32bs_writel(host, val, reg);
	udelay(SDHCI_HLWD_WRITE_DELAY);
}

static void sdhci_hlwd_writew(struct sdhci_host *host, u16 val, int reg)
{
	sdhci_be32bs_writew(host, val, reg);
	udelay(SDHCI_HLWD_WRITE_DELAY);
}

static void sdhci_hlwd_writeb(struct sdhci_host *host, u8 val, int reg)
{
	sdhci_be32bs_writeb(host, val, reg);
	udelay(SDHCI_HLWD_WRITE_DELAY);
}

static const struct sdhci_ops sdhci_hlwd_ops = {
	.read_l = sdhci_be32bs_readl,
	.read_w = sdhci_be32bs_readw,
	.read_b = sdhci_be32bs_readb,
	.write_l = sdhci_hlwd_writel,
	.write_w = sdhci_hlwd_writew,
	.write_b = sdhci_hlwd_writeb,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_hlwd_pdata = {
	.quirks = SDHCI_QUIRK_32BIT_DMA_ADDR |
		  SDHCI_QUIRK_32BIT_DMA_SIZE,
	.ops = &sdhci_hlwd_ops,
};

static int sdhci_hlwd_probe(struct platform_device *pdev)
{
	return sdhci_pltfm_init_and_add_host(pdev, &sdhci_hlwd_pdata, 0);
}

static const struct of_device_id sdhci_hlwd_of_match[] = {
	{ .compatible = "nintendo,hollywood-sdhci" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_hlwd_of_match);

static struct platform_driver sdhci_hlwd_driver = {
	.driver = {
		.name = "sdhci-hlwd",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sdhci_hlwd_of_match,
		.pm = &sdhci_pltfm_pmops,
	},
	.probe = sdhci_hlwd_probe,
	.remove = sdhci_pltfm_remove,
};

module_platform_driver(sdhci_hlwd_driver);

MODULE_DESCRIPTION("Nintendo Wii SDHCI OF driver");
MODULE_AUTHOR("The GameCube Linux Team, Albert Herranz");
MODULE_LICENSE("GPL v2");
