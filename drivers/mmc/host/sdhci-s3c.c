// SPDX-License-Identifier: GPL-2.0-only
/* linux/drivers/mmc/host/sdhci-s3c.c
 *
 * Copyright 2008 Openmoko Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * SDHCI (HSMMC) support for Samsung SoC
 */

#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/platform_data/mmc-sdhci-s3c.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <linux/mmc/host.h>

#include "sdhci.h"

#define MAX_BUS_CLK	(4)

#define S3C_SDHCI_CONTROL2			(0x80)
#define S3C_SDHCI_CONTROL3			(0x84)
#define S3C64XX_SDHCI_CONTROL4			(0x8C)

#define S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR	BIT(31)
#define S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK		BIT(30)
#define S3C_SDHCI_CTRL2_CDINVRXD3		BIT(29)
#define S3C_SDHCI_CTRL2_SLCARDOUT		BIT(28)

#define S3C_SDHCI_CTRL2_FLTCLKSEL_MASK		(0xf << 24)
#define S3C_SDHCI_CTRL2_FLTCLKSEL_SHIFT		(24)
#define S3C_SDHCI_CTRL2_FLTCLKSEL(_x)		((_x) << 24)

#define S3C_SDHCI_CTRL2_LVLDAT_MASK		(0xff << 16)
#define S3C_SDHCI_CTRL2_LVLDAT_SHIFT		(16)
#define S3C_SDHCI_CTRL2_LVLDAT(_x)		((_x) << 16)

#define S3C_SDHCI_CTRL2_ENFBCLKTX		BIT(15)
#define S3C_SDHCI_CTRL2_ENFBCLKRX		BIT(14)
#define S3C_SDHCI_CTRL2_SDCDSEL			BIT(13)
#define S3C_SDHCI_CTRL2_SDSIGPC			BIT(12)
#define S3C_SDHCI_CTRL2_ENBUSYCHKTXSTART	BIT(11)

#define S3C_SDHCI_CTRL2_DFCNT_MASK		(0x3 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_SHIFT		(9)
#define S3C_SDHCI_CTRL2_DFCNT_NONE		(0x0 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_4SDCLK		(0x1 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_16SDCLK		(0x2 << 9)
#define S3C_SDHCI_CTRL2_DFCNT_64SDCLK		(0x3 << 9)

#define S3C_SDHCI_CTRL2_ENCLKOUTHOLD		BIT(8)
#define S3C_SDHCI_CTRL2_RWAITMODE		BIT(7)
#define S3C_SDHCI_CTRL2_DISBUFRD		BIT(6)

#define S3C_SDHCI_CTRL2_SELBASECLK_MASK		(0x3 << 4)
#define S3C_SDHCI_CTRL2_SELBASECLK_SHIFT	(4)
#define S3C_SDHCI_CTRL2_PWRSYNC			BIT(3)
#define S3C_SDHCI_CTRL2_ENCLKOUTMSKCON		BIT(1)
#define S3C_SDHCI_CTRL2_HWINITFIN		BIT(0)

#define S3C_SDHCI_CTRL3_FCSEL3			BIT(31)
#define S3C_SDHCI_CTRL3_FCSEL2			BIT(23)
#define S3C_SDHCI_CTRL3_FCSEL1			BIT(15)
#define S3C_SDHCI_CTRL3_FCSEL0			BIT(7)

#define S3C_SDHCI_CTRL3_FIA3_MASK		(0x7f << 24)
#define S3C_SDHCI_CTRL3_FIA3_SHIFT		(24)
#define S3C_SDHCI_CTRL3_FIA3(_x)		((_x) << 24)

#define S3C_SDHCI_CTRL3_FIA2_MASK		(0x7f << 16)
#define S3C_SDHCI_CTRL3_FIA2_SHIFT		(16)
#define S3C_SDHCI_CTRL3_FIA2(_x)		((_x) << 16)

#define S3C_SDHCI_CTRL3_FIA1_MASK		(0x7f << 8)
#define S3C_SDHCI_CTRL3_FIA1_SHIFT		(8)
#define S3C_SDHCI_CTRL3_FIA1(_x)		((_x) << 8)

