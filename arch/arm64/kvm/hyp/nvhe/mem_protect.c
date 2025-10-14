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
#include <asm/kvm_pkvm.h>
#include <asm/stage2_pgtable.h>

#include <hyp/fault.h>

#include <nvhe/gfp.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>

#define KVM_HOST_S2_FLAGS (KVM_PGTABLE_S2_NOFWB | KVM_PGTABLE_S2_IDMAP)

struct host_mmu host_mmu;

static struct hyp_pool host_s2_pool;

static DEFINE_PER_CPU(struct pkvm_hyp_vm *, __current_vm);
#define current_vm (*this_cpu_ptr(&__current_vm))

static void guest_lock_component(struct pkvm_hyp_vm *vm)
{
	hyp_spin_lock(&vm->lock);
	current_vm = vm;
}

static void guest_unlock_component(struct pkvm_hyp_vm *vm)
{
	current_vm = NULL;
	hyp_spin_unlock(&vm->lock);
}

static void host_lock_component(void)
{
	hyp_spin_lock(&host_mmu.lock);
}

static void host_unlock_component(void)
{
	hyp_spin_unlock(&host_mmu.lock);
}

static void hyp_lock_component(void)
{
	hyp_spin_lock(&pkvm_pgd_lock);
}

static void hyp_unlock_component(void)
{
	hyp_spin_unlock(&pkvm_pgd_lock);
}

#define for_each_hyp_page(__p, __st, __sz)				\
	for (struct hyp_page *__p = hyp_phys_to_page(__st),		\
			     *__e = __p + ((__sz) >> PAGE_SHIFT);	\
	     __p < __e; __p++)

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

