/*
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "cirrus_drv.h"


static void cirrus_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct cirrus_framebuffer *cirrus_fb = to_cirrus_framebuffer(fb);

	drm_gem_object_unreference_unlocked(cirrus_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

static const struct drm_framebuffer_funcs cirrus_fb_funcs = {
	.destroy = cirrus_user_framebuffer_destroy,
};

int cirrus_framebuffer_init(struct drm_device *dev,
			    struct cirrus_framebuffer *gfb,
			    const struct drm_mode_fb_cmd2 *mode_cmd,
			    struct drm_gem_object *obj)
{
	int ret;

	drm_helper_mode_fill_fb_struct(&gfb->base, mode_cmd);
	gfb->obj = obj;
	ret = drm_framebuffer_init(dev, &gfb->base, &cirrus_fb_funcs);
	if (ret) {
		DRM_ERROR("drm_framebuffer_init failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static struct drm_framebuffer *
cirrus_user_framebuffer_create(struct drm_device *dev,
			       struct drm_file *filp,
			       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct cirrus_device *cdev = dev->dev_private;
	struct drm_gem_object *obj;
	struct cirrus_framebuffer *cirrus_fb;
	int ret;
	u32 bpp, depth;

	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &depth, &bpp);

	if (!cirrus_check_framebuffer(cdev, mode_cmd->width, mode_cmd->height,
				      bpp, mode_cmd->pitches[0]))
		return ERR_PTR(-EINVAL);

	obj = drm_gem_object_lookup(filp, mode_cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	cirrus_fb = kzalloc(sizeof(*cirrus_fb), GFP_KERNEL);
	if (!cirrus_fb) {
		drm_gem_object_unreference_unlocked(obj);
		return ERR_PTR(-ENOMEM);
	}

	ret = cirrus_framebuffer_init(dev, cirrus_fb, mode_cmd, obj);
	if (ret) {
		drm_gem_object_unreference_unlocked(obj);
		kfree(cirrus_fb);
		return ERR_PTR(ret);
	}
	return &cirrus_fb->base;
}

static const struct drm_mode_config_funcs cirrus_mode_funcs = {
	.fb_create = cirrus_user_framebuffer_create,
};

/* Unmap the framebuffer from the core and release the memory */
static void cirrus_vram_fini(struct cirrus_device *cdev)
{
	iounmap(cdev->rmmio);
	cdev->rmmio = NULL;
	if (cdev->mc.vram_base)
		release_mem_region(cdev->mc.vram_base, cdev->mc.vram_size);
}

/* Map the framebuffer from the card and configure the core */
static int cirrus_vram_init(struct cirrus_device *cdev)
{
	/* BAR 0 is VRAM */
	cdev->mc.vram_base = pci_resource_start(cdev->dev->pdev, 0);
	cdev->mc.vram_size = pci_resource_len(cdev->dev->pdev, 0);

	if (!request_mem_region(cdev->mc.vram_base, cdev->mc.vram_size,
				"cirrusdrmfb_vram")) {
		DRM_ERROR("can't reserve VRAM\n");
		return -ENXIO;
	}

	return 0;
}

/*
 * Our emulated hardware has two sets of memory. One is video RAM and can
 * simply be used as a linear framebuffer - the other provides mmio access
 * to the display registers. The latter can also be accessed via IO port
 * access, but we map the range and use mmio to program them instead
 */

int cirrus_device_init(struct cirrus_device *cdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev, uint32_t flags)
{
	int ret;

	cdev->dev = ddev;
	cdev->flags = flags;

	/* Hardcode the number of CRTCs to 1 */
	cdev->num_crtc = 1;

	/* BAR 0 is the framebuffer, BAR 1 contains registers */
	cdev->rmmio_base = pci_resource_start(cdev->dev->pdev, 1);
	cdev->rmmio_size = pci_resource_len(cdev->dev->pdev, 1);

	if (!request_mem_region(cdev->rmmio_base, cdev->rmmio_size,
				"cirrusdrmfb_mmio")) {
		DRM_ERROR("can't reserve mmio registers\n");
		return -ENOMEM;
	}

	cdev->rmmio = ioremap(cdev->rmmio_base, cdev->rmmio_size);

	if (cdev->rmmio == NULL)
		return -ENOMEM;

	ret = cirrus_vram_init(cdev);
	if (ret) {
		release_mem_region(cdev->rmmio_base, cdev->rmmio_size);
		return ret;
	}

	return 0;
}

