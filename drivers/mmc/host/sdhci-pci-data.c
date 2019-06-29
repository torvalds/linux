// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/mmc/sdhci-pci-data.h>

struct sdhci_pci_data *(*sdhci_pci_get_data)(struct pci_dev *pdev, int slotno);
EXPORT_SYMBOL_GPL(sdhci_pci_get_data);