static void host_s2_free_unlinked_table(void *addr, s8 level)
{
	kvm_pgtable_stage2_free_unlinked(&host_mmu.mm_ops, addr, level);
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

	host_mmu.mm_ops = (struct kvm_pgtable_mm_ops) {
		.zalloc_pages_exact = host_s2_zalloc_pages_exact,
		.zalloc_page = host_s2_zalloc_page,
		.free_unlinked_table = host_s2_free_unlinked_table,
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

	host_mmu.arch.mmu.vtcr = kvm_get_vtcr(id_aa64mmfr0_el1_sys_val,
					      id_aa64mmfr1_el1_sys_val, phys_shift);
}

static bool host_stage2_force_pte_cb(u64 addr, u64 end, enum kvm_pgtable_prot prot);

int kvm_host_prepare_stage2(void *pgt_pool_base)
{
	struct kvm_s2_mmu *mmu = &host_mmu.arch.mmu;
	int ret;

	prepare_host_vtcr();
	hyp_spin_lock_init(&host_mmu.lock);
	mmu->arch = &host_mmu.arch;

	ret = prepare_s2_pool(pgt_pool_base);
	if (ret)
		return ret;

	ret = __kvm_pgtable_stage2_init(&host_mmu.pgt, mmu,
					&host_mmu.mm_ops, KVM_HOST_S2_FLAGS,
					host_stage2_force_pte_cb);
	if (ret)
		return ret;

	mmu->pgd_phys = __hyp_pa(host_mmu.pgt.pgd);
	mmu->pgt = &host_mmu.pgt;
	atomic64_set(&mmu->vmid.id, 0);

	return 0;
}

static void *guest_s2_zalloc_pages_exact(size_t size)
{
	void *addr = hyp_alloc_pages(&current_vm->pool, get_order(size));

	WARN_ON(size != (PAGE_SIZE << get_order(size)));
	hyp_split_page(hyp_virt_to_page(addr));

	return addr;
}

static void guest_s2_free_pages_exact(void *addr, unsigned long size)
{
	u8 order = get_order(size);
	unsigned int i;

	for (i = 0; i < (1 << order); i++)
		hyp_put_page(&current_vm->pool, addr + (i * PAGE_SIZE));
}

static void *guest_s2_zalloc_page(void *mc)
{
	struct hyp_page *p;
	void *addr;

	addr = hyp_alloc_pages(&current_vm->pool, 0);
	if (addr)
		return addr;

	addr = pop_hyp_memcache(mc, hyp_phys_to_virt);
	if (!addr)
		return addr;

	memset(addr, 0, PAGE_SIZE);
	p = hyp_virt_to_page(addr);
	p->refcount = 1;
	p->order = 0;

	return addr;
}

static void guest_s2_get_page(void *addr)
{
	hyp_get_page(&current_vm->pool, addr);
}

static void guest_s2_put_page(void *addr)
{
	hyp_put_page(&current_vm->pool, addr);
}

static void __apply_guest_page(void *va, size_t size,
			       void (*func)(void *addr, size_t size))
{
	size += va - PTR_ALIGN_DOWN(va, PAGE_SIZE);
	va = PTR_ALIGN_DOWN(va, PAGE_SIZE);
	size = PAGE_ALIGN(size);

	while (size) {
		size_t map_size = PAGE_SIZE;
		void *map;

		if (IS_ALIGNED((unsigned long)va, PMD_SIZE) && size >= PMD_SIZE)
			map = hyp_fixblock_map(__hyp_pa(va), &map_size);
		else
			map = hyp_fixmap_map(__hyp_pa(va));

		func(map, map_size);

		if (map_size == PMD_SIZE)
			hyp_fixblock_unmap();
		else
			hyp_fixmap_unmap();

		size -= map_size;
		va += map_size;
	}
}

static void clean_dcache_guest_page(void *va, size_t size)
{
	__apply_guest_page(va, size, __clean_dcache_guest_page);
}

static void invalidate_icache_guest_page(void *va, size_t size)
{
	__apply_guest_page(va, size, __invalidate_icache_guest_page);
}

int kvm_guest_prepare_stage2(struct pkvm_hyp_vm *vm, void *pgd)
{
	struct kvm_s2_mmu *mmu = &vm->kvm.arch.mmu;
	unsigned long nr_pages;
	int ret;

	nr_pages = kvm_pgtable_stage2_pgd_size(mmu->vtcr) >> PAGE_SHIFT;
	ret = hyp_pool_init(&vm->pool, hyp_virt_to_pfn(pgd), nr_pages, 0);
	if (ret)
		return ret;

	hyp_spin_lock_init(&vm->lock);
	vm->mm_ops = (struct kvm_pgtable_mm_ops) {
		.zalloc_pages_exact	= guest_s2_zalloc_pages_exact,
		.free_pages_exact	= guest_s2_free_pages_exact,
		.zalloc_page		= guest_s2_zalloc_page,
		.phys_to_virt		= hyp_phys_to_virt,
		.virt_to_phys		= hyp_virt_to_phys,
		.page_count		= hyp_page_count,
		.get_page		= guest_s2_get_page,
		.put_page		= guest_s2_put_page,
		.dcache_clean_inval_poc	= clean_dcache_guest_page,
		.icache_inval_pou	= invalidate_icache_guest_page,
	};

	guest_lock_component(vm);
	ret = __kvm_pgtable_stage2_init(mmu->pgt, mmu, &vm->mm_ops, 0, NULL);
	guest_unlock_component(vm);
	if (ret)
		return ret;

	vm->kvm.arch.mmu.pgd_phys = __hyp_pa(vm->pgt.pgd);

	return 0;
}

void reclaim_pgtable_pages(struct pkvm_hyp_vm *vm, struct kvm_hyp_memcache *mc)
{
	struct hyp_page *page;
	void *addr;

	/* Dump all pgtable pages in the hyp_pool */
	guest_lock_component(vm);
	kvm_pgtable_stage2_destroy(&vm->pgt);
	vm->kvm.arch.mmu.pgd_phys = 0ULL;
	guest_unlock_component(vm);

	/* Drain the hyp_pool into the memcache */
	addr = hyp_alloc_pages(&vm->pool, 0);
	while (addr) {
		page = hyp_virt_to_page(addr);
		page->refcount = 0;
		page->order = 0;
		push_hyp_memcache(mc, addr, hyp_virt_to_phys);
		WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(addr), 1));
		addr = hyp_alloc_pages(&vm->pool, 0);
	}
}

int __pkvm_prot_finalize(void)
{
	struct kvm_s2_mmu *mmu = &host_mmu.arch.mmu;
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);

	if (params->hcr_el2 & HCR_VM)
		return -EPERM;

	params->vttbr = kvm_get_vttbr(mmu);
	params->vtcr = mmu->vtcr;
	params->hcr_el2 |= HCR_VM;

	/*
	 * The CMO below not only cleans the updated params to the
	 * PoC, but also provides the DSB that ensures ongoing
	 * page-table walks that have started before we trapped to EL2
	 * have completed.
	 */
	kvm_flush_dcache_to_poc(params, sizeof(*params));

	write_sysreg_hcr(params->hcr_el2);
	__load_stage2(&host_mmu.arch.mmu, &host_mmu.arch);

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
	struct kvm_pgtable *pgt = &host_mmu.pgt;
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

static struct memblock_region *find_mem_range(phys_addr_t addr, struct kvm_mem_range *range)
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
			return reg;
		}
	}

	return NULL;
}

