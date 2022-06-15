// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/component.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "sun4i_drv.h"
#include "sun4i_frontend.h"
#include "sun4i_framebuffer.h"
#include "sun4i_tcon.h"
#include "sun8i_tcon_top.h"

static int drm_sun4i_gem_dumb_create(struct drm_file *file_priv,
				     struct drm_device *drm,
				     struct drm_mode_create_dumb *args)
{
	/* The hardware only allows even pitches for YUV buffers. */
	args->pitch = ALIGN(DIV_ROUND_UP(args->width * args->bpp, 8), 2);

	return drm_gem_cma_dumb_create_internal(file_priv, drm, args);
}

DEFINE_DRM_GEM_CMA_FOPS(sun4i_drv_fops);

static const struct drm_driver sun4i_drv_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,

	/* Generic Operations */
	.fops			= &sun4i_drv_fops,
	.name			= "sun4i-drm",
	.desc			= "Allwinner sun4i Display Engine",
	.date			= "20150629",
	.major			= 1,
	.minor			= 0,

	/* GEM Operations */
	DRM_GEM_CMA_DRIVER_OPS_VMAP_WITH_DUMB_CREATE(drm_sun4i_gem_dumb_create),
};

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
	INIT_LIST_HEAD(&drv->frontend_list);
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

	/* Remove early framebuffers (ie. simplefb) */
	ret = drm_aperture_remove_framebuffers(false, &sun4i_drv_driver);
	if (ret)
		goto cleanup_mode_config;

	sun4i_framebuffer_init(drm);

	/* Enable connectors polling */
	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto finish_poll;

	drm_fbdev_generic_setup(drm, 32);

	dev_set_drvdata(dev, drm);

	return 0;

finish_poll:
	drm_kms_helper_poll_fini(drm);
cleanup_mode_config:
	drm_mode_config_cleanup(drm);
	of_reserved_mem_device_release(dev);
free_drm:
	drm_dev_put(drm);
	return ret;
}

static void sun4i_drv_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	dev_set_drvdata(dev, NULL);
	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);
	drm_mode_config_cleanup(drm);

	component_unbind_all(dev, NULL);
	of_reserved_mem_device_release(dev);

	drm_dev_put(drm);
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
		of_device_is_compatible(node, "allwinner,sun8i-a23-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun8i-a33-display-frontend") ||
		of_device_is_compatible(node, "allwinner,sun9i-a80-display-frontend");
}

static bool sun4i_drv_node_is_deu(struct device_node *node)
{
	return of_device_is_compatible(node, "allwinner,sun9i-a80-deu");
}

static bool sun4i_drv_node_is_supported_frontend(struct device_node *node)
{
	if (IS_ENABLED(CONFIG_DRM_SUN4I_BACKEND))
		return !!of_match_node(sun4i_frontend_of_table, node);

	return false;
}

static bool sun4i_drv_node_is_tcon(struct device_node *node)
{
	return !!of_match_node(sun4i_tcon_of_table, node);
}

static bool sun4i_drv_node_is_tcon_with_ch0(struct device_node *node)
{
	const struct of_device_id *match;

	match = of_match_node(sun4i_tcon_of_table, node);
	if (match) {
		struct sun4i_tcon_quirks *quirks;

		quirks = (struct sun4i_tcon_quirks *)match->data;

		return quirks->has_channel_0;
	}

	return false;
}

static bool sun4i_drv_node_is_tcon_top(struct device_node *node)
{
	return IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP) &&
		!!of_match_node(sun8i_tcon_top_of_table, node);
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

