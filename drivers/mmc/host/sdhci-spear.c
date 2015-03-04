/*
 * drivers/mmc/host/sdhci-spear.c
 *
 * Support of SDHCI platform devices for spear soc family
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * Inspired by sdhci-pltfm.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci-spear.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/io.h>
#include "sdhci.h"

struct spear_sdhci {
	struct clk *clk;
	struct sdhci_plat_data *data;
};

/* sdhci ops */
static const struct sdhci_ops sdhci_pltfm_ops = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_plat_data *sdhci_probe_config_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_plat_data *pdata = NULL;
	int cd_gpio;

	cd_gpio = of_get_named_gpio(np, "cd-gpios", 0);
	if (!gpio_is_valid(cd_gpio))
		cd_gpio = -1;

	/* If pdata is required */
	if (cd_gpio != -1) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			dev_err(&pdev->dev, "DT: kzalloc failed\n");
		else
			pdata->card_int_gpio = cd_gpio;
	}

	return pdata;
}

static int sdhci_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct resource *iomem;
	struct spear_sdhci *sdhci;
	struct device *dev;
	int ret;

	dev = pdev->dev.parent ? pdev->dev.parent : &pdev->dev;
	host = sdhci_alloc_host(dev, sizeof(*sdhci));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		dev_dbg(&pdev->dev, "cannot allocate memory for sdhci\n");
		goto err;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->ioaddr = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(host->ioaddr)) {
		ret = PTR_ERR(host->ioaddr);
		dev_dbg(&pdev->dev, "unable to map iomem: %d\n", ret);
		goto err_host;
	}

	host->hw_name = "sdhci";
	host->ops = &sdhci_pltfm_ops;
	host->irq = platform_get_irq(pdev, 0);
	host->quirks = SDHCI_QUIRK_BROKEN_ADMA;

	sdhci = sdhci_priv(host);

	/* clk enable */
	sdhci->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sdhci->clk)) {
		ret = PTR_ERR(sdhci->clk);
		dev_dbg(&pdev->dev, "Error getting clock\n");
		goto err_host;
	}

	ret = clk_prepare_enable(sdhci->clk);
	if (ret) {
		dev_dbg(&pdev->dev, "Error enabling clock\n");
		goto err_host;
	}

	ret = clk_set_rate(sdhci->clk, 50000000);
	if (ret)
		dev_dbg(&pdev->dev, "Error setting desired clk, clk=%lu\n",
				clk_get_rate(sdhci->clk));

	sdhci->data = sdhci_probe_config_dt(pdev);
	if (IS_ERR(sdhci->data)) {
		dev_err(&pdev->dev, "DT: Failed to get pdata\n");
		goto disable_clk;
	}

	/*
	 * It is optional to use GPIOs for sdhci card detection. If
	 * sdhci->data is NULL, then use original sdhci lines otherwise
	 * GPIO lines. We use the built-in GPIO support for this.
	 */
	if (sdhci->data && sdhci->data->card_int_gpio >= 0) {
		ret = mmc_gpio_request_cd(host->mmc,
					  sdhci->data->card_int_gpio, 0);
		if (ret < 0) {
			dev_dbg(&pdev->dev,
				"failed to request card-detect gpio%d\n",
				sdhci->data->card_int_gpio);
			goto disable_clk;
		}
	}

	ret = sdhci_add_host(host);
	if (ret) {
		dev_dbg(&pdev->dev, "error adding host\n");
		goto disable_clk;
	}

	platform_set_drvdata(pdev, host);

	return 0;

disable_clk:
	clk_disable_unprepare(sdhci->clk);
err_host:
	sdhci_free_host(host);
err:
	dev_err(&pdev->dev, "spear-sdhci probe failed: %d\n", ret);
	return ret;
}

static int sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct spear_sdhci *sdhci = sdhci_priv(host);
	int dead = 0;
	u32 scratch;

	scratch = readl(host->ioaddr + SDHCI_INT_STATUS);
	if (scratch == (u32)-1)
		dead = 1;

	sdhci_remove_host(host, dead);
	clk_disable_unprepare(sdhci->clk);
	sdhci_free_host(host);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spear_sdhci *sdhci = sdhci_priv(host);
	int ret;

	ret = sdhci_suspend_host(host);
	if (!ret)
		clk_disable(sdhci->clk);

	return ret;
}

static int sdhci_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spear_sdhci *sdhci = sdhci_priv(host);
	int ret;

	ret = clk_enable(sdhci->clk);
	if (ret) {
		dev_dbg(dev, "Resume: Error enabling clock\n");
		return ret;
	}

	return sdhci_resume_host(host);
}
#endif

static SIMPLE_DEV_PM_OPS(sdhci_pm_ops, sdhci_suspend, sdhci_resume);

#ifdef CONFIG_OF
static const struct of_device_id sdhci_spear_id_table[] = {
	{ .compatible = "st,spear300-sdhci" },
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_spear_id_table);
#endif

static struct platform_driver sdhci_driver = {
	.driver = {
		.name	= "sdhci",
		.pm	= &sdhci_pm_ops,
		.of_match_table = of_match_ptr(sdhci_spear_id_table),
	},
	.probe		= sdhci_probe,
	.remove		= sdhci_remove,
};

module_platform_driver(sdhci_driver);

MODULE_DESCRIPTION("SPEAr Secure Digital Host Controller Interface driver");
MODULE_AUTHOR("Viresh Kumar <viresh.linux@gmail.com>");
MODULE_LICENSE("GPL v2");
