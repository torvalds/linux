/*
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "linux/mman.h"
#include <mali_kbase.h>

/* mali_kbase_mmap.c
 *
 * This file contains Linux specific implementation of
 * kbase_context_get_unmapped_area() interface.
 */


/**
 * align_and_check() - Align the specified pointer to the provided alignment and
 *                     check that it is still in range.
 * @gap_end:        Highest possible start address for allocation (end of gap in
 *                  address space)
 * @gap_start:      Start address of current memory area / gap in address space
 * @info:           vm_unmapped_area_info structure passed to caller, containing
 *                  alignment, length and limits for the allocation
 * @is_shader_code: True if the allocation is for shader code (which has
 *                  additional alignment requirements)
 * @is_same_4gb_page: True if the allocation needs to reside completely within
 *                    a 4GB chunk
 *
 * Return: true if gap_end is now aligned correctly and is still in range,
 *         false otherwise
 */
static bool align_and_check(unsigned long *gap_end, unsigned long gap_start,
		struct vm_unmapped_area_info *info, bool is_shader_code,
		bool is_same_4gb_page)
{
	/* Compute highest gap address at the desired alignment */
	(*gap_end) -= info->length;
	(*gap_end) -= (*gap_end - info->align_offset) & info->align_mask;

	if (is_shader_code) {
		/* Check for 4GB boundary */
		if (0 == (*gap_end & BASE_MEM_MASK_4GB))
			(*gap_end) -= (info->align_offset ? info->align_offset :
					info->length);
		if (0 == ((*gap_end + info->length) & BASE_MEM_MASK_4GB))
			(*gap_end) -= (info->align_offset ? info->align_offset :
					info->length);

		if (!(*gap_end & BASE_MEM_MASK_4GB) || !((*gap_end +
				info->length) & BASE_MEM_MASK_4GB))
			return false;
	} else if (is_same_4gb_page) {
		unsigned long start = *gap_end;
		unsigned long end = *gap_end + info->length;
		unsigned long mask = ~((unsigned long)U32_MAX);

		/* Check if 4GB boundary is straddled */
		if ((start & mask) != ((end - 1) & mask)) {
			unsigned long offset = end - (end & mask);
			/* This is to ensure that alignment doesn't get
			 * disturbed in an attempt to prevent straddling at
			 * 4GB boundary. The GPU VA is aligned to 2MB when the
			 * allocation size is > 2MB and there is enough CPU &
			 * GPU virtual space.
			 */
			unsigned long rounded_offset =
					ALIGN(offset, info->align_mask + 1);

			start -= rounded_offset;
			end -= rounded_offset;

			*gap_end = start;

			/* The preceding 4GB boundary shall not get straddled,
			 * even after accounting for the alignment, as the
			 * size of allocation is limited to 4GB and the initial
			 * start location was already aligned.
			 */
			WARN_ON((start & mask) != ((end - 1) & mask));
		}
	}


	if ((*gap_end < info->low_limit) || (*gap_end < gap_start))
		return false;


	return true;
}

/**
 * kbase_unmapped_area_topdown() - allocates new areas top-down from
 *                                 below the stack limit.
 * @info:              Information about the memory area to allocate.
 * @is_shader_code:    Boolean which denotes whether the allocated area is
 *                      intended for the use by shader core in which case a
 *                      special alignment requirements apply.
 * @is_same_4gb_page: Boolean which indicates whether the allocated area needs
 *                    to reside completely within a 4GB chunk.
 *
 * The unmapped_area_topdown() function in the Linux kernel is not exported
 * using EXPORT_SYMBOL_GPL macro. To allow us to call this function from a
 * module and also make use of the fact that some of the requirements for
 * the unmapped area are known in advance, we implemented an extended version
 * of this function and prefixed it with 'kbase_'.
 *
 * The difference in the call parameter list comes from the fact that
 * kbase_unmapped_area_topdown() is called with additional parameters which
 * are provided to indicate whether the allocation is for a shader core memory,
 * which has additional alignment requirements, and whether the allocation can
 * straddle a 4GB boundary.
 *
 * The modification of the original Linux function lies in how the computation
 * of the highest gap address at the desired alignment is performed once the
 * gap with desirable properties is found. For this purpose a special function
 * is introduced (@ref align_and_check()) which beside computing the gap end
 * at the desired alignment also performs additional alignment checks for the
 * case when the memory is executable shader core memory, for which it is
 * ensured that the gap does not end on a 4GB boundary, and for the case when
 * memory needs to be confined within a 4GB chunk.
 *
 * Return: address of the found gap end (high limit) if area is found;
 *         -ENOMEM if search is unsuccessful
*/

static unsigned long kbase_unmapped_area_topdown(struct vm_unmapped_area_info
		*info, bool is_shader_code, bool is_same_4gb_page)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/*
	 * Adjust search limits by the desired length.
	 * See implementation comment at top of unmapped_area().
	 */
	gap_end = info->high_limit;
	if (gap_end < length)
		return -ENOMEM;
	high_limit = gap_end - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	if (gap_start <= high_limit) {
		if (align_and_check(&gap_end, gap_start, info,
				is_shader_code, is_same_4gb_page))
			return gap_end;
	}

	/* Check if rbtree root looks promising */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		return -ENOMEM;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		return -ENOMEM;

	while (true) {
		/* Visit right subtree if it looks promising */
		gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
		if (gap_start <= high_limit && vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

check_current:
		/* Check if current node has a suitable gap */
		gap_end = vma->vm_start;
		if (gap_end < low_limit)
			return -ENOMEM;
		if (gap_start <= high_limit && gap_end - gap_start >= length) {
			/* We found a suitable gap. Clip it with the original
			 * high_limit.
			 */
			if (gap_end > info->high_limit)
				gap_end = info->high_limit;

			if (align_and_check(&gap_end, gap_start, info,
					is_shader_code, is_same_4gb_page))
				return gap_end;
		}

		/* Visit left subtree if it looks promising */
		if (vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;

			if (!rb_parent(prev))
				return -ENOMEM;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_right) {
				gap_start = vma->vm_prev ?
					vma->vm_prev->vm_end : 0;
				goto check_current;
			}
		}
	}

	return -ENOMEM;
}


