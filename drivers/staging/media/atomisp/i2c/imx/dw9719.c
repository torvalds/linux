/*
 * Support for dw9719 vcm driver.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/delay.h>
#include "dw9719.h"

static struct dw9719_device dw9719_dev;

static int dw9719_i2c_rd8(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2] = { reg };

	msg[0].addr = DW9719_VCM_ADDR;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = DW9719_VCM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &buf[1];
	*val = 0;

	if (i2c_transfer(client->adapter, msg, 2) != 2)
		return -EIO;
	*val = buf[1];

	return 0;
}

static int dw9719_i2c_wr8(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2] = { reg, val };

	msg.addr = DW9719_VCM_ADDR;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;

	return 0;
}

static int dw9719_i2c_wr16(struct i2c_client *client, u8 reg, u16 val)
{
	struct i2c_msg msg;
	u8 buf[3] = { reg, (u8)(val >> 8), (u8)(val & 0xff)};

	msg.addr = DW9719_VCM_ADDR;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;

	return 0;
}

int dw9719_vcm_power_up(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 value;

	/* Enable power */
	ret = dw9719_dev.platform_data->power_ctrl(sd, 1);
	/* waiting time requested by DW9714A(vcm) */
	if (ret)
		return ret;
	/* Wait for VBAT to stabilize */
	udelay(1);

	/*
	 * Jiggle SCL pin to wake up device.
	 */
	ret = dw9719_i2c_wr8(client, DW9719_CONTROL, 1);
	/* Need 100us to transit from SHUTDOWN to STANDBY*/
	usleep_range(100, 1000);

	/* Enable the ringing compensation */
	ret = dw9719_i2c_wr8(client, DW9719_CONTROL, DW9719_ENABLE_RINGING);
	if (ret < 0)
		goto fail_powerdown;

	/* Use SAC3 mode */
	ret = dw9719_i2c_wr8(client, DW9719_MODE, DW9719_MODE_SAC3);
	if (ret < 0)
		goto fail_powerdown;

	/* Set the resonance frequency */
	ret = dw9719_i2c_wr8(client, DW9719_VCM_FREQ, DW9719_DEFAULT_VCM_FREQ);
	if (ret < 0)
		goto fail_powerdown;

	/* Detect device */
	ret = dw9719_i2c_rd8(client, DW9719_INFO, &value);
	if (ret < 0)
		goto fail_powerdown;
	if (value != DW9719_ID) {
		ret = -ENXIO;
		goto fail_powerdown;
	}
	dw9719_dev.focus = 0;
	dw9719_dev.initialized = true;

	return 0;

fail_powerdown:
	dw9719_dev.platform_data->power_ctrl(sd, 0);
	return ret;
}

int dw9719_vcm_power_down(struct v4l2_subdev *sd)
{
	return dw9719_dev.platform_data->power_ctrl(sd, 0);
}

int dw9719_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	static const struct timespec move_time = {

		.tv_sec = 0,
		.tv_nsec = 60000000
	};
	struct timespec current_time, finish_time, delta_time;

	getnstimeofday(&current_time);
	finish_time = timespec_add(dw9719_dev.focus_time, move_time);
	delta_time = timespec_sub(current_time, finish_time);
	if (delta_time.tv_sec >= 0 && delta_time.tv_nsec >= 0) {
		*value = ATOMISP_FOCUS_HP_COMPLETE |
			 ATOMISP_FOCUS_STATUS_ACCEPTS_NEW_MOVE;
	} else {
		*value = ATOMISP_FOCUS_STATUS_MOVING |
			 ATOMISP_FOCUS_HP_IN_PROGRESS;
	}

	return 0;
}

int dw9719_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	return -EINVAL;
}

int dw9719_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	value = clamp(value, 0, DW9719_MAX_FOCUS_POS);
	ret = dw9719_i2c_wr16(client, DW9719_VCM_CURRENT, value);
	if (ret < 0)
		return ret;

	getnstimeofday(&dw9719_dev.focus_time);
	dw9719_dev.focus = value;

	return 0;
}

int dw9719_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{
	return dw9719_t_focus_abs(sd, dw9719_dev.focus + value);
}

int dw9719_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	*value = dw9719_dev.focus;
	return 0;
}
int dw9719_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int dw9719_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int dw9719_vcm_init(struct v4l2_subdev *sd)
{
	dw9719_dev.platform_data = camera_get_af_platform_data();
	return (NULL == dw9719_dev.platform_data) ? -ENODEV : 0;
}
