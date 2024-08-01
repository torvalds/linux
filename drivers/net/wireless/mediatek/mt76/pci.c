// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2019 Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include "mt76.h"
#include <linux/pci.h>

void mt76_pci_disable_aspm(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev->bus->self;
	u16 aspm_conf, parent_aspm_conf = 0;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &aspm_conf);
	aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	if (parent) {
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_aspm_conf);
		parent_aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	}

	if (!aspm_conf && (!parent || !parent_aspm_conf)) {
		/* aspm already disabled */
		return;
	}

	dev_info(&pdev->dev, "disabling ASPM %s %s\n",
		 (aspm_conf & PCI_EXP_LNKCTL_ASPM_L0S) ? "L0s" : "",
		 (aspm_conf & PCI_EXP_LNKCTL_ASPM_L1) ? "L1" : "");

	if (IS_ENABLED(CONFIG_PCIEASPM)) {
		int err;

		err = pci_disable_link_state(pdev, aspm_conf);
		if (!err)
			return;
	}

	/* both device and parent should have the same ASPM setting.
	 * disable ASPM in downstream component first and then upstream.
	 */
	pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL, aspm_conf);
	if (parent)
		pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
					   aspm_conf);
}
EXPORT_SYMBOL_GPL(mt76_pci_disable_aspm);

bool mt76_pci_aspm_supported(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev->bus->self;
	u16 aspm_conf, parent_aspm_conf = 0;
	bool result = true;

	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &aspm_conf);
	aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	if (parent) {
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_aspm_conf);
		parent_aspm_conf &= PCI_EXP_LNKCTL_ASPMC;
	}

	if (!aspm_conf && (!parent || !parent_aspm_conf)) {
		/* aspm already disabled */
		result = false;
	}

	return result;
}
EXPORT_SYMBOL_GPL(mt76_pci_aspm_supported);
