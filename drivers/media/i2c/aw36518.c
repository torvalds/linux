// SPDX-License-Identifier: GPL-2.0
/*
 * General device driver for awinic aw36518, FLASH LED Driver
 *
 * Copyright (C) 2022 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 init version.
 * V0.0X01.0X01 fix power off torch not off issue.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-led-flash.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/compat.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)
#define AW36518_NAME			"aw36518"

#define AW36518_REG_ID			0x00
#define AW36518_ID			0x30

#define AW36518_REG_MODE		0x01
#define AW36518_REG_LED0_FLASH_CUR	0x03
#define AW36518_REG_LED0_TORCH_CUR	0x05
#define AW36518_REG_LED1_FLASH_CUR	0x03
#define AW36518_REG_LED1_TORCH_CUR	0x05
#define AW36518_HW_TORCH		BIT(4)
#define AW36518_HW_STROBE		BIT(5)

#define AW36518_REG_FAULT		0x0A
#define AW36518_REG_FL_SC		0x70
#define AW36518_REG_FL_OT		BIT(2)
#define AW36518_REG_FL_TO		BIT(0)

#define AW36518_REG_FAULT2		0x0B
#define AW36518_REG_FL_OVP		BIT(1)

#define AW36518_REG_ENABLE		0x01
#define LED_ON				0x03
#define LED_OFF				0x00

/*  FLASH Brightness
 *	min 2940uA, step 5870uA, max 1500000uA
 */
#define AW36518_MIN_FLASH_INTENSITY	2940
#define AW36518_MAX_FLASH_INTENSITY	1500000
#define AW36518_FLASH_INTENSITY_DEFAULT	748430
#define AW36518_FLASH_INTENSITY_STEP	5870
#define AW36518_FLASH_BRT_uA_TO_REG(a)	\
	((a) < AW36518_MIN_FLASH_INTENSITY ? 0 :	\
	 (((a) - AW36518_MIN_FLASH_INTENSITY) / AW36518_FLASH_INTENSITY_STEP))
#define AW36518_FLASH_BRT_REG_TO_uA(a)		\
	((a) * AW36518_FLASH_INTENSITY_STEP + AW36518_MIN_FLASH_INTENSITY)

/*  TORCH BRT
 *	min 750uA, step 1510uA, max 3860000uA
 */
#define AW36518_MIN_TORCH_INTENSITY	750
#define AW36518_MAX_TORCH_INTENSITY	386000
#define AW36518_TORCH_INTENSITY_DEFAULT	192000
#define AW36518_TORCH_INTENSITY_STEP	1510
#define AW36518_TORCH_BRT_uA_TO_REG(a)	\
	((a) < AW36518_MIN_TORCH_INTENSITY ? 0 :	\
	 (((a) - AW36518_MIN_TORCH_INTENSITY) / AW36518_TORCH_INTENSITY_STEP))
#define AW36518_TORCH_BRT_REG_TO_uA(a)		\
	((a) * AW36518_TORCH_INTENSITY_STEP + AW36518_MIN_TORCH_INTENSITY)


/*  FLASH TIMEOUT DURATION
 *	min 40ms, step 40 or 200ms, max 1600ms
 */
#define TIMEOUT_MAX			1600000
#define TIMEOUT_STEP			40000
#define TIMEOUT_MIN			40000
#define TIMEOUT_STEP2			200000
#define TIMEOUT_DEFAULT			600000

#define AW36518_FLASH_TOUT_ms_TO_REG(a)	\
	((a) < TIMEOUT_MIN ? 0 :	\
	 (((a) - TIMEOUT_MIN) / TIMEOUT_STEP))
#define AW36518_FLASH_TOUT_REG_TO_ms(a)		\
	((a) * TIMEOUT_STEP + TIMEOUT_MIN)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

enum aw36518_led_id {
	LED0 = 0,
	LED1,
	LED_MAX
};

struct aw36518_led {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *flash_brt;
	struct v4l2_ctrl *torch_brt;
	struct __kernel_old_timeval timestamp;
	u32 max_flash_timeout;
	u32 max_flash_intensity;
	u32 max_torch_intensity;
};

