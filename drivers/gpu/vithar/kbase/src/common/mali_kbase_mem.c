/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_mem.c
 * Base kernel memory APIs
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/common/mali_kbase_cache_policy.h>

typedef struct kbasep_memory_region_performance
{
	kbase_memory_performance cpu_performance;
	kbase_memory_performance gpu_performance;
} kbasep_memory_region_performance;

static mali_bool kbasep_allocator_order_list_create( osk_phy_allocator * allocators,
		kbasep_memory_region_performance *region_performance,
		int memory_region_count, osk_phy_allocator ***sorted_allocs, int allocator_order_count);

/* 
 * An iterator which uses one of the orders listed in kbase_phys_allocator_order enum to iterate over allocators array.
 */
typedef struct kbase_phys_allocator_iterator
{
	unsigned int cur_idx;
	kbase_phys_allocator_array * array;
	kbase_phys_allocator_order order;
} kbase_phys_allocator_iterator;


mali_error kbase_mem_init(kbase_device * kbdev)
{
	CSTD_UNUSED(kbdev);
	/* nothing to do, zero-inited when kbase_device was created */
	return MALI_ERROR_NONE;
}

void kbase_mem_halt(kbase_device * kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbase_mem_term(kbase_device * kbdev)
{
	u32 i;
	kbasep_mem_device * memdev;
	OSK_ASSERT(kbdev);

	memdev = &kbdev->memdev;

	for (i = 0; i < memdev->allocators.count; i++)
	{
		osk_phy_allocator_term(&memdev->allocators.allocs[i]);
	}
	osk_free(memdev->allocators.allocs);
	osk_free(memdev->allocators.sorted_allocs[0]);

	kbase_mem_usage_term(&memdev->usage);
}
KBASE_EXPORT_TEST_API(kbase_mem_term)

static mali_error kbase_phys_it_init(kbase_device * kbdev, kbase_phys_allocator_iterator * it, kbase_phys_allocator_order order)
{
	OSK_ASSERT(kbdev);
	OSK_ASSERT(it);

	if (!kbdev->memdev.allocators.count)
	{
		return MALI_ERROR_OUT_OF_MEMORY;
	}
	
	it->cur_idx = 0;
	it->array = &kbdev->memdev.allocators;
	it->order = order;

#if MALI_DEBUG
	it->array->it_bound = MALI_TRUE;
#endif /* MALI_DEBUG */

	return MALI_ERROR_NONE;
}

static void kbase_phys_it_term(kbase_phys_allocator_iterator * it)
{
	OSK_ASSERT(it);
	it->cur_idx = 0;
#if MALI_DEBUG
	it->array->it_bound = MALI_FALSE;
#endif /* MALI_DEBUG */
	it->array = NULL;
	return;
}

static osk_phy_allocator * kbase_phys_it_deref(kbase_phys_allocator_iterator * it)
{
	OSK_ASSERT(it);
	OSK_ASSERT(it->array);

	if (it->cur_idx < it->array->count)
	{
		return it->array->sorted_allocs[it->order][it->cur_idx];
	}
	else
	{
		return NULL;
	}
}

static osk_phy_allocator * kbase_phys_it_deref_and_advance(kbase_phys_allocator_iterator * it)
{
	osk_phy_allocator * alloc;

	OSK_ASSERT(it);
	OSK_ASSERT(it->array);

	alloc = kbase_phys_it_deref(it);
	if (alloc)
	{
		it->cur_idx++;
	}
	return alloc;
}

/*
 * Page free helper.
 * Handles that commit objects tracks the pages we free
 */
static void kbase_free_phy_pages_helper(kbase_va_region * reg, u32 nr_pages);

mali_error kbase_mem_usage_init(kbasep_mem_usage * usage, u32 max_pages)
{
	OSK_ASSERT(usage);
	osk_atomic_set(&usage->cur_pages, 0);
	/* query the max page count */
	usage->max_pages = max_pages;

	return MALI_ERROR_NONE;
}

void kbase_mem_usage_term(kbasep_mem_usage * usage)
{
	OSK_ASSERT(usage);
	/* No memory should be in use now */
	OSK_ASSERT(0 == osk_atomic_get(&usage->cur_pages));
	/* So any new alloc requests will fail */
	usage->max_pages = 0;
	/* So we assert on double term */
	osk_atomic_set(&usage->cur_pages, U32_MAX);
}

mali_error kbase_mem_usage_request_pages(kbasep_mem_usage *usage, u32 nr_pages)
{
	u32 cur_pages;
	u32 old_cur_pages;

	OSK_ASSERT(usage);
	OSK_ASSERT(nr_pages); /* 0 pages would be an error in the calling code */

	/* 
	 * Fetch the initial cur_pages value
	 * each loop iteration below fetches
	 * it as part of the store attempt
	 */
	cur_pages = osk_atomic_get(&usage->cur_pages);

	/* this check allows the simple if test in the loop below */
	if (usage->max_pages < nr_pages)
	{
		goto usage_cap_exceeded;
	}

	do
	{
		u32 new_cur_pages;

		/* enough pages to fullfill the request? */
		if (usage->max_pages - nr_pages < cur_pages)
		{
usage_cap_exceeded:
			OSK_PRINT_WARN( OSK_BASE_MEM,
			                "Memory usage cap has been reached:\n"
			                "\t%d pages currently used\n"
			                "\t%d pages usage cap\n"
			                "\t%d new pages requested\n"
			                "\twould result in %d pages over the cap\n",
			                cur_pages,
			                usage->max_pages,
			                nr_pages,
			                cur_pages + nr_pages - usage->max_pages
			);
			return MALI_ERROR_OUT_OF_MEMORY;
		}

		/* try to atomically commit the new count */
		old_cur_pages = cur_pages;
		new_cur_pages = cur_pages + nr_pages;
		cur_pages = osk_atomic_compare_and_swap(&usage->cur_pages, old_cur_pages, new_cur_pages);
		/* cur_pages will be like old_cur_pages if there was no race */
	} while (cur_pages != old_cur_pages);

	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbase_mem_usage_release_pages)
void kbase_mem_usage_release_pages(kbasep_mem_usage * usage, u32 nr_pages)
{
	OSK_ASSERT(usage);
	OSK_ASSERT(nr_pages <= osk_atomic_get(&usage->cur_pages));

	osk_atomic_sub(&usage->cur_pages, nr_pages);
}

#if BASE_HW_ISSUE_6367
#ifdef CONFIG_VITHAR_RT_PM
#include <linux/delay.h>
#endif

/**
 * @brief Wait for GPU write flush.
 *
 * Wait 1000 GPU clock cycles. This delay is known to give the GPU time to flush its write buffer.
 */
#ifdef CONFIG_VITHAR_RT_PM
static void kbase_wait_write_flush(struct kbase_context *kctx)
{
	/* 1000 GPU clock @ the minimum 100Mhz is 10us */
	udelay(10);
}
#else
#if MALI_NO_MALI
static void kbase_wait_write_flush(struct kbase_context *kctx) { }
#else
static void kbase_wait_write_flush(struct kbase_context *kctx)
{
	u32 base_count = 0;
	kbase_reg_write(kctx->kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CYCLE_COUNT_START, NULL);
	while( MALI_TRUE )
	{
		u32 new_count;
		new_count = kbase_reg_read(kctx->kbdev, GPU_CONTROL_REG(CYCLE_COUNT_LO), NULL);
		/* First time around, just store the count. */
		if( base_count == 0 )
		{
			base_count = new_count;
			continue;
		}

		/* No need to handle wrapping, unsigned maths works for this. */
		if( (new_count - base_count) > 1000 )
		{
			break;
		}
	}
}
#endif
#endif
#endif

/**
 * @brief Check the zone compatibility of two regions.
 */
STATIC int kbase_match_zone(struct kbase_va_region *reg1, struct kbase_va_region *reg2)
{
	return ((reg1->flags & KBASE_REG_ZONE_MASK) == (reg2->flags & KBASE_REG_ZONE_MASK));
}
KBASE_EXPORT_TEST_API(kbase_match_zone)

/**
 * @brief Allocate a free region object.
 *
 * The allocated object is not part of any list yet, and is flagged as
 * KBASE_REG_FREE. No mapping is allocated yet.
 *
 * zone is KBASE_REG_ZONE_TMEM or KBASE_REG_ZONE_PMEM.
 *
 */
struct kbase_va_region *kbase_alloc_free_region(struct kbase_context *kctx, u64 start_pfn, u32 nr_pages, u32 zone)
{
	struct kbase_va_region *new_reg;

	OSK_ASSERT(kctx != NULL);

	/* zone argument should only contain zone related region flags */
	OSK_ASSERT((zone & ~KBASE_REG_ZONE_MASK) == 0);
	OSK_ASSERT(nr_pages > 0);
	OSK_ASSERT( start_pfn + nr_pages <= (UINT64_MAX / OSK_PAGE_SIZE) ); /* 64-bit address range is the max */

	new_reg = osk_calloc(sizeof(*new_reg));
	if (!new_reg) 
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "calloc failed");
		return NULL;
	}

	new_reg->kctx = kctx;
	new_reg->flags = zone | KBASE_REG_FREE;

	if ( KBASE_REG_ZONE_TMEM == zone )
	{
		new_reg->flags |= KBASE_REG_GROWABLE;
	}

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;
	OSK_DLIST_INIT(&new_reg->map_list);
	new_reg->root_commit.allocator = NULL;
	new_reg->last_commit = &new_reg->root_commit;

	return new_reg;
}
KBASE_EXPORT_TEST_API(kbase_alloc_free_region)

