/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS MMU handling in the KVM module.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/highmem.h>
#include <linux/kvm_host.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	BUG_ON(!mc || !mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
}

static u32 kvm_mips_get_kernel_asid(struct kvm_vcpu *vcpu)
{
	struct mm_struct *kern_mm = &vcpu->arch.guest_kernel_mm;
	int cpu = smp_processor_id();

	return cpu_asid(cpu, kern_mm);
}

static u32 kvm_mips_get_user_asid(struct kvm_vcpu *vcpu)
{
	struct mm_struct *user_mm = &vcpu->arch.guest_user_mm;
	int cpu = smp_processor_id();

	return cpu_asid(cpu, user_mm);
}

/**
 * kvm_mips_walk_pgd() - Walk page table with optional allocation.
 * @pgd:	Page directory pointer.
 * @addr:	Address to index page table using.
 * @cache:	MMU page cache to allocate new page tables from, or NULL.
 *
 * Walk the page tables pointed to by @pgd to find the PTE corresponding to the
 * address @addr. If page tables don't exist for @addr, they will be created
 * from the MMU cache if @cache is not NULL.
 *
 * Returns:	Pointer to pte_t corresponding to @addr.
 *		NULL if a page table doesn't exist for @addr and !@cache.
 *		NULL if a page table allocation failed.
 */
static pte_t *kvm_mips_walk_pgd(pgd_t *pgd, struct kvm_mmu_memory_cache *cache,
				unsigned long addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pgd += pgd_index(addr);
	if (pgd_none(*pgd)) {
		/* Not used on MIPS yet */
		BUG();
		return NULL;
	}
	pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		pmd_t *new_pmd;

		if (!cache)
			return NULL;
		new_pmd = mmu_memory_cache_alloc(cache);
		pmd_init((unsigned long)new_pmd,
			 (unsigned long)invalid_pte_table);
		pud_populate(NULL, pud, new_pmd);
	}
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		pte_t *new_pte;

		if (!cache)
			return NULL;
		new_pte = mmu_memory_cache_alloc(cache);
		clear_page(new_pte);
		pmd_populate_kernel(NULL, pmd, new_pte);
	}
	return pte_offset(pmd, addr);
}

static int kvm_mips_map_page(struct kvm *kvm, gfn_t gfn)
{
	int srcu_idx, err = 0;
	kvm_pfn_t pfn;

	if (kvm->arch.guest_pmap[gfn] != KVM_INVALID_PAGE)
		return 0;

	srcu_idx = srcu_read_lock(&kvm->srcu);
	pfn = gfn_to_pfn(kvm, gfn);

	if (is_error_noslot_pfn(pfn)) {
		kvm_err("Couldn't get pfn for gfn %#llx!\n", gfn);
		err = -EFAULT;
		goto out;
	}

	kvm->arch.guest_pmap[gfn] = pfn;
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return err;
}

/* Translate guest KSEG0 addresses to Host PA */
unsigned long kvm_mips_translate_guest_kseg0_to_hpa(struct kvm_vcpu *vcpu,
						    unsigned long gva)
{
	gfn_t gfn;
	unsigned long offset = gva & ~PAGE_MASK;
	struct kvm *kvm = vcpu->kvm;

	if (KVM_GUEST_KSEGX(gva) != KVM_GUEST_KSEG0) {
		kvm_err("%s/%p: Invalid gva: %#lx\n", __func__,
			__builtin_return_address(0), gva);
		return KVM_INVALID_PAGE;
	}

	gfn = (KVM_GUEST_CPHYSADDR(gva) >> PAGE_SHIFT);

	if (gfn >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: %#llx, GVA: %#lx\n", __func__, gfn,
			gva);
		return KVM_INVALID_PAGE;
	}

	if (kvm_mips_map_page(vcpu->kvm, gfn) < 0)
		return KVM_INVALID_ADDR;

	return (kvm->arch.guest_pmap[gfn] << PAGE_SHIFT) + offset;
}

void kvm_trap_emul_invalidate_gva(struct kvm_vcpu *vcpu, unsigned long addr,
				  bool user)
{
	pgd_t *pgdp;
	pte_t *ptep;

	addr &= PAGE_MASK << 1;

	pgdp = vcpu->arch.guest_kernel_mm.pgd;
	ptep = kvm_mips_walk_pgd(pgdp, NULL, addr);
	if (ptep) {
		ptep[0] = pfn_pte(0, __pgprot(0));
		ptep[1] = pfn_pte(0, __pgprot(0));
	}

	if (user) {
		pgdp = vcpu->arch.guest_user_mm.pgd;
		ptep = kvm_mips_walk_pgd(pgdp, NULL, addr);
		if (ptep) {
			ptep[0] = pfn_pte(0, __pgprot(0));
			ptep[1] = pfn_pte(0, __pgprot(0));
		}
	}
}