struct aw36518_flash {
	struct i2c_client *client;
	struct aw36518_led leds[LED_MAX];

	struct gpio_desc *en_gpio;
	struct gpio_desc *torch_gpio;
	struct gpio_desc *strobe_gpio;
	struct gpio_desc *tx_gpio;

	struct mutex lock;

	u32 flash_timeout;
	enum v4l2_flash_led_mode led_mode;

	u32 module_index;
	const char *module_facing;
	bool			power_on;
};

#define to_led(sd) container_of(sd, struct aw36518_led, sd)

static int aw36518_i2c_write(struct aw36518_flash *flash, u8 reg, u8 val)
{
	struct i2c_client *client = flash->client;
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev,
			 "%s: reg:0x%x val:0x%x failed\n",
			 __func__, reg, val);

	v4l2_dbg(2, debug, &flash->leds[0].sd,
		 "%s: reg:0x%x val:0x%x\n",
		  __func__, reg, val);

	return ret;
}

static int aw36518_i2c_read(struct aw36518_flash *flash, u8 reg)
{
	struct i2c_client *client = flash->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev,
			 "%s: reg:0x%x failed\n",
			 __func__, reg);

	dev_dbg(&client->dev, "%s: reg:0x%x val:0x%x\n",
		 __func__, reg, ret);
	return ret;
}

static int aw36518_led_on(struct aw36518_flash *flash, bool on)
{
	int ret;
	struct i2c_client *client = flash->client;
	int temp;
	u8 val = 0;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: on:%d\n", __func__, on);

	temp = aw36518_i2c_read(flash, AW36518_REG_ENABLE);
	if (temp < 0)
		dev_err(&client->dev,
			 "%s: read reg:0x%x failed.\n",
			 __func__, AW36518_REG_ENABLE);

	if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH && on)
		temp = temp | 0x0c;
	else
		temp = temp & 0xfc;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: temp:%d\n", __func__, temp);
	val = on ? LED_ON : LED_OFF;
	ret = aw36518_i2c_write(flash, AW36518_REG_ENABLE, val | temp);
	flash->leds[0].timestamp = ns_to_kernel_old_timeval(ktime_get_ns());
	flash->leds[1].timestamp = flash->leds[0].timestamp;
	return ret;
}

static int aw36518_get_fault(struct aw36518_flash *flash)
{
	int fault = 0;
	int temp = 0;

	fault = aw36518_i2c_read(flash, AW36518_REG_FAULT);

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: 0x%x\n", __func__, fault);
	fault = fault & 0xfd;

	temp = aw36518_i2c_read(flash, AW36518_REG_FAULT2);
	fault = fault | (temp & 0x2);

	return fault;
}

static int aw36518_timeout_cal(struct aw36518_flash *flash)
{
	u8 val;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: timeout:%dUS\n", __func__, flash->flash_timeout);

	if (flash->flash_timeout < 400000)
		val =  AW36518_FLASH_TOUT_ms_TO_REG(flash->flash_timeout);
	else {
		val =  (flash->flash_timeout - 0x400000) / TIMEOUT_STEP2;
		val = val + 0x09;
	}

	return val;
}

static int aw36518_set_timeout(struct aw36518_flash *flash, u32 timeout)
{
	int ret;
	u8 val;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: %d\n",
		 __func__, flash->flash_timeout);

	flash->flash_timeout = timeout;
	ret = aw36518_i2c_read(flash, 0x08);
	if (ret < 0)
		return ret;

	val =  aw36518_timeout_cal(flash);

	return aw36518_i2c_write(flash, 0x08, val | (ret & 0xf0));
}

static int aw36518_torch_brt(struct aw36518_flash *flash,
			     enum aw36518_led_id id)
{
	struct aw36518_led *led = &flash->leds[id];
	u8 val, reg;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: %d\n", __func__, led->torch_brt->val);

	val = AW36518_TORCH_BRT_uA_TO_REG(led->torch_brt->val);
	reg = id ? AW36518_REG_LED1_TORCH_CUR : AW36518_REG_LED0_TORCH_CUR;
	return aw36518_i2c_write(flash, reg, val);
}

