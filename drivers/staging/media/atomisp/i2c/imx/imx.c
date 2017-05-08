/*
 * Support for Sony imx 8MP camera sensor.
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
#include <asm/intel-mid.h>
#include "../../include/linux/atomisp_platform.h"
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
#include "../../include/linux/libmsrlisthelper.h"
#include <linux/mm.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "imx.h"

/*
 * The imx135 embedded data info:
 * embedded data line num: 2
 * line 0 effective data size(byte): 76
 * line 1 effective data size(byte): 113
 */
static const uint32_t
	imx135_embedded_effective_size[IMX135_EMBEDDED_DATA_LINE_NUM]
	=  {76, 113};

static enum atomisp_bayer_order imx_bayer_order_mapping[] = {
	atomisp_bayer_order_rggb,
	atomisp_bayer_order_grbg,
	atomisp_bayer_order_gbrg,
	atomisp_bayer_order_bggr
};

static const unsigned int
IMX227_BRACKETING_LUT_FRAME_ENTRY[IMX_MAX_AE_LUT_LENGTH] = {
	0x0E10, 0x0E1E, 0x0E2C, 0x0E3A, 0x0E48};

static int
imx_read_reg(struct i2c_client *client, u16 len, u16 reg, u16 *val)
{
	struct i2c_msg msg[2];
	u16 data[IMX_SHORT_MAX];
	int ret, i;
	int retry = 0;

	if (len > IMX_BYTE_MAX) {
		dev_err(&client->dev, "%s error, invalid data length\n",
			__func__);
		return -EINVAL;
	}

	do {
		memset(msg, 0 , sizeof(msg));
		memset(data, 0 , sizeof(data));

		msg[0].addr = client->addr;
		msg[0].flags = 0;
		msg[0].len = I2C_MSG_LENGTH;
		msg[0].buf = (u8 *)data;
		/* high byte goes first */
		data[0] = cpu_to_be16(reg);

		msg[1].addr = client->addr;
		msg[1].len = len;
		msg[1].flags = I2C_M_RD;
		msg[1].buf = (u8 *)data;

		ret = i2c_transfer(client->adapter, msg, 2);
		if (ret != 2) {
			dev_err(&client->dev,
			  "retrying i2c read from offset 0x%x error %d... %d\n",
			  reg, ret, retry);
			msleep(20);
		}
	} while (ret != 2 && retry++ < I2C_RETRY_COUNT);

	if (ret != 2)
		return -EIO;

	/* high byte comes first */
	if (len == IMX_8BIT) {
		*val = (u8)data[0];
	} else {
		/* 16-bit access is default when len > 1 */
		for (i = 0; i < (len >> 1); i++)
			val[i] = be16_to_cpu(data[i]);
	}

	return 0;
}

static int imx_i2c_write(struct i2c_client *client, u16 len, u8 *data)
{
	struct i2c_msg msg;
	int ret;
	int retry = 0;

	do {
		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = len;
		msg.buf = data;

		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret != 1) {
			dev_err(&client->dev,
				"retrying i2c write transfer... %d\n", retry);
				msleep(20);
		}
	} while (ret != 1 && retry++ < I2C_RETRY_COUNT);

	return ret == 1 ? 0 : -EIO;
}

int
imx_write_reg(struct i2c_client *client, u16 data_length, u16 reg, u16 val)
{
	int ret;
	unsigned char data[4] = {0};
	u16 *wreg = (u16 *)data;
	const u16 len = data_length + sizeof(u16); /* 16-bit address + data */

	if (data_length != IMX_8BIT && data_length != IMX_16BIT) {
		v4l2_err(client, "%s error, invalid data_length\n", __func__);
		return -EINVAL;
	}

	/* high byte goes out first */
	*wreg = cpu_to_be16(reg);

	if (data_length == IMX_8BIT)
		data[2] = (u8)(val);
	else {
		/* IMX_16BIT */
		u16 *wdata = (u16 *)&data[2];
		*wdata = cpu_to_be16(val);
	}

	ret = imx_i2c_write(client, len, data);
	if (ret)
		dev_err(&client->dev,
			"write error: wrote 0x%x to offset 0x%x error %d",
			val, reg, ret);

	return ret;
}

/*
 * imx_write_reg_array - Initializes a list of imx registers
 * @client: i2c driver client structure
 * @reglist: list of registers to be written
 *
 * This function initializes a list of registers. When consecutive addresses
 * are found in a row on the list, this function creates a buffer and sends
 * consecutive data in a single i2c_transfer().
 *
 * __imx_flush_reg_array, __imx_buf_reg_array() and
 * __imx_write_reg_is_consecutive() are internal functions to
 * imx_write_reg_array_fast() and should be not used anywhere else.
 *
 */

static int __imx_flush_reg_array(struct i2c_client *client,
				     struct imx_write_ctrl *ctrl)
{
	u16 size;

	if (ctrl->index == 0)
		return 0;

	size = sizeof(u16) + ctrl->index; /* 16-bit address + data */
	ctrl->buffer.addr = cpu_to_be16(ctrl->buffer.addr);
	ctrl->index = 0;

	return imx_i2c_write(client, size, (u8 *)&ctrl->buffer);
}

static int __imx_buf_reg_array(struct i2c_client *client,
				   struct imx_write_ctrl *ctrl,
				   const struct imx_reg *next)
{
	int size;
	u16 *data16;

	switch (next->type) {
	case IMX_8BIT:
		size = 1;
		ctrl->buffer.data[ctrl->index] = (u8)next->val;
		break;
	case IMX_16BIT:
		size = 2;
		data16 = (u16 *)&ctrl->buffer.data[ctrl->index];
		*data16 = cpu_to_be16((u16)next->val);
		break;
	default:
		return -EINVAL;
	}

	/* When first item is added, we need to store its starting address */
	if (ctrl->index == 0)
		ctrl->buffer.addr = next->sreg;

	ctrl->index += size;

	/*
	 * Buffer cannot guarantee free space for u32? Better flush it to avoid
	 * possible lack of memory for next item.
	 */
	if (ctrl->index + sizeof(u16) >= IMX_MAX_WRITE_BUF_SIZE)
		return __imx_flush_reg_array(client, ctrl);

	return 0;
}

static int
__imx_write_reg_is_consecutive(struct i2c_client *client,
				   struct imx_write_ctrl *ctrl,
				   const struct imx_reg *next)
{
	if (ctrl->index == 0)
		return 1;

	return ctrl->buffer.addr + ctrl->index == next->sreg;
}

static int imx_write_reg_array(struct i2c_client *client,
				   const struct imx_reg *reglist)
{
	const struct imx_reg *next = reglist;
	struct imx_write_ctrl ctrl;
	int err;

	ctrl.index = 0;
	for (; next->type != IMX_TOK_TERM; next++) {
		switch (next->type & IMX_TOK_MASK) {
		case IMX_TOK_DELAY:
			err = __imx_flush_reg_array(client, &ctrl);
			if (err)
				return err;
			msleep(next->val);
			break;

		default:
			/*
			 * If next address is not consecutive, data needs to be
			 * flushed before proceed.
			 */
			if (!__imx_write_reg_is_consecutive(client, &ctrl,
								next)) {
				err = __imx_flush_reg_array(client, &ctrl);
				if (err)
					return err;
			}
			err = __imx_buf_reg_array(client, &ctrl, next);
			if (err) {
				v4l2_err(client, "%s: write error, aborted\n",
					 __func__);
				return err;
			}
			break;
		}
	}

	return __imx_flush_reg_array(client, &ctrl);
}

static int __imx_min_fps_diff(int fps, const struct imx_fps_setting *fps_list)
{
	int diff = INT_MAX;
	int i;

	if (fps == 0)
		return 0;

	for (i = 0; i < MAX_FPS_OPTIONS_SUPPORTED; i++) {
		if (!fps_list[i].fps)
			break;
		if (abs(fps_list[i].fps - fps) < diff)
			diff = abs(fps_list[i].fps - fps);
	}

	return diff;
}

static int __imx_nearest_fps_index(int fps,
					const struct imx_fps_setting *fps_list)
{
	int fps_index = 0;
	int i;

	for (i = 0; i < MAX_FPS_OPTIONS_SUPPORTED; i++) {
		if (!fps_list[i].fps)
			break;
		if (abs(fps_list[i].fps - fps)
		    < abs(fps_list[fps_index].fps - fps))
			fps_index = i;
	}
	return fps_index;
}

/*
 * This is to choose the nearest fps setting above the requested fps
 * fps_list should be in ascendant order.
 */
