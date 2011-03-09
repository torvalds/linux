/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 * Copyright (c) 2009 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2009 Imre Kaloz <kaloz@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <ar231x_platform.h>
#include "ath5k.h"
#include "debug.h"
#include "base.h"
#include "reg.h"
#include "debug.h"

/* return bus cachesize in 4B word units */
static void ath5k_ahb_read_cachesize(struct ath_common *common, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

bool ath5k_ahb_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath5k_softc *sc = common->priv;
	struct platform_device *pdev = to_platform_device(sc->dev);
	struct ar231x_board_config *bcfg = pdev->dev.platform_data;
	u16 *eeprom, *eeprom_end;



	bcfg = pdev->dev.platform_data;
	eeprom = (u16 *) bcfg->radio;
	eeprom_end = ((void *) bcfg->config) + BOARD_CONFIG_BUFSZ;

	eeprom += off;
	if (eeprom > eeprom_end)
		return -EINVAL;

	*data = *eeprom;
	return 0;
}

int ath5k_hw_read_srev(struct ath5k_hw *ah)
{
	struct ath5k_softc *sc = ah->ah_sc;
	struct platform_device *pdev = to_platform_device(sc->dev);
	struct ar231x_board_config *bcfg = pdev->dev.platform_data;
	ah->ah_mac_srev = bcfg->devid;
	return 0;
}

static const struct ath_bus_ops ath_ahb_bus_ops = {
	.ath_bus_type = ATH_AHB,
	.read_cachesize = ath5k_ahb_read_cachesize,
	.eeprom_read = ath5k_ahb_eeprom_read,
};

/*Initialization*/
static int ath_ahb_probe(struct platform_device *pdev)
{
	struct ar231x_board_config *bcfg = pdev->dev.platform_data;
	struct ath5k_softc *sc;
	struct ieee80211_hw *hw;
	struct resource *res;
	void __iomem *mem;
	int irq;
	int ret = 0;
	u32 reg;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data specified\n");
		ret = -EINVAL;
		goto err_out;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource found\n");
		ret = -ENXIO;
		goto err_out;
	}

	mem = ioremap_nocache(res->start, res->end - res->start + 1);
	if (mem == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_out;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no IRQ resource found\n");
		ret = -ENXIO;
		goto err_out;
	}

	irq = res->start;

	hw = ieee80211_alloc_hw(sizeof(struct ath5k_softc), &ath5k_hw_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "no memory for ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_out;
	}

	sc = hw->priv;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->iobase = mem;
	sc->irq = irq;
	sc->devid = bcfg->devid;

	if (bcfg->devid >= AR5K_SREV_AR2315_R6) {
		/* Enable WMAC AHB arbitration */
		reg = __raw_readl((void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
		reg |= AR5K_AR2315_AHB_ARB_CTL_WLAN;
		__raw_writel(reg, (void __iomem *) AR5K_AR2315_AHB_ARB_CTL);

		/* Enable global WMAC swapping */
		reg = __raw_readl((void __iomem *) AR5K_AR2315_BYTESWAP);
		reg |= AR5K_AR2315_BYTESWAP_WMAC;
		__raw_writel(reg, (void __iomem *) AR5K_AR2315_BYTESWAP);
	} else {
		/* Enable WMAC DMA access (assuming 5312 or 231x*/
		/* TODO: check other platforms */
		reg = __raw_readl((void __iomem *) AR5K_AR5312_ENABLE);
		if (to_platform_device(sc->dev)->id == 0)
			reg |= AR5K_AR5312_ENABLE_WLAN0;
		else
			reg |= AR5K_AR5312_ENABLE_WLAN1;
		__raw_writel(reg, (void __iomem *) AR5K_AR5312_ENABLE);
	}

	ret = ath5k_init_softc(sc, &ath_ahb_bus_ops);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to attach device, err=%d\n", ret);
		ret = -ENODEV;
		goto err_free_hw;
	}

	platform_set_drvdata(pdev, hw);

	return 0;

 err_free_hw:
	ieee80211_free_hw(hw);
	platform_set_drvdata(pdev, NULL);
 err_out:
	return ret;
}

static int ath_ahb_remove(struct platform_device *pdev)
{
	struct ar231x_board_config *bcfg = pdev->dev.platform_data;
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct ath5k_softc *sc;
	u32 reg;

	if (!hw)
		return 0;

	sc = hw->priv;

	if (bcfg->devid >= AR5K_SREV_AR2315_R6) {
		/* Disable WMAC AHB arbitration */
		reg = __raw_readl((void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
		reg &= ~AR5K_AR2315_AHB_ARB_CTL_WLAN;
		__raw_writel(reg, (void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
	} else {
		/*Stop DMA access */
		reg = __raw_readl((void __iomem *) AR5K_AR5312_ENABLE);
		if (to_platform_device(sc->dev)->id == 0)
			reg &= ~AR5K_AR5312_ENABLE_WLAN0;
		else
			reg &= ~AR5K_AR5312_ENABLE_WLAN1;
		__raw_writel(reg, (void __iomem *) AR5K_AR5312_ENABLE);
	}

	ath5k_deinit_softc(sc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ath_ahb_driver = {
	.probe      = ath_ahb_probe,
	.remove     = ath_ahb_remove,
	.driver		= {
		.name	= "ar231x-wmac",
		.owner	= THIS_MODULE,
	},
};

static int __init
ath5k_ahb_init(void)
{
	return platform_driver_register(&ath_ahb_driver);
}

static void __exit
ath5k_ahb_exit(void)
{
	platform_driver_unregister(&ath_ahb_driver);
}

module_init(ath5k_ahb_init);
module_exit(ath5k_ahb_exit);
