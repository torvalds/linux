/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright 2010 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/cputable.h>

/* Pages in the VRMA are 16MB pages */
#define VRMA_PAGE_ORDER	24
#define VRMA_VSID	0x1ffffffUL	/* 1TB VSID reserved for VRMA */

/* POWER7 has 10-bit LPIDs, PPC970 has 6-bit LPIDs */
#define MAX_LPID_970	63
#define NR_LPIDS	(LPID_RSVD + 1)
unsigned long lpid_inuse[BITS_TO_LONGS(NR_LPIDS)];

long kvmppc_alloc_hpt(struct kvm *kvm)
{
	unsigned long hpt;
	unsigned long lpid;
	struct revmap_entry *rev;

	/* Allocate guest's hashed page table */
	hpt = __get_free_pages(GFP_KERNEL|__GFP_ZERO|__GFP_REPEAT|__GFP_NOWARN,
			       HPT_ORDER - PAGE_SHIFT);
	if (!hpt) {
		pr_err("kvm_alloc_hpt: Couldn't alloc HPT\n");
		return -ENOMEM;
	}
	kvm->arch.hpt_virt = hpt;

	/* Allocate reverse map array */
	rev = vmalloc(sizeof(struct revmap_entry) * HPT_NPTE);
	if (!rev) {
		pr_err("kvmppc_alloc_hpt: Couldn't alloc reverse map array\n");
		goto out_freehpt;
	}
	kvm->arch.revmap = rev;

	/* Allocate the guest's logical partition ID */
	do {
		lpid = find_first_zero_bit(lpid_inuse, NR_LPIDS);
		if (lpid >= NR_LPIDS) {
			pr_err("kvm_alloc_hpt: No LPIDs free\n");
			goto out_freeboth;
		}
	} while (test_and_set_bit(lpid, lpid_inuse));

	kvm->arch.sdr1 = __pa(hpt) | (HPT_ORDER - 18);
	kvm->arch.lpid = lpid;

	pr_info("KVM guest htab at %lx, LPID %lx\n", hpt, lpid);
	return 0;

 out_freeboth:
	vfree(rev);
 out_freehpt:
	free_pages(hpt, HPT_ORDER - PAGE_SHIFT);
	return -ENOMEM;
}

void kvmppc_free_hpt(struct kvm *kvm)
{
	clear_bit(kvm->arch.lpid, lpid_inuse);
	vfree(kvm->arch.revmap);
	free_pages(kvm->arch.hpt_virt, HPT_ORDER - PAGE_SHIFT);
}

void kvmppc_map_vrma(struct kvm_vcpu *vcpu, struct kvm_memory_slot *memslot)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long i;
	unsigned long npages;
	unsigned long hp_v, hp_r;
	unsigned long addr, hash;
	unsigned long porder = kvm->arch.ram_porder;
	long ret;

	npages = kvm->arch.slot_npages[memslot->id];

	/* VRMA can't be > 1TB */
	if (npages > 1ul << (40 - porder))
		npages = 1ul << (40 - porder);
	/* Can't use more than 1 HPTE per HPTEG */
	if (npages > HPT_NPTEG)
		npages = HPT_NPTEG;

	for (i = 0; i < npages; ++i) {
		addr = i << porder;
		/* can't use hpt_hash since va > 64 bits */
		hash = (i ^ (VRMA_VSID ^ (VRMA_VSID << 25))) & HPT_HASH_MASK;
		/*
		 * We assume that the hash table is empty and no
		 * vcpus are using it at this stage.  Since we create
		 * at most one HPTE per HPTEG, we just assume entry 7
		 * is available and use it.
		 */
		hash = (hash << 3) + 7;
		hp_v = HPTE_V_1TB_SEG | (VRMA_VSID << (40 - 16)) |
			(i << (VRMA_PAGE_ORDER - 16)) | HPTE_V_BOLTED |
			HPTE_V_LARGE | HPTE_V_VALID;
		hp_r = addr | HPTE_R_R | HPTE_R_C | HPTE_R_M | PP_RWXX;
		ret = kvmppc_virtmode_h_enter(vcpu, H_EXACT, hash, hp_v, hp_r);
		if (ret != H_SUCCESS) {
			pr_err("KVM: map_vrma at %lx failed, ret=%ld\n",
			       addr, ret);
			break;
		}
	}
}

int kvmppc_mmu_hv_init(void)
{
	unsigned long host_lpid, rsvd_lpid;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return -EINVAL;

	memset(lpid_inuse, 0, sizeof(lpid_inuse));

	if (cpu_has_feature(CPU_FTR_ARCH_206)) {
		host_lpid = mfspr(SPRN_LPID);	/* POWER7 */
		rsvd_lpid = LPID_RSVD;
	} else {
		host_lpid = 0;			/* PPC970 */
		rsvd_lpid = MAX_LPID_970;
	}

	set_bit(host_lpid, lpid_inuse);
	/* rsvd_lpid is reserved for use in partition switching */
	set_bit(rsvd_lpid, lpid_inuse);

	return 0;
}

void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu)
{
}

