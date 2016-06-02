/*
 * Freescale i.MX drm driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/component.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_of.h>
#include <video/imx-ipu-v3.h>

#include "imx-drm.h"

#define MAX_CRTC	4

struct imx_drm_component {
	struct device_node *of_node;
	struct list_head list;
};

struct imx_drm_device {
	struct drm_device			*drm;
	struct imx_drm_crtc			*crtc[MAX_CRTC];
	unsigned int				pipes;
	struct drm_fbdev_cma			*fbhelper;
};

struct imx_drm_crtc {
	struct drm_crtc				*crtc;
	struct imx_drm_crtc_helper_funcs	imx_drm_helper_funcs;
};

#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
static int legacyfb_depth = 16;
module_param(legacyfb_depth, int, 0444);
#endif

unsigned int imx_drm_crtc_id(struct imx_drm_crtc *crtc)
{
	return drm_crtc_index(crtc->crtc);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_id);

static void imx_drm_driver_lastclose(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	drm_fbdev_cma_restore_mode(imxdrm->fbhelper);
}

static int imx_drm_driver_unload(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	drm_kms_helper_poll_fini(drm);

	if (imxdrm->fbhelper)
		drm_fbdev_cma_fini(imxdrm->fbhelper);

	component_unbind_all(drm->dev, drm);

	drm_vblank_cleanup(drm);
	drm_mode_config_cleanup(drm);

	platform_set_drvdata(drm->platformdev, NULL);

	return 0;
}

static struct imx_drm_crtc *imx_drm_find_crtc(struct drm_crtc *crtc)
{
	struct imx_drm_device *imxdrm = crtc->dev->dev_private;
	unsigned i;

	for (i = 0; i < MAX_CRTC; i++)
		if (imxdrm->crtc[i] && imxdrm->crtc[i]->crtc == crtc)
			return imxdrm->crtc[i];

	return NULL;
}

int imx_drm_set_bus_format_pins(struct drm_encoder *encoder, u32 bus_format,
		int hsync_pin, int vsync_pin)
{
	struct imx_drm_crtc_helper_funcs *helper;
	struct imx_drm_crtc *imx_crtc;

	imx_crtc = imx_drm_find_crtc(encoder->crtc);
	if (!imx_crtc)
		return -EINVAL;

	helper = &imx_crtc->imx_drm_helper_funcs;
	if (helper->set_interface_pix_fmt)
		return helper->set_interface_pix_fmt(encoder->crtc,
					bus_format, hsync_pin, vsync_pin);
	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_set_bus_format_pins);

int imx_drm_set_bus_format(struct drm_encoder *encoder, u32 bus_format)
{
	return imx_drm_set_bus_format_pins(encoder, bus_format, 2, 3);
}
EXPORT_SYMBOL_GPL(imx_drm_set_bus_format);

int imx_drm_crtc_vblank_get(struct imx_drm_crtc *imx_drm_crtc)
{
	return drm_crtc_vblank_get(imx_drm_crtc->crtc);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_get);

void imx_drm_crtc_vblank_put(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_crtc_vblank_put(imx_drm_crtc->crtc);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_put);

void imx_drm_handle_vblank(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_crtc_handle_vblank(imx_drm_crtc->crtc);
}
EXPORT_SYMBOL_GPL(imx_drm_handle_vblank);

static int imx_drm_enable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[pipe];
	int ret;

	if (!imx_drm_crtc)
		return -EINVAL;

	if (!imx_drm_crtc->imx_drm_helper_funcs.enable_vblank)
		return -ENOSYS;

	ret = imx_drm_crtc->imx_drm_helper_funcs.enable_vblank(
			imx_drm_crtc->crtc);

	return ret;
}

static void imx_drm_disable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[pipe];

	if (!imx_drm_crtc)
		return;

	if (!imx_drm_crtc->imx_drm_helper_funcs.disable_vblank)
		return;

	imx_drm_crtc->imx_drm_helper_funcs.disable_vblank(imx_drm_crtc->crtc);
}

static const struct file_operations imx_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

void imx_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}
EXPORT_SYMBOL_GPL(imx_drm_connector_destroy);

void imx_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_destroy);

static void imx_drm_output_poll_changed(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	drm_fbdev_cma_hotplug_event(imxdrm->fbhelper);
}

static const struct drm_mode_config_funcs imx_drm_mode_config_funcs = {
	.fb_create = drm_fb_cma_create,
	.output_poll_changed = imx_drm_output_poll_changed,
};

/*
 * Main DRM initialisation. This binds, initialises and registers
 * with DRM the subcomponents of the driver.
 */
