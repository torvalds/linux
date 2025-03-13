// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/pci.h>
#include "fdomain.h"

static int fdomain_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *d)
{
	int err;
	struct Scsi_Host *sh;

	err = pci_enable_device(pdev);
	if (err)
		goto fail;

	err = pci_request_regions(pdev, "fdomain_pci");
	if (err)
		goto disable_device;

	err = -ENODEV;
	if (pci_resource_len(pdev, 0) == 0)
		goto release_region;

	sh = fdomain_create(pci_resource_start(pdev, 0), pdev->irq, 7,
			    &pdev->dev);
	if (!sh)
		goto release_region;

	pci_set_drvdata(pdev, sh);
	return 0;

release_region:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
fail:
	return err;
}

static void fdomain_pci_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *sh = pci_get_drvdata(pdev);

	fdomain_destroy(sh);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id fdomain_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FD, PCI_DEVICE_ID_FD_36C70) },
	{}
};
MODULE_DEVICE_TABLE(pci, fdomain_pci_table);

static struct pci_driver fdomain_pci_driver = {
	.name		= "fdomain_pci",
	.id_table	= fdomain_pci_table,
	.probe		= fdomain_pci_probe,
	.remove		= fdomain_pci_remove,
	.driver.pm	= FDOMAIN_PM_OPS,
};

module_pci_driver(fdomain_pci_driver);

MODULE_AUTHOR("Ondrej Zary, Rickard E. Faith");
MODULE_DESCRIPTION("Future Domain TMC-3260 PCI SCSI driver");
MODULE_LICENSE("GPL");
