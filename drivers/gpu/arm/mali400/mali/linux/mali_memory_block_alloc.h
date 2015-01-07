/*
 * Copyright (C) 2010, 2013-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_BLOCK_ALLOCATOR_H__
#define __MALI_BLOCK_ALLOCATOR_H__

#include "mali_session.h"
#include "mali_memory.h"

#include "mali_memory_types.h"

typedef struct mali_mem_allocator mali_mem_allocator;

mali_mem_allocator *mali_block_allocator_create(u32 base_address, u32 cpu_usage_adjust, u32 size);
void mali_mem_block_allocator_destroy(mali_mem_allocator *allocator);

mali_mem_allocation *mali_mem_block_alloc(u32 mali_addr, u32 size, struct vm_area_struct *vma, struct mali_session_data *session);
void mali_mem_block_release(mali_mem_allocation *descriptor);

u32 mali_mem_block_allocator_stat(void);

#endif /* __MALI_BLOCK_ALLOCATOR_H__ */
