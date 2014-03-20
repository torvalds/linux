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

#include <kbase/mali_kbase_config.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/common/mali_kbase_cache_policy.h>
#include <kbase/src/common/mali_kbase_hw.h>
#include <kbase/src/common/mali_kbase_gator.h>

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

/* Find allocated region enclosing range. */
struct kbase_va_region *kbase_region_tracker_find_region_enclosing_range(kbase_context *kctx, u64 start_pfn, size_t nr_pages)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;
	u64 end_pfn = start_pfn + nr_pages;

	rbnode = kctx->reg_rbtree.rb_node;

	while (rbnode) {
		u64 tmp_start_pfn, tmp_end_pfn;
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		tmp_start_pfn = reg->start_pfn;
		tmp_end_pfn = reg->start_pfn + kbase_reg_current_backed_size(reg);

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

/* Find allocated region enclosing free range. */
struct kbase_va_region *kbase_region_tracker_find_region_enclosing_range_free(kbase_context *kctx, u64 start_pfn, size_t nr_pages)
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
kbase_va_region *kbase_region_tracker_find_region_enclosing_address(kbase_context *kctx, mali_addr64 gpu_addr)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;

	KBASE_DEBUG_ASSERT(NULL != kctx);

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
kbase_va_region *kbase_region_tracker_find_region_base_address(kbase_context *kctx, mali_addr64 gpu_addr)
{
	u64 gpu_pfn = gpu_addr >> PAGE_SHIFT;
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(NULL != kctx);

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
static struct kbase_va_region *kbase_region_tracker_find_region_meeting_reqs(kbase_context *kctx, struct kbase_va_region *reg_reqs, size_t nr_pages, size_t align)
{
	struct rb_node *rbnode;
	struct kbase_va_region *reg;

	/* Note that this search is a linear search, as we do not have a target
	   address in mind, so does not benefit from the rbtree search */
	rbnode = rb_first(&(kctx->reg_rbtree));
	while (rbnode) {
		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if ((reg->nr_pages >= nr_pages) && (reg->flags & KBASE_REG_FREE) && kbase_region_tracker_match_zone(reg, reg_reqs)) {

			/* Check alignment */
			u64 start_pfn = (reg->start_pfn + align - 1) & ~(align - 1);
			if ((start_pfn >= reg->start_pfn) && (start_pfn <= (reg->start_pfn + reg->nr_pages - 1)) && ((start_pfn + nr_pages - 1) <= (reg->start_pfn + reg->nr_pages - 1)))
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
STATIC mali_error kbase_remove_va_region(kbase_context *kctx, struct kbase_va_region *reg)
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
static mali_error kbase_insert_va_region_nolock(kbase_context *kctx, struct kbase_va_region *new_reg, struct kbase_va_region *at_reg, u64 start_pfn, size_t nr_pages)
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
		struct kbase_va_region *new_front_reg = kbase_alloc_free_region(kctx, at_reg->start_pfn, start_pfn - at_reg->start_pfn, at_reg->flags & KBASE_REG_ZONE_MASK);
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
mali_error kbase_add_va_region(kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align)
{
	struct kbase_va_region *tmp;
	u64 gpu_pfn = addr >> PAGE_SHIFT;
	mali_error err = MALI_ERROR_NONE;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	if (!align)
		align = 1;

	/* must be a power of 2 */
	KBASE_DEBUG_ASSERT((align & (align - 1)) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);

	/* Path 1: Map a specific address. Find the enclosing region, which *must* be free. */
	if (gpu_pfn) {
		KBASE_DEBUG_ASSERT(!(gpu_pfn & (align - 1)));

		tmp = kbase_region_tracker_find_region_enclosing_range_free(kctx, gpu_pfn, nr_pages);
		if (!tmp) {
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Enclosing region not found: 0x%08llx gpu_pfn, %zu nr_pages", gpu_pfn, nr_pages);
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}

		if ((!kbase_region_tracker_match_zone(tmp, reg)) || (!(tmp->flags & KBASE_REG_FREE))) {
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Zone mismatch: %lu != %lu", tmp->flags & KBASE_REG_ZONE_MASK, reg->flags & KBASE_REG_ZONE_MASK);
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "!(tmp->flags & KBASE_REG_FREE): tmp->start_pfn=0x%llx tmp->flags=0x%lx tmp->nr_pages=0x%zx gpu_pfn=0x%llx nr_pages=0x%zx\n", tmp->start_pfn, tmp->flags, tmp->nr_pages, gpu_pfn, nr_pages);
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "in function %s (%p, %p, 0x%llx, 0x%zx, 0x%zx)\n", __func__, kctx, reg, addr, nr_pages, align);
			err = MALI_ERROR_OUT_OF_GPU_MEMORY;
			goto exit;
		}

		err = kbase_insert_va_region_nolock(kctx, reg, tmp, gpu_pfn, nr_pages);
		if (err) {
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Failed to insert va region");
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
static void kbase_region_tracker_ds_init(kbase_context *kctx, struct kbase_va_region *same_va_reg, struct kbase_va_region *exec_reg, struct kbase_va_region *custom_va_reg)
{
	kctx->reg_rbtree = RB_ROOT;
	kbase_region_tracker_insert(kctx, same_va_reg);

	/* exec and custom_va_reg doesn't always exist */
	if (exec_reg && custom_va_reg) {
		kbase_region_tracker_insert(kctx, exec_reg);
		kbase_region_tracker_insert(kctx, custom_va_reg);
	}
}

void kbase_region_tracker_term(kbase_context *kctx)
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
mali_error kbase_region_tracker_init(kbase_context *kctx)
{
	struct kbase_va_region *same_va_reg;
	struct kbase_va_region *exec_reg = NULL;
	struct kbase_va_region *custom_va_reg = NULL;
	size_t same_va_bits = sizeof(void*) * BITS_PER_BYTE;
	u64 custom_va_size = KBASE_REG_ZONE_CUSTOM_VA_SIZE;
	u64 gpu_va_limit = (1ULL << kctx->kbdev->gpu_props.mmu.va_bits) >> PAGE_SHIFT;

#ifdef CONFIG_64BIT
	if (is_compat_task())
		same_va_bits = 32;
#endif

	if (kctx->kbdev->gpu_props.mmu.va_bits < same_va_bits)
		return MALI_ERROR_FUNCTION_FAILED;

	/* all have SAME_VA */
	same_va_reg = kbase_alloc_free_region(kctx, 1, (1ULL << (same_va_bits - PAGE_SHIFT)) - 2, KBASE_REG_ZONE_SAME_VA);
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
		if( (KBASE_REG_ZONE_CUSTOM_VA_BASE + KBASE_REG_ZONE_CUSTOM_VA_SIZE) >= gpu_va_limit )
			custom_va_size = gpu_va_limit - KBASE_REG_ZONE_CUSTOM_VA_BASE;

		exec_reg = kbase_alloc_free_region(kctx, KBASE_REG_ZONE_EXEC_BASE, KBASE_REG_ZONE_EXEC_SIZE, KBASE_REG_ZONE_EXEC);
		if (!exec_reg) {
			kbase_free_alloced_region(same_va_reg);
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		custom_va_reg = kbase_alloc_free_region(kctx, KBASE_REG_ZONE_CUSTOM_VA_BASE, custom_va_size, KBASE_REG_ZONE_CUSTOM_VA);
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
	kbasep_mem_device *memdev;
	size_t max_shared_memory;
	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	max_shared_memory = (size_t)kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX);

	if (MALI_ERROR_NONE != kbase_mem_usage_init(&memdev->usage, max_shared_memory >> PAGE_SHIFT)) {
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* nothing to do, zero-inited when kbase_device was created */
	return MALI_ERROR_NONE;
}

void kbase_mem_halt(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_mem_term(kbase_device *kbdev)
{
	kbasep_mem_device *memdev;
	KBASE_DEBUG_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	kbase_mem_usage_term(&memdev->usage);
}

KBASE_EXPORT_TEST_API(kbase_mem_term)

mali_error kbase_mem_usage_init(struct kbasep_mem_usage *usage, size_t max_pages)
{
	KBASE_DEBUG_ASSERT(usage);

	atomic_set(&usage->cur_pages, 0);
	/* query the max page count */
	usage->max_pages = max_pages;

	return MALI_ERROR_NONE;
}

void kbase_mem_usage_term(kbasep_mem_usage *usage)
{
	KBASE_DEBUG_ASSERT(usage);
	/* No memory should be in use now */
	if (0 != atomic_read(&usage->cur_pages)) {
		printk(KERN_ERR "Pages in use! %d\n", atomic_read(&usage->cur_pages));
	}
	/* So any new alloc requests will fail */
	usage->max_pages = 0;
	/* So we printk on double term */
	atomic_set(&usage->cur_pages, INT_MAX);
}

mali_error kbase_mem_usage_request_pages(kbasep_mem_usage *usage, size_t nr_pages)
{
	int cur_pages;
	int old_cur_pages;

	KBASE_DEBUG_ASSERT(usage);
	KBASE_DEBUG_ASSERT(nr_pages);	/* 0 pages would be an error in the calling code */

	/*
	 * Fetch the initial cur_pages value
	 * each loop iteration below fetches
	 * it as part of the store attempt
	 */
	cur_pages = atomic_read(&usage->cur_pages);

	/* this check allows the simple if test in the loop below */
	if (usage->max_pages < nr_pages)
		goto usage_cap_exceeded;

	do {
		int new_cur_pages;
		/* enough pages to fullfill the request? */
		if (usage->max_pages - nr_pages < cur_pages) {
 usage_cap_exceeded:
			KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Memory usage cap has been reached:\n" "\t%lu pages currently used\n" "\t%lu pages usage cap\n" "\t%lu new pages requested\n" "\twould result in %lu pages over the cap\n", (unsigned long)cur_pages, (unsigned long)usage->max_pages, (unsigned long)nr_pages, (unsigned long)(cur_pages + nr_pages - usage->max_pages));
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		/* try to atomically commit the new count */
		old_cur_pages = cur_pages;
		new_cur_pages = cur_pages + nr_pages;
		cur_pages = atomic_cmpxchg(&usage->cur_pages, old_cur_pages,
					    new_cur_pages);
		/* cur_pages will be like old_cur_pages if there was no race */
	} while (cur_pages != old_cur_pages);

#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_total_alloc_pages_change((long long int)cur_pages);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */

	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbase_mem_usage_request_pages)

void kbase_mem_usage_release_pages(kbasep_mem_usage *usage, size_t nr_pages)
{
	int new_val;
	KBASE_DEBUG_ASSERT(usage);
	KBASE_DEBUG_ASSERT(nr_pages <= atomic_read(&usage->cur_pages));

	new_val = atomic_sub_return(nr_pages, &usage->cur_pages);
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_total_alloc_pages_change((long long int)new_val);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */
}

KBASE_EXPORT_TEST_API(kbase_mem_usage_release_pages)

/**
 * @brief Wait for GPU write flush - only in use for BASE_HW_ISSUE_6367
 *
 * Wait 1000 GPU clock cycles. This delay is known to give the GPU time to flush its write buffer.
 * @note If GPU resets occur then the counters are reset to zero, the delay may not be as expected.
 */
#ifndef CONFIG_MALI_NO_MALI
void kbase_wait_write_flush(kbase_context *kctx)
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
struct kbase_va_region *kbase_alloc_free_region(kbase_context *kctx, u64 start_pfn, size_t nr_pages, int zone)
{
	struct kbase_va_region *new_reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	/* zone argument should only contain zone related region flags */
	KBASE_DEBUG_ASSERT((zone & ~KBASE_REG_ZONE_MASK) == 0);
	KBASE_DEBUG_ASSERT(nr_pages > 0);
	KBASE_DEBUG_ASSERT(start_pfn + nr_pages <= (UINT64_MAX / PAGE_SIZE));	/* 64-bit address range is the max */

	new_reg = kzalloc(sizeof(*new_reg), GFP_KERNEL);

	if (!new_reg) {
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "kzalloc failed");
		return NULL;
	}

	new_reg->alloc = NULL; /* no alloc bound yet */
	new_reg->kctx = kctx;
	new_reg->flags = zone | KBASE_REG_FREE;

	new_reg->flags |= KBASE_REG_GROWABLE;

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
		KBASE_DEBUG_ASSERT(reg->alloc);
		kbase_mem_phy_alloc_put(reg->alloc);
		KBASE_DEBUG_CODE(
					/* To detect use-after-free in debug builds */
					reg->flags |= KBASE_REG_FREE);
	}
	kfree(reg);
}

KBASE_EXPORT_TEST_API(kbase_free_alloced_region)

void kbase_mmu_update(kbase_context *kctx)
{
	/* Use GPU implementation-defined caching policy. */
	u32 memattr = ASn_MEMATTR_IMPL_DEF_CACHE_POLICY;
	u32 pgd_high;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	pgd_high = sizeof(kctx->pgd) > 4 ? (kctx->pgd >> 32) : 0;

	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_LO), (kctx->pgd & ASn_TRANSTAB_ADDR_SPACE_MASK) | ASn_TRANSTAB_READ_INNER | ASn_TRANSTAB_ADRMODE_TABLE, kctx);

	/* Need to use a conditional expression to avoid "right shift count >= width of type"
	 * error when using an if statement - although the size_of condition is evaluated at compile
	 * time the unused branch is not removed until after it is type-checked and the error
	 * produced.
	 */
	pgd_high = sizeof(kctx->pgd) > 4 ? (kctx->pgd >> 32) : 0;

	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_HI), pgd_high, kctx);

	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_MEMATTR_LO), memattr, kctx);
	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_MEMATTR_HI), memattr, kctx);
	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UPDATE, kctx);
}

