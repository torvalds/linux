// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2015 Broadcom
 * Copyright (C) 2013 Red Hat
 */

/**
 * DOC: Broadcom VC4 Graphics Driver
 *
 * The Broadcom VideoCore 4 (present in the Raspberry Pi) contains a
 * OpenGL ES 2.0-compatible 3D engine called V3D, and a highly
 * configurable display output pipeline that supports HDMI, DSI, DPI,
 * and Composite TV output.
 *
 * The 3D engine also has an interface for submitting arbitrary
 * compute shader-style jobs using the same shader processor as is
 * used for vertex and fragment shaders in GLES 2.0.  However, given
 * that the hardware isn't able to expose any standard interfaces like
 * OpenGL compute shaders or OpenCL, it isn't supported by this
 * driver.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_vblank.h>

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

	if (!vc4->v3d)
		return -ENODEV;

	switch (args->param) {
	case DRM_VC4_PARAM_V3D_IDENT0:
		ret = vc4_v3d_pm_get(vc4);
		if (ret)
			return ret;
		args->value = V3D_READ(V3D_IDENT0);
		vc4_v3d_pm_put(vc4);
		break;
	case DRM_VC4_PARAM_V3D_IDENT1:
		ret = vc4_v3d_pm_get(vc4);
		if (ret)
			return ret;
		args->value = V3D_READ(V3D_IDENT1);
		vc4_v3d_pm_put(vc4);
		break;
	case DRM_VC4_PARAM_V3D_IDENT2:
		ret = vc4_v3d_pm_get(vc4);
		if (ret)
			return ret;
		args->value = V3D_READ(V3D_IDENT2);
		vc4_v3d_pm_put(vc4);
		break;
	case DRM_VC4_PARAM_SUPPORTS_BRANCHES:
	case DRM_VC4_PARAM_SUPPORTS_ETC1:
	case DRM_VC4_PARAM_SUPPORTS_THREADED_FS:
	case DRM_VC4_PARAM_SUPPORTS_FIXED_RCL_ORDER:
	case DRM_VC4_PARAM_SUPPORTS_MADVISE:
	case DRM_VC4_PARAM_SUPPORTS_PERFMON:
		args->value = true;
		break;
	default:
		DRM_DEBUG("Unknown parameter %d\n", args->param);
		return -EINVAL;
	}

	return 0;
}

static int vc4_open(struct drm_device *dev, struct drm_file *file)
{
	struct vc4_file *vc4file;

	vc4file = kzalloc(sizeof(*vc4file), GFP_KERNEL);
	if (!vc4file)
		return -ENOMEM;

	vc4_perfmon_open_file(vc4file);
	file->driver_priv = vc4file;
	return 0;
}

static void vc4_close(struct drm_device *dev, struct drm_file *file)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_file *vc4file = file->driver_priv;

	if (vc4file->bin_bo_used)
		vc4_v3d_bin_bo_put(vc4);

	vc4_perfmon_close_file(vc4file);
	kfree(vc4file);
}

static const struct vm_operations_struct vc4_vm_ops = {
	.fault = vc4_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations vc4_drm_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = vc4_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.compat_ioctl = drm_compat_ioctl,
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
	DRM_IOCTL_DEF_DRV(VC4_SET_TILING, vc4_set_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_GET_TILING, vc4_get_tiling_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_LABEL_BO, vc4_label_bo_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_GEM_MADVISE, vc4_gem_madvise_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_PERFMON_CREATE, vc4_perfmon_create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_PERFMON_DESTROY, vc4_perfmon_destroy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VC4_PERFMON_GET_VALUES, vc4_perfmon_get_values_ioctl, DRM_RENDER_ALLOW),
};

static struct drm_driver vc4_drm_driver = {
	.driver_features = (DRIVER_MODESET |
			    DRIVER_ATOMIC |
			    DRIVER_GEM |
			    DRIVER_RENDER |
			    DRIVER_SYNCOBJ),
	.open = vc4_open,
	.postclose = vc4_close,
	.irq_handler = vc4_irq,
	.irq_preinstall = vc4_irq_preinstall,
	.irq_postinstall = vc4_irq_postinstall,
	.irq_uninstall = vc4_irq_uninstall,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = vc4_debugfs_init,
#endif

	.gem_create_object = vc4_create_object,
	.gem_free_object_unlocked = vc4_free_object,
	.gem_vm_ops = &vc4_vm_ops,

	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = vc4_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = vc4_prime_import_sg_table,
	.gem_prime_vmap = vc4_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = vc4_prime_mmap,

	.dumb_create = vc4_dumb_create,

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

		while ((d = platform_find_device_by_driver(p, drv))) {
			put_device(p);
			component_match_add(dev, match, compare_dev, d);
			p = d;
		}
		put_device(p);
	}
}

static int vc4_drm_bind(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm;
	struct vc4_dev *vc4;
	struct device_node *node;
	struct drm_crtc *crtc;
	int ret = 0;

	dev->coherent_dma_mask = DMA_BIT_MASK(32);

	/* If VC4 V3D is missing, don't advertise render nodes. */
	node = of_find_matching_node_and_match(NULL, vc4_v3d_dt_match, NULL);
	if (!node || !of_device_is_available(node))
		vc4_drm_driver.driver_features &= ~DRIVER_RENDER;
	of_node_put(node);

	vc4 = devm_drm_dev_alloc(dev, &vc4_drm_driver, struct vc4_dev, base);
	if (IS_ERR(vc4))
		return PTR_ERR(vc4);

	drm = &vc4->base;
	platform_set_drvdata(pdev, drm);
	INIT_LIST_HEAD(&vc4->debugfs_list);

	mutex_init(&vc4->bin_bo_lock);

	ret = vc4_bo_cache_init(drm);
	if (ret)
		return ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	ret = vc4_gem_init(drm);
	if (ret)
		return ret;

	ret = component_bind_all(dev, drm);
	if (ret)
		return ret;

	ret = vc4_plane_create_additional_planes(drm);
	if (ret)
		goto unbind_all;

	drm_fb_helper_remove_conflicting_framebuffers(NULL, "vc4drmfb", false);

	ret = vc4_kms_load(drm);
	if (ret < 0)
		goto unbind_all;

	drm_for_each_crtc(crtc, drm)
		vc4_crtc_disable_at_boot(crtc);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto unbind_all;

	drm_fbdev_generic_setup(drm, 16);

	return 0;