/**
 * @brief Free a region object.
 *
 * The described region must be freed of any mapping.
 *
 * If the region is not flagged as KBASE_REG_FREE, the destructor
 * kbase_free_phy_pages() will be called.
 */
void kbase_free_alloced_region(struct kbase_va_region *reg)
{
	OSK_ASSERT(NULL != reg);
	OSK_ASSERT(OSK_DLIST_IS_EMPTY(&reg->map_list));
	if (!(reg->flags & KBASE_REG_FREE))
	{
		kbase_free_phy_pages(reg);
		OSK_DEBUG_CODE(
			 /* To detect use-after-free in debug builds */
			reg->flags |= KBASE_REG_FREE
		);
	}
	osk_free(reg);
}
KBASE_EXPORT_TEST_API(kbase_free_alloced_region)

/**
 * @brief Insert a region object in the global list.
 *
 * The region new_reg is inserted at start_pfn by replacing at_reg
 * partially or completely. at_reg must be a KBASE_REG_FREE region
 * that contains start_pfn and at least nr_pages from start_pfn.  It
 * must be called with the context region lock held. Internal use
 * only.
 */
static mali_error kbase_insert_va_region_nolock(struct kbase_context *kctx,
					 struct kbase_va_region *new_reg,
					 struct kbase_va_region *at_reg,
					 u64 start_pfn, u32 nr_pages)
{
	struct kbase_va_region *new_front_reg;
	mali_error err = MALI_ERROR_NONE;

	/* Must be a free region */
	OSK_ASSERT((at_reg->flags & KBASE_REG_FREE) != 0);
	/* start_pfn should be contained within at_reg */
	OSK_ASSERT((start_pfn >= at_reg->start_pfn) && (start_pfn < at_reg->start_pfn + at_reg->nr_pages));
	/* at least nr_pages from start_pfn should be contained within at_reg */
	OSK_ASSERT(start_pfn + nr_pages <= at_reg->start_pfn + at_reg->nr_pages );

	new_reg->start_pfn = start_pfn;
	new_reg->nr_pages = nr_pages;

	/* Trivial replacement case */
	if (at_reg->start_pfn == start_pfn && at_reg->nr_pages == nr_pages)
	{
		OSK_DLIST_INSERT_BEFORE(&kctx->reg_list, new_reg, at_reg, struct kbase_va_region, link);
		OSK_DLIST_REMOVE(&kctx->reg_list, at_reg, link);
		kbase_free_alloced_region(at_reg);
	}
	/* Begin case */
	else if (at_reg->start_pfn == start_pfn)
	{
		at_reg->start_pfn += nr_pages;
		OSK_ASSERT(at_reg->nr_pages >= nr_pages);
		at_reg->nr_pages -= nr_pages;

		OSK_DLIST_INSERT_BEFORE(&kctx->reg_list, new_reg, at_reg, struct kbase_va_region, link);
	}
	/* End case */
	else if ((at_reg->start_pfn + at_reg->nr_pages) == (start_pfn + nr_pages))
	{
		at_reg->nr_pages -= nr_pages;

		OSK_DLIST_INSERT_AFTER(&kctx->reg_list, new_reg, at_reg, struct kbase_va_region, link);
	}
	/* Middle of the road... */
	else
	{
		new_front_reg = kbase_alloc_free_region(kctx, at_reg->start_pfn,
							start_pfn - at_reg->start_pfn,
							at_reg->flags & KBASE_REG_ZONE_MASK);
		if (new_front_reg)
		{
			at_reg->nr_pages -= nr_pages + new_front_reg->nr_pages;
			at_reg->start_pfn = start_pfn + nr_pages;

			OSK_DLIST_INSERT_BEFORE(&kctx->reg_list, new_front_reg, at_reg, struct kbase_va_region, link);
			OSK_DLIST_INSERT_BEFORE(&kctx->reg_list, new_reg, at_reg, struct kbase_va_region, link);
		}
		else
		{
			err = MALI_ERROR_OUT_OF_MEMORY;
		}
	}

	return err;
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
	struct kbase_va_region *prev;
	struct kbase_va_region *next;
	int merged_front = 0;
	int merged_back = 0;
	mali_error err = MALI_ERROR_NONE;

	prev = OSK_DLIST_PREV(reg, struct kbase_va_region, link);
	if (!OSK_DLIST_IS_VALID(prev, link))
	{
		prev = NULL;
	}
	
	next = OSK_DLIST_NEXT(reg, struct kbase_va_region, link);
	OSK_ASSERT(NULL != next);
	if (!OSK_DLIST_IS_VALID(next, link))
	{
		next = NULL;
	}

	/* Try to merge with front first */
	if (prev && (prev->flags & KBASE_REG_FREE) && kbase_match_zone(prev, reg))
	{
		/* We're compatible with the previous VMA, merge with it */
		OSK_DLIST_REMOVE(&kctx->reg_list, reg, link);
		prev->nr_pages += reg->nr_pages;
		reg = prev;
		merged_front = 1;
	}

	/* Try to merge with back next */
	if (next && (next->flags & KBASE_REG_FREE) && kbase_match_zone(next, reg))
	{
		/* We're compatible with the next VMA, merge with it */
		next->start_pfn = reg->start_pfn;
		next->nr_pages += reg->nr_pages;
		OSK_DLIST_REMOVE(&kctx->reg_list, reg, link);

		if (merged_front)
		{
			/* we already merged with prev, free it */
			kbase_free_alloced_region(prev);
		}

		merged_back = 1;
	}

	if (!(merged_front || merged_back))
	{
		/*
		 * We didn't merge anything. Add a new free
		 * placeholder and remove the original one.
		 */
		struct kbase_va_region *free_reg;

		free_reg = kbase_alloc_free_region(kctx, reg->start_pfn, reg->nr_pages, reg->flags & KBASE_REG_ZONE_MASK);
		if (!free_reg)
		{
			err = MALI_ERROR_OUT_OF_MEMORY;
			goto out;
		}

		OSK_DLIST_INSERT_BEFORE(&kctx->reg_list, free_reg, reg, struct kbase_va_region, link);
		OSK_DLIST_REMOVE(&kctx->reg_list, reg, link);
	}

out:
	return err;
}
KBASE_EXPORT_TEST_API(kbase_remove_va_region)

/**
 * @brief Add a region to the global list.
 *
 * Add reg to the global list, according to its zone. If addr is
 * non-null, this address is used directly (as in the PMEM
 * case). Alignment can be enforced by specifying a number of pages
 * (which *must* be a power of 2).
 *
 * Context region list lock must be held.
 *
 * Mostly used by kbase_gpu_mmap(), but also useful to register the
 * ring-buffer region.
 */
