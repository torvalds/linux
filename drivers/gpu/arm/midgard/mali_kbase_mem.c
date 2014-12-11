/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_mem.c
 * Base kernel memory APIs
 */
#ifdef CONFIG_DMA_SHARED_BUFFER
#include <linux/dma-buf.h>
#endif				/* CONFIG_DMA_SHARED_BUFFER */

#include <linux/bug.h>
#include <linux/compat.h>

#include <mali_kbase_config.h>
#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_cache_policy.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_gator.h>

/**
 * @brief Check the zone compatibility of two regions.
 */
STATIC int kbase_region_tracker_match_zone(struct kbase_va_region *reg1, struct kbase_va_region *reg2)
{
	return ((reg1->flags & KBASE_REG_ZONE_MASK) == (reg2->flags & KBASE_REG_ZONE_MASK));
}

KBASE_EXPORT_TEST_API(kbase_region_tracker_match_zone)

/* This function inserts a region into the tree. */
static void kbase_region_tracker_insert(struct kbase_context *kctx, struct kbase_va_region *new_reg)
{
	u64 start_pfn = new_reg->start_pfn;
	struct rb_node **link = &(kctx->reg_rbtree.rb_node);
	struct rb_node *parent = NULL;

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
	rb_insert_color(&(new_reg->rblink), &(kctx->reg_rbtree));
}

/* Find allocated region enclosing free range. */
static struct kbase_va_region *kbase_region_tracker_find_region_enclosing_range_free(
		struct kbase_context *kctx, u64 start_pfn, size_t nr_pages)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	u64 end_pfn = start_pfn + nr_pages;

	rbnode = kctx->reg_rbtree.rb_node;
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
struct kbase_va_region *kbase_region_tracker_find_region_enclosing_address(struct kbase_context *kctx, mali_addr64 gpu_addr)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	rbnode = kctx->reg_rbtree.rb_node;
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

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_enclosing_address)

/* Find region with given base address */
struct kbase_va_region *kbase_region_tracker_find_region_base_address(struct kbase_context *kctx, mali_addr64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	lockdep_assert_held(&kctx->reg_lock);

	rbnode = kctx->reg_rbtree.rb_node;
	while (rbnode) {
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if (reg->start_pfn > gpu_pfn)
			rbnode = rbnode->rb_left;
		else if (reg->start_pfn < gpu_pfn)
			rbnode = rbnode->rb_right;
		else if (gpu_pfn == reg->start_pfn)
			return reg;
		else
			rbnode = NULL;
	}

	return NULL;
}

KBASE_EXPORT_TEST_API(kbase_region_tracker_find_region_base_address)

