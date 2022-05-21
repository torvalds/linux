// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd.

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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define SGM3784_NAME			"sgm3784"

#define SGM3784_REG_ID			0x00
#define SGM3784_ID			0x18

#define SGM3784_REG_MODE		0x01
#define SGM3784_REG_LED0_FLASH_CUR	0x06
#define SGM3784_REG_LED0_TORCH_CUR	0x08
#define SGM3784_REG_LED1_FLASH_CUR	0x09
#define SGM3784_REG_LED1_TORCH_CUR	0x0b

#define SGM3784_REG_FAULT		0x0c
#define SGM3784_REG_FL_OVP		BIT(7)
#define SGM3784_REG_FL_SC		BIT(6)
#define SGM3784_REG_FL_OT		BIT(5)
#define SGM3784_REG_FL_TO		BIT(4)

#define SGM3784_REG_ENABLE		0x0f
#define SGM3784_ON			0x03
#define SGM3784_OFF			0x00

#define SGM3784_MIN_FLASH_INTENSITY	0
#define SGM3784_MAX_FLASH_INTENSITY	1122000
#define SGM3784_FLASH_INTENSITY_DEFAULT	748000

#define SGM3784_MIN_TORCH_INTENSITY	0
#define SGM3784_MAX_TORCH_INTENSITY	299200
#define SGM3784_TORCH_INTENSITY_DEFAULT	74800
#define SGM3784_INTENSITY_STEP		18700
#define TIMEOUT_MAX			1600000
#define TIMEOUT_STEP			100000
#define TIMEOUT_MIN			100000

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

enum sgm3784_led_id {
	LED0 = 0,
	LED1,
	LED_MAX
};

struct sgm3784_led {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *flash_brt;
	struct v4l2_ctrl *torch_brt;
	struct __kernel_old_timeval timestamp;
	u32 max_flash_timeout;
	u32 max_flash_intensity;
	u32 max_torch_intensity;
};

struct sgm3784_flash {
	struct i2c_client *client;
	struct sgm3784_led leds[LED_MAX];

	struct gpio_desc *en_gpio;
	struct gpio_desc *torch_gpio;
	struct gpio_desc *strobe_gpio;

	struct mutex lock;

	u32 flash_timeout;
	enum v4l2_flash_led_mode cur_mode;

	u32 module_index;
	const char *module_facing;
};

static int sgm3784_i2c_write(struct sgm3784_flash *flash, u8 reg, u8 val)
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

static int sgm3784_i2c_read(struct sgm3784_flash *flash, u8 reg)
{
	struct i2c_client *client = flash->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev,
			 "%s: reg:0x%x failed\n",
			 __func__, reg);

	v4l2_dbg(2, debug, &flash->leds[0].sd,
		 "%s: reg:0x%x val:0x%x\n",
		 __func__, reg, ret);
	return ret;
}

static int sgm3784_led_on(struct sgm3784_flash *flash, bool on)
{
	int ret;
	u8 val;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: on:%d\n", __func__, on);

	val = on ? SGM3784_ON : SGM3784_OFF;
	ret = sgm3784_i2c_write(flash, SGM3784_REG_ENABLE, val);
	flash->leds[0].timestamp = ns_to_kernel_old_timeval(ktime_get_ns());
	flash->leds[1].timestamp = flash->leds[0].timestamp;
	return ret;
}

static int sgm3784_get_fault(struct sgm3784_flash *flash)
{
	int fault = 0;

	fault = sgm3784_i2c_read(flash, SGM3784_REG_FAULT);
	if (fault < 0)
		return fault;

	if (!fault)
		return 0;

	v4l2_info(&flash->leds[0].sd,
		 "%s: 0x%x\n", __func__, fault);
	return fault;
}

static int sgm3784_set_timeout(struct sgm3784_flash *flash)
{
	int ret;
	u8 val;

	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: %d\n",
		 __func__, flash->flash_timeout);

	ret = sgm3784_i2c_read(flash, 0x02);
	if (ret != 0)
		return ret;

	val =  (flash->flash_timeout - TIMEOUT_MIN) / TIMEOUT_STEP;
	return sgm3784_i2c_write(flash, 0x02, val | (ret & 0xf0));
}

static int sgm3784_torch_brt(struct sgm3784_flash *flash,
			     enum sgm3784_led_id id)
{
	struct sgm3784_led *led = &flash->leds[id];
	u8 val, reg;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: %d\n", __func__, led->torch_brt->val);

	val = led->torch_brt->val / SGM3784_INTENSITY_STEP;
	reg = id ? SGM3784_REG_LED1_TORCH_CUR : SGM3784_REG_LED0_TORCH_CUR;
	return sgm3784_i2c_write(flash, reg, val);
}

