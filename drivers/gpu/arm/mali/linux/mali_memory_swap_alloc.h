/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_MEMORY_SWAP_ALLOC_H__
#define __MALI_MEMORY_SWAP_ALLOC_H__

#include "mali_osk.h"
#include "mali_session.h"

#include "mali_memory_types.h"
#include "mali_pp_job.h"

/**
 * Initialize memory swapping module.
 */
_mali_osk_errcode_t mali_mem_swap_init(void);

void mali_mem_swap_term(void);

/**
 * Return global share memory file to other modules.
 */
struct file *mali_mem_swap_get_global_swap_file(void);

/**
 * Unlock the given memory backend and pages in it could be swapped out by kernel.
 */
void mali_mem_swap_unlock_single_mem_backend(mali_mem_backend *mem_bkend);

/**
 * Remove the given memory backend from global swap list.
 */
void mali_memory_swap_list_backend_delete(mali_mem_backend *mem_bkend);

/**
 * Add the given memory backend to global swap list.
 */
void mali_memory_swap_list_backend_add(mali_mem_backend *mem_bkend);

/**
 * Allocate 1 index from bitmap used as page index in global swap file.
 */
u32 mali_mem_swap_idx_alloc(void);

void mali_mem_swap_idx_free(u32 idx);

/**
 * Allocate a new swap item without page index.
 */
struct mali_swap_item *mali_mem_swap_alloc_swap_item(void);

/**
 * Free a swap item, truncate the corresponding space in page cache and free index of page.
 */
void mali_mem_swap_free_swap_item(mali_swap_item *swap_item);

/**
 * Allocate a page node with swap item.
 */
struct mali_page_node *_mali_mem_swap_page_node_allocate(void);

/**
 * Reduce the reference count of given page node and if return 0, just free this page node.
 */
_mali_osk_errcode_t _mali_mem_swap_put_page_node(struct mali_page_node *m_page);

void _mali_mem_swap_page_node_free(struct mali_page_node *m_page);

/**
 * Free a swappable memory backend.
 */
u32 mali_mem_swap_free(mali_mem_swap *swap_mem);

/**
 * Ummap and free.
 */
u32 mali_mem_swap_release(mali_mem_backend *mem_bkend, mali_bool is_mali_mapped);

/**
 * Read in a page from global swap file with the pre-allcated page index.
 */
mali_bool mali_mem_swap_in_page_node(struct mali_page_node *page_node);

int mali_mem_swap_alloc_pages(mali_mem_swap *swap_mem, u32 size, u32 *bkend_idx);

_mali_osk_errcode_t mali_mem_swap_mali_map(mali_mem_swap *swap_mem, struct mali_session_data *session, u32 vaddr, u32 props);

void mali_mem_swap_mali_unmap(mali_mem_allocation *alloc);

/**
 * When pp job created, we need swap in all of memory backend needed by this pp job.
 */
int mali_mem_swap_in_pages(struct mali_pp_job *job);

/**
 * Put all of memory backends used this pp job to the global swap list.
 */
int mali_mem_swap_out_pages(struct mali_pp_job *job);

/**
 * This will be called in page fault to process CPU read&write.
 */
int mali_mem_swap_allocate_page_on_demand(mali_mem_backend *mem_bkend, u32 offset, struct page **pagep) ;

/**
 * Used to process cow on demand for swappable memory backend.
 */
int mali_mem_swap_cow_page_on_demand(mali_mem_backend *mem_bkend, u32 offset, struct page **pagep);

#ifdef MALI_MEM_SWAP_TRACKING
void mali_mem_swap_tracking(u32 *swap_pool_size, u32 *unlock_size);
#endif
#endif /* __MALI_MEMORY_SWAP_ALLOC_H__ */