static int __imx_above_nearest_fps_index(int fps,
					const struct imx_fps_setting *fps_list)
{
	int fps_index = 0;
	int i;

	for (i = 0; i < MAX_FPS_OPTIONS_SUPPORTED; i++) {
		if (!fps_list[i].fps)
			break;
		if (fps <= fps_list[i].fps) {
			fps_index = i;
			break;
		}
	}

	return fps_index;
}

static int imx_get_lanes(struct v4l2_subdev *sd)
{
	struct camera_mipi_info *imx_info = v4l2_get_subdev_hostdata(sd);

	if (!imx_info)
		return -ENOSYS;
	if (imx_info->num_lanes < 1 || imx_info->num_lanes > 4 ||
	    imx_info->num_lanes == 3)
		return -EINVAL;

	return imx_info->num_lanes;
}

static int __imx_update_exposure_timing(struct i2c_client *client, u16 exposure,
			u16 llp, u16 fll)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx_device *dev = to_imx_sensor(sd);
	int ret = 0;

	if (dev->sensor_id != IMX227_ID) {
		/* Increase the VTS to match exposure + margin */
		if (exposure > fll - IMX_INTEGRATION_TIME_MARGIN)
			fll = exposure + IMX_INTEGRATION_TIME_MARGIN;
	}

	ret = imx_write_reg(client, IMX_16BIT,
		dev->reg_addr->line_length_pixels, llp);
	if (ret)
		return ret;

	ret = imx_write_reg(client, IMX_16BIT,
		dev->reg_addr->frame_length_lines, fll);
	if (ret)
		return ret;

	if (exposure)
		ret = imx_write_reg(client, IMX_16BIT,
			dev->reg_addr->coarse_integration_time, exposure);

	return ret;
}

static int __imx_update_gain(struct v4l2_subdev *sd, u16 gain)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	/* set global gain */
	ret = imx_write_reg(client, IMX_8BIT, dev->reg_addr->global_gain, gain);
	if (ret)
		return ret;

	/* set short analog gain */
	if (dev->sensor_id == IMX135_ID)
		ret = imx_write_reg(client, IMX_8BIT, IMX_SHORT_AGC_GAIN, gain);

	return ret;
}

static int __imx_update_digital_gain(struct i2c_client *client, u16 digitgain)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx_device *dev = to_imx_sensor(sd);
	struct imx_write_buffer digit_gain;

	digit_gain.addr = cpu_to_be16(dev->reg_addr->dgc_adj);
	digit_gain.data[0] = (digitgain >> 8) & 0xFF;
	digit_gain.data[1] = digitgain & 0xFF;

	if (dev->sensor_id == IMX219_ID) {
		return imx_i2c_write(client, IMX219_DGC_LEN, (u8 *)&digit_gain);
	} else if (dev->sensor_id == IMX227_ID) {
		return imx_i2c_write(client, IMX227_DGC_LEN, (u8 *)&digit_gain);
	} else {
		digit_gain.data[2] = (digitgain >> 8) & 0xFF;
		digit_gain.data[3] = digitgain & 0xFF;
		digit_gain.data[4] = (digitgain >> 8) & 0xFF;
		digit_gain.data[5] = digitgain & 0xFF;
		digit_gain.data[6] = (digitgain >> 8) & 0xFF;
		digit_gain.data[7] = digitgain & 0xFF;
		return imx_i2c_write(client, IMX_DGC_LEN, (u8 *)&digit_gain);
	}
	return 0;
}

static int imx_set_exposure_gain(struct v4l2_subdev *sd, u16 coarse_itg,
	u16 gain, u16 digitgain)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int lanes = imx_get_lanes(sd);
	unsigned int digitgain_scaled;
	int ret = 0;

	/* Validate exposure:  cannot exceed VTS-4 where VTS is 16bit */
	coarse_itg = clamp_t(u16, coarse_itg, 0, IMX_MAX_EXPOSURE_SUPPORTED);

	/* Validate gain: must not exceed maximum 8bit value */
	gain = clamp_t(u16, gain, 0, IMX_MAX_GLOBAL_GAIN_SUPPORTED);

	mutex_lock(&dev->input_lock);

	if (dev->sensor_id == IMX227_ID) {
		ret = imx_write_reg_array(client, imx_param_hold);
		if (ret) {
			mutex_unlock(&dev->input_lock);
			return ret;
		}
	}

	/* For imx175, setting gain must be delayed by one */
	if ((dev->sensor_id == IMX175_ID) && dev->digital_gain)
		digitgain_scaled = dev->digital_gain;
	else
		digitgain_scaled = digitgain;
	/* imx132 with two lanes needs more gain to saturate at max */
	if (dev->sensor_id == IMX132_ID && lanes > 1) {
		digitgain_scaled *= IMX132_2LANES_GAINFACT;
		digitgain_scaled >>= IMX132_2LANES_GAINFACT_SHIFT;
	}
	/* Validate digital gain: must not exceed 12 bit value*/
	digitgain_scaled = clamp_t(unsigned int, digitgain_scaled,
				   0, IMX_MAX_DIGITAL_GAIN_SUPPORTED);

	ret = __imx_update_exposure_timing(client, coarse_itg,
			dev->pixels_per_line, dev->lines_per_frame);
	if (ret)
		goto out;
	dev->coarse_itg = coarse_itg;

	if (dev->sensor_id == IMX175_ID)
		ret = __imx_update_gain(sd, dev->gain);
	else
		ret = __imx_update_gain(sd, gain);
	if (ret)
		goto out;
	dev->gain = gain;

	ret = __imx_update_digital_gain(client, digitgain_scaled);
	if (ret)
		goto out;
	dev->digital_gain = digitgain;

out:
	if (dev->sensor_id == IMX227_ID)
		ret = imx_write_reg_array(client, imx_param_update);
	mutex_unlock(&dev->input_lock);
	return ret;
}

static long imx_s_exposure(struct v4l2_subdev *sd,
			       struct atomisp_exposure *exposure)
{
	return imx_set_exposure_gain(sd, exposure->integration_time[0],
				exposure->gain[0], exposure->gain[1]);
}

/* FIXME -To be updated with real OTP reading */
static int imx_g_priv_int_data(struct v4l2_subdev *sd,
				   struct v4l2_private_int_data *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	u8 __user *to = priv->data;
	u32 read_size = priv->size;
	int ret;

	/* No need to copy data if size is 0 */
	if (!read_size)
		goto out;

	if (IS_ERR(dev->otp_data)) {
		dev_err(&client->dev, "OTP data not available");
		return PTR_ERR(dev->otp_data);
	}
	/* Correct read_size value only if bigger than maximum */
	if (read_size > dev->otp_driver->size)
		read_size = dev->otp_driver->size;

	ret = copy_to_user(to, dev->otp_data, read_size);
	if (ret) {
		dev_err(&client->dev, "%s: failed to copy OTP data to user\n",
			 __func__);
		return -EFAULT;
	}
out:
	/* Return correct size */
	priv->size = dev->otp_driver->size;

	return 0;
}

static int __imx_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	int lanes = imx_get_lanes(sd);
	int ret;

	if (dev->sensor_id == IMX_ID_DEFAULT)
		return 0;

	/* The default is no flip at sensor initialization */
	dev->h_flip->cur.val = 0;
	dev->v_flip->cur.val = 0;
	/* Sets the default FPS */
	dev->fps_index = 0;
	dev->curr_res_table = dev->mode_tables->res_preview;
	dev->entries_curr_table = dev->mode_tables->n_res_preview;

	ret = imx_write_reg_array(client, dev->mode_tables->init_settings);
	if (ret)
		return ret;

	if (dev->sensor_id == IMX132_ID && lanes > 0) {
		static const u8 imx132_rglanesel[] = {
			IMX132_RGLANESEL_1LANE,		/* 1 lane */
			IMX132_RGLANESEL_2LANES,	/* 2 lanes */
			IMX132_RGLANESEL_1LANE,		/* undefined */
			IMX132_RGLANESEL_4LANES,	/* 4 lanes */
		};
		ret = imx_write_reg(client, IMX_8BIT,
				IMX132_RGLANESEL, imx132_rglanesel[lanes - 1]);
	}

	return ret;
}

static int imx_init(struct v4l2_subdev *sd, u32 val)
{
	struct imx_device *dev = to_imx_sensor(sd);
	int ret = 0;

	mutex_lock(&dev->input_lock);
	ret = __imx_init(sd, val);
	mutex_unlock(&dev->input_lock);

	return ret;
}

static long imx_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{

	switch (cmd) {
	case ATOMISP_IOC_S_EXPOSURE:
		return imx_s_exposure(sd, arg);
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA:
		return imx_g_priv_int_data(sd, arg);
	default:
		return -EINVAL;
	}
	return 0;
}