#define S3C_SDHCI_CTRL3_FIA0_MASK		(0x7f << 0)
#define S3C_SDHCI_CTRL3_FIA0_SHIFT		(0)
#define S3C_SDHCI_CTRL3_FIA0(_x)		((_x) << 0)

#define S3C64XX_SDHCI_CONTROL4_DRIVE_MASK	(0x3 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_SHIFT	(16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_2mA	(0x0 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_4mA	(0x1 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_7mA	(0x2 << 16)
#define S3C64XX_SDHCI_CONTROL4_DRIVE_9mA	(0x3 << 16)

#define S3C64XX_SDHCI_CONTROL4_BUSY		(1)

/**
 * struct sdhci_s3c - S3C SDHCI instance
 * @host: The SDHCI host created
 * @pdev: The platform device we where created from.
 * @ioarea: The resource created when we claimed the IO area.
 * @pdata: The platform data for this controller.
 * @cur_clk: The index of the current bus clock.
 * @ext_cd_irq: External card detect interrupt.
 * @clk_io: The clock for the internal bus interface.
 * @clk_rates: Clock frequencies.
 * @clk_bus: The clocks that are available for the SD/MMC bus clock.
 * @no_divider: No or non-standard internal clock divider.
 */
struct sdhci_s3c {
	struct sdhci_host	*host;
	struct platform_device	*pdev;
	struct resource		*ioarea;
	struct s3c_sdhci_platdata *pdata;
	int			cur_clk;
	int			ext_cd_irq;

	struct clk		*clk_io;
	struct clk		*clk_bus[MAX_BUS_CLK];
	unsigned long		clk_rates[MAX_BUS_CLK];

	bool			no_divider;
};

/**
 * struct sdhci_s3c_drv_data - S3C SDHCI platform specific driver data
 * @sdhci_quirks: sdhci host specific quirks.
 * @no_divider: no or non-standard internal clock divider.
 * @ops: sdhci_ops to use for this variant
 *
 * Specifies platform specific configuration of sdhci controller.
 * Note: A structure for driver specific platform data is used for future
 * expansion of its usage.
 */
struct sdhci_s3c_drv_data {
	unsigned int		sdhci_quirks;
	bool			no_divider;
	const struct sdhci_ops	*ops;
};

static inline struct sdhci_s3c *to_s3c(struct sdhci_host *host)
{
	return sdhci_priv(host);
}

/**
 * sdhci_s3c_get_max_clk - callback to get maximum clock frequency.
 * @host: The SDHCI host instance.
 *
 * Callback to return the maximum clock rate acheivable by the controller.
*/
static unsigned int sdhci_s3c_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	unsigned long rate, max = 0;
	int src;

	for (src = 0; src < MAX_BUS_CLK; src++) {
		rate = ourhost->clk_rates[src];
		if (rate > max)
			max = rate;
	}

	return max;
}

/**
 * sdhci_s3c_consider_clock - consider one the bus clocks for current setting
 * @ourhost: Our SDHCI instance.
 * @src: The source clock index.
 * @wanted: The clock frequency wanted.
 */
static unsigned int sdhci_s3c_consider_clock(struct sdhci_s3c *ourhost,
					     unsigned int src,
					     unsigned int wanted)
{
	unsigned long rate;
	struct clk *clksrc = ourhost->clk_bus[src];
	int shift;

	if (IS_ERR(clksrc))
		return UINT_MAX;

	/*
	 * If controller uses a non-standard clock division, find the best clock
	 * speed possible with selected clock source and skip the division.
	 */
	if (ourhost->no_divider) {
		rate = clk_round_rate(clksrc, wanted);
		return wanted - rate;
	}

	rate = ourhost->clk_rates[src];

	for (shift = 0; shift <= 8; ++shift) {
		if ((rate >> shift) <= wanted)
			break;
	}

	if (shift > 8) {
		dev_dbg(&ourhost->pdev->dev,
			"clk %d: rate %ld, min rate %lu > wanted %u\n",
			src, rate, rate / 256, wanted);
		return UINT_MAX;
	}

	dev_dbg(&ourhost->pdev->dev, "clk %d: rate %ld, want %d, got %ld\n",
		src, rate, wanted, rate >> shift);

	return wanted - (rate >> shift);
}

