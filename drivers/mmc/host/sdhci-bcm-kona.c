/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>

#include "sdhci-pltfm.h"
#include "sdhci.h"

#define SDHCI_SOFT_RESET			0x01000000
#define KONA_SDHOST_CORECTRL			0x8000
#define KONA_SDHOST_CD_PINCTRL			0x00000008
#define KONA_SDHOST_STOP_HCLK			0x00000004
#define KONA_SDHOST_RESET			0x00000002
#define KONA_SDHOST_EN				0x00000001

#define KONA_SDHOST_CORESTAT			0x8004
#define KONA_SDHOST_WP				0x00000002
#define KONA_SDHOST_CD_SW			0x00000001

#define KONA_SDHOST_COREIMR			0x8008
#define KONA_SDHOST_IP				0x00000001

#define KONA_SDHOST_COREISR			0x800C
#define KONA_SDHOST_COREIMSR			0x8010
#define KONA_SDHOST_COREDBG1			0x8014
#define KONA_SDHOST_COREGPO_MASK		0x8018

#define SD_DETECT_GPIO_DEBOUNCE_128MS		128

#define KONA_MMC_AUTOSUSPEND_DELAY		(50)

struct sdhci_bcm_kona_dev {
	struct mutex	write_lock; /* protect back to back writes */
};


static int sdhci_bcm_kona_sd_reset(struct sdhci_host *host)
{
	unsigned int val;
	unsigned long timeout;

	/* This timeout should be sufficent for core to reset */
	timeout = jiffies + msecs_to_jiffies(100);

	/* reset the host using the top level reset */
	val = sdhci_readl(host, KONA_SDHOST_CORECTRL);
	val |= KONA_SDHOST_RESET;
	sdhci_writel(host, val, KONA_SDHOST_CORECTRL);

	while (!(sdhci_readl(host, KONA_SDHOST_CORECTRL) & KONA_SDHOST_RESET)) {
		if (time_is_before_jiffies(timeout)) {
			pr_err("Error: sd host is stuck in reset!!!\n");
			return -EFAULT;
		}
	}

	/* bring the host out of reset */
	val = sdhci_readl(host, KONA_SDHOST_CORECTRL);
	val &= ~KONA_SDHOST_RESET;

	/*
	 * Back-to-Back register write needs a delay of 1ms at bootup (min 10uS)
	 * Back-to-Back writes to same register needs delay when SD bus clock
	 * is very low w.r.t AHB clock, mainly during boot-time and during card
	 * insert-removal.
	 */
	usleep_range(1000, 5000);
	sdhci_writel(host, val, KONA_SDHOST_CORECTRL);

	return 0;
}

static void sdhci_bcm_kona_sd_init(struct sdhci_host *host)
{
	unsigned int val;

	/* enable the interrupt from the IP core */
	val = sdhci_readl(host, KONA_SDHOST_COREIMR);
	val |= KONA_SDHOST_IP;
	sdhci_writel(host, val, KONA_SDHOST_COREIMR);

	/* Enable the AHB clock gating module to the host */
	val = sdhci_readl(host, KONA_SDHOST_CORECTRL);
	val |= KONA_SDHOST_EN;

	/*
	 * Back-to-Back register write needs a delay of 1ms at bootup (min 10uS)
	 * Back-to-Back writes to same register needs delay when SD bus clock
	 * is very low w.r.t AHB clock, mainly during boot-time and during card
	 * insert-removal.
	 */
	usleep_range(1000, 5000);
	sdhci_writel(host, val, KONA_SDHOST_CORECTRL);
}

/*
 * Software emulation of the SD card insertion/removal. Set insert=1 for insert
 * and insert=0 for removal. The card detection is done by GPIO. For Broadcom
 * IP to function properly the bit 0 of CORESTAT register needs to be set/reset
 * to generate the CD IRQ handled in sdhci.c which schedules card_tasklet.
 */
static int sdhci_bcm_kona_sd_card_emulate(struct sdhci_host *host, int insert)
{
	struct sdhci_pltfm_host *pltfm_priv = sdhci_priv(host);
	struct sdhci_bcm_kona_dev *kona_dev = sdhci_pltfm_priv(pltfm_priv);
	u32 val;

	/*
	 * Back-to-Back register write needs a delay of min 10uS.
	 * Back-to-Back writes to same register needs delay when SD bus clock
	 * is very low w.r.t AHB clock, mainly during boot-time and during card
	 * insert-removal.
	 * We keep 20uS
	 */
	mutex_lock(&kona_dev->write_lock);
	udelay(20);
	val = sdhci_readl(host, KONA_SDHOST_CORESTAT);

	if (insert) {
		int ret;

		ret = mmc_gpio_get_ro(host->mmc);
		if (ret >= 0)
			val = (val & ~KONA_SDHOST_WP) |
				((ret) ? KONA_SDHOST_WP : 0);

		val |= KONA_SDHOST_CD_SW;
		sdhci_writel(host, val, KONA_SDHOST_CORESTAT);
	} else {
		val &= ~KONA_SDHOST_CD_SW;
		sdhci_writel(host, val, KONA_SDHOST_CORESTAT);
	}
	mutex_unlock(&kona_dev->write_lock);

	return 0;
}

/*
 * SD card interrupt event callback
 */
static void sdhci_bcm_kona_card_event(struct sdhci_host *host)
{
	if (mmc_gpio_get_cd(host->mmc) > 0) {
		dev_dbg(mmc_dev(host->mmc),
			"card inserted\n");
		sdhci_bcm_kona_sd_card_emulate(host, 1);
	} else {
		dev_dbg(mmc_dev(host->mmc),
			"card removed\n");
		sdhci_bcm_kona_sd_card_emulate(host, 0);
	}
}

