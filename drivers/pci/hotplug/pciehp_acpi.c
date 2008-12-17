/*
 * ACPI related functions for PCI Express Hot Plug driver.
 *
 * Copyright (C) 2008 Kenji Kaneshige
 * Copyright (C) 2008 Fujitsu Limited.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/acpi.h>
#include "pciehp.h"

#define PCIEHP_DETECT_PCIE	(0)
#define PCIEHP_DETECT_ACPI	(1)
#define PCIEHP_DETECT_DEFAULT	PCIEHP_DETECT_PCIE

static int slot_detection_mode;
static char *pciehp_detect_mode;
module_param(pciehp_detect_mode, charp, 0444);
MODULE_PARM_DESC(pciehp_detect_mode,
		 "Slot detection mode: pcie, acpi\n"
		 "  pcie - Use PCIe based slot detection (default)\n"
		 "  acpi - Use ACPI for slot detection\n");

static int is_ejectable(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;
	unsigned long long removable;
	status = acpi_get_handle(handle, "_ADR", &tmp);
	if (ACPI_FAILURE(status))
		return 0;
	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_SUCCESS(status))
		return 1;
	status = acpi_evaluate_integer(handle, "_RMV", NULL, &removable);
	if (ACPI_SUCCESS(status) && removable)
		return 1;
	return 0;
}

static acpi_status
check_hotplug(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *found = (int *)context;
	if (is_ejectable(handle)) {
		*found = 1;
		return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

static int pciehp_detect_acpi_slot(struct pci_bus *pbus)
{
	acpi_handle handle;
	struct pci_dev *pdev = pbus->self;
	int found = 0;

	if (!pdev){
		int seg = pci_domain_nr(pbus), busnr = pbus->number;
		handle = acpi_get_pci_rootbridge_handle(seg, busnr);
	} else
		handle = DEVICE_ACPI_HANDLE(&(pdev->dev));

	if (!handle)
		return 0;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
			    check_hotplug, (void *)&found, NULL);
	return found;
}

int pciehp_acpi_slot_detection_check(struct pci_dev *dev)
{
	if (slot_detection_mode != PCIEHP_DETECT_ACPI)
		return 0;
	if (pciehp_detect_acpi_slot(dev->subordinate))
		return 0;
	return -ENODEV;
}

static int __init parse_detect_mode(void)
{
	if (!pciehp_detect_mode)
		return PCIEHP_DETECT_DEFAULT;
	if (!strcmp(pciehp_detect_mode, "pcie"))
		return PCIEHP_DETECT_PCIE;
	if (!strcmp(pciehp_detect_mode, "acpi"))
		return PCIEHP_DETECT_ACPI;
	warn("bad specifier '%s' for pciehp_detect_mode. Use default\n",
	     pciehp_detect_mode);
	return PCIEHP_DETECT_DEFAULT;
}

void __init pciehp_acpi_slot_detection_init(void)
{
	slot_detection_mode = parse_detect_mode();
}
