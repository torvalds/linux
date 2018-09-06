/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/tinydrm/tinydrm.h>
#include <linux/device.h>
#include <linux/dma-buf.h>

/**
 * DOC: overview
 *
 * This library provides driver helpers for very simple display hardware.
 *
 * It is based on &drm_simple_display_pipe coupled with a &drm_connector which
 * has only one fixed &drm_display_mode. The framebuffers are backed by the
 * cma helper and have support for framebuffer flushing (dirty).
 * fbdev support is also included.
 *
 */

/**
 * DOC: core
 *
 * The driver allocates &tinydrm_device, initializes it using
 * devm_tinydrm_init(), sets up the pipeline using tinydrm_display_pipe_init()
 * and registers the DRM device using devm_tinydrm_register().
 */

/**
 * tinydrm_gem_cma_prime_import_sg_table - Produce a CMA GEM object from
 *     another driver's scatter/gather table of pinned pages
 * @drm: DRM device to import into
 * @attach: DMA-BUF attachment
 * @sgt: Scatter/gather table of pinned pages
 *
 * This function imports a scatter/gather table exported via DMA-BUF by
 * another driver using drm_gem_cma_prime_import_sg_table(). It sets the
 * kernel virtual address on the CMA object. Drivers should use this as their
 * &drm_driver->gem_prime_import_sg_table callback if they need the virtual
 * address. tinydrm_gem_cma_free_object() should be used in combination with
 * this function.
 *
 * Returns:
 * A pointer to a newly created GEM object or an ERR_PTR-encoded negative
 * error code on failure.
 */
struct drm_gem_object *
tinydrm_gem_cma_prime_import_sg_table(struct drm_device *drm,
				      struct dma_buf_attachment *attach,
				      struct sg_table *sgt)
{
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *obj;
	void *vaddr;

	vaddr = dma_buf_vmap(attach->dmabuf);
	if (!vaddr) {
		DRM_ERROR("Failed to vmap PRIME buffer\n");
		return ERR_PTR(-ENOMEM);
	}

	obj = drm_gem_cma_prime_import_sg_table(drm, attach, sgt);
	if (IS_ERR(obj)) {
		dma_buf_vunmap(attach->dmabuf, vaddr);
		return obj;
	}

	cma_obj = to_drm_gem_cma_obj(obj);
	cma_obj->vaddr = vaddr;

	return obj;
}
EXPORT_SYMBOL(tinydrm_gem_cma_prime_import_sg_table);

/**
 * tinydrm_gem_cma_free_object - Free resources associated with a CMA GEM
 *                               object
 * @gem_obj: GEM object to free
 *
 * This function frees the backing memory of the CMA GEM object, cleans up the
 * GEM object state and frees the memory used to store the object itself using
 * drm_gem_cma_free_object(). It also handles PRIME buffers which has the kernel
 * virtual address set by tinydrm_gem_cma_prime_import_sg_table(). Drivers
 * can use this as their &drm_driver->gem_free_object_unlocked callback.
 */
void tinydrm_gem_cma_free_object(struct drm_gem_object *gem_obj)
{
	if (gem_obj->import_attach) {
		struct drm_gem_cma_object *cma_obj;

		cma_obj = to_drm_gem_cma_obj(gem_obj);
		dma_buf_vunmap(gem_obj->import_attach->dmabuf, cma_obj->vaddr);
		cma_obj->vaddr = NULL;
	}

	drm_gem_cma_free_object(gem_obj);
}
EXPORT_SYMBOL_GPL(tinydrm_gem_cma_free_object);

static struct drm_framebuffer *
tinydrm_fb_create(struct drm_device *drm, struct drm_file *file_priv,
		  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct tinydrm_device *tdev = drm->dev_private;

	return drm_gem_fb_create_with_funcs(drm, file_priv, mode_cmd,
					    tdev->fb_funcs);
}

