/* exynos_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_buf.h"

static unsigned int convert_to_vm_err_msg(int msg)
{
	unsigned int out_msg;

	switch (msg) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		out_msg = VM_FAULT_NOPAGE;
		break;

	case -ENOMEM:
		out_msg = VM_FAULT_OOM;
		break;

	default:
		out_msg = VM_FAULT_SIGBUS;
		break;
	}

	return out_msg;
}

static unsigned int get_gem_mmap_offset(struct drm_gem_object *obj)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	return (unsigned int)obj->map_list.hash.key << PAGE_SHIFT;
}

struct exynos_drm_gem_obj *exynos_drm_gem_create(struct drm_file *file_priv,
		struct drm_device *dev, unsigned int size,
		unsigned int *handle)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct exynos_drm_buf_entry *entry;
	struct drm_gem_object *obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	size = roundup(size, PAGE_SIZE);

	exynos_gem_obj = kzalloc(sizeof(*exynos_gem_obj), GFP_KERNEL);
	if (!exynos_gem_obj) {
		DRM_ERROR("failed to allocate exynos gem object.\n");
		return ERR_PTR(-ENOMEM);
	}

	/* allocate the new buffer object and memory region. */
	entry = exynos_drm_buf_create(dev, size);
	if (!entry) {
		kfree(exynos_gem_obj);
		return ERR_PTR(-ENOMEM);
	}

	exynos_gem_obj->entry = entry;

	obj = &exynos_gem_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_ERROR("failed to initailize gem object.\n");
		goto err_obj_init;
	}

	DRM_DEBUG_KMS("created file object = 0x%x\n", (unsigned int)obj->filp);

	ret = drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		DRM_ERROR("failed to allocate mmap offset.\n");
		goto err_create_mmap_offset;
	}

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		goto err_handle_create;

	DRM_DEBUG_KMS("gem handle = 0x%x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(obj);

	return exynos_gem_obj;

err_handle_create:
	drm_gem_free_mmap_offset(obj);

err_create_mmap_offset:
	drm_gem_object_release(obj);

err_obj_init:
	exynos_drm_buf_destroy(dev, exynos_gem_obj->entry);

	kfree(exynos_gem_obj);

	return ERR_PTR(ret);
}

int exynos_drm_gem_create_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_exynos_gem_create *args = data;
	struct exynos_drm_gem_obj *exynos_gem_obj;

	DRM_DEBUG_KMS("%s : size = 0x%x\n", __FILE__, args->size);

	exynos_gem_obj = exynos_drm_gem_create(file_priv, dev, args->size,
			&args->handle);
	if (IS_ERR(exynos_gem_obj))
		return PTR_ERR(exynos_gem_obj);

	return 0;
}

int exynos_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_exynos_gem_map_off *args = data;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	DRM_DEBUG_KMS("handle = 0x%x, offset = 0x%lx\n",
			args->handle, (unsigned long)args->offset);

	if (!(dev->driver->driver_features & DRIVER_GEM)) {
		DRM_ERROR("does not support GEM.\n");
		return -ENODEV;
	}

	return exynos_drm_gem_dumb_map_offset(file_priv, dev, args->handle,
			&args->offset);
}

static int exynos_drm_gem_mmap_buffer(struct file *filp,
		struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = filp->private_data;
	struct exynos_drm_gem_obj *exynos_gem_obj = to_exynos_gem_obj(obj);
	struct exynos_drm_buf_entry *entry;
	unsigned long pfn, vm_size;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	vma->vm_flags |= (VM_IO | VM_RESERVED);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_file = filp;

	vm_size = vma->vm_end - vma->vm_start;
	/*
	 * a entry contains information to physically continuous memory
	 * allocated by user request or at framebuffer creation.
	 */
	entry = exynos_gem_obj->entry;

	/* check if user-requested size is valid. */
	if (vm_size > entry->size)
		return -EINVAL;

	/*
	 * get page frame number to physical memory to be mapped
	 * to user space.
	 */
	pfn = exynos_gem_obj->entry->paddr >> PAGE_SHIFT;

	DRM_DEBUG_KMS("pfn = 0x%lx\n", pfn);

	if (remap_pfn_range(vma, vma->vm_start, pfn, vm_size,
				vma->vm_page_prot)) {
		DRM_ERROR("failed to remap pfn range.\n");
		return -EAGAIN;
	}

	return 0;
}

