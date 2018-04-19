/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright 2010-2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/log2.h>

#include <asm/tlbflush.h>
#include <asm/trace.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/pte-walk.h>

/* Translate address of a vmalloc'd thing to a linear map address */
static void *real_vmalloc_addr(void *x)
{
	unsigned long addr = (unsigned long) x;
	pte_t *p;
	/*
	 * assume we don't have huge pages in vmalloc space...
	 * So don't worry about THP collapse/split. Called
	 * Only in realmode with MSR_EE = 0, hence won't need irq_save/restore.
	 */
	p = find_init_mm_pte(addr, NULL);
	if (!p || !pte_present(*p))
		return NULL;
	addr = (pte_pfn(*p) << PAGE_SHIFT) | (addr & ~PAGE_MASK);
	return __va(addr);
}

/* Return 1 if we need to do a global tlbie, 0 if we can use tlbiel */
static int global_invalidates(struct kvm *kvm, unsigned long flags)
{
	int global;
	int cpu;

	/*
	 * If there is only one vcore, and it's currently running,
	 * as indicated by local_paca->kvm_hstate.kvm_vcpu being set,
	 * we can use tlbiel as long as we mark all other physical
	 * cores as potentially having stale TLB entries for this lpid.
	 * Otherwise, don't use tlbiel.
	 */
	if (kvm->arch.online_vcores == 1 && local_paca->kvm_hstate.kvm_vcpu)
		global = 0;
	else
		global = 1;

	if (!global) {
		/* any other core might now have stale TLB entries... */
		smp_wmb();
		cpumask_setall(&kvm->arch.need_tlb_flush);
		cpu = local_paca->kvm_hstate.kvm_vcore->pcpu;
		/*
		 * On POWER9, threads are independent but the TLB is shared,
		 * so use the bit for the first thread to represent the core.
		 */
		if (cpu_has_feature(CPU_FTR_ARCH_300))
			cpu = cpu_first_thread_sibling(cpu);
		cpumask_clear_cpu(cpu, &kvm->arch.need_tlb_flush);
	}

	return global;
}

/*
 * Add this HPTE into the chain for the real page.
 * Must be called with the chain locked; it unlocks the chain.
 */
void kvmppc_add_revmap_chain(struct kvm *kvm, struct revmap_entry *rev,
			     unsigned long *rmap, long pte_index, int realmode)
{
	struct revmap_entry *head, *tail;
	unsigned long i;

	if (*rmap & KVMPPC_RMAP_PRESENT) {
		i = *rmap & KVMPPC_RMAP_INDEX;
		head = &kvm->arch.hpt.rev[i];
		if (realmode)
			head = real_vmalloc_addr(head);
		tail = &kvm->arch.hpt.rev[head->back];
		if (realmode)
			tail = real_vmalloc_addr(tail);
		rev->forw = i;
		rev->back = head->back;
		tail->forw = pte_index;
		head->back = pte_index;
	} else {
		rev->forw = rev->back = pte_index;
		*rmap = (*rmap & ~KVMPPC_RMAP_INDEX) |
			pte_index | KVMPPC_RMAP_PRESENT;
	}
	unlock_rmap(rmap);
}
EXPORT_SYMBOL_GPL(kvmppc_add_revmap_chain);

/* Update the changed page order field of an rmap entry */
void kvmppc_update_rmap_change(unsigned long *rmap, unsigned long psize)
{
	unsigned long order;

	if (!psize)
		return;
	order = ilog2(psize);
	order <<= KVMPPC_RMAP_CHG_SHIFT;
	if (order > (*rmap & KVMPPC_RMAP_CHG_ORDER))
		*rmap = (*rmap & ~KVMPPC_RMAP_CHG_ORDER) | order;
}
EXPORT_SYMBOL_GPL(kvmppc_update_rmap_change);

/* Returns a pointer to the revmap entry for the page mapped by a HPTE */
static unsigned long *revmap_for_hpte(struct kvm *kvm, unsigned long hpte_v,
				      unsigned long hpte_gr)
{
	struct kvm_memory_slot *memslot;
	unsigned long *rmap;
	unsigned long gfn;

	gfn = hpte_rpn(hpte_gr, hpte_page_size(hpte_v, hpte_gr));
	memslot = __gfn_to_memslot(kvm_memslots_raw(kvm), gfn);
	if (!memslot)
		return NULL;

	rmap = real_vmalloc_addr(&memslot->arch.rmap[gfn - memslot->base_gfn]);
	return rmap;
}

/* Remove this HPTE from the chain for a real page */
static void remove_revmap_chain(struct kvm *kvm, long pte_index,
				struct revmap_entry *rev,
				unsigned long hpte_v, unsigned long hpte_r)
{
	struct revmap_entry *next, *prev;
	unsigned long ptel, head;
	unsigned long *rmap;
	unsigned long rcbits;

	rcbits = hpte_r & (HPTE_R_R | HPTE_R_C);
	ptel = rev->guest_rpte |= rcbits;
	rmap = revmap_for_hpte(kvm, hpte_v, ptel);
	if (!rmap)
		return;
	lock_rmap(rmap);

	head = *rmap & KVMPPC_RMAP_INDEX;
	next = real_vmalloc_addr(&kvm->arch.hpt.rev[rev->forw]);
	prev = real_vmalloc_addr(&kvm->arch.hpt.rev[rev->back]);
	next->back = rev->back;
	prev->forw = rev->forw;
	if (head == pte_index) {
		head = rev->forw;
		if (head == pte_index)
			*rmap &= ~(KVMPPC_RMAP_PRESENT | KVMPPC_RMAP_INDEX);
		else
			*rmap = (*rmap & ~KVMPPC_RMAP_INDEX) | head;
	}
	*rmap |= rcbits << KVMPPC_RMAP_RC_SHIFT;
	if (rcbits & HPTE_R_C)
		kvmppc_update_rmap_change(rmap, hpte_page_size(hpte_v, hpte_r));
	unlock_rmap(rmap);
}

