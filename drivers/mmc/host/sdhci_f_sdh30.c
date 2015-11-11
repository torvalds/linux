/*
 * linux/drivers/mmc/host/sdhci_f_sdh30.c
 *
 * Copyright (C) 2013 - 2015 Fujitsu Semiconductor, Ltd
 *              Vincent Yang <vincent.yang@tw.fujitsu.com>
 * Copyright (C) 2015 Linaro Ltd  Andy Green <andy.green@linaro.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/clk.h>

#include "sdhci-pltfm.h"

/* F_SDH30 extended Controller registers */
#define F_SDH30_AHB_CONFIG		0x100
#define  F_SDH30_AHB_BIGED		0x00000040
#define  F_SDH30_BUSLOCK_DMA		0x00000020
#define  F_SDH30_BUSLOCK_EN		0x00000010
#define  F_SDH30_SIN			0x00000008
#define  F_SDH30_AHB_INCR_16		0x00000004
#define  F_SDH30_AHB_INCR_8		0x00000002
#define  F_SDH30_AHB_INCR_4		0x00000001

#define F_SDH30_TUNING_SETTING		0x108
#define  F_SDH30_CMD_CHK_DIS		0x00010000

#define F_SDH30_IO_CONTROL2		0x114
#define  F_SDH30_CRES_O_DN		0x00080000
#define  F_SDH30_MSEL_O_1_8		0x00040000

#define F_SDH30_ESD_CONTROL		0x124
#define  F_SDH30_EMMC_RST		0x00000002
#define  F_SDH30_EMMC_HS200		0x01000000

#define F_SDH30_CMD_DAT_DELAY		0x200

#define F_SDH30_MIN_CLOCK		400000

struct f_sdhost_priv {
	struct clk *clk_iface;
	struct clk *clk;
	u32 vendor_hs200;
	struct device *dev;
};

static void sdhci_f_sdh30_soft_voltage_switch(struct sdhci_host *host)
{
	struct f_sdhost_priv *priv = sdhci_priv(host);
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
	if (sdhci_readw(host, SDHCI_CLOCK_CONTROL) == 0)
		sdhci_writew(host, 0xBC01, SDHCI_CLOCK_CONTROL);

	sdhci_reset(host, mask);
}

static const struct sdhci_ops sdhci_f_sdh30_ops = {
	.voltage_switch = sdhci_f_sdh30_soft_voltage_switch,
	.get_min_clock = sdhci_f_sdh30_get_min_clock,
	.reset = sdhci_f_sdh30_reset,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static int sdhci_f_sdh30_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, ctrl = 0, ret = 0;
	struct f_sdhost_priv *priv;
	u32 reg = 0;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "%s: no irq specified\n", __func__);
		return irq;
	}

	host = sdhci_alloc_host(dev, sizeof(struct f_sdhost_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	priv = sdhci_priv(host);
	priv->dev = dev;

	host->quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		       SDHCI_QUIRK_INVERTED_WRITE_PROTECT;
	host->quirks2 = SDHCI_QUIRK2_SUPPORT_SINGLE |
			SDHCI_QUIRK2_TUNING_WORK_AROUND;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, host);

	sdhci_get_of_property(pdev);
	host->hw_name = "f_sdh30";
	host->ops = &sdhci_f_sdh30_ops;
	host->irq = irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->ioaddr)) {
		ret = PTR_ERR(host->ioaddr);
		goto err;
	}

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

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	clk_disable_unprepare(priv->clk);
err_clk:
	clk_disable_unprepare(priv->clk_iface);
err:
	sdhci_free_host(host);
	return ret;
}

static int sdhci_f_sdh30_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct f_sdhost_priv *priv = sdhci_priv(host);

	sdhci_remove_host(host, readl(host->ioaddr + SDHCI_INT_STATUS) ==
			  0xffffffff);

	clk_disable_unprepare(priv->clk_iface);
	clk_disable_unprepare(priv->clk);

	sdhci_free_host(host);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id f_sdh30_dt_ids[] = {
	{ .compatible = "fujitsu,mb86s70-sdhci-3.0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, f_sdh30_dt_ids);

static struct platform_driver sdhci_f_sdh30_driver = {
	.driver = {
		.name = "f_sdh30",
		.of_match_table = f_sdh30_dt_ids,
		.pm	= SDHCI_PLTFM_PMOPS,
	},
	.probe	= sdhci_f_sdh30_probe,
	.remove	= sdhci_f_sdh30_remove,
};

module_platform_driver(sdhci_f_sdh30_driver);

MODULE_DESCRIPTION("F_SDH30 SD Card Controller driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("FUJITSU SEMICONDUCTOR LTD.");
MODULE_ALIAS("platform:f_sdh30");
