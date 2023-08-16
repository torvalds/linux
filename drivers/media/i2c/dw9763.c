// SPDX-License-Identifier: GPL-2.0
/*
 * dw9763 vcm driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
// #define DEBUG
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
#include <linux/regulator/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x1)
#define DW9763_NAME			"dw9763"

#define DW9763_MAX_CURRENT		120U
#define DW9763_MAX_REG			1023U
#define DW9763_GRADUAL_MOVELENS_STEPS	32

#define DW9763_DEFAULT_START_CURRENT	20
#define DW9763_DEFAULT_RATED_CURRENT	90
#define DW9763_DEFAULT_STEP_MODE	0x3
#define DW9763_DEFAULT_T_SACT		0x20
#define DW9763_DEFAULT_T_DIV		0x1
#define REG_NULL			0xFF

#define DW9763_CHIP_ID			0xF9
#define DW9763_REG_CHIP_ID		0x00

enum mode_e {
	SAC1_MODE,
	SAC2_MODE,
	SAC2_5_MODE,
	SAC3_MODE,
	SAC4_MODE = 5,
	DIRECT_MODE,
};

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

/* dw9763 device structure */
struct dw9763_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct v4l2_device vdev;
	u16 current_val;

	struct gpio_desc *power_gpio;
	unsigned short current_related_pos;
	unsigned short current_lens_pos;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step;
	unsigned int step_mode;
	unsigned int vcm_movefull_t;
	unsigned int t_src;
	unsigned int t_div;

	struct __kernel_old_timeval start_move_tv;
	struct __kernel_old_timeval end_move_tv;
	unsigned long move_us;

	u32 module_index;
	const char *module_facing;
	struct rk_cam_vcm_cfg vcm_cfg;
	int max_ma;
	struct mutex lock;
	struct regulator *supply;
	bool power_on;
};

static inline struct dw9763_device *to_dw9763_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct dw9763_device, ctrls_vcm);
}

static inline struct dw9763_device *sd_to_dw9763_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9763_device, sd);
}

static int dw9763_write_reg(struct i2c_client *client, u8 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[5];
	u8 *val_p;
	__be32 val_be;
	struct dw9763_device *dev_vcm = i2c_get_clientdata(client);

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
	v4l2_dbg(1, debug, &dev_vcm->sd, "succeed to write 0x%04x,0x%x\n", reg, val);

	return 0;
}

static int dw9763_read_reg(struct i2c_client *client,
			    u8 reg,
			    unsigned int len,
			    u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	int ret;
	struct dw9763_device *dev_vcm = i2c_get_clientdata(client);

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

	v4l2_dbg(1, debug, &dev_vcm->sd, "succeed to read 0x%04x,0x%x\n", reg, *val);

	return 0;
}

static unsigned int dw9763_move_time_div(struct dw9763_device *dev_vcm,
					 unsigned int move_time_us)
{
	struct i2c_client *client = dev_vcm->client;
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
		dev_err(&client->dev, "%s: t_div parameter err %d\n", __func__, dev_vcm->t_div);
		move_time = move_time_us;
		break;
	}
	return move_time;
}

