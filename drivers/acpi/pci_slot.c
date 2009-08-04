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
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

static int debug;
static int check_sta_before_sun;

#define DRIVER_VERSION 	"0.1"
#define DRIVER_AUTHOR	"Alex Chiang <achiang@hp.com>"
#define DRIVER_DESC	"ACPI PCI Slot Detection Driver"
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
module_param(debug, bool, 0644);

#define _COMPONENT		ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_slot");

#define MY_NAME "pci_slot"
#define err(format, arg...) printk(KERN_ERR "%s: " format , MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format , MY_NAME , ## arg)
#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg);		\
	} while (0)

#define SLOT_NAME_SIZE 21		/* Inspired by #define in acpiphp.h */

struct acpi_pci_slot {
	acpi_handle root_handle;	/* handle of the root bridge */
	struct pci_slot *pci_slot;	/* corresponding pci_slot */
	struct list_head list;		/* node in the list of slots */
};

static int acpi_pci_slot_add(acpi_handle handle);
static void acpi_pci_slot_remove(acpi_handle handle);

static LIST_HEAD(slot_list);
static DEFINE_MUTEX(slot_list_lock);
static struct acpi_pci_driver acpi_pci_slot_driver = {
	.add = acpi_pci_slot_add,
	.remove = acpi_pci_slot_remove,
};

static int
check_slot(acpi_handle handle, unsigned long long *sun)
{
	int device = -1;
	unsigned long long adr, sta;
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	dbg("Checking slot on path: %s\n", (char *)buffer.pointer);

	if (check_sta_before_sun) {
		/* If SxFy doesn't have _STA, we just assume it's there */
		status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
		if (ACPI_SUCCESS(status) && !(sta & ACPI_STA_DEVICE_PRESENT))
			goto out;
	}

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);
	if (ACPI_FAILURE(status)) {
		dbg("_ADR returned %d on %s\n", status, (char *)buffer.pointer);
		goto out;
	}

	/* No _SUN == not a slot == bail */
	status = acpi_evaluate_integer(handle, "_SUN", NULL, sun);
	if (ACPI_FAILURE(status)) {
		dbg("_SUN returned %d on %s\n", status, (char *)buffer.pointer);
		goto out;
	}

	device = (adr >> 16) & 0xffff;
out:
	kfree(buffer.pointer);
	return device;
}

struct callback_args {
	acpi_walk_callback	user_function;	/* only for walk_p2p_bridge */
	struct pci_bus		*pci_bus;
	acpi_handle		root_handle;
};

/*
 * register_slot
 *
 * Called once for each SxFy object in the namespace. Don't worry about
 * calling pci_create_slot multiple times for the same pci_bus:device,
 * since each subsequent call simply bumps the refcount on the pci_slot.
 *
 * The number of calls to pci_destroy_slot from unregister_slot is
 * symmetrical.
 */
static acpi_status
register_slot(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int device;
	unsigned long long sun;
	char name[SLOT_NAME_SIZE];
	struct acpi_pci_slot *slot;
	struct pci_slot *pci_slot;
	struct callback_args *parent_context = context;
	struct pci_bus *pci_bus = parent_context->pci_bus;

	device = check_slot(handle, &sun);
	if (device < 0)
		return AE_OK;

	slot = kmalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		err("%s: cannot allocate memory\n", __func__);
		return AE_OK;
	}

	snprintf(name, sizeof(name), "%llu", sun);
	pci_slot = pci_create_slot(pci_bus, device, name, NULL);
	if (IS_ERR(pci_slot)) {
		err("pci_create_slot returned %ld\n", PTR_ERR(pci_slot));
		kfree(slot);
		return AE_OK;
	}

	slot->root_handle = parent_context->root_handle;
	slot->pci_slot = pci_slot;
	INIT_LIST_HEAD(&slot->list);
	mutex_lock(&slot_list_lock);
	list_add(&slot->list, &slot_list);
	mutex_unlock(&slot_list_lock);

	get_device(&pci_bus->dev);

	dbg("pci_slot: %p, pci_bus: %x, device: %d, name: %s\n",
		pci_slot, pci_bus->number, device, name);

	return AE_OK;
}

/*
 * walk_p2p_bridge - discover and walk p2p bridges
 * @handle: points to an acpi_pci_root
 * @context: p2p_bridge_context pointer
 *
 * Note that when we call ourselves recursively, we pass a different
 * value of pci_bus in the child_context.
 */
