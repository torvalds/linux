/*
 * IA-32 Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/sysctl.h>
#include <asm/mman.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>

static unsigned long page_table_shareable(struct vm_area_struct *svma,
				struct vm_area_struct *vma,
				unsigned long addr, pgoff_t idx)
{
	unsigned long saddr = ((idx - svma->vm_pgoff) << PAGE_SHIFT) +
				svma->vm_start;
	unsigned long sbase = saddr & PUD_MASK;
	unsigned long s_end = sbase + PUD_SIZE;

	/* Allow segments to share if only one is marked locked */
	unsigned long vm_flags = vma->vm_flags & ~VM_LOCKED;
	unsigned long svm_flags = svma->vm_flags & ~VM_LOCKED;

	/*
	 * match the virtual addresses, permission and the alignment of the
	 * page table page.
	 */
	if (pmd_index(addr) != pmd_index(saddr) ||
	    vm_flags != svm_flags ||
	    sbase < svma->vm_start || svma->vm_end < s_end)
		return 0;

	return saddr;
}

static int vma_shareable(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long base = addr & PUD_MASK;
	unsigned long end = base + PUD_SIZE;

	/*
	 * check on proper vm_flags and page table alignment
	 */
	if (vma->vm_flags & VM_MAYSHARE &&
	    vma->vm_start <= base && end <= vma->vm_end)
		return 1;
	return 0;
}

/*
 * Search for a shareable pmd page for hugetlb. In any case calls pmd_alloc()
 * and returns the corresponding pte. While this is not necessary for the
 * !shared pmd case because we can allocate the pmd later as well, it makes the
 * code much cleaner. pmd allocation is essential for the shared case because
 * pud has to be populated inside the same i_mmap_mutex section - otherwise
 * racing tasks could either miss the sharing (see huge_pte_offset) or select a
 * bad pmd for sharing.
 */
static pte_t *
huge_pmd_share(struct mm_struct *mm, unsigned long addr, pud_t *pud)
{
	struct vm_area_struct *vma = find_vma(mm, addr);
	struct address_space *mapping = vma->vm_file->f_mapping;
	pgoff_t idx = ((addr - vma->vm_start) >> PAGE_SHIFT) +
			vma->vm_pgoff;
	struct vm_area_struct *svma;
	unsigned long saddr;
	pte_t *spte = NULL;
	pte_t *pte;

	if (!vma_shareable(vma, addr))
		return (pte_t *)pmd_alloc(mm, pud, addr);

	mutex_lock(&mapping->i_mmap_mutex);
	vma_interval_tree_foreach(svma, &mapping->i_mmap, idx, idx) {
		if (svma == vma)
			continue;

		saddr = page_table_shareable(svma, vma, addr, idx);
		if (saddr) {
			spte = huge_pte_offset(svma->vm_mm, saddr);
			if (spte) {
				get_page(virt_to_page(spte));
				break;
			}
		}
	}

	if (!spte)
		goto out;

	spin_lock(&mm->page_table_lock);
	if (pud_none(*pud))
		pud_populate(mm, pud, (pmd_t *)((unsigned long)spte & PAGE_MASK));
	else
		put_page(virt_to_page(spte));
	spin_unlock(&mm->page_table_lock);
out:
	pte = (pte_t *)pmd_alloc(mm, pud, addr);
	mutex_unlock(&mapping->i_mmap_mutex);
	return pte;
}

/*
 * unmap huge page backed by shared pte.
 *
 * Hugetlb pte page is ref counted at the time of mapping.  If pte is shared
 * indicated by page_count > 1, unmap is achieved by clearing pud and
 * decrementing the ref count. If count == 1, the pte page is not shared.
 *
 * called with vma->vm_mm->page_table_lock held.
 *
 * returns: 1 successfully unmapped a shared pte page
 *	    0 the underlying pte page is not shared, or it is the last user
 */
