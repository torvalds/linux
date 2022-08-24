// SPDX-License-Identifier: GPL-2.0
/*
 * cn3927v vcm driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 reduce vcm collision noise.
 * V0.0X01.0X02 check dev connection before register.
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

#define OF_CAMERA_VCMDRV_EDLC_ENABLE	"rockchip,vcm-edlc-enable"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x2)
#define CN3927V_NAME			"cn3927v"

#define CN3927V_MAX_CURRENT		120U
#define CN3927V_MAX_REG			1023U
#define CN3927V_GRADUAL_MOVELENS_STEPS	32

#define CN3927V_DEFAULT_START_CURRENT	0
#define CN3927V_DEFAULT_RATED_CURRENT	100
#define CN3927V_DEFAULT_STEP_MODE	0xd
#define CN3927V_DEFAULT_EDLC_EN		0x0
#define CN3927V_DEFAULT_DLC_EN		0x0
#define CN3927V_DEFAULT_MCLK		0x0
#define CN3927V_DEFAULT_T_SRC		0x0
#define REG_NULL			0xFF

/* cn3927v advanced mode */
#define CN3927V_ADVMODE_IC_INFO		0x00
#define CN3927V_ADVMODE_IC_VER		0x01
#define CN3927V_ADVMODE_CONTROL		0x02
#define CN3927V_ADVMODE_VCM_MSB		0x03
#define CN3927V_ADVMODE_VCM_LSB		0x04
#define CN3927V_ADVMODE_STATUS		0x05
#define CN3927V_ADVMODE_SAC_CFG		0x06
#define CN3927V_ADVMODE_PRESC		0x07
#define CN3927V_ADVMODE_SAC_TIME	0x08
#define CN3927V_ADVMODE_PRESET		0x09
#define CN3927V_ADVMODE_NRC		0x0A
#define CN3927V_ADVMODE_RING_EN		1

#define CN3927V_DEFAULT_ADVMODE		0x00
#define CN3927V_DEFAULT_SAC_MODE	0x04
#define CN3927V_DEFAULT_SAC_TIME	0x0A
#define CN3927V_DEFAULT_SAC_PRESCL	0x02
#define CN3927V_DEFAULT_NRC_EN		0x00
#define CN3927V_DEFAULT_NRC_MODE	0x00
#define CN3927V_DEFAULT_NRC_PRESET	0x00
#define CN3927V_DEFAULT_NRC_INFL	0x00
#define CN3927V_DEFAULT_NRC_TIME	0x00

/* cn3927v device structure */
struct cn3927v_device {
	struct v4l2_ctrl_handler ctrls_vcm;
	struct v4l2_ctrl *focus;
	struct v4l2_subdev sd;
	struct v4l2_device vdev;
	u16 current_val;

	unsigned short current_related_pos;
	unsigned short current_lens_pos;
	unsigned int max_current;
	unsigned int start_current;
	unsigned int rated_current;
	unsigned int step_mode;
	unsigned int vcm_movefull_t;
	unsigned int edlc_enable;
	unsigned int dlc_enable;
	unsigned int t_src;
	unsigned int mclk;
	unsigned int max_logicalpos;

	/* advanced mode*/
	unsigned char adcanced_mode;
	unsigned char sac_mode;
	unsigned char sac_time;
	unsigned char sac_prescl;
	unsigned char nrc_en;
	unsigned char nrc_mode;
	unsigned char nrc_preset;
	unsigned char nrc_infl;
	unsigned char nrc_time;

	struct __kernel_old_timeval start_move_tv;
	struct __kernel_old_timeval end_move_tv;
	unsigned long move_ms;

	u32 module_index;
	const char *module_facing;
	struct rk_cam_vcm_cfg vcm_cfg;

	struct gpio_desc *xsd_gpio;
	struct regulator *supply;
	struct i2c_client *client;
	bool power_on;
};

struct TimeTabel_s {
	unsigned int t_src;/* time of slew rate control */
	unsigned int step00;/* S[1:0] /MCLK[1:0] step period */
	unsigned int step01;
	unsigned int step10;
	unsigned int step11;
};

static const struct TimeTabel_s cn3927v_lsc_time_table[] = {/* 1/10us */
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

static const struct TimeTabel_s cn3927v_dlc_time_table[] = {/* us */
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

static inline struct cn3927v_device *to_cn3927v_vcm(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct cn3927v_device, ctrls_vcm);
}

static inline struct cn3927v_device *sd_to_cn3927v_vcm(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct cn3927v_device, sd);
}

