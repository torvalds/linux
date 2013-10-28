/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/ath9k_platform.h>
#include <linux/module.h>
#include "ath9k.h"

static DEFINE_PCI_DEVICE_TABLE(ath_pci_id_table) = {
	{ PCI_VDEVICE(ATHEROS, 0x0023) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0024) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0027) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0029) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002A) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002B) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002C) }, /* PCI-E 802.11n bonded out */
	{ PCI_VDEVICE(ATHEROS, 0x002D) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002E) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0030) }, /* PCI-E  AR9300 */
	{ PCI_VDEVICE(ATHEROS, 0x0032) }, /* PCI-E  AR9485 */
	{ PCI_VDEVICE(ATHEROS, 0x0033) }, /* PCI-E  AR9580 */
	{ PCI_VDEVICE(ATHEROS, 0x0034) }, /* PCI-E  AR9462 */
	{ PCI_VDEVICE(ATHEROS, 0x0037) }, /* PCI-E  AR1111/AR9485 */
	{ 0 }
};


/* return bus cachesize in 4B word units */
static void ath_pci_read_cachesize(struct ath_common *common, int *csz)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	u8 u8tmp;

	pci_read_config_byte(to_pci_dev(sc->dev), PCI_CACHE_LINE_SIZE, &u8tmp);
	*csz = (int)u8tmp;

	/*
	 * This check was put in to avoid "unpleasant" consequences if
	 * the bootrom has not fully initialized all PCI devices.
	 * Sometimes the cache line size register is not set
	 */

	if (*csz == 0)
		*csz = DEFAULT_CACHELINE >> 2;   /* Use the default size */
}

static bool ath_pci_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct ath9k_platform_data *pdata = sc->dev->platform_data;

	if (pdata) {
		if (off >= (ARRAY_SIZE(pdata->eeprom_data))) {
			ath_err(common,
				"%s: eeprom read failed, offset %08x is out of range\n",
				__func__, off);
		}

		*data = pdata->eeprom_data[off];
	} else {
		struct ath_hw *ah = (struct ath_hw *) common->ah;

		common->ops->read(ah, AR5416_EEPROM_OFFSET +
				      (off << AR5416_EEPROM_S));

		if (!ath9k_hw_wait(ah,
				   AR_EEPROM_STATUS_DATA,
				   AR_EEPROM_STATUS_DATA_BUSY |
				   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0,
				   AH_WAIT_TIMEOUT)) {
			return false;
		}

		*data = MS(common->ops->read(ah, AR_EEPROM_STATUS_DATA),
			   AR_EEPROM_STATUS_DATA_VAL);
	}

	return true;
}

static void ath_pci_extn_synch_enable(struct ath_common *common)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct pci_dev *pdev = to_pci_dev(sc->dev);
	u8 lnkctl;

	pci_read_config_byte(pdev, sc->sc_ah->caps.pcie_lcr_offset, &lnkctl);
	lnkctl |= PCI_EXP_LNKCTL_ES;
	pci_write_config_byte(pdev, sc->sc_ah->caps.pcie_lcr_offset, lnkctl);
}

