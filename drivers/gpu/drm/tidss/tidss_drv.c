// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/console.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_irq.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "tidss_dispc.h"
#include "tidss_drv.h"
#include "tidss_kms.h"
#include "tidss_irq.h"

/* Power management */

int tidss_runtime_get(struct tidss_device *tidss)
{
	int r;

	dev_dbg(tidss->dev, "%s\n", __func__);

	r = pm_runtime_get_sync(tidss->dev);
	WARN_ON(r < 0);
	return r < 0 ? r : 0;
}

void tidss_runtime_put(struct tidss_device *tidss)
{
	int r;

	dev_dbg(tidss->dev, "%s\n", __func__);

	r = pm_runtime_put_sync(tidss->dev);
	WARN_ON(r < 0);
}

static int __maybe_unused tidss_pm_runtime_suspend(struct device *dev)
{
	struct tidss_device *tidss = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	return dispc_runtime_suspend(tidss->dispc);
}

static int __maybe_unused tidss_pm_runtime_resume(struct device *dev)
{
	struct tidss_device *tidss = dev_get_drvdata(dev);
	int r;

	dev_dbg(dev, "%s\n", __func__);

	r = dispc_runtime_resume(tidss->dispc);
	if (r)
		return r;

	return 0;
}

static int __maybe_unused tidss_suspend(struct device *dev)
{
	struct tidss_device *tidss = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	return drm_mode_config_helper_suspend(&tidss->ddev);
}

static int __maybe_unused tidss_resume(struct device *dev)
{
	struct tidss_device *tidss = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	return drm_mode_config_helper_resume(&tidss->ddev);
}

#ifdef CONFIG_PM

static const struct dev_pm_ops tidss_pm_ops = {
	.runtime_suspend = tidss_pm_runtime_suspend,
	.runtime_resume = tidss_pm_runtime_resume,
	SET_SYSTEM_SLEEP_PM_OPS(tidss_suspend, tidss_resume)
};

#endif /* CONFIG_PM */

/* DRM device Information */

static void tidss_release(struct drm_device *ddev)
{
	drm_kms_helper_poll_fini(ddev);
}

DEFINE_DRM_GEM_CMA_FOPS(tidss_fops);

static struct drm_driver tidss_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &tidss_fops,
	.release		= tidss_release,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.name			= "tidss",
	.desc			= "TI Keystone DSS",
	.date			= "20180215",
	.major			= 1,
	.minor			= 0,

	.irq_preinstall		= tidss_irq_preinstall,
	.irq_postinstall	= tidss_irq_postinstall,
	.irq_handler		= tidss_irq_handler,
	.irq_uninstall		= tidss_irq_uninstall,
};

static int tidss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tidss_device *tidss;
	struct drm_device *ddev;
	int ret;
	int irq;

	dev_dbg(dev, "%s\n", __func__);

	tidss = devm_drm_dev_alloc(&pdev->dev, &tidss_driver,
				   struct tidss_device, ddev);
	if (IS_ERR(tidss))
		return PTR_ERR(tidss);

	ddev = &tidss->ddev;

	tidss->dev = dev;
	tidss->feat = of_device_get_match_data(dev);

	platform_set_drvdata(pdev, tidss);

	ret = dispc_init(tidss);
	if (ret) {
		dev_err(dev, "failed to initialize dispc: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(dev);

#ifndef CONFIG_PM
	/* If we don't have PM, we need to call resume manually */
	dispc_runtime_resume(tidss->dispc);
#endif

	ret = tidss_modeset_init(tidss);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to init DRM/KMS (%d)\n", ret);
		goto err_runtime_suspend;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_runtime_suspend;
	}

	ret = drm_irq_install(ddev, irq);
	if (ret) {
		dev_err(dev, "drm_irq_install failed: %d\n", ret);
		goto err_runtime_suspend;
	}

	drm_kms_helper_poll_init(ddev);

	drm_mode_config_reset(ddev);

	ret = drm_dev_register(ddev, 0);
	if (ret) {
		dev_err(dev, "failed to register DRM device\n");
		goto err_irq_uninstall;
	}

	drm_fbdev_generic_setup(ddev, 32);

	dev_dbg(dev, "%s done\n", __func__);

	return 0;

err_irq_uninstall:
	drm_irq_uninstall(ddev);

err_runtime_suspend:
#ifndef CONFIG_PM
	dispc_runtime_suspend(tidss->dispc);
#endif
	pm_runtime_disable(dev);

	return ret;
}

static int tidss_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tidss_device *tidss = platform_get_drvdata(pdev);
	struct drm_device *ddev = &tidss->ddev;

	dev_dbg(dev, "%s\n", __func__);

	drm_dev_unregister(ddev);

	drm_atomic_helper_shutdown(ddev);

	drm_irq_uninstall(ddev);

#ifndef CONFIG_PM
	/* If we don't have PM, we need to call suspend manually */
	dispc_runtime_suspend(tidss->dispc);
#endif
	pm_runtime_disable(dev);

	/* devm allocated dispc goes away with the dev so mark it NULL */
	dispc_remove(tidss);

	dev_dbg(dev, "%s done\n", __func__);

	return 0;
}

static void tidss_shutdown(struct platform_device *pdev)
{
	drm_atomic_helper_shutdown(platform_get_drvdata(pdev));
}

static const struct of_device_id tidss_of_table[] = {
	{ .compatible = "ti,k2g-dss", .data = &dispc_k2g_feats, },
	{ .compatible = "ti,am65x-dss", .data = &dispc_am65x_feats, },
	{ .compatible = "ti,j721e-dss", .data = &dispc_j721e_feats, },
	{ }
};

MODULE_DEVICE_TABLE(of, tidss_of_table);

static struct platform_driver tidss_platform_driver = {
	.probe		= tidss_probe,
	.remove		= tidss_remove,
	.shutdown	= tidss_shutdown,
	.driver		= {
		.name	= "tidss",
#ifdef CONFIG_PM
		.pm	= &tidss_pm_ops,
#endif
		.of_match_table = tidss_of_table,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tidss_platform_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("TI Keystone DSS Driver");
MODULE_LICENSE("GPL v2");
