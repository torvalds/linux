// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Google LLC
 * Author: Quentin Perret <qperret@google.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_hypevents.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/kvm_pkvm.h>
#include <asm/stage2_pgtable.h>

#include <hyp/adjust_pc.h>
#include <hyp/fault.h>

#include <nvhe/gfp.h>
#include <nvhe/iommu.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>
#include <nvhe/modules.h>

#define KVM_HOST_S2_FLAGS (KVM_PGTABLE_S2_NOFWB | KVM_PGTABLE_S2_IDMAP)

struct host_mmu host_mmu;

struct pkvm_moveable_reg pkvm_moveable_regs[PKVM_NR_MOVEABLE_REGS];
unsigned int pkvm_moveable_regs_nr;

static struct hyp_pool host_s2_pool;

static DEFINE_PER_CPU(struct pkvm_hyp_vm *, __current_vm);
#define current_vm (*this_cpu_ptr(&__current_vm))

static struct kvm_pgtable_pte_ops host_s2_pte_ops;
static bool host_stage2_force_pte(u64 addr, u64 end, enum kvm_pgtable_prot prot);
static bool host_stage2_pte_is_counted(kvm_pte_t pte, u32 level);
static bool guest_stage2_force_pte_cb(u64 addr, u64 end,
				      enum kvm_pgtable_prot prot);
static bool guest_stage2_pte_is_counted(kvm_pte_t pte, u32 level);

static struct kvm_pgtable_pte_ops guest_s2_pte_ops = {
	.force_pte_cb = guest_stage2_force_pte_cb,
	.pte_is_counted_cb = guest_stage2_pte_is_counted
};

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

	host_mmu.arch.vtcr = kvm_get_vtcr(id_aa64mmfr0_el1_sys_val,
					  id_aa64mmfr1_el1_sys_val, phys_shift);
}

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

	host_s2_pte_ops.force_pte_cb = host_stage2_force_pte;
	host_s2_pte_ops.pte_is_counted_cb = host_stage2_pte_is_counted;

	ret = __kvm_pgtable_stage2_init(&host_mmu.pgt, mmu,
					&host_mmu.mm_ops, KVM_HOST_S2_FLAGS,
					&host_s2_pte_ops);
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

static bool guest_stage2_pte_is_counted(kvm_pte_t pte, u32 level)
{
	/*
	 * The refcount tracks valid entries as well as invalid entries if they
	 * encode ownership of a page to another entity than the page-table
	 * owner, whose id is 0.
	 */
	return !!pte;
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

	nr_pages = kvm_pgtable_stage2_pgd_size(vm->kvm.arch.vtcr) >> PAGE_SHIFT;
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
					&guest_s2_pte_ops);
	guest_unlock_component(vm);
	if (ret)
		return ret;

	vm->kvm.arch.mmu.pgd_phys = __hyp_pa(vm->pgt.pgd);

	return 0;
}

struct relinquish_data {
	enum pkvm_page_state expected_state;
	u64 pa;
};

static int relinquish_walker(u64 addr, u64 end, u32 level, kvm_pte_t *ptep,
			     enum kvm_pgtable_walk_flags flag, void * const arg)
{
	kvm_pte_t pte = *ptep;
	struct relinquish_data *data = arg;
	enum pkvm_page_state state;
	phys_addr_t phys;

	if (!kvm_pte_valid(pte))
		return 0;

	state = pkvm_getstate(kvm_pgtable_stage2_pte_prot(pte));
	if (state != data->expected_state)
		return -EPERM;

	phys = kvm_pte_to_phys(pte);
	if (state == PKVM_PAGE_OWNED) {
		hyp_poison_page(phys);
		psci_mem_protect_dec(1);
	}

	data->pa = phys;

	return 0;
}

int __pkvm_guest_relinquish_to_host(struct pkvm_hyp_vcpu *vcpu,
				    u64 ipa, u64 *ppa)
{
	struct relinquish_data data;
	struct kvm_pgtable_walker walker = {
		.cb     = relinquish_walker,
		.flags  = KVM_PGTABLE_WALK_LEAF,
		.arg    = &data,
	};
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	int ret;

	host_lock_component();
	guest_lock_component(vm);

	/* Expected page state depends on VM type. */
	data.expected_state = pkvm_hyp_vcpu_is_protected(vcpu) ?
		PKVM_PAGE_OWNED :
		PKVM_PAGE_SHARED_BORROWED;

	/* Set default pa value to "not found". */
	data.pa = 0;

	/* If ipa is mapped: poisons the page, and gets the pa. */
	ret = kvm_pgtable_walk(&vm->pgt, ipa, PAGE_SIZE, &walker);

	/* Zap the guest stage2 pte and return ownership to the host */
	if (!ret && data.pa) {
		WARN_ON(host_stage2_set_owner_locked(data.pa, PAGE_SIZE, PKVM_ID_HOST));
		WARN_ON(kvm_pgtable_stage2_unmap(&vm->pgt, ipa, PAGE_SIZE));
	}

	guest_unlock_component(vm);
	host_unlock_component();

	*ppa = data.pa;
	return ret;
}

int __pkvm_prot_finalize(void)
{
	struct kvm_s2_mmu *mmu = &host_mmu.arch.mmu;
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);

	if (params->hcr_el2 & HCR_VM)
		return -EPERM;

	params->vttbr = kvm_get_vttbr(mmu);
	params->vtcr = host_mmu.arch.vtcr;
	params->hcr_el2 |= HCR_VM;
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

	__pkvm_close_module_registration();

	return 0;
}

