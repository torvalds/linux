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
 *
 * Return:
 *	Zero if success. Nonzero for otherwise.
 *
 * Invoked when PCIE bus loads AER service driver. To avoid conflict with
 * BIOS AER support requires BIOS to yield AER control to OS native driver.
 **/
int aer_osc_setup(struct pci_dev *dev)
{
	int retval = OSC_METHOD_RUN_SUCCESS;
	acpi_status status;
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->dev);
	struct pci_dev *pdev = dev;
	struct pci_bus *parent;

	while (!handle) {
		if (!pdev || !pdev->bus->parent)
			break;
		parent = pdev->bus->parent;
		if (!parent->self)
			/* Parent must be a host bridge */
			handle = acpi_get_pci_rootbridge_handle(
					pci_domain_nr(parent),
					parent->number);
		else
			handle = DEVICE_ACPI_HANDLE(
					&(parent->self->dev));
		pdev = parent->self;
	}

	if (!handle)
		return OSC_METHOD_NOT_SUPPORTED;

	pci_osc_support_set(OSC_EXT_PCI_CONFIG_SUPPORT);
	status = pci_osc_control_set(handle, OSC_PCI_EXPRESS_AER_CONTROL |
		OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	if (ACPI_FAILURE(status)) {
		if (status == AE_SUPPORT)
			retval = OSC_METHOD_NOT_SUPPORTED;
	 	else
			retval = OSC_METHOD_RUN_FAILURE;
	}

	return retval;
}

