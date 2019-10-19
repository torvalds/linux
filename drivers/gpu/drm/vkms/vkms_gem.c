// SPDX-License-Identifier: GPL-2.0+

#include <linux/shmem_fs.h>
#include <linux/vmalloc.h>

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

	WARN_ON(gem->pages);
	WARN_ON(gem->vaddr);

	mutex_destroy(&gem->pages_lock);
	drm_gem_object_release(obj);
	kfree(gem);
}

vm_fault_t vkms_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct vkms_gem_object *obj = vma->vm_private_data;
	unsigned long vaddr = vmf->address;
	pgoff_t page_offset;
	loff_t num_pages;
	vm_fault_t ret = VM_FAULT_SIGBUS;

	page_offset = (vaddr - vma->vm_start) >> PAGE_SHIFT;
	num_pages = DIV_ROUND_UP(obj->gem.size, PAGE_SIZE);

	if (page_offset > num_pages)
		return VM_FAULT_SIGBUS;

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
	if (ret)
		return ERR_PTR(ret);

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

static struct page **_get_pages(struct vkms_gem_object *vkms_obj)
{
	struct drm_gem_object *gem_obj = &vkms_obj->gem;

	if (!vkms_obj->pages) {
		struct page **pages = drm_gem_get_pages(gem_obj);

		if (IS_ERR(pages))
			return pages;

		if (cmpxchg(&vkms_obj->pages, NULL, pages))
			drm_gem_put_pages(gem_obj, pages, false, true);
	}

	return vkms_obj->pages;
}

void vkms_gem_vunmap(struct drm_gem_object *obj)
{
	struct vkms_gem_object *vkms_obj = drm_gem_to_vkms_gem(obj);

	mutex_lock(&vkms_obj->pages_lock);
	if (vkms_obj->vmap_count < 1) {
		WARN_ON(vkms_obj->vaddr);
		WARN_ON(vkms_obj->pages);
		mutex_unlock(&vkms_obj->pages_lock);
		return;
	}

	vkms_obj->vmap_count--;

	if (vkms_obj->vmap_count == 0) {
		vunmap(vkms_obj->vaddr);
		vkms_obj->vaddr = NULL;
		drm_gem_put_pages(obj, vkms_obj->pages, false, true);
		vkms_obj->pages = NULL;
	}

	mutex_unlock(&vkms_obj->pages_lock);
}

int vkms_gem_vmap(struct drm_gem_object *obj)
{
	struct vkms_gem_object *vkms_obj = drm_gem_to_vkms_gem(obj);
	int ret = 0;

	mutex_lock(&vkms_obj->pages_lock);

	if (!vkms_obj->vaddr) {
		unsigned int n_pages = obj->size >> PAGE_SHIFT;
		struct page **pages = _get_pages(vkms_obj);

		if (IS_ERR(pages)) {
			ret = PTR_ERR(pages);
			goto out;
		}

		vkms_obj->vaddr = vmap(pages, n_pages, VM_MAP, PAGE_KERNEL);
		if (!vkms_obj->vaddr)
			goto err_vmap;
	}

	vkms_obj->vmap_count++;
	goto out;

err_vmap:
	ret = -ENOMEM;
	drm_gem_put_pages(obj, vkms_obj->pages, false, true);
	vkms_obj->pages = NULL;
out:
	mutex_unlock(&vkms_obj->pages_lock);
	return ret;
}
