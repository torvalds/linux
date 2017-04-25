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
 * Copyright 2011 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 * Copyright 2016 Alexey Kardashevskiy, IBM Corporation <aik@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/list.h>

#include <asm/tlbflush.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/book3s/64/mmu-hash.h>
#include <asm/mmu_context.h>
#include <asm/hvcall.h>
#include <asm/synch.h>
#include <asm/ppc-opcode.h>
#include <asm/kvm_host.h>
#include <asm/udbg.h>
#include <asm/iommu.h>
#include <asm/tce.h>

#define TCES_PER_PAGE	(PAGE_SIZE / sizeof(u64))

/*
 * Finds a TCE table descriptor by LIOBN.
 *
 * WARNING: This will be called in real or virtual mode on HV KVM and virtual
 *          mode on PR KVM
 */
struct kvmppc_spapr_tce_table *kvmppc_find_table(struct kvm_vcpu *vcpu,
		unsigned long liobn)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvmppc_spapr_tce_table *stt;

	list_for_each_entry_lockless(stt, &kvm->arch.spapr_tce_tables, list)
		if (stt->liobn == liobn)
			return stt;

	return NULL;
}
EXPORT_SYMBOL_GPL(kvmppc_find_table);

/*
 * Validates IO address.
 *
 * WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
long kvmppc_ioba_validate(struct kvmppc_spapr_tce_table *stt,
		unsigned long ioba, unsigned long npages)
{
	unsigned long mask = (1ULL << stt->page_shift) - 1;
	unsigned long idx = ioba >> stt->page_shift;

	if ((ioba & mask) || (idx < stt->offset) ||
			(idx - stt->offset + npages > stt->size) ||
			(idx + npages < idx))
		return H_PARAMETER;

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_ioba_validate);

/*
 * Validates TCE address.
 * At the moment flags and page mask are validated.
 * As the host kernel does not access those addresses (just puts them
 * to the table and user space is supposed to process them), we can skip
 * checking other things (such as TCE is a guest RAM address or the page
 * was actually allocated).
 *
 * WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
long kvmppc_tce_validate(struct kvmppc_spapr_tce_table *stt, unsigned long tce)
{
	unsigned long page_mask = ~((1ULL << stt->page_shift) - 1);
	unsigned long mask = ~(page_mask | TCE_PCI_WRITE | TCE_PCI_READ);

	if (tce & mask)
		return H_PARAMETER;

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_tce_validate);

/* Note on the use of page_address() in real mode,
 *
 * It is safe to use page_address() in real mode on ppc64 because
 * page_address() is always defined as lowmem_page_address()
 * which returns __va(PFN_PHYS(page_to_pfn(page))) which is arithmetic
 * operation and does not access page struct.
 *
 * Theoretically page_address() could be defined different
 * but either WANT_PAGE_VIRTUAL or HASHED_PAGE_VIRTUAL
 * would have to be enabled.
 * WANT_PAGE_VIRTUAL is never enabled on ppc32/ppc64,
 * HASHED_PAGE_VIRTUAL could be enabled for ppc32 only and only
 * if CONFIG_HIGHMEM is defined. As CONFIG_SPARSEMEM_VMEMMAP
 * is not expected to be enabled on ppc32, page_address()
 * is safe for ppc32 as well.
 *
 * WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
static u64 *kvmppc_page_address(struct page *page)
{
#if defined(HASHED_PAGE_VIRTUAL) || defined(WANT_PAGE_VIRTUAL)
#error TODO: fix to avoid page_address() here
#endif
	return (u64 *) page_address(page);
}

/*
 * Handles TCE requests for emulated devices.
 * Puts guest TCE values to the table and expects user space to convert them.
 * Called in both real and virtual modes.
 * Cannot fail so kvmppc_tce_validate must be called before it.
 *
 * WARNING: This will be called in real-mode on HV KVM and virtual
 *          mode on PR KVM
 */
void kvmppc_tce_put(struct kvmppc_spapr_tce_table *stt,
		unsigned long idx, unsigned long tce)
{
	struct page *page;
	u64 *tbl;

	idx -= stt->offset;
	page = stt->pages[idx / TCES_PER_PAGE];
	tbl = kvmppc_page_address(page);

	tbl[idx % TCES_PER_PAGE] = tce;
}
EXPORT_SYMBOL_GPL(kvmppc_tce_put);

