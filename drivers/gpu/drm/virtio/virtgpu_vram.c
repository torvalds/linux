// SPDX-License-Identifier: GPL-2.0
#include "virtgpu_drv.h"

static void virtio_gpu_vram_free(struct drm_gem_object *obj)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_object_vram *vram = to_virtio_gpu_vram(bo);
	bool unmap;

	if (bo->created) {
		spin_lock(&vgdev->host_visible_lock);
		unmap = drm_mm_node_allocated(&vram->vram_node);
		spin_unlock(&vgdev->host_visible_lock);

		if (unmap)
			virtio_gpu_cmd_unmap(vgdev, bo);

		virtio_gpu_cmd_unref_resource(vgdev, bo);
		virtio_gpu_notify(vgdev);
		return;
	}
}

static const struct vm_operations_struct virtio_gpu_vram_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static int virtio_gpu_vram_mmap(struct drm_gem_object *obj,
				struct vm_area_struct *vma)
{
	int ret;
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_object_vram *vram = to_virtio_gpu_vram(bo);
	unsigned long vm_size = vma->vm_end - vma->vm_start;

	if (!(bo->blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE))
		return -EINVAL;

	wait_event(vgdev->resp_wq, vram->map_state != STATE_INITIALIZING);
	if (vram->map_state != STATE_OK)
		return -EINVAL;

	vma->vm_pgoff -= drm_vma_node_start(&obj->vma_node);
	vma->vm_flags |= VM_MIXEDMAP | VM_DONTEXPAND;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);
	vma->vm_ops = &virtio_gpu_vram_vm_ops;

	if (vram->map_info == VIRTIO_GPU_MAP_CACHE_WC)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else if (vram->map_info == VIRTIO_GPU_MAP_CACHE_UNCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* Partial mappings of GEM buffers don't happen much in practice. */
	if (vm_size != vram->vram_node.size)
		return -EINVAL;

	ret = io_remap_pfn_range(vma, vma->vm_start,
				 vram->vram_node.start >> PAGE_SHIFT,
				 vm_size, vma->vm_page_prot);
	return ret;
}

static const struct drm_gem_object_funcs virtio_gpu_vram_funcs = {
	.open = virtio_gpu_gem_object_open,
	.close = virtio_gpu_gem_object_close,
	.free = virtio_gpu_vram_free,
	.mmap = virtio_gpu_vram_mmap,
};

bool virtio_gpu_is_vram(struct virtio_gpu_object *bo)
{
	return bo->base.base.funcs == &virtio_gpu_vram_funcs;
}

static int virtio_gpu_vram_map(struct virtio_gpu_object *bo)
{
	int ret;
	uint64_t offset;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_device *vgdev = bo->base.base.dev->dev_private;
	struct virtio_gpu_object_vram *vram = to_virtio_gpu_vram(bo);

	if (!vgdev->has_host_visible)
		return -EINVAL;

	spin_lock(&vgdev->host_visible_lock);
	ret = drm_mm_insert_node(&vgdev->host_visible_mm, &vram->vram_node,
				 bo->base.base.size);
	spin_unlock(&vgdev->host_visible_lock);

	if (ret)
		return ret;

	objs = virtio_gpu_array_alloc(1);
	if (!objs) {
		ret = -ENOMEM;
		goto err_remove_node;
	}

	virtio_gpu_array_add_obj(objs, &bo->base.base);
	/*TODO: Add an error checking helper function in drm_mm.h */
	offset = vram->vram_node.start - vgdev->host_visible_region.addr;

	ret = virtio_gpu_cmd_map(vgdev, objs, offset);
	if (ret) {
		virtio_gpu_array_put_free(objs);
		goto err_remove_node;
	}

	return 0;

err_remove_node:
	spin_lock(&vgdev->host_visible_lock);
	drm_mm_remove_node(&vram->vram_node);
	spin_unlock(&vgdev->host_visible_lock);
	return ret;
}

int virtio_gpu_vram_create(struct virtio_gpu_device *vgdev,
			   struct virtio_gpu_object_params *params,
			   struct virtio_gpu_object **bo_ptr)
{
	struct drm_gem_object *obj;
	struct virtio_gpu_object_vram *vram;
	int ret;

	vram = kzalloc(sizeof(*vram), GFP_KERNEL);
	if (!vram)
		return -ENOMEM;

	obj = &vram->base.base.base;
	obj->funcs = &virtio_gpu_vram_funcs;
	drm_gem_private_object_init(vgdev->ddev, obj, params->size);

	/* Create fake offset */
	ret = drm_gem_create_mmap_offset(obj);
	if (ret) {
		kfree(vram);
		return ret;
	}

	ret = virtio_gpu_resource_id_get(vgdev, &vram->base.hw_res_handle);
	if (ret) {
		kfree(vram);
		return ret;
	}

	if (params->blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE) {
		ret = virtio_gpu_vram_map(&vram->base);
		if (ret) {
			virtio_gpu_vram_free(obj);
			return ret;
		}
	}

	*bo_ptr = &vram->base;
	return 0;
}