/*
 * kvm_mips_flush_gva_{pte,pmd,pud,pgd,pt}.
 * Flush a range of guest physical address space from the VM's GPA page tables.
 */

static bool kvm_mips_flush_gva_pte(pte_t *pte, unsigned long start_gva,
				   unsigned long end_gva)
{
	int i_min = __pte_offset(start_gva);
	int i_max = __pte_offset(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PTE - 1);
	int i;

	/*
	 * There's no freeing to do, so there's no point clearing individual
	 * entries unless only part of the last level page table needs flushing.
	 */
	if (safe_to_remove)
		return true;

	for (i = i_min; i <= i_max; ++i) {
		if (!pte_present(pte[i]))
			continue;

		set_pte(pte + i, __pte(0));
	}
	return false;
}

static bool kvm_mips_flush_gva_pmd(pmd_t *pmd, unsigned long start_gva,
				   unsigned long end_gva)
{
	pte_t *pte;
	unsigned long end = ~0ul;
	int i_min = __pmd_offset(start_gva);
	int i_max = __pmd_offset(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PMD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pmd_present(pmd[i]))
			continue;

		pte = pte_offset(pmd + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pte(pte, start_gva, end)) {
			pmd_clear(pmd + i);
			pte_free_kernel(NULL, pte);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gva_pud(pud_t *pud, unsigned long start_gva,
				   unsigned long end_gva)
{
	pmd_t *pmd;
	unsigned long end = ~0ul;
	int i_min = __pud_offset(start_gva);
	int i_max = __pud_offset(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PUD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pud_present(pud[i]))
			continue;

		pmd = pmd_offset(pud + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pmd(pmd, start_gva, end)) {
			pud_clear(pud + i);
			pmd_free(NULL, pmd);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gva_pgd(pgd_t *pgd, unsigned long start_gva,
				   unsigned long end_gva)
{
	pud_t *pud;
	unsigned long end = ~0ul;
	int i_min = pgd_index(start_gva);
	int i_max = pgd_index(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PGD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pgd_present(pgd[i]))
			continue;

		pud = pud_offset(pgd + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pud(pud, start_gva, end)) {
			pgd_clear(pgd + i);
			pud_free(NULL, pud);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

void kvm_mips_flush_gva_pt(pgd_t *pgd, enum kvm_mips_flush flags)
{
	if (flags & KMF_GPA) {
		/* all of guest virtual address space could be affected */
		if (flags & KMF_KERN)
			/* useg, kseg0, seg2/3 */
			kvm_mips_flush_gva_pgd(pgd, 0, 0x7fffffff);
		else
			/* useg */
			kvm_mips_flush_gva_pgd(pgd, 0, 0x3fffffff);
	} else {
		/* useg */
		kvm_mips_flush_gva_pgd(pgd, 0, 0x3fffffff);

		/* kseg2/3 */
		if (flags & KMF_KERN)
			kvm_mips_flush_gva_pgd(pgd, 0x60000000, 0x7fffffff);
	}
}

/* XXXKYMA: Must be called with interrupts disabled */
int kvm_mips_handle_kseg0_tlb_fault(unsigned long badvaddr,
				    struct kvm_vcpu *vcpu)
{
	gfn_t gfn;
	kvm_pfn_t pfn0, pfn1;
	unsigned long vaddr = 0;
	unsigned long entryhi = 0, entrylo0 = 0, entrylo1 = 0;
	struct kvm *kvm = vcpu->kvm;
	const int flush_dcache_mask = 0;
	int ret;

	if (KVM_GUEST_KSEGX(badvaddr) != KVM_GUEST_KSEG0) {
		kvm_err("%s: Invalid BadVaddr: %#lx\n", __func__, badvaddr);
		kvm_mips_dump_host_tlbs();
		return -1;
	}

	gfn = (KVM_GUEST_CPHYSADDR(badvaddr) >> PAGE_SHIFT);
	if ((gfn | 1) >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: %#llx, BadVaddr: %#lx\n", __func__,
			gfn, badvaddr);
		kvm_mips_dump_host_tlbs();
		return -1;
	}
	vaddr = badvaddr & (PAGE_MASK << 1);

	if (kvm_mips_map_page(vcpu->kvm, gfn) < 0)
		return -1;

	if (kvm_mips_map_page(vcpu->kvm, gfn ^ 0x1) < 0)
		return -1;

	pfn0 = kvm->arch.guest_pmap[gfn & ~0x1];
	pfn1 = kvm->arch.guest_pmap[gfn | 0x1];

	entrylo0 = mips3_paddr_to_tlbpfn(pfn0 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		ENTRYLO_D | ENTRYLO_V;
	entrylo1 = mips3_paddr_to_tlbpfn(pfn1 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		ENTRYLO_D | ENTRYLO_V;

	preempt_disable();
	entryhi = (vaddr | kvm_mips_get_kernel_asid(vcpu));
	ret = kvm_mips_host_tlb_write(vcpu, entryhi, entrylo0, entrylo1,
				      flush_dcache_mask);
	preempt_enable();

	return ret;
}

int kvm_mips_handle_mapped_seg_tlb_fault(struct kvm_vcpu *vcpu,
					 struct kvm_mips_tlb *tlb)
{
	unsigned long entryhi = 0, entrylo0 = 0, entrylo1 = 0;
	struct kvm *kvm = vcpu->kvm;
	kvm_pfn_t pfn0, pfn1;
	gfn_t gfn0, gfn1;
	long tlb_lo[2];
	int ret;

	tlb_lo[0] = tlb->tlb_lo[0];
	tlb_lo[1] = tlb->tlb_lo[1];

	/*
	 * The commpage address must not be mapped to anything else if the guest
	 * TLB contains entries nearby, or commpage accesses will break.
	 */
	if (!((tlb->tlb_hi ^ KVM_GUEST_COMMPAGE_ADDR) &
			VPN2_MASK & (PAGE_MASK << 1)))
		tlb_lo[(KVM_GUEST_COMMPAGE_ADDR >> PAGE_SHIFT) & 1] = 0;

	gfn0 = mips3_tlbpfn_to_paddr(tlb_lo[0]) >> PAGE_SHIFT;
	gfn1 = mips3_tlbpfn_to_paddr(tlb_lo[1]) >> PAGE_SHIFT;
	if (gfn0 >= kvm->arch.guest_pmap_npages ||
	    gfn1 >= kvm->arch.guest_pmap_npages) {
		kvm_err("%s: Invalid gfn: [%#llx, %#llx], EHi: %#lx\n",
			__func__, gfn0, gfn1, tlb->tlb_hi);
		kvm_mips_dump_guest_tlbs(vcpu);
		return -1;
	}

	if (kvm_mips_map_page(kvm, gfn0) < 0)
		return -1;

	if (kvm_mips_map_page(kvm, gfn1) < 0)
		return -1;

	pfn0 = kvm->arch.guest_pmap[gfn0];
	pfn1 = kvm->arch.guest_pmap[gfn1];

	/* Get attributes from the Guest TLB */
	entrylo0 = mips3_paddr_to_tlbpfn(pfn0 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		(tlb_lo[0] & ENTRYLO_D) |
		(tlb_lo[0] & ENTRYLO_V);
	entrylo1 = mips3_paddr_to_tlbpfn(pfn1 << PAGE_SHIFT) |
		((_page_cachable_default >> _CACHE_SHIFT) << ENTRYLO_C_SHIFT) |
		(tlb_lo[1] & ENTRYLO_D) |
		(tlb_lo[1] & ENTRYLO_V);

	kvm_debug("@ %#lx tlb_lo0: 0x%08lx tlb_lo1: 0x%08lx\n", vcpu->arch.pc,
		  tlb->tlb_lo[0], tlb->tlb_lo[1]);

	preempt_disable();
	entryhi = (tlb->tlb_hi & VPN2_MASK) | (KVM_GUEST_KERNEL_MODE(vcpu) ?
					       kvm_mips_get_kernel_asid(vcpu) :
					       kvm_mips_get_user_asid(vcpu));
	ret = kvm_mips_host_tlb_write(vcpu, entryhi, entrylo0, entrylo1,
				      tlb->tlb_mask);
	preempt_enable();

	return ret;
}

void kvm_get_new_mmu_context(struct mm_struct *mm, unsigned long cpu,
			     struct kvm_vcpu *vcpu)
{
	unsigned long asid = asid_cache(cpu);

	asid += cpu_asid_inc();
	if (!(asid & cpu_asid_mask(&cpu_data[cpu]))) {
		if (cpu_has_vtag_icache)
			flush_icache_all();

		kvm_local_flush_tlb_all();      /* start new asid cycle */

		if (!asid)      /* fix version if needed */
			asid = asid_first_version(cpu);
	}

	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}

/**
 * kvm_mips_migrate_count() - Migrate timer.
 * @vcpu:	Virtual CPU.
 *
 * Migrate CP0_Count hrtimer to the current CPU by cancelling and restarting it
 * if it was running prior to being cancelled.
 *
 * Must be called when the VCPU is migrated to a different CPU to ensure that
 * timer expiry during guest execution interrupts the guest and causes the
 * interrupt to be delivered in a timely manner.
 */
static void kvm_mips_migrate_count(struct kvm_vcpu *vcpu)
{
	if (hrtimer_cancel(&vcpu->arch.comparecount_timer))
		hrtimer_restart(&vcpu->arch.comparecount_timer);
}

/* Restore ASID once we are scheduled back after preemption */
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	unsigned long flags;

	kvm_debug("%s: vcpu %p, cpu: %d\n", __func__, vcpu, cpu);

	local_irq_save(flags);

	if (vcpu->arch.last_sched_cpu != cpu) {
		kvm_debug("[%d->%d]KVM VCPU[%d] switch\n",
			  vcpu->arch.last_sched_cpu, cpu, vcpu->vcpu_id);
		/*
		 * Migrate the timer interrupt to the current CPU so that it
		 * always interrupts the guest and synchronously triggers a
		 * guest timer interrupt.
		 */
		kvm_mips_migrate_count(vcpu);
	}

	/* restore guest state to registers */
	kvm_mips_callbacks->vcpu_load(vcpu, cpu);

	local_irq_restore(flags);
}

/* ASID can change if another task is scheduled during preemption */
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();
	vcpu->arch.last_sched_cpu = cpu;

	/* save guest state in registers */
	kvm_mips_callbacks->vcpu_put(vcpu, cpu);

	local_irq_restore(flags);
}

u32 kvm_get_inst(u32 *opc, struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long paddr, flags, vpn2, asid;
	unsigned long va = (unsigned long)opc;
	void *vaddr;
	u32 inst;
	int index;

	if (KVM_GUEST_KSEGX(va) < KVM_GUEST_KSEG0 ||
	    KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG23) {
		local_irq_save(flags);
		index = kvm_mips_host_tlb_lookup(vcpu, va);
		if (index >= 0) {
			inst = *(opc);
		} else {
			vpn2 = va & VPN2_MASK;
			asid = kvm_read_c0_guest_entryhi(cop0) &
						KVM_ENTRYHI_ASID;
			index = kvm_mips_guest_tlb_lookup(vcpu, vpn2 | asid);
			if (index < 0) {
				kvm_err("%s: get_user_failed for %p, vcpu: %p, ASID: %#lx\n",
					__func__, opc, vcpu, read_c0_entryhi());
				kvm_mips_dump_host_tlbs();
				kvm_mips_dump_guest_tlbs(vcpu);
				local_irq_restore(flags);
				return KVM_INVALID_INST;
			}
			if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu,
						&vcpu->arch.guest_tlb[index])) {
				kvm_err("%s: handling mapped seg tlb fault failed for %p, index: %u, vcpu: %p, ASID: %#lx\n",
					__func__, opc, index, vcpu,
					read_c0_entryhi());
				kvm_mips_dump_guest_tlbs(vcpu);
				local_irq_restore(flags);
				return KVM_INVALID_INST;
			}
			inst = *(opc);
		}
		local_irq_restore(flags);
	} else if (KVM_GUEST_KSEGX(va) == KVM_GUEST_KSEG0) {
		paddr = kvm_mips_translate_guest_kseg0_to_hpa(vcpu, va);
		vaddr = kmap_atomic(pfn_to_page(PHYS_PFN(paddr)));
		vaddr += paddr & ~PAGE_MASK;
		inst = *(u32 *)vaddr;
		kunmap_atomic(vaddr);
	} else {
		kvm_err("%s: illegal address: %p\n", __func__, opc);
		return KVM_INVALID_INST;
	}

	return inst;
}
