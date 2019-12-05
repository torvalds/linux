// SPDX-License-Identifier: GPL-2.0
/*
 * Sensor HUB driver that discovers sensors behind a ChromeOS Embedded
 * Controller.
 *
 * Copyright 2019 Google LLC
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mfd/cros_ec.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_sensorhub.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#define DRV_NAME		"cros-ec-sensorhub"

static void cros_ec_sensorhub_free_sensor(void *arg)
{
	struct platform_device *pdev = arg;

	platform_device_unregister(pdev);
}

static int cros_ec_sensorhub_allocate_sensor(struct device *parent,
					     char *sensor_name,
					     int sensor_num)
{
	struct cros_ec_sensor_platform sensor_platforms = {
		.sensor_num = sensor_num,
	};
	struct platform_device *pdev;

	pdev = platform_device_register_data(parent, sensor_name,
					     PLATFORM_DEVID_AUTO,
					     &sensor_platforms,
					     sizeof(sensor_platforms));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	return devm_add_action_or_reset(parent,
					cros_ec_sensorhub_free_sensor,
					pdev);
}

static int cros_ec_sensorhub_register(struct device *dev,
				      struct cros_ec_sensorhub *sensorhub)
{
	int sensor_type[MOTIONSENSE_TYPE_MAX] = { 0 };
	struct cros_ec_dev *ec = sensorhub->ec;
	struct ec_params_motion_sense *params;
	struct ec_response_motion_sense *resp;
	struct cros_ec_command *msg;
	int ret, i, sensor_num;
	char *name;

	sensor_num = cros_ec_get_sensor_count(ec);
	if (sensor_num < 0) {
		dev_err(dev,
			"Unable to retrieve sensor information (err:%d)\n",
			sensor_num);
		return sensor_num;
	}

	if (sensor_num == 0) {
		dev_err(dev, "Zero sensors reported.\n");
		return -EINVAL;
	}

	/* Prepare a message to send INFO command to each sensor. */
	msg = kzalloc(sizeof(*msg) + max(sizeof(*params), sizeof(*resp)),
		      GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 1;
	msg->command = EC_CMD_MOTION_SENSE_CMD + ec->cmd_offset;
	msg->outsize = sizeof(*params);
	msg->insize = sizeof(*resp);
	params = (struct ec_params_motion_sense *)msg->data;
	resp = (struct ec_response_motion_sense *)msg->data;

	for (i = 0; i < sensor_num; i++) {
		params->cmd = MOTIONSENSE_CMD_INFO;
		params->info.sensor_num = i;

		ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
		if (ret < 0) {
			dev_warn(dev, "no info for EC sensor %d : %d/%d\n",
				 i, ret, msg->result);
			continue;
		}

		switch (resp->info.type) {
		case MOTIONSENSE_TYPE_ACCEL:
			name = "cros-ec-accel";
			break;
		case MOTIONSENSE_TYPE_BARO:
			name = "cros-ec-baro";
			break;
		case MOTIONSENSE_TYPE_GYRO:
			name = "cros-ec-gyro";
			break;
		case MOTIONSENSE_TYPE_MAG:
			name = "cros-ec-mag";
			break;
		case MOTIONSENSE_TYPE_PROX:
			name = "cros-ec-prox";
			break;
		case MOTIONSENSE_TYPE_LIGHT:
			name = "cros-ec-light";
			break;
		case MOTIONSENSE_TYPE_ACTIVITY:
			name = "cros-ec-activity";
			break;
		default:
			dev_warn(dev, "unknown type %d\n", resp->info.type);
			continue;
		}

		ret = cros_ec_sensorhub_allocate_sensor(dev, name, i);
		if (ret)
			goto error;

		sensor_type[resp->info.type]++;
	}

	if (sensor_type[MOTIONSENSE_TYPE_ACCEL] >= 2)
		ec->has_kb_wake_angle = true;

	if (cros_ec_check_features(ec,
				   EC_FEATURE_REFINED_TABLET_MODE_HYSTERESIS)) {
		ret = cros_ec_sensorhub_allocate_sensor(dev,
							"cros-ec-lid-angle",
							0);
		if (ret)
			goto error;
	}

	kfree(msg);
	return 0;

error:
	kfree(msg);
	return ret;
}

static int cros_ec_sensorhub_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_sensorhub *data;
	int ret;
	int i;

	data = devm_kzalloc(dev, sizeof(struct cros_ec_sensorhub), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ec = dev_get_drvdata(dev->parent);
	dev_set_drvdata(dev, data);

	/* Check whether this EC is a sensor hub. */
	if (cros_ec_check_features(data->ec, EC_FEATURE_MOTION_SENSE)) {
		ret = cros_ec_sensorhub_register(dev, data);
		if (ret)
			return ret;
	} else {
		/*
		 * If the device has sensors but does not claim to
		 * be a sensor hub, we are in legacy mode.
		 */
		for (i = 0; i < 2; i++) {
			ret = cros_ec_sensorhub_allocate_sensor(dev,
						"cros-ec-accel-legacy", i);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static struct platform_driver cros_ec_sensorhub_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = cros_ec_sensorhub_probe,
};

module_platform_driver(cros_ec_sensorhub_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_DESCRIPTION("ChromeOS EC MEMS Sensor Hub Driver");
MODULE_LICENSE("GPL");