static int imx_drm_driver_load(struct drm_device *drm, unsigned long flags)
{
	struct imx_drm_device *imxdrm;
	struct drm_connector *connector;
	int ret;

	imxdrm = devm_kzalloc(drm->dev, sizeof(*imxdrm), GFP_KERNEL);
	if (!imxdrm)
		return -ENOMEM;

	imxdrm->drm = drm;

	drm->dev_private = imxdrm;

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *      just specific driver own one instead because
	 *      drm framework supports only one irq handler and
	 *      drivers can well take care of their interrupts
	 */
	drm->irq_enabled = true;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	drm->mode_config.min_width = 64;
	drm->mode_config.min_height = 64;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &imx_drm_mode_config_funcs;

	drm_mode_config_init(drm);

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret)
		goto err_kms;

	platform_set_drvdata(drm->platformdev, drm);

	/* Now try and bind all our sub-components */
	ret = component_bind_all(drm->dev, drm);
	if (ret)
		goto err_vblank;

	/*
	 * All components are now added, we can publish the connector sysfs
	 * entries to userspace.  This will generate hotplug events and so
	 * userspace will expect to be able to access DRM at this point.
	 */
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		ret = drm_connector_register(connector);
		if (ret) {
			dev_err(drm->dev,
				"[CONNECTOR:%d:%s] drm_connector_register failed: %d\n",
				connector->base.id,
				connector->name, ret);
			goto err_unbind;
		}
	}

	/*
	 * All components are now initialised, so setup the fb helper.
	 * The fb helper takes copies of key hardware information, so the
	 * crtcs/connectors/encoders must not change after this point.
	 */
#if IS_ENABLED(CONFIG_DRM_FBDEV_EMULATION)
	if (legacyfb_depth != 16 && legacyfb_depth != 32) {
		dev_warn(drm->dev, "Invalid legacyfb_depth.  Defaulting to 16bpp\n");
		legacyfb_depth = 16;
	}
	drm_helper_disable_unused_functions(drm);
	imxdrm->fbhelper = drm_fbdev_cma_init(drm, legacyfb_depth,
				drm->mode_config.num_crtc, MAX_CRTC);
	if (IS_ERR(imxdrm->fbhelper)) {
		ret = PTR_ERR(imxdrm->fbhelper);
		imxdrm->fbhelper = NULL;
		goto err_unbind;
	}
#endif

	drm_kms_helper_poll_init(drm);

	return 0;

err_unbind:
	component_unbind_all(drm->dev, drm);
err_vblank:
	drm_vblank_cleanup(drm);
err_kms:
	drm_mode_config_cleanup(drm);

	return ret;
}

/*
 * imx_drm_add_crtc - add a new crtc
 */
int imx_drm_add_crtc(struct drm_device *drm, struct drm_crtc *crtc,
		struct imx_drm_crtc **new_crtc, struct drm_plane *primary_plane,
		const struct imx_drm_crtc_helper_funcs *imx_drm_helper_funcs,
		struct device_node *port)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc;

	/*
	 * The vblank arrays are dimensioned by MAX_CRTC - we can't
	 * pass IDs greater than this to those functions.
	 */
	if (imxdrm->pipes >= MAX_CRTC)
		return -EINVAL;

	if (imxdrm->drm->open_count)
		return -EBUSY;

	imx_drm_crtc = kzalloc(sizeof(*imx_drm_crtc), GFP_KERNEL);
	if (!imx_drm_crtc)
		return -ENOMEM;

	imx_drm_crtc->imx_drm_helper_funcs = *imx_drm_helper_funcs;
	imx_drm_crtc->crtc = crtc;

	crtc->port = port;

	imxdrm->crtc[imxdrm->pipes++] = imx_drm_crtc;

	*new_crtc = imx_drm_crtc;

	drm_crtc_helper_add(crtc,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_helper_funcs);

	drm_crtc_init_with_planes(drm, crtc, primary_plane, NULL,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_funcs, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_add_crtc);

