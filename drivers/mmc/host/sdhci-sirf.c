/*
 * SDHCI support for SiRF primaII and marco SoCs
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>
#include "sdhci-pltfm.h"

#define SDHCI_CLK_DELAY_SETTING 0x4C
#define SDHCI_SIRF_8BITBUS BIT(3)
#define SIRF_TUNING_COUNT 128

struct sdhci_sirf_priv {
	int gpio_cd;
};

static void sdhci_sirf_set_bus_width(struct sdhci_host *host, int width)
{
	u8 ctrl;

	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
	ctrl &= ~(SDHCI_CTRL_4BITBUS | SDHCI_SIRF_8BITBUS);

	/*
	 * CSR atlas7 and prima2 SD host version is not 3.0
	 * 8bit-width enable bit of CSR SD hosts is 3,
	 * while stardard hosts use bit 5
	 */
	if (width == MMC_BUS_WIDTH_8)
		ctrl |= SDHCI_SIRF_8BITBUS;
	else if (width == MMC_BUS_WIDTH_4)
		ctrl |= SDHCI_CTRL_4BITBUS;

	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
}

static int sdhci_sirf_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int tuning_seq_cnt = 3;
	u8 phase, tuned_phases[SIRF_TUNING_COUNT];
	u8 tuned_phase_cnt = 0;
	int rc = 0, longest_range = 0;
	int start = -1, end = 0, tuning_value = -1, range = 0;
	u16 clock_setting;
	struct mmc_host *mmc = host->mmc;

	clock_setting = sdhci_readw(host, SDHCI_CLK_DELAY_SETTING);
	clock_setting &= ~0x3fff;

retry:
	phase = 0;
	do {
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		if (!mmc_send_tuning(mmc)) {
			/* Tuning is successful at this tuning point */
			tuned_phases[tuned_phase_cnt++] = phase;
			dev_dbg(mmc_dev(mmc), "%s: Found good phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (start == -1)
				start = phase;
			end = phase;
			range++;
			if (phase == (SIRF_TUNING_COUNT - 1)
				&& range > longest_range)
				tuning_value = (start + end) / 2;
		} else {
			dev_dbg(mmc_dev(mmc), "%s: Found bad phase = %d\n",
				 mmc_hostname(mmc), phase);
			if (range > longest_range) {
				tuning_value = (start + end) / 2;
				longest_range = range;
			}
			start = -1;
			end = range = 0;
		}
	} while (++phase < ARRAY_SIZE(tuned_phases));

	if (tuned_phase_cnt && tuning_value > 0) {
		/*
		 * Finally set the selected phase in delay
		 * line hw block.
		 */
		phase = tuning_value;
		sdhci_writel(host,
			clock_setting | phase,
			SDHCI_CLK_DELAY_SETTING);

		dev_dbg(mmc_dev(mmc), "%s: Setting the tuning phase to %d\n",
			 mmc_hostname(mmc), phase);
	} else {
		if (--tuning_seq_cnt)
			goto retry;
		/* Tuning failed */
		dev_dbg(mmc_dev(mmc), "%s: No tuning point found\n",
		       mmc_hostname(mmc));
		rc = -EIO;
	}

	return rc;
}

static struct sdhci_ops sdhci_sirf_ops = {
	.platform_execute_tuning = sdhci_sirf_execute_tuning,
	.set_clock = sdhci_set_clock,
	.get_max_clock	= sdhci_pltfm_clk_get_max_clock,
	.set_bus_width = sdhci_sirf_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_sirf_pdata = {
	.ops = &sdhci_sirf_ops,
	.quirks = SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
		SDHCI_QUIRK_DELAY_AFTER_POWER,
};

static int sdhci_sirf_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_sirf_priv *priv;
	struct clk *clk;
	int gpio_cd;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get clock");
		return PTR_ERR(clk);
	}

	if (pdev->dev.of_node)
		gpio_cd = of_get_named_gpio(pdev->dev.of_node, "cd-gpios", 0);
	else
		gpio_cd = -EINVAL;

	host = sdhci_pltfm_init(pdev, &sdhci_sirf_pdata, sizeof(struct sdhci_sirf_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;
	priv = sdhci_pltfm_priv(pltfm_host);
	priv->gpio_cd = gpio_cd;

	sdhci_get_of_property(pdev);

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		goto err_clk_prepare;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_sdhci_add;

	/*
	 * We must request the IRQ after sdhci_add_host(), as the tasklet only
	 * gets setup in sdhci_add_host() and we oops.
	 */
	if (gpio_is_valid(priv->gpio_cd)) {
		ret = mmc_gpio_request_cd(host->mmc, priv->gpio_cd, 0);
		if (ret) {
			dev_err(&pdev->dev, "card detect irq request failed: %d\n",
				ret);
			goto err_request_cd;
		}
		mmc_gpiod_request_cd_irq(host->mmc);
	}

	return 0;

err_request_cd:
	sdhci_remove_host(host, 0);
err_sdhci_add:
	clk_disable_unprepare(pltfm_host->clk);
err_clk_prepare:
	sdhci_pltfm_free(pdev);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_sirf_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable(pltfm_host->clk);

	return 0;
}

static int sdhci_sirf_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret;

	ret = clk_enable(pltfm_host->clk);
	if (ret) {
		dev_dbg(dev, "Resume: Error enabling clock\n");
		return ret;
	}

	return sdhci_resume_host(host);
}

static SIMPLE_DEV_PM_OPS(sdhci_sirf_pm_ops, sdhci_sirf_suspend, sdhci_sirf_resume);
#endif

static const struct of_device_id sdhci_sirf_of_match[] = {
	{ .compatible = "sirf,prima2-sdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_sirf_of_match);

static struct platform_driver sdhci_sirf_driver = {
	.driver		= {
		.name	= "sdhci-sirf",
		.of_match_table = sdhci_sirf_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm	= &sdhci_sirf_pm_ops,
#endif
	},
	.probe		= sdhci_sirf_probe,
	.remove		= sdhci_pltfm_unregister,
};

module_platform_driver(sdhci_sirf_driver);

MODULE_DESCRIPTION("SDHCI driver for SiRFprimaII/SiRFmarco");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL v2");
