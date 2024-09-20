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

static bool guest_stage2_force_pte_cb(u64 addr, u64 end,
				      enum kvm_pgtable_prot prot)
{
	return true;
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
	memset(p, 0, sizeof(*p));
	p->refcount = 1;

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

static void clean_dcache_guest_page(void *va, size_t size)
{
	__clean_dcache_guest_page(hyp_fixmap_map(__hyp_pa(va)), size);
	hyp_fixmap_unmap();
}

static void invalidate_icache_guest_page(void *va, size_t size)
{
	__invalidate_icache_guest_page(hyp_fixmap_map(__hyp_pa(va)), size);
	hyp_fixmap_unmap();
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
	ret = __kvm_pgtable_stage2_init(mmu->pgt, mmu, &vm->mm_ops, 0,
					guest_stage2_force_pte_cb);
	guest_unlock_component(vm);
	if (ret)
		return ret;

	vm->kvm.arch.mmu.pgd_phys = __hyp_pa(vm->pgt.pgd);

	return 0;
}

void reclaim_guest_pages(struct pkvm_hyp_vm *vm, struct kvm_hyp_memcache *mc)
{
	void *addr;

	/* Dump all pgtable pages in the hyp_pool */
	guest_lock_component(vm);
	kvm_pgtable_stage2_destroy(&vm->pgt);
	vm->kvm.arch.mmu.pgd_phys = 0ULL;
	guest_unlock_component(vm);

	/* Drain the hyp_pool into the memcache */
	addr = hyp_alloc_pages(&vm->pool, 0);
	while (addr) {
		memset(hyp_virt_to_page(addr), 0, sizeof(struct hyp_page));
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

	write_sysreg(params->hcr_el2, hcr_el2);
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

static bool addr_is_allowed_memory(phys_addr_t phys)
{
	struct memblock_region *reg;
	struct kvm_mem_range range;

	reg = find_mem_range(phys, &range);

	return reg && !(reg->flags & MEMBLOCK_NOMAP);
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
	s8 level;
	int ret;

	hyp_assert_lock_held(&host_mmu.lock);
	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, addr, &pte, &level);
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
	} while ((level <= KVM_PGTABLE_LAST_LEVEL) &&
			!(kvm_level_supports_block_mapping(level) &&
			  range_included(&cur, range)));

	*range = cur;

	return 0;
}

int host_stage2_idmap_locked(phys_addr_t addr, u64 size,
			     enum kvm_pgtable_prot prot)
{
	return host_stage2_try(__host_stage2_idmap, addr, addr + size, prot);
}

int host_stage2_set_owner_locked(phys_addr_t addr, u64 size, u8 owner_id)
{
	return host_stage2_try(kvm_pgtable_stage2_set_owner, &host_mmu.pgt,
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

	addr = (fault.hpfar_el2 & HPFAR_MASK) << 8;
	ret = host_stage2_idmap(addr);
	BUG_ON(ret && ret != -EAGAIN);
}

struct pkvm_mem_transition {
	u64				nr_pages;

	struct {
		enum pkvm_component_id	id;
		/* Address in the initiator's address space */
		u64			addr;

		union {
			struct {
				/* Address in the completer's address space */
				u64	completer_addr;
			} host;
			struct {
				u64	completer_addr;
			} hyp;
		};
	} initiator;

	struct {
		enum pkvm_component_id	id;
	} completer;
};

struct pkvm_mem_share {
	const struct pkvm_mem_transition	tx;
	const enum kvm_pgtable_prot		completer_prot;
};

struct pkvm_mem_donation {
	const struct pkvm_mem_transition	tx;
};

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

static enum pkvm_page_state host_get_page_state(kvm_pte_t pte, u64 addr)
{
	if (!addr_is_allowed_memory(addr))
		return PKVM_NOPAGE;

	if (!kvm_pte_valid(pte) && pte)
		return PKVM_NOPAGE;

	return pkvm_getstate(kvm_pgtable_stage2_pte_prot(pte));
}

static int __host_check_page_state_range(u64 addr, u64 size,
					 enum pkvm_page_state state)
{
	struct check_walk_data d = {
		.desired	= state,
		.get_page_state	= host_get_page_state,
	};

	hyp_assert_lock_held(&host_mmu.lock);
	return check_page_state_range(&host_mmu.pgt, addr, size, &d);
}

static int __host_set_page_state_range(u64 addr, u64 size,
				       enum pkvm_page_state state)
{
	enum kvm_pgtable_prot prot = pkvm_mkstate(PKVM_HOST_MEM_PROT, state);

	return host_stage2_idmap_locked(addr, size, prot);
}

static int host_request_owned_transition(u64 *completer_addr,
					 const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;

	*completer_addr = tx->initiator.host.completer_addr;
	return __host_check_page_state_range(addr, size, PKVM_PAGE_OWNED);
}

static int host_request_unshare(u64 *completer_addr,
				const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;

	*completer_addr = tx->initiator.host.completer_addr;
	return __host_check_page_state_range(addr, size, PKVM_PAGE_SHARED_OWNED);
}

static int host_initiate_share(u64 *completer_addr,
			       const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;

	*completer_addr = tx->initiator.host.completer_addr;
	return __host_set_page_state_range(addr, size, PKVM_PAGE_SHARED_OWNED);
}

static int host_initiate_unshare(u64 *completer_addr,
				 const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;

	*completer_addr = tx->initiator.host.completer_addr;
	return __host_set_page_state_range(addr, size, PKVM_PAGE_OWNED);
}

static int host_initiate_donation(u64 *completer_addr,
				  const struct pkvm_mem_transition *tx)
{
	u8 owner_id = tx->completer.id;
	u64 size = tx->nr_pages * PAGE_SIZE;

	*completer_addr = tx->initiator.host.completer_addr;
	return host_stage2_set_owner_locked(tx->initiator.addr, size, owner_id);
}

static bool __host_ack_skip_pgtable_check(const struct pkvm_mem_transition *tx)
{
	return !(IS_ENABLED(CONFIG_NVHE_EL2_DEBUG) ||
		 tx->initiator.id != PKVM_ID_HYP);
}

static int __host_ack_transition(u64 addr, const struct pkvm_mem_transition *tx,
				 enum pkvm_page_state state)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (__host_ack_skip_pgtable_check(tx))
		return 0;

	return __host_check_page_state_range(addr, size, state);
}

static int host_ack_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	return __host_ack_transition(addr, tx, PKVM_NOPAGE);
}