static const struct drm_mode_config_funcs tinydrm_mode_config_funcs = {
	.fb_create = tinydrm_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int tinydrm_init(struct device *parent, struct tinydrm_device *tdev,
			const struct drm_framebuffer_funcs *fb_funcs,
			struct drm_driver *driver)
{
	struct drm_device *drm;

	mutex_init(&tdev->dirty_lock);
	tdev->fb_funcs = fb_funcs;

	/*
	 * We don't embed drm_device, because that prevent us from using
	 * devm_kzalloc() to allocate tinydrm_device in the driver since
	 * drm_dev_put() frees the structure. The devm_ functions provide
	 * for easy error handling.
	 */
	drm = drm_dev_alloc(driver, parent);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	tdev->drm = drm;
	drm->dev_private = tdev;
	drm_mode_config_init(drm);
	drm->mode_config.funcs = &tinydrm_mode_config_funcs;

	return 0;
}

static void tinydrm_fini(struct tinydrm_device *tdev)
{
	drm_mode_config_cleanup(tdev->drm);
	mutex_destroy(&tdev->dirty_lock);
	tdev->drm->dev_private = NULL;
	drm_dev_put(tdev->drm);
}

static void devm_tinydrm_release(void *data)
{
	tinydrm_fini(data);
}

/**
 * devm_tinydrm_init - Initialize tinydrm device
 * @parent: Parent device object
 * @tdev: tinydrm device
 * @fb_funcs: Framebuffer functions
 * @driver: DRM driver
 *
 * This function initializes @tdev, the underlying DRM device and it's
 * mode_config. Resources will be automatically freed on driver detach (devres)
 * using drm_mode_config_cleanup() and drm_dev_put().
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int devm_tinydrm_init(struct device *parent, struct tinydrm_device *tdev,
		      const struct drm_framebuffer_funcs *fb_funcs,
		      struct drm_driver *driver)
{
	int ret;

	ret = tinydrm_init(parent, tdev, fb_funcs, driver);
	if (ret)
		return ret;

	ret = devm_add_action(parent, devm_tinydrm_release, tdev);
	if (ret)
		tinydrm_fini(tdev);

	return ret;
}
EXPORT_SYMBOL(devm_tinydrm_init);

static int tinydrm_register(struct tinydrm_device *tdev)
{
	struct drm_device *drm = tdev->drm;
	int ret;

	ret = drm_dev_register(tdev->drm, 0);
	if (ret)
		return ret;

	ret = drm_fbdev_generic_setup(drm, 0);
	if (ret)
		DRM_ERROR("Failed to initialize fbdev: %d\n", ret);

	return 0;
}

static void tinydrm_unregister(struct tinydrm_device *tdev)
{
	drm_atomic_helper_shutdown(tdev->drm);
	drm_dev_unregister(tdev->drm);
}

static void devm_tinydrm_register_release(void *data)
{
	tinydrm_unregister(data);
}

/**
 * devm_tinydrm_register - Register tinydrm device
 * @tdev: tinydrm device
 *
 * This function registers the underlying DRM device and fbdev.
 * These resources will be automatically unregistered on driver detach (devres)
 * and the display pipeline will be disabled.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
int devm_tinydrm_register(struct tinydrm_device *tdev)
{
	struct device *dev = tdev->drm->dev;
	int ret;

	ret = tinydrm_register(tdev);
	if (ret)
		return ret;

	ret = devm_add_action(dev, devm_tinydrm_register_release, tdev);
	if (ret)
		tinydrm_unregister(tdev);

	return ret;
}
EXPORT_SYMBOL(devm_tinydrm_register);

/**
 * tinydrm_shutdown - Shutdown tinydrm
 * @tdev: tinydrm device
 *
 * This function makes sure that the display pipeline is disabled.
 * Used by drivers in their shutdown callback to turn off the display
 * on machine shutdown and reboot.
 */
void tinydrm_shutdown(struct tinydrm_device *tdev)
{
	drm_atomic_helper_shutdown(tdev->drm);
}
EXPORT_SYMBOL(tinydrm_shutdown);

MODULE_LICENSE("GPL");
