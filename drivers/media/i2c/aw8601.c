// SPDX-License-Identifier: GPL-2.0
/*
 * aw8601 vcm driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co., Ltd.
 */
//#define DEBUG
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/rk_vcm_head.h>
#include <linux/compat.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define AW8601_NAME			"aw8601"

#define AW8601_MAX_CURRENT		1023U
#define AW8601_MAX_REG			1023U

#define AW8601_DEFAULT_START_CURRENT	553
#define AW8601_DEFAULT_RATED_CURRENT	853
#define AW8601_DEFAULT_STEP_MODE	0x0
#define AW8601_DEFAULT_T_SRC		0x10
#define AW8601_DEFAULT_T_DIV		0x1
#define REG_NULL			0xFF

enum mode_e {
	ARC2_MODE,
	ARC3_MODE,
	ARC4_MODE,
	ARC5_MODE,
	DIRECT_MODE,
	LSC_MODE,
};

/* aw8601 device structure */
struct aw8601_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_ctrl *focus;
	struct v4l2_subdev sd;
	struct v4l2_device vdev;
	u16 current_val;

	unsigned short current_related_pos;
	unsigned short current_lens_pos;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step_mode;
	unsigned int vcm_movefull_t;
	unsigned int t_src;
	unsigned int t_div;
	unsigned int max_logicalpos;

	struct __kernel_old_timeval start_move_tv;
	struct __kernel_old_timeval end_move_tv;
	unsigned long move_us;

	u32 module_index;
	const char *module_facing;
	struct rk_cam_vcm_cfg vcm_cfg;
	int max_ma;
};

static inline struct aw8601_device *to_aw8601_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct aw8601_device, ctrls_vcm);
}

static inline struct aw8601_device *sd_to_aw8601_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct aw8601_device, sd);
}