long kvmppc_do_h_enter(struct kvm *kvm, unsigned long flags,
		       long pte_index, unsigned long pteh, unsigned long ptel,
		       pgd_t *pgdir, bool realmode, unsigned long *pte_idx_ret)
{
	unsigned long i, pa, gpa, gfn, psize;
	unsigned long slot_fn, hva;
	__be64 *hpte;
	struct revmap_entry *rev;
	unsigned long g_ptel;
	struct kvm_memory_slot *memslot;
	unsigned hpage_shift;
	bool is_ci;
	unsigned long *rmap;
	pte_t *ptep;
	unsigned int writing;
	unsigned long mmu_seq;
	unsigned long rcbits, irq_flags = 0;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	psize = hpte_page_size(pteh, ptel);
	if (!psize)
		return H_PARAMETER;
	writing = hpte_is_writable(ptel);
	pteh &= ~(HPTE_V_HVLOCK | HPTE_V_ABSENT | HPTE_V_VALID);
	ptel &= ~HPTE_GR_RESERVED;
	g_ptel = ptel;

	/* used later to detect if we might have been invalidated */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	/* Find the memslot (if any) for this address */
	gpa = (ptel & HPTE_R_RPN) & ~(psize - 1);
	gfn = gpa >> PAGE_SHIFT;
	memslot = __gfn_to_memslot(kvm_memslots_raw(kvm), gfn);
	pa = 0;
	is_ci = false;
	rmap = NULL;
	if (!(memslot && !(memslot->flags & KVM_MEMSLOT_INVALID))) {
		/* Emulated MMIO - mark this with key=31 */
		pteh |= HPTE_V_ABSENT;
		ptel |= HPTE_R_KEY_HI | HPTE_R_KEY_LO;
		goto do_insert;
	}

	/* Check if the requested page fits entirely in the memslot. */
	if (!slot_is_aligned(memslot, psize))
		return H_PARAMETER;
	slot_fn = gfn - memslot->base_gfn;
	rmap = &memslot->arch.rmap[slot_fn];

	/* Translate to host virtual address */
	hva = __gfn_to_hva_memslot(memslot, gfn);
	/*
	 * If we had a page table table change after lookup, we would
	 * retry via mmu_notifier_retry.
	 */
	if (!realmode)
		local_irq_save(irq_flags);
	/*
	 * If called in real mode we have MSR_EE = 0. Otherwise
	 * we disable irq above.
	 */
	ptep = __find_linux_pte(pgdir, hva, NULL, &hpage_shift);
	if (ptep) {
		pte_t pte;
		unsigned int host_pte_size;

		if (hpage_shift)
			host_pte_size = 1ul << hpage_shift;
		else
			host_pte_size = PAGE_SIZE;
		/*
		 * We should always find the guest page size
		 * to <= host page size, if host is using hugepage
		 */
		if (host_pte_size < psize) {
			if (!realmode)
				local_irq_restore(flags);
			return H_PARAMETER;
		}
		pte = kvmppc_read_update_linux_pte(ptep, writing);
		if (pte_present(pte) && !pte_protnone(pte)) {
			if (writing && !__pte_write(pte))
				/* make the actual HPTE be read-only */
				ptel = hpte_make_readonly(ptel);
			is_ci = pte_ci(pte);
			pa = pte_pfn(pte) << PAGE_SHIFT;
			pa |= hva & (host_pte_size - 1);
			pa |= gpa & ~PAGE_MASK;
		}
	}
	if (!realmode)
		local_irq_restore(irq_flags);

	ptel &= HPTE_R_KEY | HPTE_R_PP0 | (psize-1);
	ptel |= pa;

	if (pa)
		pteh |= HPTE_V_VALID;
	else {
		pteh |= HPTE_V_ABSENT;
		ptel &= ~(HPTE_R_KEY_HI | HPTE_R_KEY_LO);
	}

	/*If we had host pte mapping then  Check WIMG */
	if (ptep && !hpte_cache_flags_ok(ptel, is_ci)) {
		if (is_ci)
			return H_PARAMETER;
		/*
		 * Allow guest to map emulated device memory as
		 * uncacheable, but actually make it cacheable.
		 */
		ptel &= ~(HPTE_R_W|HPTE_R_I|HPTE_R_G);
		ptel |= HPTE_R_M;
	}

	/* Find and lock the HPTEG slot to use */
 do_insert:
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;
	if (likely((flags & H_EXACT) == 0)) {
		pte_index &= ~7UL;
		hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
		for (i = 0; i < 8; ++i) {
			if ((be64_to_cpu(*hpte) & HPTE_V_VALID) == 0 &&
			    try_lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID |
					  HPTE_V_ABSENT))
				break;
			hpte += 2;
		}
		if (i == 8) {
			/*
			 * Since try_lock_hpte doesn't retry (not even stdcx.
			 * failures), it could be that there is a free slot
			 * but we transiently failed to lock it.  Try again,
			 * actually locking each slot and checking it.
			 */
			hpte -= 16;
			for (i = 0; i < 8; ++i) {
				u64 pte;
				while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
					cpu_relax();
				pte = be64_to_cpu(hpte[0]);
				if (!(pte & (HPTE_V_VALID | HPTE_V_ABSENT)))
					break;
				__unlock_hpte(hpte, pte);
				hpte += 2;
			}
			if (i == 8)
				return H_PTEG_FULL;
		}
		pte_index += i;
	} else {
		hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
		if (!try_lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID |
				   HPTE_V_ABSENT)) {
			/* Lock the slot and check again */
			u64 pte;

			while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
				cpu_relax();
			pte = be64_to_cpu(hpte[0]);
			if (pte & (HPTE_V_VALID | HPTE_V_ABSENT)) {
				__unlock_hpte(hpte, pte);
				return H_PTEG_FULL;
			}
		}
	}

	/* Save away the guest's idea of the second HPTE dword */
	rev = &kvm->arch.hpt.rev[pte_index];
	if (realmode)
		rev = real_vmalloc_addr(rev);
	if (rev) {
		rev->guest_rpte = g_ptel;
		note_hpte_modification(kvm, rev);
	}

	/* Link HPTE into reverse-map chain */
	if (pteh & HPTE_V_VALID) {
		if (realmode)
			rmap = real_vmalloc_addr(rmap);
		lock_rmap(rmap);
		/* Check for pending invalidations under the rmap chain lock */
		if (mmu_notifier_retry(kvm, mmu_seq)) {
			/* inval in progress, write a non-present HPTE */
			pteh |= HPTE_V_ABSENT;
			pteh &= ~HPTE_V_VALID;
			ptel &= ~(HPTE_R_KEY_HI | HPTE_R_KEY_LO);
			unlock_rmap(rmap);
		} else {
			kvmppc_add_revmap_chain(kvm, rev, rmap, pte_index,
						realmode);
			/* Only set R/C in real HPTE if already set in *rmap */
			rcbits = *rmap >> KVMPPC_RMAP_RC_SHIFT;
			ptel &= rcbits | ~(HPTE_R_R | HPTE_R_C);
		}
	}

	/* Convert to new format on P9 */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		ptel = hpte_old_to_new_r(pteh, ptel);
		pteh = hpte_old_to_new_v(pteh);
	}
	hpte[1] = cpu_to_be64(ptel);

	/* Write the first HPTE dword, unlocking the HPTE and making it valid */
	eieio();
	__unlock_hpte(hpte, pteh);
	asm volatile("ptesync" : : : "memory");

	*pte_idx_ret = pte_index;
	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_do_h_enter);

