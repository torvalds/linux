/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * 	Copyright (C) Ashok Raj <ashok.raj@intel.com>
 *	Copyright (C) Shaohua Li <shaohua.li@intel.com>
 *	Copyright (C) Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *
 * 	This file implements early detection/parsing of DMA Remapping Devices
 * reported to OS through BIOS via DMA remapping reporting (DMAR) ACPI
 * tables.
 */

#include <linux/pci.h>
#include <linux/dmar.h>

#undef PREFIX
#define PREFIX "DMAR:"

/* No locks are needed as DMA remapping hardware unit
 * list is constructed at boot time and hotplug of
 * these units are not supported by the architecture.
 */
LIST_HEAD(dmar_drhd_units);
LIST_HEAD(dmar_rmrr_units);

static struct acpi_table_header * __initdata dmar_tbl;

static void __init dmar_register_drhd_unit(struct dmar_drhd_unit *drhd)
{
	/*
	 * add INCLUDE_ALL at the tail, so scan the list will find it at
	 * the very end.
	 */
	if (drhd->include_all)
		list_add_tail(&drhd->list, &dmar_drhd_units);
	else
		list_add(&drhd->list, &dmar_drhd_units);
}

static void __init dmar_register_rmrr_unit(struct dmar_rmrr_unit *rmrr)
{
	list_add(&rmrr->list, &dmar_rmrr_units);
}

static int __init dmar_parse_one_dev_scope(struct acpi_dmar_device_scope *scope,
					   struct pci_dev **dev, u16 segment)
{
	struct pci_bus *bus;
	struct pci_dev *pdev = NULL;
	struct acpi_dmar_pci_path *path;
	int count;

	bus = pci_find_bus(segment, scope->bus);
	path = (struct acpi_dmar_pci_path *)(scope + 1);
	count = (scope->length - sizeof(struct acpi_dmar_device_scope))
		/ sizeof(struct acpi_dmar_pci_path);

	while (count) {
		if (pdev)
			pci_dev_put(pdev);
		/*
		 * Some BIOSes list non-exist devices in DMAR table, just
		 * ignore it
		 */
		if (!bus) {
			printk(KERN_WARNING
			PREFIX "Device scope bus [%d] not found\n",
			scope->bus);
			break;
		}
		pdev = pci_get_slot(bus, PCI_DEVFN(path->dev, path->fn));
		if (!pdev) {
			printk(KERN_WARNING PREFIX
			"Device scope device [%04x:%02x:%02x.%02x] not found\n",
				segment, bus->number, path->dev, path->fn);
			break;
		}
		path ++;
		count --;
		bus = pdev->subordinate;
	}
	if (!pdev) {
		printk(KERN_WARNING PREFIX
		"Device scope device [%04x:%02x:%02x.%02x] not found\n",
		segment, scope->bus, path->dev, path->fn);
		*dev = NULL;
		return 0;
	}
	if ((scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT && \
			pdev->subordinate) || (scope->entry_type == \
			ACPI_DMAR_SCOPE_TYPE_BRIDGE && !pdev->subordinate)) {
		pci_dev_put(pdev);
		printk(KERN_WARNING PREFIX
			"Device scope type does not match for %s\n",
			 pci_name(pdev));
		return -EINVAL;
	}
	*dev = pdev;
	return 0;
}

static int __init dmar_parse_dev_scope(void *start, void *end, int *cnt,
				       struct pci_dev ***devices, u16 segment)
{
	struct acpi_dmar_device_scope *scope;
	void * tmp = start;
	int index;
	int ret;

	*cnt = 0;
	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE)
			(*cnt)++;
		else
			printk(KERN_WARNING PREFIX
				"Unsupported device scope\n");
		start += scope->length;
	}
	if (*cnt == 0)
		return 0;

	*devices = kcalloc(*cnt, sizeof(struct pci_dev *), GFP_KERNEL);
	if (!*devices)
		return -ENOMEM;

	start = tmp;
	index = 0;
	while (start < end) {
		scope = start;
		if (scope->entry_type == ACPI_DMAR_SCOPE_TYPE_ENDPOINT ||
		    scope->entry_type == ACPI_DMAR_SCOPE_TYPE_BRIDGE) {
			ret = dmar_parse_one_dev_scope(scope,
				&(*devices)[index], segment);
			if (ret) {
				kfree(*devices);
				return ret;
			}
			index ++;
		}
		start += scope->length;
	}

	return 0;
}

/**
 * dmar_parse_one_drhd - parses exactly one DMA remapping hardware definition
 * structure which uniquely represent one DMA remapping hardware unit
 * present in the platform
 */
