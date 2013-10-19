/*
 * Copyright (C) 2008-2013 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, yu.liu@freescale.com
 *         Scott Wood, scottwood@freescale.com
 *         Ashish Kalra, ashish.kalra@freescale.com
 *         Varun Sethi, varun.sethi@freescale.com
 *         Alexander Graf, agraf@suse.de
 *
 * Description:
 * This file is based on arch/powerpc/kvm/44x_tlb.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <linux/hugetlb.h>
#include <asm/kvm_ppc.h>

#include "e500.h"
#include "trace.h"
#include "timing.h"
#include "e500_mmu_host.h"

#define to_htlb1_esel(esel) (host_tlb_params[1].entries - (esel) - 1)

static struct kvmppc_e500_tlb_params host_tlb_params[E500_TLB_NUM];

static inline unsigned int tlb1_max_shadow_size(void)
{
	/* reserve one entry for magic page */
	return host_tlb_params[1].entries - tlbcam_index - 1;
}

static inline u32 e500_shadow_mas3_attrib(u32 mas3, int usermode)
{
	/* Mask off reserved bits. */
	mas3 &= MAS3_ATTRIB_MASK;

#ifndef CONFIG_KVM_BOOKE_HV
	if (!usermode) {
		/* Guest is in supervisor mode,
		 * so we need to translate guest
		 * supervisor permissions into user permissions. */
		mas3 &= ~E500_TLB_USER_PERM_MASK;
		mas3 |= (mas3 & E500_TLB_SUPER_PERM_MASK) << 1;
	}
	mas3 |= E500_TLB_SUPER_PERM_MASK;
#endif
	return mas3;
}

static inline u32 e500_shadow_mas2_attrib(u32 mas2, int usermode)
{
#ifdef CONFIG_SMP
	return (mas2 & MAS2_ATTRIB_MASK) | MAS2_M;
#else
	return mas2 & MAS2_ATTRIB_MASK;
#endif
}

/*
 * writing shadow tlb entry to host TLB
 */
static inline void __write_host_tlbe(struct kvm_book3e_206_tlb_entry *stlbe,
				     uint32_t mas0)
{
	unsigned long flags;

	local_irq_save(flags);
	mtspr(SPRN_MAS0, mas0);
	mtspr(SPRN_MAS1, stlbe->mas1);
	mtspr(SPRN_MAS2, (unsigned long)stlbe->mas2);
	mtspr(SPRN_MAS3, (u32)stlbe->mas7_3);
	mtspr(SPRN_MAS7, (u32)(stlbe->mas7_3 >> 32));
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_MAS8, stlbe->mas8);
#endif
	asm volatile("isync; tlbwe" : : : "memory");

#ifdef CONFIG_KVM_BOOKE_HV
	/* Must clear mas8 for other host tlbwe's */
	mtspr(SPRN_MAS8, 0);
	isync();
#endif
	local_irq_restore(flags);

	trace_kvm_booke206_stlb_write(mas0, stlbe->mas8, stlbe->mas1,
	                              stlbe->mas2, stlbe->mas7_3);
}

/*
 * Acquire a mas0 with victim hint, as if we just took a TLB miss.
 *
 * We don't care about the address we're searching for, other than that it's
 * in the right set and is not present in the TLB.  Using a zero PID and a
 * userspace address means we don't have to set and then restore MAS5, or
 * calculate a proper MAS6 value.
 */
static u32 get_host_mas0(unsigned long eaddr)
{
	unsigned long flags;
	u32 mas0;

	local_irq_save(flags);
	mtspr(SPRN_MAS6, 0);
	asm volatile("tlbsx 0, %0" : : "b" (eaddr & ~CONFIG_PAGE_OFFSET));
	mas0 = mfspr(SPRN_MAS0);
	local_irq_restore(flags);

	return mas0;
}

