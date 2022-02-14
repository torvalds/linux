// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

#include <nvhe/iommu.h>

enum {
	IOMMU_DRIVER_NOT_READY = 0,
	IOMMU_DRIVER_INITIALIZING,
	IOMMU_DRIVER_READY,
};

struct pkvm_iommu_driver {
	const struct pkvm_iommu_ops *ops;
	atomic_t state;
};

static struct pkvm_iommu_driver iommu_drivers[PKVM_IOMMU_NR_DRIVERS];

/* IOMMU device list. Must only be accessed with host_mmu.lock held. */
static LIST_HEAD(iommu_list);

static void assert_host_component_locked(void)
{
	hyp_assert_lock_held(&host_mmu.lock);
}

/*
 * Find IOMMU driver by its ID. The input ID is treated as unstrusted
 * and is properly validated.
 */
static inline struct pkvm_iommu_driver *get_driver(enum pkvm_iommu_driver_id id)
{
	size_t index = (size_t)id;

	if (index >= ARRAY_SIZE(iommu_drivers))
		return NULL;

	return &iommu_drivers[index];
}

static const struct pkvm_iommu_ops *get_driver_ops(enum pkvm_iommu_driver_id id)
{
	switch (id) {
	default:
		return NULL;
	}
}

static inline bool driver_acquire_init(struct pkvm_iommu_driver *drv)
{
	return atomic_cmpxchg_acquire(&drv->state, IOMMU_DRIVER_NOT_READY,
				      IOMMU_DRIVER_INITIALIZING)
			== IOMMU_DRIVER_NOT_READY;
}

static inline void driver_release_init(struct pkvm_iommu_driver *drv,
				       bool success)
{
	atomic_set_release(&drv->state, success ? IOMMU_DRIVER_READY
						: IOMMU_DRIVER_NOT_READY);
}

/*
 * Initialize EL2 IOMMU driver.
 *
 * This is a common hypercall for driver initialization. Driver-specific
 * arguments are passed in a shared memory buffer. The driver is expected to
 * initialize it's page-table bookkeeping.
 */
int __pkvm_iommu_driver_init(enum pkvm_iommu_driver_id id, void *data, size_t size)
{
	struct pkvm_iommu_driver *drv;
	const struct pkvm_iommu_ops *ops;
	int ret = 0;

	data = kern_hyp_va(data);

	drv = get_driver(id);
	ops = get_driver_ops(id);
	if (!drv || !ops)
		return -EINVAL;

	if (!driver_acquire_init(drv))
		return -EBUSY;

	drv->ops = ops;

	/* This can change stage-2 mappings. */
	if (ops->init) {
		ret = hyp_pin_shared_mem(data, data + size);
		if (!ret) {
			ret = ops->init(data, size);
			hyp_unpin_shared_mem(data, data + size);
		}
		if (ret)
			goto out;
	}

out:
	driver_release_init(drv, /*success=*/!ret);
	return ret;
}

/*
 * Check host memory access against IOMMUs' MMIO regions.
 * Returns -EPERM if the address is within the bounds of a registered device.
 * Otherwise returns zero and adjusts boundaries of the new mapping to avoid
 * MMIO regions of registered IOMMUs.
 */
int pkvm_iommu_host_stage2_adjust_range(phys_addr_t addr, phys_addr_t *start,
					phys_addr_t *end)
{
	struct pkvm_iommu *dev;
	phys_addr_t new_start = *start;
	phys_addr_t new_end = *end;
	phys_addr_t dev_start, dev_end;

	assert_host_component_locked();

	list_for_each_entry(dev, &iommu_list, list) {
		dev_start = dev->pa;
		dev_end = dev_start + dev->size;

		if (addr < dev_start)
			new_end = min(new_end, dev_start);
		else if (addr >= dev_end)
			new_start = max(new_start, dev_end);
		else
			return -EPERM;
	}

	*start = new_start;
	*end = new_end;
	return 0;
}
