/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#ifndef __KVM_NVHE_MEM_PROTECT__
#define __KVM_NVHE_MEM_PROTECT__
#include <linux/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/virt.h>
#include <nvhe/pkvm.h>
#include <nvhe/spinlock.h>

/*
 * SW bits 0-1 are reserved to track the memory ownership state of each page:
 *   00: The page is owned exclusively by the page-table owner.
 *   01: The page is owned by the page-table owner, but is shared
 *       with another entity.
 *   10: The page is shared with, but not owned by the page-table owner.
 *   11: Reserved for future use (lending).
 */
enum pkvm_page_state {
	PKVM_PAGE_OWNED			= 0ULL,
	PKVM_PAGE_SHARED_OWNED		= KVM_PGTABLE_PROT_SW0,
	PKVM_PAGE_SHARED_BORROWED	= KVM_PGTABLE_PROT_SW1,
	__PKVM_PAGE_RESERVED		= KVM_PGTABLE_PROT_SW0 |
					  KVM_PGTABLE_PROT_SW1,

	/* Meta-states which aren't encoded directly in the PTE's SW bits */
	PKVM_NOPAGE			= BIT(0),
	PKVM_PAGE_RESTRICTED_PROT	= BIT(1),
	PKVM_MODULE_DONT_TOUCH		= BIT(2),
};

#define PKVM_PAGE_STATE_PROT_MASK	(KVM_PGTABLE_PROT_SW0 | KVM_PGTABLE_PROT_SW1)
static inline enum kvm_pgtable_prot pkvm_mkstate(enum kvm_pgtable_prot prot,
						 enum pkvm_page_state state)
{
	return (prot & ~PKVM_PAGE_STATE_PROT_MASK) | state;
}

static inline enum pkvm_page_state pkvm_getstate(enum kvm_pgtable_prot prot)
{
	return prot & PKVM_PAGE_STATE_PROT_MASK;
}

struct host_mmu {
	struct kvm_arch arch;
	struct kvm_pgtable pgt;
	struct kvm_pgtable_mm_ops mm_ops;
	hyp_spinlock_t lock;
};
extern struct host_mmu host_mmu;

/* This corresponds to page-table locking order */
enum pkvm_component_id {
	PKVM_ID_HOST,
	PKVM_ID_HYP,
	PKVM_ID_GUEST,
	PKVM_ID_FFA,
	PKVM_ID_PROTECTED,
	PKVM_ID_MAX = PKVM_ID_PROTECTED,
};

extern unsigned long hyp_nr_cpus;

int __pkvm_prot_finalize(void);
int __pkvm_host_share_hyp(u64 pfn);
int __pkvm_host_unshare_hyp(u64 pfn);
int __pkvm_host_reclaim_page(struct pkvm_hyp_vm *vm, u64 pfn, u64 ipa);
int __pkvm_host_donate_hyp(u64 pfn, u64 nr_pages);
int __pkvm_hyp_donate_host(u64 pfn, u64 nr_pages);
int __pkvm_host_share_guest(u64 pfn, u64 gfn, struct pkvm_hyp_vcpu *vcpu);
int __pkvm_host_donate_guest(u64 pfn, u64 gfn, struct pkvm_hyp_vcpu *vcpu);
int __pkvm_guest_share_host(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa);
int __pkvm_guest_unshare_host(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa);
int __pkvm_guest_relinquish_to_host(struct pkvm_hyp_vcpu *vcpu,
				    u64 ipa, u64 *ppa);
int __pkvm_install_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa);
int __pkvm_remove_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa);
bool __pkvm_check_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu);
int __pkvm_host_share_ffa(u64 pfn, u64 nr_pages);
int __pkvm_host_unshare_ffa(u64 pfn, u64 nr_pages);

bool addr_is_memory(phys_addr_t phys);
int host_stage2_idmap_locked(phys_addr_t addr, u64 size, enum kvm_pgtable_prot prot,
			     bool update_iommu);
int host_stage2_set_owner_locked(phys_addr_t addr, u64 size, enum pkvm_component_id owner_id);
int host_stage2_protect_pages_locked(phys_addr_t addr, u64 size);
int host_stage2_unmap_reg_locked(phys_addr_t start, u64 size);
int kvm_host_prepare_stage2(void *pgt_pool_base);
int kvm_guest_prepare_stage2(struct pkvm_hyp_vm *vm, void *pgd);
void handle_host_mem_abort(struct kvm_cpu_context *host_ctxt);

int hyp_register_host_perm_fault_handler(int (*cb)(struct kvm_cpu_context *ctxt, u64 esr, u64 addr));
int hyp_pin_shared_mem(void *from, void *to);
void hyp_unpin_shared_mem(void *from, void *to);
int host_stage2_get_leaf(phys_addr_t phys, kvm_pte_t *ptep, u32 *level);
int refill_memcache(struct kvm_hyp_memcache *mc, unsigned long min_pages,
		    struct kvm_hyp_memcache *host_mc);

int module_change_host_page_prot(u64 pfn, enum kvm_pgtable_prot prot);

void destroy_hyp_vm_pgt(struct pkvm_hyp_vm *vm);
void drain_hyp_pool(struct pkvm_hyp_vm *vm, struct kvm_hyp_memcache *mc);

void psci_mem_protect_inc(u64 n);
void psci_mem_protect_dec(u64 n);

static __always_inline void __load_host_stage2(void)
{
	if (static_branch_likely(&kvm_protected_mode_initialized))
		__load_stage2(&host_mmu.arch.mmu, &host_mmu.arch);
	else
		write_sysreg(0, vttbr_el2);
}
#endif /* __KVM_NVHE_MEM_PROTECT__ */
