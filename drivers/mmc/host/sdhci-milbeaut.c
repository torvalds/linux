// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 - 2015 Fujitsu Semiconductor, Ltd
 *              Vincent Yang <vincent.yang@tw.fujitsu.com>
 * Copyright (C) 2015 Linaro Ltd  Andy Green <andy.green@linaro.org>
 * Copyright (C) 2019 Socionext Inc.
 *              Takao Orito <orito.takao@socionext.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>

#include "sdhci-pltfm.h"
#include "sdhci_f_sdh30.h"

/* milbeaut bridge controller register */
#define MLB_SOFT_RESET		0x0200
#define  MLB_SOFT_RESET_RSTX		BIT(0)

#define MLB_WP_CD_LED_SET	0x0210
#define  MLB_WP_CD_LED_SET_LED_INV  BIT(2)

#define MLB_CR_SET			0x0220
#define  MLB_CR_SET_CR_TOCLKUNIT       BIT(24)
#define  MLB_CR_SET_CR_TOCLKFREQ_SFT   (16)
#define  MLB_CR_SET_CR_TOCLKFREQ_MASK  (0x3F << MLB_CR_SET_CR_TOCLKFREQ_SFT)
#define  MLB_CR_SET_CR_BCLKFREQ_SFT    (8)
#define  MLB_CR_SET_CR_BCLKFREQ_MASK   (0xFF << MLB_CR_SET_CR_BCLKFREQ_SFT)
#define  MLB_CR_SET_CR_RTUNTIMER_SFT   (4)
#define  MLB_CR_SET_CR_RTUNTIMER_MASK  (0xF << MLB_CR_SET_CR_RTUNTIMER_SFT)

#define MLB_SD_TOCLK_I_DIV  16
#define MLB_TOCLKFREQ_UNIT_THRES    16000000
#define MLB_CAL_TOCLKFREQ_MHZ(rate) (rate / MLB_SD_TOCLK_I_DIV / 1000000)
#define MLB_CAL_TOCLKFREQ_KHZ(rate) (rate / MLB_SD_TOCLK_I_DIV / 1000)
#define MLB_TOCLKFREQ_MAX   63
#define MLB_TOCLKFREQ_MIN    1

#define MLB_SD_BCLK_I_DIV   4
#define MLB_CAL_BCLKFREQ(rate)  (rate / MLB_SD_BCLK_I_DIV / 1000000)
#define MLB_BCLKFREQ_MAX        255
#define MLB_BCLKFREQ_MIN          1

#define MLB_CDR_SET			0x0230
#define MLB_CDR_SET_CLK2POW16	3

struct f_sdhost_priv {
	struct clk *clk_iface;
	struct clk *clk;
	struct device *dev;
	bool enable_cmd_dat_delay;
};

static void sdhci_milbeaut_soft_voltage_switch(struct sdhci_host *host)
{
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

	ctrl = sdhci_readl(host, F_SDH30_TUNING_SETTING);
	ctrl |= F_SDH30_CMD_CHK_DIS;
	sdhci_writel(host, ctrl, F_SDH30_TUNING_SETTING);
}

static unsigned int sdhci_milbeaut_get_min_clock(struct sdhci_host *host)
{
	return F_SDH30_MIN_CLOCK;
}

static void sdhci_milbeaut_reset(struct sdhci_host *host, u8 mask)
{
	struct f_sdhost_priv *priv = sdhci_priv(host);
	u16 clk;
	u32 ctl;
	ktime_t timeout;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk = (clk & ~SDHCI_CLOCK_CARD_EN) | SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	sdhci_reset(host, mask);

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	timeout = ktime_add_ms(ktime_get(), 10);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		if (clk & SDHCI_CLOCK_INT_STABLE)
			break;
		if (timedout) {
			pr_err("%s: Internal clock never stabilised.\n",
				mmc_hostname(host->mmc));
			sdhci_dumpregs(host);
			return;
		}
		udelay(10);
	}

	if (priv->enable_cmd_dat_delay) {
		ctl = sdhci_readl(host, F_SDH30_ESD_CONTROL);
		ctl |= F_SDH30_CMD_DAT_DELAY;
		sdhci_writel(host, ctl, F_SDH30_ESD_CONTROL);
	}
}