static int power_up(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	int ret;

       /* power control */
	ret = dev->platform_data->power_ctrl(sd, 1);
	if (ret)
		goto fail_power;

	/* flis clock control */
	ret = dev->platform_data->flisclk_ctrl(sd, 1);
	if (ret)
		goto fail_clk;

	/* gpio ctrl */
	ret = dev->platform_data->gpio_ctrl(sd, 1);
	if (ret) {
		dev_err(&client->dev, "gpio failed\n");
		goto fail_gpio;
	}

	return 0;
fail_gpio:
	dev->platform_data->gpio_ctrl(sd, 0);
fail_clk:
	dev->platform_data->flisclk_ctrl(sd, 0);
fail_power:
	dev->platform_data->power_ctrl(sd, 0);
	dev_err(&client->dev, "sensor power-up failed\n");

	return ret;
}

static int power_down(struct v4l2_subdev *sd)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = dev->platform_data->flisclk_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "flisclk failed\n");

	/* gpio ctrl */
	ret = dev->platform_data->gpio_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "gpio failed\n");

	/* power control */
	ret = dev->platform_data->power_ctrl(sd, 0);
	if (ret)
		dev_err(&client->dev, "vprog failed.\n");

	return ret;
}

static int __imx_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx_device *dev = to_imx_sensor(sd);
	int ret = 0;
	int r = 0;

	if (on == 0) {
		ret = power_down(sd);
		if (dev->vcm_driver && dev->vcm_driver->power_down)
			r = dev->vcm_driver->power_down(sd);
		if (ret == 0)
			ret = r;
		dev->power = 0;
	} else {
		if (dev->vcm_driver && dev->vcm_driver->power_up)
			ret = dev->vcm_driver->power_up(sd);
		if (ret)
			return ret;
		ret = power_up(sd);
		if (!ret) {
			dev->power = 1;
			return __imx_init(sd, 0);
		}
	}

	return ret;
}

static int imx_s_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	ret = __imx_s_power(sd, on);
	mutex_unlock(&dev->input_lock);

	return ret;
}

static int imx_get_intg_factor(struct i2c_client *client,
				struct camera_mipi_info *info,
				const struct imx_reg *reglist)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx_device *dev = to_imx_sensor(sd);
	int lanes = imx_get_lanes(sd);
	u32 vt_pix_clk_div;
	u32 vt_sys_clk_div;
	u32 pre_pll_clk_div;
	u32 pll_multiplier;

	const int ext_clk_freq_hz = 19200000;
	struct atomisp_sensor_mode_data *buf = &info->data;
	int ret;
	u16 data[IMX_INTG_BUF_COUNT];

	u32 vt_pix_clk_freq_mhz;
	u32 coarse_integration_time_min;
	u32 coarse_integration_time_max_margin;
	u32 read_mode;
	u32 div;

	if (info == NULL)
		return -EINVAL;

	memset(data, 0, IMX_INTG_BUF_COUNT * sizeof(u16));
	ret = imx_read_reg(client, 1, IMX_VT_PIX_CLK_DIV, data);
	if (ret)
		return ret;
	vt_pix_clk_div = data[0] & IMX_MASK_5BIT;

	if (dev->sensor_id == IMX132_ID || dev->sensor_id == IMX208_ID) {
		static const int rgpltd[] = { 2, 4, 1, 1 };
		ret = imx_read_reg(client, 1, IMX132_208_VT_RGPLTD, data);
		if (ret)
			return ret;
		vt_sys_clk_div = rgpltd[data[0] & IMX_MASK_2BIT];
	} else {
		ret = imx_read_reg(client, 1, IMX_VT_SYS_CLK_DIV, data);
		if (ret)
			return ret;
		vt_sys_clk_div = data[0] & IMX_MASK_2BIT;
	}
	ret = imx_read_reg(client, 1, IMX_PRE_PLL_CLK_DIV, data);
	if (ret)
		return ret;
	pre_pll_clk_div = data[0] & IMX_MASK_4BIT;

	ret = imx_read_reg(client, 2,
		(dev->sensor_id == IMX132_ID ||
		 dev->sensor_id == IMX219_ID ||
		 dev->sensor_id == IMX208_ID) ?
		IMX132_208_219_PLL_MULTIPLIER : IMX_PLL_MULTIPLIER, data);
	if (ret)
		return ret;
	pll_multiplier = data[0] & IMX_MASK_11BIT;

	memset(data, 0, IMX_INTG_BUF_COUNT * sizeof(u16));
	ret = imx_read_reg(client, 4, IMX_COARSE_INTG_TIME_MIN, data);
	if (ret)
		return ret;
	coarse_integration_time_min = data[0];
	coarse_integration_time_max_margin = data[1];

	/* Get the cropping and output resolution to ISP for this mode. */
	ret =  imx_read_reg(client, 2, dev->reg_addr->horizontal_start_h, data);
	if (ret)
		return ret;
	buf->crop_horizontal_start = data[0];

	ret = imx_read_reg(client, 2, dev->reg_addr->vertical_start_h, data);
	if (ret)
		return ret;
	buf->crop_vertical_start = data[0];

	ret = imx_read_reg(client, 2, dev->reg_addr->horizontal_end_h, data);
	if (ret)
		return ret;
	buf->crop_horizontal_end = data[0];

	ret = imx_read_reg(client, 2, dev->reg_addr->vertical_end_h, data);
	if (ret)
		return ret;
	buf->crop_vertical_end = data[0];

	ret = imx_read_reg(client, 2,
		dev->reg_addr->horizontal_output_size_h, data);
	if (ret)
		return ret;
	buf->output_width = data[0];

	ret = imx_read_reg(client, 2,
		dev->reg_addr->vertical_output_size_h, data);
	if (ret)
		return ret;
	buf->output_height = data[0];

	memset(data, 0, IMX_INTG_BUF_COUNT * sizeof(u16));
	if (dev->sensor_id == IMX132_ID || dev->sensor_id == IMX208_ID ||
		dev->sensor_id == IMX219_ID)
		read_mode = 0;
	else {
		if (dev->sensor_id == IMX227_ID)
			ret = imx_read_reg(client, 1, IMX227_READ_MODE, data);
		else
			ret = imx_read_reg(client, 1, IMX_READ_MODE, data);

		if (ret)
			return ret;
		read_mode = data[0] & IMX_MASK_2BIT;
	}

	div = pre_pll_clk_div*vt_sys_clk_div*vt_pix_clk_div;
	if (div == 0)
		return -EINVAL;

	if (dev->sensor_id == IMX132_ID || dev->sensor_id == IMX208_ID)
		vt_pix_clk_freq_mhz = ext_clk_freq_hz / div;
	else if (dev->sensor_id == IMX227_ID) {
		/* according to IMX227 datasheet:
		 * vt_pix_freq_mhz = * num_of_vt_lanes(4) * ivt_pix_clk_freq_mhz
		 */
		vt_pix_clk_freq_mhz =
			(u64)4 * ext_clk_freq_hz * pll_multiplier;
		do_div(vt_pix_clk_freq_mhz, div);
	} else
		vt_pix_clk_freq_mhz = 2 * ext_clk_freq_hz / div;

	vt_pix_clk_freq_mhz *= pll_multiplier;
	if (dev->sensor_id == IMX132_ID && lanes > 0)
		vt_pix_clk_freq_mhz *= lanes;

	dev->vt_pix_clk_freq_mhz = vt_pix_clk_freq_mhz;

	buf->vt_pix_clk_freq_mhz = vt_pix_clk_freq_mhz;
	buf->coarse_integration_time_min = coarse_integration_time_min;
	buf->coarse_integration_time_max_margin =
				coarse_integration_time_max_margin;

	buf->fine_integration_time_min = IMX_FINE_INTG_TIME;
	buf->fine_integration_time_max_margin = IMX_FINE_INTG_TIME;
	buf->fine_integration_time_def = IMX_FINE_INTG_TIME;
	buf->frame_length_lines = dev->lines_per_frame;
	buf->line_length_pck = dev->pixels_per_line;
	buf->read_mode = read_mode;

	if (dev->sensor_id == IMX132_ID || dev->sensor_id == IMX208_ID ||
		dev->sensor_id == IMX219_ID) {
		buf->binning_factor_x = 1;
		buf->binning_factor_y = 1;
	} else {
		if (dev->sensor_id == IMX227_ID)
			ret = imx_read_reg(client, 1, IMX227_BINNING_ENABLE,
				data);
		else
			ret = imx_read_reg(client, 1, IMX_BINNING_ENABLE, data);

		if (ret)
			return ret;
		/* 1:binning enabled, 0:disabled */
		if (data[0] == 1) {
			if (dev->sensor_id == IMX227_ID)
				ret = imx_read_reg(client, 1,
					IMX227_BINNING_TYPE, data);
			else
				ret = imx_read_reg(client, 1,
					IMX_BINNING_TYPE, data);

			if (ret)
				return ret;
			buf->binning_factor_x = data[0] >> 4 & 0x0f;
			if (!buf->binning_factor_x)
				buf->binning_factor_x = 1;
			buf->binning_factor_y = data[0] & 0xf;
			if (!buf->binning_factor_y)
				buf->binning_factor_y = 1;
			/* WOWRKAROUND, NHD setting for IMX227 should have 4x4
			 * binning but the register setting does not reflect
			 * this, I am asking vendor why this happens. this is
			 * workaround for INTEL BZ 216560.
			 */
			if (dev->sensor_id == IMX227_ID) {
				if (dev->curr_res_table[dev->fmt_idx].width ==
					376 &&
				    dev->curr_res_table[dev->fmt_idx].height ==
					656) {
					buf->binning_factor_x = 4;
					buf->binning_factor_y = 4;
				}
			}
		} else {
			buf->binning_factor_x = 1;
			buf->binning_factor_y = 1;
		}
	}

	return 0;
}