long kvmppc_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
		    long pte_index, unsigned long pteh, unsigned long ptel)
{
	return kvmppc_do_h_enter(vcpu->kvm, flags, pte_index, pteh, ptel,
				 vcpu->arch.pgdir, true, &vcpu->arch.gpr[4]);
}

#ifdef __BIG_ENDIAN__
#define LOCK_TOKEN	(*(u32 *)(&get_paca()->lock_token))
#else
#define LOCK_TOKEN	(*(u32 *)(&get_paca()->paca_index))
#endif

static inline int is_mmio_hpte(unsigned long v, unsigned long r)
{
	return ((v & HPTE_V_ABSENT) &&
		(r & (HPTE_R_KEY_HI | HPTE_R_KEY_LO)) ==
		(HPTE_R_KEY_HI | HPTE_R_KEY_LO));
}

static inline int try_lock_tlbie(unsigned int *lock)
{
	unsigned int tmp, old;
	unsigned int token = LOCK_TOKEN;

	asm volatile("1:lwarx	%1,0,%2\n"
		     "	cmpwi	cr0,%1,0\n"
		     "	bne	2f\n"
		     "  stwcx.	%3,0,%2\n"
		     "	bne-	1b\n"
		     "  isync\n"
		     "2:"
		     : "=&r" (tmp), "=&r" (old)
		     : "r" (lock), "r" (token)
		     : "cc", "memory");
	return old == 0;
}

static void do_tlbies(struct kvm *kvm, unsigned long *rbvalues,
		      long npages, int global, bool need_sync)
{
	long i;

	/*
	 * We use the POWER9 5-operand versions of tlbie and tlbiel here.
	 * Since we are using RIC=0 PRS=0 R=0, and P7/P8 tlbiel ignores
	 * the RS field, this is backwards-compatible with P7 and P8.
	 */
	if (global) {
		while (!try_lock_tlbie(&kvm->arch.tlbie_lock))
			cpu_relax();
		if (need_sync)
			asm volatile("ptesync" : : : "memory");
		for (i = 0; i < npages; ++i) {
			asm volatile(PPC_TLBIE_5(%0,%1,0,0,0) : :
				     "r" (rbvalues[i]), "r" (kvm->arch.lpid));
		}
		asm volatile("eieio; tlbsync; ptesync" : : : "memory");
		kvm->arch.tlbie_lock = 0;
	} else {
		if (need_sync)
			asm volatile("ptesync" : : : "memory");
		for (i = 0; i < npages; ++i) {
			asm volatile(PPC_TLBIEL(%0,%1,0,0,0) : :
				     "r" (rbvalues[i]), "r" (0));
		}
		asm volatile("ptesync" : : : "memory");
	}
}