long kvmppc_gpa_to_ua(struct kvm *kvm, unsigned long gpa,
		unsigned long *ua, unsigned long **prmap)
{
	unsigned long gfn = gpa >> PAGE_SHIFT;
	struct kvm_memory_slot *memslot;

	memslot = search_memslots(kvm_memslots(kvm), gfn);
	if (!memslot)
		return -EINVAL;

	*ua = __gfn_to_hva_memslot(memslot, gfn) |
		(gpa & ~(PAGE_MASK | TCE_PCI_READ | TCE_PCI_WRITE));

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	if (prmap)
		*prmap = &memslot->arch.rmap[gfn - memslot->base_gfn];
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_gpa_to_ua);

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
long kvmppc_rm_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		unsigned long ioba, unsigned long tce)
{
	struct kvmppc_spapr_tce_table *stt = kvmppc_find_table(vcpu, liobn);
	long ret;

	/* udbg_printf("H_PUT_TCE(): liobn=0x%lx ioba=0x%lx, tce=0x%lx\n", */
	/* 	    liobn, ioba, tce); */

	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	ret = kvmppc_tce_validate(stt, tce);
	if (ret != H_SUCCESS)
		return ret;

	kvmppc_tce_put(stt, ioba >> stt->page_shift, tce);

	return H_SUCCESS;
}

static long kvmppc_rm_ua_to_hpa(struct kvm_vcpu *vcpu,
		unsigned long ua, unsigned long *phpa)
{
	pte_t *ptep, pte;
	unsigned shift = 0;

	ptep = __find_linux_pte_or_hugepte(vcpu->arch.pgdir, ua, NULL, &shift);
	if (!ptep || !pte_present(*ptep))
		return -ENXIO;
	pte = *ptep;

	if (!shift)
		shift = PAGE_SHIFT;

	/* Avoid handling anything potentially complicated in realmode */
	if (shift > PAGE_SHIFT)
		return -EAGAIN;

	if (!pte_young(pte))
		return -EAGAIN;

	*phpa = (pte_pfn(pte) << PAGE_SHIFT) | (ua & ((1ULL << shift) - 1)) |
			(ua & ~PAGE_MASK);

	return 0;
}

long kvmppc_rm_h_put_tce_indirect(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_list,	unsigned long npages)
{
	struct kvmppc_spapr_tce_table *stt;
	long i, ret = H_SUCCESS;
	unsigned long tces, entry, ua = 0;
	unsigned long *rmap = NULL;

	stt = kvmppc_find_table(vcpu, liobn);
	if (!stt)
		return H_TOO_HARD;

	entry = ioba >> stt->page_shift;
	/*
	 * The spec says that the maximum size of the list is 512 TCEs
	 * so the whole table addressed resides in 4K page
	 */
	if (npages > 512)
		return H_PARAMETER;

	if (tce_list & (SZ_4K - 1))
		return H_PARAMETER;

	ret = kvmppc_ioba_validate(stt, ioba, npages);
	if (ret != H_SUCCESS)
		return ret;

	if (kvmppc_gpa_to_ua(vcpu->kvm, tce_list, &ua, &rmap))
		return H_TOO_HARD;

	rmap = (void *) vmalloc_to_phys(rmap);

	/*
	 * Synchronize with the MMU notifier callbacks in
	 * book3s_64_mmu_hv.c (kvm_unmap_hva_hv etc.).
	 * While we have the rmap lock, code running on other CPUs
	 * cannot finish unmapping the host real page that backs
	 * this guest real page, so we are OK to access the host
	 * real page.
	 */
	lock_rmap(rmap);
	if (kvmppc_rm_ua_to_hpa(vcpu, ua, &tces)) {
		ret = H_TOO_HARD;
		goto unlock_exit;
	}

	for (i = 0; i < npages; ++i) {
		unsigned long tce = be64_to_cpu(((u64 *)tces)[i]);

		ret = kvmppc_tce_validate(stt, tce);
		if (ret != H_SUCCESS)
			goto unlock_exit;

		kvmppc_tce_put(stt, entry + i, tce);
	}

unlock_exit:
	unlock_rmap(rmap);

	return ret;
}

long kvmppc_rm_h_stuff_tce(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_value, unsigned long npages)
{
	struct kvmppc_spapr_tce_table *stt;
	long i, ret;

	stt = kvmppc_find_table(vcpu, liobn);
	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, npages);
	if (ret != H_SUCCESS)
		return ret;

	/* Check permission bits only to allow userspace poison TCE for debug */
	if (tce_value & (TCE_PCI_WRITE | TCE_PCI_READ))
		return H_PARAMETER;

	for (i = 0; i < npages; ++i, ioba += (1ULL << stt->page_shift))
		kvmppc_tce_put(stt, ioba >> stt->page_shift, tce_value);

	return H_SUCCESS;
}

long kvmppc_h_get_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		      unsigned long ioba)
{
	struct kvmppc_spapr_tce_table *stt = kvmppc_find_table(vcpu, liobn);
	long ret;
	unsigned long idx;
	struct page *page;
	u64 *tbl;

	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	idx = (ioba >> stt->page_shift) - stt->offset;
	page = stt->pages[idx / TCES_PER_PAGE];
	tbl = (u64 *)page_address(page);

	vcpu->arch.gpr[4] = tbl[idx % TCES_PER_PAGE];

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_get_tce);

#endif /* KVM_BOOK3S_HV_POSSIBLE */
