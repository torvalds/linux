/*
 *
 * (C) COPYRIGHT 2010-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/**
 * Base kernel memory APIs
 */
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/version.h>
#include <linux/log2.h>
#ifdef CONFIG_OF
#include <linux/of_platform.h>
#endif

#include <mali_kbase_config.h>
#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_cache_policy.h>
#include <mali_kbase_hw.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_kbase_native_mgm.h>
#include <mali_kbase_mem_pool_group.h>
#include <mmu/mali_kbase_mmu.h>
#include <mali_kbase_config_defaults.h>

/*
 * Alignment of objects allocated by the GPU inside a just-in-time memory
 * region whose size is given by an end address
 *
 * This is the alignment of objects allocated by the GPU, but possibly not
 * fully written to. When taken into account with
 * KBASE_GPU_ALLOCATED_OBJECT_MAX_BYTES it gives the maximum number of bytes
 * that the JIT memory report size can exceed the actual backed memory size.
 */
#define KBASE_GPU_ALLOCATED_OBJECT_ALIGN_BYTES (128u)

/*
 * Maximum size of objects allocated by the GPU inside a just-in-time memory
 * region whose size is given by an end address
 *
 * This is the maximum size of objects allocated by the GPU, but possibly not
 * fully written to. When taken into account with
 * KBASE_GPU_ALLOCATED_OBJECT_ALIGN_BYTES it gives the maximum number of bytes
 * that the JIT memory report size can exceed the actual backed memory size.
 */
#define KBASE_GPU_ALLOCATED_OBJECT_MAX_BYTES (512u)


/* Forward declarations */
static void free_partial_locked(struct kbase_context *kctx,
		struct kbase_mem_pool *pool, struct tagged_addr tp);

static size_t kbase_get_num_cpu_va_bits(struct kbase_context *kctx)
{
#if defined(CONFIG_ARM64)
	/* VA_BITS can be as high as 48 bits, but all bits are available for
	 * both user and kernel.
	 */
	size_t cpu_va_bits = VA_BITS;
#elif defined(CONFIG_X86_64)
	/* x86_64 can access 48 bits of VA, but the 48th is used to denote
	 * kernel (1) vs userspace (0), so the max here is 47.
	 */
	size_t cpu_va_bits = 47;
#elif defined(CONFIG_ARM) || defined(CONFIG_X86_32)
	size_t cpu_va_bits = sizeof(void *) * BITS_PER_BYTE;
#else
#error "Unknown CPU VA width for this architecture"
#endif

#ifdef CONFIG_64BIT
	if (kbase_ctx_flag(kctx, KCTX_COMPAT))
		cpu_va_bits = 32;
#endif

	return cpu_va_bits;
}

/* This function finds out which RB tree the given pfn from the GPU VA belongs
 * to based on the memory zone the pfn refers to */
static struct rb_root *kbase_gpu_va_to_rbtree(struct kbase_context *kctx,
								    u64 gpu_pfn)
{
	struct rb_root *rbtree = NULL;

	/* The gpu_pfn can only be greater than the starting pfn of the EXEC_VA
	 * zone if this has been initialized.
	 */
	if (gpu_pfn >= kctx->exec_va_start)
		rbtree = &kctx->reg_rbtree_exec;
	else {
		u64 same_va_end;

#ifdef CONFIG_64BIT
		if (kbase_ctx_flag(kctx, KCTX_COMPAT))
#endif /* CONFIG_64BIT */
			same_va_end = KBASE_REG_ZONE_CUSTOM_VA_BASE;
#ifdef CONFIG_64BIT
		else
			same_va_end = kctx->same_va_end;
#endif /* CONFIG_64BIT */

		if (gpu_pfn >= same_va_end)
			rbtree = &kctx->reg_rbtree_custom;
		else
			rbtree = &kctx->reg_rbtree_same;
	}

	return rbtree;
}

/* This function inserts a region into the tree. */
static void kbase_region_tracker_insert(struct kbase_va_region *new_reg)
{
	u64 start_pfn = new_reg->start_pfn;
	struct rb_node **link = NULL;
	struct rb_node *parent = NULL;
	struct rb_root *rbtree = NULL;

	rbtree = new_reg->rbtree;

	link = &(rbtree->rb_node);
	/* Find the right place in the tree using tree search */
	while (*link) {
		struct kbase_va_region *old_reg;

		parent = *link;
		old_reg = rb_entry(parent, struct kbase_va_region, rblink);

		/* RBTree requires no duplicate entries. */
		KBASE_DEBUG_ASSERT(old_reg->start_pfn != start_pfn);

		if (old_reg->start_pfn > start_pfn)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	/* Put the new node there, and rebalance tree */
	rb_link_node(&(new_reg->rblink), parent, link);

	rb_insert_color(&(new_reg->rblink), rbtree);
}

static struct kbase_va_region *find_region_enclosing_range_rbtree(
		struct rb_root *rbtree, u64 start_pfn, size_t nr_pages)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;
	u64 end_pfn = start_pfn + nr_pages;

	rbnode = rbtree->rb_node;

	while (rbnode) {
		u64 tmp_start_pfn, tmp_end_pfn;

		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		tmp_start_pfn = reg->start_pfn;
		tmp_end_pfn = reg->start_pfn + reg->nr_pages;

		/* If start is lower than this, go left. */
		if (start_pfn < tmp_start_pfn)
			rbnode = rbnode->rb_left;
		/* If end is higher than this, then go right. */
		else if (end_pfn > tmp_end_pfn)
			rbnode = rbnode->rb_right;
		else	/* Enclosing */
			return reg;
	}

	return NULL;
}

struct kbase_va_region *kbase_find_region_enclosing_address(
		struct rb_root *rbtree, u64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	rbnode = rbtree->rb_node;

	while (rbnode) {
		u64 tmp_start_pfn, tmp_end_pfn;

		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		tmp_start_pfn = reg->start_pfn;
		tmp_end_pfn = reg->start_pfn + reg->nr_pages;

		/* If start is lower than this, go left. */
		if (gpu_pfn < tmp_start_pfn)
			rbnode = rbnode->rb_left;
		/* If end is higher than this, then go right. */
		else if (gpu_pfn >= tmp_end_pfn)
			rbnode = rbnode->rb_right;
		else	/* Enclosing */
			return reg;
	}

	return NULL;
}

/* Find region enclosing given address. */
struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(
		struct kbase_context *kctx, u64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_root *rbtree = NULL;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	rbtree = kbase_gpu_va_to_rbtree(kctx, gpu_pfn);

	return kbase_find_region_enclosing_address(rbtree, gpu_addr);
}

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_enclosing_address);

struct kbase_va_region *kbase_find_region_base_address(
		struct rb_root *rbtree, u64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_node *rbnode = NULL;
	struct kbase_va_region *reg = NULL;

	rbnode = rbtree->rb_node;

	while (rbnode) {
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if (reg->start_pfn > gpu_pfn)
			rbnode = rbnode->rb_left;
		else if (reg->start_pfn < gpu_pfn)
			rbnode = rbnode->rb_right;
		else
			return reg;
	}

	return NULL;
}

/* Find region with given base address */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(
		struct kbase_context *kctx, u64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_root *rbtree = NULL;

	lockdep_assert_held(&kctx->reg_lock);

	rbtree = kbase_gpu_va_to_rbtree(kctx, gpu_pfn);

	return kbase_find_region_base_address(rbtree, gpu_addr);
}

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_base_address);

/* Find region meeting given requirements */
static struct kbase_va_region *kbase_region_tracker_find_region_meeting_reqs(
		struct kbase_va_region *reg_reqs,
		size_t nr_pages, size_t align_offset, size_t align_mask,
		u64 *out_start_pfn)
{
	struct rb_node *rbnode = NULL;
	struct kbase_va_region *reg = NULL;
	struct rb_root *rbtree = NULL;

	/* Note that this search is a linear search, as we do not have a target
	   address in mind, so does not benefit from the rbtree search */
	rbtree = reg_reqs->rbtree;

	for (rbnode = rb_first(rbtree); rbnode; rbnode = rb_next(rbnode)) {
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if ((reg->nr_pages >= nr_pages) &&
				(reg->flags & KBASE_REG_FREE)) {
			/* Check alignment */
			u64 start_pfn = reg->start_pfn;

			/* When align_offset == align, this sequence is
			 * equivalent to:
			 *   (start_pfn + align_mask) & ~(align_mask)
			 *
			 * Otherwise, it aligns to n*align + offset, for the
			 * lowest value n that makes this still >start_pfn */
			start_pfn += align_mask;
			start_pfn -= (start_pfn - align_offset) & (align_mask);

			if (!(reg_reqs->flags & KBASE_REG_GPU_NX)) {
				/* Can't end at 4GB boundary */
				if (0 == ((start_pfn + nr_pages) & BASE_MEM_PFN_MASK_4GB))
					start_pfn += align_offset;

				/* Can't start at 4GB boundary */
				if (0 == (start_pfn & BASE_MEM_PFN_MASK_4GB))
					start_pfn += align_offset;

				if (!((start_pfn + nr_pages) & BASE_MEM_PFN_MASK_4GB) ||
				    !(start_pfn & BASE_MEM_PFN_MASK_4GB))
					continue;
			} else if (reg_reqs->flags &
					KBASE_REG_GPU_VA_SAME_4GB_PAGE) {
				u64 end_pfn = start_pfn + nr_pages - 1;

				if ((start_pfn & ~BASE_MEM_PFN_MASK_4GB) !=
				    (end_pfn & ~BASE_MEM_PFN_MASK_4GB))
					start_pfn = end_pfn & ~BASE_MEM_PFN_MASK_4GB;
			}

			if ((start_pfn >= reg->start_pfn) &&
					(start_pfn <= (reg->start_pfn + reg->nr_pages - 1)) &&
					((start_pfn + nr_pages - 1) <= (reg->start_pfn + reg->nr_pages - 1))) {
				*out_start_pfn = start_pfn;
				return reg;
			}
		}
	}

	return NULL;
}

/**
 * @brief Remove a region object from the global list.
 *
 * The region reg is removed, possibly by merging with other free and
 * compatible adjacent regions.  It must be called with the context
 * region lock held. The associated memory is not released (see
 * kbase_free_alloced_region). Internal use only.
 */
int kbase_remove_va_region(struct kbase_va_region *reg)
{
	struct rb_node *rbprev;
	struct kbase_va_region *prev = NULL;
	struct rb_node *rbnext;
	struct kbase_va_region *next = NULL;
	struct rb_root *reg_rbtree = NULL;

	int merged_front = 0;
	int merged_back = 0;
	int err = 0;

	reg_rbtree = reg->rbtree;

	/* Try to merge with the previous block first */
	rbprev = rb_prev(&(reg->rblink));
	if (rbprev) {
		prev = rb_entry(rbprev, struct kbase_va_region, rblink);
		if (prev->flags & KBASE_REG_FREE) {
			/* We're compatible with the previous VMA,
			 * merge with it */
			WARN_ON((prev->flags & KBASE_REG_ZONE_MASK) !=
					    (reg->flags & KBASE_REG_ZONE_MASK));
			prev->nr_pages += reg->nr_pages;
			rb_erase(&(reg->rblink), reg_rbtree);
			reg = prev;
			merged_front = 1;
		}
	}

	/* Try to merge with the next block second */
	/* Note we do the lookup here as the tree may have been rebalanced. */
	rbnext = rb_next(&(reg->rblink));
	if (rbnext) {
		/* We're compatible with the next VMA, merge with it */
		next = rb_entry(rbnext, struct kbase_va_region, rblink);
		if (next->flags & KBASE_REG_FREE) {
			WARN_ON((next->flags & KBASE_REG_ZONE_MASK) !=
					    (reg->flags & KBASE_REG_ZONE_MASK));
			next->start_pfn = reg->start_pfn;
			next->nr_pages += reg->nr_pages;
			rb_erase(&(reg->rblink), reg_rbtree);
			merged_back = 1;
			if (merged_front) {
				/* We already merged with prev, free it */
				kfree(reg);
			}
		}
	}

	/* If we failed to merge then we need to add a new block */
	if (!(merged_front || merged_back)) {
		/*
		 * We didn't merge anything. Add a new free
		 * placeholder and remove the original one.
		 */
		struct kbase_va_region *free_reg;

		free_reg = kbase_alloc_free_region(reg_rbtree,
				reg->start_pfn, reg->nr_pages,
				reg->flags & KBASE_REG_ZONE_MASK);
		if (!free_reg) {
			err = -ENOMEM;
			goto out;
		}
		rb_replace_node(&(reg->rblink), &(free_reg->rblink), reg_rbtree);
	}

 out:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_remove_va_region);

/**
 * kbase_insert_va_region_nolock - Insert a VA region to the list,
 * replacing the existing one.
 *
 * @new_reg: The new region to insert
 * @at_reg: The region to replace
 * @start_pfn: The Page Frame Number to insert at
 * @nr_pages: The number of pages of the region
 */
static int kbase_insert_va_region_nolock(struct kbase_va_region *new_reg,
		struct kbase_va_region *at_reg, u64 start_pfn, size_t nr_pages)
{
	struct rb_root *reg_rbtree = NULL;
	int err = 0;

	reg_rbtree = at_reg->rbtree;

	/* Must be a free region */
	KBASE_DEBUG_ASSERT((at_reg->flags & KBASE_REG_FREE) != 0);
	/* start_pfn should be contained within at_reg */
	KBASE_DEBUG_ASSERT((start_pfn >= at_reg->start_pfn) && (start_pfn < at_reg->start_pfn + at_reg->nr_pages));
	/* at least nr_pages from start_pfn should be contained within at_reg */
	KBASE_DEBUG_ASSERT(start_pfn + nr_pages <= at_reg->start_pfn + at_reg->nr_pages);

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;

	/* Regions are a whole use, so swap and delete old one. */
	if (at_reg->start_pfn == start_pfn && at_reg->nr_pages == nr_pages) {
		rb_replace_node(&(at_reg->rblink), &(new_reg->rblink),
								reg_rbtree);
		kfree(at_reg);
	}
	/* New region replaces the start of the old one, so insert before. */
	else if (at_reg->start_pfn == start_pfn) {
		at_reg->start_pfn += nr_pages;
		KBASE_DEBUG_ASSERT(at_reg->nr_pages >= nr_pages);
		at_reg->nr_pages -= nr_pages;

		kbase_region_tracker_insert(new_reg);
	}
	/* New region replaces the end of the old one, so insert after. */
	else if ((at_reg->start_pfn + at_reg->nr_pages) == (start_pfn + nr_pages)) {
		at_reg->nr_pages -= nr_pages;

		kbase_region_tracker_insert(new_reg);
	}
	/* New region splits the old one, so insert and create new */
	else {
		struct kbase_va_region *new_front_reg;

		new_front_reg = kbase_alloc_free_region(reg_rbtree,
				at_reg->start_pfn,
				start_pfn - at_reg->start_pfn,
				at_reg->flags & KBASE_REG_ZONE_MASK);

		if (new_front_reg) {
			at_reg->nr_pages -= nr_pages + new_front_reg->nr_pages;
			at_reg->start_pfn = start_pfn + nr_pages;

			kbase_region_tracker_insert(new_front_reg);
			kbase_region_tracker_insert(new_reg);
		} else {
			err = -ENOMEM;
		}
	}

	return err;
}

/**
 * kbase_add_va_region - Add a VA region to the region list for a context.
 *
 * @kctx: kbase context containing the region
 * @reg: the region to add
 * @addr: the address to insert the region at
 * @nr_pages: the number of pages in the region
 * @align: the minimum alignment in pages
 */
int kbase_add_va_region(struct kbase_context *kctx,
		struct kbase_va_region *reg, u64 addr,
		size_t nr_pages, size_t align)
{
	int err = 0;
	struct kbase_device *kbdev = kctx->kbdev;
	int cpu_va_bits = kbase_get_num_cpu_va_bits(kctx);
	int gpu_pc_bits =
		kbdev->gpu_props.props.core_props.log2_program_counter_size;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	lockdep_assert_held(&kctx->reg_lock);

	/* The executable allocation from the SAME_VA zone would already have an
	 * appropriately aligned GPU VA chosen for it.
	 * Also the executable allocation from EXEC_VA zone doesn't need the
	 * special alignment.
	 */
	if (!(reg->flags & KBASE_REG_GPU_NX) && !addr &&
	    ((reg->flags & KBASE_REG_ZONE_MASK) != KBASE_REG_ZONE_EXEC_VA)) {
		if (cpu_va_bits > gpu_pc_bits) {
			align = max(align, (size_t)((1ULL << gpu_pc_bits)
						>> PAGE_SHIFT));
		}
	}

	do {
		err = kbase_add_va_region_rbtree(kbdev, reg, addr, nr_pages,
				align);
		if (err != -ENOMEM)
			break;

		/*
		 * If the allocation is not from the same zone as JIT
		 * then don't retry, we're out of VA and there is
		 * nothing which can be done about it.
		 */
		if ((reg->flags & KBASE_REG_ZONE_MASK) !=
				KBASE_REG_ZONE_CUSTOM_VA)
			break;
	} while (kbase_jit_evict(kctx));

	return err;
}

KBASE_EXPORT_TEST_API(kbase_add_va_region);

/**
 * kbase_add_va_region_rbtree - Insert a region into its corresponding rbtree
 *
 * Insert a region into the rbtree that was specified when the region was
 * created. If addr is 0 a free area in the rbtree is used, otherwise the
 * specified address is used.
 *
 * @kbdev: The kbase device
 * @reg: The region to add
 * @addr: The address to add the region at, or 0 to map at any available address
 * @nr_pages: The size of the region in pages
 * @align: The minimum alignment in pages
 */