mali_error kbase_add_va_region(struct kbase_context *kctx,
			       struct kbase_va_region *reg,
			       mali_addr64 addr, u32 nr_pages,
			       u32 align)
{
	struct kbase_va_region *tmp;
	u64 gpu_pfn = addr >> OSK_PAGE_SHIFT;
	mali_error err = MALI_ERROR_NONE;

	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != reg);

	if (!align)
	{
		align = 1;
	}

	/* must be a power of 2 */
	OSK_ASSERT((align & (align - 1)) == 0);
	OSK_ASSERT( nr_pages > 0 );

	if (gpu_pfn)
	{
		OSK_ASSERT(!(gpu_pfn & (align - 1)));

		/*
		 * So we want a specific address. Parse the list until
		 * we find the enclosing region, which *must* be free.
		 */
		OSK_DLIST_FOREACH(&kctx->reg_list, struct kbase_va_region, link, tmp)
		{
			if (tmp->start_pfn <= gpu_pfn &&
			    (tmp->start_pfn + tmp->nr_pages) >= (gpu_pfn + nr_pages))
			{
				/* We have the candidate */
				if (!kbase_match_zone(tmp, reg))
				{
					/* Wrong zone, fail */
					err = MALI_ERROR_OUT_OF_GPU_MEMORY;
					OSK_PRINT_WARN(OSK_BASE_MEM, "Zone mismatch: %d != %d", tmp->flags & KBASE_REG_ZONE_MASK, reg->flags & KBASE_REG_ZONE_MASK);
					goto out;
				}

				if (!(tmp->flags & KBASE_REG_FREE))
				{
					OSK_PRINT_WARN(OSK_BASE_MEM, "!(tmp->flags & KBASE_REG_FREE): tmp->start_pfn=0x%llx tmp->flags=0x%x tmp->nr_pages=0x%x gpu_pfn=0x%llx nr_pages=0x%x\n",
							tmp->start_pfn, tmp->flags, tmp->nr_pages, gpu_pfn, nr_pages);
					OSK_PRINT_WARN(OSK_BASE_MEM, "in function %s (%p, %p, 0x%llx, 0x%x, 0x%x)\n", __func__,
							kctx,reg,addr, nr_pages, align);
					/* Busy, fail */
					err = MALI_ERROR_OUT_OF_GPU_MEMORY;
					goto out;
				}

				err = kbase_insert_va_region_nolock(kctx, reg, tmp, gpu_pfn, nr_pages);
				if (err) OSK_PRINT_WARN(OSK_BASE_MEM, "Failed to insert va region");
				goto out;
			}
		}

		err = MALI_ERROR_OUT_OF_GPU_MEMORY;
		OSK_PRINT_WARN(OSK_BASE_MEM, "Out of mem");
		goto out;
	}

	/* Find the first free region that accomodates our requirements */
	OSK_DLIST_FOREACH(&kctx->reg_list, struct kbase_va_region, link, tmp)
	{
		if (tmp->nr_pages >= nr_pages &&
		    (tmp->flags & KBASE_REG_FREE) &&
		    kbase_match_zone(tmp, reg))
		{
			/* Check alignment */
			u64 start_pfn;

			start_pfn = (tmp->start_pfn + align - 1) & ~(align - 1);
			if ((tmp->start_pfn + tmp->nr_pages - start_pfn) < nr_pages)
			{
				continue;
			}

			/* It fits, let's use it */
			
			err = kbase_insert_va_region_nolock(kctx, reg, tmp, start_pfn, nr_pages);
			
			goto out;
		}
	}

	err = MALI_ERROR_OUT_OF_GPU_MEMORY;
out:
	return err;
}
KBASE_EXPORT_TEST_API(kbase_add_va_region)


void kbase_mmu_update(struct kbase_context *kctx)
{
#if BASE_HW_ISSUE_6494
	/* Force all resources to be cached. */
	u32 memattr = 0x4F4F4F4F;
#else
	/* Use GPU implementation-defined caching policy. */
	u32 memattr = 0x48484848;
#endif

	OSK_ASSERT(NULL != kctx);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	OSK_ASSERT( kctx->as_nr != KBASEP_AS_NR_INVALID );

	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_LO),
	                (kctx->pgd & 0xfffff000) | (1ul << 2) | 3, kctx);
	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_HI),
	                kctx->pgd >> 32, kctx);

	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_MEMATTR_LO),
	                memattr, kctx);
	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_MEMATTR_HI),
	                memattr, kctx);
	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_COMMAND),
	                ASn_COMMAND_UPDATE, kctx);
}
KBASE_EXPORT_TEST_API(kbase_mmu_update)

void kbase_mmu_disable (kbase_context *kctx)
{
	OSK_ASSERT(NULL != kctx);
	/* ASSERT that the context has a valid as_nr, which is only the case
	 * when it's scheduled in.
	 *
	 * as_nr won't change because the caller has the runpool_irq lock */
	OSK_ASSERT( kctx->as_nr != KBASEP_AS_NR_INVALID );

	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_LO),
	                0, kctx);
	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_TRANSTAB_HI),
	                0, kctx);
	kbase_reg_write(kctx->kbdev,
	                MMU_AS_REG(kctx->as_nr, ASn_COMMAND),
	                ASn_COMMAND_UPDATE, kctx);
}
KBASE_EXPORT_TEST_API(kbase_mmu_disable)
/**
 * @brief Register region and map it on the GPU.
 *
 * Call kbase_add_va_region() and map the region on the GPU.
 */
mali_error kbase_gpu_mmap(struct kbase_context *kctx,
                          struct kbase_va_region *reg,
                          mali_addr64 addr, u32 nr_pages,
                          u32 align)
{
	mali_error err;
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != reg);
	if ((err = kbase_add_va_region(kctx, reg, addr, nr_pages, align)))
	{
		return err;
	}

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn,
	                             kbase_get_phy_pages(reg),
	                             reg->nr_alloc_pages, reg->flags);
	if(MALI_ERROR_NONE != err)
	{
		kbase_remove_va_region(kctx, reg);
	}

	return err;
}
KBASE_EXPORT_TEST_API(kbase_gpu_mmap)

/**
 * @brief Remove the region from the GPU and unregister it.
 *
 * Not exported for the time being. Must be called with context lock
 * held.
 */
static mali_error kbase_gpu_munmap(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	mali_error err;

	if(reg->start_pfn == 0 )
	{
		return MALI_ERROR_NONE;
	}
	err = kbase_mmu_teardown_pages(kctx, reg->start_pfn, reg->nr_alloc_pages);
	if(MALI_ERROR_NONE != err)
	{
		return err;
	}

	err = kbase_remove_va_region(kctx, reg);
	return err;
}

kbase_va_region *kbase_region_lookup(kbase_context *kctx, mali_addr64 gpu_addr)
{
	kbase_va_region *tmp;
	u64 gpu_pfn = gpu_addr >> OSK_PAGE_SHIFT;
	OSK_ASSERT(NULL != kctx);
	OSK_DLIST_FOREACH(&kctx->reg_list, kbase_va_region, link, tmp)
	{
		if (gpu_pfn >= tmp->start_pfn && (gpu_pfn < tmp->start_pfn + tmp->nr_pages))
		{
			return tmp;
		}
	}

	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_region_lookup)

/**
 * @brief Check that a pointer is actually a valid region.
 *
 * Not exported for the time being. Must be called with context lock
 * held.
 */
STATIC struct kbase_va_region *kbase_validate_region(struct kbase_context *kctx, mali_addr64 gpu_addr)
{
	struct kbase_va_region *tmp;
	u64 gpu_pfn = gpu_addr >> OSK_PAGE_SHIFT;
	OSK_ASSERT(NULL != kctx);
	OSK_DLIST_FOREACH(&kctx->reg_list, struct kbase_va_region, link, tmp)
	{
		if (tmp->start_pfn == gpu_pfn)
		{
			return tmp;
		}
	}

	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_validate_region)

/**
 * @brief Find a mapping keyed with ptr in region reg
 */
STATIC struct kbase_cpu_mapping *kbase_find_cpu_mapping(struct kbase_va_region *reg,
							const void *ptr)
{
	struct kbase_cpu_mapping *map;
	OSK_ASSERT(NULL != reg);
	OSK_DLIST_FOREACH(&reg->map_list, struct kbase_cpu_mapping, link, map)
	{
		if (map->private == ptr)
		{
			return map;
		}
	}

	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_find_cpu_mapping)

STATIC struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping_of_region(
                                      const struct kbase_va_region *reg,
                                      osk_virt_addr                 uaddr,
                                      size_t                        size)
{
	struct kbase_cpu_mapping *map;

	OSK_ASSERT(NULL != reg);

	if ((uintptr_t)uaddr + size < (uintptr_t)uaddr) /* overflow check */
	{
		return NULL;
	}

	OSK_DLIST_FOREACH(&reg->map_list, struct kbase_cpu_mapping, link, map)
	{
		if (map->uaddr <= uaddr &&
		    ((uintptr_t)map->uaddr + (map->nr_pages << OSK_PAGE_SHIFT)) >= ((uintptr_t)uaddr + size))
		{
			return map;
		}
	}

	return NULL;
}
KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping_of_region)

static void kbase_dump_mappings(struct kbase_va_region *reg)
{
	struct kbase_cpu_mapping *map;

	OSK_ASSERT(NULL != reg);

	OSK_DLIST_FOREACH(&reg->map_list, struct kbase_cpu_mapping, link, map)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "uaddr %p nr_pages %d page_off %016llx vma %p\n",
		       map->uaddr, map->nr_pages,
		       map->page_off, map->private);
	}
}