/* This returns the exposure time being used. This should only be used
   for filling in EXIF data, not for actual image processing. */
static int imx_q_exposure(struct v4l2_subdev *sd, s32 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	u16 coarse;
	int ret;

	/* the fine integration time is currently not calculated */
	ret = imx_read_reg(client, IMX_16BIT,
		dev->reg_addr->coarse_integration_time, &coarse);
	*value = coarse;

	return ret;
}

static int imx_test_pattern(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	int ret;

	if (dev->power == 0)
		return 0;

	ret = imx_write_reg(client, IMX_16BIT, IMX_TEST_PATTERN_COLOR_R,
		(u16)(dev->tp_r->val >> 22));
	if (ret)
		return ret;

	ret = imx_write_reg(client, IMX_16BIT, IMX_TEST_PATTERN_COLOR_GR,
		(u16)(dev->tp_gr->val >> 22));
	if (ret)
		return ret;

	ret = imx_write_reg(client, IMX_16BIT, IMX_TEST_PATTERN_COLOR_GB,
		(u16)(dev->tp_gb->val >> 22));
	if (ret)
		return ret;

	ret = imx_write_reg(client, IMX_16BIT, IMX_TEST_PATTERN_COLOR_B,
		(u16)(dev->tp_b->val >> 22));
	if (ret)
		return ret;

	return imx_write_reg(client, IMX_16BIT, IMX_TEST_PATTERN_MODE,
		(u16)(dev->tp_mode->val));
}

static u32 imx_translate_bayer_order(enum atomisp_bayer_order code)
{
	switch (code) {
	case atomisp_bayer_order_rggb:
		return MEDIA_BUS_FMT_SRGGB10_1X10;
	case atomisp_bayer_order_grbg:
		return MEDIA_BUS_FMT_SGRBG10_1X10;
	case atomisp_bayer_order_bggr:
		return MEDIA_BUS_FMT_SBGGR10_1X10;
	case atomisp_bayer_order_gbrg:
		return MEDIA_BUS_FMT_SGBRG10_1X10;
	}
	return 0;
}

static int imx_v_flip(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct camera_mipi_info *imx_info = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val;

	if (dev->power == 0)
		return -EIO;

	ret = imx_write_reg_array(client, dev->param_hold);
	if (ret)
		return ret;

	ret = imx_read_reg(client, IMX_8BIT,
		dev->reg_addr->img_orientation, &val);
	if (ret)
		return ret;
	if (value)
		val |= IMX_VFLIP_BIT;
	else
		val &= ~IMX_VFLIP_BIT;

	ret = imx_write_reg(client, IMX_8BIT,
		dev->reg_addr->img_orientation, val);
	if (ret)
		return ret;

	imx_info = v4l2_get_subdev_hostdata(sd);
	if (imx_info) {
		val &= (IMX_VFLIP_BIT|IMX_HFLIP_BIT);
		imx_info->raw_bayer_order = imx_bayer_order_mapping[val];
		dev->format.code = imx_translate_bayer_order(
			imx_info->raw_bayer_order);
	}

	return imx_write_reg_array(client, dev->param_update);
}

static int imx_h_flip(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct camera_mipi_info *imx_info = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	u16 val;

	if (dev->power == 0)
		return -EIO;

	ret = imx_write_reg_array(client, dev->param_hold);
	if (ret)
		return ret;
	ret = imx_read_reg(client, IMX_8BIT,
		dev->reg_addr->img_orientation, &val);
	if (ret)
		return ret;
	if (value)
		val |= IMX_HFLIP_BIT;
	else
		val &= ~IMX_HFLIP_BIT;
	ret = imx_write_reg(client, IMX_8BIT,
		dev->reg_addr->img_orientation, val);
	if (ret)
		return ret;

	imx_info = v4l2_get_subdev_hostdata(sd);
	if (imx_info) {
		val &= (IMX_VFLIP_BIT|IMX_HFLIP_BIT);
		imx_info->raw_bayer_order = imx_bayer_order_mapping[val];
		dev->format.code = imx_translate_bayer_order(
		imx_info->raw_bayer_order);
	}

	return imx_write_reg_array(client, dev->param_update);
}

static int imx_g_focal(struct v4l2_subdev *sd, s32 *val)
{
	*val = (IMX_FOCAL_LENGTH_NUM << 16) | IMX_FOCAL_LENGTH_DEM;
	return 0;
}

static int imx_g_fnumber(struct v4l2_subdev *sd, s32 *val)
{
	/*const f number for imx*/
	*val = (IMX_F_NUMBER_DEFAULT_NUM << 16) | IMX_F_NUMBER_DEM;
	return 0;
}

static int imx_g_fnumber_range(struct v4l2_subdev *sd, s32 *val)
{
	*val = (IMX_F_NUMBER_DEFAULT_NUM << 24) |
		(IMX_F_NUMBER_DEM << 16) |
		(IMX_F_NUMBER_DEFAULT_NUM << 8) | IMX_F_NUMBER_DEM;
	return 0;
}

static int imx_g_bin_factor_x(struct v4l2_subdev *sd, s32 *val)
{
	struct imx_device *dev = to_imx_sensor(sd);

	*val = dev->curr_res_table[dev->fmt_idx].bin_factor_x;

	return 0;
}

static int imx_g_bin_factor_y(struct v4l2_subdev *sd, s32 *val)
{
	struct imx_device *dev = to_imx_sensor(sd);

	*val = dev->curr_res_table[dev->fmt_idx].bin_factor_y;

	return 0;
}

int imx_vcm_power_up(struct v4l2_subdev *sd)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->power_up)
		return dev->vcm_driver->power_up(sd);
	return 0;
}

int imx_vcm_power_down(struct v4l2_subdev *sd)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->power_down)
		return dev->vcm_driver->power_down(sd);
	return 0;
}

int imx_vcm_init(struct v4l2_subdev *sd)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->init)
		return dev->vcm_driver->init(sd);
	return 0;
}

int imx_t_focus_vcm(struct v4l2_subdev *sd, u16 val)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->t_focus_vcm)
		return dev->vcm_driver->t_focus_vcm(sd, val);
	return 0;
}

int imx_t_focus_abs(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->t_focus_abs)
		return dev->vcm_driver->t_focus_abs(sd, value);
	return 0;
}
int imx_t_focus_rel(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->t_focus_rel)
		return dev->vcm_driver->t_focus_rel(sd, value);
	return 0;
}

int imx_q_focus_status(struct v4l2_subdev *sd, s32 *value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->q_focus_status)
		return dev->vcm_driver->q_focus_status(sd, value);
	return 0;
}

int imx_q_focus_abs(struct v4l2_subdev *sd, s32 *value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->q_focus_abs)
		return dev->vcm_driver->q_focus_abs(sd, value);
	return 0;
}

int imx_t_vcm_slew(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->t_vcm_slew)
		return dev->vcm_driver->t_vcm_slew(sd, value);
	return 0;
}

int imx_t_vcm_timing(struct v4l2_subdev *sd, s32 value)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (dev->vcm_driver && dev->vcm_driver->t_vcm_timing)
		return dev->vcm_driver->t_vcm_timing(sd, value);
	return 0;
}