/* Find region meeting given requirements */
static struct kbase_va_region *kbase_region_tracker_find_region_meeting_reqs(struct kbase_context *kctx, struct kbase_va_region *reg_reqs, size_t nr_pages, size_t align)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	/* Note that this search is a linear search, as we do not have a target
	   address in mind, so does not benefit from the rbtree search */
	rbnode = rb_first(&(kctx->reg_rbtree));
	while (rbnode) {
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if ((reg->nr_pages >= nr_pages) &&
				(reg->flags & KBASE_REG_FREE) &&
				kbase_region_tracker_match_zone(reg, reg_reqs)) {
			/* Check alignment */
			u64 start_pfn = (reg->start_pfn + align - 1) & ~(align - 1);

			if ((start_pfn >= reg->start_pfn) &&
					(start_pfn <= (reg->start_pfn + reg->nr_pages - 1)) &&
					((start_pfn + nr_pages - 1) <= (reg->start_pfn + reg->nr_pages - 1)))
				return reg;
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
STATIC mali_error kbase_remove_va_region(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	struct rb_node *rbprev;
	struct kbase_va_region *prev = NULL;
	struct rb_node *rbnext;
	struct kbase_va_region *next = NULL;

	int merged_front = 0;
	int merged_back = 0;
	mali_error err = MALI_ERROR_NONE;

	/* Try to merge with the previous block first */
	rbprev = rb_prev(&(reg->rblink));
	if (rbprev) {
		prev = rb_entry(rbprev, struct kbase_va_region, rblink);
		if ((prev->flags & KBASE_REG_FREE) && kbase_region_tracker_match_zone(prev, reg)) {
			/* We're compatible with the previous VMA, merge with it */
			prev->nr_pages += reg->nr_pages;
			rb_erase(&(reg->rblink), &kctx->reg_rbtree);
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
		if ((next->flags & KBASE_REG_FREE) && kbase_region_tracker_match_zone(next, reg)) {
			next->start_pfn = reg->start_pfn;
			next->nr_pages += reg->nr_pages;
			rb_erase(&(reg->rblink), &kctx->reg_rbtree);
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
			err = MALI_ERROR_OUT_OF_MEMORY;
			goto out;
		}

		rb_replace_node(&(reg->rblink), &(free_reg->rblink), &(kctx->reg_rbtree));
	}

 out:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_remove_va_region)

/**
 * @brief Insert a VA region to the list, replacing the current at_reg.
 */
static mali_error kbase_insert_va_region_nolock(struct kbase_context *kctx, struct kbase_va_region *new_reg, struct kbase_va_region *at_reg, u64 start_pfn, size_t nr_pages)
{
	mali_error err = MALI_ERROR_NONE;

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
		rb_replace_node(&(at_reg->rblink), &(new_reg->rblink), &(kctx->reg_rbtree));
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
			err = MALI_ERROR_OUT_OF_MEMORY;
		}
	}

	return err;
}

/**
 * @brief Add a VA region to the list.
 */
mali_error kbase_add_va_region(struct kbase_context *kctx,
		struct kbase_va_region *reg, mali_addr64 addr,
		size_t nr_pages, size_t align)
{
	struct kbase_va_region *tmp;
	u64 gpu_pfn = addr >> PAGE_SHIFT;
	mali_error err = MALI_ERROR_NONE;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	lockdep_assert_held(&kctx->reg_lock);

	if (!align)
		align = 1;

	/* must be a power of 2 */
	KBASE_DEBUG_ASSERT((align & (align - 1)) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);

	/* Path 1: Map a specific address. Find the enclosing region, which *must* be free. */
	if (gpu_pfn) {
		struct device *dev = kctx->kbdev->dev;

		KBASE_DEBUG_ASSERT(!(gpu_pfn & (align - 1)));

		tmp = kbase_region_tracker_find_region_enclosing_range_free(kctx, gpu_pfn, nr_pages);
		if (!tmp) {
			dev_warn(dev, "Enclosing region not found: 0x%08llx gpu_pfn, %zu nr_pages", gpu_pfn, nr_pages);
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}

		if ((!kbase_region_tracker_match_zone(tmp, reg)) ||
				(!(tmp->flags & KBASE_REG_FREE))) {
			dev_warn(dev, "Zone mismatch: %lu != %lu", tmp->flags & KBASE_REG_ZONE_MASK, reg->flags & KBASE_REG_ZONE_MASK);
			dev_warn(dev, "!(tmp->flags & KBASE_REG_FREE): tmp->start_pfn=0x%llx tmp->flags=0x%lx tmp->nr_pages=0x%zx gpu_pfn=0x%llx nr_pages=0x%zx\n", tmp->start_pfn, tmp->flags, tmp->nr_pages, gpu_pfn, nr_pages);
			dev_warn(dev, "in function %s (%p, %p, 0x%llx, 0x%zx, 0x%zx)\n", __func__, kctx, reg, addr, nr_pages, align);
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}

		err = kbase_insert_va_region_nolock(kctx, reg, tmp, gpu_pfn, nr_pages);
		if (err) {
			dev_warn(dev, "Failed to insert va region");
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}

		goto exit;
	}

	/* Path 2: Map any free address which meets the requirements.  */
	{
		u64 start_pfn;

		tmp = kbase_region_tracker_find_region_meeting_reqs(kctx, reg, nr_pages, align);
		if (!tmp) {
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}
		start_pfn = (tmp->start_pfn + align - 1) & ~(align - 1);
		err = kbase_insert_va_region_nolock(kctx, reg, tmp, start_pfn, nr_pages);
	}

 exit:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_add_va_region)

/**
 * @brief Initialize the internal region tracker data structure.
 */
static void kbase_region_tracker_ds_init(struct kbase_context *kctx, struct kbase_va_region *same_va_reg, struct kbase_va_region *exec_reg, struct kbase_va_region *custom_va_reg)
{
	kctx->reg_rbtree = RB_ROOT;
	kbase_region_tracker_insert(kctx, same_va_reg);

	/* exec and custom_va_reg doesn't always exist */
	if (exec_reg && custom_va_reg) {
		kbase_region_tracker_insert(kctx, exec_reg);
		kbase_region_tracker_insert(kctx, custom_va_reg);
	}
}

void kbase_region_tracker_term(struct kbase_context *kctx)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	do {
		rbnode = rb_first(&(kctx->reg_rbtree));
		if (rbnode) {
			rb_erase(rbnode, &(kctx->reg_rbtree));
			reg = rb_entry(rbnode, struct kbase_va_region, rblink);
			kbase_free_alloced_region(reg);
		}
	} while (rbnode);
}