/* sesel is for tlb1 only */
static inline void write_host_tlbe(struct kvmppc_vcpu_e500 *vcpu_e500,
		int tlbsel, int sesel, struct kvm_book3e_206_tlb_entry *stlbe)
{
	u32 mas0;

	if (tlbsel == 0) {
		mas0 = get_host_mas0(stlbe->mas2);
		__write_host_tlbe(stlbe, mas0);
	} else {
		__write_host_tlbe(stlbe,
				  MAS0_TLBSEL(1) |
				  MAS0_ESEL(to_htlb1_esel(sesel)));
	}
}

/* sesel is for tlb1 only */
static void write_stlbe(struct kvmppc_vcpu_e500 *vcpu_e500,
			struct kvm_book3e_206_tlb_entry *gtlbe,
			struct kvm_book3e_206_tlb_entry *stlbe,
			int stlbsel, int sesel)
{
	int stid;

	preempt_disable();
	stid = kvmppc_e500_get_tlb_stid(&vcpu_e500->vcpu, gtlbe);

	stlbe->mas1 |= MAS1_TID(stid);
	write_host_tlbe(vcpu_e500, stlbsel, sesel, stlbe);
	preempt_enable();
}

#ifdef CONFIG_KVM_E500V2
/* XXX should be a hook in the gva2hpa translation */
void kvmppc_map_magic(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct kvm_book3e_206_tlb_entry magic;
	ulong shared_page = ((ulong)vcpu->arch.shared) & PAGE_MASK;
	unsigned int stid;
	pfn_t pfn;

	pfn = (pfn_t)virt_to_phys((void *)shared_page) >> PAGE_SHIFT;
	get_page(pfn_to_page(pfn));

	preempt_disable();
	stid = kvmppc_e500_get_sid(vcpu_e500, 0, 0, 0, 0);

	magic.mas1 = MAS1_VALID | MAS1_TS | MAS1_TID(stid) |
		     MAS1_TSIZE(BOOK3E_PAGESZ_4K);
	magic.mas2 = vcpu->arch.magic_page_ea | MAS2_M;
	magic.mas7_3 = ((u64)pfn << PAGE_SHIFT) |
		       MAS3_SW | MAS3_SR | MAS3_UW | MAS3_UR;
	magic.mas8 = 0;

	__write_host_tlbe(&magic, MAS0_TLBSEL(1) | MAS0_ESEL(tlbcam_index));
	preempt_enable();
}
#endif

void inval_gtlbe_on_host(struct kvmppc_vcpu_e500 *vcpu_e500, int tlbsel,
			 int esel)
{
	struct kvm_book3e_206_tlb_entry *gtlbe =
		get_entry(vcpu_e500, tlbsel, esel);
	struct tlbe_ref *ref = &vcpu_e500->gtlb_priv[tlbsel][esel].ref;

	/* Don't bother with unmapped entries */
	if (!(ref->flags & E500_TLB_VALID)) {
		WARN(ref->flags & (E500_TLB_BITMAP | E500_TLB_TLB0),
		     "%s: flags %x\n", __func__, ref->flags);
		WARN_ON(tlbsel == 1 && vcpu_e500->g2h_tlb1_map[esel]);
	}

	if (tlbsel == 1 && ref->flags & E500_TLB_BITMAP) {
		u64 tmp = vcpu_e500->g2h_tlb1_map[esel];
		int hw_tlb_indx;
		unsigned long flags;

		local_irq_save(flags);
		while (tmp) {
			hw_tlb_indx = __ilog2_u64(tmp & -tmp);
			mtspr(SPRN_MAS0,
			      MAS0_TLBSEL(1) |
			      MAS0_ESEL(to_htlb1_esel(hw_tlb_indx)));
			mtspr(SPRN_MAS1, 0);
			asm volatile("tlbwe");
			vcpu_e500->h2g_tlb1_rmap[hw_tlb_indx] = 0;
			tmp &= tmp - 1;
		}
		mb();
		vcpu_e500->g2h_tlb1_map[esel] = 0;
		ref->flags &= ~(E500_TLB_BITMAP | E500_TLB_VALID);
		local_irq_restore(flags);
	}

	if (tlbsel == 1 && ref->flags & E500_TLB_TLB0) {
		/*
		 * TLB1 entry is backed by 4k pages. This should happen
		 * rarely and is not worth optimizing. Invalidate everything.
		 */
		kvmppc_e500_tlbil_all(vcpu_e500);
		ref->flags &= ~(E500_TLB_TLB0 | E500_TLB_VALID);
	}

	/* Already invalidated in between */
	if (!(ref->flags & E500_TLB_VALID))
		return;

	/* Guest tlbe is backed by at most one host tlbe per shadow pid. */
	kvmppc_e500_tlbil_one(vcpu_e500, gtlbe);

	/* Mark the TLB as not backed by the host anymore */
	ref->flags &= ~E500_TLB_VALID;
}