long kvmppc_do_h_remove(struct kvm *kvm, unsigned long flags,
			unsigned long pte_index, unsigned long avpn,
			unsigned long *hpret)
{
	__be64 *hpte;
	unsigned long v, r, rb;
	struct revmap_entry *rev;
	u64 pte, orig_pte, pte_r;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;
	hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	pte = orig_pte = be64_to_cpu(hpte[0]);
	pte_r = be64_to_cpu(hpte[1]);
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		pte = hpte_new_to_old_v(pte, pte_r);
		pte_r = hpte_new_to_old_r(pte_r);
	}
	if ((pte & (HPTE_V_ABSENT | HPTE_V_VALID)) == 0 ||
	    ((flags & H_AVPN) && (pte & ~0x7fUL) != avpn) ||
	    ((flags & H_ANDCOND) && (pte & avpn) != 0)) {
		__unlock_hpte(hpte, orig_pte);
		return H_NOT_FOUND;
	}

	rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
	v = pte & ~HPTE_V_HVLOCK;
	if (v & HPTE_V_VALID) {
		hpte[0] &= ~cpu_to_be64(HPTE_V_VALID);
		rb = compute_tlbie_rb(v, pte_r, pte_index);
		do_tlbies(kvm, &rb, 1, global_invalidates(kvm, flags), true);
		/*
		 * The reference (R) and change (C) bits in a HPT
		 * entry can be set by hardware at any time up until
		 * the HPTE is invalidated and the TLB invalidation
		 * sequence has completed.  This means that when
		 * removing a HPTE, we need to re-read the HPTE after
		 * the invalidation sequence has completed in order to
		 * obtain reliable values of R and C.
		 */
		remove_revmap_chain(kvm, pte_index, rev, v,
				    be64_to_cpu(hpte[1]));
	}
	r = rev->guest_rpte & ~HPTE_GR_RESERVED;
	note_hpte_modification(kvm, rev);
	unlock_hpte(hpte, 0);

	if (is_mmio_hpte(v, pte_r))
		atomic64_inc(&kvm->arch.mmio_update);

	if (v & HPTE_V_ABSENT)
		v = (v & ~HPTE_V_ABSENT) | HPTE_V_VALID;
	hpret[0] = v;
	hpret[1] = r;
	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_do_h_remove);

long kvmppc_h_remove(struct kvm_vcpu *vcpu, unsigned long flags,
		     unsigned long pte_index, unsigned long avpn)
{
	return kvmppc_do_h_remove(vcpu->kvm, flags, pte_index, avpn,
				  &vcpu->arch.gpr[4]);
}

long kvmppc_h_bulk_remove(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *args = &vcpu->arch.gpr[4];
	__be64 *hp, *hptes[4];
	unsigned long tlbrb[4];
	long int i, j, k, n, found, indexes[4];
	unsigned long flags, req, pte_index, rcbits;
	int global;
	long int ret = H_SUCCESS;
	struct revmap_entry *rev, *revs[4];
	u64 hp0, hp1;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	global = global_invalidates(kvm, 0);
	for (i = 0; i < 4 && ret == H_SUCCESS; ) {
		n = 0;
		for (; i < 4; ++i) {
			j = i * 2;
			pte_index = args[j];
			flags = pte_index >> 56;
			pte_index &= ((1ul << 56) - 1);
			req = flags >> 6;
			flags &= 3;
			if (req == 3) {		/* no more requests */
				i = 4;
				break;
			}
			if (req != 1 || flags == 3 ||
			    pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt)) {
				/* parameter error */
				args[j] = ((0xa0 | flags) << 56) + pte_index;
				ret = H_PARAMETER;
				break;
			}
			hp = (__be64 *) (kvm->arch.hpt.virt + (pte_index << 4));
			/* to avoid deadlock, don't spin except for first */
			if (!try_lock_hpte(hp, HPTE_V_HVLOCK)) {
				if (n)
					break;
				while (!try_lock_hpte(hp, HPTE_V_HVLOCK))
					cpu_relax();
			}
			found = 0;
			hp0 = be64_to_cpu(hp[0]);
			hp1 = be64_to_cpu(hp[1]);
			if (cpu_has_feature(CPU_FTR_ARCH_300)) {
				hp0 = hpte_new_to_old_v(hp0, hp1);
				hp1 = hpte_new_to_old_r(hp1);
			}
			if (hp0 & (HPTE_V_ABSENT | HPTE_V_VALID)) {
				switch (flags & 3) {
				case 0:		/* absolute */
					found = 1;
					break;
				case 1:		/* andcond */
					if (!(hp0 & args[j + 1]))
						found = 1;
					break;
				case 2:		/* AVPN */
					if ((hp0 & ~0x7fUL) == args[j + 1])
						found = 1;
					break;
				}
			}
			if (!found) {
				hp[0] &= ~cpu_to_be64(HPTE_V_HVLOCK);
				args[j] = ((0x90 | flags) << 56) + pte_index;
				continue;
			}

			args[j] = ((0x80 | flags) << 56) + pte_index;
			rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
			note_hpte_modification(kvm, rev);

			if (!(hp0 & HPTE_V_VALID)) {
				/* insert R and C bits from PTE */
				rcbits = rev->guest_rpte & (HPTE_R_R|HPTE_R_C);
				args[j] |= rcbits << (56 - 5);
				hp[0] = 0;
				if (is_mmio_hpte(hp0, hp1))
					atomic64_inc(&kvm->arch.mmio_update);
				continue;
			}

			/* leave it locked */
			hp[0] &= ~cpu_to_be64(HPTE_V_VALID);
			tlbrb[n] = compute_tlbie_rb(hp0, hp1, pte_index);
			indexes[n] = j;
			hptes[n] = hp;
			revs[n] = rev;
			++n;
		}

		if (!n)
			break;

		/* Now that we've collected a batch, do the tlbies */
		do_tlbies(kvm, tlbrb, n, global, true);

		/* Read PTE low words after tlbie to get final R/C values */
		for (k = 0; k < n; ++k) {
			j = indexes[k];
			pte_index = args[j] & ((1ul << 56) - 1);
			hp = hptes[k];
			rev = revs[k];
			remove_revmap_chain(kvm, pte_index, rev,
				be64_to_cpu(hp[0]), be64_to_cpu(hp[1]));
			rcbits = rev->guest_rpte & (HPTE_R_R|HPTE_R_C);
			args[j] |= rcbits << (56 - 5);
			__unlock_hpte(hp, 0);
		}
	}

	return ret;
}