static int cn3927v_read_msg(struct i2c_client *client,
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
		"%s: i2c read to failed with error %d\n", __func__, ret);
	return ret;
}

static int cn3927v_write_msg(struct i2c_client *client,
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
		msleep(20);
	}
	dev_err(&client->dev,
		"i2c write to failed with error %d\n", ret);
	return ret;
}

/* Write registers up to 4 at a time */
static int cn3927v_write_reg(struct i2c_client *client, u8 reg, u32 len, u32 val)
{
	u32 buf_i, val_i, retries;
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

	for (retries = 0; retries < 5; retries++) {
		if (i2c_master_send(client, buf, len + 1) == len + 1) {
			dev_dbg(&client->dev,
				"%s: vcm i2c ok, reg 0x%x, val 0x%x, len 0x%x\n",
				__func__, reg, val, len);
			return 0;
		}

		dev_info(&client->dev,
			"retrying I2C... %d\n", retries);
		msleep(20);
	}

	dev_err(&client->dev, "Failed to write 0x%04x,0x%x\n", reg, val);
	return -EIO;
}

/* Read registers up to 4 at a time */
static int cn3927v_read_reg(struct i2c_client *client, u8 reg, u32 len, u32 *val)
{
	struct i2c_msg msgs[2];
	__be32 data_be = 0;
	u8 *data_be_p;
	u32 retries;
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

	for (retries = 0; retries < 5; retries++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs)) {
			*val = be32_to_cpu(data_be);
			dev_dbg(&client->dev,
				"%s: vcm i2c ok, reg 0x%x, val 0x%x\n",
				__func__, reg, *val);
			return 0;
		}
	}

	dev_err(&client->dev,
		"%s: i2c read to failed with error %d\n", __func__, ret);
	return -EIO;
}