static int imx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx_device *dev = container_of(
		ctrl->handler, struct imx_device, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&dev->sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		ret = imx_test_pattern(&dev->sd);
		break;
	case V4L2_CID_VFLIP:
		dev_dbg(&client->dev, "%s: CID_VFLIP:%d.\n",
			__func__, ctrl->val);
		ret = imx_v_flip(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		dev_dbg(&client->dev, "%s: CID_HFLIP:%d.\n",
			__func__, ctrl->val);
		ret = imx_h_flip(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		ret = imx_t_focus_abs(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_FOCUS_RELATIVE:
		ret = imx_t_focus_rel(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_VCM_SLEW:
		ret = imx_t_vcm_slew(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_VCM_TIMEING:
		ret = imx_t_vcm_timing(&dev->sd, ctrl->val);
		break;
	}

	return ret;
}

static int imx_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx_device *dev = container_of(
		ctrl->handler, struct imx_device, ctrl_handler);
	int ret = 0;
	unsigned int val;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = imx_q_exposure(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FOCUS_ABSOLUTE:
		ret = imx_q_focus_abs(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FOCUS_STATUS:
		ret = imx_q_focus_status(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FOCAL_ABSOLUTE:
		ret = imx_g_focal(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_ABSOLUTE:
		ret = imx_g_fnumber(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_FNUMBER_RANGE:
		ret = imx_g_fnumber_range(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_HORZ:
		ret = imx_g_bin_factor_x(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_BIN_FACTOR_VERT:
		ret = imx_g_bin_factor_y(&dev->sd, &ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ctrl->val = dev->lines_per_frame -
			dev->curr_res_table[dev->fmt_idx].height;
		break;
	case V4L2_CID_HBLANK:
		ctrl->val = dev->pixels_per_line -
			dev->curr_res_table[dev->fmt_idx].width;
		break;
	case V4L2_CID_PIXEL_RATE:
		ctrl->val = dev->vt_pix_clk_freq_mhz;
		break;
	case V4L2_CID_LINK_FREQ:
		val = dev->curr_res_table[dev->fmt_idx].
					fps_options[dev->fps_index].mipi_freq;
		if (val == 0)
			val = dev->curr_res_table[dev->fmt_idx].mipi_freq;
		if (val == 0)
			return -EINVAL;
		ctrl->val = val * 1000;			/* To Hz */
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = imx_s_ctrl,
	.g_volatile_ctrl = imx_g_volatile_ctrl
};

static const struct v4l2_ctrl_config imx_controls[] = {
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_EXPOSURE_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "exposure",
		.min = 0x0,
		.max = 0xffff,
		.step = 0x01,
		.def = 0x00,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_TEST_PATTERN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Test pattern",
		.min = 0,
		.max = 0xffff,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_TEST_PATTERN_COLOR_R,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Test pattern solid color R",
		.min = INT_MIN,
		.max = INT_MAX,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_TEST_PATTERN_COLOR_GR,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Test pattern solid color GR",
		.min = INT_MIN,
		.max = INT_MAX,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_TEST_PATTERN_COLOR_GB,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Test pattern solid color GB",
		.min = INT_MIN,
		.max = INT_MAX,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_TEST_PATTERN_COLOR_B,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Test pattern solid color B",
		.min = INT_MIN,
		.max = INT_MAX,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Flip",
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_HFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Mirror",
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FOCUS_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "focus move absolute",
		.min = 0,
		.max = IMX_MAX_FOCUS_POS,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FOCUS_RELATIVE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "focus move relative",
		.min = IMX_MAX_FOCUS_NEG,
		.max = IMX_MAX_FOCUS_POS,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FOCUS_STATUS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "focus status",
		.min = 0,
		.max = 100, /* allow enum to grow in the future */
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VCM_SLEW,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "vcm slew",
		.min = 0,
		.max = IMX_VCM_SLEW_STEP_MAX,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VCM_TIMEING,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "vcm step time",
		.min = 0,
		.max = IMX_VCM_SLEW_TIME_MAX,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FOCAL_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "focal length",
		.min = IMX_FOCAL_LENGTH_DEFAULT,
		.max = IMX_FOCAL_LENGTH_DEFAULT,
		.step = 0x01,
		.def = IMX_FOCAL_LENGTH_DEFAULT,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FNUMBER_ABSOLUTE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "f-number",
		.min = IMX_F_NUMBER_DEFAULT,
		.max = IMX_F_NUMBER_DEFAULT,
		.step = 0x01,
		.def = IMX_F_NUMBER_DEFAULT,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_FNUMBER_RANGE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "f-number range",
		.min = IMX_F_NUMBER_RANGE,
		.max =  IMX_F_NUMBER_RANGE,
		.step = 0x01,
		.def = IMX_F_NUMBER_RANGE,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_BIN_FACTOR_HORZ,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "horizontal binning factor",
		.min = 0,
		.max = IMX_BIN_FACTOR_MAX,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_BIN_FACTOR_VERT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "vertical binning factor",
		.min = 0,
		.max = IMX_BIN_FACTOR_MAX,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_LINK_FREQ,
		.name = "Link Frequency",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 1,
		.max = 1500000 * 1000,
		.step = 1,
		.def = 1,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_PIXEL_RATE,
		.name = "Pixel Rate",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = INT_MAX,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_HBLANK,
		.name = "Horizontal Blanking",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = SHRT_MAX,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VBLANK,
		.name = "Vertical Blanking",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = SHRT_MAX,
		.step = 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_HFLIP,
		.name = "Horizontal Flip",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_VFLIP,
		.name = "Vertical Flip",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.max = 1,
		.step = 1,
		.def = 0,
		.flags = 0,
	},
};

/*
 * distance - calculate the distance
 * @res: resolution
 * @w: width
 * @h: height
 *
 * Get the gap between resolution and w/h.
 * res->width/height smaller than w/h wouldn't be considered.
 * Returns the value of gap or -1 if fail.
 */
#define LARGEST_ALLOWED_RATIO_MISMATCH 600
static int distance(struct imx_resolution const *res, u32 w, u32 h,
		bool keep_ratio)
{
	unsigned int w_ratio;
	unsigned int h_ratio;
	int match;
	unsigned int allowed_ratio_mismatch = LARGEST_ALLOWED_RATIO_MISMATCH;

	if (!keep_ratio)
		allowed_ratio_mismatch = ~0;

	if (w == 0)
		return -1;
	w_ratio = (res->width << 13) / w;
	if (h == 0)
		return -1;
	h_ratio = (res->height << 13) / h;
	if (h_ratio == 0)
		return -1;
	match   = abs(((w_ratio << 13) / h_ratio) - ((int)8192));

	if ((w_ratio < (int)8192) || (h_ratio < (int)8192)  ||
		(match > allowed_ratio_mismatch))
		return -1;

	return w_ratio + h_ratio;
}

/* Return the nearest higher resolution index */
static int nearest_resolution_index(struct v4l2_subdev *sd, int w, int h)
{
	int i;
	int idx = -1;
	int dist;
	int fps_diff;
	int min_fps_diff = INT_MAX;
	int min_dist = INT_MAX;
	const struct imx_resolution *tmp_res = NULL;
	struct imx_device *dev = to_imx_sensor(sd);
	bool again = 1;
retry:
	for (i = 0; i < dev->entries_curr_table; i++) {
		tmp_res = &dev->curr_res_table[i];
		dist = distance(tmp_res, w, h, again);
		if (dist == -1)
			continue;
		if (dist < min_dist) {
			min_dist = dist;
			idx = i;
		}
		if (dist == min_dist) {
			fps_diff = __imx_min_fps_diff(dev->targetfps,
						tmp_res->fps_options);
			if (fps_diff < min_fps_diff) {
				min_fps_diff = fps_diff;
				idx = i;
			}
		}
	}

	/*
	 * FIXME!
	 * only IMX135 for Saltbay and IMX227 use this algorithm
	 */
	if (idx == -1 && again == true && dev->new_res_sel_method) {
		again = false;
		goto retry;
	}
	return idx;
}

/* Call with ctrl_handler.lock hold */
static int __adjust_hvblank(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);
	u16 new_frame_length_lines, new_line_length_pck;
	int ret;

	/*
	 * No need to adjust h/v blank if not set dbg value
	 * Note that there is no other checking on the h/v blank value,
	 * as h/v blank can be set to any value above zero for debug purpose
	 */
	if (!dev->v_blank->val || !dev->h_blank->val)
		return 0;

	new_frame_length_lines = dev->curr_res_table[dev->fmt_idx].height +
		dev->v_blank->val;
	new_line_length_pck = dev->curr_res_table[dev->fmt_idx].width +
		dev->h_blank->val;

	ret = imx_write_reg(client, IMX_16BIT,
		dev->reg_addr->line_length_pixels, new_line_length_pck);
	if (ret)
		return ret;
	ret = imx_write_reg(client, IMX_16BIT,
		dev->reg_addr->frame_length_lines, new_frame_length_lines);
	if (ret)
		return ret;

	dev->lines_per_frame = new_frame_length_lines;
	dev->pixels_per_line = new_line_length_pck;

	return 0;
}

static int imx_set_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct imx_device *dev = to_imx_sensor(sd);
	struct camera_mipi_info *imx_info = NULL;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct imx_resolution *res;
	int lanes = imx_get_lanes(sd);
	int ret;
	u16 data, val;
	 int idx;
	if (format->pad)
		return -EINVAL;
	if (!fmt)
		return -EINVAL;

	imx_info = v4l2_get_subdev_hostdata(sd);
	if (imx_info == NULL)
		return -EINVAL;
	if ((fmt->width > imx_max_res[dev->sensor_id].res_max_width)
		|| (fmt->height > imx_max_res[dev->sensor_id].res_max_height)) {
		fmt->width =  imx_max_res[dev->sensor_id].res_max_width;
		fmt->height = imx_max_res[dev->sensor_id].res_max_height;
	} else {
		idx = nearest_resolution_index(sd, fmt->width, fmt->height);

		/*
		 * nearest_resolution_index() doesn't return smaller
		 *  resolutions. If it fails, it means the requested
		 *  resolution is higher than wecan support. Fallback
		 *  to highest possible resolution in this case.
		 */
		if (idx == -1)
			idx = dev->entries_curr_table - 1;

		fmt->width = dev->curr_res_table[idx].width;
		fmt->height = dev->curr_res_table[idx].height;
	}

	fmt->code = dev->format.code;
    if(format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *fmt;
		return 0;
	}
	mutex_lock(&dev->input_lock);

	dev->fmt_idx = nearest_resolution_index(sd, fmt->width, fmt->height);
	if (dev->fmt_idx == -1) {
		ret = -EINVAL;
		goto out;
	}
	res = &dev->curr_res_table[dev->fmt_idx];

	/* Adjust the FPS selection based on the resolution selected */
	dev->fps_index = __imx_nearest_fps_index(dev->targetfps,
						res->fps_options);
	dev->fps = res->fps_options[dev->fps_index].fps;
	dev->regs = res->fps_options[dev->fps_index].regs;
	if (!dev->regs)
		dev->regs = res->regs;

	ret = imx_write_reg_array(client, dev->regs);
	if (ret)
		goto out;

	if (dev->sensor_id == IMX132_ID && lanes > 0) {
		static const u8 imx132_rgpltd[] = {
			2,		/* 1 lane:  /1 */
			0,		/* 2 lanes: /2 */
			0,		/* undefined   */
			1,		/* 4 lanes: /4 */
		};
		ret = imx_write_reg(client, IMX_8BIT, IMX132_208_VT_RGPLTD,
				    imx132_rgpltd[lanes - 1]);
		if (ret)
			goto out;
	}

	dev->pixels_per_line = res->fps_options[dev->fps_index].pixels_per_line;
	dev->lines_per_frame = res->fps_options[dev->fps_index].lines_per_frame;

	/* dbg h/v blank time */
	__adjust_hvblank(sd);

	ret = __imx_update_exposure_timing(client, dev->coarse_itg,
		dev->pixels_per_line, dev->lines_per_frame);
	if (ret)
		goto out;

	ret = __imx_update_gain(sd, dev->gain);
	if (ret)
		goto out;

	ret = __imx_update_digital_gain(client, dev->digital_gain);
	if (ret)
		goto out;

	ret = imx_write_reg_array(client, dev->param_update);
	if (ret)
		goto out;

	ret = imx_get_intg_factor(client, imx_info, dev->regs);
	if (ret)
		goto out;

	ret = imx_read_reg(client, IMX_8BIT,
		dev->reg_addr->img_orientation, &val);
	if (ret)
		goto out;
	val &= (IMX_VFLIP_BIT|IMX_HFLIP_BIT);
	imx_info->raw_bayer_order = imx_bayer_order_mapping[val];
	dev->format.code = imx_translate_bayer_order(
		imx_info->raw_bayer_order);

	/*
	 * Fill meta data info. add imx135 metadata setting for RAW10 format
	 */
	switch (dev->sensor_id) {
	case IMX135_ID:
		ret = imx_read_reg(client, 2,
				IMX135_OUTPUT_DATA_FORMAT_REG, &data);
		if (ret)
			goto out;
		/*
		 * The IMX135 can support various resolutions like
		 * RAW6/8/10/12/14.
		 * 1.The data format is RAW10:
		 *   matadata width = current resolution width(pixel) * 10 / 8
		 * 2.The data format is RAW6 or RAW8:
		 *   matadata width = current resolution width(pixel);
		 * 3.other data format(RAW12/14 etc):
		 *   TBD.
		 */
		if (data == IMX135_OUTPUT_FORMAT_RAW10)
			/* the data format is RAW10. */
			imx_info->metadata_width = res->width * 10 / 8;
		else
			/* The data format is RAW6/8/12/14/ etc. */
			imx_info->metadata_width = res->width;

		imx_info->metadata_height = IMX135_EMBEDDED_DATA_LINE_NUM;

		if (imx_info->metadata_effective_width == NULL)
			imx_info->metadata_effective_width =
				imx135_embedded_effective_size;

		break;
	case IMX227_ID:
		ret = imx_read_reg(client, 2, IMX227_OUTPUT_DATA_FORMAT_REG,
			&data);
		if (ret)
			goto out;
		if (data == IMX227_OUTPUT_FORMAT_RAW10)
			/* the data format is RAW10. */
			imx_info->metadata_width = res->width * 10 / 8;
		else
			/* The data format is RAW6/8/12/14/ etc. */
			imx_info->metadata_width = res->width;

		imx_info->metadata_height = IMX227_EMBEDDED_DATA_LINE_NUM;

		if (imx_info->metadata_effective_width == NULL)
			imx_info->metadata_effective_width =
				imx227_embedded_effective_size;

		break;
	default:
		imx_info->metadata_width = 0;
		imx_info->metadata_height = 0;
		imx_info->metadata_effective_width = NULL;
		break;
	}

out:
	mutex_unlock(&dev->input_lock);
	return ret;
}


static int imx_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct imx_device *dev = to_imx_sensor(sd);

	if (format->pad)
		return -EINVAL;
	if (!fmt)
		return -EINVAL;

	mutex_lock(&dev->input_lock);
	fmt->width = dev->curr_res_table[dev->fmt_idx].width;
	fmt->height = dev->curr_res_table[dev->fmt_idx].height;
	fmt->code = dev->format.code;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int imx_detect(struct i2c_client *client, u16 *id, u8 *revision)
{
	struct i2c_adapter *adapter = client->adapter;

	/* i2c check */
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -ENODEV;

	/* check sensor chip ID	 */
	if (imx_read_reg(client, IMX_16BIT, IMX132_175_208_219_CHIP_ID, id)) {
		v4l2_err(client, "sensor_id = 0x%x\n", *id);
		return -ENODEV;
	}

	if (*id == IMX132_ID || *id == IMX175_ID ||
		*id == IMX208_ID || *id == IMX219_ID)
		goto found;

	if (imx_read_reg(client, IMX_16BIT, IMX134_135_227_CHIP_ID, id)) {
		v4l2_err(client, "sensor_id = 0x%x\n", *id);
		return -ENODEV;
	}
	if (*id != IMX134_ID && *id != IMX135_ID && *id != IMX227_ID) {
		v4l2_err(client, "no imx sensor found\n");
		return -ENODEV;
	}
found:
	v4l2_info(client, "sensor_id = 0x%x\n", *id);

	/* TODO - need to be updated */
	*revision = 0;

	return 0;
}

static void __imx_print_timing(struct v4l2_subdev *sd)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 width = dev->curr_res_table[dev->fmt_idx].width;
	u16 height = dev->curr_res_table[dev->fmt_idx].height;

	dev_dbg(&client->dev, "Dump imx timing in stream on:\n");
	dev_dbg(&client->dev, "width: %d:\n", width);
	dev_dbg(&client->dev, "height: %d:\n", height);
	dev_dbg(&client->dev, "pixels_per_line: %d:\n", dev->pixels_per_line);
	dev_dbg(&client->dev, "line per frame: %d:\n", dev->lines_per_frame);
	dev_dbg(&client->dev, "pix freq: %d:\n", dev->vt_pix_clk_freq_mhz);
	dev_dbg(&client->dev, "init fps: %d:\n", dev->vt_pix_clk_freq_mhz /
			dev->pixels_per_line / dev->lines_per_frame);
	dev_dbg(&client->dev, "HBlank: %d nS:\n",
			1000 * (dev->pixels_per_line - width) /
			(dev->vt_pix_clk_freq_mhz / 1000000));
	dev_dbg(&client->dev, "VBlank: %d uS:\n",
			(dev->lines_per_frame - height) * dev->pixels_per_line /
			(dev->vt_pix_clk_freq_mhz / 1000000));
}

/*
 * imx stream on/off
 */
static int imx_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	if (enable) {
		/* Noise reduction & dead pixel applied before streaming */
		if (dev->fw == NULL) {
			dev_warn(&client->dev, "No MSR loaded from library");
		} else {
			ret = apply_msr_data(client, dev->fw);
			if (ret) {
				mutex_unlock(&dev->input_lock);
				return ret;
			}
		}
		ret = imx_test_pattern(sd);
		if (ret) {
			v4l2_err(client, "Configure test pattern failed.\n");
			mutex_unlock(&dev->input_lock);
			return ret;
		}
		__imx_print_timing(sd);
		ret = imx_write_reg_array(client, imx_streaming);
		if (ret != 0) {
			v4l2_err(client, "write_reg_array err\n");
			mutex_unlock(&dev->input_lock);
			return ret;
		}
		dev->streaming = 1;
		if (dev->vcm_driver && dev->vcm_driver->t_focus_abs_init)
			dev->vcm_driver->t_focus_abs_init(sd);
	} else {
		ret = imx_write_reg_array(client, imx_soft_standby);
		if (ret != 0) {
			v4l2_err(client, "write_reg_array err\n");
			mutex_unlock(&dev->input_lock);
			return ret;
		}
		dev->streaming = 0;
		dev->targetfps = 0;
	}
	mutex_unlock(&dev->input_lock);

	return 0;
}

static int __update_imx_device_settings(struct imx_device *dev, u16 sensor_id)
{
	/* IMX on other platform is not supported yet */
	return -EINVAL;
}

static int imx_s_config(struct v4l2_subdev *sd,
			    int irq, void *pdata)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 sensor_revision;
	u16 sensor_id;
	int ret;
	if (pdata == NULL)
		return -ENODEV;

	dev->platform_data = pdata;

	mutex_lock(&dev->input_lock);

	if (dev->platform_data->platform_init) {
		ret = dev->platform_data->platform_init(client);
		if (ret) {
			mutex_unlock(&dev->input_lock);
			dev_err(&client->dev, "imx platform init err\n");
			return ret;
		}
	}
	/*
	 * power off the module first.
	 *
	 * As first power on by board have undecided state of power/gpio pins.
	 */
	ret = __imx_s_power(sd, 0);
	if (ret) {
		v4l2_err(client, "imx power-down err.\n");
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = __imx_s_power(sd, 1);
	if (ret) {
		v4l2_err(client, "imx power-up err.\n");
		mutex_unlock(&dev->input_lock);
		return ret;
	}

	ret = dev->platform_data->csi_cfg(sd, 1);
	if (ret)
		goto fail_csi_cfg;

	/* config & detect sensor */
	ret = imx_detect(client, &sensor_id, &sensor_revision);
	if (ret) {
		v4l2_err(client, "imx_detect err s_config.\n");
		goto fail_detect;
	}

	dev->sensor_id = sensor_id;
	dev->sensor_revision = sensor_revision;

	/* Resolution settings depend on sensor type and platform */
	ret = __update_imx_device_settings(dev, dev->sensor_id);
	if (ret)
		goto fail_detect;
	/* Read sensor's OTP data */
	dev->otp_data = dev->otp_driver->otp_read(sd,
		dev->otp_driver->dev_addr, dev->otp_driver->start_addr,
		dev->otp_driver->size);

	/* power off sensor */
	ret = __imx_s_power(sd, 0);

	mutex_unlock(&dev->input_lock);
	if (ret)
		v4l2_err(client, "imx power-down err.\n");

	return ret;

fail_detect:
	dev->platform_data->csi_cfg(sd, 0);
fail_csi_cfg:
	__imx_s_power(sd, 0);
	if (dev->platform_data->platform_deinit)
		dev->platform_data->platform_deinit();
	mutex_unlock(&dev->input_lock);
	dev_err(&client->dev, "sensor power-gating failed\n");
	return ret;
}

static int
imx_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		   struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx_device *dev = to_imx_sensor(sd);
	if (code->index >= MAX_FMTS)
		return -EINVAL;

	mutex_lock(&dev->input_lock);
	code->code = dev->format.code;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int
imx_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		    struct v4l2_subdev_frame_size_enum *fse)
{
	int index = fse->index;
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	if (index >= dev->entries_curr_table) {
		mutex_unlock(&dev->input_lock);
		return -EINVAL;
	}

	fse->min_width = dev->curr_res_table[index].width;
	fse->min_height = dev->curr_res_table[index].height;
	fse->max_width = dev->curr_res_table[index].width;
	fse->max_height = dev->curr_res_table[index].height;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int
imx_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *param)
{
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	dev->run_mode = param->parm.capture.capturemode;

	switch (dev->run_mode) {
	case CI_MODE_VIDEO:
		dev->curr_res_table = dev->mode_tables->res_video;
		dev->entries_curr_table = dev->mode_tables->n_res_video;
		break;
	case CI_MODE_STILL_CAPTURE:
		dev->curr_res_table = dev->mode_tables->res_still;
		dev->entries_curr_table = dev->mode_tables->n_res_still;
		break;
	default:
		dev->curr_res_table = dev->mode_tables->res_preview;
		dev->entries_curr_table = dev->mode_tables->n_res_preview;
	}
	mutex_unlock(&dev->input_lock);
	return 0;
}

int
imx_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *interval)
{
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	interval->interval.denominator = dev->fps;
	interval->interval.numerator = 1;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int __imx_s_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *interval)
{
	struct imx_device *dev = to_imx_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct imx_resolution *res =
				&dev->curr_res_table[dev->fmt_idx];
	struct camera_mipi_info *imx_info = NULL;
	unsigned short pixels_per_line;
	unsigned short lines_per_frame;
	unsigned int fps_index;
	int fps;
	int ret = 0;


	imx_info = v4l2_get_subdev_hostdata(sd);
	if (imx_info == NULL)
		return -EINVAL;

	if (!interval->interval.numerator)
		interval->interval.numerator = 1;

	fps = interval->interval.denominator / interval->interval.numerator;

	if (!fps)
		return -EINVAL;

	dev->targetfps = fps;
	/* No need to proceed further if we are not streaming */
	if (!dev->streaming)
		return 0;

	 /* Ignore if we are already using the required FPS. */
	if (fps == dev->fps)
		return 0;

	/*
	 * Start here, sensor is already streaming, so adjust fps dynamically
	 */
	fps_index = __imx_above_nearest_fps_index(fps, res->fps_options);
	if (fps > res->fps_options[fps_index].fps) {
		/*
		 * if does not have high fps setting, not support increase fps
		 * by adjust lines per frame.
		 */
		dev_err(&client->dev, "Could not support fps: %d.\n", fps);
		return -EINVAL;
	}

	if (res->fps_options[fps_index].regs &&
	    res->fps_options[fps_index].regs != dev->regs) {
		/*
		 * if need a new setting, but the new setting has difference
		 * with current setting, not use this one, as may have
		 * unexpected result, e.g. PLL, IQ.
		 */
		dev_dbg(&client->dev,
			"Sensor is streaming, not apply new sensor setting\n");
		if (fps > res->fps_options[dev->fps_index].fps) {
			/*
			 * Does not support increase fps based on low fps
			 * setting, as the high fps setting could not be used,
			 * and fps requested is above current setting fps.
			 */
			dev_warn(&client->dev,
			"Could not support fps: %d, keep current: %d.\n",
			fps, dev->fps);
			return 0;
		}
	} else {
		dev->fps_index = fps_index;
		dev->fps = res->fps_options[dev->fps_index].fps;
	}

	/* Update the new frametimings based on FPS */
	pixels_per_line = res->fps_options[dev->fps_index].pixels_per_line;
	lines_per_frame = res->fps_options[dev->fps_index].lines_per_frame;

	if (fps > res->fps_options[fps_index].fps) {
		/*
		 * if does not have high fps setting, not support increase fps
		 * by adjust lines per frame.
		 */
		dev_warn(&client->dev, "Could not support fps: %d. Use:%d.\n",
				fps, res->fps_options[fps_index].fps);
		goto done;
	}

	/* if the new setting does not match exactly */
	if (dev->fps != fps) {
#define MAX_LINES_PER_FRAME	0xffff
		dev_dbg(&client->dev, "adjusting fps using lines_per_frame\n");
		/*
		 * FIXME!
		 * 1: check DS on max value of lines_per_frame
		 * 2: consider use pixel per line for more range?
		 */
		if (dev->lines_per_frame * dev->fps / fps >
			MAX_LINES_PER_FRAME) {
			dev_warn(&client->dev,
		"adjust lines_per_frame out of range, try to use max value.\n");
			lines_per_frame = MAX_LINES_PER_FRAME;
		} else {
			lines_per_frame = lines_per_frame * dev->fps / fps;
		}
	}
done:
	/* Update the new frametimings based on FPS */
	dev->pixels_per_line = pixels_per_line;
	dev->lines_per_frame = lines_per_frame;

	/* Update the new values so that user side knows the current settings */
	ret = __imx_update_exposure_timing(client,
		dev->coarse_itg, dev->pixels_per_line, dev->lines_per_frame);
	if (ret)
		return ret;

	dev->fps = fps;

	ret = imx_get_intg_factor(client, imx_info, dev->regs);
	if (ret)
		return ret;

	interval->interval.denominator = res->fps_options[dev->fps_index].fps;
	interval->interval.numerator = 1;
	__imx_print_timing(sd);

	return ret;
}

static int imx_s_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *interval)
{
	struct imx_device *dev = to_imx_sensor(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	ret = __imx_s_frame_interval(sd, interval);
	mutex_unlock(&dev->input_lock);

	return ret;
}
static int imx_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct imx_device *dev = to_imx_sensor(sd);

	mutex_lock(&dev->input_lock);
	*frames = dev->curr_res_table[dev->fmt_idx].skip_frames;
	mutex_unlock(&dev->input_lock);

	return 0;
}

static const struct v4l2_subdev_sensor_ops imx_sensor_ops = {
	.g_skip_frames	= imx_g_skip_frames,
};

static const struct v4l2_subdev_video_ops imx_video_ops = {
	.s_stream = imx_s_stream,
	.s_parm = imx_s_parm,
	.g_frame_interval = imx_g_frame_interval,
	.s_frame_interval = imx_s_frame_interval,
};

static const struct v4l2_subdev_core_ops imx_core_ops = {
	.s_power = imx_s_power,
	.ioctl = imx_ioctl,
	.init = imx_init,
};

static const struct v4l2_subdev_pad_ops imx_pad_ops = {
	.enum_mbus_code = imx_enum_mbus_code,
	.enum_frame_size = imx_enum_frame_size,
	.get_fmt = imx_get_fmt,
	.set_fmt = imx_set_fmt,
};

static const struct v4l2_subdev_ops imx_ops = {
	.core = &imx_core_ops,
	.video = &imx_video_ops,
	.pad = &imx_pad_ops,
	.sensor = &imx_sensor_ops,
};

static const struct media_entity_operations imx_entity_ops = {
	.link_setup = NULL,
};

static int imx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx_device *dev = to_imx_sensor(sd);

	if (dev->platform_data->platform_deinit)
		dev->platform_data->platform_deinit();

	media_entity_cleanup(&dev->sd.entity);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	dev->platform_data->csi_cfg(sd, 0);
	v4l2_device_unregister_subdev(sd);
	release_msr_list(client, dev->fw);
	kfree(dev);

	return 0;
}

static int __imx_init_ctrl_handler(struct imx_device *dev)
{
	struct v4l2_ctrl_handler *hdl;
	int i;

	hdl = &dev->ctrl_handler;

	v4l2_ctrl_handler_init(&dev->ctrl_handler, ARRAY_SIZE(imx_controls));

	for (i = 0; i < ARRAY_SIZE(imx_controls); i++)
		v4l2_ctrl_new_custom(&dev->ctrl_handler,
				&imx_controls[i], NULL);

	dev->pixel_rate = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_PIXEL_RATE);
	dev->h_blank = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_HBLANK);
	dev->v_blank = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_VBLANK);
	dev->link_freq = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_LINK_FREQ);
	dev->h_flip = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_HFLIP);
	dev->v_flip = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_VFLIP);
	dev->tp_mode = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_TEST_PATTERN);
	dev->tp_r = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_TEST_PATTERN_COLOR_R);
	dev->tp_gr = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_TEST_PATTERN_COLOR_GR);
	dev->tp_gb = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_TEST_PATTERN_COLOR_GB);
	dev->tp_b = v4l2_ctrl_find(&dev->ctrl_handler,
				V4L2_CID_TEST_PATTERN_COLOR_B);

	if (dev->ctrl_handler.error || dev->pixel_rate == NULL
		|| dev->h_blank == NULL || dev->v_blank == NULL
		|| dev->h_flip == NULL || dev->v_flip == NULL
		|| dev->link_freq == NULL) {
		return dev->ctrl_handler.error;
	}

	dev->ctrl_handler.lock = &dev->input_lock;
	dev->sd.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(&dev->ctrl_handler);

	return 0;
}