int host_stage2_unmap_reg_locked(phys_addr_t start, u64 size)
{
	int ret;

	hyp_assert_lock_held(&host_mmu.lock);

	ret = kvm_pgtable_stage2_unmap(&host_mmu.pgt, start, size);
	if (ret)
		return ret;

	pkvm_iommu_host_stage2_idmap(start, start + size, 0);
	return 0;
}

static int host_stage2_unmap_unmoveable_regs(void)
{
	struct kvm_pgtable *pgt = &host_mmu.pgt;
	struct pkvm_moveable_reg *reg;
	u64 addr = 0;
	int i, ret;

	/* Unmap all unmoveable regions to recycle the pages */
	for (i = 0; i < pkvm_moveable_regs_nr; i++) {
		reg = &pkvm_moveable_regs[i];
		if (reg->start > addr) {
			ret = host_stage2_unmap_reg_locked(addr, reg->start - addr);
			if (ret)
				return ret;
		}
		addr = max(addr, reg->start + reg->size);
	}
	return host_stage2_unmap_reg_locked(addr, BIT(pgt->ia_bits) - addr);
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

static enum kvm_pgtable_prot default_host_prot(bool is_memory)
{
	return is_memory ? PKVM_HOST_MEM_PROT : PKVM_HOST_MMIO_PROT;
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
				      enum kvm_pgtable_prot prot,
				      bool update_iommu)
{
	int ret;

	ret = kvm_pgtable_stage2_map(&host_mmu.pgt, start, end - start, start,
				     prot, &host_s2_pool);
	if (ret)
		return ret;

	if (update_iommu)
		pkvm_iommu_host_stage2_idmap(start, end, prot);
	return 0;
}

/*
 * The pool has been provided with enough pages to cover all of moveable regions
 * with page granularity, but it is difficult to know how much of the
 * non-moveable regions we will need to cover upfront, so we may need to
 * 'recycle' the pages if we run out.
 */
#define host_stage2_try(fn, ...)					\
	({								\
		int __ret;						\
		hyp_assert_lock_held(&host_mmu.lock);			\
		__ret = fn(__VA_ARGS__);				\
		if (__ret == -ENOMEM) {					\
			__ret = host_stage2_unmap_unmoveable_regs();		\
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

static int host_stage2_adjust_range(u64 addr, struct kvm_mem_range *range,
				    u32 level)
{
	struct kvm_mem_range cur;

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
			     enum kvm_pgtable_prot prot, bool update_iommu)
{
	return host_stage2_try(__host_stage2_idmap, addr, addr + size, prot, update_iommu);
}

#define KVM_INVALID_PTE_OWNER_MASK	GENMASK(9, 2)
static kvm_pte_t kvm_init_invalid_leaf_owner(enum pkvm_component_id owner_id)
{
	return FIELD_PREP(KVM_INVALID_PTE_OWNER_MASK, owner_id);
}

int host_stage2_set_owner_locked(phys_addr_t addr, u64 size, enum pkvm_component_id owner_id)
{
	kvm_pte_t annotation;
	enum kvm_pgtable_prot prot;
	int ret;

	if (owner_id > PKVM_ID_MAX)
		return -EINVAL;

	annotation = kvm_init_invalid_leaf_owner(owner_id);

	ret = host_stage2_try(kvm_pgtable_stage2_annotate, &host_mmu.pgt,
			      addr, size, &host_s2_pool, annotation);
	if (ret)
		return ret;

	prot = owner_id == PKVM_ID_HOST ? PKVM_HOST_MEM_PROT : 0;
	pkvm_iommu_host_stage2_idmap(addr, addr + size, prot);
	return 0;
}

static bool host_stage2_force_pte(u64 addr, u64 end, enum kvm_pgtable_prot prot)
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
	return prot != default_host_prot(range_is_memory(addr, end));
}

static bool host_stage2_pte_is_counted(kvm_pte_t pte, u32 level)
{
	u64 phys;

	if (!kvm_pte_valid(pte))
		return !!pte;

	if (kvm_pte_table(pte, level))
		return true;

	phys = kvm_pte_to_phys(pte);
	if (addr_is_memory(phys))
		return (pte & KVM_HOST_S2_DEFAULT_MASK) !=
			KVM_HOST_S2_DEFAULT_MEM_PTE;

	return (pte & KVM_HOST_S2_DEFAULT_MASK) != KVM_HOST_S2_DEFAULT_MMIO_PTE;
}

#define DEFERRED_MEMATTR_NOTE	(1ULL << 24)
#ifdef CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC
static enum pkvm_page_state host_get_page_state(kvm_pte_t pte, u64 addr);

int __pkvm_host_set_stage2_memattr(phys_addr_t phys, bool force_nc)
{
	kvm_pte_t pte;
	int ret = 0;

	if (!static_branch_unlikely(&pkvm_force_nc))
		return -ENOENT;

	phys = ALIGN_DOWN(phys, PAGE_SIZE);
	hyp_spin_lock(&host_mmu.lock);

	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, phys, &pte, NULL);
	if (ret)
		goto unlock;

	if (!addr_is_memory(phys)) {
		ret = -EIO;
		goto unlock;
	}

	if (!kvm_pte_valid(pte) && pte) {
		switch (pte) {
		case DEFERRED_MEMATTR_NOTE:
			break;
		default:
			ret = -EPERM;
		}
	} else if (host_get_page_state(pte, phys) != PKVM_PAGE_OWNED) {
		ret = -EPERM;
	}

	if (ret)
		goto unlock;

	if (force_nc) {
		ret = host_stage2_idmap_locked(phys, PAGE_SIZE,
					       PKVM_HOST_MEM_PROT |
					       KVM_PGTABLE_PROT_NC,
					       false);
		if (ret)
			goto unlock;

		kvm_flush_dcache_to_poc(hyp_fixmap_map_nc(phys), PAGE_SIZE);
		hyp_fixmap_unmap();
	} else {
		ret = kvm_pgtable_stage2_annotate(&host_mmu.pgt, phys,
						  PAGE_SIZE, &host_s2_pool,
						  DEFERRED_MEMATTR_NOTE);
	}
unlock:
	hyp_spin_unlock(&host_mmu.lock);
	return ret;
}

static int handle_memattr_annotation(struct kvm_vcpu_fault_info *fault,
				     u64 addr, enum kvm_pgtable_prot *prot,
				     struct kvm_mem_range *range)
{
	u64 par, oldpar;

	/* If the S1 MMU is disabled, treat the access as cacheable */
	if (unlikely(!(read_sysreg(sctlr_el1) & SCTLR_ELx_M)))
		return 0;

	/* If we took a fault on a PTW, then treat it as cacheable */
	if (fault->esr_el2 & ESR_ELx_S1PTW)
		return 0;

	oldpar = read_sysreg_par();

	if (!__kvm_at("s1e1r", fault->far_el2))
		par = read_sysreg_par();
	else
		par = SYS_PAR_EL1_F;

	write_sysreg(oldpar, par_el1);

	if (unlikely(par & SYS_PAR_EL1_F))
		return -EAGAIN;

	if ((par >> 56) == MAIR_ATTR_NORMAL_NC) {
		range->start	= ALIGN_DOWN(addr, PAGE_SIZE);
		range->end	= range->start + PAGE_SIZE;
		*prot		|= KVM_PGTABLE_PROT_NC;
	}

	return 0;
}
#else
static int handle_memattr_annotation(struct kvm_vcpu_fault_info *fault,
				     u64 addr, enum kvm_pgtable_prot *prot,
				     struct kvm_mem_range *range)
{
	return -EPERM;
}
#endif

static int host_stage2_idmap(struct kvm_vcpu_fault_info *fault, u64 addr)
{
	struct kvm_mem_range range;
	bool is_memory = !!find_mem_range(addr, &range);
	enum kvm_pgtable_prot prot = default_host_prot(is_memory);
	kvm_pte_t pte;
	u32 level;
	int ret;

	hyp_assert_lock_held(&host_mmu.lock);

	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, addr, &pte, &level);
	if (ret)
		return ret;

	if (kvm_pte_valid(pte))
		return -EAGAIN;

	if (pte) {
		if (!is_memory)
			return -EPERM;

		switch (pte) {
		case DEFERRED_MEMATTR_NOTE:
			ret = handle_memattr_annotation(fault, addr, &prot,
							&range);
			if (ret)
				return ret;
			break;
		default:
			return -EPERM;
		}
	}

	/*
	 * Adjust against IOMMU devices first. host_stage2_adjust_range() should
	 * be called last for proper alignment.
	 */
	if (!is_memory) {
		ret = pkvm_iommu_host_stage2_adjust_range(addr, &range.start,
							  &range.end);
		if (ret)
			return ret;
	}

	ret = host_stage2_adjust_range(addr, &range, level);
	if (ret)
		return ret;

	return host_stage2_idmap_locked(range.start, range.end - range.start, prot, false);
}

static void (*illegal_abt_notifier)(struct kvm_cpu_context *host_ctxt);

int __pkvm_register_illegal_abt_notifier(void (*cb)(struct kvm_cpu_context *))
{
	return cmpxchg(&illegal_abt_notifier, NULL, cb) ? -EBUSY : 0;
}

static void host_inject_abort(struct kvm_cpu_context *host_ctxt)
{
	u64 spsr = read_sysreg_el2(SYS_SPSR);
	u64 esr = read_sysreg_el2(SYS_ESR);
	u64 ventry, ec;

	if (READ_ONCE(illegal_abt_notifier))
		illegal_abt_notifier(host_ctxt);

	/* Repaint the ESR to report a same-level fault if taken from EL1 */
	if ((spsr & PSR_MODE_MASK) != PSR_MODE_EL0t) {
		ec = ESR_ELx_EC(esr);
		if (ec == ESR_ELx_EC_DABT_LOW)
			ec = ESR_ELx_EC_DABT_CUR;
		else if (ec == ESR_ELx_EC_IABT_LOW)
			ec = ESR_ELx_EC_IABT_CUR;
		else
			WARN_ON(1);
		esr &= ~ESR_ELx_EC_MASK;
		esr |= ec << ESR_ELx_EC_SHIFT;
	}

	/*
	 * Since S1PTW should only ever be set for stage-2 faults, we're pretty
	 * much guaranteed that it won't be set in ESR_EL1 by the hardware. So,
	 * let's use that bit to allow the host abort handler to differentiate
	 * this abort from normal userspace faults.
	 *
	 * Note: although S1PTW is RES0 at EL1, it is guaranteed by the
	 * architecture to be backed by flops, so it should be safe to use.
	 */
	esr |= ESR_ELx_S1PTW;

	write_sysreg_el1(esr, SYS_ESR);
	write_sysreg_el1(spsr, SYS_SPSR);
	write_sysreg_el1(read_sysreg_el2(SYS_ELR), SYS_ELR);
	write_sysreg_el1(read_sysreg_el2(SYS_FAR), SYS_FAR);

	ventry = read_sysreg_el1(SYS_VBAR);
	ventry += get_except64_offset(spsr, PSR_MODE_EL1h, except_type_sync);
	write_sysreg_el2(ventry, SYS_ELR);

	spsr = get_except64_cpsr(spsr, system_supports_mte(),
				 read_sysreg_el1(SYS_SCTLR), PSR_MODE_EL1h);
	write_sysreg_el2(spsr, SYS_SPSR);
}

static bool is_dabt(u64 esr)
{
	return ESR_ELx_EC(esr) == ESR_ELx_EC_DABT_LOW;
}

static int (*perm_fault_handler)(struct kvm_cpu_context *host_ctxt, u64 esr, u64 addr);

int hyp_register_host_perm_fault_handler(int (*cb)(struct kvm_cpu_context *ctxt, u64 esr, u64 addr))
{
	return cmpxchg(&perm_fault_handler, NULL, cb) ? -EBUSY : 0;
}

static int handle_host_perm_fault(struct kvm_cpu_context *host_ctxt, u64 esr, u64 addr)
{
	int (*cb)(struct kvm_cpu_context *host_ctxt, u64 esr, u64 addr);

	cb = READ_ONCE(perm_fault_handler);
	return cb ? cb(host_ctxt, esr, addr) : -EPERM;
}

void handle_host_mem_abort(struct kvm_cpu_context *host_ctxt)
{
	struct kvm_vcpu_fault_info fault;
	u64 esr, addr;
	int ret = -EPERM;

	esr = read_sysreg_el2(SYS_ESR);
	BUG_ON(!__get_fault_info(esr, &fault));
	fault.esr_el2 = esr;

	addr = (fault.hpfar_el2 & HPFAR_MASK) << 8;
	addr |= fault.far_el2 & FAR_MASK;

	host_lock_component();

	/* Check if an IOMMU device can handle the DABT. */
	if (is_dabt(esr) && !addr_is_memory(addr) &&
	    pkvm_iommu_host_dabt_handler(host_ctxt, esr, addr))
		ret = 0;

	/* If not handled, attempt to map the page. */
	if (ret == -EPERM)
		ret = host_stage2_idmap(&fault, addr);

	host_unlock_component();

	if ((esr & ESR_ELx_FSC_TYPE) == FSC_PERM)
		ret = handle_host_perm_fault(host_ctxt, esr, addr);

	if (ret == -EPERM)
		host_inject_abort(host_ctxt);
	else
		BUG_ON(ret && ret != -EAGAIN);

	trace_host_mem_abort(esr, addr);
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
			struct {
				struct pkvm_hyp_vcpu *hyp_vcpu;
			} guest;
		};
	} initiator;

	struct {
		enum pkvm_component_id	id;

		union {
			struct {
				struct pkvm_hyp_vcpu *hyp_vcpu;
				phys_addr_t phys;
			} guest;
		};
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

static int __check_page_state_visitor(u64 addr, u64 end, u32 level,
				      kvm_pte_t *ptep,
				      enum kvm_pgtable_walk_flags flag,
				      void * const arg)
{
	struct check_walk_data *d = arg;
	kvm_pte_t pte = *ptep;

	return d->get_page_state(pte, addr) == d->desired ? 0 : -EPERM;
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
	bool is_memory = addr_is_memory(addr);
	enum pkvm_page_state state = 0;
	enum kvm_pgtable_prot prot;

	if (is_memory && hyp_phys_to_page(addr)->flags & MODULE_OWNED_PAGE)
	       return PKVM_MODULE_DONT_TOUCH;

	if (!addr_is_allowed_memory(addr))
		return PKVM_NOPAGE;

	if (!kvm_pte_valid(pte) && pte)
		return PKVM_NOPAGE;

	prot = kvm_pgtable_stage2_pte_prot(pte);
	if (kvm_pte_valid(pte)) {
		if ((prot & KVM_PGTABLE_PROT_RWX) != default_host_prot(is_memory))
			state = PKVM_PAGE_RESTRICTED_PROT;
	}

	return state | pkvm_getstate(prot);
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

	return host_stage2_idmap_locked(addr, size, prot, true);
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
	enum pkvm_component_id owner_id = tx->completer.id;
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

static int host_ack_share(u64 addr, const struct pkvm_mem_transition *tx,
			  enum kvm_pgtable_prot perms)
{
	if (perms != PKVM_HOST_MEM_PROT)
		return -EPERM;

	return __host_ack_transition(addr, tx, PKVM_NOPAGE);
}

static int host_ack_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	return __host_ack_transition(addr, tx, PKVM_NOPAGE);
}

static int host_ack_unshare(u64 addr, const struct pkvm_mem_transition *tx)
{
	return __host_ack_transition(addr, tx, PKVM_PAGE_SHARED_BORROWED);
}

static int host_complete_share(u64 addr, const struct pkvm_mem_transition *tx,
			       enum kvm_pgtable_prot perms)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	int err;

	err = __host_set_page_state_range(addr, size, PKVM_PAGE_SHARED_BORROWED);
	if (err)
		return err;

	if (tx->initiator.id == PKVM_ID_GUEST)
		psci_mem_protect_dec(tx->nr_pages);

	return 0;
}

