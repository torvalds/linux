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

	/* P9 DD1 interprets RTS (radix tree size) differently */
	offset = rts + 31;
	if (cpu_has_feature(CPU_FTR_POWER9_DD1))
		offset -= 3;

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

#ifdef CONFIG_PPC_64K_PAGES
#define MMU_BASE_PSIZE	MMU_PAGE_64K
#else
#define MMU_BASE_PSIZE	MMU_PAGE_4K
#endif

static void kvmppc_radix_tlbie_page(struct kvm *kvm, unsigned long addr,
				    unsigned int pshift)
{
	int psize = MMU_BASE_PSIZE;

	if (pshift >= PMD_SHIFT)
		psize = MMU_PAGE_2M;
	addr &= ~0xfffUL;
	addr |= mmu_psize_defs[psize].ap << 5;
	asm volatile("ptesync": : :"memory");
	asm volatile(PPC_TLBIE_5(%0, %1, 0, 0, 1)
		     : : "r" (addr), "r" (kvm->arch.lpid) : "memory");
	asm volatile("ptesync": : :"memory");
}

unsigned long kvmppc_radix_update_pte(struct kvm *kvm, pte_t *ptep,
				      unsigned long clr, unsigned long set,
				      unsigned long addr, unsigned int shift)
{
	unsigned long old = 0;

	if (!(clr & _PAGE_PRESENT) && cpu_has_feature(CPU_FTR_POWER9_DD1) &&
	    pte_present(*ptep)) {
		/* have to invalidate it first */
		old = __radix_pte_update(ptep, _PAGE_PRESENT, 0);
		kvmppc_radix_tlbie_page(kvm, addr, shift);
		set |= _PAGE_PRESENT;
		old &= _PAGE_PRESENT;
	}
	return __radix_pte_update(ptep, clr, set) | old;
}

void kvmppc_radix_set_pte_at(struct kvm *kvm, unsigned long addr,
			     pte_t *ptep, pte_t pte)
{
	radix__set_pte_at(kvm->mm, addr, ptep, pte, 0);
}

static struct kmem_cache *kvm_pte_cache;

static pte_t *kvmppc_pte_alloc(void)
{
	return kmem_cache_alloc(kvm_pte_cache, GFP_KERNEL);
}

static void kvmppc_pte_free(pte_t *ptep)
{
	kmem_cache_free(kvm_pte_cache, ptep);
}

static int kvmppc_create_pte(struct kvm *kvm, pte_t pte, unsigned long gpa,
			     unsigned int level, unsigned long mmu_seq)
{
	pgd_t *pgd;
	pud_t *pud, *new_pud = NULL;
	pmd_t *pmd, *new_pmd = NULL;
	pte_t *ptep, *new_ptep = NULL;
	unsigned long old;
	int ret;

	/* Traverse the guest's 2nd-level tree, allocate new levels needed */
	pgd = kvm->arch.pgtable + pgd_index(gpa);
	pud = NULL;
	if (pgd_present(*pgd))
		pud = pud_offset(pgd, gpa);
	else
		new_pud = pud_alloc_one(kvm->mm, gpa);

	pmd = NULL;
	if (pud && pud_present(*pud))
		pmd = pmd_offset(pud, gpa);
	else
		new_pmd = pmd_alloc_one(kvm->mm, gpa);

	if (level == 0 && !(pmd && pmd_present(*pmd)))
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
	if (pud_none(*pud)) {
		if (!new_pmd)
			goto out_unlock;
		pud_populate(kvm->mm, pud, new_pmd);
		new_pmd = NULL;
	}
	pmd = pmd_offset(pud, gpa);
	if (pmd_large(*pmd)) {
		/* Someone else has instantiated a large page here; retry */
		ret = -EAGAIN;
		goto out_unlock;
	}
	if (level == 1 && !pmd_none(*pmd)) {
		/*
		 * There's a page table page here, but we wanted
		 * to install a large page.  Tell the caller and let
		 * it try installing a normal page if it wants.
		 */
		ret = -EBUSY;
		goto out_unlock;
	}
	if (level == 0) {
		if (pmd_none(*pmd)) {
			if (!new_ptep)
				goto out_unlock;
			pmd_populate(kvm->mm, pmd, new_ptep);
			new_ptep = NULL;
		}
		ptep = pte_offset_kernel(pmd, gpa);
		if (pte_present(*ptep)) {
			/* PTE was previously valid, so invalidate it */
			old = kvmppc_radix_update_pte(kvm, ptep, _PAGE_PRESENT,
						      0, gpa, 0);
			kvmppc_radix_tlbie_page(kvm, gpa, 0);
			if (old & _PAGE_DIRTY)
				mark_page_dirty(kvm, gpa >> PAGE_SHIFT);
		}
		kvmppc_radix_set_pte_at(kvm, gpa, ptep, pte);
	} else {
		kvmppc_radix_set_pte_at(kvm, gpa, pmdp_ptep(pmd), pte);
	}
	ret = 0;

 out_unlock:
	spin_unlock(&kvm->mmu_lock);
	if (new_pud)
		pud_free(kvm->mm, new_pud);
	if (new_pmd)
		pmd_free(kvm->mm, new_pmd);
	if (new_ptep)
		kvmppc_pte_free(new_ptep);
	return ret;
}

