/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/

/*
 * Generic simple memory manager implementation. Intended to be used as a base
 * class implementation for more advanced memory managers.
 *
 * Note that the algorithm used is quite simple and there might be substantial
 * performance gains if a smarter free list is implemented. Currently it is just an
 * unordered stack of free regions. This could easily be improved if an RB-tree
 * is used instead. At least if we expect heavy fragmentation.
 *
 * Aligned allocations can also see improvement.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <drm/drmP.h>
#include <drm/drm_mm.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/export.h>

#define MM_UNUSED_TARGET 4

static struct drm_mm_node *drm_mm_kmalloc(struct drm_mm *mm, int atomic)
{
	struct drm_mm_node *child;

	if (atomic)
		child = kzalloc(sizeof(*child), GFP_ATOMIC);
	else
		child = kzalloc(sizeof(*child), GFP_KERNEL);

	if (unlikely(child == NULL)) {
		spin_lock(&mm->unused_lock);
		if (list_empty(&mm->unused_nodes))
			child = NULL;
		else {
			child =
			    list_entry(mm->unused_nodes.next,
				       struct drm_mm_node, node_list);
			list_del(&child->node_list);
			--mm->num_unused;
		}
		spin_unlock(&mm->unused_lock);
	}
	return child;
}

/* drm_mm_pre_get() - pre allocate drm_mm_node structure
 * drm_mm:	memory manager struct we are pre-allocating for
 *
 * Returns 0 on success or -ENOMEM if allocation fails.
 */
int drm_mm_pre_get(struct drm_mm *mm)
{
	struct drm_mm_node *node;

	spin_lock(&mm->unused_lock);
	while (mm->num_unused < MM_UNUSED_TARGET) {
		spin_unlock(&mm->unused_lock);
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		spin_lock(&mm->unused_lock);

		if (unlikely(node == NULL)) {
			int ret = (mm->num_unused < 2) ? -ENOMEM : 0;
			spin_unlock(&mm->unused_lock);
			return ret;
		}
		++mm->num_unused;
		list_add_tail(&node->node_list, &mm->unused_nodes);
	}
	spin_unlock(&mm->unused_lock);
	return 0;
}
EXPORT_SYMBOL(drm_mm_pre_get);

static void drm_mm_insert_helper(struct drm_mm_node *hole_node,
				 struct drm_mm_node *node,
				 unsigned long size, unsigned alignment,
				 unsigned long color)
{
	struct drm_mm *mm = hole_node->mm;
	unsigned long hole_start = drm_mm_hole_node_start(hole_node);
	unsigned long hole_end = drm_mm_hole_node_end(hole_node);
	unsigned long adj_start = hole_start;
	unsigned long adj_end = hole_end;

	BUG_ON(node->allocated);

	if (mm->color_adjust)
		mm->color_adjust(hole_node, color, &adj_start, &adj_end);

	if (alignment) {
		unsigned tmp = adj_start % alignment;
		if (tmp)
			adj_start += alignment - tmp;
	}

	if (adj_start == hole_start) {
		hole_node->hole_follows = 0;
		list_del(&hole_node->hole_stack);
	}

	node->start = adj_start;
	node->size = size;
	node->mm = mm;
	node->color = color;
	node->allocated = 1;

	INIT_LIST_HEAD(&node->hole_stack);
	list_add(&node->node_list, &hole_node->node_list);

	BUG_ON(node->start + node->size > adj_end);

	node->hole_follows = 0;
	if (__drm_mm_hole_node_start(node) < hole_end) {
		list_add(&node->hole_stack, &mm->hole_stack);
		node->hole_follows = 1;
	}
}

int drm_mm_reserve_node(struct drm_mm *mm, struct drm_mm_node *node)
{
	struct drm_mm_node *hole;
	unsigned long end = node->start + node->size;
	unsigned long hole_start;
	unsigned long hole_end;

	BUG_ON(node == NULL);

	/* Find the relevant hole to add our node to */
	drm_mm_for_each_hole(hole, mm, hole_start, hole_end) {
		if (hole_start > node->start || hole_end < end)
			continue;

		node->mm = mm;
		node->allocated = 1;

		INIT_LIST_HEAD(&node->hole_stack);
		list_add(&node->node_list, &hole->node_list);

		if (node->start == hole_start) {
			hole->hole_follows = 0;
			list_del_init(&hole->hole_stack);
		}

		node->hole_follows = 0;
		if (end != hole_end) {
			list_add(&node->hole_stack, &mm->hole_stack);
			node->hole_follows = 1;
		}

		return 0;
	}

	WARN(1, "no hole found for node 0x%lx + 0x%lx\n",
	     node->start, node->size);
	return -ENOSPC;
}
EXPORT_SYMBOL(drm_mm_reserve_node);

