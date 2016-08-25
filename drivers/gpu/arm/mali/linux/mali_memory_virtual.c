/*
 * Copyright (C) 2013-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_kernel_linux.h"
#include "mali_scheduler.h"
#include "mali_memory_os_alloc.h"
#include "mali_memory_manager.h"
#include "mali_memory_virtual.h"


/**
*internal helper to link node into the rb-tree
*/
static inline void _mali_vma_offset_add_rb(struct mali_allocation_manager *mgr,
		struct mali_vma_node *node)
{
	struct rb_node **iter = &mgr->allocation_mgr_rb.rb_node;
	struct rb_node *parent = NULL;
	struct mali_vma_node *iter_node;

	while (likely(*iter)) {
		parent = *iter;
		iter_node = rb_entry(*iter, struct mali_vma_node, vm_rb);

		if (node->vm_node.start < iter_node->vm_node.start)
			iter = &(*iter)->rb_left;
		else if (node->vm_node.start > iter_node->vm_node.start)
			iter = &(*iter)->rb_right;
		else
			MALI_DEBUG_ASSERT(0);
	}

	rb_link_node(&node->vm_rb, parent, iter);
	rb_insert_color(&node->vm_rb, &mgr->allocation_mgr_rb);
}

/**
 * mali_vma_offset_add() - Add offset node to RB Tree
 */
int mali_vma_offset_add(struct mali_allocation_manager *mgr,
			struct mali_vma_node *node)
{
	int ret = 0;
	write_lock(&mgr->vm_lock);

	if (node->vm_node.allocated) {
		goto out;
	}

	_mali_vma_offset_add_rb(mgr, node);
	/* set to allocated */
	node->vm_node.allocated = 1;

out:
	write_unlock(&mgr->vm_lock);
	return ret;
}

/**
 * mali_vma_offset_remove() - Remove offset node from RB tree
 */
void mali_vma_offset_remove(struct mali_allocation_manager *mgr,
			    struct mali_vma_node *node)
{
	write_lock(&mgr->vm_lock);

	if (node->vm_node.allocated) {
		rb_erase(&node->vm_rb, &mgr->allocation_mgr_rb);
		memset(&node->vm_node, 0, sizeof(node->vm_node));
	}
	write_unlock(&mgr->vm_lock);
}

/**
* mali_vma_offset_search - Search the node in RB tree
*/
struct mali_vma_node *mali_vma_offset_search(struct mali_allocation_manager *mgr,
		unsigned long start, unsigned long pages)
{
	struct mali_vma_node *node, *best;
	struct rb_node *iter;
	unsigned long offset;
	read_lock(&mgr->vm_lock);

	iter = mgr->allocation_mgr_rb.rb_node;
	best = NULL;

	while (likely(iter)) {
		node = rb_entry(iter, struct mali_vma_node, vm_rb);
		offset = node->vm_node.start;
		if (start >= offset) {
			iter = iter->rb_right;
			best = node;
			if (start == offset)
				break;
		} else {
			iter = iter->rb_left;
		}
	}

	if (best) {
		offset = best->vm_node.start + best->vm_node.size;
		if (offset <= start + pages)
			best = NULL;
	}
	read_unlock(&mgr->vm_lock);

	return best;
}

