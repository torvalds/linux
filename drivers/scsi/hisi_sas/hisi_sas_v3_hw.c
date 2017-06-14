/*
 * Copyright (c) 2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "hisi_sas.h"
#define DRV_NAME "hisi_sas_v3_hw"

static int
hisi_sas_v3_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return 0;
}

static void hisi_sas_v3_remove(struct pci_dev *pdev)
{
}

enum {
	/* instances of the controller */
	hip08,
};

static const struct pci_device_id sas_v3_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, 0xa230), hip08 },
	{}
};

static struct pci_driver sas_v3_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= sas_v3_pci_table,
	.probe		= hisi_sas_v3_probe,
	.remove		= hisi_sas_v3_remove,
};

module_pci_driver(sas_v3_pci_driver);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Garry <john.garry@huawei.com>");
MODULE_DESCRIPTION("HISILICON SAS controller v3 hw driver based on pci device");
MODULE_ALIAS("platform:" DRV_NAME);