static unsigned int cn3927v_move_time(struct cn3927v_device *dev_vcm,
	unsigned int move_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int move_time_ms = 200;
	unsigned int step_period_lsc = 0;
	unsigned int step_period_dlc = 0;
	unsigned int codes_per_step = 1;
	unsigned int step_case;
	unsigned int sac_prescl;
	int table_cnt = 0;
	int i = 0;

	if (dev_vcm->adcanced_mode) {
		// sac setting time = tvib = (3.81ms+(SACT[6:0]*0.03ms)) * PRESC[1:0]))
		sac_prescl = 1 << dev_vcm->sac_prescl;
		move_time_ms = (((381 + 3 * dev_vcm->sac_time)) * sac_prescl + 50) / 100;
		return move_time_ms;
	} else if (dev_vcm->dlc_enable) {
		step_case = dev_vcm->mclk & 0x3;
		table_cnt = sizeof(cn3927v_dlc_time_table) /
					sizeof(struct TimeTabel_s);
		for (i = 0; i < table_cnt; i++) {
			if (cn3927v_dlc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	} else {
		step_case = dev_vcm->step_mode & 0x3;
		table_cnt = sizeof(cn3927v_lsc_time_table) /
					sizeof(struct TimeTabel_s);
		for (i = 0; i < table_cnt; i++) {
			if (cn3927v_lsc_time_table[i].t_src == dev_vcm->t_src)
				break;
		}
	}

	if (i >= table_cnt)
		i = 0;

	switch (step_case) {
	case 0:
		step_period_lsc = cn3927v_lsc_time_table[i].step00;
		step_period_dlc = cn3927v_dlc_time_table[i].step00;
		break;
	case 1:
		step_period_lsc = cn3927v_lsc_time_table[i].step01;
		step_period_dlc = cn3927v_dlc_time_table[i].step01;
		break;
	case 2:
		step_period_lsc = cn3927v_lsc_time_table[i].step10;
		step_period_dlc = cn3927v_dlc_time_table[i].step10;
		break;
	case 3:
		step_period_lsc = cn3927v_lsc_time_table[i].step11;
		step_period_dlc = cn3927v_dlc_time_table[i].step11;
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

	if (!dev_vcm->dlc_enable) {
		if (!codes_per_step)
			move_time_ms = (step_period_lsc * move_pos + 9999) / 10000;
		else
			move_time_ms = (step_period_lsc * move_pos / codes_per_step + 9999) / 10000;
	} else {
		move_time_ms = (step_period_dlc + 999) / 1000;
	}

	return move_time_ms;
}

static int cn3927v_get_dac(struct cn3927v_device *dev_vcm, unsigned int *cur_dac)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret;
	unsigned char lsb = 0;
	unsigned char msb = 0;
	unsigned int abs_step;

	if (dev_vcm->adcanced_mode) {
		ret = cn3927v_read_reg(client, CN3927V_ADVMODE_VCM_MSB, 2, &abs_step);
		if (ret != 0)
			goto err;
	} else {
		ret = cn3927v_read_msg(client, &msb, &lsb);
		if (ret != 0)
			goto err;

		abs_step = (((unsigned int)(msb & 0x3FU)) << 4U) |
			   (((unsigned int)lsb) >> 4U);
	}

	*cur_dac = abs_step;
	dev_dbg(&client->dev, "%s: get dac %d\n", __func__, *cur_dac);
	return 0;

err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int cn3927v_set_dac(struct cn3927v_device *dev_vcm,
	unsigned int dest_dac)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	int ret;

	if (dev_vcm->adcanced_mode) {
		unsigned int i;
		bool vcm_idle = false;

		/* wait for I2C bus idle */
		vcm_idle = false;
		for (i = 0; i < 10; i++) {
			unsigned int status = 0;

			cn3927v_read_reg(client, CN3927V_ADVMODE_STATUS, 1, &status);
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
		ret = cn3927v_write_reg(client, CN3927V_ADVMODE_VCM_MSB,
					2, dest_dac);
		if (ret != 0)
			goto err;
	} else {
		unsigned char msb, lsb;

		msb = (0x00U | ((dest_dac & 0x3F0U) >> 4U));
		lsb = (((dest_dac & 0x0FU) << 4U) | dev_vcm->step_mode);
		ret = cn3927v_write_msg(client, msb, lsb);
		if (ret != 0)
			goto err;
	}

	return ret;
err:
	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int cn3927v_get_pos(struct cn3927v_device *dev_vcm,
	unsigned int *cur_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int dac, position, range;
	int ret;

	range = dev_vcm->rated_current - dev_vcm->start_current;
	ret = cn3927v_get_dac(dev_vcm, &dac);
	if (!ret) {
		if (dac <= dev_vcm->start_current) {
			position = dev_vcm->max_logicalpos;
		} else if ((dac > dev_vcm->start_current) &&
			 (dac <= dev_vcm->rated_current)) {
			position = (dac - dev_vcm->start_current) * dev_vcm->max_logicalpos / range;
			position = dev_vcm->max_logicalpos - position;
		} else {
			position = 0;
		}

		*cur_pos = position;

		dev_dbg(&client->dev, "%s: get position %d, dac %d\n", __func__, *cur_pos, dac);
		return 0;
	}

	dev_err(&client->dev,
		"%s: failed with error %d\n", __func__, ret);
	return ret;
}

static int cn3927v_set_pos(struct cn3927v_device *dev_vcm,
	unsigned int dest_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int position;
	unsigned int range;
	int ret;

	range = dev_vcm->rated_current - dev_vcm->start_current;
	if (dest_pos >= dev_vcm->max_logicalpos)
		position = dev_vcm->start_current;
	else
		position = dev_vcm->start_current +
			   (range * (dev_vcm->max_logicalpos - dest_pos) / dev_vcm->max_logicalpos);

	if (position > CN3927V_MAX_REG)
		position = CN3927V_MAX_REG;

	dev_vcm->current_lens_pos = position;
	dev_vcm->current_related_pos = dest_pos;

	ret = cn3927v_set_dac(dev_vcm, position);
	dev_dbg(&client->dev, "%s: set position %d, dac %d\n", __func__, dest_pos, position);

	return ret;
}

static int cn3927v_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cn3927v_device *dev_vcm = to_cn3927v_vcm(ctrl);

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE)
		return cn3927v_get_pos(dev_vcm, &ctrl->val);

	return -EINVAL;
}

static int cn3927v_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cn3927v_device *dev_vcm = to_cn3927v_vcm(ctrl);
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);
	unsigned int dest_pos = ctrl->val;
	int move_pos;
	long mv_us;
	int ret = 0;

	if (ctrl->id == V4L2_CID_FOCUS_ABSOLUTE) {
		if (dest_pos > dev_vcm->max_logicalpos) {
			dev_err(&client->dev,
				"%s dest_pos is error. %d > %d\n",
				__func__, dest_pos, dev_vcm->max_logicalpos);
			return -EINVAL;
		}
		/* calculate move time */
		move_pos = dev_vcm->current_related_pos - dest_pos;
		if (move_pos < 0)
			move_pos = -move_pos;

		ret = cn3927v_set_pos(dev_vcm, dest_pos);
		if (dev_vcm->dlc_enable || dev_vcm->adcanced_mode)
			dev_vcm->move_ms = dev_vcm->vcm_movefull_t;
		else
			dev_vcm->move_ms =
				((dev_vcm->vcm_movefull_t * (uint32_t)move_pos) /
				dev_vcm->max_logicalpos);

		dev_dbg(&client->dev,
			"dest_pos %d, dac %d, move_ms %ld\n",
			dest_pos, dev_vcm->current_lens_pos, dev_vcm->move_ms);

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

static const struct v4l2_ctrl_ops cn3927v_vcm_ctrl_ops = {
	.g_volatile_ctrl = cn3927v_get_ctrl,
	.s_ctrl = cn3927v_set_ctrl,
};

static int cn3927v_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rval;

	rval = pm_runtime_get_sync(sd->dev);
	if (rval < 0) {
		pm_runtime_put_noidle(sd->dev);
		return rval;
	}

	return 0;
}

static int cn3927v_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

static const struct v4l2_subdev_internal_ops cn3927v_int_ops = {
	.open = cn3927v_open,
	.close = cn3927v_close,
};

static void cn3927v_update_vcm_cfg(struct cn3927v_device *dev_vcm)
{
	struct i2c_client *client = v4l2_get_subdevdata(&dev_vcm->sd);

	if (dev_vcm->max_current == 0) {
		dev_err(&client->dev, "max current is zero");
		return;
	}

	dev_vcm->start_current = dev_vcm->vcm_cfg.start_ma *
				 CN3927V_MAX_REG / dev_vcm->max_current;
	dev_vcm->rated_current = dev_vcm->vcm_cfg.rated_ma *
				 CN3927V_MAX_REG / dev_vcm->max_current;
	dev_vcm->step_mode = dev_vcm->vcm_cfg.step_mode;

	dev_dbg(&client->dev,
		"vcm_cfg: %d, %d, %d, max_current %d\n",
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->vcm_cfg.step_mode,
		dev_vcm->max_current);
}

static long cn3927v_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct cn3927v_device *dev_vcm = sd_to_cn3927v_vcm(sd);
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

		dev_dbg(&client->dev, "cn3927v_get_move_res 0x%lx, 0x%lx, 0x%lx, 0x%lx\n",
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

		if (vcm_cfg->rated_ma > CN3927V_MAX_CURRENT) {
			dev_warn(&client->dev,
				 "vcm_cfg use dac value, do convert!\n");
			vcm_cfg->rated_ma = vcm_cfg->rated_ma *
					    dev_vcm->max_current / CN3927V_MAX_REG;
			vcm_cfg->start_ma = vcm_cfg->start_ma *
					    dev_vcm->max_current / CN3927V_MAX_REG;
		}

		dev_vcm->vcm_cfg.start_ma = vcm_cfg->start_ma;
		dev_vcm->vcm_cfg.rated_ma = vcm_cfg->rated_ma;
		dev_vcm->vcm_cfg.step_mode = vcm_cfg->step_mode;
		cn3927v_update_vcm_cfg(dev_vcm);
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
static long cn3927v_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = cn3927v_ioctl(sd, RK_VIDIOC_VCM_TIMEINFO, &vcm_tim);
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
		ret = cn3927v_ioctl(sd, RK_VIDIOC_GET_VCM_CFG, &vcm_cfg);
		if (!ret) {
			ret = copy_to_user(up, &vcm_cfg, sizeof(vcm_cfg));
			if (ret)
				ret = -EFAULT;
		}
	} else if (cmd == RK_VIDIOC_SET_VCM_CFG) {
		ret = copy_from_user(&vcm_cfg, up, sizeof(vcm_cfg));
		if (!ret)
			ret = cn3927v_ioctl(sd, cmd, &vcm_cfg);
		else
			ret = -EFAULT;
	} else if (cmd == RK_VIDIOC_SET_VCM_MAX_LOGICALPOS) {
		ret = copy_from_user(&max_logicalpos, up, sizeof(max_logicalpos));
		if (!ret)
			ret = cn3927v_ioctl(sd, cmd, &max_logicalpos);
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

static const struct v4l2_subdev_core_ops cn3927v_core_ops = {
	.ioctl = cn3927v_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cn3927v_compat_ioctl32
#endif
};

static const struct v4l2_subdev_ops cn3927v_ops = {
	.core = &cn3927v_core_ops,
};

static void cn3927v_subdev_cleanup(struct cn3927v_device *cn3927v_dev)
{
	v4l2_device_unregister_subdev(&cn3927v_dev->sd);
	v4l2_device_unregister(&cn3927v_dev->vdev);
	v4l2_ctrl_handler_free(&cn3927v_dev->ctrls_vcm);
	media_entity_cleanup(&cn3927v_dev->sd.entity);
}

static int cn3927v_init_controls(struct cn3927v_device *dev_vcm)
{
	struct v4l2_ctrl_handler *hdl = &dev_vcm->ctrls_vcm;
	const struct v4l2_ctrl_ops *ops = &cn3927v_vcm_ctrl_ops;

	v4l2_ctrl_handler_init(hdl, 1);

	dev_vcm->focus = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FOCUS_ABSOLUTE,
				0, dev_vcm->max_logicalpos, 1, dev_vcm->max_logicalpos);

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
	struct cn3927v_device *dev_vcm = sd_to_cn3927v_vcm(sd);
	int val = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &val);
	if (!ret)
		cn3927v_set_dac(dev_vcm, val);

	return count;
}