/**
 * @brief Delete a mapping keyed with ptr in region reg
 */
mali_error kbase_cpu_free_mapping(struct kbase_va_region *reg, const void *ptr)
{
	struct kbase_cpu_mapping *map;
	mali_error err = MALI_ERROR_NONE;
	OSK_ASSERT(NULL != reg);
	map = kbase_find_cpu_mapping(reg, ptr);
	if (!map)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "Freeing unknown mapping %p in region %p\n", ptr, (void*)reg);
		kbase_dump_mappings(reg);
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	OSK_DLIST_REMOVE(&reg->map_list, map, link);
	osk_free(map);
out:
	return err;
}
KBASE_EXPORT_TEST_API(kbase_cpu_free_mapping)

struct kbase_cpu_mapping *kbasep_find_enclosing_cpu_mapping(
                               struct kbase_context *kctx,
                               mali_addr64           gpu_addr,
                               osk_virt_addr         uaddr,
                               size_t                size )
{
	struct kbase_cpu_mapping     *map = NULL;
	const struct kbase_va_region *reg;

	OSKP_ASSERT( kctx != NULL );
	kbase_gpu_vm_lock(kctx);

	reg = kbase_region_lookup( kctx, gpu_addr );
	if ( NULL != reg )
	{
		map = kbasep_find_enclosing_cpu_mapping_of_region( reg, uaddr, size);
	}

	kbase_gpu_vm_unlock(kctx);

	return map;
}
KBASE_EXPORT_TEST_API(kbasep_find_enclosing_cpu_mapping)

static mali_error kbase_do_syncset(struct kbase_context *kctx, base_syncset *set,
			     osk_sync_kmem_fn sync_fn)
{
	mali_error err = MALI_ERROR_NONE;
	struct basep_syncset *sset = &set->basep_sset;
	struct kbase_va_region *reg;
	struct kbase_cpu_mapping *map;
	osk_phy_addr *pa;
	u64 page_off, page_count, size_in_pages;
	osk_virt_addr start;
	size_t size;
	u64 i;
	u32 offset_within_page;
	osk_phy_addr base_phy_addr = 0;
	osk_virt_addr base_virt_addr = 0;
	size_t area_size = 0;

	kbase_os_mem_map_lock(kctx);

	kbase_gpu_vm_lock(kctx);

	/* find the region where the virtual address is contained */
	reg = kbase_region_lookup(kctx, sset->mem_handle);
	if (!reg)
	{
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	if (!(reg->flags & KBASE_REG_CPU_CACHED))
	{
		goto out_unlock;
	}

	start = (osk_virt_addr)(uintptr_t)sset->user_addr;
	size = sset->size;

	map = kbasep_find_enclosing_cpu_mapping_of_region(reg, start, size);
	if (!map)
	{
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out_unlock;
	}

	offset_within_page = (uintptr_t)start & (OSK_PAGE_SIZE - 1);
	size_in_pages = (size + offset_within_page + (OSK_PAGE_SIZE - 1)) & OSK_PAGE_MASK;
	page_off = map->page_off + (((uintptr_t)start - (uintptr_t)map->uaddr) >> OSK_PAGE_SHIFT);
	page_count = (size_in_pages >> OSK_PAGE_SHIFT);
	pa = kbase_get_phy_pages(reg);

	for (i = 0; i < page_count; i++)
	{
		u32 offset = (uintptr_t)start & (OSK_PAGE_SIZE - 1);
		osk_phy_addr paddr = pa[page_off + i] + offset;
		size_t sz = OSK_MIN(((size_t)OSK_PAGE_SIZE - offset), size);

		if (paddr == base_phy_addr + area_size &&
		    start == (osk_virt_addr)((uintptr_t)base_virt_addr + area_size))
		{
			area_size += sz;
		}
		else if (area_size > 0)
		{
			sync_fn(base_phy_addr, base_virt_addr, area_size);
			area_size = 0;
		}

		if (area_size == 0)
		{
			base_phy_addr = paddr;
			base_virt_addr = start;
			area_size = sz;
		}

		start = (osk_virt_addr)((uintptr_t)start + sz);
		size -= sz;
	}
	
	if (area_size > 0)
	{
		sync_fn(base_phy_addr, base_virt_addr, area_size);
	}

	OSK_ASSERT(size == 0);

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	kbase_os_mem_map_unlock(kctx);
	return err;
}

static mali_error kbase_sync_to_memory(kbase_context *kctx, base_syncset *syncset)
{
	return kbase_do_syncset(kctx, syncset, osk_sync_to_memory);
}

static mali_error kbase_sync_to_cpu(kbase_context *kctx, base_syncset *syncset)
{
	return kbase_do_syncset(kctx, syncset, osk_sync_to_cpu);
}

mali_error kbase_sync_now(kbase_context *kctx, base_syncset *syncset)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	struct basep_syncset *sset;

	OSK_ASSERT( NULL != kctx );
	OSK_ASSERT( NULL != syncset );

	sset = &syncset->basep_sset;

	switch(sset->type)
	{
	case BASE_SYNCSET_OP_MSYNC:
		err = kbase_sync_to_memory(kctx, syncset);
		break;
		
	case BASE_SYNCSET_OP_CSYNC:
		err = kbase_sync_to_cpu(kctx, syncset);
		break;

	default:
		OSK_PRINT_WARN(OSK_BASE_MEM, "Unknown msync op %d\n", sset->type);
		break;
	}

	return err;
}
KBASE_EXPORT_TEST_API(kbase_sync_now)

void kbase_pre_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr)
{
	u32 i;

	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != syncsets);

	for (i = 0; i < nr; i++) 
	{
		u8 type = syncsets[i].basep_sset.type;

		switch(type)
		{
		case BASE_SYNCSET_OP_MSYNC:
			kbase_sync_to_memory(kctx, &syncsets[i]);
			break;

		case BASE_SYNCSET_OP_CSYNC:
			continue;

		default:
			OSK_PRINT_WARN(OSK_BASE_MEM, "Unknown msync op %d\n", type);
			break;
		}
	}
}
KBASE_EXPORT_TEST_API(kbase_pre_job_sync)

void kbase_post_job_sync(kbase_context *kctx, base_syncset *syncsets, u32 nr)
{
	u32 i;

	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != syncsets);

	for (i = 0; i < nr; i++) 
	{
		struct basep_syncset *sset = &syncsets[i].basep_sset;
		switch(sset->type)
		{
		case BASE_SYNCSET_OP_CSYNC:
			kbase_sync_to_cpu(kctx, &syncsets[i]);
			break;

		case BASE_SYNCSET_OP_MSYNC:
			continue;

		default:
			OSK_PRINT_WARN(OSK_BASE_MEM, "Unknown msync op %d\n", sset->type);
			break;
		}
	}
}
KBASE_EXPORT_TEST_API(kbase_post_job_sync)

/* vm lock must be held */
mali_error kbase_mem_free_region(struct kbase_context *kctx,
				 kbase_va_region *reg)
{
	mali_error err;
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != reg);

	if (!OSK_DLIST_IS_EMPTY(&reg->map_list))
	{
		/*
		 * We still have mappings, can't free
		 * memory. This also handles the race
		 * condition with the unmap code (see
		 * kbase_cpu_vm_close()).
		 */
		OSK_PRINT_WARN(OSK_BASE_MEM, "Pending CPU mappings, not freeing memory!\n");
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	err = kbase_gpu_munmap(kctx, reg);
	if (err)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "Could not unmap from the GPU...\n");
		goto out;
	}

#if BASE_HW_ISSUE_6367
	/* Wait for GPU to flush write buffer before freeing physical pages */
	kbase_wait_write_flush(kctx);
