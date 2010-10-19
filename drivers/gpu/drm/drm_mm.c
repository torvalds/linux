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

#include "drmP.h"
#include "drm_mm.h"
#include <linux/slab.h>
#include <linux/seq_file.h>

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
				       struct drm_mm_node, free_stack);
			list_del(&child->free_stack);
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
		list_add_tail(&node->free_stack, &mm->unused_nodes);
	}
	spin_unlock(&mm->unused_lock);
	return 0;
}
EXPORT_SYMBOL(drm_mm_pre_get);

static int drm_mm_create_tail_node(struct drm_mm *mm,
				   unsigned long start,
				   unsigned long size, int atomic)
{
	struct drm_mm_node *child;

	child = drm_mm_kmalloc(mm, atomic);
	if (unlikely(child == NULL))
		return -ENOMEM;

	child->free = 1;
	child->size = size;
	child->start = start;
	child->mm = mm;

	list_add_tail(&child->node_list, &mm->node_list);
	list_add_tail(&child->free_stack, &mm->free_stack);

	return 0;
}

static struct drm_mm_node *drm_mm_split_at_start(struct drm_mm_node *parent,
						 unsigned long size,
						 int atomic)
{
	struct drm_mm_node *child;

	child = drm_mm_kmalloc(parent->mm, atomic);
	if (unlikely(child == NULL))
		return NULL;

	INIT_LIST_HEAD(&child->free_stack);

	child->size = size;
	child->start = parent->start;
	child->mm = parent->mm;

	list_add_tail(&child->node_list, &parent->node_list);
	INIT_LIST_HEAD(&child->free_stack);

	parent->size -= size;
	parent->start += size;
	return child;
}


struct drm_mm_node *drm_mm_get_block_generic(struct drm_mm_node *node,
					     unsigned long size,
					     unsigned alignment,
					     int atomic)
{

	struct drm_mm_node *align_splitoff = NULL;
	unsigned tmp = 0;

	if (alignment)
		tmp = node->start % alignment;

	if (tmp) {
		align_splitoff =
		    drm_mm_split_at_start(node, alignment - tmp, atomic);
		if (unlikely(align_splitoff == NULL))
			return NULL;
	}

	if (node->size == size) {
		list_del_init(&node->free_stack);
		node->free = 0;
	} else {
		node = drm_mm_split_at_start(node, size, atomic);
	}

	if (align_splitoff)
		drm_mm_put_block(align_splitoff);

	return node;
}
EXPORT_SYMBOL(drm_mm_get_block_generic);

struct drm_mm_node *drm_mm_get_block_range_generic(struct drm_mm_node *node,
						unsigned long size,
						unsigned alignment,
						unsigned long start,
						unsigned long end,
						int atomic)
{
	struct drm_mm_node *align_splitoff = NULL;
	unsigned tmp = 0;
	unsigned wasted = 0;

	if (node->start < start)
		wasted += start - node->start;
	if (alignment)
		tmp = ((node->start + wasted) % alignment);

	if (tmp)
		wasted += alignment - tmp;
	if (wasted) {
		align_splitoff = drm_mm_split_at_start(node, wasted, atomic);
		if (unlikely(align_splitoff == NULL))
			return NULL;
	}

	if (node->size == size) {
		list_del_init(&node->free_stack);
		node->free = 0;
	} else {
		node = drm_mm_split_at_start(node, size, atomic);
	}

	if (align_splitoff)
		drm_mm_put_block(align_splitoff);

	return node;
}
EXPORT_SYMBOL(drm_mm_get_block_range_generic);

/*
 * Put a block. Merge with the previous and / or next block if they are free.
 * Otherwise add to the free stack.
 */

void drm_mm_put_block(struct drm_mm_node *cur)
{

	struct drm_mm *mm = cur->mm;
	struct list_head *cur_head = &cur->node_list;
	struct list_head *root_head = &mm->node_list;
	struct drm_mm_node *prev_node = NULL;
	struct drm_mm_node *next_node;

	int merged = 0;

	BUG_ON(cur->scanned_block || cur->scanned_prev_free
				  || cur->scanned_next_free);

	if (cur_head->prev != root_head) {
		prev_node =
		    list_entry(cur_head->prev, struct drm_mm_node, node_list);
		if (prev_node->free) {
			prev_node->size += cur->size;
			merged = 1;
		}
	}
	if (cur_head->next != root_head) {
		next_node =
		    list_entry(cur_head->next, struct drm_mm_node, node_list);
		if (next_node->free) {
			if (merged) {
				prev_node->size += next_node->size;
				list_del(&next_node->node_list);
				list_del(&next_node->free_stack);
				spin_lock(&mm->unused_lock);
				if (mm->num_unused < MM_UNUSED_TARGET) {
					list_add(&next_node->free_stack,
						 &mm->unused_nodes);
					++mm->num_unused;
				} else
					kfree(next_node);
				spin_unlock(&mm->unused_lock);
			} else {
				next_node->size += cur->size;
				next_node->start = cur->start;
				merged = 1;
			}
		}
	}
	if (!merged) {
		cur->free = 1;
		list_add(&cur->free_stack, &mm->free_stack);
	} else {
		list_del(&cur->node_list);
		spin_lock(&mm->unused_lock);
		if (mm->num_unused < MM_UNUSED_TARGET) {
			list_add(&cur->free_stack, &mm->unused_nodes);
			++mm->num_unused;
		} else
			kfree(cur);
		spin_unlock(&mm->unused_lock);
	}
}

