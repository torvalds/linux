// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>

#include "dc-de.h"
#include "dc-drv.h"
#include "dc-pe.h"

struct dc_priv {
	struct drm_device *drm;
	struct clk *clk_cfg;
};

DEFINE_DRM_GEM_DMA_FOPS(dc_drm_driver_fops);

static struct drm_driver dc_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.fops = &dc_drm_driver_fops,
	.name = "imx8-dc",
	.desc = "i.MX8 DC DRM graphics",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

static void
dc_add_components(struct device *dev, struct component_match **matchptr)
{
	struct device_node *child, *grandchild;

	for_each_available_child_of_node(dev->of_node, child) {
		/* The interrupt controller is not a component. */
		if (of_device_is_compatible(child, "fsl,imx8qxp-dc-intc"))
			continue;

		drm_of_component_match_add(dev, matchptr, component_compare_of,
					   child);

		for_each_available_child_of_node(child, grandchild)
			drm_of_component_match_add(dev, matchptr,
						   component_compare_of,
						   grandchild);
	}
}

static int dc_drm_component_bind_all(struct dc_drm_device *dc_drm)
{
	struct drm_device *drm = &dc_drm->base;
	int ret;

	ret = component_bind_all(drm->dev, dc_drm);
	if (ret)
		return ret;

	dc_de_post_bind(dc_drm);
	dc_pe_post_bind(dc_drm);

	return 0;
}

static void dc_drm_component_unbind_all(void *ptr)
{
	struct dc_drm_device *dc_drm = ptr;
	struct drm_device *drm = &dc_drm->base;

	component_unbind_all(drm->dev, dc_drm);
}

static int dc_drm_bind(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	struct dc_drm_device *dc_drm;
	struct drm_device *drm;
	int ret;

	dc_drm = devm_drm_dev_alloc(dev, &dc_drm_driver, struct dc_drm_device,
				    base);
	if (IS_ERR(dc_drm))
		return PTR_ERR(dc_drm);

	drm = &dc_drm->base;

	ret = dc_drm_component_bind_all(dc_drm);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, dc_drm_component_unbind_all,
				       dc_drm);
	if (ret)
		return ret;

	ret = dc_kms_init(dc_drm);
	if (ret)
		return ret;

	ret = drm_dev_register(drm, 0);
	if (ret) {
		dev_err(dev, "failed to register drm device: %d\n", ret);
		goto err;
	}

	drm_client_setup_with_fourcc(drm, DRM_FORMAT_XRGB8888);

	priv->drm = drm;

	return 0;

err:
	dc_kms_uninit(dc_drm);

	return ret;
}

static void dc_drm_unbind(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	struct dc_drm_device *dc_drm = to_dc_drm_device(priv->drm);
	struct drm_device *drm = &dc_drm->base;

	priv->drm = NULL;
	drm_dev_unplug(drm);
	dc_kms_uninit(dc_drm);
	drm_atomic_helper_shutdown(drm);
}

static const struct component_master_ops dc_drm_ops = {
	.bind = dc_drm_bind,
	.unbind = dc_drm_unbind,
};

static int dc_probe(struct platform_device *pdev)
{
	struct component_match *match = NULL;
	struct dc_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk_cfg = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk_cfg))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->clk_cfg),
				     "failed to get cfg clock\n");

	dev_set_drvdata(&pdev->dev, priv);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret)
		return ret;

	dc_add_components(&pdev->dev, &match);

	ret = component_master_add_with_match(&pdev->dev, &dc_drm_ops, match);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to add component master\n");

	return 0;
}

static void dc_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &dc_drm_ops);
}

static int dc_runtime_suspend(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk_cfg);

	return 0;
}

static int dc_runtime_resume(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk_cfg);
	if (ret)
		dev_err(dev, "failed to enable cfg clock: %d\n", ret);

	return ret;
}

static int dc_suspend(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(priv->drm);
}

static int dc_resume(struct device *dev)
{
	struct dc_priv *priv = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(priv->drm);
}

static void dc_shutdown(struct platform_device *pdev)
{
	struct dc_priv *priv = dev_get_drvdata(&pdev->dev);

	drm_atomic_helper_shutdown(priv->drm);
}

static const struct dev_pm_ops dc_pm_ops = {
	RUNTIME_PM_OPS(dc_runtime_suspend, dc_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(dc_suspend, dc_resume)
};

static const struct of_device_id dc_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-dc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dc_dt_ids);

static struct platform_driver dc_driver = {
	.probe = dc_probe,
	.remove = dc_remove,
	.shutdown = dc_shutdown,
	.driver = {
		.name = "imx8-dc",
		.of_match_table	= dc_dt_ids,
		.pm = pm_sleep_ptr(&dc_pm_ops),
	},
};

static struct platform_driver * const dc_drivers[] = {
	&dc_cf_driver,
	&dc_de_driver,
	&dc_ed_driver,
	&dc_fg_driver,
	&dc_fl_driver,
	&dc_fw_driver,
	&dc_ic_driver,
	&dc_lb_driver,
	&dc_pe_driver,
	&dc_tc_driver,
	&dc_driver,
};

static int __init dc_drm_init(void)
{
	return platform_register_drivers(dc_drivers, ARRAY_SIZE(dc_drivers));
}

static void __exit dc_drm_exit(void)
{
	platform_unregister_drivers(dc_drivers, ARRAY_SIZE(dc_drivers));
}

module_init(dc_drm_init);
module_exit(dc_drm_exit);

MODULE_DESCRIPTION("i.MX8 Display Controller DRM Driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