static const struct sdhci_ops sdhci_milbeaut_ops = {
	.voltage_switch = sdhci_milbeaut_soft_voltage_switch,
	.get_min_clock = sdhci_milbeaut_get_min_clock,
	.reset = sdhci_milbeaut_reset,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.set_power = sdhci_set_power_and_bus_voltage,
};

static void sdhci_milbeaut_bridge_reset(struct sdhci_host *host,
						int reset_flag)
{
	if (reset_flag)
		sdhci_writel(host, 0, MLB_SOFT_RESET);
	else
		sdhci_writel(host, MLB_SOFT_RESET_RSTX, MLB_SOFT_RESET);
}

static void sdhci_milbeaut_bridge_init(struct sdhci_host *host,
						int rate)
{
	u32 val, clk;

	/* IO_SDIO_CR_SET should be set while reset */
	val = sdhci_readl(host, MLB_CR_SET);
	val &= ~(MLB_CR_SET_CR_TOCLKFREQ_MASK | MLB_CR_SET_CR_TOCLKUNIT |
			MLB_CR_SET_CR_BCLKFREQ_MASK);
	if (rate >= MLB_TOCLKFREQ_UNIT_THRES) {
		clk = MLB_CAL_TOCLKFREQ_MHZ(rate);
		clk = min_t(u32, MLB_TOCLKFREQ_MAX, clk);
		val |= MLB_CR_SET_CR_TOCLKUNIT |
			(clk << MLB_CR_SET_CR_TOCLKFREQ_SFT);
	} else {
		clk = MLB_CAL_TOCLKFREQ_KHZ(rate);
		clk = min_t(u32, MLB_TOCLKFREQ_MAX, clk);
		clk = max_t(u32, MLB_TOCLKFREQ_MIN, clk);
		val |= clk << MLB_CR_SET_CR_TOCLKFREQ_SFT;
	}

	clk = MLB_CAL_BCLKFREQ(rate);
	clk = min_t(u32, MLB_BCLKFREQ_MAX, clk);
	clk = max_t(u32, MLB_BCLKFREQ_MIN, clk);
	val |=  clk << MLB_CR_SET_CR_BCLKFREQ_SFT;
	val &= ~MLB_CR_SET_CR_RTUNTIMER_MASK;
	sdhci_writel(host, val, MLB_CR_SET);

	sdhci_writel(host, MLB_CDR_SET_CLK2POW16, MLB_CDR_SET);

	sdhci_writel(host, MLB_WP_CD_LED_SET_LED_INV, MLB_WP_CD_LED_SET);
}

static void sdhci_milbeaut_vendor_init(struct sdhci_host *host)
{
	struct f_sdhost_priv *priv = sdhci_priv(host);
	u32 ctl;

	ctl = sdhci_readl(host, F_SDH30_IO_CONTROL2);
	ctl |= F_SDH30_CRES_O_DN;
	sdhci_writel(host, ctl, F_SDH30_IO_CONTROL2);
	ctl &= ~F_SDH30_MSEL_O_1_8;
	sdhci_writel(host, ctl, F_SDH30_IO_CONTROL2);
	ctl &= ~F_SDH30_CRES_O_DN;
	sdhci_writel(host, ctl, F_SDH30_IO_CONTROL2);

	ctl = sdhci_readw(host, F_SDH30_AHB_CONFIG);
	ctl |= F_SDH30_SIN | F_SDH30_AHB_INCR_16 | F_SDH30_AHB_INCR_8 |
	       F_SDH30_AHB_INCR_4;
	ctl &= ~(F_SDH30_AHB_BIGED | F_SDH30_BUSLOCK_EN);
	sdhci_writew(host, ctl, F_SDH30_AHB_CONFIG);

	if (priv->enable_cmd_dat_delay) {
		ctl = sdhci_readl(host, F_SDH30_ESD_CONTROL);
		ctl |= F_SDH30_CMD_DAT_DELAY;
		sdhci_writel(host, ctl, F_SDH30_ESD_CONTROL);
	}
}

