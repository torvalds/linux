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
#include <linux/slab.h>

unsigned long drm_mm_tail_space(drm_mm_t *mm)
{
	struct list_head *tail_node;
	drm_mm_node_t *entry;

	tail_node = mm->ml_entry.prev;
	entry = list_entry(tail_node, drm_mm_node_t, ml_entry);
	if (!entry->free)
		return 0;

	return entry->size;
}

int drm_mm_remove_space_from_tail(drm_mm_t *mm, unsigned long size)
{
	struct list_head *tail_node;
	drm_mm_node_t *entry;

	tail_node = mm->ml_entry.prev;
	entry = list_entry(tail_node, drm_mm_node_t, ml_entry);
	if (!entry->free)
		return -ENOMEM;

	if (entry->size <= size)
		return -ENOMEM;

	entry->size -= size;
	return 0;
}


static int drm_mm_create_tail_node(drm_mm_t *mm,
			    unsigned long start,
			    unsigned long size)
{
	drm_mm_node_t *child;

	child = (drm_mm_node_t *)
		drm_alloc(sizeof(*child), DRM_MEM_MM);
	if (!child)
		return -ENOMEM;

	child->free = 1;
	child->size = size;
	child->start = start;
	child->mm = mm;

	list_add_tail(&child->ml_entry, &mm->ml_entry);
	list_add_tail(&child->fl_entry, &mm->fl_entry);

	return 0;
}


int drm_mm_add_space_to_tail(drm_mm_t *mm, unsigned long size)
{
	struct list_head *tail_node;
	drm_mm_node_t *entry;

	tail_node = mm->ml_entry.prev;
	entry = list_entry(tail_node, drm_mm_node_t, ml_entry);
	if (!entry->free) {
		return drm_mm_create_tail_node(mm, entry->start + entry->size, size);
	}
	entry->size += size;
	return 0;
}

static drm_mm_node_t *drm_mm_split_at_start(drm_mm_node_t *parent,
					    unsigned long size)
{
	drm_mm_node_t *child;

	child = (drm_mm_node_t *)
		drm_alloc(sizeof(*child), DRM_MEM_MM);
	if (!child)
		return NULL;

	INIT_LIST_HEAD(&child->fl_entry);

	child->free = 0;
	child->size = size;
	child->start = parent->start;
	child->mm = parent->mm;

	list_add_tail(&child->ml_entry, &parent->ml_entry);
	INIT_LIST_HEAD(&child->fl_entry);

	parent->size -= size;
	parent->start += size;
	return child;
}



drm_mm_node_t *drm_mm_get_block(drm_mm_node_t * parent,
				unsigned long size, unsigned alignment)
{

	drm_mm_node_t *align_splitoff = NULL;
	drm_mm_node_t *child;
	unsigned tmp = 0;

	if (alignment)
		tmp = parent->start % alignment;

	if (tmp) {
		align_splitoff = drm_mm_split_at_start(parent, alignment - tmp);
		if (!align_splitoff)
			return NULL;
	}

	if (parent->size == size) {
		list_del_init(&parent->fl_entry);
		parent->free = 0;
		return parent;
	} else {
		child = drm_mm_split_at_start(parent, size);
	}

	if (align_splitoff)
		drm_mm_put_block(align_splitoff);

	return child;
}

/*
 * Put a block. Merge with the previous and / or next block if they are free.
 * Otherwise add to the free stack.
 */

void drm_mm_put_block(drm_mm_node_t * cur)
{

	drm_mm_t *mm = cur->mm;
	struct list_head *cur_head = &cur->ml_entry;
	struct list_head *root_head = &mm->ml_entry;
	drm_mm_node_t *prev_node = NULL;
	drm_mm_node_t *next_node;

	int merged = 0;

	if (cur_head->prev != root_head) {
		prev_node = list_entry(cur_head->prev, drm_mm_node_t, ml_entry);
		if (prev_node->free) {
			prev_node->size += cur->size;
			merged = 1;
		}
	}
	if (cur_head->next != root_head) {
		next_node = list_entry(cur_head->next, drm_mm_node_t, ml_entry);
		if (next_node->free) {
			if (merged) {
				prev_node->size += next_node->size;
				list_del(&next_node->ml_entry);
				list_del(&next_node->fl_entry);
				drm_free(next_node, sizeof(*next_node),
					 DRM_MEM_MM);
			} else {
				next_node->size += cur->size;
				next_node->start = cur->start;
				merged = 1;
			}
		}
	}
	if (!merged) {
		cur->free = 1;
		list_add(&cur->fl_entry, &mm->fl_entry);
	} else {
		list_del(&cur->ml_entry);
		drm_free(cur, sizeof(*cur), DRM_MEM_MM);
	}
}

drm_mm_node_t *drm_mm_search_free(const drm_mm_t * mm,
				  unsigned long size,
				  unsigned alignment, int best_match)
{
	struct list_head *list;
	const struct list_head *free_stack = &mm->fl_entry;
	drm_mm_node_t *entry;
	drm_mm_node_t *best;
	unsigned long best_size;
	unsigned wasted;

	best = NULL;
	best_size = ~0UL;

	list_for_each(list, free_stack) {
		entry = list_entry(list, drm_mm_node_t, fl_entry);
		wasted = 0;

		if (entry->size < size)
			continue;

		if (alignment) {
			register unsigned tmp = entry->start % alignment;
			if (tmp)
				wasted += alignment - tmp;
		}


		if (entry->size >= size + wasted) {
			if (!best_match)
				return entry;
			if (size < best_size) {
				best = entry;
				best_size = entry->size;
			}
		}
	}

	return best;
}

int drm_mm_clean(drm_mm_t * mm)
{
	struct list_head *head = &mm->ml_entry;

	return (head->next->next == head);
}

int drm_mm_init(drm_mm_t * mm, unsigned long start, unsigned long size)
{
	INIT_LIST_HEAD(&mm->ml_entry);
	INIT_LIST_HEAD(&mm->fl_entry);

	return drm_mm_create_tail_node(mm, start, size);
}

EXPORT_SYMBOL(drm_mm_init);

void drm_mm_takedown(drm_mm_t * mm)
{
	struct list_head *bnode = mm->fl_entry.next;
	drm_mm_node_t *entry;

	entry = list_entry(bnode, drm_mm_node_t, fl_entry);

	if (entry->ml_entry.next != &mm->ml_entry ||
	    entry->fl_entry.next != &mm->fl_entry) {
		DRM_ERROR("Memory manager not clean. Delaying takedown\n");
		return;
	}

	list_del(&entry->fl_entry);
	list_del(&entry->ml_entry);

	drm_free(entry, sizeof(*entry), DRM_MEM_MM);
}

EXPORT_SYMBOL(drm_mm_takedown);
