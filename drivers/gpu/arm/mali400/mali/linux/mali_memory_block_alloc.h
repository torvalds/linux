/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2010, 2013-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
