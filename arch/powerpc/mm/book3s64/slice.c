// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * address space "slices" (meta-segments) support
 *
 * Copyright (C) 2007 Benjamin Herrenschmidt, IBM Corporation.
 *
 * Based on hugetlb implementation
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/hugetlb.h>
#include <linux/sched/mm.h>
#include <linux/security.h>
#include <asm/mman.h>
#include <asm/mmu.h>
#include <asm/copro.h>
#include <asm/hugetlb.h>
#include <asm/mmu_context.h>

static DEFINE_SPINLOCK(slice_convert_lock);

#ifdef DEBUG
int _slice_debug = 1;

static void slice_print_mask(const char *label, const struct slice_mask *mask)
{
	if (!_slice_debug)
		return;
	pr_devel("%s low_slice: %*pbl\n", label,
			(int)SLICE_NUM_LOW, &mask->low_slices);
	pr_devel("%s high_slice: %*pbl\n", label,
			(int)SLICE_NUM_HIGH, mask->high_slices);
}

#define slice_dbg(fmt...) do { if (_slice_debug) pr_devel(fmt); } while (0)

#else

static void slice_print_mask(const char *label, const struct slice_mask *mask) {}
#define slice_dbg(fmt...)

#endif

static inline notrace bool slice_addr_is_low(unsigned long addr)
{
	u64 tmp = (u64)addr;

	return tmp < SLICE_LOW_TOP;
}

static void slice_range_to_mask(unsigned long start, unsigned long len,
				struct slice_mask *ret)
{
	unsigned long end = start + len - 1;

	ret->low_slices = 0;
	if (SLICE_NUM_HIGH)
		bitmap_zero(ret->high_slices, SLICE_NUM_HIGH);

	if (slice_addr_is_low(start)) {
		unsigned long mend = min(end,
					 (unsigned long)(SLICE_LOW_TOP - 1));

		ret->low_slices = (1u << (GET_LOW_SLICE_INDEX(mend) + 1))
			- (1u << GET_LOW_SLICE_INDEX(start));
	}

	if (SLICE_NUM_HIGH && !slice_addr_is_low(end)) {
		unsigned long start_index = GET_HIGH_SLICE_INDEX(start);
		unsigned long align_end = ALIGN(end, (1UL << SLICE_HIGH_SHIFT));
		unsigned long count = GET_HIGH_SLICE_INDEX(align_end) - start_index;

		bitmap_set(ret->high_slices, start_index, count);
	}
}

static int slice_area_is_free(struct mm_struct *mm, unsigned long addr,
			      unsigned long len)
{
	struct vm_area_struct *vma;

	if ((mm_ctx_slb_addr_limit(&mm->context) - len) < addr)
		return 0;
	vma = find_vma(mm, addr);
	return (!vma || (addr + len) <= vm_start_gap(vma));
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
		start = (unsigned long)SLICE_LOW_TOP;

	return !slice_area_is_free(mm, start, end - start);
}

static void slice_mask_for_free(struct mm_struct *mm, struct slice_mask *ret,
				unsigned long high_limit)
{
	unsigned long i;

	ret->low_slices = 0;
	if (SLICE_NUM_HIGH)
		bitmap_zero(ret->high_slices, SLICE_NUM_HIGH);

	for (i = 0; i < SLICE_NUM_LOW; i++)
		if (!slice_low_has_vma(mm, i))
			ret->low_slices |= 1u << i;

	if (slice_addr_is_low(high_limit - 1))
		return;

	for (i = 0; i < GET_HIGH_SLICE_INDEX(high_limit); i++)
		if (!slice_high_has_vma(mm, i))
			__set_bit(i, ret->high_slices);
}

