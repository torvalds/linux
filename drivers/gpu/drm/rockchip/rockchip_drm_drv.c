// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.c
 */

#include <linux/aperture.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/iommu.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#if defined(CONFIG_ARM_DMA_USE_IOMMU)
#include <asm/dma-iommu.h>
#else
#define arm_iommu_detach_device(...)	({ })
#define arm_iommu_release_mapping(...)	({ })
#define to_dma_iommu_mapping(dev) NULL
#endif

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"RockChip Soc DRM"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static const struct drm_driver rockchip_drm_driver;

/*
 * Attach a (component) device to the shared drm dma mapping from master drm
 * device.  This is used by the VOPs to map GEM buffers to a common DMA
 * mapping.
 */
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	int ret;

	if (!private->domain)
		return 0;

	if (IS_ENABLED(CONFIG_ARM_DMA_USE_IOMMU)) {
		struct dma_iommu_mapping *mapping = to_dma_iommu_mapping(dev);

		if (mapping) {
			arm_iommu_detach_device(dev);
			arm_iommu_release_mapping(mapping);
		}
	}

	ret = iommu_attach_device(private->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!private->domain)
		return;

	iommu_detach_device(private->domain, dev);
}

void rockchip_drm_dma_init_device(struct drm_device *drm_dev,
				  struct device *dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!device_iommu_mapped(dev))
		private->iommu_dev = ERR_PTR(-ENODEV);
	else if (!private->iommu_dev)
		private->iommu_dev = dev;
}

static int rockchip_drm_init_iommu(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct iommu_domain_geometry *geometry;
	u64 start, end;
	int ret;

	if (IS_ERR_OR_NULL(private->iommu_dev))
		return 0;

	private->domain = iommu_paging_domain_alloc(private->iommu_dev);
	if (IS_ERR(private->domain)) {
		ret = PTR_ERR(private->domain);
		private->domain = NULL;
		return ret;
	}

	geometry = &private->domain->geometry;
	start = geometry->aperture_start;
	end = geometry->aperture_end;

	DRM_DEBUG("IOMMU context initialized (aperture: %#llx-%#llx)\n",
		  start, end);
	drm_mm_init(&private->mm, start, end - start + 1);
	mutex_init(&private->mm_lock);

	return 0;
}

static void rockchip_iommu_cleanup(struct drm_device *drm_dev)
{
	struct rockchip_drm_private *private = drm_dev->dev_private;

	if (!private->domain)
		return;

	drm_mm_takedown(&private->mm);
	iommu_domain_free(private->domain);
}

static int rockchip_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct rockchip_drm_private *private;
	int ret;

	/* Remove existing drivers that may own the framebuffer memory. */
	ret = aperture_remove_all_conflicting_devices(rockchip_drm_driver.name);
	if (ret) {
		DRM_DEV_ERROR(dev,
			      "Failed to remove existing framebuffers - %d.\n",
			      ret);
		return ret;
	}

	drm_dev = drm_dev_alloc(&rockchip_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	private = devm_kzalloc(drm_dev->dev, sizeof(*private), GFP_KERNEL);
	if (!private) {
		ret = -ENOMEM;
		goto err_free;
	}

	drm_dev->dev_private = private;

	ret = drmm_mode_config_init(drm_dev);
	if (ret)
		goto err_free;

	rockchip_drm_mode_config_init(drm_dev);

	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_free;

	ret = rockchip_drm_init_iommu(drm_dev);
	if (ret)
		goto err_unbind_all;

	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_iommu_cleanup;

	drm_mode_config_reset(drm_dev);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm_dev);

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_kms_helper_poll_fini;

	drm_client_setup(drm_dev, NULL);

	return 0;
err_kms_helper_poll_fini:
	drm_kms_helper_poll_fini(drm_dev);
err_iommu_cleanup:
	rockchip_iommu_cleanup(drm_dev);
err_unbind_all:
	component_unbind_all(dev, drm_dev);
err_free:
	drm_dev_put(drm_dev);
	return ret;
}

static void rockchip_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_dev_unregister(drm_dev);

	drm_kms_helper_poll_fini(drm_dev);

	drm_atomic_helper_shutdown(drm_dev);
	component_unbind_all(dev, drm_dev);
	rockchip_iommu_cleanup(drm_dev);

	drm_dev_put(drm_dev);
}