static int host_complete_unshare(u64 addr, const struct pkvm_mem_transition *tx)
{
	enum pkvm_component_id owner_id = tx->initiator.id;
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (tx->initiator.id == PKVM_ID_GUEST)
		psci_mem_protect_inc(tx->nr_pages);

	return host_stage2_set_owner_locked(addr, size, owner_id);
}

static int host_complete_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;
	enum pkvm_component_id host_id = tx->completer.id;

	return host_stage2_set_owner_locked(addr, size, host_id);
}

static enum pkvm_page_state hyp_get_page_state(kvm_pte_t pte, u64 addr)
{
	enum pkvm_page_state state = 0;
	enum kvm_pgtable_prot prot;

	if (!kvm_pte_valid(pte))
		return PKVM_NOPAGE;

	prot = kvm_pgtable_hyp_pte_prot(pte);
	if (kvm_pte_valid(pte) && ((prot & KVM_PGTABLE_PROT_RWX) != PAGE_HYP))
		state = PKVM_PAGE_RESTRICTED_PROT;

	return state | pkvm_getstate(prot);
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

static enum pkvm_page_state guest_get_page_state(kvm_pte_t pte, u64 addr)
{
	enum pkvm_page_state state = 0;
	enum kvm_pgtable_prot prot;

	if (!kvm_pte_valid(pte))
		return PKVM_NOPAGE;

