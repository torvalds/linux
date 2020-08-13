// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "core.h"
#include "debug.h"

#define QCA6390_DEVICE_ID		0x1101

static const struct pci_device_id ath11k_pci_id_table[] = {
	{ PCI_VDEVICE(QCOM, QCA6390_DEVICE_ID) },
	{0}
};

MODULE_DEVICE_TABLE(pci, ath11k_pci_id_table);

static int ath11k_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_dev)
{
	struct ath11k_base *ab;
	enum ath11k_hw_rev hw_rev;

	dev_warn(&pdev->dev, "WARNING: ath11k PCI support is experimental!\n");

	switch (pci_dev->device) {
	case QCA6390_DEVICE_ID:
		hw_rev = ATH11K_HW_QCA6390_HW20;
		break;
	default:
		dev_err(&pdev->dev, "Unknown PCI device found: 0x%x\n",
			pci_dev->device);
		return -ENOTSUPP;
	}

	ab = ath11k_core_alloc(&pdev->dev, 0, ATH11K_BUS_PCI);
	if (!ab) {
		dev_err(&pdev->dev, "failed to allocate ath11k base\n");
		return -ENOMEM;
	}

	ab->dev = &pdev->dev;
	ab->hw_rev = hw_rev;
	pci_set_drvdata(pdev, ab);

	return 0;
}

static void ath11k_pci_remove(struct pci_dev *pdev)
{
	struct ath11k_base *ab = pci_get_drvdata(pdev);

	set_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags);
	ath11k_core_free(ab);
}

static struct pci_driver ath11k_pci_driver = {
	.name = "ath11k_pci",
	.id_table = ath11k_pci_id_table,
	.probe = ath11k_pci_probe,
	.remove = ath11k_pci_remove,
};

static int ath11k_pci_init(void)
{
	int ret;

	ret = pci_register_driver(&ath11k_pci_driver);
	if (ret)
		pr_err("failed to register ath11k pci driver: %d\n",
		       ret);

	return ret;
}
module_init(ath11k_pci_init);

static void ath11k_pci_exit(void)
{
	pci_unregister_driver(&ath11k_pci_driver);
}

module_exit(ath11k_pci_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11ax WLAN PCIe devices");
MODULE_LICENSE("Dual BSD/GPL");
