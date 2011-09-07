/**************************************************************************
 * Copyright (c) 2011, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <drm/drmP.h>
#include <drm/drm.h>

void drm_gem_object_release_wrap(struct drm_gem_object *obj)
{
	/* Remove the list map if one is present */
	if (obj->map_list.map) {
		struct drm_gem_mm *mm = obj->dev->mm_private;
		struct drm_map_list *list = &obj->map_list;
		drm_ht_remove_item(&mm->offset_hash, &list->hash);
		drm_mm_put_block(list->file_offset_node);
		kfree(list->map);
		list->map = NULL;
	}
	drm_gem_object_release(obj);
}

/**
 *	gem_create_mmap_offset		-	invent an mmap offset
 *	@obj: our object
 *
 *	Standard implementation of offset generation for mmap as is
 *	duplicated in several drivers. This belongs in GEM.
 */
int gem_create_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list;
	struct drm_local_map *map;
	int ret;

	list = &obj->map_list;
	list->map = kzalloc(sizeof(struct drm_map_list), GFP_KERNEL);
	if (list->map == NULL)
		return -ENOMEM;
	map = list->map;
	map->type = _DRM_GEM;
	map->size = obj->size;
	map->handle = obj;

	list->file_offset_node = drm_mm_search_free(&mm->offset_manager,
					obj->size / PAGE_SIZE, 0, 0);
	if (!list->file_offset_node) {
		dev_err(dev->dev, "failed to allocate offset for bo %d\n",
								obj->name);
		ret = -ENOSPC;
		goto free_it;
	}
	list->file_offset_node = drm_mm_get_block(list->file_offset_node,
					obj->size / PAGE_SIZE, 0);
	if (!list->file_offset_node) {
		ret = -ENOMEM;
		goto free_it;
	}
	list->hash.key = list->file_offset_node->start;
	ret = drm_ht_insert_item(&mm->offset_hash, &list->hash);
	if (ret) {
		dev_err(dev->dev, "failed to add to map hash\n");
		goto free_mm;
	}
	return 0;

free_mm:
	drm_mm_put_block(list->file_offset_node);
free_it:
	kfree(list->map);
	list->map = NULL;
	return ret;
}