int kbase_add_va_region_rbtree(struct kbase_device *kbdev,
		struct kbase_va_region *reg,
		u64 addr, size_t nr_pages, size_t align)
{
	struct device *const dev = kbdev->dev;
	struct rb_root *rbtree = NULL;
	struct kbase_va_region *tmp;
	u64 gpu_pfn = addr >> PAGE_SHIFT;
	int err = 0;

	rbtree = reg->rbtree;

	if (!align)
		align = 1;

	/* must be a power of 2 */
	KBASE_DEBUG_ASSERT(is_power_of_2(align));
	KBASE_DEBUG_ASSERT(nr_pages > 0);

	/* Path 1: Map a specific address. Find the enclosing region,
	 * which *must* be free.
	 */
	if (gpu_pfn) {
		KBASE_DEBUG_ASSERT(!(gpu_pfn & (align - 1)));

		tmp = find_region_enclosing_range_rbtree(rbtree, gpu_pfn,
				nr_pages);
		if (kbase_is_region_invalid(tmp)) {
			dev_warn(dev, "Enclosing region not found or invalid: 0x%08llx gpu_pfn, %zu nr_pages", gpu_pfn, nr_pages);
			err = -ENOMEM;
			goto exit;
		} else if (!kbase_is_region_free(tmp)) {
			dev_warn(dev, "!(tmp->flags & KBASE_REG_FREE): tmp->start_pfn=0x%llx tmp->flags=0x%lx tmp->nr_pages=0x%zx gpu_pfn=0x%llx nr_pages=0x%zx\n",
					tmp->start_pfn, tmp->flags,
					tmp->nr_pages, gpu_pfn, nr_pages);
			err = -ENOMEM;
			goto exit;
		}

		err = kbase_insert_va_region_nolock(reg, tmp, gpu_pfn,
				nr_pages);
		if (err) {
			dev_warn(dev, "Failed to insert va region");
			err = -ENOMEM;
		}
	} else {
		/* Path 2: Map any free address which meets the requirements. */
		u64 start_pfn;
		size_t align_offset = align;
		size_t align_mask = align - 1;

		if ((reg->flags & KBASE_REG_TILER_ALIGN_TOP)) {
			WARN(align > 1, "%s with align %lx might not be honored for KBASE_REG_TILER_ALIGN_TOP memory",
					__func__,
					(unsigned long)align);
			align_mask  = reg->extent - 1;
			align_offset = reg->extent - reg->initial_commit;
		}

		tmp = kbase_region_tracker_find_region_meeting_reqs(reg,
				nr_pages, align_offset, align_mask,
				&start_pfn);
		if (tmp) {
			err = kbase_insert_va_region_nolock(reg, tmp,
							start_pfn, nr_pages);
			if (unlikely(err)) {
				dev_warn(dev, "Failed to insert region: 0x%08llx start_pfn, %zu nr_pages",
					start_pfn, nr_pages);
			}
		} else {
			dev_dbg(dev, "Failed to find a suitable region: %zu nr_pages, %zu align_offset, %zu align_mask\n",
				nr_pages, align_offset, align_mask);
			err = -ENOMEM;
		}
	}

exit:
	return err;
}

/**
 * @brief Initialize the internal region tracker data structure.
 */
static void kbase_region_tracker_ds_init(struct kbase_context *kctx,
		struct kbase_va_region *same_va_reg,
		struct kbase_va_region *custom_va_reg)
{
	kctx->reg_rbtree_same = RB_ROOT;
	kbase_region_tracker_insert(same_va_reg);

	/* Although custom_va_reg and exec_va_reg don't always exist,
	 * initialize unconditionally because of the mem_view debugfs
	 * implementation which relies on them being empty.
	 *
	 * The difference between the two is that the EXEC_VA region
	 * is never initialized at this stage.
	 */
	kctx->reg_rbtree_custom = RB_ROOT;
	kctx->reg_rbtree_exec = RB_ROOT;

	if (custom_va_reg)
		kbase_region_tracker_insert(custom_va_reg);
}

static void kbase_region_tracker_erase_rbtree(struct rb_root *rbtree)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	do {
		rbnode = rb_first(rbtree);
		if (rbnode) {
			rb_erase(rbnode, rbtree);
			reg = rb_entry(rbnode, struct kbase_va_region, rblink);
			WARN_ON(reg->va_refcnt != 1);
			/* Reset the start_pfn - as the rbtree is being
			 * destroyed and we've already erased this region, there
			 * is no further need to attempt to remove it.
			 * This won't affect the cleanup if the region was
			 * being used as a sticky resource as the cleanup
			 * related to sticky resources anyways need to be
			 * performed before the term of region tracker.
			 */
			reg->start_pfn = 0;
			kbase_free_alloced_region(reg);
		}
	} while (rbnode);
}

void kbase_region_tracker_term(struct kbase_context *kctx)
{
	kbase_gpu_vm_lock(kctx);
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_same);
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_custom);
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_exec);
	kbase_gpu_vm_unlock(kctx);
}

void kbase_region_tracker_term_rbtree(struct rb_root *rbtree)
{
	kbase_region_tracker_erase_rbtree(rbtree);
}

static size_t kbase_get_same_va_bits(struct kbase_context *kctx)
{
	return min(kbase_get_num_cpu_va_bits(kctx),
			(size_t) kctx->kbdev->gpu_props.mmu.va_bits);
}

int kbase_region_tracker_init(struct kbase_context *kctx)
{
	struct kbase_va_region *same_va_reg;
	struct kbase_va_region *custom_va_reg = NULL;
	size_t same_va_bits = kbase_get_same_va_bits(kctx);
	u64 custom_va_size = KBASE_REG_ZONE_CUSTOM_VA_SIZE;
	u64 gpu_va_limit = (1ULL << kctx->kbdev->gpu_props.mmu.va_bits) >> PAGE_SHIFT;
	u64 same_va_pages;
	int err;

	/* Take the lock as kbase_free_alloced_region requires it */
	kbase_gpu_vm_lock(kctx);

	same_va_pages = (1ULL << (same_va_bits - PAGE_SHIFT)) - 1;
	/* all have SAME_VA */
	same_va_reg = kbase_alloc_free_region(&kctx->reg_rbtree_same, 1,
			same_va_pages,
			KBASE_REG_ZONE_SAME_VA);

	if (!same_va_reg) {
		err = -ENOMEM;
		goto fail_unlock;
	}

#ifdef CONFIG_64BIT
	/* 32-bit clients have custom VA zones */
	if (kbase_ctx_flag(kctx, KCTX_COMPAT)) {
#endif
		if (gpu_va_limit <= KBASE_REG_ZONE_CUSTOM_VA_BASE) {
			err = -EINVAL;
			goto fail_free_same_va;
		}
		/* If the current size of TMEM is out of range of the
		 * virtual address space addressable by the MMU then
		 * we should shrink it to fit
		 */
		if ((KBASE_REG_ZONE_CUSTOM_VA_BASE + KBASE_REG_ZONE_CUSTOM_VA_SIZE) >= gpu_va_limit)
			custom_va_size = gpu_va_limit - KBASE_REG_ZONE_CUSTOM_VA_BASE;

		custom_va_reg = kbase_alloc_free_region(
				&kctx->reg_rbtree_custom,
				KBASE_REG_ZONE_CUSTOM_VA_BASE,
				custom_va_size, KBASE_REG_ZONE_CUSTOM_VA);

		if (!custom_va_reg) {
			err = -ENOMEM;
			goto fail_free_same_va;
		}
#ifdef CONFIG_64BIT
	} else {
		custom_va_size = 0;
	}
#endif

	kbase_region_tracker_ds_init(kctx, same_va_reg, custom_va_reg);

	kctx->same_va_end = same_va_pages + 1;
	kctx->gpu_va_end = kctx->same_va_end + custom_va_size;
	kctx->exec_va_start = U64_MAX;
	kctx->jit_va = false;


	kbase_gpu_vm_unlock(kctx);
	return 0;

fail_free_same_va:
	kbase_free_alloced_region(same_va_reg);
fail_unlock:
	kbase_gpu_vm_unlock(kctx);
	return err;
}

#ifdef CONFIG_64BIT
static int kbase_region_tracker_init_jit_64(struct kbase_context *kctx,
		u64 jit_va_pages)
{
	struct kbase_va_region *same_va;
	struct kbase_va_region *custom_va_reg;

	lockdep_assert_held(&kctx->reg_lock);

	/* First verify that a JIT_VA zone has not been created already. */
	if (kctx->jit_va)
		return -EINVAL;

	/*
	 * Modify the same VA free region after creation. Be careful to ensure
	 * that allocations haven't been made as they could cause an overlap
	 * to happen with existing same VA allocations and the custom VA zone.
	 */
	same_va = kbase_region_tracker_find_region_base_address(kctx,
			PAGE_SIZE);
	if (!same_va)
		return -ENOMEM;

	if (same_va->nr_pages < jit_va_pages || kctx->same_va_end < jit_va_pages)
		return -ENOMEM;

	/* It's safe to adjust the same VA zone now */
	same_va->nr_pages -= jit_va_pages;
	kctx->same_va_end -= jit_va_pages;

	/*
	 * Create a custom VA zone at the end of the VA for allocations which
	 * JIT can use so it doesn't have to allocate VA from the kernel.
	 */
	custom_va_reg = kbase_alloc_free_region(&kctx->reg_rbtree_custom,
				kctx->same_va_end,
				jit_va_pages,
				KBASE_REG_ZONE_CUSTOM_VA);

	/*
	 * The context will be destroyed if we fail here so no point
	 * reverting the change we made to same_va.
	 */
	if (!custom_va_reg)
		return -ENOMEM;

	kbase_region_tracker_insert(custom_va_reg);
	return 0;
}
#endif

int kbase_region_tracker_init_jit(struct kbase_context *kctx, u64 jit_va_pages,
		int max_allocations, int trim_level, int group_id,
		u64 phys_pages_limit)
{
	int err = 0;

	if (trim_level < 0 || trim_level > BASE_JIT_MAX_TRIM_LEVEL)
		return -EINVAL;

	if (group_id < 0 || group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)
		return -EINVAL;

#if MALI_JIT_PRESSURE_LIMIT
	if (phys_pages_limit > jit_va_pages)
#else
	if (phys_pages_limit != jit_va_pages)
#endif /* MALI_JIT_PRESSURE_LIMIT */
		return -EINVAL;

	kbase_gpu_vm_lock(kctx);

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT))
		err = kbase_region_tracker_init_jit_64(kctx, jit_va_pages);
#endif
	/*
	 * Nothing to do for 32-bit clients, JIT uses the existing
	 * custom VA zone.
	 */

	if (!err) {
		kctx->jit_max_allocations = max_allocations;
		kctx->trim_level = trim_level;
		kctx->jit_va = true;
		kctx->jit_group_id = group_id;
#if MALI_JIT_PRESSURE_LIMIT
		kctx->jit_phys_pages_limit = phys_pages_limit;
		dev_dbg(kctx->kbdev->dev, "phys_pages_limit set to %llu\n",
				phys_pages_limit);
#endif /* MALI_JIT_PRESSURE_LIMIT */
	}

	kbase_gpu_vm_unlock(kctx);

	return err;
}

int kbase_region_tracker_init_exec(struct kbase_context *kctx, u64 exec_va_pages)
{
	struct kbase_va_region *shrinking_va_reg;
	struct kbase_va_region *exec_va_reg;
	u64 exec_va_start, exec_va_base_addr;
	int err;

	/* The EXEC_VA zone shall be created by making space at the end of the
	 * address space. Firstly, verify that the number of EXEC_VA pages
	 * requested by the client is reasonable and then make sure that it is
	 * not greater than the address space itself before calculating the base
	 * address of the new zone.
	 */
	if (exec_va_pages == 0 || exec_va_pages > KBASE_REG_ZONE_EXEC_VA_MAX_PAGES)
		return -EINVAL;

	kbase_gpu_vm_lock(kctx);

	/* First verify that a JIT_VA zone has not been created already. */
	if (kctx->jit_va) {
		err = -EPERM;
		goto exit_unlock;
	}

	if (exec_va_pages > kctx->gpu_va_end) {
		err = -ENOMEM;
		goto exit_unlock;
	}

	exec_va_start = kctx->gpu_va_end - exec_va_pages;
	exec_va_base_addr = exec_va_start << PAGE_SHIFT;

	shrinking_va_reg = kbase_region_tracker_find_region_enclosing_address(kctx,
			exec_va_base_addr);
	if (!shrinking_va_reg) {
		err = -ENOMEM;
		goto exit_unlock;
	}

	/* Make sure that the EXEC_VA region is still uninitialized */
	if ((shrinking_va_reg->flags & KBASE_REG_ZONE_MASK) ==
			KBASE_REG_ZONE_EXEC_VA) {
		err = -EPERM;
		goto exit_unlock;
	}

	if (shrinking_va_reg->nr_pages <= exec_va_pages) {
		err = -ENOMEM;
		goto exit_unlock;
	}

	exec_va_reg = kbase_alloc_free_region(&kctx->reg_rbtree_exec,
			exec_va_start,
			exec_va_pages,
			KBASE_REG_ZONE_EXEC_VA);
	if (!exec_va_reg) {
		err = -ENOMEM;
		goto exit_unlock;
	}

	shrinking_va_reg->nr_pages -= exec_va_pages;
#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT))
		kctx->same_va_end -= exec_va_pages;
#endif
	kctx->exec_va_start = exec_va_start;

	kbase_region_tracker_insert(exec_va_reg);
	err = 0;

exit_unlock:
	kbase_gpu_vm_unlock(kctx);
	return err;
}


int kbase_mem_init(struct kbase_device *kbdev)
{
	int err = 0;
	struct kbasep_mem_device *memdev;
#ifdef CONFIG_OF
	struct device_node *mgm_node = NULL;
#endif

	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	kbase_mem_pool_group_config_set_max_size(&kbdev->mem_pool_defaults,
		KBASE_MEM_POOL_MAX_SIZE_KCTX);

	/* Initialize memory usage */
	atomic_set(&memdev->used_pages, 0);

#ifdef IR_THRESHOLD
	atomic_set(&memdev->ir_threshold, IR_THRESHOLD);
#else
	atomic_set(&memdev->ir_threshold, DEFAULT_IR_THRESHOLD);
#endif

	kbdev->mgm_dev = &kbase_native_mgm_dev;

#ifdef CONFIG_OF
	/* Check to see whether or not a platform-specific memory group manager
	 * is configured and available.
	 */
	mgm_node = of_parse_phandle(kbdev->dev->of_node,
		"physical-memory-group-manager", 0);
	if (!mgm_node) {
		dev_info(kbdev->dev,
			"No memory group manager is configured\n");
	} else {
		struct platform_device *const pdev =
			of_find_device_by_node(mgm_node);

		if (!pdev) {
			dev_err(kbdev->dev,
				"The configured memory group manager was not found\n");
		} else {
			kbdev->mgm_dev = platform_get_drvdata(pdev);
			if (!kbdev->mgm_dev) {
				dev_info(kbdev->dev,
					"Memory group manager is not ready\n");
				err = -EPROBE_DEFER;
			} else if (!try_module_get(kbdev->mgm_dev->owner)) {
				dev_err(kbdev->dev,
					"Failed to get memory group manger module\n");
				err = -ENODEV;
				kbdev->mgm_dev = NULL;
			} else {
				dev_info(kbdev->dev,
					"Memory group manager successfully loaded\n");
			}
		}
		of_node_put(mgm_node);
	}
#endif

	if (likely(!err)) {
		struct kbase_mem_pool_group_config mem_pool_defaults;

		kbase_mem_pool_group_config_set_max_size(&mem_pool_defaults,
			KBASE_MEM_POOL_MAX_SIZE_KBDEV);

		err = kbase_mem_pool_group_init(&kbdev->mem_pools, kbdev,
			&mem_pool_defaults, NULL);
	}

	return err;
}