	prot = kvm_pgtable_stage2_pte_prot(pte);
	if (kvm_pte_valid(pte) && ((prot & KVM_PGTABLE_PROT_RWX) != KVM_PGTABLE_PROT_RWX))
		state = PKVM_PAGE_RESTRICTED_PROT;

	return state | pkvm_getstate(prot);
}

static int __guest_check_page_state_range(struct pkvm_hyp_vcpu *vcpu, u64 addr,
					  u64 size, enum pkvm_page_state state)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	struct check_walk_data d = {
		.desired	= state,
		.get_page_state	= guest_get_page_state,
	};

	hyp_assert_lock_held(&vm->lock);
	return check_page_state_range(&vm->pgt, addr, size, &d);
}

static int guest_ack_share(u64 addr, const struct pkvm_mem_transition *tx,
			   enum kvm_pgtable_prot perms)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	if (perms != KVM_PGTABLE_PROT_RWX)
		return -EPERM;

	return __guest_check_page_state_range(tx->completer.guest.hyp_vcpu,
					      addr, size, PKVM_NOPAGE);
}

static int guest_ack_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	u64 size = tx->nr_pages * PAGE_SIZE;

	return __guest_check_page_state_range(tx->completer.guest.hyp_vcpu,
					      addr, size, PKVM_NOPAGE);
}

