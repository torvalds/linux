/*
 * Support for dw9718 vcm driver.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
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
#include "dw9718.h"

static struct dw9718_device dw9718_dev;

static int dw9718_i2c_rd8(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2] = { reg };

	msg[0].addr = DW9718_VCM_ADDR;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = DW9718_VCM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &buf[1];
	*val = 0;

	if (i2c_transfer(client->adapter, msg, 2) != 2)
		return -EIO;
	*val = buf[1];

	return 0;
}

static int dw9718_i2c_wr8(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2] = { reg, val};

	msg.addr = DW9718_VCM_ADDR;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;

	return 0;
}

static int dw9718_i2c_wr16(struct i2c_client *client, u8 reg, u16 val)
{
	struct i2c_msg msg;
	u8 buf[3] = { reg, (u8)(val >> 8), (u8)(val & 0xff)};

	msg.addr = DW9718_VCM_ADDR;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;

	return 0;
}

int dw9718_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	value = clamp(value, 0, DW9718_MAX_FOCUS_POS);
	ret = dw9718_i2c_wr16(client, DW9718_DATA_M, value);
	/*pr_info("%s: value = %d\n", __func__, value);*/
	if (ret < 0)
		return ret;

	getnstimeofday(&dw9718_dev.focus_time);
	dw9718_dev.focus = value;

	return 0;
}

int dw9718_vcm_power_up(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 value;

	if (dw9718_dev.power_on)
		return 0;

	/* Enable power */
	ret = dw9718_dev.platform_data->power_ctrl(sd, 1);
	if (ret) {
		dev_err(&client->dev, "DW9718_PD power_ctrl failed %d\n", ret);
		return ret;
	}
	/* Wait for VBAT to stabilize */
	udelay(100);

	/* Detect device */
	ret = dw9718_i2c_rd8(client, DW9718_SACT, &value);
	if (ret < 0) {
		dev_err(&client->dev, "read DW9718_SACT failed %d\n", ret);
		goto fail_powerdown;
	}
	/*
	 * WORKAROUND: for module P8V12F-203 which are used on
	 * Cherrytrail Refresh Davis Reef AoB, register SACT is not
	 * returning default value as spec. But VCM works as expected and
	 * root cause is still under discussion with vendor.
	 * workaround here to avoid aborting the power up sequence and just
	 * give a warning about this error.
	 */
	if (value != DW9718_SACT_DEFAULT_VAL)
		dev_warn(&client->dev, "%s error, incorrect ID\n", __func__);

	/* Initialize according to recommended settings */
	ret = dw9718_i2c_wr8(client, DW9718_CONTROL,
			     DW9718_CONTROL_SW_LINEAR |
			     DW9718_CONTROL_S_SAC4 |
			     DW9718_CONTROL_OCP_DISABLE |
			     DW9718_CONTROL_UVLO_DISABLE);
	if (ret < 0) {
		dev_err(&client->dev, "write DW9718_CONTROL  failed %d\n", ret);
		goto fail_powerdown;
	}
	ret = dw9718_i2c_wr8(client, DW9718_SACT,
			     DW9718_SACT_MULT_TWO |
			     DW9718_SACT_PERIOD_8_8MS);
	if (ret < 0) {
		dev_err(&client->dev, "write DW9718_SACT  failed %d\n", ret);
		goto fail_powerdown;
	}

	ret = dw9718_t_focus_abs(sd, dw9718_dev.focus);
	if (ret)
		return ret;
	dw9718_dev.initialized = true;
	dw9718_dev.power_on = 1;

	return 0;

fail_powerdown:
	dev_err(&client->dev, "%s error, powerup failed\n", __func__);
	dw9718_dev.platform_data->power_ctrl(sd, 0);
	return ret;
}

int dw9718_vcm_power_down(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (!dw9718_dev.power_on)
		return 0;

	ret =  dw9718_dev.platform_data->power_ctrl(sd, 0);
	if (ret) {
		dev_err(&client->dev, "%s power_ctrl failed\n",
				__func__);
		return ret;
	}
	dw9718_dev.power_on = 0;

	return 0;
}

int dw9718_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	static const struct timespec move_time = {
		.tv_sec = 0,
		.tv_nsec = 60000000
	};
	struct timespec current_time, finish_time, delta_time;

	getnstimeofday(&current_time);
	finish_time = timespec_add(dw9718_dev.focus_time, move_time);
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

int dw9718_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	return -EINVAL;
}

int dw9718_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{
	return dw9718_t_focus_abs(sd, dw9718_dev.focus + value);
}

int dw9718_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	*value  = dw9718_dev.focus;
	return 0;
}
int dw9718_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int dw9718_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int dw9718_vcm_init(struct v4l2_subdev *sd)
{
	dw9718_dev.platform_data = camera_get_af_platform_data();
	dw9718_dev.focus = DW9718_DEFAULT_FOCUS_POSITION;
	dw9718_dev.power_on = 0;
	return (NULL == dw9718_dev.platform_data) ? -ENODEV : 0;
}
