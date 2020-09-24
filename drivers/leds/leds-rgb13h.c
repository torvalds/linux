// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd.
/*
 * v0.1.1 Fix the bug that when pwm is disabled, the light cannot be turned off
 */
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/led-class-flash.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-led-flash.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/pwm.h>

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x01, 0x1)

#define FLASH_TIMEOUT_MIN	1000
#define FLASH_TIMEOUT_STEP	1000

struct rgb13h_led {
	struct platform_device *pdev;
	struct led_classdev_flash fled_cdev;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_subdev sd;
	/* secures access to the device */
	struct mutex lock;
	struct gpio_desc *gpio_en;
	/* maximum LED current in torch mode*/
	u32 max_mm_current;
	/* maximum LED current in flash mode */
	u32 max_flash_current;
	/* maximum flash timeout */
	u32 max_flash_tm;
	u32 intensity;
	u32 intensity_torch;
	bool strobe_state;
	/* brightness cache */
	u32 torch_brightness;
	/* assures led-triggers compatibility */
	struct work_struct work_brightness_set;

	struct timeval timestamp;

	u32 timeout;
	bool waiting;
	wait_queue_head_t done;
	struct work_struct work_timeout;

	enum v4l2_flash_led_mode led_mode;

	u32 module_index;
	const char *module_facing;
	struct pwm_device   *pwm;
	struct pwm_state pwm_state;
};

static struct rgb13h_led *fled_cdev_to_led(struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct rgb13h_led, fled_cdev);
}

static struct rgb13h_led *sd_to_led(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct rgb13h_led, sd);
}

static int rgb13h_set_output(struct rgb13h_led *led, bool on)
{
	mutex_lock(&led->lock);
	if (!IS_ERR(led->gpio_en))
		gpiod_direction_output(led->gpio_en, on);
	if (!IS_ERR(led->pwm)) {
		if (led->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			led->pwm_state.duty_cycle =
				div_u64(led->intensity_torch * led->pwm_state.period, led->max_mm_current);
		else
			led->pwm_state.duty_cycle =
				div_u64(led->intensity * led->pwm_state.period, led->max_flash_current);
		if (on) {
			led->pwm_state.enabled = true;
			pwm_apply_state(led->pwm, &led->pwm_state);
			dev_dbg(&led->pdev->dev, "led pwm duty=%llu, period=%llu, polarity=%d\n",
				led->pwm_state.duty_cycle, led->pwm_state.period, led->pwm_state.polarity);
		} else {
			led->pwm_state.enabled = false;
			pwm_apply_state(led->pwm, &led->pwm_state);
		}
	}
	if (!on) {
		led->strobe_state = false;
		if (led->waiting) {
			led->waiting = false;
			wake_up(&led->done);
		}
	} else {
		led->timestamp = ns_to_timeval(ktime_get_ns());
	}
	mutex_unlock(&led->lock);
	return 0;
}

static void rgb13h_timeout_work(struct work_struct *work)
{
	struct rgb13h_led *led =
		container_of(work, struct rgb13h_led, work_timeout);

	wait_event_timeout(led->done, !led->waiting,
		usecs_to_jiffies(led->timeout));
	if (led->waiting) {
		led->waiting = false;
		led->strobe_state = false;
		rgb13h_set_output(led, false);
	}
}

static void rgb13h_brightness_set_work(struct work_struct *work)
{
	struct rgb13h_led *led =
		container_of(work, struct rgb13h_led, work_brightness_set);

	rgb13h_set_output(led, !!led->torch_brightness);
}

static void rgb13h_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct rgb13h_led *led = fled_cdev_to_led(fled_cdev);

	led->torch_brightness = brightness;
	schedule_work(&led->work_brightness_set);
}

static int rgb13h_led_brightness_set_sync(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct rgb13h_led *led = fled_cdev_to_led(fled_cdev);

	rgb13h_set_output(led, !!brightness);
	return 0;
}

static int rgb13h_led_flash_strobe_set(struct led_classdev_flash *fled_cdev,
				       bool state)

{
	struct rgb13h_led *led = fled_cdev_to_led(fled_cdev);

	mutex_lock(&led->lock);
	led->strobe_state = state;
	if (state) {
		led->waiting = true;
		schedule_work(&led->work_timeout);
	}
	mutex_unlock(&led->lock);

	return rgb13h_set_output(led, state);
}

static int rgb13h_led_flash_strobe_get(struct led_classdev_flash *fled_cdev,
				       bool *state)
{
	struct rgb13h_led *led = fled_cdev_to_led(fled_cdev);

	mutex_lock(&led->lock);
	*state = led->strobe_state;
	mutex_unlock(&led->lock);
	return 0;
}