static void kvmppc_mmu_book3s_64_hv_reset_msr(struct kvm_vcpu *vcpu)
{
	kvmppc_set_msr(vcpu, MSR_SF | MSR_ME);
}

/*
 * This is called to get a reference to a guest page if there isn't
 * one already in the kvm->arch.slot_phys[][] arrays.
 */
static long kvmppc_get_guest_page(struct kvm *kvm, unsigned long gfn,
				  struct kvm_memory_slot *memslot)
{
	unsigned long start;
	long np;
	struct page *page, *pages[1];
	unsigned long *physp;
	unsigned long pfn, i;

	physp = kvm->arch.slot_phys[memslot->id];
	if (!physp)
		return -EINVAL;
	i = (gfn - memslot->base_gfn) >> (kvm->arch.ram_porder - PAGE_SHIFT);
	if (physp[i])
		return 0;

	page = NULL;
	start = gfn_to_hva_memslot(memslot, gfn);

	/* Instantiate and get the page we want access to */
	np = get_user_pages_fast(start, 1, 1, pages);
	if (np != 1)
		return -EINVAL;
	page = pages[0];

	/* Check it's a 16MB page */
	if (!PageHead(page) ||
	    compound_order(page) != (kvm->arch.ram_porder - PAGE_SHIFT)) {
		pr_err("page at %lx isn't 16MB (o=%d)\n",
		       start, compound_order(page));
		put_page(page);
		return -EINVAL;
	}
	pfn = page_to_pfn(page);

	spin_lock(&kvm->arch.slot_phys_lock);
	if (!physp[i])
		physp[i] = (pfn << PAGE_SHIFT) | KVMPPC_GOT_PAGE;
	else
		put_page(page);
	spin_unlock(&kvm->arch.slot_phys_lock);

	return 0;
}

/*
 * We come here on a H_ENTER call from the guest when
 * we don't have the requested page pinned already.
 */
long kvmppc_virtmode_h_enter(struct kvm_vcpu *vcpu, unsigned long flags,
			long pte_index, unsigned long pteh, unsigned long ptel)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long psize, gpa, gfn;
	struct kvm_memory_slot *memslot;
	long ret;

	psize = hpte_page_size(pteh, ptel);
	if (!psize)
		return H_PARAMETER;

	/* Find the memslot (if any) for this address */
	gpa = (ptel & HPTE_R_RPN) & ~(psize - 1);
	gfn = gpa >> PAGE_SHIFT;
	memslot = gfn_to_memslot(kvm, gfn);
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		return H_PARAMETER;
	if (kvmppc_get_guest_page(kvm, gfn, memslot) < 0)
		return H_PARAMETER;

	preempt_disable();
	ret = kvmppc_h_enter(vcpu, flags, pte_index, pteh, ptel);
	preempt_enable();
	if (ret == H_TOO_HARD) {
		/* this can't happen */
		pr_err("KVM: Oops, kvmppc_h_enter returned too hard!\n");
		ret = H_RESOURCE;	/* or something */
	}
	return ret;

}

static int kvmppc_mmu_book3s_64_hv_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				struct kvmppc_pte *gpte, bool data)
{
	return -ENOENT;
}

void *kvmppc_pin_guest_page(struct kvm *kvm, unsigned long gpa,
			    unsigned long *nb_ret)
{
	struct kvm_memory_slot *memslot;
	unsigned long gfn = gpa >> PAGE_SHIFT;
	struct page *page;
	unsigned long offset;
	unsigned long pfn, pa;
	unsigned long *physp;

	memslot = gfn_to_memslot(kvm, gfn);
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID))
		return NULL;
	physp = kvm->arch.slot_phys[memslot->id];
	if (!physp)
		return NULL;
	physp += (gfn - memslot->base_gfn) >>
		(kvm->arch.ram_porder - PAGE_SHIFT);
	pa = *physp;
	if (!pa) {
		if (kvmppc_get_guest_page(kvm, gfn, memslot) < 0)
			return NULL;
		pa = *physp;
	}
	pfn = pa >> PAGE_SHIFT;
	page = pfn_to_page(pfn);
	get_page(page);
	offset = gpa & (kvm->arch.ram_psize - 1);
	if (nb_ret)
		*nb_ret = kvm->arch.ram_psize - offset;
	return page_address(page) + offset;
}

void kvmppc_unpin_guest_page(struct kvm *kvm, void *va)
{
	struct page *page = virt_to_page(va);

	page = compound_head(page);
	put_page(page);
}

void kvmppc_mmu_book3s_hv_init(struct kvm_vcpu *vcpu)
{
	struct kvmppc_mmu *mmu = &vcpu->arch.mmu;

	if (cpu_has_feature(CPU_FTR_ARCH_206))
		vcpu->arch.slb_nr = 32;		/* POWER7 */
	else
		vcpu->arch.slb_nr = 64;

	mmu->xlate = kvmppc_mmu_book3s_64_hv_xlate;
	mmu->reset_msr = kvmppc_mmu_book3s_64_hv_reset_msr;

	vcpu->arch.hflags |= BOOK3S_HFLAG_SLB;
}