int kvmppc_book3s_radix_page_fault(struct kvm_run *run, struct kvm_vcpu *vcpu,
				   unsigned long ea, unsigned long dsisr)
{
	struct kvm *kvm = vcpu->kvm;
	unsigned long mmu_seq, pte_size;
	unsigned long gpa, gfn, hva, pfn;
	struct kvm_memory_slot *memslot;
	struct page *page = NULL, *pages[1];
	long ret, npages, ok;
	unsigned int writing;
	struct vm_area_struct *vma;
	unsigned long flags;
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

	/* used to check for invalidations in progress */
	mmu_seq = kvm->mmu_notifier_seq;
	smp_rmb();

	writing = (dsisr & DSISR_ISSTORE) != 0;
	hva = gfn_to_hva_memslot(memslot, gfn);
	if (dsisr & DSISR_SET_RC) {
		/*
		 * Need to set an R or C bit in the 2nd-level tables;
		 * if the relevant bits aren't already set in the linux
		 * page tables, fall through to do the gup_fast to
		 * set them in the linux page tables too.
		 */
		ok = 0;
		pgflags = _PAGE_ACCESSED;
		if (writing)
			pgflags |= _PAGE_DIRTY;
		local_irq_save(flags);
		ptep = find_current_mm_pte(current->mm->pgd, hva, NULL, NULL);
		if (ptep) {
			pte = READ_ONCE(*ptep);
			if (pte_present(pte) &&
			    (pte_val(pte) & pgflags) == pgflags)
				ok = 1;
		}
		local_irq_restore(flags);
		if (ok) {
			spin_lock(&kvm->mmu_lock);
			if (mmu_notifier_retry(vcpu->kvm, mmu_seq)) {
				spin_unlock(&kvm->mmu_lock);
				return RESUME_GUEST;
			}
			/*
			 * We are walking the secondary page table here. We can do this
			 * without disabling irq.
			 */
			ptep = __find_linux_pte(kvm->arch.pgtable,
						gpa, NULL, &shift);
			if (ptep && pte_present(*ptep)) {
				kvmppc_radix_update_pte(kvm, ptep, 0, pgflags,
							gpa, shift);
				spin_unlock(&kvm->mmu_lock);
				return RESUME_GUEST;
			}
			spin_unlock(&kvm->mmu_lock);
		}
	}

	ret = -EFAULT;
	pfn = 0;
	pte_size = PAGE_SIZE;
	pgflags = _PAGE_READ | _PAGE_EXEC;
	level = 0;
	npages = get_user_pages_fast(hva, 1, writing, pages);
	if (npages < 1) {
		/* Check if it's an I/O mapping */
		down_read(&current->mm->mmap_sem);
		vma = find_vma(current->mm, hva);
		if (vma && vma->vm_start <= hva && hva < vma->vm_end &&
		    (vma->vm_flags & VM_PFNMAP)) {
			pfn = vma->vm_pgoff +
				((hva - vma->vm_start) >> PAGE_SHIFT);
			pgflags = pgprot_val(vma->vm_page_prot);
		}
		up_read(&current->mm->mmap_sem);
		if (!pfn)
			return -EFAULT;
	} else {
		page = pages[0];
		pfn = page_to_pfn(page);
		if (PageHuge(page)) {
			page = compound_head(page);
			pte_size <<= compound_order(page);
			/* See if we can insert a 2MB large-page PTE here */
			if (pte_size >= PMD_SIZE &&
			    (gpa & PMD_MASK & PAGE_MASK) ==
			    (hva & PMD_MASK & PAGE_MASK)) {
				level = 1;
				pfn &= ~((PMD_SIZE >> PAGE_SHIFT) - 1);
			}
		}
		/* See if we can provide write access */
		if (writing) {
			/*
			 * We assume gup_fast has set dirty on the host PTE.
			 */
			pgflags |= _PAGE_WRITE;
		} else {
			local_irq_save(flags);
			ptep = find_current_mm_pte(current->mm->pgd,
						   hva, NULL, NULL);
			if (ptep && pte_write(*ptep) && pte_dirty(*ptep))
				pgflags |= _PAGE_WRITE;
			local_irq_restore(flags);
		}
	}

	/*
	 * Compute the PTE value that we need to insert.
	 */
	pgflags |= _PAGE_PRESENT | _PAGE_PTE | _PAGE_ACCESSED;
	if (pgflags & _PAGE_WRITE)
		pgflags |= _PAGE_DIRTY;
	pte = pfn_pte(pfn, __pgprot(pgflags));

	/* Allocate space in the tree and write the PTE */
	ret = kvmppc_create_pte(kvm, pte, gpa, level, mmu_seq);
	if (ret == -EBUSY) {
		/*
		 * There's already a PMD where wanted to install a large page;
		 * for now, fall back to installing a small page.
		 */
		level = 0;
		pfn |= gfn & ((PMD_SIZE >> PAGE_SHIFT) - 1);
		pte = pfn_pte(pfn, __pgprot(pgflags));
		ret = kvmppc_create_pte(kvm, pte, gpa, level, mmu_seq);
	}
	if (ret == 0 || ret == -EAGAIN)
		ret = RESUME_GUEST;

	if (page) {
		/*
		 * We drop pages[0] here, not page because page might
		 * have been set to the head page of a compound, but
		 * we have to drop the reference on the correct tail
		 * page to match the get inside gup()
		 */
		put_page(pages[0]);
	}
	return ret;
}

