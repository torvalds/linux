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

#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "sti_drm_drv.h"
#include "sti_drm_crtc.h"

#define DRIVER_NAME	"sti"
#define DRIVER_DESC	"STMicroelectronics SoC DRM"
#define DRIVER_DATE	"20140601"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define STI_MAX_FB_HEIGHT	4096
#define STI_MAX_FB_WIDTH	4096

static struct drm_mode_config_funcs sti_drm_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
};

static void sti_drm_mode_config_init(struct drm_device *dev)
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

	dev->mode_config.funcs = &sti_drm_mode_config_funcs;
}

static int sti_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct sti_drm_private *private;
	int ret;

	private = kzalloc(sizeof(struct sti_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("Failed to allocate private\n");
		return -ENOMEM;
	}
	dev->dev_private = (void *)private;
	private->drm_dev = dev;

	drm_mode_config_init(dev);
	drm_kms_helper_poll_init(dev);

	sti_drm_mode_config_init(dev);

	ret = component_bind_all(dev->dev, dev);
	if (ret) {
		drm_kms_helper_poll_fini(dev);
		drm_mode_config_cleanup(dev);
		kfree(private);
		return ret;
	}

	drm_helper_disable_unused_functions(dev);

#ifdef CONFIG_DRM_STI_FBDEV
	drm_fbdev_cma_init(dev, 32,
		   dev->mode_config.num_crtc,
		   dev->mode_config.num_connector);
#endif
	return 0;
}

static const struct file_operations sti_drm_driver_fops = {
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

static struct dma_buf *sti_drm_gem_prime_export(struct drm_device *dev,
						struct drm_gem_object *obj,
						int flags)
{
	/* we want to be able to write in mmapped buffer */
	flags |= O_RDWR;
	return drm_gem_prime_export(dev, obj, flags);
}

static struct drm_driver sti_drm_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_MODESET |
	    DRIVER_GEM | DRIVER_PRIME,
	.load = sti_drm_load,
	.gem_free_object = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.fops = &sti_drm_driver_fops,

	.get_vblank_counter = drm_vblank_count,
	.enable_vblank = sti_drm_crtc_enable_vblank,
	.disable_vblank = sti_drm_crtc_disable_vblank,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = sti_drm_gem_prime_export,
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

static int sti_drm_bind(struct device *dev)
{
	return drm_platform_init(&sti_drm_driver, to_platform_device(dev));
}

static void sti_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops sti_drm_ops = {
	.bind = sti_drm_bind,
	.unbind = sti_drm_unbind,
};

static int sti_drm_master_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->parent->of_node;
	struct device_node *child_np;
	struct component_match *match = NULL;

	dma_set_coherent_mask(dev, DMA_BIT_MASK(32));

	child_np = of_get_next_available_child(node, NULL);

	while (child_np) {
		component_match_add(dev, &match, compare_of, child_np);
		of_node_put(child_np);
		child_np = of_get_next_available_child(node, child_np);
	}

	return component_master_add_with_match(dev, &sti_drm_ops, match);
}

static int sti_drm_master_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &sti_drm_ops);
	return 0;
}

static struct platform_driver sti_drm_master_driver = {
	.probe = sti_drm_master_probe,
	.remove = sti_drm_master_remove,
	.driver = {
		.name = DRIVER_NAME "__master",
	},
};

static int sti_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct platform_device *master;

	of_platform_populate(node, NULL, NULL, dev);

	platform_driver_register(&sti_drm_master_driver);
	master = platform_device_register_resndata(dev,
			DRIVER_NAME "__master", -1,
			NULL, 0, NULL, 0);
	if (IS_ERR(master))
               return PTR_ERR(master);

	platform_set_drvdata(pdev, master);
	return 0;
}

static int sti_drm_platform_remove(struct platform_device *pdev)
{
	struct platform_device *master = platform_get_drvdata(pdev);

	of_platform_depopulate(&pdev->dev);
	platform_device_unregister(master);
	platform_driver_unregister(&sti_drm_master_driver);
	return 0;
}

static const struct of_device_id sti_drm_dt_ids[] = {
	{ .compatible = "st,sti-display-subsystem", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, sti_drm_dt_ids);

static struct platform_driver sti_drm_platform_driver = {
	.probe = sti_drm_platform_probe,
	.remove = sti_drm_platform_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = sti_drm_dt_ids,
	},
};

module_platform_driver(sti_drm_platform_driver);

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
