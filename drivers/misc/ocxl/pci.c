// SPDX-License-Identifier: GPL-2.0+
// Copyright 2019 IBM Corp.
#include <linux/module.h>
#include "ocxl_internal.h"

/*
 * Any opencapi device which wants to use this 'generic' driver should
 * use the 0x062B device ID. Vendors should define the subsystem
 * vendor/device ID to help differentiate devices.
 */
static const struct pci_device_id ocxl_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IBM, 0x062B), },
	{ }
};
MODULE_DEVICE_TABLE(pci, ocxl_pci_tbl);

static int ocxl_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int rc, afu_count = 0;
	u8 afu;
	struct ocxl_fn *fn;

	if (!radix_enabled()) {
		dev_err(&dev->dev, "Unsupported memory model (hash)\n");
		return -ENODEV;
	}

	fn = init_function(dev);
	if (IS_ERR(fn)) {
		dev_err(&dev->dev, "function init failed: %li\n",
			PTR_ERR(fn));
		return PTR_ERR(fn);
	}

	for (afu = 0; afu <= fn->config.max_afu_index; afu++) {
		rc = ocxl_config_check_afu_index(dev, &fn->config, afu);
		if (rc > 0) {
			rc = init_afu(dev, fn, afu);
			if (rc) {
				dev_err(&dev->dev,
					"Can't initialize AFU index %d\n", afu);
				continue;
			}
			afu_count++;
		}
	}
	dev_info(&dev->dev, "%d AFU(s) configured\n", afu_count);
	return 0;
}

static void ocxl_remove(struct pci_dev *dev)
{
	struct ocxl_afu *afu, *tmp;
	struct ocxl_fn *fn = pci_get_drvdata(dev);

	list_for_each_entry_safe(afu, tmp, &fn->afu_list, list) {
		remove_afu(afu);
	}
	remove_function(fn);
}

struct pci_driver ocxl_pci_driver = {
	.name = "ocxl",
	.id_table = ocxl_pci_tbl,
	.probe = ocxl_probe,
	.remove = ocxl_remove,
	.shutdown = ocxl_remove,
};
