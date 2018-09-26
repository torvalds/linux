/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright 2016 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/pte-walk.h>

/*
 * Supported radix tree geometry.
 * Like p9, we support either 5 or 9 bits at the first (lowest) level,
 * for a page size of 64k or 4k.
 */
static int p9_supported_radix_bits[4] = { 5, 9, 9, 13 };

int kvmppc_mmu_radix_xlate(struct kvm_vcpu *vcpu, gva_t eaddr,
			   struct kvmppc_pte *gpte, bool data, bool iswrite)
{
	struct kvm *kvm = vcpu->kvm;
	u32 pid;
	int ret, level, ps;
	__be64 prte, rpte;
	unsigned long ptbl;
	unsigned long root, pte, index;
	unsigned long rts, bits, offset;
	unsigned long gpa;
	unsigned long proc_tbl_size;

	/* Work out effective PID */
	switch (eaddr >> 62) {
	case 0:
		pid = vcpu->arch.pid;
		break;
	case 3:
		pid = 0;
		break;
	default:
		return -EINVAL;
	}
	proc_tbl_size = 1 << ((kvm->arch.process_table & PRTS_MASK) + 12);
	if (pid * 16 >= proc_tbl_size)
		return -EINVAL;

	/* Read partition table to find root of tree for effective PID */
	ptbl = (kvm->arch.process_table & PRTB_MASK) + (pid * 16);
	ret = kvm_read_guest(kvm, ptbl, &prte, sizeof(prte));
	if (ret)
		return ret;

	root = be64_to_cpu(prte);
	rts = ((root & RTS1_MASK) >> (RTS1_SHIFT - 3)) |
		((root & RTS2_MASK) >> RTS2_SHIFT);
	bits = root & RPDS_MASK;
	root = root & RPDB_MASK;

	offset = rts + 31;

	/* current implementations only support 52-bit space */
	if (offset != 52)
		return -EINVAL;

	for (level = 3; level >= 0; --level) {
		if (level && bits != p9_supported_radix_bits[level])
			return -EINVAL;
		if (level == 0 && !(bits == 5 || bits == 9))
			return -EINVAL;
		offset -= bits;
		index = (eaddr >> offset) & ((1UL << bits) - 1);
		/* check that low bits of page table base are zero */
		if (root & ((1UL << (bits + 3)) - 1))
			return -EINVAL;
		ret = kvm_read_guest(kvm, root + index * 8,
				     &rpte, sizeof(rpte));
		if (ret)
			return ret;
		pte = __be64_to_cpu(rpte);
		if (!(pte & _PAGE_PRESENT))
			return -ENOENT;
		if (pte & _PAGE_PTE)
			break;
		bits = pte & 0x1f;
		root = pte & 0x0fffffffffffff00ul;
	}
	/* need a leaf at lowest level; 512GB pages not supported */
	if (level < 0 || level == 3)
		return -EINVAL;

	/* offset is now log base 2 of the page size */
	gpa = pte & 0x01fffffffffff000ul;
	if (gpa & ((1ul << offset) - 1))
		return -EINVAL;
	gpa += eaddr & ((1ul << offset) - 1);
	for (ps = MMU_PAGE_4K; ps < MMU_PAGE_COUNT; ++ps)
		if (offset == mmu_psize_defs[ps].shift)
			break;
	gpte->page_size = ps;

	gpte->eaddr = eaddr;
	gpte->raddr = gpa;

	/* Work out permissions */
	gpte->may_read = !!(pte & _PAGE_READ);
	gpte->may_write = !!(pte & _PAGE_WRITE);
	gpte->may_execute = !!(pte & _PAGE_EXEC);
	if (kvmppc_get_msr(vcpu) & MSR_PR) {
		if (pte & _PAGE_PRIVILEGED) {
			gpte->may_read = 0;
			gpte->may_write = 0;
			gpte->may_execute = 0;
		}
	} else {
		if (!(pte & _PAGE_PRIVILEGED)) {
			/* Check AMR/IAMR to see if strict mode is in force */
			if (vcpu->arch.amr & (1ul << 62))
				gpte->may_read = 0;
			if (vcpu->arch.amr & (1ul << 63))
				gpte->may_write = 0;
			if (vcpu->arch.iamr & (1ul << 62))
				gpte->may_execute = 0;
		}
	}