static int aw36518_flash_brt(struct aw36518_flash *flash,
			     enum aw36518_led_id id)
{
	struct aw36518_led *led = &flash->leds[id];
	u8 val, reg;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: %d\n", __func__, led->flash_brt->val);

	val = AW36518_FLASH_BRT_uA_TO_REG(led->flash_brt->val);
	reg = id ? AW36518_REG_LED1_FLASH_CUR : AW36518_REG_LED0_FLASH_CUR;
	return aw36518_i2c_write(flash, reg, val);
}

static int aw36518_set_mode(struct aw36518_flash *flash,
			    enum aw36518_led_id id, unsigned int mode)
{
	int ret = 0;

	v4l2_dbg(1, debug, &flash->leds[id].sd,
		 "%s: %d cur:%d\n", __func__,
		 mode, flash->led_mode);

	if (flash->led_mode == mode)
		return 0;

	aw36518_led_on(flash, false);

	flash->led_mode = mode;

	if (mode == V4L2_FLASH_LED_MODE_FLASH) {
		ret = aw36518_i2c_write(flash, 0x01, 0x0C);
		ret |= aw36518_flash_brt(flash, LED0);
	} else if (mode == V4L2_FLASH_LED_MODE_TORCH) {
		//ret = aw36518_i2c_write(flash, 0x01, 0x08);
		/* hw torch/strobe io trigger torch */
		ret = aw36518_i2c_write(flash, 0x01, AW36518_HW_TORCH);
		ret |= aw36518_torch_brt(flash, LED0);
		ret |= aw36518_led_on(flash, true);
		if (flash->torch_gpio) {
			v4l2_dbg(1, debug, &flash->leds[id].sd,
				 "%s:set torch gpio high.\n", __func__);
			gpiod_set_value_cansleep(flash->torch_gpio, 1);
		}

	} else {
		ret = aw36518_i2c_write(flash, 0x01, 0x00);
		if (flash->torch_gpio) {
			v4l2_dbg(1, debug, &flash->leds[id].sd,
				 "%s:set torch gpio low.\n", __func__);
			gpiod_set_value_cansleep(flash->torch_gpio, 0);
		}

	}

	return ret;
}

static int aw36518_strobe(struct aw36518_flash *flash, bool on)
{
	int ret;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: on %d\n", __func__, on);

	if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
		return -EBUSY;
	ret = aw36518_led_on(flash, on);

	return ret;
}

static int aw36518_get_ctrl(struct v4l2_ctrl *ctrl, enum aw36518_led_id id)
{
	struct aw36518_led *led =
		container_of(ctrl->handler, struct aw36518_led, ctrls);
	struct aw36518_flash *flash =
		container_of(led, struct aw36518_flash, leds[id]);
	int ret = 0;
	struct i2c_client *client = flash->client;

	v4l2_dbg(1, debug, &flash->leds[id].sd,
		 "%s: id 0x%x\n", __func__, ctrl->id);

	mutex_lock(&flash->lock);
	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		ret = aw36518_get_fault(flash);
		ctrl->val = 0;
		if (ret & AW36518_REG_FL_SC)
			ctrl->val |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (ret & AW36518_REG_FL_OT)
			ctrl->val |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (ret & AW36518_REG_FL_TO)
			ctrl->val |= V4L2_FLASH_FAULT_TIMEOUT;
		if (ret & AW36518_REG_FL_OVP)
			ctrl->val |= V4L2_FLASH_FAULT_OVER_VOLTAGE;
		ret = 0;
		break;
	default:
		dev_err(&client->dev,
			 "ctrl 0x%x not supported\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&flash->lock);
	return ret;
}

static int aw36518_set_ctrl(struct v4l2_ctrl *ctrl, enum aw36518_led_id id)
{
	struct aw36518_led *led =
		container_of(ctrl->handler, struct aw36518_led, ctrls);
	struct aw36518_flash *flash =
		container_of(led, struct aw36518_flash, leds[id]);
	int ret = 0;
	struct i2c_client *client = flash->client;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: id 0x%x val 0x%x\n",
		 __func__, ctrl->id, ctrl->val);

	mutex_lock(&flash->lock);

	ret = aw36518_get_fault(flash);
	if ((ret & (AW36518_REG_FL_OVP |
		     AW36518_REG_FL_OT |
		     AW36518_REG_FL_SC)) &&
	    (ctrl->id == V4L2_CID_FLASH_STROBE ||
	     ctrl->id == V4L2_CID_FLASH_TORCH_INTENSITY ||
	     ctrl->id == V4L2_CID_FLASH_LED_MODE)) {
		ret = -EBUSY;
		goto err;
	}

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		ret = aw36518_set_mode(flash, id, ctrl->val);
		break;
	case V4L2_CID_FLASH_STROBE:
		ret = aw36518_strobe(flash, true);
		break;
	case V4L2_CID_FLASH_STROBE_STOP:
		ret = aw36518_strobe(flash, false);
		break;
	case V4L2_CID_FLASH_TIMEOUT:
		ret = aw36518_set_timeout(flash, ctrl->val);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ret = aw36518_flash_brt(flash, id);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		ret = aw36518_torch_brt(flash, id);
		break;
	default:
		dev_err(&client->dev,
			"ctrl 0x%x not supported\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

err:
	mutex_unlock(&flash->lock);
	return ret;
}

static int aw36518_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw36518_get_ctrl(ctrl, LED0);
}