static int guest_complete_share(u64 addr, const struct pkvm_mem_transition *tx,
				enum kvm_pgtable_prot perms)
{
	struct pkvm_hyp_vcpu *vcpu = tx->completer.guest.hyp_vcpu;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	u64 size = tx->nr_pages * PAGE_SIZE;
	enum kvm_pgtable_prot prot;

	prot = pkvm_mkstate(perms, PKVM_PAGE_SHARED_BORROWED);
	return kvm_pgtable_stage2_map(&vm->pgt, addr, size, tx->completer.guest.phys,
				      prot, &vcpu->vcpu.arch.pkvm_memcache);
}

static int guest_complete_donation(u64 addr, const struct pkvm_mem_transition *tx)
{
	enum kvm_pgtable_prot prot = pkvm_mkstate(KVM_PGTABLE_PROT_RWX, PKVM_PAGE_OWNED);
	struct pkvm_hyp_vcpu *vcpu = tx->completer.guest.hyp_vcpu;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	phys_addr_t phys = tx->completer.guest.phys;
	u64 size = tx->nr_pages * PAGE_SIZE;
	int err;

	if (tx->initiator.id == PKVM_ID_HOST)
		psci_mem_protect_inc(tx->nr_pages);

	if (pkvm_ipa_range_has_pvmfw(vm, addr, addr + size)) {
		if (WARN_ON(!pkvm_hyp_vcpu_is_protected(vcpu))) {
			err = -EPERM;
			goto err_undo_psci;
		}

		WARN_ON(tx->initiator.id != PKVM_ID_HOST);
		err = pkvm_load_pvmfw_pages(vm, addr, phys, size);
		if (err)
			goto err_undo_psci;
	}

	/*
	 * If this fails, we effectively leak the pages since they're now
	 * owned by the guest but not mapped into its stage-2 page-table.
	 */
	return kvm_pgtable_stage2_map(&vm->pgt, addr, size, phys, prot,
				      &vcpu->vcpu.arch.pkvm_memcache);

err_undo_psci:
	if (tx->initiator.id == PKVM_ID_HOST)
		psci_mem_protect_dec(tx->nr_pages);
	return err;
}

