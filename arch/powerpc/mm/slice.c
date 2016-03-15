/*
 * address space "slices" (meta-segments) support
 *
 * Copyright (C) 2007 Benjamin Herrenschmidt, IBM Corporation.
 *
 * Based on hugetlb implementation
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/hugetlb.h>
#include <asm/mman.h>
#include <asm/mmu.h>
#include <asm/copro.h>
#include <asm/hugetlb.h>

/* some sanity checks */
#if (PGTABLE_RANGE >> 43) > SLICE_MASK_SIZE
#error PGTABLE_RANGE exceeds slice_mask high_slices size
#endif

static DEFINE_SPINLOCK(slice_convert_lock);


#ifdef DEBUG
int _slice_debug = 1;

static void slice_print_mask(const char *label, struct slice_mask mask)
{
	char	*p, buf[16 + 3 + 64 + 1];
	int	i;

	if (!_slice_debug)
		return;
	p = buf;
	for (i = 0; i < SLICE_NUM_LOW; i++)
		*(p++) = (mask.low_slices & (1 << i)) ? '1' : '0';
	*(p++) = ' ';
	*(p++) = '-';
	*(p++) = ' ';
	for (i = 0; i < SLICE_NUM_HIGH; i++)
		*(p++) = (mask.high_slices & (1ul << i)) ? '1' : '0';
	*(p++) = 0;

	printk(KERN_DEBUG "%s:%s\n", label, buf);
}

#define slice_dbg(fmt...) do { if (_slice_debug) pr_debug(fmt); } while(0)

#else

static void slice_print_mask(const char *label, struct slice_mask mask) {}
#define slice_dbg(fmt...)

#endif

static struct slice_mask slice_range_to_mask(unsigned long start,
					     unsigned long len)
{
	unsigned long end = start + len - 1;
	struct slice_mask ret = { 0, 0 };

	if (start < SLICE_LOW_TOP) {
		unsigned long mend = min(end, SLICE_LOW_TOP);
		unsigned long mstart = min(start, SLICE_LOW_TOP);

		ret.low_slices = (1u << (GET_LOW_SLICE_INDEX(mend) + 1))
			- (1u << GET_LOW_SLICE_INDEX(mstart));
	}

	if ((start + len) > SLICE_LOW_TOP)
		ret.high_slices = (1ul << (GET_HIGH_SLICE_INDEX(end) + 1))
			- (1ul << GET_HIGH_SLICE_INDEX(start));

	return ret;
}

static int slice_area_is_free(struct mm_struct *mm, unsigned long addr,
			      unsigned long len)
{
	struct vm_area_struct *vma;

	if ((mm->task_size - len) < addr)
		return 0;
	vma = find_vma(mm, addr);
	return (!vma || (addr + len) <= vma->vm_start);
}

static int slice_low_has_vma(struct mm_struct *mm, unsigned long slice)
{
	return !slice_area_is_free(mm, slice << SLICE_LOW_SHIFT,
				   1ul << SLICE_LOW_SHIFT);
}

static int slice_high_has_vma(struct mm_struct *mm, unsigned long slice)
{
	unsigned long start = slice << SLICE_HIGH_SHIFT;
	unsigned long end = start + (1ul << SLICE_HIGH_SHIFT);

	/* Hack, so that each addresses is controlled by exactly one
	 * of the high or low area bitmaps, the first high area starts
	 * at 4GB, not 0 */
	if (start == 0)
		start = SLICE_LOW_TOP;

	return !slice_area_is_free(mm, start, end - start);
}

static struct slice_mask slice_mask_for_free(struct mm_struct *mm)
{
	struct slice_mask ret = { 0, 0 };
	unsigned long i;

	for (i = 0; i < SLICE_NUM_LOW; i++)
		if (!slice_low_has_vma(mm, i))
			ret.low_slices |= 1u << i;

	if (mm->task_size <= SLICE_LOW_TOP)
		return ret;

	for (i = 0; i < SLICE_NUM_HIGH; i++)
		if (!slice_high_has_vma(mm, i))
			ret.high_slices |= 1ul << i;

	return ret;
}

static struct slice_mask slice_mask_for_size(struct mm_struct *mm, int psize)
{
	unsigned char *hpsizes;
	int index, mask_index;
	struct slice_mask ret = { 0, 0 };
	unsigned long i;
	u64 lpsizes;

	lpsizes = mm->context.low_slices_psize;
	for (i = 0; i < SLICE_NUM_LOW; i++)
		if (((lpsizes >> (i * 4)) & 0xf) == psize)
			ret.low_slices |= 1u << i;

