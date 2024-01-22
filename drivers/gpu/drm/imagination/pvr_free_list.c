// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_free_list.h"
#include "pvr_gem.h"
#include "pvr_hwrt.h"
#include "pvr_rogue_fwif.h"
#include "pvr_vm.h"

#include <drm/drm_gem.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <uapi/drm/pvr_drm.h>

#define FREE_LIST_ENTRY_SIZE sizeof(u32)

#define FREE_LIST_ALIGNMENT \
	((ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE / FREE_LIST_ENTRY_SIZE) - 1)

#define FREE_LIST_MIN_PAGES 50
#define FREE_LIST_MIN_PAGES_BRN66011 40
#define FREE_LIST_MIN_PAGES_ROGUEXE 25

/**
 * pvr_get_free_list_min_pages() - Get minimum free list size for this device
 * @pvr_dev: Device pointer.
 *
 * Returns:
 *  * Minimum free list size, in PM physical pages.
 */
u32
pvr_get_free_list_min_pages(struct pvr_device *pvr_dev)
{
	u32 value;

	if (PVR_HAS_FEATURE(pvr_dev, roguexe)) {
		if (PVR_HAS_QUIRK(pvr_dev, 66011))
			value = FREE_LIST_MIN_PAGES_BRN66011;
		else
			value = FREE_LIST_MIN_PAGES_ROGUEXE;
	} else {
		value = FREE_LIST_MIN_PAGES;
	}

	return value;
}

static int
free_list_create_kernel_structure(struct pvr_file *pvr_file,
				  struct drm_pvr_ioctl_create_free_list_args *args,
				  struct pvr_free_list *free_list)
{
	struct pvr_gem_object *free_list_obj;
	struct pvr_vm_context *vm_ctx;
	u64 free_list_size;
	int err;

	if (args->grow_threshold > 100 ||
	    args->initial_num_pages > args->max_num_pages ||
	    args->grow_num_pages > args->max_num_pages ||
	    args->max_num_pages == 0 ||
	    (args->initial_num_pages < args->max_num_pages && !args->grow_num_pages) ||
	    (args->initial_num_pages == args->max_num_pages && args->grow_num_pages))
		return -EINVAL;

	if ((args->initial_num_pages & FREE_LIST_ALIGNMENT) ||
	    (args->max_num_pages & FREE_LIST_ALIGNMENT) ||
	    (args->grow_num_pages & FREE_LIST_ALIGNMENT))
		return -EINVAL;

	vm_ctx = pvr_vm_context_lookup(pvr_file, args->vm_context_handle);
	if (!vm_ctx)
		return -EINVAL;

	free_list_obj = pvr_vm_find_gem_object(vm_ctx, args->free_list_gpu_addr,
					       NULL, &free_list_size);
	if (!free_list_obj) {
		err = -EINVAL;
		goto err_put_vm_context;
	}

	if ((free_list_obj->flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS) ||
	    !(free_list_obj->flags & DRM_PVR_BO_PM_FW_PROTECT) ||
	    free_list_size < (args->max_num_pages * FREE_LIST_ENTRY_SIZE)) {
		err = -EINVAL;
		goto err_put_free_list_obj;
	}

	free_list->pvr_dev = pvr_file->pvr_dev;
	free_list->current_pages = 0;
	free_list->max_pages = args->max_num_pages;
	free_list->grow_pages = args->grow_num_pages;
	free_list->grow_threshold = args->grow_threshold;
	free_list->obj = free_list_obj;
	free_list->free_list_gpu_addr = args->free_list_gpu_addr;
	free_list->initial_num_pages = args->initial_num_pages;

	pvr_vm_context_put(vm_ctx);

	return 0;

err_put_free_list_obj:
	pvr_gem_object_put(free_list_obj);

err_put_vm_context:
	pvr_vm_context_put(vm_ctx);

	return err;
}

static void
free_list_destroy_kernel_structure(struct pvr_free_list *free_list)
{
	WARN_ON(!list_empty(&free_list->hwrt_list));

	pvr_gem_object_put(free_list->obj);
}

