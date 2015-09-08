/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <drm/drmP.h>

#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "sti_crtc.h"
#include "sti_drv.h"

#define DRIVER_NAME	"sti"
#define DRIVER_DESC	"STMicroelectronics SoC DRM"
#define DRIVER_DATE	"20140601"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define STI_MAX_FB_HEIGHT	4096
#define STI_MAX_FB_WIDTH	4096

static void sti_atomic_schedule(struct sti_private *private,
				struct drm_atomic_state *state)
{
	private->commit.state = state;
	schedule_work(&private->commit.work);
}

static void sti_atomic_complete(struct sti_private *private,
				struct drm_atomic_state *state)
{
	struct drm_device *drm = private->drm_dev;

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one condition: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 */

	drm_atomic_helper_commit_modeset_disables(drm, state);
	drm_atomic_helper_commit_planes(drm, state, false);
	drm_atomic_helper_commit_modeset_enables(drm, state);

	drm_atomic_helper_wait_for_vblanks(drm, state);

	drm_atomic_helper_cleanup_planes(drm, state);
	drm_atomic_state_free(state);
}

static void sti_atomic_work(struct work_struct *work)
{
	struct sti_private *private = container_of(work,
			struct sti_private, commit.work);

	sti_atomic_complete(private, private->commit.state);
}

static int sti_atomic_commit(struct drm_device *drm,
			     struct drm_atomic_state *state, bool async)
{
	struct sti_private *private = drm->dev_private;
	int err;

	err = drm_atomic_helper_prepare_planes(drm, state);
	if (err)
		return err;

	/* serialize outstanding asynchronous commits */
	mutex_lock(&private->commit.lock);
	flush_work(&private->commit.work);

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(drm, state);

	if (async)
		sti_atomic_schedule(private, state);
	else
		sti_atomic_complete(private, state);

	mutex_unlock(&private->commit.lock);
	return 0;
}

static struct drm_mode_config_funcs sti_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = sti_atomic_commit,
};

static void sti_mode_config_init(struct drm_device *dev)
{
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	/*
	 * set max width and height as default value.
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	dev->mode_config.max_width = STI_MAX_FB_HEIGHT;
	dev->mode_config.max_height = STI_MAX_FB_WIDTH;

	dev->mode_config.funcs = &sti_mode_config_funcs;
}

static int sti_load(struct drm_device *dev, unsigned long flags)
{
	struct sti_private *private;
	int ret;

	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("Failed to allocate private\n");
		return -ENOMEM;
	}
	dev->dev_private = (void *)private;
	private->drm_dev = dev;

	mutex_init(&private->commit.lock);
	INIT_WORK(&private->commit.work, sti_atomic_work);

	drm_mode_config_init(dev);
	drm_kms_helper_poll_init(dev);

	sti_mode_config_init(dev);

	ret = component_bind_all(dev->dev, dev);
	if (ret) {
		drm_kms_helper_poll_fini(dev);
		drm_mode_config_cleanup(dev);
		kfree(private);
		return ret;
	}

	drm_mode_config_reset(dev);

#ifdef CONFIG_DRM_STI_FBDEV
	drm_fbdev_cma_init(dev, 32,
			   dev->mode_config.num_crtc,
			   dev->mode_config.num_connector);
#endif
	return 0;
}

static const struct file_operations sti_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release = drm_release,
};

static struct dma_buf *sti_gem_prime_export(struct drm_device *dev,
					    struct drm_gem_object *obj,
					    int flags)
{
	/* we want to be able to write in mmapped buffer */
	flags |= O_RDWR;
	return drm_gem_prime_export(dev, obj, flags);
}

static struct drm_driver sti_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_MODESET |
	    DRIVER_GEM | DRIVER_PRIME,
	.load = sti_load,
	.gem_free_object = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.fops = &sti_driver_fops,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = sti_crtc_enable_vblank,
	.disable_vblank = sti_crtc_disable_vblank,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = sti_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int sti_bind(struct device *dev)
{
	return drm_platform_init(&sti_driver, to_platform_device(dev));
}

static void sti_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops sti_ops = {
	.bind = sti_bind,
	.unbind = sti_unbind,
};

static int sti_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *child_np;
	struct component_match *match = NULL;

	dma_set_coherent_mask(dev, DMA_BIT_MASK(32));

	of_platform_populate(node, NULL, NULL, dev);

	child_np = of_get_next_available_child(node, NULL);

	while (child_np) {
		component_match_add(dev, &match, compare_of, child_np);
		of_node_put(child_np);
		child_np = of_get_next_available_child(node, child_np);
	}

	return component_master_add_with_match(dev, &sti_ops, match);
}

static int sti_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &sti_ops);
	of_platform_depopulate(&pdev->dev);

	return 0;
}

static const struct of_device_id sti_dt_ids[] = {
	{ .compatible = "st,sti-display-subsystem", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, sti_dt_ids);

static struct platform_driver sti_platform_driver = {
	.probe = sti_platform_probe,
	.remove = sti_platform_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = sti_dt_ids,
	},
};

module_platform_driver(sti_platform_driver);

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
