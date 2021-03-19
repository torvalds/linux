// SPDX-License-Identifier: GPL-2.0
/*
 * motor  driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 */
//#define DEBUG
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/hrtimer.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/completion.h>
#include <linux/rk_vcm_head.h>

#define DRIVER_VERSION	KERNEL_VERSION(0, 0x01, 0x00)

#define DRIVER_NAME "hall-dc"

#define IRIS_MAX_LOG 100
#define IRIS_LOG_STEP 1

#define PWM_PERIOD_DEF 333333

struct motor_dev {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct pwm_device *pwm;
	struct device *dev;
	struct mutex mutex;
	struct pwm_state pwm_state;
	u32 module_index;
	const char *module_facing;
};

static int motor_dev_parse_dt(struct motor_dev *motor)
{
	struct device_node *node = motor->dev->of_node;
	int ret = 0;
	int error = 0;

	motor->pwm = devm_pwm_get(motor->dev, NULL);
	if (IS_ERR(motor->pwm)) {
		error = PTR_ERR(motor->pwm);
		if (error != -EPROBE_DEFER)
			dev_err(motor->dev, "Failed to request PWM device: %d\n", error);
		return error;
	}
	if (motor->pwm && motor->pwm->args.period != 0) {
		motor->pwm_state.period = motor->pwm->args.period;
		motor->pwm_state.polarity = motor->pwm->args.polarity;
        } else {
		motor->pwm_state.period = PWM_PERIOD_DEF;
		motor->pwm_state.polarity = 0;
        }
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &motor->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &motor->module_facing);
	if (ret) {
		dev_err(motor->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	return 0;
}

static int motor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct motor_dev *motor = container_of(ctrl->handler,
					     struct motor_dev, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_IRIS_ABSOLUTE:
		motor->pwm_state.enabled = true;
		motor->pwm_state.duty_cycle =
			div64_u64((u64)motor->pwm_state.period * ctrl->val, IRIS_MAX_LOG);
		pwm_apply_state(motor->pwm, &motor->pwm_state);
		dev_dbg(motor->dev, "iris, ctrl->val %d, pwm duty %lld, period %lld, polarity %d\n",
			ctrl->val,
			motor->pwm_state.duty_cycle,
			motor->pwm_state.period,
			motor->pwm_state.polarity);
		break;
	default:
		dev_err(motor->dev, "not support cmd %d\n", ctrl->id);
		break;
	}
	return ret;
}

static long motor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	return 0;
}

static const struct v4l2_subdev_core_ops motor_core_ops = {
	.ioctl = motor_ioctl,
};

static const struct v4l2_subdev_ops motor_subdev_ops = {
	.core	= &motor_core_ops,
};

static const struct v4l2_ctrl_ops motor_ctrl_ops = {
	.s_ctrl = motor_s_ctrl,
};

static int motor_initialize_controls(struct motor_dev *motor)
{
	struct v4l2_ctrl_handler *handler;
	int ret = 0;

	handler = &motor->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	handler->lock = &motor->mutex;
	v4l2_ctrl_new_std(handler, &motor_ctrl_ops,
		V4L2_CID_IRIS_ABSOLUTE, 0, IRIS_MAX_LOG, IRIS_LOG_STEP, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(motor->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	motor->sd.ctrl_handler = handler;
	return ret;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int motor_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct motor_dev *motor;
	struct v4l2_subdev *sd;
	char facing[2];

	dev_info(&pdev->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);
	motor = devm_kzalloc(&pdev->dev, sizeof(*motor), GFP_KERNEL);
	if (!motor)
		return -ENOMEM;
	motor->dev = &pdev->dev;
	dev_set_name(motor->dev, "motor");
	dev_set_drvdata(motor->dev, motor);
	if (motor_dev_parse_dt(motor)) {
		dev_err(motor->dev, "parse dt error\n");
		return -EINVAL;
	}
	mutex_init(&motor->mutex);
	v4l2_subdev_init(&motor->sd, &motor_subdev_ops);
	motor->sd.owner = pdev->dev.driver->owner;
	motor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	motor->sd.dev = &pdev->dev;
	v4l2_set_subdevdata(&motor->sd, pdev);
	platform_set_drvdata(pdev, &motor->sd);
	motor_initialize_controls(motor);
	if (ret)
		goto err_free;
	ret = media_entity_pads_init(&motor->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_free;
	sd = &motor->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;
	sd->entity.flags = 2;

	memset(facing, 0, sizeof(facing));
	if (strcmp(motor->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';
	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s",
		 motor->module_index, facing,
		 DRIVER_NAME);
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&pdev->dev, "v4l2 async register subdev failed\n");

	dev_info(motor->dev, "gpio motor driver probe success\n");
	return 0;
err_free:
	v4l2_ctrl_handler_free(&motor->ctrl_handler);
	v4l2_device_unregister_subdev(&motor->sd);
	media_entity_cleanup(&motor->sd.entity);
	return ret;
}

static int motor_dev_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct motor_dev *motor;

	if (sd)
		motor = v4l2_get_subdevdata(sd);
	else
		return -ENODEV;
	if (sd)
		v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&motor->ctrl_handler);
	media_entity_cleanup(&motor->sd.entity);
	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id motor_dev_of_match[] = {
	{ .compatible = "rockchip,hall-dc", },
	{},
};
#endif

static struct platform_driver motor_dev_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(motor_dev_of_match),
	},
	.probe = motor_dev_probe,
	.remove = motor_dev_remove,
};

module_platform_driver(motor_dev_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:motor");
MODULE_AUTHOR("ROCKCHIP");