static int aw8601_write_reg(struct i2c_client *client, u8 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[5];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 1;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 1) != len + 1) {
		dev_err(&client->dev, "Failed to write 0x%04x,0x%x\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int aw8601_read_reg(struct i2c_client *client,
			    u8 reg,
			    unsigned int len,
			    u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = (u8 *)&reg;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static unsigned int aw8601_move_time_div(struct aw8601_device *dev_vcm,
					 unsigned int move_time_us)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int move_time = 0;

	switch (dev_vcm->t_div) {
	case 0:
		move_time = move_time_us * 2;
		break;
	case 1:
		move_time = move_time_us;
		break;
	case 2:
		move_time = move_time_us / 2;
		break;
	case 3:
		move_time = move_time_us / 4;
		break;
	case 4:
		move_time = move_time_us * 8;
		break;
	case 5:
		move_time = move_time_us * 4;
		break;
	default:
		dev_err(&client->dev,
			"%s: t_div parameter err %d\n",
			__func__, dev_vcm->t_div);
		break;
	}
	return move_time;
}

static unsigned int aw8601_move_time(struct aw8601_device *dev_vcm,
	unsigned int move_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int move_time_us = 0;

	switch (dev_vcm->step_mode) {
	case LSC_MODE:
		move_time_us = 252 + dev_vcm->t_src * 4;
		move_time_us = move_time_us * move_pos;
		break;
	case ARC2_MODE:
	case ARC3_MODE:
	case ARC4_MODE:
	case ARC5_MODE:
		move_time_us = 6300 + dev_vcm->t_src * 100;
		move_time_us = aw8601_move_time_div(dev_vcm, move_time_us);
		break;
	case DIRECT_MODE:
		move_time_us = 30000;
		break;
	default:
		dev_err(&client->dev,
			"%s: step_mode is error %d\n",
			__func__, dev_vcm->step_mode);
		break;
	}

	return move_time_us;
}

static int aw8601_get_pos(struct aw8601_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int abs_step, range;
	int ret;

	range = dev_vcm->rated_current - dev_vcm->start_current;
	ret = aw8601_read_reg(client, 0x03, 2, &abs_step);
	if (ret != 0)
		goto err;

	if (abs_step <= dev_vcm->start_current) {
		abs_step = dev_vcm->max_logicalpos;
	} else if ((abs_step > dev_vcm->start_current) &&
		 (abs_step <= dev_vcm->rated_current)) {
		abs_step = (abs_step - dev_vcm->start_current) * dev_vcm->max_logicalpos / range;
		abs_step = dev_vcm->max_logicalpos - abs_step;
	} else {
		abs_step = 0;
	}

	*cur_pos = abs_step;
	dev_dbg(&client->dev, "%s: get position %d\n", __func__, *cur_pos);
	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int aw8601_set_pos(struct aw8601_device *dev_vcm,
	unsigned int dest_pos)
{
	int ret;
	unsigned int position = 0;
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	u32 is_busy, i;
	u32 range;

	range = dev_vcm->rated_current - dev_vcm->start_current;
	if (dest_pos >= dev_vcm->max_logicalpos)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (range * (dev_vcm->max_logicalpos - dest_pos) / dev_vcm->max_logicalpos);

	if (position > AW8601_MAX_REG)
		position = AW8601_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;
	for (i = 0; i < 500; i++) {
		ret = aw8601_read_reg(client, 0x05, 1, &is_busy);
		if (!ret && !(is_busy & 0x01))
			break;
		usleep_range(100, 200);
	}
	ret = aw8601_write_reg(client, 0x03, 2, dev_vcm->current_lens_pos);
	if (ret != 0)
		goto err;
	dev_dbg(&client->dev,
		"%s: set reg val %d\n", __func__, dev_vcm->current_lens_pos);
	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int aw8601_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct aw8601_device *dev_vcm = to_aw8601_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return aw8601_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int aw8601_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct aw8601_device *dev_vcm = to_aw8601_vcm(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int dest_pos = ctrl->val;
	int move_pos;
	long mv_us;
	int ret = 0;

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		if (dest_pos > dev_vcm->max_logicalpos) {
			dev_info(&client->dev,
				"%s dest_pos is error. %d > %d\n",
				__func__, dest_pos, dev_vcm->max_logicalpos);
			return -EINVAL;
		}
		/* calculate move time */
		move_pos = dev_vcm->current_related_pos - dest_pos;
		if (move_pos < 0)
			move_pos = -move_pos;

		ret = aw8601_set_pos(dev_vcm, dest_pos);

		if (dev_vcm->step_mode == LSC_MODE)
			dev_vcm->move_us = ((dev_vcm->vcm_movefull_t * (uint32_t)move_pos) /
					   dev_vcm->max_logicalpos);
		else
			dev_vcm->move_us = dev_vcm->vcm_movefull_t;

		dev_dbg(&client->dev,
			"dest_pos %d, move_us %ld\n",
			dest_pos, dev_vcm->move_us);

		dev_vcm->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
		mv_us = dev_vcm->start_move_tv.tv_usec +
				dev_vcm->move_us;
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

static const struct v4l2_ctrl_ops aw8601_vcm_ctrl_ops = {
	.g_volatile_ctrl = aw8601_get_ctrl,
	.s_ctrl = aw8601_set_ctrl,
};

static int aw8601_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int aw8601_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops aw8601_int_ops = {
	.open = aw8601_open,
	.close = aw8601_close,
};

static void aw8601_update_vcm_cfg(struct aw8601_device *dev_vcm)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 AW8601_MAX_REG / dev_vcm->max_ma;
	dev_vcm->rated_current = dev_vcm->vcm_cfg.rated_ma *
				 AW8601_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step_mode = dev_vcm->vcm_cfg.step_mode;

	dev_info(&client->dev,
		"vcm_cfg: %d, %d, %d, max_ma %d\n",
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->vcm_cfg.step_mode,
		dev_vcm->max_ma);
}

static long aw8601_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct aw8601_device *dev_vcm = sd_to_aw8601_vcm(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rk_cam_vcm_tim *vcm_tim;
	struct rk_cam_vcm_cfg *vcm_cfg;
	unsigned int max_logicalpos;
	int ret = 0;

	if (cmd == RK_VIDIOC_VCM_TIMEINFO) {
		vcm_tim = (struct rk_cam_vcm_tim *)arg;

		vcm_tim->vcm_start_t.tv_sec = dev_vcm->start_move_tv.tv_sec;
		vcm_tim->vcm_start_t.tv_usec =
				dev_vcm->start_move_tv.tv_usec;
		vcm_tim->vcm_end_t.tv_sec = dev_vcm->end_move_tv.tv_sec;
		vcm_tim->vcm_end_t.tv_usec = dev_vcm->end_move_tv.tv_usec;

		dev_dbg(&client->dev, "aw8601_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			vcm_tim->vcm_start_t.tv_sec,
			vcm_tim->vcm_start_t.tv_usec,
			vcm_tim->vcm_end_t.tv_sec,
			vcm_tim->vcm_end_t.tv_usec);
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		vcm_cfg->start_ma = dev_vcm->vcm_cfg.start_ma;
		vcm_cfg->rated_ma = dev_vcm->vcm_cfg.rated_ma;
		vcm_cfg->step_mode = dev_vcm->vcm_cfg.step_mode;
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		if (vcm_cfg->start_ma == 0 && vcm_cfg->rated_ma == 0) {
			dev_err(&client->dev,
				"vcm_cfg err, start_ma %d, rated_ma %d\n",
				vcm_cfg->start_ma, vcm_cfg->rated_ma);
			return -EINVAL;
		}
		dev_vcm->vcm_cfg.start_ma = vcm_cfg->start_ma;
		dev_vcm->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		dev_vcm->vcm_cfg.step_mode = vcm_cfg->step_mode;
		aw8601_update_vcm_cfg(dev_vcm);
	} else if (cmd == RK_VIDIOC_SET_VCM_MAX_LOGICALPOS) {
		max_logicalpos = *(unsigned int *)arg;

		if (max_logicalpos > 0) {
			dev_vcm->max_logicalpos = max_logicalpos;
			__v4l2_ctrl_modify_range(dev_vcm->focus,
				0, dev_vcm->max_logicalpos, 1, dev_vcm->max_logicalpos);
		}
		dev_dbg(&client->dev,
			"max_logicalpos %d\n", max_logicalpos);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long aw8601_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	void __user *up = compat_ptr(arg);
	struct rk_cam_compat_vcm_tim compat_vcm_tim;
	struct rk_cam_vcm_tim vcm_tim;
	struct rk_cam_vcm_cfg vcm_cfg;
	unsigned int max_logicalpos;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		struct rk_cam_compat_vcm_tim __user *p32 = up;

		ret = aw8601_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
		compat_vcm_tim.vcm_start_t.tv_sec = vcm_tim.vcm_start_t.tv_sec;
		compat_vcm_tim.vcm_start_t.tv_usec = vcm_tim.vcm_start_t.tv_usec;
		compat_vcm_tim.vcm_end_t.tv_sec = vcm_tim.vcm_end_t.tv_sec;
		compat_vcm_tim.vcm_end_t.tv_usec = vcm_tim.vcm_end_t.tv_usec;

		put_user(compat_vcm_tim.vcm_start_t.tv_sec,
			&p32->vcm_start_t.tv_sec);
		put_user(compat_vcm_tim.vcm_start_t.tv_usec,
			&p32->vcm_start_t.tv_usec);
		put_user(compat_vcm_tim.vcm_end_t.tv_sec,
			&p32->vcm_end_t.tv_sec);
		put_user(compat_vcm_tim.vcm_end_t.tv_usec,
			&p32->vcm_end_t.tv_usec);
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		ret = aw8601_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret) {
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
			if (ret)
				ret = -EFAULT;
		}
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = aw8601_ioctl(sd, cmd, &vcm_cfg);
		else
			ret = -EFAULT;
	} else if (cmd == RK_VIDIOC_SET_VCM_MAX_LOGICALPOS) {
		ret = copy_from_user(&max_logicalpos, up, sizeof(max_logicalpos));
		if (!ret)
			ret = aw8601_ioctl(sd, cmd, &max_logicalpos);
		else
			ret = -EFAULT;
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops aw8601_core_ops = {
	.ioctl = aw8601_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = aw8601_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops aw8601_ops = {
	.core = &aw8601_core_ops,
};

static void aw8601_subdev_cleanup(struct aw8601_device *aw8601_dev)
{
	v4l2_device_unregister_subdev(&aw8601_dev->sd);
	v4l2_device_unregister(&aw8601_dev->vdev);
	v4l2_ctrl_handler_free(&aw8601_dev->ctrls_vcm);
	media_entity_cleanup(&aw8601_dev->sd.entity);
}

static int aw8601_init_controls(struct aw8601_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &aw8601_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	dev_vcm->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
					   0, dev_vcm->max_logicalpos, 1, 0);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int aw8601_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct aw8601_device *aw8601_dev;
	unsigned int max_ma, start_ma, rated_ma, step_mode;
	unsigned int t_src, t_div;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(&client->dev, "probing...\n");
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&max_ma)) {
		max_ma = AW8601_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (max_ma == 0)
		max_ma = AW8601_MAX_CURRENT;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&start_ma)) {
		start_ma = AW8601_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&rated_ma)) {
		rated_ma = AW8601_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&step_mode)) {
		step_mode = AW8601_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_SRC,
		(unsigned int *)&t_src)) {
		t_src = AW8601_DEFAULT_T_SRC;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_SRC);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_DIV,
		(unsigned int *)&t_div)) {
		t_div = AW8601_DEFAULT_T_DIV;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_DIV);
	}

	aw8601_dev = devm_kzalloc(&client->dev, sizeof(*aw8601_dev),
				  GFP_KERNEL);
	if (aw8601_dev == NULL)
		return -ENOMEM;

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &aw8601_dev->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &aw8601_dev->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	v4l2_i2c_subdev_init(&aw8601_dev->sd, client, &aw8601_ops);
	aw8601_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	aw8601_dev->sd.internal_ops = &aw8601_int_ops;

	aw8601_dev->max_logicalpos = VCMDRV_MAX_LOG;
	ret = aw8601_init_controls(aw8601_dev);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&aw8601_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	sd = &aw8601_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(aw8601_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 aw8601_dev->module_index, facing,
		 AW8601_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	aw8601_dev->max_ma = max_ma;
	aw8601_dev->vcm_cfg.start_ma = start_ma;
	aw8601_dev->vcm_cfg.rated_ma = rated_ma;
	aw8601_dev->vcm_cfg.step_mode = step_mode;
	aw8601_update_vcm_cfg(aw8601_dev);
	aw8601_dev->move_us	= 0;
	aw8601_dev->current_related_pos = aw8601_dev->max_logicalpos;
	aw8601_dev->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	aw8601_dev->end_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());

	aw8601_dev->t_src = t_src;
	aw8601_dev->t_div = t_div;

	aw8601_dev->vcm_movefull_t =
		aw8601_move_time(aw8601_dev, AW8601_MAX_REG);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;

