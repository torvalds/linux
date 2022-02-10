// SPDX-License-Identifier: GPL-2.0
/*
 * fp5510 vcm driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/version.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <linux/rk_vcm_head.h>
#include <linux/gpio/consumer.h>
#include <linux/compat.h>

#define DRIVER_VERSION	KERNEL_VERSION(0, 0x01, 0x0)
#define FP5510_NAME			"fp5510"

#define FP5510_MAX_CURRENT		120U
#define FP5510_MAX_REG			1023U

#define FP5510_DEFAULT_START_CURRENT	0
#define FP5510_DEFAULT_RATED_CURRENT	100
#define FP5510_DEFAULT_STEP_MODE	0xd
#define REG_NULL			0xFF
#define ENABLE				0x1
#define DISABLE				0x0
/* fp5510 device structure */
struct fp5510_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_subdev sd;
	struct v4l2_device vdev;
	u16 current_val;
	struct gpio_desc *pwdn_gpio;

	unsigned short current_related_pos;
	unsigned short current_lens_pos;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step;
	unsigned int step_mode;
	unsigned int vcm_movefull_t;
	unsigned int esc_enable;
	unsigned int tsc_enable;
	unsigned int t_src;
	unsigned int mclk;

	struct __kernel_old_timeval start_move_tv;
	struct __kernel_old_timeval end_move_tv;
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

static const struct TimeTabel_s fp5510_lsc_time_table[] = {
	{0b10000, 136, 272, 544, 1088},
	{0b10001, 130, 260, 520, 1040},
	{0b10010, 125, 250, 500, 1000},
	{0b10011, 120, 240, 480, 960 },
	{0b10100, 116, 232, 464, 928 },
	{0b10101, 112, 224, 448, 896 },
	{0b10110, 108, 216, 432, 864 },
	{0b10111, 104, 208, 416, 832 },
	{0b11000, 101, 202, 404, 808 },
	{0b11001,  98, 196, 392, 784 },
	{0b11010,  95, 190, 380, 760 },
	{0b11011,  92, 184, 368, 736 },
	{0b11100,  89, 178, 356, 712 },
	{0b11101,  87, 174, 348, 696 },
	{0b11110,  85, 170, 340, 680 },
	{0b11111,  83, 166, 332, 664 },
	{0b00000,  81, 162, 324, 648 },
	{0b00001,  79, 158, 316, 632 },
	{0b00010,  77, 155, 310, 620 },
	{0b00011,  76, 152, 304, 608 },
	{0b00100,  74, 149, 298, 596 },
	{0b00101,  73, 146, 292, 584 },
	{0b00110,  71, 143, 286, 572 },
	{0b00111,  70, 140, 280, 560 },
	{0b01000,  69, 138, 276, 552 },
	{0b01001,  68, 136, 272, 544 },
	{0b01010,  67, 134, 268, 536 },
	{0b01011,  66, 132, 264, 528 },
	{0b01100,  65, 131, 262, 524 },
	{0b01101,  65, 130, 260, 520 },
	{0b01110,  64, 129, 258, 516 },
	{0b01111,  64, 128, 256, 512 },
	{REG_NULL,  0, 0, 0, 0},
};

static const struct TimeTabel_s fp5510_dlc_time_table[] = {/* us */
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

static inline struct fp5510_device *to_fp5510_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct fp5510_device, ctrls_vcm);
}

static inline struct fp5510_device *sd_to_fp5510_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct fp5510_device, sd);
}

static int __fp5510_power_on(struct fp5510_device *dev_vcm)
{
	if (!IS_ERR(dev_vcm->pwdn_gpio)) {
		gpiod_set_value_cansleep(dev_vcm->pwdn_gpio, 1);
		dev_err(dev_vcm->sd.dev, "power on success!\n");
	}

	return 0;
}

static int __fp5510_power_off(struct fp5510_device *dev_vcm)
{
	if (!IS_ERR(dev_vcm->pwdn_gpio)) {
		gpiod_set_value_cansleep(dev_vcm->pwdn_gpio, 0);
		dev_err(dev_vcm->sd.dev, "power off success!\n");
	}

	return 0;
}

static int fp5510_read_msg(struct i2c_client *client,
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
		msg->flags = I2C_M_RD;
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
		msleep(20);
	}
	dev_err(&client->dev,
		"%s: i2c write to failed with error %d\n", __func__, ret);
	return ret;
}

static int fp5510_write_msg(struct i2c_client *client,
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

		if (ret == 1) {
			dev_dbg(&client->dev,
				"%s: vcm i2c ok, addr 0x%x, data 0x%x, 0x%x\n",
				__func__, msg->addr, data[0], data[1]);
			return 0;
		}

		dev_info(&client->dev,
			"retrying I2C... %d\n", retries);
		retries++;
		msleep(20);
	}
	dev_err(&client->dev,
		"i2c write to failed with error %d msb:%d lsb:%d\n",
		ret, msb, lsb);
	return ret;
}

