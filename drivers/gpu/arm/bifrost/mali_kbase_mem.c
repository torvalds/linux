/*
 *
 * (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved.
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
 * @file mali_kbase_mem.c
 * Base kernel memory APIs
 */
#ifdef CONFIG_DMA_SHARED_BUFFER
#include <linux/dma-buf.h>
#endif				/* CONFIG_DMA_SHARED_BUFFER */
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/version.h>
#include <linux/log2.h>

#include <mali_kbase_config.h>
#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_cache_policy.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_tlstream.h>

/* This function finds out which RB tree the given GPU VA region belongs to
 * based on the region zone */
static struct rb_root *kbase_reg_flags_to_rbtree(struct kbase_context *kctx,
						    struct kbase_va_region *reg)
{
	struct rb_root *rbtree = NULL;

	switch (reg->flags & KBASE_REG_ZONE_MASK) {
	case KBASE_REG_ZONE_CUSTOM_VA:
		rbtree = &kctx->reg_rbtree_custom;
		break;
	case KBASE_REG_ZONE_EXEC:
		rbtree = &kctx->reg_rbtree_exec;
		break;
	case KBASE_REG_ZONE_SAME_VA:
		rbtree = &kctx->reg_rbtree_same;
		/* fall through */
	default:
		rbtree = &kctx->reg_rbtree_same;
		break;
	}

	return rbtree;
}

/* This function finds out which RB tree the given pfn from the GPU VA belongs
 * to based on the memory zone the pfn refers to */
static struct rb_root *kbase_gpu_va_to_rbtree(struct kbase_context *kctx,
								    u64 gpu_pfn)
{
	struct rb_root *rbtree = NULL;

#ifdef CONFIG_64BIT
	if (kbase_ctx_flag(kctx, KCTX_COMPAT)) {
#endif /* CONFIG_64BIT */
		if (gpu_pfn >= KBASE_REG_ZONE_CUSTOM_VA_BASE)
			rbtree = &kctx->reg_rbtree_custom;
		else if (gpu_pfn >= KBASE_REG_ZONE_EXEC_BASE)
			rbtree = &kctx->reg_rbtree_exec;
		else
			rbtree = &kctx->reg_rbtree_same;
#ifdef CONFIG_64BIT
	} else {
		if (gpu_pfn >= kctx->same_va_end)
			rbtree = &kctx->reg_rbtree_custom;
		else
			rbtree = &kctx->reg_rbtree_same;
	}
#endif /* CONFIG_64BIT */

	return rbtree;
}

/* This function inserts a region into the tree. */
static void kbase_region_tracker_insert(struct kbase_context *kctx,
						struct kbase_va_region *new_reg)
{
	u64 start_pfn = new_reg->start_pfn;
	struct rb_node **link = NULL;
	struct rb_node *parent = NULL;
	struct rb_root *rbtree = NULL;

	rbtree = kbase_reg_flags_to_rbtree(kctx, new_reg);

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

/* Find allocated region enclosing free range. */
static struct kbase_va_region *kbase_region_tracker_find_region_enclosing_range_free(
		struct kbase_context *kctx, u64 start_pfn, size_t nr_pages)
{
	struct rb_node *rbnode = NULL;
	struct kbase_va_region *reg = NULL;
	struct rb_root *rbtree = NULL;

	u64 end_pfn = start_pfn + nr_pages;

