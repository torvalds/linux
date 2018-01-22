/*
 * Atmel SDMMC controller driver.
 *
 * Copyright (C) 2015 Atmel,
 *		 2015 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "sdhci-pltfm.h"

#define SDMMC_MC1R	0x204
#define		SDMMC_MC1R_DDR		BIT(3)
#define		SDMMC_MC1R_FCD		BIT(7)
#define SDMMC_CACR	0x230
#define		SDMMC_CACR_CAPWREN	BIT(0)
#define		SDMMC_CACR_KEY		(0x46 << 8)

#define SDHCI_AT91_PRESET_COMMON_CONF	0x400 /* drv type B, programmable clock mode */

struct sdhci_at91_priv {
	struct clk *hclock;
	struct clk *gck;
	struct clk *mainck;
	bool restore_needed;
};

static void sdhci_at91_set_force_card_detect(struct sdhci_host *host)
{
	u8 mc1r;

	mc1r = readb(host->ioaddr + SDMMC_MC1R);
	mc1r |= SDMMC_MC1R_FCD;
	writeb(mc1r, host->ioaddr + SDMMC_MC1R);
}

static void sdhci_at91_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;
	unsigned long timeout;

	host->mmc->actual_clock = 0;

	/*
	 * There is no requirement to disable the internal clock before
	 * changing the SD clock configuration. Moreover, disabling the
	 * internal clock, changing the configuration and re-enabling the
	 * internal clock causes some bugs. It can prevent to get the internal
	 * clock stable flag ready and an unexpected switch to the base clock
	 * when using presets.
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	clk = sdhci_calc_clk(host, clock, &host->mmc->actual_clock);

	clk |= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Wait max 20 ms */
	timeout = 20;
	while (!((clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL))
		& SDHCI_CLOCK_INT_STABLE)) {
		if (timeout == 0) {
			pr_err("%s: Internal clock never stabilised.\n",
			       mmc_hostname(host->mmc));
			return;
		}
		timeout--;
		mdelay(1);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

/*
 * In this specific implementation of the SDHCI controller, the power register
 * needs to have a valid voltage set even when the power supply is managed by
 * an external regulator.
 */
static void sdhci_at91_set_power(struct sdhci_host *host, unsigned char mode,
		     unsigned short vdd)
{
	if (!IS_ERR(host->mmc->supply.vmmc)) {
		struct mmc_host *mmc = host->mmc;

		mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, vdd);
	}
	sdhci_set_power_noreg(host, mode, vdd);
}

static void sdhci_at91_set_uhs_signaling(struct sdhci_host *host,
					 unsigned int timing)
{
	if (timing == MMC_TIMING_MMC_DDR52)
		sdhci_writeb(host, SDMMC_MC1R_DDR, SDMMC_MC1R);
	sdhci_set_uhs_signaling(host, timing);
}

static void sdhci_at91_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_reset(host, mask);

	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		sdhci_at91_set_force_card_detect(host);
}

static const struct sdhci_ops sdhci_at91_sama5d2_ops = {
	.set_clock		= sdhci_at91_set_clock,
	.set_bus_width		= sdhci_set_bus_width,
	.reset			= sdhci_at91_reset,
	.set_uhs_signaling	= sdhci_at91_set_uhs_signaling,
	.set_power		= sdhci_at91_set_power,
};

static const struct sdhci_pltfm_data soc_data_sama5d2 = {
	.ops = &sdhci_at91_sama5d2_ops,
};

static const struct of_device_id sdhci_at91_dt_match[] = {
	{ .compatible = "atmel,sama5d2-sdhci", .data = &soc_data_sama5d2 },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_at91_dt_match);

