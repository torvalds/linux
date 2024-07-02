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

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);
	int rc;

	mutex_lock(&zdev->state_lock);
	if (zdev->state != ZPCI_FN_STATE_STANDBY) {
		rc = -EIO;
		goto out;
	}

	rc = sclp_pci_configure(zdev->fid);
	zpci_dbg(3, "conf fid:%x, rc:%d\n", zdev->fid, rc);
	if (rc)
		goto out;
	zdev->state = ZPCI_FN_STATE_CONFIGURED;

	rc = zpci_scan_configured_device(zdev, zdev->fh);
out:
	mutex_unlock(&zdev->state_lock);
	return rc;
}

static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);
	struct pci_dev *pdev = NULL;
	int rc;

	mutex_lock(&zdev->state_lock);
	if (zdev->state != ZPCI_FN_STATE_CONFIGURED) {
		rc = -EIO;
		goto out;
	}

	pdev = pci_get_slot(zdev->zbus->bus, zdev->devfn);
	if (pdev && pci_num_vf(pdev)) {
		pci_dev_put(pdev);
		rc = -EBUSY;
		goto out;
	}

	rc = zpci_deconfigure_device(zdev);
out:
	mutex_unlock(&zdev->state_lock);
	if (pdev)
		pci_dev_put(pdev);
	return rc;
}

static int reset_slot(struct hotplug_slot *hotplug_slot, bool probe)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);
	int rc = -EIO;

	/*
	 * If we can't get the zdev->state_lock the device state is
	 * currently undergoing a transition and we bail out - just
	 * the same as if the device's state is not configured at all.
	 */
	if (!mutex_trylock(&zdev->state_lock))
		return rc;

	/* We can reset only if the function is configured */
	if (zdev->state != ZPCI_FN_STATE_CONFIGURED)
		goto out;

	if (probe) {
		rc = 0;
		goto out;
	}

	rc = zpci_hot_reset_device(zdev);
out:
	mutex_unlock(&zdev->state_lock);
	return rc;
}

static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct zpci_dev *zdev = container_of(hotplug_slot, struct zpci_dev,
					     hotplug_slot);

	*value = zpci_is_device_configured(zdev) ? 1 : 0;
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
	.reset_slot =		reset_slot,
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
