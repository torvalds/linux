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
#include <linux/pci-aspm.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include "../ath.h"
#include "ath5k.h"
#include "debug.h"
#include "base.h"
#include "reg.h"

/* Known PCI ids */
static DEFINE_PCI_DEVICE_TABLE(ath5k_pci_id_table) = {
	{ PCI_VDEVICE(ATHEROS, 0x0207) }, /* 5210 early */
	{ PCI_VDEVICE(ATHEROS, 0x0007) }, /* 5210 */
	{ PCI_VDEVICE(ATHEROS, 0x0011) }, /* 5311 - this is on AHB bus !*/
	{ PCI_VDEVICE(ATHEROS, 0x0012) }, /* 5211 */
	{ PCI_VDEVICE(ATHEROS, 0x0013) }, /* 5212 */
	{ PCI_VDEVICE(3COM_2,  0x0013) }, /* 3com 5212 */
	{ PCI_VDEVICE(3COM,    0x0013) }, /* 3com 3CRDAG675 5212 */
	{ PCI_VDEVICE(ATHEROS, 0x1014) }, /* IBM minipci 5212 */
	{ PCI_VDEVICE(ATHEROS, 0x0014) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x0015) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x0016) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x0017) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x0018) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x0019) }, /* 5212 compatible */
	{ PCI_VDEVICE(ATHEROS, 0x001a) }, /* 2413 Griffin-lite */
	{ PCI_VDEVICE(ATHEROS, 0x001b) }, /* 5413 Eagle */
	{ PCI_VDEVICE(ATHEROS, 0x001c) }, /* PCI-E cards */
	{ PCI_VDEVICE(ATHEROS, 0x001d) }, /* 2417 Nala */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ath5k_pci_id_table);

/* return bus cachesize in 4B word units */
static void ath5k_pci_read_cachesize(struct ath_common *common, int *csz)
{
	struct ath5k_hw *ah = (struct ath5k_hw *) common->priv;
	u8 u8tmp;

	pci_read_config_byte(ah->pdev, PCI_CACHE_LINE_SIZE, &u8tmp);
	*csz = (int)u8tmp;

	/*
	 * This check was put in to avoid "unpleasant" consequences if
	 * the bootrom has not fully initialized all PCI devices.
	 * Sometimes the cache line size register is not set
	 */

	if (*csz == 0)
		*csz = L1_CACHE_BYTES >> 2;   /* Use the default size */
}

/*
 * Read from eeprom
 */
static bool
ath5k_pci_eeprom_read(struct ath_common *common, u32 offset, u16 *data)
{
	struct ath5k_hw *ah = (struct ath5k_hw *) common->ah;
	u32 status, timeout;

	/*
	 * Initialize EEPROM access
	 */
	if (ah->ah_version == AR5K_AR5210) {
		AR5K_REG_ENABLE_BITS(ah, AR5K_PCICFG, AR5K_PCICFG_EEAE);
		(void)ath5k_hw_reg_read(ah, AR5K_EEPROM_BASE + (4 * offset));
	} else {
		ath5k_hw_reg_write(ah, offset, AR5K_EEPROM_BASE);
		AR5K_REG_ENABLE_BITS(ah, AR5K_EEPROM_CMD,
				AR5K_EEPROM_CMD_READ);
	}

	for (timeout = AR5K_TUNE_REGISTER_TIMEOUT; timeout > 0; timeout--) {
		status = ath5k_hw_reg_read(ah, AR5K_EEPROM_STATUS);
		if (status & AR5K_EEPROM_STAT_RDDONE) {
			if (status & AR5K_EEPROM_STAT_RDERR)
				return false;
			*data = (u16)(ath5k_hw_reg_read(ah, AR5K_EEPROM_DATA) &
					0xffff);
			return true;
		}
		udelay(15);
	}

	return false;
}

int ath5k_hw_read_srev(struct ath5k_hw *ah)
{
	ah->ah_mac_srev = ath5k_hw_reg_read(ah, AR5K_SREV);
	return 0;
}

/*
 * Read the MAC address from eeprom or platform_data
 */
static int ath5k_pci_eeprom_read_mac(struct ath5k_hw *ah, u8 *mac)
{
	u8 mac_d[ETH_ALEN] = {};
	u32 total, offset;
	u16 data;
	int octet;

	AR5K_EEPROM_READ(0x20, data);

	for (offset = 0x1f, octet = 0, total = 0; offset >= 0x1d; offset--) {
		AR5K_EEPROM_READ(offset, data);

		total += data;
		mac_d[octet + 1] = data & 0xff;
		mac_d[octet] = data >> 8;
		octet += 2;
	}

	if (!total || total == 3 * 0xffff)
		return -EINVAL;

	memcpy(mac, mac_d, ETH_ALEN);

	return 0;
}


