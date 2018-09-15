// SPDX-License-Identifier: GPL-2.0
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/shmem_fs.h>

#include "vkms_drv.h"

static struct vkms_gem_object *__vkms_gem_create(struct drm_device *dev,
						 u64 size)
{
	struct vkms_gem_object *obj;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	size = roundup(size, PAGE_SIZE);
	ret = drm_gem_object_init(dev, &obj->gem, size);
	if (ret) {
		kfree(obj);
		return ERR_PTR(ret);
	}

	mutex_init(&obj->pages_lock);

	return obj;
}

void vkms_gem_free_object(struct drm_gem_object *obj)
{
	struct vkms_gem_object *gem = container_of(obj, struct vkms_gem_object,
						   gem);

	kvfree(gem->pages);
	mutex_destroy(&gem->pages_lock);
	drm_gem_object_release(obj);
	kfree(gem);
}

int vkms_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct vkms_gem_object *obj = vma->vm_private_data;
	unsigned long vaddr = vmf->address;
	pgoff_t page_offset;
	loff_t num_pages;
	int ret;

	page_offset = (vaddr - vma->vm_start) >> PAGE_SHIFT;
	num_pages = DIV_ROUND_UP(obj->gem.size, PAGE_SIZE);

	if (page_offset > num_pages)
		return VM_FAULT_SIGBUS;

	ret = -ENOENT;
	mutex_lock(&obj->pages_lock);
	if (obj->pages) {
		get_page(obj->pages[page_offset]);
		vmf->page = obj->pages[page_offset];
		ret = 0;
	}
	mutex_unlock(&obj->pages_lock);
	if (ret) {
		struct page *page;
		struct address_space *mapping;

		mapping = file_inode(obj->gem.filp)->i_mapping;
		page = shmem_read_mapping_page(mapping, page_offset);

		if (!IS_ERR(page)) {
			vmf->page = page;
			ret = 0;
		} else {
			switch (PTR_ERR(page)) {
			case -ENOSPC:
			case -ENOMEM:
				ret = VM_FAULT_OOM;
				break;
			case -EBUSY:
				ret = VM_FAULT_RETRY;
				break;
			case -EFAULT:
			case -EINVAL:
				ret = VM_FAULT_SIGBUS;
				break;
			default:
				WARN_ON(PTR_ERR(page));
				ret = VM_FAULT_SIGBUS;
				break;
			}
		}
	}
	return ret;
}

struct drm_gem_object *vkms_gem_create(struct drm_device *dev,
				       struct drm_file *file,
				       u32 *handle,
				       u64 size)
{
	struct vkms_gem_object *obj;
	int ret;

	if (!file || !dev || !handle)
		return ERR_PTR(-EINVAL);

	obj = __vkms_gem_create(dev, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	ret = drm_gem_handle_create(file, &obj->gem, handle);
	drm_gem_object_put_unlocked(&obj->gem);
	if (ret) {
		drm_gem_object_release(&obj->gem);
		kfree(obj);
		return ERR_PTR(ret);
	}

	return &obj->gem;
}

int vkms_dumb_create(struct drm_file *file, struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gem_obj;
	u64 pitch, size;

	if (!args || !dev || !file)
		return -EINVAL;

	pitch = args->width * DIV_ROUND_UP(args->bpp, 8);
	size = pitch * args->height;

	if (!size)
		return -EINVAL;

	gem_obj = vkms_gem_create(dev, file, &args->handle, size);
	if (IS_ERR(gem_obj))
		return PTR_ERR(gem_obj);

	args->size = gem_obj->size;
	args->pitch = pitch;

	DRM_DEBUG_DRIVER("Created object of size %lld\n", size);

	return 0;
}

int vkms_dumb_map(struct drm_file *file, struct drm_device *dev,
		  u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	int ret;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	if (!obj->filp) {
		ret = -EINVAL;
		goto unref;
	}

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto unref;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);
unref:
	drm_gem_object_put_unlocked(obj);

	return ret;
}