static int __init
dmar_parse_one_drhd(struct acpi_dmar_header *header)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct dmar_drhd_unit *dmaru;
	int ret = 0;
	static int include_all;

	dmaru = kzalloc(sizeof(*dmaru), GFP_KERNEL);
	if (!dmaru)
		return -ENOMEM;

	drhd = (struct acpi_dmar_hardware_unit *)header;
	dmaru->reg_base_addr = drhd->address;
	dmaru->include_all = drhd->flags & 0x1; /* BIT0: INCLUDE_ALL */

	if (!dmaru->include_all)
		ret = dmar_parse_dev_scope((void *)(drhd + 1),
				((void *)drhd) + header->length,
				&dmaru->devices_cnt, &dmaru->devices,
				drhd->segment);
	else {
		/* Only allow one INCLUDE_ALL */
		if (include_all) {
			printk(KERN_WARNING PREFIX "Only one INCLUDE_ALL "
				"device scope is allowed\n");
			ret = -EINVAL;
		}
		include_all = 1;
	}

	if (ret || (dmaru->devices_cnt == 0 && !dmaru->include_all))
		kfree(dmaru);
	else
		dmar_register_drhd_unit(dmaru);
	return ret;
}

static int __init
dmar_parse_one_rmrr(struct acpi_dmar_header *header)
{
	struct acpi_dmar_reserved_memory *rmrr;
	struct dmar_rmrr_unit *rmrru;
	int ret = 0;

	rmrru = kzalloc(sizeof(*rmrru), GFP_KERNEL);
	if (!rmrru)
		return -ENOMEM;

	rmrr = (struct acpi_dmar_reserved_memory *)header;
	rmrru->base_address = rmrr->base_address;
	rmrru->end_address = rmrr->end_address;
	ret = dmar_parse_dev_scope((void *)(rmrr + 1),
		((void *)rmrr) + header->length,
		&rmrru->devices_cnt, &rmrru->devices, rmrr->segment);

	if (ret || (rmrru->devices_cnt == 0))
		kfree(rmrru);
	else
		dmar_register_rmrr_unit(rmrru);
	return ret;
}

static void __init
dmar_table_print_dmar_entry(struct acpi_dmar_header *header)
{
	struct acpi_dmar_hardware_unit *drhd;
	struct acpi_dmar_reserved_memory *rmrr;

	switch (header->type) {
	case ACPI_DMAR_TYPE_HARDWARE_UNIT:
		drhd = (struct acpi_dmar_hardware_unit *)header;
		printk (KERN_INFO PREFIX
			"DRHD (flags: 0x%08x)base: 0x%016Lx\n",
			drhd->flags, drhd->address);
		break;
	case ACPI_DMAR_TYPE_RESERVED_MEMORY:
		rmrr = (struct acpi_dmar_reserved_memory *)header;

		printk (KERN_INFO PREFIX
			"RMRR base: 0x%016Lx end: 0x%016Lx\n",
			rmrr->base_address, rmrr->end_address);
		break;
	}
}

/**
 * parse_dmar_table - parses the DMA reporting table
 */
static int __init
parse_dmar_table(void)
{
	struct acpi_table_dmar *dmar;
	struct acpi_dmar_header *entry_header;
	int ret = 0;

	dmar = (struct acpi_table_dmar *)dmar_tbl;
	if (!dmar)
		return -ENODEV;

	if (!dmar->width) {
		printk (KERN_WARNING PREFIX "Zero: Invalid DMAR haw\n");
		return -EINVAL;
	}

	printk (KERN_INFO PREFIX "Host address width %d\n",
		dmar->width + 1);

	entry_header = (struct acpi_dmar_header *)(dmar + 1);
	while (((unsigned long)entry_header) <
			(((unsigned long)dmar) + dmar_tbl->length)) {
		dmar_table_print_dmar_entry(entry_header);

		switch (entry_header->type) {
		case ACPI_DMAR_TYPE_HARDWARE_UNIT:
			ret = dmar_parse_one_drhd(entry_header);
			break;
		case ACPI_DMAR_TYPE_RESERVED_MEMORY:
			ret = dmar_parse_one_rmrr(entry_header);
			break;
		default:
			printk(KERN_WARNING PREFIX
				"Unknown DMAR structure type\n");
			ret = 0; /* for forward compatibility */
			break;
		}
		if (ret)
			break;

		entry_header = ((void *)entry_header + entry_header->length);
	}
	return ret;
}


int __init dmar_table_init(void)
{

	parse_dmar_table();
	if (list_empty(&dmar_drhd_units)) {
		printk(KERN_INFO PREFIX "No DMAR devices found\n");
		return -ENODEV;
	}
	return 0;
}

/**
 * early_dmar_detect - checks to see if the platform supports DMAR devices
 */
int __init early_dmar_detect(void)
{
	acpi_status status = AE_OK;

	/* if we could find DMAR table, then there are DMAR devices */
	status = acpi_get_table(ACPI_SIG_DMAR, 0,
				(struct acpi_table_header **)&dmar_tbl);

	if (ACPI_SUCCESS(status) && !dmar_tbl) {
		printk (KERN_WARNING PREFIX "Unable to map DMAR\n");
		status = AE_NOT_FOUND;
	}

	return (ACPI_SUCCESS(status) ? 1 : 0);
}