void cirrus_device_fini(struct cirrus_device *cdev)
{
	release_mem_region(cdev->rmmio_base, cdev->rmmio_size);
	cirrus_vram_fini(cdev);
}

/*
 * Functions here will be called by the core once it's bound the driver to
 * a PCI device
 */

int cirrus_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct cirrus_device *cdev;
	int r;

	cdev = kzalloc(sizeof(struct cirrus_device), GFP_KERNEL);
	if (cdev == NULL)
		return -ENOMEM;
	dev->dev_private = (void *)cdev;

	r = cirrus_device_init(cdev, dev, dev->pdev, flags);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during GPU init: %d\n", r);
		goto out;
	}

	r = cirrus_mm_init(cdev);
	if (r) {
		dev_err(&dev->pdev->dev, "fatal err on mm init\n");
		goto out;
	}

	/*
	 * cirrus_modeset_init() is initializing/registering the emulated fbdev
	 * and DRM internals can access/test some of the fields in
	 * mode_config->funcs as part of the fbdev registration process.
	 * Make sure dev->mode_config.funcs is properly set to avoid
	 * dereferencing a NULL pointer.
	 * FIXME: mode_config.funcs assignment should probably be done in
	 * cirrus_modeset_init() (that's a common pattern seen in other DRM
	 * drivers).
	 */
	dev->mode_config.funcs = &cirrus_mode_funcs;
	r = cirrus_modeset_init(cdev);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during modeset init: %d\n", r);
		goto out;
	}

	return 0;
out:
	cirrus_driver_unload(dev);
	return r;
}

int cirrus_driver_unload(struct drm_device *dev)
{
	struct cirrus_device *cdev = dev->dev_private;

	if (cdev == NULL)
		return 0;
	cirrus_modeset_fini(cdev);
	cirrus_mm_fini(cdev);
	cirrus_device_fini(cdev);
	kfree(cdev);
	dev->dev_private = NULL;
	return 0;
}

int cirrus_gem_create(struct drm_device *dev,
		   u32 size, bool iskernel,
		   struct drm_gem_object **obj)
{
	struct cirrus_bo *cirrusbo;
	int ret;

	*obj = NULL;

	size = roundup(size, PAGE_SIZE);
	if (size == 0)
		return -EINVAL;

	ret = cirrus_bo_create(dev, size, 0, 0, &cirrusbo);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("failed to allocate GEM object\n");
		return ret;
	}
	*obj = &cirrusbo->gem;
	return 0;
}

int cirrus_dumb_create(struct drm_file *file,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	int ret;
	struct drm_gem_object *gobj;
	u32 handle;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	ret = cirrus_gem_create(dev, args->size, false,
			     &gobj);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file, gobj, &handle);
	drm_gem_object_unreference_unlocked(gobj);
	if (ret)
		return ret;

	args->handle = handle;
	return 0;
}

static void cirrus_bo_unref(struct cirrus_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->bo);
	ttm_bo_unref(&tbo);
	*bo = NULL;
}

void cirrus_gem_free_object(struct drm_gem_object *obj)
{
	struct cirrus_bo *cirrus_bo = gem_to_cirrus_bo(obj);

	cirrus_bo_unref(&cirrus_bo);
}


static inline u64 cirrus_bo_mmap_offset(struct cirrus_bo *bo)
{
	return drm_vma_node_offset_addr(&bo->bo.vma_node);
}

int
cirrus_dumb_mmap_offset(struct drm_file *file,
		     struct drm_device *dev,
		     uint32_t handle,
		     uint64_t *offset)
{
	struct drm_gem_object *obj;
	struct cirrus_bo *bo;

	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL)
		return -ENOENT;

	bo = gem_to_cirrus_bo(obj);
	*offset = cirrus_bo_mmap_offset(bo);

	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

bool cirrus_check_framebuffer(struct cirrus_device *cdev, int width, int height,
			      int bpp, int pitch)
{
	const int max_pitch = 0x1FF << 3; /* (4096 - 1) & ~111b bytes */
	const int max_size = cdev->mc.vram_size;

	if (bpp > cirrus_bpp)
		return false;
	if (bpp > 32)
		return false;

	if (pitch > max_pitch)
		return false;

	if (pitch * height > max_size)
		return false;

	return true;
}
