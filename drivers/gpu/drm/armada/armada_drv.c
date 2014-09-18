/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include <drm/armada_drm.h>
#include "armada_ioctlP.h"

#ifdef CONFIG_DRM_ARMADA_TDA1998X
#include <drm/i2c/tda998x.h>
#include "armada_slave.h"

static struct tda998x_encoder_params params = {
	/* With 0x24, there is no translation between vp_out and int_vp
	FB	LCD out	Pins	VIP	Int Vp
	R:23:16	R:7:0	VPC7:0	7:0	7:0[R]
	G:15:8	G:15:8	VPB7:0	23:16	23:16[G]
	B:7:0	B:23:16	VPA7:0	15:8	15:8[B]
	*/
	.swap_a = 2,
	.swap_b = 3,
	.swap_c = 4,
	.swap_d = 5,
	.swap_e = 0,
	.swap_f = 1,
	.audio_cfg = BIT(2),
	.audio_frame[1] = 1,
	.audio_format = AFMT_SPDIF,
	.audio_sample_rate = 44100,
};

static const struct armada_drm_slave_config tda19988_config = {
	.i2c_adapter_id = 0,
	.crtcs = 1 << 0, /* Only LCD0 at the moment */
	.polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT,
	.interlace_allowed = true,
	.info = {
		.type = "tda998x",
		.addr = 0x70,
		.platform_data = &params,
	},
};
#endif

static bool is_componentized(struct device *dev)
{
	return dev->of_node || dev->platform_data;
}

static void armada_drm_unref_work(struct work_struct *work)
{
	struct armada_private *priv =
		container_of(work, struct armada_private, fb_unref_work);
	struct drm_framebuffer *fb;

	while (kfifo_get(&priv->fb_unref, &fb))
		drm_framebuffer_unreference(fb);
}

/* Must be called with dev->event_lock held */
void __armada_drm_queue_unref_work(struct drm_device *dev,
	struct drm_framebuffer *fb)
{
	struct armada_private *priv = dev->dev_private;

	WARN_ON(!kfifo_put(&priv->fb_unref, fb));
	schedule_work(&priv->fb_unref_work);
}

void armada_drm_queue_unref_work(struct drm_device *dev,
	struct drm_framebuffer *fb)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	__armada_drm_queue_unref_work(dev, fb);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int armada_drm_load(struct drm_device *dev, unsigned long flags)
{
	const struct platform_device_id *id;
	const struct armada_variant *variant;
	struct armada_private *priv;
	struct resource *res[ARRAY_SIZE(priv->dcrtc)];
	struct resource *mem = NULL;
	int ret, n, i;

	memset(res, 0, sizeof(res));

	for (n = i = 0; ; n++) {
		struct resource *r = platform_get_resource(dev->platformdev,
							   IORESOURCE_MEM, n);
		if (!r)
			break;

		/* Resources above 64K are graphics memory */
		if (resource_size(r) > SZ_64K)
			mem = r;
		else if (i < ARRAY_SIZE(priv->dcrtc))
			res[i++] = r;
		else
			return -EINVAL;
	}

	if (!mem)
		return -ENXIO;

	if (!devm_request_mem_region(dev->dev, mem->start,
			resource_size(mem), "armada-drm"))
		return -EBUSY;

	priv = devm_kzalloc(dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		DRM_ERROR("failed to allocate private\n");
		return -ENOMEM;
	}

	platform_set_drvdata(dev->platformdev, dev);
	dev->dev_private = priv;

	/* Get the implementation specific driver data. */
	id = platform_get_device_id(dev->platformdev);
	if (!id)
		return -ENXIO;

	variant = (const struct armada_variant *)id->driver_data;

	INIT_WORK(&priv->fb_unref_work, armada_drm_unref_work);
	INIT_KFIFO(priv->fb_unref);

	/* Mode setting support */
	drm_mode_config_init(dev);
	dev->mode_config.min_width = 320;
	dev->mode_config.min_height = 200;

	/*
	 * With vscale enabled, the maximum width is 1920 due to the
	 * 1920 by 3 lines RAM
	 */
	dev->mode_config.max_width = 1920;
	dev->mode_config.max_height = 2048;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.funcs = &armada_drm_mode_config_funcs;
	drm_mm_init(&priv->linear, mem->start, resource_size(mem));

