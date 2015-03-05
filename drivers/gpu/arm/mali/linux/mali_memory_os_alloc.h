/*
 * Copyright (C) 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_OS_ALLOC_H__
#define __MALI_MEMORY_OS_ALLOC_H__

#include "mali_osk.h"
#include "mali_session.h"

#include "mali_memory_types.h"

/* OS memory allocator */
/** @brief Allocate memory from OS
 *
 * This function will create a descriptor, allocate pages and map these on the CPU and Mali.
 *
 * @param mali_addr Mali virtual address to use for Mali mapping
 * @param size Size to allocate
 * @param vma Pointer to vma for CPU mapping
 * @param session Pointer to session doing the allocation
 */
mali_mem_allocation *mali_mem_os_alloc(u32 mali_addr, u32 size, struct vm_area_struct *vma, struct mali_session_data *session);

/** @brief Release Mali OS memory
 *
 * The session memory_lock must be held when calling this function.
 *
 * @param descriptor Pointer to the descriptor to release
 */
void mali_mem_os_release(mali_mem_allocation *descriptor);

_mali_osk_errcode_t mali_mem_os_get_table_page(mali_dma_addr *phys, mali_io_address *mapping);

void mali_mem_os_release_table_page(mali_dma_addr phys, void *virt);

_mali_osk_errcode_t mali_mem_os_init(void);
void mali_mem_os_term(void);
u32 mali_mem_os_stat(void);

#endif /* __MALI_MEMORY_OS_ALLOC_H__ */
