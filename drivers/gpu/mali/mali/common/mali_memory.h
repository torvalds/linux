/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
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

struct mali_cluster;
struct mali_group;

/** @brief Initialize Mali memory subsystem
 *
 * Allocate and initialize internal data structures. Must be called before
 * allocating Mali memory.
 *
 * @return On success _MALI_OSK_ERR_OK, othervise some error code describing the error.
 */
_mali_osk_errcode_t mali_memory_initialize(void);

/** @brief Terminate Mali memory system
 *
 * Clean up and release internal data structures.
 */
void mali_memory_terminate(void);

/** @brief Start new Mali memory session
 *
 * Allocate and prepare session specific memory allocation data data. The
 * session page directory, lock, and descriptor map is set up.
 *
 * @param mali_session_data pointer to the session data structure
 */
_mali_osk_errcode_t mali_memory_session_begin(struct mali_session_data *mali_session_data);

/** @brief Close a Mali memory session
 *
 * Release session specific memory allocation related data.
 *
 * @param mali_session_data pointer to the session data structure
 */
void mali_memory_session_end(struct mali_session_data *mali_session_data);

/** @brief Allocate a page table page
 *
 * Allocate a page for use as a page directory or page table. The page is
 * mapped into kernel space.
 *
 * @return _MALI_OSK_ERR_OK on success, othervise an error code
 * @param table_page GPU pointer to the allocated page
 * @param mapping CPU pointer to the mapping of the allocated page
 */
_mali_osk_errcode_t mali_mmu_get_table_page(u32 *table_page, mali_io_address *mapping);

/** @brief Release a page table page
 *
 * Release a page table page allocated through \a mali_mmu_get_table_page
 *
 * @param pa the GPU address of the page to release
 */
void mali_mmu_release_table_page(u32 pa);


/** @brief Parse resource and prepare the OS memory allocator
 */
_mali_osk_errcode_t mali_memory_core_resource_os_memory(_mali_osk_resource_t * resource);

/** @brief Parse resource and prepare the dedicated memory allocator
 */
_mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(_mali_osk_resource_t * resource);

#endif /* __MALI_MEMORY_H__ */
