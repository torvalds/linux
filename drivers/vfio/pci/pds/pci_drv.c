// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/vfio.h>

#include <linux/pds/pds_core_if.h>

#include "vfio_dev.h"

#define PDS_VFIO_DRV_DESCRIPTION	"AMD/Pensando VFIO Device Driver"
#define PCI_VENDOR_ID_PENSANDO		0x1dd8

static int pds_vfio_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct pds_vfio_pci_device *pds_vfio;
	int err;

	pds_vfio = vfio_alloc_device(pds_vfio_pci_device, vfio_coredev.vdev,
				     &pdev->dev, pds_vfio_ops_info());
	if (IS_ERR(pds_vfio))
		return PTR_ERR(pds_vfio);

	dev_set_drvdata(&pdev->dev, &pds_vfio->vfio_coredev);

	err = vfio_pci_core_register_device(&pds_vfio->vfio_coredev);
	if (err)
		goto out_put_vdev;

	return 0;

out_put_vdev:
	vfio_put_device(&pds_vfio->vfio_coredev.vdev);
	return err;
}

static void pds_vfio_pci_remove(struct pci_dev *pdev)
{
	struct pds_vfio_pci_device *pds_vfio = pds_vfio_pci_drvdata(pdev);

	vfio_pci_core_unregister_device(&pds_vfio->vfio_coredev);
	vfio_put_device(&pds_vfio->vfio_coredev.vdev);
}

static const struct pci_device_id pds_vfio_pci_table[] = {
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_PENSANDO, 0x1003) }, /* Ethernet VF */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pds_vfio_pci_table);

static struct pci_driver pds_vfio_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pds_vfio_pci_table,
	.probe = pds_vfio_pci_probe,
	.remove = pds_vfio_pci_remove,
	.driver_managed_dma = true,
};

module_pci_driver(pds_vfio_pci_driver);

MODULE_DESCRIPTION(PDS_VFIO_DRV_DESCRIPTION);
MODULE_AUTHOR("Brett Creeley <brett.creeley@amd.com>");
MODULE_LICENSE("GPL");