	rbtree = kbase_gpu_va_to_rbtree(kctx, start_pfn);

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

/* Find region enclosing given address. */
struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(struct kbase_context *kctx, u64 gpu_addr)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_root *rbtree = NULL;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	rbtree = kbase_gpu_va_to_rbtree(kctx, gpu_pfn);

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

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_enclosing_address);

/* Find region with given base address */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(struct kbase_context *kctx, u64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_node *rbnode = NULL;
	struct kbase_va_region *reg = NULL;
	struct rb_root *rbtree = NULL;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	rbtree = kbase_gpu_va_to_rbtree(kctx, gpu_pfn);

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

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_base_address);

/* Find region meeting given requirements */
static struct kbase_va_region *kbase_region_tracker_find_region_meeting_reqs(
		struct kbase_context *kctx, struct kbase_va_region *reg_reqs,
		size_t nr_pages, size_t align_offset, size_t align_mask,
		u64 *out_start_pfn)
{
	struct rb_node *rbnode = NULL;
	struct kbase_va_region *reg = NULL;
	struct rb_root *rbtree = NULL;

	/* Note that this search is a linear search, as we do not have a target
	   address in mind, so does not benefit from the rbtree search */
	rbtree = kbase_reg_flags_to_rbtree(kctx, reg_reqs);

	rbnode = rb_first(rbtree);

	while (rbnode) {
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

			if ((start_pfn >= reg->start_pfn) &&
					(start_pfn <= (reg->start_pfn + reg->nr_pages - 1)) &&
					((start_pfn + nr_pages - 1) <= (reg->start_pfn + reg->nr_pages - 1))) {
				*out_start_pfn = start_pfn;
				return reg;
			}
		}
		rbnode = rb_next(rbnode);
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
static int kbase_remove_va_region(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	struct rb_node *rbprev;
	struct kbase_va_region *prev = NULL;
	struct rb_node *rbnext;
	struct kbase_va_region *next = NULL;
	struct rb_root *reg_rbtree = NULL;

	int merged_front = 0;
	int merged_back = 0;
	int err = 0;

	reg_rbtree = kbase_reg_flags_to_rbtree(kctx, reg);

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
				kbase_free_alloced_region(reg);
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

		free_reg = kbase_alloc_free_region(kctx, reg->start_pfn, reg->nr_pages, reg->flags & KBASE_REG_ZONE_MASK);
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
 * @brief Insert a VA region to the list, replacing the current at_reg.
 */
static int kbase_insert_va_region_nolock(struct kbase_context *kctx, struct kbase_va_region *new_reg, struct kbase_va_region *at_reg, u64 start_pfn, size_t nr_pages)
{
	struct rb_root *reg_rbtree = NULL;
	int err = 0;

	reg_rbtree = kbase_reg_flags_to_rbtree(kctx, at_reg);

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
		kbase_free_alloced_region(at_reg);
	}
	/* New region replaces the start of the old one, so insert before. */
	else if (at_reg->start_pfn == start_pfn) {
		at_reg->start_pfn += nr_pages;
		KBASE_DEBUG_ASSERT(at_reg->nr_pages >= nr_pages);
		at_reg->nr_pages -= nr_pages;

		kbase_region_tracker_insert(kctx, new_reg);
	}
	/* New region replaces the end of the old one, so insert after. */
	else if ((at_reg->start_pfn + at_reg->nr_pages) == (start_pfn + nr_pages)) {
		at_reg->nr_pages -= nr_pages;

		kbase_region_tracker_insert(kctx, new_reg);
	}
	/* New region splits the old one, so insert and create new */
	else {
		struct kbase_va_region *new_front_reg;

		new_front_reg = kbase_alloc_free_region(kctx,
				at_reg->start_pfn,
				start_pfn - at_reg->start_pfn,
				at_reg->flags & KBASE_REG_ZONE_MASK);

		if (new_front_reg) {
			at_reg->nr_pages -= nr_pages + new_front_reg->nr_pages;
			at_reg->start_pfn = start_pfn + nr_pages;

			kbase_region_tracker_insert(kctx, new_front_reg);
			kbase_region_tracker_insert(kctx, new_reg);
		} else {
			err = -ENOMEM;
		}
	}

	return err;
}

/**
 * @brief Add a VA region to the list.
 */
int kbase_add_va_region(struct kbase_context *kctx,
		struct kbase_va_region *reg, u64 addr,
		size_t nr_pages, size_t align)
{
	struct kbase_va_region *tmp;
	u64 gpu_pfn = addr >> PAGE_SHIFT;
	int err = 0;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	lockdep_assert_held(&kctx->reg_lock);

	if (!align)
		align = 1;

	/* must be a power of 2 */
	KBASE_DEBUG_ASSERT(is_power_of_2(align));
	KBASE_DEBUG_ASSERT(nr_pages > 0);

	/* Path 1: Map a specific address. Find the enclosing region, which *must* be free. */
	if (gpu_pfn) {
		struct device *dev = kctx->kbdev->dev;

		KBASE_DEBUG_ASSERT(!(gpu_pfn & (align - 1)));

		tmp = kbase_region_tracker_find_region_enclosing_range_free(kctx, gpu_pfn, nr_pages);
		if (!tmp) {
			dev_warn(dev, "Enclosing region not found: 0x%08llx gpu_pfn, %zu nr_pages", gpu_pfn, nr_pages);
			err = -ENOMEM;
			goto exit;
		}
		if (!(tmp->flags & KBASE_REG_FREE)) {
			dev_warn(dev, "Zone mismatch: %lu != %lu", tmp->flags & KBASE_REG_ZONE_MASK, reg->flags & KBASE_REG_ZONE_MASK);
			dev_warn(dev, "!(tmp->flags & KBASE_REG_FREE): tmp->start_pfn=0x%llx tmp->flags=0x%lx tmp->nr_pages=0x%zx gpu_pfn=0x%llx nr_pages=0x%zx\n", tmp->start_pfn, tmp->flags, tmp->nr_pages, gpu_pfn, nr_pages);
			dev_warn(dev, "in function %s (%p, %p, 0x%llx, 0x%zx, 0x%zx)\n", __func__, kctx, reg, addr, nr_pages, align);
			err = -ENOMEM;
			goto exit;
		}

		err = kbase_insert_va_region_nolock(kctx, reg, tmp, gpu_pfn, nr_pages);
		if (err) {
			dev_warn(dev, "Failed to insert va region");
			err = -ENOMEM;
			goto exit;
		}

		goto exit;
	}

	/* Path 2: Map any free address which meets the requirements.
	 *
	 * Depending on the zone the allocation request is for
	 * we might need to retry it. */
	do {
		u64 start_pfn;
		size_t align_offset = align;
		size_t align_mask = align - 1;

		if ((reg->flags & KBASE_REG_TILER_ALIGN_TOP)) {
			WARN(align > 1,
					"kbase_add_va_region with align %lx might not be honored for KBASE_REG_TILER_ALIGN_TOP memory",
					(unsigned long)align);
			align_mask  = reg->extent - 1;
			align_offset = reg->extent - reg->initial_commit;
		}

		tmp = kbase_region_tracker_find_region_meeting_reqs(kctx, reg,
				nr_pages, align_offset, align_mask,
				&start_pfn);
		if (tmp) {
			err = kbase_insert_va_region_nolock(kctx, reg, tmp,
					start_pfn, nr_pages);
			break;
		}

		/*
		 * If the allocation is not from the same zone as JIT
		 * then don't retry, we're out of VA and there is
		 * nothing which can be done about it.
		 */
		if ((reg->flags & KBASE_REG_ZONE_MASK) !=
				KBASE_REG_ZONE_CUSTOM_VA)
			break;
	} while (kbase_jit_evict(kctx));

	if (!tmp)
		err = -ENOMEM;

 exit:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_add_va_region);

/**
 * @brief Initialize the internal region tracker data structure.
 */
static void kbase_region_tracker_ds_init(struct kbase_context *kctx,
		struct kbase_va_region *same_va_reg,
		struct kbase_va_region *exec_reg,
		struct kbase_va_region *custom_va_reg)
{
	kctx->reg_rbtree_same = RB_ROOT;
	kbase_region_tracker_insert(kctx, same_va_reg);

	/* Although exec and custom_va_reg don't always exist,
	 * initialize unconditionally because of the mem_view debugfs
	 * implementation which relies on these being empty
	 */
	kctx->reg_rbtree_exec = RB_ROOT;
	kctx->reg_rbtree_custom = RB_ROOT;

	if (exec_reg)
		kbase_region_tracker_insert(kctx, exec_reg);
	if (custom_va_reg)
		kbase_region_tracker_insert(kctx, custom_va_reg);
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
			kbase_free_alloced_region(reg);
		}
	} while (rbnode);
}

void kbase_region_tracker_term(struct kbase_context *kctx)
{
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_same);
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_exec);
	kbase_region_tracker_erase_rbtree(&kctx->reg_rbtree_custom);
}

static size_t kbase_get_same_va_bits(struct kbase_context *kctx)
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

	return min(cpu_va_bits, (size_t) kctx->kbdev->gpu_props.mmu.va_bits);
}

/**
 * Initialize the region tracker data structure.
 */
