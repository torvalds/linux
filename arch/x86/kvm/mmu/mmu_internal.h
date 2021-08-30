/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_MMU_INTERNAL_H
#define __KVM_X86_MMU_INTERNAL_H

#include <linux/types.h>
#include <linux/kvm_host.h>
#include <asm/kvm_host.h>

#undef MMU_DEBUG

#ifdef MMU_DEBUG
extern bool dbg;

#define pgprintk(x...) do { if (dbg) printk(x); } while (0)
#define rmap_printk(fmt, args...) do { if (dbg) printk("%s: " fmt, __func__, ## args); } while (0)
#define MMU_WARN_ON(x) WARN_ON(x)
#else
#define pgprintk(x...) do { } while (0)
#define rmap_printk(x...) do { } while (0)
#define MMU_WARN_ON(x) do { } while (0)
#endif

/*
 * Unlike regular MMU roots, PAE "roots", a.k.a. PDPTEs/PDPTRs, have a PRESENT
 * bit, and thus are guaranteed to be non-zero when valid.  And, when a guest
 * PDPTR is !PRESENT, its corresponding PAE root cannot be set to INVALID_PAGE,
 * as the CPU would treat that as PRESENT PDPTR with reserved bits set.  Use
 * '0' instead of INVALID_PAGE to indicate an invalid PAE root.
 */
#define INVALID_PAE_ROOT	0
#define IS_VALID_PAE_ROOT(x)	(!!(x))

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;
	struct list_head lpage_disallowed_link;

	bool unsync;
	u8 mmu_valid_gen;
	bool lpage_disallowed; /* Can't be replaced by an equiv large page */

	/*
	 * The following two entries are used to key the shadow page in the
	 * hash table.
	 */
	union kvm_mmu_page_role role;
	gfn_t gfn;

	u64 *spt;
	/* hold the gfn of each spte inside spt */
	gfn_t *gfns;
	/* Currently serving as active root */
	union {
		int root_count;
		refcount_t tdp_mmu_root_count;
	};
	unsigned int unsync_children;
	struct kvm_rmap_head parent_ptes; /* rmap pointers to parent sptes */
	DECLARE_BITMAP(unsync_child_bitmap, 512);

#ifdef CONFIG_X86_32
	/*
	 * Used out of the mmu-lock to avoid reading spte values while an
	 * update is in progress; see the comments in __get_spte_lockless().
	 */
	int clear_spte_count;
#endif

	/* Number of writes since the last time traversal visited this page.  */
	atomic_t write_flooding_count;

#ifdef CONFIG_X86_64
	bool tdp_mmu_page;

	/* Used for freeing the page asynchronously if it is a TDP MMU page. */
	struct rcu_head rcu_head;
#endif
};

extern struct kmem_cache *mmu_page_header_cache;

static inline struct kvm_mmu_page *to_shadow_page(hpa_t shadow_page)
{
	struct page *page = pfn_to_page(shadow_page >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

static inline struct kvm_mmu_page *sptep_to_sp(u64 *sptep)
{
	return to_shadow_page(__pa(sptep));
}

static inline int kvm_mmu_role_as_id(union kvm_mmu_page_role role)
{
	return role.smm ? 1 : 0;
}

static inline int kvm_mmu_page_as_id(struct kvm_mmu_page *sp)
{
	return kvm_mmu_role_as_id(sp->role);
}

static inline bool kvm_vcpu_ad_need_write_protect(struct kvm_vcpu *vcpu)
{
	/*
	 * When using the EPT page-modification log, the GPAs in the CPU dirty
	 * log would come from L2 rather than L1.  Therefore, we need to rely
	 * on write protection to record dirty pages, which bypasses PML, since
	 * writes now result in a vmexit.  Note, the check on CPU dirty logging
	 * being enabled is mandatory as the bits used to denote WP-only SPTEs
	 * are reserved for NPT w/ PAE (32-bit KVM).
	 */
	return vcpu->arch.mmu == &vcpu->arch.guest_mmu &&
	       kvm_x86_ops.cpu_dirty_log_size;
}

extern int nx_huge_pages;
static inline bool is_nx_huge_page_enabled(void)
{
	return READ_ONCE(nx_huge_pages);
}

int mmu_try_to_unsync_pages(struct kvm_vcpu *vcpu, gfn_t gfn, bool can_unsync);

void kvm_mmu_gfn_disallow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_mmu_gfn_allow_lpage(const struct kvm_memory_slot *slot, gfn_t gfn);
bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn,
				    int min_level);
void kvm_flush_remote_tlbs_with_address(struct kvm *kvm,
					u64 start_gfn, u64 pages);
unsigned int pte_list_count(struct kvm_rmap_head *rmap_head);

/*
 * Return values of handle_mmio_page_fault, mmu.page_fault, and fast_page_fault().
 *
 * RET_PF_RETRY: let CPU fault again on the address.
 * RET_PF_EMULATE: mmio page fault, emulate the instruction directly.
 * RET_PF_INVALID: the spte is invalid, let the real page fault path update it.
 * RET_PF_FIXED: The faulting entry has been fixed.
 * RET_PF_SPURIOUS: The faulting entry was already fixed, e.g. by another vCPU.
 *
 * Any names added to this enum should be exported to userspace for use in
 * tracepoints via TRACE_DEFINE_ENUM() in mmutrace.h
 */
enum {
	RET_PF_RETRY = 0,
	RET_PF_EMULATE,
	RET_PF_INVALID,
	RET_PF_FIXED,
	RET_PF_SPURIOUS,
};

/* Bits which may be returned by set_spte() */
#define SET_SPTE_WRITE_PROTECTED_PT	BIT(0)
#define SET_SPTE_NEED_REMOTE_TLB_FLUSH	BIT(1)
#define SET_SPTE_SPURIOUS		BIT(2)

int kvm_mmu_max_mapping_level(struct kvm *kvm,
			      const struct kvm_memory_slot *slot, gfn_t gfn,
			      kvm_pfn_t pfn, int max_level);
int kvm_mmu_hugepage_adjust(struct kvm_vcpu *vcpu, gfn_t gfn,
			    int max_level, kvm_pfn_t *pfnp,
			    bool huge_page_disallowed, int *req_level);
void disallowed_hugepage_adjust(u64 spte, gfn_t gfn, int cur_level,
				kvm_pfn_t *pfnp, int *goal_levelp);

void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc);

void account_huge_nx_page(struct kvm *kvm, struct kvm_mmu_page *sp);
void unaccount_huge_nx_page(struct kvm *kvm, struct kvm_mmu_page *sp);

#endif /* __KVM_X86_MMU_INTERNAL_H */
