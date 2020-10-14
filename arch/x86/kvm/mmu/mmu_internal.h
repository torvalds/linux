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
#define rmap_printk(x...) do { if (dbg) printk(x); } while (0)
#define MMU_WARN_ON(x) WARN_ON(x)
#else
#define pgprintk(x...) do { } while (0)
#define rmap_printk(x...) do { } while (0)
#define MMU_WARN_ON(x) do { } while (0)
#endif

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;
	struct list_head lpage_disallowed_link;

	bool unsync;
	u8 mmu_valid_gen;
	bool mmio_cached;
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
	int root_count;          /* Currently serving as active root */
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

	bool tdp_mmu_page;
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

static inline bool kvm_vcpu_ad_need_write_protect(struct kvm_vcpu *vcpu)
{
	/*
	 * When using the EPT page-modification log, the GPAs in the log
	 * would come from L2 rather than L1.  Therefore, we need to rely
	 * on write protection to record dirty pages.  This also bypasses
	 * PML, since writes now result in a vmexit.
	 */
	return vcpu->arch.mmu == &vcpu->arch.guest_mmu;
}

bool is_nx_huge_page_enabled(void);
bool mmu_need_write_protect(struct kvm_vcpu *vcpu, gfn_t gfn,
			    bool can_unsync);

void kvm_mmu_gfn_disallow_lpage(struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_mmu_gfn_allow_lpage(struct kvm_memory_slot *slot, gfn_t gfn);
bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn);

static inline void kvm_mmu_get_root(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	BUG_ON(!sp->root_count);
	lockdep_assert_held(&kvm->mmu_lock);

	++sp->root_count;
}

static inline bool kvm_mmu_put_root(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	lockdep_assert_held(&kvm->mmu_lock);
	--sp->root_count;

	return !sp->root_count;
}

#endif /* __KVM_X86_MMU_INTERNAL_H */
