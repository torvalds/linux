// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip driver for IR-Cuter
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>

#define RK_IRCUT_NAME "ircut"
#define DRIVER_VERSION	KERNEL_VERSION(0, 0x01, 0x00)

//#define USED_SYS_DEBUG

enum IRCUT_STATE_e {
	IRCUT_STATE_CLOSED,
	IRCUT_STATE_CLOSING,
	IRCUT_STATE_OPENING,
	IRCUT_STATE_OPENED,
};

struct ircut_op_work {
	struct work_struct work;
	int op_cmd;
	struct ircut_dev *dev;
};

struct ircut_drv_data {
	int	(*parse_dt)(struct ircut_dev *ircut, struct device_node *node);
	void	(*ctrl)(struct ircut_dev *ircut, int cmd);
};

struct ircut_dev {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler ctrl_handler;
	struct device *dev;
	struct completion complete;
	/* state mutex */
	struct mutex mut_state;
	enum IRCUT_STATE_e state;
	struct workqueue_struct *wq;
	int pulse_width;
	int val;
	struct gpio_desc *open_gpio;
	struct gpio_desc *close_gpio;
	struct gpio_desc *led_gpio;
	u32 module_index;
	const char *module_facing;
	const struct ircut_drv_data *drv_data;
};

#define IRCUT_STATE_EQ(expected) \
	((ircut->state == (expected)) ? true : false)

static int ap1511a_parse_dt(struct ircut_dev *ircut, struct device_node *node)
{
	ircut->open_gpio = devm_gpiod_get(ircut->dev, "ircut-open", GPIOD_OUT_HIGH);
	if (IS_ERR(ircut->open_gpio)) {
		dev_err(ircut->dev, "Failed to get ircut-open-gpios\n");
		return PTR_ERR(ircut->open_gpio);
	}

	ircut->led_gpio = devm_gpiod_get_optional(ircut->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(ircut->led_gpio))
		dev_err(ircut->dev, "Failed to get led-gpios\n");

	return 0;
}

static void ap1511a_ctrl(struct ircut_dev *ircut, int cmd)
{
	if (cmd > 0) {
		gpiod_set_value_cansleep(ircut->open_gpio, 1);
		if (!IS_ERR(ircut->led_gpio))
			gpiod_set_value_cansleep(ircut->led_gpio, 0);
	} else {
		gpiod_set_value_cansleep(ircut->open_gpio, 0);
		if (!IS_ERR(ircut->led_gpio))
			gpiod_set_value_cansleep(ircut->led_gpio, 1);
	}
}

static int ba6208_parse_dt(struct ircut_dev *ircut, struct device_node *node)
{
	int ret;

	dev_dbg(ircut->dev, "ircut_gpio_parse_dt");
	ret = of_property_read_u32(node,
		"rockchip,pulse-width",
		&ircut->pulse_width);
	if (ret != 0) {
		ircut->pulse_width = 100;
		dev_err(ircut->dev,
			"failed get pulse-width,use dafult value 100\n");
	}
	if (ircut->pulse_width > 2000) {
		ircut->pulse_width = 300;
		dev_info(ircut->dev,
			"pulse width to long,use default dafult 300");
	}
	dev_dbg(ircut->dev, "pulse-width value from dts %d\n",
		ircut->pulse_width);
	/* get ircut open gpio */
	ircut->open_gpio = devm_gpiod_get(ircut->dev, "ircut-open", GPIOD_OUT_LOW);
	if (IS_ERR(ircut->open_gpio))
		dev_err(ircut->dev, "Failed to get ircut-open-gpios\n");

	/* get ircut close gpio */
	ircut->close_gpio = devm_gpiod_get(ircut->dev,
				"ircut-close", GPIOD_OUT_LOW);
	if (IS_ERR(ircut->close_gpio))
		dev_err(ircut->dev, "Failed to get ircut-close-gpios\n");

	/* get led gpio */
	ircut->led_gpio = devm_gpiod_get_optional(ircut->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(ircut->led_gpio))
		dev_err(ircut->dev, "Failed to get led-gpios\n");

	return 0;
}

