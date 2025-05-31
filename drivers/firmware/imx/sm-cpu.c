// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 NXP
 */

#include <linux/firmware/imx/sm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/scmi_imx_protocol.h>

static const struct scmi_imx_cpu_proto_ops *imx_cpu_ops;
static struct scmi_protocol_handle *ph;

int scmi_imx_cpu_reset_vector_set(u32 cpuid, u64 vector, bool start, bool boot,
				  bool resume)
{
	if (!ph)
		return -EPROBE_DEFER;

	return imx_cpu_ops->cpu_reset_vector_set(ph, cpuid, vector, start,
						 boot, resume);
}
EXPORT_SYMBOL(scmi_imx_cpu_reset_vector_set);

int scmi_imx_cpu_start(u32 cpuid, bool start)
{
	if (!ph)
		return -EPROBE_DEFER;

	if (start)
		return imx_cpu_ops->cpu_start(ph, cpuid, true);

	return imx_cpu_ops->cpu_start(ph, cpuid, false);
};
EXPORT_SYMBOL(scmi_imx_cpu_start);

int scmi_imx_cpu_started(u32 cpuid, bool *started)
{
	if (!ph)
		return -EPROBE_DEFER;

	if (!started)
		return -EINVAL;

	return imx_cpu_ops->cpu_started(ph, cpuid, started);
};
EXPORT_SYMBOL(scmi_imx_cpu_started);

static int scmi_imx_cpu_probe(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;

	if (!handle)
		return -ENODEV;

	if (imx_cpu_ops) {
		dev_err(&sdev->dev, "sm cpu already initialized\n");
		return -EEXIST;
	}

	imx_cpu_ops = handle->devm_protocol_get(sdev, SCMI_PROTOCOL_IMX_CPU, &ph);
	if (IS_ERR(imx_cpu_ops))
		return PTR_ERR(imx_cpu_ops);

	return 0;
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_IMX_CPU, "imx-cpu" },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_imx_cpu_driver = {
	.name = "scmi-imx-cpu",
	.probe = scmi_imx_cpu_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_imx_cpu_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("IMX SM CPU driver");
MODULE_LICENSE("GPL");
