// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "rk_vcm_head.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define GT9760S_NAME			"gt9760s"

#define GT9760S_MAX_CURRENT		120U
#define GT9760S_MAX_REG			1023U

#define GT9760S_DEFAULT_START_CURRENT	0
#define GT9760S_DEFAULT_RATED_CURRENT	120
#define GT9760S_DEFAULT_STEP_MODE	4

#define GT9760S_SEL_ON_BYTE1		0xEC
#define GT9760S_SEL_ON_BYTE2		0xA3
#define GT9760S_DVO_DLC_BYTE1		0xA1
#define GT9760S_DVO_DLC_BYTE2		0xD
#define GT9760S_T_SRC_BYTE1		0xF2
#define GT9760S_T_SRC_BYTE2		0xF8
#define GT9760S_SEL_OFF_BYTE1		0xDC
#define GT9760S_SEL_OFF_BYTE2		0x51

/* Time to move the motor, this is fixed in the DLC specific setting */
#define GT9760S_DLC_MOVE_MS		7

/* gt9760s device structure */
struct gt9760s_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_subdev sd;
	struct v4l2_device vdev;
	u16 current_val;

	unsigned short current_related_pos;
	unsigned short current_lens_pos;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step;
	unsigned int step_mode;
	unsigned int vcm_movefull_t;

	struct timeval start_move_tv;
	struct timeval end_move_tv;
	unsigned long move_ms;

	u32 module_index;
	const char *module_facing;
};

static inline struct gt9760s_device *to_vcm_dev(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct gt9760s_device, ctrls_vcm);
}

static inline struct gt9760s_device *sd_to_vcm_dev(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct gt9760s_device, sd);
}

static int gt9760s_read_msg(struct i2c_client *client, u8 *msb, u8 *lsb)
{
	int ret = 0;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retries;

	if (!client->adapter) {
		dev_err(&client->dev, "client->adapter NULL\n");
		return -ENODEV;
	}

	for (retries = 0; retries < 5; retries++) {
		msg->addr = client->addr;
		msg->flags = 1;
		msg->len = 2;
		msg->buf = data;

		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			dev_dbg(&client->dev,
				"%s: vcm i2c ok, addr 0x%x, data 0x%x, 0x%x\n",
				__func__, msg->addr, data[0], data[1]);

			*msb = data[0];
			*lsb = data[1];
			return 0;
		}

		dev_info(&client->dev,
			"retrying I2C... %d\n", retries);
		retries++;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(20));
	}
	dev_err(&client->dev,
		"%s: i2c write to failed with error %d\n", __func__, ret);
	return ret;
}

static int gt9760s_write_msg(struct i2c_client *client, u8 msb, u8 lsb)
{
	int ret = 0;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retries;

	if (!client->adapter) {
		dev_err(&client->dev, "client->adapter NULL\n");
		return -ENODEV;
	}

	for (retries = 0; retries < 5; retries++) {
		msg->addr = client->addr;
		msg->flags = 0;
		msg->len = 2;
		msg->buf = data;

		data[0] = msb;
		data[1] = lsb;

		ret = i2c_transfer(client->adapter, msg, 1);
		usleep_range(50, 100);

		if (ret == 1) {
			dev_dbg(&client->dev,
				"%s: vcm i2c ok, addr 0x%x, data 0x%x, 0x%x\n",
				__func__, msg->addr, data[0], data[1]);
			return 0;
		}

		dev_info(&client->dev,
			"retrying I2C... %d\n", retries);
		retries++;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(20));
	}
	dev_err(&client->dev,
		"i2c write to failed with error %d\n", ret);
	return ret;
}

static int gt9760s_init(struct i2c_client *client)
{
	int ret = 0;

	usleep_range(7000, 7500);

	ret = gt9760s_write_msg(client, GT9760S_SEL_ON_BYTE1,
				GT9760S_SEL_ON_BYTE2);
	if (ret)
		goto err;

	ret = gt9760s_write_msg(client, GT9760S_DVO_DLC_BYTE1,
				GT9760S_DVO_DLC_BYTE2);
	if (ret)
		goto err;

	ret = gt9760s_write_msg(client, GT9760S_T_SRC_BYTE1,
				GT9760S_T_SRC_BYTE2);
	if (ret)
		goto err;

	ret = gt9760s_write_msg(client, GT9760S_SEL_OFF_BYTE1,
				GT9760S_SEL_OFF_BYTE2);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&client->dev, "failed with error %d\n", ret);
	return -1;
}