struct drm_mm_node *drm_mm_get_block_generic(struct drm_mm_node *hole_node,
					     unsigned long size,
					     unsigned alignment,
					     unsigned long color,
					     int atomic)
{
	struct drm_mm_node *node;

	node = drm_mm_kmalloc(hole_node->mm, atomic);
	if (unlikely(node == NULL))
		return NULL;

	drm_mm_insert_helper(hole_node, node, size, alignment, color);

	return node;
}
EXPORT_SYMBOL(drm_mm_get_block_generic);

/**
 * Search for free space and insert a preallocated memory node. Returns
 * -ENOSPC if no suitable free area is available. The preallocated memory node
 * must be cleared.
 */
int drm_mm_insert_node_generic(struct drm_mm *mm, struct drm_mm_node *node,
			       unsigned long size, unsigned alignment,
			       unsigned long color,
			       enum drm_mm_search_flags flags)
{
	struct drm_mm_node *hole_node;

	hole_node = drm_mm_search_free_generic(mm, size, alignment,
					       color, flags);
	if (!hole_node)
		return -ENOSPC;

	drm_mm_insert_helper(hole_node, node, size, alignment, color);
	return 0;
}
EXPORT_SYMBOL(drm_mm_insert_node_generic);

static void drm_mm_insert_helper_range(struct drm_mm_node *hole_node,
				       struct drm_mm_node *node,
				       unsigned long size, unsigned alignment,
				       unsigned long color,
				       unsigned long start, unsigned long end)
{
	struct drm_mm *mm = hole_node->mm;
	unsigned long hole_start = drm_mm_hole_node_start(hole_node);
	unsigned long hole_end = drm_mm_hole_node_end(hole_node);
	unsigned long adj_start = hole_start;
	unsigned long adj_end = hole_end;

	BUG_ON(!hole_node->hole_follows || node->allocated);

	if (adj_start < start)
		adj_start = start;
	if (adj_end > end)
		adj_end = end;

	if (mm->color_adjust)
		mm->color_adjust(hole_node, color, &adj_start, &adj_end);

	if (alignment) {
		unsigned tmp = adj_start % alignment;
		if (tmp)
			adj_start += alignment - tmp;
	}

	if (adj_start == hole_start) {
		hole_node->hole_follows = 0;
		list_del(&hole_node->hole_stack);
	}

	node->start = adj_start;
	node->size = size;
	node->mm = mm;
	node->color = color;
	node->allocated = 1;

	INIT_LIST_HEAD(&node->hole_stack);
	list_add(&node->node_list, &hole_node->node_list);

	BUG_ON(node->start + node->size > adj_end);
	BUG_ON(node->start + node->size > end);

	node->hole_follows = 0;
	if (__drm_mm_hole_node_start(node) < hole_end) {
		list_add(&node->hole_stack, &mm->hole_stack);
		node->hole_follows = 1;
	}
}

struct drm_mm_node *drm_mm_get_block_range_generic(struct drm_mm_node *hole_node,
						unsigned long size,
						unsigned alignment,
						unsigned long color,
						unsigned long start,
						unsigned long end,
						int atomic)
{
	struct drm_mm_node *node;

	node = drm_mm_kmalloc(hole_node->mm, atomic);
	if (unlikely(node == NULL))
		return NULL;

	drm_mm_insert_helper_range(hole_node, node, size, alignment, color,
				   start, end);

	return node;
}
EXPORT_SYMBOL(drm_mm_get_block_range_generic);

/**
 * Search for free space and insert a preallocated memory node. Returns
 * -ENOSPC if no suitable free area is available. This is for range
 * restricted allocations. The preallocated memory node must be cleared.
 */
int drm_mm_insert_node_in_range_generic(struct drm_mm *mm, struct drm_mm_node *node,
					unsigned long size, unsigned alignment, unsigned long color,
					unsigned long start, unsigned long end,
					enum drm_mm_search_flags flags)
{
	struct drm_mm_node *hole_node;

	hole_node = drm_mm_search_free_in_range_generic(mm,
							size, alignment, color,
							start, end, flags);
	if (!hole_node)
		return -ENOSPC;

	drm_mm_insert_helper_range(hole_node, node,
				   size, alignment, color,
				   start, end);
	return 0;
}
EXPORT_SYMBOL(drm_mm_insert_node_in_range_generic);

/**
 * Remove a memory node from the allocator.
 */