DEFINE_DRM_GEM_FOPS(rockchip_drm_driver_fops);

static const struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.dumb_create		= rockchip_gem_dumb_create,
	.gem_prime_import_sg_table	= rockchip_gem_prime_import_sg_table,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int rockchip_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int rockchip_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm);
}
#endif

static const struct dev_pm_ops rockchip_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_drm_sys_suspend,
				rockchip_drm_sys_resume)
};

#define MAX_ROCKCHIP_SUB_DRIVERS 16
static struct platform_driver *rockchip_sub_drivers[MAX_ROCKCHIP_SUB_DRIVERS];
static int num_rockchip_sub_drivers;

/*
 * Get the endpoint id of the remote endpoint of the given encoder. This
 * information is used by the VOP2 driver to identify the encoder.
 *
 * @rkencoder: The encoder to get the remote endpoint id from
 * @np: The encoder device node
 * @port: The number of the port leading to the VOP2
 * @reg: The endpoint number leading to the VOP2
 */
int rockchip_drm_encoder_set_crtc_endpoint_id(struct rockchip_encoder *rkencoder,
					      struct device_node *np, int port, int reg)
{
	struct of_endpoint ep;
	struct device_node *en, *ren;
	int ret;

	en = of_graph_get_endpoint_by_regs(np, port, reg);
	if (!en)
		return -ENOENT;

	ren = of_graph_get_remote_endpoint(en);
	if (!ren)
		return -ENOENT;

	ret = of_graph_parse_endpoint(ren, &ep);
	if (ret)
		return ret;

	rkencoder->crtc_endpoint_id = ep.id;

	return 0;
}

/*
 * Check if a vop endpoint is leading to a rockchip subdriver or bridge.
 * Should be called from the component bind stage of the drivers
 * to ensure that all subdrivers are probed.
 *
 * @ep: endpoint of a rockchip vop
 *
 * returns true if subdriver, false if external bridge and -ENODEV
 * if remote port does not contain a device.
 */
int rockchip_drm_endpoint_is_subdriver(struct device_node *ep)
{
	struct device_node *node = of_graph_get_remote_port_parent(ep);
	struct platform_device *pdev;
	struct device_driver *drv;
	int i;

	if (!node)
		return -ENODEV;

	/* status disabled will prevent creation of platform-devices */
	if (!of_device_is_available(node)) {
		of_node_put(node);
		return -ENODEV;
	}

	pdev = of_find_device_by_node(node);
	of_node_put(node);

	/* enabled non-platform-devices can immediately return here */
	if (!pdev)
		return false;

	/*
	 * All rockchip subdrivers have probed at this point, so
	 * any device not having a driver now is an external bridge.
	 */
	drv = pdev->dev.driver;
	if (!drv) {
		platform_device_put(pdev);
		return false;
	}

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		if (rockchip_sub_drivers[i] == to_platform_driver(drv)) {
			platform_device_put(pdev);
			return true;
		}
	}

	platform_device_put(pdev);
	return false;
}

static void rockchip_drm_match_remove(struct device *dev)
{
	struct device_link *link;

	list_for_each_entry(link, &dev->links.consumers, s_node)
		device_link_del(link);
}

/* list of preferred vop devices */
static const char *const rockchip_drm_match_preferred[] = {
	"rockchip,rk3399-vop-big",
	NULL,
};

static struct component_match *rockchip_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	struct device_node *port;
	int i;

	/* add preferred vop device match before adding driver device matches */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent) &&
		    of_device_compatible_match(port->parent,
					       rockchip_drm_match_preferred))
			drm_of_component_match_add(dev, &match,
						   component_compare_of,
						   port->parent);

		of_node_put(port);
	}

	for (i = 0; i < num_rockchip_sub_drivers; i++) {
		struct platform_driver *drv = rockchip_sub_drivers[i];
		struct device *p = NULL, *d;

		do {
			d = platform_find_device_by_driver(p, &drv->driver);
			put_device(p);
			p = d;

			if (!d)
				break;

			device_link_add(dev, d, DL_FLAG_STATELESS);
			component_match_add(dev, &match, component_compare_dev, d);
		} while (true);
	}

	if (IS_ERR(match))
		rockchip_drm_match_remove(dev);

	return match ?: ERR_PTR(-ENODEV);
}

