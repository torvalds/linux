/*
 * Copyright (C) 2010, 2013, 2015-2017 ARM Limited. All rights reserved.
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
#include <linux/spinlock.h>

#include "mali_memory_types.h"

#define MALI_BLOCK_SIZE (PAGE_SIZE)  /* 4 kB, manage BLOCK memory as page size */
#define MALI_BLOCK_REF_MASK (0xFFF)
#define MALI_BLOCK_MAX_REF_COUNT (0xFFF)



typedef struct mali_block_allocator {
	/*
	* In free list, each node's ref_count is 0,
	* ref_count added when allocated or referenced in COW
	*/
	mali_block_item *items; /* information for each block item*/
	struct list_head free; /*free list of mali_memory_node*/
	spinlock_t sp_lock; /*lock for reference count & free list opertion*/
	u32 total_num; /* Number of total pages*/
	atomic_t free_num; /*number of free pages*/
} mali_block_allocator;

unsigned long _mali_blk_item_get_phy_addr(mali_block_item *item);
unsigned long _mali_blk_item_get_pfn(mali_block_item *item);
u32 mali_mem_block_get_ref_count(mali_page_node *node);
u32 mali_mem_block_add_ref(mali_page_node *node);
u32 mali_mem_block_dec_ref(mali_page_node *node);
u32 mali_mem_block_release(mali_mem_backend *mem_bkend);
int mali_mem_block_alloc(mali_mem_block_mem *block_mem, u32 size);
int mali_mem_block_mali_map(mali_mem_block_mem *block_mem, struct mali_session_data *session, u32 vaddr, u32 props);
void mali_mem_block_mali_unmap(mali_mem_allocation *alloc);

int mali_mem_block_cpu_map(mali_mem_backend *mem_bkend, struct vm_area_struct *vma);
_mali_osk_errcode_t mali_memory_core_resource_dedicated_memory(u32 start, u32 size);
mali_bool mali_memory_have_dedicated_memory(void);
u32 mali_mem_block_free(mali_mem_block_mem *block_mem);
u32 mali_mem_block_free_list(struct list_head *list);
void mali_mem_block_free_node(struct mali_page_node *node);
void mali_mem_block_allocator_destroy(void);
_mali_osk_errcode_t mali_mem_block_unref_node(struct mali_page_node *node);
u32 mali_mem_block_allocator_stat(void);

#endif /* __MALI_BLOCK_ALLOCATOR_H__ */