static int __guest_get_completer_addr(u64 *completer_addr, phys_addr_t phys,
				      const struct pkvm_mem_transition *tx)
{
	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		*completer_addr = phys;
		break;
	case PKVM_ID_HYP:
		*completer_addr = (u64)__hyp_va(phys);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int __guest_request_page_transition(u64 *completer_addr,
					   const struct pkvm_mem_transition *tx,
					   enum pkvm_page_state desired)
{
	struct pkvm_hyp_vcpu *vcpu = tx->initiator.guest.hyp_vcpu;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	enum pkvm_page_state state;
	phys_addr_t phys;
	kvm_pte_t pte;
	u32 level;
	int ret;

	if (tx->nr_pages != 1)
		return -E2BIG;

	ret = kvm_pgtable_get_leaf(&vm->pgt, tx->initiator.addr, &pte, &level);
	if (ret)
		return ret;

	state = guest_get_page_state(pte, tx->initiator.addr);
	if (state == PKVM_NOPAGE)
		return -EFAULT;

	if (state != desired)
		return -EPERM;

	/*
	 * We only deal with page granular mappings in the guest for now as
	 * the pgtable code relies on being able to recreate page mappings
	 * lazily after zapping a block mapping, which doesn't work once the
	 * pages have been donated.
	 */
	if (level != KVM_PGTABLE_MAX_LEVELS - 1)
		return -EINVAL;

	phys = kvm_pte_to_phys(pte);
	if (!addr_is_allowed_memory(phys))
		return -EINVAL;

	return __guest_get_completer_addr(completer_addr, phys, tx);
}

static int guest_request_share(u64 *completer_addr,
			       const struct pkvm_mem_transition *tx)
{
	return __guest_request_page_transition(completer_addr, tx,
					       PKVM_PAGE_OWNED);
}

static int guest_request_unshare(u64 *completer_addr,
				 const struct pkvm_mem_transition *tx)
{
	return __guest_request_page_transition(completer_addr, tx,
					       PKVM_PAGE_SHARED_OWNED);
}

static int __guest_initiate_page_transition(u64 *completer_addr,
					    const struct pkvm_mem_transition *tx,
					    enum pkvm_page_state state)
{
	struct pkvm_hyp_vcpu *vcpu = tx->initiator.guest.hyp_vcpu;
	struct kvm_hyp_memcache *mc = &vcpu->vcpu.arch.pkvm_memcache;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	u64 size = tx->nr_pages * PAGE_SIZE;
	u64 addr = tx->initiator.addr;
	enum kvm_pgtable_prot prot;
	phys_addr_t phys;
	kvm_pte_t pte;
	int ret;

	ret = kvm_pgtable_get_leaf(&vm->pgt, addr, &pte, NULL);
	if (ret)
		return ret;

	phys = kvm_pte_to_phys(pte);
	prot = pkvm_mkstate(kvm_pgtable_stage2_pte_prot(pte), state);
	ret = kvm_pgtable_stage2_map(&vm->pgt, addr, size, phys, prot, mc);
	if (ret)
		return ret;

	return __guest_get_completer_addr(completer_addr, phys, tx);
}

static int guest_initiate_share(u64 *completer_addr,
				const struct pkvm_mem_transition *tx)
{
	return __guest_initiate_page_transition(completer_addr, tx,
						PKVM_PAGE_SHARED_OWNED);
}

static int guest_initiate_unshare(u64 *completer_addr,
				  const struct pkvm_mem_transition *tx)
{
	return __guest_initiate_page_transition(completer_addr, tx,
						PKVM_PAGE_OWNED);
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
	case PKVM_ID_GUEST:
		ret = guest_request_share(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_ack_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_HYP:
		ret = hyp_ack_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_GUEST:
		ret = guest_ack_share(completer_addr, tx, share->completer_prot);
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
	case PKVM_ID_GUEST:
		ret = guest_initiate_share(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_complete_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_HYP:
		ret = hyp_complete_share(completer_addr, tx, share->completer_prot);
		break;
	case PKVM_ID_GUEST:
		ret = guest_complete_share(completer_addr, tx, share->completer_prot);
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
	case PKVM_ID_GUEST:
		ret = guest_request_unshare(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_ack_unshare(completer_addr, tx);
		break;
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
	case PKVM_ID_GUEST:
		ret = guest_initiate_unshare(&completer_addr, tx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	switch (tx->completer.id) {
	case PKVM_ID_HOST:
		ret = host_complete_unshare(completer_addr, tx);
		break;
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
	case PKVM_ID_GUEST:
		ret = guest_ack_donation(completer_addr, tx);
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
	case PKVM_ID_GUEST:
		ret = guest_complete_donation(completer_addr, tx);
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

int __pkvm_guest_share_host(struct pkvm_hyp_vcpu *vcpu, u64 ipa)
{
	int ret;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_GUEST,
				.addr	= ipa,
				.guest	= {
					.hyp_vcpu = vcpu,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HOST,
			},
		},
		.completer_prot	= PKVM_HOST_MEM_PROT,
	};

	host_lock_component();
	guest_lock_component(vm);

	ret = do_share(&share);

	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
}

int __pkvm_guest_unshare_host(struct pkvm_hyp_vcpu *vcpu, u64 ipa)
{
	int ret;
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_GUEST,
				.addr	= ipa,
				.guest	= {
					.hyp_vcpu = vcpu,
				},
			},
			.completer	= {
				.id	= PKVM_ID_HOST,
			},
		},
		.completer_prot	= PKVM_HOST_MEM_PROT,
	};

	host_lock_component();
	guest_lock_component(vm);

	ret = do_unshare(&share);

	guest_unlock_component(vm);
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

static int restrict_host_page_perms(u64 addr, kvm_pte_t pte, u32 level, enum kvm_pgtable_prot prot)
{
	int ret = 0;

	/* XXX: optimize ... */
	if (kvm_pte_valid(pte) && (level == KVM_PGTABLE_MAX_LEVELS - 1))
		ret = kvm_pgtable_stage2_unmap(&host_mmu.pgt, addr, PAGE_SIZE);
	if (!ret)
		ret = host_stage2_idmap_locked(addr, PAGE_SIZE, prot, false);

	return ret;
}

int module_change_host_page_prot(u64 pfn, enum kvm_pgtable_prot prot)
{
	u64 addr = hyp_pfn_to_phys(pfn);
	struct hyp_page *page;
	kvm_pte_t pte;
	u32 level;
	int ret;

	if ((prot & KVM_PGTABLE_PROT_RWX) != prot || !addr_is_memory(addr))
		return -EINVAL;

	host_lock_component();
	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, addr, &pte, &level);
	if (ret)
		goto unlock;

	ret = -EPERM;
	page = hyp_phys_to_page(addr);

	/*
	 * Modules can only relax permissions of pages they own, and restrict
	 * permissions of pristine pages.
	 */
	if (prot == KVM_PGTABLE_PROT_RWX) {
		if (!(page->flags & MODULE_OWNED_PAGE))
			goto unlock;
	} else if (host_get_page_state(pte, addr) != PKVM_PAGE_OWNED) {
		goto unlock;
	}

	if (prot == KVM_PGTABLE_PROT_RWX)
		ret = host_stage2_set_owner_locked(addr, PAGE_SIZE, PKVM_ID_HOST);
	else if (!prot)
		ret = host_stage2_set_owner_locked(addr, PAGE_SIZE, PKVM_ID_PROTECTED);
	else
		ret = restrict_host_page_perms(addr, pte, level, prot);

	if (ret)
		goto unlock;

	if (prot != KVM_PGTABLE_PROT_RWX)
		hyp_phys_to_page(addr)->flags |= MODULE_OWNED_PAGE;
	else
		hyp_phys_to_page(addr)->flags &= ~MODULE_OWNED_PAGE;

unlock:
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

int __pkvm_host_share_guest(u64 pfn, u64 gfn, struct pkvm_hyp_vcpu *vcpu)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 guest_addr = hyp_pfn_to_phys(gfn);
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	struct pkvm_mem_share share = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= host_addr,
				.host	= {
					.completer_addr = guest_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_GUEST,
				.guest	= {
					.hyp_vcpu = vcpu,
					.phys = host_addr,
				},
			},
		},
		.completer_prot	= KVM_PGTABLE_PROT_RWX,
	};

	host_lock_component();
	guest_lock_component(vm);

	ret = do_share(&share);

	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
}

