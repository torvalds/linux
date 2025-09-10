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

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nl80211.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "ath9k.h"

static const struct of_device_id ath9k_of_match_table[] = {
	{ .compatible = "qca,ar9130-wifi", .data = (void *)AR5416_AR9100_DEVID },
	{ .compatible = "qca,ar9330-wifi", .data = (void *)AR9300_DEVID_AR9330 },
	{ .compatible = "qca,ar9340-wifi", .data = (void *)AR9300_DEVID_AR9340 },
	{ .compatible = "qca,qca9530-wifi", .data = (void *)AR9300_DEVID_AR953X },
	{ .compatible = "qca,qca9550-wifi", .data = (void *)AR9300_DEVID_QCA955X },
	{ .compatible = "qca,qca9560-wifi", .data = (void *)AR9300_DEVID_QCA956X },
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
	struct ieee80211_hw *hw;
	struct ath_softc *sc;
	struct ath_hw *ah;
	void __iomem *mem;
	char hw_name[64];
	u16 dev_id;
	int irq;
	int ret;

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem)) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return PTR_ERR(mem);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

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

	dev_id = (u16)(kernel_ulong_t)of_device_get_match_data(&pdev->dev);
	ret = ath9k_init_device(dev_id, sc, &ath_ahb_bus_ops);
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

static void ath_ahb_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);

	if (hw) {
		struct ath_softc *sc = hw->priv;

		ath9k_deinit_device(sc);
		free_irq(sc->irq, sc);
		ieee80211_free_hw(sc->hw);
	}
}

static struct platform_driver ath_ahb_driver = {
	.probe = ath_ahb_probe,
	.remove = ath_ahb_remove,
	.driver = {
		.name = "ath9k",
		.of_match_table = ath9k_of_match_table,
	},
};

MODULE_DEVICE_TABLE(of, ath9k_of_match_table);

int ath_ahb_init(void)
{
	return platform_driver_register(&ath_ahb_driver);
}

void ath_ahb_exit(void)
{
	platform_driver_unregister(&ath_ahb_driver);
}
