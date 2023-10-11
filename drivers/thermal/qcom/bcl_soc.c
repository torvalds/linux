// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include "thermal_zone_internal.h"

#define BCL_DRIVER_NAME       "bcl_soc_peripheral"

struct bcl_device {
	struct device				*dev;
	struct notifier_block			psy_nb;
	struct work_struct			soc_eval_work;
	long					trip_temp;
	int					trip_val;
	struct mutex				state_trans_lock;
	bool					irq_enabled;
	struct thermal_zone_device		*tz_dev;
	struct thermal_zone_device_ops	ops;
};

static struct bcl_device *bcl_perph;

static int bcl_soc_get_trend(struct thermal_zone_device *tz, int trip,
				    enum thermal_trend *trend)
{
	int trip_temp = 0, trip_hyst = 0, temp, ret;

	if (!tz)
		return -EINVAL;

	ret = tz->ops->get_trip_temp(tz, trip, &trip_temp);
	if (ret)
		return ret;

	if (tz->ops->get_trip_hyst) {
		ret = tz->ops->get_trip_hyst(tz, trip, &trip_hyst);
		if (ret)
			return ret;
	}
	temp = READ_ONCE(tz->temperature);

	if (temp >= trip_temp)
		*trend = THERMAL_TREND_RAISING;
	else if (!trip_hyst && temp < trip_temp)
		*trend = THERMAL_TREND_DROPPING;
	else if (temp <= (trip_temp - trip_hyst))
		*trend = THERMAL_TREND_DROPPING;
	else
		*trend = THERMAL_TREND_STABLE;

	return 0;
}

static int bcl_set_soc(struct thermal_zone_device *tz, int low, int high)
{
	if (high == bcl_perph->trip_temp)
		return 0;

	mutex_lock(&bcl_perph->state_trans_lock);
	pr_debug("socd threshold:%d\n", high);
	bcl_perph->trip_temp = high;
	if (high == INT_MAX) {
		bcl_perph->irq_enabled = false;
		goto unlock_and_exit;
	}
	bcl_perph->irq_enabled = true;
	schedule_work(&bcl_perph->soc_eval_work);

unlock_and_exit:
	mutex_unlock(&bcl_perph->state_trans_lock);
	return 0;
}

static int bcl_read_soc(struct thermal_zone_device *tz, int *val)
{
	static struct power_supply *batt_psy;
	union power_supply_propval ret = {0,};
	int err = 0;

	*val = 0;
	if (!batt_psy)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		err = power_supply_get_property(batt_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);
		if (err) {
			pr_err("battery percentage read error:%d\n",
				err);
			return err;
		}
		*val = 100 - ret.intval;
	}
	pr_debug("soc:%d\n", *val);

	return err;
}

static void bcl_evaluate_soc(struct work_struct *work)
{
	int battery_depletion;

	if (!bcl_perph->tz_dev)
		return;

	if (bcl_read_soc(NULL, &battery_depletion))
		return;

	mutex_lock(&bcl_perph->state_trans_lock);
	if (!bcl_perph->irq_enabled)
		goto eval_exit;
	if (battery_depletion < bcl_perph->trip_temp)
		goto eval_exit;

	bcl_perph->trip_val = battery_depletion;
	mutex_unlock(&bcl_perph->state_trans_lock);
	thermal_zone_device_update(bcl_perph->tz_dev,
				THERMAL_TRIP_VIOLATED);

	return;
eval_exit:
	mutex_unlock(&bcl_perph->state_trans_lock);
}

static int battery_supply_callback(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct power_supply *psy = data;

	if (strcmp(psy->desc->name, "battery"))
		return NOTIFY_OK;
	schedule_work(&bcl_perph->soc_eval_work);

	return NOTIFY_OK;
}

static int bcl_soc_remove(struct platform_device *pdev)
{
	power_supply_unreg_notifier(&bcl_perph->psy_nb);
	flush_work(&bcl_perph->soc_eval_work);
	if (bcl_perph->tz_dev) {
		qti_update_tz_ops(bcl_perph->tz_dev, false);
	}

	return 0;
}

static int bcl_soc_probe(struct platform_device *pdev)
{
	int ret = 0;

	bcl_perph = devm_kzalloc(&pdev->dev, sizeof(*bcl_perph), GFP_KERNEL);
	if (!bcl_perph)
		return -ENOMEM;

	mutex_init(&bcl_perph->state_trans_lock);
	bcl_perph->dev = &pdev->dev;
	bcl_perph->ops.get_temp = bcl_read_soc;
	bcl_perph->ops.set_trips = bcl_set_soc;
	bcl_perph->ops.get_trend = bcl_soc_get_trend;
	INIT_WORK(&bcl_perph->soc_eval_work, bcl_evaluate_soc);
	bcl_perph->psy_nb.notifier_call = battery_supply_callback;
	ret = power_supply_reg_notifier(&bcl_perph->psy_nb);
	if (ret < 0) {
		pr_err("soc notifier registration error. defer. err:%d\n",
			ret);
		ret = -EPROBE_DEFER;
		goto bcl_soc_probe_exit;
	}

	if (bcl_read_soc(bcl_perph->tz_dev, &ret) == -ENODATA) {
		ret = -EPROBE_DEFER;
		goto bcl_soc_probe_exit;
	}

	bcl_perph->tz_dev = devm_thermal_of_zone_register(&pdev->dev,
				0, bcl_perph, &bcl_perph->ops);
	if (IS_ERR(bcl_perph->tz_dev)) {
		pr_err("soc TZ register failed. err:%ld\n",
				PTR_ERR(bcl_perph->tz_dev));
		ret = PTR_ERR(bcl_perph->tz_dev);
		bcl_perph->tz_dev = NULL;
		goto bcl_soc_probe_exit;
	}
	qti_update_tz_ops(bcl_perph->tz_dev, true);
	thermal_zone_device_update(bcl_perph->tz_dev, THERMAL_DEVICE_UP);
	schedule_work(&bcl_perph->soc_eval_work);

	dev_set_drvdata(&pdev->dev, bcl_perph);

	return 0;

bcl_soc_probe_exit:
	bcl_soc_remove(pdev);
	return ret;
}

static const struct of_device_id bcl_match[] = {
	{
		.compatible = "qcom,msm-bcl-soc",
	},
	{},
};

static struct platform_driver bcl_driver = {
	.probe  = bcl_soc_probe,
	.remove = bcl_soc_remove,
	.driver = {
		.name           = BCL_DRIVER_NAME,
		.of_match_table = bcl_match,
	},
};

module_platform_driver(bcl_driver);
MODULE_LICENSE("GPL");
