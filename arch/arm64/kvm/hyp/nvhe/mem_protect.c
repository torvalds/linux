// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/stage2_pgtable.h>

#include <hyp/fault.h>

#include <nvhe/gfp.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>

#define KVM_HOST_S2_FLAGS (KVM_PGTABLE_S2_NOFWB | KVM_PGTABLE_S2_IDMAP)

extern unsigned long hyp_nr_cpus;
struct host_kvm host_kvm;

static struct hyp_pool host_s2_pool;

const u8 pkvm_hyp_id = 1;

static void *host_s2_zalloc_pages_exact(size_t size)
{
	void *addr = hyp_alloc_pages(&host_s2_pool, get_order(size));

	hyp_split_page(hyp_virt_to_page(addr));

	/*
	 * The size of concatenated PGDs is always a power of two of PAGE_SIZE,
	 * so there should be no need to free any of the tail pages to make the
	 * allocation exact.
	 */
	WARN_ON(size != (PAGE_SIZE << get_order(size)));

	return addr;
}

static void *host_s2_zalloc_page(void *pool)
{
	return hyp_alloc_pages(pool, 0);
}

static void host_s2_get_page(void *addr)
{
	hyp_get_page(&host_s2_pool, addr);
}

static void host_s2_put_page(void *addr)
{
	hyp_put_page(&host_s2_pool, addr);
}

static int prepare_s2_pool(void *pgt_pool_base)
{
	unsigned long nr_pages, pfn;
	int ret;

	pfn = hyp_virt_to_pfn(pgt_pool_base);
	nr_pages = host_s2_pgtable_pages();
	ret = hyp_pool_init(&host_s2_pool, pfn, nr_pages, 0);
	if (ret)
		return ret;

	host_kvm.mm_ops = (struct kvm_pgtable_mm_ops) {
		.zalloc_pages_exact = host_s2_zalloc_pages_exact,
		.zalloc_page = host_s2_zalloc_page,
		.phys_to_virt = hyp_phys_to_virt,
		.virt_to_phys = hyp_virt_to_phys,
		.page_count = hyp_page_count,
		.get_page = host_s2_get_page,
		.put_page = host_s2_put_page,
	};

	return 0;
}

static void prepare_host_vtcr(void)
{
	u32 parange, phys_shift;

	/* The host stage 2 is id-mapped, so use parange for T0SZ */
	parange = kvm_get_parange(id_aa64mmfr0_el1_sys_val);
	phys_shift = id_aa64mmfr0_parange_to_phys_shift(parange);

	host_kvm.arch.vtcr = kvm_get_vtcr(id_aa64mmfr0_el1_sys_val,
					  id_aa64mmfr1_el1_sys_val, phys_shift);
}

static bool host_stage2_force_pte_cb(u64 addr, u64 end, enum kvm_pgtable_prot prot);

int kvm_host_prepare_stage2(void *pgt_pool_base)
{
	struct kvm_s2_mmu *mmu = &host_kvm.arch.mmu;
	int ret;

	prepare_host_vtcr();
	hyp_spin_lock_init(&host_kvm.lock);

	ret = prepare_s2_pool(pgt_pool_base);
	if (ret)
		return ret;

	ret = __kvm_pgtable_stage2_init(&host_kvm.pgt, &host_kvm.arch,
					&host_kvm.mm_ops, KVM_HOST_S2_FLAGS,
					host_stage2_force_pte_cb);
	if (ret)
		return ret;

	mmu->pgd_phys = __hyp_pa(host_kvm.pgt.pgd);
	mmu->arch = &host_kvm.arch;
	mmu->pgt = &host_kvm.pgt;
	WRITE_ONCE(mmu->vmid.vmid_gen, 0);
	WRITE_ONCE(mmu->vmid.vmid, 0);

	return 0;
}

int __pkvm_prot_finalize(void)
{
	struct kvm_s2_mmu *mmu = &host_kvm.arch.mmu;
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);

	if (params->hcr_el2 & HCR_VM)
		return -EPERM;

	params->vttbr = kvm_get_vttbr(mmu);
	params->vtcr = host_kvm.arch.vtcr;
	params->hcr_el2 |= HCR_VM;
	kvm_flush_dcache_to_poc(params, sizeof(*params));

	write_sysreg(params->hcr_el2, hcr_el2);
	__load_stage2(&host_kvm.arch.mmu, &host_kvm.arch);

	/*
	 * Make sure to have an ISB before the TLB maintenance below but only
	 * when __load_stage2() doesn't include one already.
	 */
	asm(ALTERNATIVE("isb", "nop", ARM64_WORKAROUND_SPECULATIVE_AT));

	/* Invalidate stale HCR bits that may be cached in TLBs */
	__tlbi(vmalls12e1);
	dsb(nsh);
	isb();

	return 0;
}