/**
 * calculate_free_list_ready_pages_locked() - Function to work out the number of free
 *                                            list pages to reserve for growing within
 *                                            the FW without having to wait for the
 *                                            host to progress a grow request
 * @free_list: Pointer to free list.
 * @pages: Total pages currently in free list.
 *
 * If the threshold or grow size means less than the alignment size (4 pages on
 * Rogue), then the feature is not used.
 *
 * Caller must hold &free_list->lock.
 *
 * Return: number of pages to reserve.
 */
static u32
calculate_free_list_ready_pages_locked(struct pvr_free_list *free_list, u32 pages)
{
	u32 ready_pages;

	lockdep_assert_held(&free_list->lock);

	ready_pages = ((pages * free_list->grow_threshold) / 100);

	/* The number of pages must be less than the grow size. */
	ready_pages = min(ready_pages, free_list->grow_pages);

	/*
	 * The number of pages must be a multiple of the free list align size.
	 */
	ready_pages &= ~FREE_LIST_ALIGNMENT;

	return ready_pages;
}

static u32
calculate_free_list_ready_pages(struct pvr_free_list *free_list, u32 pages)
{
	u32 ret;

	mutex_lock(&free_list->lock);

	ret = calculate_free_list_ready_pages_locked(free_list, pages);

	mutex_unlock(&free_list->lock);

	return ret;
}

static void
free_list_fw_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_freelist *fw_data = cpu_ptr;
	struct pvr_free_list *free_list = priv;
	u32 ready_pages;

	/* Fill out FW structure */
	ready_pages = calculate_free_list_ready_pages(free_list,
						      free_list->initial_num_pages);

	fw_data->max_pages = free_list->max_pages;
	fw_data->current_pages = free_list->initial_num_pages - ready_pages;
	fw_data->grow_pages = free_list->grow_pages;
	fw_data->ready_pages = ready_pages;
	fw_data->freelist_id = free_list->fw_id;
	fw_data->grow_pending = false;
	fw_data->current_stack_top = fw_data->current_pages - 1;
	fw_data->freelist_dev_addr = free_list->free_list_gpu_addr;
	fw_data->current_dev_addr = (fw_data->freelist_dev_addr +
				     ((fw_data->max_pages - fw_data->current_pages) *
				      FREE_LIST_ENTRY_SIZE)) &
				    ~((u64)ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE - 1);
}

static int
free_list_create_fw_structure(struct pvr_file *pvr_file,
			      struct drm_pvr_ioctl_create_free_list_args *args,
			      struct pvr_free_list *free_list)
{
	struct pvr_device *pvr_dev = pvr_file->pvr_dev;

	/*
	 * Create and map the FW structure so we can initialise it. This is not
	 * accessed on the CPU side post-initialisation so the mapping lifetime
	 * is only for this function.
	 */
	free_list->fw_data = pvr_fw_object_create_and_map(pvr_dev, sizeof(*free_list->fw_data),
							  PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
							  free_list_fw_init, free_list,
							  &free_list->fw_obj);
	if (IS_ERR(free_list->fw_data))
		return PTR_ERR(free_list->fw_data);

	return 0;
}

static void
free_list_destroy_fw_structure(struct pvr_free_list *free_list)
{
	pvr_fw_object_unmap_and_destroy(free_list->fw_obj);
}

static int
pvr_free_list_insert_pages_locked(struct pvr_free_list *free_list,
				  struct sg_table *sgt, u32 offset, u32 num_pages)
{
	struct sg_dma_page_iter dma_iter;
	u32 *page_list;

	lockdep_assert_held(&free_list->lock);

	page_list = pvr_gem_object_vmap(free_list->obj);
	if (IS_ERR(page_list))
		return PTR_ERR(page_list);

	offset /= FREE_LIST_ENTRY_SIZE;
	/* clang-format off */
	for_each_sgtable_dma_page(sgt, &dma_iter, 0) {
		dma_addr_t dma_addr = sg_page_iter_dma_address(&dma_iter);
		u64 dma_pfn = dma_addr >>
			       ROGUE_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT;
		u32 dma_addr_offset;

		BUILD_BUG_ON(ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE > PAGE_SIZE);

		for (dma_addr_offset = 0; dma_addr_offset < PAGE_SIZE;
		     dma_addr_offset += ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE) {
			WARN_ON_ONCE(dma_pfn >> 32);

			page_list[offset++] = (u32)dma_pfn;
			dma_pfn++;

			num_pages--;
			if (!num_pages)
				break;
		}

		if (!num_pages)
			break;
	}
	/* clang-format on */