EXPORT_SYMBOL(drm_mm_put_block);

static int check_free_hole(unsigned long start, unsigned long end,
			   unsigned long size, unsigned alignment)
{
	unsigned wasted = 0;

	if (end - start < size)
		return 0;

	if (alignment) {
		unsigned tmp = start % alignment;
		if (tmp)
			wasted = alignment - tmp;
	}

	if (end >= start + size + wasted) {
		return 1;
	}

	return 0;
}

struct drm_mm_node *drm_mm_search_free(const struct drm_mm *mm,
				       unsigned long size,
				       unsigned alignment, int best_match)
{
	struct drm_mm_node *entry;
	struct drm_mm_node *best;
	unsigned long best_size;

	BUG_ON(mm->scanned_blocks);

	best = NULL;
	best_size = ~0UL;

	list_for_each_entry(entry, &mm->free_stack, free_stack) {
		if (!check_free_hole(entry->start, entry->start + entry->size,
				     size, alignment))
			continue;

		if (!best_match)
			return entry;

		if (entry->size < best_size) {
			best = entry;
			best_size = entry->size;
		}
	}

	return best;
}
EXPORT_SYMBOL(drm_mm_search_free);

struct drm_mm_node *drm_mm_search_free_in_range(const struct drm_mm *mm,
						unsigned long size,
						unsigned alignment,
						unsigned long start,
						unsigned long end,
						int best_match)
{
	struct drm_mm_node *entry;
	struct drm_mm_node *best;
	unsigned long best_size;

	BUG_ON(mm->scanned_blocks);

	best = NULL;
	best_size = ~0UL;

	list_for_each_entry(entry, &mm->free_stack, free_stack) {
		unsigned long adj_start = entry->start < start ?
			start : entry->start;
		unsigned long adj_end = entry->start + entry->size > end ?
			end : entry->start + entry->size;

		if (!check_free_hole(adj_start, adj_end, size, alignment))
			continue;

		if (!best_match)
			return entry;

		if (entry->size < best_size) {
			best = entry;
			best_size = entry->size;
		}
	}

	return best;
}
EXPORT_SYMBOL(drm_mm_search_free_in_range);

/**
 * Initializa lru scanning.
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole.
 *
 * Warning: As long as the scan list is non-empty, no other operations than
 * adding/removing nodes to/from the scan list are allowed.
 */
void drm_mm_init_scan(struct drm_mm *mm, unsigned long size,
		      unsigned alignment)
{
	mm->scan_alignment = alignment;
	mm->scan_size = size;
	mm->scanned_blocks = 0;
	mm->scan_hit_start = 0;
	mm->scan_hit_size = 0;
}
EXPORT_SYMBOL(drm_mm_init_scan);

/**
 * Add a node to the scan list that might be freed to make space for the desired
 * hole.
 *
 * Returns non-zero, if a hole has been found, zero otherwise.
 */