long kvmppc_h_protect(struct kvm_vcpu *vcpu, unsigned long flags,
		      unsigned long pte_index, unsigned long avpn,
		      unsigned long va)
{
	struct kvm *kvm = vcpu->kvm;
	__be64 *hpte;
	struct revmap_entry *rev;
	unsigned long v, r, rb, mask, bits;
	u64 pte_v, pte_r;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;

	hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	v = pte_v = be64_to_cpu(hpte[0]);
	if (cpu_has_feature(CPU_FTR_ARCH_300))
		v = hpte_new_to_old_v(v, be64_to_cpu(hpte[1]));
	if ((v & (HPTE_V_ABSENT | HPTE_V_VALID)) == 0 ||
	    ((flags & H_AVPN) && (v & ~0x7fUL) != avpn)) {
		__unlock_hpte(hpte, pte_v);
		return H_NOT_FOUND;
	}

	pte_r = be64_to_cpu(hpte[1]);
	bits = (flags << 55) & HPTE_R_PP0;
	bits |= (flags << 48) & HPTE_R_KEY_HI;
	bits |= flags & (HPTE_R_PP | HPTE_R_N | HPTE_R_KEY_LO);

	/* Update guest view of 2nd HPTE dword */
	mask = HPTE_R_PP0 | HPTE_R_PP | HPTE_R_N |
		HPTE_R_KEY_HI | HPTE_R_KEY_LO;
	rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
	if (rev) {
		r = (rev->guest_rpte & ~mask) | bits;
		rev->guest_rpte = r;
		note_hpte_modification(kvm, rev);
	}

	/* Update HPTE */
	if (v & HPTE_V_VALID) {
		/*
		 * If the page is valid, don't let it transition from
		 * readonly to writable.  If it should be writable, we'll
		 * take a trap and let the page fault code sort it out.
		 */
		r = (pte_r & ~mask) | bits;
		if (hpte_is_writable(r) && !hpte_is_writable(pte_r))
			r = hpte_make_readonly(r);
		/* If the PTE is changing, invalidate it first */
		if (r != pte_r) {
			rb = compute_tlbie_rb(v, r, pte_index);
			hpte[0] = cpu_to_be64((pte_v & ~HPTE_V_VALID) |
					      HPTE_V_ABSENT);
			do_tlbies(kvm, &rb, 1, global_invalidates(kvm, flags),
				  true);
			/* Don't lose R/C bit updates done by hardware */
			r |= be64_to_cpu(hpte[1]) & (HPTE_R_R | HPTE_R_C);
			hpte[1] = cpu_to_be64(r);
		}
	}
	unlock_hpte(hpte, pte_v & ~HPTE_V_HVLOCK);
	asm volatile("ptesync" : : : "memory");
	if (is_mmio_hpte(v, pte_r))
		atomic64_inc(&kvm->arch.mmio_update);

	return H_SUCCESS;
}

long kvmppc_h_read(struct kvm_vcpu *vcpu, unsigned long flags,
		   unsigned long pte_index)
{
	struct kvm *kvm = vcpu->kvm;
	__be64 *hpte;
	unsigned long v, r;
	int i, n = 1;
	struct revmap_entry *rev = NULL;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;
	if (flags & H_READ_4) {
		pte_index &= ~3;
		n = 4;
	}
	rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
	for (i = 0; i < n; ++i, ++pte_index) {
		hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
		v = be64_to_cpu(hpte[0]) & ~HPTE_V_HVLOCK;
		r = be64_to_cpu(hpte[1]);
		if (cpu_has_feature(CPU_FTR_ARCH_300)) {
			v = hpte_new_to_old_v(v, r);
			r = hpte_new_to_old_r(r);
		}
		if (v & HPTE_V_ABSENT) {
			v &= ~HPTE_V_ABSENT;
			v |= HPTE_V_VALID;
		}
		if (v & HPTE_V_VALID) {
			r = rev[i].guest_rpte | (r & (HPTE_R_R | HPTE_R_C));
			r &= ~HPTE_GR_RESERVED;
		}
		vcpu->arch.gpr[4 + i * 2] = v;
		vcpu->arch.gpr[5 + i * 2] = r;
	}
	return H_SUCCESS;
}

