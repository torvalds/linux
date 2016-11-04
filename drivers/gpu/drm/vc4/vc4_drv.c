/*
 * Copyright (C) 2014-2015 Broadcom
 * Copyright (C) 2013 Red Hat
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "drm_fb_cma_helper.h"
#include <drm/drm_fb_helper.h>

#include "uapi/drm/vc4_drm.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

#define DRIVER_NAME "vc4"
#define DRIVER_DESC "Broadcom VC4 graphics"
#define DRIVER_DATE "20140616"
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

/* Helper function for mapping the regs on a platform device. */
void __iomem *vc4_ioremap_regs(struct platform_device *dev, int index)
{
	struct resource *res;
	void __iomem *map;

	res = platform_get_resource(dev, IORESOURCE_MEM, index);
	map = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(map)) {
		DRM_ERROR("Failed to map registers: %ld\n", PTR_ERR(map));
		return map;
	}

	return map;
}

static int vc4_get_param_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct drm_vc4_get_param *args = data;
	int ret;

	if (args->pad != 0)
		return -EINVAL;

	switch (args->param) {
	case DRM_VC4_PARAM_V3D_IDENT0:
		ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);
		if (ret < 0)
			return ret;
		args->value = V3D_READ(V3D_IDENT0);
		pm_runtime_mark_last_busy(&vc4->v3d->pdev->dev);
		pm_runtime_put_autosuspend(&vc4->v3d->pdev->dev);
		break;
	case DRM_VC4_PARAM_V3D_IDENT1:
		ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);
		if (ret < 0)
			return ret;
		args->value = V3D_READ(V3D_IDENT1);
		pm_runtime_mark_last_busy(&vc4->v3d->pdev->dev);
		pm_runtime_put_autosuspend(&vc4->v3d->pdev->dev);
		break;
	case DRM_VC4_PARAM_V3D_IDENT2:
		ret = pm_runtime_get_sync(&vc4->v3d->pdev->dev);
		if (ret < 0)
			return ret;
		args->value = V3D_READ(V3D_IDENT2);
		pm_runtime_mark_last_busy(&vc4->v3d->pdev->dev);
		pm_runtime_put_autosuspend(&vc4->v3d->pdev->dev);
		break;
	case DRM_VC4_PARAM_SUPPORTS_BRANCHES:
		args->value = true;
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", args->param);
		return -EINVAL;
	}

	return 0;
}

static void vc4_lastclose(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_fbdev_cma_restore_mode(vc4->fbdev);
}

static const struct file_operations vc4_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = vc4_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static const struct drm_ioctl_desc vc4_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VC4_SUBMIT_CL, vc4_submit_cl_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_WAIT_SEQNO, vc4_wait_seqno_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_WAIT_BO, vc4_wait_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_CREATE_BO, vc4_create_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_MMAP_BO, vc4_mmap_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_CREATE_SHADER_BO, vc4_create_shader_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_GET_HANG_STATE, vc4_get_hang_state_ioctl,
			  DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(VC4_GET_PARAM, vc4_get_param_ioctl, DRM_RENDER_ALLOW),
};

static struct drm_driver vc4_drm_driver = {
	.driver_features = (DRIVER_MODESET |
			    DRIVER_ATOMIC |
			    DRIVER_GEM |
			    DRIVER_HAVE_IRQ |
			    DRIVER_RENDER |
			    DRIVER_PRIME),
	.lastclose = vc4_lastclose,
	.irq_handler = vc4_irq,
	.irq_preinstall = vc4_irq_preinstall,
	.irq_postinstall = vc4_irq_postinstall,
	.irq_uninstall = vc4_irq_uninstall,

	.enable_vblank = vc4_enable_vblank,
	.disable_vblank = vc4_disable_vblank,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.get_scanout_position = vc4_crtc_get_scanoutpos,
	.get_vblank_timestamp = vc4_crtc_get_vblank_timestamp,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = vc4_debugfs_init,
	.debugfs_cleanup = vc4_debugfs_cleanup,
#endif

	.gem_create_object = vc4_create_object,
	.gem_free_object_unlocked = vc4_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_export = vc4_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = vc4_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = vc4_prime_mmap,

	.dumb_create = vc4_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,