static const struct file_operations exynos_drm_gem_fops = {
	.mmap = exynos_drm_gem_mmap_buffer,
};

int exynos_drm_gem_mmap_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_exynos_gem_mmap *args = data;
	struct drm_gem_object *obj;
	unsigned int addr;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!(dev->driver->driver_features & DRIVER_GEM)) {
		DRM_ERROR("does not support GEM.\n");
		return -ENODEV;
	}

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		return -EINVAL;
	}

	obj->filp->f_op = &exynos_drm_gem_fops;
	obj->filp->private_data = obj;

	down_write(&current->mm->mmap_sem);
	addr = do_mmap(obj->filp, 0, args->size,
			PROT_READ | PROT_WRITE, MAP_SHARED, 0);
	up_write(&current->mm->mmap_sem);

	drm_gem_object_unreference_unlocked(obj);

	if (IS_ERR((void *)addr))
		return PTR_ERR((void *)addr);

	args->mapped = addr;

	DRM_DEBUG_KMS("mapped = 0x%lx\n", (unsigned long)args->mapped);

	return 0;
}

int exynos_drm_gem_init_object(struct drm_gem_object *obj)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	return 0;
}

void exynos_drm_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	DRM_DEBUG_KMS("handle count = %d\n",
			atomic_read(&gem_obj->handle_count));

	if (gem_obj->map_list.map)
		drm_gem_free_mmap_offset(gem_obj);

	/* release file pointer to gem object. */
	drm_gem_object_release(gem_obj);

	exynos_gem_obj = to_exynos_gem_obj(gem_obj);

	exynos_drm_buf_destroy(gem_obj->dev, exynos_gem_obj->entry);

	kfree(exynos_gem_obj);
}

int exynos_drm_gem_dumb_create(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * alocate memory to be used for framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_CREATE_DUMB command.
	 */

	args->pitch = args->width * args->bpp >> 3;
	args->size = args->pitch * args->height;

	exynos_gem_obj = exynos_drm_gem_create(file_priv, dev, args->size,
							&args->handle);
	if (IS_ERR(exynos_gem_obj))
		return PTR_ERR(exynos_gem_obj);

	return 0;
}

int exynos_drm_gem_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *dev, uint32_t handle, uint64_t *offset)
{
	struct exynos_drm_gem_obj *exynos_gem_obj;
	struct drm_gem_object *obj;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mutex_lock(&dev->struct_mutex);

	/*
	 * get offset of memory allocated for drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_MAP_DUMB command.
	 */

	obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!obj) {
		DRM_ERROR("failed to lookup gem object.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	exynos_gem_obj = to_exynos_gem_obj(obj);

	*offset = get_gem_mmap_offset(&exynos_gem_obj->base);

	drm_gem_object_unreference(obj);

	DRM_DEBUG_KMS("offset = 0x%lx\n", (unsigned long)*offset);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int exynos_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct exynos_drm_gem_obj *exynos_gem_obj = to_exynos_gem_obj(obj);
	struct drm_device *dev = obj->dev;
	unsigned long pfn;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	mutex_lock(&dev->struct_mutex);

	pfn = (exynos_gem_obj->entry->paddr >> PAGE_SHIFT) + page_offset;

	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

	mutex_unlock(&dev->struct_mutex);

	return convert_to_vm_err_msg(ret);
}

int exynos_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	return ret;
}


int exynos_drm_gem_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *dev, unsigned int handle)
{
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * obj->refcount and obj->handle_count are decreased and
	 * if both them are 0 then exynos_drm_gem_free_object()
	 * would be called by callback to release resources.
	 */
	ret = drm_gem_handle_delete(file_priv, handle);
	if (ret < 0) {
		DRM_ERROR("failed to delete drm_gem_handle.\n");
		return ret;
	}

	return 0;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM GEM Module");
MODULE_LICENSE("GPL");
