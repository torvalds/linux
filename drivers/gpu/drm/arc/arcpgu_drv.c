/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_atomic_helper.h>
#include <linux/of_reserved_mem.h>

#include "arcpgu.h"
#include "arcpgu_regs.h"

static void arcpgu_fb_output_poll_changed(struct drm_device *dev)
{
	struct arcpgu_drm_private *arcpgu = dev->dev_private;

	drm_fbdev_cma_hotplug_event(arcpgu->fbdev);
}

static struct drm_mode_config_funcs arcpgu_drm_modecfg_funcs = {
	.fb_create  = drm_fb_cma_create,
	.output_poll_changed = arcpgu_fb_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void arcpgu_setup_mode_config(struct drm_device *drm)
{
	drm_mode_config_init(drm);
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 1920;
	drm->mode_config.max_height = 1080;
	drm->mode_config.funcs = &arcpgu_drm_modecfg_funcs;
}

static int arcpgu_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	return 0;
}

static const struct file_operations arcpgu_drm_ops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = no_llseek,
	.mmap = arcpgu_gem_mmap,
};

static void arcpgu_lastclose(struct drm_device *drm)
{
	struct arcpgu_drm_private *arcpgu = drm->dev_private;

	drm_fbdev_cma_restore_mode(arcpgu->fbdev);
}

static int arcpgu_load(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct arcpgu_drm_private *arcpgu;
	struct device_node *encoder_node;
	struct resource *res;
	int ret;

	arcpgu = devm_kzalloc(&pdev->dev, sizeof(*arcpgu), GFP_KERNEL);
	if (arcpgu == NULL)
		return -ENOMEM;

	drm->dev_private = arcpgu;

	arcpgu->clk = devm_clk_get(drm->dev, "pxlclk");
	if (IS_ERR(arcpgu->clk))
		return PTR_ERR(arcpgu->clk);

	arcpgu_setup_mode_config(drm);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	arcpgu->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(arcpgu->regs))
		return PTR_ERR(arcpgu->regs);

	dev_info(drm->dev, "arc_pgu ID: 0x%x\n",
		 arc_pgu_read(arcpgu, ARCPGU_REG_ID));

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(drm->dev);
	if (ret && ret != -ENODEV)
		return ret;

	if (dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32)))
		return -ENODEV;

	if (arc_pgu_setup_crtc(drm) < 0)
		return -ENODEV;

	/* find the encoder node and initialize it */
	encoder_node = of_parse_phandle(drm->dev->of_node, "encoder-slave", 0);
	if (encoder_node) {
		ret = arcpgu_drm_hdmi_init(drm, encoder_node);
		of_node_put(encoder_node);
		if (ret < 0)
			return ret;
	} else {
		ret = arcpgu_drm_sim_init(drm, NULL);
		if (ret < 0)
			return ret;
	}

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	arcpgu->fbdev = drm_fbdev_cma_init(drm, 16,
					   drm->mode_config.num_connector);
	if (IS_ERR(arcpgu->fbdev)) {
		ret = PTR_ERR(arcpgu->fbdev);
		arcpgu->fbdev = NULL;
		return -ENODEV;
	}

	platform_set_drvdata(pdev, arcpgu);
	return 0;
}

static int arcpgu_unload(struct drm_device *drm)
{
	struct arcpgu_drm_private *arcpgu = drm->dev_private;

	if (arcpgu->fbdev) {
		drm_fbdev_cma_fini(arcpgu->fbdev);
		arcpgu->fbdev = NULL;
	}
	drm_kms_helper_poll_fini(drm);
	drm_vblank_cleanup(drm);
	drm_mode_config_cleanup(drm);

	return 0;
}

static struct drm_driver arcpgu_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.lastclose = arcpgu_lastclose,
	.name = "drm-arcpgu",
	.desc = "ARC PGU Controller",
	.date = "20160219",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
	.fops = &arcpgu_drm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.dumb_map_offset = drm_gem_cma_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
};

static int arcpgu_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&arcpgu_drm_driver, &pdev->dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	ret = arcpgu_load(drm);
	if (ret)
		goto err_unref;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unload;

	return 0;

err_unload:
	arcpgu_unload(drm);

err_unref:
	drm_dev_unref(drm);

	return ret;
}

static int arcpgu_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	arcpgu_unload(drm);
	drm_dev_unref(drm);

	return 0;
}

static const struct of_device_id arcpgu_of_table[] = {
	{.compatible = "snps,arcpgu"},
	{}
};

MODULE_DEVICE_TABLE(of, arcpgu_of_table);

static struct platform_driver arcpgu_platform_driver = {
	.probe = arcpgu_probe,
	.remove = arcpgu_remove,
	.driver = {
		   .name = "arcpgu",
		   .of_match_table = arcpgu_of_table,
		   },
};

module_platform_driver(arcpgu_platform_driver);

MODULE_AUTHOR("Carlos Palminha <palminha@synopsys.com>");
MODULE_DESCRIPTION("ARC PGU DRM driver");
MODULE_LICENSE("GPL");