bool addr_is_memory(phys_addr_t phys)
{
	struct kvm_mem_range range;

	return !!find_mem_range(phys, &range);
}

static bool is_in_mem_range(u64 addr, struct kvm_mem_range *range)
{
	return range->start <= addr && addr < range->end;
}

static int check_range_allowed_memory(u64 start, u64 end)
{
	struct memblock_region *reg;
	struct kvm_mem_range range;

	/*
	 * Callers can't check the state of a range that overlaps memory and
	 * MMIO regions, so ensure [start, end[ is in the same kvm_mem_range.
	 */
	reg = find_mem_range(start, &range);
	if (!is_in_mem_range(end - 1, &range))
		return -EINVAL;

	if (!reg || reg->flags & MEMBLOCK_NOMAP)
		return -EPERM;

	return 0;
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
	return kvm_pgtable_stage2_map(&host_mmu.pgt, start, end - start, start,
				      prot, &host_s2_pool, 0);
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
		hyp_assert_lock_held(&host_mmu.lock);			\
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
	u64 granule;
	s8 level;
	int ret;

	hyp_assert_lock_held(&host_mmu.lock);
	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, addr, &pte, &level);
	if (ret)
		return ret;

	if (kvm_pte_valid(pte))
		return -EAGAIN;

	if (pte) {
		WARN_ON(addr_is_memory(addr) &&
			get_host_state(hyp_phys_to_page(addr)) != PKVM_NOPAGE);
		return -EPERM;
	}

	for (; level <= KVM_PGTABLE_LAST_LEVEL; level++) {
		if (!kvm_level_supports_block_mapping(level))
			continue;
		granule = kvm_granule_size(level);
		cur.start = ALIGN_DOWN(addr, granule);
		cur.end = cur.start + granule;
		if (!range_included(&cur, range))
			continue;
		*range = cur;
		return 0;
	}

	WARN_ON(1);

	return -EINVAL;
}

int host_stage2_idmap_locked(phys_addr_t addr, u64 size,
			     enum kvm_pgtable_prot prot)
{
	return host_stage2_try(__host_stage2_idmap, addr, addr + size, prot);
}

static void __host_update_page_state(phys_addr_t addr, u64 size, enum pkvm_page_state state)
{
	for_each_hyp_page(page, addr, size)
		set_host_state(page, state);
}

int host_stage2_set_owner_locked(phys_addr_t addr, u64 size, u8 owner_id)
{
	int ret;

	if (!range_is_memory(addr, addr + size))
		return -EPERM;

	ret = host_stage2_try(kvm_pgtable_stage2_set_owner, &host_mmu.pgt,
			      addr, size, &host_s2_pool, owner_id);
	if (ret)
		return ret;

	/* Don't forget to update the vmemmap tracking for the host */
	if (owner_id == PKVM_ID_HOST)
		__host_update_page_state(addr, size, PKVM_PAGE_OWNED);
	else
		__host_update_page_state(addr, size, PKVM_NOPAGE);

	return 0;
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
	bool is_memory = !!find_mem_range(addr, &range);
	enum kvm_pgtable_prot prot;
	int ret;

	prot = is_memory ? PKVM_HOST_MEM_PROT : PKVM_HOST_MMIO_PROT;

	host_lock_component();
	ret = host_stage2_adjust_range(addr, &range);
	if (ret)
		goto unlock;

	ret = host_stage2_idmap_locked(range.start, range.end - range.start, prot);
unlock:
	host_unlock_component();

	return ret;
}

void handle_host_mem_abort(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_vcpu_fault_info fault;
	u64 esr, addr;
	int ret = 0;

	esr = read_sysreg_el2(SYS_ESR);
	if (!__get_fault_info(esr, &fault)) {
		/*
		 * We've presumably raced with a page-table change which caused
		 * AT to fail, try again.
		 */
		return;
	}


	/*
	 * Yikes, we couldn't resolve the fault IPA. This should reinject an
	 * abort into the host when we figure out how to do that.
	 */
	BUG_ON(!(fault.hpfar_el2 & HPFAR_EL2_NS));
	addr = FIELD_GET(HPFAR_EL2_FIPA, fault.hpfar_el2) << 12;

	ret = host_stage2_idmap(addr);
	BUG_ON(ret && ret != -EAGAIN);
}

struct check_walk_data {
	enum pkvm_page_state	desired;
	enum pkvm_page_state	(*get_page_state)(kvm_pte_t pte, u64 addr);
};

