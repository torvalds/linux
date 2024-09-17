// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/mmc/host/sdhci_f_sdh30.c
 *
 * Copyright (C) 2013 - 2015 Fujitsu Semiconductor, Ltd
 *              Vincent Yang <vincent.yang@tw.fujitsu.com>
 * Copyright (C) 2015 Linaro Ltd  Andy Green <andy.green@linaro.org>
 */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/clk.h>

#include "sdhci-pltfm.h"
#include "sdhci_f_sdh30.h"

struct f_sdhost_priv {
	struct clk *clk_iface;
	struct clk *clk;
	u32 vendor_hs200;
	struct device *dev;
	bool enable_cmd_dat_delay;
};

static void *sdhci_f_sdhost_priv(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return sdhci_pltfm_priv(pltfm_host);
}

static void sdhci_f_sdh30_soft_voltage_switch(struct sdhci_host *host)
{
	struct f_sdhost_priv *priv = sdhci_f_sdhost_priv(host);
	u32 ctrl = 0;

	usleep_range(2500, 3000);
	ctrl = sdhci_readl(host, F_SDH30_IO_CONTROL2);
	ctrl |= F_SDH30_CRES_O_DN;
	sdhci_writel(host, ctrl, F_SDH30_IO_CONTROL2);
	ctrl |= F_SDH30_MSEL_O_1_8;
	sdhci_writel(host, ctrl, F_SDH30_IO_CONTROL2);

	ctrl &= ~F_SDH30_CRES_O_DN;
	sdhci_writel(host, ctrl, F_SDH30_IO_CONTROL2);
	usleep_range(2500, 3000);

	if (priv->vendor_hs200) {
		dev_info(priv->dev, "%s: setting hs200\n", __func__);
		ctrl = sdhci_readl(host, F_SDH30_ESD_CONTROL);
		ctrl |= priv->vendor_hs200;
		sdhci_writel(host, ctrl, F_SDH30_ESD_CONTROL);
	}

	ctrl = sdhci_readl(host, F_SDH30_TUNING_SETTING);
	ctrl |= F_SDH30_CMD_CHK_DIS;
	sdhci_writel(host, ctrl, F_SDH30_TUNING_SETTING);
}

static unsigned int sdhci_f_sdh30_get_min_clock(struct sdhci_host *host)
{
	return F_SDH30_MIN_CLOCK;
}

static void sdhci_f_sdh30_reset(struct sdhci_host *host, u8 mask)
{
	struct f_sdhost_priv *priv = sdhci_f_sdhost_priv(host);
	u32 ctl;

	if (sdhci_readw(host, SDHCI_CLOCK_CONTROL) == 0)
		sdhci_writew(host, 0xBC01, SDHCI_CLOCK_CONTROL);

	sdhci_reset(host, mask);

	if (priv->enable_cmd_dat_delay) {
		ctl = sdhci_readl(host, F_SDH30_ESD_CONTROL);
		ctl |= F_SDH30_CMD_DAT_DELAY;
		sdhci_writel(host, ctl, F_SDH30_ESD_CONTROL);
	}
}

static const struct sdhci_ops sdhci_f_sdh30_ops = {
	.voltage_switch = sdhci_f_sdh30_soft_voltage_switch,
	.get_min_clock = sdhci_f_sdh30_get_min_clock,
	.reset = sdhci_f_sdh30_reset,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_f_sdh30_pltfm_data = {
	.ops = &sdhci_f_sdh30_ops,
	.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC
		| SDHCI_QUIRK_INVERTED_WRITE_PROTECT,
	.quirks2 = SDHCI_QUIRK2_SUPPORT_SINGLE
		|  SDHCI_QUIRK2_TUNING_WORK_AROUND,
};

static int sdhci_f_sdh30_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct device *dev = &pdev->dev;
	int ctrl = 0, ret = 0;
	struct f_sdhost_priv *priv;
	struct sdhci_pltfm_host *pltfm_host;
	u32 reg = 0;

	host = sdhci_pltfm_init(pdev, &sdhci_f_sdh30_pltfm_data,
				sizeof(struct f_sdhost_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);
	priv->dev = dev;

	priv->enable_cmd_dat_delay = device_property_read_bool(dev,
						"fujitsu,cmd-dat-delay-select");

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err;

	if (dev_of_node(dev)) {
		sdhci_get_of_property(pdev);

		priv->clk_iface = devm_clk_get(&pdev->dev, "iface");
		if (IS_ERR(priv->clk_iface)) {
			ret = PTR_ERR(priv->clk_iface);
			goto err;
		}

		ret = clk_prepare_enable(priv->clk_iface);
		if (ret)
			goto err;

		priv->clk = devm_clk_get(&pdev->dev, "core");
		if (IS_ERR(priv->clk)) {
			ret = PTR_ERR(priv->clk);
			goto err_clk;
		}

		ret = clk_prepare_enable(priv->clk);
		if (ret)
			goto err_clk;
	}

	/* init vendor specific regs */
	ctrl = sdhci_readw(host, F_SDH30_AHB_CONFIG);
	ctrl |= F_SDH30_SIN | F_SDH30_AHB_INCR_16 | F_SDH30_AHB_INCR_8 |
		F_SDH30_AHB_INCR_4;
	ctrl &= ~(F_SDH30_AHB_BIGED | F_SDH30_BUSLOCK_EN);
	sdhci_writew(host, ctrl, F_SDH30_AHB_CONFIG);

	reg = sdhci_readl(host, F_SDH30_ESD_CONTROL);
	sdhci_writel(host, reg & ~F_SDH30_EMMC_RST, F_SDH30_ESD_CONTROL);
	msleep(20);
	sdhci_writel(host, reg | F_SDH30_EMMC_RST, F_SDH30_ESD_CONTROL);

	reg = sdhci_readl(host, SDHCI_CAPABILITIES);
	if (reg & SDHCI_CAN_DO_8BIT)
		priv->vendor_hs200 = F_SDH30_EMMC_HS200;

	if (!(reg & SDHCI_TIMEOUT_CLK_MASK))
		host->quirks |= SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	clk_disable_unprepare(priv->clk);
err_clk:
	clk_disable_unprepare(priv->clk_iface);
err:
	sdhci_pltfm_free(pdev);

	return ret;
}

static int sdhci_f_sdh30_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct f_sdhost_priv *priv = sdhci_f_sdhost_priv(host);
	struct clk *clk_iface = priv->clk_iface;
	struct clk *clk = priv->clk;

	sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(clk_iface);
	clk_disable_unprepare(clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id f_sdh30_dt_ids[] = {
	{ .compatible = "fujitsu,mb86s70-sdhci-3.0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, f_sdh30_dt_ids);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id f_sdh30_acpi_ids[] = {
	{ "SCX0002" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(acpi, f_sdh30_acpi_ids);
#endif

static struct platform_driver sdhci_f_sdh30_driver = {
	.driver = {
		.name = "f_sdh30",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(f_sdh30_dt_ids),
		.acpi_match_table = ACPI_PTR(f_sdh30_acpi_ids),
		.pm	= &sdhci_pltfm_pmops,
	},
	.probe	= sdhci_f_sdh30_probe,
	.remove	= sdhci_f_sdh30_remove,
};

module_platform_driver(sdhci_f_sdh30_driver);

MODULE_DESCRIPTION("F_SDH30 SD Card Controller driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("FUJITSU SEMICONDUCTOR LTD.");
MODULE_ALIAS("platform:f_sdh30");
