// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright Red Hat

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

#define ICE_ADAPTER_FIXED_INDEX	BIT_ULL(63)

#define ICE_ADAPTER_INDEX_E825C	\
	(ICE_DEV_ID_E825C_BACKPLANE | ICE_ADAPTER_FIXED_INDEX)

static u64 ice_adapter_index(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case ICE_DEV_ID_E825C_BACKPLANE:
	case ICE_DEV_ID_E825C_QSFP:
	case ICE_DEV_ID_E825C_SFP:
	case ICE_DEV_ID_E825C_SGMII:
		/* E825C devices have multiple NACs which are connected to the
		 * same clock source, and which must share the same
		 * ice_adapter structure. We can't use the serial number since
		 * each NAC has its own NVM generated with its own unique
		 * Device Serial Number. Instead, rely on the embedded nature
		 * of the E825C devices, and use a fixed index. This relies on
		 * the fact that all E825C physical functions in a given
		 * system are part of the same overall device.
		 */
		return ICE_ADAPTER_INDEX_E825C;
	default:
		return pci_get_dsn(pdev) & ~ICE_ADAPTER_FIXED_INDEX;
	}
}

static unsigned long ice_adapter_xa_index(struct pci_dev *pdev)
{
	u64 index = ice_adapter_index(pdev);

#if BITS_PER_LONG == 64
	return index;
#else
	return (u32)index ^ (u32)(index >> 32);
#endif
}

static struct ice_adapter *ice_adapter_new(struct pci_dev *pdev)
{
	struct ice_adapter *adapter;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	adapter->index = ice_adapter_index(pdev);
	spin_lock_init(&adapter->ptp_gltsyn_time_lock);
	spin_lock_init(&adapter->txq_ctx_lock);
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
struct ice_adapter *ice_adapter_get(struct pci_dev *pdev)
{
	struct ice_adapter *adapter;
	unsigned long index;
	int err;

	index = ice_adapter_xa_index(pdev);
	scoped_guard(mutex, &ice_adapters_mutex) {
		adapter = xa_load(&ice_adapters, index);
		if (adapter) {
			refcount_inc(&adapter->refcount);
			WARN_ON_ONCE(adapter->index != ice_adapter_index(pdev));
			return adapter;
		}
		err = xa_reserve(&ice_adapters, index, GFP_KERNEL);
		if (err)
			return ERR_PTR(err);

		adapter = ice_adapter_new(pdev);
		if (!adapter) {
			xa_release(&ice_adapters, index);
			return ERR_PTR(-ENOMEM);
		}
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
void ice_adapter_put(struct pci_dev *pdev)
{
	struct ice_adapter *adapter;
	unsigned long index;

	index = ice_adapter_xa_index(pdev);
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
