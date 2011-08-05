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

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu-hash64.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/cputable.h>

/* For now use fixed-size 16MB page table */
#define HPT_ORDER	24
#define HPT_NPTEG	(1ul << (HPT_ORDER - 7))	/* 128B per pteg */
#define HPT_HASH_MASK	(HPT_NPTEG - 1)

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

	hpt = __get_free_pages(GFP_KERNEL|__GFP_ZERO|__GFP_REPEAT|__GFP_NOWARN,
			       HPT_ORDER - PAGE_SHIFT);
	if (!hpt) {
		pr_err("kvm_alloc_hpt: Couldn't alloc HPT\n");
		return -ENOMEM;
	}
	kvm->arch.hpt_virt = hpt;

	do {
		lpid = find_first_zero_bit(lpid_inuse, NR_LPIDS);
		if (lpid >= NR_LPIDS) {
			pr_err("kvm_alloc_hpt: No LPIDs free\n");
			free_pages(hpt, HPT_ORDER - PAGE_SHIFT);
			return -ENOMEM;
		}
	} while (test_and_set_bit(lpid, lpid_inuse));

	kvm->arch.sdr1 = __pa(hpt) | (HPT_ORDER - 18);
	kvm->arch.lpid = lpid;

	pr_info("KVM guest htab at %lx, LPID %lx\n", hpt, lpid);
	return 0;
}

void kvmppc_free_hpt(struct kvm *kvm)
{
	clear_bit(kvm->arch.lpid, lpid_inuse);
	free_pages(kvm->arch.hpt_virt, HPT_ORDER - PAGE_SHIFT);
}

void kvmppc_map_vrma(struct kvm *kvm, struct kvm_userspace_memory_region *mem)
{
	unsigned long i;
	unsigned long npages = kvm->arch.ram_npages;
	unsigned long pfn;
	unsigned long *hpte;
	unsigned long hash;
	struct kvmppc_pginfo *pginfo = kvm->arch.ram_pginfo;

	if (!pginfo)
		return;

	/* VRMA can't be > 1TB */
	if (npages > 1ul << (40 - kvm->arch.ram_porder))
		npages = 1ul << (40 - kvm->arch.ram_porder);
	/* Can't use more than 1 HPTE per HPTEG */
	if (npages > HPT_NPTEG)
		npages = HPT_NPTEG;

	for (i = 0; i < npages; ++i) {
		pfn = pginfo[i].pfn;
		if (!pfn)
			break;
		/* can't use hpt_hash since va > 64 bits */
		hash = (i ^ (VRMA_VSID ^ (VRMA_VSID << 25))) & HPT_HASH_MASK;
		/*
		 * We assume that the hash table is empty and no
		 * vcpus are using it at this stage.  Since we create
		 * at most one HPTE per HPTEG, we just assume entry 7
		 * is available and use it.
		 */
		hpte = (unsigned long *) (kvm->arch.hpt_virt + (hash << 7));
		hpte += 7 * 2;
		/* HPTE low word - RPN, protection, etc. */
		hpte[1] = (pfn << PAGE_SHIFT) | HPTE_R_R | HPTE_R_C |
			HPTE_R_M | PP_RWXX;
		wmb();
		hpte[0] = HPTE_V_1TB_SEG | (VRMA_VSID << (40 - 16)) |
			(i << (VRMA_PAGE_ORDER - 16)) | HPTE_V_BOLTED |
			HPTE_V_LARGE | HPTE_V_VALID;
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

static int kvmppc_mmu_book3s_64_hv_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
				struct kvmppc_pte *gpte, bool data)
{
	return -ENOENT;
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