	hpsizes = mm->context.high_slices_psize;
	for (i = 0; i < SLICE_NUM_HIGH; i++) {
		mask_index = i & 0x1;
		index = i >> 1;
		if (((hpsizes[index] >> (mask_index * 4)) & 0xf) == psize)
			ret.high_slices |= 1ul << i;
	}

	return ret;
}

static int slice_check_fit(struct slice_mask mask, struct slice_mask available)
{
	return (mask.low_slices & available.low_slices) == mask.low_slices &&
		(mask.high_slices & available.high_slices) == mask.high_slices;
}

static void slice_flush_segments(void *parm)
{
	struct mm_struct *mm = parm;
	unsigned long flags;

	if (mm != current->active_mm)
		return;

	copy_mm_to_paca(&current->active_mm->context);

	local_irq_save(flags);
	slb_flush_and_rebolt();
	local_irq_restore(flags);
}

static void slice_convert(struct mm_struct *mm, struct slice_mask mask, int psize)
{
	int index, mask_index;
	/* Write the new slice psize bits */
	unsigned char *hpsizes;
	u64 lpsizes;
	unsigned long i, flags;

	slice_dbg("slice_convert(mm=%p, psize=%d)\n", mm, psize);
	slice_print_mask(" mask", mask);

	/* We need to use a spinlock here to protect against
	 * concurrent 64k -> 4k demotion ...
	 */
	spin_lock_irqsave(&slice_convert_lock, flags);

	lpsizes = mm->context.low_slices_psize;
	for (i = 0; i < SLICE_NUM_LOW; i++)
		if (mask.low_slices & (1u << i))
			lpsizes = (lpsizes & ~(0xful << (i * 4))) |
				(((unsigned long)psize) << (i * 4));

	/* Assign the value back */
	mm->context.low_slices_psize = lpsizes;

	hpsizes = mm->context.high_slices_psize;
	for (i = 0; i < SLICE_NUM_HIGH; i++) {
		mask_index = i & 0x1;
		index = i >> 1;
		if (mask.high_slices & (1ul << i))
			hpsizes[index] = (hpsizes[index] &
					  ~(0xf << (mask_index * 4))) |
				(((unsigned long)psize) << (mask_index * 4));
	}

	slice_dbg(" lsps=%lx, hsps=%lx\n",
		  mm->context.low_slices_psize,
		  mm->context.high_slices_psize);

	spin_unlock_irqrestore(&slice_convert_lock, flags);

	copro_flush_all_slbs(mm);
}

/*
 * Compute which slice addr is part of;
 * set *boundary_addr to the start or end boundary of that slice
 * (depending on 'end' parameter);
 * return boolean indicating if the slice is marked as available in the
 * 'available' slice_mark.
 */
static bool slice_scan_available(unsigned long addr,
				 struct slice_mask available,
				 int end,
				 unsigned long *boundary_addr)
{
	unsigned long slice;
	if (addr < SLICE_LOW_TOP) {
		slice = GET_LOW_SLICE_INDEX(addr);
		*boundary_addr = (slice + end) << SLICE_LOW_SHIFT;
		return !!(available.low_slices & (1u << slice));
	} else {
		slice = GET_HIGH_SLICE_INDEX(addr);
		*boundary_addr = (slice + end) ?
			((slice + end) << SLICE_HIGH_SHIFT) : SLICE_LOW_TOP;
		return !!(available.high_slices & (1ul << slice));
	}
}

static unsigned long slice_find_area_bottomup(struct mm_struct *mm,
					      unsigned long len,
					      struct slice_mask available,
					      int psize)
{
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	unsigned long addr, found, next_end;
	struct vm_unmapped_area_info info;

	info.flags = 0;
	info.length = len;
	info.align_mask = PAGE_MASK & ((1ul << pshift) - 1);
	info.align_offset = 0;

	addr = TASK_UNMAPPED_BASE;
	while (addr < TASK_SIZE) {
		info.low_limit = addr;
		if (!slice_scan_available(addr, available, 1, &addr))
			continue;

 next_slice:
		/*
		 * At this point [info.low_limit; addr) covers
		 * available slices only and ends at a slice boundary.
		 * Check if we need to reduce the range, or if we can
		 * extend it to cover the next available slice.
		 */
		if (addr >= TASK_SIZE)
			addr = TASK_SIZE;
		else if (slice_scan_available(addr, available, 1, &next_end)) {
			addr = next_end;
			goto next_slice;
		}
		info.high_limit = addr;

		found = vm_unmapped_area(&info);
		if (!(found & ~PAGE_MASK))
			return found;
	}

	return -ENOMEM;
}

