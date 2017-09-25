#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-device.h>

#include "ad5816g.h"

struct ad5816g_device ad5816g_dev;

static int ad5816g_i2c_rd8(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	buf[0] = reg;
	buf[1] = 0;

	msg[0].addr = AD5816G_VCM_ADDR;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &buf[0];

	msg[1].addr = AD5816G_VCM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &buf[1];
	*val = 0;
	if (i2c_transfer(client->adapter, msg, 2) != 2)
		return -EIO;
	*val = buf[1];
	return 0;
}

static int ad5816g_i2c_wr8(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	buf[0] = reg;
	buf[1] = val;
	msg.addr = AD5816G_VCM_ADDR;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = &buf[0];
	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int ad5816g_i2c_wr16(struct i2c_client *client, u8 reg, u16 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	buf[0] = reg;
	buf[1] = (u8)(val >> 8);
	buf[2] = (u8)(val & 0xff);
	msg.addr = AD5816G_VCM_ADDR;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = &buf[0];
	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int ad5816g_set_arc_mode(struct i2c_client *client)
{
	int ret;

	ret = ad5816g_i2c_wr8(client, AD5816G_CONTROL, AD5816G_ARC_EN);
	if (ret)
		return ret;

	ret = ad5816g_i2c_wr8(client, AD5816G_MODE,
				AD5816G_MODE_2_5M_SWITCH_CLOCK);
	if (ret)
		return ret;

	ret = ad5816g_i2c_wr8(client, AD5816G_VCM_FREQ, AD5816G_DEF_FREQ);
	return ret;
}

int ad5816g_vcm_power_up(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 ad5816g_id;

	/* Enable power */
	ret = ad5816g_dev.platform_data->power_ctrl(sd, 1);
	if (ret)
		return ret;
	/* waiting time AD5816G(vcm) - t1 + t2
	  * t1(1ms) -Time from VDD high to first i2c cmd
	  * t2(100us) - exit power-down mode time
	  */
	usleep_range(1100, 2200);
	/* Detect device */
	ret = ad5816g_i2c_rd8(client, AD5816G_IC_INFO, &ad5816g_id);
	if (ret < 0)
		goto fail_powerdown;
	if (ad5816g_id != AD5816G_ID) {
		ret = -ENXIO;
		goto fail_powerdown;
	}
	ret = ad5816g_set_arc_mode(client);
	if (ret)
		return ret;

	/* set the VCM_THRESHOLD */
	ret = ad5816g_i2c_wr8(client, AD5816G_VCM_THRESHOLD,
		AD5816G_DEF_THRESHOLD);

	return ret;

fail_powerdown:
	ad5816g_dev.platform_data->power_ctrl(sd, 0);
	return ret;
}

int ad5816g_vcm_power_down(struct v4l2_subdev *sd)
{
	return ad5816g_dev.platform_data->power_ctrl(sd, 0);
}


static int ad5816g_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 data = val & VCM_CODE_MASK;

	return ad5816g_i2c_wr16(client, AD5816G_VCM_CODE_MSB, data);
}

int ad5816g_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	int ret;

	value = clamp(value, 0, AD5816G_MAX_FOCUS_POS);
	ret = ad5816g_t_focus_vcm(sd, value);
	if (ret == 0) {
		ad5816g_dev.number_of_steps = value - ad5816g_dev.focus;
		ad5816g_dev.focus = value;
		getnstimeofday(&(ad5816g_dev.timestamp_t_focus_abs));
	}

	return ret;
}

int ad5816g_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{

	return ad5816g_t_focus_abs(sd, ad5816g_dev.focus + value);
}

int ad5816g_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	u32 status = 0;
	struct timespec temptime;
	const struct timespec timedelay = {
		0,
		min_t(u32, abs(ad5816g_dev.number_of_steps) * DELAY_PER_STEP_NS,
			DELAY_MAX_PER_STEP_NS),
	};

	ktime_get_ts(&temptime);

	temptime = timespec_sub(temptime, (ad5816g_dev.timestamp_t_focus_abs));

	if (timespec_compare(&temptime, &timedelay) <= 0) {
		status |= ATOMISP_FOCUS_STATUS_MOVING;
		status |= ATOMISP_FOCUS_HP_IN_PROGRESS;
	} else {
		status |= ATOMISP_FOCUS_STATUS_ACCEPTS_NEW_MOVE;
		status |= ATOMISP_FOCUS_HP_COMPLETE;
	}
	*value = status;

	return 0;
}

int ad5816g_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	s32 val;

	ad5816g_q_focus_status(sd, &val);

	if (val & ATOMISP_FOCUS_STATUS_MOVING)
		*value  = ad5816g_dev.focus - ad5816g_dev.number_of_steps;
	else
		*value = ad5816g_dev.focus;

	return 0;
}

int ad5816g_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int ad5816g_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}
