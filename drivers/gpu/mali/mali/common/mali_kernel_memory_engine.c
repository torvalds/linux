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
#include "mali_osk_list.h"

typedef struct memory_engine
{
	mali_kernel_mem_address_manager * mali_address;
	mali_kernel_mem_address_manager * process_address;
} memory_engine;

mali_allocation_engine mali_allocation_engine_create(mali_kernel_mem_address_manager * mali_address_manager, mali_kernel_mem_address_manager * process_address_manager)
{
	memory_engine * engine;

	/* Mali Address Manager need not support unmap_physical */
	MALI_DEBUG_ASSERT_POINTER(mali_address_manager);
	MALI_DEBUG_ASSERT_POINTER(mali_address_manager->allocate);
	MALI_DEBUG_ASSERT_POINTER(mali_address_manager->release);
	MALI_DEBUG_ASSERT_POINTER(mali_address_manager->map_physical);

	/* Process Address Manager must support unmap_physical for OS allocation
	 * error path handling */
	MALI_DEBUG_ASSERT_POINTER(process_address_manager);
	MALI_DEBUG_ASSERT_POINTER(process_address_manager->allocate);
	MALI_DEBUG_ASSERT_POINTER(process_address_manager->release);
	MALI_DEBUG_ASSERT_POINTER(process_address_manager->map_physical);
	MALI_DEBUG_ASSERT_POINTER(process_address_manager->unmap_physical);


	engine = (memory_engine*)_mali_osk_malloc(sizeof(memory_engine));
	if (NULL == engine) return NULL;

	engine->mali_address = mali_address_manager;
	engine->process_address = process_address_manager;

	return (mali_allocation_engine)engine;
}

void mali_allocation_engine_destroy(mali_allocation_engine engine)
{
	MALI_DEBUG_ASSERT_POINTER(engine);
	_mali_osk_free(engine);
}

_mali_osk_errcode_t mali_allocation_engine_allocate_memory(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor, mali_physical_memory_allocator * physical_allocators, _mali_osk_list_t *tracking_list )
{
	memory_engine * engine = (memory_engine*)mem_engine;

	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_POINTER(physical_allocators);
	/* ASSERT that the list member has been initialized, even if it won't be
	 * used for tracking. We need it to be initialized to see if we need to
	 * delete it from a list in the release function. */
	MALI_DEBUG_ASSERT( NULL != descriptor->list.next && NULL != descriptor->list.prev );

	if (_MALI_OSK_ERR_OK == engine->mali_address->allocate(descriptor))
	{
		_mali_osk_errcode_t res = _MALI_OSK_ERR_OK;
		if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
		{
			res = engine->process_address->allocate(descriptor);
		}
		if ( _MALI_OSK_ERR_OK == res )
		{
			/* address space setup OK, commit physical memory to the allocation */
			mali_physical_memory_allocator * active_allocator = physical_allocators;
			struct mali_physical_memory_allocation * active_allocation_tracker = &descriptor->physical_allocation;
			u32 offset = 0;

			while ( NULL != active_allocator )
			{
				switch (active_allocator->allocate(active_allocator->ctx, mem_engine, descriptor, &offset, active_allocation_tracker))
				{
					case MALI_MEM_ALLOC_FINISHED:
						if ( NULL != tracking_list )
						{
							/* Insert into the memory session list */
							/* ASSERT that it is not already part of a list */
							MALI_DEBUG_ASSERT( _mali_osk_list_empty( &descriptor->list ) );
							_mali_osk_list_add( &descriptor->list, tracking_list );
						}

						MALI_SUCCESS; /* all done */
					case MALI_MEM_ALLOC_NONE:
						/* reuse current active_allocation_tracker */
						MALI_DEBUG_PRINT( 4, ("Memory Engine Allocate: No allocation on %s, resorting to %s\n",
											  ( active_allocator->name ) ? active_allocator->name : "UNNAMED",
											  ( active_allocator->next ) ? (( active_allocator->next->name )? active_allocator->next->name : "UNNAMED") : "NONE") );
						active_allocator = active_allocator->next;
						break;
					case MALI_MEM_ALLOC_PARTIAL:
						if (NULL != active_allocator->next)
						{
							/* need a new allocation tracker */
							active_allocation_tracker->next = _mali_osk_calloc(1, sizeof(mali_physical_memory_allocation));
							if (NULL != active_allocation_tracker->next)
							{
								active_allocation_tracker = active_allocation_tracker->next;
								MALI_DEBUG_PRINT( 2, ("Memory Engine Allocate: Partial allocation on %s, resorting to %s\n",
													  ( active_allocator->name ) ? active_allocator->name : "UNNAMED",
													  ( active_allocator->next ) ? (( active_allocator->next->name )? active_allocator->next->name : "UNNAMED") : "NONE") );
								active_allocator = active_allocator->next;
								break;
							}
						}
					   /* FALL THROUGH */
					case MALI_MEM_ALLOC_INTERNAL_FAILURE:
					   active_allocator = NULL; /* end the while loop */
					   break;
				}
			}

			MALI_PRINT(("Memory allocate failed, could not allocate size %d kB.\n", descriptor->size/1024));

			/* allocation failure, start cleanup */
			/* loop over any potential partial allocations */
			active_allocation_tracker = &descriptor->physical_allocation;
			while (NULL != active_allocation_tracker)
			{
				/* handle blank trackers which will show up during failure */
				if (NULL != active_allocation_tracker->release)
				{
					active_allocation_tracker->release(active_allocation_tracker->ctx, active_allocation_tracker->handle);
				}
				active_allocation_tracker = active_allocation_tracker->next;
			}

			/* free the allocation tracker objects themselves, skipping the tracker stored inside the descriptor itself */
			for ( active_allocation_tracker = descriptor->physical_allocation.next; active_allocation_tracker != NULL; )
			{
				void * buf = active_allocation_tracker;
				active_allocation_tracker = active_allocation_tracker->next;
				_mali_osk_free(buf);
			}

			/* release the address spaces */

			if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
			{
				engine->process_address->release(descriptor);
			}
		}
		engine->mali_address->release(descriptor);
	}

	MALI_ERROR(_MALI_OSK_ERR_FAULT);
}