#endif

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
	
	OSK_ASSERT(kctx != NULL);

	if (0 == gpu_addr)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "gpu_addr 0 is reserved for the ringbuffer and it's an error to try to free it using kbase_mem_free\n");
		return MALI_ERROR_FUNCTION_FAILED;
	}
	kbase_gpu_vm_lock(kctx);

	if (gpu_addr < OSK_PAGE_SIZE)
	{
		/* an OS specific cookie, ask the OS specific code to validate it */
		reg = kbase_lookup_cookie(kctx, gpu_addr);
		if (!reg)
		{
			err = MALI_ERROR_FUNCTION_FAILED;
			goto out_unlock;
		}

		/* ask to unlink the cookie as we'll free it */
		kbase_unlink_cookie(kctx, gpu_addr, reg);

		kbase_free_alloced_region(reg);
	}
	else
	{
		/* A real GPU va */

		/* Validate the region */
		reg = kbase_validate_region(kctx, gpu_addr);
		if (!reg)
		{
			OSK_ASSERT_MSG(0, "Trying to free nonexistent region\n 0x%llX", gpu_addr);
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

void kbase_update_region_flags(struct kbase_va_region *reg, u32 flags, mali_bool is_growable)
{
	OSK_ASSERT(NULL != reg);
	OSK_ASSERT((flags & ~((1 << BASE_MEM_FLAGS_NR_BITS) - 1)) == 0);

	reg->flags |= kbase_cache_enabled(flags, reg->nr_pages);

	if ((flags & BASE_MEM_GROW_ON_GPF) || is_growable)
	{
		reg->flags |= KBASE_REG_GROWABLE;

		if (flags & BASE_MEM_GROW_ON_GPF)
		{
			reg->flags |= KBASE_REG_PF_GROW;
		}
	}
	else
	{
		/* As this region is not growable but the default is growable, we
		   explicitly clear the growable flag. */
		reg->flags &= ~KBASE_REG_GROWABLE;
	}

	if (flags & BASE_MEM_PROT_CPU_WR)
	{
		reg->flags |= KBASE_REG_CPU_RW;
	}

	if (flags & BASE_MEM_PROT_GPU_WR)
	{
		reg->flags |= KBASE_REG_GPU_RW;
	}

	if (0 == (flags & BASE_MEM_PROT_GPU_EX))
	{
		reg->flags |= KBASE_REG_GPU_NX;
	}

	if (flags & BASE_MEM_COHERENT_LOCAL)
	{
		reg->flags |= KBASE_REG_SHARE_IN;
	}
	else if (flags & BASE_MEM_COHERENT_SYSTEM)
	{
		reg->flags |= KBASE_REG_SHARE_BOTH;
	}
}

static void kbase_free_phy_pages_helper(kbase_va_region * reg, u32 nr_pages_to_free)
{
	osk_phy_addr *page_array;

	u32 nr_pages;

	OSK_ASSERT(reg);
	OSK_ASSERT(reg->kctx);

	/* Can't call this on TB or UMP buffers */
	OSK_ASSERT(0 == (reg->flags & (KBASE_REG_IS_TB | KBASE_REG_IS_UMP)));
	/* Free of too many pages attempted! */
	OSK_ASSERT(reg->nr_alloc_pages >= nr_pages_to_free);
	/* A complete free is required if not marked as growable */
	OSK_ASSERT((reg->flags & KBASE_REG_GROWABLE) || (reg->nr_alloc_pages == nr_pages_to_free));
 
	if (0 == nr_pages_to_free)
	{
		/* early out if nothing to free */
		return;
	}
  	
	nr_pages = nr_pages_to_free;
	
	page_array = kbase_get_phy_pages(reg);

	OSK_ASSERT(nr_pages_to_free == 0 || page_array != NULL);

	while (nr_pages)
	{
		kbase_mem_commit * commit;
		commit = reg->last_commit;

		if (nr_pages >= commit->nr_pages)
		{
			/* free the whole commit */
			kbase_phy_pages_free(reg->kctx->kbdev, commit->allocator, commit->nr_pages,
					page_array + reg->nr_alloc_pages - commit->nr_pages);
			
			/* update page counts */
			nr_pages -= commit->nr_pages;
			reg->nr_alloc_pages -= commit->nr_pages;
			
			/* free the node (unless it's the root node) */
			if (commit != &reg->root_commit)
			{
				reg->last_commit = commit->prev;
				osk_free(commit);
			}
			else
			{
				/* mark the root node as having no commit */
				commit->nr_pages = 0;
				OSK_ASSERT(nr_pages == 0);
				OSK_ASSERT(reg->nr_alloc_pages == 0);
				break;
			}
		}
		else
		{
			/* partial free of this commit */
			kbase_phy_pages_free(reg->kctx->kbdev, commit->allocator, nr_pages,
					page_array + reg->nr_alloc_pages - nr_pages);
			commit->nr_pages -= nr_pages;
			reg->nr_alloc_pages -= nr_pages;
			break; /* end the loop */
		}
	}

	kbase_mem_usage_release_pages(&reg->kctx->usage, nr_pages_to_free);
}
KBASE_EXPORT_TEST_API(kbase_update_region_flags)

u32 kbase_phy_pages_alloc(struct kbase_device *kbdev, osk_phy_allocator *allocator, u32 nr_pages,
		osk_phy_addr *pages)
{
	OSK_ASSERT(kbdev != NULL);
	OSK_ASSERT(allocator != NULL);
	OSK_ASSERT(pages != NULL);

	if (allocator->type == OSKP_PHY_ALLOCATOR_OS)
	{
		u32 pages_allocated;

		/* Claim pages from OS shared quota. Note that shared OS memory may be used by different allocators. That's why
		 * page request is made here and not on per-allocator basis */
		if (MALI_ERROR_NONE != kbase_mem_usage_request_pages(&kbdev->memdev.usage, nr_pages))
		{
			return 0;
		}

		pages_allocated = osk_phy_pages_alloc(allocator, nr_pages, pages);

		if (pages_allocated < nr_pages)
		{
			kbase_mem_usage_release_pages(&kbdev->memdev.usage, nr_pages - pages_allocated);
		}
		return pages_allocated;
	}
	else
	{
		/* Dedicated memory is tracked per allocator. Memory limits are checked in osk_phy_pages_alloc function */
		return osk_phy_pages_alloc(allocator, nr_pages, pages);
	}
}
KBASE_EXPORT_TEST_API(kbase_phy_pages_free)
void kbase_phy_pages_free(struct kbase_device *kbdev, osk_phy_allocator *allocator, u32 nr_pages, osk_phy_addr *pages)
{
	OSK_ASSERT(kbdev != NULL);
	OSK_ASSERT(allocator != NULL);
	OSK_ASSERT(pages != NULL);

	osk_phy_pages_free(allocator, nr_pages, pages);

	if (allocator->type == OSKP_PHY_ALLOCATOR_OS)
	{
		/* release pages from OS shared quota */
		kbase_mem_usage_release_pages(&kbdev->memdev.usage, nr_pages);
	}
}


mali_error kbase_alloc_phy_pages_helper(struct kbase_va_region *reg, u32 nr_pages_requested)
{
	kbase_phys_allocator_iterator it;
	osk_phy_addr *page_array;
	u32 nr_pages_left;
	u32 num_pages_on_start;
	u32 pages_committed;
	kbase_phys_allocator_order order;
	u32 performance_flags;

	OSK_ASSERT(reg);
	OSK_ASSERT(reg->kctx);

	/* Can't call this on TB or UMP buffers */
	OSK_ASSERT(0 == (reg->flags & (KBASE_REG_IS_TB | KBASE_REG_IS_UMP)));
	/* Growth of too many pages attempted! (written this way to catch overflow)) */
	OSK_ASSERT(reg->nr_pages - reg->nr_alloc_pages >= nr_pages_requested);
	/* A complete commit is required if not marked as growable */
	OSK_ASSERT((reg->flags & KBASE_REG_GROWABLE) || (reg->nr_pages == nr_pages_requested));

	if (0 == nr_pages_requested)
	{
		/* early out if nothing to do */
		return MALI_ERROR_NONE;
	}

	/* track the number pages so we can roll back on alloc fail */
	num_pages_on_start = reg->nr_alloc_pages;
	nr_pages_left = nr_pages_requested;

	page_array = kbase_get_phy_pages(reg);
	OSK_ASSERT(page_array);

	/* claim the pages from our per-context quota */
	if (MALI_ERROR_NONE != kbase_mem_usage_request_pages(&reg->kctx->usage, nr_pages_requested))
	{
		return MALI_ERROR_OUT_OF_MEMORY;
	}

	/* First try to extend the last commit */
	if (reg->last_commit->allocator)
	{
		pages_committed = kbase_phy_pages_alloc(reg->kctx->kbdev, reg->last_commit->allocator, nr_pages_left,
				page_array + reg->nr_alloc_pages);
		reg->last_commit->nr_pages += pages_committed;
		reg->nr_alloc_pages += pages_committed;
		nr_pages_left -= pages_committed;

		if (!nr_pages_left)
		{
			return MALI_ERROR_NONE;
		}
	}

	performance_flags = reg->flags & (KBASE_REG_CPU_CACHED | KBASE_REG_GPU_CACHED);

	if (performance_flags == 0)
	{
		order = ALLOCATOR_ORDER_CONFIG;
	}
	else if (performance_flags == KBASE_REG_CPU_CACHED)
	{
		order = ALLOCATOR_ORDER_CPU_PERFORMANCE;
	}
	else if (performance_flags == KBASE_REG_GPU_CACHED)
	{
		order = ALLOCATOR_ORDER_GPU_PERFORMANCE;
	}
	else
	{
		order = ALLOCATOR_ORDER_CPU_GPU_PERFORMANCE;
	}

	/* If not fully commited (or no prev allocator) we need to ask all the allocators */

	/* initialize the iterator we use to loop over the memory providers */
	if (MALI_ERROR_NONE == kbase_phys_it_init(reg->kctx->kbdev, &it, order))
	{
		for (;nr_pages_left && kbase_phys_it_deref(&it); kbase_phys_it_deref_and_advance(&it))
		{
			pages_committed = kbase_phy_pages_alloc(reg->kctx->kbdev, kbase_phys_it_deref(&it), nr_pages_left,
					page_array + reg->nr_alloc_pages);

			OSK_ASSERT(pages_committed <= nr_pages_left);

			if (pages_committed)
			{
				/* got some pages, track them */
				kbase_mem_commit * commit;

				if (reg->last_commit->allocator)
				{
					commit = (kbase_mem_commit*)osk_calloc(sizeof(*commit));
					if (commit == NULL)
					{
						kbase_phy_pages_free(reg->kctx->kbdev, kbase_phys_it_deref(&it), pages_committed,
								page_array + reg->nr_alloc_pages);
						break;
					}
					commit->prev = reg->last_commit;
				}
				else
				{
					commit = reg->last_commit;
				}

				commit->allocator = kbase_phys_it_deref(&it);
				commit->nr_pages = pages_committed;

				reg->last_commit = commit;
				reg->nr_alloc_pages += pages_committed;

				nr_pages_left -= pages_committed;
			}
		}

		/* no need for the iterator any more */
		kbase_phys_it_term(&it);

		if (nr_pages_left == 0)
		{
			return MALI_ERROR_NONE;
		}
	}

	/* failed to allocate enough memory, roll back */
	if (reg->nr_alloc_pages != num_pages_on_start)
	{
		/* kbase_free_phy_pages_helper implicitly calls kbase_mem_usage_release_pages */
		kbase_free_phy_pages_helper(reg, reg->nr_alloc_pages - num_pages_on_start);
		/* Release the remaining pages */
		kbase_mem_usage_release_pages(&reg->kctx->usage,
		                              nr_pages_requested - (reg->nr_alloc_pages - num_pages_on_start));
	}
	else
	{
		kbase_mem_usage_release_pages(&reg->kctx->usage, nr_pages_requested);
	}
	return MALI_ERROR_OUT_OF_MEMORY;
}


/* Frees all allocated pages of a region */
void kbase_free_phy_pages(struct kbase_va_region *reg)
{
	osk_phy_addr *page_array;
	OSK_ASSERT(NULL != reg);

	page_array = kbase_get_phy_pages(reg);

#if MALI_USE_UMP
	if (reg->flags & KBASE_REG_IS_UMP)
	{
		ump_dd_handle umph;
		umph = (ump_dd_handle)reg->ump_handle;
		ump_dd_release(umph);
	}
	else
	{
#endif /* MALI_USE_UMP */
		if (reg->flags & KBASE_REG_IS_TB)
		{
			/* trace buffer being freed. Disconnect, then use osk_vfree */
			/* save tb so we can free it after the disconnect call */
			void * tb;
			tb = reg->kctx->jctx.tb;
			kbase_device_trace_buffer_uninstall(reg->kctx);
			osk_vfree(tb);
		}
		else if (reg->flags & KBASE_REG_IS_RB)
		{
			/* nothing to do */
		}
		else
		{
			kbase_free_phy_pages_helper(reg, reg->nr_alloc_pages);
		}
#if MALI_USE_UMP
	}
#endif /* MALI_USE_UMP */

	kbase_set_phy_pages(reg, NULL);
	osk_vfree(page_array);
}
KBASE_EXPORT_TEST_API(kbase_free_phy_pages)

int kbase_alloc_phy_pages(struct kbase_va_region *reg, u32 vsize, u32 size)
{
	osk_phy_addr *page_array;

	OSK_ASSERT( NULL != reg );
	OSK_ASSERT( vsize > 0 ); 

	/* validate user provided arguments */
	if (size > vsize || vsize > reg->nr_pages) 
	{
		goto out_term;
	}

	/* Prevent vsize*sizeof from wrapping around.
	 * For instance, if vsize is 2**29+1, we'll allocate 1 byte and the alloc won't fail.
	 */
	if ((size_t)vsize > ((size_t)-1 / sizeof(*page_array)))
	{
		goto out_term;
	}

	page_array = osk_vmalloc(vsize * sizeof(*page_array));
	if (!page_array)
	{
		goto out_term;
	}

	kbase_set_phy_pages(reg, page_array);

	if (MALI_ERROR_NONE != kbase_alloc_phy_pages_helper(reg, size))
	{
		goto out_free;
	}

	return 0;

out_free:
	osk_vfree(page_array);
out_term:
	return -1;
}
KBASE_EXPORT_TEST_API(kbase_alloc_phy_pages)

/** @brief Round to +inf a tmem growable delta in pages */
STATIC mali_bool kbasep_tmem_growable_round_delta( s32 *delta_ptr )
{
	s32 delta;

	OSK_ASSERT( delta_ptr != NULL );

	delta = *delta_ptr;

	if (delta >= 0)
	{
		u32 new_delta_unsigned = kbasep_tmem_growable_round_size( (u32)delta );
		if ( new_delta_unsigned > S32_MAX )
		{
			/* Can't support a delta of this size */
			return MALI_FALSE;
		}

		*delta_ptr = (s32)new_delta_unsigned;
	}
	else
	{
		u32 new_delta_unsigned = (u32)-delta;
		/* Round down */
		new_delta_unsigned = new_delta_unsigned & ~(KBASEP_TMEM_GROWABLE_BLOCKSIZE_PAGES-1);

		*delta_ptr = (s32)-new_delta_unsigned;
	}

	return MALI_TRUE;
}

mali_bool kbase_check_alloc_flags(u32 flags)
{
	/* At least one flags should be set */
	if (flags == 0)
	{
		return MALI_FALSE;
	}
	/* Either the GPU or CPU must be reading from the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD)) == 0)
	{
		return MALI_FALSE;
	}
	/* Either the GPU or CPU must be writing to the allocated memory */
	if ((flags & (BASE_MEM_PROT_CPU_WR | BASE_MEM_PROT_GPU_WR)) == 0)
	{
		return MALI_FALSE;
	}
	/* GPU cannot be writing to GPU executable memory */
	if ((flags & BASE_MEM_PROT_GPU_EX) && (flags & BASE_MEM_PROT_GPU_WR))
	{
		return MALI_FALSE;
	}
	/* GPU should have at least read or write access otherwise there is no
	reason for allocating pmem/tmem. */
	if ((flags & (BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR)) == 0)
	{
		return MALI_FALSE;
	}

	return MALI_TRUE;
}

struct kbase_va_region *kbase_tmem_alloc(struct kbase_context *kctx,
					 u32 vsize, u32 psize,
					 u32 extent, u32 flags, mali_bool is_growable)
{
	struct kbase_va_region *reg;
	mali_error err;
	u32 align = 1;
	u32 vsize_rounded = vsize;
	u32 psize_rounded = psize;
	u32 extent_rounded = extent;

	if ( 0 == vsize )
	{
		goto out1;
	}

	OSK_ASSERT(NULL != kctx);

	if (!kbase_check_alloc_flags(flags))
	{
		goto out1;
	}

	if ((flags & BASE_MEM_GROW_ON_GPF) != MALI_FALSE)
	{
		/* Round up the sizes for growable on GPU page fault memory */
		vsize_rounded  = kbasep_tmem_growable_round_size( vsize );
		psize_rounded  = kbasep_tmem_growable_round_size( psize );
		extent_rounded = kbasep_tmem_growable_round_size( extent );

		if ( vsize_rounded < vsize || psize_rounded < psize || extent_rounded < extent )
		{
			/* values too large to round */
			return NULL;
		}
	}

	if (flags & BASE_MEM_PROT_GPU_EX)
	{
		if (kbase_device_has_feature(kctx->kbdev, KBASE_FEATURE_HAS_16BIT_PC))
		{
			/* A 16-bit PC gives us a 64kB max size and a 64kB alignment req */
			align = (1ul << (16 - OSK_PAGE_SHIFT));
		}
		else
		{
			/* A 32-bit PC is the default, giving a 4GB/4GB max size/alignment */
			align = (1ul << (32 - OSK_PAGE_SHIFT));
		}

		if (vsize_rounded > align)
		{
			OSK_PRINT_WARN(OSK_BASE_MEM, "Executable tmem virtual size %lx is larger than the pc (%lx) (in pages)", (unsigned long)vsize_rounded, (unsigned long)align);
			return NULL;
		}
	}

	if ( extent > 0 && !(flags & BASE_MEM_GROW_ON_GPF))
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "BASE_MEM_GROW_ON_GPF flag not set when extent is greater than 0");
		goto out1;
	}

	reg = kbase_alloc_free_region(kctx, 0, vsize_rounded, KBASE_REG_ZONE_TMEM);
	if (!reg)
	{
		goto out1;
	}

	reg->flags &= ~KBASE_REG_FREE;

	kbase_update_region_flags(reg, flags, is_growable);

	if (kbase_alloc_phy_pages(reg, vsize_rounded, psize_rounded))
	{
		goto out2;
	}

	reg->nr_alloc_pages	= psize_rounded;
	reg->extent		= extent_rounded;

	kbase_gpu_vm_lock(kctx);
	err = kbase_gpu_mmap(kctx, reg, 0, vsize_rounded, align);
	kbase_gpu_vm_unlock(kctx);

	if (err)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "kbase_gpu_mmap failed\n");
		goto out3;
	}

	return reg;