static unsigned int dw9763_move_time(struct dw9763_device *dev_vcm,
	unsigned int move_pos)
{
	struct i2c_client *client = dev_vcm->client;
	unsigned int move_time_us = 0;

	switch (dev_vcm->step_mode) {
	case SAC1_MODE:
	case SAC2_MODE:
	case SAC2_5_MODE:
	case SAC3_MODE:
	case SAC4_MODE:
		move_time_us = 6300 + dev_vcm->t_src * 100;
		move_time_us = dw9763_move_time_div(dev_vcm, move_time_us);
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

	v4l2_dbg(1, debug, &dev_vcm->sd,
		"%s: vcm_movefull_t is: %d us\n",
		__func__, move_time_us);

	return ((move_time_us + 500) / 1000);
}

static int dw9763_set_dac(struct dw9763_device *dev_vcm,
	unsigned int dest_dac)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret;
	u32 is_busy, i;

	for (i = 0; i < 10; i++) {
		ret = dw9763_read_reg(client, 0x05, 1, &is_busy);
		if (!ret && !(is_busy & 0x01))
			break;
		usleep_range(100, 200);
	}

	ret = dw9763_write_reg(client, 0x03, 2, dest_dac);
	if (ret != 0)
		goto err;
	v4l2_dbg(1, debug, &dev_vcm->sd,
		"%s: set reg val %d\n", __func__, dest_dac);

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9763_get_dac(struct dw9763_device *dev_vcm, unsigned int *cur_dac)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret;
	unsigned int abs_step;

	ret = dw9763_read_reg(client, 0x03, 2, &abs_step);
	if (ret != 0)
		goto err;

	*cur_dac = abs_step;
	v4l2_dbg(1, debug, &dev_vcm->sd, "%s: get dac %d\n", __func__, *cur_dac);

	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9763_get_pos(struct dw9763_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = dev_vcm->client;
	int ret;
	unsigned int abs_step;

	ret = dw9763_read_reg(client, 0x03, 2, &abs_step);
	if (ret != 0)
		goto err;

	if (abs_step <= dev_vcm->start_current)
		abs_step = VCMDRV_MAX_LOG;
	else if ((abs_step > dev_vcm->start_current) &&
		 (abs_step <= dev_vcm->rated_current))
		abs_step = (dev_vcm->rated_current - abs_step) / dev_vcm->step;
	else
		abs_step = 0;

	*cur_pos = abs_step;
	v4l2_dbg(1, debug, &dev_vcm->sd, "%s: get position %d\n", __func__, *cur_pos);
	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9763_set_pos(struct dw9763_device *dev_vcm,
	unsigned int dest_pos)
{
	int ret;
	unsigned int position = 0;

	if (dest_pos >= VCMDRV_MAX_LOG)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (dev_vcm->step * (VCMDRV_MAX_LOG - dest_pos));

	if (position > DW9763_MAX_REG)
		position = DW9763_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;

	ret = dw9763_set_dac(dev_vcm, position);
	v4l2_dbg(1, debug, &dev_vcm->sd, "%s: set position %d, dac %d\n",
		 __func__, dest_pos, position);

	return ret;
}

static int dw9763_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9763_device *dev_vcm = to_dw9763_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return dw9763_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int dw9763_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9763_device *dev_vcm = to_dw9763_vcm(ctrl);
	struct i2c_client *client = dev_vcm->client;
	unsigned int dest_pos = ctrl->val;
	long mv_us;
	int ret = 0;

	v4l2_dbg(1, debug, &dev_vcm->sd, "ctrl->id: 0x%x, ctrl->val: 0x%x\n",
		ctrl->id, ctrl->val);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {

		if (dest_pos > VCMDRV_MAX_LOG) {
			dev_info(&client->dev,
				"%s dest_pos is error. %d > %d\n",
				__func__, dest_pos, VCMDRV_MAX_LOG);
			return -EINVAL;
		}

		ret = dw9763_set_pos(dev_vcm, dest_pos);

		dev_vcm->move_us = dev_vcm->vcm_movefull_t * 1000;

		v4l2_dbg(1, debug, &dev_vcm->sd,
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

static const struct v4l2_ctrl_ops dw9763_vcm_ctrl_ops = {
	.g_volatile_ctrl = dw9763_get_ctrl,
	.s_ctrl = dw9763_set_ctrl,
};

static int dw9763_init(struct i2c_client *client);
static int dw9763_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	unsigned int move_time;
	int dac = dev_vcm->start_current;
	struct i2c_client *client = dev_vcm->client;

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: enter, power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	dw9763_init(client);

	v4l2_dbg(1, debug, sd, "%s: current_lens_pos %d, current_related_pos %d\n",
		 __func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);

	move_time = 1000 * dw9763_move_time(dev_vcm, DW9763_GRADUAL_MOVELENS_STEPS);
	while (dac <= dev_vcm->current_lens_pos) {
		dw9763_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 100);
		dac += DW9763_GRADUAL_MOVELENS_STEPS;
		if (dac > dev_vcm->current_lens_pos)
			break;
	}

	if (dac > dev_vcm->current_lens_pos) {
		dac = dev_vcm->current_lens_pos;
		dw9763_set_dac(dev_vcm, dac);
	}

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: exit, power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif
	return 0;
}

static int dw9763_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	int dac = dev_vcm->current_lens_pos;
	unsigned int move_time;
	int ret;
	struct i2c_client *client = dev_vcm->client;

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: enter, power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	v4l2_dbg(1, debug, sd, "%s: current_lens_pos %d, current_related_pos %d\n",
		 __func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);

	dac -= DW9763_GRADUAL_MOVELENS_STEPS;
	move_time = 1000 * dw9763_move_time(dev_vcm, DW9763_GRADUAL_MOVELENS_STEPS);
	while (dac >= DW9763_GRADUAL_MOVELENS_STEPS) {
		dw9763_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 1000);
		dac -= DW9763_GRADUAL_MOVELENS_STEPS;
		if (dac <= 0)
			break;
	}

	if (dac < DW9763_GRADUAL_MOVELENS_STEPS) {
		dac = DW9763_GRADUAL_MOVELENS_STEPS / 2;
		dw9763_set_dac(dev_vcm, dac);
	}
	/* set to power down mode */
	ret = dw9763_write_reg(client, 0x02, 1, 0x01);
	if (ret)
		dev_err(&client->dev, "failed to set power down mode!\n");

	pm_runtime_put(sd->dev);
#ifdef CONFIG_PM
	v4l2_info(sd, "%s: exit, power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9763_int_ops = {
	.open = dw9763_open,
	.close = dw9763_close,
};

static void dw9763_update_vcm_cfg(struct dw9763_device *dev_vcm)
{
	struct i2c_client *client = dev_vcm->client;
	int cur_dist;

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	cur_dist = dev_vcm->vcm_cfg.rated_ma - dev_vcm->vcm_cfg.start_ma;
	cur_dist = cur_dist * DW9763_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step = (cur_dist + (VCMDRV_MAX_LOG - 1)) / VCMDRV_MAX_LOG;
	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 DW9763_MAX_REG / dev_vcm->max_ma;
	dev_vcm->rated_current = dev_vcm->vcm_cfg.rated_ma *
				 DW9763_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step_mode = dev_vcm->vcm_cfg.step_mode;

	dev_info(&client->dev,
		"vcm_cfg: %d, %d, %d, max_ma %d\n",
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->vcm_cfg.step_mode,
		dev_vcm->max_ma);
}

static long dw9763_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	struct i2c_client *client = dev_vcm->client;
	struct rk_cam_vcm_tim *vcm_tim;
	struct rk_cam_vcm_cfg *vcm_cfg;
	int ret = 0;

	if (cmd == RK_VIDIOC_VCM_TIMEINFO) {
		vcm_tim = (struct rk_cam_vcm_tim *)arg;

		vcm_tim->vcm_start_t.tv_sec = dev_vcm->start_move_tv.tv_sec;
		vcm_tim->vcm_start_t.tv_usec =
				dev_vcm->start_move_tv.tv_usec;
		vcm_tim->vcm_end_t.tv_sec = dev_vcm->end_move_tv.tv_sec;
		vcm_tim->vcm_end_t.tv_usec = dev_vcm->end_move_tv.tv_usec;

		v4l2_dbg(1, debug, &dev_vcm->sd, "dw9763_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
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
		dw9763_update_vcm_cfg(dev_vcm);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long dw9763_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	struct i2c_client *client = dev_vcm->client;
	void __user *up = compat_ptr(arg);
	struct rk_cam_compat_vcm_tim compat_vcm_tim;
	struct rk_cam_vcm_tim vcm_tim;
	struct rk_cam_vcm_cfg vcm_cfg;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		struct rk_cam_compat_vcm_tim __user *p32 = up;

		ret = dw9763_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
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
		ret = dw9763_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret) {
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
			if (ret)
				ret = -EFAULT;
		}
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = dw9763_ioctl(sd, cmd, &vcm_cfg);
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

static const struct v4l2_subdev_core_ops dw9763_core_ops = {
	.ioctl = dw9763_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = dw9763_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops dw9763_ops = {
	.core = &dw9763_core_ops,
};

static void dw9763_subdev_cleanup(struct dw9763_device *dw9763_dev)
{
	v4l2_device_unregister_subdev(&dw9763_dev->sd);
	v4l2_device_unregister(&dw9763_dev->vdev);
	v4l2_ctrl_handler_free(&dw9763_dev->ctrls_vcm);
	media_entity_cleanup(&dw9763_dev->sd.entity);
}

static int dw9763_init_controls(struct dw9763_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &dw9763_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, VCMDRV_MAX_LOG, 1, 32);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

#define USED_SYS_DEBUG
#ifdef USED_SYS_DEBUG
static ssize_t set_dacval(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret)
		dw9763_set_dac(dev_vcm, val);

	return count;
}

static ssize_t get_dacval(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct dw9763_device *dev_vcm = sd_to_dw9763_vcm(sd);
	unsigned int dac = 0;

	dw9763_get_dac(dev_vcm, &dac);
	return sprintf(buf, "%u\n", dac);
}

static struct device_attribute attributes[] = {
	__ATTR(dacval, 0600, get_dacval, set_dacval),
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

static int remove_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
	return 0;
}
#else
static inline int add_sysfs_interfaces(struct device *dev)
{
	return 0;
}

static inline int remove_sysfs_interfaces(struct device *dev)
{
	return 0;
}
#endif

static int __dw9763_set_power(struct dw9763_device *dw9763, bool on)
{
	struct i2c_client *client = dw9763->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);

	if (dw9763->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = regulator_enable(dw9763->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable regulator\n");
			goto unlock_and_return;
		}
		dw9763->power_on = true;
	} else {
		ret = regulator_disable(dw9763->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to disable regulator\n");
			goto unlock_and_return;
		}
		dw9763->power_on = false;
	}

unlock_and_return:
	return ret;
}

static int dw9763_configure_regulator(struct dw9763_device *dw9763)
{
	struct i2c_client *client = dw9763->client;
	int ret = 0;

	dw9763->supply = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(dw9763->supply)) {
		ret = PTR_ERR(dw9763->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "could not get regulator avdd\n");
		return ret;
	}
	dw9763->power_on = false;
	return ret;
}