void mali_allocation_engine_release_memory(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor)
{
	mali_allocation_engine_release_pt1_mali_pagetables_unmap(mem_engine, descriptor);
	mali_allocation_engine_release_pt2_physical_memory_free(mem_engine, descriptor);
}

void mali_allocation_engine_release_pt1_mali_pagetables_unmap(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor)
{
	memory_engine * engine = (memory_engine*)mem_engine;

	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);

	/* Calling: mali_address_manager_release()  */
	/* This function is allowed to be called several times, and it only does the release on the first call. */
	engine->mali_address->release(descriptor);
}

void mali_allocation_engine_release_pt2_physical_memory_free(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor)
{
	memory_engine * engine = (memory_engine*)mem_engine;
	mali_physical_memory_allocation * active_allocation_tracker;

	/* Remove this from a tracking list in session_data->memory_head */
	if ( ! _mali_osk_list_empty( &descriptor->list ) )
	{
		_mali_osk_list_del( &descriptor->list );
		/* Clear the list for debug mode, catch use-after-free */
		MALI_DEBUG_CODE( descriptor->list.next = descriptor->list.prev = NULL; )
	}

	active_allocation_tracker = &descriptor->physical_allocation;
	while (NULL != active_allocation_tracker)
	{
		MALI_DEBUG_ASSERT_POINTER(active_allocation_tracker->release);
		active_allocation_tracker->release(active_allocation_tracker->ctx, active_allocation_tracker->handle);
		active_allocation_tracker = active_allocation_tracker->next;
	}

	/* free the allocation tracker objects themselves, skipping the tracker stored inside the descriptor itself */
	for ( active_allocation_tracker = descriptor->physical_allocation.next; active_allocation_tracker != NULL; )
	{
		void * buf = active_allocation_tracker;
		active_allocation_tracker = active_allocation_tracker->next;
		_mali_osk_free(buf);
	}

	if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
	{
		engine->process_address->release(descriptor);
	}
}

_mali_osk_errcode_t mali_allocation_engine_map_physical(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor, u32 offset, u32 phys, u32 cpu_usage_adjust, u32 size)
{
	_mali_osk_errcode_t err;
	memory_engine * engine = (memory_engine*)mem_engine;
	_mali_osk_mem_mapregion_flags_t unmap_flags = (_mali_osk_mem_mapregion_flags_t)0;

	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);

	MALI_DEBUG_PRINT(7, ("Mapping phys 0x%08X length 0x%08X at offset 0x%08X\n", phys, size, offset));

	MALI_DEBUG_ASSERT_POINTER(engine->mali_address);
	MALI_DEBUG_ASSERT_POINTER(engine->mali_address->map_physical);

	/* Handle process address manager first, because we may need them to
	 * allocate the physical page */
	if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
	{
		/* Handle OS-allocated specially, since an adjustment may be required */
		if ( MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC == phys )
		{
			MALI_DEBUG_ASSERT( _MALI_OSK_CPU_PAGE_SIZE == size );

			/* Set flags to use on error path */
			unmap_flags |= _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR;

			err = engine->process_address->map_physical(descriptor, offset, &phys, size);
			/* Adjust for cpu physical address to mali physical address */
			phys -= cpu_usage_adjust;
		}
		else
		{
			u32 cpu_phys;
			/* Adjust mali physical address to cpu physical address */
			cpu_phys = phys + cpu_usage_adjust;
			err = engine->process_address->map_physical(descriptor, offset, &cpu_phys, size);
		}

		if ( _MALI_OSK_ERR_OK != err )
		{
			MALI_ERROR( err );
		}
	}

	MALI_DEBUG_PRINT(7, ("Mapping phys 0x%08X length 0x%08X at offset 0x%08X to CPUVA 0x%08X\n", phys, size, offset, (u32)(descriptor->mapping) + offset));

	/* Mali address manager must use the physical address - no point in asking
	 * it to allocate another one for us */
	MALI_DEBUG_ASSERT( MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC != phys );

	err = engine->mali_address->map_physical(descriptor, offset, &phys, size);

	if ( _MALI_OSK_ERR_OK != err )
	{
		if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
		{
			MALI_DEBUG_PRINT( 2, ("Process address manager succeeded, but Mali Address manager failed for phys=0x%08X size=0x%08X, offset=0x%08X. Will unmap.\n", phys, size, offset));
			engine->process_address->unmap_physical(descriptor, offset, size, unmap_flags);
		}

		MALI_ERROR( err );
	}

	MALI_SUCCESS;
}

