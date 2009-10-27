/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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
#include "ath9k.h"

static struct pci_device_id ath_pci_id_table[] __devinitdata = {
	{ PCI_VDEVICE(ATHEROS, 0x0023) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0024) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0027) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0029) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002A) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002B) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002D) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002E) }, /* PCI-E */
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
	 * This check was put in to avoid "unplesant" consequences if
	 * the bootrom has not fully initialized all PCI devices.
	 * Sometimes the cache line size register is not set
	 */

	if (*csz == 0)
		*csz = DEFAULT_CACHELINE >> 2;   /* Use the default size */
}

static void ath_pci_cleanup(struct ath_common *common)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct pci_dev *pdev = to_pci_dev(sc->dev);

	pci_iounmap(pdev, sc->mem);
	pci_disable_device(pdev);
	pci_release_region(pdev, 0);
}

static bool ath_pci_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath_hw *ah = (struct ath_hw *) common->ah;

	common->ops->read(ah, AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!ath9k_hw_wait(ah,
			   AR_EEPROM_STATUS_DATA,
			   AR_EEPROM_STATUS_DATA_BUSY |
			   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0,
			   AH_WAIT_TIMEOUT)) {
		return false;
	}

	*data = MS(common->ops->read(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;
}

/*
 * Bluetooth coexistance requires disabling ASPM.
 */
static void ath_pci_bt_coex_prep(struct ath_common *common)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	struct pci_dev *pdev = to_pci_dev(sc->dev);
	u8 aspm;

	if (!pdev->is_pcie)
		return;

	pci_read_config_byte(pdev, ATH_PCIE_CAP_LINK_CTRL, &aspm);
	aspm &= ~(ATH_PCIE_CAP_LINK_L0S | ATH_PCIE_CAP_LINK_L1);
	pci_write_config_byte(pdev, ATH_PCIE_CAP_LINK_CTRL, aspm);
}

const static struct ath_bus_ops ath_pci_bus_ops = {
	.read_cachesize = ath_pci_read_cachesize,
	.cleanup = ath_pci_cleanup,
	.eeprom_read = ath_pci_eeprom_read,
	.bt_coex_prep = ath_pci_bt_coex_prep,
};

static int ath_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath_wiphy *aphy;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	u8 csz;
	u16 subsysid;
	u32 val;
	int ret = 0;
	struct ath_hw *ah;

	if (pci_enable_device(pdev))
		return -EIO;

	ret =  pci_set_dma_mask(pdev, DMA_BIT_MASK(32));

	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA not available\n");
		goto bad;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));

	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA consistent "
			"DMA enable failed\n");
		goto bad;
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
		goto bad;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		printk(KERN_ERR "PCI memory map error\n") ;
		ret = -EIO;
		goto bad1;
	}

	hw = ieee80211_alloc_hw(sizeof(struct ath_wiphy) +
				sizeof(struct ath_softc), &ath9k_ops);
	if (!hw) {
		dev_err(&pdev->dev, "no memory for ieee80211_hw\n");
		ret = -ENOMEM;
		goto bad2;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	aphy = hw->priv;
	sc = (struct ath_softc *) (aphy + 1);
	aphy->sc = sc;
	aphy->hw = hw;
	sc->pri_wiphy = aphy;
	sc->hw = hw;
	sc->dev = &pdev->dev;
	sc->mem = mem;

	pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &subsysid);
	ret = ath_init_device(id->device, sc, subsysid, &ath_pci_bus_ops);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize device\n");
		goto bad3;
	}

	/* setup interrupt service routine */

	ret = request_irq(pdev->irq, ath_isr, IRQF_SHARED, "ath9k", sc);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto bad4;
	}

	sc->irq = pdev->irq;

	ah = sc->sc_ah;
	printk(KERN_INFO
	       "%s: Atheros AR%s MAC/BB Rev:%x "
	       "AR%s RF Rev:%x: mem=0x%lx, irq=%d\n",
	       wiphy_name(hw->wiphy),
	       ath9k_hw_mac_bb_name(ah->hw_version.macVersion),
	       ah->hw_version.macRev,
	       ath9k_hw_rf_name((ah->hw_version.analog5GhzRev &
				 AR_RADIO_SREV_MAJOR)),
	       ah->hw_version.phyRev,
	       (unsigned long)mem, pdev->irq);

	return 0;
bad4:
	ath_detach(sc);
bad3:
	ieee80211_free_hw(hw);
bad2:
	pci_iounmap(pdev, mem);
bad1:
	pci_release_region(pdev, 0);
bad:
	pci_disable_device(pdev);
	return ret;
}

static void ath_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	ath_cleanup(sc);
}

#ifdef CONFIG_PM

static int ath_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int ath_pci_resume(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	u32 val;
	int err;

	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	/* Enable LED */
	ath9k_hw_cfg_output(sc->sc_ah, sc->sc_ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);

	return 0;
}

#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(pci, ath_pci_id_table);

static struct pci_driver ath_pci_driver = {
	.name       = "ath9k",
	.id_table   = ath_pci_id_table,
	.probe      = ath_pci_probe,
	.remove     = ath_pci_remove,
#ifdef CONFIG_PM
	.suspend    = ath_pci_suspend,
	.resume     = ath_pci_resume,
#endif /* CONFIG_PM */
};

int ath_pci_init(void)
{
	return pci_register_driver(&ath_pci_driver);
}

void ath_pci_exit(void)
{
	pci_unregister_driver(&ath_pci_driver);
}