	/* Create all LCD controllers */
	for (n = 0; n < ARRAY_SIZE(priv->dcrtc); n++) {
		int irq;

		if (!res[n])
			break;

		irq = platform_get_irq(dev->platformdev, n);
		if (irq < 0)
			goto err_kms;

		ret = armada_drm_crtc_create(dev, dev->dev, res[n], irq,
					     variant, NULL);
		if (ret)
			goto err_kms;
	}

	if (is_componentized(dev->dev)) {
		ret = component_bind_all(dev->dev, dev);
		if (ret)
			goto err_kms;
	} else {
#ifdef CONFIG_DRM_ARMADA_TDA1998X
		ret = armada_drm_connector_slave_create(dev, &tda19988_config);
		if (ret)
			goto err_kms;
#endif
	}

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret)
		goto err_comp;

	dev->vblank_disable_allowed = 1;

	ret = armada_fbdev_init(dev);
	if (ret)
		goto err_comp;

	drm_kms_helper_poll_init(dev);

	return 0;

 err_comp:
	if (is_componentized(dev->dev))
		component_unbind_all(dev->dev, dev);
 err_kms:
	drm_mode_config_cleanup(dev);
	drm_mm_takedown(&priv->linear);
	flush_work(&priv->fb_unref_work);

	return ret;
}

static int armada_drm_unload(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;

	drm_kms_helper_poll_fini(dev);
	armada_fbdev_fini(dev);

	if (is_componentized(dev->dev))
		component_unbind_all(dev->dev, dev);

	drm_mode_config_cleanup(dev);
	drm_mm_takedown(&priv->linear);
	flush_work(&priv->fb_unref_work);
	dev->dev_private = NULL;

	return 0;
}

void armada_drm_vbl_event_add(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	if (list_empty(&evt->node)) {
		list_add_tail(&evt->node, &dcrtc->vbl_list);

		drm_vblank_get(dcrtc->crtc.dev, dcrtc->num);
	}
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

void armada_drm_vbl_event_remove(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	if (!list_empty(&evt->node)) {
		list_del_init(&evt->node);
		drm_vblank_put(dcrtc->crtc.dev, dcrtc->num);
	}
}

void armada_drm_vbl_event_remove_unlocked(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_vbl_event_remove(dcrtc, evt);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

/* These are called under the vbl_lock. */
static int armada_drm_enable_vblank(struct drm_device *dev, int crtc)
{
	struct armada_private *priv = dev->dev_private;
	armada_drm_crtc_enable_irq(priv->dcrtc[crtc], VSYNC_IRQ_ENA);
	return 0;
}

static void armada_drm_disable_vblank(struct drm_device *dev, int crtc)
{
	struct armada_private *priv = dev->dev_private;
	armada_drm_crtc_disable_irq(priv->dcrtc[crtc], VSYNC_IRQ_ENA);
}

static struct drm_ioctl_desc armada_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_CREATE, armada_gem_create_ioctl,
		DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_MMAP, armada_gem_mmap_ioctl,
		DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_PWRITE, armada_gem_pwrite_ioctl,
		DRM_UNLOCKED),
};

static void armada_drm_lastclose(struct drm_device *dev)
{
	armada_fbdev_lastclose(dev);
}

static const struct file_operations armada_drm_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.read			= drm_read,
	.poll			= drm_poll,
	.unlocked_ioctl		= drm_ioctl,
	.mmap			= drm_gem_mmap,
	.open			= drm_open,
	.release		= drm_release,
};

static struct drm_driver armada_drm_driver = {
	.load			= armada_drm_load,
	.open			= NULL,
	.preclose		= NULL,
	.postclose		= NULL,
	.lastclose		= armada_drm_lastclose,
	.unload			= armada_drm_unload,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= armada_drm_enable_vblank,
	.disable_vblank		= armada_drm_disable_vblank,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= armada_drm_debugfs_init,
	.debugfs_cleanup	= armada_drm_debugfs_cleanup,
#endif
	.gem_free_object	= armada_gem_free_object,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= armada_gem_prime_export,
	.gem_prime_import	= armada_gem_prime_import,
	.dumb_create		= armada_gem_dumb_create,
	.dumb_map_offset	= armada_gem_dumb_map_offset,
	.dumb_destroy		= armada_gem_dumb_destroy,
	.gem_vm_ops		= &armada_gem_vm_ops,
	.major			= 1,
	.minor			= 0,
	.name			= "armada-drm",
	.desc			= "Armada SoC DRM",
	.date			= "20120730",
	.driver_features	= DRIVER_GEM | DRIVER_MODESET |
				  DRIVER_PRIME,
	.ioctls			= armada_ioctls,
	.fops			= &armada_drm_fops,
};