static inline int tlbe_is_writable(struct kvm_book3e_206_tlb_entry *tlbe)
{
	return tlbe->mas7_3 & (MAS3_SW|MAS3_UW);
}

static inline void kvmppc_e500_ref_setup(struct tlbe_ref *ref,
					 struct kvm_book3e_206_tlb_entry *gtlbe,
					 pfn_t pfn)
{
	ref->pfn = pfn;
	ref->flags |= E500_TLB_VALID;

	if (tlbe_is_writable(gtlbe))
		kvm_set_pfn_dirty(pfn);
}

static inline void kvmppc_e500_ref_release(struct tlbe_ref *ref)
{
	if (ref->flags & E500_TLB_VALID) {
		/* FIXME: don't log bogus pfn for TLB1 */
		trace_kvm_booke206_ref_release(ref->pfn, ref->flags);
		ref->flags = 0;
	}
}

static void clear_tlb1_bitmap(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	if (vcpu_e500->g2h_tlb1_map)
		memset(vcpu_e500->g2h_tlb1_map, 0,
		       sizeof(u64) * vcpu_e500->gtlb_params[1].entries);
	if (vcpu_e500->h2g_tlb1_rmap)
		memset(vcpu_e500->h2g_tlb1_rmap, 0,
		       sizeof(unsigned int) * host_tlb_params[1].entries);
}

static void clear_tlb_privs(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	int tlbsel;
	int i;

	for (tlbsel = 0; tlbsel <= 1; tlbsel++) {
		for (i = 0; i < vcpu_e500->gtlb_params[tlbsel].entries; i++) {
			struct tlbe_ref *ref =
				&vcpu_e500->gtlb_priv[tlbsel][i].ref;
			kvmppc_e500_ref_release(ref);
		}
	}
}

void kvmppc_core_flush_tlb(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	kvmppc_e500_tlbil_all(vcpu_e500);
	clear_tlb_privs(vcpu_e500);
	clear_tlb1_bitmap(vcpu_e500);
}

/* TID must be supplied by the caller */
static void kvmppc_e500_setup_stlbe(
	struct kvm_vcpu *vcpu,
	struct kvm_book3e_206_tlb_entry *gtlbe,
	int tsize, struct tlbe_ref *ref, u64 gvaddr,
	struct kvm_book3e_206_tlb_entry *stlbe)
{
	pfn_t pfn = ref->pfn;
	u32 pr = vcpu->arch.shared->msr & MSR_PR;

	BUG_ON(!(ref->flags & E500_TLB_VALID));

	/* Force IPROT=0 for all guest mappings. */
	stlbe->mas1 = MAS1_TSIZE(tsize) | get_tlb_sts(gtlbe) | MAS1_VALID;
	stlbe->mas2 = (gvaddr & MAS2_EPN) |
		      e500_shadow_mas2_attrib(gtlbe->mas2, pr);
	stlbe->mas7_3 = ((u64)pfn << PAGE_SHIFT) |
			e500_shadow_mas3_attrib(gtlbe->mas7_3, pr);

#ifdef CONFIG_KVM_BOOKE_HV
	stlbe->mas8 = MAS8_TGS | vcpu->kvm->arch.lpid;
#endif
}