void kbase_mem_halt(struct kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_mem_term(struct kbase_device *kbdev)
{
	struct kbasep_mem_device *memdev;
	int pages;

	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	pages = atomic_read(&memdev->used_pages);
	if (pages != 0)
		dev_warn(kbdev->dev, "%s: %d pages in use!\n", __func__, pages);

	kbase_mem_pool_group_term(&kbdev->mem_pools);

	if (kbdev->mgm_dev)
		module_put(kbdev->mgm_dev->owner);
}
KBASE_EXPORT_TEST_API(kbase_mem_term);

/**
 * @brief Allocate a free region object.
 *
 * The allocated object is not part of any list yet, and is flagged as
 * KBASE_REG_FREE. No mapping is allocated yet.
 *
 * zone is KBASE_REG_ZONE_CUSTOM_VA or KBASE_REG_ZONE_SAME_VA.
 *
 */
struct kbase_va_region *kbase_alloc_free_region(struct rb_root *rbtree,
		u64 start_pfn, size_t nr_pages, int zone)
{
	struct kbase_va_region *new_reg;

	KBASE_DEBUG_ASSERT(rbtree != NULL);

	/* zone argument should only contain zone related region flags */
	KBASE_DEBUG_ASSERT((zone & ~KBASE_REG_ZONE_MASK) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_pfn + nr_pages <= (U64_MAX / PAGE_SIZE));

	new_reg = kzalloc(sizeof(*new_reg), GFP_KERNEL);

	if (!new_reg)
		return NULL;

	new_reg->va_refcnt = 1;
	new_reg->cpu_alloc = NULL; /* no alloc bound yet */
	new_reg->gpu_alloc = NULL; /* no alloc bound yet */
	new_reg->rbtree = rbtree;
	new_reg->flags = zone | KBASE_REG_FREE;

	new_reg->flags |= KBASE_REG_GROWABLE;

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;

	INIT_LIST_HEAD(&new_reg->jit_node);
	INIT_LIST_HEAD(&new_reg->link);

	return new_reg;
}

KBASE_EXPORT_TEST_API(kbase_alloc_free_region);

static struct kbase_context *kbase_reg_flags_to_kctx(
		struct kbase_va_region *reg)
{
	struct kbase_context *kctx = NULL;
	struct rb_root *rbtree = reg->rbtree;

	switch (reg->flags & KBASE_REG_ZONE_MASK) {
	case KBASE_REG_ZONE_CUSTOM_VA:
		kctx = container_of(rbtree, struct kbase_context,
				reg_rbtree_custom);
		break;
	case KBASE_REG_ZONE_SAME_VA:
		kctx = container_of(rbtree, struct kbase_context,
				reg_rbtree_same);
		break;
	case KBASE_REG_ZONE_EXEC_VA:
		kctx = container_of(rbtree, struct kbase_context,
				reg_rbtree_exec);
		break;
	default:
		WARN(1, "Unknown zone in region: flags=0x%lx\n", reg->flags);
		break;
	}

	return kctx;
}

/**
 * @brief Free a region object.
 *
 * The described region must be freed of any mapping.
 *
 * If the region is not flagged as KBASE_REG_FREE, the region's
 * alloc object will be released.
 * It is a bug if no alloc object exists for non-free regions.
 *
 */
void kbase_free_alloced_region(struct kbase_va_region *reg)
{
	if (!(reg->flags & KBASE_REG_FREE)) {
		struct kbase_context *kctx = kbase_reg_flags_to_kctx(reg);

		if (WARN_ON(!kctx))
			return;

		if (WARN_ON(kbase_is_region_invalid(reg)))
			return;

		dev_dbg(kctx->kbdev->dev, "Freeing memory region %p\n",
			(void *)reg);

		mutex_lock(&kctx->jit_evict_lock);

		/*
		 * The physical allocation should have been removed from the
		 * eviction list before this function is called. However, in the
		 * case of abnormal process termination or the app leaking the
		 * memory kbase_mem_free_region is not called so it can still be
		 * on the list at termination time of the region tracker.
		 */
		if (!list_empty(&reg->gpu_alloc->evict_node)) {
			mutex_unlock(&kctx->jit_evict_lock);

			/*
			 * Unlink the physical allocation before unmaking it
			 * evictable so that the allocation isn't grown back to
			 * its last backed size as we're going to unmap it
			 * anyway.
			 */
			reg->cpu_alloc->reg = NULL;
			if (reg->cpu_alloc != reg->gpu_alloc)
				reg->gpu_alloc->reg = NULL;

			/*
			 * If a region has been made evictable then we must
			 * unmake it before trying to free it.
			 * If the memory hasn't been reclaimed it will be
			 * unmapped and freed below, if it has been reclaimed
			 * then the operations below are no-ops.
			 */
			if (reg->flags & KBASE_REG_DONT_NEED) {
				KBASE_DEBUG_ASSERT(reg->cpu_alloc->type ==
						   KBASE_MEM_TYPE_NATIVE);
				kbase_mem_evictable_unmake(reg->gpu_alloc);
			}
		} else {
			mutex_unlock(&kctx->jit_evict_lock);
		}

		/*
		 * Remove the region from the sticky resource metadata
		 * list should it be there.
		 */
		kbase_sticky_resource_release_force(kctx, NULL,
				reg->start_pfn << PAGE_SHIFT);

		kbase_mem_phy_alloc_put(reg->cpu_alloc);
		kbase_mem_phy_alloc_put(reg->gpu_alloc);

		reg->flags |= KBASE_REG_VA_FREED;
		kbase_va_region_alloc_put(kctx, reg);
	} else {
		kfree(reg);
	}
}

KBASE_EXPORT_TEST_API(kbase_free_alloced_region);

int kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr, size_t nr_pages, size_t align)
{
	int err;
	size_t i = 0;
	unsigned long attr;
	unsigned long mask = ~KBASE_REG_MEMATTR_MASK;
	unsigned long gwt_mask = ~0;
	int group_id;
	struct kbase_mem_phy_alloc *alloc;

#ifdef CONFIG_MALI_CINSTR_GWT
	if (kctx->gwt_enabled)
		gwt_mask = ~KBASE_REG_GPU_WR;
#endif

	if ((kctx->kbdev->system_coherency == COHERENCY_ACE) &&
		(reg->flags & KBASE_REG_SHARE_BOTH))
		attr = KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_OUTER_WA);
	else
		attr = KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_WRITE_ALLOC);

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	err = kbase_add_va_region(kctx, reg, addr, nr_pages, align);
	if (err)
		return err;

	alloc = reg->gpu_alloc;
	group_id = alloc->group_id;

	if (reg->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS) {
		u64 const stride = alloc->imported.alias.stride;

		KBASE_DEBUG_ASSERT(alloc->imported.alias.aliased);
		for (i = 0; i < alloc->imported.alias.nents; i++) {
			if (alloc->imported.alias.aliased[i].alloc) {
				err = kbase_mmu_insert_pages(kctx->kbdev,
						&kctx->mmu,
						reg->start_pfn + (i * stride),
						alloc->imported.alias.aliased[i].alloc->pages + alloc->imported.alias.aliased[i].offset,
						alloc->imported.alias.aliased[i].length,
						reg->flags & gwt_mask,
						kctx->as_nr,
						group_id);
				if (err)
					goto bad_insert;

				kbase_mem_phy_alloc_gpu_mapped(alloc->imported.alias.aliased[i].alloc);
			} else {
				err = kbase_mmu_insert_single_page(kctx,
					reg->start_pfn + i * stride,
					kctx->aliasing_sink_page,
					alloc->imported.alias.aliased[i].length,
					(reg->flags & mask & gwt_mask) | attr,
					group_id);

				if (err)
					goto bad_insert;
			}
		}
	} else {
		err = kbase_mmu_insert_pages(kctx->kbdev,
				&kctx->mmu,
				reg->start_pfn,
				kbase_get_gpu_phy_pages(reg),
				kbase_reg_current_backed_size(reg),
				reg->flags & gwt_mask,
				kctx->as_nr,
				group_id);
		if (err)
			goto bad_insert;
		kbase_mem_phy_alloc_gpu_mapped(alloc);
	}

	if (reg->flags & KBASE_REG_IMPORT_PAD &&
	    !WARN_ON(reg->nr_pages < reg->gpu_alloc->nents) &&
	    reg->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM &&
	    reg->gpu_alloc->imported.umm.current_mapping_usage_count) {
		/* For padded imported dma-buf memory, map the dummy aliasing
		 * page from the end of the dma-buf pages, to the end of the
		 * region using a read only mapping.
		 *
		 * Only map when it's imported dma-buf memory that is currently
		 * mapped.
		 *
		 * Assume reg->gpu_alloc->nents is the number of actual pages
		 * in the dma-buf memory.
		 */
		err = kbase_mmu_insert_single_page(kctx,
				reg->start_pfn + reg->gpu_alloc->nents,
				kctx->aliasing_sink_page,
				reg->nr_pages - reg->gpu_alloc->nents,
				(reg->flags | KBASE_REG_GPU_RD) &
				~KBASE_REG_GPU_WR,
				KBASE_MEM_GROUP_SINK);
		if (err)
			goto bad_insert;
	}

	return err;

bad_insert:
	kbase_mmu_teardown_pages(kctx->kbdev, &kctx->mmu,
				 reg->start_pfn, reg->nr_pages,
				 kctx->as_nr);

	if (alloc->type == KBASE_MEM_TYPE_ALIAS) {
		KBASE_DEBUG_ASSERT(alloc->imported.alias.aliased);
		while (i--)
			if (alloc->imported.alias.aliased[i].alloc)
				kbase_mem_phy_alloc_gpu_unmapped(alloc->imported.alias.aliased[i].alloc);
	}

	kbase_remove_va_region(reg);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_gpu_mmap);

static void kbase_jd_user_buf_unmap(struct kbase_context *kctx,
		struct kbase_mem_phy_alloc *alloc, bool writeable);

int kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	int err = 0;
	size_t i;

	if (reg->start_pfn == 0)
		return 0;

	if (!reg->gpu_alloc)
		return -EINVAL;

	/* Tear down down GPU page tables, depending on memory type. */
	switch (reg->gpu_alloc->type) {
	case KBASE_MEM_TYPE_ALIAS: /* Fall-through */
	case KBASE_MEM_TYPE_IMPORTED_UMM:
		err = kbase_mmu_teardown_pages(kctx->kbdev, &kctx->mmu,
				reg->start_pfn, reg->nr_pages, kctx->as_nr);
		break;
	default:
		err = kbase_mmu_teardown_pages(kctx->kbdev, &kctx->mmu,
			reg->start_pfn, kbase_reg_current_backed_size(reg),
			kctx->as_nr);
		break;
	}

	/* Update tracking, and other cleanup, depending on memory type. */
	switch (reg->gpu_alloc->type) {
	case KBASE_MEM_TYPE_ALIAS:
		KBASE_DEBUG_ASSERT(reg->gpu_alloc->imported.alias.aliased);
		for (i = 0; i < reg->gpu_alloc->imported.alias.nents; i++)
			if (reg->gpu_alloc->imported.alias.aliased[i].alloc)
				kbase_mem_phy_alloc_gpu_unmapped(reg->gpu_alloc->imported.alias.aliased[i].alloc);
		break;
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
			struct kbase_alloc_import_user_buf *user_buf =
				&reg->gpu_alloc->imported.user_buf;

			if (user_buf->current_mapping_usage_count & PINNED_ON_IMPORT) {
				user_buf->current_mapping_usage_count &=
					~PINNED_ON_IMPORT;

				/* The allocation could still have active mappings. */
				if (user_buf->current_mapping_usage_count == 0) {
					kbase_jd_user_buf_unmap(kctx, reg->gpu_alloc,
						(reg->flags & KBASE_REG_GPU_WR));
				}
			}
		}
		/* Fall-through */
	default:
		kbase_mem_phy_alloc_gpu_unmapped(reg->gpu_alloc);
		break;
	}

	return err;
}

static struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping(
		struct kbase_context *kctx,
		unsigned long uaddr, size_t size, u64 *offset)
{
	struct vm_area_struct *vma;
	struct kbase_cpu_mapping *map;
	unsigned long vm_pgoff_in_region;
	unsigned long vm_off_in_region;
	unsigned long map_start;
	size_t map_size;

	lockdep_assert_held(&current->mm->mmap_sem);

	if ((uintptr_t) uaddr + size < (uintptr_t) uaddr) /* overflow check */
		return NULL;

	vma = find_vma_intersection(current->mm, uaddr, uaddr+size);

	if (!vma || vma->vm_start > uaddr)
		return NULL;
	if (vma->vm_ops != &kbase_vm_ops)
		/* Not ours! */
		return NULL;

	map = vma->vm_private_data;

	if (map->kctx != kctx)
		/* Not from this context! */
		return NULL;

	vm_pgoff_in_region = vma->vm_pgoff - map->region->start_pfn;
	vm_off_in_region = vm_pgoff_in_region << PAGE_SHIFT;
	map_start = vma->vm_start - vm_off_in_region;
	map_size = map->region->nr_pages << PAGE_SHIFT;

	if ((uaddr + size) > (map_start + map_size))
		/* Not within the CPU mapping */
		return NULL;

	*offset = (uaddr - vma->vm_start) + vm_off_in_region;

	return map;
}

int kbasep_find_enclosing_cpu_mapping_offset(
		struct kbase_context *kctx,
		unsigned long uaddr, size_t size, u64 *offset)
{
	struct kbase_cpu_mapping *map;

	kbase_os_mem_map_lock(kctx);

	map = kbasep_find_enclosing_cpu_mapping(kctx, uaddr, size, offset);

	kbase_os_mem_map_unlock(kctx);

	if (!map)
		return -EINVAL;

	return 0;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping_offset);

int kbasep_find_enclosing_gpu_mapping_start_and_offset(struct kbase_context *kctx,
		u64 gpu_addr, size_t size, u64 *start, u64 *offset)
{
	struct kbase_va_region *region;

	kbase_gpu_vm_lock(kctx);

	region = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);

	if (!region) {
		kbase_gpu_vm_unlock(kctx);
		return -EINVAL;
	}

	*start = region->start_pfn << PAGE_SHIFT;

	*offset = gpu_addr - *start;

	if (((region->start_pfn + region->nr_pages) << PAGE_SHIFT) < (gpu_addr + size)) {
		kbase_gpu_vm_unlock(kctx);
		return -EINVAL;
	}

	kbase_gpu_vm_unlock(kctx);

	return 0;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_gpu_mapping_start_and_offset);

void kbase_sync_single(struct kbase_context *kctx,
		struct tagged_addr t_cpu_pa, struct tagged_addr t_gpu_pa,
		off_t offset, size_t size, enum kbase_sync_type sync_fn)
{
	struct page *cpu_page;
	phys_addr_t cpu_pa = as_phys_addr_t(t_cpu_pa);
	phys_addr_t gpu_pa = as_phys_addr_t(t_gpu_pa);

	cpu_page = pfn_to_page(PFN_DOWN(cpu_pa));

	if (likely(cpu_pa == gpu_pa)) {
		dma_addr_t dma_addr;

		BUG_ON(!cpu_page);
		BUG_ON(offset + size > PAGE_SIZE);

		dma_addr = kbase_dma_addr(cpu_page) + offset;
		if (sync_fn == KBASE_SYNC_TO_CPU)
			dma_sync_single_for_cpu(kctx->kbdev->dev, dma_addr,
					size, DMA_BIDIRECTIONAL);
		else if (sync_fn == KBASE_SYNC_TO_DEVICE)
			dma_sync_single_for_device(kctx->kbdev->dev, dma_addr,
					size, DMA_BIDIRECTIONAL);
	} else {
		void *src = NULL;
		void *dst = NULL;
		struct page *gpu_page;

		if (WARN(!gpu_pa, "No GPU PA found for infinite cache op"))
			return;

		gpu_page = pfn_to_page(PFN_DOWN(gpu_pa));

		if (sync_fn == KBASE_SYNC_TO_DEVICE) {
			src = ((unsigned char *)kmap(cpu_page)) + offset;
			dst = ((unsigned char *)kmap(gpu_page)) + offset;
		} else if (sync_fn == KBASE_SYNC_TO_CPU) {
			dma_sync_single_for_cpu(kctx->kbdev->dev,
					kbase_dma_addr(gpu_page) + offset,
					size, DMA_BIDIRECTIONAL);
			src = ((unsigned char *)kmap(gpu_page)) + offset;
			dst = ((unsigned char *)kmap(cpu_page)) + offset;
		}
		memcpy(dst, src, size);
		kunmap(gpu_page);
		kunmap(cpu_page);
		if (sync_fn == KBASE_SYNC_TO_DEVICE)
			dma_sync_single_for_device(kctx->kbdev->dev,
					kbase_dma_addr(gpu_page) + offset,
					size, DMA_BIDIRECTIONAL);
	}
}

static int kbase_do_syncset(struct kbase_context *kctx,
		struct basep_syncset *sset, enum kbase_sync_type sync_fn)
{
	int err = 0;
	struct kbase_va_region *reg;
	struct kbase_cpu_mapping *map;
	unsigned long start;
	size_t size;
	struct tagged_addr *cpu_pa;
	struct tagged_addr *gpu_pa;
	u64 page_off, page_count;
	u64 i;
	u64 offset;

	kbase_os_mem_map_lock(kctx);
	kbase_gpu_vm_lock(kctx);

	/* find the region where the virtual address is contained */
	reg = kbase_region_tracker_find_region_enclosing_address(kctx,
			sset->mem_handle.basep.handle);
	if (kbase_is_region_invalid_or_free(reg)) {
		dev_warn(kctx->kbdev->dev, "Can't find a valid region at VA 0x%016llX",
				sset->mem_handle.basep.handle);
		err = -EINVAL;
		goto out_unlock;
	}

	/*
	 * Handle imported memory before checking for KBASE_REG_CPU_CACHED. The
	 * CPU mapping cacheability is defined by the owner of the imported
	 * memory, and not by kbase, therefore we must assume that any imported
	 * memory may be cached.
	 */
	if (kbase_mem_is_imported(reg->gpu_alloc->type)) {
		err = kbase_mem_do_sync_imported(kctx, reg, sync_fn);
		goto out_unlock;
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED))
		goto out_unlock;

	start = (uintptr_t)sset->user_addr;
	size = (size_t)sset->size;

	map = kbasep_find_enclosing_cpu_mapping(kctx, start, size, &offset);
	if (!map) {
		dev_warn(kctx->kbdev->dev, "Can't find CPU mapping 0x%016lX for VA 0x%016llX",
				start, sset->mem_handle.basep.handle);
		err = -EINVAL;
		goto out_unlock;
	}

	page_off = offset >> PAGE_SHIFT;
	offset &= ~PAGE_MASK;
	page_count = (size + offset + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	cpu_pa = kbase_get_cpu_phy_pages(reg);
	gpu_pa = kbase_get_gpu_phy_pages(reg);

	if (page_off > reg->nr_pages ||
			page_off + page_count > reg->nr_pages) {
		/* Sync overflows the region */
		err = -EINVAL;
		goto out_unlock;
	}

	/* Sync first page */
	if (as_phys_addr_t(cpu_pa[page_off])) {
		size_t sz = MIN(((size_t) PAGE_SIZE - offset), size);

		kbase_sync_single(kctx, cpu_pa[page_off], gpu_pa[page_off],
				offset, sz, sync_fn);
	}

	/* Sync middle pages (if any) */
	for (i = 1; page_count > 2 && i < page_count - 1; i++) {
		/* we grow upwards, so bail on first non-present page */
		if (!as_phys_addr_t(cpu_pa[page_off + i]))
			break;

		kbase_sync_single(kctx, cpu_pa[page_off + i],
				gpu_pa[page_off + i], 0, PAGE_SIZE, sync_fn);
	}

	/* Sync last page (if any) */
	if (page_count > 1 &&
	    as_phys_addr_t(cpu_pa[page_off + page_count - 1])) {
		size_t sz = ((start + size - 1) & ~PAGE_MASK) + 1;

		kbase_sync_single(kctx, cpu_pa[page_off + page_count - 1],
				gpu_pa[page_off + page_count - 1], 0, sz,
				sync_fn);
	}

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	kbase_os_mem_map_unlock(kctx);
	return err;
}

int kbase_sync_now(struct kbase_context *kctx, struct basep_syncset *sset)
{
	int err = -EINVAL;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(sset != NULL);

	if (sset->mem_handle.basep.handle & ~PAGE_MASK) {
		dev_warn(kctx->kbdev->dev,
				"mem_handle: passed parameter is invalid");
		return -EINVAL;
	}

	switch (sset->type) {
	case BASE_SYNCSET_OP_MSYNC:
		err = kbase_do_syncset(kctx, sset, KBASE_SYNC_TO_DEVICE);
		break;

	case BASE_SYNCSET_OP_CSYNC:
		err = kbase_do_syncset(kctx, sset, KBASE_SYNC_TO_CPU);
		break;

	default:
		dev_warn(kctx->kbdev->dev, "Unknown msync op %d\n", sset->type);
		break;
	}

	return err;
}

KBASE_EXPORT_TEST_API(kbase_sync_now);