static ssize_t get_dacval(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cn3927v_device *dev_vcm = sd_to_cn3927v_vcm(sd);
	unsigned int dac = 0;

	cn3927v_get_dac(dev_vcm, &dac);
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

static int __cn3927v_set_power(struct cn3927v_device *cn3927v, bool on)
{
	struct i2c_client *client = cn3927v->client;
	int ret = 0;

	if (cn3927v->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = regulator_enable(cn3927v->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable regulator\n");
			goto unlock_and_return;
		}
		cn3927v->power_on = true;
	} else {
		ret = regulator_disable(cn3927v->supply);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to disable regulator\n");
			goto unlock_and_return;
		}
		cn3927v->power_on = false;
	}

unlock_and_return:
	return ret;
}

static int cn3927v_check_i2c(struct cn3927v_device *cn3927v,
				  struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret;

	// need to wait 1ms after poweron
	usleep_range(1000, 1200);
	// Advanced Mode TEST set
	ret = cn3927v_write_msg(client, 0xED, 0xAB);
	if (!ret)
		dev_info(dev, "Check cn3927v connection OK!\n");
	else
		dev_info(dev, "cn3927v not connect!\n");
	return ret;
}

static int cn3927v_configure_regulator(struct cn3927v_device *cn3927v)
{
	struct i2c_client *client = cn3927v->client;
	int ret = 0;

	cn3927v->supply = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(cn3927v->supply)) {
		ret = PTR_ERR(cn3927v->supply);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "could not get regulator avdd\n");
		return ret;
	}
	cn3927v->power_on = false;
	return ret;
}