int huge_pmd_unshare(struct mm_struct *mm, unsigned long *addr, pte_t *ptep)
{
	pgd_t *pgd = pgd_offset(mm, *addr);
	pud_t *pud = pud_offset(pgd, *addr);

	BUG_ON(page_count(virt_to_page(ptep)) == 0);
	if (page_count(virt_to_page(ptep)) == 1)
		return 0;

	pud_clear(pud);
	put_page(virt_to_page(ptep));
	*addr = ALIGN(*addr, HPAGE_SIZE * PTRS_PER_PTE) - HPAGE_SIZE;
	return 1;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	pud = pud_alloc(mm, pgd, addr);
	if (pud) {
		if (sz == PUD_SIZE) {
			pte = (pte_t *)pud;
		} else {
			BUG_ON(sz != PMD_SIZE);
			if (pud_none(*pud))
				pte = huge_pmd_share(mm, addr, pud);
			else
				pte = (pte_t *)pmd_alloc(mm, pud, addr);
		}
	}
	BUG_ON(pte && !pte_none(*pte) && !pte_huge(*pte));

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_present(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (pud_present(*pud)) {
			if (pud_large(*pud))
				return (pte_t *)pud;
			pmd = pmd_offset(pud, addr);
		}
	}
	return (pte_t *) pmd;
}

#if 0	/* This is just for testing */
struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	unsigned long start = address;
	int length = 1;
	int nr;
	struct page *page;
	struct vm_area_struct *vma;

	vma = find_vma(mm, addr);
	if (!vma || !is_vm_hugetlb_page(vma))
		return ERR_PTR(-EINVAL);

	pte = huge_pte_offset(mm, address);

	/* hugetlb should be locked, and hence, prefaulted */
	WARN_ON(!pte || pte_none(*pte));

	page = &pte_page(*pte)[vpfn % (HPAGE_SIZE/PAGE_SIZE)];

	WARN_ON(!PageHead(page));

	return page;
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

int pud_huge(pud_t pud)
{
	return 0;
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	return NULL;
}

#else

struct page *
follow_huge_addr(struct mm_struct *mm, unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PSE);
}

int pud_huge(pud_t pud)
{
	return !!(pud_val(pud) & _PAGE_PSE);
}

struct page *
follow_huge_pmd(struct mm_struct *mm, unsigned long address,
		pmd_t *pmd, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pmd);
	if (page)
		page += ((address & ~PMD_MASK) >> PAGE_SHIFT);
	return page;
}

struct page *
follow_huge_pud(struct mm_struct *mm, unsigned long address,
		pud_t *pud, int write)
{
	struct page *page;

	page = pte_page(*(pte_t *)pud);
	if (page)
		page += ((address & ~PUD_MASK) >> PAGE_SHIFT);
	return page;
}

#endif

/* x86_64 also uses this file */

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
static unsigned long hugetlb_get_unmapped_area_bottomup(struct file *file,
		unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

static unsigned long hugetlb_get_unmapped_area_topdown(struct file *file,
		unsigned long addr0, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;
	unsigned long addr;

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = current->mm->mmap_base;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (addr & ~PAGE_MASK) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	if (mm->get_unmapped_area == arch_get_unmapped_area)
		return hugetlb_get_unmapped_area_bottomup(file, addr, len,
				pgoff, flags);
	else
		return hugetlb_get_unmapped_area_topdown(file, addr, len,
				pgoff, flags);
}

#endif /*HAVE_ARCH_HUGETLB_UNMAPPED_AREA*/

#ifdef CONFIG_X86_64
static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);
	if (ps == PMD_SIZE) {
		hugetlb_add_hstate(PMD_SHIFT - PAGE_SHIFT);
	} else if (ps == PUD_SIZE && cpu_has_gbpages) {
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	} else {
		printk(KERN_ERR "hugepagesz: Unsupported page size %lu M\n",
			ps >> 20);
		return 0;
	}
	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);
#endif
