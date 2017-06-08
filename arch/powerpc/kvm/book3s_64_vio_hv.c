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

#ifdef CONFIG_BUG

#define WARN_ON_ONCE_RM(condition)	({			\
	static bool __section(.data.unlikely) __warned;		\
	int __ret_warn_once = !!(condition);			\
								\
	if (unlikely(__ret_warn_once && !__warned)) {		\
		__warned = true;				\
		pr_err("WARN_ON_ONCE_RM: (%s) at %s:%u\n",	\
				__stringify(condition),		\
				__func__, __LINE__);		\
		dump_stack();					\
	}							\
	unlikely(__ret_warn_once);				\
})

#else

#define WARN_ON_ONCE_RM(condition) ({				\
	int __ret_warn_on = !!(condition);			\
	unlikely(__ret_warn_on);				\
})

#endif

#define TCES_PER_PAGE	(PAGE_SIZE / sizeof(u64))

/*
 * Finds a TCE table descriptor by LIOBN.
 *
 * WARNING: This will be called in real or virtual mode on HV KVM and virtual
 *          mode on PR KVM
 */
struct kvmppc_spapr_tce_table *kvmppc_find_table(struct kvm *kvm,
		unsigned long liobn)
{
	struct kvmppc_spapr_tce_table *stt;

	list_for_each_entry_lockless(stt, &kvm->arch.spapr_tce_tables, list)
		if (stt->liobn == liobn)
			return stt;

	return NULL;
}
EXPORT_SYMBOL_GPL(kvmppc_find_table);

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
	unsigned long gpa = tce & ~(TCE_PCI_READ | TCE_PCI_WRITE);
	enum dma_data_direction dir = iommu_tce_direction(tce);

	/* Allow userspace to poison TCE table */
	if (dir == DMA_NONE)
		return H_SUCCESS;

	if (iommu_tce_check_gpa(stt->page_shift, gpa))
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
static void kvmppc_rm_clear_tce(struct iommu_table *tbl, unsigned long entry)
{
	unsigned long hpa = 0;
	enum dma_data_direction dir = DMA_NONE;

	iommu_tce_xchg_rm(tbl, entry, &hpa, &dir);
}

static long kvmppc_rm_tce_iommu_mapped_dec(struct kvm *kvm,
		struct iommu_table *tbl, unsigned long entry)
{
	struct mm_iommu_table_group_mem_t *mem = NULL;
	const unsigned long pgsize = 1ULL << tbl->it_page_shift;
	unsigned long *pua = IOMMU_TABLE_USERSPACE_ENTRY(tbl, entry);

	if (!pua)
		/* it_userspace allocation might be delayed */
		return H_TOO_HARD;

	pua = (void *) vmalloc_to_phys(pua);
	if (WARN_ON_ONCE_RM(!pua))
		return H_HARDWARE;

	mem = mm_iommu_lookup_rm(kvm->mm, *pua, pgsize);
	if (!mem)
		return H_TOO_HARD;

	mm_iommu_mapped_dec(mem);

	*pua = 0;

	return H_SUCCESS;
}

static long kvmppc_rm_tce_iommu_unmap(struct kvm *kvm,
		struct iommu_table *tbl, unsigned long entry)
{
	enum dma_data_direction dir = DMA_NONE;
	unsigned long hpa = 0;
	long ret;

	if (iommu_tce_xchg_rm(tbl, entry, &hpa, &dir))
		/*
		 * real mode xchg can fail if struct page crosses
		 * a page boundary
		 */
		return H_TOO_HARD;

	if (dir == DMA_NONE)
		return H_SUCCESS;

	ret = kvmppc_rm_tce_iommu_mapped_dec(kvm, tbl, entry);
	if (ret)
		iommu_tce_xchg_rm(tbl, entry, &hpa, &dir);

	return ret;
}

