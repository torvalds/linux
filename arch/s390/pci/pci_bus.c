// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s):
 *   Pierre Morel <pmorel@linux.ibm.com>
 *
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/jump_label.h>
#include <linux/pci.h>
#include <linux/printk.h>

#include <asm/pci_clp.h>
#include <asm/pci_dma.h>

#include "pci_bus.h"
#include "pci_iov.h"

static LIST_HEAD(zbus_list);
static DEFINE_SPINLOCK(zbus_list_lock);
static int zpci_nb_devices;

/* zpci_bus_scan_device - Scan a single device adding it to the PCI core
 * @zdev: the zdev to be scanned
 *
 * Scans the PCI function making it available to the common PCI code.
 *
 * Return: 0 on success, an error value otherwise
 */
int zpci_bus_scan_device(struct zpci_dev *zdev)
{
	struct pci_dev *pdev;

	pdev = pci_scan_single_device(zdev->zbus->bus, zdev->devfn);
	if (!pdev)
		return -ENODEV;

	pci_bus_add_device(pdev);
	pci_lock_rescan_remove();
	pci_bus_add_devices(zdev->zbus->bus);
	pci_unlock_rescan_remove();

	return 0;
}

/* zpci_bus_remove_device - Removes the given zdev from the PCI core
 * @zdev: the zdev to be removed from the PCI core
 * @set_error: if true the device's error state is set to permanent failure
 *
 * Sets a zPCI device to a configured but offline state; the zPCI
 * device is still accessible through its hotplug slot and the zPCI
 * API but is removed from the common code PCI bus, making it
 * no longer available to drivers.
 */
void zpci_bus_remove_device(struct zpci_dev *zdev, bool set_error)
{
	struct zpci_bus *zbus = zdev->zbus;
	struct pci_dev *pdev;

	if (!zdev->zbus->bus)
		return;

	pdev = pci_get_slot(zbus->bus, zdev->devfn);
	if (pdev) {
		if (set_error)
			pdev->error_state = pci_channel_io_perm_failure;
		if (pdev->is_virtfn) {
			zpci_iov_remove_virtfn(pdev, zdev->vfn);
			/* balance pci_get_slot */
			pci_dev_put(pdev);
			return;
		}
		pci_stop_and_remove_bus_device_locked(pdev);
		/* balance pci_get_slot */
		pci_dev_put(pdev);
	}
}

/* zpci_bus_scan - Scan the PCI bus associated with this zbus
 * @zbus: the zbus holding the zdevices
 * @f0: function 0 of the bus
 * @ops: the pci operations
 *
 * Function zero is taken as a parameter as this is used to determine the
 * domain, multifunction property and maximum bus speed of the entire bus.
 *
 * Return: 0 on success, an error code otherwise
 */
static int zpci_bus_scan(struct zpci_bus *zbus, struct zpci_dev *f0, struct pci_ops *ops)
{
	struct pci_bus *bus;
	int domain;

	domain = zpci_alloc_domain((u16)f0->uid);
	if (domain < 0)
		return domain;

	zbus->domain_nr = domain;
	zbus->multifunction = f0->rid_available;
	zbus->max_bus_speed = f0->max_bus_speed;

	/*
	 * Note that the zbus->resources are taken over and zbus->resources
	 * is empty after a successful call
	 */
	bus = pci_scan_root_bus(NULL, ZPCI_BUS_NR, ops, zbus, &zbus->resources);
	if (!bus) {
		zpci_free_domain(zbus->domain_nr);
		return -EFAULT;
	}

	zbus->bus = bus;
	pci_bus_add_devices(bus);
	return 0;
}

static void zpci_bus_release(struct kref *kref)
{
	struct zpci_bus *zbus = container_of(kref, struct zpci_bus, kref);

	if (zbus->bus) {
		pci_lock_rescan_remove();
		pci_stop_root_bus(zbus->bus);

		zpci_free_domain(zbus->domain_nr);
		pci_free_resource_list(&zbus->resources);

		pci_remove_root_bus(zbus->bus);
		pci_unlock_rescan_remove();
	}

	spin_lock(&zbus_list_lock);
	list_del(&zbus->bus_next);
	spin_unlock(&zbus_list_lock);
	kfree(zbus);
}

static void zpci_bus_put(struct zpci_bus *zbus)
{
	kref_put(&zbus->kref, zpci_bus_release);
}

static struct zpci_bus *zpci_bus_get(int pchid)
{
	struct zpci_bus *zbus;

	spin_lock(&zbus_list_lock);
	list_for_each_entry(zbus, &zbus_list, bus_next) {
		if (pchid == zbus->pchid) {
			kref_get(&zbus->kref);
			goto out_unlock;
		}
	}
	zbus = NULL;
out_unlock:
	spin_unlock(&zbus_list_lock);
	return zbus;
}

static struct zpci_bus *zpci_bus_alloc(int pchid)
{
	struct zpci_bus *zbus;