static inline int kvmppc_e500_shadow_map(struct kvmppc_vcpu_e500 *vcpu_e500,
	u64 gvaddr, gfn_t gfn, struct kvm_book3e_206_tlb_entry *gtlbe,
	int tlbsel, struct kvm_book3e_206_tlb_entry *stlbe,
	struct tlbe_ref *ref)
{
	struct kvm_memory_slot *slot;
	unsigned long pfn = 0; /* silence GCC warning */
	unsigned long hva;
	int pfnmap = 0;
	int tsize = BOOK3E_PAGESZ_4K;
	int ret = 0;
	unsigned long mmu_seq;
	struct kvm *kvm = vcpu_e500->vcpu.kvm;

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	/*
	 * Translate guest physical to true physical, acquiring
	 * a page reference if it is normal, non-reserved memory.
	 *
	 * gfn_to_memslot() must succeed because otherwise we wouldn't
	 * have gotten this far.  Eventually we should just pass the slot
	 * pointer through from the first lookup.
	 */
	slot = gfn_to_memslot(vcpu_e500->vcpu.kvm, gfn);
	hva = gfn_to_hva_memslot(slot, gfn);

	if (tlbsel == 1) {
		struct vm_area_struct *vma;
		down_read(&current->mm->mmap_sem);

		vma = find_vma(current->mm, hva);
		if (vma && hva >= vma->vm_start &&
		    (vma->vm_flags & VM_PFNMAP)) {
			/*
			 * This VMA is a physically contiguous region (e.g.
			 * /dev/mem) that bypasses normal Linux page
			 * management.  Find the overlap between the
			 * vma and the memslot.
			 */

			unsigned long start, end;
			unsigned long slot_start, slot_end;

			pfnmap = 1;

			start = vma->vm_pgoff;
			end = start +
			      ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT);

			pfn = start + ((hva - vma->vm_start) >> PAGE_SHIFT);

			slot_start = pfn - (gfn - slot->base_gfn);
			slot_end = slot_start + slot->npages;

			if (start < slot_start)
				start = slot_start;
			if (end > slot_end)
				end = slot_end;

			tsize = (gtlbe->mas1 & MAS1_TSIZE_MASK) >>
				MAS1_TSIZE_SHIFT;

			/*
			 * e500 doesn't implement the lowest tsize bit,
			 * or 1K pages.
			 */
			tsize = max(BOOK3E_PAGESZ_4K, tsize & ~1);

			/*
			 * Now find the largest tsize (up to what the guest
			 * requested) that will cover gfn, stay within the
			 * range, and for which gfn and pfn are mutually
			 * aligned.
			 */

			for (; tsize > BOOK3E_PAGESZ_4K; tsize -= 2) {
				unsigned long gfn_start, gfn_end, tsize_pages;
				tsize_pages = 1 << (tsize - 2);

				gfn_start = gfn & ~(tsize_pages - 1);
				gfn_end = gfn_start + tsize_pages;

				if (gfn_start + pfn - gfn < start)
					continue;
				if (gfn_end + pfn - gfn > end)
					continue;
				if ((gfn & (tsize_pages - 1)) !=
				    (pfn & (tsize_pages - 1)))
					continue;

				gvaddr &= ~((tsize_pages << PAGE_SHIFT) - 1);
				pfn &= ~(tsize_pages - 1);
				break;
			}
		} else if (vma && hva >= vma->vm_start &&
			   (vma->vm_flags & VM_HUGETLB)) {
			unsigned long psize = vma_kernel_pagesize(vma);

			tsize = (gtlbe->mas1 & MAS1_TSIZE_MASK) >>
				MAS1_TSIZE_SHIFT;

			/*
			 * Take the largest page size that satisfies both host
			 * and guest mapping
			 */
			tsize = min(__ilog2(psize) - 10, tsize);

			/*
			 * e500 doesn't implement the lowest tsize bit,
			 * or 1K pages.
			 */
			tsize = max(BOOK3E_PAGESZ_4K, tsize & ~1);
		}

