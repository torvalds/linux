// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Elkhart Lake Programmable Service Engine (PSE) I/O
 *
 * Copyright (c) 2025 Intel Corporation.
 *
 * Author: Raag Jadav <raag.jadav@intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/device/devres.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/ioport.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include <linux/ehl_pse_io_aux.h>

#define EHL_PSE_IO_DEV_SIZE	SZ_4K

static int ehl_pse_io_dev_create(struct pci_dev *pci, const char *name, int idx)
{
	struct device *dev = &pci->dev;
	struct auxiliary_device *adev;
	struct ehl_pse_io_data *data;
	resource_size_t start, offset;
	u32 id;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	id = (pci_domain_nr(pci->bus) << 16) | pci_dev_id(pci);
	start = pci_resource_start(pci, 0);
	offset = EHL_PSE_IO_DEV_SIZE * idx;

	data->mem = DEFINE_RES_MEM(start + offset, EHL_PSE_IO_DEV_SIZE);
	data->irq = pci_irq_vector(pci, idx);

	adev = __devm_auxiliary_device_create(dev, EHL_PSE_IO_NAME, name, data, id);

	return adev ? 0 : -ENODEV;
}

static int ehl_pse_io_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	int ret;

	ret = pcim_enable_device(pci);
	if (ret)
		return ret;

	pci_set_master(pci);

	ret = pci_alloc_irq_vectors(pci, 2, 2, PCI_IRQ_MSI);
	if (ret < 0)
		return ret;

	ret = ehl_pse_io_dev_create(pci, EHL_PSE_GPIO_NAME, 0);
	if (ret)
		return ret;

	return ehl_pse_io_dev_create(pci, EHL_PSE_TIO_NAME, 1);
}

static const struct pci_device_id ehl_pse_io_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x4b88) },
	{ PCI_VDEVICE(INTEL, 0x4b89) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ehl_pse_io_ids);

static struct pci_driver ehl_pse_io_driver = {
	.name		= EHL_PSE_IO_NAME,
	.id_table	= ehl_pse_io_ids,
	.probe		= ehl_pse_io_probe,
};
module_pci_driver(ehl_pse_io_driver);

MODULE_AUTHOR("Raag Jadav <raag.jadav@intel.com>");
MODULE_DESCRIPTION("Intel Elkhart Lake PSE I/O driver");
MODULE_LICENSE("GPL");