long kvmppc_h_clear_ref(struct kvm_vcpu *vcpu, unsigned long flags,
			unsigned long pte_index)
{
	struct kvm *kvm = vcpu->kvm;
	__be64 *hpte;
	unsigned long v, r, gr;
	struct revmap_entry *rev;
	unsigned long *rmap;
	long ret = H_NOT_FOUND;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;

	rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
	hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	v = be64_to_cpu(hpte[0]);
	r = be64_to_cpu(hpte[1]);
	if (!(v & (HPTE_V_VALID | HPTE_V_ABSENT)))
		goto out;

	gr = rev->guest_rpte;
	if (rev->guest_rpte & HPTE_R_R) {
		rev->guest_rpte &= ~HPTE_R_R;
		note_hpte_modification(kvm, rev);
	}
	if (v & HPTE_V_VALID) {
		gr |= r & (HPTE_R_R | HPTE_R_C);
		if (r & HPTE_R_R) {
			kvmppc_clear_ref_hpte(kvm, hpte, pte_index);
			rmap = revmap_for_hpte(kvm, v, gr);
			if (rmap) {
				lock_rmap(rmap);
				*rmap |= KVMPPC_RMAP_REFERENCED;
				unlock_rmap(rmap);
			}
		}
	}
	vcpu->arch.gpr[4] = gr;
	ret = H_SUCCESS;
 out:
	unlock_hpte(hpte, v & ~HPTE_V_HVLOCK);
	return ret;
}

long kvmppc_h_clear_mod(struct kvm_vcpu *vcpu, unsigned long flags,
			unsigned long pte_index)
{
	struct kvm *kvm = vcpu->kvm;
	__be64 *hpte;
	unsigned long v, r, gr;
	struct revmap_entry *rev;
	unsigned long *rmap;
	long ret = H_NOT_FOUND;

	if (kvm_is_radix(kvm))
		return H_FUNCTION;
	if (pte_index >= kvmppc_hpt_npte(&kvm->arch.hpt))
		return H_PARAMETER;

	rev = real_vmalloc_addr(&kvm->arch.hpt.rev[pte_index]);
	hpte = (__be64 *)(kvm->arch.hpt.virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	v = be64_to_cpu(hpte[0]);
	r = be64_to_cpu(hpte[1]);
	if (!(v & (HPTE_V_VALID | HPTE_V_ABSENT)))
		goto out;

	gr = rev->guest_rpte;
	if (gr & HPTE_R_C) {
		rev->guest_rpte &= ~HPTE_R_C;
		note_hpte_modification(kvm, rev);
	}
	if (v & HPTE_V_VALID) {
		/* need to make it temporarily absent so C is stable */
		hpte[0] |= cpu_to_be64(HPTE_V_ABSENT);
		kvmppc_invalidate_hpte(kvm, hpte, pte_index);
		r = be64_to_cpu(hpte[1]);
		gr |= r & (HPTE_R_R | HPTE_R_C);
		if (r & HPTE_R_C) {
			unsigned long psize = hpte_page_size(v, r);
			hpte[1] = cpu_to_be64(r & ~HPTE_R_C);
			eieio();
			rmap = revmap_for_hpte(kvm, v, gr);
			if (rmap) {
				lock_rmap(rmap);
				*rmap |= KVMPPC_RMAP_CHANGED;
				kvmppc_update_rmap_change(rmap, psize);
				unlock_rmap(rmap);
			}
		}
	}
	vcpu->arch.gpr[4] = gr;
	ret = H_SUCCESS;
 out:
	unlock_hpte(hpte, v & ~HPTE_V_HVLOCK);
	return ret;
}

void kvmppc_invalidate_hpte(struct kvm *kvm, __be64 *hptep,
			unsigned long pte_index)
{
	unsigned long rb;
	u64 hp0, hp1;

	hptep[0] &= ~cpu_to_be64(HPTE_V_VALID);
	hp0 = be64_to_cpu(hptep[0]);
	hp1 = be64_to_cpu(hptep[1]);
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		hp0 = hpte_new_to_old_v(hp0, hp1);
		hp1 = hpte_new_to_old_r(hp1);
	}
	rb = compute_tlbie_rb(hp0, hp1, pte_index);
	do_tlbies(kvm, &rb, 1, 1, true);
}
EXPORT_SYMBOL_GPL(kvmppc_invalidate_hpte);

void kvmppc_clear_ref_hpte(struct kvm *kvm, __be64 *hptep,
			   unsigned long pte_index)
{
	unsigned long rb;
	unsigned char rbyte;
	u64 hp0, hp1;

	hp0 = be64_to_cpu(hptep[0]);
	hp1 = be64_to_cpu(hptep[1]);
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		hp0 = hpte_new_to_old_v(hp0, hp1);
		hp1 = hpte_new_to_old_r(hp1);
	}
	rb = compute_tlbie_rb(hp0, hp1, pte_index);
	rbyte = (be64_to_cpu(hptep[1]) & ~HPTE_R_R) >> 8;
	/* modify only the second-last byte, which contains the ref bit */
	*((char *)hptep + 14) = rbyte;
	do_tlbies(kvm, &rb, 1, 1, false);
}
EXPORT_SYMBOL_GPL(kvmppc_clear_ref_hpte);