	return 0;
}

static void kvmppc_radix_tlbie_page(struct kvm *kvm, unsigned long addr,
				    unsigned int pshift)
{
	unsigned long psize = PAGE_SIZE;

	if (pshift)
		psize = 1UL << pshift;

	addr &= ~(psize - 1);
	radix__flush_tlb_lpid_page(kvm->arch.lpid, addr, psize);
}

static void kvmppc_radix_flush_pwc(struct kvm *kvm)
{
	radix__flush_pwc_lpid(kvm->arch.lpid);
}

static unsigned long kvmppc_radix_update_pte(struct kvm *kvm, pte_t *ptep,
				      unsigned long clr, unsigned long set,
				      unsigned long addr, unsigned int shift)
{
	return __radix_pte_update(ptep, clr, set);
}

void kvmppc_radix_set_pte_at(struct kvm *kvm, unsigned long addr,
			     pte_t *ptep, pte_t pte)
{
	radix__set_pte_at(kvm->mm, addr, ptep, pte, 0);
}

static struct kmem_cache *kvm_pte_cache;
static struct kmem_cache *kvm_pmd_cache;

static pte_t *kvmppc_pte_alloc(void)
{
	return kmem_cache_alloc(kvm_pte_cache, GFP_KERNEL);
}

static void kvmppc_pte_free(pte_t *ptep)
{
	kmem_cache_free(kvm_pte_cache, ptep);
}

/* Like pmd_huge() and pmd_large(), but works regardless of config options */
static inline int pmd_is_leaf(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PTE);
}

static pmd_t *kvmppc_pmd_alloc(void)
{
	return kmem_cache_alloc(kvm_pmd_cache, GFP_KERNEL);
}

static void kvmppc_pmd_free(pmd_t *pmdp)
{
	kmem_cache_free(kvm_pmd_cache, pmdp);
}

static void kvmppc_unmap_pte(struct kvm *kvm, pte_t *pte,
			     unsigned long gpa, unsigned int shift)

{
	unsigned long page_size = 1ul << shift;
	unsigned long old;

	old = kvmppc_radix_update_pte(kvm, pte, ~0UL, 0, gpa, shift);
	kvmppc_radix_tlbie_page(kvm, gpa, shift);
	if (old & _PAGE_DIRTY) {
		unsigned long gfn = gpa >> PAGE_SHIFT;
		struct kvm_memory_slot *memslot;

		memslot = gfn_to_memslot(kvm, gfn);
		if (memslot && memslot->dirty_bitmap)
			kvmppc_update_dirty_map(memslot, gfn, page_size);
	}
}

/*
 * kvmppc_free_p?d are used to free existing page tables, and recursively
 * descend and clear and free children.
 * Callers are responsible for flushing the PWC.
 *
 * When page tables are being unmapped/freed as part of page fault path
 * (full == false), ptes are not expected. There is code to unmap them
 * and emit a warning if encountered, but there may already be data
 * corruption due to the unexpected mappings.
 */
static void kvmppc_unmap_free_pte(struct kvm *kvm, pte_t *pte, bool full)
{
	if (full) {
		memset(pte, 0, sizeof(long) << PTE_INDEX_SIZE);
	} else {
		pte_t *p = pte;
		unsigned long it;

		for (it = 0; it < PTRS_PER_PTE; ++it, ++p) {
			if (pte_val(*p) == 0)
				continue;
			WARN_ON_ONCE(1);
			kvmppc_unmap_pte(kvm, p,
					 pte_pfn(*p) << PAGE_SHIFT,
					 PAGE_SHIFT);
		}
	}

	kvmppc_pte_free(pte);
}

static void kvmppc_unmap_free_pmd(struct kvm *kvm, pmd_t *pmd, bool full)
{
	unsigned long im;
	pmd_t *p = pmd;

	for (im = 0; im < PTRS_PER_PMD; ++im, ++p) {
		if (!pmd_present(*p))
			continue;
		if (pmd_is_leaf(*p)) {
			if (full) {
				pmd_clear(p);
			} else {
				WARN_ON_ONCE(1);
				kvmppc_unmap_pte(kvm, (pte_t *)p,
					 pte_pfn(*(pte_t *)p) << PAGE_SHIFT,
					 PMD_SHIFT);
			}
		} else {
			pte_t *pte;

			pte = pte_offset_map(p, 0);
			kvmppc_unmap_free_pte(kvm, pte, full);
			pmd_clear(p);
		}
	}
	kvmppc_pmd_free(pmd);
}

