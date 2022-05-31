/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>

#include "virtgpu_drv.h"

static int virtio_gpu_virglrenderer_workaround = 1;
module_param_named(virglhack, virtio_gpu_virglrenderer_workaround, int, 0400);

int virtio_gpu_resource_id_get(struct virtio_gpu_device *vgdev, uint32_t *resid)
{
	if (virtio_gpu_virglrenderer_workaround) {
		/*
		 * Hack to avoid re-using resource IDs.
		 *
		 * virglrenderer versions up to (and including) 0.7.0
		 * can't deal with that.  virglrenderer commit
		 * "f91a9dd35715 Fix unlinking resources from hash
		 * table." (Feb 2019) fixes the bug.
		 */
		static atomic_t seqno = ATOMIC_INIT(0);
		int handle = atomic_inc_return(&seqno);
		*resid = handle + 1;
	} else {
		int handle = ida_alloc(&vgdev->resource_ida, GFP_KERNEL);
		if (handle < 0)
			return handle;
		*resid = handle + 1;
	}
	return 0;
}

static void virtio_gpu_resource_id_put(struct virtio_gpu_device *vgdev, uint32_t id)
{
	if (!virtio_gpu_virglrenderer_workaround) {
		ida_free(&vgdev->resource_ida, id - 1);
	}
}

void virtio_gpu_cleanup_object(struct virtio_gpu_object *bo)
{
	struct virtio_gpu_device *vgdev = bo->base.base.dev->dev_private;

	virtio_gpu_resource_id_put(vgdev, bo->hw_res_handle);
	if (virtio_gpu_is_shmem(bo)) {
		struct virtio_gpu_object_shmem *shmem = to_virtio_gpu_shmem(bo);

		if (shmem->pages) {
			if (shmem->mapped) {
				dma_unmap_sgtable(vgdev->vdev->dev.parent,
					     shmem->pages, DMA_TO_DEVICE, 0);
				shmem->mapped = 0;
			}

			sg_free_table(shmem->pages);
			kfree(shmem->pages);
			shmem->pages = NULL;
			drm_gem_shmem_unpin(&bo->base);
		}

		drm_gem_shmem_free(&bo->base);
	} else if (virtio_gpu_is_vram(bo)) {
		struct virtio_gpu_object_vram *vram = to_virtio_gpu_vram(bo);

		spin_lock(&vgdev->host_visible_lock);
		if (drm_mm_node_allocated(&vram->vram_node))
			drm_mm_remove_node(&vram->vram_node);

		spin_unlock(&vgdev->host_visible_lock);

		drm_gem_free_mmap_offset(&vram->base.base.base);
		drm_gem_object_release(&vram->base.base.base);
		kfree(vram);
	}
}

static void virtio_gpu_free_object(struct drm_gem_object *obj)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_device *vgdev = bo->base.base.dev->dev_private;

	if (bo->created) {
		virtio_gpu_cmd_unref_resource(vgdev, bo);
		virtio_gpu_notify(vgdev);
		/* completion handler calls virtio_gpu_cleanup_object() */
		return;
	}
	virtio_gpu_cleanup_object(bo);
}

static const struct drm_gem_object_funcs virtio_gpu_shmem_funcs = {
	.free = virtio_gpu_free_object,
	.open = virtio_gpu_gem_object_open,
	.close = virtio_gpu_gem_object_close,
	.print_info = drm_gem_shmem_object_print_info,
	.export = virtgpu_gem_prime_export,
	.pin = drm_gem_shmem_object_pin,
	.unpin = drm_gem_shmem_object_unpin,
	.get_sg_table = drm_gem_shmem_object_get_sg_table,
	.vmap = drm_gem_shmem_object_vmap,
	.vunmap = drm_gem_shmem_object_vunmap,
	.mmap = drm_gem_shmem_object_mmap,
	.vm_ops = &drm_gem_shmem_vm_ops,
};

bool virtio_gpu_is_shmem(struct virtio_gpu_object *bo)
{
	return bo->base.base.funcs == &virtio_gpu_shmem_funcs;
}

struct drm_gem_object *virtio_gpu_create_object(struct drm_device *dev,
						size_t size)
{
	struct virtio_gpu_object_shmem *shmem;
	struct drm_gem_shmem_object *dshmem;

	shmem = kzalloc(sizeof(*shmem), GFP_KERNEL);
	if (!shmem)
		return ERR_PTR(-ENOMEM);

	dshmem = &shmem->base.base;
	dshmem->base.funcs = &virtio_gpu_shmem_funcs;
	return &dshmem->base;
}