static int __maybe_unused dw9763_check_id(struct dw9763_device *dw9763_dev)
{
	int ret = 0;
	unsigned int pid = 0x00;
	struct i2c_client *client = dw9763_dev->client;
	struct device *dev = &client->dev;

	__dw9763_set_power(dw9763_dev, true);
	ret = dw9763_read_reg(client, DW9763_REG_CHIP_ID, 1, &pid);

	if (pid != DW9763_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", pid, ret);
		return -ENODEV;
	}

	dev_info(&dw9763_dev->client->dev,
		 "Detected dw9763 vcm id:0x%x\n", DW9763_CHIP_ID);
	return 0;
}

static int dw9763_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct dw9763_device *dw9763_dev;
	unsigned int max_ma, start_ma, rated_ma, step_mode;
	unsigned int t_src, t_div;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(&client->dev, "probing...\n");
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&max_ma)) {
		max_ma = DW9763_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (max_ma == 0)
		max_ma = DW9763_MAX_CURRENT;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&start_ma)) {
		start_ma = DW9763_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&rated_ma)) {
		rated_ma = DW9763_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&step_mode)) {
		step_mode = DW9763_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_SRC,
		(unsigned int *)&t_src)) {
		t_src = DW9763_DEFAULT_T_SACT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_SRC);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_DIV,
		(unsigned int *)&t_div)) {
		t_div = DW9763_DEFAULT_T_DIV;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_DIV);
	}

	dw9763_dev = devm_kzalloc(&client->dev, sizeof(*dw9763_dev),
				  GFP_KERNEL);
	if (dw9763_dev == NULL)
		return -ENOMEM;

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &dw9763_dev->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &dw9763_dev->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	dw9763_dev->client = client;
	dw9763_dev->power_gpio = devm_gpiod_get(&client->dev,
					"power", GPIOD_OUT_LOW);
	if (IS_ERR(dw9763_dev->power_gpio)) {
		dw9763_dev->power_gpio = NULL;
		dev_warn(&client->dev,
			"Failed to get power-gpios, maybe no use\n");
	}
	ret = dw9763_configure_regulator(dw9763_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to get power regulator!\n");
		return ret;
	}

	v4l2_i2c_subdev_init(&dw9763_dev->sd, client, &dw9763_ops);
	dw9763_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9763_dev->sd.internal_ops = &dw9763_int_ops;

	ret = dw9763_init_controls(dw9763_dev);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&dw9763_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	ret = dw9763_check_id(dw9763_dev);
	if (ret)
		goto err_power_off;

	sd = &dw9763_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(dw9763_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 dw9763_dev->module_index, facing,
		 DW9763_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	dw9763_dev->max_ma = max_ma;
	dw9763_dev->vcm_cfg.start_ma = start_ma;
	dw9763_dev->vcm_cfg.rated_ma = rated_ma;
	dw9763_dev->vcm_cfg.step_mode = step_mode;
	dw9763_update_vcm_cfg(dw9763_dev);
	dw9763_dev->move_us	= 0;
	dw9763_dev->current_related_pos = VCMDRV_MAX_LOG;
	dw9763_dev->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	dw9763_dev->end_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());

	dw9763_dev->t_src = t_src;
	dw9763_dev->t_div = t_div;

	i2c_set_clientdata(client, dw9763_dev);
	mutex_init(&dw9763_dev->lock);

	dw9763_dev->vcm_movefull_t =
		dw9763_move_time(dw9763_dev, DW9763_MAX_REG);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	add_sysfs_interfaces(&client->dev);
	dev_info(&client->dev, "probing successful\n");

	return 0;