static void ba6208_ctrl(struct ircut_dev *ircut, int cmd)
{
	if (cmd > 0) {
		if (!IS_ERR(ircut->open_gpio))
			gpiod_set_value_cansleep(ircut->open_gpio, 1);
		msleep(ircut->pulse_width);
		if (!IS_ERR(ircut->open_gpio))
			gpiod_set_value_cansleep(ircut->open_gpio, 0);
		if (!IS_ERR(ircut->led_gpio))
			gpiod_set_value_cansleep(ircut->led_gpio, 0);
	} else {
		if (!IS_ERR(ircut->close_gpio))
			gpiod_set_value_cansleep(ircut->close_gpio, 1);
		msleep(ircut->pulse_width);
		if (!IS_ERR(ircut->close_gpio))
			gpiod_set_value_cansleep(ircut->close_gpio, 0);
		if (!IS_ERR(ircut->led_gpio))
			gpiod_set_value_cansleep(ircut->led_gpio, 1);
	}
}

static void ircut_op_work(struct work_struct *work)
{
	struct ircut_op_work *wk =
		container_of(work, struct ircut_op_work, work);
	struct ircut_dev *ircut = wk->dev;
	enum IRCUT_STATE_e state;

	if (ircut->drv_data->ctrl)
		ircut->drv_data->ctrl(ircut, wk->op_cmd);

	state = (wk->op_cmd > 0) ? IRCUT_STATE_OPENED : IRCUT_STATE_CLOSED;
	mutex_lock(&ircut->mut_state);
	complete(&ircut->complete);
	ircut->state = state;
	mutex_unlock(&ircut->mut_state);
	kfree(wk);
	wk = NULL;
}

static int ircut_operation(struct ircut_dev *ircut, int op)
{
	struct ircut_op_work *wk = NULL;
	bool should_wait = false;
	bool do_nothing = false;
	enum IRCUT_STATE_e old_state;

	dev_dbg(ircut->dev, "%s, status %d\n", __func__, op);
	mutex_lock(&ircut->mut_state);
	old_state = ircut->state;
	if (op > 0) {
		/* check state */
		if (IRCUT_STATE_EQ(IRCUT_STATE_OPENING) ||
			IRCUT_STATE_EQ(IRCUT_STATE_OPENED)) {
			/* already in opening or opened state, do nothing */
			do_nothing = true;
		} else if (IRCUT_STATE_EQ(IRCUT_STATE_CLOSING)) {
			/* in closing state,should wait */
			should_wait = true;
		} else {
			/* in closed state,queue work */
		}
	} else {
		/* check state */
		if (IRCUT_STATE_EQ(IRCUT_STATE_CLOSING) ||
			IRCUT_STATE_EQ(IRCUT_STATE_CLOSED)) {
			/* already in closing or closed state, do nothing */
			do_nothing = true;
		} else if (IRCUT_STATE_EQ(IRCUT_STATE_OPENING)) {
			/* in opening state,should wait */
			should_wait = true;
		} else {
			/* in opened state,queue work */
		}
	}
	mutex_unlock(&ircut->mut_state);
	if (do_nothing)
		goto op_done;
	if (should_wait)
		wait_for_completion(&ircut->complete);
	wk = kmalloc(sizeof(*wk),
		GFP_KERNEL);
	if (!wk) {
		dev_err(ircut->dev, "failed to alloc ircut work struct\n");
		goto err_ircut_operation;
	}
	wk->op_cmd = op;
	wk->dev = ircut;
	mutex_lock(&ircut->mut_state);
	if (op > 0)
		ircut->state = IRCUT_STATE_OPENING;
	else
		ircut->state = IRCUT_STATE_CLOSING;
	reinit_completion(&ircut->complete);
	mutex_unlock(&ircut->mut_state);
	/* queue work */
	INIT_WORK(&wk->work, ircut_op_work);
	if (!queue_work(ircut->wq, &wk->work)) {
		dev_err(ircut->dev, "queue work failed\n");
		kfree(wk);
		ircut->state = old_state;
		goto err_ircut_operation;
	}
op_done:
	return 0;
err_ircut_operation:
	return -1;
}

#ifdef USED_SYS_DEBUG
static ssize_t set_ircut_status(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct ircut_dev *ircut = dev_get_drvdata(dev);
	int status = 0;
	int ret = 0;

	ret = sscanf(buf, "%d", &status);
	if (ret)
		ircut_operation(ircut, status);
	return count;
}

static struct device_attribute attributes[] = {
	__ATTR(sys_set_ircut, S_IWUSR, NULL, set_ircut_status),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
#endif

static int ircut_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = -EINVAL;
	struct ircut_dev *ircut = container_of(ctrl->handler,
					     struct ircut_dev, ctrl_handler);

	if (ctrl->id == V4L2_CID_BAND_STOP_FILTER) {
		ret = ircut_operation(ircut, ctrl->val);
		if (ret == 0)
			ircut->val = ctrl->val;
	}
	return ret;
}