static int host_stage2_unmap_dev_all(void)
{
	struct kvm_pgtable *pgt = &host_kvm.pgt;
	struct memblock_region *reg;
	u64 addr = 0;
	int i, ret;

	/* Unmap all non-memory regions to recycle the pages */
	for (i = 0; i < hyp_memblock_nr; i++, addr = reg->base + reg->size) {
		reg = &hyp_memory[i];
		ret = kvm_pgtable_stage2_unmap(pgt, addr, reg->base - addr);
		if (ret)
			return ret;
	}
	return kvm_pgtable_stage2_unmap(pgt, addr, BIT(pgt->ia_bits) - addr);
}

struct kvm_mem_range {
	u64 start;
	u64 end;
};

static bool find_mem_range(phys_addr_t addr, struct kvm_mem_range *range)
{
	int cur, left = 0, right = hyp_memblock_nr;
	struct memblock_region *reg;
	phys_addr_t end;

	range->start = 0;
	range->end = ULONG_MAX;

	/* The list of memblock regions is sorted, binary search it */
	while (left < right) {
		cur = (left + right) >> 1;
		reg = &hyp_memory[cur];
		end = reg->base + reg->size;
		if (addr < reg->base) {
			right = cur;
			range->end = reg->base;
		} else if (addr >= end) {
			left = cur + 1;
			range->start = end;
		} else {
			range->start = reg->base;
			range->end = end;
			return true;
		}
	}

	return false;
}

bool addr_is_memory(phys_addr_t phys)
{
	struct kvm_mem_range range;

	return find_mem_range(phys, &range);
}

static bool is_in_mem_range(u64 addr, struct kvm_mem_range *range)
{
	return range->start <= addr && addr < range->end;
}

static bool range_is_memory(u64 start, u64 end)
{
	struct kvm_mem_range r;

	if (!find_mem_range(start, &r))
		return false;

	return is_in_mem_range(end - 1, &r);
}

static inline int __host_stage2_idmap(u64 start, u64 end,
				      enum kvm_pgtable_prot prot)
{
	return kvm_pgtable_stage2_map(&host_kvm.pgt, start, end - start, start,
				      prot, &host_s2_pool);
}

/*
 * The pool has been provided with enough pages to cover all of memory with
 * page granularity, but it is difficult to know how much of the MMIO range
 * we will need to cover upfront, so we may need to 'recycle' the pages if we
 * run out.
 */
#define host_stage2_try(fn, ...)					\
	({								\
		int __ret;						\
		hyp_assert_lock_held(&host_kvm.lock);			\
		__ret = fn(__VA_ARGS__);				\
		if (__ret == -ENOMEM) {					\
			__ret = host_stage2_unmap_dev_all();		\
			if (!__ret)					\
				__ret = fn(__VA_ARGS__);		\
		}							\
		__ret;							\
	 })

static inline bool range_included(struct kvm_mem_range *child,
				  struct kvm_mem_range *parent)
{
	return parent->start <= child->start && child->end <= parent->end;
}

static int host_stage2_adjust_range(u64 addr, struct kvm_mem_range *range)
{
	struct kvm_mem_range cur;
	kvm_pte_t pte;
	u32 level;
	int ret;

	hyp_assert_lock_held(&host_kvm.lock);
	ret = kvm_pgtable_get_leaf(&host_kvm.pgt, addr, &pte, &level);
	if (ret)
		return ret;

	if (kvm_pte_valid(pte))
		return -EAGAIN;

	if (pte)
		return -EPERM;

	do {
		u64 granule = kvm_granule_size(level);
		cur.start = ALIGN_DOWN(addr, granule);
		cur.end = cur.start + granule;
		level++;
	} while ((level < KVM_PGTABLE_MAX_LEVELS) &&
			!(kvm_level_supports_block_mapping(level) &&
			  range_included(&cur, range)));

	*range = cur;

	return 0;
}

int host_stage2_idmap_locked(phys_addr_t addr, u64 size,
			     enum kvm_pgtable_prot prot)
{
	hyp_assert_lock_held(&host_kvm.lock);

	return host_stage2_try(__host_stage2_idmap, addr, addr + size, prot);
}

int host_stage2_set_owner_locked(phys_addr_t addr, u64 size, u8 owner_id)
{
	hyp_assert_lock_held(&host_kvm.lock);

	return host_stage2_try(kvm_pgtable_stage2_set_owner, &host_kvm.pgt,
			       addr, size, &host_s2_pool, owner_id);
}