static int sdhci_at91_set_clks_presets(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_at91_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;
	unsigned int			caps0, caps1;
	unsigned int			clk_base, clk_mul;
	unsigned int			gck_rate, real_gck_rate;
	unsigned int			preset_div;

	/*
	 * The mult clock is provided by as a generated clock by the PMC
	 * controller. In order to set the rate of gck, we have to get the
	 * base clock rate and the clock mult from capabilities.
	 */
	clk_prepare_enable(priv->hclock);
	caps0 = readl(host->ioaddr + SDHCI_CAPABILITIES);
	caps1 = readl(host->ioaddr + SDHCI_CAPABILITIES_1);
	clk_base = (caps0 & SDHCI_CLOCK_V3_BASE_MASK) >> SDHCI_CLOCK_BASE_SHIFT;
	clk_mul = (caps1 & SDHCI_CLOCK_MUL_MASK) >> SDHCI_CLOCK_MUL_SHIFT;
	gck_rate = clk_base * 1000000 * (clk_mul + 1);
	ret = clk_set_rate(priv->gck, gck_rate);
	if (ret < 0) {
		dev_err(dev, "failed to set gck");
		clk_disable_unprepare(priv->hclock);
		return ret;
	}
	/*
	 * We need to check if we have the requested rate for gck because in
	 * some cases this rate could be not supported. If it happens, the rate
	 * is the closest one gck can provide. We have to update the value
	 * of clk mul.
	 */
	real_gck_rate = clk_get_rate(priv->gck);
	if (real_gck_rate != gck_rate) {
		clk_mul = real_gck_rate / (clk_base * 1000000) - 1;
		caps1 &= (~SDHCI_CLOCK_MUL_MASK);
		caps1 |= ((clk_mul << SDHCI_CLOCK_MUL_SHIFT) &
			  SDHCI_CLOCK_MUL_MASK);
		/* Set capabilities in r/w mode. */
		writel(SDMMC_CACR_KEY | SDMMC_CACR_CAPWREN,
		       host->ioaddr + SDMMC_CACR);
		writel(caps1, host->ioaddr + SDHCI_CAPABILITIES_1);
		/* Set capabilities in ro mode. */
		writel(0, host->ioaddr + SDMMC_CACR);
		dev_info(dev, "update clk mul to %u as gck rate is %u Hz\n",
			 clk_mul, real_gck_rate);
	}

	/*
	 * We have to set preset values because it depends on the clk_mul
	 * value. Moreover, SDR104 is supported in a degraded mode since the
	 * maximum sd clock value is 120 MHz instead of 208 MHz. For that
	 * reason, we need to use presets to support SDR104.
	 */
	preset_div = DIV_ROUND_UP(real_gck_rate, 24000000) - 1;
	writew(SDHCI_AT91_PRESET_COMMON_CONF | preset_div,
	       host->ioaddr + SDHCI_PRESET_FOR_SDR12);
	preset_div = DIV_ROUND_UP(real_gck_rate, 50000000) - 1;
	writew(SDHCI_AT91_PRESET_COMMON_CONF | preset_div,
	       host->ioaddr + SDHCI_PRESET_FOR_SDR25);
	preset_div = DIV_ROUND_UP(real_gck_rate, 100000000) - 1;
	writew(SDHCI_AT91_PRESET_COMMON_CONF | preset_div,
	       host->ioaddr + SDHCI_PRESET_FOR_SDR50);
	preset_div = DIV_ROUND_UP(real_gck_rate, 120000000) - 1;
	writew(SDHCI_AT91_PRESET_COMMON_CONF | preset_div,
	       host->ioaddr + SDHCI_PRESET_FOR_SDR104);
	preset_div = DIV_ROUND_UP(real_gck_rate, 50000000) - 1;
	writew(SDHCI_AT91_PRESET_COMMON_CONF | preset_div,
	       host->ioaddr + SDHCI_PRESET_FOR_DDR50);

	clk_prepare_enable(priv->mainck);
	clk_prepare_enable(priv->gck);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_at91_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_at91_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = pm_runtime_force_suspend(dev);

	priv->restore_needed = true;

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int sdhci_at91_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_at91_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = sdhci_runtime_suspend_host(host);

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	clk_disable_unprepare(priv->gck);
	clk_disable_unprepare(priv->hclock);
	clk_disable_unprepare(priv->mainck);

	return ret;
}

static int sdhci_at91_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_at91_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (priv->restore_needed) {
		ret = sdhci_at91_set_clks_presets(dev);
		if (ret)
			return ret;

		priv->restore_needed = false;
		goto out;
	}

	ret = clk_prepare_enable(priv->mainck);
	if (ret) {
		dev_err(dev, "can't enable mainck\n");
		return ret;
	}

	ret = clk_prepare_enable(priv->hclock);
	if (ret) {
		dev_err(dev, "can't enable hclock\n");
		return ret;
	}

	ret = clk_prepare_enable(priv->gck);
	if (ret) {
		dev_err(dev, "can't enable gck\n");
		return ret;
	}

out:
	return sdhci_runtime_resume_host(host);
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops sdhci_at91_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_at91_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(sdhci_at91_runtime_suspend,
			   sdhci_at91_runtime_resume,
			   NULL)
};