int kbase_region_tracker_init(struct kbase_context *kctx)
{
	struct kbase_va_region *same_va_reg;
	struct kbase_va_region *exec_reg = NULL;
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
	same_va_reg = kbase_alloc_free_region(kctx, 1,
			same_va_pages,
			KBASE_REG_ZONE_SAME_VA);

	if (!same_va_reg) {
		err = -ENOMEM;
		goto fail_unlock;
	}

#ifdef CONFIG_64BIT
	/* 32-bit clients have exec and custom VA zones */
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

		exec_reg = kbase_alloc_free_region(kctx,
				KBASE_REG_ZONE_EXEC_BASE,
				KBASE_REG_ZONE_EXEC_SIZE,
				KBASE_REG_ZONE_EXEC);

		if (!exec_reg) {
			err = -ENOMEM;
			goto fail_free_same_va;
		}

		custom_va_reg = kbase_alloc_free_region(kctx,
				KBASE_REG_ZONE_CUSTOM_VA_BASE,
				custom_va_size, KBASE_REG_ZONE_CUSTOM_VA);

		if (!custom_va_reg) {
			err = -ENOMEM;
			goto fail_free_exec;
		}
#ifdef CONFIG_64BIT
	}
#endif

	kbase_region_tracker_ds_init(kctx, same_va_reg, exec_reg,
					custom_va_reg);

	kctx->same_va_end = same_va_pages + 1;

	kbase_gpu_vm_unlock(kctx);
	return 0;

fail_free_exec:
	kbase_free_alloced_region(exec_reg);
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
	u64 same_va_bits = kbase_get_same_va_bits(kctx);
	u64 total_va_size;
	int err;

	total_va_size = (1ULL << (same_va_bits - PAGE_SHIFT)) - 1;

	kbase_gpu_vm_lock(kctx);

	/*
	 * Modify the same VA free region after creation. Be careful to ensure
	 * that allocations haven't been made as they could cause an overlap
	 * to happen with existing same VA allocations and the custom VA zone.
	 */
	same_va = kbase_region_tracker_find_region_base_address(kctx,
			PAGE_SIZE);
	if (!same_va) {
		err = -ENOMEM;
		goto fail_unlock;
	}

	/* The region flag or region size has changed since creation so bail. */
	if ((!(same_va->flags & KBASE_REG_FREE)) ||
			(same_va->nr_pages != total_va_size)) {
		err = -ENOMEM;
		goto fail_unlock;
	}

	if (same_va->nr_pages < jit_va_pages ||
			kctx->same_va_end < jit_va_pages) {
		err = -ENOMEM;
		goto fail_unlock;
	}

	/* It's safe to adjust the same VA zone now */
	same_va->nr_pages -= jit_va_pages;
	kctx->same_va_end -= jit_va_pages;

	/*
	 * Create a custom VA zone at the end of the VA for allocations which
	 * JIT can use so it doesn't have to allocate VA from the kernel.
	 */
	custom_va_reg = kbase_alloc_free_region(kctx,
				kctx->same_va_end,
				jit_va_pages,
				KBASE_REG_ZONE_CUSTOM_VA);

	if (!custom_va_reg) {
		/*
		 * The context will be destroyed if we fail here so no point
		 * reverting the change we made to same_va.
		 */
		err = -ENOMEM;
		goto fail_unlock;
	}

	kbase_region_tracker_insert(kctx, custom_va_reg);

	kbase_gpu_vm_unlock(kctx);
	return 0;

fail_unlock:
	kbase_gpu_vm_unlock(kctx);
	return err;
}
#endif

int kbase_region_tracker_init_jit(struct kbase_context *kctx, u64 jit_va_pages,
		u8 max_allocations, u8 trim_level)
{
	if (trim_level > 100)
		return -EINVAL;

	kctx->jit_max_allocations = max_allocations;
	kctx->trim_level = trim_level;

#ifdef CONFIG_64BIT
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT))
		return kbase_region_tracker_init_jit_64(kctx, jit_va_pages);
#endif
	/*
	 * Nothing to do for 32-bit clients, JIT uses the existing
	 * custom VA zone.
	 */
	return 0;
}

int kbase_mem_init(struct kbase_device *kbdev)
{
	struct kbasep_mem_device *memdev;
	int ret;

	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;
	kbdev->mem_pool_max_size_default = KBASE_MEM_POOL_MAX_SIZE_KCTX;

	/* Initialize memory usage */
	atomic_set(&memdev->used_pages, 0);

	ret = kbase_mem_pool_init(&kbdev->mem_pool,
			KBASE_MEM_POOL_MAX_SIZE_KBDEV,
			KBASE_MEM_POOL_4KB_PAGE_TABLE_ORDER,
			kbdev,
			NULL);
	if (ret)
		return ret;

	ret = kbase_mem_pool_init(&kbdev->lp_mem_pool,
			(KBASE_MEM_POOL_MAX_SIZE_KBDEV >> 9),
			KBASE_MEM_POOL_2MB_PAGE_TABLE_ORDER,
			kbdev,
			NULL);
	if (ret)
		kbase_mem_pool_term(&kbdev->mem_pool);

	return ret;
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

	kbase_mem_pool_term(&kbdev->mem_pool);
	kbase_mem_pool_term(&kbdev->lp_mem_pool);
}

KBASE_EXPORT_TEST_API(kbase_mem_term);




/**
 * @brief Allocate a free region object.
 *
 * The allocated object is not part of any list yet, and is flagged as
 * KBASE_REG_FREE. No mapping is allocated yet.
 *
 * zone is KBASE_REG_ZONE_CUSTOM_VA, KBASE_REG_ZONE_SAME_VA,
 * or KBASE_REG_ZONE_EXEC
 *
 */
struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone)
{
	struct kbase_va_region *new_reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* zone argument should only contain zone related region flags */
	KBASE_DEBUG_ASSERT((zone & ~KBASE_REG_ZONE_MASK) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);
	/* 64-bit address range is the max */
	KBASE_DEBUG_ASSERT(start_pfn + nr_pages <= (U64_MAX / PAGE_SIZE));

	new_reg = kzalloc(sizeof(*new_reg), GFP_KERNEL);

	if (!new_reg)
		return NULL;

	new_reg->cpu_alloc = NULL; /* no alloc bound yet */
	new_reg->gpu_alloc = NULL; /* no alloc bound yet */
	new_reg->kctx = kctx;
	new_reg->flags = zone | KBASE_REG_FREE;

	new_reg->flags |= KBASE_REG_GROWABLE;

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;

	INIT_LIST_HEAD(&new_reg->jit_node);

	return new_reg;
}

