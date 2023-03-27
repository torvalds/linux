// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/scmi_pmu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <soc/qcom/pmu_lib.h>

static int scmi_pmu_probe(struct scmi_device *sdev)
{
	if (!sdev)
		return -ENODEV;

	return cpucp_pmu_init(sdev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ .protocol_id = SCMI_PMU_PROTOCOL, .name = "scmi_pmu_protocol" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_pmu_drv = {
	.name		= "scmi-pmu-driver",
	.probe		= scmi_pmu_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_pmu_drv);

MODULE_SOFTDEP("pre: pmu_vendor");
MODULE_DESCRIPTION("ARM SCMI PMU driver");
MODULE_LICENSE("GPL");