static int slb_base_page_shift[4] = {
	24,	/* 16M */
	16,	/* 64k */
	34,	/* 16G */
	20,	/* 1M, unsupported */
};

static struct mmio_hpte_cache_entry *mmio_cache_search(struct kvm_vcpu *vcpu,
		unsigned long eaddr, unsigned long slb_v, long mmio_update)
{
	struct mmio_hpte_cache_entry *entry = NULL;
	unsigned int pshift;
	unsigned int i;

	for (i = 0; i < MMIO_HPTE_CACHE_SIZE; i++) {
		entry = &vcpu->arch.mmio_cache.entry[i];
		if (entry->mmio_update == mmio_update) {
			pshift = entry->slb_base_pshift;
			if ((entry->eaddr >> pshift) == (eaddr >> pshift) &&
			    entry->slb_v == slb_v)
				return entry;
		}
	}
	return NULL;
}

static struct mmio_hpte_cache_entry *
			next_mmio_cache_entry(struct kvm_vcpu *vcpu)
{
	unsigned int index = vcpu->arch.mmio_cache.index;

	vcpu->arch.mmio_cache.index++;
	if (vcpu->arch.mmio_cache.index == MMIO_HPTE_CACHE_SIZE)
		vcpu->arch.mmio_cache.index = 0;

	return &vcpu->arch.mmio_cache.entry[index];
}

/* When called from virtmode, this func should be protected by
 * preempt_disable(), otherwise, the holding of HPTE_V_HVLOCK
 * can trigger deadlock issue.
 */
long kvmppc_hv_find_lock_hpte(struct kvm *kvm, gva_t eaddr, unsigned long slb_v,
			      unsigned long valid)
{
	unsigned int i;
	unsigned int pshift;
	unsigned long somask;
	unsigned long vsid, hash;
	unsigned long avpn;
	__be64 *hpte;
	unsigned long mask, val;
	unsigned long v, r, orig_v;

	/* Get page shift, work out hash and AVPN etc. */
	mask = SLB_VSID_B | HPTE_V_AVPN | HPTE_V_SECONDARY;
	val = 0;
	pshift = 12;
	if (slb_v & SLB_VSID_L) {
		mask |= HPTE_V_LARGE;
		val |= HPTE_V_LARGE;
		pshift = slb_base_page_shift[(slb_v & SLB_VSID_LP) >> 4];
	}
	if (slb_v & SLB_VSID_B_1T) {
		somask = (1UL << 40) - 1;
		vsid = (slb_v & ~SLB_VSID_B) >> SLB_VSID_SHIFT_1T;
		vsid ^= vsid << 25;
	} else {
		somask = (1UL << 28) - 1;
		vsid = (slb_v & ~SLB_VSID_B) >> SLB_VSID_SHIFT;
	}
	hash = (vsid ^ ((eaddr & somask) >> pshift)) & kvmppc_hpt_mask(&kvm->arch.hpt);
	avpn = slb_v & ~(somask >> 16);	/* also includes B */
	avpn |= (eaddr & somask) >> 16;

	if (pshift >= 24)
		avpn &= ~((1UL << (pshift - 16)) - 1);
	else
		avpn &= ~0x7fUL;
	val |= avpn;

	for (;;) {
		hpte = (__be64 *)(kvm->arch.hpt.virt + (hash << 7));

		for (i = 0; i < 16; i += 2) {
			/* Read the PTE racily */
			v = be64_to_cpu(hpte[i]) & ~HPTE_V_HVLOCK;
			if (cpu_has_feature(CPU_FTR_ARCH_300))
				v = hpte_new_to_old_v(v, be64_to_cpu(hpte[i+1]));

			/* Check valid/absent, hash, segment size and AVPN */
			if (!(v & valid) || (v & mask) != val)
				continue;

			/* Lock the PTE and read it under the lock */
			while (!try_lock_hpte(&hpte[i], HPTE_V_HVLOCK))
				cpu_relax();
			v = orig_v = be64_to_cpu(hpte[i]) & ~HPTE_V_HVLOCK;
			r = be64_to_cpu(hpte[i+1]);
			if (cpu_has_feature(CPU_FTR_ARCH_300)) {
				v = hpte_new_to_old_v(v, r);
				r = hpte_new_to_old_r(r);
			}

			/*
			 * Check the HPTE again, including base page size
			 */
			if ((v & valid) && (v & mask) == val &&
			    hpte_base_page_size(v, r) == (1ul << pshift))
				/* Return with the HPTE still locked */
				return (hash << 3) + (i >> 1);

			__unlock_hpte(&hpte[i], orig_v);
		}

		if (val & HPTE_V_SECONDARY)
			break;
		val |= HPTE_V_SECONDARY;
		hash = hash ^ kvmppc_hpt_mask(&kvm->arch.hpt);
	}
	return -1;
}
EXPORT_SYMBOL(kvmppc_hv_find_lock_hpte);