KBASE_EXPORT_TEST_API(kbase_alloc_free_region);

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
		mutex_lock(&reg->kctx->jit_evict_lock);

		/*
		 * The physical allocation should have been removed from the
		 * eviction list before this function is called. However, in the
		 * case of abnormal process termination or the app leaking the
		 * memory kbase_mem_free_region is not called so it can still be
		 * on the list at termination time of the region tracker.
		 */
		if (!list_empty(&reg->gpu_alloc->evict_node)) {
			mutex_unlock(&reg->kctx->jit_evict_lock);

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
			mutex_unlock(&reg->kctx->jit_evict_lock);
		}

		/*
		 * Remove the region from the sticky resource metadata
		 * list should it be there.
		 */
		kbase_sticky_resource_release(reg->kctx, NULL,
				reg->start_pfn << PAGE_SHIFT);

		kbase_mem_phy_alloc_put(reg->cpu_alloc);
		kbase_mem_phy_alloc_put(reg->gpu_alloc);
		/* To detect use-after-free in debug builds */
		KBASE_DEBUG_CODE(reg->flags |= KBASE_REG_FREE);
	}
	kfree(reg);
}

KBASE_EXPORT_TEST_API(kbase_free_alloced_region);

int kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, u64 addr, size_t nr_pages, size_t align)
{
	int err;
	size_t i = 0;
	unsigned long attr;
	unsigned long mask = ~KBASE_REG_MEMATTR_MASK;
	unsigned long gwt_mask = ~0;

#ifdef CONFIG_MALI_JOB_DUMP
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

	if (reg->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS) {
		u64 stride;
		struct kbase_mem_phy_alloc *alloc;

		alloc = reg->gpu_alloc;
		stride = alloc->imported.alias.stride;
		KBASE_DEBUG_ASSERT(alloc->imported.alias.aliased);
		for (i = 0; i < alloc->imported.alias.nents; i++) {
			if (alloc->imported.alias.aliased[i].alloc) {
				err = kbase_mmu_insert_pages(kctx,
						reg->start_pfn + (i * stride),
						alloc->imported.alias.aliased[i].alloc->pages + alloc->imported.alias.aliased[i].offset,
						alloc->imported.alias.aliased[i].length,
						reg->flags & gwt_mask);
				if (err)
					goto bad_insert;

				kbase_mem_phy_alloc_gpu_mapped(alloc->imported.alias.aliased[i].alloc);
			} else {
				err = kbase_mmu_insert_single_page(kctx,
					reg->start_pfn + i * stride,
					kctx->aliasing_sink_page,
					alloc->imported.alias.aliased[i].length,
					(reg->flags & mask & gwt_mask) | attr);

				if (err)
					goto bad_insert;
			}
		}
	} else {
		err = kbase_mmu_insert_pages(kctx, reg->start_pfn,
				kbase_get_gpu_phy_pages(reg),
				kbase_reg_current_backed_size(reg),
				reg->flags & gwt_mask);
		if (err)
			goto bad_insert;
		kbase_mem_phy_alloc_gpu_mapped(reg->gpu_alloc);
	}

	return err;

bad_insert:
	if (reg->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS) {
		u64 stride;

		stride = reg->gpu_alloc->imported.alias.stride;
		KBASE_DEBUG_ASSERT(reg->gpu_alloc->imported.alias.aliased);
		while (i--)
			if (reg->gpu_alloc->imported.alias.aliased[i].alloc) {
				kbase_mmu_teardown_pages(kctx, reg->start_pfn + (i * stride), reg->gpu_alloc->imported.alias.aliased[i].length);
				kbase_mem_phy_alloc_gpu_unmapped(reg->gpu_alloc->imported.alias.aliased[i].alloc);
			}
	}

	kbase_remove_va_region(kctx, reg);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_gpu_mmap);

static void kbase_jd_user_buf_unmap(struct kbase_context *kctx,
		struct kbase_mem_phy_alloc *alloc, bool writeable);

int kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	int err;

	if (reg->start_pfn == 0)
		return 0;

	if (reg->gpu_alloc && reg->gpu_alloc->type == KBASE_MEM_TYPE_ALIAS) {
		size_t i;

		err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, reg->nr_pages);
		KBASE_DEBUG_ASSERT(reg->gpu_alloc->imported.alias.aliased);
		for (i = 0; i < reg->gpu_alloc->imported.alias.nents; i++)
			if (reg->gpu_alloc->imported.alias.aliased[i].alloc)
				kbase_mem_phy_alloc_gpu_unmapped(reg->gpu_alloc->imported.alias.aliased[i].alloc);
	} else {
		err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, kbase_reg_current_backed_size(reg));
		kbase_mem_phy_alloc_gpu_unmapped(reg->gpu_alloc);
	}

	if (reg->gpu_alloc && reg->gpu_alloc->type ==
			KBASE_MEM_TYPE_IMPORTED_USER_BUF) {
		struct kbase_alloc_import_user_buf *user_buf =
			&reg->gpu_alloc->imported.user_buf;

		if (user_buf->current_mapping_usage_count & PINNED_ON_IMPORT) {
			user_buf->current_mapping_usage_count &=
				~PINNED_ON_IMPORT;

			kbase_jd_user_buf_unmap(kctx, reg->gpu_alloc,
					(reg->flags & KBASE_REG_GPU_WR));
		}
	}

	if (err)
		return err;

	err = kbase_remove_va_region(kctx, reg);
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
	if (!reg) {
		dev_warn(kctx->kbdev->dev, "Can't find region at VA 0x%016llX",
				sset->mem_handle.basep.handle);
		err = -EINVAL;
		goto out_unlock;
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED) ||
			kbase_mem_is_imported(reg->gpu_alloc->type))
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
	lockdep_assert_held(&kctx->reg_lock);

	if (reg->flags & KBASE_REG_JIT) {
		dev_warn(reg->kctx->kbdev->dev, "Attempt to free JIT memory!\n");
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
		dev_warn(reg->kctx->kbdev->dev, "Could not unmap from the GPU...\n");
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
		kctx->cookies |= (1UL << cookie);

		kbase_free_alloced_region(reg);
	} else {
		/* A real GPU va */
		/* Validate the region */
		reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
		if (!reg || (reg->flags & KBASE_REG_FREE)) {
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
		if (flags & BASE_MEM_COHERENT_SYSTEM_REQUIRED)
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
	if (kctx->kbdev->system_coherency == COHERENCY_ACE &&
		(reg->flags & KBASE_REG_SHARE_BOTH)) {
		reg->flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT_ACE);
	} else {
		reg->flags |=
			KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT);
	}

	return 0;
}

