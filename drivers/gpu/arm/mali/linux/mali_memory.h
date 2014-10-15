/*
 * Copyright (C) 2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_H__
#define __MALI_MEMORY_H__

#include "mali_osk.h"
#include "mali_session.h"

#include <linux/list.h>
#include <linux/mm.h>

#include "mali_memory_types.h"
#include "mali_memory_os_alloc.h"

_mali_osk_errcode_t mali_memory_initialize(void);
void mali_memory_terminate(void);

/** @brief Allocate a page table page
 *
 * Allocate a page for use as a page directory or page table. The page is
 * mapped into kernel space.
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise an error code
 * @param table_page GPU pointer to the allocated page
 * @param mapping CPU pointer to the mapping of the allocated page
 */
MALI_STATIC_INLINE _mali_osk_errcode_t mali_mmu_get_table_page(u32 *table_page, mali_io_address *mapping)
{
	return mali_mem_os_get_table_page(table_page, mapping);
}

/** @brief Release a page table page
 *
 * Release a page table page allocated through \a mali_mmu_get_table_page
 *
 * @param pa the GPU address of the page to release
 */
MALI_STATIC_INLINE void mali_mmu_release_table_page(u32 phys, void *virt)
{
	mali_mem_os_release_table_page(phys, virt);
}

/** @brief mmap function
 *
 * mmap syscalls on the Mali device node will end up here.
 *
 * This function allocates Mali memory and maps it on CPU and Mali.
 */
int mali_mmap(struct file *filp, struct vm_area_struct *vma);

/** @brief Allocate and initialize a Mali memory descriptor
 *
 * @param session Pointer to the session allocating the descriptor
 * @param type Type of memory the descriptor will represent
 */
mali_mem_allocation *mali_mem_descriptor_create(struct mali_session_data *session, mali_mem_type type);

/** @brief Destroy a Mali memory descriptor
 *
 * This function will only free the descriptor itself, and not the memory it
 * represents.
 *
 * @param descriptor Pointer to the descriptor to destroy
 */
void mali_mem_descriptor_destroy(mali_mem_allocation *descriptor);

/** @brief Start a new memory session
 *
 * Called when a process opens the Mali device node.
 *
 * @param session Pointer to session to initialize
 */
_mali_osk_errcode_t mali_memory_session_begin(struct mali_session_data *session);

/** @brief Close a memory session
 *
 * Called when a process closes the Mali device node.
 *
 * Memory allocated by the session will be freed
 *
 * @param session Pointer to the session to terminate
 */
void mali_memory_session_end(struct mali_session_data *session);

/** @brief Prepare Mali page tables for mapping
 *
 * This function will prepare the Mali page tables for mapping the memory
 * described by \a descriptor.
 *
 * Page tables will be reference counted and allocated, if not yet present.
 *
 * @param descriptor Pointer to the memory descriptor to the mapping
 */
_mali_osk_errcode_t mali_mem_mali_map_prepare(mali_mem_allocation *descriptor);

/** @brief Free Mali page tables for mapping
 *
 * This function will unmap pages from Mali memory and free the page tables
 * that are now unused.
 *
 * The updated pages in the Mali L2 cache will be invalidated, and the MMU TLBs will be zapped if necessary.
 *
 * @param descriptor Pointer to the memory descriptor to unmap
 */
void mali_mem_mali_map_free(mali_mem_allocation *descriptor);

/** @brief Parse resource and prepare the OS memory allocator
 *
 * @param size Maximum size to allocate for Mali GPU.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t mali_memory_core_resource_os_memory(u32 size);

/** @brief Parse resource and prepare the dedicated memory allocator
 *
 * @param start Physical start address of dedicated Mali GPU memory.
 * @param size Size of dedicated Mali GPU memory.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(u32 start, u32 size);


void mali_mem_ump_release(mali_mem_allocation *descriptor);
void mali_mem_external_release(mali_mem_allocation *descriptor);

#endif /* __MALI_MEMORY_H__ */