err_cleanup:
	aw8601_subdev_cleanup(aw8601_dev);
	dev_err(&client->dev, "Probe failed: %d\n", ret);
	return ret;
}

static int aw8601_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct aw8601_device *aw8601_dev = sd_to_aw8601_vcm(sd);

	pm_runtime_disable(&client->dev);
	aw8601_subdev_cleanup(aw8601_dev);

	return 0;
}

static int aw8601_init(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct aw8601_device *dev_vcm = sd_to_aw8601_vcm(sd);
	int ret = 0;
	u32 ring = 0;
	u32 mode_val = 0;
	u32 algo_time = 0;

	usleep_range(1000, 2000);

	ret = aw8601_write_reg(client, 0x02, 1, 0x01);
	if (ret)
		goto err;
	usleep_range(100, 200);
	ret = aw8601_write_reg(client, 0x02, 1, 0x00);
	if (ret)
		goto err;
	usleep_range(100, 200);

	if (dev_vcm->step_mode != DIRECT_MODE &&
	    dev_vcm->step_mode != LSC_MODE)
		ring = 0x02;
	ret = aw8601_write_reg(client, 0x02, 1, ring);
	if (ret)
		goto err;
	switch (dev_vcm->step_mode) {
	case ARC2_MODE:
	case ARC3_MODE:
	case ARC4_MODE:
	case ARC5_MODE:
		mode_val |= dev_vcm->step_mode << 6;
		break;
	case LSC_MODE:
		mode_val |= 0x10 << 6;
		break;
	default:
		break;
	}
	mode_val |= ((dev_vcm->t_div >> 2) & 0x01);
	algo_time = dev_vcm->t_div << 6 | dev_vcm->t_src;
	ret = aw8601_write_reg(client, 0x06, 1, mode_val);
	if (ret)
		goto err;
	ret = aw8601_write_reg(client, 0x07, 1, algo_time);
	if (ret)
		goto err;
	usleep_range(100, 200);

	return 0;
err:
	dev_err(&client->dev, "failed with error %d\n", ret);
	return -1;
}

static int __maybe_unused aw8601_vcm_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused aw8601_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct aw8601_device *dev_vcm = sd_to_aw8601_vcm(sd);

	aw8601_init(client);
	aw8601_set_pos(dev_vcm, dev_vcm->current_related_pos);
	return 0;
}

static const struct i2c_device_id aw8601_id_table[] = {
	{ AW8601_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, aw8601_id_table);

static const struct of_device_id aw8601_of_table[] = {
	{ .compatible = "awinic,aw8601" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, aw8601_of_table);

static const struct dev_pm_ops aw8601_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(aw8601_vcm_suspend, aw8601_vcm_resume)
	SET_RUNTIME_PM_OPS(aw8601_vcm_suspend, aw8601_vcm_resume, NULL)
};

static struct i2c_driver aw8601_i2c_driver = {
	.driver = {
		.name = AW8601_NAME,
		.pm = &aw8601_pm_ops,
		.of_match_table = aw8601_of_table,
	},
	.probe = &aw8601_probe,
	.remove = &aw8601_remove,
	.id_table = aw8601_id_table,
};

module_i2c_driver(aw8601_i2c_driver);

MODULE_DESCRIPTION("AW8601 VCM driver");
MODULE_LICENSE("GPL v2");