/**
 * sdhci_s3c_set_clock - callback on clock change
 * @host: The SDHCI host being changed
 * @clock: The clock rate being requested.
 *
 * When the card's clock is going to be changed, look at the new frequency
 * and find the best clock source to go with it.
*/
static void sdhci_s3c_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	unsigned int best = UINT_MAX;
	unsigned int delta;
	int best_src = 0;
	int src;
	u32 ctrl;

	host->mmc->actual_clock = 0;

	/* don't bother if the clock is going off. */
	if (clock == 0) {
		sdhci_set_clock(host, clock);
		return;
	}

	for (src = 0; src < MAX_BUS_CLK; src++) {
		delta = sdhci_s3c_consider_clock(ourhost, src, clock);
		if (delta < best) {
			best = delta;
			best_src = src;
		}
	}

	dev_dbg(&ourhost->pdev->dev,
		"selected source %d, clock %d, delta %d\n",
		 best_src, clock, best);

	/* select the new clock source */
	if (ourhost->cur_clk != best_src) {
		struct clk *clk = ourhost->clk_bus[best_src];

		clk_prepare_enable(clk);
		if (ourhost->cur_clk >= 0)
			clk_disable_unprepare(
					ourhost->clk_bus[ourhost->cur_clk]);

		ourhost->cur_clk = best_src;
		host->max_clk = ourhost->clk_rates[best_src];
	}

	/* turn clock off to card before changing clock source */
	writew(0, host->ioaddr + SDHCI_CLOCK_CONTROL);

	ctrl = readl(host->ioaddr + S3C_SDHCI_CONTROL2);
	ctrl &= ~S3C_SDHCI_CTRL2_SELBASECLK_MASK;
	ctrl |= best_src << S3C_SDHCI_CTRL2_SELBASECLK_SHIFT;
	writel(ctrl, host->ioaddr + S3C_SDHCI_CONTROL2);

	/* reprogram default hardware configuration */
	writel(S3C64XX_SDHCI_CONTROL4_DRIVE_9mA,
		host->ioaddr + S3C64XX_SDHCI_CONTROL4);

	ctrl = readl(host->ioaddr + S3C_SDHCI_CONTROL2);
	ctrl |= (S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR |
		  S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK |
		  S3C_SDHCI_CTRL2_ENFBCLKRX |
		  S3C_SDHCI_CTRL2_DFCNT_NONE |
		  S3C_SDHCI_CTRL2_ENCLKOUTHOLD);
	writel(ctrl, host->ioaddr + S3C_SDHCI_CONTROL2);

	/* reconfigure the controller for new clock rate */
	ctrl = (S3C_SDHCI_CTRL3_FCSEL1 | S3C_SDHCI_CTRL3_FCSEL0);
	if (clock < 25 * 1000000)
		ctrl |= (S3C_SDHCI_CTRL3_FCSEL3 | S3C_SDHCI_CTRL3_FCSEL2);
	writel(ctrl, host->ioaddr + S3C_SDHCI_CONTROL3);

	sdhci_set_clock(host, clock);
}

/**
 * sdhci_s3c_get_min_clock - callback to get minimal supported clock value
 * @host: The SDHCI host being queried
 *
 * To init mmc host properly a minimal clock value is needed. For high system
 * bus clock's values the standard formula gives values out of allowed range.
 * The clock still can be set to lower values, if clock source other then
 * system bus is selected.
*/
static unsigned int sdhci_s3c_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	unsigned long rate, min = ULONG_MAX;
	int src;

	for (src = 0; src < MAX_BUS_CLK; src++) {
		rate = ourhost->clk_rates[src] / 256;
		if (!rate)
			continue;
		if (rate < min)
			min = rate;
	}

	return min;
}

/* sdhci_cmu_get_max_clk - callback to get maximum clock frequency.*/
static unsigned int sdhci_cmu_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	unsigned long rate, max = 0;
	int src;

	for (src = 0; src < MAX_BUS_CLK; src++) {
		struct clk *clk;

		clk = ourhost->clk_bus[src];
		if (IS_ERR(clk))
			continue;

		rate = clk_round_rate(clk, ULONG_MAX);
		if (rate > max)
			max = rate;
	}

	return max;
}