KBASE_EXPORT_TEST_API(kbase_mmu_update)

void kbase_mmu_disable(kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(NULL != kctx);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);

	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_LO), 0, kctx);
	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_HI), 0, kctx);
	kbase_reg_write(kctx->kbdev, MMU_AS_REG(kctx->as_nr, ASn_COMMAND), ASn_COMMAND_UPDATE, kctx);
}

KBASE_EXPORT_TEST_API(kbase_mmu_disable)

mali_error kbase_gpu_mmap(kbase_context *kctx, struct kbase_va_region *reg, mali_addr64 addr, size_t nr_pages, size_t align)
{
	mali_error err;
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);

	err = kbase_add_va_region(kctx, reg, addr, nr_pages, align);
	if (MALI_ERROR_NONE != err)
		return err;

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn, kbase_get_phy_pages(reg), kbase_reg_current_backed_size(reg) , reg->flags & ((1 << KBASE_REG_FLAGS_NR_BITS) - 1));
	if (MALI_ERROR_NONE != err)
		kbase_remove_va_region(kctx, reg);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_gpu_mmap)

mali_error kbase_gpu_munmap(kbase_context *kctx, struct kbase_va_region *reg)
{
	mali_error err;

	if (reg->start_pfn == 0)
		return MALI_ERROR_NONE;

	err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, kbase_reg_current_backed_size(reg));
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
		if (map->vma->vm_start <= uaddr && map->vma->vm_end >= uaddr + size)
			return map;
	}

	return NULL;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping_of_region)

struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping(kbase_context *kctx, mali_addr64 gpu_addr, unsigned long uaddr, size_t size)
{
	struct kbase_cpu_mapping *map = NULL;
	const struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_tracker_find_region_enclosing_address(kctx, gpu_addr);
	if (NULL != reg)
		map = kbasep_find_enclosing_cpu_mapping_of_region(reg, uaddr, size);

	kbase_gpu_vm_unlock(kctx);

	return map;
}

KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping)

static mali_error kbase_do_syncset(kbase_context *kctx, struct base_syncset *set, kbase_sync_kmem_fn sync_fn)
{
	mali_error err = MALI_ERROR_NONE;
	struct basep_syncset *sset = &set->basep_sset;
	struct kbase_va_region *reg;
	struct kbase_cpu_mapping *map;
	unsigned long start;
	size_t size;
	phys_addr_t base_phy_addr = 0;
	phys_addr_t *pa;
	u64 page_off, page_count;
	u64 i;
	unsigned int offset_within_page;
	void *base_virt_addr = 0;
	size_t area_size = 0;

	kbase_os_mem_map_lock(kctx);

	kbase_gpu_vm_lock(kctx);

	/* find the region where the virtual address is contained */
	reg = kbase_region_tracker_find_region_enclosing_address(kctx, sset->mem_handle);
	if (!reg) {
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Can't find region at VA 0x%016llX", sset->mem_handle);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED))
		goto out_unlock;

	start = (uintptr_t)sset->user_addr;
	size = (size_t)sset->size;

	map = kbasep_find_enclosing_cpu_mapping_of_region(reg, start, size);
	if (!map) {
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Can't find CPU mapping 0x%016lX for VA 0x%016llX", start, sset->mem_handle);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	offset_within_page = start & (PAGE_SIZE - 1);
	page_off = map->page_off + ((start - map->vma->vm_start) >> PAGE_SHIFT);
	page_count = ((size + offset_within_page + (PAGE_SIZE - 1)) & PAGE_MASK) >> PAGE_SHIFT;
	pa = kbase_get_phy_pages(reg);

	for (i = 0; i < page_count; i++) {
		u32 offset = start & (PAGE_SIZE - 1);
		phys_addr_t paddr = pa[page_off + i] + offset;
		size_t sz = MIN(((size_t) PAGE_SIZE - offset), size);

		if (paddr == base_phy_addr + area_size && start == ((uintptr_t) base_virt_addr + area_size)) {
			area_size += sz;
		} else if (area_size > 0) {
			sync_fn(base_phy_addr, base_virt_addr, area_size);
			area_size = 0;
		}

		if (area_size == 0) {
			base_phy_addr = paddr;
			base_virt_addr = (void *)(uintptr_t)start;
			area_size = sz;
		}

		start += sz;
		size -= sz;
	}

	if (area_size > 0)
		sync_fn(base_phy_addr, base_virt_addr, area_size);

	KBASE_DEBUG_ASSERT(size == 0);

 out_unlock:
	kbase_gpu_vm_unlock(kctx);
	kbase_os_mem_map_unlock(kctx);
	return err;
}

mali_error kbase_sync_now(kbase_context *kctx, struct base_syncset *syncset)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	struct basep_syncset *sset;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != syncset);

	sset = &syncset->basep_sset;

	switch (sset->type) {
	case BASE_SYNCSET_OP_MSYNC:
		err = kbase_do_syncset(kctx, syncset, kbase_sync_to_memory);
		break;

	case BASE_SYNCSET_OP_CSYNC:
		err = kbase_do_syncset(kctx, syncset, kbase_sync_to_cpu);
		break;

	default:
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Unknown msync op %d\n", sset->type);
		break;
	}

	return err;
}

