// SPDX-License-Identifier: GPL-2.0-only
/*
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
#include <linux/stringify.h>

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
#include <asm/pte-walk.h>

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

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
static long kvmppc_rm_tce_to_ua(struct kvm *kvm, unsigned long tce,
		unsigned long *ua, unsigned long **prmap)
{
	unsigned long gfn = tce >> PAGE_SHIFT;
	struct kvm_memory_slot *memslot;

	memslot = search_memslots(kvm_memslots_raw(kvm), gfn);
	if (!memslot)
		return -EINVAL;

	*ua = __gfn_to_hva_memslot(memslot, gfn) |
		(tce & ~(PAGE_MASK | TCE_PCI_READ | TCE_PCI_WRITE));

	if (prmap)
		*prmap = &memslot->arch.rmap[gfn - memslot->base_gfn];

	return 0;
}

/*
 * Validates TCE address.
 * At the moment flags and page mask are validated.
 * As the host kernel does not access those addresses (just puts them
 * to the table and user space is supposed to process them), we can skip
 * checking other things (such as TCE is a guest RAM address or the page
 * was actually allocated).
 */
static long kvmppc_rm_tce_validate(struct kvmppc_spapr_tce_table *stt,
		unsigned long tce)
{
	unsigned long gpa = tce & ~(TCE_PCI_READ | TCE_PCI_WRITE);
	enum dma_data_direction dir = iommu_tce_direction(tce);
	struct kvmppc_spapr_tce_iommu_table *stit;
	unsigned long ua = 0;

	/* Allow userspace to poison TCE table */
	if (dir == DMA_NONE)
		return H_SUCCESS;

	if (iommu_tce_check_gpa(stt->page_shift, gpa))
		return H_PARAMETER;

	if (kvmppc_rm_tce_to_ua(stt->kvm, tce, &ua, NULL))
		return H_TOO_HARD;

	list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
		unsigned long hpa = 0;
		struct mm_iommu_table_group_mem_t *mem;
		long shift = stit->tbl->it_page_shift;

		mem = mm_iommu_lookup_rm(stt->kvm->mm, ua, 1ULL << shift);
		if (!mem)
			return H_TOO_HARD;

		if (mm_iommu_ua_to_hpa_rm(mem, ua, shift, &hpa))
			return H_TOO_HARD;
	}

	return H_SUCCESS;
}

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
 * Cannot fail so kvmppc_rm_tce_validate must be called before it.
 */
static void kvmppc_rm_tce_put(struct kvmppc_spapr_tce_table *stt,
		unsigned long idx, unsigned long tce)
{
	struct page *page;
	u64 *tbl;

	idx -= stt->offset;
	page = stt->pages[idx / TCES_PER_PAGE];
	/*
	 * page must not be NULL in real mode,
	 * kvmppc_rm_ioba_validate() must have taken care of this.
	 */
	WARN_ON_ONCE_RM(!page);
	tbl = kvmppc_page_address(page);

	tbl[idx % TCES_PER_PAGE] = tce;
}

/*
 * TCEs pages are allocated in kvmppc_rm_tce_put() which won't be able to do so
 * in real mode.
 * Check if kvmppc_rm_tce_put() can succeed in real mode, i.e. a TCEs page is
 * allocated or not required (when clearing a tce entry).
 */
static long kvmppc_rm_ioba_validate(struct kvmppc_spapr_tce_table *stt,
		unsigned long ioba, unsigned long npages, bool clearing)
{
	unsigned long i, idx, sttpage, sttpages;
	unsigned long ret = kvmppc_ioba_validate(stt, ioba, npages);

	if (ret)
		return ret;
	/*
	 * clearing==true says kvmppc_rm_tce_put won't be allocating pages
	 * for empty tces.
	 */
	if (clearing)
		return H_SUCCESS;

	idx = (ioba >> stt->page_shift) - stt->offset;
	sttpage = idx / TCES_PER_PAGE;
	sttpages = _ALIGN_UP(idx % TCES_PER_PAGE + npages, TCES_PER_PAGE) /
			TCES_PER_PAGE;
	for (i = sttpage; i < sttpage + sttpages; ++i)
		if (!stt->pages[i])
			return H_TOO_HARD;

	return H_SUCCESS;
}