/**
 * Initialize the region tracker data structure.
 */
mali_error kbase_region_tracker_init(struct kbase_context *kctx)
{
	struct kbase_va_region *same_va_reg;
	struct kbase_va_region *exec_reg = NULL;
	struct kbase_va_region *custom_va_reg = NULL;
	size_t same_va_bits = sizeof(void *) * BITS_PER_BYTE;
	u64 custom_va_size = KBASE_REG_ZONE_CUSTOM_VA_SIZE;
	u64 gpu_va_limit = (1ULL << kctx->kbdev->gpu_props.mmu.va_bits) >> PAGE_SHIFT;

#if defined(CONFIG_ARM64)
	same_va_bits = VA_BITS;
#elif defined(CONFIG_X86_64)
	same_va_bits = 47;
#elif defined(CONFIG_64BIT)
#error Unsupported 64-bit architecture
#endif

#ifdef CONFIG_64BIT
	if (is_compat_task())
		same_va_bits = 32;
	else if (kbase_hw_has_feature(kctx->kbdev, BASE_HW_FEATURE_33BIT_VA))
		same_va_bits = 33;
#endif

	if (kctx->kbdev->gpu_props.mmu.va_bits < same_va_bits)
		return MALI_ERROR_FUNCTION_FAILED;

	/* all have SAME_VA */
	same_va_reg = kbase_alloc_free_region(kctx, 1,
			(1ULL << (same_va_bits - PAGE_SHIFT)) - 2,
			KBASE_REG_ZONE_SAME_VA);

	if (!same_va_reg)
		return MALI_ERROR_OUT_OF_MEMORY;

#ifdef CONFIG_64BIT
	/* only 32-bit clients have the other two zones */
	if (is_compat_task()) {
#endif
		if (gpu_va_limit <= KBASE_REG_ZONE_CUSTOM_VA_BASE) {
			kbase_free_alloced_region(same_va_reg);
			return MALI_ERROR_FUNCTION_FAILED;
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
			kbase_free_alloced_region(same_va_reg);
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		custom_va_reg = kbase_alloc_free_region(kctx,
				KBASE_REG_ZONE_CUSTOM_VA_BASE,
				custom_va_size, KBASE_REG_ZONE_CUSTOM_VA);

		if (!custom_va_reg) {
			kbase_free_alloced_region(same_va_reg);
			kbase_free_alloced_region(exec_reg);
			return MALI_ERROR_OUT_OF_MEMORY;
		}
#ifdef CONFIG_64BIT
	}
#endif

	kbase_region_tracker_ds_init(kctx, same_va_reg, exec_reg, custom_va_reg);

	return MALI_ERROR_NONE;
}

mali_error kbase_mem_init(struct kbase_device *kbdev)
{
	struct kbasep_mem_device *memdev;

	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	/* Initialize memory usage */
	atomic_set(&memdev->used_pages, 0);

	/* nothing to do, zero-inited when struct kbase_device was created */
	return MALI_ERROR_NONE;
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
}

KBASE_EXPORT_TEST_API(kbase_mem_term)

/**
 * @brief Wait for GPU write flush - only in use for BASE_HW_ISSUE_6367
 *
 * Wait 1000 GPU clock cycles. This delay is known to give the GPU time to flush its write buffer.
 * @note If GPU resets occur then the counters are reset to zero, the delay may not be as expected.
 */
#ifndef CONFIG_MALI_NO_MALI
void kbase_wait_write_flush(struct kbase_context *kctx)
{
	u32 base_count = 0;

	/* A suspend won't happen here, because we're in a syscall from a userspace thread */
	kbase_pm_context_active(kctx->kbdev);
	kbase_pm_request_gpu_cycle_counter(kctx->kbdev);

	while (MALI_TRUE) {
		u32 new_count;

		new_count = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_LO), NULL);
		/* First time around, just store the count. */
		if (base_count == 0) {
			base_count = new_count;
			continue;
		}

		/* No need to handle wrapping, unsigned maths works for this. */
		if ((new_count - base_count) > 1000)
			break;
	}

	kbase_pm_release_gpu_cycle_counter(kctx->kbdev);
	kbase_pm_context_idle(kctx->kbdev);
}
#endif				/* CONFIG_MALI_NO_MALI */



