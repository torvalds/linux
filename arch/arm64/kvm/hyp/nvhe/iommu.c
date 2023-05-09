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
#include <nvhe/modules.h>
#include <nvhe/pkvm.h>

#define DRV_ID(drv_addr)			((unsigned long)drv_addr)

enum {
	IOMMU_DRIVER_NOT_READY = 0,
	IOMMU_DRIVER_INITIALIZING,
	IOMMU_DRIVER_READY,
};

/* List of registered IOMMU drivers, protected with iommu_drv_lock. */
static LIST_HEAD(iommu_drivers);
/* IOMMU device list. Must only be accessed with host_mmu.lock held. */
static LIST_HEAD(iommu_list);

static bool iommu_finalized;
static DEFINE_HYP_SPINLOCK(iommu_registration_lock);
static DEFINE_HYP_SPINLOCK(iommu_drv_lock);

static void *iommu_mem_pool;
static size_t iommu_mem_remaining;

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
static inline struct pkvm_iommu_driver *get_driver(unsigned long id)
{
	struct pkvm_iommu_driver *drv, *ret = NULL;

	hyp_spin_lock(&iommu_drv_lock);
	list_for_each_entry(drv, &iommu_drivers, list) {
		if (DRV_ID(drv) == id) {
			ret =  drv;
			break;
		}
	}
	hyp_spin_unlock(&iommu_drv_lock);
	return ret;
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

static size_t __iommu_alloc_size(struct pkvm_iommu_driver *drv)
{
	return ALIGN(sizeof(struct pkvm_iommu) + drv->ops->data_size,
		     sizeof(unsigned long));
}

static bool validate_driver_id_unique(struct pkvm_iommu_driver *drv)
{
	struct pkvm_iommu_driver *cur;

	hyp_assert_lock_held(&iommu_drv_lock);
	list_for_each_entry(cur, &iommu_drivers, list) {
		if (DRV_ID(drv) == DRV_ID(cur))
			return false;
	}
	return true;
}

static int __pkvm_register_iommu_driver(struct pkvm_iommu_driver *drv)
{
	int ret = 0;

	if (!drv)
		return -EINVAL;

	hyp_assert_lock_held(&iommu_registration_lock);
	hyp_spin_lock(&iommu_drv_lock);
	if (validate_driver_id_unique(drv))
		list_add_tail(&drv->list, &iommu_drivers);
	else
		ret = -EEXIST;
	hyp_spin_unlock(&iommu_drv_lock);
	return ret;
}

/* Global memory pool for allocating IOMMU list entry structs. */
static inline struct pkvm_iommu *alloc_iommu(struct pkvm_iommu_driver *drv,
					     void *mem, size_t mem_size)
{
	size_t size = __iommu_alloc_size(drv);
	void *ptr;

	assert_host_component_locked();

	/*
	 * If new memory is being provided, replace the existing pool with it.
	 * Any remaining memory in the pool is discarded.
	 */
	if (mem && mem_size) {
		iommu_mem_pool = mem;
		iommu_mem_remaining = mem_size;
	}

	if (size > iommu_mem_remaining)
		return NULL;

	ptr = iommu_mem_pool;
	iommu_mem_pool += size;
	iommu_mem_remaining -= size;
	return ptr;
}

static inline void free_iommu(struct pkvm_iommu_driver *drv, struct pkvm_iommu *ptr)
{
	size_t size = __iommu_alloc_size(drv);

	assert_host_component_locked();

	if (!ptr)
		return;

	/* Only allow freeing the last allocated buffer. */
	if ((void *)ptr + size != iommu_mem_pool)
		return;

	iommu_mem_pool -= size;
	iommu_mem_remaining += size;
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

static int __snapshot_host_stage2(u64 start, u64 pa_max, u32 level,
				  kvm_pte_t *ptep,
				  enum kvm_pgtable_walk_flags flags,
				  void * const arg)
{
	struct pkvm_iommu_driver * const drv = arg;
	u64 end = start + kvm_granule_size(level);
	kvm_pte_t pte = *ptep;

	/*
	 * Valid stage-2 entries are created lazily, invalid ones eagerly.
	 * Note: In the future we may need to check if [start,end) is MMIO.
	 * Note: Drivers initialize their PTs to all memory owned by the host,
	 * so we only call the driver on regions where that is not the case.
	 */
	if (pte && !kvm_pte_valid(pte))
		drv->ops->host_stage2_idmap_prepare(start, end, /*prot*/ 0);
	return 0;
}

static int snapshot_host_stage2(struct pkvm_iommu_driver * const drv)
{
	struct kvm_pgtable_walker walker = {
		.cb	= __snapshot_host_stage2,
		.arg	= drv,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};
	struct kvm_pgtable *pgt = &host_mmu.pgt;

	if (!drv->ops->host_stage2_idmap_prepare)
		return 0;

	return kvm_pgtable_walk(pgt, 0, BIT(pgt->ia_bits), &walker);
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
int __pkvm_iommu_driver_init(struct pkvm_iommu_driver *drv, void *data, size_t size)
{
	const struct pkvm_iommu_ops *ops;
	int ret = 0;

	/* New driver initialization not allowed after __pkvm_iommu_finalize(). */
	hyp_spin_lock(&iommu_registration_lock);
	if (iommu_finalized) {
		ret = -EPERM;
		goto out_unlock;
	}

	ret =  __pkvm_register_iommu_driver(drv);
	if (ret)
		return ret;

	if (!drv->ops) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!driver_acquire_init(drv)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ops = drv->ops;

	/* This can change stage-2 mappings. */
	if (ops->init) {
		ret = hyp_pin_shared_mem(data, data + size);
		if (!ret) {
			ret = ops->init(data, size);
			hyp_unpin_shared_mem(data, data + size);
		}
		if (ret)
			goto out_release;
	}

	/*
	 * Walk host stage-2 and pass current mappings to the driver. Start
	 * accepting host stage-2 updates as soon as the host lock is released.
	 */
	host_lock_component();
	ret = snapshot_host_stage2(drv);
	if (!ret)
		driver_release_init(drv, /*success=*/true);
	host_unlock_component();

out_release:
	if (ret)
		driver_release_init(drv, /*success=*/false);

out_unlock:
	hyp_spin_unlock(&iommu_registration_lock);
	return ret;
}

int __pkvm_iommu_register(unsigned long dev_id, unsigned long drv_id,
			  phys_addr_t dev_pa, size_t dev_size,
			  unsigned long parent_id,
			  void *kern_mem_va, size_t mem_size)
{
	struct pkvm_iommu *dev = NULL;
	struct pkvm_iommu_driver *drv;
	void *mem_va = NULL;
	int ret = 0;

	/* New device registration not allowed after __pkvm_iommu_finalize(). */
	hyp_spin_lock(&iommu_registration_lock);
	if (iommu_finalized) {
		ret = -EPERM;
		goto out_unlock;
	}

	drv = get_driver(drv_id);
	if (!drv || !is_driver_ready(drv)) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (!PAGE_ALIGNED(dev_pa) || !PAGE_ALIGNED(dev_size)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!is_mmio_range(dev_pa, dev_size)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Accept memory donation if the host is providing new memory.
	 * Note: We do not return the memory even if there is an error later.
	 */
	if (kern_mem_va && mem_size) {
		mem_va = kern_hyp_va(kern_mem_va);

		if (!PAGE_ALIGNED(mem_va) || !PAGE_ALIGNED(mem_size)) {
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = __pkvm_host_donate_hyp(hyp_virt_to_pfn(mem_va),
					     mem_size >> PAGE_SHIFT);
		if (ret)
			goto out_unlock;
	}

	host_lock_component();

	/* Allocate memory for the new device entry. */
	dev = alloc_iommu(drv, mem_va, mem_size);
	if (!dev) {
		ret = -ENOMEM;
		goto out_free;
	}

	/* Populate the new device entry. */
	*dev = (struct pkvm_iommu){
		.children = LIST_HEAD_INIT(dev->children),
		.id = dev_id,
		.ops = drv->ops,
		.pa = dev_pa,
		.size = dev_size,
	};

	if (!validate_against_existing_iommus(dev)) {
		ret = -EBUSY;
		goto out_free;
	}

	if (parent_id) {
		dev->parent = find_iommu_by_id(parent_id);
		if (!dev->parent) {
			ret = -EINVAL;
			goto out_free;
		}

		if (dev->parent->ops->validate_child) {
			ret = dev->parent->ops->validate_child(dev->parent, dev);
			if (ret)
				goto out_free;
		}
	}

	if (dev->ops->validate) {
		ret = dev->ops->validate(dev);
		if (ret)
			goto out_free;
	}

	/*
	 * Unmap the device's MMIO range from host stage-2. If registration
	 * is successful, future attempts to re-map will be blocked by
	 * pkvm_iommu_host_stage2_adjust_range.
	 */
	ret = host_stage2_unmap_reg_locked(dev_pa, dev_size);
	if (ret)
		goto out_free;

	/* Create EL2 mapping for the device. */
	ret = __pkvm_create_private_mapping(dev_pa, dev_size,
					    PAGE_HYP_DEVICE, (unsigned long *)(&dev->va));
	if (ret){
		goto out_free;
	}

	/* Register device and prevent host from mapping the MMIO range. */
	list_add_tail(&dev->list, &iommu_list);
	if (dev->parent)
		list_add_tail(&dev->siblings, &dev->parent->children);

out_free:
	if (ret)
		free_iommu(drv, dev);
	host_unlock_component();

out_unlock:
	hyp_spin_unlock(&iommu_registration_lock);
	return ret;
}

int __pkvm_iommu_finalize(int err)
{
	int ret = 0;

	hyp_spin_lock(&iommu_registration_lock);
	if (!iommu_finalized)
		iommu_finalized = true;
	else
		ret = -EPERM;
	hyp_spin_unlock(&iommu_registration_lock);

	/*
	 * If finalize failed in EL1 driver for any reason, this means we can't trust the DMA
	 * isolation. So we have to inform pKVM to properly protect itself.
	 */
	if (!ret && err)
		pkvm_handle_system_misconfiguration(NO_DMA_ISOLATION);

	__pkvm_close_late_module_registration();

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
			if (!ret)
				dev->powered = false;
		} else if (event == PKVM_IOMMU_PM_RESUME) {
			ret = dev->ops->resume ? dev->ops->resume(dev) : 0;
			if (!ret)
				dev->powered = true;
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

		/* No 'powered' check - the host assumes it is powered. */
		if (!dev->ops->host_dabt_handler ||
		    !dev->ops->host_dabt_handler(dev, host_ctxt, esr, pa - dev->pa))
			return false;

		kvm_skip_host_instr();
		return true;
	}
	return false;
}

void pkvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				  enum kvm_pgtable_prot prot)
{
	struct pkvm_iommu_driver *drv;
	struct pkvm_iommu *dev;

	assert_host_component_locked();
	hyp_spin_lock(&iommu_drv_lock);
	list_for_each_entry(drv, &iommu_drivers, list) {
		if (drv && is_driver_ready(drv) && drv->ops->host_stage2_idmap_prepare)
			drv->ops->host_stage2_idmap_prepare(start, end, prot);
	}
	hyp_spin_unlock(&iommu_drv_lock);

	list_for_each_entry(dev, &iommu_list, list) {
		if (dev->powered && dev->ops->host_stage2_idmap_apply)
			dev->ops->host_stage2_idmap_apply(dev, start, end);
	}

	list_for_each_entry(dev, &iommu_list, list) {
		if (dev->powered && dev->ops->host_stage2_idmap_complete)
			dev->ops->host_stage2_idmap_complete(dev);
	}
}