static int aw36518_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw36518_set_ctrl(ctrl, LED0);
}

static int aw36518_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw36518_get_ctrl(ctrl, LED1);
}

static int aw36518_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return aw36518_set_ctrl(ctrl, LED1);
}

static const struct v4l2_ctrl_ops aw36518_ctrl_ops[LED_MAX] = {
	[LED0] = {
		.g_volatile_ctrl = aw36518_led0_get_ctrl,
		.s_ctrl = aw36518_led0_set_ctrl,
	},
	[LED1] = {
		.g_volatile_ctrl = aw36518_led1_get_ctrl,
		.s_ctrl = aw36518_led1_set_ctrl,
	}
};

static int aw36518_init_controls(struct aw36518_flash *flash,
				 enum aw36518_led_id id)
{
	struct v4l2_ctrl *fault;
	struct v4l2_ctrl_handler *hdl = &flash->leds[id].ctrls;
	const struct v4l2_ctrl_ops *ops = &aw36518_ctrl_ops[id];
	struct aw36518_led *led = &flash->leds[id];

	v4l2_ctrl_handler_init(hdl, 8);

	v4l2_ctrl_new_std_menu(hdl, ops,
			       V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7, 0);
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;

	v4l2_ctrl_new_std_menu(hdl, ops,
			       V4L2_CID_FLASH_STROBE_SOURCE,
			       V4L2_FLASH_STROBE_SOURCE_SOFTWARE, ~0x1, 0);

