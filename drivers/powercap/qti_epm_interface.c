// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "qti_epm.h"

#define EPM_HW "qti-epm-hw"

static inline struct epm_device_pz_data *to_epm_dev_pz(struct powercap_zone *pz)
{
	return container_of(pz, struct epm_device_pz_data, pz);
}

static const char * const constraint_name[] = {
	"dummy",
};

static int epm_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct epm_tz_device *epm_tz = (struct epm_tz_device *)tz->devdata;
	struct epm_priv *epm = epm_tz->priv;

	return epm->ops->get_temp(epm_tz, temp);
}

static const struct thermal_zone_device_ops epm_thermal_of_ops = {
	.get_temp = epm_get_temp,
};

static int thermal_zone_register(struct epm_priv *epm)
{
	int idx;
	struct epm_tz_device *epm_tz = NULL;

	for (idx = 0; idx < epm->dt_tz_cnt; idx++) {
		epm_tz = &epm->epm_tz[idx];
		epm_tz->tz = devm_thermal_of_zone_register(
					epm->dev, idx, &epm->epm_tz[idx],
					&epm_thermal_of_ops);
		if (IS_ERR(epm_tz->tz)) {
			epm_tz->tz = NULL;
			continue;
		}
	}

	return 0;
}

static int epm_get_time_window_us(struct powercap_zone *pcz, int cid, u64 *window)
{
	return -EOPNOTSUPP;
}

static int epm_set_time_window_us(struct powercap_zone *pcz, int cid, u64 window)
{
	return -EOPNOTSUPP;
}

static int epm_get_max_power_range_uw(struct powercap_zone *pcz, u64 *max_power_uw)
{
	struct epm_device_pz_data *epm_pz = to_epm_dev_pz(pcz);
	struct epm_device *epm_dev = epm_pz->epm_dev;
	struct epm_priv *epm = epm_dev->priv;

	if (epm->ops->get_max_power)
		epm->ops->get_max_power(epm_dev, max_power_uw);

	return 0;
}

static int epm_get_power_uw(struct powercap_zone *pcz, u64 *power_uw)
{
	struct epm_device_pz_data *epm_pz = to_epm_dev_pz(pcz);
	struct epm_device *epm_dev = epm_pz->epm_dev;
	struct epm_priv *epm = epm_dev->priv;

	if (epm->ops->get_power)
		epm->ops->get_power(epm_dev, epm_pz->type, power_uw);
	else
		return -EOPNOTSUPP;

	return 0;
}

static int epm_release_zone(struct powercap_zone *pcz)
{
	struct epm_device_pz_data *epm_pz = to_epm_dev_pz(pcz);
	struct epm_device *epm_dev = epm_pz->epm_dev;
	struct epm_priv *epm = epm_dev->priv;

	if (epm->ops->release)
		epm->ops->release(epm);

	return 0;
}

static int epm_get_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 *power_limit)
{
	return -EOPNOTSUPP;
}

static int epm_set_power_limit_uw(struct powercap_zone *pcz,
			      int cid, u64 power_limit)
{
	return -EOPNOTSUPP;
}

static const char *get_constraint_name(struct powercap_zone *pcz, int cid)
{
	return constraint_name[cid];
}

static int epm_get_max_power_uw(struct powercap_zone *pcz, int id, u64 *max_power)
{
	struct epm_device_pz_data *epm_pz = to_epm_dev_pz(pcz);
	struct epm_device *epm_dev = epm_pz->epm_dev;
	struct epm_priv *epm = epm_dev->priv;

	if (epm->ops->get_max_power)
		return epm->ops->get_max_power(epm_dev, max_power);
	else
		return -EOPNOTSUPP;
}

static struct powercap_zone_constraint_ops constraint_ops = {
	.set_power_limit_uw = epm_set_power_limit_uw,
	.get_power_limit_uw = epm_get_power_limit_uw,
	.set_time_window_us = epm_set_time_window_us,
	.get_time_window_us = epm_get_time_window_us,
	.get_max_power_uw = epm_get_max_power_uw,
	.get_name = get_constraint_name,
};