static long ircut_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	return 0;
}

static const struct v4l2_subdev_core_ops ircut_core_ops = {
	.ioctl = ircut_ioctl,
};

static const struct v4l2_subdev_ops ircut_subdev_ops = {
	.core	= &ircut_core_ops,
};

static const struct v4l2_ctrl_ops ircut_ctrl_ops = {
	.s_ctrl = ircut_s_ctrl,
};

static const struct ircut_drv_data ap1511a_drv_data = {
	.parse_dt	= ap1511a_parse_dt,
	.ctrl		= ap1511a_ctrl,
};

static const struct ircut_drv_data ba6208_drv_data = {
	.parse_dt	= ba6208_parse_dt,
	.ctrl		= ba6208_ctrl,
};

#if defined(CONFIG_OF)
static const struct of_device_id ircut_of_match[] = {
	{ .compatible = "rockchip,ircut",
		.data = &ba6208_drv_data },
	{ .compatible = "ap1511a,ircut",
		.data = &ap1511a_drv_data },
	{},
};
MODULE_DEVICE_TABLE(of, ircut_of_match);
#endif

static int ircut_probe(struct platform_device *pdev)
{
	struct ircut_dev *ircut = NULL;
	struct device_node *node = pdev->dev.of_node;
	struct v4l2_ctrl_handler *handler;
	const struct of_device_id *match;
	int ret = 0;
	struct v4l2_subdev *sd;
	char facing[2];

	dev_info(&pdev->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);
	ircut = devm_kzalloc(&pdev->dev, sizeof(*ircut), GFP_KERNEL);
	if (!ircut) {
		dev_err(&pdev->dev, "alloc ircut failed\n");
		return -ENOMEM;
	}

	match = of_match_node(ircut_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	ircut->drv_data = match->data;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ircut->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ircut->module_facing);
	if (ret) {
		dev_err(&pdev->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	ircut->dev = &pdev->dev;
	mutex_init(&ircut->mut_state);
	init_completion(&ircut->complete);
	ircut->wq = alloc_workqueue("ircut wq",
			WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	v4l2_subdev_init(&ircut->sd, &ircut_subdev_ops);
	ircut->sd.owner = pdev->dev.driver->owner;
	ircut->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ircut->sd.dev = &pdev->dev;
	v4l2_set_subdevdata(&ircut->sd, pdev);
	platform_set_drvdata(pdev, &ircut->sd);

	handler = &ircut->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 1);
	if (ret)
		return ret;
	v4l2_ctrl_new_std(handler, &ircut_ctrl_ops,
		V4L2_CID_BAND_STOP_FILTER, 0, 4, 1, 1);
	if (handler->error) {
		ret = handler->error;
		dev_err(&pdev->dev, "Failed to init controls(%d)\n", ret);
		goto err_free;
	}
	ircut->sd.ctrl_handler = handler;
	ret = media_entity_pads_init(&ircut->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_free;

	if (ircut->drv_data->parse_dt) {
		ret = ircut->drv_data->parse_dt(ircut, node);
		if (ret)
			goto err_free;
	}

	sd = &ircut->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;
	sd->entity.flags = 1;

	memset(facing, 0, sizeof(facing));
	if (strcmp(ircut->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s",
		 ircut->module_index, facing,
		 RK_IRCUT_NAME);
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&pdev->dev, "v4l2 async register subdev failed\n");

	/* set default state to open */
	ircut_operation(ircut, 3);
	ircut->val = 3;
#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(ircut->dev);
#endif
	dev_info(&pdev->dev, "probe successful!");
	return 0;

err_free:
	v4l2_ctrl_handler_free(handler);
	v4l2_device_unregister_subdev(&ircut->sd);
	media_entity_cleanup(&ircut->sd.entity);
	return ret;
}

static int ircut_drv_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct ircut_dev *ircut = NULL;

	if (sd)
		ircut = v4l2_get_subdevdata(sd);
	if (ircut && ircut->wq)
		drain_workqueue(ircut->wq);
	if (sd)
		v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&ircut->ctrl_handler);
	media_entity_cleanup(&ircut->sd.entity);
	return 0;
}

static struct platform_driver ircut_driver = {
	.driver = {
		.name = RK_IRCUT_NAME,
		.of_match_table = of_match_ptr(ircut_of_match),
	},
	.probe = ircut_probe,
	.remove = ircut_drv_remove,
};

module_platform_driver(ircut_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rockchip-ircut");
MODULE_AUTHOR("ROCKCHIP");
