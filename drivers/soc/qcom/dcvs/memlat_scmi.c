// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/scmi_protocol.h>
#include <linux/scmi_memlat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/platform_device.h>

extern int cpucp_memlat_init(struct scmi_device *sdev);

static int scmi_memlat_probe(struct scmi_device *sdev)
{
	if (!sdev)
		return -ENODEV;

	return cpucp_memlat_init(sdev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ .protocol_id = SCMI_PROTOCOL_MEMLAT, .name = "scmi_protocol_memlat" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_memlat_drv = {
	.name		= "scmi-memlat-driver",
	.probe		= scmi_memlat_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_memlat_drv);

MODULE_SOFTDEP("pre: memlat_vendor");
MODULE_DESCRIPTION("ARM SCMI Memlat driver");
MODULE_LICENSE("GPL");
