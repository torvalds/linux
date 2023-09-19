// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <soc/qcom/rpm-smd.h>

#define RPM_SMD_CDEV_DRIVER   "rpm-smd-cooling-device"
#define RPM_SMD_RES_TYPE       0x6d726874
#define RPM_SMD_RES_ID         0
#define RPM_SMD_KEY            1

enum rpm_smd_temp_band {
	RPM_SMD_COLD_CRITICAL = 1,
	RPM_SMD_COLD,
	RPM_SMD_COOL,
	RPM_SMD_NORMAL,
	RPM_SMD_WARM,
	RPM_SMD_HOT,
	RPM_SMD_HOT_CRITICAL,
	RPM_SMD_TEMP_MAX_NR,
};

struct rpm_smd_cdev {
	struct thermal_cooling_device	*cool_dev;
	char				dev_name[THERMAL_NAME_LENGTH];
	unsigned int			state;
	struct msm_rpm_request		*rpm_handle;
};

static int rpm_smd_send_request_to_rpm(struct rpm_smd_cdev *rpm_smd_dev,
			unsigned int state)
{
	unsigned int band;
	int msg_id, ret;

	if (!rpm_smd_dev || !rpm_smd_dev->rpm_handle) {
		pr_err("Invalid RPM SMD handle\n");
		return -EINVAL;
	}

	if (rpm_smd_dev->state == state)
		return 0;

	/* if state is zero, then send RPM_SMD_NORMAL band */
	if (!state)
		band = RPM_SMD_NORMAL;
	else
		band = state;

	ret = msm_rpm_add_kvp_data(rpm_smd_dev->rpm_handle, RPM_SMD_KEY,
		(const uint8_t *)&band, (int)sizeof(band));
	if (ret) {
		pr_err("Adding KVP data failed. err:%d\n", ret);
		return ret;
	}

	msg_id = msm_rpm_send_request(rpm_smd_dev->rpm_handle);
	if (!msg_id) {
		pr_err("RPM SMD send request failed\n");
		return -ENXIO;
	}

	ret = msm_rpm_wait_for_ack(msg_id);
	if (ret) {
		pr_err("RPM SMD wait for ACK failed. err:%d\n", ret);
		return ret;
	}
	rpm_smd_dev->state = state;

	pr_debug("Requested RPM SMD band:%d for %s\n", band,
		rpm_smd_dev->dev_name);

	return ret;
}

static int rpm_smd_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = RPM_SMD_TEMP_MAX_NR - 1;

	return 0;
}

static int rpm_smd_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct rpm_smd_cdev *rpm_smd_dev = cdev->devdata;
	int ret = 0;

	if (state > (RPM_SMD_TEMP_MAX_NR - 1))
		return -EINVAL;

	ret = rpm_smd_send_request_to_rpm(rpm_smd_dev, (unsigned int)state);
	if (ret)
		return ret;

	return ret;
}

static int rpm_smd_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct rpm_smd_cdev *rpm_smd_dev = cdev->devdata;

	*state = rpm_smd_dev->state;

	return 0;
}

static struct thermal_cooling_device_ops rpm_smd_device_ops = {
	.get_max_state = rpm_smd_get_max_state,
	.get_cur_state = rpm_smd_get_cur_state,
	.set_cur_state = rpm_smd_set_cur_state,
};

static int rpm_smd_cdev_remove(struct platform_device *pdev)
{
	struct rpm_smd_cdev *rpm_smd_dev =
		(struct rpm_smd_cdev *)dev_get_drvdata(&pdev->dev);

	if (rpm_smd_dev) {
		if (rpm_smd_dev->cool_dev)
			thermal_cooling_device_unregister(
					rpm_smd_dev->cool_dev);

		rpm_smd_send_request_to_rpm(rpm_smd_dev, RPM_SMD_NORMAL);
		msm_rpm_free_request(rpm_smd_dev->rpm_handle);
	}

	return 0;
}

static int rpm_smd_cdev_probe(struct platform_device *pdev)
{
	struct rpm_smd_cdev *rpm_smd_dev;
	int ret = 0;
	struct device_node *np;

	np = dev_of_node(&pdev->dev);
	if (!np) {
		dev_err(&pdev->dev,
			"of node not available for rpm smd cooling device\n");
		return -EINVAL;
	}

	rpm_smd_dev = devm_kzalloc(&pdev->dev, sizeof(*rpm_smd_dev),
					GFP_KERNEL);
	if (!rpm_smd_dev)
		return -ENOMEM;

	rpm_smd_dev->rpm_handle = msm_rpm_create_request(MSM_RPM_CTX_ACTIVE_SET,
			RPM_SMD_RES_TYPE, RPM_SMD_RES_ID, 1);
	if (!rpm_smd_dev->rpm_handle) {
		dev_err(&pdev->dev, "Creating RPM SMD request handle failed\n");
		return -ENXIO;
	}

	strscpy(rpm_smd_dev->dev_name, np->name, THERMAL_NAME_LENGTH);

	/* Be pro-active and mitigate till we get first vote from TF */
	rpm_smd_send_request_to_rpm(rpm_smd_dev, RPM_SMD_COLD);

	rpm_smd_dev->cool_dev = thermal_of_cooling_device_register(
					np, rpm_smd_dev->dev_name, rpm_smd_dev,
					&rpm_smd_device_ops);
	if (IS_ERR(rpm_smd_dev->cool_dev)) {
		ret = PTR_ERR(rpm_smd_dev->cool_dev);
		dev_err(&pdev->dev, "rpm_smd cdev register err:%d\n", ret);
		rpm_smd_dev->cool_dev = NULL;
		return ret;
	}

	dev_set_drvdata(&pdev->dev, rpm_smd_dev);

	return ret;
}

static const struct of_device_id rpm_smd_cdev_of_match[] = {
	{.compatible = "qcom,rpm-smd-cooling-device", },
	{},
};

static struct platform_driver rpm_smd_cdev_driver = {
	.driver = {
		.name = RPM_SMD_CDEV_DRIVER,
		.of_match_table = rpm_smd_cdev_of_match,
	},
	.probe = rpm_smd_cdev_probe,
	.remove = rpm_smd_cdev_remove,
};

module_platform_driver(rpm_smd_cdev_driver);
MODULE_DESCRIPTION("RPM shared memory cooling device");
MODULE_LICENSE("GPL");