	v4l2_ctrl_new_std(hdl, ops,
			  V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	v4l2_ctrl_new_std(hdl, ops,
			  V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	v4l2_ctrl_new_std(hdl, ops,
			  V4L2_CID_FLASH_TIMEOUT, TIMEOUT_MIN,
			  led->max_flash_timeout,
			  TIMEOUT_STEP,
			  led->max_flash_timeout);
	flash->flash_timeout = led->max_flash_timeout;

	flash->leds[id].flash_brt =
		v4l2_ctrl_new_std(hdl, ops,
				  V4L2_CID_FLASH_INTENSITY,
				  AW36518_MIN_FLASH_INTENSITY,
				  led->max_flash_intensity,
				  AW36518_FLASH_INTENSITY_STEP,
				  AW36518_FLASH_INTENSITY_DEFAULT);

	flash->leds[id].torch_brt =
		v4l2_ctrl_new_std(hdl, ops,
				  V4L2_CID_FLASH_TORCH_INTENSITY,
				  AW36518_MIN_TORCH_INTENSITY,
				  led->max_torch_intensity,
				  AW36518_TORCH_INTENSITY_STEP,
				  AW36518_TORCH_INTENSITY_DEFAULT);

	fault = v4l2_ctrl_new_std(hdl, ops,
				  V4L2_CID_FLASH_FAULT, 0,
				  V4L2_FLASH_FAULT_OVER_VOLTAGE
				  | V4L2_FLASH_FAULT_TIMEOUT
				  | V4L2_FLASH_FAULT_OVER_TEMPERATURE
				  | V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);

	if (hdl->error)
		return hdl->error;

	fault->flags |= V4L2_CTRL_FLAG_VOLATILE;

	flash->leds[id].sd.ctrl_handler = hdl;
	return 0;
}

static void aw36518_get_time_info(struct v4l2_subdev *sd,
				  struct old_timeval32 *compat_ti)
{
	struct aw36518_led *led =
		container_of(sd, struct aw36518_led, sd);

	memset(compat_ti, 0, sizeof(*compat_ti));
	compat_ti->tv_sec = led->timestamp.tv_sec;
	compat_ti->tv_usec = led->timestamp.tv_usec;
}

static long aw36518_ioctl(struct v4l2_subdev *sd,
			 unsigned int cmd, void *arg)
{
	long ret = 0;

	switch (cmd) {
	case RK_VIDIOC_FLASH_TIMEINFO:
		aw36518_get_time_info(sd, (struct old_timeval32 *)arg);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
#define RK_VIDIOC_COMPAT_FLASH_TIMEINFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct old_timeval32)

static long aw36518_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd,
				  unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct old_timeval32 *compat_t;
	long ret;

	switch (cmd) {
	case RK_VIDIOC_COMPAT_FLASH_TIMEINFO:
		compat_t = kzalloc(sizeof(*compat_t), GFP_KERNEL);
		if (!compat_t) {
			ret = -ENOMEM;
			return ret;
		}
		ret = aw36518_ioctl(sd, RK_VIDIOC_FLASH_TIMEINFO, compat_t);
		if (!ret) {
			ret = copy_to_user(up, compat_t, sizeof(*compat_t));
			if (ret)
				ret = -EFAULT;
		}
		kfree(compat_t);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int aw36518_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int aw36518_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static int aw36518_s_power(struct v4l2_subdev *sd, int on)
{
	struct aw36518_led *led = to_led(sd);
	struct aw36518_flash *flash =
		container_of(led, struct aw36518_flash, leds[0]);
	int ret = 0;
	struct i2c_client *client = flash->client;

	dev_info(&client->dev, "%s on(%d)\n", __func__, on);
	mutex_lock(&flash->lock);

	/* If the power state is not modified - no work to do. */
	if (flash->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		flash->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		flash->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&flash->lock);

	return ret;
}

static const struct v4l2_subdev_core_ops aw36518_core_ops = {
	.s_power = aw36518_s_power,
	.ioctl = aw36518_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = aw36518_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops aw36518_ops = {
	.core = &aw36518_core_ops,
};

static const struct v4l2_subdev_internal_ops aw36518_internal_ops = {
	.open = aw36518_open,
	.close = aw36518_close,
};

static int __aw36518_set_power(struct aw36518_flash *flash, bool on)
{
	gpiod_direction_output(flash->en_gpio, on);

	return 0;
}

static int aw36518_check_id(struct aw36518_flash *flash)
{
	int ret = 0;

	__aw36518_set_power(flash, true);
	ret = aw36518_i2c_read(flash, AW36518_REG_ID);
	if (ret != AW36518_ID) {
		dev_err(&flash->client->dev,
			"Read chip id error\n");
		return -ENODEV;
	}

	dev_info(&flash->client->dev,
		 "Detected aw36518 flash id:0x%x\n", ret);
	return 0;
}

static int aw36518_of_init(struct i2c_client *client,
			   struct aw36518_flash *flash)
{
	struct device_node *node = client->dev.of_node;
	struct device_node *child;
	struct aw36518_led *led;
	int ret = 0;

	if (!node) {
		dev_err(&client->dev,
			"get device node failed\n");
		goto err;
	}

	ret = of_property_read_u32(node,
				   RKMODULE_CAMERA_MODULE_INDEX,
				   &flash->module_index);
	ret |= of_property_read_string(node,
				       RKMODULE_CAMERA_MODULE_FACING,
				       &flash->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		goto err;
	}

	flash->en_gpio = devm_gpiod_get(&client->dev,
					"enable", GPIOD_OUT_LOW);
	if (IS_ERR(flash->en_gpio)) {
		flash->en_gpio = NULL;
		dev_warn(&client->dev,
			 "get enable-gpio failed, using assist light mode\n");
	}

	flash->torch_gpio = devm_gpiod_get(&client->dev,
					   "torch", GPIOD_OUT_LOW);
	if (IS_ERR(flash->torch_gpio)) {
		flash->torch_gpio = NULL;
		dev_warn(&client->dev,
			 "get torch-gpio failed, using assist light mode\n");
	}

	flash->strobe_gpio = devm_gpiod_get(&client->dev,
					    "strobe", GPIOD_OUT_LOW);
	if (IS_ERR(flash->strobe_gpio)) {
		flash->strobe_gpio = NULL;
		dev_warn(&client->dev,
			 "get strobe-gpio failed, using assist light mode\n");

	}
	flash->tx_gpio = devm_gpiod_get(&client->dev,
					    "tx", GPIOD_OUT_LOW);
	if (IS_ERR(flash->tx_gpio)) {
		flash->tx_gpio = NULL;
		dev_warn(&client->dev,
			 "get tx-gpio failed, using assist light mode\n");
	}

	for_each_child_of_node(node, child) {
		u32 id = 0;

		of_property_read_u32(child, "reg", &id);
		if (id >= LED_MAX) {
			dev_err(&client->dev, "only support 2 leds\n");
			goto err;
		}
		led = &flash->leds[id];
		led->sd.fwnode = of_fwnode_handle(child);
		if (of_property_read_u32(child, "flash-max-timeout-us",
					 &led->max_flash_timeout)) {
			dev_err(&client->dev,
				"get led%d flash-max-timeout-us fail\n", id);
			goto err;
		}
		if (led->max_flash_timeout > TIMEOUT_MAX)
			led->max_flash_timeout = TIMEOUT_MAX;

		if (of_property_read_u32(child, "flash-max-microamp",
					 &led->max_flash_intensity)) {
			dev_err(&client->dev,
				"get led%d flash-max-microamp fail\n", id);
			goto err;
		}
		if (led->max_flash_intensity > AW36518_MAX_FLASH_INTENSITY)
			led->max_flash_intensity = AW36518_MAX_FLASH_INTENSITY;

		if (of_property_read_u32(child, "led-max-microamp",
					  &led->max_torch_intensity)) {
			dev_err(&client->dev,
				"get led%d led-max-microamp fail\n", id);
			goto err;
		}
		if (led->max_torch_intensity > AW36518_MAX_TORCH_INTENSITY)
			led->max_torch_intensity = AW36518_MAX_TORCH_INTENSITY;

		v4l2_dbg(1, debug, &led->sd,
			 "led%d max torch:%dUA flash:%dUA timeout:%dUS\n",
			 id, led->max_torch_intensity,
			 led->max_flash_intensity,
			 led->max_flash_timeout);
	}

	return 0;
err:
	return -EINVAL;
}

static int aw36518_init_device(struct aw36518_flash *flash)
{
	int ret;
	unsigned int reg_val;
	struct i2c_client *client = flash->client;

	/* output disable */
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	ret = aw36518_set_mode(flash, 0, 0);
	if (ret < 0)
		return ret;
	/* reset faults */
	reg_val = aw36518_i2c_read(flash, AW36518_REG_FAULT);
	dev_info(&client->dev, "%s: fault: 0x%x.\n", __func__, reg_val);
	/* tx input default low */
	if (flash->tx_gpio)
		gpiod_direction_output(flash->tx_gpio, 0);
	/* STROBE/Torch input default low */
	if (flash->torch_gpio)
		gpiod_set_value_cansleep(flash->torch_gpio, 0);

	return ret;
}

static int aw36518_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct aw36518_flash *flash;
	struct v4l2_subdev *sd;
	char facing[2];
	int i, ret;

	dev_info(&client->dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return -ENOMEM;

	flash->client = client;

	ret = aw36518_of_init(client, flash);
	if (ret)
		return ret;

	ret = aw36518_check_id(flash);
	if (ret)
		goto err_power_off;

	for (i = 0; i < LED_MAX; i++) {
		sd = &flash->leds[i].sd;
		v4l2_i2c_subdev_init(sd, client, &aw36518_ops);
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		sd->internal_ops = &aw36518_internal_ops;

		memset(facing, 0, sizeof(facing));
		if (strcmp(flash->module_facing, "back") == 0)
			facing[0] = 'b';
		else
			facing[0] = 'f';
		/* NOTE: to distinguish between two led
		 * name: led0 meet the main led
		 * name: led1 meet the secondary led
		 */
		snprintf(sd->name, sizeof(sd->name),
			 "m%02d_%s_%s_led%d %s",
			 flash->module_index, facing,
			 AW36518_NAME, i, dev_name(sd->dev));

		ret = aw36518_init_controls(flash, i);
		if (ret)
			goto err;

		ret = media_entity_pads_init(&sd->entity, 0, NULL);
		if (ret < 0)
			goto free_ctl;

		sd->entity.function = MEDIA_ENT_F_FLASH;
		ret = v4l2_async_register_subdev(sd);
		if (ret)
			goto free_media;
	}

	ret = aw36518_init_device(flash);
	if (ret < 0)
		goto free_media;

	i2c_set_clientdata(client, flash);

	mutex_init(&flash->lock);

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;
free_media:
	media_entity_cleanup(&flash->leds[i].sd.entity);
free_ctl:
	v4l2_ctrl_handler_free(&flash->leds[i].ctrls);
err:
	for (--i; i >= 0; --i) {
		v4l2_device_unregister_subdev(&flash->leds[i].sd);
		media_entity_cleanup(&flash->leds[i].sd.entity);
		v4l2_ctrl_handler_free(&flash->leds[i].ctrls);
	}
err_power_off:
	__aw36518_set_power(flash, false);
	return ret;
}

static int aw36518_remove(struct i2c_client *client)
{
	struct aw36518_flash *flash = i2c_get_clientdata(client);
	unsigned int i;

	pm_runtime_disable(&client->dev);
	for (i = 0; i < LED_MAX; i++) {
		v4l2_device_unregister_subdev(&flash->leds[i].sd);
		v4l2_ctrl_handler_free(&flash->leds[i].ctrls);
		media_entity_cleanup(&flash->leds[i].sd.entity);
	}
	mutex_destroy(&flash->lock);
	return 0;
}

static int __maybe_unused aw36518_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw36518_flash *flash = i2c_get_clientdata(client);

	return __aw36518_set_power(flash, false);
}

static int __maybe_unused aw36518_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw36518_flash *flash = i2c_get_clientdata(client);

	return __aw36518_set_power(flash, true);
}

static const struct i2c_device_id aw36518_id_table[] = {
	{ AW36518_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, aw36518_id_table);

static const struct of_device_id aw36518_of_table[] = {
	{ .compatible = "awinic,aw36518" },
	{ { 0 } }
};

static const struct dev_pm_ops aw36518_pm_ops = {
	SET_RUNTIME_PM_OPS(aw36518_runtime_suspend,
			   aw36518_runtime_resume, NULL)
};

static struct i2c_driver aw36518_i2c_driver = {
	.driver	= {
		.name = AW36518_NAME,
		.pm = &aw36518_pm_ops,
		.of_match_table = aw36518_of_table,
	},
	.probe = aw36518_probe,
	.remove	= aw36518_remove,
	.id_table = aw36518_id_table,
};

module_i2c_driver(aw36518_i2c_driver);

MODULE_DESCRIPTION("AW36518 LED flash driver");
MODULE_LICENSE("GPL");
