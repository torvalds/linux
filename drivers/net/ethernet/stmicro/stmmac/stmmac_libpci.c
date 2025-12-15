// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI bus helpers for STMMAC driver
 * Copyright (C) 2025 Yao Zi <ziyao@disroot.org>
 */

#include <linux/device.h>
#include <linux/pci.h>

#include "stmmac_libpci.h"

int stmmac_pci_plat_suspend(struct device *dev, void *bsp_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_pci_plat_suspend);

int stmmac_pci_plat_resume(struct device *dev, void *bsp_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return 0;
}
EXPORT_SYMBOL_GPL(stmmac_pci_plat_resume);

MODULE_DESCRIPTION("STMMAC PCI helper library");
MODULE_AUTHOR("Yao Zi <ziyao@disroot.org>");
MODULE_LICENSE("GPL");