static void sdhci_bcm_kona_init_74_clocks(struct sdhci_host *host,
				u8 power_mode)
{
	/*
	 *  JEDEC and SD spec specify supplying 74 continuous clocks to
	 * device after power up. With minimum bus (100KHz) that
	 * that translates to 740us
	 */
	if (power_mode != MMC_POWER_OFF)
		udelay(740);
}

static struct sdhci_ops sdhci_bcm_kona_ops = {
	.set_clock = sdhci_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_pltfm_clk_get_max_clock,
	.platform_send_init_74_clocks = sdhci_bcm_kona_init_74_clocks,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.card_event = sdhci_bcm_kona_card_event,
};

static struct sdhci_pltfm_data sdhci_pltfm_data_kona = {
	.ops    = &sdhci_bcm_kona_ops,
	.quirks = SDHCI_QUIRK_NO_CARD_NO_RESET |
		SDHCI_QUIRK_BROKEN_TIMEOUT_VAL | SDHCI_QUIRK_32BIT_DMA_ADDR |
		SDHCI_QUIRK_32BIT_DMA_SIZE | SDHCI_QUIRK_32BIT_ADMA_SIZE |
		SDHCI_QUIRK_FORCE_BLK_SZ_2048 |
		SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
};

static const struct of_device_id sdhci_bcm_kona_of_match[] = {
	{ .compatible = "brcm,kona-sdhci"},
	{ .compatible = "bcm,kona-sdhci"}, /* deprecated name */
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_bcm_kona_of_match);

static int sdhci_bcm_kona_probe(struct platform_device *pdev)
{
	struct sdhci_bcm_kona_dev *kona_dev = NULL;
	struct sdhci_pltfm_host *pltfm_priv;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host;
	int ret;

	ret = 0;

	host = sdhci_pltfm_init(pdev, &sdhci_pltfm_data_kona,
			sizeof(*kona_dev));
	if (IS_ERR(host))
		return PTR_ERR(host);

	dev_dbg(dev, "%s: inited. IOADDR=%p\n", __func__, host->ioaddr);

	pltfm_priv = sdhci_priv(host);

	kona_dev = sdhci_pltfm_priv(pltfm_priv);
	mutex_init(&kona_dev->write_lock);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_pltfm_free;

	if (!host->mmc->f_max) {
		dev_err(&pdev->dev, "Missing max-freq for SDHCI cfg\n");
		ret = -ENXIO;
		goto err_pltfm_free;
	}

	/* Get and enable the core clock */
	pltfm_priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pltfm_priv->clk)) {
		dev_err(dev, "Failed to get core clock\n");
		ret = PTR_ERR(pltfm_priv->clk);
		goto err_pltfm_free;
	}

	if (clk_set_rate(pltfm_priv->clk, host->mmc->f_max) != 0) {
		dev_err(dev, "Failed to set rate core clock\n");
		goto err_pltfm_free;
	}

	if (clk_prepare_enable(pltfm_priv->clk) != 0) {
		dev_err(dev, "Failed to enable core clock\n");
		goto err_pltfm_free;
	}

	dev_dbg(dev, "non-removable=%c\n",
		(host->mmc->caps & MMC_CAP_NONREMOVABLE) ? 'Y' : 'N');
	dev_dbg(dev, "cd_gpio %c, wp_gpio %c\n",
		(mmc_gpio_get_cd(host->mmc) != -ENOSYS) ? 'Y' : 'N',
		(mmc_gpio_get_ro(host->mmc) != -ENOSYS) ? 'Y' : 'N');

	if (host->mmc->caps & MMC_CAP_NONREMOVABLE)
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

	dev_dbg(dev, "is_8bit=%c\n",
		(host->mmc->caps | MMC_CAP_8_BIT_DATA) ? 'Y' : 'N');

	ret = sdhci_bcm_kona_sd_reset(host);
	if (ret)
		goto err_clk_disable;

	sdhci_bcm_kona_sd_init(host);

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(dev, "Failed sdhci_add_host\n");
		goto err_reset;
	}

	/* if device is eMMC, emulate card insert right here */
	if (host->mmc->caps & MMC_CAP_NONREMOVABLE) {
		ret = sdhci_bcm_kona_sd_card_emulate(host, 1);
		if (ret) {
			dev_err(dev,
				"unable to emulate card insertion\n");
			goto err_remove_host;
		}
	}
	/*
	 * Since the card detection GPIO interrupt is configured to be
	 * edge sensitive, check the initial GPIO value here, emulate
	 * only if the card is present
	 */
	if (mmc_gpio_get_cd(host->mmc) > 0)
		sdhci_bcm_kona_sd_card_emulate(host, 1);

	dev_dbg(dev, "initialized properly\n");
	return 0;

err_remove_host:
	sdhci_remove_host(host, 0);

err_reset:
	sdhci_bcm_kona_sd_reset(host);

err_clk_disable:
	clk_disable_unprepare(pltfm_priv->clk);

err_pltfm_free:
	sdhci_pltfm_free(pdev);

	dev_err(dev, "Probing of sdhci-pltfm failed: %d\n", ret);
	return ret;
}

static int sdhci_bcm_kona_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static struct platform_driver sdhci_bcm_kona_driver = {
	.driver		= {
		.name	= "sdhci-kona",
		.pm	= SDHCI_PLTFM_PMOPS,
		.of_match_table = sdhci_bcm_kona_of_match,
	},
	.probe		= sdhci_bcm_kona_probe,
	.remove		= sdhci_bcm_kona_remove,
};
module_platform_driver(sdhci_bcm_kona_driver);

MODULE_DESCRIPTION("SDHCI driver for Broadcom Kona platform");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