static void kvmppc_unmap_free_pud(struct kvm *kvm, pud_t *pud)
{
	unsigned long iu;
	pud_t *p = pud;

	for (iu = 0; iu < PTRS_PER_PUD; ++iu, ++p) {
		if (!pud_present(*p))
			continue;
		if (pud_huge(*p)) {
			pud_clear(p);
		} else {
			pmd_t *pmd;

			pmd = pmd_offset(p, 0);
			kvmppc_unmap_free_pmd(kvm, pmd, true);
			pud_clear(p);
		}
	}
	pud_free(kvm->mm, pud);
}

void kvmppc_free_radix(struct kvm *kvm)
{
	unsigned long ig;
	pgd_t *pgd;

	if (!kvm->arch.pgtable)
		return;
	pgd = kvm->arch.pgtable;
	for (ig = 0; ig < PTRS_PER_PGD; ++ig, ++pgd) {
		pud_t *pud;

		if (!pgd_present(*pgd))
			continue;
		pud = pud_offset(pgd, 0);
		kvmppc_unmap_free_pud(kvm, pud);
		pgd_clear(pgd);
	}
	pgd_free(kvm->mm, kvm->arch.pgtable);
	kvm->arch.pgtable = NULL;
}

static void kvmppc_unmap_free_pmd_entry_table(struct kvm *kvm, pmd_t *pmd,
					      unsigned long gpa)
{
	pte_t *pte = pte_offset_kernel(pmd, 0);

	/*
	 * Clearing the pmd entry then flushing the PWC ensures that the pte
	 * page no longer be cached by the MMU, so can be freed without
	 * flushing the PWC again.
	 */
	pmd_clear(pmd);
	kvmppc_radix_flush_pwc(kvm);

	kvmppc_unmap_free_pte(kvm, pte, false);
}

static void kvmppc_unmap_free_pud_entry_table(struct kvm *kvm, pud_t *pud,
					unsigned long gpa)
{
	pmd_t *pmd = pmd_offset(pud, 0);

	/*
	 * Clearing the pud entry then flushing the PWC ensures that the pmd
	 * page and any children pte pages will no longer be cached by the MMU,
	 * so can be freed without flushing the PWC again.
	 */
	pud_clear(pud);
	kvmppc_radix_flush_pwc(kvm);

	kvmppc_unmap_free_pmd(kvm, pmd, false);
}

/*
 * There are a number of bits which may differ between different faults to
 * the same partition scope entry. RC bits, in the course of cleaning and
 * aging. And the write bit can change, either the access could have been
 * upgraded, or a read fault could happen concurrently with a write fault
 * that sets those bits first.
 */
#define PTE_BITS_MUST_MATCH (~(_PAGE_WRITE | _PAGE_DIRTY | _PAGE_ACCESSED))

