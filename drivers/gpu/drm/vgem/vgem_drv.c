/*
 * Copyright 2011 Red Hat, Inc.
 * Copyright © 2014 The Chromium OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * them Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTIBILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Adam Jackson <ajax@redhat.com>
 *	Ben Widawsky <ben@bwidawsk.net>
 */

/**
 * This is vgem, a (non-hardware-backed) GEM service.  This is used by Mesa's
 * software renderer and the X server for efficient buffer sharing.
 */

#include <linux/module.h>
#include <linux/ramfs.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include "vgem_drv.h"

#define DRIVER_NAME	"vgem"
#define DRIVER_DESC	"Virtual GEM provider"
#define DRIVER_DATE	"20120112"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static void vgem_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_vgem_gem_object *vgem_obj = to_vgem_bo(obj);

	drm_gem_object_release(obj);
	kfree(vgem_obj);
}

static int vgem_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_vgem_gem_object *obj = vma->vm_private_data;
	/* We don't use vmf->pgoff since that has the fake offset */
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	struct page *page;

	page = shmem_read_mapping_page(file_inode(obj->base.filp)->i_mapping,
				       (vaddr - vma->vm_start) >> PAGE_SHIFT);
	if (!IS_ERR(page)) {
		vmf->page = page;
		return 0;
	} else switch (PTR_ERR(page)) {
		case -ENOSPC:
		case -ENOMEM:
			return VM_FAULT_OOM;
		case -EBUSY:
			return VM_FAULT_RETRY;
		case -EFAULT:
		case -EINVAL:
			return VM_FAULT_SIGBUS;
		default:
			WARN_ON_ONCE(PTR_ERR(page));
			return VM_FAULT_SIGBUS;
	}
}

static const struct vm_operations_struct vgem_gem_vm_ops = {
	.fault = vgem_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

/* ioctls */

static struct drm_gem_object *vgem_gem_create(struct drm_device *dev,
					      struct drm_file *file,
					      unsigned int *handle,
					      unsigned long size)
{
	struct drm_vgem_gem_object *obj;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_object_init(dev, &obj->base, roundup(size, PAGE_SIZE));
	if (ret)
		goto err_free;

	ret = drm_gem_handle_create(file, &obj->base, handle);
	drm_gem_object_unreference_unlocked(&obj->base);
	if (ret)
		goto err;

	return &obj->base;

err_free:
	kfree(obj);
err:
	return ERR_PTR(ret);
}

static int vgem_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
				struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gem_object;
	u64 pitch, size;

	pitch = args->width * DIV_ROUND_UP(args->bpp, 8);
	size = args->height * pitch;
	if (size == 0)
		return -EINVAL;

	gem_object = vgem_gem_create(dev, file, &args->handle, size);
	if (IS_ERR(gem_object))
		return PTR_ERR(gem_object);

	args->size = gem_object->size;
	args->pitch = pitch;

	DRM_DEBUG_DRIVER("Created object of size %lld\n", size);

	return 0;
}

static int vgem_gem_dumb_map(struct drm_file *file, struct drm_device *dev,
			     uint32_t handle, uint64_t *offset)
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
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static struct drm_ioctl_desc vgem_ioctls[] = {
};

static int vgem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long flags = vma->vm_flags;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	/* Keep the WC mmaping set by drm_gem_mmap() but our pages
	 * are ordinary and not special.
	 */
	vma->vm_flags = flags | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}

static const struct file_operations vgem_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= vgem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl = drm_ioctl,
	.release	= drm_release,
};

static struct drm_driver vgem_driver = {
	.driver_features		= DRIVER_GEM,
	.gem_free_object_unlocked	= vgem_gem_free_object,
	.gem_vm_ops			= &vgem_gem_vm_ops,
	.ioctls				= vgem_ioctls,
	.fops				= &vgem_driver_fops,
	.dumb_create			= vgem_gem_dumb_create,
	.dumb_map_offset		= vgem_gem_dumb_map,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static struct drm_device *vgem_device;

static int __init vgem_init(void)
{
	int ret;

	vgem_device = drm_dev_alloc(&vgem_driver, NULL);
	if (!vgem_device) {
		ret = -ENOMEM;
		goto out;
	}

	ret  = drm_dev_register(vgem_device, 0);
	if (ret)
		goto out_unref;

	return 0;

out_unref:
	drm_dev_unref(vgem_device);
out:
	return ret;
}

static void __exit vgem_exit(void)
{
	drm_dev_unregister(vgem_device);
	drm_dev_unref(vgem_device);
}

module_init(vgem_init);
module_exit(vgem_exit);

MODULE_AUTHOR("Red Hat, Inc.");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
