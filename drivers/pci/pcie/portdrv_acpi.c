// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe Port Native Services Support, ACPI-Related Part
 *
 * Copyright (C) 2010 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */

#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/pci-acpi.h>

#include "aer/aerdrv.h"
#include "../pci.h"
#include "portdrv.h"

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
void pcie_port_acpi_setup(struct pci_dev *port, int *srv_mask)
{
	struct acpi_pci_root *root;
	acpi_handle handle;
	u32 flags;

	if (acpi_pci_disabled)
		return;

	handle = acpi_find_root_bridge_handle(port);
	if (!handle)
		return;

	root = acpi_pci_find_root(handle);
	if (!root)
		return;

	flags = root->osc_control_set;

	*srv_mask = 0;
	if (flags & OSC_PCI_EXPRESS_NATIVE_HP_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_HP;
	if (flags & OSC_PCI_EXPRESS_PME_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_PME;
	if (flags & OSC_PCI_EXPRESS_AER_CONTROL)
		*srv_mask |= PCIE_PORT_SERVICE_AER | PCIE_PORT_SERVICE_DPC;
}
