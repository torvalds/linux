// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include <hyp/adjust_pc.h>
#include <nvhe/iommu.h>
#include <nvhe/mm.h>

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

static void host_lock_component(void)
{
	hyp_spin_lock(&host_mmu.lock);
}

static void host_unlock_component(void)
{
	hyp_spin_unlock(&host_mmu.lock);
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

static inline bool is_driver_ready(struct pkvm_iommu_driver *drv)
{
	return atomic_read(&drv->state) == IOMMU_DRIVER_READY;
}

/* Global memory pool for allocating IOMMU list entry structs. */
static inline struct pkvm_iommu *
alloc_iommu_list_entry(struct pkvm_iommu_driver *drv, void *mem, size_t mem_size)
{
	static void *pool;
	static size_t remaining;
	static DEFINE_HYP_SPINLOCK(lock);
	size_t size = sizeof(struct pkvm_iommu) + drv->ops->data_size;
	void *ptr;

	size = ALIGN(size, sizeof(unsigned long));

	hyp_spin_lock(&lock);

	/*
	 * If new memory is being provided, replace the existing pool with it.
	 * Any remaining memory in the pool is discarded.
	 */
	if (mem && mem_size) {
		pool = mem;
		remaining = mem_size;
	}

	if (size <= remaining) {
		ptr = pool;
		pool += size;
		remaining -= size;
	} else {
		ptr = NULL;
	}

	hyp_spin_unlock(&lock);
	return ptr;
}

static bool is_overlap(phys_addr_t r1_start, size_t r1_size,
		       phys_addr_t r2_start, size_t r2_size)
{
	phys_addr_t r1_end = r1_start + r1_size;
	phys_addr_t r2_end = r2_start + r2_size;

	return (r1_start < r2_end) && (r2_start < r1_end);
}

static bool is_mmio_range(phys_addr_t base, size_t size)
{
	struct memblock_region *reg;
	phys_addr_t limit = BIT(host_mmu.pgt.ia_bits);
	size_t i;

	/* Check against limits of host IPA space. */
	if ((base >= limit) || !size || (size > limit - base))
		return false;

	for (i = 0; i < hyp_memblock_nr; i++) {
		reg = &hyp_memory[i];
		if (is_overlap(base, size, reg->base, reg->size))
			return false;
	}
	return true;
}

static bool validate_against_existing_iommus(struct pkvm_iommu *dev)
{
	struct pkvm_iommu *other;

	assert_host_component_locked();

	list_for_each_entry(other, &iommu_list, list) {
		/* Device ID must be unique. */
		if (dev->id == other->id)
			return false;

		/* MMIO regions must not overlap. */
		if (is_overlap(dev->pa, dev->size, other->pa, other->size))
			return false;
	}
	return true;
}

static struct pkvm_iommu *find_iommu_by_id(unsigned long id)
{
	struct pkvm_iommu *dev;

	assert_host_component_locked();

	list_for_each_entry(dev, &iommu_list, list) {
		if (dev->id == id)
			return dev;
	}
	return NULL;
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

int __pkvm_iommu_register(unsigned long dev_id,
			  enum pkvm_iommu_driver_id drv_id,
			  phys_addr_t dev_pa, size_t dev_size,
			  void *kern_mem_va, size_t mem_size)
{
	struct pkvm_iommu *dev = NULL;
	struct pkvm_iommu_driver *drv;
	void *dev_va, *mem_va = NULL;
	int ret = 0;

	drv = get_driver(drv_id);
	if (!drv || !is_driver_ready(drv))
		return -ENOENT;

	if (!PAGE_ALIGNED(dev_pa) || !PAGE_ALIGNED(dev_size))
		return -EINVAL;

	if (!is_mmio_range(dev_pa, dev_size))
		return -EINVAL;

	if (drv->ops->validate) {
		ret = drv->ops->validate(dev_pa, dev_size);
		if (ret)
			return ret;
	}

	/*
	 * Accept memory donation if the host is providing new memory.
	 * Note: We do not return the memory even if there is an error later.
	 */
	if (kern_mem_va && mem_size) {
		mem_va = kern_hyp_va(kern_mem_va);

		if (!PAGE_ALIGNED(mem_va) || !PAGE_ALIGNED(mem_size))
			return -EINVAL;

		ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(mem_va),
					     mem_size >> PAGE_SHIFT);
		if (ret)
			return ret;
	}

	/* Allocate memory for the new device entry. */
	dev = alloc_iommu_list_entry(drv, mem_va, mem_size);
	if (!dev)
		return -ENOMEM;

	/* Create EL2 mapping for the device. */
	ret = __pkvm_create_private_mapping(dev_pa, dev_size,
				 PAGE_HYP_DEVICE,(unsigned long *)&dev_va);
	if (ret)
		return ret;

	/* Populate the new device entry. */
	*dev = (struct pkvm_iommu){
		.id = dev_id,
		.ops = drv->ops,
		.pa = dev_pa,
		.va = dev_va,
		.size = dev_size,
	};

	/* Take the host_mmu lock to block host stage-2 changes. */
	host_lock_component();
	if (!validate_against_existing_iommus(dev)) {
		ret = -EBUSY;
		goto out;
	}

	/* Unmap the device's MMIO range from host stage-2. */
	ret = host_stage2_unmap_dev_locked(dev_pa, dev_size);
	if (ret)
		goto out;

	/* Register device and prevent host from mapping the MMIO range. */
	list_add_tail(&dev->list, &iommu_list);

out:
	host_unlock_component();
	return ret;
}

int __pkvm_iommu_pm_notify(unsigned long dev_id, enum pkvm_iommu_pm_event event)
{
	struct pkvm_iommu *dev;
	int ret;

	host_lock_component();
	dev = find_iommu_by_id(dev_id);
	if (dev) {
		if (event == PKVM_IOMMU_PM_SUSPEND) {
			ret = dev->ops->suspend ? dev->ops->suspend(dev) : 0;
			dev->powered = !!ret;
		} else if (event == PKVM_IOMMU_PM_RESUME) {
			ret = dev->ops->resume ? dev->ops->resume(dev) : 0;
			dev->powered = !ret;
		} else {
			ret = -EINVAL;
		}
	} else {
		ret = -ENODEV;
	}
	host_unlock_component();
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

bool pkvm_iommu_host_dabt_handler(struct kvm_cpu_context *host_ctxt, u32 esr,
				  phys_addr_t pa)
{
	struct pkvm_iommu *dev;

	assert_host_component_locked();

	list_for_each_entry(dev, &iommu_list, list) {
		if (pa < dev->pa || pa >= dev->pa + dev->size)
			continue;

		if (!dev->powered || !dev->ops->host_dabt_handler ||
		    !dev->ops->host_dabt_handler(dev, host_ctxt, esr, pa - dev->pa))
			return false;

		kvm_skip_host_instr();
		return true;
	}
	return false;
}