int __pkvm_host_donate_guest(u64 pfn, u64 gfn, struct pkvm_hyp_vcpu *vcpu)
{
	int ret;
	u64 host_addr = hyp_pfn_to_phys(pfn);
	u64 guest_addr = hyp_pfn_to_phys(gfn);
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(vcpu);
	struct pkvm_mem_donation donation = {
		.tx	= {
			.nr_pages	= 1,
			.initiator	= {
				.id	= PKVM_ID_HOST,
				.addr	= host_addr,
				.host	= {
					.completer_addr = guest_addr,
				},
			},
			.completer	= {
				.id	= PKVM_ID_GUEST,
				.guest	= {
					.hyp_vcpu = vcpu,
					.phys = host_addr,
				},
			},
		},
	};

	host_lock_component();
	guest_lock_component(vm);

	ret = do_donate(&donation);

	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
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

void hyp_poison_page(phys_addr_t phys)
{
	void *addr = hyp_fixmap_map(phys);

	memset(addr, 0, PAGE_SIZE);
	/*
	 * Prefer kvm_flush_dcache_to_poc() over __clean_dcache_guest_page()
	 * here as the latter may elide the CMO under the assumption that FWB
	 * will be enabled on CPUs that support it. This is incorrect for the
	 * host stage-2 and would otherwise lead to a malicious host potentially
	 * being able to read the contents of newly reclaimed guest pages.
	 */
	kvm_flush_dcache_to_poc(addr, PAGE_SIZE);
	hyp_fixmap_unmap();
}

void destroy_hyp_vm_pgt(struct pkvm_hyp_vm *vm)
{
	guest_lock_component(vm);
	kvm_pgtable_stage2_destroy(&vm->pgt);
	guest_unlock_component(vm);
}

void drain_hyp_pool(struct pkvm_hyp_vm *vm, struct kvm_hyp_memcache *mc)
{
	void *addr = hyp_alloc_pages(&vm->pool, 0);

	while (addr) {
		memset(hyp_virt_to_page(addr), 0, sizeof(struct hyp_page));
		push_hyp_memcache(mc, addr, hyp_virt_to_phys);
		WARN_ON(__pkvm_hyp_donate_host(hyp_virt_to_pfn(addr), 1));
		addr = hyp_alloc_pages(&vm->pool, 0);
	}
}

int __pkvm_host_reclaim_page(struct pkvm_hyp_vm *vm, u64 pfn, u64 ipa)
{
	phys_addr_t phys = hyp_pfn_to_phys(pfn);
	kvm_pte_t pte;
	int ret;

	host_lock_component();
	guest_lock_component(vm);

	ret = kvm_pgtable_get_leaf(&vm->pgt, ipa, &pte, NULL);
	if (ret)
		goto unlock;

	if (!kvm_pte_valid(pte)) {
		ret = -EINVAL;
		goto unlock;
	} else if (phys != kvm_pte_to_phys(pte)) {
		ret = -EPERM;
		goto unlock;
	}

	/* We could avoid TLB inval, it is done per VMID on the finalize path */
	WARN_ON(kvm_pgtable_stage2_unmap(&vm->pgt, ipa, PAGE_SIZE));

	switch(guest_get_page_state(pte, ipa)) {
	case PKVM_PAGE_OWNED:
		WARN_ON(__host_check_page_state_range(phys, PAGE_SIZE, PKVM_NOPAGE));
		hyp_poison_page(phys);
		psci_mem_protect_dec(1);
		break;
	case PKVM_PAGE_SHARED_BORROWED:
		WARN_ON(__host_check_page_state_range(phys, PAGE_SIZE, PKVM_PAGE_SHARED_OWNED));
		break;
	case PKVM_PAGE_SHARED_OWNED:
		WARN_ON(__host_check_page_state_range(phys, PAGE_SIZE, PKVM_PAGE_SHARED_BORROWED));
		break;
	default:
		BUG_ON(1);
	}

	WARN_ON(host_stage2_set_owner_locked(phys, PAGE_SIZE, PKVM_ID_HOST));

unlock:
	guest_unlock_component(vm);
	host_unlock_component();

	return ret;
}

/* Replace this with something more structured once day */
#define MMIO_NOTE	(('M' << 24 | 'M' << 16 | 'I' << 8 | 'O') << 1)

static bool __check_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	kvm_pte_t pte;
	u32 level;
	int ret;

	ret = kvm_pgtable_get_leaf(&vm->pgt, ipa, &pte, &level);
	if (ret)
		return false;

	/* Must be a PAGE_SIZE mapping with our annotation */
	return (BIT(ARM64_HW_PGTABLE_LEVEL_SHIFT(level)) == PAGE_SIZE &&
		pte == MMIO_NOTE);
}