/**
 * @brief Allocate a free region object.
 *
 * The allocated object is not part of any list yet, and is flagged as
 * KBASE_REG_FREE. No mapping is allocated yet.
 *
 * zone is KBASE_REG_ZONE_CUSTOM_VA, KBASE_REG_ZONE_SAME_VA, or KBASE_REG_ZONE_EXEC
 *
 */
struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone)
{
	struct kbase_va_region *new_reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* zone argument should only contain zone related region flags */
	KBASE_DEBUG_ASSERT((zone & ~KBASE_REG_ZONE_MASK) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);
	KBASE_DEBUG_ASSERT(start_pfn + nr_pages <= (UINT64_MAX / PAGE_SIZE));	/* 64-bit address range is the max */

	new_reg = kzalloc(sizeof(*new_reg), GFP_KERNEL);

	if (!new_reg) {
		dev_warn(kctx->kbdev->dev, "kzalloc failed");
		return NULL;
	}

	new_reg->alloc = NULL; /* no alloc bound yet */
	new_reg->kctx = kctx;
	new_reg->flags = zone | KBASE_REG_FREE;

	new_reg->flags |= KBASE_REG_GROWABLE;

	/* Set up default MEMATTR usage */
	new_reg->flags |= KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_DEFAULT);

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;

	return new_reg;
}

KBASE_EXPORT_TEST_API(kbase_alloc_free_region)

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
	KBASE_DEBUG_ASSERT(NULL != reg);
	if (!(reg->flags & KBASE_REG_FREE)) {
		kbase_mem_phy_alloc_put(reg->alloc);
		/* To detect use-after-free in debug builds */
		KBASE_DEBUG_CODE(reg->flags |= KBASE_REG_FREE);
	}
	kfree(reg);
}

KBASE_EXPORT_TEST_API(kbase_free_alloced_region)

void kbase_mmu_update(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	struct kbase_as *as;
	struct kbase_mmu_setup *current_setup;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	kbdev = kctx->kbdev;
	as = &kbdev->as[kctx->as_nr];
	current_setup = &as->current_setup;

	/* Use GPU implementation-defined caching policy. */
	current_setup->memattr = kctx->mem_attrs;

	current_setup->transtab = (u64) kctx->pgd & ((0xFFFFFFFFULL << 32) | AS_TRANSTAB_ADDR_SPACE_MASK);
	current_setup->transtab |= AS_TRANSTAB_READ_INNER |
				   AS_TRANSTAB_ADRMODE_TABLE;

	/* Apply the address space setting */
	kbase_mmu_hw_configure(kbdev, as, kctx);
}

KBASE_EXPORT_TEST_API(kbase_mmu_update)

void kbase_mmu_disable(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	struct kbase_as *as;
	struct kbase_mmu_setup *current_setup;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	kbdev = kctx->kbdev;
	as = &kbdev->as[kctx->as_nr];
	current_setup = &as->current_setup;

	current_setup->transtab = 0ULL;

	/* Apply the address space setting */
	kbase_mmu_hw_configure(kbdev, as, kctx);
}

KBASE_EXPORT_TEST_API(kbase_mmu_disable)

