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
#include <asm/intel-mid.h>

#include "drv201.h"

static struct drv201_device drv201_dev;

static int drv201_i2c_rd8(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	buf[0] = reg;
	buf[1] = 0;

	msg[0].addr = DRV201_VCM_ADDR;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &buf[0];

	msg[1].addr = DRV201_VCM_ADDR;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &buf[1];
	*val = 0;
	if (i2c_transfer(client->adapter, msg, 2) != 2)
		return -EIO;
	*val = buf[1];
	return 0;
}

static int drv201_i2c_wr8(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	buf[0] = reg;
	buf[1] = val;
	msg.addr = DRV201_VCM_ADDR;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = &buf[0];
	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int drv201_i2c_wr16(struct i2c_client *client, u8 reg, u16 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	buf[0] = reg;
	buf[1] = (u8)(val >> 8);
	buf[2] = (u8)(val & 0xff);
	msg.addr = DRV201_VCM_ADDR;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = &buf[0];
	if (i2c_transfer(client->adapter, &msg, 1) != 1)
		return -EIO;
	return 0;
}

int drv201_vcm_power_up(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u8 value;

	/* Enable power */
	ret = drv201_dev.platform_data->power_ctrl(sd, 1);
	if (ret)
		return ret;
	/* Wait for VBAT to stabilize */
	udelay(1);
	/*
	 * Jiggle SCL pin to wake up device.
	 * Drv201 expect SCL from low to high to wake device up.
	 * So the 1st access to i2c would fail.
	 * Using following function to wake device up.
	 */
	drv201_i2c_wr8(client, DRV201_CONTROL, DRV201_RESET);

	/* Need 100us to transit from SHUTDOWN to STANDBY*/
	usleep_range(WAKEUP_DELAY_US, WAKEUP_DELAY_US * 10);

	/* Reset device */
	ret = drv201_i2c_wr8(client, DRV201_CONTROL, DRV201_RESET);
	if (ret < 0)
		goto fail_powerdown;

	/* Detect device */
	ret = drv201_i2c_rd8(client, DRV201_CONTROL, &value);
	if (ret < 0)
		goto fail_powerdown;
	if (value != DEFAULT_CONTROL_VAL) {
		ret = -ENXIO;
		goto fail_powerdown;
	}

	drv201_dev.focus = DRV201_MAX_FOCUS_POS;
	drv201_dev.initialized = true;

	return 0;
fail_powerdown:
	drv201_dev.platform_data->power_ctrl(sd, 0);
	return ret;
}

int drv201_vcm_power_down(struct v4l2_subdev *sd)
{
	return drv201_dev.platform_data->power_ctrl(sd, 0);
}


int drv201_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 data = val & VCM_CODE_MASK;

	if (!drv201_dev.initialized)
		return -ENODEV;
	return drv201_i2c_wr16(client, DRV201_VCM_CURRENT, data);
}

int drv201_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	int ret;

	value = clamp(value, 0, DRV201_MAX_FOCUS_POS);
	ret = drv201_t_focus_vcm(sd, value);
	if (ret == 0) {
		drv201_dev.number_of_steps = value - drv201_dev.focus;
		drv201_dev.focus = value;
		getnstimeofday(&(drv201_dev.timestamp_t_focus_abs));
	}

	return ret;
}

int drv201_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{
	return drv201_t_focus_abs(sd, drv201_dev.focus + value);
}

int drv201_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	u32 status = 0;
	struct timespec temptime;
	const struct timespec timedelay = {
		0,
		min_t(u32, abs(drv201_dev.number_of_steps)*DELAY_PER_STEP_NS,
			DELAY_MAX_PER_STEP_NS),
	};

	ktime_get_ts(&temptime);

	temptime = timespec_sub(temptime, (drv201_dev.timestamp_t_focus_abs));

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

int drv201_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	s32 val;

	drv201_q_focus_status(sd, &val);

	if (val & ATOMISP_FOCUS_STATUS_MOVING)
		*value  = drv201_dev.focus - drv201_dev.number_of_steps;
	else
		*value  = drv201_dev.focus;

	return 0;
}

int drv201_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int drv201_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	return 0;
}

int drv201_vcm_init(struct v4l2_subdev *sd)
{
	drv201_dev.platform_data = camera_get_af_platform_data();
	return (NULL == drv201_dev.platform_data) ? -ENODEV : 0;
}



