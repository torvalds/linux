// SPDX-License-Identifier: GPL-2.0
/*
 * dw9800v vcm driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
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
#include <linux/regulator/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define DW9800V_NAME			"dw9800v"

#define DW9800V_MAX_CURRENT		1023U
#define DW9800V_MAX_REG			1023U
#define DW9800V_GRADUAL_MOVELENS_STEPS	32

#define DW9800V_DEFAULT_START_CURRENT	553
#define DW9800V_DEFAULT_RATED_CURRENT	853
#define DW9800V_DEFAULT_STEP_MODE	0x0
#define DW9800V_DEFAULT_T_SACT		0x10
#define DW9800V_DEFAULT_T_DIV		0x1
#define REG_NULL			0xFF

#define DW9800V_ADVMODE_VCM_MSB		0x03
#define DW9800V_ADVMODE_VCM_LSB		0x04
#define DW9800V_ADVMODE_STATUS		0x05

#define DW9800V_CHIP_ID			0xEB
#define DW9800V_REG_CHIP_ID		0x00

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

enum mode_e {
	SAC2_MODE,
	SAC3_MODE,
	SAC4_MODE,
	SAC5_MODE,
	DIRECT_MODE,
	LSC_MODE,
};

/* dw9800v device structure */
struct dw9800v_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_ctrl *focus;
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
	unsigned int max_logicalpos;

	struct __kernel_old_timeval start_move_tv;
	struct __kernel_old_timeval end_move_tv;
	unsigned long move_us;

	u32 module_index;
	const char *module_facing;
	struct rk_cam_vcm_cfg vcm_cfg;
	int max_ma;

	struct gpio_desc *xsd_gpio;
	struct regulator *supply;
	struct i2c_client *client;
	bool power_on;

};

static inline struct dw9800v_device *to_dw9800v_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct dw9800v_device, ctrls_vcm);
}

static inline struct dw9800v_device *sd_to_dw9800v_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct dw9800v_device, sd);
}