/* sdhci_cmu_get_min_clock - callback to get minimal supported clock value. */
static unsigned int sdhci_cmu_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	unsigned long rate, min = ULONG_MAX;
	int src;

	for (src = 0; src < MAX_BUS_CLK; src++) {
		struct clk *clk;

		clk = ourhost->clk_bus[src];
		if (IS_ERR(clk))
			continue;

		rate = clk_round_rate(clk, 0);
		if (rate < min)
			min = rate;
	}

	return min;
}

/* sdhci_cmu_set_clock - callback on clock change.*/
static void sdhci_cmu_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_s3c *ourhost = to_s3c(host);
	struct device *dev = &ourhost->pdev->dev;
	unsigned long timeout;
	u16 clk = 0;
	int ret;

	host->mmc->actual_clock = 0;

	/* If the clock is going off, set to 0 at clock control register */
	if (clock == 0) {
		sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);
		return;
	}

	sdhci_s3c_set_clock(host, clock);

	/* Reset SD Clock Enable */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	ret = clk_set_rate(ourhost->clk_bus[ourhost->cur_clk], clock);
	if (ret != 0) {
		dev_err(dev, "%s: failed to set clock rate %uHz\n",
			mmc_hostname(host->mmc), clock);
		return;
	}

	clk = SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Wait max 20 ms */
	timeout = 20;
	while (!((clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL))
		& SDHCI_CLOCK_INT_STABLE)) {
		if (timeout == 0) {
			dev_err(dev, "%s: Internal clock never stabilised.\n",
				mmc_hostname(host->mmc));
			return;
		}
		timeout--;
		mdelay(1);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

static const struct sdhci_ops sdhci_s3c_ops_s3c6410 = {
	.get_max_clock		= sdhci_s3c_get_max_clk,
	.set_clock		= sdhci_s3c_set_clock,
	.get_min_clock		= sdhci_s3c_get_min_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_reset,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_s3c_ops_exynos4 __maybe_unused = {
	.get_max_clock		= sdhci_cmu_get_max_clock,
	.set_clock		= sdhci_cmu_set_clock,
	.get_min_clock		= sdhci_cmu_get_min_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_reset,
	.set_uhs_signaling	= sdhci_set_uhs_signaling,
};

#ifdef CONFIG_OF
static int sdhci_s3c_parse_dt(struct device *dev,
		struct sdhci_host *host, struct s3c_sdhci_platdata *pdata)
{
	struct device_node *node = dev->of_node;
	u32 max_width;

	/* if the bus-width property is not specified, assume width as 1 */
	if (of_property_read_u32(node, "bus-width", &max_width))
		max_width = 1;
	pdata->max_width = max_width;

	/* get the card detection method */
	if (of_property_read_bool(node, "broken-cd")) {
		pdata->cd_type = S3C_SDHCI_CD_NONE;
		return 0;
	}

	if (of_property_read_bool(node, "non-removable")) {
		pdata->cd_type = S3C_SDHCI_CD_PERMANENT;
		return 0;
	}

	if (of_property_present(node, "cd-gpios"))
		return 0;

	/* assuming internal card detect that will be configured by pinctrl */
	pdata->cd_type = S3C_SDHCI_CD_INTERNAL;
	return 0;
}
#else
static int sdhci_s3c_parse_dt(struct device *dev,
		struct sdhci_host *host, struct s3c_sdhci_platdata *pdata)
{
	return -EINVAL;
}
#endif

static inline const struct sdhci_s3c_drv_data *sdhci_s3c_get_driver_data(
			struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node)
		return of_device_get_match_data(&pdev->dev);
#endif
	return (const struct sdhci_s3c_drv_data *)
			platform_get_device_id(pdev)->driver_data;
}

static int sdhci_s3c_probe(struct platform_device *pdev)
{
	struct s3c_sdhci_platdata *pdata;
	const struct sdhci_s3c_drv_data *drv_data;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host;
	struct sdhci_s3c *sc;
	int ret, irq, ptr, clks;

	if (!pdev->dev.platform_data && !pdev->dev.of_node) {
		dev_err(dev, "no device data specified\n");
		return -ENOENT;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = sdhci_alloc_host(dev, sizeof(struct sdhci_s3c));
	if (IS_ERR(host)) {
		dev_err(dev, "sdhci_alloc_host() failed\n");
		return PTR_ERR(host);
	}
	sc = sdhci_priv(host);

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		ret = sdhci_s3c_parse_dt(&pdev->dev, host, pdata);
		if (ret)
			return ret;
	} else {
		memcpy(pdata, pdev->dev.platform_data, sizeof(*pdata));
	}

	drv_data = sdhci_s3c_get_driver_data(pdev);

	sc->host = host;
	sc->pdev = pdev;
	sc->pdata = pdata;
	sc->cur_clk = -1;

	platform_set_drvdata(pdev, host);

	sc->clk_io = devm_clk_get(dev, "hsmmc");
	if (IS_ERR(sc->clk_io)) {
		dev_err(dev, "failed to get io clock\n");
		return PTR_ERR(sc->clk_io);
	}

	/* enable the local io clock and keep it running for the moment. */
	clk_prepare_enable(sc->clk_io);

	for (clks = 0, ptr = 0; ptr < MAX_BUS_CLK; ptr++) {
		char name[14];

		snprintf(name, 14, "mmc_busclk.%d", ptr);
		sc->clk_bus[ptr] = devm_clk_get(dev, name);
		if (IS_ERR(sc->clk_bus[ptr]))
			continue;

		clks++;
		sc->clk_rates[ptr] = clk_get_rate(sc->clk_bus[ptr]);

		dev_info(dev, "clock source %d: %s (%ld Hz)\n",
				ptr, name, sc->clk_rates[ptr]);
	}

	if (clks == 0) {
		dev_err(dev, "failed to find any bus clocks\n");
		ret = -ENOENT;
		goto err_no_busclks;
	}

	host->ioaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->ioaddr)) {
		ret = PTR_ERR(host->ioaddr);
		goto err_req_regs;
	}

	/* Ensure we have minimal gpio selected CMD/CLK/Detect */
	if (pdata->cfg_gpio)
		pdata->cfg_gpio(pdev, pdata->max_width);

	host->hw_name = "samsung-hsmmc";
	host->ops = &sdhci_s3c_ops_s3c6410;
	host->quirks = 0;
	host->quirks2 = 0;
	host->irq = irq;

	/* Setup quirks for the controller */
	host->quirks |= SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC;
	host->quirks |= SDHCI_QUIRK_NO_HISPD_BIT;
	if (drv_data) {
		host->quirks |= drv_data->sdhci_quirks;
		host->ops = drv_data->ops;
		sc->no_divider = drv_data->no_divider;
	}