static void sun4i_drv_traverse_endpoints(struct endpoint_list *list,
					 struct device_node *node,
					 int port_id)
{
	struct device_node *ep, *remote, *port;

	port = of_graph_get_port_by_id(node, port_id);
	if (!port) {
		DRM_DEBUG_DRIVER("No output to bind on port %d\n", port_id);
		return;
	}

	for_each_available_child_of_node(port, ep) {
		remote = of_graph_get_remote_port_parent(ep);
		if (!remote) {
			DRM_DEBUG_DRIVER("Error retrieving the output node\n");
			continue;
		}

		if (sun4i_drv_node_is_tcon(node)) {
			/*
			 * TCON TOP is always probed before TCON. However, TCON
			 * points back to TCON TOP when it is source for HDMI.
			 * We have to skip it here to prevent infinite looping
			 * between TCON TOP and TCON.
			 */
			if (sun4i_drv_node_is_tcon_top(remote)) {
				DRM_DEBUG_DRIVER("TCON output endpoint is TCON TOP... skipping\n");
				of_node_put(remote);
				continue;
			}

			/*
			 * If the node is our TCON with channel 0, the first
			 * port is used for panel or bridges, and will not be
			 * part of the component framework.
			 */
			if (sun4i_drv_node_is_tcon_with_ch0(node)) {
				struct of_endpoint endpoint;

				if (of_graph_parse_endpoint(ep, &endpoint)) {
					DRM_DEBUG_DRIVER("Couldn't parse endpoint\n");
					of_node_put(remote);
					continue;
				}

				if (!endpoint.id) {
					DRM_DEBUG_DRIVER("Endpoint is our panel... skipping\n");
					of_node_put(remote);
					continue;
				}
			}
		}

		kfifo_put(&list->fifo, remote);
	}
}

static int sun4i_drv_add_endpoints(struct device *dev,
				   struct endpoint_list *list,
				   struct component_match **match,
				   struct device_node *node)
{
	int count = 0;

	/*
	 * The frontend has been disabled in some of our old device
	 * trees. If we find a node that is the frontend and is
	 * disabled, we should just follow through and parse its
	 * child, but without adding it to the component list.
	 * Otherwise, we obviously want to add it to the list.
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

	/*
	 * If the device is either just a regular device, or an
	 * enabled frontend supported by the driver, we add it to our
	 * component list.
	 */
	if (!(sun4i_drv_node_is_frontend(node) ||
	      sun4i_drv_node_is_deu(node)) ||
	    (sun4i_drv_node_is_supported_frontend(node) &&
	     of_device_is_available(node))) {
		/* Add current component */
		DRM_DEBUG_DRIVER("Adding component %pOF\n", node);
		drm_of_component_match_add(dev, match, compare_of, node);
		count++;
	}

	/* each node has at least one output */
	sun4i_drv_traverse_endpoints(list, node, 1);

	/* TCON TOP has second and third output */
	if (sun4i_drv_node_is_tcon_top(node)) {
		sun4i_drv_traverse_endpoints(list, node, 3);
		sun4i_drv_traverse_endpoints(list, node, 5);
	}

	return count;
}

#ifdef CONFIG_PM_SLEEP
static int sun4i_drv_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int sun4i_drv_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops sun4i_drv_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sun4i_drv_drm_sys_suspend,
				sun4i_drv_drm_sys_resume)
};

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
	component_master_del(&pdev->dev, &sun4i_drv_master_ops);

	return 0;
}

static const struct of_device_id sun4i_drv_of_table[] = {
	{ .compatible = "allwinner,sun4i-a10-display-engine" },
	{ .compatible = "allwinner,sun5i-a10s-display-engine" },
	{ .compatible = "allwinner,sun5i-a13-display-engine" },
	{ .compatible = "allwinner,sun6i-a31-display-engine" },
	{ .compatible = "allwinner,sun6i-a31s-display-engine" },
	{ .compatible = "allwinner,sun7i-a20-display-engine" },
	{ .compatible = "allwinner,sun8i-a23-display-engine" },
	{ .compatible = "allwinner,sun8i-a33-display-engine" },
	{ .compatible = "allwinner,sun8i-a83t-display-engine" },
	{ .compatible = "allwinner,sun8i-h3-display-engine" },
	{ .compatible = "allwinner,sun8i-r40-display-engine" },
	{ .compatible = "allwinner,sun8i-v3s-display-engine" },
	{ .compatible = "allwinner,sun9i-a80-display-engine" },
	{ .compatible = "allwinner,sun50i-a64-display-engine" },
	{ .compatible = "allwinner,sun50i-h6-display-engine" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_drv_of_table);

static struct platform_driver sun4i_drv_platform_driver = {
	.probe		= sun4i_drv_probe,
	.remove		= sun4i_drv_remove,
	.driver		= {
		.name		= "sun4i-drm",
		.of_match_table	= sun4i_drv_of_table,
		.pm = &sun4i_drv_drm_pm_ops,
	},
};
module_platform_driver(sun4i_drv_platform_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Engine DRM/KMS Driver");
MODULE_LICENSE("GPL");
