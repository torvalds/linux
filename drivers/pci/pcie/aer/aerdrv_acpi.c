/*
 * Access ACPI _OSC method
 *
 * Copyright (C) 2006 Intel Corp.
 *	Tom Long Nguyen (tom.l.nguyen@intel.com)
 *	Zhang Yanmin (yanmin.zhang@intel.com)
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#include <linux/delay.h>
#include "aerdrv.h"

/**
 * aer_osc_setup - run ACPI _OSC method
 * @pciedev: pcie_device which AER is being enabled on
 *
 * @return: Zero on success. Nonzero otherwise.
 *
 * Invoked when PCIE bus loads AER service driver. To avoid conflict with
 * BIOS AER support requires BIOS to yield AER control to OS native driver.
 **/
int aer_osc_setup(struct pcie_device *pciedev)
{
	acpi_status status = AE_NOT_FOUND;
	struct pci_dev *pdev = pciedev->port;
	acpi_handle handle = NULL;

	if (acpi_pci_disabled)
		return -1;

	/* Find root host bridge */
	while (pdev->bus->self)
		pdev = pdev->bus->self;
	handle = acpi_get_pci_rootbridge_handle(
		pci_domain_nr(pdev->bus), pdev->bus->number);

	if (handle) {
		pcie_osc_support_set(OSC_EXT_PCI_CONFIG_SUPPORT);
		status = pci_osc_control_set(handle,
					OSC_PCI_EXPRESS_AER_CONTROL |
					OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	}

	if (ACPI_FAILURE(status)) {
		printk(KERN_DEBUG "AER service couldn't init device %s - %s\n",
		    pciedev->device.bus_id,
		    (status == AE_SUPPORT || status == AE_NOT_FOUND) ?
		    "no _OSC support" : "Run ACPI _OSC fails");
		return -1;
	}

	return 0;
}
