/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck., ND., USA.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/
/*
 * Simple memory manager interface that keeps track on allocate regions on a
 * per "owner" basis. All regions associated with an "owner" can be released
 * with a simple call. Typically if the "owner" exists. The owner is any
 * "unsigned long" identifier. Can typically be a pointer to a file private
 * struct or a context identifier.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drm_sman.h"

typedef struct drm_owner_item {
	drm_hash_item_t owner_hash;
	struct list_head sman_list;
	struct list_head mem_blocks;
} drm_owner_item_t;

void drm_sman_takedown(drm_sman_t * sman)
{
	drm_ht_remove(&sman->user_hash_tab);
	drm_ht_remove(&sman->owner_hash_tab);
	if (sman->mm)
		drm_free(sman->mm, sman->num_managers * sizeof(*sman->mm),
			 DRM_MEM_MM);
}

EXPORT_SYMBOL(drm_sman_takedown);

int
drm_sman_init(drm_sman_t * sman, unsigned int num_managers,
	      unsigned int user_order, unsigned int owner_order)
{
	int ret = 0;

	sman->mm = (drm_sman_mm_t *) drm_calloc(num_managers, sizeof(*sman->mm),
						DRM_MEM_MM);
	if (!sman->mm) {
		ret = -ENOMEM;
		goto out;
	}
	sman->num_managers = num_managers;
	INIT_LIST_HEAD(&sman->owner_items);
	ret = drm_ht_create(&sman->owner_hash_tab, owner_order);
	if (ret)
		goto out1;
	ret = drm_ht_create(&sman->user_hash_tab, user_order);
	if (!ret)
		goto out;

	drm_ht_remove(&sman->owner_hash_tab);
out1:
	drm_free(sman->mm, num_managers * sizeof(*sman->mm), DRM_MEM_MM);
out:
	return ret;
}

EXPORT_SYMBOL(drm_sman_init);

static void *drm_sman_mm_allocate(void *private, unsigned long size,
				  unsigned alignment)
{
	drm_mm_t *mm = (drm_mm_t *) private;
	drm_mm_node_t *tmp;

	tmp = drm_mm_search_free(mm, size, alignment, 1);
	if (!tmp) {
		return NULL;
	}
	tmp = drm_mm_get_block(tmp, size, alignment);
	return tmp;
}

static void drm_sman_mm_free(void *private, void *ref)
{
	drm_mm_t *mm = (drm_mm_t *) private;
	drm_mm_node_t *node = (drm_mm_node_t *) ref;

	drm_mm_put_block(mm, node);
}

static void drm_sman_mm_destroy(void *private)
{
	drm_mm_t *mm = (drm_mm_t *) private;
	drm_mm_takedown(mm);
	drm_free(mm, sizeof(*mm), DRM_MEM_MM);
}

static unsigned long drm_sman_mm_offset(void *private, void *ref)
{
	drm_mm_node_t *node = (drm_mm_node_t *) ref;
	return node->start;
}

int
drm_sman_set_range(drm_sman_t * sman, unsigned int manager,
		   unsigned long start, unsigned long size)
{
	drm_sman_mm_t *sman_mm;
	drm_mm_t *mm;
	int ret;

	BUG_ON(manager >= sman->num_managers);

	sman_mm = &sman->mm[manager];
	mm = drm_calloc(1, sizeof(*mm), DRM_MEM_MM);
	if (!mm) {
		return -ENOMEM;
	}
	sman_mm->private = mm;
	ret = drm_mm_init(mm, start, size);

	if (ret) {
		drm_free(mm, sizeof(*mm), DRM_MEM_MM);
		return ret;
	}

	sman_mm->allocate = drm_sman_mm_allocate;
	sman_mm->free = drm_sman_mm_free;
	sman_mm->destroy = drm_sman_mm_destroy;
	sman_mm->offset = drm_sman_mm_offset;

	return 0;
}

EXPORT_SYMBOL(drm_sman_set_range);

int
drm_sman_set_manager(drm_sman_t * sman, unsigned int manager,
		     drm_sman_mm_t * allocator)
{
	BUG_ON(manager >= sman->num_managers);
	sman->mm[manager] = *allocator;

	return 0;
}
EXPORT_SYMBOL(drm_sman_set_manager);

static drm_owner_item_t *drm_sman_get_owner_item(drm_sman_t * sman,
						 unsigned long owner)
{
	int ret;
	drm_hash_item_t *owner_hash_item;
	drm_owner_item_t *owner_item;

	ret = drm_ht_find_item(&sman->owner_hash_tab, owner, &owner_hash_item);
	if (!ret) {
		return drm_hash_entry(owner_hash_item, drm_owner_item_t,
				      owner_hash);
	}

	owner_item = drm_calloc(1, sizeof(*owner_item), DRM_MEM_MM);
	if (!owner_item)
		goto out;

	INIT_LIST_HEAD(&owner_item->mem_blocks);
	owner_item->owner_hash.key = owner;
	if (drm_ht_insert_item(&sman->owner_hash_tab, &owner_item->owner_hash))
		goto out1;

	list_add_tail(&owner_item->sman_list, &sman->owner_items);
	return owner_item;

out1:
	drm_free(owner_item, sizeof(*owner_item), DRM_MEM_MM);
out:
	return NULL;
}

drm_memblock_item_t *drm_sman_alloc(drm_sman_t *sman, unsigned int manager,
				    unsigned long size, unsigned alignment,
				    unsigned long owner)
{
	void *tmp;
	drm_sman_mm_t *sman_mm;
	drm_owner_item_t *owner_item;
	drm_memblock_item_t *memblock;

	BUG_ON(manager >= sman->num_managers);

	sman_mm = &sman->mm[manager];
	tmp = sman_mm->allocate(sman_mm->private, size, alignment);

	if (!tmp) {
		return NULL;
	}

	memblock = drm_calloc(1, sizeof(*memblock), DRM_MEM_MM);

	if (!memblock)
		goto out;

	memblock->mm_info = tmp;
	memblock->mm = sman_mm;
	memblock->sman = sman;

	if (drm_ht_just_insert_please
	    (&sman->user_hash_tab, &memblock->user_hash,
	     (unsigned long)memblock, 32, 0, 0))
		goto out1;

	owner_item = drm_sman_get_owner_item(sman, owner);
	if (!owner_item)
		goto out2;

	list_add_tail(&memblock->owner_list, &owner_item->mem_blocks);

	return memblock;

out2:
	drm_ht_remove_item(&sman->user_hash_tab, &memblock->user_hash);
out1:
	drm_free(memblock, sizeof(*memblock), DRM_MEM_MM);
out:
	sman_mm->free(sman_mm->private, tmp);

	return NULL;
}

EXPORT_SYMBOL(drm_sman_alloc);

static void drm_sman_free(drm_memblock_item_t *item)
{
	drm_sman_t *sman = item->sman;

	list_del(&item->owner_list);
	drm_ht_remove_item(&sman->user_hash_tab, &item->user_hash);
	item->mm->free(item->mm->private, item->mm_info);
	drm_free(item, sizeof(*item), DRM_MEM_MM);
}

int drm_sman_free_key(drm_sman_t *sman, unsigned int key)
{
	drm_hash_item_t *hash_item;
	drm_memblock_item_t *memblock_item;

	if (drm_ht_find_item(&sman->user_hash_tab, key, &hash_item))
		return -EINVAL;

	memblock_item = drm_hash_entry(hash_item, drm_memblock_item_t, user_hash);
	drm_sman_free(memblock_item);
	return 0;
}

EXPORT_SYMBOL(drm_sman_free_key);

static void drm_sman_remove_owner(drm_sman_t *sman,
				  drm_owner_item_t *owner_item)
{
	list_del(&owner_item->sman_list);
	drm_ht_remove_item(&sman->owner_hash_tab, &owner_item->owner_hash);
	drm_free(owner_item, sizeof(*owner_item), DRM_MEM_MM);
}

int drm_sman_owner_clean(drm_sman_t *sman, unsigned long owner)
{

	drm_hash_item_t *hash_item;
	drm_owner_item_t *owner_item;

	if (drm_ht_find_item(&sman->owner_hash_tab, owner, &hash_item)) {
		return -1;
	}

	owner_item = drm_hash_entry(hash_item, drm_owner_item_t, owner_hash);
	if (owner_item->mem_blocks.next == &owner_item->mem_blocks) {
		drm_sman_remove_owner(sman, owner_item);
		return -1;
	}

	return 0;
}

EXPORT_SYMBOL(drm_sman_owner_clean);

static void drm_sman_do_owner_cleanup(drm_sman_t *sman,
				      drm_owner_item_t *owner_item)
{
	drm_memblock_item_t *entry, *next;

	list_for_each_entry_safe(entry, next, &owner_item->mem_blocks,
				 owner_list) {
		drm_sman_free(entry);
	}
	drm_sman_remove_owner(sman, owner_item);
}

void drm_sman_owner_cleanup(drm_sman_t *sman, unsigned long owner)
{

	drm_hash_item_t *hash_item;
	drm_owner_item_t *owner_item;

	if (drm_ht_find_item(&sman->owner_hash_tab, owner, &hash_item)) {

		return;
	}

	owner_item = drm_hash_entry(hash_item, drm_owner_item_t, owner_hash);
	drm_sman_do_owner_cleanup(sman, owner_item);
}

EXPORT_SYMBOL(drm_sman_owner_cleanup);

void drm_sman_cleanup(drm_sman_t *sman)
{
	drm_owner_item_t *entry, *next;
	unsigned int i;
	drm_sman_mm_t *sman_mm;

	list_for_each_entry_safe(entry, next, &sman->owner_items, sman_list) {
		drm_sman_do_owner_cleanup(sman, entry);
	}
	if (sman->mm) {
		for (i = 0; i < sman->num_managers; ++i) {
			sman_mm = &sman->mm[i];
			if (sman_mm->private) {
				sman_mm->destroy(sman_mm->private);
				sman_mm->private = NULL;
			}
		}
	}
}

EXPORT_SYMBOL(drm_sman_cleanup);