/* vm lock must be held */
int kbase_mem_free_region(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	int err;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);
	dev_dbg(kctx->kbdev->dev, "%s %p in kctx %p\n",
		__func__, (void *)reg, (void *)kctx);
	lockdep_assert_held(&kctx->reg_lock);

	if (reg->flags & KBASE_REG_NO_USER_FREE) {
		dev_warn(kctx->kbdev->dev, "Attempt to free GPU memory whose freeing by user space is forbidden!\n");
		return -EINVAL;
	}

	/*
	 * Unlink the physical allocation before unmaking it evictable so
	 * that the allocation isn't grown back to its last backed size
	 * as we're going to unmap it anyway.
	 */
	reg->cpu_alloc->reg = NULL;
	if (reg->cpu_alloc != reg->gpu_alloc)
		reg->gpu_alloc->reg = NULL;

	/*
	 * If a region has been made evictable then we must unmake it
	 * before trying to free it.
	 * If the memory hasn't been reclaimed it will be unmapped and freed
	 * below, if it has been reclaimed then the operations below are no-ops.
	 */
	if (reg->flags & KBASE_REG_DONT_NEED) {
		KBASE_DEBUG_ASSERT(reg->cpu_alloc->type ==
				   KBASE_MEM_TYPE_NATIVE);
		kbase_mem_evictable_unmake(reg->gpu_alloc);
	}

	err = kbase_gpu_munmap(kctx, reg);
	if (err) {
		dev_warn(kctx->kbdev->dev, "Could not unmap from the GPU...\n");
		goto out;
	}

	/* This will also free the physical pages */
	kbase_free_alloced_region(reg);

 out:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mem_free_region);

/**
 * @brief Free the region from the GPU and unregister it.
 *
 * This function implements the free operation on a memory segment.
 * It will loudly fail if called with outstanding mappings.
 */
int kbase_mem_free(struct kbase_context *kctx, u64 gpu_addr)
{
	int err = 0;
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	dev_dbg(kctx->kbdev->dev, "%s 0x%llx in kctx %p\n",
		__func__, gpu_addr, (void *)kctx);

	if ((gpu_addr & ~PAGE_MASK) && (gpu_addr >= PAGE_SIZE)) {
		dev_warn(kctx->kbdev->dev, "kbase_mem_free: gpu_addr parameter is invalid");
		return -EINVAL;
	}

	if (0 == gpu_addr) {
		dev_warn(kctx->kbdev->dev, "gpu_addr 0 is reserved for the ringbuffer and it's an error to try to free it using kbase_mem_free\n");
		return -EINVAL;
	}
	kbase_gpu_vm_lock(kctx);

	if (gpu_addr >= BASE_MEM_COOKIE_BASE &&
	    gpu_addr < BASE_MEM_FIRST_FREE_ADDRESS) {
		int cookie = PFN_DOWN(gpu_addr - BASE_MEM_COOKIE_BASE);

		reg = kctx->pending_regions[cookie];
		if (!reg) {
			err = -EINVAL;
			goto out_unlock;
		}

		/* ask to unlink the cookie as we'll free it */

		kctx->pending_regions[cookie] = NULL;
		bitmap_set(kctx->cookies, cookie, 1);

		kbase_free_alloced_region(reg);
	} else {
		/* A real GPU va */
		/* Validate the region */
		reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
		if (kbase_is_region_invalid_or_free(reg)) {
			dev_warn(kctx->kbdev->dev, "kbase_mem_free called with nonexistent gpu_addr 0x%llX",
					gpu_addr);
			err = -EINVAL;
			goto out_unlock;
		}

		if ((reg->flags & KBASE_REG_ZONE_MASK) == KBASE_REG_ZONE_SAME_VA) {
			/* SAME_VA must be freed through munmap */
			dev_warn(kctx->kbdev->dev, "%s called on SAME_VA memory 0x%llX", __func__,
					gpu_addr);
			err = -EINVAL;
			goto out_unlock;
		}
		err = kbase_mem_free_region(kctx, reg);
	}

 out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mem_free);

int kbase_update_region_flags(struct kbase_context *kctx,
		struct kbase_va_region *reg, unsigned long flags)
{
	KBASE_DEBUG_ASSERT(NULL != reg);
	KBASE_DEBUG_ASSERT((flags & ~((1ul << BASE_MEM_FLAGS_NR_BITS) - 1)) == 0);

	reg->flags |= kbase_cache_enabled(flags, reg->nr_pages);
	/* all memory is now growable */
	reg->flags |= KBASE_REG_GROWABLE;

	if (flags & BASE_MEM_GROW_ON_GPF)
		reg->flags |= KBASE_REG_PF_GROW;

	if (flags & BASE_MEM_PROT_CPU_WR)
		reg->flags |= KBASE_REG_CPU_WR;

	if (flags & BASE_MEM_PROT_CPU_RD)
		reg->flags |= KBASE_REG_CPU_RD;

	if (flags & BASE_MEM_PROT_GPU_WR)
		reg->flags |= KBASE_REG_GPU_WR;

	if (flags & BASE_MEM_PROT_GPU_RD)
		reg->flags |= KBASE_REG_GPU_RD;

	if (0 == (flags & BASE_MEM_PROT_GPU_EX))
		reg->flags |= KBASE_REG_GPU_NX;

	if (!kbase_device_is_cpu_coherent(kctx->kbdev)) {
		if (flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED &&
				!(flags & BASE_MEM_UNCACHED_GPU))
			return -EINVAL;
	} else if (flags & (BASE_MEM_COHERENT_SYSTEM |
			BASE_MEM_COHERENT_SYSTEM_REQUIRED)) {
		reg->flags |= KBASE_REG_SHARE_BOTH;
	}

	if (!(reg->flags & KBASE_REG_SHARE_BOTH) &&
			flags & BASE_MEM_COHERENT_LOCAL) {
		reg->flags |= KBASE_REG_SHARE_IN;
	}

	if (flags & BASE_MEM_TILER_ALIGN_TOP)
		reg->flags |= KBASE_REG_TILER_ALIGN_TOP;


	/* Set up default MEMATTR usage */
	if (!(reg->flags & KBASE_REG_GPU_CACHED)) {
		if (kctx->kbdev->mmu_mode->flags &
				KBASE_MMU_MODE_HAS_NON_CACHEABLE) {
			/* Override shareability, and MEMATTR for uncached */
			reg->flags &= ~(KBASE_REG_SHARE_IN | KBASE_REG_SHARE_BOTH);
			reg->flags |= KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_NON_CACHEABLE);
		} else {
			dev_warn(kctx->kbdev->dev,
				"Can't allocate GPU uncached memory due to MMU in Legacy Mode\n");
			return -EINVAL;
		}
	} else if (kctx->kbdev->system_coherency == COHERENCY_ACE &&
		(reg->flags & KBASE_REG_SHARE_BOTH)) {
		reg->flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT_ACE);
	} else {
		reg->flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT);
	}

	if (flags & BASEP_MEM_PERMANENT_KERNEL_MAPPING)
		reg->flags |= KBASE_REG_PERMANENT_KERNEL_MAPPING;

	if (flags & BASEP_MEM_NO_USER_FREE)
		reg->flags |= KBASE_REG_NO_USER_FREE;

	if (flags & BASE_MEM_GPU_VA_SAME_4GB_PAGE)
		reg->flags |= KBASE_REG_GPU_VA_SAME_4GB_PAGE;

	return 0;
}

int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc *alloc,
		size_t nr_pages_requested)
{
	int new_page_count __maybe_unused;
	size_t nr_left = nr_pages_requested;
	int res;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct tagged_addr *tp;

	if (WARN_ON(alloc->type != KBASE_MEM_TYPE_NATIVE) ||
	    WARN_ON(alloc->imported.native.kctx == NULL) ||
	    WARN_ON(alloc->group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)) {
		return -EINVAL;
	}

	if (alloc->reg) {
		if (nr_pages_requested > alloc->reg->nr_pages - alloc->nents)
			goto invalid_request;
	}

	kctx = alloc->imported.native.kctx;
	kbdev = kctx->kbdev;

	if (nr_pages_requested == 0)
		goto done; /*nothing to do*/

	new_page_count = atomic_add_return(
		nr_pages_requested, &kctx->used_pages);
	atomic_add(nr_pages_requested,
		&kctx->kbdev->memdev.used_pages);

	/* Increase mm counters before we allocate pages so that this
	 * allocation is visible to the OOM killer */
	kbase_process_page_usage_inc(kctx, nr_pages_requested);

	tp = alloc->pages + alloc->nents;

#ifdef CONFIG_MALI_2MB_ALLOC
	/* Check if we have enough pages requested so we can allocate a large
	 * page (512 * 4KB = 2MB )
	 */
	if (nr_left >= (SZ_2M / SZ_4K)) {
		int nr_lp = nr_left / (SZ_2M / SZ_4K);

		res = kbase_mem_pool_alloc_pages(
			&kctx->mem_pools.large[alloc->group_id],
			 nr_lp * (SZ_2M / SZ_4K),
			 tp,
			 true);

		if (res > 0) {
			nr_left -= res;
			tp += res;
		}

		if (nr_left) {
			struct kbase_sub_alloc *sa, *temp_sa;

			spin_lock(&kctx->mem_partials_lock);

			list_for_each_entry_safe(sa, temp_sa,
						 &kctx->mem_partials, link) {
				int pidx = 0;

				while (nr_left) {
					pidx = find_next_zero_bit(sa->sub_pages,
								  SZ_2M / SZ_4K,
								  pidx);
					bitmap_set(sa->sub_pages, pidx, 1);
					*tp++ = as_tagged_tag(page_to_phys(sa->page +
									   pidx),
							      FROM_PARTIAL);
					nr_left--;

					if (bitmap_full(sa->sub_pages, SZ_2M / SZ_4K)) {
						/* unlink from partial list when full */
						list_del_init(&sa->link);
						break;
					}
				}
			}
			spin_unlock(&kctx->mem_partials_lock);
		}

		/* only if we actually have a chunk left <512. If more it indicates
		 * that we couldn't allocate a 2MB above, so no point to retry here.
		 */
		if (nr_left > 0 && nr_left < (SZ_2M / SZ_4K)) {
			/* create a new partial and suballocate the rest from it */
			struct page *np = NULL;

			do {
				int err;

				np = kbase_mem_pool_alloc(
					&kctx->mem_pools.large[
						alloc->group_id]);
				if (np)
					break;

				err = kbase_mem_pool_grow(
					&kctx->mem_pools.large[alloc->group_id],
					1);
				if (err)
					break;
			} while (1);

			if (np) {
				int i;
				struct kbase_sub_alloc *sa;
				struct page *p;

				sa = kmalloc(sizeof(*sa), GFP_KERNEL);
				if (!sa) {
					kbase_mem_pool_free(
						&kctx->mem_pools.large[
							alloc->group_id],
						np,
						false);
					goto no_new_partial;
				}

				/* store pointers back to the control struct */
				np->lru.next = (void *)sa;
				for (p = np; p < np + SZ_2M / SZ_4K; p++)
					p->lru.prev = (void *)np;
				INIT_LIST_HEAD(&sa->link);
				bitmap_zero(sa->sub_pages, SZ_2M / SZ_4K);
				sa->page = np;

				for (i = 0; i < nr_left; i++)
					*tp++ = as_tagged_tag(page_to_phys(np + i), FROM_PARTIAL);

				bitmap_set(sa->sub_pages, 0, nr_left);
				nr_left = 0;

				/* expose for later use */
				spin_lock(&kctx->mem_partials_lock);
				list_add(&sa->link, &kctx->mem_partials);
				spin_unlock(&kctx->mem_partials_lock);
			}
		}
	}
no_new_partial:
#endif

	if (nr_left) {
		res = kbase_mem_pool_alloc_pages(
			&kctx->mem_pools.small[alloc->group_id],
			nr_left, tp, false);
		if (res <= 0)
			goto alloc_failed;
	}

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kbdev,
			kctx->id,
			(u64)new_page_count);

	alloc->nents += nr_pages_requested;
done:
	return 0;

alloc_failed:
	/* rollback needed if got one or more 2MB but failed later */
	if (nr_left != nr_pages_requested) {
		size_t nr_pages_to_free = nr_pages_requested - nr_left;

		alloc->nents += nr_pages_to_free;

		kbase_process_page_usage_inc(kctx, nr_pages_to_free);
		atomic_add(nr_pages_to_free, &kctx->used_pages);
		atomic_add(nr_pages_to_free,
			&kctx->kbdev->memdev.used_pages);

		kbase_free_phy_pages_helper(alloc, nr_pages_to_free);
	}

	kbase_process_page_usage_dec(kctx, nr_pages_requested);
	atomic_sub(nr_pages_requested, &kctx->used_pages);
	atomic_sub(nr_pages_requested,
		&kctx->kbdev->memdev.used_pages);

invalid_request:
	return -ENOMEM;
}

struct tagged_addr *kbase_alloc_phy_pages_helper_locked(
		struct kbase_mem_phy_alloc *alloc, struct kbase_mem_pool *pool,
		size_t nr_pages_requested,
		struct kbase_sub_alloc **prealloc_sa)
{
	int new_page_count __maybe_unused;
	size_t nr_left = nr_pages_requested;
	int res;
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	struct tagged_addr *tp;
	struct tagged_addr *new_pages = NULL;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.native.kctx);

	lockdep_assert_held(&pool->pool_lock);

#if !defined(CONFIG_MALI_2MB_ALLOC)
	WARN_ON(pool->order);
#endif

	if (alloc->reg) {
		if (nr_pages_requested > alloc->reg->nr_pages - alloc->nents)
			goto invalid_request;
	}

	kctx = alloc->imported.native.kctx;
	kbdev = kctx->kbdev;

	lockdep_assert_held(&kctx->mem_partials_lock);

	if (nr_pages_requested == 0)
		goto done; /*nothing to do*/

	new_page_count = atomic_add_return(
		nr_pages_requested, &kctx->used_pages);
	atomic_add(nr_pages_requested,
		&kctx->kbdev->memdev.used_pages);

	/* Increase mm counters before we allocate pages so that this
	 * allocation is visible to the OOM killer
	 */
	kbase_process_page_usage_inc(kctx, nr_pages_requested);

	tp = alloc->pages + alloc->nents;
	new_pages = tp;

#ifdef CONFIG_MALI_2MB_ALLOC
	if (pool->order) {
		int nr_lp = nr_left / (SZ_2M / SZ_4K);

		res = kbase_mem_pool_alloc_pages_locked(pool,
						 nr_lp * (SZ_2M / SZ_4K),
						 tp);

		if (res > 0) {
			nr_left -= res;
			tp += res;
		}

		if (nr_left) {
			struct kbase_sub_alloc *sa, *temp_sa;

			list_for_each_entry_safe(sa, temp_sa,
						 &kctx->mem_partials, link) {
				int pidx = 0;

				while (nr_left) {
					pidx = find_next_zero_bit(sa->sub_pages,
								  SZ_2M / SZ_4K,
								  pidx);
					bitmap_set(sa->sub_pages, pidx, 1);
					*tp++ = as_tagged_tag(page_to_phys(
							sa->page + pidx),
							FROM_PARTIAL);
					nr_left--;

					if (bitmap_full(sa->sub_pages,
							SZ_2M / SZ_4K)) {
						/* unlink from partial list when
						 * full
						 */
						list_del_init(&sa->link);
						break;
					}
				}
			}
		}

		/* only if we actually have a chunk left <512. If more it
		 * indicates that we couldn't allocate a 2MB above, so no point
		 * to retry here.
		 */
		if (nr_left > 0 && nr_left < (SZ_2M / SZ_4K)) {
			/* create a new partial and suballocate the rest from it
			 */
			struct page *np = NULL;

			np = kbase_mem_pool_alloc_locked(pool);

			if (np) {
				int i;
				struct kbase_sub_alloc *const sa = *prealloc_sa;
				struct page *p;

				/* store pointers back to the control struct */
				np->lru.next = (void *)sa;
				for (p = np; p < np + SZ_2M / SZ_4K; p++)
					p->lru.prev = (void *)np;
				INIT_LIST_HEAD(&sa->link);
				bitmap_zero(sa->sub_pages, SZ_2M / SZ_4K);
				sa->page = np;

				for (i = 0; i < nr_left; i++)
					*tp++ = as_tagged_tag(
							page_to_phys(np + i),
							FROM_PARTIAL);

				bitmap_set(sa->sub_pages, 0, nr_left);
				nr_left = 0;
				/* Indicate to user that we'll free this memory
				 * later.
				 */
				*prealloc_sa = NULL;

				/* expose for later use */
				list_add(&sa->link, &kctx->mem_partials);
			}
		}
		if (nr_left)
			goto alloc_failed;
	} else {
#endif
		res = kbase_mem_pool_alloc_pages_locked(pool,
						 nr_left,
						 tp);
		if (res <= 0)
			goto alloc_failed;
#ifdef CONFIG_MALI_2MB_ALLOC
	}
#endif

	KBASE_TLSTREAM_AUX_PAGESALLOC(
			kbdev,
			kctx->id,
			(u64)new_page_count);

	alloc->nents += nr_pages_requested;
done:
	return new_pages;

alloc_failed:
	/* rollback needed if got one or more 2MB but failed later */
	if (nr_left != nr_pages_requested) {
		size_t nr_pages_to_free = nr_pages_requested - nr_left;

		struct tagged_addr *start_free = alloc->pages + alloc->nents;

#ifdef CONFIG_MALI_2MB_ALLOC
		if (pool->order) {
			while (nr_pages_to_free) {
				if (is_huge_head(*start_free)) {
					kbase_mem_pool_free_pages_locked(
						pool, 512,
						start_free,
						false, /* not dirty */
						true); /* return to pool */
					nr_pages_to_free -= 512;
					start_free += 512;
				} else if (is_partial(*start_free)) {
					free_partial_locked(kctx, pool,
							*start_free);
					nr_pages_to_free--;
					start_free++;
				}
			}
		} else {
#endif
			kbase_mem_pool_free_pages_locked(pool,
					nr_pages_to_free,
					start_free,
					false, /* not dirty */
					true); /* return to pool */
#ifdef CONFIG_MALI_2MB_ALLOC
		}
#endif
	}

	kbase_process_page_usage_dec(kctx, nr_pages_requested);
	atomic_sub(nr_pages_requested, &kctx->used_pages);
	atomic_sub(nr_pages_requested, &kctx->kbdev->memdev.used_pages);

