/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/component.h>
#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_helper.h>

#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_framebuffer.h"
#include "sun4i_layer.h"
#include "sun4i_tcon.h"

static int sun4i_drv_enable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Enabling VBLANK on pipe %d\n", pipe);

	sun4i_tcon_enable_vblank(tcon, true);

	return 0;
}

static void sun4i_drv_disable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Disabling VBLANK on pipe %d\n", pipe);

	sun4i_tcon_enable_vblank(tcon, false);
}

static const struct file_operations sun4i_drv_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver sun4i_drv_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_ATOMIC,

	/* Generic Operations */
	.fops			= &sun4i_drv_fops,
	.name			= "sun4i-drm",
	.desc			= "Allwinner sun4i Display Engine",
	.date			= "20150629",
	.major			= 1,
	.minor			= 0,

	/* GEM Operations */
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,

	/* PRIME Operations */
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,

	/* Frame Buffer Operations */

	/* VBlank Operations */
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.enable_vblank		= sun4i_drv_enable_vblank,
	.disable_vblank		= sun4i_drv_disable_vblank,
};

static void sun4i_remove_framebuffers(void)
{
	struct apertures_struct *ap;

	ap = alloc_apertures(1);
	if (!ap)
		return;

	/* The framebuffer can be located anywhere in RAM */
	ap->ranges[0].base = 0;
	ap->ranges[0].size = ~0;

	drm_fb_helper_remove_conflicting_framebuffers(ap, "sun4i-drm-fb", false);
	kfree(ap);
}

static int sun4i_drv_bind(struct device *dev)
{
	struct drm_device *drm;
	struct sun4i_drv *drv;
	int ret;

	drm = drm_dev_alloc(&sun4i_drv_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv) {
		ret = -ENOMEM;
		goto free_drm;
	}
	drm->dev_private = drv;

	drm_vblank_init(drm, 1);
	drm_mode_config_init(drm);

	ret = component_bind_all(drm->dev, drm);
	if (ret) {
		dev_err(drm->dev, "Couldn't bind all pipelines components\n");
		goto free_drm;
	}

	/* Create our layers */
	drv->layers = sun4i_layers_init(drm);
	if (IS_ERR(drv->layers)) {
		dev_err(drm->dev, "Couldn't create the planes\n");
		ret = PTR_ERR(drv->layers);
		goto free_drm;
	}

	/* Create our CRTC */
	drv->crtc = sun4i_crtc_init(drm);
	if (!drv->crtc) {
		dev_err(drm->dev, "Couldn't create the CRTC\n");
		ret = -EINVAL;
		goto free_drm;
	}
	drm->irq_enabled = true;

	/* Remove early framebuffers (ie. simplefb) */
	sun4i_remove_framebuffers();

	/* Create our framebuffer */
	drv->fbdev = sun4i_framebuffer_init(drm);
	if (IS_ERR(drv->fbdev)) {
		dev_err(drm->dev, "Couldn't create our framebuffer\n");
		ret = PTR_ERR(drv->fbdev);
		goto free_drm;
	}

	/* Enable connectors polling */
	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto free_drm;

	return 0;

free_drm:
	drm_dev_unref(drm);
	return ret;
}

static void sun4i_drv_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	sun4i_framebuffer_free(drm);
	drm_vblank_cleanup(drm);
	drm_dev_unref(drm);
}

static const struct component_master_ops sun4i_drv_master_ops = {
	.bind	= sun4i_drv_bind,
	.unbind	= sun4i_drv_unbind,
};

static bool sun4i_drv_node_is_frontend(struct device_node *node)
{
	return of_device_is_compatible(node, "allwinner,sun5i-a13-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun8i-a33-display-frontend");
}

static bool sun4i_drv_node_is_tcon(struct device_node *node)
{
	return of_device_is_compatible(node, "allwinner,sun5i-a13-tcon") ||
		of_device_is_compatible(node, "allwinner,sun8i-a33-tcon");
}

static int compare_of(struct device *dev, void *data)
{
	DRM_DEBUG_DRIVER("Comparing of node %s with %s\n",
			 of_node_full_name(dev->of_node),
			 of_node_full_name(data));

	return dev->of_node == data;
}

static int sun4i_drv_add_endpoints(struct device *dev,
				   struct component_match **match,
				   struct device_node *node)
{
	struct device_node *port, *ep, *remote;
	int count = 0;

	/*
	 * We don't support the frontend for now, so we will never
	 * have a device bound. Just skip over it, but we still want
	 * the rest our pipeline to be added.
	 */
	if (!sun4i_drv_node_is_frontend(node) &&
	    !of_device_is_available(node))
		return 0;

	if (!sun4i_drv_node_is_frontend(node)) {
		/* Add current component */
		DRM_DEBUG_DRIVER("Adding component %s\n",
				 of_node_full_name(node));
		component_match_add(dev, match, compare_of, node);
		count++;
	}

	/* Inputs are listed first, then outputs */
	port = of_graph_get_port_by_id(node, 1);
	if (!port) {
		DRM_DEBUG_DRIVER("No output to bind\n");
		return count;
	}

	for_each_available_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote) {
			DRM_DEBUG_DRIVER("Error retrieving the output node\n");
			of_node_put(remote);
			continue;
		}

		/*
		 * If the node is our TCON, the first port is used for
		 * panel or bridges, and will not be part of the
		 * component framework.
		 */
		if (sun4i_drv_node_is_tcon(node)) {
			struct of_endpoint endpoint;

			if (of_graph_parse_endpoint(ep, &endpoint)) {
				DRM_DEBUG_DRIVER("Couldn't parse endpoint\n");
				continue;
			}

			if (!endpoint.id) {
				DRM_DEBUG_DRIVER("Endpoint is our panel... skipping\n");
				continue;
			}
		}

		/* Walk down our tree */
		count += sun4i_drv_add_endpoints(dev, match, remote);

		of_node_put(remote);
	}

	return count;
}

static int sun4i_drv_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device_node *np = pdev->dev.of_node;
	int i, count = 0;

	for (i = 0;; i++) {
		struct device_node *pipeline = of_parse_phandle(np,
								"allwinner,pipelines",
								i);
		if (!pipeline)
			break;

		count += sun4i_drv_add_endpoints(&pdev->dev, &match,
						pipeline);
		of_node_put(pipeline);

		DRM_DEBUG_DRIVER("Queued %d outputs on pipeline %d\n",
				 count, i);
	}

	if (count)
		return component_master_add_with_match(&pdev->dev,
						       &sun4i_drv_master_ops,
						       match);
	else
		return 0;
}

static int sun4i_drv_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sun4i_drv_of_table[] = {
	{ .compatible = "allwinner,sun5i-a13-display-engine" },
	{ .compatible = "allwinner,sun8i-a33-display-engine" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_drv_of_table);

static struct platform_driver sun4i_drv_platform_driver = {
	.probe		= sun4i_drv_probe,
	.remove		= sun4i_drv_remove,
	.driver		= {
		.name		= "sun4i-drm",
		.of_match_table	= sun4i_drv_of_table,
	},
};
module_platform_driver(sun4i_drv_platform_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Engine DRM/KMS Driver");
MODULE_LICENSE("GPL");