static int host_complete_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u8 host_id = tx->completer.id;

	return host_stage2_set_owner_locked(addr, size, host_id);
}

static enum pkvm_page_state hyp_get_page_state(kvm_pte_t pte, u64 addr)
{
	if (!kvm_pte_valid(pte))
		return PKVM_NOPAGE;

	return pkvm_getstate(kvm_pgtable_hyp_pte_prot(pte));
}

static int __hyp_check_page_state_range(u64 addr, u64 size,
					enum pkvm_page_state state)
{
	struct check_walk_data d = {
		.desired	= state,
		.get_page_state	= hyp_get_page_state,
	};

	hyp_assert_lock_held(&pkvm_pgd_lock);
	return check_page_state_range(&pkvm_pgtable, addr, size, &d);
}

static int hyp_request_donation(u64 *completer_addr,
				const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;

	*completer_addr = tx->initiator.hyp.completer_addr;
	return __hyp_check_page_state_range(addr, size, PKVM_PAGE_OWNED);
}

static int hyp_initiate_donation(u64 *completer_addr,
				 const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	int ret;

	*completer_addr = tx->initiator.hyp.completer_addr;
	ret = kvm_pgtable_hyp_unmap(&pkvm_pgtable, tx->initiator.addr, size);
	return (ret != size) ? -EFAULT : 0;
}

static bool __hyp_ack_skip_pgtable_check(const struct pkvm_mem_transition *tx)
{
	return !(IS_ENABLED(CONFIG_NVHE_EL2_DEBUG) ||
		 tx->initiator.id != PKVM_ID_HOST);
}

static int hyp_ack_share(u64 addr, const struct pkvm_mem_transition *tx,
			 enum kvm_pgtable_prot perms)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (perms != PAGE_HYP)
		return -EPERM;

	if (__hyp_ack_skip_pgtable_check(tx))
		return 0;

	return __hyp_check_page_state_range(addr, size, PKVM_NOPAGE);
}