static int __check_page_state_visitor(const struct kvm_pgtable_visit_ctx *ctx,
				      enum kvm_pgtable_walk_flags visit)
{
	struct check_walk_data *d = ctx->arg;

	return d->get_page_state(ctx->old, ctx->addr) == d->desired ? 0 : -EPERM;
}

static int check_page_state_range(struct kvm_pgtable *pgt, u64 addr, u64 size,
				  struct check_walk_data *data)
{
	struct kvm_pgtable_walker walker = {
		.cb	= __check_page_state_visitor,
		.arg	= data,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};

	return kvm_pgtable_walk(pgt, addr, size, &walker);
}

static int __host_check_page_state_range(u64 addr, u64 size,
					 enum pkvm_page_state state)
{
	int ret;

	ret = check_range_allowed_memory(addr, addr + size);
	if (ret)
		return ret;

	hyp_assert_lock_held(&host_mmu.lock);

	for_each_hyp_page(page, addr, size) {
		if (get_host_state(page) != state)
			return -EPERM;
	}

	return 0;
}

static int __host_set_page_state_range(u64 addr, u64 size,
				       enum pkvm_page_state state)
{
	if (get_host_state(hyp_phys_to_page(addr)) == PKVM_NOPAGE) {
		int ret = host_stage2_idmap_locked(addr, size, PKVM_HOST_MEM_PROT);

		if (ret)
			return ret;
	}

	__host_update_page_state(addr, size, state);

	return 0;
}

static void __hyp_set_page_state_range(phys_addr_t phys, u64 size, enum pkvm_page_state state)
{
	for_each_hyp_page(page, phys, size)
		set_hyp_state(page, state);
}

static int __hyp_check_page_state_range(phys_addr_t phys, u64 size, enum pkvm_page_state state)
{
	for_each_hyp_page(page, phys, size) {
		if (get_hyp_state(page) != state)
			return -EPERM;
	}

	return 0;
}

static enum pkvm_page_state guest_get_page_state(kvm_pte_t pte, u64 addr)
{
	if (!kvm_pte_valid(pte))
		return PKVM_NOPAGE;

	return pkvm_getstate(kvm_pgtable_stage2_pte_prot(pte));
}

static int __guest_check_page_state_range(struct pkvm_hyp_vm *vm, u64 addr,
					  u64 size, enum pkvm_page_state state)
{
	struct check_walk_data d = {
		.desired	= state,
		.get_page_state	= guest_get_page_state,
	};

	hyp_assert_lock_held(&vm->lock);
	return check_page_state_range(&vm->pgt, addr, size, &d);
}

int __pkvm_host_share_hyp(u64 pfn)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 size = PAGE_SIZE;
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_OWNED);
	if (ret)
		goto unlock;
	ret = __hyp_check_page_state_range(phys, size, PKVM_NOPAGE);
	if (ret)
		goto unlock;

	__hyp_set_page_state_range(phys, size, PKVM_PAGE_SHARED_BORROWED);
	WARN_ON(__host_set_page_state_range(phys, size, PKVM_PAGE_SHARED_OWNED));

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_host_unshare_hyp(u64 pfn)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 virt = (u64)__hyp_va(phys);
	u64 size = PAGE_SIZE;
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_SHARED_OWNED);
	if (ret)
		goto unlock;
	ret = __hyp_check_page_state_range(phys, size, PKVM_PAGE_SHARED_BORROWED);
	if (ret)
		goto unlock;
	if (hyp_page_count((void *)virt)) {
		ret = -EBUSY;
		goto unlock;
	}

	__hyp_set_page_state_range(phys, size, PKVM_NOPAGE);
	WARN_ON(__host_set_page_state_range(phys, size, PKVM_PAGE_OWNED));

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_host_donate_hyp(u64 pfn, u64 nr_pages)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 size = PAGE_SIZE * nr_pages;
	void *virt = __hyp_va(phys);
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_OWNED);
	if (ret)
		goto unlock;
	ret = __hyp_check_page_state_range(phys, size, PKVM_NOPAGE);
	if (ret)
		goto unlock;

	__hyp_set_page_state_range(phys, size, PKVM_PAGE_OWNED);
	WARN_ON(pkvm_create_mappings_locked(virt, virt + size, PAGE_HYP));
	WARN_ON(host_stage2_set_owner_locked(phys, size, PKVM_ID_HYP));

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_hyp_donate_host(u64 pfn, u64 nr_pages)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 size = PAGE_SIZE * nr_pages;
	u64 virt = (u64)__hyp_va(phys);
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __hyp_check_page_state_range(phys, size, PKVM_PAGE_OWNED);
	if (ret)
		goto unlock;
	ret = __host_check_page_state_range(phys, size, PKVM_NOPAGE);
	if (ret)
		goto unlock;

	__hyp_set_page_state_range(phys, size, PKVM_NOPAGE);
	WARN_ON(kvm_pgtable_hyp_unmap(&pkvm_pgtable, virt, size) != size);
	WARN_ON(host_stage2_set_owner_locked(phys, size, PKVM_ID_HOST));

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int hyp_pin_shared_mem(void *from, void *to)
{
	u64 cur, start = ALIGN_DOWN((u64)from, PAGE_SIZE);
	u64 end = PAGE_ALIGN((u64)to);
	u64 phys = __hyp_pa(start);
	u64 size = end - start;
	struct hyp_page *p;
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_SHARED_OWNED);
	if (ret)
		goto unlock;

	ret = __hyp_check_page_state_range(phys, size, PKVM_PAGE_SHARED_BORROWED);
	if (ret)
		goto unlock;

	for (cur = start; cur < end; cur += PAGE_SIZE) {
		p = hyp_virt_to_page(cur);
		hyp_page_ref_inc(p);
		if (p->refcount == 1)
			WARN_ON(pkvm_create_mappings_locked((void *)cur,
							    (void *)cur + PAGE_SIZE,
							    PAGE_HYP));
	}

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

