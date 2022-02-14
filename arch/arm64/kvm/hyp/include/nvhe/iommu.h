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
	 * Driver initialization lock held during callback.
	 */
	int (*init)(void *data, size_t size);

	/*
	 * Driver-specific validation of device registration inputs.
	 * This should be stateless. No locks are held at entry.
	 */
	int (*validate)(phys_addr_t base, size_t size);

	/* Power management callbacks. Called with host lock held. */
	int (*suspend)(struct pkvm_iommu *dev);
	int (*resume)(struct pkvm_iommu *dev);

	/* Amount of memory allocated per-device for use by the driver. */
	size_t data_size;
};

struct pkvm_iommu {
	struct list_head list;
	unsigned long id;
	const struct pkvm_iommu_ops *ops;
	phys_addr_t pa;
	void *va;
	size_t size;
	bool powered;
	char data[];
};

int __pkvm_iommu_driver_init(enum pkvm_iommu_driver_id id, void *data, size_t size);
int __pkvm_iommu_register(unsigned long dev_id,
			  enum pkvm_iommu_driver_id drv_id,
			  phys_addr_t dev_pa, size_t dev_size,
			  void *kern_mem_va, size_t mem_size);
int __pkvm_iommu_pm_notify(unsigned long dev_id,
			   enum pkvm_iommu_pm_event event);
int pkvm_iommu_host_stage2_adjust_range(phys_addr_t addr, phys_addr_t *start,
					phys_addr_t *end);

struct kvm_iommu_ops {
	int (*init)(void);
	bool (*host_smc_handler)(struct kvm_cpu_context *host_ctxt);
	bool (*host_mmio_dabt_handler)(struct kvm_cpu_context *host_ctxt,
				       phys_addr_t fault_pa, unsigned int len,
				       bool is_write, int rd);
	void (*host_stage2_set_owner)(phys_addr_t addr, size_t size,
				      enum pkvm_component_id owner_id);
	int (*host_stage2_adjust_mmio_range)(phys_addr_t addr, phys_addr_t *start,
					     phys_addr_t *end);
};

extern struct kvm_iommu_ops kvm_iommu_ops;
extern const struct kvm_iommu_ops kvm_s2mpu_ops;

#endif	/* __ARM64_KVM_NVHE_IOMMU_H__ */
