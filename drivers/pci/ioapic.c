/*
 * IOAPIC/IOxAPIC/IOSAPIC driver
 *
 * Copyright (C) 2009 Fujitsu Limited.
 * (c) Copyright 2009 Hewlett-Packard Development Company, L.P.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This driver manages PCI I/O APICs added by hotplug after boot.  We try to
 * claim all I/O APIC PCI devices, but those present at boot were registered
 * when we parsed the ACPI MADT, so we'll fail when we try to re-register
 * them.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <acpi/acpi_bus.h>

struct ioapic {
	acpi_handle	handle;
	u32		gsi_base;
};

static int ioapic_probe(struct pci_dev *dev, const struct pci_device_id *ent)
{
	acpi_handle handle;
	acpi_status status;
	unsigned long long gsb;
	struct ioapic *ioapic;
	int ret;
	char *type;
	struct resource *res;

	handle = DEVICE_ACPI_HANDLE(&dev->dev);
	if (!handle)
		return -EINVAL;

	status = acpi_evaluate_integer(handle, "_GSB", NULL, &gsb);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	/*
	 * The previous code in acpiphp evaluated _MAT if _GSB failed, but
	 * ACPI spec 4.0 sec 6.2.2 requires _GSB for hot-pluggable I/O APICs.
	 */

	ioapic = kzalloc(sizeof(*ioapic), GFP_KERNEL);
	if (!ioapic)
		return -ENOMEM;

	ioapic->handle = handle;
	ioapic->gsi_base = (u32) gsb;

	if (dev->class == PCI_CLASS_SYSTEM_PIC_IOAPIC)
		type = "IOAPIC";
	else
		type = "IOxAPIC";

	ret = pci_enable_device(dev);
	if (ret < 0)
		goto exit_free;

	pci_set_master(dev);

	if (pci_request_region(dev, 0, type))
		goto exit_disable;

	res = &dev->resource[0];
	if (acpi_register_ioapic(ioapic->handle, res->start, ioapic->gsi_base))
		goto exit_release;

	pci_set_drvdata(dev, ioapic);
	dev_info(&dev->dev, "%s at %pR, GSI %u\n", type, res, ioapic->gsi_base);
	return 0;

exit_release:
	pci_release_region(dev, 0);
exit_disable:
	pci_disable_device(dev);
exit_free:
	kfree(ioapic);
	return -ENODEV;
}

static void ioapic_remove(struct pci_dev *dev)
{
	struct ioapic *ioapic = pci_get_drvdata(dev);

	acpi_unregister_ioapic(ioapic->handle, ioapic->gsi_base);
	pci_release_region(dev, 0);
	pci_disable_device(dev);
	kfree(ioapic);
}


static DEFINE_PCI_DEVICE_TABLE(ioapic_devices) = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_SYSTEM_PIC_IOAPIC, ~0) },
	{ PCI_DEVICE_CLASS(PCI_CLASS_SYSTEM_PIC_IOXAPIC, ~0) },
	{ }
};
MODULE_DEVICE_TABLE(pci, ioapic_devices);

static struct pci_driver ioapic_driver = {
	.name		= "ioapic",
	.id_table	= ioapic_devices,
	.probe		= ioapic_probe,
	.remove		= ioapic_remove,
};

module_pci_driver(ioapic_driver);

MODULE_LICENSE("GPL");
