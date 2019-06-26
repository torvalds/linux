// SPDX-License-Identifier: GPL-2.0+
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include "pcie.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee.Brooke@Daktronics.com, Matt.Sickler@Daktronics.com");
MODULE_SOFTDEP("pre: uio post: kpc_nwl_dma kpc_i2c kpc_spi");

struct class *kpc_uio_class;
ATTRIBUTE_GROUPS(kpc_uio_class);

static const struct pci_device_id kp2000_pci_device_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_DAKTRONICS, PCI_DEVICE_ID_DAKTRONICS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DAKTRONICS, PCI_DEVICE_ID_DAKTRONICS_KADOKA_P2KR0) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, kp2000_pci_device_ids);

static struct pci_driver  kp2000_driver_inst = {
	.name       = "kp2000_pcie",
	.id_table   = kp2000_pci_device_ids,
	.probe      = kp2000_pcie_probe,
	.remove     = kp2000_pcie_remove
};


static int __init  kp2000_pcie_init(void)
{
	kpc_uio_class = class_create(THIS_MODULE, "kpc_uio");
	if (IS_ERR(kpc_uio_class))
		return PTR_ERR(kpc_uio_class);

	kpc_uio_class->dev_groups = kpc_uio_class_groups;
	return pci_register_driver(&kp2000_driver_inst);
}
module_init(kp2000_pcie_init);

static void __exit  kp2000_pcie_exit(void)
{
	pci_unregister_driver(&kp2000_driver_inst);
	class_destroy(kpc_uio_class);
}
module_exit(kp2000_pcie_exit);
