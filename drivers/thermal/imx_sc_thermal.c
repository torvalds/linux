// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018-2020 NXP.
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/err.h>
#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#include "thermal_core.h"
#include "thermal_hwmon.h"

#define IMX_SC_MISC_FUNC_GET_TEMP	13

static struct imx_sc_ipc *thermal_ipc_handle;

struct imx_sc_sensor {
	struct thermal_zone_device *tzd;
	u32 resource_id;
};

struct req_get_temp {
	u16 resource_id;
	u8 type;
} __packed __aligned(4);

struct resp_get_temp {
	s16 celsius;
	s8 tenths;
} __packed __aligned(4);

struct imx_sc_msg_misc_get_temp {
	struct imx_sc_rpc_msg hdr;
	union {
		struct req_get_temp req;
		struct resp_get_temp resp;
	} data;
} __packed __aligned(4);

static int imx_sc_thermal_get_temp(void *data, int *temp)
{
	struct imx_sc_msg_misc_get_temp msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	struct imx_sc_sensor *sensor = data;
	int ret;

	msg.data.req.resource_id = sensor->resource_id;
	msg.data.req.type = IMX_SC_C_TEMP;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_GET_TEMP;
	hdr->size = 2;

	ret = imx_scu_call_rpc(thermal_ipc_handle, &msg, true);
	if (ret) {
		dev_err(&sensor->tzd->device, "read temp sensor %d failed, ret %d\n",
			sensor->resource_id, ret);
		return ret;
	}

	*temp = msg.data.resp.celsius * 1000 + msg.data.resp.tenths * 100;

	return 0;
}

static const struct thermal_zone_of_device_ops imx_sc_thermal_ops = {
	.get_temp = imx_sc_thermal_get_temp,
};

static int imx_sc_thermal_probe(struct platform_device *pdev)
{
	struct device_node *np, *child, *sensor_np;
	struct imx_sc_sensor *sensor;
	int ret;

	ret = imx_scu_get_handle(&thermal_ipc_handle);
	if (ret)
		return ret;

	np = of_find_node_by_name(NULL, "thermal-zones");
	if (!np)
		return -ENODEV;

	sensor_np = of_node_get(pdev->dev.of_node);

	for_each_available_child_of_node(np, child) {
		sensor = devm_kzalloc(&pdev->dev, sizeof(*sensor), GFP_KERNEL);
		if (!sensor) {
			of_node_put(child);
			of_node_put(sensor_np);
			return -ENOMEM;
		}

		ret = thermal_zone_of_get_sensor_id(child,
						    sensor_np,
						    &sensor->resource_id);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"failed to get valid sensor resource id: %d\n",
				ret);
			of_node_put(child);
			break;
		}

		sensor->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev,
								   sensor->resource_id,
								   sensor,
								   &imx_sc_thermal_ops);
		if (IS_ERR(sensor->tzd)) {
			dev_err(&pdev->dev, "failed to register thermal zone\n");
			ret = PTR_ERR(sensor->tzd);
			of_node_put(child);
			break;
		}

		if (devm_thermal_add_hwmon_sysfs(sensor->tzd))
			dev_warn(&pdev->dev, "failed to add hwmon sysfs attributes\n");
	}

	of_node_put(sensor_np);

	return ret;
}

static int imx_sc_thermal_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id imx_sc_thermal_table[] = {
	{ .compatible = "fsl,imx-sc-thermal", },
	{}
};
MODULE_DEVICE_TABLE(of, imx_sc_thermal_table);

static struct platform_driver imx_sc_thermal_driver = {
		.probe = imx_sc_thermal_probe,
		.remove	= imx_sc_thermal_remove,
		.driver = {
			.name = "imx-sc-thermal",
			.of_match_table = imx_sc_thermal_table,
		},
};
module_platform_driver(imx_sc_thermal_driver);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("Thermal driver for NXP i.MX SoCs with system controller");
MODULE_LICENSE("GPL v2");