static int armada_drm_bind(struct device *dev)
{
	return drm_platform_init(&armada_drm_driver, to_platform_device(dev));
}

static void armada_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int compare_dev_name(struct device *dev, void *data)
{
	const char *name = data;
	return !strcmp(dev_name(dev), name);
}

static void armada_add_endpoints(struct device *dev,
	struct component_match **match, struct device_node *port)
{
	struct device_node *ep, *remote;

	for_each_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote || !of_device_is_available(remote)) {
			of_node_put(remote);
			continue;
		} else if (!of_device_is_available(remote->parent)) {
			dev_warn(dev, "parent device of %s is not available\n",
				 remote->full_name);
			of_node_put(remote);
			continue;
		}

		component_match_add(dev, match, compare_of, remote);
		of_node_put(remote);
	}
}

static int armada_drm_find_components(struct device *dev,
	struct component_match **match)
{
	struct device_node *port;
	int i;

	if (dev->of_node) {
		struct device_node *np = dev->of_node;

		for (i = 0; ; i++) {
			port = of_parse_phandle(np, "ports", i);
			if (!port)
				break;

			component_match_add(dev, match, compare_of, port);
			of_node_put(port);
		}

		if (i == 0) {
			dev_err(dev, "missing 'ports' property\n");
			return -ENODEV;
		}

		for (i = 0; ; i++) {
			port = of_parse_phandle(np, "ports", i);
			if (!port)
				break;

			armada_add_endpoints(dev, match, port);
			of_node_put(port);
		}
	} else if (dev->platform_data) {
		char **devices = dev->platform_data;
		struct device *d;

		for (i = 0; devices[i]; i++)
			component_match_add(dev, match, compare_dev_name,
					    devices[i]);

		if (i == 0) {
			dev_err(dev, "missing 'ports' property\n");
			return -ENODEV;
		}

		for (i = 0; devices[i]; i++) {
			d = bus_find_device_by_name(&platform_bus_type, NULL,
					devices[i]);
			if (d && d->of_node) {
				for_each_child_of_node(d->of_node, port)
					armada_add_endpoints(dev, match, port);
			}
			put_device(d);
		}
	}

	return 0;
}

static const struct component_master_ops armada_master_ops = {
	.bind = armada_drm_bind,
	.unbind = armada_drm_unbind,
};

static int armada_drm_probe(struct platform_device *pdev)
{
	if (is_componentized(&pdev->dev)) {
		struct component_match *match = NULL;
		int ret;

		ret = armada_drm_find_components(&pdev->dev, &match);
		if (ret < 0)
			return ret;

		return component_master_add_with_match(&pdev->dev,
				&armada_master_ops, match);
	} else {
		return drm_platform_init(&armada_drm_driver, pdev);
	}
}

static int armada_drm_remove(struct platform_device *pdev)
{
	if (is_componentized(&pdev->dev))
		component_master_del(&pdev->dev, &armada_master_ops);
	else
		drm_put_dev(platform_get_drvdata(pdev));
	return 0;
}

static const struct platform_device_id armada_drm_platform_ids[] = {
	{
		.name		= "armada-drm",
		.driver_data	= (unsigned long)&armada510_ops,
	}, {
		.name		= "armada-510-drm",
		.driver_data	= (unsigned long)&armada510_ops,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, armada_drm_platform_ids);

static struct platform_driver armada_drm_platform_driver = {
	.probe	= armada_drm_probe,
	.remove	= armada_drm_remove,
	.driver	= {
		.name	= "armada-drm",
		.owner	= THIS_MODULE,
	},
	.id_table = armada_drm_platform_ids,
};

static int __init armada_drm_init(void)
{
	int ret;

	armada_drm_driver.num_ioctls = ARRAY_SIZE(armada_ioctls);

	ret = platform_driver_register(&armada_lcd_platform_driver);
	if (ret)
		return ret;
	ret = platform_driver_register(&armada_drm_platform_driver);
	if (ret)
		platform_driver_unregister(&armada_lcd_platform_driver);
	return ret;
}
module_init(armada_drm_init);

static void __exit armada_drm_exit(void)
{
	platform_driver_unregister(&armada_drm_platform_driver);
	platform_driver_unregister(&armada_lcd_platform_driver);
}
module_exit(armada_drm_exit);

MODULE_AUTHOR("Russell King <rmk+kernel@arm.linux.org.uk>");
MODULE_DESCRIPTION("Armada DRM Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:armada-drm");