invalid_request:
	return NULL;
}

static void free_partial(struct kbase_context *kctx, int group_id, struct
		tagged_addr tp)
{
	struct page *p, *head_page;
	struct kbase_sub_alloc *sa;

	p = as_page(tp);
	head_page = (struct page *)p->lru.prev;
	sa = (struct kbase_sub_alloc *)head_page->lru.next;
	spin_lock(&kctx->mem_partials_lock);
	clear_bit(p - head_page, sa->sub_pages);
	if (bitmap_empty(sa->sub_pages, SZ_2M / SZ_4K)) {
		list_del(&sa->link);
		kbase_mem_pool_free(
			&kctx->mem_pools.large[group_id],
			head_page,
			true);
		kfree(sa);
	} else if (bitmap_weight(sa->sub_pages, SZ_2M / SZ_4K) ==
		   SZ_2M / SZ_4K - 1) {
		/* expose the partial again */
		list_add(&sa->link, &kctx->mem_partials);
	}
	spin_unlock(&kctx->mem_partials_lock);
}

int kbase_free_phy_pages_helper(
	struct kbase_mem_phy_alloc *alloc,
	size_t nr_pages_to_free)
{
	struct kbase_context *kctx = alloc->imported.native.kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	bool syncback;
	bool reclaimed = (alloc->evicted != 0);
	struct tagged_addr *start_free;
	int new_page_count __maybe_unused;
	size_t freed = 0;

	if (WARN_ON(alloc->type != KBASE_MEM_TYPE_NATIVE) ||
	    WARN_ON(alloc->imported.native.kctx == NULL) ||
	    WARN_ON(alloc->nents < nr_pages_to_free) ||
	    WARN_ON(alloc->group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)) {
		return -EINVAL;
	}

	/* early out if nothing to do */
	if (0 == nr_pages_to_free)
		return 0;

	start_free = alloc->pages + alloc->nents - nr_pages_to_free;

	syncback = alloc->properties & KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED;

	/* pad start_free to a valid start location */
	while (nr_pages_to_free && is_huge(*start_free) &&
	       !is_huge_head(*start_free)) {
		nr_pages_to_free--;
		start_free++;
	}

	while (nr_pages_to_free) {
		if (is_huge_head(*start_free)) {
			/* This is a 2MB entry, so free all the 512 pages that
			 * it points to
			 */
			kbase_mem_pool_free_pages(
				&kctx->mem_pools.large[alloc->group_id],
				512,
				start_free,
				syncback,
				reclaimed);
			nr_pages_to_free -= 512;
			start_free += 512;
			freed += 512;
		} else if (is_partial(*start_free)) {
			free_partial(kctx, alloc->group_id, *start_free);
			nr_pages_to_free--;
			start_free++;
			freed++;
		} else {
			struct tagged_addr *local_end_free;

			local_end_free = start_free;
			while (nr_pages_to_free &&
				!is_huge(*local_end_free) &&
				!is_partial(*local_end_free)) {
				local_end_free++;
				nr_pages_to_free--;
			}
			kbase_mem_pool_free_pages(
				&kctx->mem_pools.small[alloc->group_id],
				local_end_free - start_free,
				start_free,
				syncback,
				reclaimed);
			freed += local_end_free - start_free;
			start_free += local_end_free - start_free;
		}
	}

	alloc->nents -= freed;

	/*
	 * If the allocation was not evicted (i.e. evicted == 0) then
	 * the page accounting needs to be done.
	 */
	if (!reclaimed) {
		kbase_process_page_usage_dec(kctx, freed);
		new_page_count = atomic_sub_return(freed,
			&kctx->used_pages);
		atomic_sub(freed,
			&kctx->kbdev->memdev.used_pages);

		KBASE_TLSTREAM_AUX_PAGESALLOC(
			kbdev,
			kctx->id,
			(u64)new_page_count);
	}

	return 0;
}

static void free_partial_locked(struct kbase_context *kctx,
		struct kbase_mem_pool *pool, struct tagged_addr tp)
{
	struct page *p, *head_page;
	struct kbase_sub_alloc *sa;

	lockdep_assert_held(&pool->pool_lock);
	lockdep_assert_held(&kctx->mem_partials_lock);

	p = as_page(tp);
	head_page = (struct page *)p->lru.prev;
	sa = (struct kbase_sub_alloc *)head_page->lru.next;
	clear_bit(p - head_page, sa->sub_pages);
	if (bitmap_empty(sa->sub_pages, SZ_2M / SZ_4K)) {
		list_del(&sa->link);
		kbase_mem_pool_free_locked(pool, head_page, true);
		kfree(sa);
	} else if (bitmap_weight(sa->sub_pages, SZ_2M / SZ_4K) ==
		   SZ_2M / SZ_4K - 1) {
		/* expose the partial again */
		list_add(&sa->link, &kctx->mem_partials);
	}
}

void kbase_free_phy_pages_helper_locked(struct kbase_mem_phy_alloc *alloc,
		struct kbase_mem_pool *pool, struct tagged_addr *pages,
		size_t nr_pages_to_free)
{
	struct kbase_context *kctx = alloc->imported.native.kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	bool syncback;
	bool reclaimed = (alloc->evicted != 0);
	struct tagged_addr *start_free;
	size_t freed = 0;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.native.kctx);
	KBASE_DEBUG_ASSERT(alloc->nents >= nr_pages_to_free);

	lockdep_assert_held(&pool->pool_lock);
	lockdep_assert_held(&kctx->mem_partials_lock);

	/* early out if nothing to do */
	if (!nr_pages_to_free)
		return;

	start_free = pages;

	syncback = alloc->properties & KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED;

	/* pad start_free to a valid start location */
	while (nr_pages_to_free && is_huge(*start_free) &&
	       !is_huge_head(*start_free)) {
		nr_pages_to_free--;
		start_free++;
	}

	while (nr_pages_to_free) {
		if (is_huge_head(*start_free)) {
			/* This is a 2MB entry, so free all the 512 pages that
			 * it points to
			 */
			WARN_ON(!pool->order);
			kbase_mem_pool_free_pages_locked(pool,
					512,
					start_free,
					syncback,
					reclaimed);
			nr_pages_to_free -= 512;
			start_free += 512;
			freed += 512;
		} else if (is_partial(*start_free)) {
			WARN_ON(!pool->order);
			free_partial_locked(kctx, pool, *start_free);
			nr_pages_to_free--;
			start_free++;
			freed++;
		} else {
			struct tagged_addr *local_end_free;

			WARN_ON(pool->order);
			local_end_free = start_free;
			while (nr_pages_to_free &&
			       !is_huge(*local_end_free) &&
			       !is_partial(*local_end_free)) {
				local_end_free++;
				nr_pages_to_free--;
			}
			kbase_mem_pool_free_pages_locked(pool,
					local_end_free - start_free,
					start_free,
					syncback,
					reclaimed);
			freed += local_end_free - start_free;
			start_free += local_end_free - start_free;
		}
	}

	alloc->nents -= freed;

	/*
	 * If the allocation was not evicted (i.e. evicted == 0) then
	 * the page accounting needs to be done.
	 */
	if (!reclaimed) {
		int new_page_count;

		kbase_process_page_usage_dec(kctx, freed);
		new_page_count = atomic_sub_return(freed,
			&kctx->used_pages);
		atomic_sub(freed,
			&kctx->kbdev->memdev.used_pages);

		KBASE_TLSTREAM_AUX_PAGESALLOC(
				kbdev,
				kctx->id,
				(u64)new_page_count);
	}
}


void kbase_mem_kref_free(struct kref *kref)
{
	struct kbase_mem_phy_alloc *alloc;

	alloc = container_of(kref, struct kbase_mem_phy_alloc, kref);

	switch (alloc->type) {
	case KBASE_MEM_TYPE_NATIVE: {

		if (!WARN_ON(!alloc->imported.native.kctx)) {
			if (alloc->permanent_map)
				kbase_phy_alloc_mapping_term(
						alloc->imported.native.kctx,
						alloc);

			/*
			 * The physical allocation must have been removed from
			 * the eviction list before trying to free it.
			 */
			mutex_lock(
				&alloc->imported.native.kctx->jit_evict_lock);
			WARN_ON(!list_empty(&alloc->evict_node));
			mutex_unlock(
				&alloc->imported.native.kctx->jit_evict_lock);

			kbase_process_page_usage_dec(
					alloc->imported.native.kctx,
					alloc->imported.native.nr_struct_pages);
		}
		kbase_free_phy_pages_helper(alloc, alloc->nents);
		break;
	}
	case KBASE_MEM_TYPE_ALIAS: {
		/* just call put on the underlying phy allocs */
		size_t i;
		struct kbase_aliased *aliased;

		aliased = alloc->imported.alias.aliased;
		if (aliased) {
			for (i = 0; i < alloc->imported.alias.nents; i++)
				if (aliased[i].alloc)
					kbase_mem_phy_alloc_put(aliased[i].alloc);
			vfree(aliased);
		}
		break;
	}
	case KBASE_MEM_TYPE_RAW:
		/* raw pages, external cleanup */
		break;
	case KBASE_MEM_TYPE_IMPORTED_UMM:
		if (!IS_ENABLED(CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND)) {
			WARN_ONCE(alloc->imported.umm.current_mapping_usage_count != 1,
					"WARNING: expected excatly 1 mapping, got %d",
					alloc->imported.umm.current_mapping_usage_count);
			dma_buf_unmap_attachment(
					alloc->imported.umm.dma_attachment,
					alloc->imported.umm.sgt,
					DMA_BIDIRECTIONAL);
		}
		dma_buf_detach(alloc->imported.umm.dma_buf,
			       alloc->imported.umm.dma_attachment);
		dma_buf_put(alloc->imported.umm.dma_buf);
		break;
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
		if (alloc->imported.user_buf.mm)
			mmdrop(alloc->imported.user_buf.mm);
		if (alloc->properties & KBASE_MEM_PHY_ALLOC_LARGE)
			vfree(alloc->imported.user_buf.pages);
		else
			kfree(alloc->imported.user_buf.pages);
		break;
	default:
		WARN(1, "Unexecpted free of type %d\n", alloc->type);
		break;
	}

	/* Free based on allocation type */
	if (alloc->properties & KBASE_MEM_PHY_ALLOC_LARGE)
		vfree(alloc);
	else
		kfree(alloc);
}

KBASE_EXPORT_TEST_API(kbase_mem_kref_free);

int kbase_alloc_phy_pages(struct kbase_va_region *reg, size_t vsize, size_t size)
{
	KBASE_DEBUG_ASSERT(NULL != reg);
	KBASE_DEBUG_ASSERT(vsize > 0);

	/* validate user provided arguments */
	if (size > vsize || vsize > reg->nr_pages)
		goto out_term;

	/* Prevent vsize*sizeof from wrapping around.
	 * For instance, if vsize is 2**29+1, we'll allocate 1 byte and the alloc won't fail.
	 */
	if ((size_t) vsize > ((size_t) -1 / sizeof(*reg->cpu_alloc->pages)))
		goto out_term;

	KBASE_DEBUG_ASSERT(0 != vsize);

	if (kbase_alloc_phy_pages_helper(reg->cpu_alloc, size) != 0)
		goto out_term;

	reg->cpu_alloc->reg = reg;
	if (reg->cpu_alloc != reg->gpu_alloc) {
		if (kbase_alloc_phy_pages_helper(reg->gpu_alloc, size) != 0)
			goto out_rollback;
		reg->gpu_alloc->reg = reg;
	}

	return 0;

out_rollback:
	kbase_free_phy_pages_helper(reg->cpu_alloc, size);
out_term:
	return -1;
}

KBASE_EXPORT_TEST_API(kbase_alloc_phy_pages);

bool kbase_check_alloc_flags(unsigned long flags)
{
	/* Only known input flags should be set. */
	if (flags & ~BASE_MEM_FLAGS_INPUT_MASK)
		return false;

	/* At least one flag should be set */
	if (flags == 0)
		return false;

	/* Either the GPU or CPU must be reading from the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD)) == 0)
		return false;

	/* Either the GPU or CPU must be writing to the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_WR)) == 0)
		return false;

	/* GPU executable memory cannot:
	 * - Be written by the GPU
	 * - Be grown on GPU page fault
	 * - Have the top of its initial commit aligned to 'extent' */
	if ((flags & BASE_MEM_PROT_GPU_EX) && (flags &
			(BASE_MEM_PROT_GPU_WR | BASE_MEM_GROW_ON_GPF |
			BASE_MEM_TILER_ALIGN_TOP)))
		return false;

	/* To have an allocation lie within a 4GB chunk is required only for
	 * TLS memory, which will never be used to contain executable code
	 * and also used for Tiler heap.
	 */
	if ((flags & BASE_MEM_GPU_VA_SAME_4GB_PAGE) && (flags &
			(BASE_MEM_PROT_GPU_EX | BASE_MEM_TILER_ALIGN_TOP)))
		return false;

	/* GPU should have at least read or write access otherwise there is no
	   reason for allocating. */
	if ((flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)) == 0)
		return false;

	/* BASE_MEM_IMPORT_SHARED is only valid for imported memory */
	if ((flags & BASE_MEM_IMPORT_SHARED) == BASE_MEM_IMPORT_SHARED)
		return false;

	/* BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP is only valid for imported
	 * memory */
	if ((flags & BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP) ==
			BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP)
		return false;

	/* Should not combine BASE_MEM_COHERENT_LOCAL with
	 * BASE_MEM_COHERENT_SYSTEM */
	if ((flags & (BASE_MEM_COHERENT_LOCAL | BASE_MEM_COHERENT_SYSTEM)) ==
			(BASE_MEM_COHERENT_LOCAL | BASE_MEM_COHERENT_SYSTEM))
		return false;

	return true;
}

bool kbase_check_import_flags(unsigned long flags)
{
	/* Only known input flags should be set. */
	if (flags & ~BASE_MEM_FLAGS_INPUT_MASK)
		return false;

	/* At least one flag should be set */
	if (flags == 0)
		return false;

	/* Imported memory cannot be GPU executable */
	if (flags & BASE_MEM_PROT_GPU_EX)
		return false;

	/* Imported memory cannot grow on page fault */
	if (flags & BASE_MEM_GROW_ON_GPF)
		return false;

	/* Imported memory cannot be aligned to the end of its initial commit */
	if (flags & BASE_MEM_TILER_ALIGN_TOP)
		return false;

	/* GPU should have at least read or write access otherwise there is no
	   reason for importing. */
	if ((flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)) == 0)
		return false;

	/* Protected memory cannot be read by the CPU */
	if ((flags & BASE_MEM_PROTECTED) && (flags & BASE_MEM_PROT_CPU_RD))
		return false;

	return true;
}

int kbase_check_alloc_sizes(struct kbase_context *kctx, unsigned long flags,
		u64 va_pages, u64 commit_pages, u64 large_extent)
{
	struct device *dev = kctx->kbdev->dev;
	int gpu_pc_bits = kctx->kbdev->gpu_props.props.core_props.log2_program_counter_size;
	u64 gpu_pc_pages_max = 1ULL << gpu_pc_bits >> PAGE_SHIFT;
	struct kbase_va_region test_reg;

	/* kbase_va_region's extent member can be of variable size, so check against that type */
	test_reg.extent = large_extent;

#define KBASE_MSG_PRE "GPU allocation attempted with "

	if (0 == va_pages) {
		dev_warn(dev, KBASE_MSG_PRE "0 va_pages!");
		return -EINVAL;
	}

	if (va_pages > KBASE_MEM_ALLOC_MAX_SIZE) {
		dev_warn(dev, KBASE_MSG_PRE "va_pages==%lld larger than KBASE_MEM_ALLOC_MAX_SIZE!",
				(unsigned long long)va_pages);
		return -ENOMEM;
	}

	/* Note: commit_pages is checked against va_pages during
	 * kbase_alloc_phy_pages() */

	/* Limit GPU executable allocs to GPU PC size */
	if ((flags & BASE_MEM_PROT_GPU_EX) && (va_pages > gpu_pc_pages_max)) {
		dev_warn(dev, KBASE_MSG_PRE "BASE_MEM_PROT_GPU_EX and va_pages==%lld larger than GPU PC range %lld",
				(unsigned long long)va_pages,
				(unsigned long long)gpu_pc_pages_max);

		return -EINVAL;
	}

	if ((flags & (BASE_MEM_GROW_ON_GPF | BASE_MEM_TILER_ALIGN_TOP)) &&
			test_reg.extent == 0) {
		dev_warn(dev, KBASE_MSG_PRE "BASE_MEM_GROW_ON_GPF or BASE_MEM_TILER_ALIGN_TOP but extent == 0\n");
		return -EINVAL;
	}

	if (!(flags & (BASE_MEM_GROW_ON_GPF | BASE_MEM_TILER_ALIGN_TOP)) &&
			test_reg.extent != 0) {
		dev_warn(dev, KBASE_MSG_PRE "neither BASE_MEM_GROW_ON_GPF nor BASE_MEM_TILER_ALIGN_TOP set but extent != 0\n");
		return -EINVAL;
	}

	/* BASE_MEM_TILER_ALIGN_TOP memory has a number of restrictions */
	if (flags & BASE_MEM_TILER_ALIGN_TOP) {
#define KBASE_MSG_PRE_FLAG KBASE_MSG_PRE "BASE_MEM_TILER_ALIGN_TOP and "
		unsigned long small_extent;

		if (large_extent > BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES) {
			dev_warn(dev, KBASE_MSG_PRE_FLAG "extent==%lld pages exceeds limit %lld",
					(unsigned long long)large_extent,
					BASE_MEM_TILER_ALIGN_TOP_EXTENT_MAX_PAGES);
			return -EINVAL;
		}
		/* For use with is_power_of_2, which takes unsigned long, so
		 * must ensure e.g. on 32-bit kernel it'll fit in that type */
		small_extent = (unsigned long)large_extent;

		if (!is_power_of_2(small_extent)) {
			dev_warn(dev, KBASE_MSG_PRE_FLAG "extent==%ld not a non-zero power of 2",
					small_extent);
			return -EINVAL;
		}

		if (commit_pages > large_extent) {
			dev_warn(dev, KBASE_MSG_PRE_FLAG "commit_pages==%ld exceeds extent==%ld",
					(unsigned long)commit_pages,
					(unsigned long)large_extent);
			return -EINVAL;
		}
#undef KBASE_MSG_PRE_FLAG
	}

	if ((flags & BASE_MEM_GPU_VA_SAME_4GB_PAGE) &&
	    (va_pages > (BASE_MEM_PFN_MASK_4GB + 1))) {
		dev_warn(dev, KBASE_MSG_PRE "BASE_MEM_GPU_VA_SAME_4GB_PAGE and va_pages==%lld greater than that needed for 4GB space",
				(unsigned long long)va_pages);
		return -EINVAL;
	}

	return 0;
#undef KBASE_MSG_PRE
}