static int cn3927v_parse_dt_property(struct i2c_client *client,
				    struct cn3927v_device *dev_vcm)
{
	struct device_node *np = of_node_get(client->dev.of_node);
	int ret;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MAX_CURRENT,
		(unsigned int *)&dev_vcm->max_current)) {
		dev_vcm->max_current = CN3927V_MAX_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MAX_CURRENT);
	}
	if (dev_vcm->max_current == 0)
		dev_vcm->max_current = CN3927V_MAX_CURRENT;

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_START_CURRENT,
		(unsigned int *)&dev_vcm->vcm_cfg.start_ma)) {
		dev_vcm->vcm_cfg.start_ma = CN3927V_DEFAULT_START_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_START_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_RATED_CURRENT,
		(unsigned int *)&dev_vcm->vcm_cfg.rated_ma)) {
		dev_vcm->vcm_cfg.rated_ma = CN3927V_DEFAULT_RATED_CURRENT;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_RATED_CURRENT);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_STEP_MODE,
		(unsigned int *)&dev_vcm->vcm_cfg.step_mode)) {
		dev_vcm->vcm_cfg.step_mode = CN3927V_DEFAULT_STEP_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_STEP_MODE);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_EDLC_ENABLE,
		(unsigned int *)&dev_vcm->edlc_enable)) {
		dev_vcm->edlc_enable = CN3927V_DEFAULT_EDLC_EN;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_EDLC_ENABLE);
	}

	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_DLC_ENABLE,
		(unsigned int *)&dev_vcm->dlc_enable)) {
		dev_vcm->dlc_enable = CN3927V_DEFAULT_DLC_EN;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_DLC_ENABLE);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_MCLK,
		(unsigned int *)&dev_vcm->mclk)) {
		dev_vcm->mclk = CN3927V_DEFAULT_MCLK;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_MCLK);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_T_SRC,
		(unsigned int *)&dev_vcm->t_src)) {
		dev_vcm->t_src = CN3927V_DEFAULT_T_SRC;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_T_SRC);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_ADVANCED_MODE,
		(unsigned int *)&dev_vcm->adcanced_mode)) {
		dev_vcm->adcanced_mode = CN3927V_DEFAULT_ADVMODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_ADVANCED_MODE);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_SAC_MODE,
		(unsigned int *)&dev_vcm->sac_mode)) {
		dev_vcm->sac_mode = CN3927V_DEFAULT_SAC_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_SAC_MODE);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_SAC_TIME,
		(unsigned int *)&dev_vcm->sac_time)) {
		dev_vcm->sac_time = CN3927V_DEFAULT_SAC_TIME;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_SAC_TIME);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_PRESC,
		(unsigned int *)&dev_vcm->sac_prescl)) {
		dev_vcm->sac_prescl = CN3927V_DEFAULT_SAC_PRESCL;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_PRESC);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_NRC_EN,
		(unsigned int *)&dev_vcm->nrc_en)) {
		dev_vcm->nrc_en = CN3927V_DEFAULT_NRC_EN;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_NRC_EN);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_NRC_MODE,
		(unsigned int *)&dev_vcm->nrc_mode)) {
		dev_vcm->nrc_mode = CN3927V_DEFAULT_NRC_MODE;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_NRC_MODE);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_NRC_PRESET,
		(unsigned int *)&dev_vcm->nrc_preset)) {
		dev_vcm->nrc_preset = CN3927V_DEFAULT_NRC_PRESET;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_NRC_PRESET);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_NRC_INFL,
		(unsigned int *)&dev_vcm->nrc_infl)) {
		dev_vcm->nrc_infl = CN3927V_DEFAULT_NRC_INFL;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_NRC_INFL);
	}
	if (of_property_read_u32(np,
		OF_CAMERA_VCMDRV_NRC_TIME,
		(unsigned int *)&dev_vcm->nrc_time)) {
		dev_vcm->nrc_time = CN3927V_DEFAULT_NRC_TIME;
		dev_info(&client->dev,
			"could not get module %s from dts!\n",
			OF_CAMERA_VCMDRV_NRC_TIME);
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
	ret = cn3927v_configure_regulator(dev_vcm);
	if (ret) {
		dev_err(&client->dev, "Failed to get power regulator!\n");
		return ret;
	}

	dev_dbg(&client->dev, "current: %d, %d, %d, dlc_en: %d, t_src: %d, mclk: %d",
		dev_vcm->max_current,
		dev_vcm->vcm_cfg.start_ma,
		dev_vcm->vcm_cfg.rated_ma,
		dev_vcm->dlc_enable,
		dev_vcm->t_src,
		dev_vcm->mclk);

	/* advanced mode*/
	dev_info(&client->dev, "adcanced: %d, sac: %d, %d, %d, nrc: %d, %d, %d, %d, %d",
		dev_vcm->adcanced_mode,
		dev_vcm->sac_mode,
		dev_vcm->sac_time,
		dev_vcm->sac_prescl,
		dev_vcm->nrc_en,
		dev_vcm->nrc_mode,
		dev_vcm->nrc_preset,
		dev_vcm->nrc_infl,
		dev_vcm->nrc_time);

	return 0;
}

