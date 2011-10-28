/*
 * Intel Poulsbo Stub driver
 *
 * Copyright (C) 2010 Novell <jlee@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/video.h>

#define DRIVER_NAME "poulsbo"

enum {
	CHIP_PSB_8108 = 0,
	CHIP_PSB_8109 = 1,
};

static DEFINE_PCI_DEVICE_TABLE(pciidlist) = {
	{0x8086, 0x8108, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PSB_8108}, \
	{0x8086, 0x8109, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PSB_8109}, \
	{0, 0, 0}
};

static int poulsbo_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return acpi_video_register();
}

static void poulsbo_remove(struct pci_dev *pdev)
{
	acpi_video_unregister();
}

static struct pci_driver poulsbo_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = poulsbo_probe,
	.remove = poulsbo_remove,
};

static int __init poulsbo_init(void)
{
	return pci_register_driver(&poulsbo_driver);
}

static void __exit poulsbo_exit(void)
{
	pci_unregister_driver(&poulsbo_driver);
}

module_init(poulsbo_init);
module_exit(poulsbo_exit);

MODULE_AUTHOR("Lee, Chun-Yi <jlee@novell.com>");
MODULE_DESCRIPTION("Poulsbo Stub Driver");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(pci, pciidlist);
