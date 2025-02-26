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
static DEFINE_MUTEX(zbus_list_lock);
static int zpci_nb_devices;

/* zpci_bus_prepare_device - Prepare a zPCI function for scanning
 * @zdev: the zPCI function to be prepared
 *
 * The PCI resources for the function are set up and added to its zbus and the
 * function is enabled. The function must be added to a zbus which must have
 * a PCI bus created. If an error occurs the zPCI function is not enabled.
 *
 * Return: 0 on success, an error code otherwise
 */
static int zpci_bus_prepare_device(struct zpci_dev *zdev)
{
	int rc, i;

	if (!zdev_enabled(zdev)) {
		rc = zpci_enable_device(zdev);
		if (rc)
			return rc;
	}

	if (!zdev->has_resources) {
		zpci_setup_bus_resources(zdev);
		for (i = 0; i < PCI_STD_NUM_BARS; i++) {
			if (zdev->bars[i].res)
				pci_bus_add_resource(zdev->zbus->bus, zdev->bars[i].res);
		}
	}

	return 0;
}

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
	int rc;

	rc = zpci_bus_prepare_device(zdev);
	if (rc)
		return rc;

	pdev = pci_scan_single_device(zdev->zbus->bus, zdev->devfn);
	if (!pdev)
		return -ENODEV;

	pci_lock_rescan_remove();
	pci_bus_add_device(pdev);
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

/* zpci_bus_scan_bus - Scan all configured zPCI functions on the bus
 * @zbus: the zbus to be scanned
 *
 * Enables and scans all PCI functions on the bus making them available to the
 * common PCI code. If a PCI function fails to be initialized an error will be
 * returned but attempts will still be made for all other functions on the bus.
 *
 * Return: 0 on success, an error value otherwise
 */
int zpci_bus_scan_bus(struct zpci_bus *zbus)
{
	struct zpci_dev *zdev;
	int devfn, rc, ret = 0;

	for (devfn = 0; devfn < ZPCI_FUNCTIONS_PER_BUS; devfn++) {
		zdev = zbus->function[devfn];
		if (zdev && zdev->state == ZPCI_FN_STATE_CONFIGURED) {
			rc = zpci_bus_prepare_device(zdev);
			if (rc)
				ret = -EIO;
		}
	}

	pci_lock_rescan_remove();
	pci_scan_child_bus(zbus->bus);
	pci_bus_add_devices(zbus->bus);
	pci_unlock_rescan_remove();

	return ret;
}

/* zpci_bus_scan_busses - Scan all registered busses
 *
 * Scan all available zbusses
 *
 */
void zpci_bus_scan_busses(void)
{
	struct zpci_bus *zbus = NULL;

	mutex_lock(&zbus_list_lock);
	list_for_each_entry(zbus, &zbus_list, bus_next) {
		zpci_bus_scan_bus(zbus);
		cond_resched();
	}
	mutex_unlock(&zbus_list_lock);
}

static bool zpci_bus_is_multifunction_root(struct zpci_dev *zdev)
{
	return !s390_pci_no_rid && zdev->rid_available &&
		!zdev->vfn;
}

/* zpci_bus_create_pci_bus - Create the PCI bus associated with this zbus
 * @zbus: the zbus holding the zdevices
 * @fr: PCI root function that will determine the bus's domain, and bus speed
 * @ops: the pci operations
 *
 * The PCI function @fr determines the domain (its UID), multifunction property
 * and maximum bus speed of the entire bus.
 *
 * Return: 0 on success, an error code otherwise
 */
static int zpci_bus_create_pci_bus(struct zpci_bus *zbus, struct zpci_dev *fr, struct pci_ops *ops)
{
	struct pci_bus *bus;
	int domain;

	domain = zpci_alloc_domain((u16)fr->uid);
	if (domain < 0)
		return domain;

	zbus->domain_nr = domain;
	zbus->multifunction = zpci_bus_is_multifunction_root(fr);
	zbus->max_bus_speed = fr->max_bus_speed;

	/*
	 * Note that the zbus->resources are taken over and zbus->resources
	 * is empty after a successful call
	 */
	bus = pci_create_root_bus(NULL, ZPCI_BUS_NR, ops, zbus, &zbus->resources);
	if (!bus) {
		zpci_free_domain(zbus->domain_nr);
		return -EFAULT;
	}

	zbus->bus = bus;

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

	mutex_lock(&zbus_list_lock);
	list_del(&zbus->bus_next);
	mutex_unlock(&zbus_list_lock);
	kfree(zbus);
}

