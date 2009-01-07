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

	handle = acpi_find_root_bridge_handle(pdev);
	if (handle) {
		status = pci_osc_control_set(handle,
					OSC_PCI_EXPRESS_AER_CONTROL |
					OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	}

	if (ACPI_FAILURE(status)) {
		dev_printk(KERN_DEBUG, &pciedev->device, "AER service couldn't "
			   "init device: %s\n",
			   (status == AE_SUPPORT || status == AE_NOT_FOUND) ?
			   "no _OSC support" : "_OSC failed");
		return -1;
	}

	return 0;
}