static int kvmppc_create_pte(struct kvm *kvm, pte_t pte, unsigned long gpa,
			     unsigned int level, unsigned long mmu_seq)
{
	pgd_t *pgd;
	pud_t *pud, *new_pud = NULL;
	pmd_t *pmd, *new_pmd = NULL;
	pte_t *ptep, *new_ptep = NULL;
	int ret;

	/* Traverse the guest's 2nd-level tree, allocate new levels needed */
	pgd = kvm->arch.pgtable + pgd_index(gpa);
	pud = NULL;
	if (pgd_present(*pgd))
		pud = pud_offset(pgd, gpa);
	else
		new_pud = pud_alloc_one(kvm->mm, gpa);

	pmd = NULL;
	if (pud && pud_present(*pud) && !pud_huge(*pud))
		pmd = pmd_offset(pud, gpa);
	else if (level <= 1)
		new_pmd = kvmppc_pmd_alloc();

	if (level == 0 && !(pmd && pmd_present(*pmd) && !pmd_is_leaf(*pmd)))
		new_ptep = kvmppc_pte_alloc();

	/* Check if we might have been invalidated; let the guest retry if so */
	spin_lock(&kvm->mmu_lock);
	ret = -EAGAIN;
	if (mmu_notifier_retry(kvm, mmu_seq))
		goto out_unlock;

	/* Now traverse again under the lock and change the tree */
	ret = -ENOMEM;
	if (pgd_none(*pgd)) {
		if (!new_pud)
			goto out_unlock;
		pgd_populate(kvm->mm, pgd, new_pud);
		new_pud = NULL;
	}
	pud = pud_offset(pgd, gpa);
	if (pud_huge(*pud)) {
		unsigned long hgpa = gpa & PUD_MASK;

		/* Check if we raced and someone else has set the same thing */
		if (level == 2) {
			if (pud_raw(*pud) == pte_raw(pte)) {
				ret = 0;
				goto out_unlock;
			}
			/* Valid 1GB page here already, add our extra bits */
			WARN_ON_ONCE((pud_val(*pud) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
			kvmppc_radix_update_pte(kvm, (pte_t *)pud,
					      0, pte_val(pte), hgpa, PUD_SHIFT);
			ret = 0;
			goto out_unlock;
		}
		/*
		 * If we raced with another CPU which has just put
		 * a 1GB pte in after we saw a pmd page, try again.
		 */
		if (!new_pmd) {
			ret = -EAGAIN;
			goto out_unlock;
		}
		/* Valid 1GB page here already, remove it */
		kvmppc_unmap_pte(kvm, (pte_t *)pud, hgpa, PUD_SHIFT);
	}
	if (level == 2) {
		if (!pud_none(*pud)) {
			/*
			 * There's a page table page here, but we wanted to
			 * install a large page, so remove and free the page
			 * table page.
			 */
			kvmppc_unmap_free_pud_entry_table(kvm, pud, gpa);
		}
		kvmppc_radix_set_pte_at(kvm, gpa, (pte_t *)pud, pte);
		ret = 0;
		goto out_unlock;
	}
	if (pud_none(*pud)) {
		if (!new_pmd)
			goto out_unlock;
		pud_populate(kvm->mm, pud, new_pmd);
		new_pmd = NULL;
	}
	pmd = pmd_offset(pud, gpa);
	if (pmd_is_leaf(*pmd)) {
		unsigned long lgpa = gpa & PMD_MASK;

		/* Check if we raced and someone else has set the same thing */
		if (level == 1) {
			if (pmd_raw(*pmd) == pte_raw(pte)) {
				ret = 0;
				goto out_unlock;
			}
			/* Valid 2MB page here already, add our extra bits */
			WARN_ON_ONCE((pmd_val(*pmd) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
			kvmppc_radix_update_pte(kvm, pmdp_ptep(pmd),
					      0, pte_val(pte), lgpa, PMD_SHIFT);
			ret = 0;
			goto out_unlock;
		}

		/*
		 * If we raced with another CPU which has just put
		 * a 2MB pte in after we saw a pte page, try again.
		 */
		if (!new_ptep) {
			ret = -EAGAIN;
			goto out_unlock;
		}
		/* Valid 2MB page here already, remove it */
		kvmppc_unmap_pte(kvm, pmdp_ptep(pmd), lgpa, PMD_SHIFT);
	}
	if (level == 1) {
		if (!pmd_none(*pmd)) {
			/*
			 * There's a page table page here, but we wanted to
			 * install a large page, so remove and free the page
			 * table page.
			 */
			kvmppc_unmap_free_pmd_entry_table(kvm, pmd, gpa);
		}
		kvmppc_radix_set_pte_at(kvm, gpa, pmdp_ptep(pmd), pte);
		ret = 0;
		goto out_unlock;
	}
	if (pmd_none(*pmd)) {
		if (!new_ptep)
			goto out_unlock;
		pmd_populate(kvm->mm, pmd, new_ptep);
		new_ptep = NULL;
	}
	ptep = pte_offset_kernel(pmd, gpa);
	if (pte_present(*ptep)) {
		/* Check if someone else set the same thing */
		if (pte_raw(*ptep) == pte_raw(pte)) {
			ret = 0;
			goto out_unlock;
		}
		/* Valid page here already, add our extra bits */
		WARN_ON_ONCE((pte_val(*ptep) ^ pte_val(pte)) &
							PTE_BITS_MUST_MATCH);
		kvmppc_radix_update_pte(kvm, ptep, 0, pte_val(pte), gpa, 0);
		ret = 0;
		goto out_unlock;
	}
	kvmppc_radix_set_pte_at(kvm, gpa, ptep, pte);
	ret = 0;

 out_unlock:
	spin_unlock(&kvm->mmu_lock);
	if (new_pud)
		pud_free(kvm->mm, new_pud);
	if (new_pmd)
		kvmppc_pmd_free(new_pmd);
	if (new_ptep)
		kvmppc_pte_free(new_ptep);
	return ret;
}

int kvmppc_book3s_radix_page_fault(struct kvm_run *run, struct kvm_vcpu *vcpu,
				   unsigned long ea, unsigned long dsisr)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long mmu_seq;
	unsigned long gpa, gfn, hva;
	struct kvm_memory_slot *memslot;
	struct page *page = NULL;
	long ret;
	bool writing;
	bool upgrade_write = false;
	bool *upgrade_p = &upgrade_write;
	pte_t pte, *ptep;
	unsigned long pgflags;
	unsigned int shift, level;

	/* Check for unusual errors */
	if (dsisr & DSISR_UNSUPP_MMU) {
		pr_err("KVM: Got unsupported MMU fault\n");
		return -EFAULT;
	}
	if (dsisr & DSISR_BADACCESS) {
		/* Reflect to the guest as DSI */
		pr_err("KVM: Got radix HV page fault with DSISR=%lx\n", dsisr);
		kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
		return RESUME_GUEST;
	}

	/* Translate the logical address and get the page */
	gpa = vcpu->arch.fault_gpa & ~0xfffUL;
	gpa &= ~0xF000000000000000ul;
	gfn = gpa >> PAGE_SHIFT;
	if (!(dsisr & DSISR_PRTABLE_FAULT))
		gpa |= ea & 0xfff;
	memslot = gfn_to_memslot(kvm, gfn);

	/* No memslot means it's an emulated MMIO region */
	if (!memslot || (memslot->flags & KVM_MEMSLOT_INVALID)) {
		if (dsisr & (DSISR_PRTABLE_FAULT | DSISR_BADACCESS |
			     DSISR_SET_RC)) {
			/*
			 * Bad address in guest page table tree, or other
			 * unusual error - reflect it to the guest as DSI.
			 */
			kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
			return RESUME_GUEST;
		}
		return kvmppc_hv_emulate_mmio(run, vcpu, gpa, ea,
					      dsisr & DSISR_ISSTORE);
	}

	writing = (dsisr & DSISR_ISSTORE) != 0;
	if (memslot->flags & KVM_MEM_READONLY) {
		if (writing) {
			/* give the guest a DSI */
			dsisr = DSISR_ISSTORE | DSISR_PROTFAULT;
			kvmppc_core_queue_data_storage(vcpu, ea, dsisr);
			return RESUME_GUEST;
		}
		upgrade_p = NULL;
	}

	if (dsisr & DSISR_SET_RC) {
		/*
		 * Need to set an R or C bit in the 2nd-level tables;
		 * since we are just helping out the hardware here,
		 * it is sufficient to do what the hardware does.
		 */
		pgflags = _PAGE_ACCESSED;
		if (writing)
			pgflags |= _PAGE_DIRTY;
		/*
		 * We are walking the secondary page table here. We can do this
		 * without disabling irq.
		 */
		spin_lock(&kvm->mmu_lock);
		ptep = __find_linux_pte(kvm->arch.pgtable,
					gpa, NULL, &shift);
		if (ptep && pte_present(*ptep) &&
		    (!writing || pte_write(*ptep))) {
			kvmppc_radix_update_pte(kvm, ptep, 0, pgflags,
						gpa, shift);
			dsisr &= ~DSISR_SET_RC;
		}
		spin_unlock(&kvm->mmu_lock);
		if (!(dsisr & (DSISR_BAD_FAULT_64S | DSISR_NOHPTE |
			       DSISR_PROTFAULT | DSISR_SET_RC)))
			return RESUME_GUEST;
	}

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	/*
	 * Do a fast check first, since __gfn_to_pfn_memslot doesn't
	 * do it with !atomic && !async, which is how we call it.
	 * We always ask for write permission since the common case
	 * is that the page is writable.
	 */
	hva = gfn_to_hva_memslot(memslot, gfn);
	if (upgrade_p && __get_user_pages_fast(hva, 1, 1, &page) == 1) {
		upgrade_write = true;
	} else {
		unsigned long pfn;

		/* Call KVM generic code to do the slow-path check */
		pfn = __gfn_to_pfn_memslot(memslot, gfn, false, NULL,
					   writing, upgrade_p);
		if (is_error_noslot_pfn(pfn))
			return -EFAULT;
		page = NULL;
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);
			if (PageReserved(page))
				page = NULL;
		}
	}

	/*
	 * Read the PTE from the process' radix tree and use that
	 * so we get the shift and attribute bits.
	 */
	local_irq_disable();
	ptep = __find_linux_pte(vcpu->arch.pgdir, hva, NULL, &shift);
	pte = *ptep;
	local_irq_enable();

	/* Get pte level from shift/size */
	if (shift == PUD_SHIFT &&
	    (gpa & (PUD_SIZE - PAGE_SIZE)) ==
	    (hva & (PUD_SIZE - PAGE_SIZE))) {
		level = 2;
	} else if (shift == PMD_SHIFT &&
		   (gpa & (PMD_SIZE - PAGE_SIZE)) ==
		   (hva & (PMD_SIZE - PAGE_SIZE))) {
		level = 1;
	} else {
		level = 0;
		if (shift > PAGE_SHIFT) {
			/*
			 * If the pte maps more than one page, bring over
			 * bits from the virtual address to get the real
			 * address of the specific single page we want.
			 */
			unsigned long rpnmask = (1ul << shift) - PAGE_SIZE;
			pte = __pte(pte_val(pte) | (hva & rpnmask));
		}
	}

	pte = __pte(pte_val(pte) | _PAGE_EXEC | _PAGE_ACCESSED);
	if (writing || upgrade_write) {
		if (pte_val(pte) & _PAGE_WRITE)
			pte = __pte(pte_val(pte) | _PAGE_DIRTY);
	} else {
		pte = __pte(pte_val(pte) & ~(_PAGE_WRITE | _PAGE_DIRTY));
	}

	/* Allocate space in the tree and write the PTE */
	ret = kvmppc_create_pte(kvm, pte, gpa, level, mmu_seq);

	if (page) {
		if (!ret && (pte_val(pte) & _PAGE_WRITE))
			set_page_dirty_lock(page);
		put_page(page);
	}

	if (ret == 0 || ret == -EAGAIN)
		ret = RESUME_GUEST;
	return ret;
}

/* Called with kvm->lock held */
int kvm_unmap_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
		    unsigned long gfn)
{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;
	unsigned long old;

	ptep = __find_linux_pte(kvm->arch.pgtable, gpa, NULL, &shift);
	if (ptep && pte_present(*ptep)) {
		old = kvmppc_radix_update_pte(kvm, ptep, ~0UL, 0,
					      gpa, shift);
		kvmppc_radix_tlbie_page(kvm, gpa, shift);
		if ((old & _PAGE_DIRTY) && memslot->dirty_bitmap) {
			unsigned long psize = PAGE_SIZE;
			if (shift)
				psize = 1ul << shift;
			kvmppc_update_dirty_map(memslot, gfn, psize);
		}
	}
	return 0;				
}