static int sgm3784_flash_brt(struct sgm3784_flash *flash,
			     enum sgm3784_led_id id)
{
	struct sgm3784_led *led = &flash->leds[id];
	u8 val, reg;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: %d\n", __func__, led->flash_brt->val);

	val = led->flash_brt->val / SGM3784_INTENSITY_STEP;
	reg = id ? SGM3784_REG_LED1_FLASH_CUR : SGM3784_REG_LED0_FLASH_CUR;
	return sgm3784_i2c_write(flash, reg, val);
}

static int sgm3784_set_mode(struct sgm3784_flash *flash,
			    enum sgm3784_led_id id, unsigned int mode)
{
	int ret = 0;
	u8 val;

	v4l2_dbg(1, debug, &flash->leds[id].sd,
		 "%s: %d cur:%d\n", __func__,
		 mode, flash->cur_mode);

	if (flash->cur_mode == mode)
		return 0;

	sgm3784_led_on(flash, false);

	flash->cur_mode = mode;

	val =  (flash->flash_timeout - TIMEOUT_MIN) / TIMEOUT_STEP;
	if (mode == V4L2_FLASH_LED_MODE_FLASH) {
		if (flash->torch_gpio)
			gpiod_direction_output(flash->torch_gpio, 0);
		ret = sgm3784_i2c_write(flash, 0x01, 0xfb);
		ret |= sgm3784_i2c_write(flash, 0x02, val | 0xc0);
		ret |= sgm3784_i2c_write(flash, 0x03, 0x48);
		ret |= sgm3784_flash_brt(flash, LED0);
		ret |= sgm3784_flash_brt(flash, LED1);
		gpiod_direction_output(flash->strobe_gpio, 1);
	} else if (mode == V4L2_FLASH_LED_MODE_TORCH) {
		gpiod_direction_output(flash->strobe_gpio, 0);
		if (flash->torch_gpio) {
			/* torch mode */
			ret = sgm3784_i2c_write(flash, 0x01, 0xf8);
			ret |= sgm3784_i2c_write(flash, 0x02, val | 0xf0);
		} else {
			/* assist mode */
			ret = sgm3784_i2c_write(flash, 0x01, 0xfa);
			ret |= sgm3784_i2c_write(flash, 0x02, val | 0xc0);
		}
		ret |= sgm3784_i2c_write(flash, 0x03, 0x48);
		ret |= sgm3784_torch_brt(flash, LED0);
		ret |= sgm3784_torch_brt(flash, LED1);
		ret |= sgm3784_led_on(flash, true);
		if (flash->torch_gpio)
			gpiod_direction_output(flash->torch_gpio, 1);
	} else {
		ret = sgm3784_i2c_write(flash, 0x01, 0xf8);
		gpiod_direction_output(flash->strobe_gpio, 0);
		if (flash->torch_gpio)
			gpiod_direction_output(flash->torch_gpio, 0);
	}

	return ret;
}

static int sgm3784_strobe(struct sgm3784_flash *flash, bool on)
{
	v4l2_dbg(1, debug, &flash->leds[0].sd,
		 "%s: on %d\n", __func__, on);

	if (flash->cur_mode != V4L2_FLASH_LED_MODE_FLASH)
		return -EBUSY;

	return sgm3784_led_on(flash, on);
}

