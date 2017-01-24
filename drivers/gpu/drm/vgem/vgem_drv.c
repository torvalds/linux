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
	unsigned long vaddr = vmf->address;
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

static int vgem_open(struct drm_device *dev, struct drm_file *file)
{
	struct vgem_file *vfile;
	int ret;

	vfile = kzalloc(sizeof(*vfile), GFP_KERNEL);
	if (!vfile)
		return -ENOMEM;

	file->driver_priv = vfile;

	ret = vgem_fence_open(vfile);
	if (ret) {
		kfree(vfile);
		return ret;
	}

	return 0;
}

static void vgem_preclose(struct drm_device *dev, struct drm_file *file)
{
	struct vgem_file *vfile = file->driver_priv;

	vgem_fence_close(vfile);
	kfree(vfile);
}

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
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_ATTACH, vgem_fence_attach_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_SIGNAL, vgem_fence_signal_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
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

static int vgem_prime_pin(struct drm_gem_object *obj)
{
	long n_pages = obj->size >> PAGE_SHIFT;
	struct page **pages;

	/* Flush the object from the CPU cache so that importers can rely
	 * on coherent indirect access via the exported dma-address.
	 */
	pages = drm_gem_get_pages(obj);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	drm_clflush_pages(pages, n_pages);
	drm_gem_put_pages(obj, pages, true, false);

	return 0;
}

static struct sg_table *vgem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct sg_table *st;
	struct page **pages;

	pages = drm_gem_get_pages(obj);
	if (IS_ERR(pages))
		return ERR_CAST(pages);

	st = drm_prime_pages_to_sg(pages, obj->size >> PAGE_SHIFT);
	drm_gem_put_pages(obj, pages, false, false);

	return st;
}

static void *vgem_prime_vmap(struct drm_gem_object *obj)
{
	long n_pages = obj->size >> PAGE_SHIFT;
	struct page **pages;
	void *addr;

	pages = drm_gem_get_pages(obj);
	if (IS_ERR(pages))
		return NULL;

	addr = vmap(pages, n_pages, 0, pgprot_writecombine(PAGE_KERNEL));
	drm_gem_put_pages(obj, pages, false, false);

	return addr;
}

static void vgem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	vunmap(vaddr);
}

static int vgem_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma)
{
	int ret;

	if (obj->size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (!obj->filp)
		return -ENODEV;

	ret = obj->filp->f_op->mmap(obj->filp, vma);
	if (ret)
		return ret;

	fput(vma->vm_file);
	vma->vm_file = get_file(obj->filp);
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));

	return 0;
}

static struct drm_driver vgem_driver = {
	.driver_features		= DRIVER_GEM | DRIVER_PRIME,
	.open				= vgem_open,
	.preclose			= vgem_preclose,
	.gem_free_object_unlocked	= vgem_gem_free_object,
	.gem_vm_ops			= &vgem_gem_vm_ops,
	.ioctls				= vgem_ioctls,
	.num_ioctls 			= ARRAY_SIZE(vgem_ioctls),
	.fops				= &vgem_driver_fops,

	.dumb_create			= vgem_gem_dumb_create,
	.dumb_map_offset		= vgem_gem_dumb_map,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.gem_prime_pin = vgem_prime_pin,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_get_sg_table = vgem_prime_get_sg_table,
	.gem_prime_vmap = vgem_prime_vmap,
	.gem_prime_vunmap = vgem_prime_vunmap,
	.gem_prime_mmap = vgem_prime_mmap,

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
	if (IS_ERR(vgem_device)) {
		ret = PTR_ERR(vgem_device);
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
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