static int cn3927v_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct cn3927v_device *cn3927v_dev;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x, probing...",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	cn3927v_dev = devm_kzalloc(dev, sizeof(*cn3927v_dev),
				  GFP_KERNEL);
	if (cn3927v_dev == NULL)
		return -ENOMEM;

	ret = cn3927v_parse_dt_property(client, cn3927v_dev);
	if (ret)
		return ret;
	v4l2_i2c_subdev_init(&cn3927v_dev->sd, client, &cn3927v_ops);
	cn3927v_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	cn3927v_dev->sd.internal_ops = &cn3927v_int_ops;

	cn3927v_dev->max_logicalpos = VCMDRV_MAX_LOG;
	ret = cn3927v_init_controls(cn3927v_dev);
	if (ret)
		goto err_cleanup;

	ret = media_entity_pads_init(&cn3927v_dev->sd.entity, 0, NULL);
	if (ret < 0)
		goto err_cleanup;

	ret = __cn3927v_set_power(cn3927v_dev, true);
	if (ret)
		goto err_cleanup;

	ret = cn3927v_check_i2c(cn3927v_dev, client);
	if (ret)
		goto err_power_off;

	sd = &cn3927v_dev->sd;
	sd->entity.function = MEDIA_ENT_F_LENS;

	memset(facing, 0, sizeof(facing));
	if (strcmp(cn3927v_dev->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 cn3927v_dev->module_index, facing,
		 CN3927V_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev(sd);
	if (ret)
		dev_err(&client->dev, "v4l2 async register subdev failed\n");

	cn3927v_update_vcm_cfg(cn3927v_dev);
	cn3927v_dev->move_ms       = 0;
	cn3927v_dev->current_related_pos = cn3927v_dev->max_logicalpos;
	cn3927v_dev->current_lens_pos = cn3927v_dev->start_current;
	cn3927v_dev->start_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	cn3927v_dev->end_move_tv = ns_to_kernel_old_timeval(ktime_get_ns());
	cn3927v_dev->vcm_movefull_t =
		cn3927v_move_time(cn3927v_dev, CN3927V_MAX_REG);

	pm_runtime_enable(dev);

	add_sysfs_interfaces(dev);
	dev_info(dev, "probing successful\n");

	return 0;
err_power_off:
	__cn3927v_set_power(cn3927v_dev, false);

err_cleanup:
	cn3927v_subdev_cleanup(cn3927v_dev);
	dev_err(&client->dev, "Probe failed: %d\n", ret);
	return ret;
}

static int cn3927v_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cn3927v_device *cn3927v_dev = sd_to_cn3927v_vcm(sd);

	remove_sysfs_interfaces(&client->dev);
	pm_runtime_disable(&client->dev);
	cn3927v_subdev_cleanup(cn3927v_dev);

	return 0;
}