#ifndef CONFIG_MMC_SDHCI_S3C_DMA

	/* we currently see overruns on errors, so disable the SDMA
	 * support as well. */
	host->quirks |= SDHCI_QUIRK_BROKEN_DMA;

#endif /* CONFIG_MMC_SDHCI_S3C_DMA */

	/* It seems we do not get an DATA transfer complete on non-busy
	 * transfers, not sure if this is a problem with this specific
	 * SDHCI block, or a missing configuration that needs to be set. */
	host->quirks |= SDHCI_QUIRK_NO_BUSY_IRQ;

	/* This host supports the Auto CMD12 */
	host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;

	/* Samsung SoCs need BROKEN_ADMA_ZEROLEN_DESC */
	host->quirks |= SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC;

	if (pdata->cd_type == S3C_SDHCI_CD_NONE ||
	    pdata->cd_type == S3C_SDHCI_CD_PERMANENT)
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	if (pdata->cd_type == S3C_SDHCI_CD_PERMANENT)
		host->mmc->caps = MMC_CAP_NONREMOVABLE;

	switch (pdata->max_width) {
	case 8:
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;
		fallthrough;
	case 4:
		host->mmc->caps |= MMC_CAP_4_BIT_DATA;
		break;
	}

	if (pdata->pm_caps)
		host->mmc->pm_caps |= pdata->pm_caps;

	host->quirks |= (SDHCI_QUIRK_32BIT_DMA_ADDR |
			 SDHCI_QUIRK_32BIT_DMA_SIZE);

	/* HSMMC on Samsung SoCs uses SDCLK as timeout clock */
	host->quirks |= SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK;

	/* It supports additional host capabilities if needed */
	if (pdata->host_caps)
		host->mmc->caps |= pdata->host_caps;

	if (pdata->host_caps2)
		host->mmc->caps2 |= pdata->host_caps2;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_suspend_ignore_children(&pdev->dev, 1);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_req_regs;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_req_regs;

