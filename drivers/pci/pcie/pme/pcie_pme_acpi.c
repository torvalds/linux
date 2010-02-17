/*
 * PCIe Native PME support, ACPI-related part
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License V2.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>
#include <linux/pcieport_if.h>

/**
 * pcie_pme_acpi_setup - Request the ACPI BIOS to release control over PCIe PME.
 * @srv - PCIe PME service for a root port or event collector.
 *
 * Invoked when the PCIe bus type loads PCIe PME service driver.  To avoid
 * conflict with the BIOS PCIe support requires the BIOS to yield PCIe PME
 * control to the kernel.
 */
int pcie_pme_acpi_setup(struct pcie_device *srv)
{
	acpi_status status = AE_NOT_FOUND;
	struct pci_dev *port = srv->port;
	acpi_handle handle;
	int error = 0;

	if (acpi_pci_disabled)
		return -ENOSYS;

	dev_info(&port->dev, "Requesting control of PCIe PME from ACPI BIOS\n");

	handle = acpi_find_root_bridge_handle(port);
	if (!handle)
		return -EINVAL;

	status = acpi_pci_osc_control_set(handle,
			OSC_PCI_EXPRESS_PME_CONTROL |
			OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	if (ACPI_FAILURE(status)) {
		dev_info(&port->dev,
			"Failed to receive control of PCIe PME service: %s\n",
			(status == AE_SUPPORT || status == AE_NOT_FOUND) ?
			"no _OSC support" : "ACPI _OSC failed");
		error = -ENODEV;
	}

	return error;
}
