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

static LIST_HEAD(zbus_list);
static DEFINE_SPINLOCK(zbus_list_lock);
static int zpci_nb_devices;

/* zpci_bus_scan
 * @zbus: the zbus holding the zdevices
 * @ops: the pci operations
 *
 * The domain number must be set before pci_scan_root_bus is called.
 * This function can be called once the domain is known, hence
 * when the function_0 is dicovered.
 */
static int zpci_bus_scan(struct zpci_bus *zbus, int domain, struct pci_ops *ops)
{
	struct pci_bus *bus;
	int rc;

	rc = zpci_alloc_domain(domain);
	if (rc < 0)
		return rc;
	zbus->domain_nr = rc;

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

	pci_lock_rescan_remove();
	pci_stop_root_bus(zbus->bus);

	zpci_free_domain(zbus->domain_nr);
	pci_free_resource_list(&zbus->resources);

	pci_remove_root_bus(zbus->bus);
	pci_unlock_rescan_remove();

	spin_lock(&zbus_list_lock);
	list_del(&zbus->bus_next);
	spin_unlock(&zbus_list_lock);
	kfree(zbus);
}

static void zpci_bus_put(struct zpci_bus *zbus)
{
	kref_put(&zbus->kref, zpci_bus_release);
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

	return zbus;
}

int zpci_bus_device_register(struct zpci_dev *zdev, struct pci_ops *ops)
{
	struct zpci_bus *zbus;
	int rc;

	if (zpci_nb_devices == ZPCI_NR_DEVICES) {
		pr_warn("Adding PCI function %08x failed because the configured limit of %d is reached\n",
			zdev->fid, ZPCI_NR_DEVICES);
		return -ENOSPC;
	}
	zpci_nb_devices++;

	if (zdev->devfn != ZPCI_DEVFN)
		return -EINVAL;

	zbus = zpci_bus_alloc(zdev->pchid);
	if (!zbus)
		return -ENOMEM;

	zdev->zbus = zbus;
	zbus->function[ZPCI_DEVFN] = zdev;

	zpci_setup_bus_resources(zdev, &zbus->resources);
	zbus->max_bus_speed = zdev->max_bus_speed;

	rc = zpci_bus_scan(zbus, (u16)zdev->uid, ops);
	if (!rc)
		return 0;

	pr_err("Adding PCI function %08x failed\n", zdev->fid);
	zdev->zbus = NULL;
	zpci_bus_put(zbus);
	return rc;
}

void zpci_bus_device_unregister(struct zpci_dev *zdev)
{
	struct zpci_bus *zbus = zdev->zbus;

	zpci_nb_devices--;
	zbus->function[ZPCI_DEVFN] = NULL;
	zpci_bus_put(zbus);
}
