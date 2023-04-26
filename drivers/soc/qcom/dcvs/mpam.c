// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>
#include <soc/qcom/mpam.h>

#define MPAM_ALGO_STR	0x4D50414D4558544E  /* "MPAMEXTN" */

enum mpam_profiling_param_ids {
	PARAM_CACHE_PORTION = 1,
};

struct mpam_cache_portion {
	uint32_t part_id;
	uint32_t cache_portion;
};

static struct mpam_cache_portion cur_cache_portion;
static struct scmi_protocol_handle *ph;
static const struct qcom_scmi_vendor_ops *ops;
static struct scmi_device *sdev;

int qcom_mpam_set_cache_portion(u32 part_id, u32 cache_portion)
{
	int ret = -EPERM;
	struct mpam_cache_portion msg;

	msg.part_id = part_id;
	msg.cache_portion = cache_portion;

	if (ops)
		ret = ops->set_param(ph, &msg, MPAM_ALGO_STR, PARAM_CACHE_PORTION,
				sizeof(msg));

	if (!ret)
		memcpy(&cur_cache_portion, &msg, sizeof(msg));

	return ret;
}
EXPORT_SYMBOL(qcom_mpam_set_cache_portion);

static int mpam_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	sdev = get_qcom_scmi_device();
	if (IS_ERR(sdev)) {
		ret = PTR_ERR(sdev);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Error getting scmi_dev ret=%d\n", ret);
		return ret;
	}
	ops = sdev->handle->devm_protocol_get(sdev, QCOM_SCMI_VENDOR_PROTOCOL, &ph);
	if (IS_ERR(ops)) {
		ret = PTR_ERR(ops);
		ops = NULL;
		dev_err(dev, "Error getting vendor protocol ops: %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id qcom_mpam_table[] = {
	{ .compatible = "qcom,mpam" },
	{},
};

static struct platform_driver qcom_mpam_driver = {
	.driver = {
		.name = "qcom-mpam",
		.of_match_table = qcom_mpam_table,
	},
	.probe = mpam_dev_probe,
};

module_platform_driver(qcom_mpam_driver);
MODULE_SOFTDEP("pre: qcom_scmi_client");
MODULE_DESCRIPTION("QCOM MPAM driver");
MODULE_LICENSE("GPL");
