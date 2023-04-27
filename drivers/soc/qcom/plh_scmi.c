// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scmi_plh.h>
#include <linux/scmi_protocol.h>

static struct scmi_device *scmi_dev;
static int scmi_plh_inited;

struct scmi_device *get_plh_scmi_device(void)
{
	if (!scmi_plh_inited)
		return ERR_PTR(-EPROBE_DEFER);

	if (!scmi_dev || !scmi_dev->handle)
		return ERR_PTR(-ENODEV);

	return scmi_dev;
}
EXPORT_SYMBOL(get_plh_scmi_device);

static int scmi_plh_probe(struct scmi_device *sdev)
{
	int ret = 0;

	scmi_dev = sdev;
	if (!sdev)
		ret = -ENODEV;

	scmi_plh_inited = 1;
	return ret;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ .protocol_id = SCMI_PROTOCOL_PLH, .name = "scmi_protocol_plh" },
	{ }
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_plh_drv = {
	.name		= "scmi-plh-driver",
	.probe		= scmi_plh_probe,
	.id_table	= scmi_id_table,
};
module_scmi_driver(scmi_plh_drv);

MODULE_SOFTDEP("pre: plh_vendor");
MODULE_DESCRIPTION("ARM SCMI PLH driver");
MODULE_LICENSE("GPL");
