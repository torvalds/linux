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

#include "dw9714.h"

static struct dw9714_device dw9714_dev;
static int dw9714_i2c_write(struct i2c_client *client, u16 data)
{
	struct i2c_msg msg;
	const int num_msg = 1;
	int ret;
	u16 val;

	val = cpu_to_be16(data);
	msg.addr = DW9714_VCM_ADDR;
	msg.flags = 0;
	msg.len = DW9714_16BIT;
	msg.buf = (u8 *)&val;

	ret = i2c_transfer(client->adapter, &msg, 1);

	return ret == num_msg ? 0 : -EIO;
}

int dw9714_vcm_power_up(struct v4l2_subdev *sd)
{
	int ret;

	/* Enable power */
	ret = dw9714_dev.platform_data->power_ctrl(sd, 1);
	/* waiting time requested by DW9714A(vcm) */
	usleep_range(12000, 12500);
	return ret;
}

int dw9714_vcm_power_down(struct v4l2_subdev *sd)
{
	return dw9714_dev.platform_data->power_ctrl(sd, 0);
}


static int dw9714_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = -EINVAL;
	u8 mclk = vcm_step_mclk(dw9714_dev.vcm_settings.step_setting);
	u8 s = vcm_step_s(dw9714_dev.vcm_settings.step_setting);

	/*
	 * For different mode, VCM_PROTECTION_OFF/ON required by the
	 * control procedure. For DW9714_DIRECT/DLC mode, slew value is
	 * VCM_DEFAULT_S(0).
	 */
	switch (dw9714_dev.vcm_mode) {
	case DW9714_DIRECT:
		if (dw9714_dev.vcm_settings.update) {
			ret = dw9714_i2c_write(client, VCM_PROTECTION_OFF);
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client, DIRECT_VCM);
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client, VCM_PROTECTION_ON);
			if (ret)
				return ret;
			dw9714_dev.vcm_settings.update = false;
		}
		ret = dw9714_i2c_write(client,
					vcm_val(val, VCM_DEFAULT_S));
		break;
	case DW9714_LSC:
		if (dw9714_dev.vcm_settings.update) {
			ret = dw9714_i2c_write(client, VCM_PROTECTION_OFF);
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client,
				vcm_dlc_mclk(DLC_DISABLE, mclk));
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client,
				vcm_tsrc(dw9714_dev.vcm_settings.t_src));
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client, VCM_PROTECTION_ON);
			if (ret)
				return ret;
			dw9714_dev.vcm_settings.update = false;
		}
		ret = dw9714_i2c_write(client, vcm_val(val, s));
		break;
	case DW9714_DLC:
		if (dw9714_dev.vcm_settings.update) {
			ret = dw9714_i2c_write(client, VCM_PROTECTION_OFF);
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client,
					vcm_dlc_mclk(DLC_ENABLE, mclk));
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client,
				vcm_tsrc(dw9714_dev.vcm_settings.t_src));
			if (ret)
				return ret;
			ret = dw9714_i2c_write(client, VCM_PROTECTION_ON);
			if (ret)
				return ret;
			dw9714_dev.vcm_settings.update = false;
		}
		ret = dw9714_i2c_write(client,
					vcm_val(val, VCM_DEFAULT_S));
		break;
	}
	return ret;
}

int dw9714_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	int ret;

	value = clamp(value, 0, DW9714_MAX_FOCUS_POS);
	ret = dw9714_t_focus_vcm(sd, value);
	if (ret == 0) {
		dw9714_dev.number_of_steps = value - dw9714_dev.focus;
		dw9714_dev.focus = value;
		getnstimeofday(&(dw9714_dev.timestamp_t_focus_abs));
	}

	return ret;
}

int dw9714_t_focus_abs_init(struct v4l2_subdev *sd)
{
	int ret;

	ret = dw9714_t_focus_vcm(sd, DW9714_DEFAULT_FOCUS_POS);
	if (ret == 0) {
		dw9714_dev.number_of_steps =
			DW9714_DEFAULT_FOCUS_POS - dw9714_dev.focus;
		dw9714_dev.focus = DW9714_DEFAULT_FOCUS_POS;
		getnstimeofday(&(dw9714_dev.timestamp_t_focus_abs));
	}

	return ret;
}

int dw9714_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{

	return dw9714_t_focus_abs(sd, dw9714_dev.focus + value);
}

int dw9714_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	u32 status = 0;
	struct timespec temptime;
	const struct timespec timedelay = {
		0,
		min_t(u32, abs(dw9714_dev.number_of_steps)*DELAY_PER_STEP_NS,
			DELAY_MAX_PER_STEP_NS),
	};

	ktime_get_ts(&temptime);

	temptime = timespec_sub(temptime, (dw9714_dev.timestamp_t_focus_abs));

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

int dw9714_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	s32 val;

	dw9714_q_focus_status(sd, &val);

	if (val & ATOMISP_FOCUS_STATUS_MOVING)
		*value  = dw9714_dev.focus - dw9714_dev.number_of_steps;
	else
		*value  = dw9714_dev.focus;

	return 0;
}

int dw9714_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	dw9714_dev.vcm_settings.step_setting = value;
	dw9714_dev.vcm_settings.update = true;

	return 0;
}

int dw9714_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	dw9714_dev.vcm_settings.t_src = value;
	dw9714_dev.vcm_settings.update = true;

	return 0;
}