static bool host_stage2_force_pte_cb(u64 addr, u64 end, enum kvm_pgtable_prot prot)
{
	/*
	 * Block mappings must be used with care in the host stage-2 as a
	 * kvm_pgtable_stage2_map() operation targeting a page in the range of
	 * an existing block will delete the block under the assumption that
	 * mappings in the rest of the block range can always be rebuilt lazily.
	 * That assumption is correct for the host stage-2 with RWX mappings
	 * targeting memory or RW mappings targeting MMIO ranges (see
	 * host_stage2_idmap() below which implements some of the host memory
	 * abort logic). However, this is not safe for any other mappings where
	 * the host stage-2 page-table is in fact the only place where this
	 * state is stored. In all those cases, it is safer to use page-level
	 * mappings, hence avoiding to lose the state because of side-effects in
	 * kvm_pgtable_stage2_map().
	 */
	if (range_is_memory(addr, end))
		return prot != PKVM_HOST_MEM_PROT;
	else
		return prot != PKVM_HOST_MMIO_PROT;
}

static int host_stage2_idmap(u64 addr)
{
	struct kvm_mem_range range;
	bool is_memory = find_mem_range(addr, &range);
	enum kvm_pgtable_prot prot;
	int ret;

	prot = is_memory ? PKVM_HOST_MEM_PROT : PKVM_HOST_MMIO_PROT;

	hyp_spin_lock(&host_kvm.lock);
	ret = host_stage2_adjust_range(addr, &range);
	if (ret)
		goto unlock;

	ret = host_stage2_idmap_locked(range.start, range.end - range.start, prot);
unlock:
	hyp_spin_unlock(&host_kvm.lock);

	return ret;
}

static inline bool check_prot(enum kvm_pgtable_prot prot,
			      enum kvm_pgtable_prot required,
			      enum kvm_pgtable_prot denied)
{
	return (prot & (required | denied)) == required;
}

int __pkvm_host_share_hyp(u64 pfn)
{
	phys_addr_t addr = hyp_pfn_to_phys(pfn);
	enum kvm_pgtable_prot prot, cur;
	void *virt = __hyp_va(addr);
	enum pkvm_page_state state;
	kvm_pte_t pte;
	int ret;

	if (!addr_is_memory(addr))
		return -EINVAL;

	hyp_spin_lock(&host_kvm.lock);
	hyp_spin_lock(&pkvm_pgd_lock);

	ret = kvm_pgtable_get_leaf(&host_kvm.pgt, addr, &pte, NULL);
	if (ret)
		goto unlock;
	if (!pte)
		goto map_shared;

	/*
	 * Check attributes in the host stage-2 PTE. We need the page to be:
	 *  - mapped RWX as we're sharing memory;
	 *  - not borrowed, as that implies absence of ownership.
	 * Otherwise, we can't let it got through
	 */
	cur = kvm_pgtable_stage2_pte_prot(pte);
	prot = pkvm_mkstate(0, PKVM_PAGE_SHARED_BORROWED);
	if (!check_prot(cur, PKVM_HOST_MEM_PROT, prot)) {
		ret = -EPERM;
		goto unlock;
	}

	state = pkvm_getstate(cur);
	if (state == PKVM_PAGE_OWNED)
		goto map_shared;

	/*
	 * Tolerate double-sharing the same page, but this requires
	 * cross-checking the hypervisor stage-1.
	 */
	if (state != PKVM_PAGE_SHARED_OWNED) {
		ret = -EPERM;
		goto unlock;
	}

	ret = kvm_pgtable_get_leaf(&pkvm_pgtable, (u64)virt, &pte, NULL);
	if (ret)
		goto unlock;

	/*
	 * If the page has been shared with the hypervisor, it must be
	 * already mapped as SHARED_BORROWED in its stage-1.
	 */
	cur = kvm_pgtable_hyp_pte_prot(pte);
	prot = pkvm_mkstate(PAGE_HYP, PKVM_PAGE_SHARED_BORROWED);
	if (!check_prot(cur, prot, ~prot))
		ret = -EPERM;
	goto unlock;

map_shared:
	/*
	 * If the page is not yet shared, adjust mappings in both page-tables
	 * while both locks are held.
	 */
	prot = pkvm_mkstate(PAGE_HYP, PKVM_PAGE_SHARED_BORROWED);
	ret = pkvm_create_mappings_locked(virt, virt + PAGE_SIZE, prot);
	BUG_ON(ret);

	prot = pkvm_mkstate(PKVM_HOST_MEM_PROT, PKVM_PAGE_SHARED_OWNED);
	ret = host_stage2_idmap_locked(addr, PAGE_SIZE, prot);
	BUG_ON(ret);

unlock:
	hyp_spin_unlock(&pkvm_pgd_lock);
	hyp_spin_unlock(&host_kvm.lock);

	return ret;
}

void handle_host_mem_abort(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_vcpu_fault_info fault;
	u64 esr, addr;
	int ret = 0;

	esr = read_sysreg_el2(SYS_ESR);
	BUG_ON(!__get_fault_info(esr, &fault));

	addr = (fault.hpfar_el2 & HPFAR_MASK) << 8;
	ret = host_stage2_idmap(addr);
	BUG_ON(ret && ret != -EAGAIN);
}