void drm_mm_remove_node(struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;
	struct drm_mm_node *prev_node;

	BUG_ON(node->scanned_block || node->scanned_prev_free
				   || node->scanned_next_free);

	prev_node =
	    list_entry(node->node_list.prev, struct drm_mm_node, node_list);

	if (node->hole_follows) {
		BUG_ON(__drm_mm_hole_node_start(node) ==
		       __drm_mm_hole_node_end(node));
		list_del(&node->hole_stack);
	} else
		BUG_ON(__drm_mm_hole_node_start(node) !=
		       __drm_mm_hole_node_end(node));


	if (!prev_node->hole_follows) {
		prev_node->hole_follows = 1;
		list_add(&prev_node->hole_stack, &mm->hole_stack);
	} else
		list_move(&prev_node->hole_stack, &mm->hole_stack);

	list_del(&node->node_list);
	node->allocated = 0;
}
EXPORT_SYMBOL(drm_mm_remove_node);

/*
 * Remove a memory node from the allocator and free the allocated struct
 * drm_mm_node. Only to be used on a struct drm_mm_node obtained by one of the
 * drm_mm_get_block functions.
 */
void drm_mm_put_block(struct drm_mm_node *node)
{

	struct drm_mm *mm = node->mm;

	drm_mm_remove_node(node);

	spin_lock(&mm->unused_lock);
	if (mm->num_unused < MM_UNUSED_TARGET) {
		list_add(&node->node_list, &mm->unused_nodes);
		++mm->num_unused;
	} else
		kfree(node);
	spin_unlock(&mm->unused_lock);
}
EXPORT_SYMBOL(drm_mm_put_block);

static int check_free_hole(unsigned long start, unsigned long end,
			   unsigned long size, unsigned alignment)
{
	if (end - start < size)
		return 0;

	if (alignment) {
		unsigned tmp = start % alignment;
		if (tmp)
			start += alignment - tmp;
	}

	return end >= start + size;
}

struct drm_mm_node *drm_mm_search_free_generic(const struct drm_mm *mm,
					       unsigned long size,
					       unsigned alignment,
					       unsigned long color,
					       enum drm_mm_search_flags flags)
{
	struct drm_mm_node *entry;
	struct drm_mm_node *best;
	unsigned long adj_start;
	unsigned long adj_end;
	unsigned long best_size;

	BUG_ON(mm->scanned_blocks);

	best = NULL;
	best_size = ~0UL;

	drm_mm_for_each_hole(entry, mm, adj_start, adj_end) {
		if (mm->color_adjust) {
			mm->color_adjust(entry, color, &adj_start, &adj_end);
			if (adj_end <= adj_start)
				continue;
		}

		if (!check_free_hole(adj_start, adj_end, size, alignment))
			continue;

		if (!(flags & DRM_MM_SEARCH_BEST))
			return entry;

		if (entry->size < best_size) {
			best = entry;
			best_size = entry->size;
		}
	}

	return best;
}
EXPORT_SYMBOL(drm_mm_search_free_generic);

struct drm_mm_node *drm_mm_search_free_in_range_generic(const struct drm_mm *mm,
							unsigned long size,
							unsigned alignment,
							unsigned long color,
							unsigned long start,
							unsigned long end,
							enum drm_mm_search_flags flags)
{
	struct drm_mm_node *entry;
	struct drm_mm_node *best;
	unsigned long adj_start;
	unsigned long adj_end;
	unsigned long best_size;

	BUG_ON(mm->scanned_blocks);

	best = NULL;
	best_size = ~0UL;

	drm_mm_for_each_hole(entry, mm, adj_start, adj_end) {
		if (adj_start < start)
			adj_start = start;
		if (adj_end > end)
			adj_end = end;

		if (mm->color_adjust) {
			mm->color_adjust(entry, color, &adj_start, &adj_end);
			if (adj_end <= adj_start)
				continue;
		}

		if (!check_free_hole(adj_start, adj_end, size, alignment))
			continue;

		if (!(flags & DRM_MM_SEARCH_BEST))
			return entry;

		if (entry->size < best_size) {
			best = entry;
			best_size = entry->size;
		}
	}

	return best;
}
EXPORT_SYMBOL(drm_mm_search_free_in_range_generic);

/**
 * Moves an allocation. To be used with embedded struct drm_mm_node.
 */
void drm_mm_replace_node(struct drm_mm_node *old, struct drm_mm_node *new)
{
	list_replace(&old->node_list, &new->node_list);
	list_replace(&old->hole_stack, &new->hole_stack);
	new->hole_follows = old->hole_follows;
	new->mm = old->mm;
	new->start = old->start;
	new->size = old->size;
	new->color = old->color;

	old->allocated = 0;
	new->allocated = 1;
}
EXPORT_SYMBOL(drm_mm_replace_node);