out3:
	kbase_free_phy_pages(reg);
out2:
	osk_free(reg);
out1:
	return NULL;
}
KBASE_EXPORT_TEST_API(kbase_tmem_alloc)

mali_error kbase_tmem_resize(struct kbase_context *kctx, mali_addr64 gpu_addr, s32 delta, u32 *size, base_backing_threshold_status * failure_reason)
{
	kbase_va_region *reg;
	mali_error ret = MALI_ERROR_FUNCTION_FAILED;
#if !( (MALI_INFINITE_CACHE != 0) && !MALI_BACKEND_KERNEL )
	/* tmem is already mapped to max_pages, so no resizing needed */
	osk_phy_addr *phy_pages;
#endif /* !( (MALI_INFINITE_CACHE != 0) && !MALI_BACKEND_KERNEL ) */
	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(size);
	OSK_ASSERT(failure_reason);
	OSK_ASSERT(gpu_addr != 0);

	kbase_gpu_vm_lock(kctx);

	/* Validate the region */
	reg = kbase_validate_region(kctx, gpu_addr);
	if (!reg || (reg->flags & KBASE_REG_FREE) )
	{
		/* not a valid region or is free memory*/
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;
		goto out_unlock;
	}

#if !( (MALI_INFINITE_CACHE != 0) && !MALI_BACKEND_KERNEL )
	/* tmem is already mapped to max_pages, don't try to resize */
	
	if ((KBASE_REG_ZONE_MASK & reg->flags) != KBASE_REG_ZONE_TMEM)
	{
		/* not a valid region - not tmem */
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;
		goto out_unlock;
	}
	if (0 == (reg->flags & KBASE_REG_GROWABLE))
	{
		/* not growable */
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_NOT_GROWABLE;
		goto out_unlock;
	}

	if (!OSK_DLIST_IS_EMPTY(&reg->map_list))
	{
		/* We still have mappings */
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_MAPPED;
		goto out_unlock;
	}

	if ( reg->flags & KBASE_REG_PF_GROW )
	{
		/* Apply rounding to +inf on the delta, which may cause a negative delta to become zero */
		if ( kbasep_tmem_growable_round_delta( &delta ) == MALI_FALSE )
		{
			/* Can't round this big a delta */
			*failure_reason = BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;
			goto out_unlock;
		}
	}

	if (delta < 0 && (u32)-delta > reg->nr_alloc_pages)
	{
		/* Underflow */
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;
		goto out_unlock;
	}
	if (reg->nr_alloc_pages + delta > reg->nr_pages)
	{
		/* Would overflow the VA region */
		*failure_reason = BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;
		goto out_unlock;
	}

	phy_pages = kbase_get_phy_pages(reg);

	if (delta > 0)
	{
		mali_error err;

		/* Allocate some more pages */
		if (MALI_ERROR_NONE != kbase_alloc_phy_pages_helper(reg, delta))
		{
			*failure_reason = BASE_BACKING_THRESHOLD_ERROR_OOM;
			goto out_unlock;
		}
		err = kbase_mmu_insert_pages(kctx, reg->start_pfn + reg->nr_alloc_pages - delta,
		                             phy_pages + reg->nr_alloc_pages - delta,
		                             delta, reg->flags);
		if(MALI_ERROR_NONE != err)
		{
			kbase_free_phy_pages_helper(reg, delta);
			*failure_reason = BASE_BACKING_THRESHOLD_ERROR_OOM;
			goto out_unlock;
		}
	}
	else if (delta < 0)
	{
		mali_error err;
		/* Free some pages */
		
		/* Get the absolute value of delta. Note that we have to add one before and after the negation to avoid
		 * overflowing when delta is INT_MIN */
		u32 num_pages = (u32)(-(delta+1))+1;

		err = kbase_mmu_teardown_pages(kctx, reg->start_pfn + reg->nr_alloc_pages - num_pages, num_pages);
		if(MALI_ERROR_NONE != err)
		{
			*failure_reason = BASE_BACKING_THRESHOLD_ERROR_OOM;
			goto out_unlock;
		}

#if BASE_HW_ISSUE_6367
		/* Wait for GPU to flush write buffer before freeing physical pages */
		kbase_wait_write_flush(kctx);
#endif
		kbase_free_phy_pages_helper(reg, num_pages);
	}
	/* else just a size query */

#endif /* !( (MALI_INFINITE_CACHE != 0) && !MALI_BACKEND_KERNEL ) */

	*size = reg->nr_alloc_pages;

	ret = MALI_ERROR_NONE;

out_unlock:
	kbase_gpu_vm_unlock(kctx);
	return ret;
}
KBASE_EXPORT_TEST_API(kbase_tmem_resize)