#ifdef CONFIG_PM
	if (pdata->cd_type != S3C_SDHCI_CD_INTERNAL)
		clk_disable_unprepare(sc->clk_io);
#endif
	return 0;

 err_req_regs:
	pm_runtime_disable(&pdev->dev);

 err_no_busclks:
	clk_disable_unprepare(sc->clk_io);

	return ret;
}

static void sdhci_s3c_remove(struct platform_device *pdev)
{
	struct sdhci_host *host =  platform_get_drvdata(pdev);
	struct sdhci_s3c *sc = sdhci_priv(host);

	if (sc->ext_cd_irq)
		free_irq(sc->ext_cd_irq, sc);

#ifdef CONFIG_PM
	if (sc->pdata->cd_type != S3C_SDHCI_CD_INTERNAL)
		clk_prepare_enable(sc->clk_io);
#endif
	sdhci_remove_host(host, 1);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(sc->clk_io);
}

static int sdhci_s3c_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	return sdhci_suspend_host(host);
}

static int sdhci_s3c_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);

	return sdhci_resume_host(host);
}

static int sdhci_s3c_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_s3c *ourhost = to_s3c(host);
	struct clk *busclk = ourhost->clk_io;

	sdhci_runtime_suspend_host(host);

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	if (ourhost->cur_clk >= 0)
		clk_disable_unprepare(ourhost->clk_bus[ourhost->cur_clk]);
	clk_disable_unprepare(busclk);
	return 0;
}

static int sdhci_s3c_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_s3c *ourhost = to_s3c(host);
	struct clk *busclk = ourhost->clk_io;

	clk_prepare_enable(busclk);
	if (ourhost->cur_clk >= 0)
		clk_prepare_enable(ourhost->clk_bus[ourhost->cur_clk]);
	sdhci_runtime_resume_host(host, 0);
	return 0;
}

static const struct dev_pm_ops sdhci_s3c_pmops = {
	SYSTEM_SLEEP_PM_OPS(sdhci_s3c_suspend, sdhci_s3c_resume)
	RUNTIME_PM_OPS(sdhci_s3c_runtime_suspend, sdhci_s3c_runtime_resume, NULL)
};

static const struct platform_device_id sdhci_s3c_driver_ids[] = {
	{
		.name		= "s3c-sdhci",
		.driver_data	= (kernel_ulong_t)NULL,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, sdhci_s3c_driver_ids);

#ifdef CONFIG_OF
static const struct sdhci_s3c_drv_data exynos4_sdhci_drv_data = {
	.no_divider = true,
	.ops = &sdhci_s3c_ops_exynos4,
};

static const struct of_device_id sdhci_s3c_dt_match[] = {
	{ .compatible = "samsung,s3c6410-sdhci", },
	{ .compatible = "samsung,exynos4210-sdhci",
		.data = &exynos4_sdhci_drv_data },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_s3c_dt_match);
#endif

static struct platform_driver sdhci_s3c_driver = {
	.probe		= sdhci_s3c_probe,
	.remove		= sdhci_s3c_remove,
	.id_table	= sdhci_s3c_driver_ids,
	.driver		= {
		.name	= "s3c-sdhci",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(sdhci_s3c_dt_match),
		.pm	= pm_ptr(&sdhci_s3c_pmops),
	},
};

module_platform_driver(sdhci_s3c_driver);

MODULE_DESCRIPTION("Samsung SDHCI (HSMMC) glue");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
