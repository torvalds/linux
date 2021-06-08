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

#include <hyp/switch.h>

#include <nvhe/gfp.h>
#include <nvhe/memory.h>
#include <nvhe/mem_protect.h>
#include <nvhe/mm.h>

#define KVM_HOST_S2_FLAGS (KVM_PGTABLE_S2_NOFWB | KVM_PGTABLE_S2_IDMAP)

extern unsigned long hyp_nr_cpus;
struct host_kvm host_kvm;

static struct hyp_pool host_s2_pool;

/*
 * Copies of the host's CPU features registers holding sanitized values.
 */
u64 id_aa64mmfr0_el1_sys_val;
u64 id_aa64mmfr1_el1_sys_val;

static const u8 pkvm_hyp_id = 1;

static void *host_s2_zalloc_pages_exact(size_t size)
{
	return hyp_alloc_pages(&host_s2_pool, get_order(size));
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

int kvm_host_prepare_stage2(void *pgt_pool_base)
{
	struct kvm_s2_mmu *mmu = &host_kvm.arch.mmu;
	int ret;

	prepare_host_vtcr();
	hyp_spin_lock_init(&host_kvm.lock);

	ret = prepare_s2_pool(pgt_pool_base);
	if (ret)
		return ret;

	ret = kvm_pgtable_stage2_init_flags(&host_kvm.pgt, &host_kvm.arch,
					    &host_kvm.mm_ops, KVM_HOST_S2_FLAGS);
	if (ret)
		return ret;

	mmu->pgd_phys = __hyp_pa(host_kvm.pgt.pgd);
	mmu->arch = &host_kvm.arch;
	mmu->pgt = &host_kvm.pgt;
	mmu->vmid.vmid_gen = 0;
	mmu->vmid.vmid = 0;

	return 0;
}

int __pkvm_prot_finalize(void)
{
	struct kvm_s2_mmu *mmu = &host_kvm.arch.mmu;
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);

	params->vttbr = kvm_get_vttbr(mmu);
	params->vtcr = host_kvm.arch.vtcr;
	params->hcr_el2 |= HCR_VM;
	kvm_flush_dcache_to_poc(params, sizeof(*params));

	write_sysreg(params->hcr_el2, hcr_el2);
	__load_stage2(&host_kvm.arch.mmu, host_kvm.arch.vtcr);

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

static bool range_is_memory(u64 start, u64 end)
{
	struct kvm_mem_range r1, r2;

	if (!find_mem_range(start, &r1) || !find_mem_range(end, &r2))
		return false;
	if (r1.start != r2.start)
		return false;

	return true;
}

static inline int __host_stage2_idmap(u64 start, u64 end,
				      enum kvm_pgtable_prot prot)
{
	return kvm_pgtable_stage2_map(&host_kvm.pgt, start, end - start, start,
				      prot, &host_s2_pool);
}

static int host_stage2_idmap(u64 addr)
{
	enum kvm_pgtable_prot prot = KVM_PGTABLE_PROT_R | KVM_PGTABLE_PROT_W;
	struct kvm_mem_range range;
	bool is_memory = find_mem_range(addr, &range);
	int ret;

	if (is_memory)
		prot |= KVM_PGTABLE_PROT_X;

	hyp_spin_lock(&host_kvm.lock);
	ret = kvm_pgtable_stage2_find_range(&host_kvm.pgt, addr, prot, &range);
	if (ret)
		goto unlock;

	ret = __host_stage2_idmap(range.start, range.end, prot);
	if (ret != -ENOMEM)
		goto unlock;

	/*
	 * The pool has been provided with enough pages to cover all of memory
	 * with page granularity, but it is difficult to know how much of the
	 * MMIO range we will need to cover upfront, so we may need to 'recycle'
	 * the pages if we run out.
	 */
	ret = host_stage2_unmap_dev_all();
	if (ret)
		goto unlock;

	ret = __host_stage2_idmap(range.start, range.end, prot);

unlock:
	hyp_spin_unlock(&host_kvm.lock);

	return ret;
}

int __pkvm_mark_hyp(phys_addr_t start, phys_addr_t end)
{
	int ret;

	/*
	 * host_stage2_unmap_dev_all() currently relies on MMIO mappings being
	 * non-persistent, so don't allow changing page ownership in MMIO range.
	 */
	if (!range_is_memory(start, end))
		return -EINVAL;

	hyp_spin_lock(&host_kvm.lock);
	ret = kvm_pgtable_stage2_set_owner(&host_kvm.pgt, start, end - start,
					   &host_s2_pool, pkvm_hyp_id);
	hyp_spin_unlock(&host_kvm.lock);

	return ret != -EAGAIN ? ret : 0;
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
