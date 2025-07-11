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

/*
 * This is vgem, a (non-hardware-backed) GEM service.  This is used by Mesa's
 * software renderer and the X server for efficient buffer sharing.
 */

#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/device/faux.h>
#include <linux/shmem_fs.h>
#include <linux/vmalloc.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_prime.h>

#include "vgem_drv.h"

#define DRIVER_NAME	"vgem"
#define DRIVER_DESC	"Virtual GEM provider"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct vgem_device {
	struct drm_device drm;
	struct faux_device *faux_dev;
} *vgem_device;

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

static void vgem_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct vgem_file *vfile = file->driver_priv;

	vgem_fence_close(vfile);
	kfree(vfile);
}

static struct drm_ioctl_desc vgem_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_ATTACH, vgem_fence_attach_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_SIGNAL, vgem_fence_signal_ioctl, DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_FOPS(vgem_driver_fops);

static struct drm_gem_object *vgem_gem_create_object(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	/*
	 * vgem doesn't have any begin/end cpu access ioctls, therefore must use
	 * coherent memory or dma-buf sharing just wont work.
	 */
	obj->map_wc = true;

	return &obj->base;
}

static const struct drm_driver vgem_driver = {
	.driver_features		= DRIVER_GEM | DRIVER_RENDER,
	.open				= vgem_open,
	.postclose			= vgem_postclose,
	.ioctls				= vgem_ioctls,
	.num_ioctls 			= ARRAY_SIZE(vgem_ioctls),
	.fops				= &vgem_driver_fops,

	DRM_GEM_SHMEM_DRIVER_OPS,
	.gem_create_object		= vgem_gem_create_object,

	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int __init vgem_init(void)
{
	int ret;
	struct faux_device *fdev;

	fdev = faux_device_create("vgem", NULL, NULL);
	if (!fdev)
		return -ENODEV;

	if (!devres_open_group(&fdev->dev, NULL, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	dma_coerce_mask_and_coherent(&fdev->dev,
				     DMA_BIT_MASK(64));

	vgem_device = devm_drm_dev_alloc(&fdev->dev, &vgem_driver,
					 struct vgem_device, drm);
	if (IS_ERR(vgem_device)) {
		ret = PTR_ERR(vgem_device);
		goto out_devres;
	}
	vgem_device->faux_dev = fdev;

	/* Final step: expose the device/driver to userspace */
	ret = drm_dev_register(&vgem_device->drm, 0);
	if (ret)
		goto out_devres;

	return 0;

out_devres:
	devres_release_group(&fdev->dev, NULL);
out_unregister:
	faux_device_destroy(fdev);
	return ret;
}

static void __exit vgem_exit(void)
{
	struct faux_device *fdev = vgem_device->faux_dev;

	drm_dev_unregister(&vgem_device->drm);
	devres_release_group(&fdev->dev, NULL);
	faux_device_destroy(fdev);
}

module_init(vgem_init);
module_exit(vgem_exit);

MODULE_AUTHOR("Red Hat, Inc.");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