	zbus = kzalloc(sizeof(*zbus), GFP_KERNEL);
	if (!zbus)
		return NULL;

	zbus->pchid = pchid;
	INIT_LIST_HEAD(&zbus->bus_next);
	spin_lock(&zbus_list_lock);
	list_add_tail(&zbus->bus_next, &zbus_list);
	spin_unlock(&zbus_list_lock);

	kref_init(&zbus->kref);
	INIT_LIST_HEAD(&zbus->resources);

	zbus->bus_resource.start = 0;
	zbus->bus_resource.end = ZPCI_BUS_NR;
	zbus->bus_resource.flags = IORESOURCE_BUS;
	pci_add_resource(&zbus->resources, &zbus->bus_resource);

	return zbus;
}

void pcibios_bus_add_device(struct pci_dev *pdev)
{
	struct zpci_dev *zdev = to_zpci(pdev);

	/*
	 * With pdev->no_vf_scan the common PCI probing code does not
	 * perform PF/VF linking.
	 */
	if (zdev->vfn) {
		zpci_iov_setup_virtfn(zdev->zbus, pdev, zdev->vfn);
		pdev->no_command_memory = 1;
	}
}

static int zpci_bus_add_device(struct zpci_bus *zbus, struct zpci_dev *zdev)
{
	struct resource_entry *window, *n;
	struct resource *res;
	struct pci_dev *pdev;
	struct pci_bus *bus;
	int rc;

	bus = zbus->bus;
	if (!bus)
		return -EINVAL;

	pdev = pci_get_slot(bus, zdev->devfn);
	if (pdev) {
		/* Device is already known. */
		pci_dev_put(pdev);
		return 0;
	}

	rc = zpci_init_slot(zdev);
	if (rc)
		return rc;
	zdev->has_hp_slot = 1;

	resource_list_for_each_entry_safe(window, n, &zbus->resources) {
		res = window->res;
		pci_bus_add_resource(bus, res, 0);
	}

	return zpci_bus_scan_device(zdev);
}

static void zpci_bus_add_devices(struct zpci_bus *zbus)
{
	int i;

	for (i = 1; i < ZPCI_FUNCTIONS_PER_BUS; i++)
		if (zbus->function[i])
			zpci_bus_add_device(zbus, zbus->function[i]);
}

int zpci_bus_device_register(struct zpci_dev *zdev, struct pci_ops *ops)
{
	struct zpci_bus *zbus = NULL;
	int rc = -EBADF;

	if (zpci_nb_devices == ZPCI_NR_DEVICES) {
		pr_warn("Adding PCI function %08x failed because the configured limit of %d is reached\n",
			zdev->fid, ZPCI_NR_DEVICES);
		return -ENOSPC;
	}
	zpci_nb_devices++;

	if (zdev->devfn >= ZPCI_FUNCTIONS_PER_BUS)
		return -EINVAL;

	if (!s390_pci_no_rid && zdev->rid_available)
		zbus = zpci_bus_get(zdev->pchid);

	if (!zbus) {
		zbus = zpci_bus_alloc(zdev->pchid);
		if (!zbus)
			return -ENOMEM;
	}

	zdev->zbus = zbus;
	if (zbus->function[zdev->devfn]) {
		pr_err("devfn %04x is already assigned\n", zdev->devfn);
		goto error; /* rc already set */
	}
	zbus->function[zdev->devfn] = zdev;

	zpci_setup_bus_resources(zdev, &zbus->resources);

	if (zbus->bus) {
		if (!zbus->multifunction) {
			WARN_ONCE(1, "zbus is not multifunction\n");
			goto error_bus;
		}
		if (!zdev->rid_available) {
			WARN_ONCE(1, "rid_available not set for multifunction\n");
			goto error_bus;
		}
		rc = zpci_bus_add_device(zbus, zdev);
		if (rc)
			goto error_bus;
	} else if (zdev->devfn == 0) {
		if (zbus->multifunction && !zdev->rid_available) {
			WARN_ONCE(1, "rid_available not set on function 0 for multifunction\n");
			goto error_bus;
		}
		rc = zpci_bus_scan(zbus, zdev, ops);
		if (rc)
			goto error_bus;
		zpci_bus_add_devices(zbus);
		rc = zpci_init_slot(zdev);
		if (rc)
			goto error_bus;
		zdev->has_hp_slot = 1;
	} else {
		zbus->multifunction = 1;
	}

	return 0;

error_bus:
	zpci_nb_devices--;
	zbus->function[zdev->devfn] = NULL;
error:
	pr_err("Adding PCI function %08x failed\n", zdev->fid);
	zpci_bus_put(zbus);
	return rc;
}

void zpci_bus_device_unregister(struct zpci_dev *zdev)
{
	struct zpci_bus *zbus = zdev->zbus;

	zpci_nb_devices--;
	zbus->function[zdev->devfn] = NULL;
	zpci_bus_put(zbus);
}
