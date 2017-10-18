/*
 * arch/metag/mm/hugetlbpage.c
 *
 * METAG HugeTLB page support.
 *
 * Cloned from SuperH
 *
 * Cloned from sparc64 by Paul Mundt.
 *
 * Copyright (C) 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

/*
 * If the arch doesn't supply something else, assume that hugepage
 * size aligned regions are ok without further preparation.
 */
int prepare_hugepage_range(struct file *file, unsigned long addr,
						unsigned long len)
{
	struct mm_struct *mm = current->mm;
	struct hstate *h = hstate_file(file);
	struct vm_area_struct *vma;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;
	if (TASK_SIZE - len < addr)
		return -EINVAL;

	vma = find_vma(mm, ALIGN_HUGEPT(addr));
	if (vma && !(vma->vm_flags & MAP_HUGETLB))
		return -EINVAL;

	vma = find_vma(mm, addr);
	if (vma) {
		if (addr + len > vma->vm_start)
			return -EINVAL;
		if (!(vma->vm_flags & MAP_HUGETLB) &&
		    (ALIGN_HUGEPT(addr + len) > vma->vm_start))
			return -EINVAL;
	}
	return 0;
}

pte_t *huge_pte_alloc(struct mm_struct *mm,
			unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	pte = pte_alloc_map(mm, pmd, addr);
	pgd->pgd &= ~_PAGE_SZ_MASK;
	pgd->pgd |= _PAGE_SZHUGE;

	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm,
		       unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	pud = pud_offset(pgd, addr);
	pmd = pmd_offset(pud, addr);
	pte = pte_offset_kernel(pmd, addr);

	return pte;
}

int pmd_huge(pmd_t pmd)
{
	return pmd_page_shift(pmd) > PAGE_SHIFT;
}

int pud_huge(pud_t pud)
{
	return 0;
}

struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
			     pmd_t *pmd, int write)
{
	return NULL;
}

#ifdef HAVE_ARCH_HUGETLB_UNMAPPED_AREA

/*
 * Look for an unmapped area starting after another hugetlb vma.
 * There are guaranteed to be no huge pte's spare if all the huge pages are
 * full size (4MB), so in that case compile out this search.
 */
#if HPAGE_SHIFT == HUGEPT_SHIFT
static inline unsigned long
hugetlb_get_unmapped_area_existing(unsigned long len)
{
	return 0;
}
#else
static unsigned long
hugetlb_get_unmapped_area_existing(unsigned long len)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr, addr;
	int after_huge;

	if (mm->context.part_huge) {
		start_addr = mm->context.part_huge;
		after_huge = 1;
	} else {
		start_addr = TASK_UNMAPPED_BASE;
		after_huge = 0;
	}
new_search:
	addr = start_addr;

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		if ((!vma && !after_huge) || TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = TASK_UNMAPPED_BASE;
				goto new_search;
			}
			return 0;
		}
		/* skip ahead if we've aligned right over some vmas */
		if (vma && vma->vm_end <= addr)
			continue;
		/* space before the next vma? */
		if (after_huge && (!vma || ALIGN_HUGEPT(addr + len)
			    <= vma->vm_start)) {
			unsigned long end = addr + len;
			if (end & HUGEPT_MASK)
				mm->context.part_huge = end;
			else if (addr == mm->context.part_huge)
				mm->context.part_huge = 0;
			return addr;
		}
		if (vma->vm_flags & MAP_HUGETLB) {
			/* space after a huge vma in 2nd level page table? */
			if (vma->vm_end & HUGEPT_MASK) {
				after_huge = 1;
				/* no need to align to the next PT block */
				addr = vma->vm_end;
				continue;
			}
		}
		after_huge = 0;
		addr = ALIGN_HUGEPT(vma->vm_end);
	}
}
#endif

/* Do a full search to find an area without any nearby normal pages. */
static unsigned long
hugetlb_get_unmapped_area_new_pmd(unsigned long len)
{
	struct vm_unmapped_area_info info;

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & HUGEPT_MASK;
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}

unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct hstate *h = hstate_file(file);

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
		if (!prepare_hugepage_range(file, addr, len))
			return addr;
	}

	/*
	 * Look for an existing hugetlb vma with space after it (this is to to
	 * minimise fragmentation caused by huge pages.
	 */
	addr = hugetlb_get_unmapped_area_existing(len);
	if (addr)
		return addr;

	/*
	 * Find an unmapped naturally aligned set of 4MB blocks that we can use
	 * for huge pages.
	 */
	return hugetlb_get_unmapped_area_new_pmd(len);
}

#endif /*HAVE_ARCH_HUGETLB_UNMAPPED_AREA*/

/* necessary for boot time 4MB huge page allocation */
static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);
	if (ps == (1 << HPAGE_SHIFT)) {
		hugetlb_add_hstate(HPAGE_SHIFT - PAGE_SHIFT);
	} else {
		hugetlb_bad_size();
		pr_err("hugepagesz: Unsupported page size %lu M\n",
		       ps >> 20);
		return 0;
	}
	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);