static int virtio_gpu_object_shmem_init(struct virtio_gpu_device *vgdev,
					struct virtio_gpu_object *bo,
					struct virtio_gpu_mem_entry **ents,
					unsigned int *nents)
{
	bool use_dma_api = !virtio_has_dma_quirk(vgdev->vdev);
	struct virtio_gpu_object_shmem *shmem = to_virtio_gpu_shmem(bo);
	struct scatterlist *sg;
	int si, ret;

	ret = drm_gem_shmem_pin(&bo->base);
	if (ret < 0)
		return -EINVAL;

	/*
	 * virtio_gpu uses drm_gem_shmem_get_sg_table instead of
	 * drm_gem_shmem_get_pages_sgt because virtio has it's own set of
	 * dma-ops. This is discouraged for other drivers, but should be fine
	 * since virtio_gpu doesn't support dma-buf import from other devices.
	 */
	shmem->pages = drm_gem_shmem_get_sg_table(&bo->base);
	if (!shmem->pages) {
		drm_gem_shmem_unpin(&bo->base);
		return -EINVAL;
	}

	if (use_dma_api) {
		ret = dma_map_sgtable(vgdev->vdev->dev.parent,
				      shmem->pages, DMA_TO_DEVICE, 0);
		if (ret)
			return ret;
		*nents = shmem->mapped = shmem->pages->nents;
	} else {
		*nents = shmem->pages->orig_nents;
	}

	*ents = kvmalloc_array(*nents,
			       sizeof(struct virtio_gpu_mem_entry),
			       GFP_KERNEL);
	if (!(*ents)) {
		DRM_ERROR("failed to allocate ent list\n");
		return -ENOMEM;
	}

	if (use_dma_api) {
		for_each_sgtable_dma_sg(shmem->pages, sg, si) {
			(*ents)[si].addr = cpu_to_le64(sg_dma_address(sg));
			(*ents)[si].length = cpu_to_le32(sg_dma_len(sg));
			(*ents)[si].padding = 0;
		}
	} else {
		for_each_sgtable_sg(shmem->pages, sg, si) {
			(*ents)[si].addr = cpu_to_le64(sg_phys(sg));
			(*ents)[si].length = cpu_to_le32(sg->length);
			(*ents)[si].padding = 0;
		}
	}

	return 0;
}

int virtio_gpu_object_create(struct virtio_gpu_device *vgdev,
			     struct virtio_gpu_object_params *params,
			     struct virtio_gpu_object **bo_ptr,
			     struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object_array *objs = NULL;
	struct drm_gem_shmem_object *shmem_obj;
	struct virtio_gpu_object *bo;
	struct virtio_gpu_mem_entry *ents;
	unsigned int nents;
	int ret;

	*bo_ptr = NULL;

	params->size = roundup(params->size, PAGE_SIZE);
	shmem_obj = drm_gem_shmem_create(vgdev->ddev, params->size);
	if (IS_ERR(shmem_obj))
		return PTR_ERR(shmem_obj);
	bo = gem_to_virtio_gpu_obj(&shmem_obj->base);

	ret = virtio_gpu_resource_id_get(vgdev, &bo->hw_res_handle);
	if (ret < 0)
		goto err_free_gem;

	bo->dumb = params->dumb;

	if (fence) {
		ret = -ENOMEM;
		objs = virtio_gpu_array_alloc(1);
		if (!objs)
			goto err_put_id;
		virtio_gpu_array_add_obj(objs, &bo->base.base);

		ret = virtio_gpu_array_lock_resv(objs);
		if (ret != 0)
			goto err_put_objs;
	}

	ret = virtio_gpu_object_shmem_init(vgdev, bo, &ents, &nents);
	if (ret != 0) {
		virtio_gpu_array_put_free(objs);
		virtio_gpu_free_object(&shmem_obj->base);
		return ret;
	}

	if (params->blob) {
		if (params->blob_mem == VIRTGPU_BLOB_MEM_GUEST)
			bo->guest_blob = true;

		virtio_gpu_cmd_resource_create_blob(vgdev, bo, params,
						    ents, nents);
	} else if (params->virgl) {
		virtio_gpu_cmd_resource_create_3d(vgdev, bo, params,
						  objs, fence);
		virtio_gpu_object_attach(vgdev, bo, ents, nents);
	} else {
		virtio_gpu_cmd_create_resource(vgdev, bo, params,
					       objs, fence);
		virtio_gpu_object_attach(vgdev, bo, ents, nents);
	}

	*bo_ptr = bo;
	return 0;

err_put_objs:
	virtio_gpu_array_put_free(objs);
err_put_id:
	virtio_gpu_resource_id_put(vgdev, bo->hw_res_handle);
err_free_gem:
	drm_gem_shmem_free(shmem_obj);
	return ret;
}