	.ioctls = vc4_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(vc4_drm_ioctls),
	.fops = &vc4_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int compare_dev(struct device *dev, void *data)
{
	return dev == data;
}

static void vc4_match_add_drivers(struct device *dev,
				  struct component_match **match,
				  struct platform_driver *const *drivers,
				  int count)
{
	int i;

	for (i = 0; i < count; i++) {
		struct device_driver *drv = &drivers[i]->driver;
		struct device *p = NULL, *d;

		while ((d = bus_find_device(&platform_bus_type, p, drv,
					    (void *)platform_bus_type.match))) {
			put_device(p);
			component_match_add(dev, match, compare_dev, d);
			p = d;
		}
		put_device(p);
	}
}

static void vc4_kick_out_firmware_fb(void)
{
	struct apertures_struct *ap;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	/* Since VC4 is a UMA device, the simplefb node may have been
	 * located anywhere in memory.
	 */
	ap->ranges[0].base = 0;
	ap->ranges[0].size = ~0;

	drm_fb_helper_remove_conflicting_framebuffers(ap, "vc4drmfb", false);
	kfree(ap);
}

static int vc4_drm_bind(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm;
	struct vc4_dev *vc4;
	int ret = 0;

	dev->coherent_dma_mask = DMA_BIT_MASK(32);

	vc4 = devm_kzalloc(dev, sizeof(*vc4), GFP_KERNEL);
	if (!vc4)
		return -ENOMEM;

	drm = drm_dev_alloc(&vc4_drm_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);
	platform_set_drvdata(pdev, drm);
	vc4->dev = drm;
	drm->dev_private = vc4;

	vc4_bo_cache_init(drm);

	drm_mode_config_init(drm);

	vc4_gem_init(drm);

	ret = component_bind_all(dev, drm);
	if (ret)
		goto gem_destroy;

	vc4_kick_out_firmware_fb();

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto unbind_all;

	vc4_kms_load(drm);

	return 0;

unbind_all:
	component_unbind_all(dev, drm);
gem_destroy:
	vc4_gem_destroy(drm);
	drm_dev_unref(drm);
	vc4_bo_cache_destroy(drm);
	return ret;
}

static void vc4_drm_unbind(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = platform_get_drvdata(pdev);
	struct vc4_dev *vc4 = to_vc4_dev(drm);

	if (vc4->fbdev)
		drm_fbdev_cma_fini(vc4->fbdev);

	drm_mode_config_cleanup(drm);

	drm_put_dev(drm);
}

static const struct component_master_ops vc4_drm_ops = {
	.bind = vc4_drm_bind,
	.unbind = vc4_drm_unbind,
};

static struct platform_driver *const component_drivers[] = {
	&vc4_hdmi_driver,
	&vc4_dpi_driver,
	&vc4_hvs_driver,
	&vc4_crtc_driver,
	&vc4_v3d_driver,
};

static int vc4_platform_drm_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device *dev = &pdev->dev;

	vc4_match_add_drivers(dev, &match,
			      component_drivers, ARRAY_SIZE(component_drivers));

	return component_master_add_with_match(dev, &vc4_drm_ops, match);
}

static int vc4_platform_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &vc4_drm_ops);

	return 0;
}

static const struct of_device_id vc4_of_match[] = {
	{ .compatible = "brcm,bcm2835-vc4", },
	{},
};
MODULE_DEVICE_TABLE(of, vc4_of_match);

static struct platform_driver vc4_platform_driver = {
	.probe		= vc4_platform_drm_probe,
	.remove		= vc4_platform_drm_remove,
	.driver		= {
		.name	= "vc4-drm",
		.of_match_table = vc4_of_match,
	},
};

static int __init vc4_drm_register(void)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(component_drivers); i++) {
		ret = platform_driver_register(component_drivers[i]);
		if (ret) {
			while (--i >= 0)
				platform_driver_unregister(component_drivers[i]);
			return ret;
		}
	}
	return platform_driver_register(&vc4_platform_driver);
}

static void __exit vc4_drm_unregister(void)
{
	int i;

	for (i = ARRAY_SIZE(component_drivers) - 1; i >= 0; i--)
		platform_driver_unregister(component_drivers[i]);

	platform_driver_unregister(&vc4_platform_driver);
}

module_init(vc4_drm_register);
module_exit(vc4_drm_unregister);

MODULE_ALIAS("platform:vc4-drm");
MODULE_DESCRIPTION("Broadcom VC4 DRM Driver");
MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_LICENSE("GPL v2");