static unsigned long slice_find_area_topdown(struct mm_struct *mm,
					     unsigned long len,
					     struct slice_mask available,
					     int psize)
{
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	unsigned long addr, found, prev;
	struct vm_unmapped_area_info info;

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.align_mask = PAGE_MASK & ((1ul << pshift) - 1);
	info.align_offset = 0;

	addr = mm->mmap_base;
	while (addr > PAGE_SIZE) {
		info.high_limit = addr;
		if (!slice_scan_available(addr - 1, available, 0, &addr))
			continue;

 prev_slice:
		/*
		 * At this point [addr; info.high_limit) covers
		 * available slices only and starts at a slice boundary.
		 * Check if we need to reduce the range, or if we can
		 * extend it to cover the previous available slice.
		 */
		if (addr < PAGE_SIZE)
			addr = PAGE_SIZE;
		else if (slice_scan_available(addr - 1, available, 0, &prev)) {
			addr = prev;
			goto prev_slice;
		}
		info.low_limit = addr;

		found = vm_unmapped_area(&info);
		if (!(found & ~PAGE_MASK))
			return found;
	}

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	return slice_find_area_bottomup(mm, len, available, psize);
}


static unsigned long slice_find_area(struct mm_struct *mm, unsigned long len,
				     struct slice_mask mask, int psize,
				     int topdown)
{
	if (topdown)
		return slice_find_area_topdown(mm, len, mask, psize);
	else
		return slice_find_area_bottomup(mm, len, mask, psize);
}

#define or_mask(dst, src)	do {			\
	(dst).low_slices |= (src).low_slices;		\
	(dst).high_slices |= (src).high_slices;		\
} while (0)

#define andnot_mask(dst, src)	do {			\
	(dst).low_slices &= ~(src).low_slices;		\
	(dst).high_slices &= ~(src).high_slices;	\
} while (0)

#ifdef CONFIG_PPC_64K_PAGES
#define MMU_PAGE_BASE	MMU_PAGE_64K
#else
#define MMU_PAGE_BASE	MMU_PAGE_4K
#endif

