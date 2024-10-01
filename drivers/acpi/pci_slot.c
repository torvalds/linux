// SPDX-License-Identifier: GPL-2.0-only
/*
 *  pci_slot.c - ACPI PCI Slot Driver
 *
 *  The code here is heavily leveraged from the acpiphp module.
 *  Thanks to Matthew Wilcox <matthew@wil.cx> for much guidance.
 *  Thanks to Kenji Kaneshige <kaneshige.kenji@jp.fujitsu.com> for code
 *  review and fixes.
 *
 *  Copyright (C) 2007-2008 Hewlett-Packard Development Company, L.P.
 *  	Alex Chiang <achiang@hp.com>
 *
 *  Copyright (C) 2013 Huawei Tech. Co., Ltd.
 *	Jiang Liu <jiang.liu@huawei.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/pci-acpi.h>

static int check_sta_before_sun;

#define SLOT_NAME_SIZE 21		/* Inspired by #define in acpiphp.h */

struct acpi_pci_slot {
	struct pci_slot *pci_slot;	/* corresponding pci_slot */
	struct list_head list;		/* node in the list of slots */
};

static LIST_HEAD(slot_list);
static DEFINE_MUTEX(slot_list_lock);

static int
check_slot(acpi_handle handle, unsigned long long *sun)
{
	int device = -1;
	unsigned long long adr, sta;
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	pr_debug("Checking slot on path: %s\n", (char *)buffer.pointer);

	if (check_sta_before_sun) {
		/* If SxFy doesn't have _STA, we just assume it's there */
		status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
		if (ACPI_SUCCESS(status) && !(sta & ACPI_STA_DEVICE_PRESENT))
			goto out;
	}

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);
	if (ACPI_FAILURE(status)) {
		pr_debug("_ADR returned %d on %s\n",
			 status, (char *)buffer.pointer);
		goto out;
	}

	/* No _SUN == not a slot == bail */
	status = acpi_evaluate_integer(handle, "_SUN", NULL, sun);
	if (ACPI_FAILURE(status)) {
		pr_debug("_SUN returned %d on %s\n",
			 status, (char *)buffer.pointer);
		goto out;
	}

	device = (adr >> 16) & 0xffff;
out:
	kfree(buffer.pointer);
	return device;
}

/*
 * Check whether handle has an associated slot and create PCI slot if it has.
 */
static acpi_status
register_slot(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int device;
	unsigned long long sun;
	char name[SLOT_NAME_SIZE];
	struct acpi_pci_slot *slot;
	struct pci_slot *pci_slot;
	struct pci_bus *pci_bus = context;

	device = check_slot(handle, &sun);
	if (device < 0)
		return AE_OK;

	/*
	 * There may be multiple PCI functions associated with the same slot.
	 * Check whether PCI slot has already been created for this PCI device.
	 */
	list_for_each_entry(slot, &slot_list, list) {
		pci_slot = slot->pci_slot;
		if (pci_slot->bus == pci_bus && pci_slot->number == device)
			return AE_OK;
	}

	slot = kmalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return AE_OK;

	snprintf(name, sizeof(name), "%llu", sun);
	pci_slot = pci_create_slot(pci_bus, device, name, NULL);
	if (IS_ERR(pci_slot)) {
		pr_err("pci_create_slot returned %ld\n", PTR_ERR(pci_slot));
		kfree(slot);
		return AE_OK;
	}

	slot->pci_slot = pci_slot;
	list_add(&slot->list, &slot_list);

	get_device(&pci_bus->dev);

	pr_debug("%p, pci_bus: %x, device: %d, name: %s\n",
		 pci_slot, pci_bus->number, device, name);

	return AE_OK;
}

void acpi_pci_slot_enumerate(struct pci_bus *bus)
{
	acpi_handle handle = ACPI_HANDLE(bus->bridge);

	if (handle) {
		mutex_lock(&slot_list_lock);
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				    register_slot, NULL, bus, NULL);
		mutex_unlock(&slot_list_lock);
	}
}

void acpi_pci_slot_remove(struct pci_bus *bus)
{
	struct acpi_pci_slot *slot, *tmp;

	mutex_lock(&slot_list_lock);
	list_for_each_entry_safe(slot, tmp, &slot_list, list) {
		if (slot->pci_slot->bus == bus) {
			list_del(&slot->list);
			pci_destroy_slot(slot->pci_slot);
			put_device(&bus->dev);
			kfree(slot);
		}
	}
	mutex_unlock(&slot_list_lock);
}

static int do_sta_before_sun(const struct dmi_system_id *d)
{
	pr_info("%s detected: will evaluate _STA before calling _SUN\n",
		d->ident);
	check_sta_before_sun = 1;
	return 0;
}

static const struct dmi_system_id acpi_pci_slot_dmi_table[] __initconst = {
	/*
	 * Fujitsu Primequest machines will return 1023 to indicate an
	 * error if the _SUN method is evaluated on SxFy objects that
	 * are not present (as indicated by _STA), so for those machines,
	 * we want to check _STA before evaluating _SUN.
	 */
	{
	 .callback = do_sta_before_sun,
	 .ident = "Fujitsu PRIMEQUEST",
	 .matches = {
		DMI_MATCH(DMI_BIOS_VENDOR, "FUJITSU LIMITED"),
		DMI_MATCH(DMI_BIOS_VERSION, "PRIMEQUEST"),
		},
	},
	{}
};

void __init acpi_pci_slot_init(void)
{
	dmi_check_system(acpi_pci_slot_dmi_table);
}