/* Need to be called after we discover btcoex capabilities */
static void ath_pci_aspm_init(struct ath_common *common)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct pci_dev *pdev = to_pci_dev(sc->dev);
	struct pci_dev *parent;
	int pos;
	u8 aspm;

	pos = pci_pcie_cap(pdev);
	if (!pos)
		return;

	parent = pdev->bus->self;
	if (!parent)
		return;

	if (ath9k_hw_get_btcoex_scheme(ah) != ATH_BTCOEX_CFG_NONE) {
		/* Bluetooth coexistance requires disabling ASPM. */
		pci_read_config_byte(pdev, pos + PCI_EXP_LNKCTL, &aspm);
		aspm &= ~(PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
		pci_write_config_byte(pdev, pos + PCI_EXP_LNKCTL, aspm);

		/*
		 * Both upstream and downstream PCIe components should
		 * have the same ASPM settings.
		 */
		pos = pci_pcie_cap(parent);
		pci_read_config_byte(parent, pos + PCI_EXP_LNKCTL, &aspm);
		aspm &= ~(PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
		pci_write_config_byte(parent, pos + PCI_EXP_LNKCTL, aspm);

		return;
	}

	pos = pci_pcie_cap(parent);
	pci_read_config_byte(parent, pos +  PCI_EXP_LNKCTL, &aspm);
	if (aspm & (PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1)) {
		ah->aspm_enabled = true;
		/* Initialize PCIe PM and SERDES registers. */
		ath9k_hw_configpcipowersave(ah, false);
	}
}

static const struct ath_bus_ops ath_pci_bus_ops = {
	.ath_bus_type = ATH_PCI,
	.read_cachesize = ath_pci_read_cachesize,
	.eeprom_read = ath_pci_eeprom_read,
	.extn_synch_en = ath_pci_extn_synch_enable,
	.aspm_init = ath_pci_aspm_init,
};

static int ath_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	u8 csz;
	u32 val;
	int ret = 0;
	char hw_name[64];

	if (pci_enable_device(pdev))
		return -EIO;

	ret =  pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA not available\n");
		goto err_dma;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA consistent "
			"DMA enable failed\n");
		goto err_dma;
	}

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &csz);
	if (csz == 0) {
		/*
		 * Linux 2.4.18 (at least) writes the cache line size
		 * register as a 16-bit wide register which is wrong.
		 * We must have this setup properly for rx buffer
		 * DMA to work so force a reasonable value here if it
		 * comes up zero.
		 */
		csz = L1_CACHE_BYTES / sizeof(u32);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems. It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	ret = pci_request_region(pdev, 0, "ath9k");
	if (ret) {
		dev_err(&pdev->dev, "PCI memory region reserve error\n");
		ret = -ENODEV;
		goto err_region;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		printk(KERN_ERR "PCI memory map error\n") ;
		ret = -EIO;
		goto err_iomap;
	}

	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (!hw) {
		dev_err(&pdev->dev, "No memory for ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_alloc_hw;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->mem = mem;

	/* Will be cleared in ath9k_start() */
	sc->sc_flags |= SC_OP_INVALID;

	ret = request_irq(pdev->irq, ath_isr, IRQF_SHARED, "ath9k", sc);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto err_irq;
	}

	sc->irq = pdev->irq;

	ret = ath9k_init_device(id->device, sc, &ath_pci_bus_ops);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize device\n");
		goto err_init;
	}

	ath9k_hw_name(sc->sc_ah, hw_name, sizeof(hw_name));
	wiphy_info(hw->wiphy, "%s mem=0x%lx, irq=%d\n",
		   hw_name, (unsigned long)mem, pdev->irq);

	return 0;

err_init:
	free_irq(sc->irq, sc);
err_irq:
	ieee80211_free_hw(hw);
err_alloc_hw:
	pci_iounmap(pdev, mem);
err_iomap:
	pci_release_region(pdev, 0);
err_region:
	/* Nothing */
err_dma:
	pci_disable_device(pdev);
	return ret;
}

static void ath_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;
	void __iomem *mem = sc->mem;

	if (!is_ath9k_unloaded)
		sc->sc_ah->ah_flags |= AH_UNPLUGGED;
	ath9k_deinit_device(sc);
	free_irq(sc->irq, sc);
	ieee80211_free_hw(sc->hw);

	pci_iounmap(pdev, mem);
	pci_disable_device(pdev);
	pci_release_region(pdev, 0);
}

#ifdef CONFIG_PM

static int ath_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;

	/* The device has to be moved to FULLSLEEP forcibly.
	 * Otherwise the chip never moved to full sleep,
	 * when no interface is up.
	 */
	ath9k_hw_disable(sc->sc_ah);
	ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_FULL_SLEEP);

	return 0;
}

static int ath_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	u32 val;

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	return 0;
}

static const struct dev_pm_ops ath9k_pm_ops = {
	.suspend = ath_pci_suspend,
	.resume = ath_pci_resume,
	.freeze = ath_pci_suspend,
	.thaw = ath_pci_resume,
	.poweroff = ath_pci_suspend,
	.restore = ath_pci_resume,
};

#define ATH9K_PM_OPS	(&ath9k_pm_ops)

#else /* !CONFIG_PM */

#define ATH9K_PM_OPS	NULL

#endif /* !CONFIG_PM */


MODULE_DEVICE_TABLE(pci, ath_pci_id_table);

static struct pci_driver ath_pci_driver = {
	.name       = "ath9k",
	.id_table   = ath_pci_id_table,
	.probe      = ath_pci_probe,
	.remove     = ath_pci_remove,
	.driver.pm  = ATH9K_PM_OPS,
};

int ath_pci_init(void)
{
	return pci_register_driver(&ath_pci_driver);
}

void ath_pci_exit(void)
{
	pci_unregister_driver(&ath_pci_driver);
}
