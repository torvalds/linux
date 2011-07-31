/*
 * PCIe Port Native Services Support, ACPI-Related Part
 *
 * Copyright (C) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
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

#include "aer/aerdrv.h"
#include "../pci.h"

/**
 * pcie_port_acpi_setup - Request the BIOS to release control of PCIe services.
 * @port: PCIe Port service for a root port or event collector.
 * @srv_mask: Bit mask of services that can be enabled for @port.
 *
 * Invoked when @port is identified as a PCIe port device.  To avoid conflicts
 * with the BIOS PCIe port native services support requires the BIOS to yield
 * control of these services to the kernel.  The mask of services that the BIOS
 * allows to be enabled for @port is written to @srv_mask.
 *
 * NOTE: It turns out that we cannot do that for individual port services
 * separately, because that would make some systems work incorrectly.
 */
int pcie_port_acpi_setup(struct pci_dev *port, int *srv_mask)
{
	acpi_status status;
	acpi_handle handle;
	u32 flags;

	if (acpi_pci_disabled)
		return 0;

	handle = acpi_find_root_bridge_handle(port);
	if (!handle)
		return -EINVAL;

	flags = OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL
		| OSC_PCI_EXPRESS_NATIVE_HP_CONTROL
		| OSC_PCI_EXPRESS_PME_CONTROL;

	if (pci_aer_available()) {
		if (pcie_aer_get_firmware_first(port))
			dev_dbg(&port->dev, "PCIe errors handled by BIOS.\n");
		else
			flags |= OSC_PCI_EXPRESS_AER_CONTROL;
	}

	status = acpi_pci_osc_control_set(handle, &flags,
					OSC_PCI_EXPRESS_CAP_STRUCTURE_CONTROL);
	if (ACPI_FAILURE(status)) {
		dev_dbg(&port->dev, "ACPI _OSC request failed (code %d)\n",
			status);
		return -ENODEV;
	}

	dev_info(&port->dev, "ACPI _OSC control granted for 0x%02x\n", flags);

	*srv_mask = PCIE_PORT_SERVICE_VC;
	if (flags & OSC_PCI_EXPRESS_NATIVE_HP_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_HP;
	if (flags & OSC_PCI_EXPRESS_PME_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_PME;
	if (flags & OSC_PCI_EXPRESS_AER_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_AER;

	return 0;
}