unsigned long slice_get_unmapped_area(unsigned long addr, unsigned long len,
				      unsigned long flags, unsigned int psize,
				      int topdown)
{
	struct slice_mask mask = {0, 0};
	struct slice_mask good_mask;
	struct slice_mask potential_mask = {0,0} /* silence stupid warning */;
	struct slice_mask compat_mask = {0, 0};
	int fixed = (flags & MAP_FIXED);
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	struct mm_struct *mm = current->mm;
	unsigned long newaddr;

	/* Sanity checks */
	BUG_ON(mm->task_size == 0);

	slice_dbg("slice_get_unmapped_area(mm=%p, psize=%d...\n", mm, psize);
	slice_dbg(" addr=%lx, len=%lx, flags=%lx, topdown=%d\n",
		  addr, len, flags, topdown);

	if (len > mm->task_size)
		return -ENOMEM;
	if (len & ((1ul << pshift) - 1))
		return -EINVAL;
	if (fixed && (addr & ((1ul << pshift) - 1)))
		return -EINVAL;
	if (fixed && addr > (mm->task_size - len))
		return -ENOMEM;

	/* If hint, make sure it matches our alignment restrictions */
	if (!fixed && addr) {
		addr = _ALIGN_UP(addr, 1ul << pshift);
		slice_dbg(" aligned addr=%lx\n", addr);
		/* Ignore hint if it's too large or overlaps a VMA */
		if (addr > mm->task_size - len ||
		    !slice_area_is_free(mm, addr, len))
			addr = 0;
	}

	/* First make up a "good" mask of slices that have the right size
	 * already
	 */
	good_mask = slice_mask_for_size(mm, psize);
	slice_print_mask(" good_mask", good_mask);

	/*
	 * Here "good" means slices that are already the right page size,
	 * "compat" means slices that have a compatible page size (i.e.
	 * 4k in a 64k pagesize kernel), and "free" means slices without
	 * any VMAs.
	 *
	 * If MAP_FIXED:
	 *	check if fits in good | compat => OK
	 *	check if fits in good | compat | free => convert free
	 *	else bad
	 * If have hint:
	 *	check if hint fits in good => OK
	 *	check if hint fits in good | free => convert free
	 * Otherwise:
	 *	search in good, found => OK
	 *	search in good | free, found => convert free
	 *	search in good | compat | free, found => convert free.
	 */

#ifdef CONFIG_PPC_64K_PAGES
	/* If we support combo pages, we can allow 64k pages in 4k slices */
	if (psize == MMU_PAGE_64K) {
		compat_mask = slice_mask_for_size(mm, MMU_PAGE_4K);
		if (fixed)
			or_mask(good_mask, compat_mask);
	}
#endif

	/* First check hint if it's valid or if we have MAP_FIXED */
	if (addr != 0 || fixed) {
		/* Build a mask for the requested range */
		mask = slice_range_to_mask(addr, len);
		slice_print_mask(" mask", mask);

		/* Check if we fit in the good mask. If we do, we just return,
		 * nothing else to do
		 */
		if (slice_check_fit(mask, good_mask)) {
			slice_dbg(" fits good !\n");
			return addr;
		}
	} else {
		/* Now let's see if we can find something in the existing
		 * slices for that size
		 */
		newaddr = slice_find_area(mm, len, good_mask, psize, topdown);
		if (newaddr != -ENOMEM) {
			/* Found within the good mask, we don't have to setup,
			 * we thus return directly
			 */
			slice_dbg(" found area at 0x%lx\n", newaddr);
			return newaddr;
		}
	}

	/* We don't fit in the good mask, check what other slices are
	 * empty and thus can be converted
	 */
	potential_mask = slice_mask_for_free(mm);
	or_mask(potential_mask, good_mask);
	slice_print_mask(" potential", potential_mask);

	if ((addr != 0 || fixed) && slice_check_fit(mask, potential_mask)) {
		slice_dbg(" fits potential !\n");
		goto convert;
	}

	/* If we have MAP_FIXED and failed the above steps, then error out */
	if (fixed)
		return -EBUSY;

	slice_dbg(" search...\n");

	/* If we had a hint that didn't work out, see if we can fit
	 * anywhere in the good area.
	 */
	if (addr) {
		addr = slice_find_area(mm, len, good_mask, psize, topdown);
		if (addr != -ENOMEM) {
			slice_dbg(" found area at 0x%lx\n", addr);
			return addr;
		}
	}

	/* Now let's see if we can find something in the existing slices
	 * for that size plus free slices
	 */
	addr = slice_find_area(mm, len, potential_mask, psize, topdown);

#ifdef CONFIG_PPC_64K_PAGES
	if (addr == -ENOMEM && psize == MMU_PAGE_64K) {
		/* retry the search with 4k-page slices included */
		or_mask(potential_mask, compat_mask);
		addr = slice_find_area(mm, len, potential_mask, psize,
				       topdown);
	}
#endif

	if (addr == -ENOMEM)
		return -ENOMEM;

	mask = slice_range_to_mask(addr, len);
	slice_dbg(" found potential area at 0x%lx\n", addr);
	slice_print_mask(" mask", mask);

 convert:
	andnot_mask(mask, good_mask);
	andnot_mask(mask, compat_mask);
	if (mask.low_slices || mask.high_slices) {
		slice_convert(mm, mask, psize);
		if (psize > MMU_PAGE_BASE)
			on_each_cpu(slice_flush_segments, mm, 1);
	}
	return addr;

}
EXPORT_SYMBOL_GPL(slice_get_unmapped_area);

unsigned long arch_get_unmapped_area(struct file *filp,
				     unsigned long addr,
				     unsigned long len,
				     unsigned long pgoff,
				     unsigned long flags)
{
	return slice_get_unmapped_area(addr, len, flags,
				       current->mm->context.user_psize, 0);
}

unsigned long arch_get_unmapped_area_topdown(struct file *filp,
					     const unsigned long addr0,
					     const unsigned long len,
					     const unsigned long pgoff,
					     const unsigned long flags)
{
	return slice_get_unmapped_area(addr0, len, flags,
				       current->mm->context.user_psize, 1);
}

unsigned int get_slice_psize(struct mm_struct *mm, unsigned long addr)
{
	unsigned char *hpsizes;
	int index, mask_index;

	if (addr < SLICE_LOW_TOP) {
		u64 lpsizes;
		lpsizes = mm->context.low_slices_psize;
		index = GET_LOW_SLICE_INDEX(addr);
		return (lpsizes >> (index * 4)) & 0xf;
	}
	hpsizes = mm->context.high_slices_psize;
	index = GET_HIGH_SLICE_INDEX(addr);
	mask_index = index & 0x1;
	return (hpsizes[index >> 1] >> (mask_index * 4)) & 0xf;
}
EXPORT_SYMBOL_GPL(get_slice_psize);

