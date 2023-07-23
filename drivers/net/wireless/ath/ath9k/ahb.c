/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include "ath9k.h"

static const struct platform_device_id ath9k_platform_id_table[] = {
	{
		.name = "ath9k",
		.driver_data = AR5416_AR9100_DEVID,
	},
	{
		.name = "ar933x_wmac",
		.driver_data = AR9300_DEVID_AR9330,
	},
	{
		.name = "ar934x_wmac",
		.driver_data = AR9300_DEVID_AR9340,
	},
	{
		.name = "qca955x_wmac",
		.driver_data = AR9300_DEVID_QCA955X,
	},
	{
		.name = "qca953x_wmac",
		.driver_data = AR9300_DEVID_AR953X,
	},
	{
		.name = "qca956x_wmac",
		.driver_data = AR9300_DEVID_QCA956X,
	},
	{},
};

/* return bus cachesize in 4B word units */
static void ath_ahb_read_cachesize(struct ath_common *common, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

static bool ath_ahb_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	ath_err(common, "%s: eeprom data has to be provided externally\n",
		__func__);
	return false;
}

static const struct ath_bus_ops ath_ahb_bus_ops  = {
	.ath_bus_type = ATH_AHB,
	.read_cachesize = ath_ahb_read_cachesize,
	.eeprom_read = ath_ahb_eeprom_read,
};

static int ath_ahb_probe(struct platform_device *pdev)
{
	void __iomem *mem;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	struct resource *res;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	int irq;
	int ret = 0;
	struct ath_hw *ah;
	char hw_name[64];

	if (!dev_get_platdata(&pdev->dev)) {
		dev_err(&pdev->dev, "no platform data specified\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no memory resource found\n");
		return -ENXIO;
	}

	mem = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (mem == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no IRQ resource found\n");
		return -ENXIO;
	}

	irq = res->start;

	ath9k_fill_chanctx_ops();
	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "no memory for ieee80211_hw\n");
		return -ENOMEM;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	platform_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->mem = mem;
	sc->irq = irq;

	ret = request_irq(irq, ath_isr, IRQF_SHARED, "ath9k", sc);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_free_hw;
	}

	ret = ath9k_init_device(id->driver_data, sc, &ath_ahb_bus_ops);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize device\n");
		goto err_irq;
	}

	ah = sc->sc_ah;
	ath9k_hw_name(ah, hw_name, sizeof(hw_name));
	wiphy_info(hw->wiphy, "%s mem=0x%p, irq=%d\n",
		   hw_name, mem, irq);

	return 0;

 err_irq:
	free_irq(irq, sc);
 err_free_hw:
	ieee80211_free_hw(hw);
	return ret;
}

static int ath_ahb_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);

	if (hw) {
		struct ath_softc *sc = hw->priv;

		ath9k_deinit_device(sc);
		free_irq(sc->irq, sc);
		ieee80211_free_hw(sc->hw);
	}

	return 0;
}

static struct platform_driver ath_ahb_driver = {
	.probe      = ath_ahb_probe,
	.remove     = ath_ahb_remove,
	.driver		= {
		.name	= "ath9k",
	},
	.id_table    = ath9k_platform_id_table,
};

MODULE_DEVICE_TABLE(platform, ath9k_platform_id_table);

int ath_ahb_init(void)
{
	return platform_driver_register(&ath_ahb_driver);
}

void ath_ahb_exit(void)
{
	platform_driver_unregister(&ath_ahb_driver);
}
