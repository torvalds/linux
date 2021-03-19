// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd.

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/rk_vcm_head.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define VM149C_NAME			"vm149c"

#define VM149C_MAX_CURRENT		100U
#define VM149C_MAX_REG			1023U

#define VM149C_DEFAULT_START_CURRENT	0
#define VM149C_DEFAULT_RATED_CURRENT	100
#define VM149C_DEFAULT_STEP_MODE	4

/* vm149c device structure */
struct vm149c_device {
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
	struct rk_cam_vcm_cfg vcm_cfg;
	int max_ma;
};

static inline struct vm149c_device *to_vm149c_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct vm149c_device, ctrls_vcm);
}

static inline struct vm149c_device *sd_to_vm149c_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vm149c_device, sd);
}

static int vm149c_read_msg(
	struct i2c_client *client,
	unsigned char *msb, unsigned char *lsb)
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

static int vm149c_write_msg(
	struct i2c_client *client,
	u8 msb, u8 lsb)
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

static int vm149c_get_pos(
	struct vm149c_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret = 0;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int abs_step = 0;

	ret = vm149c_read_msg(client, &msb, &lsb);
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

static int vm149c_set_pos(
	struct vm149c_device *dev_vcm,
	unsigned int dest_pos)
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

	if (position > VM149C_MAX_REG)
		position = VM149C_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;
	msb = (0x00U | ((dev_vcm->current_lens_pos & 0x3F0U) >> 4U));
	lsb = (((dev_vcm->current_lens_pos & 0x0FU) << 4U) |
		dev_vcm->step_mode);
	ret = vm149c_write_msg(client, msb, lsb);
	if (ret != 0)
		goto err;

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int vm149c_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vm149c_device *dev_vcm = to_vm149c_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return vm149c_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int vm149c_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vm149c_device *dev_vcm = to_vm149c_vcm(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int dest_pos = ctrl->val;
	int move_pos;
	long int mv_us;
	int ret = 0;

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		if (dest_pos > VCMDRV_MAX_LOG) {
			dev_info(&client->dev,
				"%s dest_pos is error. %d > %d\n",
				__func__, dest_pos, VCMDRV_MAX_LOG);
			return -EINVAL;
		}
		/* calculate move time */
		move_pos = dev_vcm->current_related_pos - dest_pos;
		if (move_pos < 0)
			move_pos = -move_pos;

		ret = vm149c_set_pos(dev_vcm, dest_pos);

		dev_vcm->move_ms =
			((dev_vcm->vcm_movefull_t *
			(uint32_t)move_pos) /
			VCMDRV_MAX_LOG);
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

static const struct v4l2_ctrl_ops vm149c_vcm_ctrl_ops = {
	.g_volatile_ctrl = vm149c_get_ctrl,
	.s_ctrl = vm149c_set_ctrl,
};

static int vm149c_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int vm149c_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops vm149c_int_ops = {
	.open = vm149c_open,
	.close = vm149c_close,
};

static void vm149c_update_vcm_cfg(struct vm149c_device *dev_vcm)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int cur_dist;

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	cur_dist = dev_vcm->vcm_cfg.rated_ma - dev_vcm->vcm_cfg.start_ma;
	cur_dist = cur_dist * VM149C_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step = (cur_dist + (VCMDRV_MAX_LOG - 1)) / VCMDRV_MAX_LOG;
	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 VM149C_MAX_REG / dev_vcm->max_ma;
	dev_vcm->rated_current = dev_vcm->start_current +
				 VCMDRV_MAX_LOG * dev_vcm->step;
	dev_vcm->step_mode = dev_vcm->vcm_cfg.step_mode;

	dev_dbg(&client->dev,
		"vcm_cfg: %d, %d, %d, max_ma %d\n",
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->vcm_cfg.step_mode,
		dev_vcm->max_ma);
}

static long vm149c_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct vm149c_device *vm149c_dev = sd_to_vm149c_vcm(sd);
	struct rk_cam_vcm_tim *vcm_tim;
	struct rk_cam_vcm_cfg *vcm_cfg;
	int ret = 0;

	if (cmd == RK_VIDIOC_VCM_TIMEINFO) {
		vcm_tim = (struct rk_cam_vcm_tim *)arg;

		vcm_tim->vcm_start_t.tv_sec = vm149c_dev->start_move_tv.tv_sec;
		vcm_tim->vcm_start_t.tv_usec = vm149c_dev->start_move_tv.tv_usec;
		vcm_tim->vcm_end_t.tv_sec = vm149c_dev->end_move_tv.tv_sec;
		vcm_tim->vcm_end_t.tv_usec = vm149c_dev->end_move_tv.tv_usec;

		dev_dbg(&client->dev, "vm149c_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			vcm_tim->vcm_start_t.tv_sec, vcm_tim->vcm_start_t.tv_usec,
			vcm_tim->vcm_end_t.tv_sec, vcm_tim->vcm_end_t.tv_usec);
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		vcm_cfg->start_ma = vm149c_dev->vcm_cfg.start_ma;
		vcm_cfg->rated_ma = vm149c_dev->vcm_cfg.rated_ma;
		vcm_cfg->step_mode = vm149c_dev->vcm_cfg.step_mode;
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		vm149c_dev->vcm_cfg.start_ma = vcm_cfg->start_ma;
		vm149c_dev->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		vm149c_dev->vcm_cfg.step_mode = vcm_cfg->step_mode;
		vm149c_update_vcm_cfg(vm149c_dev);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long vm149c_compat_ioctl32(struct v4l2_subdev *sd, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rk_cam_compat_vcm_tim __user *p32 = compat_ptr(arg);
	struct rk_cam_compat_vcm_tim compat_vcm_tim;
	struct rk_cam_vcm_tim vcm_tim;
	struct rk_cam_vcm_cfg vcm_cfg;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		ret = vm149c_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
		compat_vcm_tim.vcm_start_t.tv_sec = vcm_tim.vcm_start_t.tv_sec;
		compat_vcm_tim.vcm_start_t.tv_usec = vcm_tim.vcm_start_t.tv_usec;
		compat_vcm_tim.vcm_end_t.tv_sec = vcm_tim.vcm_end_t.tv_sec;
		compat_vcm_tim.vcm_end_t.tv_usec = vcm_tim.vcm_end_t.tv_usec;

		put_user(compat_vcm_tim.vcm_start_t.tv_sec, &p32->vcm_start_t.tv_sec);
		put_user(compat_vcm_tim.vcm_start_t.tv_usec, &p32->vcm_start_t.tv_usec);
		put_user(compat_vcm_tim.vcm_end_t.tv_sec, &p32->vcm_end_t.tv_sec);
		put_user(compat_vcm_tim.vcm_end_t.tv_usec, &p32->vcm_end_t.tv_usec);
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		ret = vm149c_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret)
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = vm149c_ioctl(sd, cmd, &vcm_cfg);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops vm149c_core_ops = {
	.ioctl = vm149c_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = vm149c_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops vm149c_ops = {
	.core = &vm149c_core_ops,
};

static void vm149c_subdev_cleanup(struct vm149c_device *vm149c_dev)
{
	v4l2_device_unregister_subdev(&vm149c_dev->sd);
	v4l2_device_unregister(&vm149c_dev->vdev);
	v4l2_ctrl_handler_free(&vm149c_dev->ctrls_vcm);
	media_entity_cleanup(&vm149c_dev->sd.entity);
}

static int vm149c_init_controls(struct vm149c_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &vm149c_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, VCMDRV_MAX_LOG, 1, VCMDRV_MAX_LOG);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int vm149c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct vm149c_device *vm149c_dev;
	unsigned int max_ma, start_ma, rated_ma, step_mode;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(&client->dev, "probing...\n");
	dev_info(&client->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&max_ma)) {
		max_ma = VM149C_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (max_ma == 0)
		max_ma = VM149C_MAX_CURRENT;

	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&start_ma)) {
		start_ma = VM149C_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&rated_ma)) {
		rated_ma = VM149C_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&step_mode)) {
		step_mode = VM149C_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	vm149c_dev = devm_kzalloc(&client->dev, sizeof(*vm149c_dev),
				  GFP_KERNEL);
	if (vm149c_dev == NULL)
		return -ENOMEM;

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &vm149c_dev->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &vm149c_dev->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	v4l2_i2c_subdev_init(&vm149c_dev->sd, client, &vm149c_ops);
	vm149c_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	vm149c_dev->sd.internal_ops = &vm149c_int_ops;

	ret = vm149c_init_controls(vm149c_dev);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&vm149c_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	sd = &vm149c_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(vm149c_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 vm149c_dev->module_index, facing,
		 VM149C_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	vm149c_dev->max_ma = max_ma;
	vm149c_dev->vcm_cfg.start_ma = start_ma;
	vm149c_dev->vcm_cfg.rated_ma = rated_ma;
	vm149c_dev->vcm_cfg.step_mode = step_mode;
	vm149c_update_vcm_cfg(vm149c_dev);
	vm149c_dev->move_ms       = 0;
	vm149c_dev->current_related_pos = VCMDRV_MAX_LOG;
	vm149c_dev->start_move_tv = ns_to_timeval(ktime_get_ns());
	vm149c_dev->end_move_tv = ns_to_timeval(ktime_get_ns());
	if ((vm149c_dev->step_mode & 0x0c) != 0) {
		vm149c_dev->vcm_movefull_t =
			64 * (1 << (vm149c_dev->step_mode & 0x03)) * 1024 /
			((1 << (((vm149c_dev->step_mode & 0x0c) >> 2) - 1)) * 1000);
	} else {
		vm149c_dev->vcm_movefull_t = 64 * 1023 / 1000;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;

err_cleanup:
	vm149c_subdev_cleanup(vm149c_dev);
	dev_err(&client->dev, "Probe failed: %d\n", ret);
	return ret;
}

static int vm149c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct vm149c_device *vm149c_dev = sd_to_vm149c_vcm(sd);

	pm_runtime_disable(&client->dev);
	vm149c_subdev_cleanup(vm149c_dev);

	return 0;
}

static int __maybe_unused vm149c_vcm_suspend(struct device *dev)
{
	return 0;
}

static int  __maybe_unused vm149c_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct vm149c_device *vm149c_dev = sd_to_vm149c_vcm(sd);

	vm149c_set_pos(vm149c_dev, vm149c_dev->current_related_pos);
	return 0;
}

static const struct i2c_device_id vm149c_id_table[] = {
	{ VM149C_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, vm149c_id_table);

static const struct of_device_id vm149c_of_table[] = {
	{ .compatible = "silicon touch,vm149c" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, vm149c_of_table);

static const struct dev_pm_ops vm149c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vm149c_vcm_suspend, vm149c_vcm_resume)
	SET_RUNTIME_PM_OPS(vm149c_vcm_suspend, vm149c_vcm_resume, NULL)
};

static struct i2c_driver vm149c_i2c_driver = {
	.driver = {
		.name = VM149C_NAME,
		.pm = &vm149c_pm_ops,
		.of_match_table = vm149c_of_table,
	},
	.probe = &vm149c_probe,
	.remove = &vm149c_remove,
	.id_table = vm149c_id_table,
};

module_i2c_driver(vm149c_i2c_driver);

MODULE_DESCRIPTION("VM149C VCM driver");
MODULE_LICENSE("GPL v2");