void hyp_unpin_shared_mem(void *from, void *to)
{
	u64 cur, start = ALIGN_DOWN((u64)from, PAGE_SIZE);
	u64 end = PAGE_ALIGN((u64)to);
	struct hyp_page *p;

	host_lock_component();
	hyp_lock_component();

	for (cur = start; cur < end; cur += PAGE_SIZE) {
		p = hyp_virt_to_page(cur);
		if (p->refcount == 1)
			WARN_ON(kvm_pgtable_hyp_unmap(&pkvm_pgtable, cur, PAGE_SIZE) != PAGE_SIZE);
		hyp_page_ref_dec(p);
	}

	hyp_unlock_component();
	host_unlock_component();
}

int __pkvm_host_share_ffa(u64 pfn, u64 nr_pages)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 size = PAGE_SIZE * nr_pages;
	int ret;

	host_lock_component();
	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_OWNED);
	if (!ret)
		ret = __host_set_page_state_range(phys, size, PKVM_PAGE_SHARED_OWNED);
	host_unlock_component();

	return ret;
}

int __pkvm_host_unshare_ffa(u64 pfn, u64 nr_pages)
{
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 size = PAGE_SIZE * nr_pages;
	int ret;

	host_lock_component();
	ret = __host_check_page_state_range(phys, size, PKVM_PAGE_SHARED_OWNED);
	if (!ret)
		ret = __host_set_page_state_range(phys, size, PKVM_PAGE_OWNED);
	host_unlock_component();

	return ret;
}

static int __guest_check_transition_size(u64 phys, u64 ipa, u64 nr_pages, u64 *size)
{
	size_t block_size;

	if (nr_pages == 1) {
		*size = PAGE_SIZE;
		return 0;
	}

	/* We solely support second to last level huge mapping */
	block_size = kvm_granule_size(KVM_PGTABLE_LAST_LEVEL - 1);

	if (nr_pages != block_size >> PAGE_SHIFT)
		return -EINVAL;

	if (!IS_ALIGNED(phys | ipa, block_size))
		return -EINVAL;

	*size = block_size;
	return 0;
}

int __pkvm_host_share_guest(u64 pfn, u64 gfn, u64 nr_pages, struct pkvm_hyp_vcpu *vcpu,
			    enum kvm_pgtable_prot prot)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	u64 phys = hyp_pfn_to_phys(pfn);
	u64 ipa = hyp_pfn_to_phys(gfn);
	u64 size;
	int ret;

	if (prot & ~KVM_PGTABLE_PROT_RWX)
		return -EINVAL;

	ret = __guest_check_transition_size(phys, ipa, nr_pages, &size);
	if (ret)
		return ret;

	ret = check_range_allowed_memory(phys, phys + size);
	if (ret)
		return ret;

	host_lock_component();
	guest_lock_component(vm);

	ret = __guest_check_page_state_range(vm, ipa, size, PKVM_NOPAGE);
	if (ret)
		goto unlock;

	for_each_hyp_page(page, phys, size) {
		switch (get_host_state(page)) {
		case PKVM_PAGE_OWNED:
			continue;
		case PKVM_PAGE_SHARED_OWNED:
			if (page->host_share_guest_count == U32_MAX) {
				ret = -EBUSY;
				goto unlock;
			}

			/* Only host to np-guest multi-sharing is tolerated */
			if (page->host_share_guest_count)
				continue;

			fallthrough;
		default:
			ret = -EPERM;
			goto unlock;
		}
	}

	for_each_hyp_page(page, phys, size) {
		set_host_state(page, PKVM_PAGE_SHARED_OWNED);
		page->host_share_guest_count++;
	}

	WARN_ON(kvm_pgtable_stage2_map(&vm->pgt, ipa, size, phys,
				       pkvm_mkstate(prot, PKVM_PAGE_SHARED_BORROWED),
				       &vcpu->vcpu.arch.pkvm_memcache, 0));