int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc *alloc,
		size_t nr_pages_requested)
{
	int new_page_count __maybe_unused;
	size_t nr_left = nr_pages_requested;
	int res;
	struct kbase_context *kctx;
	struct tagged_addr *tp;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);

	if (alloc->reg) {
		if (nr_pages_requested > alloc->reg->nr_pages - alloc->nents)
			goto invalid_request;
	}

	kctx = alloc->imported.kctx;

	if (nr_pages_requested == 0)
		goto done; /*nothing to do*/

	new_page_count = kbase_atomic_add_pages(
			nr_pages_requested, &kctx->used_pages);
	kbase_atomic_add_pages(nr_pages_requested,
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

		res = kbase_mem_pool_alloc_pages(&kctx->lp_mem_pool,
						 nr_lp * (SZ_2M / SZ_4K),
						 tp,
						 true);

		if (res > 0) {
			nr_left -= res;
			tp += res;
		}

		if (nr_left) {
			struct kbase_sub_alloc *sa, *temp_sa;

			mutex_lock(&kctx->mem_partials_lock);

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
			mutex_unlock(&kctx->mem_partials_lock);
		}

		/* only if we actually have a chunk left <512. If more it indicates
		 * that we couldn't allocate a 2MB above, so no point to retry here.
		 */
		if (nr_left > 0 && nr_left < (SZ_2M / SZ_4K)) {
			/* create a new partial and suballocate the rest from it */
			struct page *np = NULL;

			do {
				int err;

				np = kbase_mem_pool_alloc(&kctx->lp_mem_pool);
				if (np)
					break;
				err = kbase_mem_pool_grow(&kctx->lp_mem_pool, 1);
				if (err)
					break;
			} while (1);

			if (np) {
				int i;
				struct kbase_sub_alloc *sa;
				struct page *p;

				sa = kmalloc(sizeof(*sa), GFP_KERNEL);
				if (!sa) {
					kbase_mem_pool_free(&kctx->lp_mem_pool, np, false);
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
				mutex_lock(&kctx->mem_partials_lock);
				list_add(&sa->link, &kctx->mem_partials);
				mutex_unlock(&kctx->mem_partials_lock);
			}
		}
	}
no_new_partial:
#endif

	if (nr_left) {
		res = kbase_mem_pool_alloc_pages(&kctx->mem_pool,
						 nr_left,
						 tp,
						 false);
		if (res <= 0)
			goto alloc_failed;
	}

	KBASE_TLSTREAM_AUX_PAGESALLOC(
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
		kbase_atomic_add_pages(nr_pages_to_free, &kctx->used_pages);
		kbase_atomic_add_pages(nr_pages_to_free,
			       &kctx->kbdev->memdev.used_pages);

		kbase_free_phy_pages_helper(alloc, nr_pages_to_free);
	}

	kbase_process_page_usage_dec(kctx, nr_pages_requested);
	kbase_atomic_sub_pages(nr_pages_requested, &kctx->used_pages);
	kbase_atomic_sub_pages(nr_pages_requested,
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
	struct tagged_addr *tp;
	struct tagged_addr *new_pages = NULL;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);

	lockdep_assert_held(&pool->pool_lock);

#if !defined(CONFIG_MALI_2MB_ALLOC)
	WARN_ON(pool->order);
#endif

	if (alloc->reg) {
		if (nr_pages_requested > alloc->reg->nr_pages - alloc->nents)
			goto invalid_request;
	}

	kctx = alloc->imported.kctx;

	lockdep_assert_held(&kctx->mem_partials_lock);

	if (nr_pages_requested == 0)
		goto done; /*nothing to do*/

	new_page_count = kbase_atomic_add_pages(
			nr_pages_requested, &kctx->used_pages);
	kbase_atomic_add_pages(nr_pages_requested,
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
			kctx->id,
			(u64)new_page_count);

	alloc->nents += nr_pages_requested;
done:
	return new_pages;

alloc_failed:
	/* rollback needed if got one or more 2MB but failed later */
	if (nr_left != nr_pages_requested) {
		size_t nr_pages_to_free = nr_pages_requested - nr_left;

		alloc->nents += nr_pages_to_free;

		kbase_process_page_usage_inc(kctx, nr_pages_to_free);
		kbase_atomic_add_pages(nr_pages_to_free, &kctx->used_pages);
		kbase_atomic_add_pages(nr_pages_to_free,
			       &kctx->kbdev->memdev.used_pages);

		kbase_free_phy_pages_helper(alloc, nr_pages_to_free);
	}

	kbase_process_page_usage_dec(kctx, nr_pages_requested);
	kbase_atomic_sub_pages(nr_pages_requested, &kctx->used_pages);
	kbase_atomic_sub_pages(nr_pages_requested,
			       &kctx->kbdev->memdev.used_pages);

invalid_request:
	return NULL;
}

static void free_partial(struct kbase_context *kctx, struct tagged_addr tp)
{
	struct page *p, *head_page;
	struct kbase_sub_alloc *sa;

	p = phys_to_page(as_phys_addr_t(tp));
	head_page = (struct page *)p->lru.prev;
	sa = (struct kbase_sub_alloc *)head_page->lru.next;
	mutex_lock(&kctx->mem_partials_lock);
	clear_bit(p - head_page, sa->sub_pages);
	if (bitmap_empty(sa->sub_pages, SZ_2M / SZ_4K)) {
		list_del(&sa->link);
		kbase_mem_pool_free(&kctx->lp_mem_pool, head_page, true);
		kfree(sa);
	} else if (bitmap_weight(sa->sub_pages, SZ_2M / SZ_4K) ==
		   SZ_2M / SZ_4K - 1) {
		/* expose the partial again */
		list_add(&sa->link, &kctx->mem_partials);
	}
	mutex_unlock(&kctx->mem_partials_lock);
}