/* Called with kvm->lock held */
int kvm_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
		  unsigned long gfn)
{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;
	int ref = 0;

	ptep = __find_linux_pte(kvm->arch.pgtable, gpa, NULL, &shift);
	if (ptep && pte_present(*ptep) && pte_young(*ptep)) {
		kvmppc_radix_update_pte(kvm, ptep, _PAGE_ACCESSED, 0,
					gpa, shift);
		/* XXX need to flush tlb here? */
		ref = 1;
	}
	return ref;
}

/* Called with kvm->lock held */
int kvm_test_age_radix(struct kvm *kvm, struct kvm_memory_slot *memslot,
		       unsigned long gfn)
{
	pte_t *ptep;
	unsigned long gpa = gfn << PAGE_SHIFT;
	unsigned int shift;
	int ref = 0;

	ptep = __find_linux_pte(kvm->arch.pgtable, gpa, NULL, &shift);
	if (ptep && pte_present(*ptep) && pte_young(*ptep))
		ref = 1;
	return ref;
}

/* Returns the number of PAGE_SIZE pages that are dirty */
static int kvm_radix_test_clear_dirty(struct kvm *kvm,
				struct kvm_memory_slot *memslot, int pagenum)
{
	unsigned long gfn = memslot->base_gfn + pagenum;
	unsigned long gpa = gfn << PAGE_SHIFT;
	pte_t *ptep;
	unsigned int shift;
	int ret = 0;