static void zpci_bus_put(struct zpci_bus *zbus)
{
	kref_put(&zbus->kref, zpci_bus_release);
}

static struct zpci_bus *zpci_bus_get(int topo, bool topo_is_tid)
{
	struct zpci_bus *zbus;

	mutex_lock(&zbus_list_lock);
	list_for_each_entry(zbus, &zbus_list, bus_next) {
		if (!zbus->multifunction)
			continue;
		if (topo_is_tid == zbus->topo_is_tid && topo == zbus->topo) {
			kref_get(&zbus->kref);
			goto out_unlock;
		}
	}
	zbus = NULL;
out_unlock:
	mutex_unlock(&zbus_list_lock);
	return zbus;
}

static struct zpci_bus *zpci_bus_alloc(int topo, bool topo_is_tid)
{
	struct zpci_bus *zbus;

	zbus = kzalloc(sizeof(*zbus), GFP_KERNEL);
	if (!zbus)
		return NULL;

	zbus->topo = topo;
	zbus->topo_is_tid = topo_is_tid;
	INIT_LIST_HEAD(&zbus->bus_next);
	mutex_lock(&zbus_list_lock);
	list_add_tail(&zbus->bus_next, &zbus_list);
	mutex_unlock(&zbus_list_lock);

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
	int rc = -EINVAL;

	if (zbus->multifunction) {
		if (!zdev->rid_available) {
			WARN_ONCE(1, "rid_available not set for multifunction\n");
			return rc;
		}
		zdev->devfn = zdev->rid & ZPCI_RID_MASK_DEVFN;
	}

	if (zbus->function[zdev->devfn]) {
		pr_err("devfn %04x is already assigned\n", zdev->devfn);
		return rc;
	}
	zdev->zbus = zbus;
	zbus->function[zdev->devfn] = zdev;
	zpci_nb_devices++;

	rc = zpci_init_slot(zdev);
	if (rc)
		goto error;
	zdev->has_hp_slot = 1;

	return 0;

error:
	zbus->function[zdev->devfn] = NULL;
	zdev->zbus = NULL;
	zpci_nb_devices--;
	return rc;
}

static bool zpci_bus_is_isolated_vf(struct zpci_bus *zbus, struct zpci_dev *zdev)
{
	struct pci_dev *pdev;

	pdev = zpci_iov_find_parent_pf(zbus, zdev);
	if (!pdev)
		return true;
	pci_dev_put(pdev);
	return false;
}

int zpci_bus_device_register(struct zpci_dev *zdev, struct pci_ops *ops)
{
	bool topo_is_tid = zdev->tid_avail;
	struct zpci_bus *zbus = NULL;
	int topo, rc = -EBADF;

	if (zpci_nb_devices == ZPCI_NR_DEVICES) {
		pr_warn("Adding PCI function %08x failed because the configured limit of %d is reached\n",
			zdev->fid, ZPCI_NR_DEVICES);
		return -ENOSPC;
	}

	topo = topo_is_tid ? zdev->tid : zdev->pchid;
	zbus = zpci_bus_get(topo, topo_is_tid);
	/*
	 * An isolated VF gets its own domain/bus even if there exists
	 * a matching domain/bus already
	 */
	if (zbus && zpci_bus_is_isolated_vf(zbus, zdev)) {
		zpci_bus_put(zbus);
		zbus = NULL;
	}

	if (!zbus) {
		zbus = zpci_bus_alloc(topo, topo_is_tid);
		if (!zbus)
			return -ENOMEM;
	}

	if (!zbus->bus) {
		/* The UID of the first PCI function registered with a zpci_bus
		 * is used as the domain number for that bus. Currently there
		 * is exactly one zpci_bus per domain.
		 */
		rc = zpci_bus_create_pci_bus(zbus, zdev, ops);
		if (rc)
			goto error;
	}

	rc = zpci_bus_add_device(zbus, zdev);
	if (rc)
		goto error;

	return 0;

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