static void imx_update_reg_info(struct imx_device *dev)
{
	if (dev->sensor_id == IMX219_ID) {
		dev->reg_addr = &imx219_addr;
		dev->param_hold = imx219_param_hold;
		dev->param_update = imx219_param_update;
	} else {
		dev->reg_addr = &imx_addr;
		dev->param_hold = imx_param_hold;
		dev->param_update = imx_param_update;
	}
}

static int imx_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct imx_device *dev;
	struct camera_mipi_info *imx_info = NULL;
	int ret;
	char *msr_file_name = NULL;

	/* allocate sensor device & init sub device */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		v4l2_err(client, "%s: out of memory\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&dev->input_lock);

	dev->i2c_id = id->driver_data;
	dev->fmt_idx = 0;
	dev->sensor_id = IMX_ID_DEFAULT;
	dev->vcm_driver = &imx_vcms[IMX_ID_DEFAULT];
	dev->digital_gain = 256;

	v4l2_i2c_subdev_init(&(dev->sd), client, &imx_ops);

	if (client->dev.platform_data) {
		ret = imx_s_config(&dev->sd, client->irq,
				       client->dev.platform_data);
		if (ret)
			goto out_free;
	}
	imx_info = v4l2_get_subdev_hostdata(&dev->sd);

	/*
	 * sd->name is updated with sensor driver name by the v4l2.
	 * change it to sensor name in this case.
	 */
	imx_update_reg_info(dev);
	snprintf(dev->sd.name, sizeof(dev->sd.name), "%s%x %d-%04x",
		IMX_SUBDEV_PREFIX, dev->sensor_id,
		i2c_adapter_id(client->adapter), client->addr);

	ret = __imx_init_ctrl_handler(dev);
	if (ret)
		goto out_ctrl_handler_free;

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->format.code = imx_translate_bayer_order(
		imx_info->raw_bayer_order);
	dev->sd.entity.ops = &imx_entity_ops;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret) {
		imx_remove(client);
		return ret;
	}

	/* Load the Noise reduction, Dead pixel registers from cpf file*/
	if (dev->platform_data->msr_file_name != NULL)
		msr_file_name = dev->platform_data->msr_file_name();
	if (msr_file_name) {
		ret = load_msr_list(client, msr_file_name, &dev->fw);
		if (ret) {
			imx_remove(client);
			return ret;
		}
	} else {
		dev_warn(&client->dev, "Drvb file not present");
	}

	return ret;