/* Common ath_bus_opts structure */
static const struct ath_bus_ops ath_pci_bus_ops = {
	.ath_bus_type = ATH_PCI,
	.read_cachesize = ath5k_pci_read_cachesize,
	.eeprom_read = ath5k_pci_eeprom_read,
	.eeprom_read_mac = ath5k_pci_eeprom_read_mac,
};

/********************\
* PCI Initialization *
\********************/

static int __devinit
ath5k_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath5k_hw *ah;
	struct ieee80211_hw *hw;
	int ret;
	u8 csz;

	/*
	 * L0s needs to be disabled on all ath5k cards.
	 *
	 * For distributions shipping with CONFIG_PCIEASPM (this will be enabled
	 * by default in the future in 2.6.36) this will also mean both L1 and
	 * L0s will be disabled when a pre 1.1 PCIe device is detected. We do
	 * know L1 works correctly even for all ath5k pre 1.1 PCIe devices
	 * though but cannot currently undue the effect of a blacklist, for
	 * details you can read pcie_aspm_sanity_check() and see how it adjusts
	 * the device link capability.
	 *
	 * It may be possible in the future to implement some PCI API to allow
	 * drivers to override blacklists for pre 1.1 PCIe but for now it is
	 * best to accept that both L0s and L1 will be disabled completely for
	 * distributions shipping with CONFIG_PCIEASPM rather than having this
	 * issue present. Motivation for adding this new API will be to help
	 * with power consumption for some of these devices.
	 */
	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "can't enable device\n");
		goto err;
	}

	/* XXX 32-bit addressing only */
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "32-bit DMA not available\n");
		goto err_dis;
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
		csz = L1_CACHE_BYTES >> 2;
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems.  It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	/* Enable bus mastering */
	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, 0x41, 0);

	ret = pci_request_region(pdev, 0, "ath5k");
	if (ret) {
		dev_err(&pdev->dev, "cannot reserve PCI memory region\n");
		goto err_dis;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		dev_err(&pdev->dev, "cannot remap PCI memory region\n");
		ret = -EIO;
		goto err_reg;
	}

	/*
	 * Allocate hw (mac80211 main struct)
	 * and hw->priv (driver private data)
	 */
	hw = ieee80211_alloc_hw(sizeof(*ah), &ath5k_hw_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "cannot allocate ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_map;
	}

	dev_info(&pdev->dev, "registered as '%s'\n", wiphy_name(hw->wiphy));

	ah = hw->priv;
	ah->hw = hw;
	ah->pdev = pdev;
	ah->dev = &pdev->dev;
	ah->irq = pdev->irq;
	ah->devid = id->device;
	ah->iobase = mem; /* So we can unmap it on detach */

	/* Initialize */
	ret = ath5k_init_ah(ah, &ath_pci_bus_ops);
	if (ret)
		goto err_free;

	/* Set private data */
	pci_set_drvdata(pdev, hw);

	return 0;
err_free:
	ieee80211_free_hw(hw);
err_map:
	pci_iounmap(pdev, mem);
err_reg:
	pci_release_region(pdev, 0);
err_dis:
	pci_disable_device(pdev);
err:
	return ret;
}

static void __devexit
ath5k_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_hw *ah = hw->priv;

	ath5k_deinit_ah(ah);
	pci_iounmap(pdev, ah->iobase);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	ieee80211_free_hw(hw);
}

#ifdef CONFIG_PM_SLEEP
static int ath5k_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_hw *ah = hw->priv;

	ath5k_led_off(ah);
	return 0;
}

static int ath5k_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_hw *ah = hw->priv;

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_write_config_byte(pdev, 0x41, 0);

	ath5k_led_enable(ah);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ath5k_pm_ops, ath5k_pci_suspend, ath5k_pci_resume);
#define ATH5K_PM_OPS	(&ath5k_pm_ops)
#else
#define ATH5K_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct pci_driver ath5k_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= ath5k_pci_id_table,
	.probe		= ath5k_pci_probe,
	.remove		= __devexit_p(ath5k_pci_remove),
	.driver.pm	= ATH5K_PM_OPS,
};

/*
 * Module init/exit functions
 */
static int __init
init_ath5k_pci(void)
{
	int ret;

	ret = pci_register_driver(&ath5k_pci_driver);
	if (ret) {
		printk(KERN_ERR "ath5k_pci: can't register pci driver\n");
		return ret;
	}

	return 0;
}

static void __exit
exit_ath5k_pci(void)
{
	pci_unregister_driver(&ath5k_pci_driver);
}

module_init(init_ath5k_pci);
module_exit(exit_ath5k_pci);
