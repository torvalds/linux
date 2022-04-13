// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/pm_runtime.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include "komeda_dev.h"
#include "komeda_kms.h"

struct komeda_drv {
	struct komeda_dev *mdev;
	struct komeda_kms_dev *kms;
};

struct komeda_dev *dev_to_mdev(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);

	return mdrv ? mdrv->mdev : NULL;
}

static void komeda_unbind(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);

	if (!mdrv)
		return;

	komeda_kms_detach(mdrv->kms);

	if (pm_runtime_enabled(dev))
		pm_runtime_disable(dev);
	else
		komeda_dev_suspend(mdrv->mdev);

	komeda_dev_destroy(mdrv->mdev);

	dev_set_drvdata(dev, NULL);
	devm_kfree(dev, mdrv);
}

static int komeda_bind(struct device *dev)
{
	struct komeda_drv *mdrv;
	int err;

	mdrv = devm_kzalloc(dev, sizeof(*mdrv), GFP_KERNEL);
	if (!mdrv)
		return -ENOMEM;

	mdrv->mdev = komeda_dev_create(dev);
	if (IS_ERR(mdrv->mdev)) {
		err = PTR_ERR(mdrv->mdev);
		goto free_mdrv;
	}

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev))
		komeda_dev_resume(mdrv->mdev);

	mdrv->kms = komeda_kms_attach(mdrv->mdev);
	if (IS_ERR(mdrv->kms)) {
		err = PTR_ERR(mdrv->kms);
		goto destroy_mdev;
	}

	dev_set_drvdata(dev, mdrv);

	return 0;

destroy_mdev:
	if (pm_runtime_enabled(dev))
		pm_runtime_disable(dev);
	else
		komeda_dev_suspend(mdrv->mdev);

	komeda_dev_destroy(mdrv->mdev);

free_mdrv:
	devm_kfree(dev, mdrv);
	return err;
}

static const struct component_master_ops komeda_master_ops = {
	.bind	= komeda_bind,
	.unbind	= komeda_unbind,
};

static void komeda_add_slave(struct device *master,
			     struct component_match **match,
			     struct device_node *np,
			     u32 port, u32 endpoint)
{
	struct device_node *remote;

	remote = of_graph_get_remote_node(np, port, endpoint);
	if (remote) {
		drm_of_component_match_add(master, match, component_compare_of, remote);
		of_node_put(remote);
	}
}

static int komeda_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct device_node *child;

	if (!dev->of_node)
		return -ENODEV;

	for_each_available_child_of_node(dev->of_node, child) {
		if (of_node_cmp(child->name, "pipeline") != 0)
			continue;

		/* add connector */
		komeda_add_slave(dev, &match, child, KOMEDA_OF_PORT_OUTPUT, 0);
		komeda_add_slave(dev, &match, child, KOMEDA_OF_PORT_OUTPUT, 1);
	}

	return component_master_add_with_match(dev, &komeda_master_ops, match);
}

static int komeda_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &komeda_master_ops);
	return 0;
}

static const struct of_device_id komeda_of_match[] = {
	{ .compatible = "arm,mali-d71", .data = d71_identify, },
	{ .compatible = "arm,mali-d32", .data = d71_identify, },
	{},
};

MODULE_DEVICE_TABLE(of, komeda_of_match);

static int __maybe_unused komeda_rt_pm_suspend(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);

	return komeda_dev_suspend(mdrv->mdev);
}

static int __maybe_unused komeda_rt_pm_resume(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);

	return komeda_dev_resume(mdrv->mdev);
}

static int __maybe_unused komeda_pm_suspend(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);
	int res;

	res = drm_mode_config_helper_suspend(&mdrv->kms->base);

	if (!pm_runtime_status_suspended(dev))
		komeda_dev_suspend(mdrv->mdev);

	return res;
}

static int __maybe_unused komeda_pm_resume(struct device *dev)
{
	struct komeda_drv *mdrv = dev_get_drvdata(dev);

	if (!pm_runtime_status_suspended(dev))
		komeda_dev_resume(mdrv->mdev);

	return drm_mode_config_helper_resume(&mdrv->kms->base);
}

static const struct dev_pm_ops komeda_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(komeda_pm_suspend, komeda_pm_resume)
	SET_RUNTIME_PM_OPS(komeda_rt_pm_suspend, komeda_rt_pm_resume, NULL)
};

static struct platform_driver komeda_platform_driver = {
	.probe	= komeda_platform_probe,
	.remove	= komeda_platform_remove,
	.driver	= {
		.name = "komeda",
		.of_match_table	= komeda_of_match,
		.pm = &komeda_pm_ops,
	},
};

drm_module_platform_driver(komeda_platform_driver);

MODULE_AUTHOR("James.Qian.Wang <james.qian.wang@arm.com>");
MODULE_DESCRIPTION("Komeda KMS driver");
MODULE_LICENSE("GPL v2");