static void mark_pages_dirty(struct kvm *kvm, struct kvm_memory_slot *memslot,
			     unsigned long gfn, unsigned int order)
{
	unsigned long i, limit;
	unsigned long *dp;

	if (!memslot->dirty_bitmap)
		return;
	limit = 1ul << order;
	if (limit < BITS_PER_LONG) {
		for (i = 0; i < limit; ++i)
			mark_page_dirty(kvm, gfn + i);
		return;
	}
	dp = memslot->dirty_bitmap + (gfn - memslot->base_gfn);
	limit /= BITS_PER_LONG;
	for (i = 0; i < limit; ++i)
		*dp++ = ~0ul;
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
		old = kvmppc_radix_update_pte(kvm, ptep, _PAGE_PRESENT, 0,
					      gpa, shift);
		kvmppc_radix_tlbie_page(kvm, gpa, shift);
		if (old & _PAGE_DIRTY) {
			if (!shift)
				mark_page_dirty(kvm, gfn);
			else
				mark_pages_dirty(kvm, memslot,
						 gfn, shift - PAGE_SHIFT);
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
	unsigned long n, *p;
	int npages;

	/*
	 * Radix accumulates dirty bits in the first half of the
	 * memslot's dirty_bitmap area, for when pages are paged
	 * out or modified by the host directly.  Pick up these
	 * bits and add them to the map.
	 */
	n = kvm_dirty_bitmap_bytes(memslot) / sizeof(long);
	p = memslot->dirty_bitmap;
	for (i = 0; i < n; ++i)
		map[i] |= xchg(&p[i], 0);

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
		if (npages)
			for (j = i; npages; ++j, --npages)
				__set_bit_le(j, map);
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

void kvmppc_free_radix(struct kvm *kvm)
{
	unsigned long ig, iu, im;
	pte_t *pte;
	pmd_t *pmd;
	pud_t *pud;
	pgd_t *pgd;

	if (!kvm->arch.pgtable)
		return;
	pgd = kvm->arch.pgtable;
	for (ig = 0; ig < PTRS_PER_PGD; ++ig, ++pgd) {
		if (!pgd_present(*pgd))
			continue;
		pud = pud_offset(pgd, 0);
		for (iu = 0; iu < PTRS_PER_PUD; ++iu, ++pud) {
			if (!pud_present(*pud))
				continue;
			pmd = pmd_offset(pud, 0);
			for (im = 0; im < PTRS_PER_PMD; ++im, ++pmd) {
				if (pmd_huge(*pmd)) {
					pmd_clear(pmd);
					continue;
				}
				if (!pmd_present(*pmd))
					continue;
				pte = pte_offset_map(pmd, 0);
				memset(pte, 0, sizeof(long) << PTE_INDEX_SIZE);
				kvmppc_pte_free(pte);
				pmd_clear(pmd);
			}
			pmd_free(kvm->mm, pmd_offset(pud, 0));
			pud_clear(pud);
		}
		pud_free(kvm->mm, pud_offset(pgd, 0));
		pgd_clear(pgd);
	}
	pgd_free(kvm->mm, kvm->arch.pgtable);
}

static void pte_ctor(void *addr)
{
	memset(addr, 0, PTE_TABLE_SIZE);
}

int kvmppc_radix_init(void)
{
	unsigned long size = sizeof(void *) << PTE_INDEX_SIZE;

	kvm_pte_cache = kmem_cache_create("kvm-pte", size, size, 0, pte_ctor);
	if (!kvm_pte_cache)
		return -ENOMEM;
	return 0;
}

void kvmppc_radix_exit(void)
{
	kmem_cache_destroy(kvm_pte_cache);
}