/*
 * imx_drm_remove_crtc - remove a crtc
 */
int imx_drm_remove_crtc(struct imx_drm_crtc *imx_drm_crtc)
{
	struct imx_drm_device *imxdrm = imx_drm_crtc->crtc->dev->dev_private;
	unsigned int pipe = drm_crtc_index(imx_drm_crtc->crtc);

	drm_crtc_cleanup(imx_drm_crtc->crtc);

	imxdrm->crtc[pipe] = NULL;

	kfree(imx_drm_crtc);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_remove_crtc);

int imx_drm_encoder_parse_of(struct drm_device *drm,
	struct drm_encoder *encoder, struct device_node *np)
{
	uint32_t crtc_mask = drm_of_find_possible_crtcs(drm, np);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (crtc_mask == 0)
		return -EPROBE_DEFER;

	encoder->possible_crtcs = crtc_mask;

	/* FIXME: this is the mask of outputs which can clone this output. */
	encoder->possible_clones = ~0;

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_parse_of);

static const struct drm_ioctl_desc imx_drm_ioctls[] = {
	/* none so far */
};

static struct drm_driver imx_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load			= imx_drm_driver_load,
	.unload			= imx_drm_driver_unload,
	.lastclose		= imx_drm_driver_lastclose,
	.set_busid		= drm_platform_set_busid,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,

	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.enable_vblank		= imx_drm_enable_vblank,
	.disable_vblank		= imx_drm_disable_vblank,
	.ioctls			= imx_drm_ioctls,
	.num_ioctls		= ARRAY_SIZE(imx_drm_ioctls),
	.fops			= &imx_drm_driver_fops,
	.name			= "imx-drm",
	.desc			= "i.MX DRM graphics",
	.date			= "20120507",
	.major			= 1,
	.minor			= 0,
	.patchlevel		= 0,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	/* Special case for DI, dev->of_node may not be set yet */
	if (strcmp(dev->driver->name, "imx-ipuv3-crtc") == 0) {
		struct ipu_client_platformdata *pdata = dev->platform_data;

		return pdata->of_node == np;
	}

	/* Special case for LDB, one device for two channels */
	if (of_node_cmp(np->name, "lvds-channel") == 0) {
		np = of_get_parent(np);
		of_node_put(np);
	}

	return dev->of_node == np;
}

static int imx_drm_bind(struct device *dev)
{
	return drm_platform_init(&imx_drm_driver, to_platform_device(dev));
}

static void imx_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops imx_drm_ops = {
	.bind = imx_drm_bind,
	.unbind = imx_drm_unbind,
};

static int imx_drm_platform_probe(struct platform_device *pdev)
{
	int ret = drm_of_component_probe(&pdev->dev, compare_of, &imx_drm_ops);

	if (!ret)
		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	return ret;
}

static int imx_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &imx_drm_ops);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int imx_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	/* The drm_dev is NULL before .load hook is called */
	if (drm_dev == NULL)
		return 0;

	drm_kms_helper_poll_disable(drm_dev);

	return 0;
}

static int imx_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	if (drm_dev == NULL)
		return 0;

	drm_helper_resume_force_mode(drm_dev);
	drm_kms_helper_poll_enable(drm_dev);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(imx_drm_pm_ops, imx_drm_suspend, imx_drm_resume);

static const struct of_device_id imx_drm_dt_ids[] = {
	{ .compatible = "fsl,imx-display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx_drm_dt_ids);

static struct platform_driver imx_drm_pdrv = {
	.probe		= imx_drm_platform_probe,
	.remove		= imx_drm_platform_remove,
	.driver		= {
		.name	= "imx-drm",
		.pm	= &imx_drm_pm_ops,
		.of_match_table = imx_drm_dt_ids,
	},
};
module_platform_driver(imx_drm_pdrv);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX drm driver core");
MODULE_LICENSE("GPL");