mali_error kbase_gpu_mmap(struct kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align)
{
	mali_error err;
	size_t i = 0;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	err = kbase_add_va_region(kctx, reg, addr, nr_pages, align);
	if (MALI_ERROR_NONE != err)
		return err;

	if (reg->alloc->type == KBASE_MEM_TYPE_ALIAS) {
		u64 stride;

		stride = reg->alloc->imported.alias.stride;
		KBASE_DEBUG_ASSERT(reg->alloc->imported.alias.aliased);
		for (i = 0; i < reg->alloc->imported.alias.nents; i++) {
			if (reg->alloc->imported.alias.aliased[i].alloc) {
				err = kbase_mmu_insert_pages(kctx,
						reg->start_pfn + (i * stride),
						reg->alloc->imported.alias.aliased[i].alloc->pages + reg->alloc->imported.alias.aliased[i].offset,
						reg->alloc->imported.alias.aliased[i].length,
						reg->flags);
				if (MALI_ERROR_NONE != err)
					goto bad_insert;

				kbase_mem_phy_alloc_gpu_mapped(reg->alloc->imported.alias.aliased[i].alloc);
			} else {
				err = kbase_mmu_insert_single_page(kctx,
						reg->start_pfn + i * stride,
						kctx->aliasing_sink_page,
						reg->alloc->imported.alias.aliased[i].length,
						(reg->flags & ~KBASE_REG_MEMATTR_MASK) | KBASE_REG_MEMATTR_INDEX(AS_MEMATTR_INDEX_WRITE_ALLOC)
						);
				if (MALI_ERROR_NONE != err)
					goto bad_insert;
			}
		}
	} else {
		err = kbase_mmu_insert_pages(kctx, reg->start_pfn,
				kbase_get_phy_pages(reg),
				kbase_reg_current_backed_size(reg),
				reg->flags);
		if (MALI_ERROR_NONE != err)
			goto bad_insert;
		kbase_mem_phy_alloc_gpu_mapped(reg->alloc);
	}

	return err;

bad_insert:
	if (reg->alloc->type == KBASE_MEM_TYPE_ALIAS) {
		u64 stride;

		stride = reg->alloc->imported.alias.stride;
		KBASE_DEBUG_ASSERT(reg->alloc->imported.alias.aliased);
		while (i--)
			if (reg->alloc->imported.alias.aliased[i].alloc) {
				kbase_mmu_teardown_pages(kctx, reg->start_pfn + (i * stride), reg->alloc->imported.alias.aliased[i].length);
				kbase_mem_phy_alloc_gpu_unmapped(reg->alloc->imported.alias.aliased[i].alloc);
			}
	}

	kbase_remove_va_region(kctx, reg);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_gpu_mmap)

mali_error kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	mali_error err;

	if (reg->start_pfn == 0)
		return MALI_ERROR_NONE;

	if (reg->alloc && reg->alloc->type == KBASE_MEM_TYPE_ALIAS) {
		size_t i;

		err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, reg->nr_pages);
		KBASE_DEBUG_ASSERT(reg->alloc->imported.alias.aliased);
		for (i = 0; i < reg->alloc->imported.alias.nents; i++)
			if (reg->alloc->imported.alias.aliased[i].alloc)
				kbase_mem_phy_alloc_gpu_unmapped(reg->alloc->imported.alias.aliased[i].alloc);
	} else {
		err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, kbase_reg_current_backed_size(reg));
		kbase_mem_phy_alloc_gpu_unmapped(reg->alloc);
	}

	if (MALI_ERROR_NONE != err)
		return err;

	err = kbase_remove_va_region(kctx, reg);
	return err;
}

STATIC struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping_of_region(const struct kbase_va_region *reg, unsigned long uaddr, size_t size)
{
	struct kbase_cpu_mapping *map;
	struct list_head *pos;

	KBASE_DEBUG_ASSERT(NULL != reg);
	KBASE_DEBUG_ASSERT(reg->alloc);

	if ((uintptr_t) uaddr + size < (uintptr_t) uaddr) /* overflow check */
		return NULL;

	list_for_each(pos, &reg->alloc->mappings) {
		map = list_entry(pos, kbase_cpu_mapping, mappings_list);
		if (map->vm_start <= uaddr && map->vm_end >= uaddr + size)
			return map;
	}

	return NULL;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping_of_region)

