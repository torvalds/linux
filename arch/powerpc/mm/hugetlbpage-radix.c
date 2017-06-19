#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>
#include <asm/mman.h>
#include <asm/tlb.h>

void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	radix__flush_tlb_page_psize(vma->vm_mm, vmaddr, psize);
}

void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	radix__local_flush_tlb_page_psize(vma->vm_mm, vmaddr, psize);
}

void radix__flush_hugetlb_tlb_range(struct vm_area_struct *vma, unsigned long start,
				   unsigned long end)
{
	int psize;
	struct hstate *hstate = hstate_file(vma->vm_file);

	psize = hstate_get_psize(hstate);
	radix__flush_tlb_range_psize(vma->vm_mm, start, end, psize);
}

/*
 * A vairant of hugetlb_get_unmapped_area doing topdown search
 * FIXME!! should we do as x86 does or non hugetlb area does ?
 * ie, use topdown or not based on mmap_is_legacy check ?
 */
unsigned long
radix__hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;

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
		    (!vma || addr + len <= vm_start_gap(vma)))
			return addr;
	}
	/*
	 * We are always doing an topdown search here. Slice code
	 * does that too.
	 */
	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = current->mm->mmap_base;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}