static struct powercap_zone_ops zone_ops = {
	.get_max_power_range_uw = epm_get_max_power_range_uw,
	.get_power_uw = epm_get_power_uw,
	.release = epm_release_zone,
};

static int powercap_register(struct epm_priv *epm)
{
	struct epm_device *epm_dev;
	struct powercap_zone *pcz = NULL;

	epm->pct = powercap_register_control_type(NULL, "epm", NULL);
	if (IS_ERR(epm->pct)) {
		dev_err(epm->dev, "Failed to register control type\n");
		return PTR_ERR(epm->pct);
	}

	list_for_each_entry(epm_dev, &epm->epm_dev_head, epm_node) {
		if (!epm_dev->enabled)
			continue;

		epm_dev->epm_pz[EPM_10S_AVG_DATA].type = EPM_10S_AVG_DATA;
		epm_dev->epm_pz[EPM_10S_AVG_DATA].epm_dev = epm_dev;
		pcz = powercap_register_zone(
				&epm_dev->epm_pz[EPM_10S_AVG_DATA].pz,
				epm->pct, epm_dev->name, NULL, &zone_ops, 1,
				  &constraint_ops);
		if (IS_ERR(pcz))
			return PTR_ERR(pcz);
	}
	return 0;
}

static int epm_hw_device_probe(struct platform_device *pdev)
{
	int ret;
	struct epm_priv *epm;

	epm = devm_kzalloc(&pdev->dev, sizeof(*epm), GFP_KERNEL);
	if (!epm)
		return -ENOMEM;

	epm->dev = &pdev->dev;
	epm->ops = &epm_hw_ops;

	platform_set_drvdata(pdev, epm);

	epm->ipc_log = ipc_log_context_create(IPC_LOGPAGES, "qti_epm", 0);
	if (!epm->ipc_log)
		dev_err(epm->dev, "%s: unable to create IPC Logging for %s\n",
					__func__, "qti_epm");


	if (!epm->ops || !epm->ops->init || !epm->ops->get_mode ||
		!epm->ops->get_power || !epm->ops->release)
		return -EINVAL;

	ret = epm->ops->init(epm);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s: init failed\n", __func__);
		return ret;
	}

	switch (epm->ops->get_mode(epm)) {
	case EPM_ACAT_MODE:
		ret = powercap_register(epm);
		break;
	default:
		break;
	}

	if (epm->dt_tz_cnt)
		thermal_zone_register(epm);

	return ret;
}

static int epm_hw_device_remove(struct platform_device *pdev)
{
	struct epm_priv *epm = platform_get_drvdata(pdev);
	struct epm_device *epm_dev;

	list_for_each_entry(epm_dev, &epm->epm_dev_head, epm_node) {
		if (epm->pct) {
			powercap_unregister_zone(epm->pct,
				&epm_dev->epm_pz[EPM_1S_DATA].pz);
			powercap_unregister_zone(epm->pct,
				&epm_dev->epm_pz[EPM_10S_AVG_DATA].pz);
		}
	}
	if (epm->pct)
		powercap_unregister_control_type(epm->pct);

	if (epm->ops->release)
		epm->ops->release(epm);

	return 0;
}

static const struct of_device_id epm_hw_device_match[] = {
	{.compatible = "qcom,epm-devices"},
	{}
};

static struct platform_driver epm_hw_device_driver = {
	.probe          = epm_hw_device_probe,
	.remove         = epm_hw_device_remove,
	.driver         = {
		.name   = EPM_HW,
		.of_match_table = epm_hw_device_match,
	},
};

static int __init epm_hw_device_init(void)
{
	return platform_driver_register(&epm_hw_device_driver);
}
module_init(epm_hw_device_init);

static void __exit epm_hw_device_exit(void)
{
	platform_driver_unregister(&epm_hw_device_driver);
}
module_exit(epm_hw_device_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. EPM Hardware driver");
MODULE_LICENSE("GPL");