int kbase_free_phy_pages_helper(
	struct kbase_mem_phy_alloc *alloc,
	size_t nr_pages_to_free)
{
	struct kbase_context *kctx = alloc->imported.kctx;
	bool syncback;
	bool reclaimed = (alloc->evicted != 0);
	struct tagged_addr *start_free;
	int new_page_count __maybe_unused;
	size_t freed = 0;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);
	KBASE_DEBUG_ASSERT(alloc->nents >= nr_pages_to_free);

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
			kbase_mem_pool_free_pages(&kctx->lp_mem_pool,
					512,
					start_free,
					syncback,
					reclaimed);
			nr_pages_to_free -= 512;
			start_free += 512;
			freed += 512;
		} else if (is_partial(*start_free)) {
			free_partial(kctx, *start_free);
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
			kbase_mem_pool_free_pages(&kctx->mem_pool,
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
		new_page_count = kbase_atomic_sub_pages(freed,
							&kctx->used_pages);
		kbase_atomic_sub_pages(freed,
				       &kctx->kbdev->memdev.used_pages);

		KBASE_TLSTREAM_AUX_PAGESALLOC(
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

	p = phys_to_page(as_phys_addr_t(tp));
	head_page = (struct page *)p->lru.prev;
	sa = (struct kbase_sub_alloc *)head_page->lru.next;
	clear_bit(p - head_page, sa->sub_pages);
	if (bitmap_empty(sa->sub_pages, SZ_2M / SZ_4K)) {
		list_del(&sa->link);
		kbase_mem_pool_free(pool, head_page, true);
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
	struct kbase_context *kctx = alloc->imported.kctx;
	bool syncback;
	bool reclaimed = (alloc->evicted != 0);
	struct tagged_addr *start_free;
	size_t freed = 0;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);
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
		new_page_count = kbase_atomic_sub_pages(freed,
							&kctx->used_pages);
		kbase_atomic_sub_pages(freed,
				       &kctx->kbdev->memdev.used_pages);

		KBASE_TLSTREAM_AUX_PAGESALLOC(
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
		if (!WARN_ON(!alloc->imported.kctx)) {
			/*
			 * The physical allocation must have been removed from
			 * the eviction list before trying to free it.
			 */
			mutex_lock(&alloc->imported.kctx->jit_evict_lock);
			WARN_ON(!list_empty(&alloc->evict_node));
			mutex_unlock(&alloc->imported.kctx->jit_evict_lock);
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
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM:
		dma_buf_detach(alloc->imported.umm.dma_buf,
			       alloc->imported.umm.dma_attachment);
		dma_buf_put(alloc->imported.umm.dma_buf);
		break;
#endif
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF:
		if (alloc->imported.user_buf.mm)
			mmdrop(alloc->imported.user_buf.mm);
		kfree(alloc->imported.user_buf.pages);
		break;
	case KBASE_MEM_TYPE_TB:{
		void *tb;

		tb = alloc->imported.kctx->jctx.tb;
		kbase_device_trace_buffer_uninstall(alloc->imported.kctx);
		vfree(tb);
		break;
	}
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

	/* GPU should have at least read or write access otherwise there is no
	   reason for allocating. */
	if ((flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)) == 0)
		return false;

	/* BASE_MEM_IMPORT_SHARED is only valid for imported memory */
	if ((flags & BASE_MEM_IMPORT_SHARED) == BASE_MEM_IMPORT_SHARED)
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

	/* Secure memory cannot be read by the CPU */
	if ((flags & BASE_MEM_SECURE) && (flags & BASE_MEM_PROT_CPU_RD))
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

	if (va_pages > (U64_MAX / PAGE_SIZE)) {
		/* 64-bit address range is the max */
		dev_warn(dev, KBASE_MSG_PRE "va_pages==%lld larger than 64-bit address range!",
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

void kbase_jit_debugfs_init(struct kbase_context *kctx)
{
	/* Debugfs entry for getting the number of JIT allocations. */
	debugfs_create_file("mem_jit_count", S_IRUGO, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_count_fops);

	/*
	 * Debugfs entry for getting the total number of virtual pages
	 * used by JIT allocations.
	 */
	debugfs_create_file("mem_jit_vm", S_IRUGO, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_vm_fops);

	/*
	 * Debugfs entry for getting the number of physical pages used
	 * by JIT allocations.
	 */
	debugfs_create_file("mem_jit_phys", S_IRUGO, kctx->kctx_dentry,
			kctx, &kbase_jit_debugfs_phys_fops);
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
		reg->flags &= ~KBASE_REG_JIT;
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

	INIT_LIST_HEAD(&kctx->jit_pending_alloc);
	INIT_LIST_HEAD(&kctx->jit_atoms_head);
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
	struct kbase_va_region *walker, struct base_jit_alloc_info *info)
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

static int kbase_jit_grow(struct kbase_context *kctx,
		struct base_jit_alloc_info *info, struct kbase_va_region *reg)
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
		pool = &kctx->lp_mem_pool;
		/* Round up to number of 2 MB pages required */
		pages_required += ((SZ_2M / SZ_4K) - 1);
		pages_required /= (SZ_2M / SZ_4K);
	} else {
#endif
		pool = &kctx->mem_pool;
#ifdef CONFIG_MALI_2MB_ALLOC
	}
#endif

	if (reg->cpu_alloc != reg->gpu_alloc)
		pages_required *= 2;

	mutex_lock(&kctx->mem_partials_lock);
	kbase_mem_pool_lock(pool);

	/* As we can not allocate memory from the kernel with the vm_lock held,
	 * grow the pool to the required size with the lock dropped. We hold the
	 * pool lock to prevent another thread from allocating from the pool
	 * between the grow and allocation.
	 */
	while (kbase_mem_pool_size(pool) < pages_required) {
		int pool_delta = pages_required - kbase_mem_pool_size(pool);

		kbase_mem_pool_unlock(pool);
		mutex_unlock(&kctx->mem_partials_lock);
		kbase_gpu_vm_unlock(kctx);

		if (kbase_mem_pool_grow(pool, pool_delta))
			goto update_failed_unlocked;

		kbase_gpu_vm_lock(kctx);
		mutex_lock(&kctx->mem_partials_lock);
		kbase_mem_pool_lock(pool);
	}

	gpu_pages = kbase_alloc_phy_pages_helper_locked(reg->gpu_alloc, pool,
			delta, &prealloc_sas[0]);
	if (!gpu_pages) {
		kbase_mem_pool_unlock(pool);
		mutex_unlock(&kctx->mem_partials_lock);
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
			mutex_unlock(&kctx->mem_partials_lock);
			goto update_failed;
		}
	}
	kbase_mem_pool_unlock(pool);
	mutex_unlock(&kctx->mem_partials_lock);

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

struct kbase_va_region *kbase_jit_allocate(struct kbase_context *kctx,
		struct base_jit_alloc_info *info)
{
	struct kbase_va_region *reg = NULL;

	if (kctx->jit_current_allocations >= kctx->jit_max_allocations) {
		/* Too many current allocations */
		return NULL;
	}
	if (info->max_allocations > 0 &&
			kctx->jit_current_allocations_per_bin[info->bin_id] >=
			info->max_allocations) {
		/* Too many current allocations in this bin */
		return NULL;
	}

	mutex_lock(&kctx->jit_evict_lock);

	/*
	 * Scan the pool for an existing allocation which meets our
	 * requirements and remove it.
	 */
	if (info->usage_id != 0) {
		/* First scan for an allocation with the same usage ID */
		struct kbase_va_region *walker;
		struct kbase_va_region *temp;
		size_t current_diff = SIZE_MAX;

		list_for_each_entry_safe(walker, temp, &kctx->jit_pool_head,
				jit_node) {

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
		struct kbase_va_region *temp;
		size_t current_diff = SIZE_MAX;

		list_for_each_entry_safe(walker, temp, &kctx->jit_pool_head,
				jit_node) {

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
			goto update_failed_unlocked;
		}
	} else {
		/* No suitable JIT allocation was found so create a new one */
		u64 flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD |
				BASE_MEM_PROT_GPU_WR | BASE_MEM_GROW_ON_GPF |
				BASE_MEM_COHERENT_LOCAL;
		u64 gpu_addr;

		mutex_unlock(&kctx->jit_evict_lock);

		if (info->flags & BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP)
			flags |= BASE_MEM_TILER_ALIGN_TOP;

		reg = kbase_mem_alloc(kctx, info->va_pages, info->commit_pages,
				info->extent, &flags, &gpu_addr);
		if (!reg)
			goto out_unlocked;

		reg->flags |= KBASE_REG_JIT;

		mutex_lock(&kctx->jit_evict_lock);
		list_add(&reg->jit_node, &kctx->jit_active_head);
		mutex_unlock(&kctx->jit_evict_lock);
	}

	kctx->jit_current_allocations++;
	kctx->jit_current_allocations_per_bin[info->bin_id]++;

	reg->jit_usage_id = info->usage_id;
	reg->jit_bin_id = info->bin_id;

	return reg;

update_failed_unlocked:
	mutex_lock(&kctx->jit_evict_lock);
	list_move(&reg->jit_node, &kctx->jit_pool_head);
	mutex_unlock(&kctx->jit_evict_lock);
out_unlocked:
	return NULL;
}

void kbase_jit_free(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	u64 old_pages;

	/* Get current size of JIT region */
	old_pages = kbase_reg_current_backed_size(reg);
	if (reg->initial_commit < old_pages) {
		/* Free trim_level % of region, but don't go below initial
		 * commit size
		 */
		u64 new_size = MAX(reg->initial_commit,
			div_u64(old_pages * (100 - kctx->trim_level), 100));
		u64 delta = old_pages - new_size;

		if (delta) {
			kbase_mem_shrink_cpu_mapping(kctx, reg, old_pages-delta,
					old_pages);
			kbase_mem_shrink_gpu_mapping(kctx, reg, old_pages-delta,
					old_pages);

			kbase_free_phy_pages_helper(reg->cpu_alloc, delta);
			if (reg->cpu_alloc != reg->gpu_alloc)
				kbase_free_phy_pages_helper(reg->gpu_alloc,
						delta);
		}
	}

	kctx->jit_current_allocations--;
	kctx->jit_current_allocations_per_bin[reg->jit_bin_id]--;

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
	struct kbase_context *kctx = reg->kctx;

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
		reg->flags &= ~KBASE_REG_JIT;
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
		walker->flags &= ~KBASE_REG_JIT;
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
		walker->flags &= ~KBASE_REG_JIT;
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

static int kbase_jd_user_buf_map(struct kbase_context *kctx,
		struct kbase_va_region *reg)
{
	long pinned_pages;
	struct kbase_mem_phy_alloc *alloc;
	struct page **pages;
	struct tagged_addr *pa;
	long i;
	int err = -ENOMEM;
	unsigned long address;
	struct mm_struct *mm;
	struct device *dev;
	unsigned long offset;
	unsigned long local_size;
	unsigned long gwt_mask = ~0;

	alloc = reg->gpu_alloc;
	pa = kbase_get_gpu_phy_pages(reg);
	address = alloc->imported.user_buf.address;
	mm = alloc->imported.user_buf.mm;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_IMPORTED_USER_BUF);

	pages = alloc->imported.user_buf.pages;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	pinned_pages = get_user_pages(NULL, mm,
			address,
			alloc->imported.user_buf.nr_pages,
			reg->flags & KBASE_REG_GPU_WR,
			0, pages, NULL);
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

	alloc->nents = pinned_pages;
#ifdef CONFIG_MALI_JOB_DUMP
	if (kctx->gwt_enabled)
		gwt_mask = ~KBASE_REG_GPU_WR;
#endif

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn, pa,
			kbase_reg_current_backed_size(reg),
			reg->flags & gwt_mask);
	if (err == 0)
		return 0;

	alloc->nents = 0;
	/* fall down */
unwind:
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

/* to replace sg_dma_len. */
#define MALI_SG_DMA_LEN(sg)        ((sg)->length)

#ifdef CONFIG_DMA_SHARED_BUFFER
static int kbase_jd_umm_map(struct kbase_context *kctx,
		struct kbase_va_region *reg)
{
	struct sg_table *sgt;
	struct scatterlist *s;
	int i;
	struct tagged_addr *pa;
	int err;
	size_t count = 0;
	struct kbase_mem_phy_alloc *alloc;
	unsigned long gwt_mask = ~0;

	alloc = reg->gpu_alloc;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM);
	KBASE_DEBUG_ASSERT(NULL == alloc->imported.umm.sgt);
	sgt = dma_buf_map_attachment(alloc->imported.umm.dma_attachment,
			DMA_BIDIRECTIONAL);

	if (IS_ERR_OR_NULL(sgt))
		return -EINVAL;

	/* save for later */
	alloc->imported.umm.sgt = sgt;

	pa = kbase_get_gpu_phy_pages(reg);
	KBASE_DEBUG_ASSERT(pa);

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		int j;
		size_t pages = PFN_UP(MALI_SG_DMA_LEN(s));

		WARN_ONCE(MALI_SG_DMA_LEN(s) & (PAGE_SIZE-1),
		"MALI_SG_DMA_LEN(s)=%u is not a multiple of PAGE_SIZE\n",
		MALI_SG_DMA_LEN(s));

		WARN_ONCE(sg_dma_address(s) & (PAGE_SIZE-1),
		"sg_dma_address(s)=%llx is not aligned to PAGE_SIZE\n",
		(unsigned long long) sg_dma_address(s));

		for (j = 0; (j < pages) && (count < reg->nr_pages); j++,
				count++)
			*pa++ = as_tagged(sg_dma_address(s) +
				(j << PAGE_SHIFT));
		WARN_ONCE(j < pages,
			  "sg list from dma_buf_map_attachment > dma_buf->size=%zu\n",
		alloc->imported.umm.dma_buf->size);
	}

	if (!(reg->flags & KBASE_REG_IMPORT_PAD) &&
			WARN_ONCE(count < reg->nr_pages,
			"sg list from dma_buf_map_attachment < dma_buf->size=%zu\n",
			alloc->imported.umm.dma_buf->size)) {
		err = -EINVAL;
		goto err_unmap_attachment;
	}

	/* Update nents as we now have pages to map */
	alloc->nents = reg->nr_pages;

#ifdef CONFIG_MALI_JOB_DUMP
	if (kctx->gwt_enabled)
		gwt_mask = ~KBASE_REG_GPU_WR;
#endif

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn,
			kbase_get_gpu_phy_pages(reg),
			count,
			(reg->flags | KBASE_REG_GPU_WR | KBASE_REG_GPU_RD) &
			 gwt_mask);
	if (err)
		goto err_unmap_attachment;

	if (reg->flags & KBASE_REG_IMPORT_PAD) {
		err = kbase_mmu_insert_single_page(kctx,
				reg->start_pfn + count,
				kctx->aliasing_sink_page,
				reg->nr_pages - count,
				(reg->flags | KBASE_REG_GPU_RD) &
				~KBASE_REG_GPU_WR);
		if (err)
			goto err_teardown_orig_pages;
	}

	return 0;

err_teardown_orig_pages:
	kbase_mmu_teardown_pages(kctx, reg->start_pfn, count);
err_unmap_attachment:
	dma_buf_unmap_attachment(alloc->imported.umm.dma_attachment,
			alloc->imported.umm.sgt, DMA_BIDIRECTIONAL);
	alloc->imported.umm.sgt = NULL;

	return err;
}

static void kbase_jd_umm_unmap(struct kbase_context *kctx,
		struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(alloc);
	KBASE_DEBUG_ASSERT(alloc->imported.umm.dma_attachment);
	KBASE_DEBUG_ASSERT(alloc->imported.umm.sgt);
	dma_buf_unmap_attachment(alloc->imported.umm.dma_attachment,
	    alloc->imported.umm.sgt, DMA_BIDIRECTIONAL);
	alloc->imported.umm.sgt = NULL;
	alloc->nents = 0;
}
#endif				/* CONFIG_DMA_SHARED_BUFFER */

struct kbase_mem_phy_alloc *kbase_map_external_resource(
		struct kbase_context *kctx, struct kbase_va_region *reg,
		struct mm_struct *locked_mm)
{
	int err;

	/* decide what needs to happen for this resource */
	switch (reg->gpu_alloc->type) {
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
		if (reg->gpu_alloc->imported.user_buf.mm != locked_mm)
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
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		reg->gpu_alloc->imported.umm.current_mapping_usage_count++;
		if (1 == reg->gpu_alloc->imported.umm.current_mapping_usage_count) {
			err = kbase_jd_umm_map(kctx, reg);
			if (err) {
				reg->gpu_alloc->imported.umm.current_mapping_usage_count--;
				goto exit;
			}
		}
		break;
	}
#endif
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
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM: {
		alloc->imported.umm.current_mapping_usage_count--;

		if (0 == alloc->imported.umm.current_mapping_usage_count) {
			if (reg && reg->gpu_alloc == alloc) {
				int err;

				err = kbase_mmu_teardown_pages(
						kctx,
						reg->start_pfn,
						alloc->nents);
				WARN_ON(err);
			}

			kbase_jd_umm_unmap(kctx, alloc);
		}
	}
	break;
#endif /* CONFIG_DMA_SHARED_BUFFER */
	case KBASE_MEM_TYPE_IMPORTED_USER_BUF: {
		alloc->imported.user_buf.current_mapping_usage_count--;

		if (0 == alloc->imported.user_buf.current_mapping_usage_count) {
			bool writeable = true;

			if (reg && reg->gpu_alloc == alloc)
				kbase_mmu_teardown_pages(
						kctx,
						reg->start_pfn,
						kbase_reg_current_backed_size(reg));

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
			break;
		}
	}

	/* No metadata exists so create one. */
	if (!meta) {
		struct kbase_va_region *reg;

		/* Find the region */
		reg = kbase_region_tracker_find_region_enclosing_address(
				kctx, gpu_addr);
		if (NULL == reg || (reg->flags & KBASE_REG_FREE))
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

bool kbase_sticky_resource_release(struct kbase_context *kctx,
		struct kbase_ctx_ext_res_meta *meta, u64 gpu_addr)
{
	struct kbase_ctx_ext_res_meta *walker;
	struct kbase_va_region *reg;

	lockdep_assert_held(&kctx->reg_lock);

	/* Search of the metadata if one isn't provided. */
	if (!meta) {
		/*
		 * Walk the per context external resource metadata list for the
		 * metadata which matches the region which is being released.
		 */
		list_for_each_entry(walker, &kctx->ext_res_meta_head,
				ext_res_node) {
			if (walker->gpu_addr == gpu_addr) {
				meta = walker;
				break;
			}
		}
	}

	/* No metadata so just return. */
	if (!meta)
		return false;

	/* Drop the physical memory reference and free the metadata. */
	reg = kbase_region_tracker_find_region_enclosing_address(
			kctx,
			meta->gpu_addr);

	kbase_unmap_external_resource(kctx, reg, meta->alloc);
	list_del(&meta->ext_res_node);
	kfree(meta);

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

		kbase_sticky_resource_release(kctx, walker, 0);
	}
}