#if MALI_USE_UMP

struct kbase_va_region *kbase_tmem_from_ump(struct kbase_context *kctx, ump_secure_id id, u64 * const pages)
{
	struct kbase_va_region *reg;
	mali_error err;
	ump_dd_handle umph;
	u64 vsize;
	u64 block_count;
	const ump_dd_physical_block_64 * block_array;
	osk_phy_addr *page_array;
	u64 i, j;
	int page = 0;
	ump_alloc_flags ump_flags;
	ump_alloc_flags cpu_flags;
	ump_alloc_flags gpu_flags;

	OSK_ASSERT(NULL != pages);

	umph = ump_dd_from_secure_id(id);
	if (UMP_DD_INVALID_MEMORY_HANDLE == umph)
	{
		return NULL;
	}

	ump_flags = ump_dd_allocation_flags_get(umph);
	cpu_flags = (ump_flags >> UMP_DEVICE_CPU_SHIFT) & UMP_DEVICE_MASK;
	gpu_flags = (ump_flags >> kctx->kbdev->memdev.ump_device_id) & UMP_DEVICE_MASK;

	vsize = ump_dd_size_get_64(umph);
	vsize >>= OSK_PAGE_SHIFT;

	reg = kbase_alloc_free_region(kctx, 0, vsize, KBASE_REG_ZONE_TMEM);
	if (!reg)
	{
		goto out1;
	}

	reg->flags &= ~KBASE_REG_FREE;
	reg->flags |= KBASE_REG_IS_UMP;
	reg->flags |= KBASE_REG_GPU_NX;    /* UMP is always No eXecute */
	reg->flags &= ~KBASE_REG_GROWABLE; /* UMP cannot be grown */

	reg->ump_handle = umph;

	if ((cpu_flags & (UMP_HINT_DEVICE_RD|UMP_HINT_DEVICE_WR)) == (UMP_HINT_DEVICE_RD|UMP_HINT_DEVICE_WR))
	{
		reg->flags |= KBASE_REG_CPU_CACHED;
	}

	if (cpu_flags & UMP_PROT_DEVICE_WR)
	{
		reg->flags |= KBASE_REG_CPU_RW;
	}

	if ((gpu_flags & (UMP_HINT_DEVICE_RD|UMP_HINT_DEVICE_WR)) == (UMP_HINT_DEVICE_RD|UMP_HINT_DEVICE_WR))
	{
		reg->flags |= KBASE_REG_GPU_CACHED;
	}

	if (gpu_flags & UMP_PROT_DEVICE_WR)
	{
		reg->flags |= KBASE_REG_GPU_RW;
	}

	/* ump phys block query */
	ump_dd_phys_blocks_get_64(umph, &block_count, &block_array);

	page_array = osk_vmalloc(vsize * sizeof(*page_array));
	if (!page_array)
	{
		goto out2;
	}