static int hyp_ack_unshare(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (tx->initiator.id == PKVM_ID_HOST && hyp_page_count((void *)addr))
		return -EBUSY;

	if (__hyp_ack_skip_pgtable_check(tx))
		return 0;

	return __hyp_check_page_state_range(addr, size,
					    PKVM_PAGE_SHARED_BORROWED);
}

static int hyp_ack_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (__hyp_ack_skip_pgtable_check(tx))
		return 0;

	return __hyp_check_page_state_range(addr, size, PKVM_NOPAGE);
}

static int hyp_complete_share(u64 addr, const struct pkvm_mem_transition *tx,
			      enum kvm_pgtable_prot perms)
{
	void *start = (void *)addr, *end = start + (tx->nr_pages * PAGE_SIZE);
	enum kvm_pgtable_prot prot;

	prot = pkvm_mkstate(perms, PKVM_PAGE_SHARED_BORROWED);
	return pkvm_create_mappings_locked(start, end, prot);
}

static int hyp_complete_unshare(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	int ret = kvm_pgtable_hyp_unmap(&pkvm_pgtable, addr, size);

	return (ret != size) ? -EFAULT : 0;
}

static int hyp_complete_donation(u64 addr,
				 const struct pkvm_mem_transition *tx)
{
	void *start = (void *)addr, *end = start + (tx->nr_pages * PAGE_SIZE);
	enum kvm_pgtable_prot prot = pkvm_mkstate(PAGE_HYP, PKVM_PAGE_OWNED);

	return pkvm_create_mappings_locked(start, end, prot);
}

