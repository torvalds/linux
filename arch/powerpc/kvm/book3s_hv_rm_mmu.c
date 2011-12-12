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

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>

/*
 * Since this file is built in even if KVM is a module, we need
 * a local copy of this function for the case where kvm_main.c is
 * modular.
 */
static struct kvm_memory_slot *builtin_gfn_to_memslot(struct kvm *kvm,
						gfn_t gfn)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots)
		if (gfn >= memslot->base_gfn &&
		      gfn < memslot->base_gfn + memslot->npages)
			return memslot;
	return NULL;
}

/* Translate address of a vmalloc'd thing to a linear map address */
static void *real_vmalloc_addr(void *x)
{
	unsigned long addr = (unsigned long) x;
	pte_t *p;

	p = find_linux_pte(swapper_pg_dir, addr);
	if (!p || !pte_present(*p))
		return NULL;
	/* assume we don't have huge pages in vmalloc space... */
	addr = (pte_pfn(*p) << PAGE_SHIFT) | (addr & ~PAGE_MASK);
	return __va(addr);
}

long kvmppc_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
		    long pte_index, unsigned long pteh, unsigned long ptel)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long i, pa, gpa, gfn, psize;
	unsigned long slot_fn;
	unsigned long *hpte;
	struct revmap_entry *rev;
	unsigned long g_ptel = ptel;
	struct kvm_memory_slot *memslot;
	unsigned long *physp, pte_size;
	bool realmode = vcpu->arch.vcore->vcore_state == VCORE_RUNNING;

	psize = hpte_page_size(pteh, ptel);
	if (!psize)
		return H_PARAMETER;

	/* Find the memslot (if any) for this address */
	gpa = (ptel & HPTE_R_RPN) & ~(psize - 1);
	gfn = gpa >> PAGE_SHIFT;
	memslot = builtin_gfn_to_memslot(kvm, gfn);
	if (!(memslot && !(memslot->flags & KVM_MEMSLOT_INVALID)))
		return H_PARAMETER;
	slot_fn = gfn - memslot->base_gfn;

	physp = kvm->arch.slot_phys[memslot->id];
	if (!physp)
		return H_PARAMETER;
	physp += slot_fn;
	if (realmode)
		physp = real_vmalloc_addr(physp);
	pa = *physp;
	if (!pa)
		return H_TOO_HARD;
	pa &= PAGE_MASK;

	pte_size = kvm->arch.ram_psize;
	if (pte_size < psize)
		return H_PARAMETER;
	if (pa && pte_size > psize)
		pa |= gpa & (pte_size - 1);

	ptel &= ~(HPTE_R_PP0 - psize);
	ptel |= pa;

	/* Check WIMG */
	if ((ptel & HPTE_R_WIMG) != HPTE_R_M &&
	    (ptel & HPTE_R_WIMG) != (HPTE_R_W | HPTE_R_I | HPTE_R_M))
		return H_PARAMETER;
	pteh &= ~0x60UL;
	pteh |= HPTE_V_VALID;

	if (pte_index >= HPT_NPTE)
		return H_PARAMETER;
	if (likely((flags & H_EXACT) == 0)) {
		pte_index &= ~7UL;
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		for (i = 0; i < 8; ++i) {
			if ((*hpte & HPTE_V_VALID) == 0 &&
			    try_lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID))
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
				while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
					cpu_relax();
				if ((*hpte & HPTE_V_VALID) == 0)
					break;
				*hpte &= ~HPTE_V_HVLOCK;
				hpte += 2;
			}
			if (i == 8)
				return H_PTEG_FULL;
		}
		pte_index += i;
	} else {
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		if (!try_lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID)) {
			/* Lock the slot and check again */
			while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
				cpu_relax();
			if (*hpte & HPTE_V_VALID) {
				*hpte &= ~HPTE_V_HVLOCK;
				return H_PTEG_FULL;
			}
		}
	}

	/* Save away the guest's idea of the second HPTE dword */
	rev = real_vmalloc_addr(&kvm->arch.revmap[pte_index]);
	if (rev)
		rev->guest_rpte = g_ptel;
	hpte[1] = ptel;
	eieio();
	hpte[0] = pteh;
	asm volatile("ptesync" : : : "memory");
	vcpu->arch.gpr[4] = pte_index;
	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_enter);