static const struct component_master_ops rockchip_drm_ops = {
	.bind = rockchip_drm_bind,
	.unbind = rockchip_drm_unbind,
};

static int rockchip_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0;; i++) {
		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		found = true;
		of_node_put(port);
	}

	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!found) {
		DRM_DEV_ERROR(dev,
			      "No available vop found for display-subsystem.\n");
		return -ENODEV;
	}

	return 0;
}

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	int ret;

	ret = rockchip_drm_platform_of_probe(dev);
	if (ret)
		return ret;

	match = rockchip_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	ret = component_master_add_with_match(dev, &rockchip_drm_ops, match);
	if (ret < 0) {
		rockchip_drm_match_remove(dev);
		return ret;
	}

	return 0;
}

static void rockchip_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &rockchip_drm_ops);

	rockchip_drm_match_remove(&pdev->dev);
}

static void rockchip_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id rockchip_drm_dt_ids[] = {
	{ .compatible = "rockchip,display-subsystem", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rockchip_drm_dt_ids);

static struct platform_driver rockchip_drm_platform_driver = {
	.probe = rockchip_drm_platform_probe,
	.remove = rockchip_drm_platform_remove,
	.shutdown = rockchip_drm_platform_shutdown,
	.driver = {
		.name = "rockchip-drm",
		.of_match_table = rockchip_drm_dt_ids,
		.pm = &rockchip_drm_pm_ops,
	},
};

#define ADD_ROCKCHIP_SUB_DRIVER(drv, cond) { \
	if (IS_ENABLED(cond) && \
	    !WARN_ON(num_rockchip_sub_drivers >= MAX_ROCKCHIP_SUB_DRIVERS)) \
		rockchip_sub_drivers[num_rockchip_sub_drivers++] = &drv; \
}

static int __init rockchip_drm_init(void)
{
	int ret;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	num_rockchip_sub_drivers = 0;
	ADD_ROCKCHIP_SUB_DRIVER(vop_platform_driver, CONFIG_ROCKCHIP_VOP);
	ADD_ROCKCHIP_SUB_DRIVER(vop2_platform_driver, CONFIG_ROCKCHIP_VOP2);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_lvds_driver,
				CONFIG_ROCKCHIP_LVDS);
	ADD_ROCKCHIP_SUB_DRIVER(rockchip_dp_driver,
				CONFIG_ROCKCHIP_ANALOGIX_DP);
	ADD_ROCKCHIP_SUB_DRIVER(cdn_dp_driver, CONFIG_ROCKCHIP_CDN_DP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_hdmi_qp_rockchip_pltfm_driver,
				CONFIG_ROCKCHIP_DW_HDMI_QP);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi_rockchip_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI);
	ADD_ROCKCHIP_SUB_DRIVER(dw_mipi_dsi2_rockchip_driver,
				CONFIG_ROCKCHIP_DW_MIPI_DSI2);
	ADD_ROCKCHIP_SUB_DRIVER(inno_hdmi_driver, CONFIG_ROCKCHIP_INNO_HDMI);
	ADD_ROCKCHIP_SUB_DRIVER(rk3066_hdmi_driver,
				CONFIG_ROCKCHIP_RK3066_HDMI);

	ret = platform_register_drivers(rockchip_sub_drivers,
					num_rockchip_sub_drivers);
	if (ret)
		return ret;

	ret = platform_driver_register(&rockchip_drm_platform_driver);
	if (ret)
		goto err_unreg_drivers;

	return 0;

err_unreg_drivers:
	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
	return ret;
}

static void __exit rockchip_drm_fini(void)
{
	platform_driver_unregister(&rockchip_drm_platform_driver);

	platform_unregister_drivers(rockchip_sub_drivers,
				    num_rockchip_sub_drivers);
}

module_init(rockchip_drm_init);
module_exit(rockchip_drm_fini);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DRM Driver");
MODULE_LICENSE("GPL v2");