static bool slice_check_range_fits(struct mm_struct *mm,
			   const struct slice_mask *available,
			   unsigned long start, unsigned long len)
{
	unsigned long end = start + len - 1;
	u64 low_slices = 0;

	if (slice_addr_is_low(start)) {
		unsigned long mend = min(end,
					 (unsigned long)(SLICE_LOW_TOP - 1));

		low_slices = (1u << (GET_LOW_SLICE_INDEX(mend) + 1))
				- (1u << GET_LOW_SLICE_INDEX(start));
	}
	if ((low_slices & available->low_slices) != low_slices)
		return false;

	if (SLICE_NUM_HIGH && !slice_addr_is_low(end)) {
		unsigned long start_index = GET_HIGH_SLICE_INDEX(start);
		unsigned long align_end = ALIGN(end, (1UL << SLICE_HIGH_SHIFT));
		unsigned long count = GET_HIGH_SLICE_INDEX(align_end) - start_index;
		unsigned long i;

		for (i = start_index; i < start_index + count; i++) {
			if (!test_bit(i, available->high_slices))
				return false;
		}
	}

	return true;
}

static void slice_flush_segments(void *parm)
{
#ifdef CONFIG_PPC64
	struct mm_struct *mm = parm;
	unsigned long flags;

	if (mm != current->active_mm)
		return;

	copy_mm_to_paca(current->active_mm);

	local_irq_save(flags);
	slb_flush_and_restore_bolted();
	local_irq_restore(flags);
#endif
}

static void slice_convert(struct mm_struct *mm,
				const struct slice_mask *mask, int psize)
{
	int index, mask_index;
	/* Write the new slice psize bits */
	unsigned char *hpsizes, *lpsizes;
	struct slice_mask *psize_mask, *old_mask;
	unsigned long i, flags;
	int old_psize;

	slice_dbg("slice_convert(mm=%p, psize=%d)\n", mm, psize);
	slice_print_mask(" mask", mask);

	psize_mask = slice_mask_for_size(&mm->context, psize);

	/* We need to use a spinlock here to protect against
	 * concurrent 64k -> 4k demotion ...
	 */
	spin_lock_irqsave(&slice_convert_lock, flags);

	lpsizes = mm_ctx_low_slices(&mm->context);
	for (i = 0; i < SLICE_NUM_LOW; i++) {
		if (!(mask->low_slices & (1u << i)))
			continue;

		mask_index = i & 0x1;
		index = i >> 1;

		/* Update the slice_mask */
		old_psize = (lpsizes[index] >> (mask_index * 4)) & 0xf;
		old_mask = slice_mask_for_size(&mm->context, old_psize);
		old_mask->low_slices &= ~(1u << i);
		psize_mask->low_slices |= 1u << i;

		/* Update the sizes array */
		lpsizes[index] = (lpsizes[index] & ~(0xf << (mask_index * 4))) |
				(((unsigned long)psize) << (mask_index * 4));
	}

	hpsizes = mm_ctx_high_slices(&mm->context);
	for (i = 0; i < GET_HIGH_SLICE_INDEX(mm_ctx_slb_addr_limit(&mm->context)); i++) {
		if (!test_bit(i, mask->high_slices))
			continue;

		mask_index = i & 0x1;
		index = i >> 1;

		/* Update the slice_mask */
		old_psize = (hpsizes[index] >> (mask_index * 4)) & 0xf;
		old_mask = slice_mask_for_size(&mm->context, old_psize);
		__clear_bit(i, old_mask->high_slices);
		__set_bit(i, psize_mask->high_slices);

		/* Update the sizes array */
		hpsizes[index] = (hpsizes[index] & ~(0xf << (mask_index * 4))) |
				(((unsigned long)psize) << (mask_index * 4));
	}

	slice_dbg(" lsps=%lx, hsps=%lx\n",
		  (unsigned long)mm_ctx_low_slices(&mm->context),
		  (unsigned long)mm_ctx_high_slices(&mm->context));

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
				 const struct slice_mask *available,
				 int end, unsigned long *boundary_addr)
{
	unsigned long slice;
	if (slice_addr_is_low(addr)) {
		slice = GET_LOW_SLICE_INDEX(addr);
		*boundary_addr = (slice + end) << SLICE_LOW_SHIFT;
		return !!(available->low_slices & (1u << slice));
	} else {
		slice = GET_HIGH_SLICE_INDEX(addr);
		*boundary_addr = (slice + end) ?
			((slice + end) << SLICE_HIGH_SHIFT) : SLICE_LOW_TOP;
		return !!test_bit(slice, available->high_slices);
	}
}