int __pkvm_install_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	kvm_pte_t pte;
	u32 level;
	int ret;

	if (!test_bit(KVM_ARCH_FLAG_MMIO_GUARD, &vm->kvm.arch.flags))
		return -EINVAL;

	if (ipa & ~PAGE_MASK)
		return -EINVAL;

	guest_lock_component(vm);

	ret = kvm_pgtable_get_leaf(&vm->pgt, ipa, &pte, &level);
	if (ret)
		goto unlock;

	if (pte && BIT(ARM64_HW_PGTABLE_LEVEL_SHIFT(level)) == PAGE_SIZE) {
		/*
		 * Already flagged as MMIO, let's accept it, and fail
		 * otherwise
		 */
		if (pte != MMIO_NOTE)
			ret = -EBUSY;

		goto unlock;
	}

	ret = kvm_pgtable_stage2_annotate(&vm->pgt, ipa, PAGE_SIZE,
					  &hyp_vcpu->vcpu.arch.pkvm_memcache,
					  MMIO_NOTE);

unlock:
	guest_unlock_component(vm);
	return ret;
}

int __pkvm_remove_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu, u64 ipa)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);

	if (!test_bit(KVM_ARCH_FLAG_MMIO_GUARD, &vm->kvm.arch.flags))
		return -EINVAL;

	guest_lock_component(vm);

	if (__check_ioguard_page(hyp_vcpu, ipa))
		WARN_ON(kvm_pgtable_stage2_unmap(&vm->pgt,
				ALIGN_DOWN(ipa, PAGE_SIZE), PAGE_SIZE));

	guest_unlock_component(vm);
	return 0;
}

bool __pkvm_check_ioguard_page(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	struct pkvm_hyp_vm *vm = pkvm_hyp_vcpu_to_hyp_vm(hyp_vcpu);
	u64 ipa, end;
	bool ret;

	if (!kvm_vcpu_dabt_isvalid(&hyp_vcpu->vcpu))
		return false;

	if (!test_bit(KVM_ARCH_FLAG_MMIO_GUARD, &vm->kvm.arch.flags))
		return true;

	ipa  = kvm_vcpu_get_fault_ipa(&hyp_vcpu->vcpu);
	ipa |= kvm_vcpu_get_hfar(&hyp_vcpu->vcpu) & FAR_MASK;
	end = ipa + kvm_vcpu_dabt_get_as(&hyp_vcpu->vcpu) - 1;

	guest_lock_component(vm);
	ret = __check_ioguard_page(hyp_vcpu, ipa);
	if ((end & PAGE_MASK) != (ipa & PAGE_MASK))
		ret &= __check_ioguard_page(hyp_vcpu, end);
	guest_unlock_component(vm);

	return ret;
}

int host_stage2_protect_pages_locked(phys_addr_t addr, u64 size)
{
	int ret;

	hyp_assert_lock_held(&host_mmu.lock);

	ret = __host_check_page_state_range(addr, size, PKVM_PAGE_OWNED);
	if (!ret)
		ret = host_stage2_set_owner_locked(addr, size, PKVM_ID_PROTECTED);

	return ret;
}

int host_stage2_get_leaf(phys_addr_t phys, kvm_pte_t *ptep, u32 *level)
{
	int ret;

	host_lock_component();
	ret = kvm_pgtable_get_leaf(&host_mmu.pgt, phys, ptep, level);
	host_unlock_component();

	return ret;
}