static int rgb13h_led_flash_timeout_set(struct led_classdev_flash *fled_cdev,
					u32 timeout)
{
	struct rgb13h_led *led = fled_cdev_to_led(fled_cdev);

	mutex_lock(&led->lock);
	led->timeout = timeout;
	mutex_unlock(&led->lock);
	return 0;
}

static int rgb13h_led_parse_dt(struct rgb13h_led *led,
			struct device_node **sub_node)
{
	struct led_classdev *led_cdev = &led->fled_cdev.led_cdev;
	struct device *dev = &led->pdev->dev;
	struct device_node *child_node = dev->of_node;
	int ret = 0;

	ret = of_property_read_u32(child_node,
				   RKMODULE_CAMERA_MODULE_INDEX,
				   &led->module_index);
	ret |= of_property_read_string(child_node,
				       RKMODULE_CAMERA_MODULE_FACING,
				       &led->module_facing);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	led->gpio_en = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(led->gpio_en)) {
		ret = PTR_ERR(led->gpio_en);
		dev_info(dev, "Unable to claim enable-gpio\n");
	}
	led->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(led->pwm)) {
		ret = PTR_ERR(led->pwm);
		dev_info(dev, "Unable to get pwm device\n");
	} else {
		led->pwm_state.period = led->pwm->args.period;
		led->pwm_state.polarity = led->pwm->args.polarity;
		dev_dbg(dev, "period %llu, polarity %d\n",
			led->pwm_state.period, led->pwm_state.polarity);
	}
	if (IS_ERR(led->gpio_en) && IS_ERR(led->pwm)) {
		dev_err(dev, "Neither enable-gpio nor pwm can be get,return error\n");
		return ret;
	}

	led_cdev->name = of_get_property(child_node, "label", NULL) ? :
						child_node->name;

	ret = of_property_read_u32(child_node, "led-max-microamp",
				&led->max_mm_current);
	if (ret < 0)
		dev_warn(dev,
			"led-max-microamp DT property missing\n");
	if (led->max_mm_current <= 0) {
		led->max_mm_current = 20000;
		dev_warn(dev,
			"get led-max-microamp error value, used default value 20000\n");
	}

	ret = of_property_read_u32(child_node, "flash-max-microamp",
				&led->max_flash_current);
	if (ret < 0) {
		dev_err(dev,
			"flash-max-microamp DT property missing\n");
		return ret;
	}
	if (led->max_flash_current <= 0) {
		led->max_flash_current = 20000;
		dev_warn(dev,
			"get flash-max-microamp error value, used default value 20000\n");
	}

	ret = of_property_read_u32(child_node, "flash-max-timeout-us",
				&led->max_flash_tm);
	if (ret < 0) {
		dev_err(dev,
			"flash-max-timeout-us DT property missing\n");
		return ret;
	}
	if (led->max_flash_tm <= 0) {
		led->max_flash_tm = 1000000;
		dev_warn(dev,
			"get flash-max-timeout-us error value, used default value 1s\n");
	}

	*sub_node = child_node;
	return ret;
}

static int rgb13h_led_get_configuration(struct rgb13h_led *led,
					struct device_node **sub_node)
{
	int ret;

	ret = rgb13h_led_parse_dt(led, sub_node);
	if (ret < 0)
		return ret;

	return 0;
}

static void rgb13h_init_flash_timeout(struct rgb13h_led *led)
{
	struct led_classdev_flash *fled_cdev = &led->fled_cdev;
	struct led_flash_setting *setting;

	/* Init flash timeout setting */
	setting = &fled_cdev->timeout;
	setting->min = FLASH_TIMEOUT_MIN;
	setting->max = led->max_flash_tm;
	setting->step = FLASH_TIMEOUT_STEP;
	setting->val = setting->max;
}