unlock:
	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
}

static int __check_host_shared_guest(struct pkvm_hyp_vm *vm, u64 *__phys, u64 ipa, u64 size)
{
	enum pkvm_page_state state;
	kvm_pte_t pte;
	u64 phys;
	s8 level;
	int ret;

	ret = kvm_pgtable_get_leaf(&vm->pgt, ipa, &pte, &level);
	if (ret)
		return ret;
	if (!kvm_pte_valid(pte))
		return -ENOENT;
	if (size && kvm_granule_size(level) != size)
		return -E2BIG;

	if (!size)
		size = kvm_granule_size(level);

	state = guest_get_page_state(pte, ipa);
	if (state != PKVM_PAGE_SHARED_BORROWED)
		return -EPERM;

	phys = kvm_pte_to_phys(pte);
	ret = check_range_allowed_memory(phys, phys + size);
	if (WARN_ON(ret))
		return ret;

	for_each_hyp_page(page, phys, size) {
		if (get_host_state(page) != PKVM_PAGE_SHARED_OWNED)
			return -EPERM;
		if (WARN_ON(!page->host_share_guest_count))
			return -EINVAL;
	}

	*__phys = phys;

	return 0;
}

int __pkvm_host_unshare_guest(u64 gfn, u64 nr_pages, struct pkvm_hyp_vm *vm)
{
	u64 ipa = hyp_pfn_to_phys(gfn);
	u64 size, phys;
	int ret;

	ret = __guest_check_transition_size(0, ipa, nr_pages, &size);
	if (ret)
		return ret;

	host_lock_component();
	guest_lock_component(vm);

	ret = __check_host_shared_guest(vm, &phys, ipa, size);
	if (ret)
		goto unlock;

	ret = kvm_pgtable_stage2_unmap(&vm->pgt, ipa, size);
	if (ret)
		goto unlock;

	for_each_hyp_page(page, phys, size) {
		/* __check_host_shared_guest() protects against underflow */
		page->host_share_guest_count--;
		if (!page->host_share_guest_count)
			set_host_state(page, PKVM_PAGE_OWNED);
	}

unlock:
	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
}

static void assert_host_shared_guest(struct pkvm_hyp_vm *vm, u64 ipa, u64 size)
{
	u64 phys;
	int ret;

	if (!IS_ENABLED(CONFIG_NVHE_EL2_DEBUG))
		return;

	host_lock_component();
	guest_lock_component(vm);

	ret = __check_host_shared_guest(vm, &phys, ipa, size);

	guest_unlock_component(vm);
	host_unlock_component();

	WARN_ON(ret && ret != -ENOENT);
}

int __pkvm_host_relax_perms_guest(u64 gfn, struct pkvm_hyp_vcpu *vcpu, enum kvm_pgtable_prot prot)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	u64 ipa = hyp_pfn_to_phys(gfn);
	int ret;

	if (pkvm_hyp_vm_is_protected(vm))
		return -EPERM;

	if (prot & ~KVM_PGTABLE_PROT_RWX)
		return -EINVAL;

	assert_host_shared_guest(vm, ipa, 0);
	guest_lock_component(vm);
	ret = kvm_pgtable_stage2_relax_perms(&vm->pgt, ipa, prot, 0);
	guest_unlock_component(vm);

	return ret;
}

int __pkvm_host_wrprotect_guest(u64 gfn, u64 nr_pages, struct pkvm_hyp_vm *vm)
{
	u64 size, ipa = hyp_pfn_to_phys(gfn);
	int ret;

	if (pkvm_hyp_vm_is_protected(vm))
		return -EPERM;

	ret = __guest_check_transition_size(0, ipa, nr_pages, &size);
	if (ret)
		return ret;

	assert_host_shared_guest(vm, ipa, size);
	guest_lock_component(vm);
	ret = kvm_pgtable_stage2_wrprotect(&vm->pgt, ipa, size);
	guest_unlock_component(vm);

	return ret;
}

