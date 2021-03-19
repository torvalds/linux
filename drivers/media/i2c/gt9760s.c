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
#include <linux/rk_vcm_head.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)
#define GT9760S_NAME			"gt9760s"

#define GT9760S_MAX_CURRENT		120U
#define GT9760S_MAX_REG			1023U

#define GT9760S_DEFAULT_START_CURRENT	0
#define GT9760S_DEFAULT_RATED_CURRENT	120
#define GT9760S_DEFAULT_STEP_MODE	4
#define GT9760S_DEFAULT_DLC_ENABLE	1
#define GT9760S_DEFAULT_MCLK		0x1
#define GT9760S_DEFAULT_T_SRC		0x1F

#define GT9760S_SEL_ON_BYTE1		0xEC
#define GT9760S_SEL_ON_BYTE2		0xA3
#define GT9760S_DVO_DLC_BYTE1		0xA1
#define GT9760S_DVO_DLC_BYTE2		0xD
#define GT9760S_T_SRC_BYTE1		0xF2
#define GT9760S_T_SRC_BYTE2		0xF8
#define GT9760S_SEL_OFF_BYTE1		0xDC
#define GT9760S_SEL_OFF_BYTE2		0x51
#define REG_NULL			0xFF

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
	unsigned int dlc_enable;
	unsigned int t_src;
	unsigned int mclk;

	struct timeval start_move_tv;
	struct timeval end_move_tv;
	unsigned long move_ms;

	u32 module_index;
	const char *module_facing;

	struct rk_cam_vcm_cfg vcm_cfg;
	int max_ma;
};

struct TimeTabel_s {
	unsigned int t_src;/* time of slew rate control */
	unsigned int step00;/* S[1:0] /MCLK[1:0] step period */
	unsigned int step01;
	unsigned int step10;
	unsigned int step11;
};

static const struct TimeTabel_s gt9760s_lsc_time_table[] = {/* 1/10us */
	{0b10000, 1360, 2720, 5440, 10880},
	{0b10001, 1300, 2600, 5200, 10400},
	{0b10010, 1250, 2500, 5000, 10000},
	{0b10011, 1200, 2400, 4800,  9600},
	{0b10100, 1160, 2320, 4640,  9280},
	{0b10101, 1120, 2240, 4480,  8960},
	{0b10110, 1080, 2160, 4320,  8640},
	{0b10111, 1040, 2080, 4160,  8320},
	{0b11000, 1010, 2020, 4040,  8080},
	{0b11001,  980, 1960, 3920,  7840},
	{0b11010,  950, 1900, 3800,  7600},
	{0b11011,  920, 1840, 3680,  7360},
	{0b11100,  890, 1780, 3560,  7120},
	{0b11101,  870, 1740, 3480,  6960},
	{0b11110,  850, 1700, 3400,  6800},
	{0b11111,  830, 1660, 3320,  6640},
	{0b00000,  810, 1620, 3240,  6480},
	{0b00001,  790, 1580, 3160,  6320},
	{0b00010,  775, 1550, 3100,  6200},
	{0b00011,  760, 1520, 3040,  6080},
	{0b00100,  745, 1490, 2980,  5960},
	{0b00101,  730, 1460, 2920,  5840},
	{0b00110,  715, 1430, 2860,  5720},
	{0b00111,  700, 1400, 2800,  5600},
	{0b01000,  690, 1380, 2760,  5520},
	{0b01001,  680, 1360, 2720,  5440},
	{0b01010,  670, 1340, 2680,  5360},
	{0b01011,  660, 1320, 2640,  5280},
	{0b01100,  655, 1310, 2620,  5240},
	{0b01101,  650, 1300, 2600,  5200},
	{0b01110,  645, 1290, 2580,  5160},
	{0b01111,  640, 1280, 2560,  5120},
	{REG_NULL,  0, 0, 0, 0},
};

