/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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

_mali_osk_errcode_t mali_mem_os_get_table_page(u32 *phys, mali_io_address *mapping);

void mali_mem_os_release_table_page(u32 phys, void *virt);

_mali_osk_errcode_t mali_mem_os_init(void);
void mali_mem_os_term(void);
u32 mali_mem_os_stat(void);

#endif /* __MALI_MEMORY_OS_ALLOC_H__ */
