// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright Red Hat

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>
#include "ice_adapter.h"
#include "ice.h"

static DEFINE_XARRAY(ice_adapters);
static DEFINE_MUTEX(ice_adapters_mutex);

/* PCI bus number is 8 bits. Slot is 5 bits. Domain can have the rest. */
#define INDEX_FIELD_DOMAIN GENMASK(BITS_PER_LONG - 1, 13)
#define INDEX_FIELD_DEV    GENMASK(31, 16)
#define INDEX_FIELD_BUS    GENMASK(12, 5)
#define INDEX_FIELD_SLOT   GENMASK(4, 0)

static unsigned long ice_adapter_index(const struct pci_dev *pdev)
{
	unsigned int domain = pci_domain_nr(pdev->bus);

	WARN_ON(domain > FIELD_MAX(INDEX_FIELD_DOMAIN));

	switch (pdev->device) {
	case ICE_DEV_ID_E825C_BACKPLANE:
	case ICE_DEV_ID_E825C_QSFP:
	case ICE_DEV_ID_E825C_SFP:
	case ICE_DEV_ID_E825C_SGMII:
		return FIELD_PREP(INDEX_FIELD_DEV, pdev->device);
	default:
		return FIELD_PREP(INDEX_FIELD_DOMAIN, domain) |
		       FIELD_PREP(INDEX_FIELD_BUS,    pdev->bus->number) |
		       FIELD_PREP(INDEX_FIELD_SLOT,   PCI_SLOT(pdev->devfn));
	}
}

static struct ice_adapter *ice_adapter_new(void)
{
	struct ice_adapter *adapter;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	spin_lock_init(&adapter->ptp_gltsyn_time_lock);
	refcount_set(&adapter->refcount, 1);

	mutex_init(&adapter->ports.lock);
	INIT_LIST_HEAD(&adapter->ports.ports);

	return adapter;
}

static void ice_adapter_free(struct ice_adapter *adapter)
{
	WARN_ON(!list_empty(&adapter->ports.ports));
	mutex_destroy(&adapter->ports.lock);

	kfree(adapter);
}

/**
 * ice_adapter_get - Get a shared ice_adapter structure.
 * @pdev: Pointer to the pci_dev whose driver is getting the ice_adapter.
 *
 * Gets a pointer to a shared ice_adapter structure. Physical functions (PFs)
 * of the same multi-function PCI device share one ice_adapter structure.
 * The ice_adapter is reference-counted. The PF driver must use ice_adapter_put
 * to release its reference.
 *
 * Context: Process, may sleep.
 * Return:  Pointer to ice_adapter on success.
 *          ERR_PTR() on error. -ENOMEM is the only possible error.
 */
struct ice_adapter *ice_adapter_get(const struct pci_dev *pdev)
{
	unsigned long index = ice_adapter_index(pdev);
	struct ice_adapter *adapter;
	int err;

	scoped_guard(mutex, &ice_adapters_mutex) {
		err = xa_insert(&ice_adapters, index, NULL, GFP_KERNEL);
		if (err == -EBUSY) {
			adapter = xa_load(&ice_adapters, index);
			refcount_inc(&adapter->refcount);
			return adapter;
		}
		if (err)
			return ERR_PTR(err);

		adapter = ice_adapter_new();
		if (!adapter)
			return ERR_PTR(-ENOMEM);
		xa_store(&ice_adapters, index, adapter, GFP_KERNEL);
	}
	return adapter;
}

/**
 * ice_adapter_put - Release a reference to the shared ice_adapter structure.
 * @pdev: Pointer to the pci_dev whose driver is releasing the ice_adapter.
 *
 * Releases the reference to ice_adapter previously obtained with
 * ice_adapter_get.
 *
 * Context: Process, may sleep.
 */
void ice_adapter_put(const struct pci_dev *pdev)
{
	unsigned long index = ice_adapter_index(pdev);
	struct ice_adapter *adapter;

	scoped_guard(mutex, &ice_adapters_mutex) {
		adapter = xa_load(&ice_adapters, index);
		if (WARN_ON(!adapter))
			return;
		if (!refcount_dec_and_test(&adapter->refcount))
			return;

		WARN_ON(xa_erase(&ice_adapters, index) != adapter);
	}
	ice_adapter_free(adapter);
}