static int dw9800v_write_reg(struct i2c_client *client, u8 reg,
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

static int dw9800v_read_reg(struct i2c_client *client,
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

static unsigned int dw9800v_move_time_div(struct dw9800v_device *dev_vcm,
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
		dev_err(&client->dev,
			"%s: t_div parameter err %d\n",
			__func__, dev_vcm->t_div);
		break;
	}
	return move_time;
}

static unsigned int dw9800v_move_time(struct dw9800v_device *dev_vcm,
	unsigned int move_pos)
{
	struct i2c_client *client = dev_vcm->client;
	unsigned int move_time_us = 0;

	switch (dev_vcm->step_mode) {
	case LSC_MODE:
		move_time_us = 252 + dev_vcm->t_src * 4;
		move_time_us = move_time_us * move_pos;
		break;
	case SAC2_MODE:
	case SAC3_MODE:
	case SAC4_MODE:
	case SAC5_MODE:
		move_time_us = 6300 + dev_vcm->t_src * 100;
		move_time_us = dw9800v_move_time_div(dev_vcm, move_time_us);
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

	dev_info(&client->dev,
		"%s: vcm_movefull_t is: %d us\n",
		__func__, move_time_us);

	return move_time_us;
}

static int dw9800v_set_dac(struct dw9800v_device *dev_vcm,
	unsigned int dest_dac)
{
	struct i2c_client *client = dev_vcm->client;
	int ret;

	unsigned int i;
	bool vcm_idle = false;

	/* wait for I2C bus idle */
	vcm_idle = false;
	for (i = 0; i < 10; i++) {
		unsigned int status = 0;

		dw9800v_read_reg(client, DW9800V_ADVMODE_STATUS, 1, &status);
		status &= 0x01;
		if (status == 0) {
			vcm_idle = true;
			break;
		}
		usleep_range(1000, 1200);
	}

	if (!vcm_idle) {
		dev_err(&client->dev,
			"%s: watting 0x05 flag timeout!\n", __func__);
		return -ETIMEDOUT;
	}

	/* vcm move */
	ret = dw9800v_write_reg(client, DW9800V_ADVMODE_VCM_MSB,
				2, dest_dac);
	if (ret != 0)
		goto err;

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9800v_get_pos(struct dw9800v_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = dev_vcm->client;
	int ret;
	unsigned int dac, abs_step;


	ret = dw9800v_read_reg(client, 0x03, 2, &dac);
	if (ret != 0)
		goto err;

	if (dac <= dev_vcm->start_current)
		abs_step = dev_vcm->max_logicalpos;
	else if ((dac > dev_vcm->start_current) &&
		 (dac <= dev_vcm->rated_current))
		abs_step = (dev_vcm->rated_current - dac) / dev_vcm->step;
	else
		abs_step = 0;

	*cur_pos = abs_step;
	v4l2_dbg(1, debug, &dev_vcm->sd, "%s: get position %d, dac %d\n",
		 __func__, *cur_pos, dac);
	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9800v_set_pos(struct dw9800v_device *dev_vcm,
	unsigned int dest_pos)
{
	int ret;
	unsigned int position = 0;
	struct i2c_client *client = dev_vcm->client;
	u32 is_busy, i;

	if (dest_pos >= dev_vcm->max_logicalpos)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (dev_vcm->step * (dev_vcm->max_logicalpos - dest_pos));

	if (position > DW9800V_MAX_REG)
		position = DW9800V_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;
	for (i = 0; i < 100; i++) {
		ret = dw9800v_read_reg(client, 0x05, 1, &is_busy);
		if (!ret && !(is_busy & 0x01))
			break;
		usleep_range(100, 200);
	}

	ret = dw9800v_write_reg(client, 0x03, 2, dev_vcm->current_lens_pos);
	if (ret != 0)
		goto err;
	v4l2_dbg(1, debug, &dev_vcm->sd, "%s: set position %d, dac %d\n",
		 __func__, dest_pos, position);

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int dw9800v_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9800v_device *dev_vcm = to_dw9800v_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return dw9800v_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int dw9800v_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct dw9800v_device *dev_vcm = to_dw9800v_vcm(ctrl);
	struct i2c_client *client = dev_vcm->client;
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

		ret = dw9800v_set_pos(dev_vcm, dest_pos);
		if (dev_vcm->step_mode == LSC_MODE)
			dev_vcm->move_us = ((dev_vcm->vcm_movefull_t * (uint32_t)move_pos) /
					   dev_vcm->max_logicalpos);
		else
			dev_vcm->move_us = dev_vcm->vcm_movefull_t;

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

static const struct v4l2_ctrl_ops dw9800v_vcm_ctrl_ops = {
	.g_volatile_ctrl = dw9800v_get_ctrl,
	.s_ctrl = dw9800v_set_ctrl,
};

static int dw9800v_init(struct i2c_client *client);

static int dw9800v_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;
	struct dw9800v_device *dev_vcm = sd_to_dw9800v_vcm(sd);
	unsigned int move_time;
	int dac = dev_vcm->start_current;
	struct i2c_client *client = dev_vcm->client;

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: enter,  power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}
	dw9800v_init(client);

	usleep_range(1000, 1200);
	v4l2_dbg(1, debug, sd, "%s: current_lens_pos %d, current_related_pos %d\n",
		 __func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);

	move_time = dw9800v_move_time(dev_vcm, DW9800V_GRADUAL_MOVELENS_STEPS);
	while (dac <= dev_vcm->current_lens_pos) {
		dw9800v_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 1000);
		dac += DW9800V_GRADUAL_MOVELENS_STEPS;
		if (dac >= dev_vcm->current_lens_pos)
			break;
	}

	if (dac > dev_vcm->current_lens_pos) {
		dac = dev_vcm->current_lens_pos;
		dw9800v_set_dac(dev_vcm, dac);
	}

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: exit,  power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	return 0;
}

static int dw9800v_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct dw9800v_device *dev_vcm = sd_to_dw9800v_vcm(sd);
	int dac = dev_vcm->current_lens_pos;
	unsigned int move_time;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: enter,  power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif
	v4l2_dbg(1, debug, sd, "%s: current_lens_pos %d, current_related_pos %d\n",
		 __func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);
	move_time = dw9800v_move_time(dev_vcm, DW9800V_GRADUAL_MOVELENS_STEPS);
	while (dac >= DW9800V_GRADUAL_MOVELENS_STEPS) {
		dw9800v_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 1000);
		dac -= DW9800V_GRADUAL_MOVELENS_STEPS;
		if (dac <= 0)
			break;
	}

	if (dac < DW9800V_GRADUAL_MOVELENS_STEPS) {
		dac = DW9800V_GRADUAL_MOVELENS_STEPS;
		dw9800v_set_dac(dev_vcm, dac);
	}
	/* set to power down mode */
	dw9800v_write_reg(client, 0x02, 1, 0x01);

	pm_runtime_put(sd->dev);

#ifdef CONFIG_PM
	v4l2_info(sd, "%s: exit,  power.usage_count(%d)!\n", __func__,
		  atomic_read(&sd->dev->power.usage_count));
#endif

	return 0;
}