err_power_off:
	__dw9763_set_power(dw9763_dev, false);

err_cleanup:
	dw9763_subdev_cleanup(dw9763_dev);

	dev_err(&client->dev, "Probe failed: %d\n", ret);

	return ret;
}

static int dw9763_remove(struct i2c_client *client)
{
	struct dw9763_device *dw9763_dev = i2c_get_clientdata(client);

	remove_sysfs_interfaces(&client->dev);
	mutex_destroy(&dw9763_dev->lock);
	pm_runtime_disable(&client->dev);
	dw9763_subdev_cleanup(dw9763_dev);

	return 0;
}

static int dw9763_init(struct i2c_client *client)
{
	struct dw9763_device *dev_vcm = i2c_get_clientdata(client);
	int ret = 0;
	u32 mode_val = 0;
	u32 algo_time = 0;

	if (dev_vcm->step_mode == DIRECT_MODE)
		return 0;

	ret = dw9763_write_reg(client, 0x02, 1, 0x00);
	if (ret)
		goto err;

	usleep_range(200, 300);
	ret = dw9763_write_reg(client, 0x02, 1, 0x02);
	if (ret)
		goto err;
	switch (dev_vcm->step_mode) {
	case SAC1_MODE:
	case SAC2_MODE:
	case SAC2_5_MODE:
	case SAC3_MODE:
	case SAC4_MODE:
		mode_val |= dev_vcm->step_mode << 5;
		break;
	default:
		break;
	}

	mode_val |= (dev_vcm->t_div & 0x07);
	algo_time = dev_vcm->t_src;
	ret = dw9763_write_reg(client, 0x06, 1, mode_val);
	if (ret)
		goto err;
	ret = dw9763_write_reg(client, 0x07, 1, algo_time);
	if (ret)
		goto err;
	usleep_range(100, 200);

	return 0;
err:
	dev_err(&client->dev, "init failed with error %d\n", ret);
	return -1;
}