static long kvmppc_rm_tce_iommu_map(struct kvm *kvm, struct iommu_table *tbl,
		unsigned long entry, unsigned long ua,
		enum dma_data_direction dir)
{
	long ret;
	unsigned long hpa = 0;
	unsigned long *pua = IOMMU_TABLE_USERSPACE_ENTRY(tbl, entry);
	struct mm_iommu_table_group_mem_t *mem;

	if (!pua)
		/* it_userspace allocation might be delayed */
		return H_TOO_HARD;

	mem = mm_iommu_lookup_rm(kvm->mm, ua, 1ULL << tbl->it_page_shift);
	if (!mem)
		return H_TOO_HARD;

	if (WARN_ON_ONCE_RM(mm_iommu_ua_to_hpa_rm(mem, ua, &hpa)))
		return H_HARDWARE;

	pua = (void *) vmalloc_to_phys(pua);
	if (WARN_ON_ONCE_RM(!pua))
		return H_HARDWARE;

	if (WARN_ON_ONCE_RM(mm_iommu_mapped_inc(mem)))
		return H_CLOSED;

	ret = iommu_tce_xchg_rm(tbl, entry, &hpa, &dir);
	if (ret) {
		mm_iommu_mapped_dec(mem);
		/*
		 * real mode xchg can fail if struct page crosses
		 * a page boundary
		 */
		return H_TOO_HARD;
	}

	if (dir != DMA_NONE)
		kvmppc_rm_tce_iommu_mapped_dec(kvm, tbl, entry);

	*pua = ua;

	return 0;
}

long kvmppc_rm_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		unsigned long ioba, unsigned long tce)
{
	struct kvmppc_spapr_tce_table *stt;
	long ret;
	struct kvmppc_spapr_tce_iommu_table *stit;
	unsigned long entry, ua = 0;
	enum dma_data_direction dir;

	/* udbg_printf("H_PUT_TCE(): liobn=0x%lx ioba=0x%lx, tce=0x%lx\n", */
	/* 	    liobn, ioba, tce); */

	/* For radix, we might be in virtual mode, so punt */
	if (kvm_is_radix(vcpu->kvm))
		return H_TOO_HARD;

	stt = kvmppc_find_table(vcpu->kvm, liobn);
	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, 1);
	if (ret != H_SUCCESS)
		return ret;

	ret = kvmppc_tce_validate(stt, tce);
	if (ret != H_SUCCESS)
		return ret;

	dir = iommu_tce_direction(tce);
	if ((dir != DMA_NONE) && kvmppc_gpa_to_ua(vcpu->kvm,
			tce & ~(TCE_PCI_READ | TCE_PCI_WRITE), &ua, NULL))
		return H_PARAMETER;

	entry = ioba >> stt->page_shift;

	list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
		if (dir == DMA_NONE)
			ret = kvmppc_rm_tce_iommu_unmap(vcpu->kvm,
					stit->tbl, entry);
		else
			ret = kvmppc_rm_tce_iommu_map(vcpu->kvm,
					stit->tbl, entry, ua, dir);

		if (ret == H_SUCCESS)
			continue;

		if (ret == H_TOO_HARD)
			return ret;

		WARN_ON_ONCE_RM(1);
		kvmppc_rm_clear_tce(stit->tbl, entry);
	}

	kvmppc_tce_put(stt, entry, tce);

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
	bool prereg = false;
	struct kvmppc_spapr_tce_iommu_table *stit;

	/* For radix, we might be in virtual mode, so punt */
	if (kvm_is_radix(vcpu->kvm))
		return H_TOO_HARD;

	stt = kvmppc_find_table(vcpu->kvm, liobn);
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

	if (mm_iommu_preregistered(vcpu->kvm->mm)) {
		/*
		 * We get here if guest memory was pre-registered which
		 * is normally VFIO case and gpa->hpa translation does not
		 * depend on hpt.
		 */
		struct mm_iommu_table_group_mem_t *mem;

		if (kvmppc_gpa_to_ua(vcpu->kvm, tce_list, &ua, NULL))
			return H_TOO_HARD;

		mem = mm_iommu_lookup_rm(vcpu->kvm->mm, ua, IOMMU_PAGE_SIZE_4K);
		if (mem)
			prereg = mm_iommu_ua_to_hpa_rm(mem, ua, &tces) == 0;
	}

	if (!prereg) {
		/*
		 * This is usually a case of a guest with emulated devices only
		 * when TCE list is not in preregistered memory.
		 * We do not require memory to be preregistered in this case
		 * so lock rmap and do __find_linux_pte_or_hugepte().
		 */
		if (kvmppc_gpa_to_ua(vcpu->kvm, tce_list, &ua, &rmap))
			return H_TOO_HARD;

		rmap = (void *) vmalloc_to_phys(rmap);
		if (WARN_ON_ONCE_RM(!rmap))
			return H_HARDWARE;

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
	}

	for (i = 0; i < npages; ++i) {
		unsigned long tce = be64_to_cpu(((u64 *)tces)[i]);

		ret = kvmppc_tce_validate(stt, tce);
		if (ret != H_SUCCESS)
			goto unlock_exit;

		ua = 0;
		if (kvmppc_gpa_to_ua(vcpu->kvm,
				tce & ~(TCE_PCI_READ | TCE_PCI_WRITE),
				&ua, NULL))
			return H_PARAMETER;

		list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
			ret = kvmppc_rm_tce_iommu_map(vcpu->kvm,
					stit->tbl, entry + i, ua,
					iommu_tce_direction(tce));

			if (ret == H_SUCCESS)
				continue;

			if (ret == H_TOO_HARD)
				goto unlock_exit;

			WARN_ON_ONCE_RM(1);
			kvmppc_rm_clear_tce(stit->tbl, entry);
		}

		kvmppc_tce_put(stt, entry + i, tce);
	}

