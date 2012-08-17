/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_kernel_core.h"
#include "mali_kernel_memory_engine.h"
#include "mali_osk.h"

typedef struct os_allocation
{
	u32 num_pages;
	u32 offset_start;
	mali_allocation_engine * engine;
	mali_memory_allocation * descriptor;
} os_allocation;

typedef struct os_allocator
{
	_mali_osk_lock_t *mutex;

	/**
	 * Maximum number of pages to allocate from the OS
	 */
	u32 num_pages_max;

	/**
	 * Number of pages allocated from the OS
	 */
	u32 num_pages_allocated;

	/** CPU Usage adjustment (add to mali physical address to get cpu physical address) */
	u32 cpu_usage_adjust;
} os_allocator;

static mali_physical_memory_allocation_result os_allocator_allocate(void* ctx, mali_allocation_engine * engine,  mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info);
static mali_physical_memory_allocation_result os_allocator_allocate_page_table_block(void * ctx, mali_page_table_block * block);
static void os_allocator_release(void * ctx, void * handle);
static void os_allocator_page_table_block_release( mali_page_table_block *page_table_block );
static void os_allocator_destroy(mali_physical_memory_allocator * allocator);
static u32 os_allocator_stat(mali_physical_memory_allocator * allocator);

mali_physical_memory_allocator * mali_os_allocator_create(u32 max_allocation, u32 cpu_usage_adjust, const char *name)
{
	mali_physical_memory_allocator * allocator;
	os_allocator * info;

	max_allocation = (max_allocation + _MALI_OSK_CPU_PAGE_SIZE-1) & ~(_MALI_OSK_CPU_PAGE_SIZE-1);

	MALI_DEBUG_PRINT(2, ("Mali OS memory allocator created with max allocation size of 0x%X bytes, cpu_usage_adjust 0x%08X\n", max_allocation, cpu_usage_adjust));

	allocator = _mali_osk_malloc(sizeof(mali_physical_memory_allocator));
	if (NULL != allocator)
	{
		info = _mali_osk_malloc(sizeof(os_allocator));
		if (NULL != info)
		{
			info->num_pages_max = max_allocation / _MALI_OSK_CPU_PAGE_SIZE;
			info->num_pages_allocated = 0;
			info->cpu_usage_adjust = cpu_usage_adjust;

			info->mutex = _mali_osk_lock_init( _MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_ORDERED, 0, _MALI_OSK_LOCK_ORDER_MEM_INFO);
            if (NULL != info->mutex)
            {
			    allocator->allocate = os_allocator_allocate;
			    allocator->allocate_page_table_block = os_allocator_allocate_page_table_block;
			    allocator->destroy = os_allocator_destroy;
				allocator->stat = os_allocator_stat;
			    allocator->ctx = info;
				allocator->name = name;

			    return allocator;
            }
            _mali_osk_free(info);
		}
		_mali_osk_free(allocator);
	}

	return NULL;
}

static u32 os_allocator_stat(mali_physical_memory_allocator * allocator)
{
	os_allocator * info;
	info = (os_allocator*)allocator->ctx;
	return info->num_pages_allocated * _MALI_OSK_MALI_PAGE_SIZE;
}

static void os_allocator_destroy(mali_physical_memory_allocator * allocator)
{
	os_allocator * info;
	MALI_DEBUG_ASSERT_POINTER(allocator);
	MALI_DEBUG_ASSERT_POINTER(allocator->ctx);
	info = (os_allocator*)allocator->ctx;
	_mali_osk_lock_term(info->mutex);
	_mali_osk_free(info);
	_mali_osk_free(allocator);
}