static int sgm3784_get_ctrl(struct v4l2_ctrl *ctrl, enum sgm3784_led_id id)
{
	struct sgm3784_led *led =
		container_of(ctrl->handler, struct sgm3784_led, ctrls);
	struct sgm3784_flash *flash =
		container_of(led, struct sgm3784_flash, leds[id]);
	int ret = 0;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: id 0x%x\n", __func__, ctrl->id);

	mutex_lock(&flash->lock);
	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		ret = sgm3784_get_fault(flash);
		ctrl->val = 0;
		if (ret & SGM3784_REG_FL_SC)
			ctrl->val |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (ret & SGM3784_REG_FL_OT)
			ctrl->val |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (ret & SGM3784_REG_FL_TO)
			ctrl->val |= V4L2_FLASH_FAULT_TIMEOUT;
		if (ret & SGM3784_REG_FL_OVP)
			ctrl->val |= V4L2_FLASH_FAULT_OVER_VOLTAGE;
		ret = 0;
		break;
	default:
		v4l2_err(&led->sd,
			 "ctrl 0x%x not supported\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&flash->lock);
	return ret;
}

static int sgm3784_set_ctrl(struct v4l2_ctrl *ctrl, enum sgm3784_led_id id)
{
	struct sgm3784_led *led =
		container_of(ctrl->handler, struct sgm3784_led, ctrls);
	struct sgm3784_flash *flash =
		container_of(led, struct sgm3784_flash, leds[id]);
	int ret = 0;

	v4l2_dbg(1, debug, &led->sd,
		 "%s: id 0x%x val 0x%x\n",
		 __func__, ctrl->id, ctrl->val);

	mutex_lock(&flash->lock);

	ret = sgm3784_get_fault(flash);
	if (ret != 0)
		goto err;
	if ((ret & (SGM3784_REG_FL_OVP |
		     SGM3784_REG_FL_OT |
		     SGM3784_REG_FL_SC)) &&
	    (ctrl->id == V4L2_CID_FLASH_STROBE ||
	     ctrl->id == V4L2_CID_FLASH_TORCH_INTENSITY ||
	     ctrl->id == V4L2_CID_FLASH_LED_MODE)) {
		ret = -EBUSY;
		goto err;
	}

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		ret = sgm3784_set_mode(flash, id, ctrl->val);
		break;
	case V4L2_CID_FLASH_STROBE:
		ret = sgm3784_strobe(flash, true);
		break;
	case V4L2_CID_FLASH_STROBE_STOP:
		ret = sgm3784_strobe(flash, false);
		break;
	case V4L2_CID_FLASH_TIMEOUT:
		ret = sgm3784_set_timeout(flash);
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ret = sgm3784_flash_brt(flash, id);
		break;
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		ret = sgm3784_torch_brt(flash, id);
		break;
	default:
		v4l2_err(&led->sd,
			"ctrl 0x%x not supported\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

err:
	mutex_unlock(&flash->lock);
	return ret;
}

static int sgm3784_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return sgm3784_get_ctrl(ctrl, LED0);
}

static int sgm3784_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return sgm3784_set_ctrl(ctrl, LED0);
}

static int sgm3784_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return sgm3784_get_ctrl(ctrl, LED1);
}

static int sgm3784_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return sgm3784_set_ctrl(ctrl, LED1);
}

static const struct v4l2_ctrl_ops sgm3784_ctrl_ops[LED_MAX] = {
	[LED0] = {
		.g_volatile_ctrl = sgm3784_led0_get_ctrl,
		.s_ctrl = sgm3784_led0_set_ctrl,
	},
	[LED1] = {
		.g_volatile_ctrl = sgm3784_led1_get_ctrl,
		.s_ctrl = sgm3784_led1_set_ctrl,
	}
};

static int sgm3784_init_controls(struct sgm3784_flash *flash,
				 enum sgm3784_led_id id)
{
	struct v4l2_ctrl *fault;
	struct v4l2_ctrl_handler *hdl = &flash->leds[id].ctrls;
	const struct v4l2_ctrl_ops *ops = &sgm3784_ctrl_ops[id];
	struct sgm3784_led *led = &flash->leds[id];

	v4l2_ctrl_handler_init(hdl, 8);

	v4l2_ctrl_new_std_menu(hdl, ops,
			       V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7, 0);
	flash->cur_mode = V4L2_FLASH_LED_MODE_NONE;

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
				  SGM3784_MIN_FLASH_INTENSITY,
				  led->max_flash_intensity,
				  SGM3784_INTENSITY_STEP,
				  SGM3784_FLASH_INTENSITY_DEFAULT);

	flash->leds[id].torch_brt =
		v4l2_ctrl_new_std(hdl, ops,
				  V4L2_CID_FLASH_TORCH_INTENSITY,
				  SGM3784_MIN_TORCH_INTENSITY,
				  led->max_torch_intensity,
				  SGM3784_INTENSITY_STEP,
				  SGM3784_TORCH_INTENSITY_DEFAULT);

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

static void sgm3784_get_time_info(struct v4l2_subdev *sd,
				  struct old_timeval32 *compat_ti)
{
	struct sgm3784_led *led =
		container_of(sd, struct sgm3784_led, sd);

	memset(compat_ti, 0, sizeof(*compat_ti));
	compat_ti->tv_sec = led->timestamp.tv_sec;
	compat_ti->tv_usec = led->timestamp.tv_usec;
}