	ptep = __find_linux_pte(kvm->arch.pgtable, gpa, NULL, &shift);
	if (ptep && pte_present(*ptep) && pte_dirty(*ptep)) {
		ret = 1;
		if (shift)
			ret = 1 << (shift - PAGE_SHIFT);
		kvmppc_radix_update_pte(kvm, ptep, _PAGE_DIRTY, 0,
					gpa, shift);
		kvmppc_radix_tlbie_page(kvm, gpa, shift);
	}
	return ret;
}

long kvmppc_hv_get_dirty_log_radix(struct kvm *kvm,
			struct kvm_memory_slot *memslot, unsigned long *map)
{
	unsigned long i, j;
	int npages;

	for (i = 0; i < memslot->npages; i = j) {
		npages = kvm_radix_test_clear_dirty(kvm, memslot, i);

		/*
		 * Note that if npages > 0 then i must be a multiple of npages,
		 * since huge pages are only used to back the guest at guest
		 * real addresses that are a multiple of their size.
		 * Since we have at most one PTE covering any given guest
		 * real address, if npages > 1 we can skip to i + npages.
		 */
		j = i + 1;
		if (npages) {
			set_dirty_bits(map, i, npages);
			j = i + npages;
		}
	}
	return 0;
}

static void add_rmmu_ap_encoding(struct kvm_ppc_rmmu_info *info,
				 int psize, int *indexp)
{
	if (!mmu_psize_defs[psize].shift)
		return;
	info->ap_encodings[*indexp] = mmu_psize_defs[psize].shift |
		(mmu_psize_defs[psize].ap << 29);
	++(*indexp);
}