/**
 * Initializa lru scanning.
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole.
 *
 * Warning: As long as the scan list is non-empty, no other operations than
 * adding/removing nodes to/from the scan list are allowed.
 */
void drm_mm_init_scan(struct drm_mm *mm,
		      unsigned long size,
		      unsigned alignment,
		      unsigned long color)
{
	mm->scan_color = color;
	mm->scan_alignment = alignment;
	mm->scan_size = size;
	mm->scanned_blocks = 0;
	mm->scan_hit_start = 0;
	mm->scan_hit_end = 0;
	mm->scan_check_range = 0;
	mm->prev_scanned_node = NULL;
}
EXPORT_SYMBOL(drm_mm_init_scan);

/**
 * Initializa lru scanning.
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole. This version is for range-restricted scans.
 *
 * Warning: As long as the scan list is non-empty, no other operations than
 * adding/removing nodes to/from the scan list are allowed.
 */
void drm_mm_init_scan_with_range(struct drm_mm *mm,
				 unsigned long size,
				 unsigned alignment,
				 unsigned long color,
				 unsigned long start,
				 unsigned long end)
{
	mm->scan_color = color;
	mm->scan_alignment = alignment;
	mm->scan_size = size;
	mm->scanned_blocks = 0;
	mm->scan_hit_start = 0;
	mm->scan_hit_end = 0;
	mm->scan_start = start;
	mm->scan_end = end;
	mm->scan_check_range = 1;
	mm->prev_scanned_node = NULL;
}
EXPORT_SYMBOL(drm_mm_init_scan_with_range);

/**
 * Add a node to the scan list that might be freed to make space for the desired
 * hole.
 *
 * Returns non-zero, if a hole has been found, zero otherwise.
 */