static unsigned int fp5510_move_time(struct fp5510_device *dev_vcm,
	unsigned int move_pos)
{
	unsigned int move_time_ms = 200;
	unsigned int step_period_lsc = 0;
	unsigned int step_period_dlc = 0;
	unsigned int step_period = 0;
	int i = 0;
	int table_cnt = 0;
	unsigned int step_case;
	unsigned int codes_per_step = 1;
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);

	if (dev_vcm->esc_enable == ENABLE ||
		dev_vcm->tsc_enable == ENABLE) {
		step_case = dev_vcm->mclk & 0x3;
		table_cnt = sizeof(fp5510_dlc_time_table) /
					sizeof(struct TimeTabel_s);
		for (i = 0; i < table_cnt; i++) {
			if (fp5510_dlc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	} else {
		step_case = dev_vcm->step_mode & 0x3;
		table_cnt = sizeof(fp5510_lsc_time_table) /
					sizeof(struct TimeTabel_s);
		for (i = 0; i < table_cnt; i++) {
			if (fp5510_lsc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	}

	if (i >= table_cnt)
		i = 0;

	switch (step_case) {
	case 0:
		step_period_lsc = fp5510_lsc_time_table[i].step00;
		step_period_dlc = fp5510_dlc_time_table[i].step00;
		break;
	case 1:
		step_period_lsc = fp5510_lsc_time_table[i].step01;
		step_period_dlc = fp5510_dlc_time_table[i].step01;
		break;
	case 2:
		step_period_lsc = fp5510_lsc_time_table[i].step10;
		step_period_dlc = fp5510_dlc_time_table[i].step10;
		break;
	case 3:
		step_period_lsc = fp5510_lsc_time_table[i].step11;
		step_period_dlc = fp5510_dlc_time_table[i].step11;
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

	if (dev_vcm->esc_enable == ENABLE || dev_vcm->tsc_enable == ENABLE)
		step_period = step_period_dlc;
	else
		step_period = step_period_lsc;

	if (dev_vcm->tsc_enable == ENABLE) {
		move_time_ms = step_period * move_pos / 1000;
	} else if (dev_vcm->esc_enable == ENABLE) {
		move_time_ms = step_period * 2 * move_pos / 1000;
	} else {
		if (!codes_per_step)
			move_time_ms = step_period * move_pos / 1000;
		else
			move_time_ms = step_period * move_pos /
				codes_per_step / 1000;
	}

	return move_time_ms;
}

static int fp5510_get_pos(struct fp5510_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int abs_step;

	ret = fp5510_read_msg(client, &msb, &lsb);
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

static int fp5510_set_pos(struct fp5510_device *dev_vcm,
	unsigned int dest_pos)
{
	int ret;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int position;
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);

	if (dest_pos >= VCMDRV_MAX_LOG)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (dev_vcm->step * (VCMDRV_MAX_LOG - dest_pos));

	if (position > FP5510_MAX_REG)
		position = FP5510_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;
	msb = (0x00U | ((dev_vcm->current_lens_pos & 0x3F0U) >> 4U));
	lsb = (((dev_vcm->current_lens_pos & 0x0FU) << 4U) |
		dev_vcm->step_mode);
	ret = fp5510_write_msg(client, msb, lsb);
	if (ret != 0)
		goto err;

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int fp5510_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fp5510_device *dev_vcm = to_fp5510_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return fp5510_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int fp5510_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fp5510_device *dev_vcm = to_fp5510_vcm(ctrl);
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

		ret = fp5510_set_pos(dev_vcm, dest_pos);

		dev_vcm->move_ms =
			((dev_vcm->vcm_movefull_t *
			(uint32_t)move_pos) /
			VCMDRV_MAX_LOG);

		dev_vcm->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
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

static const struct v4l2_ctrl_ops fp5510_vcm_ctrl_ops = {
	.g_volatile_ctrl = fp5510_get_ctrl,
	.s_ctrl = fp5510_set_ctrl,
};

static int fp5510_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int fp5510_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops fp5510_int_ops = {
	.open = fp5510_open,
	.close = fp5510_close,
};

static void fp5510_update_vcm_cfg(struct fp5510_device *dev_vcm)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int cur_dist;

	if (dev_vcm->max_ma == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	cur_dist = dev_vcm->vcm_cfg.rated_ma - dev_vcm->vcm_cfg.start_ma;
	cur_dist = cur_dist * FP5510_MAX_REG / dev_vcm->max_ma;
	dev_vcm->step = (cur_dist + (VCMDRV_MAX_LOG - 1)) / VCMDRV_MAX_LOG;
	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 FP5510_MAX_REG / dev_vcm->max_ma;
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

static long fp5510_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct fp5510_device *fp5510_dev = sd_to_fp5510_vcm(sd);
	struct rk_cam_vcm_tim *vcm_tim;
	struct rk_cam_vcm_cfg *vcm_cfg;
	int ret = 0;

	if (cmd == RK_VIDIOC_VCM_TIMEINFO) {
		vcm_tim = (struct rk_cam_vcm_tim *)arg;

		vcm_tim->vcm_start_t.tv_sec = fp5510_dev->start_move_tv.tv_sec;
		vcm_tim->vcm_start_t.tv_usec =
				fp5510_dev->start_move_tv.tv_usec;
		vcm_tim->vcm_end_t.tv_sec = fp5510_dev->end_move_tv.tv_sec;
		vcm_tim->vcm_end_t.tv_usec = fp5510_dev->end_move_tv.tv_usec;

		dev_dbg(&client->dev, "fp5510_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
			vcm_tim->vcm_start_t.tv_sec,
			vcm_tim->vcm_start_t.tv_usec,
			vcm_tim->vcm_end_t.tv_sec,
			vcm_tim->vcm_end_t.tv_usec);
	} else if (cmd == RK_VIDIOC_GET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		vcm_cfg->start_ma = fp5510_dev->vcm_cfg.start_ma;
		vcm_cfg->rated_ma = fp5510_dev->vcm_cfg.rated_ma;
		vcm_cfg->step_mode = fp5510_dev->vcm_cfg.step_mode;
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		vcm_cfg = (struct rk_cam_vcm_cfg *)arg;

		fp5510_dev->vcm_cfg.start_ma = vcm_cfg->start_ma;
		fp5510_dev->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		fp5510_dev->vcm_cfg.step_mode = vcm_cfg->step_mode;
		fp5510_update_vcm_cfg(fp5510_dev);
	} else {
		dev_err(&client->dev,
			"cmd 0x%x not supported\n", cmd);
		return -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long fp5510_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rk_cam_compat_vcm_tim __user *p32 = compat_ptr(arg);
	struct rk_cam_compat_vcm_tim compat_vcm_tim;
	struct rk_cam_vcm_tim vcm_tim;
	struct rk_cam_vcm_cfg vcm_cfg;
	long ret;

	if (cmd == RK_VIDIOC_COMPAT_VCM_TIMEINFO) {
		ret = fp5510_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
		compat_vcm_tim.vcm_start_t.tv_sec = vcm_tim.vcm_start_t.tv_sec;
		compat_vcm_tim.vcm_start_t.tv_usec =
				vcm_tim.vcm_start_t.tv_usec;
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
		ret = fp5510_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret) {
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
			if (ret)
				ret = -EFAULT;
		}
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = fp5510_ioctl(sd, cmd, &vcm_cfg);
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

static const struct v4l2_subdev_core_ops fp5510_core_ops = {
	.ioctl = fp5510_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = fp5510_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops fp5510_ops = {
	.core = &fp5510_core_ops,
};

static void fp5510_subdev_cleanup(struct fp5510_device *fp5510_dev)
{
	v4l2_device_unregister_subdev(&fp5510_dev->sd);
	v4l2_device_unregister(&fp5510_dev->vdev);
	v4l2_ctrl_handler_free(&fp5510_dev->ctrls_vcm);
	media_entity_cleanup(&fp5510_dev->sd.entity);
}

static int fp5510_init_controls(struct fp5510_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &fp5510_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
			  0, VCMDRV_MAX_LOG, 1, VCMDRV_MAX_LOG);

	if (hdl->error)
		dev_err(dev_vcm->sd.dev, "%s fail error: 0x%x\n",
			__func__, hdl->error);
	dev_vcm->sd.ctrl_handler = hdl;
	return hdl->error;
}

static int fp5510_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	struct fp5510_device *fp5510_dev;
	struct device *dev = &client->dev;
	unsigned int max_ma, start_ma, rated_ma, step_mode;
	struct v4l2_subdev *sd;
	char facing[2];
	unsigned char data = 0x0;
	int ret;

	dev_info(&client->dev, "probing...\n");
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&max_ma)) {
		max_ma = FP5510_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (max_ma == 0)
		max_ma = FP5510_MAX_CURRENT;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&start_ma)) {
		start_ma = FP5510_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&rated_ma)) {
		rated_ma = FP5510_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&step_mode)) {
		step_mode = FP5510_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}

	fp5510_dev = devm_kzalloc(&client->dev, sizeof(*fp5510_dev),
				  GFP_KERNEL);
	if (!fp5510_dev)
		return -ENOMEM;

	ret = of_property_read_u32(np, RKMODULE_CAMERA_MODULE_INDEX,
				   &fp5510_dev->module_index);
	ret |= of_property_read_string(np, RKMODULE_CAMERA_MODULE_FACING,
				       &fp5510_dev->module_facing);
	if (ret) {
		dev_err(&client->dev,
			"could not get module information!\n");
		return -EINVAL;
	}

	v4l2_i2c_subdev_init(&fp5510_dev->sd, client, &fp5510_ops);

	fp5510_dev->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(fp5510_dev->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	fp5510_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	fp5510_dev->sd.internal_ops = &fp5510_int_ops;

	ret = fp5510_init_controls(fp5510_dev);
	if (ret)
		goto err_cleanup;

	__fp5510_power_on(fp5510_dev);

	ret = media_entity_pads_init(&fp5510_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	sd = &fp5510_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(fp5510_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 fp5510_dev->module_index, facing,
		 FP5510_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	fp5510_dev->max_ma = max_ma;
	fp5510_dev->vcm_cfg.start_ma = start_ma;
	fp5510_dev->vcm_cfg.rated_ma = rated_ma;
	fp5510_dev->vcm_cfg.step_mode = step_mode;
	fp5510_update_vcm_cfg(fp5510_dev);
	fp5510_dev->move_ms       = 0;
	fp5510_dev->current_related_pos = VCMDRV_MAX_LOG;
	fp5510_dev->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	fp5510_dev->end_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	/*
	 * Note:
	 * 1. At ESC mode, ESC code=1 and TSC must be equal 0.
	 * 2. If ESC code=1 and TSC code=1, it get the TSC mode.
	 */
	fp5510_dev->esc_enable = DISABLE;
	fp5510_dev->tsc_enable = DISABLE;
	fp5510_dev->mclk = 0;
	fp5510_dev->t_src = 0x0;

	ret = fp5510_write_msg(client, 0xEC, 0xA3);
	if (ret != 0)
		dev_err(&client->dev,
			"%s: failed with error %d\n", __func__, ret);

	data = (fp5510_dev->mclk & 0x3) | 0x04 |
			(((fp5510_dev->tsc_enable << 0x3) |
			(fp5510_dev->esc_enable << 0x4)) & 0x18);
	ret = fp5510_write_msg(client, 0xA1, data);
	if (ret != 0)
		dev_err(&client->dev,
			"%s: failed with error %d\n", __func__, ret);

	data = (fp5510_dev->t_src << 0x3) & 0xf8;
	ret = fp5510_write_msg(client, 0xF2, data);
	if (ret != 0)
		dev_err(&client->dev,
			"%s: failed with error %d\n", __func__, ret);

	ret = fp5510_write_msg(client, 0xDC, 0x51);
	if (ret != 0)
		dev_err(&client->dev,
			"%s: failed with error %d\n", __func__, ret);

	fp5510_dev->vcm_movefull_t =
		fp5510_move_time(fp5510_dev, FP5510_MAX_REG);
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "probing successful\n");

	return 0;

err_cleanup:
	fp5510_subdev_cleanup(fp5510_dev);
	dev_err(&client->dev, "Probe failed: %d\n", ret);
	return ret;
}

static int fp5510_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct fp5510_device *fp5510_dev = sd_to_fp5510_vcm(sd);

	pm_runtime_disable(&client->dev);
	fp5510_subdev_cleanup(fp5510_dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__fp5510_power_off(fp5510_dev);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static int fp5510_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct fp5510_device *fp5510_dev = sd_to_fp5510_vcm(sd);

	__fp5510_power_on(fp5510_dev);
	fp5510_set_pos(fp5510_dev, fp5510_dev->current_related_pos);

	return 0;
}

static int fp5510_vcm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct fp5510_device *fp5510_dev = sd_to_fp5510_vcm(sd);

	__fp5510_power_off(fp5510_dev);

	return 0;
}

static const struct i2c_device_id fp5510_id_table[] = {
	{ FP5510_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fp5510_id_table);

static const struct of_device_id fp5510_of_table[] = {
	{ .compatible = "fitipower,fp5510" },
	{ }
};
MODULE_DEVICE_TABLE(of, fp5510_of_table);

static const struct dev_pm_ops fp5510_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fp5510_vcm_suspend, fp5510_vcm_resume)
	SET_RUNTIME_PM_OPS(fp5510_vcm_suspend, fp5510_vcm_resume, NULL)
};

static struct i2c_driver fp5510_i2c_driver = {
	.driver = {
		.name = FP5510_NAME,
		.pm = &fp5510_pm_ops,
		.of_match_table = fp5510_of_table,
	},
	.probe = &fp5510_probe,
	.remove = &fp5510_remove,
	.id_table = fp5510_id_table,
};

module_i2c_driver(fp5510_i2c_driver);

MODULE_DESCRIPTION("FP5510 VCM driver");
MODULE_LICENSE("GPL v2");