static int sdhci_at91_probe(struct platform_device *pdev)
{
	const struct of_device_id	*match;
	const struct sdhci_pltfm_data	*soc_data;
	struct sdhci_host		*host;
	struct sdhci_pltfm_host		*pltfm_host;
	struct sdhci_at91_priv		*priv;
	int				ret;

	match = of_match_device(sdhci_at91_dt_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	soc_data = match->data;

	host = sdhci_pltfm_init(pdev, soc_data, sizeof(*priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);

	priv->mainck = devm_clk_get(&pdev->dev, "baseclk");
	if (IS_ERR(priv->mainck)) {
		dev_err(&pdev->dev, "failed to get baseclk\n");
		return PTR_ERR(priv->mainck);
	}

	priv->hclock = devm_clk_get(&pdev->dev, "hclock");
	if (IS_ERR(priv->hclock)) {
		dev_err(&pdev->dev, "failed to get hclock\n");
		return PTR_ERR(priv->hclock);
	}

	priv->gck = devm_clk_get(&pdev->dev, "multclk");
	if (IS_ERR(priv->gck)) {
		dev_err(&pdev->dev, "failed to get multclk\n");
		return PTR_ERR(priv->gck);
	}

	ret = sdhci_at91_set_clks_presets(&pdev->dev);
	if (ret)
		goto sdhci_pltfm_free;

	priv->restore_needed = false;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto clocks_disable_unprepare;

	sdhci_get_of_property(pdev);

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);

	ret = sdhci_add_host(host);
	if (ret)
		goto pm_runtime_disable;

	/*
	 * When calling sdhci_runtime_suspend_host(), the sdhci layer makes
	 * the assumption that all the clocks of the controller are disabled.
	 * It means we can't get irq from it when it is runtime suspended.
	 * For that reason, it is not planned to wake-up on a card detect irq
	 * from the controller.
	 * If we want to use runtime PM and to be able to wake-up on card
	 * insertion, we have to use a GPIO for the card detection or we can
	 * use polling. Be aware that using polling will resume/suspend the
	 * controller between each attempt.
	 * Disable SDHCI_QUIRK_BROKEN_CARD_DETECTION to be sure nobody tries
	 * to enable polling via device tree with broken-cd property.
	 */
	if (mmc_card_is_removable(host->mmc) &&
	    mmc_gpio_get_cd(host->mmc) < 0) {
		host->mmc->caps |= MMC_CAP_NEEDS_POLL;
		host->quirks &= ~SDHCI_QUIRK_BROKEN_CARD_DETECTION;
	}

	/*
	 * If the device attached to the MMC bus is not removable, it is safer
	 * to set the Force Card Detect bit. People often don't connect the
	 * card detect signal and use this pin for another purpose. If the card
	 * detect pin is not muxed to SDHCI controller, a default value is
	 * used. This value can be different from a SoC revision to another
	 * one. Problems come when this default value is not card present. To
	 * avoid this case, if the device is non removable then the card
	 * detection procedure using the SDMCC_CD signal is bypassed.
	 * This bit is reset when a software reset for all command is performed
	 * so we need to implement our own reset function to set back this bit.
	 */
	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		sdhci_at91_set_force_card_detect(host);

	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
clocks_disable_unprepare:
	clk_disable_unprepare(priv->gck);
	clk_disable_unprepare(priv->mainck);
	clk_disable_unprepare(priv->hclock);
sdhci_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_at91_remove(struct platform_device *pdev)
{
	struct sdhci_host	*host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host	*pltfm_host = sdhci_priv(host);
	struct sdhci_at91_priv	*priv = sdhci_pltfm_priv(pltfm_host);
	struct clk *gck = priv->gck;
	struct clk *hclock = priv->hclock;
	struct clk *mainck = priv->mainck;

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(gck);
	clk_disable_unprepare(hclock);
	clk_disable_unprepare(mainck);

	return 0;
}

static struct platform_driver sdhci_at91_driver = {
	.driver		= {
		.name	= "sdhci-at91",
		.of_match_table = sdhci_at91_dt_match,
		.pm	= &sdhci_at91_dev_pm_ops,
	},
	.probe		= sdhci_at91_probe,
	.remove		= sdhci_at91_remove,
};

module_platform_driver(sdhci_at91_driver);

MODULE_DESCRIPTION("SDHCI driver for at91");
MODULE_AUTHOR("Ludovic Desroches <ludovic.desroches@atmel.com>");
MODULE_LICENSE("GPL v2");