	/* Make sure our free_list update is flushed. */
	wmb();

	pvr_gem_object_vunmap(free_list->obj);

	return 0;
}

static int
pvr_free_list_insert_node_locked(struct pvr_free_list_node *free_list_node)
{
	struct pvr_free_list *free_list = free_list_node->free_list;
	struct sg_table *sgt;
	u32 start_page;
	u32 offset;
	int err;

	lockdep_assert_held(&free_list->lock);

	start_page = free_list->max_pages - free_list->current_pages -
		     free_list_node->num_pages;
	offset = (start_page * FREE_LIST_ENTRY_SIZE) &
		  ~((u64)ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE - 1);

	sgt = drm_gem_shmem_get_pages_sgt(&free_list_node->mem_obj->base);
	if (WARN_ON(IS_ERR(sgt)))
		return PTR_ERR(sgt);

	err = pvr_free_list_insert_pages_locked(free_list, sgt,
						offset, free_list_node->num_pages);
	if (!err)
		free_list->current_pages += free_list_node->num_pages;

	return err;
}

static int
pvr_free_list_grow(struct pvr_free_list *free_list, u32 num_pages)
{
	struct pvr_device *pvr_dev = free_list->pvr_dev;
	struct pvr_free_list_node *free_list_node;
	int err;

	mutex_lock(&free_list->lock);

	if (num_pages & FREE_LIST_ALIGNMENT) {
		err = -EINVAL;
		goto err_unlock;
	}

	free_list_node = kzalloc(sizeof(*free_list_node), GFP_KERNEL);
	if (!free_list_node) {
		err = -ENOMEM;
		goto err_unlock;
	}

	free_list_node->num_pages = num_pages;
	free_list_node->free_list = free_list;

	free_list_node->mem_obj = pvr_gem_object_create(pvr_dev,
							num_pages <<
							ROGUE_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
							PVR_BO_FW_FLAGS_DEVICE_CACHED);
	if (IS_ERR(free_list_node->mem_obj)) {
		err = PTR_ERR(free_list_node->mem_obj);
		goto err_free;
	}

	err = pvr_free_list_insert_node_locked(free_list_node);
	if (err)
		goto err_destroy_gem_object;

	list_add_tail(&free_list_node->node, &free_list->mem_block_list);

	/*
	 * Reserve a number ready pages to allow the FW to process OOM quickly
	 * and asynchronously request a grow.
	 */
	free_list->ready_pages =
		calculate_free_list_ready_pages_locked(free_list,
						       free_list->current_pages);
	free_list->current_pages -= free_list->ready_pages;

	mutex_unlock(&free_list->lock);

	return 0;

err_destroy_gem_object:
	pvr_gem_object_put(free_list_node->mem_obj);

err_free:
	kfree(free_list_node);

err_unlock:
	mutex_unlock(&free_list->lock);

	return err;
}

void pvr_free_list_process_grow_req(struct pvr_device *pvr_dev,
				    struct rogue_fwif_fwccb_cmd_freelist_gs_data *req)
{
	struct pvr_free_list *free_list = pvr_free_list_lookup_id(pvr_dev, req->freelist_id);
	struct rogue_fwif_kccb_cmd resp_cmd = {
		.cmd_type = ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE,
	};
	struct rogue_fwif_freelist_gs_data *resp = &resp_cmd.cmd_data.free_list_gs_data;
	u32 grow_pages = 0;

	/* If we don't have a freelist registered for this ID, we can't do much. */
	if (WARN_ON(!free_list))
		return;

	/* Since the FW made the request, it has already consumed the ready pages,
	 * update the host struct.
	 */
	free_list->current_pages += free_list->ready_pages;
	free_list->ready_pages = 0;

	/* If the grow succeeds, update the grow_pages argument. */
	if (!pvr_free_list_grow(free_list, free_list->grow_pages))
		grow_pages = free_list->grow_pages;

	/* Now prepare the response and send it back to the FW. */
	pvr_fw_object_get_fw_addr(free_list->fw_obj, &resp->freelist_fw_addr);
	resp->delta_pages = grow_pages;
	resp->new_pages = free_list->current_pages + free_list->ready_pages;
	resp->ready_pages = free_list->ready_pages;
	pvr_free_list_put(free_list);