/*
 * Called in real mode to check whether an HPTE not found fault
 * is due to accessing a paged-out page or an emulated MMIO page,
 * or if a protection fault is due to accessing a page that the
 * guest wanted read/write access to but which we made read-only.
 * Returns a possibly modified status (DSISR) value if not
 * (i.e. pass the interrupt to the guest),
 * -1 to pass the fault up to host kernel mode code, -2 to do that
 * and also load the instruction word (for MMIO emulation),
 * or 0 if we should make the guest retry the access.
 */
long kvmppc_hpte_hv_fault(struct kvm_vcpu *vcpu, unsigned long addr,
			  unsigned long slb_v, unsigned int status, bool data)
{
	struct kvm *kvm = vcpu->kvm;
	long int index;
	unsigned long v, r, gr, orig_v;
	__be64 *hpte;
	unsigned long valid;
	struct revmap_entry *rev;
	unsigned long pp, key;
	struct mmio_hpte_cache_entry *cache_entry = NULL;
	long mmio_update = 0;

	/* For protection fault, expect to find a valid HPTE */
	valid = HPTE_V_VALID;
	if (status & DSISR_NOHPTE) {
		valid |= HPTE_V_ABSENT;
		mmio_update = atomic64_read(&kvm->arch.mmio_update);
		cache_entry = mmio_cache_search(vcpu, addr, slb_v, mmio_update);
	}
	if (cache_entry) {
		index = cache_entry->pte_index;
		v = cache_entry->hpte_v;
		r = cache_entry->hpte_r;
		gr = cache_entry->rpte;
	} else {
		index = kvmppc_hv_find_lock_hpte(kvm, addr, slb_v, valid);
		if (index < 0) {
			if (status & DSISR_NOHPTE)
				return status;	/* there really was no HPTE */
			return 0;	/* for prot fault, HPTE disappeared */
		}
		hpte = (__be64 *)(kvm->arch.hpt.virt + (index << 4));
		v = orig_v = be64_to_cpu(hpte[0]) & ~HPTE_V_HVLOCK;
		r = be64_to_cpu(hpte[1]);
		if (cpu_has_feature(CPU_FTR_ARCH_300)) {
			v = hpte_new_to_old_v(v, r);
			r = hpte_new_to_old_r(r);
		}
		rev = real_vmalloc_addr(&kvm->arch.hpt.rev[index]);
		gr = rev->guest_rpte;

		unlock_hpte(hpte, orig_v);
	}

	/* For not found, if the HPTE is valid by now, retry the instruction */
	if ((status & DSISR_NOHPTE) && (v & HPTE_V_VALID))
		return 0;

	/* Check access permissions to the page */
	pp = gr & (HPTE_R_PP0 | HPTE_R_PP);
	key = (vcpu->arch.shregs.msr & MSR_PR) ? SLB_VSID_KP : SLB_VSID_KS;
	status &= ~DSISR_NOHPTE;	/* DSISR_NOHPTE == SRR1_ISI_NOPT */
	if (!data) {
		if (gr & (HPTE_R_N | HPTE_R_G))
			return status | SRR1_ISI_N_OR_G;
		if (!hpte_read_permission(pp, slb_v & key))
			return status | SRR1_ISI_PROT;
	} else if (status & DSISR_ISSTORE) {
		/* check write permission */
		if (!hpte_write_permission(pp, slb_v & key))
			return status | DSISR_PROTFAULT;
	} else {
		if (!hpte_read_permission(pp, slb_v & key))
			return status | DSISR_PROTFAULT;
	}

	/* Check storage key, if applicable */
	if (data && (vcpu->arch.shregs.msr & MSR_DR)) {
		unsigned int perm = hpte_get_skey_perm(gr, vcpu->arch.amr);
		if (status & DSISR_ISSTORE)
			perm >>= 1;
		if (perm & 1)
			return status | DSISR_KEYFAULT;
	}

	/* Save HPTE info for virtual-mode handler */
	vcpu->arch.pgfault_addr = addr;
	vcpu->arch.pgfault_index = index;
	vcpu->arch.pgfault_hpte[0] = v;
	vcpu->arch.pgfault_hpte[1] = r;
	vcpu->arch.pgfault_cache = cache_entry;

	/* Check the storage key to see if it is possibly emulated MMIO */
	if ((r & (HPTE_R_KEY_HI | HPTE_R_KEY_LO)) ==
	    (HPTE_R_KEY_HI | HPTE_R_KEY_LO)) {
		if (!cache_entry) {
			unsigned int pshift = 12;
			unsigned int pshift_index;

			if (slb_v & SLB_VSID_L) {
				pshift_index = ((slb_v & SLB_VSID_LP) >> 4);
				pshift = slb_base_page_shift[pshift_index];
			}
			cache_entry = next_mmio_cache_entry(vcpu);
			cache_entry->eaddr = addr;
			cache_entry->slb_base_pshift = pshift;
			cache_entry->pte_index = index;
			cache_entry->hpte_v = v;
			cache_entry->hpte_r = r;
			cache_entry->rpte = gr;
			cache_entry->slb_v = slb_v;
			cache_entry->mmio_update = mmio_update;
		}
		if (data && (vcpu->arch.shregs.msr & MSR_IR))
			return -2;	/* MMIO emulation - load instr word */
	}

	return -1;		/* send fault up to host kernel mode */
}
