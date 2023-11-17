// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/devfreq.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/thermal.h>

#define MAX_RETRY_CNT 20
#define RETRY_DELAY msecs_to_jiffies(1000)
#define DEVFREQ_VDD_CDEV_DRIVER "devfreq-vdd-cdev"

struct devfreq_vdd_cdev {
	char dev_name[THERMAL_NAME_LENGTH];
	struct thermal_cooling_device *cdev;
	struct devfreq *devfreq;
	unsigned long cur_state;
	u32 *freq_table;
	size_t freq_table_size;
	int retry_cnt;
	struct dev_pm_qos_request qos_min_freq_req;
	struct delayed_work register_work;
	struct device_node *np;
	struct platform_device *pdev;
};

static int devfreq_vdd_cdev_get_max_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_vdd_cdev *dfc = cdev->devdata;

	*state = dfc->freq_table_size - 1;

	return 0;
}

static int devfreq_vdd_cdev_get_min_state(struct thermal_cooling_device *cdev,
					 unsigned long *state)
{
	struct devfreq_vdd_cdev *dfc = cdev->devdata;

	*state = dfc->cur_state;

	return 0;
}

static int devfreq_vdd_cdev_set_min_state(struct thermal_cooling_device *cdev,
					 unsigned long state)
{
	struct devfreq_vdd_cdev *dfc = cdev->devdata;
	struct devfreq *df = dfc->devfreq;
	struct device *dev = df->dev.parent;
	unsigned long freq;
	int ret;

	if (state == dfc->cur_state)
		return 0;

	dev_dbg(dev, "Setting cooling min state %lu\n", state);

	if (state >= dfc->freq_table_size)
		return -EINVAL;

	freq = dfc->freq_table[dfc->freq_table_size - state - 1];

	ret = dev_pm_qos_update_request(&dfc->qos_min_freq_req, freq);
	if (ret < 0) {
		pr_err("Error placing qos request:%u. err:%d\n",
			freq, ret);
		return ret;
	}
	dfc->cur_state = state;

	return 0;
}

static struct thermal_cooling_device_ops devfreq_vdd_cdev_ops = {
	.get_max_state = devfreq_vdd_cdev_get_max_state,
	.get_cur_state = devfreq_vdd_cdev_get_min_state,
	.set_cur_state = devfreq_vdd_cdev_set_min_state,
};

static int devfreq_vdd_cdev_gen_tables(struct platform_device *pdev,
			struct devfreq_vdd_cdev *dfc)
{
	struct devfreq *df = dfc->devfreq;
	struct device *dev = df->dev.parent;
	struct dev_pm_opp *opp;
	int ret, num_opps;
	unsigned long freq;
	u32 *freq_table;
	int i;

	num_opps = dev_pm_opp_get_opp_count(dev);

	freq_table = devm_kcalloc(&pdev->dev, num_opps, sizeof(*freq_table),
			     GFP_KERNEL);
	if (!freq_table) {
		ret = -ENOMEM;
		return ret;
	}

	for (i = 0, freq = ULONG_MAX; i < num_opps; i++, freq--) {

		opp = dev_pm_opp_find_freq_floor(dev, &freq);
		if (IS_ERR(opp))
			ret = PTR_ERR(opp);

		dev_pm_opp_put(opp);
		freq_table[i] = freq;
	}

	dfc->freq_table = freq_table;
	dfc->freq_table_size = num_opps;

	return ret;
}

static void devfreq_vdd_cdev_work(struct work_struct *work)
{
	int ret = 0;
	struct devfreq_vdd_cdev *dfc = container_of(work,
						struct devfreq_vdd_cdev,
						register_work.work);
	struct device_node *np = dfc->pdev->dev.of_node;

	dfc->devfreq = devfreq_get_devfreq_by_phandle(&dfc->pdev->dev, "devfreq", 0);

	if (IS_ERR_OR_NULL(dfc->devfreq)) {
		ret = PTR_ERR(dfc->devfreq);
		if (--dfc->retry_cnt) {
			pr_debug("Devfreq not available:%d\n", ret);
			queue_delayed_work(system_highpri_wq, &dfc->register_work, RETRY_DELAY);
		}
		return;
	}

	ret = devfreq_vdd_cdev_gen_tables(dfc->pdev, dfc);
	if (ret) {
		dev_err(&dfc->pdev->dev,
			"Failed to get create table for min state cdev (%d)\n",
				ret);
		return;
	}

	ret = dev_pm_qos_add_request(&dfc->pdev->dev,
			&dfc->qos_min_freq_req,
			DEV_PM_QOS_MIN_FREQUENCY,
			PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret < 0)
		goto qos_exit;

	strscpy(dfc->dev_name, np->name, THERMAL_NAME_LENGTH);
	dfc->cdev = thermal_of_cooling_device_register(np, dfc->dev_name, dfc,
						  &devfreq_vdd_cdev_ops);
	if (IS_ERR(dfc->cdev)) {
		ret = PTR_ERR(dfc->cdev);
		dev_err(&dfc->pdev->dev,
			"Failed to register devfreq cooling device (%d)\n",
			ret);
		dfc->cdev = NULL;
		goto qos_exit;
	}
	dev_set_drvdata(&dfc->pdev->dev, dfc);

	return;

qos_exit:
	kfree(dfc->freq_table);
	dev_pm_qos_remove_request(&dfc->qos_min_freq_req);
}

static int devfreq_vdd_cdev_probe(struct platform_device *pdev)
{
	struct devfreq_vdd_cdev *dfc = NULL;

	dfc = devm_kzalloc(&pdev->dev, sizeof(*dfc), GFP_KERNEL);
	if (!dfc)
		return -ENOMEM;

	dfc->retry_cnt = MAX_RETRY_CNT;
	dfc->pdev = pdev;

	INIT_DEFERRABLE_WORK(&dfc->register_work, devfreq_vdd_cdev_work);
	queue_delayed_work(system_highpri_wq, &dfc->register_work, 0);

	return 0;
}

static int devfreq_vdd_cdev_remove(struct platform_device *pdev)
{
	struct devfreq_vdd_cdev *dfc =
		(struct devfreq_vdd_cdev *)dev_get_drvdata(&pdev->dev);
	if (dfc->cdev)
		thermal_cooling_device_unregister(dfc->cdev);

	return 0;
};

static const struct of_device_id devfreq_vdd_cdev_match[] = {
	{ .compatible = "qcom,devfreq-vdd-cooling-device", },
	{},
};

static struct platform_driver devfreq_vdd_cdev_driver = {
	.probe		= devfreq_vdd_cdev_probe,
	.remove         = devfreq_vdd_cdev_remove,
	.driver		= {
		.name = DEVFREQ_VDD_CDEV_DRIVER,
		.of_match_table = devfreq_vdd_cdev_match,
	},
};

module_platform_driver(devfreq_vdd_cdev_driver);
MODULE_LICENSE("GPL");