	for (i = 0; i < block_count; i++)
	{
		for (j = 0; j < (block_array[i].size >> OSK_PAGE_SHIFT); j++)
		{
			page_array[page] = block_array[i].addr + (j << OSK_PAGE_SHIFT);
			page++;
		}
	}

	kbase_set_phy_pages(reg, page_array);

	reg->nr_alloc_pages	= vsize;
	reg->extent		= vsize;

	kbase_gpu_vm_lock(kctx);
	err = kbase_gpu_mmap(kctx, reg, 0, vsize, 1/* no alignment */);
	kbase_gpu_vm_unlock(kctx);

	if (err)
	{
		OSK_PRINT_WARN(OSK_BASE_MEM, "kbase_gpu_mmap failed\n");
		goto out3;
	}

	*pages = vsize;

	return reg;

out3:
	osk_vfree(page_array);
out2:
	osk_free(reg);
out1:
	ump_dd_release(umph);
	return NULL;


}

#endif /* MALI_USE_UMP */

/**
 * @brief Acquire the per-context region list lock
 */
void kbase_gpu_vm_lock(struct kbase_context *kctx)
{
	OSK_ASSERT(kctx != NULL);
	osk_mutex_lock(&kctx->reg_lock);
}
KBASE_EXPORT_TEST_API(kbase_gpu_vm_lock)

/**
 * @brief Release the per-context region list lock
 */
void kbase_gpu_vm_unlock(struct kbase_context *kctx)
{
	OSK_ASSERT(kctx != NULL);
	osk_mutex_unlock(&kctx->reg_lock);
}
KBASE_EXPORT_TEST_API(kbase_gpu_vm_unlock)

/* will be called during init time only */
mali_error kbase_register_memory_regions(kbase_device * kbdev, const kbase_attribute *attributes)
{
	int total_regions;
	int dedicated_regions;
	int allocators_initialized;
	osk_phy_allocator * allocs;
	kbase_memory_performance shared_memory_performance;
	kbasep_memory_region_performance *region_performance;
	kbase_memory_resource *resource;
	const kbase_attribute *current_attribute;
	u32 max_shared_memory;
	kbasep_mem_device * memdev;

	OSK_ASSERT(kbdev);
	OSK_ASSERT(attributes);

	memdev = &kbdev->memdev;

	/* Programming error to register memory after we've started using the iterator interface */
#if MALI_DEBUG
	OSK_ASSERT(memdev->allocators.it_bound == MALI_FALSE);
#endif /* MALI_DEBUG */

	max_shared_memory = (u32) kbasep_get_config_value(attributes, KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX);
	shared_memory_performance =
			(kbase_memory_performance)kbasep_get_config_value(attributes, KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_PERF_GPU);
	/* count dedicated_memory_regions */
	dedicated_regions = kbasep_get_config_attribute_count_by_id(attributes, KBASE_CONFIG_ATTR_MEMORY_RESOURCE);

	total_regions = dedicated_regions;
	if (max_shared_memory > 0)
	{
		total_regions++;
	}

	if (total_regions == 0)
	{
		OSK_PRINT_ERROR(OSK_BASE_MEM,  "No memory regions specified");
		return MALI_ERROR_FUNCTION_FAILED;
	}

	region_performance = osk_malloc(sizeof(kbasep_memory_region_performance) * total_regions);

	if (region_performance == NULL)
	{
		goto out;
	}

	allocs = osk_malloc(sizeof(osk_phy_allocator) * total_regions);
	if (allocs == NULL)
	{
		goto out_perf;
	}

	current_attribute = attributes;
	allocators_initialized = 0;
	while (current_attribute != NULL)
	{
		current_attribute = kbasep_get_next_attribute(current_attribute, KBASE_CONFIG_ATTR_MEMORY_RESOURCE);

		if (current_attribute != NULL)
		{
			resource = (kbase_memory_resource *)current_attribute->data;
			if (OSK_ERR_NONE != osk_phy_allocator_init(&allocs[allocators_initialized], resource->base,
						(u32)(resource->size >> OSK_PAGE_SHIFT), resource->name))
			{
				goto out_allocator_term;
			}

			kbasep_get_memory_performance(resource, &region_performance[allocators_initialized].cpu_performance,
					&region_performance[allocators_initialized].gpu_performance);
			current_attribute++;
			allocators_initialized++;
		}
	}

	/* register shared memory region */
	if (max_shared_memory > 0)
	{
		if (OSK_ERR_NONE != osk_phy_allocator_init(&allocs[allocators_initialized], 0,
				max_shared_memory >> OSK_PAGE_SHIFT, NULL))
		{
			goto out_allocator_term;
		}

		region_performance[allocators_initialized].cpu_performance = KBASE_MEM_PERF_NORMAL;
		region_performance[allocators_initialized].gpu_performance = shared_memory_performance;
		allocators_initialized++;
	}

	if (MALI_ERROR_NONE != kbase_mem_usage_init(&memdev->usage, max_shared_memory >> OSK_PAGE_SHIFT))
	{
		goto out_allocator_term;
	}

	if (MALI_ERROR_NONE != kbasep_allocator_order_list_create(allocs, region_performance, total_regions, memdev->allocators.sorted_allocs,
			ALLOCATOR_ORDER_COUNT))
	{
		goto out_memctx_term;
	}

	memdev->allocators.allocs = allocs;
	memdev->allocators.count = total_regions;

	osk_free(region_performance);

	return MALI_ERROR_NONE;

out_memctx_term:
	kbase_mem_usage_term(&memdev->usage);
out_allocator_term:
	while (allocators_initialized-- > 0)
	{
		osk_phy_allocator_term(&allocs[allocators_initialized]);
	}
	osk_free(allocs);
out_perf:
	osk_free(region_performance);
out:
	return MALI_ERROR_OUT_OF_MEMORY;
}
KBASE_EXPORT_TEST_API(kbase_register_memory_regions)

static mali_error kbasep_allocator_order_list_create( osk_phy_allocator * allocators,
		kbasep_memory_region_performance *region_performance, int memory_region_count,
		osk_phy_allocator ***sorted_allocs, int allocator_order_count)
{
	int performance;
	int regions_sorted;
	int i;
	void *sorted_alloc_mem_block;

	sorted_alloc_mem_block = osk_malloc(sizeof(osk_phy_allocator **) * memory_region_count * allocator_order_count);
	if (sorted_alloc_mem_block == NULL)
	{
		goto out;
	}

	/* each allocator list points to memory in recently allocated block */
	for (i = 0; i < ALLOCATOR_ORDER_COUNT; i++)
	{
		sorted_allocs[i] = (osk_phy_allocator **)sorted_alloc_mem_block + memory_region_count*i;
	}

	/* use the same order as in config file */
	for (i = 0; i < memory_region_count; i++)
	{
		sorted_allocs[ALLOCATOR_ORDER_CONFIG][i] = &allocators[i];
	}

	/* Sort allocators by GPU performance */
	performance = KBASE_MEM_PERF_FAST;
	regions_sorted = 0;
	while (performance >= KBASE_MEM_PERF_SLOW)
	{
		for (i = 0; i < memory_region_count; i++)
		{
			if (region_performance[i].gpu_performance == (kbase_memory_performance)performance)
			{
				sorted_allocs[ALLOCATOR_ORDER_GPU_PERFORMANCE][regions_sorted] = &allocators[i];
				regions_sorted++;
			}
		}
		performance--;
	}

	/* Sort allocators by CPU performance */
	performance = KBASE_MEM_PERF_FAST;
	regions_sorted = 0;
	while (performance >= KBASE_MEM_PERF_SLOW)
	{
		for (i = 0; i < memory_region_count; i++)
		{
			if ((int)region_performance[i].cpu_performance == performance)
			{
				sorted_allocs[ALLOCATOR_ORDER_CPU_PERFORMANCE][regions_sorted] = &allocators[i];
				regions_sorted++;
			}
		}
		performance--;
	}

	/* Sort allocators by CPU and GPU performance (equally important) */
	performance = 2 * KBASE_MEM_PERF_FAST;
	regions_sorted = 0;
	while (performance >= 2*KBASE_MEM_PERF_SLOW)
	{
		for (i = 0; i < memory_region_count; i++)
		{
			if ((int)(region_performance[i].cpu_performance + region_performance[i].gpu_performance) == performance)
			{
				sorted_allocs[ALLOCATOR_ORDER_CPU_GPU_PERFORMANCE][regions_sorted] = &allocators[i];
				regions_sorted++;
			}
		}
		performance--;
	}
	return MALI_ERROR_NONE;
out:
	return MALI_ERROR_OUT_OF_MEMORY;
}

