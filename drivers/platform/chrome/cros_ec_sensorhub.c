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
	struct cros_ec_command *msg = sensorhub->msg;
	struct cros_ec_dev *ec = sensorhub->ec;
	int ret, i, sensor_num;
	char *name;

	sensor_num = cros_ec_get_sensor_count(ec);
	if (sensor_num < 0) {
		dev_err(dev,
			"Unable to retrieve sensor information (err:%d)\n",
			sensor_num);
		return sensor_num;
	}

	sensorhub->sensor_num = sensor_num;
	if (sensor_num == 0) {
		dev_err(dev, "Zero sensors reported.\n");
		return -EINVAL;
	}

	msg->version = 1;
	msg->insize = sizeof(struct ec_response_motion_sense);
	msg->outsize = sizeof(struct ec_params_motion_sense);

	for (i = 0; i < sensor_num; i++) {
		sensorhub->params->cmd = MOTIONSENSE_CMD_INFO;
		sensorhub->params->info.sensor_num = i;

		ret = cros_ec_cmd_xfer_status(ec->ec_dev, msg);
		if (ret < 0) {
			dev_warn(dev, "no info for EC sensor %d : %d/%d\n",
				 i, ret, msg->result);
			continue;
		}

		switch (sensorhub->resp->info.type) {
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
			dev_warn(dev, "unknown type %d\n",
				 sensorhub->resp->info.type);
			continue;
		}

		ret = cros_ec_sensorhub_allocate_sensor(dev, name, i);
		if (ret)
			return ret;

		sensor_type[sensorhub->resp->info.type]++;
	}

	if (sensor_type[MOTIONSENSE_TYPE_ACCEL] >= 2)
		ec->has_kb_wake_angle = true;

	if (cros_ec_check_features(ec,
				   EC_FEATURE_REFINED_TABLET_MODE_HYSTERESIS)) {
		ret = cros_ec_sensorhub_allocate_sensor(dev,
							"cros-ec-lid-angle",
							0);
		if (ret)
			return ret;
	}

	return 0;
}

static int cros_ec_sensorhub_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec = dev_get_drvdata(dev->parent);
	struct cros_ec_sensorhub *data;
	struct cros_ec_command *msg;
	int ret;
	int i;

	msg = devm_kzalloc(dev, sizeof(struct cros_ec_command) +
			   max((u16)sizeof(struct ec_params_motion_sense),
			       ec->ec_dev->max_response), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->command = EC_CMD_MOTION_SENSE_CMD + ec->cmd_offset;

	data = devm_kzalloc(dev, sizeof(struct cros_ec_sensorhub), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->cmd_lock);

	data->dev = dev;
	data->ec = ec;
	data->msg = msg;
	data->params = (struct ec_params_motion_sense *)msg->data;
	data->resp = (struct ec_response_motion_sense *)msg->data;

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
		data->sensor_num = 2;
		for (i = 0; i < data->sensor_num; i++) {
			ret = cros_ec_sensorhub_allocate_sensor(dev,
						"cros-ec-accel-legacy", i);
			if (ret)
				return ret;
		}
	}

	/*
	 * If the EC does not have a FIFO, the sensors will query their data
	 * themselves via sysfs or a software trigger.
	 */
	if (cros_ec_check_features(ec, EC_FEATURE_MOTION_SENSE_FIFO)) {
		ret = cros_ec_sensorhub_ring_add(data);
		if (ret)
			return ret;
		/*
		 * The msg and its data is not under the control of the ring
		 * handler.
		 */
		return devm_add_action_or_reset(dev,
						cros_ec_sensorhub_ring_remove,
						data);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * When the EC is suspending, we must stop sending interrupt,
 * we may use the same interrupt line for waking up the device.
 * Tell the EC to stop sending non-interrupt event on the iio ring.
 */
static int cros_ec_sensorhub_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_sensorhub *sensorhub = platform_get_drvdata(pdev);
	struct cros_ec_dev *ec = sensorhub->ec;

	if (cros_ec_check_features(ec, EC_FEATURE_MOTION_SENSE_FIFO))
		return cros_ec_sensorhub_ring_fifo_enable(sensorhub, false);
	return 0;
}

static int cros_ec_sensorhub_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cros_ec_sensorhub *sensorhub = platform_get_drvdata(pdev);
	struct cros_ec_dev *ec = sensorhub->ec;

	if (cros_ec_check_features(ec, EC_FEATURE_MOTION_SENSE_FIFO))
		return cros_ec_sensorhub_ring_fifo_enable(sensorhub, true);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(cros_ec_sensorhub_pm_ops,
		cros_ec_sensorhub_suspend,
		cros_ec_sensorhub_resume);

static struct platform_driver cros_ec_sensorhub_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &cros_ec_sensorhub_pm_ops,
	},
	.probe = cros_ec_sensorhub_probe,
};

module_platform_driver(cros_ec_sensorhub_driver);

MODULE_ALIAS("platform:" DRV_NAME);
MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_DESCRIPTION("ChromeOS EC MEMS Sensor Hub Driver");
MODULE_LICENSE("GPL");