static const struct TimeTabel_s gt9760s_dlc_time_table[] = {/* us */
	{0b10000, 21250, 10630, 5310, 2660},
	{0b10001, 20310, 10160, 5080, 2540},
	{0b10010, 19530,  9770, 4880, 2440},
	{0b10011, 18750,  9380, 4690, 2340},
	{0b10100, 18130,  9060, 4530, 2270},
	{0b10101, 17500,  8750, 4380, 2190},
	{0b10110, 16880,  8440, 4220, 2110},
	{0b10111, 16250,  8130, 4060, 2030},
	{0b11000, 15780,  7890, 3950, 1970},
	{0b11001, 15310,  7660, 3830, 1910},
	{0b11010, 14840,  7420, 3710, 1860},
	{0b11011, 14380,  7190, 3590, 1800},
	{0b11100, 13910,  6950, 3480, 1740},
	{0b11101, 13590,  6800, 3400, 1700},
	{0b11110, 13280,  6640, 3320, 1660},
	{0b11111, 12970,  6480, 3240, 1620},
	{0b00000, 12660,  6330, 3160, 1580},
	{0b00001, 12340,  6170, 3090, 1540},
	{0b00010, 12110,  6050, 3030, 1510},
	{0b00011, 11880,  5940, 2970, 1480},
	{0b00100, 11640,  5820, 2910, 1460},
	{0b00101, 11410,  5700, 2850, 1430},
	{0b00110, 11170,  5590, 2790, 1400},
	{0b00111, 10940,  5470, 2730, 1370},
	{0b01000, 10780,  5390, 2700, 1350},
	{0b01001, 10630,  5310, 2660, 1330},
	{0b01010, 10470,  5230, 2620, 1310},
	{0b01011, 10310,  5160, 2580, 1290},
	{0b01100, 10230,  5120, 2560, 1280},
	{0b01101, 10160,  5080, 2540, 1270},
	{0b01110, 10080,  5040, 2520, 1260},
	{0b01111, 10000,  5000, 2500, 1250},
	{REG_NULL, 0, 0, 0, 0},
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

static unsigned int gt9760s_move_time(struct gt9760s_device *dev_vcm, unsigned int move_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int move_time_ms = 200;
	unsigned int step_period_lsc = 0;
	unsigned int step_period_dlc = 0;
	unsigned int step_period = 0;
	unsigned int codes_per_step = 1;
	unsigned int step_case;
	int table_cnt = 0;
	int i = 0;

	if (dev_vcm->dlc_enable) {
		step_case = dev_vcm->mclk & 0x3;
		table_cnt = ARRAY_SIZE(gt9760s_dlc_time_table);
		for (i = 0; i < table_cnt; i++) {
			if (gt9760s_dlc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	} else {
		step_case = dev_vcm->step_mode & 0x3;
		table_cnt = ARRAY_SIZE(gt9760s_lsc_time_table);
		for (i = 0; i < table_cnt; i++) {
			if (gt9760s_lsc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	}

	if (i >= table_cnt)
		i = 0;

	switch (step_case) {
	case 0:
		step_period_lsc = gt9760s_lsc_time_table[i].step00;
		step_period_dlc = gt9760s_dlc_time_table[i].step00;
		break;
	case 1:
		step_period_lsc = gt9760s_lsc_time_table[i].step01;
		step_period_dlc = gt9760s_dlc_time_table[i].step01;
		break;
	case 2:
		step_period_lsc = gt9760s_lsc_time_table[i].step10;
		step_period_dlc = gt9760s_dlc_time_table[i].step10;
		break;
	case 3:
		step_period_lsc = gt9760s_lsc_time_table[i].step11;
		step_period_dlc = gt9760s_dlc_time_table[i].step11;
		break;
	default:
		dev_err(&client->dev,
			"%s: step_case is error %d\n",
			__func__, step_case);
		break;
	}
	codes_per_step = (dev_vcm->step_mode & 0x0c) >> 2;
	if (codes_per_step > 1)
		codes_per_step = 1 << (codes_per_step - 1);

	if (dev_vcm->dlc_enable) {
		move_time_ms = (step_period_dlc + 999) / 1000;
	} else {
		step_period = step_period_lsc;

		if (!codes_per_step)
			move_time_ms = (step_period * move_pos + 9999) / 10000;
		else
			move_time_ms = (step_period * move_pos / codes_per_step + 9999) / 10000;
	}

	return move_time_ms;
}

static int gt9760s_init(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gt9760s_device *dev_vcm = sd_to_vcm_dev(sd);
	int ret = 0;
	u8 val;

	usleep_range(7000, 7500);

	ret = gt9760s_write_msg(client, GT9760S_SEL_ON_BYTE1,
				GT9760S_SEL_ON_BYTE2);
	if (ret)
		goto err;

	val = dev_vcm->dlc_enable << 3 | dev_vcm->mclk | 0x04;
	ret = gt9760s_write_msg(client, GT9760S_DVO_DLC_BYTE1, val);
	if (ret)
		goto err;

	val = dev_vcm->t_src << 3;
	ret = gt9760s_write_msg(client, GT9760S_T_SRC_BYTE1, val);
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
	int move_pos;
	long mv_us;
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

		ret = gt9760s_set_pos(dev_vcm, dest_pos);

		if (dev_vcm->dlc_enable)
			dev_vcm->move_ms = dev_vcm->vcm_movefull_t;
		else
			dev_vcm->move_ms =
				((dev_vcm->vcm_movefull_t * (uint32_t)move_pos) / VCMDRV_MAX_LOG);

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

static void gt9760s_update_vcm_cfg(struct gt9760s_device *dev_vcm)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int cur_dist;

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	cur_dist = dev_vcm->vcm_cfg.rated_ma - dev_vcm->vcm_cfg.start_ma;
	cur_dist = cur_dist * GT9760S_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step = (cur_dist + (VCMDRV_MAX_LOG - 1)) / VCMDRV_MAX_LOG;
	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 GT9760S_MAX_REG / dev_vcm->max_ma;
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

static long gt9760s_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct gt9760s_device *dev_vcm = sd_to_vcm_dev(sd);
	struct rk_cam_vcm_cfg *vcm_cfg;
	struct rk_cam_vcm_tim *vcm_tim;
	int ret = 0;

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
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		vcm_cfg->start_ma = dev_vcm->vcm_cfg.start_ma;
		vcm_cfg->rated_ma = dev_vcm->vcm_cfg.rated_ma;
		vcm_cfg->step_mode = dev_vcm->vcm_cfg.step_mode;
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		dev_vcm->vcm_cfg.start_ma = vcm_cfg->start_ma;
		dev_vcm->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		dev_vcm->vcm_cfg.step_mode = vcm_cfg->step_mode;
		gt9760s_update_vcm_cfg(dev_vcm);
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

static int gt9760s_read_dts(struct i2c_client *client, struct gt9760s_device *dev_vcm)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct rk_cam_vcm_cfg *vcm_cfg = &dev_vcm->vcm_cfg;
	int ret;

	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&dev_vcm->max_ma)) {
		dev_vcm->max_ma = GT9760S_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (dev_vcm->max_ma == 0)
		dev_vcm->max_ma = GT9760S_MAX_CURRENT;

	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&vcm_cfg->start_ma)) {
		vcm_cfg->start_ma = GT9760S_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&vcm_cfg->rated_ma)) {
		vcm_cfg->rated_ma = GT9760S_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&vcm_cfg->step_mode)) {
		vcm_cfg->step_mode = GT9760S_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_DLC_ENABLE,
		(unsigned int *)&dev_vcm->dlc_enable)) {
		dev_vcm->dlc_enable = GT9760S_DEFAULT_DLC_ENABLE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_DLC_ENABLE);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_MCLK,
		(unsigned int *)&dev_vcm->mclk)) {
		dev_vcm->mclk = GT9760S_DEFAULT_MCLK;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MCLK);
	}
	if (of_property_read_u32(np,
		 OF_CAMERA_VCMDRV_T_SRC,
		(unsigned int *)&dev_vcm->t_src)) {
		dev_vcm->t_src = GT9760S_DEFAULT_T_SRC;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_SRC);
	}

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &dev_vcm->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &dev_vcm->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	return ret;
}

static int gt9760s_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct gt9760s_device *dev_vcm;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(&client->dev, "probing...\n");
	dev_info(&client->dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	dev_vcm = devm_kzalloc(&client->dev, sizeof(*dev_vcm),
				  GFP_KERNEL);
	if (!dev_vcm)
		return -ENOMEM;

	ret = gt9760s_read_dts(client, dev_vcm);
	if (ret)
		return ret;

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

	gt9760s_update_vcm_cfg(dev_vcm);

	dev_vcm->move_ms       = 0;
	dev_vcm->current_related_pos = VCMDRV_MAX_LOG;
	dev_vcm->start_move_tv = ns_to_timeval(ktime_get_ns());
	dev_vcm->end_move_tv = ns_to_timeval(ktime_get_ns());
	dev_vcm->vcm_movefull_t = gt9760s_move_time(dev_vcm, GT9760S_MAX_REG);

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
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gt9760s_device *dev_vcm = sd_to_vcm_dev(sd);

	gt9760s_init(client);
	gt9760s_set_pos(dev_vcm, dev_vcm->current_related_pos);
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
