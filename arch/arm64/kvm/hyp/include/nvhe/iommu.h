/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_NVHE_IOMMU_H__
#define __ARM64_KVM_NVHE_IOMMU_H__

#include <linux/types.h>
#include <asm/kvm_host.h>

#include <nvhe/mem_protect.h>

struct pkvm_iommu;

struct pkvm_iommu_ops {
	/*
	 * Global driver initialization called before devices are registered.
	 * Driver-specific arguments are passed in a buffer shared by the host.
	 * The buffer memory has been pinned in EL2 but host retains R/W access.
	 * Extra care must be taken when reading from it to avoid TOCTOU bugs.
	 * If the driver maintains its own page tables, it is expected to
	 * initialize them to all memory owned by the host.
	 * Driver initialization lock held during callback.
	 */
	int (*init)(void *data, size_t size);

	/*
	 * Driver-specific validation of a device that is being registered.
	 * All fields of the device struct have been populated.
	 * Called with the host lock held.
	 */
	int (*validate)(struct pkvm_iommu *dev);

	/*
	 * Validation of a new child device that is being register by
	 * the parent device the child selected. Called with the host lock held.
	 */
	int (*validate_child)(struct pkvm_iommu *dev, struct pkvm_iommu *child);

	/*
	 * Callback to apply a host stage-2 mapping change at driver level.
	 * Called before 'host_stage2_idmap_apply' with host lock held.
	 */
	void (*host_stage2_idmap_prepare)(phys_addr_t start, phys_addr_t end,
					  enum kvm_pgtable_prot prot);

	/*
	 * Callback to apply a host stage-2 mapping change at device level.
	 * Called after 'host_stage2_idmap_prepare' with host lock held.
	 */
	void (*host_stage2_idmap_apply)(struct pkvm_iommu *dev,
					phys_addr_t start, phys_addr_t end);

	/*
	 * Callback to finish a host stage-2 mapping change at device level.
	 * Called after 'host_stage2_idmap_apply' with host lock held.
	 */
	void (*host_stage2_idmap_complete)(struct pkvm_iommu *dev);

	/* Power management callbacks. Called with host lock held. */
	int (*suspend)(struct pkvm_iommu *dev);
	int (*resume)(struct pkvm_iommu *dev);

	/*
	 * Host data abort handler callback. Called with host lock held.
	 * Returns true if the data abort has been handled.
	 */
	bool (*host_dabt_handler)(struct pkvm_iommu *dev,
				  struct kvm_cpu_context *host_ctxt,
				  u32 esr, size_t off);

	/* Amount of memory allocated per-device for use by the driver. */
	size_t data_size;
};

struct pkvm_iommu {
	struct pkvm_iommu *parent;
	struct list_head list;
	struct list_head siblings;
	struct list_head children;
	unsigned long id;
	const struct pkvm_iommu_ops *ops;
	phys_addr_t pa;
	void *va;
	size_t size;
	bool powered;
	char data[];
};

int __pkvm_iommu_driver_init(struct pkvm_iommu_driver *drv, void *data, size_t size);
int __pkvm_iommu_register(unsigned long dev_id, unsigned long drv_id,
			  phys_addr_t dev_pa, size_t dev_size,
			  unsigned long parent_id,
			  void *kern_mem_va, size_t mem_size);
int __pkvm_iommu_pm_notify(unsigned long dev_id,
			   enum pkvm_iommu_pm_event event);
int __pkvm_iommu_finalize(int err);
int pkvm_iommu_host_stage2_adjust_range(phys_addr_t addr, phys_addr_t *start,
					phys_addr_t *end);
bool pkvm_iommu_host_dabt_handler(struct kvm_cpu_context *host_ctxt, u32 esr,
				  phys_addr_t fault_pa);
void pkvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				  enum kvm_pgtable_prot prot);

#endif	/* __ARM64_KVM_NVHE_IOMMU_H__ */
