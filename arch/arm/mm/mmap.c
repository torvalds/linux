/*
 *  linux/arch/arm/mm/mmap.c
 */
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shm.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/personality.h>
#include <linux/random.h>
#include <asm/cachetype.h>

#define COLOUR_ALIGN(addr,pgoff)		\
	((((addr)+SHMLBA-1)&~(SHMLBA-1)) +	\
	 (((pgoff)<<PAGE_SHIFT) & (SHMLBA-1)))

/* gap between mmap and stack */
#define MIN_GAP (128*1024*1024UL)
#define MAX_GAP ((TASK_SIZE)/6*5)

static int mmap_is_legacy(void)
{
	if (current->personality & ADDR_COMPAT_LAYOUT)
		return 1;

	if (rlimit(RLIMIT_STACK) == RLIM_INFINITY)
		return 1;

	return sysctl_legacy_va_layout;
}

static unsigned long mmap_base(unsigned long rnd)
{
	unsigned long gap = rlimit(RLIMIT_STACK);

	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)
		gap = MAX_GAP;

	return PAGE_ALIGN(TASK_SIZE - gap - rnd);
}

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
	int do_align = 0;
	int aliasing = cache_is_vipt_aliasing();
	struct vm_unmapped_area_info info;

	/*
	 * We only need to do colour alignment if either the I or D
	 * caches alias.
	 */
	if (aliasing)
		do_align = filp || (flags & MAP_SHARED);

	/*
	 * We enforce the MAP_FIXED case.
	 */
	if (flags & MAP_FIXED) {
		if (aliasing && flags & MAP_SHARED &&
		    (addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1))
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

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = do_align ? (PAGE_MASK & (SHMLBA - 1)) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;
	return vm_unmapped_area(&info);
}

unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			const unsigned long len, const unsigned long pgoff,
			const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	int do_align = 0;
	int aliasing = cache_is_vipt_aliasing();
	struct vm_unmapped_area_info info;

	/*
	 * We only need to do colour alignment if either the I or D
	 * caches alias.
	 */
	if (aliasing)
		do_align = filp || (flags & MAP_SHARED);

	/* requested length too big for entire address space */
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (aliasing && flags & MAP_SHARED &&
		    (addr - (pgoff << PAGE_SHIFT)) & (SHMLBA - 1))
			return -EINVAL;
		return addr;
	}

	/* requesting a specific address */
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

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = PAGE_SIZE;
	info.high_limit = mm->mmap_base;
	info.align_mask = do_align ? (PAGE_MASK & (SHMLBA - 1)) : 0;
	info.align_offset = pgoff << PAGE_SHIFT;
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
		info.low_limit = mm->mmap_base;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}

void arch_pick_mmap_layout(struct mm_struct *mm)
{
	unsigned long random_factor = 0UL;

	/* 8 bits of randomness in 20 address space bits */
	if ((current->flags & PF_RANDOMIZE) &&
	    !(current->personality & ADDR_NO_RANDOMIZE))
		random_factor = (get_random_int() % (1 << 8)) << PAGE_SHIFT;

	if (mmap_is_legacy()) {
		mm->mmap_base = TASK_UNMAPPED_BASE + random_factor;
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		mm->mmap_base = mmap_base(random_factor);
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}

/*
 * You really shouldn't be using read() or write() on /dev/mem.  This
 * might go away in the future.
 */
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	if (addr < PHYS_OFFSET)
		return 0;
	if (addr + size > __pa(high_memory - 1) + 1)
		return 0;

	return 1;
}

/*
 * We don't use supersection mappings for mmap() on /dev/mem, which
 * means that we can't map the memory area above the 4G barrier into
 * userspace.
 */
int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return !(pfn + (size >> PAGE_SHIFT) > 0x00100000);
}

#ifdef CONFIG_STRICT_DEVMEM

#include <linux/ioport.h>

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain
 * address is valid. The argument is a physical page number.
 * We mimic x86 here by disallowing access to system RAM as well as
 * device-exclusive MMIO regions. This effectively disable read()/write()
 * on /dev/mem.
 */
int devmem_is_allowed(unsigned long pfn)
{
	if (iomem_is_exclusive(pfn << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	return 0;
}

#endif
