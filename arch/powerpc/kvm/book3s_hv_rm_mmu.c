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

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>

/* For now use fixed-size 16MB page table */
#define HPT_ORDER	24
#define HPT_NPTEG	(1ul << (HPT_ORDER - 7))	/* 128B per pteg */
#define HPT_HASH_MASK	(HPT_NPTEG - 1)

#define HPTE_V_HVLOCK	0x40UL

static inline long lock_hpte(unsigned long *hpte, unsigned long bits)
{
	unsigned long tmp, old;

	asm volatile("	ldarx	%0,0,%2\n"
		     "	and.	%1,%0,%3\n"
		     "	bne	2f\n"
		     "	ori	%0,%0,%4\n"
		     "  stdcx.	%0,0,%2\n"
		     "	beq+	2f\n"
		     "	li	%1,%3\n"
		     "2:	isync"
		     : "=&r" (tmp), "=&r" (old)
		     : "r" (hpte), "r" (bits), "i" (HPTE_V_HVLOCK)
		     : "cc", "memory");
	return old == 0;
}

long kvmppc_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
		    long pte_index, unsigned long pteh, unsigned long ptel)
{
	unsigned long porder;
	struct kvm *kvm = vcpu->kvm;
	unsigned long i, lpn, pa;
	unsigned long *hpte;

	/* only handle 4k, 64k and 16M pages for now */
	porder = 12;
	if (pteh & HPTE_V_LARGE) {
		if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		    (ptel & 0xf000) == 0x1000) {
			/* 64k page */
			porder = 16;
		} else if ((ptel & 0xff000) == 0) {
			/* 16M page */
			porder = 24;
			/* lowest AVA bit must be 0 for 16M pages */
			if (pteh & 0x80)
				return H_PARAMETER;
		} else
			return H_PARAMETER;
	}
	lpn = (ptel & HPTE_R_RPN) >> kvm->arch.ram_porder;
	if (lpn >= kvm->arch.ram_npages || porder > kvm->arch.ram_porder)
		return H_PARAMETER;
	pa = kvm->arch.ram_pginfo[lpn].pfn << PAGE_SHIFT;
	if (!pa)
		return H_PARAMETER;
	/* Check WIMG */
	if ((ptel & HPTE_R_WIMG) != HPTE_R_M &&
	    (ptel & HPTE_R_WIMG) != (HPTE_R_W | HPTE_R_I | HPTE_R_M))
		return H_PARAMETER;
	pteh &= ~0x60UL;
	ptel &= ~(HPTE_R_PP0 - kvm->arch.ram_psize);
	ptel |= pa;
	if (pte_index >= (HPT_NPTEG << 3))
		return H_PARAMETER;
	if (likely((flags & H_EXACT) == 0)) {
		pte_index &= ~7UL;
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		for (i = 0; ; ++i) {
			if (i == 8)
				return H_PTEG_FULL;
			if ((*hpte & HPTE_V_VALID) == 0 &&
			    lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID))
				break;
			hpte += 2;
		}
	} else {
		i = 0;
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		if (!lock_hpte(hpte, HPTE_V_HVLOCK | HPTE_V_VALID))
			return H_PTEG_FULL;
	}
	hpte[1] = ptel;
	eieio();
	hpte[0] = pteh;
	asm volatile("ptesync" : : : "memory");
	atomic_inc(&kvm->arch.ram_pginfo[lpn].refcnt);
	vcpu->arch.gpr[4] = pte_index + i;
	return H_SUCCESS;
}

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

	if (pte_index >= (HPT_NPTEG << 3))
		return H_PARAMETER;
	hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
	while (!lock_hpte(hpte, HPTE_V_HVLOCK))
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
		    pte_index >= (HPT_NPTEG << 3)) {
			/* parameter error */
			args[i * 2] = ((0xa0 | flags) << 56) + pte_index;
			ret = H_PARAMETER;
			break;
		}
		hp = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		while (!lock_hpte(hp, HPTE_V_HVLOCK))
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
	unsigned long v, r, rb;

	if (pte_index >= (HPT_NPTEG << 3))
		return H_PARAMETER;
	hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
	while (!lock_hpte(hpte, HPTE_V_HVLOCK))
		cpu_relax();
	if ((hpte[0] & HPTE_V_VALID) == 0 ||
	    ((flags & H_AVPN) && (hpte[0] & ~0x7fUL) != avpn)) {
		hpte[0] &= ~HPTE_V_HVLOCK;
		return H_NOT_FOUND;
	}
	if (atomic_read(&kvm->online_vcpus) == 1)
		flags |= H_LOCAL;
	v = hpte[0];
	r = hpte[1] & ~(HPTE_R_PP0 | HPTE_R_PP | HPTE_R_N |
			HPTE_R_KEY_HI | HPTE_R_KEY_LO);
	r |= (flags << 55) & HPTE_R_PP0;
	r |= (flags << 48) & HPTE_R_KEY_HI;
	r |= flags & (HPTE_R_PP | HPTE_R_N | HPTE_R_KEY_LO);
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

static unsigned long reverse_xlate(struct kvm *kvm, unsigned long realaddr)
{
	long int i;
	unsigned long offset, rpn;

	offset = realaddr & (kvm->arch.ram_psize - 1);
	rpn = (realaddr - offset) >> PAGE_SHIFT;
	for (i = 0; i < kvm->arch.ram_npages; ++i)
		if (rpn == kvm->arch.ram_pginfo[i].pfn)
			return (i << PAGE_SHIFT) + offset;
	return HPTE_R_RPN;	/* all 1s in the RPN field */
}

long kvmppc_h_read(struct kvm_vcpu *vcpu, unsigned long flags,
		   unsigned long pte_index)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long *hpte, r;
	int i, n = 1;

	if (pte_index >= (HPT_NPTEG << 3))
		return H_PARAMETER;
	if (flags & H_READ_4) {
		pte_index &= ~3;
		n = 4;
	}
	for (i = 0; i < n; ++i, ++pte_index) {
		hpte = (unsigned long *)(kvm->arch.hpt_virt + (pte_index << 4));
		r = hpte[1];
		if ((flags & H_R_XLATE) && (hpte[0] & HPTE_V_VALID))
			r = reverse_xlate(kvm, r & HPTE_R_RPN) |
				(r & ~HPTE_R_RPN);
		vcpu->arch.gpr[4 + i * 2] = hpte[0];
		vcpu->arch.gpr[5 + i * 2] = r;
	}
	return H_SUCCESS;
}