int kvmhv_get_rmmu_info(struct kvm *kvm, struct kvm_ppc_rmmu_info *info)
{
	int i;

	if (!radix_enabled())
		return -EINVAL;
	memset(info, 0, sizeof(*info));

	/* 4k page size */
	info->geometries[0].page_shift = 12;
	info->geometries[0].level_bits[0] = 9;
	for (i = 1; i < 4; ++i)
		info->geometries[0].level_bits[i] = p9_supported_radix_bits[i];
	/* 64k page size */
	info->geometries[1].page_shift = 16;
	for (i = 0; i < 4; ++i)
		info->geometries[1].level_bits[i] = p9_supported_radix_bits[i];

	i = 0;
	add_rmmu_ap_encoding(info, MMU_PAGE_4K, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_64K, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_2M, &i);
	add_rmmu_ap_encoding(info, MMU_PAGE_1G, &i);

	return 0;
}

int kvmppc_init_vm_radix(struct kvm *kvm)
{
	kvm->arch.pgtable = pgd_alloc(kvm->mm);
	if (!kvm->arch.pgtable)
		return -ENOMEM;
	return 0;
}

static void pte_ctor(void *addr)
{
	memset(addr, 0, RADIX_PTE_TABLE_SIZE);
}

static void pmd_ctor(void *addr)
{
	memset(addr, 0, RADIX_PMD_TABLE_SIZE);
}

int kvmppc_radix_init(void)
{
	unsigned long size = sizeof(void *) << RADIX_PTE_INDEX_SIZE;

	kvm_pte_cache = kmem_cache_create("kvm-pte", size, size, 0, pte_ctor);
	if (!kvm_pte_cache)
		return -ENOMEM;

	size = sizeof(void *) << RADIX_PMD_INDEX_SIZE;

	kvm_pmd_cache = kmem_cache_create("kvm-pmd", size, size, 0, pmd_ctor);
	if (!kvm_pmd_cache) {
		kmem_cache_destroy(kvm_pte_cache);
		return -ENOMEM;
	}

	return 0;
}

void kvmppc_radix_exit(void)
{
	kmem_cache_destroy(kvm_pte_cache);
	kmem_cache_destroy(kvm_pmd_cache);
}