static acpi_status
walk_p2p_bridge(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int device, function;
	unsigned long long adr;
	acpi_status status;
	acpi_handle dummy_handle;
	acpi_walk_callback user_function;

	struct pci_dev *dev;
	struct pci_bus *pci_bus;
	struct callback_args child_context;
	struct callback_args *parent_context = context;

	pci_bus = parent_context->pci_bus;
	user_function = parent_context->user_function;

	status = acpi_get_handle(handle, "_ADR", &dummy_handle);
	if (ACPI_FAILURE(status))
		return AE_OK;

	status = acpi_evaluate_integer(handle, "_ADR", NULL, &adr);
	if (ACPI_FAILURE(status))
		return AE_OK;

	device = (adr >> 16) & 0xffff;
	function = adr & 0xffff;

	dev = pci_get_slot(pci_bus, PCI_DEVFN(device, function));
	if (!dev || !dev->subordinate)
		goto out;

	child_context.pci_bus = dev->subordinate;
	child_context.user_function = user_function;
	child_context.root_handle = parent_context->root_handle;

	dbg("p2p bridge walk, pci_bus = %x\n", dev->subordinate->number);
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     user_function, &child_context, NULL);
	if (ACPI_FAILURE(status))
		goto out;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     walk_p2p_bridge, &child_context, NULL);
out:
	pci_dev_put(dev);
	return AE_OK;
}

/*
 * walk_root_bridge - generic root bridge walker
 * @handle: points to an acpi_pci_root
 * @user_function: user callback for slot objects
 *
 * Call user_function for all objects underneath this root bridge.
 * Walk p2p bridges underneath us and call user_function on those too.
 */
static int
walk_root_bridge(acpi_handle handle, acpi_walk_callback user_function)
{
	int seg, bus;
	unsigned long long tmp;
	acpi_status status;
	acpi_handle dummy_handle;
	struct pci_bus *pci_bus;
	struct callback_args context;

	/* If the bridge doesn't have _STA, we assume it is always there */
	status = acpi_get_handle(handle, "_STA", &dummy_handle);
	if (ACPI_SUCCESS(status)) {
		status = acpi_evaluate_integer(handle, "_STA", NULL, &tmp);
		if (ACPI_FAILURE(status)) {
			info("%s: _STA evaluation failure\n", __func__);
			return 0;
		}
		if ((tmp & ACPI_STA_DEVICE_FUNCTIONING) == 0)
			/* don't register this object */
			return 0;
	}

	status = acpi_evaluate_integer(handle, "_SEG", NULL, &tmp);
	seg = ACPI_SUCCESS(status) ? tmp : 0;

	status = acpi_evaluate_integer(handle, "_BBN", NULL, &tmp);
	bus = ACPI_SUCCESS(status) ? tmp : 0;

	pci_bus = pci_find_bus(seg, bus);
	if (!pci_bus)
		return 0;

	context.pci_bus = pci_bus;
	context.user_function = user_function;
	context.root_handle = handle;

	dbg("root bridge walk, pci_bus = %x\n", pci_bus->number);
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     user_function, &context, NULL);
	if (ACPI_FAILURE(status))
		return status;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, (u32)1,
				     walk_p2p_bridge, &context, NULL);
	if (ACPI_FAILURE(status))
		err("%s: walk_p2p_bridge failure - %d\n", __func__, status);

	return status;
}

/*
 * acpi_pci_slot_add
 * @handle: points to an acpi_pci_root
 */
static int
acpi_pci_slot_add(acpi_handle handle)
{
	acpi_status status;

	status = walk_root_bridge(handle, register_slot);
	if (ACPI_FAILURE(status))
		err("%s: register_slot failure - %d\n", __func__, status);

	return status;
}

/*
 * acpi_pci_slot_remove
 * @handle: points to an acpi_pci_root
 */
static void
acpi_pci_slot_remove(acpi_handle handle)
{
	struct acpi_pci_slot *slot, *tmp;
	struct pci_bus *pbus;

	mutex_lock(&slot_list_lock);
	list_for_each_entry_safe(slot, tmp, &slot_list, list) {
		if (slot->root_handle == handle) {
			list_del(&slot->list);
			pbus = slot->pci_slot->bus;
			pci_destroy_slot(slot->pci_slot);
			put_device(&pbus->dev);
			kfree(slot);
		}
	}
	mutex_unlock(&slot_list_lock);
}

static int do_sta_before_sun(const struct dmi_system_id *d)
{
	info("%s detected: will evaluate _STA before calling _SUN\n", d->ident);
	check_sta_before_sun = 1;
	return 0;
}

static struct dmi_system_id acpi_pci_slot_dmi_table[] __initdata = {
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

static int __init
acpi_pci_slot_init(void)
{
	dmi_check_system(acpi_pci_slot_dmi_table);
	acpi_pci_register_driver(&acpi_pci_slot_driver);
	return 0;
}

static void __exit
acpi_pci_slot_exit(void)
{
	acpi_pci_unregister_driver(&acpi_pci_slot_driver);
}

module_init(acpi_pci_slot_init);
module_exit(acpi_pci_slot_exit);