static mali_physical_memory_allocation_result os_allocator_allocate(void* ctx, mali_allocation_engine * engine,  mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info)
{
	mali_physical_memory_allocation_result result = MALI_MEM_ALLOC_NONE;
	u32 left;
	os_allocator * info;
	os_allocation * allocation;
	int pages_allocated = 0;
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_POINTER(offset);
	MALI_DEBUG_ASSERT_POINTER(alloc_info);

	info = (os_allocator*)ctx;
	left = descriptor->size - *offset;

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW)) return MALI_MEM_ALLOC_INTERNAL_FAILURE;

	/** @note this code may not work on Linux, or may require a more complex Linux implementation */
	allocation = _mali_osk_malloc(sizeof(os_allocation));
	if (NULL != allocation)
	{
		u32 os_mem_max_usage = info->num_pages_max * _MALI_OSK_CPU_PAGE_SIZE;
		allocation->offset_start = *offset;
		allocation->num_pages = ((left + _MALI_OSK_CPU_PAGE_SIZE - 1) & ~(_MALI_OSK_CPU_PAGE_SIZE - 1)) >> _MALI_OSK_CPU_PAGE_ORDER;
		MALI_DEBUG_PRINT(6, ("Allocating page array of size %d bytes\n", allocation->num_pages * sizeof(struct page*)));

		while (left > 0 && ((info->num_pages_allocated + pages_allocated) < info->num_pages_max) && _mali_osk_mem_check_allocated(os_mem_max_usage))
		{
			err = mali_allocation_engine_map_physical(engine, descriptor, *offset, MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC, info->cpu_usage_adjust, _MALI_OSK_CPU_PAGE_SIZE);
			if ( _MALI_OSK_ERR_OK != err)
			{
				if (  _MALI_OSK_ERR_NOMEM == err)
				{
					/* 'Partial' allocation (or, out-of-memory on first page) */
					break;
				}

				MALI_DEBUG_PRINT(1, ("Mapping of physical memory failed\n"));

				/* Fatal error, cleanup any previous pages allocated. */
				if ( pages_allocated > 0 )
				{
					mali_allocation_engine_unmap_physical( engine, descriptor, allocation->offset_start, _MALI_OSK_CPU_PAGE_SIZE*pages_allocated, _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR );
					/* (*offset) doesn't need to be restored; it will not be used by the caller on failure */
				}

				pages_allocated = 0;

				result = MALI_MEM_ALLOC_INTERNAL_FAILURE;
				break;
			}

			/* Loop iteration */
			if (left < _MALI_OSK_CPU_PAGE_SIZE) left = 0;
			else left -= _MALI_OSK_CPU_PAGE_SIZE;

			pages_allocated++;

			*offset += _MALI_OSK_CPU_PAGE_SIZE;
		}

		if (left) MALI_PRINT(("Out of memory. Mali memory allocated: %d kB  Configured maximum OS memory usage: %d kB\n",
				 (info->num_pages_allocated * _MALI_OSK_CPU_PAGE_SIZE)/1024, (info->num_pages_max* _MALI_OSK_CPU_PAGE_SIZE)/1024));

		/* Loop termination; decide on result */
		if (pages_allocated)
		{
			MALI_DEBUG_PRINT(6, ("Allocated %d pages\n", pages_allocated));
			if (left) result = MALI_MEM_ALLOC_PARTIAL;
			else result = MALI_MEM_ALLOC_FINISHED;

            /* Some OS do not perform a full cache flush (including all outer caches) for uncached mapped memory.
             * They zero the memory through a cached mapping, then flush the inner caches but not the outer caches.
             * This is required for MALI to have the correct view of the memory.
             */
            _mali_osk_cache_ensure_uncached_range_flushed( (void *)descriptor, allocation->offset_start, pages_allocated *_MALI_OSK_CPU_PAGE_SIZE );
			allocation->num_pages = pages_allocated;
			allocation->engine = engine;         /* Necessary to make the engine's unmap call */
			allocation->descriptor = descriptor; /* Necessary to make the engine's unmap call */
			info->num_pages_allocated += pages_allocated;

			MALI_DEBUG_PRINT(6, ("%d out of %d pages now allocated\n", info->num_pages_allocated, info->num_pages_max));

			alloc_info->ctx = info;
			alloc_info->handle = allocation;
			alloc_info->release = os_allocator_release;
		}
		else
		{
			MALI_DEBUG_PRINT(6, ("Releasing pages array due to no pages allocated\n"));
			_mali_osk_free( allocation );
		}
	}

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	return result;
}

static void os_allocator_release(void * ctx, void * handle)
{
	os_allocator * info;
	os_allocation * allocation;
	mali_allocation_engine * engine;
	mali_memory_allocation * descriptor;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	MALI_DEBUG_ASSERT_POINTER(handle);

	info = (os_allocator*)ctx;
	allocation = (os_allocation*)handle;
	engine = allocation->engine;
	descriptor = allocation->descriptor;

	MALI_DEBUG_ASSERT_POINTER( engine );
	MALI_DEBUG_ASSERT_POINTER( descriptor );

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW))
	{
		MALI_DEBUG_PRINT(1, ("allocator release: Failed to get mutex\n"));
		return;
	}

	MALI_DEBUG_PRINT(6, ("Releasing %d os pages\n", allocation->num_pages));

	MALI_DEBUG_ASSERT( allocation->num_pages <= info->num_pages_allocated);
	info->num_pages_allocated -= allocation->num_pages;

	mali_allocation_engine_unmap_physical( engine, descriptor, allocation->offset_start, _MALI_OSK_CPU_PAGE_SIZE*allocation->num_pages, _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR );

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	_mali_osk_free(allocation);
}