static int check_share(struct pkvm_mem_share *share)
{
	const struct pkvm_mem_transition *tx = &share->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_request_owned_transition(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HYP:
		ret = hyp_ack_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_FFA:
		/*
		 * We only check the host; the secure side will check the other
		 * end when we forward the FFA call.
		 */
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __do_share(struct pkvm_mem_share *share)
{
	const struct pkvm_mem_transition *tx = &share->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_initiate_share(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HYP:
		ret = hyp_complete_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_FFA:
		/*
		 * We're not responsible for any secure page-tables, so there's
		 * nothing to do here.
		 */
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*
 * do_share():
 *
 * The page owner grants access to another component with a given set
 * of permissions.
 *
 * Initiator: OWNED	=> SHARED_OWNED
 * Completer: NOPAGE	=> SHARED_BORROWED
 */
static int do_share(struct pkvm_mem_share *share)
{
	int ret;

	ret = check_share(share);
	if (ret)
		return ret;

	return WARN_ON(__do_share(share));
}

static int check_unshare(struct pkvm_mem_share *share)
{
	const struct pkvm_mem_transition *tx = &share->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_request_unshare(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HYP:
		ret = hyp_ack_unshare(completer_addr, tx);
		break;
	case PKVM_ID_FFA:
		/* See check_share() */
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __do_unshare(struct pkvm_mem_share *share)
{
	const struct pkvm_mem_transition *tx = &share->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_initiate_unshare(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HYP:
		ret = hyp_complete_unshare(completer_addr, tx);
		break;
	case PKVM_ID_FFA:
		/* See __do_share() */
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*
 * do_unshare():
 *
 * The page owner revokes access from another component for a range of
 * pages which were previously shared using do_share().
 *
 * Initiator: SHARED_OWNED	=> OWNED
 * Completer: SHARED_BORROWED	=> NOPAGE
 */
static int do_unshare(struct pkvm_mem_share *share)
{
	int ret;

	ret = check_unshare(share);
	if (ret)
		return ret;

	return WARN_ON(__do_unshare(share));
}

static int check_donation(struct pkvm_mem_donation *donation)
{
	const struct pkvm_mem_transition *tx = &donation->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_request_owned_transition(&completer_addr, tx);
		break;
	case PKVM_ID_HYP:
		ret = hyp_request_donation(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_ack_donation(completer_addr, tx);
		break;
	case PKVM_ID_HYP:
		ret = hyp_ack_donation(completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int __do_donate(struct pkvm_mem_donation *donation)
{
	const struct pkvm_mem_transition *tx = &donation->tx;
	u64 completer_addr;
	int ret;

	switch (tx->initiator.id) {
	case PKVM_ID_HOST:
		ret = host_initiate_donation(&completer_addr, tx);
		break;
	case PKVM_ID_HYP:
		ret = hyp_initiate_donation(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_complete_donation(completer_addr, tx);
		break;
	case PKVM_ID_HYP:
		ret = hyp_complete_donation(completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*
 * do_donate():
 *
 * The page owner transfers ownership to another component, losing access
 * as a consequence.
 *
 * Initiator: OWNED	=> NOPAGE
 * Completer: NOPAGE	=> OWNED
 */
static int do_donate(struct pkvm_mem_donation *donation)
{
	int ret;

	ret = check_donation(donation);
	if (ret)
		return ret;

	return WARN_ON(__do_donate(donation));
}

int __pkvm_host_share_hyp(u64 pfn)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 hyp_addr = (u64)__hyp_va(host_addr);
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= host_addr,
				.host	= {
					.completer_addr = hyp_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HYP,
			},
		},
		.completer_prot	= PAGE_HYP,
	};

	host_lock_component();
	hyp_lock_component();

	ret = do_share(&share);

	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_host_unshare_hyp(u64 pfn)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 hyp_addr = (u64)__hyp_va(host_addr);
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= host_addr,
				.host	= {
					.completer_addr = hyp_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HYP,
			},
		},
		.completer_prot	= PAGE_HYP,
	};

	host_lock_component();
	hyp_lock_component();

	ret = do_unshare(&share);

	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_host_donate_hyp(u64 pfn, u64 nr_pages)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 hyp_addr = (u64)__hyp_va(host_addr);
	struct pkvm_mem_donation donation = {
		.tx	= {
			.nr_pages	= nr_pages,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= host_addr,
				.host	= {
					.completer_addr = hyp_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HYP,
			},
		},
	};

	host_lock_component();
	hyp_lock_component();

	ret = do_donate(&donation);

	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int __pkvm_hyp_donate_host(u64 pfn, u64 nr_pages)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 hyp_addr = (u64)__hyp_va(host_addr);
	struct pkvm_mem_donation donation = {
		.tx	= {
			.nr_pages	= nr_pages,
			.initiator	= {
				.id	= PKVM_ID_HYP,
				.addr	= hyp_addr,
				.hyp	= {
					.completer_addr = host_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HOST,
			},
		},
	};

	host_lock_component();
	hyp_lock_component();

	ret = do_donate(&donation);

	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

int hyp_pin_shared_mem(void *from, void *to)
{
	u64 cur, start = ALIGN_DOWN((u64)from, PAGE_SIZE);
	u64 end = PAGE_ALIGN((u64)to);
	u64 size = end - start;
	int ret;

	host_lock_component();
	hyp_lock_component();

	ret = __host_check_page_state_range(__hyp_pa(start), size,
					    PKVM_PAGE_SHARED_OWNED);
	if (ret)
		goto unlock;

	ret = __hyp_check_page_state_range(start, size,
					   PKVM_PAGE_SHARED_BORROWED);
	if (ret)
		goto unlock;

	for (cur = start; cur < end; cur += PAGE_SIZE)
		hyp_page_ref_inc(hyp_virt_to_page(cur));

unlock:
	hyp_unlock_component();
	host_unlock_component();

	return ret;
}

void hyp_unpin_shared_mem(void *from, void *to)
{
	u64 cur, start = ALIGN_DOWN((u64)from, PAGE_SIZE);
	u64 end = PAGE_ALIGN((u64)to);

	host_lock_component();
	hyp_lock_component();

	for (cur = start; cur < end; cur += PAGE_SIZE)
		hyp_page_ref_dec(hyp_virt_to_page(cur));

	hyp_unlock_component();
	host_unlock_component();
}

int __pkvm_host_share_ffa(u64 pfn, u64 nr_pages)
{
	int ret;
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= nr_pages,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= hyp_pfn_to_phys(pfn),
			},
			.completer	= {
				.id	= PKVM_ID_FFA,
			},
		},
	};

	host_lock_component();
	ret = do_share(&share);
	host_unlock_component();

	return ret;
}

int __pkvm_host_unshare_ffa(u64 pfn, u64 nr_pages)
{
	int ret;
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= nr_pages,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= hyp_pfn_to_phys(pfn),
			},
			.completer	= {
				.id	= PKVM_ID_FFA,
			},
		},
	};

	host_lock_component();
	ret = do_unshare(&share);
	host_unlock_component();

	return ret;
}
