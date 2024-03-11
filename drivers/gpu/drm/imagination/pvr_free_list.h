/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_FREE_LIST_H
#define PVR_FREE_LIST_H

#include <linux/compiler_attributes.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <uapi/drm/pvr_drm.h>

#include "pvr_device.h"

/* Forward declaration from pvr_gem.h. */
struct pvr_fw_object;

/* Forward declaration from pvr_gem.h. */
struct pvr_gem_object;

/* Forward declaration from pvr_hwrt.h. */
struct pvr_hwrt_data;

/**
 * struct pvr_free_list_node - structure representing an allocation in the free
 *                             list
 */
struct pvr_free_list_node {
	/** @node: List node for &pvr_free_list.mem_block_list. */
	struct list_head node;

	/** @free_list: Pointer to owning free list. */
	struct pvr_free_list *free_list;

	/** @num_pages: Number of pages in this node. */
	u32 num_pages;

	/** @mem_obj: GEM object representing the pages in this node. */
	struct pvr_gem_object *mem_obj;
};

/**
 * struct pvr_free_list - structure representing a free list
 */
struct pvr_free_list {
	/** @ref_count: Reference count of object. */
	struct kref ref_count;

	/** @pvr_dev: Pointer to device that owns this object. */
	struct pvr_device *pvr_dev;

	/** @obj: GEM object representing the free list. */
	struct pvr_gem_object *obj;

	/** @fw_obj: FW object representing the FW-side structure. */
	struct pvr_fw_object *fw_obj;

	/** @fw_data: Pointer to CPU mapping of the FW-side structure. */
	struct rogue_fwif_freelist *fw_data;

	/**
	 * @lock: Mutex protecting modification of the free list. Must be held when accessing any
	 *        of the members below.
	 */
	struct mutex lock;

	/** @fw_id: Firmware ID for this object. */
	u32 fw_id;

	/** @current_pages: Current number of pages in free list. */
	u32 current_pages;

	/** @max_pages: Maximum number of pages in free list. */
	u32 max_pages;

	/** @grow_pages: Pages to grow free list by per request. */
	u32 grow_pages;

	/**
	 * @grow_threshold: Percentage of FL memory used that should trigger a
	 *                  new grow request.
	 */
	u32 grow_threshold;

	/**
	 * @ready_pages: Number of pages reserved for FW to use while a grow
	 *               request is being processed.
	 */
	u32 ready_pages;

	/** @mem_block_list: List of memory blocks in this free list. */
	struct list_head mem_block_list;

	/** @hwrt_list: List of HWRTs using this free list. */
	struct list_head hwrt_list;

	/** @initial_num_pages: Initial number of pages in free list. */
	u32 initial_num_pages;

	/** @free_list_gpu_addr: Address of free list in GPU address space. */
	u64 free_list_gpu_addr;
};

struct pvr_free_list *
pvr_free_list_create(struct pvr_file *pvr_file,
		     struct drm_pvr_ioctl_create_free_list_args *args);

void
pvr_destroy_free_lists_for_file(struct pvr_file *pvr_file);

u32
pvr_get_free_list_min_pages(struct pvr_device *pvr_dev);

static __always_inline struct pvr_free_list *
pvr_free_list_get(struct pvr_free_list *free_list)
{
	if (free_list)
		kref_get(&free_list->ref_count);

	return free_list;
}

/**
 * pvr_free_list_lookup() - Lookup free list pointer from handle and file
 * @pvr_file: Pointer to pvr_file structure.
 * @handle: Object handle.
 *
 * Takes reference on free list object. Call pvr_free_list_put() to release.
 *
 * Returns:
 *  * The requested object on success, or
 *  * %NULL on failure (object does not exist in list, is not a free list, or
 *    does not belong to @pvr_file)
 */
static __always_inline struct pvr_free_list *
pvr_free_list_lookup(struct pvr_file *pvr_file, u32 handle)
{
	struct pvr_free_list *free_list;

	xa_lock(&pvr_file->free_list_handles);
	free_list = pvr_free_list_get(xa_load(&pvr_file->free_list_handles, handle));
	xa_unlock(&pvr_file->free_list_handles);

	return free_list;
}

/**
 * pvr_free_list_lookup_id() - Lookup free list pointer from FW ID
 * @pvr_dev: Device pointer.
 * @id: FW object ID.
 *
 * Takes reference on free list object. Call pvr_free_list_put() to release.
 *
 * Returns:
 *  * The requested object on success, or
 *  * %NULL on failure (object does not exist in list, or is not a free list)
 */
static __always_inline struct pvr_free_list *
pvr_free_list_lookup_id(struct pvr_device *pvr_dev, u32 id)
{
	struct pvr_free_list *free_list;

	xa_lock(&pvr_dev->free_list_ids);

	/* Contexts are removed from the ctx_ids set in the context release path,
	 * meaning the ref_count reached zero before they get removed. We need
	 * to make sure we're not trying to acquire a context that's being
	 * destroyed.
	 */
	free_list = xa_load(&pvr_dev->free_list_ids, id);
	if (free_list && !kref_get_unless_zero(&free_list->ref_count))
		free_list = NULL;
	xa_unlock(&pvr_dev->free_list_ids);

	return free_list;
}

void
pvr_free_list_put(struct pvr_free_list *free_list);

void
pvr_free_list_add_hwrt(struct pvr_free_list *free_list, struct pvr_hwrt_data *hwrt_data);
void
pvr_free_list_remove_hwrt(struct pvr_free_list *free_list, struct pvr_hwrt_data *hwrt_data);

void pvr_free_list_process_grow_req(struct pvr_device *pvr_dev,
				    struct rogue_fwif_fwccb_cmd_freelist_gs_data *req);

void
pvr_free_list_process_reconstruct_req(struct pvr_device *pvr_dev,
				struct rogue_fwif_fwccb_cmd_freelists_reconstruction_data *req);

#endif /* PVR_FREE_LIST_H */