static const struct v4l2_subdev_internal_ops dw9800v_int_ops = {
	.open = dw9800v_open,
	.close = dw9800v_close,
};

static void dw9800v_update_vcm_cfg(struct dw9800v_device *dev_vcm)
{
	struct i2c_client *client = dev_vcm->client;
	int cur_dist;

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	cur_dist = dev_vcm->vcm_cfg.rated_ma - dev_vcm->vcm_cfg.start_ma;
	cur_dist = cur_dist * DW9800V_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step = (cur_dist + (dev_vcm->max_logicalpos - 1)) / dev_vcm->max_logicalpos;
	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 DW9800V_MAX_REG / dev_vcm->max_ma;
	dev_vcm->rated_current = dev_vcm->vcm_cfg.rated_ma *
				 DW9800V_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step_mode = dev_vcm->vcm_cfg.step_mode;

	v4l2_dbg(1, debug, &dev_vcm->sd,
		"vcm_cfg: %d, %d, %d, max_ma %d\n",
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->vcm_cfg.step_mode,
		dev_vcm->max_ma);
}

static long dw9800v_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct dw9800v_device *dev_vcm = sd_to_dw9800v_vcm(sd);
	struct i2c_client *client = dev_vcm->client;
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

		v4l2_dbg(1, debug, sd, "dw9800v_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
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
		if (vcm_cfg->rated_ma > DW9800V_MAX_CURRENT) {
			dev_warn(&client->dev,
				 "vcm_cfg use dac value, do convert!\n");
			vcm_cfg->rated_ma = vcm_cfg->rated_ma *
					    dev_vcm->max_ma / DW9800V_MAX_REG;
			vcm_cfg->start_ma = vcm_cfg->start_ma *
					    dev_vcm->max_ma / DW9800V_MAX_REG;
		}

		dev_vcm->vcm_cfg.start_ma = vcm_cfg->start_ma;
		dev_vcm->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		dev_vcm->vcm_cfg.step_mode = vcm_cfg->step_mode;
		dw9800v_update_vcm_cfg(dev_vcm);
	} else if (cmd == RK_VIDIOC_SET_VCM_MAX_LOGICALPOS) {
		max_logicalpos = *(unsigned int *)arg;

		if (max_logicalpos > 0) {
			dev_vcm->max_logicalpos = max_logicalpos;
			__v4l2_ctrl_modify_range(dev_vcm->focus,
				0, dev_vcm->max_logicalpos, 1, dev_vcm->max_logicalpos);
		}
		v4l2_dbg(1, debug, &dev_vcm->sd,
			"max_logicalpos %d\n", max_logicalpos);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long dw9800v_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct dw9800v_device *dev_vcm = sd_to_dw9800v_vcm(sd);
	struct i2c_client *client = dev_vcm->client;
	void __user *up = compat_ptr(arg);
	struct rk_cam_compat_vcm_tim compat_vcm_tim;
	struct rk_cam_vcm_tim vcm_tim;
	struct rk_cam_vcm_cfg vcm_cfg;
	unsigned int max_logicalpos;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		struct rk_cam_compat_vcm_tim __user *p32 = up;

		ret = dw9800v_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
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
		ret = dw9800v_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret) {
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
			if (ret)
				ret = -EFAULT;
		}
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = dw9800v_ioctl(sd, cmd, &vcm_cfg);
		else
			ret = -EFAULT;
	} else if (cmd == RK_VIDIOC_SET_VCM_MAX_LOGICALPOS) {
		ret = copy_from_user(&max_logicalpos, up, sizeof(max_logicalpos));
		if (!ret)
			ret = dw9800v_ioctl(sd, cmd, &max_logicalpos);
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

static const struct v4l2_subdev_core_ops dw9800v_core_ops = {
	.ioctl = dw9800v_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = dw9800v_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops dw9800v_ops = {
	.core = &dw9800v_core_ops,
};

static void dw9800v_subdev_cleanup(struct dw9800v_device *dw9800v_dev)
{
	v4l2_device_unregister_subdev(&dw9800v_dev->sd);
	v4l2_device_unregister(&dw9800v_dev->vdev);
	v4l2_ctrl_handler_free(&dw9800v_dev->ctrls_vcm);
	media_entity_cleanup(&dw9800v_dev->sd.entity);
}

static int dw9800v_init_controls(struct dw9800v_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &dw9800v_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	dev_vcm->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
					   0, dev_vcm->max_logicalpos, 1, 0);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int __dw9800v_set_power(struct dw9800v_device *dw9800v, bool on)
{
	struct i2c_client *client = dw9800v->client;
	int ret = 0;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);

	if (dw9800v->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = regulator_enable(dw9800v->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable regulator\n");
			goto unlock_and_return;
		}
		dw9800v->power_on = true;
	} else {
		ret = regulator_disable(dw9800v->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to disable regulator\n");
			goto unlock_and_return;
		}
		dw9800v->power_on = false;
	}

unlock_and_return:
	return ret;
}

static int dw9800v_check_id(struct dw9800v_device *dw9800v_dev)
{
	int ret = 0;
	unsigned int pid = 0x00;
	struct i2c_client *client = dw9800v_dev->client;
	struct device *dev = &client->dev;

	__dw9800v_set_power(dw9800v_dev, true);
	ret = dw9800v_read_reg(client, DW9800V_REG_CHIP_ID, 1, &pid);

	if (pid != DW9800V_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", pid, ret);
		return -ENODEV;
	}

	dev_info(&dw9800v_dev->client->dev,
		 "Detected dw9800v vcm id:0x%x\n", DW9800V_CHIP_ID);
	return 0;
}
static int dw9800v_probe_init(struct i2c_client *client)
{
	int ret = 0;

	/* Default goto power down mode when finished probe */
	ret = dw9800v_write_reg(client, 0x02, 1, 0x01);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&client->dev, "probe init failed with error %d\n", ret);
	return -1;
}