static long iommu_tce_xchg_rm(struct mm_struct *mm, struct iommu_table *tbl,
		unsigned long entry, unsigned long *hpa,
		enum dma_data_direction *direction)
{
	long ret;

	ret = tbl->it_ops->exchange_rm(tbl, entry, hpa, direction);

	if (!ret && ((*direction == DMA_FROM_DEVICE) ||
				(*direction == DMA_BIDIRECTIONAL))) {
		__be64 *pua = IOMMU_TABLE_USERSPACE_ENTRY_RO(tbl, entry);
		/*
		 * kvmppc_rm_tce_iommu_do_map() updates the UA cache after
		 * calling this so we still get here a valid UA.
		 */
		if (pua && *pua)
			mm_iommu_ua_mark_dirty_rm(mm, be64_to_cpu(*pua));
	}

	return ret;
}

static void kvmppc_rm_clear_tce(struct kvm *kvm, struct iommu_table *tbl,
		unsigned long entry)
{
	unsigned long hpa = 0;
	enum dma_data_direction dir = DMA_NONE;

	iommu_tce_xchg_rm(kvm->mm, tbl, entry, &hpa, &dir);
}

static long kvmppc_rm_tce_iommu_mapped_dec(struct kvm *kvm,
		struct iommu_table *tbl, unsigned long entry)
{
	struct mm_iommu_table_group_mem_t *mem = NULL;
	const unsigned long pgsize = 1ULL << tbl->it_page_shift;
	__be64 *pua = IOMMU_TABLE_USERSPACE_ENTRY_RO(tbl, entry);

	if (!pua)
		/* it_userspace allocation might be delayed */
		return H_TOO_HARD;

	mem = mm_iommu_lookup_rm(kvm->mm, be64_to_cpu(*pua), pgsize);
	if (!mem)
		return H_TOO_HARD;

	mm_iommu_mapped_dec(mem);

	*pua = cpu_to_be64(0);

	return H_SUCCESS;
}

static long kvmppc_rm_tce_iommu_do_unmap(struct kvm *kvm,
		struct iommu_table *tbl, unsigned long entry)
{
	enum dma_data_direction dir = DMA_NONE;
	unsigned long hpa = 0;
	long ret;

	if (iommu_tce_xchg_rm(kvm->mm, tbl, entry, &hpa, &dir))
		/*
		 * real mode xchg can fail if struct page crosses
		 * a page boundary
		 */
		return H_TOO_HARD;

	if (dir == DMA_NONE)
		return H_SUCCESS;

	ret = kvmppc_rm_tce_iommu_mapped_dec(kvm, tbl, entry);
	if (ret)
		iommu_tce_xchg_rm(kvm->mm, tbl, entry, &hpa, &dir);

	return ret;
}

static long kvmppc_rm_tce_iommu_unmap(struct kvm *kvm,
		struct kvmppc_spapr_tce_table *stt, struct iommu_table *tbl,
		unsigned long entry)
{
	unsigned long i, ret = H_SUCCESS;
	unsigned long subpages = 1ULL << (stt->page_shift - tbl->it_page_shift);
	unsigned long io_entry = entry * subpages;

	for (i = 0; i < subpages; ++i) {
		ret = kvmppc_rm_tce_iommu_do_unmap(kvm, tbl, io_entry + i);
		if (ret != H_SUCCESS)
			break;
	}

	return ret;
}

