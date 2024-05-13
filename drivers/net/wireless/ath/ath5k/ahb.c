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

#include <linux/module.h>
#include <linux/nl80211.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <ath25_platform.h>
#include "ath5k.h"
#include "debug.h"
#include "base.h"
#include "reg.h"

/* return bus cachesize in 4B word units */
static void ath5k_ahb_read_cachesize(struct ath_common *common, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

static bool
ath5k_ahb_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath5k_hw *ah = common->priv;
	struct platform_device *pdev = to_platform_device(ah->dev);
	struct ar231x_board_config *bcfg = dev_get_platdata(&pdev->dev);
	u16 *eeprom, *eeprom_end;

	eeprom = (u16 *) bcfg->radio;
	eeprom_end = ((void *) bcfg->config) + BOARD_CONFIG_BUFSZ;

	eeprom += off;
	if (eeprom > eeprom_end)
		return false;

	*data = *eeprom;
	return true;
}

int ath5k_hw_read_srev(struct ath5k_hw *ah)
{
	struct platform_device *pdev = to_platform_device(ah->dev);
	struct ar231x_board_config *bcfg = dev_get_platdata(&pdev->dev);
	ah->ah_mac_srev = bcfg->devid;
	return 0;
}

static int ath5k_ahb_eeprom_read_mac(struct ath5k_hw *ah, u8 *mac)
{
	struct platform_device *pdev = to_platform_device(ah->dev);
	struct ar231x_board_config *bcfg = dev_get_platdata(&pdev->dev);
	u8 *cfg_mac;

	if (to_platform_device(ah->dev)->id == 0)
		cfg_mac = bcfg->config->wlan0_mac;
	else
		cfg_mac = bcfg->config->wlan1_mac;

	memcpy(mac, cfg_mac, ETH_ALEN);
	return 0;
}

static const struct ath_bus_ops ath_ahb_bus_ops = {
	.ath_bus_type = ATH_AHB,
	.read_cachesize = ath5k_ahb_read_cachesize,
	.eeprom_read = ath5k_ahb_eeprom_read,
	.eeprom_read_mac = ath5k_ahb_eeprom_read_mac,
};

/*Initialization*/
static int ath_ahb_probe(struct platform_device *pdev)
{
	struct ar231x_board_config *bcfg = dev_get_platdata(&pdev->dev);
	struct ath5k_hw *ah;
	struct ieee80211_hw *hw;
	struct resource *res;
	void __iomem *mem;
	int irq;
	int ret = 0;
	u32 reg;

	if (!dev_get_platdata(&pdev->dev)) {
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

	mem = ioremap(res->start, resource_size(res));
	if (mem == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_iounmap;
	}

	hw = ieee80211_alloc_hw(sizeof(struct ath5k_hw), &ath5k_hw_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "no memory for ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_iounmap;
	}

	ah = hw->priv;
	ah->hw = hw;
	ah->dev = &pdev->dev;
	ah->iobase = mem;
	ah->irq = irq;
	ah->devid = bcfg->devid;

	if (bcfg->devid >= AR5K_SREV_AR2315_R6) {
		/* Enable WMAC AHB arbitration */
		reg = ioread32((void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
		reg |= AR5K_AR2315_AHB_ARB_CTL_WLAN;
		iowrite32(reg, (void __iomem *) AR5K_AR2315_AHB_ARB_CTL);

		/* Enable global WMAC swapping */
		reg = ioread32((void __iomem *) AR5K_AR2315_BYTESWAP);
		reg |= AR5K_AR2315_BYTESWAP_WMAC;
		iowrite32(reg, (void __iomem *) AR5K_AR2315_BYTESWAP);
	} else {
		/* Enable WMAC DMA access (assuming 5312 or 231x*/
		/* TODO: check other platforms */
		reg = ioread32((void __iomem *) AR5K_AR5312_ENABLE);
		if (to_platform_device(ah->dev)->id == 0)
			reg |= AR5K_AR5312_ENABLE_WLAN0;
		else
			reg |= AR5K_AR5312_ENABLE_WLAN1;
		iowrite32(reg, (void __iomem *) AR5K_AR5312_ENABLE);

		/*
		 * On a dual-band AR5312, the multiband radio is only
		 * used as pass-through. Disable 2 GHz support in the
		 * driver for it
		 */
		if (to_platform_device(ah->dev)->id == 0 &&
		    (bcfg->config->flags & (BD_WLAN0 | BD_WLAN1)) ==
		     (BD_WLAN1 | BD_WLAN0))
			ah->ah_capabilities.cap_needs_2GHz_ovr = true;
		else
			ah->ah_capabilities.cap_needs_2GHz_ovr = false;
	}

	ret = ath5k_init_ah(ah, &ath_ahb_bus_ops);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to attach device, err=%d\n", ret);
		ret = -ENODEV;
		goto err_free_hw;
	}

	platform_set_drvdata(pdev, hw);

	return 0;

 err_free_hw:
	ieee80211_free_hw(hw);
 err_iounmap:
        iounmap(mem);
 err_out:
	return ret;
}

static int ath_ahb_remove(struct platform_device *pdev)
{
	struct ar231x_board_config *bcfg = dev_get_platdata(&pdev->dev);
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct ath5k_hw *ah;
	u32 reg;

	if (!hw)
		return 0;

	ah = hw->priv;

	if (bcfg->devid >= AR5K_SREV_AR2315_R6) {
		/* Disable WMAC AHB arbitration */
		reg = ioread32((void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
		reg &= ~AR5K_AR2315_AHB_ARB_CTL_WLAN;
		iowrite32(reg, (void __iomem *) AR5K_AR2315_AHB_ARB_CTL);
	} else {
		/*Stop DMA access */
		reg = ioread32((void __iomem *) AR5K_AR5312_ENABLE);
		if (to_platform_device(ah->dev)->id == 0)
			reg &= ~AR5K_AR5312_ENABLE_WLAN0;
		else
			reg &= ~AR5K_AR5312_ENABLE_WLAN1;
		iowrite32(reg, (void __iomem *) AR5K_AR5312_ENABLE);
	}

	ath5k_deinit_ah(ah);
	iounmap(ah->iobase);
	ieee80211_free_hw(hw);

	return 0;
}

static struct platform_driver ath_ahb_driver = {
	.probe      = ath_ahb_probe,
	.remove     = ath_ahb_remove,
	.driver		= {
		.name	= "ar231x-wmac",
	},
};

module_platform_driver(ath_ahb_driver);
