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
#include <linux/kfifo.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_of.h>

#include "sun4i_drv.h"
#include "sun4i_framebuffer.h"
#include "sun4i_tcon.h"

DEFINE_DRM_GEM_CMA_FOPS(sun4i_drv_fops);

static struct drm_driver sun4i_drv_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME | DRIVER_ATOMIC,

	/* Generic Operations */
	.lastclose		= drm_fb_helper_lastclose,
	.fops			= &sun4i_drv_fops,
	.name			= "sun4i-drm",
	.desc			= "Allwinner sun4i Display Engine",
	.date			= "20150629",
	.major			= 1,
	.minor			= 0,

	/* GEM Operations */
	.dumb_create		= drm_gem_cma_dumb_create,
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
	INIT_LIST_HEAD(&drv->engine_list);
	INIT_LIST_HEAD(&drv->tcon_list);

	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV) {
		dev_err(drm->dev, "Couldn't claim our memory region\n");
		goto free_drm;
	}

	drm_mode_config_init(drm);

	ret = component_bind_all(drm->dev, drm);
	if (ret) {
		dev_err(drm->dev, "Couldn't bind all pipelines components\n");
		goto cleanup_mode_config;
	}

	/* drm_vblank_init calls kcalloc, which can fail */
	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret)
		goto cleanup_mode_config;

	drm->irq_enabled = true;

	/* Remove early framebuffers (ie. simplefb) */
	sun4i_remove_framebuffers();

	/* Create our framebuffer */
	ret = sun4i_framebuffer_init(drm);
	if (ret) {
		dev_err(drm->dev, "Couldn't create our framebuffer\n");
		goto cleanup_mode_config;
	}

	/* Enable connectors polling */
	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto finish_poll;

	return 0;

finish_poll:
	drm_kms_helper_poll_fini(drm);
	sun4i_framebuffer_free(drm);
cleanup_mode_config:
	drm_mode_config_cleanup(drm);
	of_reserved_mem_device_release(dev);
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
	drm_mode_config_cleanup(drm);
	of_reserved_mem_device_release(dev);
	drm_dev_unref(drm);
}

static const struct component_master_ops sun4i_drv_master_ops = {
	.bind	= sun4i_drv_bind,
	.unbind	= sun4i_drv_unbind,
};

static bool sun4i_drv_node_is_connector(struct device_node *node)
{
	return of_device_is_compatible(node, "hdmi-connector");
}

static bool sun4i_drv_node_is_frontend(struct device_node *node)
{
	return of_device_is_compatible(node, "allwinner,sun4i-a10-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun5i-a13-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun6i-a31-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun7i-a20-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun8i-a33-display-frontend");
}

static bool sun4i_drv_node_is_tcon(struct device_node *node)
{
	return !!of_match_node(sun4i_tcon_of_table, node);
}

static int compare_of(struct device *dev, void *data)
{
	DRM_DEBUG_DRIVER("Comparing of node %pOF with %pOF\n",
			 dev->of_node,
			 data);

	return dev->of_node == data;
}

/*
 * The encoder drivers use drm_of_find_possible_crtcs to get upstream
 * crtcs from the device tree using of_graph. For the results to be
 * correct, encoders must be probed/bound after _all_ crtcs have been
 * created. The existing code uses a depth first recursive traversal
 * of the of_graph, which means the encoders downstream of the TCON
 * get add right after the first TCON. The second TCON or CRTC will
 * never be properly associated with encoders connected to it.
 *
 * Also, in a dual display pipeline setup, both frontends can feed
 * either backend, and both backends can feed either TCON, we want
 * all components of the same type to be added before the next type
 * in the pipeline. Fortunately, the pipelines are perfectly symmetric,
 * i.e. components of the same type are at the same depth when counted
 * from the frontend. The only exception is the third pipeline in
 * the A80 SoC, which we do not support anyway.
 *
 * Hence we can use a breadth first search traversal order to add
 * components. We do not need to check for duplicates. The component
 * matching system handles this for us.
 */
struct endpoint_list {
	DECLARE_KFIFO(fifo, struct device_node *, 16);
};

static int sun4i_drv_add_endpoints(struct device *dev,
				   struct endpoint_list *list,
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

	/*
	 * The connectors will be the last nodes in our pipeline, we
	 * can just bail out.
	 */
	if (sun4i_drv_node_is_connector(node))
		return 0;

	if (!sun4i_drv_node_is_frontend(node)) {
		/* Add current component */
		DRM_DEBUG_DRIVER("Adding component %pOF\n", node);
		drm_of_component_match_add(dev, match, compare_of, node);
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

		kfifo_put(&list->fifo, remote);
	}

	return count;
}

static int sun4i_drv_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct device_node *np = pdev->dev.of_node, *endpoint;
	struct endpoint_list list;
	int i, ret, count = 0;

	INIT_KFIFO(list.fifo);

	for (i = 0;; i++) {
		struct device_node *pipeline = of_parse_phandle(np,
								"allwinner,pipelines",
								i);
		if (!pipeline)
			break;

		kfifo_put(&list.fifo, pipeline);
	}

	while (kfifo_get(&list.fifo, &endpoint)) {
		/* process this endpoint */
		ret = sun4i_drv_add_endpoints(&pdev->dev, &list, &match,
					      endpoint);

		/* sun4i_drv_add_endpoints can fail to allocate memory */
		if (ret < 0)
			return ret;

		count += ret;
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
	{ .compatible = "allwinner,sun4i-a10-display-engine" },
	{ .compatible = "allwinner,sun5i-a10s-display-engine" },
	{ .compatible = "allwinner,sun5i-a13-display-engine" },
	{ .compatible = "allwinner,sun6i-a31-display-engine" },
	{ .compatible = "allwinner,sun6i-a31s-display-engine" },
	{ .compatible = "allwinner,sun7i-a20-display-engine" },
	{ .compatible = "allwinner,sun8i-a33-display-engine" },
	{ .compatible = "allwinner,sun8i-a83t-display-engine" },
	{ .compatible = "allwinner,sun8i-v3s-display-engine" },
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