unbind_all:
	component_unbind_all(dev, drm);

	return ret;
}

static void vc4_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);

	drm_atomic_helper_shutdown(drm);
}

static const struct component_master_ops vc4_drm_ops = {
	.bind = vc4_drm_bind,
	.unbind = vc4_drm_unbind,
};

static struct platform_driver *const component_drivers[] = {
	&vc4_hdmi_driver,
	&vc4_vec_driver,
	&vc4_dpi_driver,
	&vc4_dsi_driver,
	&vc4_hvs_driver,
	&vc4_txp_driver,
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
	{ .compatible = "brcm,bcm2711-vc5", },
	{ .compatible = "brcm,bcm2835-vc4", },
	{ .compatible = "brcm,cygnus-vc4", },
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
	int ret;

	ret = platform_register_drivers(component_drivers,
					ARRAY_SIZE(component_drivers));
	if (ret)
		return ret;

	return platform_driver_register(&vc4_platform_driver);
}

static void __exit vc4_drm_unregister(void)
{
	platform_unregister_drivers(component_drivers,
				    ARRAY_SIZE(component_drivers));
	platform_driver_unregister(&vc4_platform_driver);
}

module_init(vc4_drm_register);
module_exit(vc4_drm_unregister);

MODULE_ALIAS("platform:vc4-drm");
MODULE_DESCRIPTION("Broadcom VC4 DRM Driver");
MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_LICENSE("GPL v2");