static int cn3927v_init(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cn3927v_device *cn3927v_dev = sd_to_cn3927v_vcm(sd);
	unsigned char data = 0x0;
	int ret = 0;

	if (cn3927v_dev->adcanced_mode) {
		// need to wait 1ms after poweron
		usleep_range(1000, 1200);
		// Advanced Mode
		ret = cn3927v_write_msg(client, 0xED, 0xAB);
		if (ret)
			goto err;
		// Power down
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_CONTROL, 0x01);
		if (ret)
			goto err;
		// active
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_CONTROL, 0x00);
		if (ret)
			goto err;
		// delay 1ms
		usleep_range(1000, 1200);


		// Set Tvib (PRESC[1:0] )
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_PRESC, cn3927v_dev->sac_prescl);
		if (ret)
			goto err;
		// Set Tvib (SACT[6:0] )
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_SAC_TIME, cn3927v_dev->sac_time);
		if (ret)
			goto err;
		// nrc preset
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_PRESET, cn3927v_dev->nrc_preset);
		if (ret)
			goto err;
		// nrc en & nrc mode
		data = (cn3927v_dev->nrc_en & 0x1) << 1 |
		       (cn3927v_dev->nrc_mode & 0x1);
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_NRC, data);
		if (ret)
			goto err;

		// SAC mode & nrc_time & nrc_infl
		data = CN3927V_ADVMODE_RING_EN << 7 |
			   (cn3927v_dev->nrc_infl & 0x3) << 5 |
			   (cn3927v_dev->nrc_time & 0x1) << 4 |
			   (cn3927v_dev->sac_mode & 0xF);
		ret = cn3927v_write_msg(client, CN3927V_ADVMODE_SAC_CFG, data);
		if (ret)
			goto err;

	} else {
		// need to wait 1ms after poweron
		usleep_range(1000, 1200);

		ret = cn3927v_write_msg(client, 0xEC, 0xA3);
		if (ret)
			goto err;

		data = (cn3927v_dev->mclk & 0x3) | 0x04 |
				((cn3927v_dev->dlc_enable << 0x3) & 0x08);
		ret = cn3927v_write_msg(client, 0xA1, data);
		if (ret)
			goto err;

		data = (cn3927v_dev->t_src << 0x3) & 0xf8;
		ret = cn3927v_write_msg(client, 0xF2, data);
		if (ret)
			goto err;

		ret = cn3927v_write_msg(client, 0xDC, 0x51);
		if (ret)
			goto err;

		/* set normal mode */
		ret = cn3927v_write_msg(client, 0xDF, 0x5B);
		if (ret != 0)
			dev_err(&client->dev,
				"%s: failed with error %d\n", __func__, ret);
	}

	return 0;