static int dw9800v_configure_regulator(struct dw9800v_device *dw9800v)
{
	struct i2c_client *client = dw9800v->client;
	int ret = 0;

	dw9800v->supply = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(dw9800v->supply)) {
		ret = PTR_ERR(dw9800v->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "could not get regulator avdd\n");
		return ret;
	}
	dw9800v->power_on = false;
	return ret;
}
static int dw9800v_parse_dt_property(struct i2c_client *client,
				    struct dw9800v_device *dev_vcm)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	int ret;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&dev_vcm->max_ma)) {
		dev_vcm->max_ma = DW9800V_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (dev_vcm->max_ma == 0)
		dev_vcm->max_ma = DW9800V_MAX_CURRENT;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&dev_vcm->vcm_cfg.start_ma)) {
		dev_vcm->vcm_cfg.start_ma = DW9800V_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&dev_vcm->vcm_cfg.rated_ma)) {
		dev_vcm->vcm_cfg.rated_ma = DW9800V_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&dev_vcm->vcm_cfg.step_mode)) {
		dev_vcm->vcm_cfg.step_mode = DW9800V_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_SRC,
		(unsigned int *)&dev_vcm->t_src)) {
		dev_vcm->t_src = DW9800V_DEFAULT_T_SACT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_SRC);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_DIV,
		(unsigned int *)&dev_vcm->t_div)) {
		dev_vcm->t_div = DW9800V_DEFAULT_T_DIV;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_DIV);
	}

	dev_vcm->xsd_gpio = devm_gpiod_get(&client->dev, "xsd", GPIOD_OUT_HIGH);
	if (IS_ERR(dev_vcm->xsd_gpio))
		dev_warn(&client->dev, "Failed to get xsd-gpios\n");

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &dev_vcm->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
					   &dev_vcm->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}
	dev_vcm->client = client;
	ret = dw9800v_configure_regulator(dev_vcm);
	if (ret) {
		dev_err(&client->dev, "Failed to get power regulator!\n");
		return ret;
	}

	dev_info(&client->dev, "current: %d, %d, %d, t_div: %d, t_src: %d, step_mode: %d",
		dev_vcm->max_ma,
		dev_vcm->start_current,
		dev_vcm->rated_current,
		dev_vcm->t_div,
		dev_vcm->t_src,
		dev_vcm->vcm_cfg.step_mode);

	return 0;
}

static int dw9800v_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct dw9800v_device *dw9800v_dev;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(&client->dev, "probing...\n");
	dw9800v_dev = devm_kzalloc(&client->dev, sizeof(*dw9800v_dev),
				  GFP_KERNEL);
	if (dw9800v_dev == NULL)
		return -ENOMEM;

	ret = dw9800v_parse_dt_property(client, dw9800v_dev);
	if (ret)
		return ret;

	dw9800v_dev->client = client;

	ret = dw9800v_check_id(dw9800v_dev);
	if (ret)
		goto err_power_off;

	/* enter power down mode */
	dw9800v_probe_init(client);

	v4l2_i2c_subdev_init(&dw9800v_dev->sd, client, &dw9800v_ops);
	dw9800v_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dw9800v_dev->sd.internal_ops = &dw9800v_int_ops;

	dw9800v_dev->max_logicalpos = VCMDRV_MAX_LOG;
	ret = dw9800v_init_controls(dw9800v_dev);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&dw9800v_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	sd = &dw9800v_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(dw9800v_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 dw9800v_dev->module_index, facing,
		 DW9800V_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	dw9800v_update_vcm_cfg(dw9800v_dev);
	dw9800v_dev->move_us	= 0;
	dw9800v_dev->current_related_pos = VCMDRV_MAX_LOG;
	dw9800v_dev->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	dw9800v_dev->end_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());

	i2c_set_clientdata(client, dw9800v_dev);

	dw9800v_dev->vcm_movefull_t =
		dw9800v_move_time(dw9800v_dev, DW9800V_MAX_REG);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;
