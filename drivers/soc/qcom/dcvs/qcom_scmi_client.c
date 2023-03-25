// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/qcom_scmi_vendor.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>

struct scmi_device *scmi_dev;
static int scmi_client_inited;

struct scmi_device *get_qcom_scmi_device(void)
{
	if (!scmi_client_inited)
		return ERR_PTR(-EPROBE_DEFER);

	if (!scmi_dev || !scmi_dev->handle)
		return ERR_PTR(-ENODEV);
	return scmi_dev;
}
EXPORT_SYMBOL(get_qcom_scmi_device);

static int scmi_client_probe(struct scmi_device *sdev)
{
	int ret = 0;

	scmi_dev = sdev;
	if (!sdev)
		ret = -ENODEV;
	scmi_client_inited = 1;
	return ret;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ .protocol_id = QCOM_SCMI_VENDOR_PROTOCOL, .name = "qcom_scmi_vendor_protocol" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver qcom_scmi_client_drv = {
	.name		= "qcom-scmi-driver",
	.probe		= scmi_client_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(qcom_scmi_client_drv);

MODULE_SOFTDEP("pre: qcom_scmi_vendor");
MODULE_DESCRIPTION("QCOM SCMI client driver");
MODULE_LICENSE("GPL");


