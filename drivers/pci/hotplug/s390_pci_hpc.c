// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Hot Plug Controller Driver for System z
 *
 * Copyright 2012 IBM Corp.
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
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

static int zpci_fn_configured(enum zpci_state state)
{
	return state == ZPCI_FN_STATE_CONFIGURED ||
	       state == ZPCI_FN_STATE_ONLINE;
}

static inline int zdev_configure(struct zpci_dev *zdev)
{
	int ret = sclp_pci_configure(zdev->fid);

	zpci_dbg(3, "conf fid:%x, rc:%d\n", zdev->fid, ret);
	if (!ret)
		zdev->state = ZPCI_FN_STATE_CONFIGURED;

	return ret;
}

static inline int zdev_deconfigure(struct zpci_dev *zdev)
{
	int ret = sclp_pci_deconfigure(zdev->fid);

	zpci_dbg(3, "deconf fid:%x, rc:%d\n", zdev->fid, ret);
	if (!ret)
		zdev->state = ZPCI_FN_STATE_STANDBY;

	return ret;
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);
	struct zpci_bus *zbus = zdev->zbus;
	int rc;

	if (zdev->state != ZPCI_FN_STATE_STANDBY)
		return -EIO;

	rc = zdev_configure(zdev);
	if (rc)
		return rc;

	rc = zpci_enable_device(zdev);
	if (rc)
		goto out_deconfigure;

	pci_scan_slot(zbus->bus, zdev->devfn);
	pci_lock_rescan_remove();
	pci_bus_add_devices(zbus->bus);
	pci_unlock_rescan_remove();

	return rc;

out_deconfigure:
	zdev_deconfigure(zdev);
	return rc;
}

static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);
	struct pci_dev *pdev;
	struct zpci_bus *zbus = zdev->zbus;
	int rc;

	if (!zpci_fn_configured(zdev->state))
		return -EIO;

	pdev = pci_get_slot(zbus->bus, zdev->devfn);
	if (pdev) {
		if (pci_num_vf(pdev))
			return -EBUSY;

		pci_stop_and_remove_bus_device_locked(pdev);
		pci_dev_put(pdev);
	}

	rc = zpci_disable_device(zdev);
	if (rc)
		return rc;

	return zdev_deconfigure(zdev);
}

static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);

	switch (zdev->state) {
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

static const struct hotplug_slot_ops s390_hotplug_slot_ops = {
	.enable_slot =		enable_slot,
	.disable_slot =		disable_slot,
	.get_power_status =	get_power_status,
	.get_adapter_status =	get_adapter_status,
};

int zpci_init_slot(struct zpci_dev *zdev)
{
	char name[SLOT_NAME_SIZE];
	struct zpci_bus *zbus = zdev->zbus;

	zdev->hotplug_slot.ops = &s390_hotplug_slot_ops;

	snprintf(name, SLOT_NAME_SIZE, "%08x", zdev->fid);
	return pci_hp_register(&zdev->hotplug_slot, zbus->bus,
			       zdev->devfn, name);
}

void zpci_exit_slot(struct zpci_dev *zdev)
{
	pci_hp_deregister(&zdev->hotplug_slot);
}
