/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * This file contains a shim driver for the IOC4 IDE and serial drivers.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioc4_common.h>
#include <linux/ide.h>


static int __devinit
ioc4_probe_one(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	int ret;

	if ((ret = pci_enable_device(pdev))) {
		printk(KERN_WARNING
			 "%s: Failed to enable device with "
				"pci_dev 0x%p... returning\n",
				__FUNCTION__, (void *)pdev);
		return ret;
	}
	pci_set_master(pdev);

	/* attach each sub-device */
	ret = ioc4_ide_attach_one(pdev, pci_id);
	if (ret)
		return ret;
	return ioc4_serial_attach_one(pdev, pci_id);
}

/* pci device struct */
static struct pci_device_id ioc4_s_id_table[] = {
	{PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC4, PCI_ANY_ID,
	 PCI_ANY_ID, 0x0b4000, 0xFFFFFF},
	{0}
};
MODULE_DEVICE_TABLE(pci, ioc4_s_id_table);

static struct pci_driver __devinitdata ioc4_s_driver = {
	.name	= "IOC4",
	.id_table = ioc4_s_id_table,
	.probe	= ioc4_probe_one,
};

static int __devinit ioc4_detect(void)
{
	ioc4_serial_init();

	return pci_register_driver(&ioc4_s_driver);
}
module_init(ioc4_detect);

MODULE_AUTHOR("Pat Gefre - Silicon Graphics Inc. (SGI) <pfg@sgi.com>");
MODULE_DESCRIPTION("PCI driver module for SGI IOC4 Base-IO Card");
MODULE_LICENSE("GPL");