/**
 * @brief Acquire the per-context region list lock
 */
void kbase_gpu_vm_lock(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_lock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_lock);

/**
 * @brief Release the per-context region list lock
 */
void kbase_gpu_vm_unlock(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_unlock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_unlock);

#ifdef CONFIG_DEBUG_FS
struct kbase_jit_debugfs_data {
	int (*func)(struct kbase_jit_debugfs_data *);
	struct mutex lock;
	struct kbase_context *kctx;
	u64 active_value;
	u64 pool_value;
	u64 destroy_value;
	char buffer[50];
};

static int kbase_jit_debugfs_common_open(struct inode *inode,
		struct file *file, int (*func)(struct kbase_jit_debugfs_data *))
{
	struct kbase_jit_debugfs_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->func = func;
	mutex_init(&data->lock);
	data->kctx = (struct kbase_context *) inode->i_private;

	file->private_data = data;

	return nonseekable_open(inode, file);
}

static ssize_t kbase_jit_debugfs_common_read(struct file *file,
		char __user *buf, size_t len, loff_t *ppos)
{
	struct kbase_jit_debugfs_data *data;
	size_t size;
	int ret;

	data = (struct kbase_jit_debugfs_data *) file->private_data;
	mutex_lock(&data->lock);

	if (*ppos) {
		size = strnlen(data->buffer, sizeof(data->buffer));
	} else {
		if (!data->func) {
			ret = -EACCES;
			goto out_unlock;
		}

		if (data->func(data)) {
			ret = -EACCES;
			goto out_unlock;
		}

		size = scnprintf(data->buffer, sizeof(data->buffer),
				"%llu,%llu,%llu", data->active_value,
				data->pool_value, data->destroy_value);
	}

	ret = simple_read_from_buffer(buf, len, ppos, data->buffer, size);

out_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static int kbase_jit_debugfs_common_release(struct inode *inode,
		struct file *file)
{
	kfree(file->private_data);
	return 0;
}

#define KBASE_JIT_DEBUGFS_DECLARE(__fops, __func) \
static int __fops ## _open(struct inode *inode, struct file *file) \
{ \
	return kbase_jit_debugfs_common_open(inode, file, __func); \
} \
static const struct file_operations __fops = { \
	.owner = THIS_MODULE, \
	.open = __fops ## _open, \
	.release = kbase_jit_debugfs_common_release, \
	.read = kbase_jit_debugfs_common_read, \
	.write = NULL, \
	.llseek = generic_file_llseek, \
}

static int kbase_jit_debugfs_count_get(struct kbase_jit_debugfs_data *data)
{
	struct kbase_context *kctx = data->kctx;
	struct list_head *tmp;

	mutex_lock(&kctx->jit_evict_lock);
	list_for_each(tmp, &kctx->jit_active_head) {
		data->active_value++;
	}

	list_for_each(tmp, &kctx->jit_pool_head) {
		data->pool_value++;
	}

	list_for_each(tmp, &kctx->jit_destroy_head) {
		data->destroy_value++;
	}
	mutex_unlock(&kctx->jit_evict_lock);

	return 0;
}
KBASE_JIT_DEBUGFS_DECLARE(kbase_jit_debugfs_count_fops,
		kbase_jit_debugfs_count_get);

static int kbase_jit_debugfs_vm_get(struct kbase_jit_debugfs_data *data)
{
	struct kbase_context *kctx = data->kctx;
	struct kbase_va_region *reg;

	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry(reg, &kctx->jit_active_head, jit_node) {
		data->active_value += reg->nr_pages;
	}

	list_for_each_entry(reg, &kctx->jit_pool_head, jit_node) {
		data->pool_value += reg->nr_pages;
	}

	list_for_each_entry(reg, &kctx->jit_destroy_head, jit_node) {
		data->destroy_value += reg->nr_pages;
	}
	mutex_unlock(&kctx->jit_evict_lock);

	return 0;
}
KBASE_JIT_DEBUGFS_DECLARE(kbase_jit_debugfs_vm_fops,
		kbase_jit_debugfs_vm_get);

static int kbase_jit_debugfs_phys_get(struct kbase_jit_debugfs_data *data)
{
	struct kbase_context *kctx = data->kctx;
	struct kbase_va_region *reg;

	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry(reg, &kctx->jit_active_head, jit_node) {
		data->active_value += reg->gpu_alloc->nents;
	}

	list_for_each_entry(reg, &kctx->jit_pool_head, jit_node) {
		data->pool_value += reg->gpu_alloc->nents;
	}

	list_for_each_entry(reg, &kctx->jit_destroy_head, jit_node) {
		data->destroy_value += reg->gpu_alloc->nents;
	}
	mutex_unlock(&kctx->jit_evict_lock);

	return 0;
}
KBASE_JIT_DEBUGFS_DECLARE(kbase_jit_debugfs_phys_fops,
		kbase_jit_debugfs_phys_get);

#if MALI_JIT_PRESSURE_LIMIT
static int kbase_jit_debugfs_used_get(struct kbase_jit_debugfs_data *data)
{
	struct kbase_context *kctx = data->kctx;
	struct kbase_va_region *reg;

	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry(reg, &kctx->jit_active_head, jit_node) {
		data->active_value += reg->used_pages;
	}
	mutex_unlock(&kctx->jit_evict_lock);
	mutex_unlock(&kctx->jctx.lock);

	return 0;
}

KBASE_JIT_DEBUGFS_DECLARE(kbase_jit_debugfs_used_fops,
		kbase_jit_debugfs_used_get);

static int kbase_mem_jit_trim_pages_from_region(struct kbase_context *kctx,
		struct kbase_va_region *reg, size_t pages_needed,
		size_t *freed, bool shrink);

static int kbase_jit_debugfs_trim_get(struct kbase_jit_debugfs_data *data)
{
	struct kbase_context *kctx = data->kctx;
	struct kbase_va_region *reg;

	mutex_lock(&kctx->jctx.lock);
	kbase_gpu_vm_lock(kctx);
	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry(reg, &kctx->jit_active_head, jit_node) {
		int err;
		size_t freed = 0u;

		err = kbase_mem_jit_trim_pages_from_region(kctx, reg,
				SIZE_MAX, &freed, false);

		if (err) {
			/* Failed to calculate, try the next region */
			continue;
		}

		data->active_value += freed;
	}
	mutex_unlock(&kctx->jit_evict_lock);
	kbase_gpu_vm_unlock(kctx);
	mutex_unlock(&kctx->jctx.lock);

	return 0;
}

KBASE_JIT_DEBUGFS_DECLARE(kbase_jit_debugfs_trim_fops,
		kbase_jit_debugfs_trim_get);
#endif /* MALI_JIT_PRESSURE_LIMIT */

void kbase_jit_debugfs_init(struct kbase_context *kctx)
{
	/* prevent unprivileged use of debug file system
         * in old kernel version
         */
#if (KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE)
	/* only for newer kernel version debug file system is safe */
	const mode_t mode = 0444;
#else
	const mode_t mode = 0400;
#endif

	/* Caller already ensures this, but we keep the pattern for
	 * maintenance safety.
	 */
	if (WARN_ON(!kctx) ||
		WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;



	/* Debugfs entry for getting the number of JIT allocations. */
	debugfs_create_file("mem_jit_count", mode, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_count_fops);

	/*
	 * Debugfs entry for getting the total number of virtual pages
	 * used by JIT allocations.
	 */
	debugfs_create_file("mem_jit_vm", mode, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_vm_fops);

	/*
	 * Debugfs entry for getting the number of physical pages used
	 * by JIT allocations.
	 */
	debugfs_create_file("mem_jit_phys", mode, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_phys_fops);
#if MALI_JIT_PRESSURE_LIMIT
	/*
	 * Debugfs entry for getting the number of pages used
	 * by JIT allocations for estimating the physical pressure
	 * limit.
	 */
	debugfs_create_file("mem_jit_used", mode, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_used_fops);

	/*
	 * Debugfs entry for getting the number of pages that could
	 * be trimmed to free space for more JIT allocations.
	 */
	debugfs_create_file("mem_jit_trim", mode, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_trim_fops);
#endif /* MALI_JIT_PRESSURE_LIMIT */
}
#endif /* CONFIG_DEBUG_FS */

/**
 * kbase_jit_destroy_worker - Deferred worker which frees JIT allocations
 * @work: Work item
 *
 * This function does the work of freeing JIT allocations whose physical
 * backing has been released.
 */
static void kbase_jit_destroy_worker(struct work_struct *work)
{
	struct kbase_context *kctx;
	struct kbase_va_region *reg;

	kctx = container_of(work, struct kbase_context, jit_work);
	do {
		mutex_lock(&kctx->jit_evict_lock);
		if (list_empty(&kctx->jit_destroy_head)) {
			mutex_unlock(&kctx->jit_evict_lock);
			break;
		}

		reg = list_first_entry(&kctx->jit_destroy_head,
				struct kbase_va_region, jit_node);

		list_del(&reg->jit_node);
		mutex_unlock(&kctx->jit_evict_lock);

		kbase_gpu_vm_lock(kctx);
		reg->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_mem_free_region(kctx, reg);
		kbase_gpu_vm_unlock(kctx);
	} while (1);
}

int kbase_jit_init(struct kbase_context *kctx)
{
	mutex_lock(&kctx->jit_evict_lock);
	INIT_LIST_HEAD(&kctx->jit_active_head);
	INIT_LIST_HEAD(&kctx->jit_pool_head);
	INIT_LIST_HEAD(&kctx->jit_destroy_head);
	INIT_WORK(&kctx->jit_work, kbase_jit_destroy_worker);

	INIT_LIST_HEAD(&kctx->jctx.jit_atoms_head);
	INIT_LIST_HEAD(&kctx->jctx.jit_pending_alloc);
	mutex_unlock(&kctx->jit_evict_lock);

	kctx->jit_max_allocations = 0;
	kctx->jit_current_allocations = 0;
	kctx->trim_level = 0;

	return 0;
}

/* Check if the allocation from JIT pool is of the same size as the new JIT
 * allocation and also, if BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP is set, meets
 * the alignment requirements.
 */
static bool meet_size_and_tiler_align_top_requirements(struct kbase_context *kctx,
	struct kbase_va_region *walker, const struct base_jit_alloc_info *info)
{
	bool meet_reqs = true;

	if (walker->nr_pages != info->va_pages)
		meet_reqs = false;
	else if (info->flags & BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP) {
		size_t align = info->extent;
		size_t align_mask = align - 1;

		if ((walker->start_pfn + info->commit_pages) & align_mask)
			meet_reqs = false;
	}

	return meet_reqs;
}

#if MALI_JIT_PRESSURE_LIMIT
/* Function will guarantee *@freed will not exceed @pages_needed
 */
static int kbase_mem_jit_trim_pages_from_region(struct kbase_context *kctx,
		struct kbase_va_region *reg, size_t pages_needed,
		size_t *freed, bool shrink)
{
	int err = 0;
	size_t available_pages = 0u;
	const size_t old_pages = kbase_reg_current_backed_size(reg);
	size_t new_pages = old_pages;
	size_t to_free = 0u;
	size_t max_allowed_pages = old_pages;

	lockdep_assert_held(&kctx->jctx.lock);
	lockdep_assert_held(&kctx->reg_lock);

	/* Is this a JIT allocation that has been reported on? */
	if (reg->used_pages == reg->nr_pages)
		goto out;

	if (!(reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE)) {
		/* For address based memory usage calculation, the GPU
		 * allocates objects of up to size 's', but aligns every object
		 * to alignment 'a', with a < s.
		 *
		 * It also doesn't have to write to all bytes in an object of
		 * size 's'.
		 *
		 * Hence, we can observe the GPU's address for the end of used
		 * memory being up to (s - a) bytes into the first unallocated
		 * page.
		 *
		 * We allow for this and only warn when it exceeds this bound
		 * (rounded up to page sized units). Note, this is allowed to
		 * exceed reg->nr_pages.
		 */
		max_allowed_pages += PFN_UP(
			KBASE_GPU_ALLOCATED_OBJECT_MAX_BYTES -
			KBASE_GPU_ALLOCATED_OBJECT_ALIGN_BYTES);
	} else if (reg->flags & KBASE_REG_TILER_ALIGN_TOP) {
		/* The GPU could report being ready to write to the next
		 * 'extent' sized chunk, but didn't actually write to it, so we
		 * can report up to 'extent' size pages more than the backed
		 * size.
		 *
		 * Note, this is allowed to exceed reg->nr_pages.
		 */
		max_allowed_pages += reg->extent;

		/* Also note that in these GPUs, the GPU may make a large (>1
		 * page) initial allocation but not actually write out to all
		 * of it. Hence it might report that a much higher amount of
		 * memory was used than actually was written to. This does not
		 * result in a real warning because on growing this memory we
		 * round up the size of the allocation up to an 'extent' sized
		 * chunk, hence automatically bringing the backed size up to
		 * the reported size.
		 */
	}

	if (old_pages < reg->used_pages) {
		/* Prevent overflow on available_pages, but only report the
		 * problem if it's in a scenario where used_pages should have
		 * been consistent with the backed size
		 *
		 * Note: In case of a size-based report, this legitimately
		 * happens in common use-cases: we allow for up to this size of
		 * memory being used, but depending on the content it doesn't
		 * have to use all of it.
		 *
		 * Hence, we're much more quiet about that in the size-based
		 * report case - it's not indicating a real problem, it's just
		 * for information
		 */
		if (max_allowed_pages < reg->used_pages) {
			if (!(reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE))
				dev_warn(kctx->kbdev->dev,
						"%s: current backed pages %zu < reported used pages %zu (allowed to be up to %zu) on JIT 0x%llx vapages %zu\n",
						__func__,
						old_pages, reg->used_pages,
						max_allowed_pages,
						reg->start_pfn << PAGE_SHIFT,
						reg->nr_pages);
			else
				dev_dbg(kctx->kbdev->dev,
						"%s: no need to trim, current backed pages %zu < reported used pages %zu on size-report for JIT 0x%llx vapages %zu\n",
						__func__,
						old_pages, reg->used_pages,
						reg->start_pfn << PAGE_SHIFT,
						reg->nr_pages);
			}
		/* In any case, no error condition to report here, caller can
		 * try other regions
		 */

		goto out;
	}
	available_pages = old_pages - reg->used_pages;
	to_free = min(available_pages, pages_needed);

	if (shrink) {
		new_pages -= to_free;

		err = kbase_mem_shrink(kctx, reg, new_pages);
	}
out:
	trace_mali_jit_trim_from_region(reg, to_free, old_pages,
			available_pages, new_pages);
	*freed = to_free;
	return err;
}


/**
 * kbase_mem_jit_trim_pages - Trim JIT regions until sufficient pages have been
 * freed
 * @kctx: Pointer to the kbase context whose active JIT allocations will be
 * checked.
 * @pages_needed: The maximum number of pages to trim.
 *
 * This functions checks all active JIT allocations in @kctx for unused pages
 * at the end, and trim the backed memory regions of those allocations down to
 * the used portion and free the unused pages into the page pool.
 *
 * Specifying @pages_needed allows us to stop early when there's enough
 * physical memory freed to sufficiently bring down the total JIT physical page
 * usage (e.g. to below the pressure limit)
 *
 * Return: Total number of successfully freed pages
 */
static size_t kbase_mem_jit_trim_pages(struct kbase_context *kctx,
		size_t pages_needed)
{
	struct kbase_va_region *reg, *tmp;
	size_t total_freed = 0;

	kbase_gpu_vm_lock(kctx);
	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry_safe(reg, tmp, &kctx->jit_active_head, jit_node) {
		int err;
		size_t freed = 0u;

		err = kbase_mem_jit_trim_pages_from_region(kctx, reg,
				pages_needed, &freed, true);

		if (err) {
			/* Failed to trim, try the next region */
			continue;
		}

		total_freed += freed;
		WARN_ON(freed > pages_needed);
		pages_needed -= freed;
		if (!pages_needed)
			break;
	}
	mutex_unlock(&kctx->jit_evict_lock);
	kbase_gpu_vm_unlock(kctx);

	trace_mali_jit_trim(total_freed);

	return total_freed;
}
#endif /* MALI_JIT_PRESSURE_LIMIT */

static int kbase_jit_grow(struct kbase_context *kctx,
		const struct base_jit_alloc_info *info,
		struct kbase_va_region *reg)
{
	size_t delta;
	size_t pages_required;
	size_t old_size;
	struct kbase_mem_pool *pool;
	int ret = -ENOMEM;
	struct tagged_addr *gpu_pages;
	struct kbase_sub_alloc *prealloc_sas[2] = { NULL, NULL };
	int i;

	if (info->commit_pages > reg->nr_pages) {
		/* Attempted to grow larger than maximum size */
		return -EINVAL;
	}

	kbase_gpu_vm_lock(kctx);

	/* Make the physical backing no longer reclaimable */
	if (!kbase_mem_evictable_unmake(reg->gpu_alloc))
		goto update_failed;

	if (reg->gpu_alloc->nents >= info->commit_pages)
		goto done;

	/* Grow the backing */
	old_size = reg->gpu_alloc->nents;

	/* Allocate some more pages */
	delta = info->commit_pages - reg->gpu_alloc->nents;
	pages_required = delta;

#ifdef CONFIG_MALI_2MB_ALLOC
	/* Preallocate memory for the sub-allocation structs */
	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i) {
		prealloc_sas[i] = kmalloc(sizeof(*prealloc_sas[i]),
				GFP_KERNEL);
		if (!prealloc_sas[i])
			goto update_failed;
	}

	if (pages_required >= (SZ_2M / SZ_4K)) {
		pool = &kctx->mem_pools.large[kctx->jit_group_id];
		/* Round up to number of 2 MB pages required */
		pages_required += ((SZ_2M / SZ_4K) - 1);
		pages_required /= (SZ_2M / SZ_4K);
	} else {
#endif
		pool = &kctx->mem_pools.small[kctx->jit_group_id];
#ifdef CONFIG_MALI_2MB_ALLOC
	}
#endif

	if (reg->cpu_alloc != reg->gpu_alloc)
		pages_required *= 2;

	spin_lock(&kctx->mem_partials_lock);
	kbase_mem_pool_lock(pool);

	/* As we can not allocate memory from the kernel with the vm_lock held,
	 * grow the pool to the required size with the lock dropped. We hold the
	 * pool lock to prevent another thread from allocating from the pool
	 * between the grow and allocation.
	 */
	while (kbase_mem_pool_size(pool) < pages_required) {
		int pool_delta = pages_required - kbase_mem_pool_size(pool);

		kbase_mem_pool_unlock(pool);
		spin_unlock(&kctx->mem_partials_lock);
		kbase_gpu_vm_unlock(kctx);

		if (kbase_mem_pool_grow(pool, pool_delta))
			goto update_failed_unlocked;

		kbase_gpu_vm_lock(kctx);
		spin_lock(&kctx->mem_partials_lock);
		kbase_mem_pool_lock(pool);
	}

	gpu_pages = kbase_alloc_phy_pages_helper_locked(reg->gpu_alloc, pool,
			delta, &prealloc_sas[0]);
	if (!gpu_pages) {
		kbase_mem_pool_unlock(pool);
		spin_unlock(&kctx->mem_partials_lock);
		goto update_failed;
	}

	if (reg->cpu_alloc != reg->gpu_alloc) {
		struct tagged_addr *cpu_pages;

		cpu_pages = kbase_alloc_phy_pages_helper_locked(reg->cpu_alloc,
				pool, delta, &prealloc_sas[1]);
		if (!cpu_pages) {
			kbase_free_phy_pages_helper_locked(reg->gpu_alloc,
					pool, gpu_pages, delta);
			kbase_mem_pool_unlock(pool);
			spin_unlock(&kctx->mem_partials_lock);
			goto update_failed;
		}
	}
	kbase_mem_pool_unlock(pool);
	spin_unlock(&kctx->mem_partials_lock);

	ret = kbase_mem_grow_gpu_mapping(kctx, reg, info->commit_pages,
			old_size);
	/*
	 * The grow failed so put the allocation back in the
	 * pool and return failure.
	 */
	if (ret)
		goto update_failed;

done:
	ret = 0;

	/* Update attributes of JIT allocation taken from the pool */
	reg->initial_commit = info->commit_pages;
	reg->extent = info->extent;

update_failed:
	kbase_gpu_vm_unlock(kctx);
update_failed_unlocked:
	for (i = 0; i != ARRAY_SIZE(prealloc_sas); ++i)
		kfree(prealloc_sas[i]);

	return ret;
}

static void trace_jit_stats(struct kbase_context *kctx,
		u32 bin_id, u32 max_allocations)
{
	const u32 alloc_count =
		kctx->jit_current_allocations_per_bin[bin_id];
	struct kbase_device *kbdev = kctx->kbdev;

	struct kbase_va_region *walker;
	u32 va_pages = 0;
	u32 ph_pages = 0;

	mutex_lock(&kctx->jit_evict_lock);
	list_for_each_entry(walker, &kctx->jit_active_head, jit_node) {
		if (walker->jit_bin_id != bin_id)
			continue;

		va_pages += walker->nr_pages;
		ph_pages += walker->gpu_alloc->nents;
	}
	mutex_unlock(&kctx->jit_evict_lock);

	KBASE_TLSTREAM_AUX_JIT_STATS(kbdev, kctx->id, bin_id,
		max_allocations, alloc_count, va_pages, ph_pages);
}

#if MALI_JIT_PRESSURE_LIMIT
/**
 * get_jit_backed_pressure() - calculate the physical backing of all JIT
 * allocations
 *
 * @kctx: Pointer to the kbase context whose active JIT allocations will be
 * checked
 *
 * Return: number of pages that are committed by JIT allocations
 */
static size_t get_jit_backed_pressure(struct kbase_context *kctx)
{
	size_t backed_pressure = 0;
	int jit_id;

	lockdep_assert_held(&kctx->jctx.lock);

	kbase_gpu_vm_lock(kctx);
	for (jit_id = 0; jit_id <= BASE_JIT_ALLOC_COUNT; jit_id++) {
		struct kbase_va_region *reg = kctx->jit_alloc[jit_id];

		if (reg && (reg != KBASE_RESERVED_REG_JIT_ALLOC)) {
			/* If region has no report, be pessimistic */
			if (reg->used_pages == reg->nr_pages) {
				backed_pressure += reg->nr_pages;
			} else {
				backed_pressure +=
					kbase_reg_current_backed_size(reg);
			}
		}
	}
	kbase_gpu_vm_unlock(kctx);

	return backed_pressure;
}

/**
 * jit_trim_necessary_pages() - calculate and trim the least pages possible to
 * satisfy a new JIT allocation
 *
 * @kctx: Pointer to the kbase context
 * @info: Pointer to JIT allocation information for the new allocation
 *
 * Before allocating a new just-in-time memory region or reusing a previous
 * one, ensure that the total JIT physical page usage also will not exceed the
 * pressure limit.
 *
 * If there are no reported-on allocations, then we already guarantee this will
 * be the case - because our current pressure then only comes from the va_pages
 * of each JIT region, hence JIT physical page usage is guaranteed to be
 * bounded by this.
 *
 * However as soon as JIT allocations become "reported on", the pressure is
 * lowered to allow new JIT regions to be allocated. It is after such a point
 * that the total JIT physical page usage could (either now or in the future on
 * a grow-on-GPU-page-fault) exceed the pressure limit, but only on newly
 * allocated JIT regions. Hence, trim any "reported on" regions.
 *
 * Any pages freed will go into the pool and be allocated from there in
 * kbase_mem_alloc().
 */
static void jit_trim_necessary_pages(struct kbase_context *kctx,
		const struct base_jit_alloc_info *info)
{
	size_t backed_pressure = 0;
	size_t needed_pages = 0;

	backed_pressure = get_jit_backed_pressure(kctx);

	/* It is possible that this is the case - if this is the first
	 * allocation after "ignore_pressure_limit" allocation.
	 */
	if (backed_pressure > kctx->jit_phys_pages_limit) {
		needed_pages +=
			(backed_pressure - kctx->jit_phys_pages_limit)
			+ info->va_pages;
	} else {
		size_t backed_diff =
			kctx->jit_phys_pages_limit - backed_pressure;

		if (info->va_pages > backed_diff)
			needed_pages += info->va_pages - backed_diff;
	}

	if (needed_pages) {
		size_t trimmed_pages = kbase_mem_jit_trim_pages(kctx,
			needed_pages);

		/* This should never happen - we already asserted that
		 * we are not violating JIT pressure limit in earlier
		 * checks, which means that in-flight JIT allocations
		 * must have enough unused pages to satisfy the new
		 * allocation
		 */
		WARN_ON(trimmed_pages < needed_pages);
	}
}
#endif /* MALI_JIT_PRESSURE_LIMIT */

/**
 * jit_allow_allocate() - check whether basic conditions are satisfied to allow
 * a new JIT allocation
 *
 * @kctx: Pointer to the kbase context
 * @info: Pointer to JIT allocation information for the new allocation
 * @ignore_pressure_limit: Flag to indicate whether JIT pressure limit check
 * should be ignored
 *
 * Return: true if allocation can be executed, false otherwise
 */
static bool jit_allow_allocate(struct kbase_context *kctx,
		const struct base_jit_alloc_info *info,
		bool ignore_pressure_limit)
{
	lockdep_assert_held(&kctx->jctx.lock);

#if MALI_JIT_PRESSURE_LIMIT
	if (likely(!ignore_pressure_limit) &&
			((kctx->jit_phys_pages_limit <= kctx->jit_current_phys_pressure) ||
			(info->va_pages > (kctx->jit_phys_pages_limit - kctx->jit_current_phys_pressure)))) {
		dev_dbg(kctx->kbdev->dev,
			"Max JIT page allocations limit reached: active pages %llu, max pages %llu\n",
			kctx->jit_current_phys_pressure + info->va_pages,
			kctx->jit_phys_pages_limit);
		return false;
	}
#endif /* MALI_JIT_PRESSURE_LIMIT */

	if (kctx->jit_current_allocations >= kctx->jit_max_allocations) {
		/* Too many current allocations */
		dev_dbg(kctx->kbdev->dev,
			"Max JIT allocations limit reached: active allocations %d, max allocations %d\n",
			kctx->jit_current_allocations,
			kctx->jit_max_allocations);
		return false;
	}

	if (info->max_allocations > 0 &&
			kctx->jit_current_allocations_per_bin[info->bin_id] >=
			info->max_allocations) {
		/* Too many current allocations in this bin */
		dev_dbg(kctx->kbdev->dev,
			"Per bin limit of max JIT allocations reached: bin_id %d, active allocations %d, max allocations %d\n",
			info->bin_id,
			kctx->jit_current_allocations_per_bin[info->bin_id],
			info->max_allocations);
		return false;
	}

	return true;
}

struct kbase_va_region *kbase_jit_allocate(struct kbase_context *kctx,
		const struct base_jit_alloc_info *info,
		bool ignore_pressure_limit)
{
	struct kbase_va_region *reg = NULL;

	lockdep_assert_held(&kctx->jctx.lock);

	if (!jit_allow_allocate(kctx, info, ignore_pressure_limit))
		return NULL;

#if MALI_JIT_PRESSURE_LIMIT
	if (!ignore_pressure_limit)
		jit_trim_necessary_pages(kctx, info);
#endif /* MALI_JIT_PRESSURE_LIMIT */

	mutex_lock(&kctx->jit_evict_lock);

	/*
	 * Scan the pool for an existing allocation which meets our
	 * requirements and remove it.
	 */
	if (info->usage_id != 0) {
		/* First scan for an allocation with the same usage ID */
		struct kbase_va_region *walker;
		size_t current_diff = SIZE_MAX;

		list_for_each_entry(walker, &kctx->jit_pool_head, jit_node) {

			if (walker->jit_usage_id == info->usage_id &&
					walker->jit_bin_id == info->bin_id &&
					meet_size_and_tiler_align_top_requirements(
							kctx, walker, info)) {
				size_t min_size, max_size, diff;

				/*
				 * The JIT allocations VA requirements have been
				 * met, it's suitable but other allocations
				 * might be a better fit.
				 */
				min_size = min_t(size_t,
						walker->gpu_alloc->nents,
						info->commit_pages);
				max_size = max_t(size_t,
						walker->gpu_alloc->nents,
						info->commit_pages);
				diff = max_size - min_size;

				if (current_diff > diff) {
					current_diff = diff;
					reg = walker;
				}

				/* The allocation is an exact match */
				if (current_diff == 0)
					break;
			}
		}
	}

	if (!reg) {
		/* No allocation with the same usage ID, or usage IDs not in
		 * use. Search for an allocation we can reuse.
		 */
		struct kbase_va_region *walker;
		size_t current_diff = SIZE_MAX;

		list_for_each_entry(walker, &kctx->jit_pool_head, jit_node) {

			if (walker->jit_bin_id == info->bin_id &&
					meet_size_and_tiler_align_top_requirements(
							kctx, walker, info)) {
				size_t min_size, max_size, diff;

				/*
				 * The JIT allocations VA requirements have been
				 * met, it's suitable but other allocations
				 * might be a better fit.
				 */
				min_size = min_t(size_t,
						walker->gpu_alloc->nents,
						info->commit_pages);
				max_size = max_t(size_t,
						walker->gpu_alloc->nents,
						info->commit_pages);
				diff = max_size - min_size;

				if (current_diff > diff) {
					current_diff = diff;
					reg = walker;
				}

				/* The allocation is an exact match, so stop
				 * looking.
				 */
				if (current_diff == 0)
					break;
			}
		}
	}

	if (reg) {
		/*
		 * Remove the found region from the pool and add it to the
		 * active list.
		 */
		list_move(&reg->jit_node, &kctx->jit_active_head);

		/*
		 * Remove the allocation from the eviction list as it's no
		 * longer eligible for eviction. This must be done before
		 * dropping the jit_evict_lock
		 */
		list_del_init(&reg->gpu_alloc->evict_node);
		mutex_unlock(&kctx->jit_evict_lock);

		if (kbase_jit_grow(kctx, info, reg) < 0) {
			/*
			 * An update to an allocation from the pool failed,
			 * chances are slim a new allocation would fair any
			 * better so return the allocation to the pool and
			 * return the function with failure.
			 */
			dev_dbg(kctx->kbdev->dev,
				"JIT allocation resize failed: va_pages 0x%llx, commit_pages 0x%llx\n",
				info->va_pages, info->commit_pages);
			mutex_lock(&kctx->jit_evict_lock);
			list_move(&reg->jit_node, &kctx->jit_pool_head);
			mutex_unlock(&kctx->jit_evict_lock);
			return NULL;
		}
	} else {
		/* No suitable JIT allocation was found so create a new one */
		u64 flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD |
				BASE_MEM_PROT_GPU_WR | BASE_MEM_GROW_ON_GPF |
				BASE_MEM_COHERENT_LOCAL |
				BASEP_MEM_NO_USER_FREE;
		u64 gpu_addr;

		mutex_unlock(&kctx->jit_evict_lock);

		if (info->flags & BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP)
			flags |= BASE_MEM_TILER_ALIGN_TOP;

		flags |= base_mem_group_id_set(kctx->jit_group_id);

		reg = kbase_mem_alloc(kctx, info->va_pages, info->commit_pages,
				info->extent, &flags, &gpu_addr);
		if (!reg) {
			/* Most likely not enough GPU virtual space left for
			 * the new JIT allocation.
			 */
			dev_dbg(kctx->kbdev->dev,
				"Failed to allocate JIT memory: va_pages 0x%llx, commit_pages 0x%llx\n",
				info->va_pages, info->commit_pages);
			return NULL;
		}

		mutex_lock(&kctx->jit_evict_lock);
		list_add(&reg->jit_node, &kctx->jit_active_head);
		mutex_unlock(&kctx->jit_evict_lock);
	}

	trace_mali_jit_alloc(reg, info->id);

	kctx->jit_current_allocations++;
	kctx->jit_current_allocations_per_bin[info->bin_id]++;

	trace_jit_stats(kctx, info->bin_id, info->max_allocations);

	reg->jit_usage_id = info->usage_id;
	reg->jit_bin_id = info->bin_id;
#if MALI_JIT_PRESSURE_LIMIT
	if (info->flags & BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE)
		reg->flags = reg->flags | KBASE_REG_HEAP_INFO_IS_SIZE;
	reg->heap_info_gpu_addr = info->heap_info_gpu_addr;
	kbase_jit_report_update_pressure(kctx, reg, info->va_pages,
			KBASE_JIT_REPORT_ON_ALLOC_OR_FREE);
#endif /* MALI_JIT_PRESSURE_LIMIT */

	return reg;
}

void kbase_jit_free(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	u64 old_pages;

	/* JIT id not immediately available here, so use 0u */
	trace_mali_jit_free(reg, 0u);

	/* Get current size of JIT region */
	old_pages = kbase_reg_current_backed_size(reg);
	if (reg->initial_commit < old_pages) {
		/* Free trim_level % of region, but don't go below initial
		 * commit size
		 */
		u64 new_size = MAX(reg->initial_commit,
			div_u64(old_pages * (100 - kctx->trim_level), 100));
		u64 delta = old_pages - new_size;

		if (delta)
			kbase_mem_shrink(kctx, reg, old_pages - delta);
	}

#if MALI_JIT_PRESSURE_LIMIT
	reg->heap_info_gpu_addr = 0;
	kbase_jit_report_update_pressure(kctx, reg, 0,
			KBASE_JIT_REPORT_ON_ALLOC_OR_FREE);
#endif /* MALI_JIT_PRESSURE_LIMIT */

	kctx->jit_current_allocations--;
	kctx->jit_current_allocations_per_bin[reg->jit_bin_id]--;

	trace_jit_stats(kctx, reg->jit_bin_id, UINT_MAX);

	kbase_mem_evictable_mark_reclaim(reg->gpu_alloc);

	kbase_gpu_vm_lock(kctx);
	reg->flags |= KBASE_REG_DONT_NEED;
	kbase_mem_shrink_cpu_mapping(kctx, reg, 0, reg->gpu_alloc->nents);
	kbase_gpu_vm_unlock(kctx);

	/*
	 * Add the allocation to the eviction list and the jit pool, after this
	 * point the shrink can reclaim it, or it may be reused.
	 */
	mutex_lock(&kctx->jit_evict_lock);

	/* This allocation can't already be on a list. */
	WARN_ON(!list_empty(&reg->gpu_alloc->evict_node));
	list_add(&reg->gpu_alloc->evict_node, &kctx->evict_list);

	list_move(&reg->jit_node, &kctx->jit_pool_head);

	mutex_unlock(&kctx->jit_evict_lock);
}

void kbase_jit_backing_lost(struct kbase_va_region *reg)
{
	struct kbase_context *kctx = kbase_reg_flags_to_kctx(reg);

	if (WARN_ON(!kctx))
		return;

	lockdep_assert_held(&kctx->jit_evict_lock);

	/*
	 * JIT allocations will always be on a list, if the region
	 * is not on a list then it's not a JIT allocation.
	 */
	if (list_empty(&reg->jit_node))
		return;

	/*
	 * Freeing the allocation requires locks we might not be able
	 * to take now, so move the allocation to the free list and kick
	 * the worker which will do the freeing.
	 */
	list_move(&reg->jit_node, &kctx->jit_destroy_head);

	schedule_work(&kctx->jit_work);
}

bool kbase_jit_evict(struct kbase_context *kctx)
{
	struct kbase_va_region *reg = NULL;

	lockdep_assert_held(&kctx->reg_lock);

	/* Free the oldest allocation from the pool */
	mutex_lock(&kctx->jit_evict_lock);
	if (!list_empty(&kctx->jit_pool_head)) {
		reg = list_entry(kctx->jit_pool_head.prev,
				struct kbase_va_region, jit_node);
		list_del(&reg->jit_node);
		list_del_init(&reg->gpu_alloc->evict_node);
	}
	mutex_unlock(&kctx->jit_evict_lock);

	if (reg) {
		reg->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_mem_free_region(kctx, reg);
	}

	return (reg != NULL);
}

void kbase_jit_term(struct kbase_context *kctx)
{
	struct kbase_va_region *walker;

	/* Free all allocations for this context */

	kbase_gpu_vm_lock(kctx);
	mutex_lock(&kctx->jit_evict_lock);
	/* Free all allocations from the pool */
	while (!list_empty(&kctx->jit_pool_head)) {
		walker = list_first_entry(&kctx->jit_pool_head,
				struct kbase_va_region, jit_node);
		list_del(&walker->jit_node);
		list_del_init(&walker->gpu_alloc->evict_node);
		mutex_unlock(&kctx->jit_evict_lock);
		walker->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_mem_free_region(kctx, walker);
		mutex_lock(&kctx->jit_evict_lock);
	}

	/* Free all allocations from active list */
	while (!list_empty(&kctx->jit_active_head)) {
		walker = list_first_entry(&kctx->jit_active_head,
				struct kbase_va_region, jit_node);
		list_del(&walker->jit_node);
		list_del_init(&walker->gpu_alloc->evict_node);
		mutex_unlock(&kctx->jit_evict_lock);
		walker->flags &= ~KBASE_REG_NO_USER_FREE;
		kbase_mem_free_region(kctx, walker);
		mutex_lock(&kctx->jit_evict_lock);
	}
	mutex_unlock(&kctx->jit_evict_lock);
	kbase_gpu_vm_unlock(kctx);

	/*
	 * Flush the freeing of allocations whose backing has been freed
	 * (i.e. everything in jit_destroy_head).
	 */
	cancel_work_sync(&kctx->jit_work);
}

#if MALI_JIT_PRESSURE_LIMIT
void kbase_trace_jit_report_gpu_mem_trace_enabled(struct kbase_context *kctx,
		struct kbase_va_region *reg, unsigned int flags)
{
	/* Offset to the location used for a JIT report within the GPU memory
	 *
	 * This constants only used for this debugging function - not useful
	 * anywhere else in kbase
	 */
	const u64 jit_report_gpu_mem_offset = sizeof(u64)*2;

	u64 addr_start;
	struct kbase_vmap_struct mapping;
	u64 *ptr;

	if (reg->heap_info_gpu_addr == 0ull)
		goto out;

	/* Nothing else to trace in the case the memory just contains the
	 * size. Other tracepoints already record the relevant area of memory.
	 */
	if (reg->flags & KBASE_REG_HEAP_INFO_IS_SIZE)
		goto out;

	addr_start = reg->heap_info_gpu_addr - jit_report_gpu_mem_offset;

	ptr = kbase_vmap(kctx, addr_start, KBASE_JIT_REPORT_GPU_MEM_SIZE,
			&mapping);
	if (!ptr) {
		dev_warn(kctx->kbdev->dev,
				"%s: JIT start=0x%llx unable to map memory near end pointer %llx\n",
				__func__, reg->start_pfn << PAGE_SHIFT,
				addr_start);
		goto out;
	}

	trace_mali_jit_report_gpu_mem(addr_start, reg->start_pfn << PAGE_SHIFT,
				ptr, flags);

	kbase_vunmap(kctx, &mapping);
out:
	return;
}
#endif /* MALI_JIT_PRESSURE_LIMIT */

#if MALI_JIT_PRESSURE_LIMIT
void kbase_jit_report_update_pressure(struct kbase_context *kctx,
		struct kbase_va_region *reg, u64 new_used_pages,
		unsigned int flags)
{
	u64 diff;

	lockdep_assert_held(&kctx->jctx.lock);

	trace_mali_jit_report_pressure(reg, new_used_pages,
		kctx->jit_current_phys_pressure + new_used_pages -
			reg->used_pages,
		flags);

	if (WARN_ON(new_used_pages > reg->nr_pages))
		return;

	if (reg->used_pages > new_used_pages) {
		/* We reduced the number of used pages */
		diff = reg->used_pages - new_used_pages;

		if (!WARN_ON(diff > kctx->jit_current_phys_pressure))
			kctx->jit_current_phys_pressure -= diff;

		reg->used_pages = new_used_pages;
	} else {
		/* We increased the number of used pages */
		diff = new_used_pages - reg->used_pages;

		if (!WARN_ON(diff > U64_MAX - kctx->jit_current_phys_pressure))
			kctx->jit_current_phys_pressure += diff;

		reg->used_pages = new_used_pages;
	}

}
#endif /* MALI_JIT_PRESSURE_LIMIT */

bool kbase_has_exec_va_zone(struct kbase_context *kctx)
{
	bool has_exec_va_zone;

	kbase_gpu_vm_lock(kctx);
	has_exec_va_zone = (kctx->exec_va_start != U64_MAX);
	kbase_gpu_vm_unlock(kctx);

	return has_exec_va_zone;
}


int kbase_jd_user_buf_pin_pages(struct kbase_context *kctx,
		struct kbase_va_region *reg)
{
	struct kbase_mem_phy_alloc *alloc = reg->gpu_alloc;
	struct page **pages = alloc->imported.user_buf.pages;
	unsigned long address = alloc->imported.user_buf.address;
	struct mm_struct *mm = alloc->imported.user_buf.mm;
	long pinned_pages;
	long i;

	if (WARN_ON(alloc->type != KBASE_MEM_TYPE_IMPORTED_USER_BUF))
		return -EINVAL;

	if (alloc->nents) {
		if (WARN_ON(alloc->nents != alloc->imported.user_buf.nr_pages))
			return -EINVAL;
		else
			return 0;
	}

	if (WARN_ON(reg->gpu_alloc->imported.user_buf.mm != current->mm))
		return -EINVAL;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	pinned_pages = get_user_pages(NULL, mm,
			address,
			alloc->imported.user_buf.nr_pages,
#if KERNEL_VERSION(4, 4, 168) <= LINUX_VERSION_CODE && \
KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE
			reg->flags & KBASE_REG_GPU_WR ? FOLL_WRITE : 0,
			pages, NULL);
#else
			reg->flags & KBASE_REG_GPU_WR,
			0, pages, NULL);
#endif
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
	pinned_pages = get_user_pages_remote(NULL, mm,
			address,
			alloc->imported.user_buf.nr_pages,
			reg->flags & KBASE_REG_GPU_WR,
			0, pages, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	pinned_pages = get_user_pages_remote(NULL, mm,
			address,
			alloc->imported.user_buf.nr_pages,
			reg->flags & KBASE_REG_GPU_WR ? FOLL_WRITE : 0,
			pages, NULL);
#else
	pinned_pages = get_user_pages_remote(NULL, mm,
			address,
			alloc->imported.user_buf.nr_pages,
			reg->flags & KBASE_REG_GPU_WR ? FOLL_WRITE : 0,
			pages, NULL, NULL);
#endif

	if (pinned_pages <= 0)
		return pinned_pages;

	if (pinned_pages != alloc->imported.user_buf.nr_pages) {
		for (i = 0; i < pinned_pages; i++)
			put_page(pages[i]);
		return -ENOMEM;
	}

	alloc->nents = pinned_pages;

	return 0;
}

static int kbase_jd_user_buf_map(struct kbase_context *kctx,
		struct kbase_va_region *reg)
{
	long pinned_pages;
	struct kbase_mem_phy_alloc *alloc;
	struct page **pages;
	struct tagged_addr *pa;
	long i;
	unsigned long address;
	struct device *dev;
	unsigned long offset;
	unsigned long local_size;
	unsigned long gwt_mask = ~0;
	int err = kbase_jd_user_buf_pin_pages(kctx, reg);

	if (err)
		return err;

	alloc = reg->gpu_alloc;
	pa = kbase_get_gpu_phy_pages(reg);
	address = alloc->imported.user_buf.address;
	pinned_pages = alloc->nents;
	pages = alloc->imported.user_buf.pages;
	dev = kctx->kbdev->dev;
	offset = address & ~PAGE_MASK;
	local_size = alloc->imported.user_buf.size;

	for (i = 0; i < pinned_pages; i++) {
		dma_addr_t dma_addr;
		unsigned long min;

		min = MIN(PAGE_SIZE - offset, local_size);
		dma_addr = dma_map_page(dev, pages[i],
				offset, min,
				DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, dma_addr))
			goto unwind;

		alloc->imported.user_buf.dma_addrs[i] = dma_addr;
		pa[i] = as_tagged(page_to_phys(pages[i]));

		local_size -= min;
		offset = 0;
	}

#ifdef CONFIG_MALI_CINSTR_GWT
	if (kctx->gwt_enabled)
		gwt_mask = ~KBASE_REG_GPU_WR;
#endif

	err = kbase_mmu_insert_pages(kctx->kbdev, &kctx->mmu, reg->start_pfn,
			pa, kbase_reg_current_backed_size(reg),
			reg->flags & gwt_mask, kctx->as_nr,
			alloc->group_id);
	if (err == 0)
		return 0;

	/* fall down */
unwind:
	alloc->nents = 0;
	while (i--) {
		dma_unmap_page(kctx->kbdev->dev,
				alloc->imported.user_buf.dma_addrs[i],
				PAGE_SIZE, DMA_BIDIRECTIONAL);
	}

	while (++i < pinned_pages) {
		put_page(pages[i]);
		pages[i] = NULL;
	}

	return err;
}