unlock_exit:
	if (rmap)
		unlock_rmap(rmap);

	return ret;
}

long kvmppc_rm_h_stuff_tce(struct kvm_vcpu *vcpu,
		unsigned long liobn, unsigned long ioba,
		unsigned long tce_value, unsigned long npages)
{
	struct kvmppc_spapr_tce_table *stt;
	long i, ret;
	struct kvmppc_spapr_tce_iommu_table *stit;

	/* For radix, we might be in virtual mode, so punt */
	if (kvm_is_radix(vcpu->kvm))
		return H_TOO_HARD;

	stt = kvmppc_find_table(vcpu->kvm, liobn);
	if (!stt)
		return H_TOO_HARD;

	ret = kvmppc_ioba_validate(stt, ioba, npages);
	if (ret != H_SUCCESS)
		return ret;

	/* Check permission bits only to allow userspace poison TCE for debug */
	if (tce_value & (TCE_PCI_WRITE | TCE_PCI_READ))
		return H_PARAMETER;

	list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
		unsigned long entry = ioba >> stit->tbl->it_page_shift;

		for (i = 0; i < npages; ++i) {
			ret = kvmppc_rm_tce_iommu_unmap(vcpu->kvm,
					stit->tbl, entry + i);

			if (ret == H_SUCCESS)
				continue;

			if (ret == H_TOO_HARD)
				return ret;

			WARN_ON_ONCE_RM(1);
			kvmppc_rm_clear_tce(stit->tbl, entry);
		}
	}

	for (i = 0; i < npages; ++i, ioba += (1ULL << stt->page_shift))
		kvmppc_tce_put(stt, ioba >> stt->page_shift, tce_value);

	return H_SUCCESS;
}

/* This can be called in either virtual mode or real mode */
long kvmppc_h_get_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
		      unsigned long ioba)
{
	struct kvmppc_spapr_tce_table *stt;
	long ret;
	unsigned long idx;
	struct page *page;
	u64 *tbl;

	stt = kvmppc_find_table(vcpu->kvm, liobn);
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
