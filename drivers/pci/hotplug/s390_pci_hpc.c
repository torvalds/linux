/*
 * PCI Hot Plug Controller Driver for System z
 *
 * Copyright 2012 IBM Corp.
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 *
 * License: GPL
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <asm/pci_debug.h>
#include <asm/sclp.h>

#define SLOT_NAME_SIZE	10
static LIST_HEAD(s390_hotplug_slot_list);

static int zpci_fn_configured(enum zpci_state state)
{
	return state == ZPCI_FN_STATE_CONFIGURED ||
	       state == ZPCI_FN_STATE_ONLINE;
}

/*
 * struct slot - slot information for each *physical* slot
 */
struct slot {
	struct list_head slot_list;
	struct hotplug_slot *hotplug_slot;
	struct zpci_dev *zdev;
};

static inline int slot_configure(struct slot *slot)
{
	int ret = sclp_pci_configure(slot->zdev->fid);

	zpci_dbg(3, "conf fid:%x, rc:%d\n", slot->zdev->fid, ret);
	if (!ret)
		slot->zdev->state = ZPCI_FN_STATE_CONFIGURED;

	return ret;
}

static inline int slot_deconfigure(struct slot *slot)
{
	int ret = sclp_pci_deconfigure(slot->zdev->fid);

	zpci_dbg(3, "deconf fid:%x, rc:%d\n", slot->zdev->fid, ret);
	if (!ret)
		slot->zdev->state = ZPCI_FN_STATE_STANDBY;

	return ret;
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;
	int rc;

	if (slot->zdev->state != ZPCI_FN_STATE_STANDBY)
		return -EIO;

	rc = slot_configure(slot);
	if (rc)
		return rc;

	rc = zpci_enable_device(slot->zdev);
	if (rc)
		goto out_deconfigure;

	pci_scan_slot(slot->zdev->bus, ZPCI_DEVFN);
	pci_lock_rescan_remove();
	pci_bus_add_devices(slot->zdev->bus);
	pci_unlock_rescan_remove();

	return rc;

out_deconfigure:
	slot_deconfigure(slot);
	return rc;
}

static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;
	struct pci_dev *pdev;
	int rc;

	if (!zpci_fn_configured(slot->zdev->state))
		return -EIO;

	pdev = pci_get_slot(slot->zdev->bus, ZPCI_DEVFN);
	if (pdev) {
		pci_stop_and_remove_bus_device_locked(pdev);
		pci_dev_put(pdev);
	}

	rc = zpci_disable_device(slot->zdev);
	if (rc)
		return rc;

	return slot_deconfigure(slot);
}

static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;

	switch (slot->zdev->state) {
	case ZPCI_FN_STATE_STANDBY:
		*value = 0;
		break;
	default:
		*value = 1;
		break;
	}
	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	/* if the slot exits it always contains a function */
	*value = 1;
	return 0;
}

static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static struct hotplug_slot_ops s390_hotplug_slot_ops = {
	.enable_slot =		enable_slot,
	.disable_slot =		disable_slot,
	.get_power_status =	get_power_status,
	.get_adapter_status =	get_adapter_status,
};

int zpci_init_slot(struct zpci_dev *zdev)
{
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	char name[SLOT_NAME_SIZE];
	struct slot *slot;
	int rc;

	if (!zdev)
		return 0;

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		goto error;

	hotplug_slot = kzalloc(sizeof(*hotplug_slot), GFP_KERNEL);
	if (!hotplug_slot)
		goto error_hp;
	hotplug_slot->private = slot;

	slot->hotplug_slot = hotplug_slot;
	slot->zdev = zdev;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		goto error_info;
	hotplug_slot->info = info;

	hotplug_slot->ops = &s390_hotplug_slot_ops;
	hotplug_slot->release = &release_slot;

	get_power_status(hotplug_slot, &info->power_status);
	get_adapter_status(hotplug_slot, &info->adapter_status);

	snprintf(name, SLOT_NAME_SIZE, "%08x", zdev->fid);
	rc = pci_hp_register(slot->hotplug_slot, zdev->bus,
			     ZPCI_DEVFN, name);
	if (rc)
		goto error_reg;

	list_add(&slot->slot_list, &s390_hotplug_slot_list);
	return 0;

error_reg:
	kfree(info);
error_info:
	kfree(hotplug_slot);
error_hp:
	kfree(slot);
error:
	return -ENOMEM;
}

void zpci_exit_slot(struct zpci_dev *zdev)
{
	struct slot *slot, *next;

	list_for_each_entry_safe(slot, next, &s390_hotplug_slot_list,
				 slot_list) {
		if (slot->zdev != zdev)
			continue;
		list_del(&slot->slot_list);
		pci_hp_deregister(slot->hotplug_slot);
	}
}
