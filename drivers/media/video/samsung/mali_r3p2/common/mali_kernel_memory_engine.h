/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_MEMORY_ENGINE_H__
#define  __MALI_KERNEL_MEMORY_ENGINE_H__

typedef void * mali_allocation_engine;

typedef enum { MALI_MEM_ALLOC_FINISHED, MALI_MEM_ALLOC_PARTIAL, MALI_MEM_ALLOC_NONE, MALI_MEM_ALLOC_INTERNAL_FAILURE } mali_physical_memory_allocation_result;

typedef struct mali_physical_memory_allocation
{
	void (*release)(void * ctx, void * handle); /**< Function to call on to release the physical memory */
	void * ctx;
	void * handle;
	struct mali_physical_memory_allocation * next;
} mali_physical_memory_allocation;

struct mali_page_table_block;

typedef struct mali_page_table_block
{
	void (*release)(struct mali_page_table_block *page_table_block);
	void * ctx;
	void * handle;
	u32 size; /**< In bytes, should be a multiple of MALI_MMU_PAGE_SIZE to avoid internal fragementation */
	u32 phys_base; /**< Mali physical address */
	mali_io_address mapping;
} mali_page_table_block;


/** @addtogroup _mali_osk_low_level_memory
 * @{ */

typedef enum
{
	MALI_MEMORY_ALLOCATION_FLAG_MAP_INTO_USERSPACE = 0x1,
	MALI_MEMORY_ALLOCATION_FLAG_MAP_GUARD_PAGE     = 0x2,
} mali_memory_allocation_flag;

/**
 * Supplying this 'magic' physical address requests that the OS allocate the
 * physical address at page commit time, rather than committing a specific page
 */
#define MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC ((u32)(-1))

typedef struct mali_memory_allocation
{
	/* Information about the allocation */
	void * mapping; /**< CPU virtual address where the memory is mapped at */
	u32 mali_address; /**< The Mali seen address of the memory allocation */
	u32 size; /**< Size of the allocation */
	u32 permission; /**< Permission settings */
	mali_memory_allocation_flag flags;
	u32 cache_settings; /* type: mali_memory_cache_settings, found in <linux/mali/mali_utgard_uk_types.h> Ump DD breaks if we include it...*/

	_mali_osk_lock_t * lock;

	/* Manager specific information pointers */
	void * mali_addr_mapping_info; /**< Mali address allocation specific info */
	void * process_addr_mapping_info; /**< Mapping manager specific info */

	mali_physical_memory_allocation physical_allocation;

	_mali_osk_list_t list; /**< List for linking together memory allocations into the session's memory head */
} mali_memory_allocation;
/** @} */ /* end group _mali_osk_low_level_memory */


typedef struct mali_physical_memory_allocator
{
	mali_physical_memory_allocation_result (*allocate)(void* ctx, mali_allocation_engine * engine, mali_memory_allocation * descriptor, u32* offset, mali_physical_memory_allocation * alloc_info);
	mali_physical_memory_allocation_result (*allocate_page_table_block)(void * ctx, mali_page_table_block * block); /* MALI_MEM_ALLOC_PARTIAL not allowed */
	void (*destroy)(struct mali_physical_memory_allocator * allocator);
	u32 (*stat)(struct mali_physical_memory_allocator * allocator);
	void * ctx;
	const char * name; /**< Descriptive name for use in mali_allocation_engine_report_allocators, or NULL */
	u32 alloc_order; /**< Order in which the allocations should happen */
	struct mali_physical_memory_allocator * next;
} mali_physical_memory_allocator;

typedef struct mali_kernel_mem_address_manager
{
	_mali_osk_errcode_t (*allocate)(mali_memory_allocation *); /**< Function to call to reserve an address */
	void (*release)(mali_memory_allocation *); /**< Function to call to free the address allocated */

	 /**
	  * Function called for each physical sub allocation.
	  * Called for each physical block allocated by the physical memory manager.
	  * @param[in] descriptor The memory descriptor in question
	  * @param[in] off Offset from the start of range
	  * @param[in,out] phys_addr A pointer to the physical address of the start of the
	  * physical block. When *phys_addr == MALI_MEMORY_ALLOCATION_OS_ALLOCATED_PHYSADDR_MAGIC
	  * is used, this requests the function to allocate the physical page
	  * itself, and return it through the pointer provided.
	  * @param[in] size Length in bytes of the physical block
	  * @return _MALI_OSK_ERR_OK on success.
	  * A value of type _mali_osk_errcode_t other than _MALI_OSK_ERR_OK indicates failure.
	  * Specifically, _MALI_OSK_ERR_UNSUPPORTED indicates that the function
	  * does not support allocating physical pages itself.
	  */
	 _mali_osk_errcode_t (*map_physical)(mali_memory_allocation * descriptor, u32 offset, u32 *phys_addr, u32 size);

	 /**
	  * Function called to remove a physical sub allocation.
	  * Called on error paths where one of the address managers fails.
	  *
	  * @note this is optional. For address managers where this is not
	  * implemented, the value of this member is NULL. The memory engine
	  * currently does not require the mali address manager to be able to
	  * unmap individual pages, but the process address manager must have this
	  * capability.
	  *
	  * @param[in] descriptor The memory descriptor in question
	  * @param[in] off Offset from the start of range
	  * @param[in] size Length in bytes of the physical block
	  * @param[in] flags flags to use on a per-page basis. For OS-allocated
	  * physical pages, this must include _MALI_OSK_MEM_MAPREGION_FLAG_OS_ALLOCATED_PHYSADDR.
	  * @return _MALI_OSK_ERR_OK on success.
	  * A value of type _mali_osk_errcode_t other than _MALI_OSK_ERR_OK indicates failure.
	  */
	void (*unmap_physical)(mali_memory_allocation * descriptor, u32 offset, u32 size, _mali_osk_mem_mapregion_flags_t flags);

} mali_kernel_mem_address_manager;

mali_allocation_engine mali_allocation_engine_create(mali_kernel_mem_address_manager * mali_address_manager, mali_kernel_mem_address_manager * process_address_manager);

void mali_allocation_engine_destroy(mali_allocation_engine engine);

int mali_allocation_engine_allocate_memory(mali_allocation_engine engine, mali_memory_allocation * descriptor, mali_physical_memory_allocator * physical_provider, _mali_osk_list_t *tracking_list );
void mali_allocation_engine_release_memory(mali_allocation_engine engine, mali_memory_allocation * descriptor);

void mali_allocation_engine_release_pt1_mali_pagetables_unmap(mali_allocation_engine engine, mali_memory_allocation * descriptor);
void mali_allocation_engine_release_pt2_physical_memory_free(mali_allocation_engine engine, mali_memory_allocation * descriptor);

int mali_allocation_engine_map_physical(mali_allocation_engine engine, mali_memory_allocation * descriptor, u32 offset, u32 phys, u32 cpu_usage_adjust, u32 size);
void mali_allocation_engine_unmap_physical(mali_allocation_engine engine, mali_memory_allocation * descriptor, u32 offset, u32 size, _mali_osk_mem_mapregion_flags_t unmap_flags);

int mali_allocation_engine_allocate_page_tables(mali_allocation_engine, mali_page_table_block * descriptor, mali_physical_memory_allocator * physical_provider);

void mali_allocation_engine_report_allocators(mali_physical_memory_allocator * physical_provider);

u32 mali_allocation_engine_memory_usage(mali_physical_memory_allocator *allocator);

#endif /* __MALI_KERNEL_MEMORY_ENGINE_H__ */