static unsigned long slice_find_area_bottomup(struct mm_struct *mm,
					      unsigned long addr, unsigned long len,
					      const struct slice_mask *available,
					      int psize, unsigned long high_limit)
{
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	unsigned long found, next_end;
	struct vm_unmapped_area_info info = {
		.length = len,
		.align_mask = PAGE_MASK & ((1ul << pshift) - 1),
	};
	/*
	 * Check till the allow max value for this mmap request
	 */
	while (addr < high_limit) {
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
		if (addr >= high_limit)
			addr = high_limit;
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
					     unsigned long addr, unsigned long len,
					     const struct slice_mask *available,
					     int psize, unsigned long high_limit)
{
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	unsigned long found, prev;
	struct vm_unmapped_area_info info = {
		.flags = VM_UNMAPPED_AREA_TOPDOWN,
		.length = len,
		.align_mask = PAGE_MASK & ((1ul << pshift) - 1),
	};
	unsigned long min_addr = max(PAGE_SIZE, mmap_min_addr);

	/*
	 * If we are trying to allocate above DEFAULT_MAP_WINDOW
	 * Add the different to the mmap_base.
	 * Only for that request for which high_limit is above
	 * DEFAULT_MAP_WINDOW we should apply this.
	 */
	if (high_limit > DEFAULT_MAP_WINDOW)
		addr += mm_ctx_slb_addr_limit(&mm->context) - DEFAULT_MAP_WINDOW;

	while (addr > min_addr) {
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
		if (addr < min_addr)
			addr = min_addr;
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
	return slice_find_area_bottomup(mm, TASK_UNMAPPED_BASE, len, available, psize, high_limit);
}


static unsigned long slice_find_area(struct mm_struct *mm, unsigned long len,
				     const struct slice_mask *mask, int psize,
				     int topdown, unsigned long high_limit)
{
	if (topdown)
		return slice_find_area_topdown(mm, mm->mmap_base, len, mask, psize, high_limit);
	else
		return slice_find_area_bottomup(mm, mm->mmap_base, len, mask, psize, high_limit);
}

static inline void slice_copy_mask(struct slice_mask *dst,
					const struct slice_mask *src)
{
	dst->low_slices = src->low_slices;
	if (!SLICE_NUM_HIGH)
		return;
	bitmap_copy(dst->high_slices, src->high_slices, SLICE_NUM_HIGH);
}

static inline void slice_or_mask(struct slice_mask *dst,
					const struct slice_mask *src1,
					const struct slice_mask *src2)
{
	dst->low_slices = src1->low_slices | src2->low_slices;
	if (!SLICE_NUM_HIGH)
		return;
	bitmap_or(dst->high_slices, src1->high_slices, src2->high_slices, SLICE_NUM_HIGH);
}

static inline void slice_andnot_mask(struct slice_mask *dst,
					const struct slice_mask *src1,
					const struct slice_mask *src2)
{
	dst->low_slices = src1->low_slices & ~src2->low_slices;
	if (!SLICE_NUM_HIGH)
		return;
	bitmap_andnot(dst->high_slices, src1->high_slices, src2->high_slices, SLICE_NUM_HIGH);
}

#ifdef CONFIG_PPC_64K_PAGES
#define MMU_PAGE_BASE	MMU_PAGE_64K
#else
#define MMU_PAGE_BASE	MMU_PAGE_4K
#endif

unsigned long slice_get_unmapped_area(unsigned long addr, unsigned long len,
				      unsigned long flags, unsigned int psize,
				      int topdown)
{
	struct slice_mask good_mask;
	struct slice_mask potential_mask;
	const struct slice_mask *maskp;
	const struct slice_mask *compat_maskp = NULL;
	int fixed = (flags & MAP_FIXED);
	int pshift = max_t(int, mmu_psize_defs[psize].shift, PAGE_SHIFT);
	unsigned long page_size = 1UL << pshift;
	struct mm_struct *mm = current->mm;
	unsigned long newaddr;
	unsigned long high_limit;

	high_limit = DEFAULT_MAP_WINDOW;
	if (addr >= high_limit || (fixed && (addr + len > high_limit)))
		high_limit = TASK_SIZE;

	if (len > high_limit)
		return -ENOMEM;
	if (len & (page_size - 1))
		return -EINVAL;
	if (fixed) {
		if (addr & (page_size - 1))
			return -EINVAL;
		if (addr > high_limit - len)
			return -ENOMEM;
	}

	if (high_limit > mm_ctx_slb_addr_limit(&mm->context)) {
		/*
		 * Increasing the slb_addr_limit does not require
		 * slice mask cache to be recalculated because it should
		 * be already initialised beyond the old address limit.
		 */
		mm_ctx_set_slb_addr_limit(&mm->context, high_limit);

		on_each_cpu(slice_flush_segments, mm, 1);
	}

	/* Sanity checks */
	BUG_ON(mm->task_size == 0);
	BUG_ON(mm_ctx_slb_addr_limit(&mm->context) == 0);
	VM_BUG_ON(radix_enabled());

	slice_dbg("slice_get_unmapped_area(mm=%p, psize=%d...\n", mm, psize);
	slice_dbg(" addr=%lx, len=%lx, flags=%lx, topdown=%d\n",
		  addr, len, flags, topdown);

	/* If hint, make sure it matches our alignment restrictions */
	if (!fixed && addr) {
		addr = ALIGN(addr, page_size);
		slice_dbg(" aligned addr=%lx\n", addr);
		/* Ignore hint if it's too large or overlaps a VMA */
		if (addr > high_limit - len || addr < mmap_min_addr ||
		    !slice_area_is_free(mm, addr, len))
			addr = 0;
	}

	/* First make up a "good" mask of slices that have the right size
	 * already
	 */
	maskp = slice_mask_for_size(&mm->context, psize);

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

	/*
	 * If we support combo pages, we can allow 64k pages in 4k slices
	 * The mask copies could be avoided in most cases here if we had
	 * a pointer to good mask for the next code to use.
	 */
	if (IS_ENABLED(CONFIG_PPC_64K_PAGES) && psize == MMU_PAGE_64K) {
		compat_maskp = slice_mask_for_size(&mm->context, MMU_PAGE_4K);
		if (fixed)
			slice_or_mask(&good_mask, maskp, compat_maskp);
		else
			slice_copy_mask(&good_mask, maskp);
	} else {
		slice_copy_mask(&good_mask, maskp);
	}

	slice_print_mask(" good_mask", &good_mask);
	if (compat_maskp)
		slice_print_mask(" compat_mask", compat_maskp);

	/* First check hint if it's valid or if we have MAP_FIXED */
	if (addr != 0 || fixed) {
		/* Check if we fit in the good mask. If we do, we just return,
		 * nothing else to do
		 */
		if (slice_check_range_fits(mm, &good_mask, addr, len)) {
			slice_dbg(" fits good !\n");
			newaddr = addr;
			goto return_addr;
		}
	} else {
		/* Now let's see if we can find something in the existing
		 * slices for that size
		 */
		newaddr = slice_find_area(mm, len, &good_mask,
					  psize, topdown, high_limit);
		if (newaddr != -ENOMEM) {
			/* Found within the good mask, we don't have to setup,
			 * we thus return directly
			 */
			slice_dbg(" found area at 0x%lx\n", newaddr);
			goto return_addr;
		}
	}
	/*
	 * We don't fit in the good mask, check what other slices are
	 * empty and thus can be converted
	 */
	slice_mask_for_free(mm, &potential_mask, high_limit);
	slice_or_mask(&potential_mask, &potential_mask, &good_mask);
	slice_print_mask(" potential", &potential_mask);

	if (addr != 0 || fixed) {
		if (slice_check_range_fits(mm, &potential_mask, addr, len)) {
			slice_dbg(" fits potential !\n");
			newaddr = addr;
			goto convert;
		}
	}

	/* If we have MAP_FIXED and failed the above steps, then error out */
	if (fixed)
		return -EBUSY;

	slice_dbg(" search...\n");

	/* If we had a hint that didn't work out, see if we can fit
	 * anywhere in the good area.
	 */
	if (addr) {
		newaddr = slice_find_area(mm, len, &good_mask,
					  psize, topdown, high_limit);
		if (newaddr != -ENOMEM) {
			slice_dbg(" found area at 0x%lx\n", newaddr);
			goto return_addr;
		}
	}

	/* Now let's see if we can find something in the existing slices
	 * for that size plus free slices
	 */
	newaddr = slice_find_area(mm, len, &potential_mask,
				  psize, topdown, high_limit);

	if (IS_ENABLED(CONFIG_PPC_64K_PAGES) && newaddr == -ENOMEM &&
	    psize == MMU_PAGE_64K) {
		/* retry the search with 4k-page slices included */
		slice_or_mask(&potential_mask, &potential_mask, compat_maskp);
		newaddr = slice_find_area(mm, len, &potential_mask,
					  psize, topdown, high_limit);
	}

	if (newaddr == -ENOMEM)
		return -ENOMEM;

	slice_range_to_mask(newaddr, len, &potential_mask);
	slice_dbg(" found potential area at 0x%lx\n", newaddr);
	slice_print_mask(" mask", &potential_mask);

 convert:
	/*
	 * Try to allocate the context before we do slice convert
	 * so that we handle the context allocation failure gracefully.
	 */
	if (need_extra_context(mm, newaddr)) {
		if (alloc_extended_context(mm, newaddr) < 0)
			return -ENOMEM;
	}

	slice_andnot_mask(&potential_mask, &potential_mask, &good_mask);
	if (compat_maskp && !fixed)
		slice_andnot_mask(&potential_mask, &potential_mask, compat_maskp);
	if (potential_mask.low_slices ||
		(SLICE_NUM_HIGH &&
		 !bitmap_empty(potential_mask.high_slices, SLICE_NUM_HIGH))) {
		slice_convert(mm, &potential_mask, psize);
		if (psize > MMU_PAGE_BASE)
			on_each_cpu(slice_flush_segments, mm, 1);
	}
	return newaddr;

return_addr:
	if (need_extra_context(mm, newaddr)) {
		if (alloc_extended_context(mm, newaddr) < 0)
			return -ENOMEM;
	}
	return newaddr;
}
EXPORT_SYMBOL_GPL(slice_get_unmapped_area);

unsigned long arch_get_unmapped_area(struct file *filp,
				     unsigned long addr,
				     unsigned long len,
				     unsigned long pgoff,
				     unsigned long flags)
{
	if (radix_enabled())
		return generic_get_unmapped_area(filp, addr, len, pgoff, flags);

	return slice_get_unmapped_area(addr, len, flags,
				       mm_ctx_user_psize(&current->mm->context), 0);
}

unsigned long arch_get_unmapped_area_topdown(struct file *filp,
					     const unsigned long addr0,
					     const unsigned long len,
					     const unsigned long pgoff,
					     const unsigned long flags)
{
	if (radix_enabled())
		return generic_get_unmapped_area_topdown(filp, addr0, len, pgoff, flags);

	return slice_get_unmapped_area(addr0, len, flags,
				       mm_ctx_user_psize(&current->mm->context), 1);
}

unsigned int notrace get_slice_psize(struct mm_struct *mm, unsigned long addr)
{
	unsigned char *psizes;
	int index, mask_index;

	VM_BUG_ON(radix_enabled());

	if (slice_addr_is_low(addr)) {
		psizes = mm_ctx_low_slices(&mm->context);
		index = GET_LOW_SLICE_INDEX(addr);
	} else {
		psizes = mm_ctx_high_slices(&mm->context);
		index = GET_HIGH_SLICE_INDEX(addr);
	}
	mask_index = index & 0x1;
	return (psizes[index >> 1] >> (mask_index * 4)) & 0xf;
}
EXPORT_SYMBOL_GPL(get_slice_psize);

void slice_init_new_context_exec(struct mm_struct *mm)
{
	unsigned char *hpsizes, *lpsizes;
	struct slice_mask *mask;
	unsigned int psize = mmu_virtual_psize;

	slice_dbg("slice_init_new_context_exec(mm=%p)\n", mm);

	/*
	 * In the case of exec, use the default limit. In the
	 * case of fork it is just inherited from the mm being
	 * duplicated.
	 */
	mm_ctx_set_slb_addr_limit(&mm->context, SLB_ADDR_LIMIT_DEFAULT);
	mm_ctx_set_user_psize(&mm->context, psize);

	/*
	 * Set all slice psizes to the default.
	 */
	lpsizes = mm_ctx_low_slices(&mm->context);
	memset(lpsizes, (psize << 4) | psize, SLICE_NUM_LOW >> 1);

	hpsizes = mm_ctx_high_slices(&mm->context);
	memset(hpsizes, (psize << 4) | psize, SLICE_NUM_HIGH >> 1);

	/*
	 * Slice mask cache starts zeroed, fill the default size cache.
	 */
	mask = slice_mask_for_size(&mm->context, psize);
	mask->low_slices = ~0UL;
	if (SLICE_NUM_HIGH)
		bitmap_fill(mask->high_slices, SLICE_NUM_HIGH);
}

void slice_setup_new_exec(void)
{
	struct mm_struct *mm = current->mm;

	slice_dbg("slice_setup_new_exec(mm=%p)\n", mm);

	if (!is_32bit_task())
		return;

	mm_ctx_set_slb_addr_limit(&mm->context, DEFAULT_MAP_WINDOW);
}

void slice_set_range_psize(struct mm_struct *mm, unsigned long start,
			   unsigned long len, unsigned int psize)
{
	struct slice_mask mask;

	VM_BUG_ON(radix_enabled());

	slice_range_to_mask(start, len, &mask);
	slice_convert(mm, &mask, psize);
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
int slice_is_hugepage_only_range(struct mm_struct *mm, unsigned long addr,
			   unsigned long len)
{
	const struct slice_mask *maskp;
	unsigned int psize = mm_ctx_user_psize(&mm->context);

	VM_BUG_ON(radix_enabled());

	maskp = slice_mask_for_size(&mm->context, psize);

	/* We need to account for 4k slices too */
	if (IS_ENABLED(CONFIG_PPC_64K_PAGES) && psize == MMU_PAGE_64K) {
		const struct slice_mask *compat_maskp;
		struct slice_mask available;

		compat_maskp = slice_mask_for_size(&mm->context, MMU_PAGE_4K);
		slice_or_mask(&available, maskp, compat_maskp);
		return !slice_check_range_fits(mm, &available, addr, len);
	}

	return !slice_check_range_fits(mm, maskp, addr, len);
}

unsigned long vma_mmu_pagesize(struct vm_area_struct *vma)
{
	/* With radix we don't use slice, so derive it from vma*/
	if (radix_enabled())
		return vma_kernel_pagesize(vma);

	return 1UL << mmu_psize_to_shift(get_slice_psize(vma->vm_mm, vma->vm_start));
}

static int file_to_psize(struct file *file)
{
	struct hstate *hstate = hstate_file(file);
	return shift_to_mmu_psize(huge_page_shift(hstate));
}

unsigned long hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
					unsigned long len, unsigned long pgoff,
					unsigned long flags)
{
	if (radix_enabled())
		return generic_hugetlb_get_unmapped_area(file, addr, len, pgoff, flags);

	return slice_get_unmapped_area(addr, len, flags, file_to_psize(file), 1);
}
#endif
