// SPDX-License-Identifier: GPL-2.0
/*
 * Simple stub driver to reserve a PCI device
 *
 * Copyright (C) 2008 Red Hat, Inc.
 * Author:
 *	Chris Wright
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

static char ids[1024] __initdata;

module_param_string(ids, ids, sizeof(ids), 0);
MODULE_PARM_DESC(ids, "Initial PCI IDs to add to the stub driver, format is "
		 "\"vendor:device[:subvendor[:subdevice[:class[:class_mask]]]]\""
		 " and multiple comma separated entries can be specified");

static int pci_stub_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	pci_info(dev, "claimed by stub\n");
	return 0;
}

static struct pci_driver stub_driver = {
	.name		= "pci-stub",
	.id_table	= NULL,	/* only dynamic id's */
	.probe		= pci_stub_probe,
	.driver_managed_dma = true,
};

static int __init pci_stub_init(void)
{
	char *p, *id;
	int rc;

	rc = pci_register_driver(&stub_driver);
	if (rc)
		return rc;

	/* no ids passed actually */
	if (ids[0] == '\0')
		return 0;

	/* add ids specified in the module parameter */
	p = ids;
	while ((id = strsep(&p, ","))) {
		unsigned int vendor, device, subvendor = PCI_ANY_ID,
			subdevice = PCI_ANY_ID, class = 0, class_mask = 0;
		int fields;

		if (!strlen(id))
			continue;

		fields = sscanf(id, "%x:%x:%x:%x:%x:%x",
				&vendor, &device, &subvendor, &subdevice,
				&class, &class_mask);

		if (fields < 2) {
			pr_warn("pci-stub: invalid ID string \"%s\"\n", id);
			continue;
		}

		pr_info("pci-stub: add %04X:%04X sub=%04X:%04X cls=%08X/%08X\n",
		       vendor, device, subvendor, subdevice, class, class_mask);

		rc = pci_add_dynid(&stub_driver, vendor, device,
				   subvendor, subdevice, class, class_mask, 0);
		if (rc)
			pr_warn("pci-stub: failed to add dynamic ID (%d)\n",
				rc);
	}

	return 0;
}

static void __exit pci_stub_exit(void)
{
	pci_unregister_driver(&stub_driver);
}

module_init(pci_stub_init);
module_exit(pci_stub_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Wright <chrisw@sous-sol.org>");