	WARN_ON(pvr_kccb_send_cmd(pvr_dev, &resp_cmd, NULL));
}

static void
pvr_free_list_free_node(struct pvr_free_list_node *free_list_node)
{
	pvr_gem_object_put(free_list_node->mem_obj);

	kfree(free_list_node);
}

/**
 * pvr_free_list_create() - Create a new free list and return an object pointer
 * @pvr_file: Pointer to pvr_file structure.
 * @args: Creation arguments from userspace.
 *
 * Return:
 *  * Pointer to new free_list, or
 *  * ERR_PTR(-%ENOMEM) on out of memory.
 */
struct pvr_free_list *
pvr_free_list_create(struct pvr_file *pvr_file,
		     struct drm_pvr_ioctl_create_free_list_args *args)
{
	struct pvr_free_list *free_list;
	int err;

	/* Create and fill out the kernel structure */
	free_list = kzalloc(sizeof(*free_list), GFP_KERNEL);

	if (!free_list)
		return ERR_PTR(-ENOMEM);

	kref_init(&free_list->ref_count);
	INIT_LIST_HEAD(&free_list->mem_block_list);
	INIT_LIST_HEAD(&free_list->hwrt_list);
	mutex_init(&free_list->lock);

	err = free_list_create_kernel_structure(pvr_file, args, free_list);
	if (err < 0)
		goto err_free;

	/* Allocate global object ID for firmware. */
	err = xa_alloc(&pvr_file->pvr_dev->free_list_ids,
		       &free_list->fw_id,
		       free_list,
		       xa_limit_32b,
		       GFP_KERNEL);
	if (err)
		goto err_destroy_kernel_structure;

	err = free_list_create_fw_structure(pvr_file, args, free_list);
	if (err < 0)
		goto err_free_fw_id;

	err = pvr_free_list_grow(free_list, args->initial_num_pages);
	if (err < 0)
		goto err_fw_struct_cleanup;

	return free_list;

err_fw_struct_cleanup:
	WARN_ON(pvr_fw_structure_cleanup(free_list->pvr_dev,
					 ROGUE_FWIF_CLEANUP_FREELIST,
					 free_list->fw_obj, 0));

err_free_fw_id:
	xa_erase(&free_list->pvr_dev->free_list_ids, free_list->fw_id);

err_destroy_kernel_structure:
	free_list_destroy_kernel_structure(free_list);

err_free:
	mutex_destroy(&free_list->lock);
	kfree(free_list);

	return ERR_PTR(err);
}

static void
pvr_free_list_release(struct kref *ref_count)
{
	struct pvr_free_list *free_list =
		container_of(ref_count, struct pvr_free_list, ref_count);
	struct list_head *pos, *n;
	int err;

	xa_erase(&free_list->pvr_dev->free_list_ids, free_list->fw_id);

	err = pvr_fw_structure_cleanup(free_list->pvr_dev,
				       ROGUE_FWIF_CLEANUP_FREELIST,
				       free_list->fw_obj, 0);
	if (err == -EBUSY) {
		/* Flush the FWCCB to process any HWR or freelist reconstruction
		 * request that might keep the freelist busy, and try again.
		 */
		pvr_fwccb_process(free_list->pvr_dev);
		err = pvr_fw_structure_cleanup(free_list->pvr_dev,
					       ROGUE_FWIF_CLEANUP_FREELIST,
					       free_list->fw_obj, 0);
	}

	WARN_ON(err);

	/* clang-format off */
	list_for_each_safe(pos, n, &free_list->mem_block_list) {
		struct pvr_free_list_node *free_list_node =
			container_of(pos, struct pvr_free_list_node, node);

		list_del(pos);
		pvr_free_list_free_node(free_list_node);
	}
	/* clang-format on */

	free_list_destroy_kernel_structure(free_list);
	free_list_destroy_fw_structure(free_list);
	mutex_destroy(&free_list->lock);
	kfree(free_list);
}