mali_error kbasep_find_enclosing_cpu_mapping_offset(
	struct kbase_context *kctx, mali_addr64 gpu_addr,
	unsigned long uaddr, size_t size, mali_size64 *offset)
{
	struct kbase_cpu_mapping *map = NULL;
	const struct kbase_va_region *reg;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);
	if (reg && !(reg->flags & KBASE_REG_FREE)) {
		map = kbasep_find_enclosing_cpu_mapping_of_region(reg, uaddr,
				size);
		if (map) {
			*offset = (uaddr - PTR_TO_U64(map->vm_start)) +
						 (map->page_off << PAGE_SHIFT);
			err = MALI_ERROR_NONE;
		}
	}

	kbase_gpu_vm_unlock(kctx);

	return err;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping_offset)

void kbase_sync_single(struct kbase_context *kctx,
		phys_addr_t pa, size_t size, kbase_sync_kmem_fn sync_fn)
{
	struct page *p = pfn_to_page(PFN_DOWN(pa));
	off_t offset = pa & ~PAGE_MASK;
	dma_addr_t dma_addr;

	BUG_ON(!p);
	BUG_ON(offset + size > PAGE_SIZE);

	dma_addr = page_private(p) + offset;

	sync_fn(kctx->kbdev->dev, dma_addr, size, DMA_BIDIRECTIONAL);
}

static mali_error kbase_do_syncset(struct kbase_context *kctx, struct base_syncset *set, kbase_sync_kmem_fn sync_fn)
{
	mali_error err = MALI_ERROR_NONE;
	struct basep_syncset *sset = &set->basep_sset;
	struct kbase_va_region *reg;
	struct kbase_cpu_mapping *map;
	unsigned long start;
	size_t size;
	phys_addr_t *pa;
	u64 page_off, page_count;
	u64 i;
	unsigned int offset;

	kbase_os_mem_map_lock(kctx);
	kbase_gpu_vm_lock(kctx);

	/* find the region where the virtual address is contained */
	reg = kbase_region_tracker_find_region_enclosing_address(kctx, sset->mem_handle);
	if (!reg) {
		dev_warn(kctx->kbdev->dev, "Can't find region at VA 0x%016llX", sset->mem_handle);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED))
		goto out_unlock;

	start = (uintptr_t)sset->user_addr;
	size = (size_t)sset->size;

	map = kbasep_find_enclosing_cpu_mapping_of_region(reg, start, size);
	if (!map) {
		dev_warn(kctx->kbdev->dev, "Can't find CPU mapping 0x%016lX for VA 0x%016llX", start, sset->mem_handle);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	offset = start & (PAGE_SIZE - 1);
	page_off = map->page_off + ((start - map->vm_start) >> PAGE_SHIFT);
	page_count = (size + offset + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	pa = kbase_get_phy_pages(reg);

	/* Sync first page */
	if (pa[page_off]) {
		size_t sz = MIN(((size_t) PAGE_SIZE - offset), size);

		kbase_sync_single(kctx, pa[page_off] + offset, sz, sync_fn);
	}

	/* Sync middle pages (if any) */
	for (i = 1; page_count > 2 && i < page_count - 1; i++) {
		/* we grow upwards, so bail on first non-present page */
		if (!pa[page_off + i])
			break;

		kbase_sync_single(kctx, pa[page_off + i], PAGE_SIZE, sync_fn);
	}

	/* Sync last page (if any) */
	if (page_count > 1 && pa[page_off + page_count - 1]) {
		size_t sz = ((start + size - 1) & ~PAGE_MASK) + 1;

		kbase_sync_single(kctx, pa[page_off + page_count - 1], sz,
			sync_fn);
	}

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	kbase_os_mem_map_unlock(kctx);
	return err;
}

mali_error kbase_sync_now(struct kbase_context *kctx, struct base_syncset *syncset)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	struct basep_syncset *sset;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != syncset);

	sset = &syncset->basep_sset;

	switch (sset->type) {
	case BASE_SYNCSET_OP_MSYNC:
		err = kbase_do_syncset(kctx, syncset, dma_sync_single_for_device);
		break;

	case BASE_SYNCSET_OP_CSYNC:
		err = kbase_do_syncset(kctx, syncset, dma_sync_single_for_cpu);
		break;

	default:
		dev_warn(kctx->kbdev->dev, "Unknown msync op %d\n", sset->type);
		break;
	}

	return err;
}

