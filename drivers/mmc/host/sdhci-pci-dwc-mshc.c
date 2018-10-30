// SPDX-License-Identifier: GPL-2.0
/*
 * SDHCI driver for Synopsys DWC_MSHC controller
 *
 * Copyright (C) 2018 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors:
 *	Prabu Thangamuthu <prabu.t@synopsys.com>
 *	Manjunath M B <manjumb@synopsys.com>
 */

#include "sdhci.h"
#include "sdhci-pci.h"

#define SDHCI_VENDOR_PTR_R	0xE8

/* Synopsys vendor specific registers */
#define SDHC_GPIO_OUT		0x34
#define SDHC_AT_CTRL_R		0x40
#define SDHC_SW_TUNE_EN		0x00000010

/* MMCM DRP */
#define SDHC_MMCM_DIV_REG	0x1020
#define DIV_REG_100_MHZ		0x1145
#define DIV_REG_200_MHZ		0x1083
#define SDHC_MMCM_CLKFBOUT	0x1024
#define CLKFBOUT_100_MHZ	0x0000
#define CLKFBOUT_200_MHZ	0x0080
#define SDHC_CCLK_MMCM_RST	0x00000001

static void sdhci_snps_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	u32 reg, vendor_ptr;

	vendor_ptr = sdhci_readw(host, SDHCI_VENDOR_PTR_R);

	/* Disable software managed rx tuning */
	reg = sdhci_readl(host, (SDHC_AT_CTRL_R + vendor_ptr));
	reg &= ~SDHC_SW_TUNE_EN;
	sdhci_writel(host, reg, (SDHC_AT_CTRL_R + vendor_ptr));

	if (clock <= 52000000) {
		sdhci_set_clock(host, clock);
	} else {
		/* Assert reset to MMCM */
		reg = sdhci_readl(host, (SDHC_GPIO_OUT + vendor_ptr));
		reg |= SDHC_CCLK_MMCM_RST;
		sdhci_writel(host, reg, (SDHC_GPIO_OUT + vendor_ptr));

		/* Configure MMCM */
		if (clock == 100000000) {
			sdhci_writel(host, DIV_REG_100_MHZ, SDHC_MMCM_DIV_REG);
			sdhci_writel(host, CLKFBOUT_100_MHZ,
					SDHC_MMCM_CLKFBOUT);
		} else {
			sdhci_writel(host, DIV_REG_200_MHZ, SDHC_MMCM_DIV_REG);
			sdhci_writel(host, CLKFBOUT_200_MHZ,
					SDHC_MMCM_CLKFBOUT);
		}

		/* De-assert reset to MMCM */
		reg = sdhci_readl(host, (SDHC_GPIO_OUT + vendor_ptr));
		reg &= ~SDHC_CCLK_MMCM_RST;
		sdhci_writel(host, reg, (SDHC_GPIO_OUT + vendor_ptr));

		/* Enable clock */
		clk = SDHCI_PROG_CLOCK_MODE | SDHCI_CLOCK_INT_EN |
			SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
	}
}

static const struct sdhci_ops sdhci_snps_ops = {
	.set_clock	= sdhci_snps_set_clock,
	.enable_dma	= sdhci_pci_enable_dma,
	.set_bus_width	= sdhci_set_bus_width,
	.reset		= sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

const struct sdhci_pci_fixes sdhci_snps = {
	.ops		= &sdhci_snps_ops,
};