int __pkvm_host_test_clear_young_guest(u64 gfn, u64 nr_pages, bool mkold, struct pkvm_hyp_vm *vm)
{
	u64 size, ipa = hyp_pfn_to_phys(gfn);
	int ret;

	if (pkvm_hyp_vm_is_protected(vm))
		return -EPERM;

	ret = __guest_check_transition_size(0, ipa, nr_pages, &size);
	if (ret)
		return ret;

	assert_host_shared_guest(vm, ipa, size);
	guest_lock_component(vm);
	ret = kvm_pgtable_stage2_test_clear_young(&vm->pgt, ipa, size, mkold);
	guest_unlock_component(vm);

	return ret;
}

int __pkvm_host_mkyoung_guest(u64 gfn, struct pkvm_hyp_vcpu *vcpu)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	u64 ipa = hyp_pfn_to_phys(gfn);

	if (pkvm_hyp_vm_is_protected(vm))
		return -EPERM;

	assert_host_shared_guest(vm, ipa, 0);
	guest_lock_component(vm);
	kvm_pgtable_stage2_mkyoung(&vm->pgt, ipa, 0);
	guest_unlock_component(vm);

	return 0;
}

#ifdef CONFIG_NVHE_EL2_DEBUG
struct pkvm_expected_state {
	enum pkvm_page_state host;
	enum pkvm_page_state hyp;
	enum pkvm_page_state guest[2]; /* [ gfn, gfn + 1 ] */
};

static struct pkvm_expected_state selftest_state;
static struct hyp_page *selftest_page;

static struct pkvm_hyp_vm selftest_vm = {
	.kvm = {
		.arch = {
			.mmu = {
				.arch = &selftest_vm.kvm.arch,
				.pgt = &selftest_vm.pgt,
			},
		},
	},
};

static struct pkvm_hyp_vcpu selftest_vcpu = {
	.vcpu = {
		.arch = {
			.hw_mmu = &selftest_vm.kvm.arch.mmu,
		},
		.kvm = &selftest_vm.kvm,
	},
};

static void init_selftest_vm(void *virt)
{
	struct hyp_page *p = hyp_virt_to_page(virt);
	int i;

	selftest_vm.kvm.arch.mmu.vtcr = host_mmu.arch.mmu.vtcr;
	WARN_ON(kvm_guest_prepare_stage2(&selftest_vm, virt));

	for (i = 0; i < pkvm_selftest_pages(); i++) {
		if (p[i].refcount)
			continue;
		p[i].refcount = 1;
		hyp_put_page(&selftest_vm.pool, hyp_page_to_virt(&p[i]));
	}
}

static u64 selftest_ipa(void)
{
	return BIT(selftest_vm.pgt.ia_bits - 1);
}

static void assert_page_state(void)
{
	void *virt = hyp_page_to_virt(selftest_page);
	u64 size = PAGE_SIZE << selftest_page->order;
	struct pkvm_hyp_vcpu *vcpu = &selftest_vcpu;
	u64 phys = hyp_virt_to_phys(virt);
	u64 ipa[2] = { selftest_ipa(), selftest_ipa() + PAGE_SIZE };
	struct pkvm_hyp_vm *vm;

	vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);

	host_lock_component();
	WARN_ON(__host_check_page_state_range(phys, size, selftest_state.host));
	host_unlock_component();

	hyp_lock_component();
	WARN_ON(__hyp_check_page_state_range(phys, size, selftest_state.hyp));
	hyp_unlock_component();

	guest_lock_component(&selftest_vm);
	WARN_ON(__guest_check_page_state_range(vm, ipa[0], size, selftest_state.guest[0]));
	WARN_ON(__guest_check_page_state_range(vm, ipa[1], size, selftest_state.guest[1]));
	guest_unlock_component(&selftest_vm);
}

#define assert_transition_res(res, fn, ...)		\
	do {						\
		WARN_ON(fn(__VA_ARGS__) != res);	\
		assert_page_state();			\
	} while (0)

