#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/machdep.h>
#include <asm/mman.h>

void radix__flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned long ap, shift;
	struct hstate *hstate = hstate_file(vma->vm_file);

	shift = huge_page_shift(hstate);
	if (shift == mmu_psize_defs[MMU_PAGE_2M].shift)
		ap = mmu_get_ap(MMU_PAGE_2M);
	else if (shift == mmu_psize_defs[MMU_PAGE_1G].shift)
		ap = mmu_get_ap(MMU_PAGE_1G);
	else {
		WARN(1, "Wrong huge page shift\n");
		return ;
	}
	radix___flush_tlb_page(vma->vm_mm, vmaddr, ap, 0);
}

void radix__local_flush_hugetlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned long ap, shift;
	struct hstate *hstate = hstate_file(vma->vm_file);

	shift = huge_page_shift(hstate);
	if (shift == mmu_psize_defs[MMU_PAGE_2M].shift)
		ap = mmu_get_ap(MMU_PAGE_2M);
	else if (shift == mmu_psize_defs[MMU_PAGE_1G].shift)
		ap = mmu_get_ap(MMU_PAGE_1G);
	else {
		WARN(1, "Wrong huge page shift\n");
		return ;
	}
	radix___local_flush_tlb_page(vma->vm_mm, vmaddr, ap, 0);
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
		    (!vma || addr + len <= vma->vm_start))
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