KBASE_EXPORT_TEST_API(kbase_sync_now)

/* vm lock must be held */
mali_error kbase_mem_free_region(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	mali_error err;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);
	BUG_ON(!mutex_is_locked(&kctx->reg_lock));
	err = kbase_gpu_munmap(kctx, reg);
	if (err) {
		dev_warn(reg->kctx->kbdev->dev, "Could not unmap from the GPU...\n");
		goto out;
	}

	if (kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_6367)) {
		/* Wait for GPU to flush write buffer before freeing physical pages */
		kbase_wait_write_flush(kctx);
	}

	/* This will also free the physical pages */
	kbase_free_alloced_region(reg);

 out:
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mem_free_region)

/**
 * @brief Free the region from the GPU and unregister it.
 *
 * This function implements the free operation on a memory segment.
 * It will loudly fail if called with outstanding mappings.
 */
mali_error kbase_mem_free(struct kbase_context *kctx, mali_addr64 gpu_addr)
{
	mali_error err = MALI_ERROR_NONE;
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	if (0 == gpu_addr) {
		dev_warn(kctx->kbdev->dev, "gpu_addr 0 is reserved for the ringbuffer and it's an error to try to free it using kbase_mem_free\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}
	kbase_gpu_vm_lock(kctx);

	if (gpu_addr >= BASE_MEM_COOKIE_BASE &&
	    gpu_addr < BASE_MEM_FIRST_FREE_ADDRESS) {
		int cookie = PFN_DOWN(gpu_addr - BASE_MEM_COOKIE_BASE);

		reg = kctx->pending_regions[cookie];
		if (!reg) {
			err = MALI_ERROR_FUNCTION_FAILED;
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
		if (!reg) {
			dev_warn(kctx->kbdev->dev, "kbase_mem_free called with nonexistent gpu_addr 0x%llX",
					gpu_addr);
			err = MALI_ERROR_FUNCTION_FAILED;
			goto out_unlock;
		}

		if ((reg->flags & KBASE_REG_ZONE_MASK) == KBASE_REG_ZONE_SAME_VA) {
			/* SAME_VA must be freed through munmap */
			dev_warn(kctx->kbdev->dev, "%s called on SAME_VA memory 0x%llX", __func__,
					gpu_addr);
			err = MALI_ERROR_FUNCTION_FAILED;
			goto out_unlock;
		}

		err = kbase_mem_free_region(kctx, reg);
	}

 out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return err;
}

KBASE_EXPORT_TEST_API(kbase_mem_free)

void kbase_update_region_flags(struct kbase_va_region *reg, unsigned long flags)
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

	if (flags & BASE_MEM_COHERENT_LOCAL)
		reg->flags |= KBASE_REG_SHARE_IN;
	else if (flags & BASE_MEM_COHERENT_SYSTEM)
		reg->flags |= KBASE_REG_SHARE_BOTH;
}
KBASE_EXPORT_TEST_API(kbase_update_region_flags)

int kbase_alloc_phy_pages_helper(
	struct kbase_mem_phy_alloc *alloc,
	size_t nr_pages_requested)
{
	KBASE_DEBUG_ASSERT(alloc);
	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);

	if (nr_pages_requested == 0)
		goto done; /*nothing to do*/

	kbase_atomic_add_pages(nr_pages_requested, &alloc->imported.kctx->used_pages);
	kbase_atomic_add_pages(nr_pages_requested, &alloc->imported.kctx->kbdev->memdev.used_pages);

	if (MALI_ERROR_NONE != kbase_mem_allocator_alloc(&alloc->imported.kctx->osalloc, nr_pages_requested, alloc->pages + alloc->nents))
		goto no_alloc;

	alloc->nents += nr_pages_requested;

	kbase_process_page_usage_inc(alloc->imported.kctx, nr_pages_requested);
done:
	return 0;

no_alloc:
	kbase_atomic_sub_pages(nr_pages_requested, &alloc->imported.kctx->used_pages);
	kbase_atomic_sub_pages(nr_pages_requested, &alloc->imported.kctx->kbdev->memdev.used_pages);

	return -ENOMEM;
}

int kbase_free_phy_pages_helper(
	struct kbase_mem_phy_alloc *alloc,
	size_t nr_pages_to_free)
{
	mali_bool syncback;
	phys_addr_t *start_free;

	KBASE_DEBUG_ASSERT(alloc);
	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);
	KBASE_DEBUG_ASSERT(alloc->nents >= nr_pages_to_free);

	/* early out if nothing to do */
	if (0 == nr_pages_to_free)
		return 0;

	start_free = alloc->pages + alloc->nents - nr_pages_to_free;

	syncback = (alloc->properties & KBASE_MEM_PHY_ALLOC_ACCESSED_CACHED) ? MALI_TRUE : MALI_FALSE;

	kbase_mem_allocator_free(&alloc->imported.kctx->osalloc,
				  nr_pages_to_free,
				  start_free,
				  syncback);

	alloc->nents -= nr_pages_to_free;
	kbase_process_page_usage_dec(alloc->imported.kctx, nr_pages_to_free);
	kbase_atomic_sub_pages(nr_pages_to_free, &alloc->imported.kctx->used_pages);
	kbase_atomic_sub_pages(nr_pages_to_free, &alloc->imported.kctx->kbdev->memdev.used_pages);

	return 0;
}