/* This function is based on Linux kernel's arch_get_unmapped_area, but
 * simplified slightly. Modifications come from the fact that some values
 * about the memory area are known in advance.
 */
unsigned long kbase_context_get_unmapped_area(struct kbase_context *const kctx,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;
	unsigned long align_offset = 0;
	unsigned long align_mask = 0;
	unsigned long high_limit = mm->mmap_base;
	unsigned long low_limit = PAGE_SIZE;
	int cpu_va_bits = BITS_PER_LONG;
	int gpu_pc_bits =
	      kctx->kbdev->gpu_props.props.core_props.log2_program_counter_size;
	bool is_shader_code = false;
	bool is_same_4gb_page = false;
	unsigned long ret;

	/* the 'nolock' form is used here:
	 * - the base_pfn of the SAME_VA zone does not change
	 * - in normal use, va_size_pages is constant once the first allocation
	 *   begins
	 *
	 * However, in abnormal use this function could be processing whilst
	 * another new zone is being setup in a different thread (e.g. to
	 * borrow part of the SAME_VA zone). In the worst case, this path may
	 * witness a higher SAME_VA end_pfn than the code setting up the new
	 * zone.
	 *
	 * This is safe because once we reach the main allocation functions,
	 * we'll see the updated SAME_VA end_pfn and will determine that there
	 * is no free region at the address found originally by too large a
	 * same_va_end_addr here, and will fail the allocation gracefully.
	 */
	struct kbase_reg_zone *zone =
		kbase_ctx_reg_zone_get_nolock(kctx, KBASE_REG_ZONE_SAME_VA);
	u64 same_va_end_addr = kbase_reg_zone_end_pfn(zone) << PAGE_SHIFT;

	/* err on fixed address */
	if ((flags & MAP_FIXED) || addr)
		return -EINVAL;

#if IS_ENABLED(CONFIG_64BIT)
	/* too big? */
	if (len > TASK_SIZE - SZ_2M)
		return -ENOMEM;

	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		high_limit =
			min_t(unsigned long, mm->mmap_base, same_va_end_addr);

		/* If there's enough (> 33 bits) of GPU VA space, align
		 * to 2MB boundaries.
		 */
		if (kctx->kbdev->gpu_props.mmu.va_bits > 33) {
			if (len >= SZ_2M) {
				align_offset = SZ_2M;
				align_mask = SZ_2M - 1;
			}
		}

		low_limit = SZ_2M;
	} else {
		cpu_va_bits = 32;
	}
#endif /* CONFIG_64BIT */
	if ((PFN_DOWN(BASE_MEM_COOKIE_BASE) <= pgoff) &&
		(PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) > pgoff)) {
			int cookie = pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);
			struct kbase_va_region *reg;

			/* Need to hold gpu vm lock when using reg */
			kbase_gpu_vm_lock(kctx);
			reg = kctx->pending_regions[cookie];
			if (!reg) {
				kbase_gpu_vm_unlock(kctx);
				return -EINVAL;
			}
			if (!(reg->flags & KBASE_REG_GPU_NX)) {
				if (cpu_va_bits > gpu_pc_bits) {
					align_offset = 1ULL << gpu_pc_bits;
					align_mask = align_offset - 1;
					is_shader_code = true;
				}
#if !MALI_USE_CSF
			} else if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
				unsigned long extension_bytes =
					(unsigned long)(reg->extension
							<< PAGE_SHIFT);
				/* kbase_check_alloc_sizes() already satisfies
				 * these checks, but they're here to avoid
				 * maintenance hazards due to the assumptions
				 * involved
				 */
				WARN_ON(reg->extension >
					(ULONG_MAX >> PAGE_SHIFT));
				WARN_ON(reg->initial_commit > (ULONG_MAX >> PAGE_SHIFT));
				WARN_ON(!is_power_of_2(extension_bytes));
				align_mask = extension_bytes - 1;
				align_offset =
					extension_bytes -
					(reg->initial_commit << PAGE_SHIFT);
#endif /* !MALI_USE_CSF */
			} else if (reg->flags & KBASE_REG_GPU_VA_SAME_4GB_PAGE) {
				is_same_4gb_page = true;
			}
			kbase_gpu_vm_unlock(kctx);
#ifndef CONFIG_64BIT
	} else {
		return current->mm->get_unmapped_area(
			kctx->filp, addr, len, pgoff, flags);
#endif
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = low_limit;
	info.high_limit = high_limit;
	info.align_offset = align_offset;
	info.align_mask = align_mask;

	ret = kbase_unmapped_area_topdown(&info, is_shader_code,
			is_same_4gb_page);

	if (IS_ERR_VALUE(ret) && high_limit == mm->mmap_base &&
	    high_limit < same_va_end_addr) {
		/* Retry above mmap_base */
		info.low_limit = mm->mmap_base;
		info.high_limit = min_t(u64, TASK_SIZE, same_va_end_addr);

		ret = kbase_unmapped_area_topdown(&info, is_shader_code,
				is_same_4gb_page);
	}

	return ret;
}