err_cleanup:
	dw9800v_subdev_cleanup(dw9800v_dev);
err_power_off:
	__dw9800v_set_power(dw9800v_dev, false);

	dev_err(&client->dev, "Probe failed: %d\n", ret);

	return ret;
}

static int dw9800v_remove(struct i2c_client *client)
{
	struct dw9800v_device *dw9800v_dev = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	dw9800v_subdev_cleanup(dw9800v_dev);

	return 0;
}

static int dw9800v_init(struct i2c_client *client)
{
	struct dw9800v_device *dev_vcm = i2c_get_clientdata(client);
	int ret = 0;
	u32 ring = 0;
	u32 mode_val = 0;
	u32 algo_time = 0;


	/* Delay 200us~300us */
	usleep_range(200, 300);
	ret = dw9800v_write_reg(client, 0x02, 1, 0x00);
	if (ret)
		goto err;
	usleep_range(100, 200);

	if (dev_vcm->step_mode != DIRECT_MODE &&
	    dev_vcm->step_mode != LSC_MODE)
		ring = 0x02;
	ret = dw9800v_write_reg(client, 0x02, 1, ring);
	if (ret)
		goto err;
	switch (dev_vcm->step_mode) {
	case SAC2_MODE:
	case SAC3_MODE:
	case SAC4_MODE:
	case SAC5_MODE:
		mode_val |= dev_vcm->step_mode << 6;
		break;
	case LSC_MODE:
		mode_val |= 0x80;
		break;
	default:
		break;
	}
	mode_val |= ((dev_vcm->t_div >> 2) & 0x01);
	algo_time = dev_vcm->t_div << 6 | dev_vcm->t_src;
	ret = dw9800v_write_reg(client, 0x06, 1, mode_val);
	if (ret)
		goto err;
	ret = dw9800v_write_reg(client, 0x07, 1, algo_time);
	if (ret)
		goto err;
	usleep_range(100, 200);

	return 0;
err:
	dev_err(&client->dev, "init failed with error %d\n", ret);
	return -1;
}

static int __maybe_unused dw9800v_vcm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dw9800v_device *dev_vcm = i2c_get_clientdata(client);
	struct v4l2_subdev *sd = &dev_vcm->sd;

#ifdef CONFIG_PM
	v4l2_dbg(1, debug, sd, "%s: enter,	power.usage_count(%d)!\n", __func__,
		 atomic_read(&sd->dev->power.usage_count));
#endif

	__dw9800v_set_power(dev_vcm, false);
	return 0;
}

static int __maybe_unused dw9800v_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dw9800v_device *dev_vcm = i2c_get_clientdata(client);
	struct v4l2_subdev *sd = &dev_vcm->sd;

#ifdef CONFIG_PM
	v4l2_dbg(1, debug, sd, "%s: enter,	power.usage_count(%d)!\n", __func__,
		 atomic_read(&sd->dev->power.usage_count));
#endif

	__dw9800v_set_power(dev_vcm, true);
	return 0;
}

static const struct i2c_device_id dw9800v_id_table[] = {
	{ DW9800V_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, dw9800v_id_table);

static const struct of_device_id dw9800v_of_table[] = {
	{ .compatible = "dongwoon,dw9800v" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, dw9800v_of_table);

static const struct dev_pm_ops dw9800v_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw9800v_vcm_suspend, dw9800v_vcm_resume)
	SET_RUNTIME_PM_OPS(dw9800v_vcm_suspend, dw9800v_vcm_resume, NULL)
};

static struct i2c_driver dw9800v_i2c_driver = {
	.driver = {
		.name = DW9800V_NAME,
		.pm = &dw9800v_pm_ops,
		.of_match_table = dw9800v_of_table,
	},
	.probe = &dw9800v_probe,
	.remove = &dw9800v_remove,
	.id_table = dw9800v_id_table,
};

module_i2c_driver(dw9800v_i2c_driver);

MODULE_DESCRIPTION("DW9800V VCM driver");
MODULE_LICENSE("GPL");
