// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include "qti_power_telemetry.h"

#define QPT_HW "qti-qpt-hw"

static inline struct qpt_device *to_qpt_dev_pz(struct powercap_zone *pz)
{
	return container_of(pz, struct qpt_device, pz);
}

static const char * const constraint_name[] = {
	"dummy",
};

void qpt_sysfs_notify(struct qpt_priv *qpt)
{
	struct powercap_control_type *pct;

	if (!qpt || !qpt->pct)
		return;

	pct = qpt->pct;
	sysfs_notify(&pct->dev.kobj, NULL, "enabled");
}

static int qpt_suspend(struct device *dev)
{
	struct qpt_priv *qpt = dev_get_drvdata(dev);

	if (qpt->ops->suspend)
		return qpt->ops->suspend(qpt);

	return 0;
}

static int qpt_resume(struct device *dev)
{
	struct qpt_priv *qpt = dev_get_drvdata(dev);

	if (qpt->ops->resume)
		return qpt->ops->resume(qpt);

	return 0;
}

static int qpt_get_time_window_us(struct powercap_zone *pcz, int cid, u64 *window)
{
	return -EOPNOTSUPP;
}

static int qpt_set_time_window_us(struct powercap_zone *pcz, int cid, u64 window)
{
	return -EOPNOTSUPP;
}

static int qpt_get_max_power_range_uw(struct powercap_zone *pcz, u64 *max_power_uw)
{
	struct qpt_device *qpt_dev = to_qpt_dev_pz(pcz);
	struct qpt_priv *qpt = qpt_dev->priv;

	if (qpt->ops->get_max_power)
		qpt->ops->get_max_power(qpt_dev, max_power_uw);

	return 0;
}

static int qpt_get_power_uw(struct powercap_zone *pcz, u64 *power_uw)
{
	struct qpt_device *qpt_dev = to_qpt_dev_pz(pcz);
	struct qpt_priv *qpt = qpt_dev->priv;

	if (qpt->ops->get_power)
		qpt->ops->get_power(qpt_dev, power_uw);
	else
		return -EOPNOTSUPP;

	return 0;
}

static int qpt_release_zone(struct powercap_zone *pcz)
{
	struct qpt_device *qpt_dev = to_qpt_dev_pz(pcz);
	struct qpt_priv *qpt = qpt_dev->priv;

	if (qpt->ops->release)
		qpt->ops->release(qpt);

	return 0;
}

static int qpt_get_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 *power_limit)
{
	return -EOPNOTSUPP;
}

static int qpt_set_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 power_limit)
{
	return -EOPNOTSUPP;
}

static const char *get_constraint_name(struct powercap_zone *pcz, int cid)
{
	return constraint_name[cid];
}

static int qpt_get_max_power_uw(struct powercap_zone *pcz, int id, u64 *max_power)
{
	struct qpt_device *qpt_dev = to_qpt_dev_pz(pcz);
	struct qpt_priv *qpt = qpt_dev->priv;

	if (qpt->ops->get_max_power)
		return qpt->ops->get_max_power(qpt_dev, max_power);
	else
		return -EOPNOTSUPP;
}

static struct powercap_zone_constraint_ops constraint_ops = {
	.set_power_limit_uw = qpt_set_power_limit_uw,
	.get_power_limit_uw = qpt_get_power_limit_uw,
	.set_time_window_us = qpt_set_time_window_us,
	.get_time_window_us = qpt_get_time_window_us,
	.get_max_power_uw = qpt_get_max_power_uw,
	.get_name = get_constraint_name,
};

static struct powercap_zone_ops zone_ops = {
	.get_max_power_range_uw = qpt_get_max_power_range_uw,
	.get_power_uw = qpt_get_power_uw,
	.release = qpt_release_zone,
};

static int powercap_register(struct qpt_priv *qpt)
{
	struct qpt_device *qpt_dev;
	struct powercap_zone *pcz = NULL;

	qpt->pct = powercap_register_control_type(NULL, "qpt", NULL);
	if (IS_ERR(qpt->pct)) {
		dev_err(qpt->dev, "Failed to register control type\n");
		return PTR_ERR(qpt->pct);
	}

	list_for_each_entry(qpt_dev, &qpt->qpt_dev_head, qpt_node) {
		if (!qpt_dev->enabled)
			continue;

		pcz = powercap_register_zone(&qpt_dev->pz, qpt->pct,
				qpt_dev->name, NULL, &zone_ops, 1,
				&constraint_ops);
		if (IS_ERR(pcz))
			return PTR_ERR(pcz);
	}
	return 0;
}

static int qpt_hw_device_probe(struct platform_device *pdev)
{
	int ret;
	struct qpt_priv *qpt;

	qpt = devm_kzalloc(&pdev->dev, sizeof(*qpt), GFP_KERNEL);
	if (!qpt)
		return -ENOMEM;

	qpt->dev = &pdev->dev;
	qpt->ops = &qpt_hw_ops;

	qpt->ipc_log = ipc_log_context_create(IPC_LOGPAGES, "Qpt", 0);
	if (!qpt->ipc_log)
		dev_err(qpt->dev, "%s: unable to create IPC Logging for %s\n",
					__func__, "qti_qpt");


	if (!qpt->ops || !qpt->ops->init ||
		!qpt->ops->get_power || !qpt->ops->release)
		return -EINVAL;

	ret = qpt->ops->init(qpt);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: init failed\n", __func__);
		return ret;
	}

	platform_set_drvdata(pdev, qpt);
	dev_set_drvdata(qpt->dev, qpt);

	return powercap_register(qpt);
}

static int qpt_hw_device_remove(struct platform_device *pdev)
{
	struct qpt_priv *qpt = platform_get_drvdata(pdev);
	struct qpt_device *qpt_dev;

	list_for_each_entry(qpt_dev, &qpt->qpt_dev_head, qpt_node) {
		if (qpt->pct)
			powercap_unregister_zone(qpt->pct,
				&qpt_dev->pz);
	}
	if (qpt->pct)
		powercap_unregister_control_type(qpt->pct);

	if (qpt->ops->release)
		qpt->ops->release(qpt);

	return 0;
}

static const struct dev_pm_ops qpt_pm_ops = {
	.suspend = qpt_suspend,
	.resume = qpt_resume,
};

static const struct of_device_id qpt_hw_device_match[] = {
	{.compatible = "qcom,power-telemetry"},
	{}
};

static struct platform_driver qpt_hw_device_driver = {
	.probe          = qpt_hw_device_probe,
	.remove         = qpt_hw_device_remove,
	.driver         = {
		.name   = QPT_HW,
		.pm = &qpt_pm_ops,
		.of_match_table = qpt_hw_device_match,
	},
};

module_platform_driver(qpt_hw_device_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Power Telemetry driver");
MODULE_LICENSE("GPL");
