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
	int rc;
	struct ocxl_afu *afu, *tmp;
	struct ocxl_fn *fn;
	struct list_head *afu_list;

	fn = ocxl_function_open(dev);
	if (IS_ERR(fn))
		return PTR_ERR(fn);

	pci_set_drvdata(dev, fn);

	afu_list = ocxl_function_afu_list(fn);

	list_for_each_entry_safe(afu, tmp, afu_list, list) {
		// Cleanup handled within ocxl_file_register_afu()
		rc = ocxl_file_register_afu(afu);
		if (rc) {
			dev_err(&dev->dev, "Failed to register AFU '%s' index %d",
					afu->config.name, afu->config.idx);
		}
	}

	return 0;
}

void ocxl_remove(struct pci_dev *dev)
{
	struct ocxl_fn *fn;
	struct ocxl_afu *afu;
	struct list_head *afu_list;

	fn = pci_get_drvdata(dev);
	afu_list = ocxl_function_afu_list(fn);

	list_for_each_entry(afu, afu_list, list) {
		ocxl_file_unregister_afu(afu);
	}

	ocxl_function_close(fn);
}

struct pci_driver ocxl_pci_driver = {
	.name = "ocxl",
	.id_table = ocxl_pci_tbl,
	.probe = ocxl_probe,
	.remove = ocxl_remove,
	.shutdown = ocxl_remove,
};