static long kvmppc_rm_tce_iommu_do_map(struct kvm *kvm, struct iommu_table *tbl,
		unsigned long entry, unsigned long ua,
		enum dma_data_direction dir)
{
	long ret;
	unsigned long hpa = 0;
	__be64 *pua = IOMMU_TABLE_USERSPACE_ENTRY_RO(tbl, entry);
	struct mm_iommu_table_group_mem_t *mem;

	if (!pua)
		/* it_userspace allocation might be delayed */
		return H_TOO_HARD;

	mem = mm_iommu_lookup_rm(kvm->mm, ua, 1ULL << tbl->it_page_shift);
	if (!mem)
		return H_TOO_HARD;

	if (WARN_ON_ONCE_RM(mm_iommu_ua_to_hpa_rm(mem, ua, tbl->it_page_shift,
			&hpa)))
		return H_TOO_HARD;

	if (WARN_ON_ONCE_RM(mm_iommu_mapped_inc(mem)))
		return H_TOO_HARD;

	ret = iommu_tce_xchg_rm(kvm->mm, tbl, entry, &hpa, &dir);
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

	*pua = cpu_to_be64(ua);

	return 0;
}

static long kvmppc_rm_tce_iommu_map(struct kvm *kvm,
		struct kvmppc_spapr_tce_table *stt, struct iommu_table *tbl,
		unsigned long entry, unsigned long ua,
		enum dma_data_direction dir)
{
	unsigned long i, pgoff, ret = H_SUCCESS;
	unsigned long subpages = 1ULL << (stt->page_shift - tbl->it_page_shift);
	unsigned long io_entry = entry * subpages;

	for (i = 0, pgoff = 0; i < subpages;
			++i, pgoff += IOMMU_PAGE_SIZE(tbl)) {

		ret = kvmppc_rm_tce_iommu_do_map(kvm, tbl,
				io_entry + i, ua + pgoff, dir);
		if (ret != H_SUCCESS)
			break;
	}

	return ret;
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

	ret = kvmppc_rm_ioba_validate(stt, ioba, 1, tce == 0);
	if (ret != H_SUCCESS)
		return ret;

	ret = kvmppc_rm_tce_validate(stt, tce);
	if (ret != H_SUCCESS)
		return ret;

	dir = iommu_tce_direction(tce);
	if ((dir != DMA_NONE) && kvmppc_rm_tce_to_ua(vcpu->kvm, tce, &ua, NULL))
		return H_PARAMETER;

	entry = ioba >> stt->page_shift;

	list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
		if (dir == DMA_NONE)
			ret = kvmppc_rm_tce_iommu_unmap(vcpu->kvm, stt,
					stit->tbl, entry);
		else
			ret = kvmppc_rm_tce_iommu_map(vcpu->kvm, stt,
					stit->tbl, entry, ua, dir);

		if (ret != H_SUCCESS) {
			kvmppc_rm_clear_tce(vcpu->kvm, stit->tbl, entry);
			return ret;
		}
	}

	kvmppc_rm_tce_put(stt, entry, tce);

	return H_SUCCESS;
}

