/* pci-stub - simple stub driver to reserve a pci device
 *
 * Copyright (C) 2008 Red Hat, Inc.
 * Author:
 * 	Chris Wright
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Usage is simple, allocate a new id to the stub driver and bind the
 * device to it.  For example:
 * 
 * # echo "8086 10f5" > /sys/bus/pci/drivers/pci-stub/new_id
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/e1000e/unbind
 * # echo -n 0000:00:19.0 > /sys/bus/pci/drivers/pci-stub/bind
 * # ls -l /sys/bus/pci/devices/0000:00:19.0/driver
 * .../0000:00:19.0/driver -> ../../../bus/pci/drivers/pci-stub
 */

#include <linux/module.h>
#include <linux/pci.h>

static int pci_stub_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return 0;
}

static struct pci_driver stub_driver = {
	.name		= "pci-stub",
	.id_table	= NULL,	/* only dynamic id's */
	.probe		= pci_stub_probe,
};

static int __init pci_stub_init(void)
{
	return pci_register_driver(&stub_driver);
}

static void __exit pci_stub_exit(void)
{
	pci_unregister_driver(&stub_driver);
}

module_init(pci_stub_init);
module_exit(pci_stub_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Wright <chrisw@sous-sol.org>");
