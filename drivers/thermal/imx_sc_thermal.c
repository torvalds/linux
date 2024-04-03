// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018-2020 NXP.
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/err.h>
#include <linux/firmware/imx/sci.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

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

static int imx_sc_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct imx_sc_msg_misc_get_temp msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	struct imx_sc_sensor *sensor = thermal_zone_device_priv(tz);
	int ret;

	msg.data.req.resource_id = sensor->resource_id;
	msg.data.req.type = IMX_SC_C_TEMP;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_GET_TEMP;
	hdr->size = 2;

	ret = imx_scu_call_rpc(thermal_ipc_handle, &msg, true);
	if (ret)
		return ret;

	*temp = msg.data.resp.celsius * 1000 + msg.data.resp.tenths * 100;

	return 0;
}

static const struct thermal_zone_device_ops imx_sc_thermal_ops = {
	.get_temp = imx_sc_thermal_get_temp,
};

static int imx_sc_thermal_probe(struct platform_device *pdev)
{
	struct imx_sc_sensor *sensor;
	const int *resource_id;
	int i, ret;

	ret = imx_scu_get_handle(&thermal_ipc_handle);
	if (ret)
		return ret;

	resource_id = of_device_get_match_data(&pdev->dev);
	if (!resource_id)
		return -EINVAL;

	for (i = 0; resource_id[i] >= 0; i++) {

		sensor = devm_kzalloc(&pdev->dev, sizeof(*sensor), GFP_KERNEL);
		if (!sensor)
			return -ENOMEM;

		sensor->resource_id = resource_id[i];

		sensor->tzd = devm_thermal_of_zone_register(&pdev->dev, sensor->resource_id,
							    sensor, &imx_sc_thermal_ops);
		if (IS_ERR(sensor->tzd)) {
			/*
			 * Save the error value before freeing the
			 * sensor pointer, otherwise we endup with a
			 * use-after-free error
			 */
			ret = PTR_ERR(sensor->tzd);

			devm_kfree(&pdev->dev, sensor);

			/*
			 * The thermal framework notifies us there is
			 * no thermal zone description for such a
			 * sensor id
			 */
			if (ret == -ENODEV)
				continue;

			dev_err(&pdev->dev, "failed to register thermal zone\n");
			return ret;
		}

		devm_thermal_add_hwmon_sysfs(&pdev->dev, sensor->tzd);
	}

	return 0;
}

static const int imx_sc_sensors[] = {
	IMX_SC_R_SYSTEM, IMX_SC_R_PMIC_0,
	IMX_SC_R_AP_0, IMX_SC_R_AP_1,
	IMX_SC_R_GPU_0_PID0, IMX_SC_R_GPU_1_PID0,
	IMX_SC_R_DRC_0, -1 };

static const struct of_device_id imx_sc_thermal_table[] = {
	{ .compatible = "fsl,imx-sc-thermal", .data =  imx_sc_sensors },
	{}
};
MODULE_DEVICE_TABLE(of, imx_sc_thermal_table);

static struct platform_driver imx_sc_thermal_driver = {
		.probe = imx_sc_thermal_probe,
		.driver = {
			.name = "imx-sc-thermal",
			.of_match_table = imx_sc_thermal_table,
		},
};
module_platform_driver(imx_sc_thermal_driver);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("Thermal driver for NXP i.MX SoCs with system controller");
MODULE_LICENSE("GPL v2");
