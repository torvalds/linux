/*
 * Copyright (c) 2008 Atheros Communications Inc.
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
#include "core.h"
#include "reg.h"
#include "hw.h"

/* return bus cachesize in 4B word units */
static void ath_ahb_read_cachesize(struct ath_softc *sc, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

static void ath_ahb_cleanup(struct ath_softc *sc)
{
	iounmap(sc->mem);
}

static struct ath_bus_ops ath_ahb_bus_ops  = {
	.read_cachesize = ath_ahb_read_cachesize,
	.cleanup = ath_ahb_cleanup,
};

static int ath_ahb_probe(struct platform_device *pdev)
{
	void __iomem *mem;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	struct resource *res;
	int irq;
	int ret = 0;
	struct ath_hal *ah;

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
		goto err_iounmap;
	}

	irq = res->start;

	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "no memory for ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_iounmap;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	platform_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->mem = mem;
	sc->bus_ops = &ath_ahb_bus_ops;
	sc->irq = irq;

	ret = ath_attach(AR5416_AR9100_DEVID, sc);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to attach device, err=%d\n", ret);
		ret = -ENODEV;
		goto err_free_hw;
	}

	ret = request_irq(irq, ath_isr, IRQF_SHARED, "ath9k", sc);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed, err=%d\n", ret);
		ret = -EIO;
		goto err_detach;
	}

	ah = sc->sc_ah;
	printk(KERN_INFO
	       "%s: Atheros AR%s MAC/BB Rev:%x, "
	       "AR%s RF Rev:%x, mem=0x%lx, irq=%d\n",
	       wiphy_name(hw->wiphy),
	       ath_mac_bb_name(ah->ah_macVersion),
	       ah->ah_macRev,
	       ath_rf_name((ah->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR)),
	       ah->ah_phyRev,
	       (unsigned long)mem, irq);

	return 0;

 err_detach:
	ath_detach(sc);
 err_free_hw:
	ieee80211_free_hw(hw);
	platform_set_drvdata(pdev, NULL);
 err_iounmap:
	iounmap(mem);
 err_out:
	return ret;
}

static int ath_ahb_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);

	if (hw) {
		struct ath_softc *sc = hw->priv;

		ath_cleanup(sc);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

static struct platform_driver ath_ahb_driver = {
	.probe      = ath_ahb_probe,
	.remove     = ath_ahb_remove,
	.driver		= {
		.name	= "ath9k",
		.owner	= THIS_MODULE,
	},
};

int ath_ahb_init(void)
{
	return platform_driver_register(&ath_ahb_driver);
}

void ath_ahb_exit(void)
{
	platform_driver_unregister(&ath_ahb_driver);
}