int drm_mm_scan_add_block(struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;
	struct drm_mm_node *prev_node;
	unsigned long hole_start, hole_end;
	unsigned long adj_start, adj_end;

	mm->scanned_blocks++;

	BUG_ON(node->scanned_block);
	node->scanned_block = 1;

	prev_node = list_entry(node->node_list.prev, struct drm_mm_node,
			       node_list);

	node->scanned_preceeds_hole = prev_node->hole_follows;
	prev_node->hole_follows = 1;
	list_del(&node->node_list);
	node->node_list.prev = &prev_node->node_list;
	node->node_list.next = &mm->prev_scanned_node->node_list;
	mm->prev_scanned_node = node;

	adj_start = hole_start = drm_mm_hole_node_start(prev_node);
	adj_end = hole_end = drm_mm_hole_node_end(prev_node);

	if (mm->scan_check_range) {
		if (adj_start < mm->scan_start)
			adj_start = mm->scan_start;
		if (adj_end > mm->scan_end)
			adj_end = mm->scan_end;
	}

	if (mm->color_adjust)
		mm->color_adjust(prev_node, mm->scan_color,
				 &adj_start, &adj_end);

	if (check_free_hole(adj_start, adj_end,
			    mm->scan_size, mm->scan_alignment)) {
		mm->scan_hit_start = hole_start;
		mm->scan_hit_end = hole_end;
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(drm_mm_scan_add_block);

/**
 * Remove a node from the scan list.
 *
 * Nodes _must_ be removed in the exact same order from the scan list as they
 * have been added, otherwise the internal state of the memory manager will be
 * corrupted.
 *
 * When the scan list is empty, the selected memory nodes can be freed. An
 * immediately following drm_mm_search_free with !DRM_MM_SEARCH_BEST will then
 * return the just freed block (because its at the top of the free_stack list).
 *
 * Returns one if this block should be evicted, zero otherwise. Will always
 * return zero when no hole has been found.
 */
int drm_mm_scan_remove_block(struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;
	struct drm_mm_node *prev_node;

	mm->scanned_blocks--;

	BUG_ON(!node->scanned_block);
	node->scanned_block = 0;

	prev_node = list_entry(node->node_list.prev, struct drm_mm_node,
			       node_list);

	prev_node->hole_follows = node->scanned_preceeds_hole;
	list_add(&node->node_list, &prev_node->node_list);

	 return (drm_mm_hole_node_end(node) > mm->scan_hit_start &&
		 node->start < mm->scan_hit_end);
}
EXPORT_SYMBOL(drm_mm_scan_remove_block);

int drm_mm_clean(struct drm_mm * mm)
{
	struct list_head *head = &mm->head_node.node_list;

	return (head->next->next == head);
}
EXPORT_SYMBOL(drm_mm_clean);

void drm_mm_init(struct drm_mm * mm, unsigned long start, unsigned long size)
{
	INIT_LIST_HEAD(&mm->hole_stack);
	INIT_LIST_HEAD(&mm->unused_nodes);
	mm->num_unused = 0;
	mm->scanned_blocks = 0;
	spin_lock_init(&mm->unused_lock);

	/* Clever trick to avoid a special case in the free hole tracking. */
	INIT_LIST_HEAD(&mm->head_node.node_list);
	INIT_LIST_HEAD(&mm->head_node.hole_stack);
	mm->head_node.hole_follows = 1;
	mm->head_node.scanned_block = 0;
	mm->head_node.scanned_prev_free = 0;
	mm->head_node.scanned_next_free = 0;
	mm->head_node.mm = mm;
	mm->head_node.start = start + size;
	mm->head_node.size = start - mm->head_node.start;
	list_add_tail(&mm->head_node.hole_stack, &mm->hole_stack);

	mm->color_adjust = NULL;
}
EXPORT_SYMBOL(drm_mm_init);

void drm_mm_takedown(struct drm_mm * mm)
{
	struct drm_mm_node *entry, *next;

	if (!list_empty(&mm->head_node.node_list)) {
		DRM_ERROR("Memory manager not clean. Delaying takedown\n");
		return;
	}

	spin_lock(&mm->unused_lock);
	list_for_each_entry_safe(entry, next, &mm->unused_nodes, node_list) {
		list_del(&entry->node_list);
		kfree(entry);
		--mm->num_unused;
	}
	spin_unlock(&mm->unused_lock);

	BUG_ON(mm->num_unused != 0);
}
EXPORT_SYMBOL(drm_mm_takedown);

void drm_mm_debug_table(struct drm_mm *mm, const char *prefix)
{
	struct drm_mm_node *entry;
	unsigned long total_used = 0, total_free = 0, total = 0;
	unsigned long hole_start, hole_end, hole_size;

	hole_start = drm_mm_hole_node_start(&mm->head_node);
	hole_end = drm_mm_hole_node_end(&mm->head_node);
	hole_size = hole_end - hole_start;
	if (hole_size)
		printk(KERN_DEBUG "%s 0x%08lx-0x%08lx: %8lu: free\n",
			prefix, hole_start, hole_end,
			hole_size);
	total_free += hole_size;

	drm_mm_for_each_node(entry, mm) {
		printk(KERN_DEBUG "%s 0x%08lx-0x%08lx: %8lu: used\n",
			prefix, entry->start, entry->start + entry->size,
			entry->size);
		total_used += entry->size;

		if (entry->hole_follows) {
			hole_start = drm_mm_hole_node_start(entry);
			hole_end = drm_mm_hole_node_end(entry);
			hole_size = hole_end - hole_start;
			printk(KERN_DEBUG "%s 0x%08lx-0x%08lx: %8lu: free\n",
				prefix, hole_start, hole_end,
				hole_size);
			total_free += hole_size;
		}
	}
	total = total_free + total_used;

	printk(KERN_DEBUG "%s total: %lu, used %lu free %lu\n", prefix, total,
		total_used, total_free);
}
EXPORT_SYMBOL(drm_mm_debug_table);

#if defined(CONFIG_DEBUG_FS)
static unsigned long drm_mm_dump_hole(struct seq_file *m, struct drm_mm_node *entry)
{
	unsigned long hole_start, hole_end, hole_size;

	if (entry->hole_follows) {
		hole_start = drm_mm_hole_node_start(entry);
		hole_end = drm_mm_hole_node_end(entry);
		hole_size = hole_end - hole_start;
		seq_printf(m, "0x%08lx-0x%08lx: 0x%08lx: free\n",
				hole_start, hole_end, hole_size);
		return hole_size;
	}

	return 0;
}

int drm_mm_dump_table(struct seq_file *m, struct drm_mm *mm)
{
	struct drm_mm_node *entry;
	unsigned long total_used = 0, total_free = 0, total = 0;

	total_free += drm_mm_dump_hole(m, &mm->head_node);

	drm_mm_for_each_node(entry, mm) {
		seq_printf(m, "0x%08lx-0x%08lx: 0x%08lx: used\n",
				entry->start, entry->start + entry->size,
				entry->size);
		total_used += entry->size;
		total_free += drm_mm_dump_hole(m, entry);
	}
	total = total_free + total_used;

	seq_printf(m, "total: %lu, used %lu free %lu\n", total, total_used, total_free);
	return 0;
}
EXPORT_SYMBOL(drm_mm_dump_table);
#endif