/* This function would also perform the work of unpinning pages on Job Manager
 * GPUs, which implies that a call to kbase_jd_user_buf_pin_pages() will NOT
 * have a corresponding call to kbase_jd_user_buf_unpin_pages().
 */
static void kbase_jd_user_buf_unmap(struct kbase_context *kctx,
		struct kbase_mem_phy_alloc *alloc, bool writeable)
{
	long i;
	struct page **pages;
	unsigned long size = alloc->imported.user_buf.size;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_IMPORTED_USER_BUF);
	pages = alloc->imported.user_buf.pages;
	for (i = 0; i < alloc->imported.user_buf.nr_pages; i++) {
		unsigned long local_size;
		dma_addr_t dma_addr = alloc->imported.user_buf.dma_addrs[i];

		local_size = MIN(size, PAGE_SIZE - (dma_addr & ~PAGE_MASK));
		dma_unmap_page(kctx->kbdev->dev, dma_addr, local_size,
				DMA_BIDIRECTIONAL);
		if (writeable)
			set_page_dirty_lock(pages[i]);
		put_page(pages[i]);
		pages[i] = NULL;

		size -= local_size;
	}
	alloc->nents = 0;
}

int kbase_mem_copy_to_pinned_user_pages(struct page **dest_pages,
		void *src_page, size_t *to_copy, unsigned int nr_pages,
		unsigned int *target_page_nr, size_t offset)
{
	void *target_page = kmap(dest_pages[*target_page_nr]);
	size_t chunk = PAGE_SIZE-offset;

	if (!target_page) {
		pr_err("%s: kmap failure", __func__);
		return -ENOMEM;
	}

	chunk = min(chunk, *to_copy);

	memcpy(target_page + offset, src_page, chunk);
	*to_copy -= chunk;

	kunmap(dest_pages[*target_page_nr]);

	*target_page_nr += 1;
	if (*target_page_nr >= nr_pages || *to_copy == 0)
		return 0;

	target_page = kmap(dest_pages[*target_page_nr]);
	if (!target_page) {
		pr_err("%s: kmap failure", __func__);
		return -ENOMEM;
	}

	KBASE_DEBUG_ASSERT(target_page);

	chunk = min(offset, *to_copy);
	memcpy(target_page, src_page + PAGE_SIZE-offset, chunk);
	*to_copy -= chunk;

	kunmap(dest_pages[*target_page_nr]);

	return 0;
}