int drm_mm_scan_add_block(struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;
	struct list_head *prev_free, *next_free;
	struct drm_mm_node *prev_node, *next_node;

	mm->scanned_blocks++;

	prev_free = next_free = NULL;

	BUG_ON(node->free);
	node->scanned_block = 1;
	node->free = 1;

	if (node->node_list.prev != &mm->node_list) {
		prev_node = list_entry(node->node_list.prev, struct drm_mm_node,
				       node_list);

		if (prev_node->free) {
			list_del(&prev_node->node_list);

			node->start = prev_node->start;
			node->size += prev_node->size;

			prev_node->scanned_prev_free = 1;

			prev_free = &prev_node->free_stack;
		}
	}

	if (node->node_list.next != &mm->node_list) {
		next_node = list_entry(node->node_list.next, struct drm_mm_node,
				       node_list);

		if (next_node->free) {
			list_del(&next_node->node_list);

			node->size += next_node->size;

			next_node->scanned_next_free = 1;

			next_free = &next_node->free_stack;
		}
	}

	/* The free_stack list is not used for allocated objects, so these two
	 * pointers can be abused (as long as no allocations in this memory
	 * manager happens). */
	node->free_stack.prev = prev_free;
	node->free_stack.next = next_free;

	if (check_free_hole(node->start, node->start + node->size,
			    mm->scan_size, mm->scan_alignment)) {
		mm->scan_hit_start = node->start;
		mm->scan_hit_size = node->size;

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
 * immediatly following drm_mm_search_free with best_match = 0 will then return
 * the just freed block (because its at the top of the free_stack list).
 *
 * Returns one if this block should be evicted, zero otherwise. Will always
 * return zero when no hole has been found.
 */
int drm_mm_scan_remove_block(struct drm_mm_node *node)
{
	struct drm_mm *mm = node->mm;
	struct drm_mm_node *prev_node, *next_node;

	mm->scanned_blocks--;

	BUG_ON(!node->scanned_block);
	node->scanned_block = 0;
	node->free = 0;

	prev_node = list_entry(node->free_stack.prev, struct drm_mm_node,
			       free_stack);
	next_node = list_entry(node->free_stack.next, struct drm_mm_node,
			       free_stack);

	if (prev_node) {
		BUG_ON(!prev_node->scanned_prev_free);
		prev_node->scanned_prev_free = 0;

		list_add_tail(&prev_node->node_list, &node->node_list);

		node->start = prev_node->start + prev_node->size;
		node->size -= prev_node->size;
	}

	if (next_node) {
		BUG_ON(!next_node->scanned_next_free);
		next_node->scanned_next_free = 0;

		list_add(&next_node->node_list, &node->node_list);

		node->size -= next_node->size;
	}

	INIT_LIST_HEAD(&node->free_stack);

	/* Only need to check for containement because start&size for the
	 * complete resulting free block (not just the desired part) is
	 * stored. */
	if (node->start >= mm->scan_hit_start &&
	    node->start + node->size
	    		<= mm->scan_hit_start + mm->scan_hit_size) {
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(drm_mm_scan_remove_block);

int drm_mm_clean(struct drm_mm * mm)
{
	struct list_head *head = &mm->node_list;

	return (head->next->next == head);
}
EXPORT_SYMBOL(drm_mm_clean);

int drm_mm_init(struct drm_mm * mm, unsigned long start, unsigned long size)
{
	INIT_LIST_HEAD(&mm->node_list);
	INIT_LIST_HEAD(&mm->free_stack);
	INIT_LIST_HEAD(&mm->unused_nodes);
	mm->num_unused = 0;
	mm->scanned_blocks = 0;
	spin_lock_init(&mm->unused_lock);

	return drm_mm_create_tail_node(mm, start, size, 0);
}
EXPORT_SYMBOL(drm_mm_init);

void drm_mm_takedown(struct drm_mm * mm)
{
	struct list_head *bnode = mm->free_stack.next;
	struct drm_mm_node *entry;
	struct drm_mm_node *next;

	entry = list_entry(bnode, struct drm_mm_node, free_stack);

	if (entry->node_list.next != &mm->node_list ||
	    entry->free_stack.next != &mm->free_stack) {
		DRM_ERROR("Memory manager not clean. Delaying takedown\n");
		return;
	}

	list_del(&entry->free_stack);
	list_del(&entry->node_list);
	kfree(entry);

	spin_lock(&mm->unused_lock);
	list_for_each_entry_safe(entry, next, &mm->unused_nodes, free_stack) {
		list_del(&entry->free_stack);
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
	int total_used = 0, total_free = 0, total = 0;

	list_for_each_entry(entry, &mm->node_list, node_list) {
		printk(KERN_DEBUG "%s 0x%08lx-0x%08lx: %8ld: %s\n",
			prefix, entry->start, entry->start + entry->size,
			entry->size, entry->free ? "free" : "used");
		total += entry->size;
		if (entry->free)
			total_free += entry->size;
		else
			total_used += entry->size;
	}
	printk(KERN_DEBUG "%s total: %d, used %d free %d\n", prefix, total,
		total_used, total_free);
}
EXPORT_SYMBOL(drm_mm_debug_table);

#if defined(CONFIG_DEBUG_FS)
int drm_mm_dump_table(struct seq_file *m, struct drm_mm *mm)
{
	struct drm_mm_node *entry;
	int total_used = 0, total_free = 0, total = 0;

	list_for_each_entry(entry, &mm->node_list, node_list) {
		seq_printf(m, "0x%08lx-0x%08lx: 0x%08lx: %s\n", entry->start, entry->start + entry->size, entry->size, entry->free ? "free" : "used");
		total += entry->size;
		if (entry->free)
			total_free += entry->size;
		else
			total_used += entry->size;
	}
	seq_printf(m, "total: %d, used %d free %d\n", total, total_used, total_free);
	return 0;
}
EXPORT_SYMBOL(drm_mm_dump_table);
#endif