/*
 * This is called by hash_page when it needs to do a lazy conversion of
 * an address space from real 64K pages to combo 4K pages (typically
 * when hitting a non cacheable mapping on a processor or hypervisor
 * that won't allow them for 64K pages).
 *
 * This is also called in init_new_context() to change back the user
 * psize from whatever the parent context had it set to
 * N.B. This may be called before mm->context.id has been set.
 *
 * This function will only change the content of the {low,high)_slice_psize
 * masks, it will not flush SLBs as this shall be handled lazily by the
 * caller.
 */
void slice_set_user_psize(struct mm_struct *mm, unsigned int psize)
{
	int index, mask_index;
	unsigned char *hpsizes;
	unsigned long flags, lpsizes;
	unsigned int old_psize;
	int i;

	slice_dbg("slice_set_user_psize(mm=%p, psize=%d)\n", mm, psize);

	spin_lock_irqsave(&slice_convert_lock, flags);

	old_psize = mm->context.user_psize;
	slice_dbg(" old_psize=%d\n", old_psize);
	if (old_psize == psize)
		goto bail;

	mm->context.user_psize = psize;
	wmb();

	lpsizes = mm->context.low_slices_psize;
	for (i = 0; i < SLICE_NUM_LOW; i++)
		if (((lpsizes >> (i * 4)) & 0xf) == old_psize)
			lpsizes = (lpsizes & ~(0xful << (i * 4))) |
				(((unsigned long)psize) << (i * 4));
	/* Assign the value back */
	mm->context.low_slices_psize = lpsizes;

	hpsizes = mm->context.high_slices_psize;
	for (i = 0; i < SLICE_NUM_HIGH; i++) {
		mask_index = i & 0x1;
		index = i >> 1;
		if (((hpsizes[index] >> (mask_index * 4)) & 0xf) == old_psize)
			hpsizes[index] = (hpsizes[index] &
					  ~(0xf << (mask_index * 4))) |
				(((unsigned long)psize) << (mask_index * 4));
	}




	slice_dbg(" lsps=%lx, hsps=%lx\n",
		  mm->context.low_slices_psize,
		  mm->context.high_slices_psize);

 bail:
	spin_unlock_irqrestore(&slice_convert_lock, flags);
}

void slice_set_range_psize(struct mm_struct *mm, unsigned long start,
			   unsigned long len, unsigned int psize)
{
	struct slice_mask mask = slice_range_to_mask(start, len);

	slice_convert(mm, mask, psize);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * is_hugepage_only_range() is used by generic code to verify whether
 * a normal mmap mapping (non hugetlbfs) is valid on a given area.
 *
 * until the generic code provides a more generic hook and/or starts
 * calling arch get_unmapped_area for MAP_FIXED (which our implementation
 * here knows how to deal with), we hijack it to keep standard mappings
 * away from us.
 *
 * because of that generic code limitation, MAP_FIXED mapping cannot
 * "convert" back a slice with no VMAs to the standard page size, only
 * get_unmapped_area() can. It would be possible to fix it here but I
 * prefer working on fixing the generic code instead.
 *
 * WARNING: This will not work if hugetlbfs isn't enabled since the
 * generic code will redefine that function as 0 in that. This is ok
 * for now as we only use slices with hugetlbfs enabled. This should
 * be fixed as the generic code gets fixed.
 */
int is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len)
{
	struct slice_mask mask, available;
	unsigned int psize = mm->context.user_psize;

	mask = slice_range_to_mask(addr, len);
	available = slice_mask_for_size(mm, psize);
#ifdef CONFIG_PPC_64K_PAGES
	/* We need to account for 4k slices too */
	if (psize == MMU_PAGE_64K) {
		struct slice_mask compat_mask;
		compat_mask = slice_mask_for_size(mm, MMU_PAGE_4K);
		or_mask(available, compat_mask);
	}
#endif

#if 0 /* too verbose */
	slice_dbg("is_hugepage_only_range(mm=%p, addr=%lx, len=%lx)\n",
		 mm, addr, len);
	slice_print_mask(" mask", mask);
	slice_print_mask(" available", available);
#endif
	return !slice_check_fit(mask, available);
}
#endif