struct kbase_mem_phy_alloc *kbase_map_external_resource(
		struct kbase_context *kctx, struct kbase_va_region *reg,
		struct mm_struct *locked_mm)
{
	int err;

	lockdep_assert_held(&kctx->reg_lock);

	/* decide what needs to happen for this resource */
	switch (reg->gpu_alloc->type) {
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
		if ((reg->gpu_alloc->imported.user_buf.mm != locked_mm) &&
		    (!reg->gpu_alloc->nents))
			goto exit;

		reg->gpu_alloc->imported.user_buf.current_mapping_usage_count++;
		if (1 == reg->gpu_alloc->imported.user_buf.current_mapping_usage_count) {
			err = kbase_jd_user_buf_map(kctx, reg);
			if (err) {
				reg->gpu_alloc->imported.user_buf.current_mapping_usage_count--;
				goto exit;
			}
		}
	}
	break;
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		err = kbase_mem_umm_map(kctx, reg);
		if (err)
			goto exit;
		break;
	}
	default:
		goto exit;
	}

	return kbase_mem_phy_alloc_get(reg->gpu_alloc);
exit:
	return NULL;
}

void kbase_unmap_external_resource(struct kbase_context *kctx,
		struct kbase_va_region *reg, struct kbase_mem_phy_alloc *alloc)
{
	switch (alloc->type) {
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		kbase_mem_umm_unmap(kctx, reg, alloc);
	}
	break;
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
		alloc->imported.user_buf.current_mapping_usage_count--;

		if (0 == alloc->imported.user_buf.current_mapping_usage_count) {
			bool writeable = true;

			if (!kbase_is_region_invalid_or_free(reg) &&
					reg->gpu_alloc == alloc)
				kbase_mmu_teardown_pages(
						kctx->kbdev,
						&kctx->mmu,
						reg->start_pfn,
						kbase_reg_current_backed_size(reg),
						kctx->as_nr);

			if (reg && ((reg->flags & KBASE_REG_GPU_WR) == 0))
				writeable = false;

			kbase_jd_user_buf_unmap(kctx, alloc, writeable);
		}
	}
	break;
	default:
	break;
	}
	kbase_mem_phy_alloc_put(alloc);
}

struct kbase_ctx_ext_res_meta *kbase_sticky_resource_acquire(
		struct kbase_context *kctx, u64 gpu_addr)
{
	struct kbase_ctx_ext_res_meta *meta = NULL;
	struct kbase_ctx_ext_res_meta *walker;

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * Walk the per context external resource metadata list for the
	 * metadata which matches the region which is being acquired.
	 */
	list_for_each_entry(walker, &kctx->ext_res_meta_head, ext_res_node) {
		if (walker->gpu_addr == gpu_addr) {
			meta = walker;
			meta->ref++;
			break;
		}
	}

	/* No metadata exists so create one. */
	if (!meta) {
		struct kbase_va_region *reg;

		/* Find the region */
		reg = kbase_region_tracker_find_region_enclosing_address(
				kctx, gpu_addr);
		if (kbase_is_region_invalid_or_free(reg))
			goto failed;

		/* Allocate the metadata object */
		meta = kzalloc(sizeof(*meta), GFP_KERNEL);
		if (!meta)
			goto failed;

		/*
		 * Fill in the metadata object and acquire a reference
		 * for the physical resource.
		 */
		meta->alloc = kbase_map_external_resource(kctx, reg, NULL);
		meta->ref = 1;

		if (!meta->alloc)
			goto fail_map;

		meta->gpu_addr = reg->start_pfn << PAGE_SHIFT;

		list_add(&meta->ext_res_node, &kctx->ext_res_meta_head);
	}

	return meta;

fail_map:
	kfree(meta);
failed:
	return NULL;
}

static struct kbase_ctx_ext_res_meta *
find_sticky_resource_meta(struct kbase_context *kctx, u64 gpu_addr)
{
	struct kbase_ctx_ext_res_meta *walker;

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * Walk the per context external resource metadata list for the
	 * metadata which matches the region which is being released.
	 */
	list_for_each_entry(walker, &kctx->ext_res_meta_head, ext_res_node)
		if (walker->gpu_addr == gpu_addr)
			return walker;

	return NULL;
}

static void release_sticky_resource_meta(struct kbase_context *kctx,
		struct kbase_ctx_ext_res_meta *meta)
{
	struct kbase_va_region *reg;

	/* Drop the physical memory reference and free the metadata. */
	reg = kbase_region_tracker_find_region_enclosing_address(
			kctx,
			meta->gpu_addr);

	kbase_unmap_external_resource(kctx, reg, meta->alloc);
	list_del(&meta->ext_res_node);
	kfree(meta);
}

bool kbase_sticky_resource_release(struct kbase_context *kctx,
		struct kbase_ctx_ext_res_meta *meta, u64 gpu_addr)
{
	lockdep_assert_held(&kctx->reg_lock);

	/* Search of the metadata if one isn't provided. */
	if (!meta)
		meta = find_sticky_resource_meta(kctx, gpu_addr);

	/* No metadata so just return. */
	if (!meta)
		return false;

	if (--meta->ref != 0)
		return true;

	release_sticky_resource_meta(kctx, meta);

	return true;
}

bool kbase_sticky_resource_release_force(struct kbase_context *kctx,
		struct kbase_ctx_ext_res_meta *meta, u64 gpu_addr)
{
	lockdep_assert_held(&kctx->reg_lock);

	/* Search of the metadata if one isn't provided. */
	if (!meta)
		meta = find_sticky_resource_meta(kctx, gpu_addr);

	/* No metadata so just return. */
	if (!meta)
		return false;

	release_sticky_resource_meta(kctx, meta);

	return true;
}

int kbase_sticky_resource_init(struct kbase_context *kctx)
{
	INIT_LIST_HEAD(&kctx->ext_res_meta_head);

	return 0;
}

void kbase_sticky_resource_term(struct kbase_context *kctx)
{
	struct kbase_ctx_ext_res_meta *walker;

	lockdep_assert_held(&kctx->reg_lock);

	/*
	 * Free any sticky resources which haven't been unmapped.
	 *
	 * Note:
	 * We don't care about refcounts at this point as no future
	 * references to the meta data will be made.
	 * Region termination would find these if we didn't free them
	 * here, but it's more efficient if we do the clean up here.
	 */
	while (!list_empty(&kctx->ext_res_meta_head)) {
		walker = list_first_entry(&kctx->ext_res_meta_head,
				struct kbase_ctx_ext_res_meta, ext_res_node);

		kbase_sticky_resource_release_force(kctx, walker, 0);
	}
}