/**
 * pvr_destroy_free_lists_for_file: Destroy any free lists associated with the
 * given file.
 * @pvr_file: Pointer to pvr_file structure.
 *
 * Removes all free lists associated with @pvr_file from the device free_list
 * list and drops initial references. Free lists will then be destroyed once
 * all outstanding references are dropped.
 */
void pvr_destroy_free_lists_for_file(struct pvr_file *pvr_file)
{
	struct pvr_free_list *free_list;
	unsigned long handle;

	xa_for_each(&pvr_file->free_list_handles, handle, free_list) {
		(void)free_list;
		pvr_free_list_put(xa_erase(&pvr_file->free_list_handles, handle));
	}
}

/**
 * pvr_free_list_put() - Release reference on free list
 * @free_list: Pointer to list to release reference on
 */
void
pvr_free_list_put(struct pvr_free_list *free_list)
{
	if (free_list)
		kref_put(&free_list->ref_count, pvr_free_list_release);
}

void pvr_free_list_add_hwrt(struct pvr_free_list *free_list, struct pvr_hwrt_data *hwrt_data)
{
	mutex_lock(&free_list->lock);

	list_add_tail(&hwrt_data->freelist_node, &free_list->hwrt_list);

	mutex_unlock(&free_list->lock);
}

void pvr_free_list_remove_hwrt(struct pvr_free_list *free_list, struct pvr_hwrt_data *hwrt_data)
{
	mutex_lock(&free_list->lock);

	list_del(&hwrt_data->freelist_node);

	mutex_unlock(&free_list->lock);
}

static void
pvr_free_list_reconstruct(struct pvr_device *pvr_dev, u32 freelist_id)
{
	struct pvr_free_list *free_list = pvr_free_list_lookup_id(pvr_dev, freelist_id);
	struct pvr_free_list_node *free_list_node;
	struct rogue_fwif_freelist *fw_data;
	struct pvr_hwrt_data *hwrt_data;

	if (!free_list)
		return;

	mutex_lock(&free_list->lock);

	/* Rebuild the free list based on the memory block list. */
	free_list->current_pages = 0;

	list_for_each_entry(free_list_node, &free_list->mem_block_list, node)
		WARN_ON(pvr_free_list_insert_node_locked(free_list_node));

	/*
	 * Remove the ready pages, which are reserved to allow the FW to process OOM quickly and
	 * asynchronously request a grow.
	 */
	free_list->current_pages -= free_list->ready_pages;

	fw_data = free_list->fw_data;
	fw_data->current_stack_top = fw_data->current_pages - 1;
	fw_data->allocated_page_count = 0;
	fw_data->allocated_mmu_page_count = 0;

	/* Reset the state of any associated HWRTs. */
	list_for_each_entry(hwrt_data, &free_list->hwrt_list, freelist_node) {
		struct rogue_fwif_hwrtdata *hwrt_fw_data = pvr_fw_object_vmap(hwrt_data->fw_obj);

		if (!WARN_ON(IS_ERR(hwrt_fw_data))) {
			hwrt_fw_data->state = ROGUE_FWIF_RTDATA_STATE_HWR;
			hwrt_fw_data->hwrt_data_flags &= ~HWRTDATA_HAS_LAST_GEOM;
		}

		pvr_fw_object_vunmap(hwrt_data->fw_obj);
	}

	mutex_unlock(&free_list->lock);

	pvr_free_list_put(free_list);
}

void
pvr_free_list_process_reconstruct_req(struct pvr_device *pvr_dev,
				struct rogue_fwif_fwccb_cmd_freelists_reconstruction_data *req)
{
	struct rogue_fwif_kccb_cmd resp_cmd = {
		.cmd_type = ROGUE_FWIF_KCCB_CMD_FREELISTS_RECONSTRUCTION_UPDATE,
	};
	struct rogue_fwif_freelists_reconstruction_data *resp =
		&resp_cmd.cmd_data.free_lists_reconstruction_data;

	for (u32 i = 0; i < req->freelist_count; i++)
		pvr_free_list_reconstruct(pvr_dev, req->freelist_ids[i]);

	resp->freelist_count = req->freelist_count;
	memcpy(resp->freelist_ids, req->freelist_ids,
	       req->freelist_count * sizeof(resp->freelist_ids[0]));

	WARN_ON(pvr_kccb_send_cmd(pvr_dev, &resp_cmd, NULL));
}