		up_read(&current->mm->mmap_sem);
	}

	if (likely(!pfnmap)) {
		unsigned long tsize_pages = 1 << (tsize + 10 - PAGE_SHIFT);
		pfn = gfn_to_pfn_memslot(slot, gfn);
		if (is_error_noslot_pfn(pfn)) {
			printk(KERN_ERR "Couldn't get real page for gfn %lx!\n",
					(long)gfn);
			return -EINVAL;
		}

		/* Align guest and physical address to page map boundaries */
		pfn &= ~(tsize_pages - 1);
		gvaddr &= ~((tsize_pages << PAGE_SHIFT) - 1);
	}

	spin_lock(&kvm->mmu_lock);
	if (mmu_notifier_retry(kvm, mmu_seq)) {
		ret = -EAGAIN;
		goto out;
	}

	kvmppc_e500_ref_setup(ref, gtlbe, pfn);

	kvmppc_e500_setup_stlbe(&vcpu_e500->vcpu, gtlbe, tsize,
				ref, gvaddr, stlbe);

	/* Clear i-cache for new pages */
	kvmppc_mmu_flush_icache(pfn);

out:
	spin_unlock(&kvm->mmu_lock);

	/* Drop refcount on page, so that mmu notifiers can clear it */
	kvm_release_pfn_clean(pfn);

	return ret;
}

/* XXX only map the one-one case, for now use TLB0 */
static int kvmppc_e500_tlb0_map(struct kvmppc_vcpu_e500 *vcpu_e500, int esel,
				struct kvm_book3e_206_tlb_entry *stlbe)
{
	struct kvm_book3e_206_tlb_entry *gtlbe;
	struct tlbe_ref *ref;
	int stlbsel = 0;
	int sesel = 0;
	int r;

	gtlbe = get_entry(vcpu_e500, 0, esel);
	ref = &vcpu_e500->gtlb_priv[0][esel].ref;

	r = kvmppc_e500_shadow_map(vcpu_e500, get_tlb_eaddr(gtlbe),
			get_tlb_raddr(gtlbe) >> PAGE_SHIFT,
			gtlbe, 0, stlbe, ref);
	if (r)
		return r;

	write_stlbe(vcpu_e500, gtlbe, stlbe, stlbsel, sesel);

	return 0;
}

static int kvmppc_e500_tlb1_map_tlb1(struct kvmppc_vcpu_e500 *vcpu_e500,
				     struct tlbe_ref *ref,
				     int esel)
{
	unsigned int sesel = vcpu_e500->host_tlb1_nv++;

	if (unlikely(vcpu_e500->host_tlb1_nv >= tlb1_max_shadow_size()))
		vcpu_e500->host_tlb1_nv = 0;

	if (vcpu_e500->h2g_tlb1_rmap[sesel]) {
		unsigned int idx = vcpu_e500->h2g_tlb1_rmap[sesel] - 1;
		vcpu_e500->g2h_tlb1_map[idx] &= ~(1ULL << sesel);
	}

	vcpu_e500->gtlb_priv[1][esel].ref.flags |= E500_TLB_BITMAP;
	vcpu_e500->g2h_tlb1_map[esel] |= (u64)1 << sesel;
	vcpu_e500->h2g_tlb1_rmap[sesel] = esel + 1;
	WARN_ON(!(ref->flags & E500_TLB_VALID));

	return sesel;
}

/* Caller must ensure that the specified guest TLB entry is safe to insert into
 * the shadow TLB. */
/* For both one-one and one-to-many */
static int kvmppc_e500_tlb1_map(struct kvmppc_vcpu_e500 *vcpu_e500,
		u64 gvaddr, gfn_t gfn, struct kvm_book3e_206_tlb_entry *gtlbe,
		struct kvm_book3e_206_tlb_entry *stlbe, int esel)
{
	struct tlbe_ref *ref = &vcpu_e500->gtlb_priv[1][esel].ref;
	int sesel;
	int r;

	r = kvmppc_e500_shadow_map(vcpu_e500, gvaddr, gfn, gtlbe, 1, stlbe,
				   ref);
	if (r)
		return r;

	/* Use TLB0 when we can only map a page with 4k */
	if (get_tlb_tsize(stlbe) == BOOK3E_PAGESZ_4K) {
		vcpu_e500->gtlb_priv[1][esel].ref.flags |= E500_TLB_TLB0;
		write_stlbe(vcpu_e500, gtlbe, stlbe, 0, 0);
		return 0;
	}

