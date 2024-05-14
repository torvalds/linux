// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt)	"ioremap: " fmt

#include <linux/mm.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>
#include <linux/slab.h>

#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/hypervisor.h>

#ifndef ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO
#define ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO	5

#define ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_INFO_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO)
#endif	/* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO */

#ifndef ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL
#define ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL	6

#define ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_ENROLL_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL)
#endif	/* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL */

#ifndef ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP
#define ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP	7

#define ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_MAP_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP)
#endif	/* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP */

#ifndef ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP
#define ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP	8

#define ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_UNMAP_FUNC_ID			\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,				\
			   ARM_SMCCC_SMC_64,				\
			   ARM_SMCCC_OWNER_VENDOR_HYP,			\
			   ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP)
#endif	/* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP */

struct ioremap_guard_ref {
	refcount_t	count;
};

static DEFINE_STATIC_KEY_FALSE(ioremap_guard_key);
static DEFINE_XARRAY(ioremap_guard_array);
static DEFINE_MUTEX(ioremap_guard_lock);

static size_t guard_granule;

static bool ioremap_guard;
static int __init ioremap_guard_setup(char *str)
{
	ioremap_guard = true;

	return 0;
}
early_param("ioremap_guard", ioremap_guard_setup);

void kvm_init_ioremap_services(void)
{
	struct arm_smccc_res res;
	size_t granule;

	if (!ioremap_guard)
		return;

	/* We need all the functions to be implemented */
	if (!kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO) ||
	    !kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL) ||
	    !kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP) ||
	    !kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP))
		return;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_INFO_FUNC_ID,
			     0, 0, 0, &res);
	granule = res.a0;
	if (granule > PAGE_SIZE || !granule || (granule & (granule - 1))) {
		pr_warn("KVM MMIO guard initialization failed: "
			"guard granule (%lu), page size (%lu)\n",
			granule, PAGE_SIZE);
		return;
	}

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_ENROLL_FUNC_ID,
			     &res);
	if (res.a0 == SMCCC_RET_SUCCESS) {
		guard_granule = granule;
		static_branch_enable(&ioremap_guard_key);
		pr_info("Using KVM MMIO guard for ioremap\n");
	} else {
		pr_warn("KVM MMIO guard registration failed (%ld)\n", res.a0);
	}
}

void ioremap_phys_range_hook(phys_addr_t phys_addr, size_t size, pgprot_t prot)
{
	int guard_shift;

	if (!static_branch_unlikely(&ioremap_guard_key))
		return;

	guard_shift = __builtin_ctzl(guard_granule);

	mutex_lock(&ioremap_guard_lock);

	while (size) {
		u64 guard_fn = phys_addr >> guard_shift;
		struct ioremap_guard_ref *ref;
		struct arm_smccc_res res;

		if (pfn_valid(__phys_to_pfn(phys_addr)))
			goto next;

		ref = xa_load(&ioremap_guard_array, guard_fn);
		if (ref) {
			refcount_inc(&ref->count);
			goto next;
		}

		/*
		 * It is acceptable for the allocation to fail, specially
		 * if trying to ioremap something very early on, like with
		 * earlycon, which happens long before kmem_cache_init.
		 * This page will be permanently accessible, similar to a
		 * saturated refcount.
		 */
		if (slab_is_available())
			ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (ref) {
			refcount_set(&ref->count, 1);
			if (xa_err(xa_store(&ioremap_guard_array, guard_fn, ref,
					    GFP_KERNEL))) {
				kfree(ref);
				ref = NULL;
			}
		}

		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_MAP_FUNC_ID,
				  phys_addr, prot, &res);
		if (res.a0 != SMCCC_RET_SUCCESS) {
			pr_warn_ratelimited("Failed to register %llx\n",
					    phys_addr);
			xa_erase(&ioremap_guard_array, guard_fn);
			kfree(ref);
			goto out;
		}

	next:
		size -= guard_granule;
		phys_addr += guard_granule;
	}
out:
	mutex_unlock(&ioremap_guard_lock);
}

void iounmap_phys_range_hook(phys_addr_t phys_addr, size_t size)
{
	int guard_shift;

	if (!static_branch_unlikely(&ioremap_guard_key))
		return;

	VM_BUG_ON(phys_addr & ~PAGE_MASK || size & ~PAGE_MASK);
	guard_shift = __builtin_ctzl(guard_granule);

	mutex_lock(&ioremap_guard_lock);

	while (size) {
		u64 guard_fn = phys_addr >> guard_shift;
		struct ioremap_guard_ref *ref;
		struct arm_smccc_res res;

		ref = xa_load(&ioremap_guard_array, guard_fn);
		if (!ref) {
			pr_warn_ratelimited("%llx not tracked, left mapped\n",
					    phys_addr);
			goto next;
		}

		if (!refcount_dec_and_test(&ref->count))
			goto next;

		xa_erase(&ioremap_guard_array, guard_fn);
		kfree(ref);

		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_MMIO_GUARD_UNMAP_FUNC_ID,
				  phys_addr, &res);
		if (res.a0 != SMCCC_RET_SUCCESS) {
			pr_warn_ratelimited("Failed to unregister %llx\n",
					    phys_addr);
			goto out;
		}

	next:
		size -= guard_granule;
		phys_addr += guard_granule;
	}
out:
	mutex_unlock(&ioremap_guard_lock);
}

bool ioremap_allowed(phys_addr_t phys_addr, size_t size, unsigned long prot)
{
	unsigned long last_addr = phys_addr + size - 1;

	/* Don't allow outside PHYS_MASK */
	if (last_addr & ~PHYS_MASK)
		return false;

	/* Don't allow RAM to be mapped. */
	if (WARN_ON(pfn_is_map_memory(__phys_to_pfn(phys_addr))))
		return false;

	return true;
}

/*
 * Must be called after early_fixmap_init
 */
void __init early_ioremap_init(void)
{
	early_ioremap_setup();
}

bool arch_memremap_can_ram_remap(resource_size_t offset, size_t size,
				 unsigned long flags)
{
	unsigned long pfn = PHYS_PFN(offset);

	return pfn_is_map_memory(pfn);
}