#define LOCK_TOKEN	(*(u32 *)(&get_paca()->lock_token))

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

long kvmppc_h_remove(struct kvm_vcpu *vcpu, unsigned long flags,
		     unsigned long pte_index, unsigned long avpn,
		     unsigned long va)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *hpte;
	unsigned long v, r, rb;

	if (pte_index >= HPT_NPTE)
		return H_PARAMETER;
	hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	if ((hpte[0] & HPTE_V_VALID) == 0 ||
	    ((flags & H_AVPN) && (hpte[0] & ~0x7fUL) != avpn) ||
	    ((flags & H_ANDCOND) && (hpte[0] & avpn) != 0)) {
		hpte[0] &= ~HPTE_V_HVLOCK;
		return H_NOT_FOUND;
	}
	if (atomic_read(&kvm->online_vcpus) == 1)
		flags |= H_LOCAL;
	vcpu->arch.gpr[4] = v = hpte[0] & ~HPTE_V_HVLOCK;
	vcpu->arch.gpr[5] = r = hpte[1];
	rb = compute_tlbie_rb(v, r, pte_index);
	hpte[0] = 0;
	if (!(flags & H_LOCAL)) {
		while(!try_lock_tlbie(&kvm->arch.tlbie_lock))
			cpu_relax();
		asm volatile("ptesync" : : : "memory");
		asm volatile(PPC_TLBIE(%1,%0)"; eieio; tlbsync"
			     : : "r" (rb), "r" (kvm->arch.lpid));
		asm volatile("ptesync" : : : "memory");
		kvm->arch.tlbie_lock = 0;
	} else {
		asm volatile("ptesync" : : : "memory");
		asm volatile("tlbiel %0" : : "r" (rb));
		asm volatile("ptesync" : : : "memory");
	}
	return H_SUCCESS;
}

long kvmppc_h_bulk_remove(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *args = &vcpu->arch.gpr[4];
	unsigned long *hp, tlbrb[4];
	long int i, found;
	long int n_inval = 0;
	unsigned long flags, req, pte_index;
	long int local = 0;
	long int ret = H_SUCCESS;

	if (atomic_read(&kvm->online_vcpus) == 1)
		local = 1;
	for (i = 0; i < 4; ++i) {
		pte_index = args[i * 2];
		flags = pte_index >> 56;
		pte_index &= ((1ul << 56) - 1);
		req = flags >> 6;
		flags &= 3;
		if (req == 3)
			break;
		if (req != 1 || flags == 3 ||
		    pte_index >= HPT_NPTE) {
			/* parameter error */
			args[i * 2] = ((0xa0 | flags) << 56) + pte_index;
			ret = H_PARAMETER;
			break;
		}
		hp = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		while (!try_lock_hpte(hp, HPTE_V_HVLOCK))
			cpu_relax();
		found = 0;
		if (hp[0] & HPTE_V_VALID) {
			switch (flags & 3) {
			case 0:		/* absolute */
				found = 1;
				break;
			case 1:		/* andcond */
				if (!(hp[0] & args[i * 2 + 1]))
					found = 1;
				break;
			case 2:		/* AVPN */
				if ((hp[0] & ~0x7fUL) == args[i * 2 + 1])
					found = 1;
				break;
			}
		}
		if (!found) {
			hp[0] &= ~HPTE_V_HVLOCK;
			args[i * 2] = ((0x90 | flags) << 56) + pte_index;
			continue;
		}
		/* insert R and C bits from PTE */
		flags |= (hp[1] >> 5) & 0x0c;
		args[i * 2] = ((0x80 | flags) << 56) + pte_index;
		tlbrb[n_inval++] = compute_tlbie_rb(hp[0], hp[1], pte_index);
		hp[0] = 0;
	}
	if (n_inval == 0)
		return ret;

	if (!local) {
		while(!try_lock_tlbie(&kvm->arch.tlbie_lock))
			cpu_relax();
		asm volatile("ptesync" : : : "memory");
		for (i = 0; i < n_inval; ++i)
			asm volatile(PPC_TLBIE(%1,%0)
				     : : "r" (tlbrb[i]), "r" (kvm->arch.lpid));
		asm volatile("eieio; tlbsync; ptesync" : : : "memory");
		kvm->arch.tlbie_lock = 0;
	} else {
		asm volatile("ptesync" : : : "memory");
		for (i = 0; i < n_inval; ++i)
			asm volatile("tlbiel %0" : : "r" (tlbrb[i]));
		asm volatile("ptesync" : : : "memory");
	}
	return ret;
}