static int rgb13h_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rgb13h_led *led =
		container_of(ctrl->handler, struct rgb13h_led, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		ctrl->val = 0;
		break;
	case V4L2_CID_FLASH_STROBE_STATUS:
		if (led->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			ctrl->val = 0;
			break;
		}
		ctrl->val = led->strobe_state;
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ctrl->val = led->intensity;
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		ctrl->val = led->intensity_torch;
		break;
	case V4L2_CID_FLASH_LED_MODE:
		ctrl->val = led->led_mode;
		break;
	default:
		dev_err(&led->pdev->dev,
			"ctrl 0x%x not supported\n", ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static int rgb13h_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rgb13h_led *led =
		container_of(ctrl->handler, struct rgb13h_led, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		led->led_mode = ctrl->val;
		rgb13h_set_output(led, LED_OFF);
		if (led->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			return rgb13h_set_output(led, LED_ON);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		if (ctrl->val == V4L2_FLASH_STROBE_SOURCE_EXTERNAL)
			return -EBUSY;
		break;
	case V4L2_CID_FLASH_STROBE:
		if (led->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;
		return rgb13h_led_flash_strobe_set(&led->fled_cdev, true);
	case V4L2_CID_FLASH_STROBE_STOP:
		if (led->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			return -EBUSY;
		return rgb13h_led_flash_strobe_set(&led->fled_cdev, false);
	case V4L2_CID_FLASH_TIMEOUT:
		return rgb13h_led_flash_timeout_set(&led->fled_cdev, ctrl->val);
	case V4L2_CID_FLASH_INTENSITY:
		led->intensity = ctrl->val;
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		led->intensity_torch = ctrl->val;
		if (led->led_mode != V4L2_FLASH_LED_MODE_TORCH)
			break;
		return rgb13h_set_output(led, LED_ON);
	default:
		dev_err(&led->pdev->dev,
			"ctrl 0x%x not supported\n", ctrl->id);
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops rgb13h_ctrl_ops = {
	.g_volatile_ctrl = rgb13h_get_ctrl,
	.s_ctrl = rgb13h_set_ctrl,
};

static int rgb13h_init_controls(struct rgb13h_led *led)
{
	struct v4l2_ctrl *ctrl = NULL;

	v4l2_ctrl_handler_init(&led->ctrls, 10);
	/* V4L2_CID_FLASH_LED_MODE */
	v4l2_ctrl_new_std_menu(&led->ctrls, &rgb13h_ctrl_ops,
			       V4L2_CID_FLASH_LED_MODE, 2, ~7,
			       V4L2_FLASH_LED_MODE_NONE);
	led->led_mode = V4L2_FLASH_LED_MODE_NONE;
	/* V4L2_CID_FLASH_STROBE_SOURCE */
	v4l2_ctrl_new_std_menu(&led->ctrls, &rgb13h_ctrl_ops,
			       V4L2_CID_FLASH_STROBE_SOURCE,
			       0, ~1, V4L2_FLASH_STROBE_SOURCE_SOFTWARE);
	/* V4L2_CID_FLASH_STROBE */
	v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
			  V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);
	/* V4L2_CID_FLASH_STROBE_STOP */
	v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
			  V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);
	/* V4L2_CID_FLASH_STROBE_STATUS */
	ctrl = v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
				 V4L2_CID_FLASH_STROBE_STATUS,
				 0, 1, 1, 0);
	led->strobe_state = false;
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	/* V4L2_CID_FLASH_TIMEOUT */
	v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
			  V4L2_CID_FLASH_TIMEOUT, FLASH_TIMEOUT_MIN,
			  led->max_flash_tm, FLASH_TIMEOUT_STEP,
			  led->max_flash_tm);
	led->timeout = led->max_flash_tm;
	/* V4L2_CID_FLASH_INTENSITY */
	ctrl = v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
			  V4L2_CID_FLASH_INTENSITY, 0,
			  led->max_flash_current,
			  1,
			  led->max_flash_current);
	if (ctrl && IS_ERR(led->pwm))
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	led->intensity = led->max_flash_current;
	/* V4L2_CID_FLASH_TORCH_INTENSITY */
	ctrl = v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
			  V4L2_CID_FLASH_TORCH_INTENSITY, 0,
			  led->max_mm_current,
			  1,
			  led->max_mm_current);
	if (ctrl && IS_ERR(led->pwm))
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	led->intensity_torch = led->max_mm_current;

	/* V4L2_CID_FLASH_FAULT */
	ctrl = v4l2_ctrl_new_std(&led->ctrls, &rgb13h_ctrl_ops,
				 V4L2_CID_FLASH_FAULT, 0,
				 V4L2_FLASH_FAULT_OVER_VOLTAGE |
				 V4L2_FLASH_FAULT_TIMEOUT |
				 V4L2_FLASH_FAULT_OVER_TEMPERATURE |
				 V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	led->sd.ctrl_handler = &led->ctrls;
	return led->ctrls.error;
}

static long rgb13h_ioctl(struct v4l2_subdev *sd,
			 unsigned int cmd, void *arg)
{
	struct rgb13h_led *led = sd_to_led(sd);
	struct timeval *t;

	if (cmd == RK_VIDIOC_FLASH_TIMEINFO) {
		t = (struct timeval *)arg;
		t->tv_sec = led->timestamp.tv_sec;
		t->tv_usec = led->timestamp.tv_usec;
	} else {
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_COMPAT
#define RK_VIDIOC_COMPAT_FLASH_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct compat_timeval)
static long rgb13h_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd,
				  unsigned long arg)
{
	struct timeval t;
	struct compat_timeval compat_t;
	struct compat_timeval __user *p32 = compat_ptr(arg);

	if (cmd == RK_VIDIOC_COMPAT_FLASH_TIMEINFO) {
		rgb13h_ioctl(sd, RK_VIDIOC_FLASH_TIMEINFO, &t);
		compat_t.tv_sec = t.tv_sec;
		compat_t.tv_usec = t.tv_usec;
		put_user(compat_t.tv_sec, &p32->tv_sec);
		put_user(compat_t.tv_usec, &p32->tv_usec);
	} else {
		return -EINVAL;
	}

	return 0;
}
#endif

static const struct v4l2_subdev_core_ops v4l2_flash_core_ops = {
	.ioctl = rgb13h_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rgb13h_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops v4l2_flash_subdev_ops = {
	.core = &v4l2_flash_core_ops,
};

static const struct led_flash_ops flash_ops = {
	.strobe_set = rgb13h_led_flash_strobe_set,
	.strobe_get = rgb13h_led_flash_strobe_get,
	.timeout_set = rgb13h_led_flash_timeout_set,
};

static int rgb13h_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *sub_node = NULL;
	struct rgb13h_led *led;
	struct led_classdev *led_cdev;
	struct led_classdev_flash *fled_cdev;
	struct v4l2_subdev *sd = NULL;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->pdev = pdev;
	platform_set_drvdata(pdev, led);

	fled_cdev = &led->fled_cdev;
	fled_cdev->ops = &flash_ops;
	led_cdev = &fled_cdev->led_cdev;

	ret = rgb13h_led_get_configuration(led, &sub_node);
	if (ret < 0)
		return ret;

	mutex_init(&led->lock);

	/* Initialize LED Flash class device */
	led_cdev->brightness_set = rgb13h_led_brightness_set;
	led_cdev->brightness_set_blocking = rgb13h_led_brightness_set_sync;
	led_cdev->max_brightness = LED_FULL;
	led_cdev->flags |= LED_DEV_CAP_FLASH;
	INIT_WORK(&led->work_brightness_set, rgb13h_brightness_set_work);

	/* Init strobe timeout handle */
	led->waiting = false;
	init_waitqueue_head(&led->done);
	INIT_WORK(&led->work_timeout, rgb13h_timeout_work);

	rgb13h_init_flash_timeout(led);

	/* Register LED Flash class device */
	ret = led_classdev_flash_register(&pdev->dev, fled_cdev);
	if (ret < 0)
		goto err_flash_register;

	sd = &led->sd;
	sd->dev = dev;
	v4l2_subdev_init(sd, &v4l2_flash_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	memset(facing, 0, sizeof(facing));
	if (strcmp(led->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';
	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s",
		 led->module_index, facing,
		 led_cdev->name);
	ret = media_entity_pads_init(&sd->entity, 0, NULL);
	if (ret < 0)
		goto error_v4l2_flash_init;

	sd->entity.function = MEDIA_ENT_F_FLASH;
	ret = rgb13h_init_controls(led);
	if (ret < 0)
		goto err_init_controls;

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0)
		goto err_async_register_sd;

	return 0;

err_async_register_sd:
	v4l2_ctrl_handler_free(sd->ctrl_handler);
err_init_controls:
	media_entity_cleanup(&sd->entity);
error_v4l2_flash_init:
	led_classdev_flash_unregister(fled_cdev);
err_flash_register:
	mutex_destroy(&led->lock);

	return ret;
}

static int rgb13h_led_remove(struct platform_device *pdev)
{
	struct rgb13h_led *led = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&led->sd);
	v4l2_ctrl_handler_free(led->sd.ctrl_handler);
	media_entity_cleanup(&led->sd.entity);
	led_classdev_flash_unregister(&led->fled_cdev);

	mutex_destroy(&led->lock);

	return 0;
}

static const struct of_device_id rgb13h_led_dt_match[] = {
	{ .compatible = "led,rgb13h" },
	{},
};
MODULE_DEVICE_TABLE(of, rgb13h_led_dt_match);

static struct platform_driver rgb13h_led_driver = {
	.probe		= rgb13h_led_probe,
	.remove		= rgb13h_led_remove,
	.driver		= {
		.name	= "rgb13h-flash",
		.of_match_table = rgb13h_led_dt_match,
	},
};

module_platform_driver(rgb13h_led_driver);

MODULE_DESCRIPTION("GPIO LEDS Flash driver");
MODULE_LICENSE("GPL v2");
