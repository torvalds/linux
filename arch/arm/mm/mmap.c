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

static inline unsigned long COLOUR_ALIGN_DOWN(unsigned long addr,
					      unsigned long pgoff)
{
	unsigned long base = addr & ~(SHMLBA-1);
	unsigned long off = (pgoff << PAGE_SHIFT) & (SHMLBA-1);

	if (base + off <= addr)
		return base + off;

	return base - off;
}

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
	unsigned long start_addr;
	int do_align = 0;
	int aliasing = cache_is_vipt_aliasing();

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
	if (len > mm->cached_hole_size) {
	        start_addr = addr = mm->free_area_cache;
	} else {
	        start_addr = addr = mm->mmap_base;
	        mm->cached_hole_size = 0;
	}

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
				mm->cached_hole_size = 0;
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
		if (addr + mm->cached_hole_size < vma->vm_start)
		        mm->cached_hole_size = vma->vm_start - addr;
		addr = vma->vm_end;
		if (do_align)
			addr = COLOUR_ALIGN(addr, pgoff);
	}
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

	/* check if free_area_cache is useful for us */
	if (len <= mm->cached_hole_size) {
		mm->cached_hole_size = 0;
		mm->free_area_cache = mm->mmap_base;
	}

	/* either no address requested or can't fit in requested address hole */
	addr = mm->free_area_cache;
	if (do_align) {
		unsigned long base = COLOUR_ALIGN_DOWN(addr - len, pgoff);
		addr = base + len;
	}

	/* make sure it can fit in the remaining address space */
	if (addr > len) {
		vma = find_vma(mm, addr-len);
		if (!vma || addr <= vma->vm_start)
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr-len);
	}

	if (mm->mmap_base < len)
		goto bottomup;

	addr = mm->mmap_base - len;
	if (do_align)
		addr = COLOUR_ALIGN_DOWN(addr, pgoff);

	do {
		/*
		 * Lookup failure means no vma is above this address,
		 * else if new region fits below vma->vm_start,
		 * return with success:
		 */
		vma = find_vma(mm, addr);
		if (!vma || addr+len <= vma->vm_start)
			/* remember the address as a hint for next time */
			return (mm->free_area_cache = addr);

		/* remember the largest hole we saw so far */
		if (addr + mm->cached_hole_size < vma->vm_start)
			mm->cached_hole_size = vma->vm_start - addr;

		/* try just below the current vma->vm_start */
		addr = vma->vm_start - len;
		if (do_align)
			addr = COLOUR_ALIGN_DOWN(addr, pgoff);
	} while (len < vma->vm_start);

bottomup:
	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	mm->cached_hole_size = ~0UL;
	mm->free_area_cache = TASK_UNMAPPED_BASE;
	addr = arch_get_unmapped_area(filp, addr0, len, pgoff, flags);
	/*
	 * Restore the topdown base:
	 */
	mm->free_area_cache = mm->mmap_base;
	mm->cached_hole_size = ~0UL;

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
int valid_phys_addr_range(unsigned long addr, size_t size)
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