void pkvm_ownership_selftest(void *base)
{
	enum kvm_pgtable_prot prot = KVM_PGTABLE_PROT_RWX;
	void *virt = hyp_alloc_pages(&host_s2_pool, 0);
	struct pkvm_hyp_vcpu *vcpu = &selftest_vcpu;
	struct pkvm_hyp_vm *vm = &selftest_vm;
	u64 phys, size, pfn, gfn;

	WARN_ON(!virt);
	selftest_page = hyp_virt_to_page(virt);
	selftest_page->refcount = 0;
	init_selftest_vm(base);

	size = PAGE_SIZE << selftest_page->order;
	phys = hyp_virt_to_phys(virt);
	pfn = hyp_phys_to_pfn(phys);
	gfn = hyp_phys_to_pfn(selftest_ipa());

	selftest_state.host = PKVM_NOPAGE;
	selftest_state.hyp = PKVM_PAGE_OWNED;
	selftest_state.guest[0] = selftest_state.guest[1] = PKVM_NOPAGE;
	assert_page_state();
	assert_transition_res(-EPERM,	__pkvm_host_donate_hyp, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_ffa, pfn, 1);
	assert_transition_res(-EPERM,	hyp_pin_shared_mem, virt, virt + size);
	assert_transition_res(-EPERM,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-ENOENT,	__pkvm_host_unshare_guest, gfn, 1, vm);

	selftest_state.host = PKVM_PAGE_OWNED;
	selftest_state.hyp = PKVM_NOPAGE;
	assert_transition_res(0,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_ffa, pfn, 1);
	assert_transition_res(-ENOENT,	__pkvm_host_unshare_guest, gfn, 1, vm);
	assert_transition_res(-EPERM,	hyp_pin_shared_mem, virt, virt + size);

	selftest_state.host = PKVM_PAGE_SHARED_OWNED;
	selftest_state.hyp = PKVM_PAGE_SHARED_BORROWED;
	assert_transition_res(0,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_donate_hyp, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-ENOENT,	__pkvm_host_unshare_guest, gfn, 1, vm);

	assert_transition_res(0,	hyp_pin_shared_mem, virt, virt + size);
	assert_transition_res(0,	hyp_pin_shared_mem, virt, virt + size);
	hyp_unpin_shared_mem(virt, virt + size);
	WARN_ON(hyp_page_count(virt) != 1);
	assert_transition_res(-EBUSY,	__pkvm_host_unshare_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_donate_hyp, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-ENOENT,	__pkvm_host_unshare_guest, gfn, 1, vm);

	hyp_unpin_shared_mem(virt, virt + size);
	assert_page_state();
	WARN_ON(hyp_page_count(virt));

	selftest_state.host = PKVM_PAGE_OWNED;
	selftest_state.hyp = PKVM_NOPAGE;
	assert_transition_res(0,	__pkvm_host_unshare_hyp, pfn);

	selftest_state.host = PKVM_PAGE_SHARED_OWNED;
	selftest_state.hyp = PKVM_NOPAGE;
	assert_transition_res(0,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_donate_hyp, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-ENOENT,	__pkvm_host_unshare_guest, gfn, 1, vm);
	assert_transition_res(-EPERM,	hyp_pin_shared_mem, virt, virt + size);

	selftest_state.host = PKVM_PAGE_OWNED;
	selftest_state.hyp = PKVM_NOPAGE;
	assert_transition_res(0,	__pkvm_host_unshare_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_ffa, pfn, 1);

	selftest_state.host = PKVM_PAGE_SHARED_OWNED;
	selftest_state.guest[0] = PKVM_PAGE_SHARED_BORROWED;
	assert_transition_res(0,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-EPERM,	__pkvm_host_share_guest, pfn, gfn, 1, vcpu, prot);
	assert_transition_res(-EPERM,	__pkvm_host_share_ffa, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_donate_hyp, pfn, 1);
	assert_transition_res(-EPERM,	__pkvm_host_share_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_host_unshare_hyp, pfn);
	assert_transition_res(-EPERM,	__pkvm_hyp_donate_host, pfn, 1);
	assert_transition_res(-EPERM,	hyp_pin_shared_mem, virt, virt + size);

	selftest_state.guest[1] = PKVM_PAGE_SHARED_BORROWED;
	assert_transition_res(0,	__pkvm_host_share_guest, pfn, gfn + 1, 1, vcpu, prot);
	WARN_ON(hyp_virt_to_page(virt)->host_share_guest_count != 2);

	selftest_state.guest[0] = PKVM_NOPAGE;
	assert_transition_res(0,	__pkvm_host_unshare_guest, gfn, 1, vm);

	selftest_state.guest[1] = PKVM_NOPAGE;
	selftest_state.host = PKVM_PAGE_OWNED;
	assert_transition_res(0,	__pkvm_host_unshare_guest, gfn + 1, 1, vm);

	selftest_state.host = PKVM_NOPAGE;
	selftest_state.hyp = PKVM_PAGE_OWNED;
	assert_transition_res(0,	__pkvm_host_donate_hyp, pfn, 1);

	selftest_page->refcount = 1;
	hyp_put_page(&host_s2_pool, virt);
}
#endif