static long kvmppc_rm_ua_to_hpa(struct kvm_vcpu *vcpu,
		unsigned long ua, unsigned long *phpa)
{
	pte_t *ptep, pte;
	unsigned shift = 0;

	/*
	 * Called in real mode with MSR_EE = 0. We are safe here.
	 * It is ok to do the lookup with arch.pgdir here, because
	 * we are doing this on secondary cpus and current task there
	 * is not the hypervisor. Also this is safe against THP in the
	 * host, because an IPI to primary thread will wait for the secondary
	 * to exit which will agains result in the below page table walk
	 * to finish.
	 */
	ptep = __find_linux_pte(vcpu->arch.pgdir, ua, NULL, &shift);
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

	ret = kvmppc_rm_ioba_validate(stt, ioba, npages, false);
	if (ret != H_SUCCESS)
		return ret;

	if (mm_iommu_preregistered(vcpu->kvm->mm)) {
		/*
		 * We get here if guest memory was pre-registered which
		 * is normally VFIO case and gpa->hpa translation does not
		 * depend on hpt.
		 */
		struct mm_iommu_table_group_mem_t *mem;

		if (kvmppc_rm_tce_to_ua(vcpu->kvm, tce_list, &ua, NULL))
			return H_TOO_HARD;

		mem = mm_iommu_lookup_rm(vcpu->kvm->mm, ua, IOMMU_PAGE_SIZE_4K);
		if (mem)
			prereg = mm_iommu_ua_to_hpa_rm(mem, ua,
					IOMMU_PAGE_SHIFT_4K, &tces) == 0;
	}

	if (!prereg) {
		/*
		 * This is usually a case of a guest with emulated devices only
		 * when TCE list is not in preregistered memory.
		 * We do not require memory to be preregistered in this case
		 * so lock rmap and do __find_linux_pte_or_hugepte().
		 */
		if (kvmppc_rm_tce_to_ua(vcpu->kvm, tce_list, &ua, &rmap))
			return H_TOO_HARD;

		rmap = (void *) vmalloc_to_phys(rmap);
		if (WARN_ON_ONCE_RM(!rmap))
			return H_TOO_HARD;

		/*
		 * Synchronize with the MMU notifier callbacks in
		 * book3s_64_mmu_hv.c (kvm_unmap_hva_range_hv etc.).
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

		ret = kvmppc_rm_tce_validate(stt, tce);
		if (ret != H_SUCCESS)
			goto unlock_exit;
	}

	for (i = 0; i < npages; ++i) {
		unsigned long tce = be64_to_cpu(((u64 *)tces)[i]);

		ua = 0;
		if (kvmppc_rm_tce_to_ua(vcpu->kvm, tce, &ua, NULL)) {
			ret = H_PARAMETER;
			goto unlock_exit;
		}

		list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
			ret = kvmppc_rm_tce_iommu_map(vcpu->kvm, stt,
					stit->tbl, entry + i, ua,
					iommu_tce_direction(tce));

			if (ret != H_SUCCESS) {
				kvmppc_rm_clear_tce(vcpu->kvm, stit->tbl,
						entry);
				goto unlock_exit;
			}
		}

		kvmppc_rm_tce_put(stt, entry + i, tce);
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

	ret = kvmppc_rm_ioba_validate(stt, ioba, npages, tce_value == 0);
	if (ret != H_SUCCESS)
		return ret;

	/* Check permission bits only to allow userspace poison TCE for debug */
	if (tce_value & (TCE_PCI_WRITE | TCE_PCI_READ))
		return H_PARAMETER;

	list_for_each_entry_lockless(stit, &stt->iommu_tables, next) {
		unsigned long entry = ioba >> stt->page_shift;

		for (i = 0; i < npages; ++i) {
			ret = kvmppc_rm_tce_iommu_unmap(vcpu->kvm, stt,
					stit->tbl, entry + i);

			if (ret == H_SUCCESS)
				continue;

			if (ret == H_TOO_HARD)
				return ret;

			WARN_ON_ONCE_RM(1);
			kvmppc_rm_clear_tce(vcpu->kvm, stit->tbl, entry);
		}
	}

	for (i = 0; i < npages; ++i, ioba += (1ULL << stt->page_shift))
		kvmppc_rm_tce_put(stt, ioba >> stt->page_shift, tce_value);

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
	if (!page) {
		vcpu->arch.regs.gpr[4] = 0;
		return H_SUCCESS;
	}
	tbl = (u64 *)page_address(page);

	vcpu->arch.regs.gpr[4] = tbl[idx % TCES_PER_PAGE];

	return H_SUCCESS;
}
EXPORT_SYMBOL_GPL(kvmppc_h_get_tce);

#endif /* KVM_BOOK3S_HV_POSSIBLE */