out_ctrl_handler_free:
	v4l2_ctrl_handler_free(&dev->ctrl_handler);

out_free:
	v4l2_device_unregister_subdev(&dev->sd);
	kfree(dev);
	return ret;
}

static const struct i2c_device_id imx_ids[] = {
	{IMX_NAME_175, IMX175_ID},
	{IMX_NAME_135, IMX135_ID},
	{IMX_NAME_135_FUJI, IMX135_FUJI_ID},
	{IMX_NAME_134, IMX134_ID},
	{IMX_NAME_132, IMX132_ID},
	{IMX_NAME_208, IMX208_ID},
	{IMX_NAME_219, IMX219_ID},
	{IMX_NAME_227, IMX227_ID},
	{}
};

MODULE_DEVICE_TABLE(i2c, imx_ids);

static struct i2c_driver imx_driver = {
	.driver = {
		.name = IMX_DRIVER,
	},
	.probe = imx_probe,
	.remove = imx_remove,
	.id_table = imx_ids,
};

static __init int init_imx(void)
{
	return i2c_add_driver(&imx_driver);
}

static __exit void exit_imx(void)
{
	i2c_del_driver(&imx_driver);
}

module_init(init_imx);
module_exit(exit_imx);

MODULE_DESCRIPTION("A low-level driver for Sony IMX sensors");
MODULE_AUTHOR("Shenbo Huang <shenbo.huang@intel.com>");
MODULE_LICENSE("GPL");