static mali_physical_memory_allocation_result os_allocator_allocate_page_table_block(void * ctx, mali_page_table_block * block)
{
	int allocation_order = 6; /* _MALI_OSK_CPU_PAGE_SIZE << 6 */
	void *virt = NULL;
	u32 size = _MALI_OSK_CPU_PAGE_SIZE << allocation_order;
	os_allocator * info;

	u32 cpu_phys_base;

	MALI_DEBUG_ASSERT_POINTER(ctx);
	info = (os_allocator*)ctx;

	/* Ensure we don't allocate more than we're supposed to from the ctx */
	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW)) return MALI_MEM_ALLOC_INTERNAL_FAILURE;

	/* if the number of pages to be requested lead to exceeding the memory
	 * limit in info->num_pages_max, reduce the size that is to be requested. */
	while ( (info->num_pages_allocated + (1 << allocation_order) > info->num_pages_max)
	        && _mali_osk_mem_check_allocated(info->num_pages_max * _MALI_OSK_CPU_PAGE_SIZE) )
	{
		if ( allocation_order > 0 ) {
			--allocation_order;
		} else {
			/* return OOM */
			_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);
			return MALI_MEM_ALLOC_NONE;
		}
	}

	/* try to allocate 2^(allocation_order) pages, if that fails, try
	 * allocation_order-1 to allocation_order 0 (inclusive) */
	while ( allocation_order >= 0 )
	{
		size = _MALI_OSK_CPU_PAGE_SIZE << allocation_order;
		virt = _mali_osk_mem_allocioregion( &cpu_phys_base, size );

		if (NULL != virt) break;

		--allocation_order;
	}

	if ( NULL == virt )
	{
		MALI_DEBUG_PRINT(1, ("Failed to allocate consistent memory. Is CONSISTENT_DMA_SIZE set too low?\n"));
		/* return OOM */
		_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);
		return MALI_MEM_ALLOC_NONE;
	}

	MALI_DEBUG_PRINT(5, ("os_allocator_allocate_page_table_block: Allocation of order %i succeeded\n",
				allocation_order));

	/* we now know the size of the allocation since we know for what
	 * allocation_order the allocation succeeded */
	size = _MALI_OSK_CPU_PAGE_SIZE << allocation_order;


	block->release = os_allocator_page_table_block_release;
	block->ctx = ctx;
	block->handle = (void*)allocation_order;
	block->size = size;
	block->phys_base = cpu_phys_base - info->cpu_usage_adjust;
	block->mapping = virt;

	info->num_pages_allocated += (1 << allocation_order);

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);

	return MALI_MEM_ALLOC_FINISHED;
}

static void os_allocator_page_table_block_release( mali_page_table_block *page_table_block )
{
	os_allocator * info;
	u32 allocation_order;
	u32 pages_allocated;

	MALI_DEBUG_ASSERT_POINTER( page_table_block );

	info = (os_allocator*)page_table_block->ctx;

	MALI_DEBUG_ASSERT_POINTER( info );

	allocation_order = (u32)page_table_block->handle;

	pages_allocated = 1 << allocation_order;

	MALI_DEBUG_ASSERT( pages_allocated * _MALI_OSK_CPU_PAGE_SIZE == page_table_block->size );

	if (_MALI_OSK_ERR_OK != _mali_osk_lock_wait(info->mutex, _MALI_OSK_LOCKMODE_RW))
	{
		MALI_DEBUG_PRINT(1, ("allocator release: Failed to get mutex\n"));
		return;
	}

	MALI_DEBUG_ASSERT( pages_allocated <= info->num_pages_allocated);
	info->num_pages_allocated -= pages_allocated;

	/* Adjust phys_base from mali physical address to CPU physical address */
	_mali_osk_mem_freeioregion( page_table_block->phys_base + info->cpu_usage_adjust, page_table_block->size, page_table_block->mapping );

	_mali_osk_lock_signal(info->mutex, _MALI_OSK_LOCKMODE_RW);
}