static const struct of_device_id mlb_dt_ids[] = {
	{
		.compatible = "socionext,milbeaut-m10v-sdhci-3.0",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mlb_dt_ids);

static void sdhci_milbeaut_init(struct sdhci_host *host)
{
	struct f_sdhost_priv *priv = sdhci_priv(host);
	int rate = clk_get_rate(priv->clk);
	u16 ctl;

	sdhci_milbeaut_bridge_reset(host, 0);

	ctl = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	ctl &= ~(SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN);
	sdhci_writew(host, ctl, SDHCI_CLOCK_CONTROL);

	sdhci_milbeaut_bridge_reset(host, 1);

	sdhci_milbeaut_bridge_init(host, rate);
	sdhci_milbeaut_bridge_reset(host, 0);

	sdhci_milbeaut_vendor_init(host);
}

static int sdhci_milbeaut_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct device *dev = &pdev->dev;
	int irq, ret = 0;
	struct f_sdhost_priv *priv;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	host = sdhci_alloc_host(dev, sizeof(struct f_sdhost_priv));
	if (IS_ERR(host))
		return PTR_ERR(host);

	priv = sdhci_priv(host);
	priv->dev = dev;

	host->quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
			   SDHCI_QUIRK_INVERTED_WRITE_PROTECT |
			   SDHCI_QUIRK_CLOCK_BEFORE_RESET |
			   SDHCI_QUIRK_DELAY_AFTER_POWER;
	host->quirks2 = SDHCI_QUIRK2_SUPPORT_SINGLE |
			SDHCI_QUIRK2_TUNING_WORK_AROUND |
			SDHCI_QUIRK2_PRESET_VALUE_BROKEN;

	priv->enable_cmd_dat_delay = device_property_read_bool(dev,
						"fujitsu,cmd-dat-delay-select");

	ret = mmc_of_parse(host->mmc);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, host);

	host->hw_name = "f_sdh30";
	host->ops = &sdhci_milbeaut_ops;
	host->irq = irq;

	host->ioaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->ioaddr))
		return PTR_ERR(host->ioaddr);

	if (dev_of_node(dev)) {
		sdhci_get_of_property(pdev);

		priv->clk_iface = devm_clk_get(&pdev->dev, "iface");
		if (IS_ERR(priv->clk_iface))
			return PTR_ERR(priv->clk_iface);

		ret = clk_prepare_enable(priv->clk_iface);
		if (ret)
			return ret;

		priv->clk = devm_clk_get(&pdev->dev, "core");
		if (IS_ERR(priv->clk)) {
			ret = PTR_ERR(priv->clk);
			goto err_clk;
		}

		ret = clk_prepare_enable(priv->clk);
		if (ret)
			goto err_clk;
	}

	sdhci_milbeaut_init(host);

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	clk_disable_unprepare(priv->clk);
err_clk:
	clk_disable_unprepare(priv->clk_iface);
	return ret;
}

static void sdhci_milbeaut_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct f_sdhost_priv *priv = sdhci_priv(host);

	sdhci_remove_host(host, readl(host->ioaddr + SDHCI_INT_STATUS) ==
			  0xffffffff);

	clk_disable_unprepare(priv->clk_iface);
	clk_disable_unprepare(priv->clk);

	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver sdhci_milbeaut_driver = {
	.driver = {
		.name = "sdhci-milbeaut",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = mlb_dt_ids,
	},
	.probe	= sdhci_milbeaut_probe,
	.remove = sdhci_milbeaut_remove,
};

module_platform_driver(sdhci_milbeaut_driver);

MODULE_DESCRIPTION("MILBEAUT SD Card Controller driver");
MODULE_AUTHOR("Takao Orito <orito.takao@socionext.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sdhci-milbeaut");