void kbase_mem_kref_free(struct kref *kref)
{
	struct kbase_mem_phy_alloc *alloc;

	alloc = container_of(kref, struct kbase_mem_phy_alloc, kref);

	switch (alloc->type) {
	case KBASE_MEM_TYPE_NATIVE: {
		KBASE_DEBUG_ASSERT(alloc->imported.kctx);
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
 #ifdef CONFIG_UMP
	case KBASE_MEM_TYPE_IMPORTED_UMP:
		ump_dd_release(alloc->imported.ump_handle);
		break;
#endif
#ifdef CONFIG_DMA_SHARED_BUFFER
	case KBASE_MEM_TYPE_IMPORTED_UMM:
		dma_buf_detach(alloc->imported.umm.dma_buf,
			       alloc->imported.umm.dma_attachment);
		dma_buf_put(alloc->imported.umm.dma_buf);
		break;
#endif
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
	if ((size_t) vsize > ((size_t) -1 / sizeof(*reg->alloc->pages)))
		goto out_term;

	KBASE_DEBUG_ASSERT(0 != vsize);

	if (MALI_ERROR_NONE != kbase_alloc_phy_pages_helper(reg->alloc, size))
		goto out_term;

	return 0;

 out_term:
	return -1;
}

KBASE_EXPORT_TEST_API(kbase_alloc_phy_pages)

mali_bool kbase_check_alloc_flags(unsigned long flags)
{
	/* Only known flags should be set. */
	if (flags & ~((1ul << BASE_MEM_FLAGS_NR_INPUT_BITS) - 1))
		return MALI_FALSE;

	/* At least one flag should be set */
	if (flags == 0)
		return MALI_FALSE;

	/* Either the GPU or CPU must be reading from the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD)) == 0)
		return MALI_FALSE;

	/* Either the GPU or CPU must be writing to the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_WR)) == 0)
		return MALI_FALSE;

	/* GPU cannot be writing to GPU executable memory and cannot grow the memory on page fault. */
	if ((flags & BASE_MEM_PROT_GPU_EX) && (flags & (BASE_MEM_PROT_GPU_WR | BASE_MEM_GROW_ON_GPF)))
		return MALI_FALSE;

	/* GPU should have at least read or write access otherwise there is no
	   reason for allocating. */
	if ((flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)) == 0)
		return MALI_FALSE;

	return MALI_TRUE;
}

/**
 * @brief Acquire the per-context region list lock
 */
void kbase_gpu_vm_lock(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_lock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_lock)

/**
 * @brief Release the per-context region list lock
 */
void kbase_gpu_vm_unlock(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_unlock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_unlock)