static int __maybe_unused dw9763_vcm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dw9763_device *dev_vcm = i2c_get_clientdata(client);
	struct v4l2_subdev *sd = &(dev_vcm->sd);

#ifdef CONFIG_PM
	v4l2_dbg(1, debug, sd, "%s: enter, power.usage_count(%d)!\n", __func__,
		 atomic_read(&sd->dev->power.usage_count));
#endif

	__dw9763_set_power(dev_vcm, false);
	return 0;
}

static int __maybe_unused dw9763_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dw9763_device *dev_vcm = i2c_get_clientdata(client);
	struct v4l2_subdev *sd = &(dev_vcm->sd);

#ifdef CONFIG_PM
	v4l2_dbg(1, debug, sd, "%s: enter, power.usage_count(%d)!\n", __func__,
		 atomic_read(&sd->dev->power.usage_count));
#endif
	__dw9763_set_power(dev_vcm, true);

	return 0;
}

static const struct i2c_device_id dw9763_id_table[] = {
	{ DW9763_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, dw9763_id_table);

static const struct of_device_id dw9763_of_table[] = {
	{ .compatible = "dongwoon,dw9763" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, dw9763_of_table);

static const struct dev_pm_ops dw9763_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw9763_vcm_suspend, dw9763_vcm_resume)
	SET_RUNTIME_PM_OPS(dw9763_vcm_suspend, dw9763_vcm_resume, NULL)
};

static struct i2c_driver dw9763_i2c_driver = {
	.driver = {
		.name = DW9763_NAME,
		.pm = &dw9763_pm_ops,
		.of_match_table = dw9763_of_table,
	},
	.probe = &dw9763_probe,
	.remove = &dw9763_remove,
	.id_table = dw9763_id_table,
};

module_i2c_driver(dw9763_i2c_driver);

MODULE_DESCRIPTION("DW9763 VCM driver");
MODULE_LICENSE("GPL");