err:
	dev_err(&client->dev, "failed with error %d\n", ret);
	return -1;
}

static int __maybe_unused cn3927v_vcm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cn3927v_device *dev_vcm = sd_to_cn3927v_vcm(sd);
	int dac = dev_vcm->current_lens_pos;
	unsigned int move_time;

	dev_dbg(&client->dev, "%s: current_lens_pos %d, current_related_pos %d\n",
		__func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);
	move_time = 1000 * cn3927v_move_time(dev_vcm, CN3927V_GRADUAL_MOVELENS_STEPS);
	while (dac >= CN3927V_GRADUAL_MOVELENS_STEPS) {
		cn3927v_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 1000);
		dac -= CN3927V_GRADUAL_MOVELENS_STEPS;
		if (dac <= 0)
			break;
	}

	if (dac < CN3927V_GRADUAL_MOVELENS_STEPS) {
		dac = CN3927V_GRADUAL_MOVELENS_STEPS;
		cn3927v_set_dac(dev_vcm, dac);
	}
	/* set to power down mode */
	if (dev_vcm->adcanced_mode) {
		cn3927v_write_reg(client, 0x02, 1, 0x01);
	} else {
		/* Ringing off */
		cn3927v_write_msg(client, 0xDC, 0x51);
	}

	__cn3927v_set_power(dev_vcm, false);
	return 0;
}

static int __maybe_unused cn3927v_vcm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct cn3927v_device *dev_vcm = sd_to_cn3927v_vcm(sd);
	unsigned int move_time;
	int dac = dev_vcm->start_current;


	__cn3927v_set_power(dev_vcm, true);
	cn3927v_init(client);

	usleep_range(1000, 1200);
	dev_dbg(&client->dev, "%s: current_lens_pos %d, current_related_pos %d\n",
		__func__, dev_vcm->current_lens_pos, dev_vcm->current_related_pos);
	move_time = 1000 * cn3927v_move_time(dev_vcm, CN3927V_GRADUAL_MOVELENS_STEPS);
	while (dac <= dev_vcm->current_lens_pos) {
		cn3927v_set_dac(dev_vcm, dac);
		usleep_range(move_time, move_time + 1000);
		dac += CN3927V_GRADUAL_MOVELENS_STEPS;
		if (dac >= dev_vcm->current_lens_pos)
			break;
	}

	if (dac > dev_vcm->current_lens_pos) {
		dac = dev_vcm->current_lens_pos;
		cn3927v_set_dac(dev_vcm, dac);
	}

	return 0;
}

static const struct i2c_device_id cn3927v_id_table[] = {
	{ CN3927V_NAME, 0 },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(i2c, cn3927v_id_table);

static const struct of_device_id cn3927v_of_table[] = {
	{ .compatible = "chipnext,cn3927v" },
	{ { 0 } }
};
MODULE_DEVICE_TABLE(of, cn3927v_of_table);

static const struct dev_pm_ops cn3927v_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cn3927v_vcm_suspend, cn3927v_vcm_resume)
	SET_RUNTIME_PM_OPS(cn3927v_vcm_suspend, cn3927v_vcm_resume, NULL)
};

static struct i2c_driver cn3927v_i2c_driver = {
	.driver = {
		.name = CN3927V_NAME,
		.pm = &cn3927v_pm_ops,
		.of_match_table = cn3927v_of_table,
	},
	.probe = &cn3927v_probe,
	.remove = &cn3927v_remove,
	.id_table = cn3927v_id_table,
};

module_i2c_driver(cn3927v_i2c_driver);

MODULE_DESCRIPTION("CN3927V VCM driver");
MODULE_LICENSE("GPL");