void mali_allocation_engine_unmap_physical(mali_allocation_engine mem_engine, mali_memory_allocation * descriptor, u32 offset, u32 size, _mali_osk_mem_mapregion_flags_t unmap_flags )
{
	memory_engine * engine = (memory_engine*)mem_engine;

	MALI_DEBUG_ASSERT_POINTER(engine);
	MALI_DEBUG_ASSERT_POINTER(descriptor);

	MALI_DEBUG_PRINT(7, ("UnMapping length 0x%08X at offset 0x%08X\n", size, offset));

	MALI_DEBUG_ASSERT_POINTER(engine->mali_address);
	MALI_DEBUG_ASSERT_POINTER(engine->process_address);

	if ( descriptor->flags & MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE )
	{
		/* Mandetory for process_address manager to have an unmap function*/
		engine->process_address->unmap_physical( descriptor, offset, size, unmap_flags );
	}

	/* Optional for mali_address manager to have an unmap function*/
	if ( NULL != engine->mali_address->unmap_physical )
	{
		engine->mali_address->unmap_physical( descriptor, offset, size, unmap_flags );
	}
}


_mali_osk_errcode_t mali_allocation_engine_allocate_page_tables(mali_allocation_engine engine, mali_page_table_block * descriptor, mali_physical_memory_allocator * physical_provider)
{
	mali_physical_memory_allocator * active_allocator = physical_provider;

	MALI_DEBUG_ASSERT_POINTER(descriptor);
	MALI_DEBUG_ASSERT_POINTER(physical_provider);

	while ( NULL != active_allocator )
	{
		switch (active_allocator->allocate_page_table_block(active_allocator->ctx, descriptor))
		{
			case MALI_MEM_ALLOC_FINISHED:
				MALI_SUCCESS; /* all done */
			case MALI_MEM_ALLOC_NONE:
				/* try next */
				MALI_DEBUG_PRINT( 2, ("Memory Engine Allocate PageTables: No allocation on %s, resorting to %s\n",
									  ( active_allocator->name ) ? active_allocator->name : "UNNAMED",
									  ( active_allocator->next ) ? (( active_allocator->next->name )? active_allocator->next->name : "UNNAMED") : "NONE") );
				active_allocator = active_allocator->next;
				break;
			case MALI_MEM_ALLOC_PARTIAL:
				MALI_DEBUG_PRINT(1, ("Invalid return value from allocate_page_table_block call: MALI_MEM_ALLOC_PARTIAL\n"));
				/* FALL THROUGH */
			case MALI_MEM_ALLOC_INTERNAL_FAILURE:
				MALI_DEBUG_PRINT(1, ("Aborting due to allocation failure\n"));
				active_allocator = NULL; /* end the while loop */
				break;
		}
	}

	MALI_ERROR(_MALI_OSK_ERR_FAULT);
}


void mali_allocation_engine_report_allocators( mali_physical_memory_allocator * physical_provider )
{
	mali_physical_memory_allocator * active_allocator = physical_provider;
	MALI_DEBUG_ASSERT_POINTER(physical_provider);

	MALI_DEBUG_PRINT( 1, ("Mali memory allocators will be used in this order of preference (lowest numbered first) :\n"));
	while ( NULL != active_allocator )
	{
		if ( NULL != active_allocator->name )
		{
			MALI_DEBUG_PRINT( 1, ("\t%d: %s\n", active_allocator->alloc_order, active_allocator->name) );
		}
		else
		{
			MALI_DEBUG_PRINT( 1, ("\t%d: (UNNAMED ALLOCATOR)\n", active_allocator->alloc_order) );
		}
		active_allocator = active_allocator->next;
	}

}

u32 mali_allocation_engine_memory_usage(mali_physical_memory_allocator *allocator)
{
	u32 sum = 0;
	while(NULL != allocator)
	{
		/* Only count allocators that have set up a stat function. */
		if(allocator->stat)
			sum += allocator->stat(allocator);

		allocator = allocator->next;
	}

	return sum;
}
