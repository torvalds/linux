// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00pci
	Abstract: rt2x00 generic pci device routines.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "rt2x00.h"
#include "rt2x00pci.h"

int rt2x00pci_probe(struct pci_dev *pci_dev, const struct rt2x00_ops *ops)
{
	struct rt2x00_dev *rt2x00dev;
	struct ieee80211_hw *hw;
	void __iomem *base;
	__le16 *eeprom;
	int retval;
	u16 chip;
	u32 *rf;

	retval = pcim_enable_device(pci_dev);
	if (retval) {
		rt2x00_probe_err("Enable device failed\n");
		return retval;
	}

	base = pcim_iomap_region(pci_dev, 0, pci_name(pci_dev));
	if (IS_ERR(base)) {
		rt2x00_probe_err("PCI iomap region failed\n");
		return PTR_ERR(base);
	}

	pci_set_master(pci_dev);

	if (pcim_set_mwi(pci_dev))
		rt2x00_probe_err("MWI not available\n");

	if (dma_set_mask(&pci_dev->dev, DMA_BIT_MASK(32))) {
		rt2x00_probe_err("PCI DMA not supported\n");
		return -EIO;
	}

	eeprom = devm_kzalloc(&pci_dev->dev, ops->eeprom_size, GFP_KERNEL);
	if (!eeprom)
		return -ENOMEM;

	rf = devm_kzalloc(&pci_dev->dev, ops->rf_size, GFP_KERNEL);
	if (!rf)
		return -ENOMEM;

	hw = ieee80211_alloc_hw(sizeof(struct rt2x00_dev), ops->hw);
	if (!hw) {
		rt2x00_probe_err("Failed to allocate hardware\n");
		return -ENOMEM;
	}

	pci_set_drvdata(pci_dev, hw);

	rt2x00dev = hw->priv;
	rt2x00dev->dev = &pci_dev->dev;
	rt2x00dev->ops = ops;
	rt2x00dev->hw = hw;
	rt2x00dev->csr.base = base;
	rt2x00dev->irq = pci_dev->irq;
	rt2x00dev->name = ops->name;
	rt2x00dev->eeprom = eeprom;
	rt2x00dev->rf = rf;

	if (pci_is_pcie(pci_dev))
		rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_PCIE);
	else
		rt2x00_set_chip_intf(rt2x00dev, RT2X00_CHIP_INTF_PCI);

	/*
	 * Because rt3290 chip use different efuse offset to read efuse data.
	 * So before read efuse it need to indicate it is the
	 * rt3290 or not.
	 */
	pci_read_config_word(pci_dev, PCI_DEVICE_ID, &chip);
	rt2x00dev->chip.rt = chip;

	retval = rt2x00lib_probe_dev(rt2x00dev);
	if (retval)
		goto exit_free_device;

	return 0;

exit_free_device:
	ieee80211_free_hw(hw);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00pci_probe);

void rt2x00pci_remove(struct pci_dev *pci_dev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pci_dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	rt2x00lib_remove_dev(rt2x00dev);
	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL_GPL(rt2x00pci_remove);

static int __maybe_unused rt2x00pci_suspend(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_suspend(rt2x00dev);
}

static int __maybe_unused rt2x00pci_resume(struct device *dev)
{
	struct ieee80211_hw *hw = dev_get_drvdata(dev);
	struct rt2x00_dev *rt2x00dev = hw->priv;

	return rt2x00lib_resume(rt2x00dev);
}

SIMPLE_DEV_PM_OPS(rt2x00pci_pm_ops, rt2x00pci_suspend, rt2x00pci_resume);
EXPORT_SYMBOL_GPL(rt2x00pci_pm_ops);

/*
 * rt2x00pci module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 pci library");
MODULE_LICENSE("GPL");