long kvmppc_h_protect(struct kvm_vcpu *vcpu, unsigned long flags,
		      unsigned long pte_index, unsigned long avpn,
		      unsigned long va)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *hpte;
	struct revmap_entry *rev;
	unsigned long v, r, rb, mask, bits;

	if (pte_index >= HPT_NPTE)
		return H_PARAMETER;
	hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
	while (!try_lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	if ((hpte[0] & HPTE_V_VALID) == 0 ||
	    ((flags & H_AVPN) && (hpte[0] & ~0x7fUL) != avpn)) {
		hpte[0] &= ~HPTE_V_HVLOCK;
		return H_NOT_FOUND;
	}
	if (atomic_read(&kvm->online_vcpus) == 1)
		flags |= H_LOCAL;
	v = hpte[0];
	bits = (flags << 55) & HPTE_R_PP0;
	bits |= (flags << 48) & HPTE_R_KEY_HI;
	bits |= flags & (HPTE_R_PP | HPTE_R_N | HPTE_R_KEY_LO);

	/* Update guest view of 2nd HPTE dword */
	mask = HPTE_R_PP0 | HPTE_R_PP | HPTE_R_N |
		HPTE_R_KEY_HI | HPTE_R_KEY_LO;
	rev = real_vmalloc_addr(&kvm->arch.revmap[pte_index]);
	if (rev) {
		r = (rev->guest_rpte & ~mask) | bits;
		rev->guest_rpte = r;
	}
	r = (hpte[1] & ~mask) | bits;

	/* Update HPTE */
	rb = compute_tlbie_rb(v, r, pte_index);
	hpte[0] = v & ~HPTE_V_VALID;
	if (!(flags & H_LOCAL)) {
		while(!try_lock_tlbie(&kvm->arch.tlbie_lock))
			cpu_relax();
		asm volatile("ptesync" : : : "memory");
		asm volatile(PPC_TLBIE(%1,%0)"; eieio; tlbsync"
			     : : "r" (rb), "r" (kvm->arch.lpid));
		asm volatile("ptesync" : : : "memory");
		kvm->arch.tlbie_lock = 0;
	} else {
		asm volatile("ptesync" : : : "memory");
		asm volatile("tlbiel %0" : : "r" (rb));
		asm volatile("ptesync" : : : "memory");
	}
	hpte[1] = r;
	eieio();
	hpte[0] = v & ~HPTE_V_HVLOCK;
	asm volatile("ptesync" : : : "memory");
	return H_SUCCESS;
}

long kvmppc_h_read(struct kvm_vcpu *vcpu, unsigned long flags,
		   unsigned long pte_index)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *hpte, r;
	int i, n = 1;
	struct revmap_entry *rev = NULL;

	if (pte_index >= HPT_NPTE)
		return H_PARAMETER;
	if (flags & H_READ_4) {
		pte_index &= ~3;
		n = 4;
	}
	if (flags & H_R_XLATE)
		rev = real_vmalloc_addr(&kvm->arch.revmap[pte_index]);
	for (i = 0; i < n; ++i, ++pte_index) {
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		r = hpte[1];
		if (hpte[0] & HPTE_V_VALID) {
			if (rev)
				r = rev[i].guest_rpte;
			else
				r = hpte[1] | HPTE_R_RPN;
		}
		vcpu->arch.gpr[4 + i * 2] = hpte[0];
		vcpu->arch.gpr[5 + i * 2] = r;
	}
	return H_SUCCESS;
}