static long sgm3784_ioctl(struct v4l2_subdev *sd,
			 unsigned int cmd, void *arg)
{
	long ret = 0;

	switch (cmd) {
	case RK_VIDIOC_FLASH_TIMEINFO:
		sgm3784_get_time_info(sd, (struct old_timeval32 *)arg);
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

static long sgm3784_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd,
				  unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct old_timeval32 *compat_t;
	long ret;

	v4l2_dbg(1, debug, sd,
		 "%s: cmd 0x%x\n", __func__, cmd);

	switch (cmd) {
	case RK_VIDIOC_COMPAT_FLASH_TIMEINFO:
		compat_t = kzalloc(sizeof(*compat_t), GFP_KERNEL);
		if (!compat_t) {
			ret = -ENOMEM;
			return ret;
		}
		ret = sgm3784_ioctl(sd, RK_VIDIOC_FLASH_TIMEINFO, compat_t);
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

static int sgm3784_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int sgm3784_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_core_ops sgm3784_core_ops = {
	.ioctl = sgm3784_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sgm3784_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops sgm3784_ops = {
	.core = &sgm3784_core_ops,
};

static const struct v4l2_subdev_internal_ops sgm3784_internal_ops = {
	.open = sgm3784_open,
	.close = sgm3784_close,
};

static int __sgm3784_set_power(struct sgm3784_flash *flash, bool on)
{
	gpiod_direction_output(flash->en_gpio, on);

	return 0;
}

static int sgm3784_check_id(struct sgm3784_flash *flash)
{
	int ret = 0;

	__sgm3784_set_power(flash, true);
	ret = sgm3784_i2c_read(flash, SGM3784_REG_ID);
	__sgm3784_set_power(flash, false);
	if (ret != SGM3784_ID) {
		dev_err(&flash->client->dev,
			"Read chip id error\n");
		return -ENODEV;
	}

	dev_info(&flash->client->dev,
		 "Detected sgm3784 flash id:0x%x\n", ret);
	return 0;
}

static int sgm3784_of_init(struct i2c_client *client,
			   struct sgm3784_flash *flash)
{
	struct device_node *node = client->dev.of_node;
	struct device_node *child;
	struct sgm3784_led *led;
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
		dev_err(&client->dev, "get enable-gpio failed\n");
		goto err;
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
	if (IS_ERR(flash->en_gpio)) {
		dev_err(&client->dev, "get strobe-gpio failed\n");
		goto err;
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
		if (led->max_flash_intensity > SGM3784_MAX_FLASH_INTENSITY)
			led->max_flash_intensity = SGM3784_MAX_FLASH_INTENSITY;

		if (of_property_read_u32(child, "led-max-microamp",
					  &led->max_torch_intensity)) {
			dev_err(&client->dev,
				"get led%d led-max-microamp fail\n", id);
			goto err;
		}
		if (led->max_torch_intensity > SGM3784_MAX_TORCH_INTENSITY)
			led->max_torch_intensity = SGM3784_MAX_TORCH_INTENSITY;

		dev_info(&client->dev,
			 "led%d max torch:%dUA flash:%dUA timeout:%dUS\n",
			 id, led->max_torch_intensity,
			 led->max_flash_intensity,
			 led->max_flash_timeout);
	}

	return 0;
err:
	return -EINVAL;
}

static int sgm3784_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct sgm3784_flash *flash;
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

	ret = sgm3784_of_init(client, flash);
	if (ret)
		return ret;

	ret = sgm3784_check_id(flash);
	if (ret)
		return ret;

	for (i = 0; i < LED_MAX; i++) {
		sd = &flash->leds[i].sd;
		v4l2_i2c_subdev_init(sd, client, &sgm3784_ops);
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		sd->internal_ops = &sgm3784_internal_ops;

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
			 SGM3784_NAME, i, dev_name(sd->dev));

		ret = sgm3784_init_controls(flash, i);
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
	return ret;
}

static int sgm3784_remove(struct i2c_client *client)
{
	struct sgm3784_flash *flash = i2c_get_clientdata(client);
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

static int __maybe_unused sgm3784_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm3784_flash *flash = i2c_get_clientdata(client);

	return __sgm3784_set_power(flash, false);
}

static int __maybe_unused sgm3784_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sgm3784_flash *flash = i2c_get_clientdata(client);

	return __sgm3784_set_power(flash, true);
}

static const struct i2c_device_id sgm3784_id_table[] = {
	{ SGM3784_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, sgm3784_id_table);

static const struct of_device_id sgm3784_of_table[] = {
	{ .compatible = "sgmicro,gsm3784" },
	{ { 0 } }
};

static const struct dev_pm_ops sgm3784_pm_ops = {
	SET_RUNTIME_PM_OPS(sgm3784_runtime_suspend,
			   sgm3784_runtime_resume, NULL)
};

static struct i2c_driver sgm3784_i2c_driver = {
	.driver	= {
		.name = SGM3784_NAME,
		.pm = &sgm3784_pm_ops,
		.of_match_table = sgm3784_of_table,
	},
	.probe = sgm3784_probe,
	.remove	= sgm3784_remove,
	.id_table = sgm3784_id_table,
};

module_i2c_driver(sgm3784_i2c_driver);

MODULE_DESCRIPTION("SGM3784 LED flash driver");
MODULE_LICENSE("GPL v2");