static int gt9760s_get_pos(struct gt9760s_device *dev_vcm, u32 *cur_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret = 0;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int abs_step = 0;

	ret = gt9760s_read_msg(client, &msb, &lsb);
	if (ret != 0)
		goto err;

	abs_step = (((unsigned int)(msb & 0x3FU)) << 4U) |
		   (((unsigned int)lsb) >> 4U);
	if (abs_step <= dev_vcm->start_current)
		abs_step = VCMDRV_MAX_LOG;
	else if ((abs_step > dev_vcm->start_current) &&
		 (abs_step <= dev_vcm->rated_current))
		abs_step = (dev_vcm->rated_current - abs_step) / dev_vcm->step;
	else
		abs_step = 0;

	*cur_pos = abs_step;
	dev_dbg(&client->dev, "%s: get position %d\n", __func__, *cur_pos);
	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int gt9760s_set_pos(struct gt9760s_device *dev_vcm, u32 dest_pos)
{
	int ret = 0;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int position = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);

	if (dest_pos >= VCMDRV_MAX_LOG)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (dev_vcm->step * (VCMDRV_MAX_LOG - dest_pos));

	if (position > GT9760S_MAX_REG)
		position = GT9760S_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;
	msb = (0x00U | ((dev_vcm->current_lens_pos & 0x3F0U) >> 4U));
	lsb = (((dev_vcm->current_lens_pos & 0x0FU) << 4U) |
		dev_vcm->step_mode);
	ret = gt9760s_write_msg(client, msb, lsb);
	if (ret != 0)
		goto err;

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int gt9760s_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gt9760s_device *dev_vcm = to_vcm_dev(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return gt9760s_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int gt9760s_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gt9760s_device *dev_vcm = to_vcm_dev(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int dest_pos = ctrl->val;
	long mv_us;
	int ret = 0;

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		if (dest_pos > VCMDRV_MAX_LOG) {
			dev_info(&client->dev,
				"%s dest_pos is error. %d > %d\n",
				__func__, dest_pos, VCMDRV_MAX_LOG);
			return -EINVAL;
		}

		ret = gt9760s_set_pos(dev_vcm, dest_pos);

		dev_vcm->move_ms = GT9760S_DLC_MOVE_MS;
		dev_dbg(&client->dev, "dest_pos %d, move_ms %ld\n",
			dest_pos, dev_vcm->move_ms);

		dev_vcm->start_move_tv = ns_to_timeval(ktime_get_ns());
		mv_us = dev_vcm->start_move_tv.tv_usec +
			dev_vcm->move_ms * 1000;
		if (mv_us >= 1000000) {
			dev_vcm->end_move_tv.tv_sec =
				dev_vcm->start_move_tv.tv_sec + 1;
			dev_vcm->end_move_tv.tv_usec = mv_us - 1000000;
		} else {
			dev_vcm->end_move_tv.tv_sec =
				dev_vcm->start_move_tv.tv_sec;
			dev_vcm->end_move_tv.tv_usec = mv_us;
		}
	}

	return ret;
}

static const struct v4l2_ctrl_ops gt9760s_vcm_ctrl_ops = {
	.g_volatile_ctrl = gt9760s_get_ctrl,
	.s_ctrl = gt9760s_set_ctrl,
};

static int gt9760s_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int gt9760s_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops gt9760s_int_ops = {
	.open = gt9760s_open,
	.close = gt9760s_close,
};

static long gt9760s_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;
	struct rk_cam_vcm_tim *vcm_tim;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gt9760s_device *dev_vcm = sd_to_vcm_dev(sd);

	if (cmd == RK_VIDIOC_VCM_TIMEINFO) {
		vcm_tim = (struct rk_cam_vcm_tim *)arg;

		vcm_tim->vcm_start_t.tv_sec = dev_vcm->start_move_tv.tv_sec;
		vcm_tim->vcm_start_t.tv_usec = dev_vcm->start_move_tv.tv_usec;
		vcm_tim->vcm_end_t.tv_sec = dev_vcm->end_move_tv.tv_sec;
		vcm_tim->vcm_end_t.tv_usec = dev_vcm->end_move_tv.tv_usec;

		dev_dbg(&client->dev,
			"0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			vcm_tim->vcm_start_t.tv_sec,
			vcm_tim->vcm_start_t.tv_usec,
			vcm_tim->vcm_end_t.tv_sec,
			vcm_tim->vcm_end_t.tv_usec);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gt9760s_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	struct rk_cam_compat_vcm_tim __user *p32 = compat_ptr(arg);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rk_cam_compat_vcm_tim cmt;
	struct rk_cam_vcm_tim tim;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		ret = gt9760s_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &tim);
		cmt.vcm_start_t.tv_sec = tim.vcm_start_t.tv_sec;
		cmt.vcm_start_t.tv_usec = tim.vcm_start_t.tv_usec;
		cmt.vcm_end_t.tv_sec = tim.vcm_end_t.tv_sec;
		cmt.vcm_end_t.tv_usec = tim.vcm_end_t.tv_usec;

		put_user(cmt.vcm_start_t.tv_sec, &p32->vcm_start_t.tv_sec);
		put_user(cmt.vcm_start_t.tv_usec, &p32->vcm_start_t.tv_usec);
		put_user(cmt.vcm_end_t.tv_sec, &p32->vcm_end_t.tv_sec);
		put_user(cmt.vcm_end_t.tv_usec, &p32->vcm_end_t.tv_usec);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops gt9760s_core_ops = {
	.ioctl = gt9760s_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gt9760s_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops gt9760s_ops = {
	.core = &gt9760s_core_ops,
};

static void gt9760s_subdev_cleanup(struct gt9760s_device *dev_vcm)
{
	v4l2_device_unregister_subdev(&dev_vcm->sd);
	v4l2_device_unregister(&dev_vcm->vdev);
	v4l2_ctrl_handler_free(&dev_vcm->ctrls_vcm);
	media_entity_cleanup(&dev_vcm->sd.entity);
}

static int gt9760s_init_controls(struct gt9760s_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &gt9760s_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, VCMDRV_MAX_LOG, 1, VCMDRV_MAX_LOG);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int gt9760s_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct gt9760s_device *dev_vcm;
	int ret;
	int cur_dist;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step_mode;
	struct v4l2_subdev *sd;
	char facing[2];

	dev_info(&client->dev, "probing...\n");
	dev_info(&client->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&start_current)) {
		start_current = GT9760S_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&rated_current)) {
		rated_current = GT9760S_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&step_mode)) {
		step_mode = GT9760S_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	dev_vcm = devm_kzalloc(&client->dev, sizeof(*dev_vcm),
				  GFP_KERNEL);
	if (!dev_vcm)
		return -ENOMEM;

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &dev_vcm->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &dev_vcm->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	v4l2_i2c_subdev_init(&dev_vcm->sd, client, &gt9760s_ops);
	dev_vcm->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev_vcm->sd.internal_ops = &gt9760s_int_ops;

	ret = gt9760s_init_controls(dev_vcm);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&dev_vcm->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	sd = &dev_vcm->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(dev_vcm->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 dev_vcm->module_index, facing,
		 GT9760S_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	cur_dist = rated_current - start_current;
	cur_dist = cur_dist * GT9760S_MAX_REG / GT9760S_MAX_CURRENT;
	dev_vcm->step = (cur_dist + (VCMDRV_MAX_LOG - 1)) / VCMDRV_MAX_LOG;
	dev_vcm->start_current = start_current *
				 GT9760S_MAX_REG / GT9760S_MAX_CURRENT;
	dev_vcm->rated_current = dev_vcm->start_current +
				 VCMDRV_MAX_LOG * dev_vcm->step;
	dev_vcm->step_mode     = step_mode;
	dev_vcm->move_ms       = 0;
	dev_vcm->current_related_pos = VCMDRV_MAX_LOG;
	dev_vcm->start_move_tv = ns_to_timeval(ktime_get_ns());
	dev_vcm->end_move_tv = ns_to_timeval(ktime_get_ns());
	dev_vcm->vcm_movefull_t = GT9760S_DLC_MOVE_MS;

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;

err_cleanup:
	gt9760s_subdev_cleanup(dev_vcm);
	dev_err(&client->dev, "Probe failed: %d\n", ret);
	return ret;
}

static int gt9760s_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gt9760s_device *dev_vcm = sd_to_vcm_dev(sd);

	pm_runtime_disable(&client->dev);
	gt9760s_subdev_cleanup(dev_vcm);

	return 0;
}

static int __maybe_unused gt9760s_vcm_suspend(struct device *dev)
{
	return 0;
}

static int  __maybe_unused gt9760s_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	gt9760s_init(client);
	return 0;
}

static const struct i2c_device_id gt9760s_id_table[] = {
	{ GT9760S_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, gt9760s_id_table);

static const struct of_device_id gt9760s_of_table[] = {
	{ .compatible = "giantec semi,gt9760s" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, gt9760s_of_table);

static const struct dev_pm_ops gt9760s_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(gt9760s_vcm_suspend, gt9760s_vcm_resume)
	SET_RUNTIME_PM_OPS(gt9760s_vcm_suspend, gt9760s_vcm_resume, NULL)
};

static struct i2c_driver gt9760s_i2c_driver = {
	.driver = {
		.name = GT9760S_NAME,
		.pm = &gt9760s_pm_ops,
		.of_match_table = gt9760s_of_table,
	},
	.probe = &gt9760s_probe,
	.remove = &gt9760s_remove,
	.id_table = gt9760s_id_table,
};

module_i2c_driver(gt9760s_i2c_driver);

MODULE_DESCRIPTION("GT9760S VCM driver");
MODULE_LICENSE("GPL v2");
