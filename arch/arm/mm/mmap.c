/*
 *  linux/arch/arm/mm/mmap.c
 */
#include <linux/config.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shm.h>

#include <asm/system.h>

#define COLOUR_ALIGN(addr,pgoff)		\
	((((addr)+SHMLBA-1)&~(SHMLBA-1)) +	\
	 (((pgoff)<<PAGE_SHIFT) & (SHMLBA-1)))

/*
 * We need to ensure that shared mappings are correctly aligned to
 * avoid aliasing issues with VIPT caches.  We need to ensure that
 * a specific page of an object is always mapped at a multiple of
 * SHMLBA bytes.
 *
 * We unconditionally provide this function for all cases, however
 * in the VIVT case, we optimise out the alignment rules.
 */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long start_addr;
#ifdef CONFIG_CPU_V6
	unsigned int cache_type;
	int do_align = 0, aliasing = 0;

	/*
	 * We only need to do colour alignment if either the I or D
	 * caches alias.  This is indicated by bits 9 and 21 of the
	 * cache type register.
	 */
	cache_type = read_cpuid(CPUID_CACHETYPE);
	if (cache_type != read_cpuid(CPUID_ID)) {
		aliasing = (cache_type | cache_type >> 12) & (1 << 11);
		if (aliasing)
			do_align = filp || flags & MAP_SHARED;
	}
#else
#define do_align 0
#define aliasing 0
#endif

	/*
	 * We should enforce the MAP_FIXED case.  However, currently
	 * the generic kernel code doesn't allow us to handle this.
	 */
	if (flags & MAP_FIXED) {
		if (aliasing && flags & MAP_SHARED && addr & (SHMLBA - 1))
			return -EINVAL;
		return addr;
	}

	if (len > TASK_SIZE)
		return -ENOMEM;

	if (addr) {
		if (do_align)
			addr = COLOUR_ALIGN(addr, pgoff);
		else
			addr = PAGE_ALIGN(addr);

		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}
	start_addr = addr = mm->free_area_cache;

full_search:
	if (do_align)
		addr = COLOUR_ALIGN(addr, pgoff);
	else
		addr = PAGE_ALIGN(addr);

	for (vma = find_vma(mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr) {
			/*
			 * Start a new search - just in case we missed
			 * some holes.
			 */
			if (start_addr != TASK_UNMAPPED_BASE) {
				start_addr = addr = TASK_UNMAPPED_BASE;
				goto full_search;
			}
			return -ENOMEM;
		}
		if (!vma || addr + len <= vma->vm_start) {
			/*
			 * Remember the place where we stopped the search:
			 */
			mm->free_area_cache = addr + len;
			return addr;
		}
		addr = vma->vm_end;
		if (do_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
}