KBASE_EXPORT_TEST_API(kbase_sync_now)

/* vm lock must be held */
mali_error kbase_mem_free_region(kbase_context *kctx, kbase_va_region *reg)
{
	mali_error err;
	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != reg);
	BUG_ON(!mutex_is_locked(&kctx->reg_lock));
	err = kbase_gpu_munmap(kctx, reg);
	if (err) {
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "Could not unmap from the GPU...\n");
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
mali_error kbase_mem_free(kbase_context *kctx, mali_addr64 gpu_addr)
{
	mali_error err = MALI_ERROR_NONE;
	struct kbase_va_region *reg;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	if (0 == gpu_addr) {
		KBASE_DEBUG_PRINT_WARN(KBASE_MEM, "gpu_addr 0 is reserved for the ringbuffer and it's an error to try to free it using kbase_mem_free\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}
	kbase_gpu_vm_lock(kctx);

	if (gpu_addr < PAGE_SIZE) {
		/* an OS specific cookie, ask the OS specific code to validate it */
		reg = kbase_lookup_cookie(kctx, gpu_addr);
		if (!reg) {
			err = MALI_ERROR_FUNCTION_FAILED;
			goto out_unlock;
		}

		/* ask to unlink the cookie as we'll free it */
		kbase_unlink_cookie(kctx, gpu_addr, reg);

		kbase_free_alloced_region(reg);
	} else {
		/* A real GPU va */

		/* Validate the region */
		reg = kbase_region_tracker_find_region_base_address(kctx, gpu_addr);
		if (!reg) {
			KBASE_DEBUG_ASSERT_MSG(0, "Trying to free nonexistent region\n 0x%llX", gpu_addr);
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
	KBASE_DEBUG_ASSERT((flags & ~((1 << BASE_MEM_FLAGS_NR_BITS) - 1)) == 0);

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

int kbase_alloc_phy_pages_helper(struct kbase_mem_phy_alloc * alloc, size_t nr_pages_requested)
{
	KBASE_DEBUG_ASSERT(alloc);
	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_NATIVE);
	KBASE_DEBUG_ASSERT(alloc->imported.kctx);

	if (0 == nr_pages_requested)
		goto done; /*nothing to do*/

	if (MALI_ERROR_NONE != kbase_mem_usage_request_pages(&alloc->imported.kctx->usage, nr_pages_requested))
		goto no_kctx_usage;

	if (MALI_ERROR_NONE != kbase_mem_usage_request_pages(&alloc->imported.kctx->kbdev->memdev.usage, nr_pages_requested))
		goto no_memdev_usage;

	if (MALI_ERROR_NONE != kbase_mem_allocator_alloc(&alloc->imported.kctx->osalloc, nr_pages_requested, alloc->pages + alloc->nents))
		goto no_alloc;

	alloc->nents += nr_pages_requested;

	kbase_process_page_usage_inc(alloc->imported.kctx, nr_pages_requested);
done:
	return 0;

no_alloc:
	kbase_mem_usage_release_pages(&alloc->imported.kctx->kbdev->memdev.usage, nr_pages_requested);
no_memdev_usage:
	kbase_mem_usage_release_pages(&alloc->imported.kctx->usage, nr_pages_requested);
no_kctx_usage:
	return -ENOMEM;
}

int kbase_free_phy_pages_helper(struct kbase_mem_phy_alloc * alloc, size_t nr_pages_to_free)
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

	syncback = alloc->accessed_cached ? MALI_TRUE : MALI_FALSE;

	kbase_mem_allocator_free(&alloc->imported.kctx->osalloc,
				  nr_pages_to_free,
				  start_free,
				  syncback);

	alloc->nents -= nr_pages_to_free;
	kbase_process_page_usage_dec(alloc->imported.kctx, nr_pages_to_free);
	kbase_mem_usage_release_pages(&alloc->imported.kctx->usage, nr_pages_to_free);
	kbase_mem_usage_release_pages(&alloc->imported.kctx->kbdev->memdev.usage, nr_pages_to_free);

	return 0;
}

void kbase_mem_kref_free(struct kref * kref)
{
	struct kbase_mem_phy_alloc * alloc;
	alloc = container_of(kref, struct kbase_mem_phy_alloc, kref);

	switch (alloc->type) {
		case KBASE_MEM_TYPE_NATIVE:
		{
			KBASE_DEBUG_ASSERT(alloc->imported.kctx);
			kbase_free_phy_pages_helper(alloc, alloc->nents);
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
			dma_buf_detach(alloc->imported.umm.dma_buf, alloc->imported.umm.dma_attachment);
			dma_buf_put(alloc->imported.umm.dma_buf);
			break;
#endif
		case KBASE_MEM_TYPE_TB:
		{
			void * tb;
			tb = alloc->imported.kctx->jctx.tb;
			kbase_device_trace_buffer_uninstall(alloc->imported.kctx);
			vfree(tb);
			break;
		}
		default:
			WARN(1, "Unexecpted free of type %d\n", alloc->type);
			break;
	}
	vfree(alloc);
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
	if (flags & ~((1 << BASE_MEM_FLAGS_NR_BITS) - 1))
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
void kbase_gpu_vm_lock(kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_lock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_lock)

/**
 * @brief Release the per-context region list lock
 */
void kbase_gpu_vm_unlock(kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx != NULL);
	mutex_unlock(&kctx->reg_lock);
}

KBASE_EXPORT_TEST_API(kbase_gpu_vm_unlock)