	/* Otherwise map into TLB1 */
	sesel = kvmppc_e500_tlb1_map_tlb1(vcpu_e500, ref, esel);
	write_stlbe(vcpu_e500, gtlbe, stlbe, 1, sesel);

	return 0;
}

void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 eaddr, gpa_t gpaddr,
		    unsigned int index)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);
	struct tlbe_priv *priv;
	struct kvm_book3e_206_tlb_entry *gtlbe, stlbe;
	int tlbsel = tlbsel_of(index);
	int esel = esel_of(index);

	gtlbe = get_entry(vcpu_e500, tlbsel, esel);

	switch (tlbsel) {
	case 0:
		priv = &vcpu_e500->gtlb_priv[tlbsel][esel];

		/* Triggers after clear_tlb_privs or on initial mapping */
		if (!(priv->ref.flags & E500_TLB_VALID)) {
			kvmppc_e500_tlb0_map(vcpu_e500, esel, &stlbe);
		} else {
			kvmppc_e500_setup_stlbe(vcpu, gtlbe, BOOK3E_PAGESZ_4K,
						&priv->ref, eaddr, &stlbe);
			write_stlbe(vcpu_e500, gtlbe, &stlbe, 0, 0);
		}
		break;

	case 1: {
		gfn_t gfn = gpaddr >> PAGE_SHIFT;
		kvmppc_e500_tlb1_map(vcpu_e500, eaddr, gfn, gtlbe, &stlbe,
				     esel);
		break;
	}

	default:
		BUG();
		break;
	}
}

/************* MMU Notifiers *************/

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	trace_kvm_unmap_hva(hva);

	/*
	 * Flush all shadow tlb entries everywhere. This is slow, but
	 * we are 100% sure that we catch the to be unmapped page
	 */
	kvm_flush_remote_tlbs(kvm);

	return 0;
}

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end)
{
	/* kvm_unmap_hva flushes everything anyways */
	kvm_unmap_hva(kvm, start);

	return 0;
}

int kvm_age_hva(struct kvm *kvm, unsigned long hva)
{
	/* XXX could be more clever ;) */
	return 0;
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	/* XXX could be more clever ;) */
	return 0;
}

void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	/* The page will get remapped properly on its next fault */
	kvm_unmap_hva(kvm, hva);
}

/*****************************************/

int e500_mmu_host_init(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	host_tlb_params[0].entries = mfspr(SPRN_TLB0CFG) & TLBnCFG_N_ENTRY;
	host_tlb_params[1].entries = mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY;

	/*
	 * This should never happen on real e500 hardware, but is
	 * architecturally possible -- e.g. in some weird nested
	 * virtualization case.
	 */
	if (host_tlb_params[0].entries == 0 ||
	    host_tlb_params[1].entries == 0) {
		pr_err("%s: need to know host tlb size\n", __func__);
		return -ENODEV;
	}

	host_tlb_params[0].ways = (mfspr(SPRN_TLB0CFG) & TLBnCFG_ASSOC) >>
				  TLBnCFG_ASSOC_SHIFT;
	host_tlb_params[1].ways = host_tlb_params[1].entries;

	if (!is_power_of_2(host_tlb_params[0].entries) ||
	    !is_power_of_2(host_tlb_params[0].ways) ||
	    host_tlb_params[0].entries < host_tlb_params[0].ways ||
	    host_tlb_params[0].ways == 0) {
		pr_err("%s: bad tlb0 host config: %u entries %u ways\n",
		       __func__, host_tlb_params[0].entries,
		       host_tlb_params[0].ways);
		return -ENODEV;
	}

	host_tlb_params[0].sets =
		host_tlb_params[0].entries / host_tlb_params[0].ways;
	host_tlb_params[1].sets = 1;

	vcpu_e500->h2g_tlb1_rmap = kzalloc(sizeof(unsigned int) *
					   host_tlb_params[1].entries,
					   GFP_KERNEL);
	if (!vcpu_e500->h2g_tlb1_rmap)
		return -EINVAL;

	return 0;
}

void e500_mmu_host_uninit(struct kvmppc_vcpu_e500 *vcpu_e500)
{
	kfree(vcpu_e500->h2g_tlb1_rmap);
}
