/*
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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

#include "../include/linux/atomisp.h"
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include "ap1302.h"

#define to_ap1302_device(sub_dev) \
		container_of(sub_dev, struct ap1302_device, sd)

/* Static definitions */
static struct regmap_config ap1302_reg16_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static struct regmap_config ap1302_reg32_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
};

static enum ap1302_contexts ap1302_cntx_mapping[] = {
	CONTEXT_PREVIEW,	/* Invalid atomisp run mode */
	CONTEXT_VIDEO,		/* ATOMISP_RUN_MODE_VIDEO */
	CONTEXT_SNAPSHOT,	/* ATOMISP_RUN_MODE_STILL_CAPTURE */
	CONTEXT_SNAPSHOT,	/* ATOMISP_RUN_MODE_CONTINUOUS_CAPTURE */
	CONTEXT_PREVIEW,	/* ATOMISP_RUN_MODE_PREVIEW */
};

static struct ap1302_res_struct ap1302_preview_res[] = {
	{
		.width = 640,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 720,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 1280,
		.height = 720,
		.fps = 30,
	},
	{
		.width = 1920,
		.height = 1080,
		.fps = 30,
	}
};

static struct ap1302_res_struct ap1302_snapshot_res[] = {
	{
		.width = 640,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 720,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 1280,
		.height = 720,
		.fps = 30,
	},
	{
		.width = 1920,
		.height = 1080,
		.fps = 30,
	}
};

static struct ap1302_res_struct ap1302_video_res[] = {
	{
		.width = 640,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 720,
		.height = 480,
		.fps = 30,
	},
	{
		.width = 1280,
		.height = 720,
		.fps = 30,
	},
	{
		.width = 1920,
		.height = 1080,
		.fps = 30,
	}
};

static enum ap1302_contexts stream_to_context[] = {
	CONTEXT_SNAPSHOT,
	CONTEXT_PREVIEW,
	CONTEXT_PREVIEW,
	CONTEXT_VIDEO
};

static u16 aux_stream_config[CONTEXT_NUM][CONTEXT_NUM] = {
	{0, 0, 0},	/* Preview: No aux streams. */
	{1, 0, 2},	/* Snapshot: 1 for postview. 2 for video */
	{1, 0, 0},	/* Video: 1 for preview. */
};

static struct ap1302_context_info context_info[] = {
	{CNTX_WIDTH, AP1302_REG16, "width"},
	{CNTX_HEIGHT, AP1302_REG16, "height"},
	{CNTX_ROI_X0, AP1302_REG16, "roi_x0"},
	{CNTX_ROI_X1, AP1302_REG16, "roi_x1"},
	{CNTX_ROI_Y0, AP1302_REG16, "roi_y0"},
	{CNTX_ROI_Y1, AP1302_REG16, "roi_y1"},
	{CNTX_ASPECT, AP1302_REG16, "aspect"},
	{CNTX_LOCK, AP1302_REG16, "lock"},
	{CNTX_ENABLE, AP1302_REG16, "enable"},
	{CNTX_OUT_FMT, AP1302_REG16, "out_fmt"},
	{CNTX_SENSOR_MODE, AP1302_REG16, "sensor_mode"},
	{CNTX_MIPI_CTRL, AP1302_REG16, "mipi_ctrl"},
	{CNTX_MIPI_II_CTRL, AP1302_REG16, "mipi_ii_ctrl"},
	{CNTX_LINE_TIME, AP1302_REG32, "line_time"},
	{CNTX_MAX_FPS, AP1302_REG16, "max_fps"},
	{CNTX_AE_USG, AP1302_REG16, "ae_usg"},
	{CNTX_AE_UPPER_ET, AP1302_REG32, "ae_upper_et"},
	{CNTX_AE_MAX_ET, AP1302_REG32, "ae_max_et"},
	{CNTX_SS, AP1302_REG16, "ss"},
	{CNTX_S1_SENSOR_MODE, AP1302_REG16, "s1_sensor_mode"},
	{CNTX_HINF_CTRL, AP1302_REG16, "hinf_ctrl"},
};

/* This array stores the description list for metadata.
   The metadata contains exposure settings and face
   detection results. */
static u16 ap1302_ss_list[] = {
	0xb01c, /* From 0x0186 with size 0x1C are exposure settings. */
	0x0186,
	0xb002, /* 0x71c0 is for F-number */
	0x71c0,
	0xb010, /* From 0x03dc with size 0x10 are face general infos. */
	0x03dc,
	0xb0a0, /* From 0x03e4 with size 0xa0 are face detail infos. */
	0x03e4,
	0xb020, /* From 0x0604 with size 0x20 are smile rate infos. */
	0x0604,
	0x0000
};

/* End of static definitions */

static int ap1302_i2c_read_reg(struct v4l2_subdev *sd,
				u16 reg, u16 len, void *val)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	if (len == AP1302_REG16)
		ret = regmap_read(dev->regmap16, reg, val);
	else if (len == AP1302_REG32)
		ret = regmap_read(dev->regmap32, reg, val);
	else
		ret = -EINVAL;
	if (ret) {
		dev_dbg(&client->dev, "Read reg failed. reg=0x%04X\n", reg);
		return ret;
	}
	if (len == AP1302_REG16)
		dev_dbg(&client->dev, "read_reg[0x%04X] = 0x%04X\n",
			reg, *(u16 *)val);
	else
		dev_dbg(&client->dev, "read_reg[0x%04X] = 0x%08X\n",
			reg, *(u32 *)val);
	return ret;
}

static int ap1302_i2c_write_reg(struct v4l2_subdev *sd,
				u16 reg, u16 len, u32 val)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;
	if (len == AP1302_REG16)
		ret = regmap_write(dev->regmap16, reg, val);
	else if (len == AP1302_REG32)
		ret = regmap_write(dev->regmap32, reg, val);
	else
		ret = -EINVAL;
	if (ret) {
		dev_dbg(&client->dev, "Write reg failed. reg=0x%04X\n", reg);
		return ret;
	}
	if (len == AP1302_REG16)
		dev_dbg(&client->dev, "write_reg[0x%04X] = 0x%04X\n",
			reg, (u16)val);
	else
		dev_dbg(&client->dev, "write_reg[0x%04X] = 0x%08X\n",
			reg, (u32)val);
	return ret;
}

static u16
ap1302_calculate_context_reg_addr(enum ap1302_contexts context, u16 offset)
{
	u16 reg_addr;
	/* The register offset is defined according to preview/video registers.
	   Preview and video context have the same register definition.
	   But snapshot context does not have register S1_SENSOR_MODE.
	   When setting snapshot registers, if the offset exceeds
	   S1_SENSOR_MODE, the actual offset needs to minus 2. */
	if (context == CONTEXT_SNAPSHOT) {
		if (offset == CNTX_S1_SENSOR_MODE)
			return 0;
		if (offset > CNTX_S1_SENSOR_MODE)
			offset -= 2;
	}
	if (context == CONTEXT_PREVIEW)
		reg_addr = REG_PREVIEW_BASE + offset;
	else if (context == CONTEXT_VIDEO)
		reg_addr = REG_VIDEO_BASE + offset;
	else
		reg_addr = REG_SNAPSHOT_BASE + offset;
	return reg_addr;
}

static int ap1302_read_context_reg(struct v4l2_subdev *sd,
		enum ap1302_contexts context, u16 offset, u16 len)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	u16 reg_addr = ap1302_calculate_context_reg_addr(context, offset);
	if (reg_addr == 0)
		return -EINVAL;
	return ap1302_i2c_read_reg(sd, reg_addr, len,
			    ((u8 *)&dev->cntx_config[context]) + offset);
}

static int ap1302_write_context_reg(struct v4l2_subdev *sd,
		enum ap1302_contexts context, u16 offset, u16 len)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	u16 reg_addr = ap1302_calculate_context_reg_addr(context, offset);
	if (reg_addr == 0)
		return -EINVAL;
	return ap1302_i2c_write_reg(sd, reg_addr, len,
			*(u32 *)(((u8 *)&dev->cntx_config[context]) + offset));
}

static int ap1302_dump_context_reg(struct v4l2_subdev *sd,
				   enum ap1302_contexts context)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap1302_device *dev = to_ap1302_device(sd);
	int i;
	dev_dbg(&client->dev, "Dump registers for context[%d]:\n", context);
	for (i = 0; i < ARRAY_SIZE(context_info); i++) {
		struct ap1302_context_info *info = &context_info[i];
		u8 *var = (u8 *)&dev->cntx_config[context] + info->offset;
		/* Snapshot context does not have s1_sensor_mode register. */
		if (context == CONTEXT_SNAPSHOT &&
			info->offset == CNTX_S1_SENSOR_MODE)
			continue;
		ap1302_read_context_reg(sd, context, info->offset, info->len);
		if (info->len == AP1302_REG16)
			dev_dbg(&client->dev, "context.%s = 0x%04X (%d)\n",
				info->name, *(u16 *)var, *(u16 *)var);
		else
			dev_dbg(&client->dev, "context.%s = 0x%08X (%d)\n",
				info->name, *(u32 *)var, *(u32 *)var);
	}
	return 0;
}

static int ap1302_request_firmware(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap1302_device *dev = to_ap1302_device(sd);
	int ret;
	ret = request_firmware(&dev->fw, "ap1302_fw.bin", &client->dev);
	if (ret)
		dev_err(&client->dev,
			"ap1302_request_firmware failed. ret=%d\n", ret);
	return ret;
}

/* When loading firmware, host writes firmware data from address 0x8000.
   When the address reaches 0x9FFF, the next address should return to 0x8000.
   This function handles this address window and load firmware data to AP1302.
   win_pos indicates the offset within this window. Firmware loading procedure
   may call this function several times. win_pos records the current position
   that has been written to.*/
static int ap1302_write_fw_window(struct v4l2_subdev *sd,
				  u16 *win_pos, const u8 *buf, u32 len)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	int ret;
	u32 pos;
	u32 sub_len;
	for (pos = 0; pos < len; pos += sub_len) {
		if (len - pos < AP1302_FW_WINDOW_SIZE - *win_pos)
			sub_len = len - pos;
		else
			sub_len = AP1302_FW_WINDOW_SIZE - *win_pos;
		ret = regmap_raw_write(dev->regmap16,
					*win_pos + AP1302_FW_WINDOW_OFFSET,
					buf + pos, sub_len);
		if (ret)
			return ret;
		*win_pos += sub_len;
		if (*win_pos >= AP1302_FW_WINDOW_SIZE)
			*win_pos = 0;
	}
	return 0;
}

static int ap1302_load_firmware(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ap1302_device *dev = to_ap1302_device(sd);
	const struct ap1302_firmware *fw;
	const u8 *fw_data;
	u16 reg_val = 0;
	u16 win_pos = 0;
	int ret;

	dev_info(&client->dev, "Start to load firmware.\n");
	if (!dev->fw) {
		dev_err(&client->dev, "firmware not requested.\n");
		return -EINVAL;
	}
	fw = (const struct ap1302_firmware *) dev->fw->data;
	if (dev->fw->size != (sizeof(*fw) + fw->total_size)) {
		dev_err(&client->dev, "firmware size does not match.\n");
		return -EINVAL;
	}
	/* The fw binary contains a header of struct ap1302_firmware.
	   Following the header is the bootdata of AP1302.
	   The bootdata pointer can be referenced as &fw[1]. */
	fw_data = (u8 *)&fw[1];

	/* Clear crc register. */
	ret = ap1302_i2c_write_reg(sd, REG_SIP_CRC, AP1302_REG16, 0xFFFF);
	if (ret)
		return ret;

	/* Load FW data for PLL init stage. */
	ret = ap1302_write_fw_window(sd, &win_pos, fw_data, fw->pll_init_size);
	if (ret)
		return ret;

	/* Write 2 to bootdata_stage register to apply basic_init_hp
	   settings and enable PLL. */
	ret = ap1302_i2c_write_reg(sd, REG_BOOTDATA_STAGE,
				   AP1302_REG16, 0x0002);
	if (ret)
		return ret;

	/* Wait 1ms for PLL to lock. */
	msleep(20);

	/* Load the rest of bootdata content. */
	ret = ap1302_write_fw_window(sd, &win_pos, fw_data + fw->pll_init_size,
				     fw->total_size - fw->pll_init_size);
	if (ret)
		return ret;

	/* Check crc. */
	ret = ap1302_i2c_read_reg(sd, REG_SIP_CRC, AP1302_REG16, &reg_val);
	if (ret)
		return ret;
	if (reg_val != fw->crc) {
		dev_err(&client->dev,
			"crc does not match. T:0x%04X F:0x%04X\n",
			fw->crc, reg_val);
		return -EAGAIN;
	}

	/* Write 0xFFFF to bootdata_stage register to indicate AP1302 that
	   the whole bootdata content has been loaded. */
	ret = ap1302_i2c_write_reg(sd, REG_BOOTDATA_STAGE,
				   AP1302_REG16, 0xFFFF);
	if (ret)
		return ret;
	dev_info(&client->dev, "Load firmware successfully.\n");

	return 0;
}

static int __ap1302_s_power(struct v4l2_subdev *sd, int on, int load_fw)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret, i;
	u16 ss_ptr;

	dev_info(&client->dev, "ap1302_s_power is called.\n");
	ret = dev->platform_data->power_ctrl(sd, on);
	if (ret) {
		dev_err(&client->dev,
			"ap1302_s_power error. on=%d ret=%d\n", on, ret);
		return ret;
	}
	dev->power_on = on;
	if (!on || !load_fw)
		return 0;
	/* Load firmware after power on. */
	ret = ap1302_load_firmware(sd);
	if (ret) {
		dev_err(&client->dev,
			"ap1302_load_firmware failed. ret=%d\n", ret);
		return ret;
	}
	ret = ap1302_i2c_read_reg(sd, REG_SS_HEAD_PT0, AP1302_REG16, &ss_ptr);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(ap1302_ss_list); i++) {
		ret = ap1302_i2c_write_reg(sd, ss_ptr + i * 2,
			AP1302_REG16, ap1302_ss_list[i]);
		if (ret)
			return ret;
	}
	return ret;
}

static int ap1302_s_power(struct v4l2_subdev *sd, int on)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	int ret;

	mutex_lock(&dev->input_lock);
	ret = __ap1302_s_power(sd, on, 1);
	dev->sys_activated = 0;
	mutex_unlock(&dev->input_lock);

	return ret;
}

static int ap1302_s_config(struct v4l2_subdev *sd, void *pdata)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *mipi_info;
	u16 reg_val = 0;
	int ret;

	dev_info(&client->dev, "ap1302_s_config is called.\n");
	if (pdata == NULL)
		return -ENODEV;

	dev->platform_data = pdata;

	mutex_lock(&dev->input_lock);

	if (dev->platform_data->platform_init) {
		ret = dev->platform_data->platform_init(client);
		if (ret)
			goto fail_power;
	}

	ret = __ap1302_s_power(sd, 1, 0);
	if (ret)
		goto fail_power;

	/* Detect for AP1302 */
	ret = ap1302_i2c_read_reg(sd, REG_CHIP_VERSION, AP1302_REG16, &reg_val);
	if (ret || (reg_val != AP1302_CHIP_ID)) {
		dev_err(&client->dev,
			"Chip version does no match. ret=%d ver=0x%04x\n",
			ret, reg_val);
		goto fail_config;
	}
	dev_info(&client->dev, "AP1302 Chip ID is 0x%X\n", reg_val);

	/* Detect revision for AP1302 */
	ret = ap1302_i2c_read_reg(sd, REG_CHIP_REV, AP1302_REG16, &reg_val);
	if (ret)
		goto fail_config;
	dev_info(&client->dev, "AP1302 Chip Rev is 0x%X\n", reg_val);
	ret = dev->platform_data->csi_cfg(sd, 1);
	if (ret)
		goto fail_config;

	mipi_info = v4l2_get_subdev_hostdata(sd);
	if (!mipi_info)
		goto fail_config;
	dev->num_lanes = mipi_info->num_lanes;

	ret = __ap1302_s_power(sd, 0, 0);
	if (ret)
		goto fail_power;

	mutex_unlock(&dev->input_lock);

	return ret;

fail_config:
	__ap1302_s_power(sd, 0, 0);
fail_power:
	mutex_unlock(&dev->input_lock);
	dev_err(&client->dev, "ap1302_s_config failed\n");
	return ret;
}

static enum ap1302_contexts ap1302_get_context(struct v4l2_subdev *sd)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	return dev->cur_context;
}

static int ap1302_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_1X16;

	return 0;
}

static int ap1302_match_resolution(struct ap1302_context_res *res,
				   struct v4l2_mbus_framefmt *fmt)
{
	s32 w0, h0, mismatch, distance;
	s32 w1 = fmt->width;
	s32 h1 = fmt->height;
	s32 min_distance = INT_MAX;
	s32 i, idx = -1;

	if (w1 == 0 || h1 == 0)
		return -1;

	for (i = 0; i < res->res_num; i++) {
		w0 = res->res_table[i].width;
		h0 = res->res_table[i].height;
		if (w0 < w1 || h0 < h1)
			continue;
		mismatch = abs(w0 * h1 - w1 * h0) * 8192 / w1 / h0;
		if (mismatch > 8192 * AP1302_MAX_RATIO_MISMATCH / 100)
			continue;
		distance = (w0 * h1 + w1 * h0) * 8192 / w1 / h1;
		if (distance < min_distance) {
			min_distance = distance;
			idx = i;
		}
	}

	return idx;
}

static s32 ap1302_try_mbus_fmt_locked(struct v4l2_subdev *sd,
				enum ap1302_contexts context,
				struct v4l2_mbus_framefmt *fmt)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct ap1302_res_struct *res_table;
	s32 res_num, idx = -1;

	res_table = dev->cntx_res[context].res_table;
	res_num = dev->cntx_res[context].res_num;

	if ((fmt->width <= res_table[res_num - 1].width) &&
		(fmt->height <= res_table[res_num - 1].height))
		idx = ap1302_match_resolution(&dev->cntx_res[context], fmt);
	if (idx == -1)
		idx = res_num - 1;

	fmt->width = res_table[idx].width;
	fmt->height = res_table[idx].height;
	fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;
	return idx;
}


static int ap1302_get_fmt(struct v4l2_subdev *sd,
	                 struct v4l2_subdev_pad_config *cfg,
					 struct v4l2_subdev_format *format)

{
    struct v4l2_mbus_framefmt *fmt = &format->format;
    struct ap1302_device *dev = to_ap1302_device(sd);
	enum ap1302_contexts context;
	struct ap1302_res_struct *res_table;
	s32 cur_res;
     if (format->pad)
		return -EINVAL;
	mutex_lock(&dev->input_lock);
	context = ap1302_get_context(sd);
	res_table = dev->cntx_res[context].res_table;
	cur_res = dev->cntx_res[context].cur_res;
	fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt->width = res_table[cur_res].width;
	fmt->height = res_table[cur_res].height;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int ap1302_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct atomisp_input_stream_info *stream_info =
		(struct atomisp_input_stream_info *)fmt->reserved;
	enum ap1302_contexts context, main_context;
	if (format->pad)
		return -EINVAL;
	if (!fmt)
		return -EINVAL;
	mutex_lock(&dev->input_lock);
	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		context = ap1302_get_context(sd);
		ap1302_try_mbus_fmt_locked(sd, context, fmt);
		cfg->try_fmt = *fmt;
	    mutex_unlock(&dev->input_lock);
		return 0;
		}
	context = stream_to_context[stream_info->stream];
	dev_dbg(&client->dev, "ap1302_set_mbus_fmt. stream=%d context=%d\n",
		stream_info->stream, context);
	dev->cntx_res[context].cur_res =
		ap1302_try_mbus_fmt_locked(sd, context, fmt);
	dev->cntx_config[context].width = fmt->width;
	dev->cntx_config[context].height = fmt->height;
	ap1302_write_context_reg(sd, context, CNTX_WIDTH, AP1302_REG16);
	ap1302_write_context_reg(sd, context, CNTX_HEIGHT, AP1302_REG16);
	ap1302_read_context_reg(sd, context, CNTX_OUT_FMT, AP1302_REG16);
	dev->cntx_config[context].out_fmt &= ~OUT_FMT_TYPE_MASK;
	dev->cntx_config[context].out_fmt |= AP1302_FMT_UYVY422;
	ap1302_write_context_reg(sd, context, CNTX_OUT_FMT, AP1302_REG16);

	main_context = ap1302_get_context(sd);
	if (context == main_context) {
		ap1302_read_context_reg(sd, context,
			CNTX_MIPI_CTRL, AP1302_REG16);
		dev->cntx_config[context].mipi_ctrl &= ~MIPI_CTRL_IMGVC_MASK;
		dev->cntx_config[context].mipi_ctrl |=
			(context << MIPI_CTRL_IMGVC_OFFSET);
		dev->cntx_config[context].mipi_ctrl &= ~MIPI_CTRL_SSVC_MASK;
		dev->cntx_config[context].mipi_ctrl |=
			(context << MIPI_CTRL_SSVC_OFFSET);
		dev->cntx_config[context].mipi_ctrl &= ~MIPI_CTRL_SSTYPE_MASK;
		dev->cntx_config[context].mipi_ctrl |=
			(0x12 << MIPI_CTRL_SSTYPE_OFFSET);
		ap1302_write_context_reg(sd, context,
			CNTX_MIPI_CTRL, AP1302_REG16);
		ap1302_read_context_reg(sd, context,
			CNTX_SS, AP1302_REG16);
		dev->cntx_config[context].ss = AP1302_SS_CTRL;
		ap1302_write_context_reg(sd, context,
			CNTX_SS, AP1302_REG16);
	} else {
		/* Configure aux stream */
		ap1302_read_context_reg(sd, context,
			CNTX_MIPI_II_CTRL, AP1302_REG16);
		dev->cntx_config[context].mipi_ii_ctrl &= ~MIPI_CTRL_IMGVC_MASK;
		dev->cntx_config[context].mipi_ii_ctrl |=
			(context << MIPI_CTRL_IMGVC_OFFSET);
		ap1302_write_context_reg(sd, context,
			CNTX_MIPI_II_CTRL, AP1302_REG16);
		if (stream_info->enable) {
			ap1302_read_context_reg(sd, main_context,
				CNTX_OUT_FMT, AP1302_REG16);
			dev->cntx_config[context].out_fmt |=
				(aux_stream_config[main_context][context]
				 << OUT_FMT_IIS_OFFSET);
			ap1302_write_context_reg(sd, main_context,
				CNTX_OUT_FMT, AP1302_REG16);
		}
	}
	stream_info->ch_id = context;
	mutex_unlock(&dev->input_lock);

	return 0;
}


static int ap1302_g_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *interval)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	enum ap1302_contexts context;
	struct ap1302_res_struct *res_table;
	u32 cur_res;

	mutex_lock(&dev->input_lock);
	context = ap1302_get_context(sd);
	res_table = dev->cntx_res[context].res_table;
	cur_res = dev->cntx_res[context].cur_res;
	interval->interval.denominator = res_table[cur_res].fps;
	interval->interval.numerator = 1;
	mutex_unlock(&dev->input_lock);
	return 0;
}

static int ap1302_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	enum ap1302_contexts context;
	struct ap1302_res_struct *res_table;
	int index = fse->index;

	mutex_lock(&dev->input_lock);
	context = ap1302_get_context(sd);
	if (index >= dev->cntx_res[context].res_num) {
		mutex_unlock(&dev->input_lock);
		return -EINVAL;
	}

	res_table = dev->cntx_res[context].res_table;
	fse->min_width = res_table[index].width;
	fse->min_height = res_table[index].height;
	fse->max_width = res_table[index].width;
	fse->max_height = res_table[index].height;
	mutex_unlock(&dev->input_lock);

	return 0;
}


static int ap1302_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	*frames = 0;
	return 0;
}

static int ap1302_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	enum ap1302_contexts context;
	u32 reg_val;
	int ret;

	mutex_lock(&dev->input_lock);
	context = ap1302_get_context(sd);
	dev_dbg(&client->dev, "ap1302_s_stream. context=%d enable=%d\n",
			context, enable);
	/* Switch context */
	ap1302_i2c_read_reg(sd, REG_CTRL,
			    AP1302_REG16, &reg_val);
	reg_val &= ~CTRL_CNTX_MASK;
	reg_val |= (context<<CTRL_CNTX_OFFSET);
	ap1302_i2c_write_reg(sd, REG_CTRL,
			    AP1302_REG16, reg_val);
	/* Select sensor */
	ap1302_i2c_read_reg(sd, REG_SENSOR_SELECT,
			    AP1302_REG16, &reg_val);
	reg_val &= ~SENSOR_SELECT_MASK;
	reg_val |= (AP1302_SENSOR_PRI<<SENSOR_SELECT_OFFSET);
	ap1302_i2c_write_reg(sd, REG_SENSOR_SELECT,
			    AP1302_REG16, reg_val);
	if (enable) {
		dev_info(&client->dev, "Start stream. context=%d\n", context);
		ap1302_dump_context_reg(sd, context);
		if (!dev->sys_activated) {
			reg_val = AP1302_SYS_ACTIVATE;
			dev->sys_activated = 1;
		} else {
			reg_val = AP1302_SYS_SWITCH;
		}
	} else {
		dev_info(&client->dev, "Stop stream. context=%d\n", context);
		reg_val = AP1302_SYS_SWITCH;
	}
	ret = ap1302_i2c_write_reg(sd, REG_SYS_START, AP1302_REG16, reg_val);
	if (ret)
		dev_err(&client->dev,
			"AP1302 set stream failed. enable=%d\n", enable);
	mutex_unlock(&dev->input_lock);
	return ret;
}

static u16 ap1302_ev_values[] = {0xfd00, 0xfe80, 0x0, 0x180, 0x300};

static int ap1302_set_exposure_off(struct v4l2_subdev *sd, s32 val)
{
	val -= AP1302_MIN_EV;
	return ap1302_i2c_write_reg(sd, REG_AE_BV_OFF, AP1302_REG16,
				ap1302_ev_values[val]);
}

static u16 ap1302_wb_values[] = {
	0, /* V4L2_WHITE_BALANCE_MANUAL */
	0xf, /* V4L2_WHITE_BALANCE_AUTO */
	0x2, /* V4L2_WHITE_BALANCE_INCANDESCENT */
	0x4, /* V4L2_WHITE_BALANCE_FLUORESCENT */
	0x5, /* V4L2_WHITE_BALANCE_FLUORESCENT_H */
	0x1, /* V4L2_WHITE_BALANCE_HORIZON */
	0x5, /* V4L2_WHITE_BALANCE_DAYLIGHT */
	0xf, /* V4L2_WHITE_BALANCE_FLASH */
	0x6, /* V4L2_WHITE_BALANCE_CLOUDY */
	0x6, /* V4L2_WHITE_BALANCE_SHADE */
};

static int ap1302_set_wb_mode(struct v4l2_subdev *sd, s32 val)
{
	int ret = 0;
	u16 reg_val;

	ret = ap1302_i2c_read_reg(sd, REG_AWB_CTRL, AP1302_REG16, &reg_val);
	if (ret)
		return ret;
	reg_val &= ~AWB_CTRL_MODE_MASK;
	reg_val |= ap1302_wb_values[val] << AWB_CTRL_MODE_OFFSET;
	if (val == V4L2_WHITE_BALANCE_FLASH)
		reg_val |= AWB_CTRL_FLASH_MASK;
	else
		reg_val &= ~AWB_CTRL_FLASH_MASK;
	ret = ap1302_i2c_write_reg(sd, REG_AWB_CTRL, AP1302_REG16, reg_val);
	return ret;
}

static int ap1302_set_zoom(struct v4l2_subdev *sd, s32 val)
{
	ap1302_i2c_write_reg(sd, REG_DZ_TGT_FCT, AP1302_REG16,
		val * 4 + 0x100);
	return 0;
}

static u16 ap1302_sfx_values[] = {
	0x00, /* V4L2_COLORFX_NONE */
	0x03, /* V4L2_COLORFX_BW */
	0x0d, /* V4L2_COLORFX_SEPIA */
	0x07, /* V4L2_COLORFX_NEGATIVE */
	0x04, /* V4L2_COLORFX_EMBOSS */
	0x0f, /* V4L2_COLORFX_SKETCH */
	0x08, /* V4L2_COLORFX_SKY_BLUE */
	0x09, /* V4L2_COLORFX_GRASS_GREEN */
	0x0a, /* V4L2_COLORFX_SKIN_WHITEN */
	0x00, /* V4L2_COLORFX_VIVID */
	0x00, /* V4L2_COLORFX_AQUA */
	0x00, /* V4L2_COLORFX_ART_FREEZE */
	0x00, /* V4L2_COLORFX_SILHOUETTE */
	0x10, /* V4L2_COLORFX_SOLARIZATION */
	0x02, /* V4L2_COLORFX_ANTIQUE */
	0x00, /* V4L2_COLORFX_SET_CBCR */
};

static int ap1302_set_special_effect(struct v4l2_subdev *sd, s32 val)
{
	ap1302_i2c_write_reg(sd, REG_SFX_MODE, AP1302_REG16,
		ap1302_sfx_values[val]);
	return 0;
}

static u16 ap1302_scene_mode_values[] = {
	0x00, /* V4L2_SCENE_MODE_NONE */
	0x07, /* V4L2_SCENE_MODE_BACKLIGHT */
	0x0a, /* V4L2_SCENE_MODE_BEACH_SNOW */
	0x06, /* V4L2_SCENE_MODE_CANDLE_LIGHT */
	0x00, /* V4L2_SCENE_MODE_DAWN_DUSK */
	0x00, /* V4L2_SCENE_MODE_FALL_COLORS */
	0x0d, /* V4L2_SCENE_MODE_FIREWORKS */
	0x02, /* V4L2_SCENE_MODE_LANDSCAPE */
	0x05, /* V4L2_SCENE_MODE_NIGHT */
	0x0c, /* V4L2_SCENE_MODE_PARTY_INDOOR */
	0x01, /* V4L2_SCENE_MODE_PORTRAIT */
	0x03, /* V4L2_SCENE_MODE_SPORTS */
	0x0e, /* V4L2_SCENE_MODE_SUNSET */
	0x0b, /* V4L2_SCENE_MODE_TEXT */
};

static int ap1302_set_scene_mode(struct v4l2_subdev *sd, s32 val)
{
	ap1302_i2c_write_reg(sd, REG_SCENE_CTRL, AP1302_REG16,
		ap1302_scene_mode_values[val]);
	return 0;
}

static u16 ap1302_flicker_values[] = {
	0x0,    /* OFF */
	0x3201, /* 50HZ */
	0x3c01, /* 60HZ */
	0x2     /* AUTO */
};

static int ap1302_set_flicker_freq(struct v4l2_subdev *sd, s32 val)
{
	ap1302_i2c_write_reg(sd, REG_FLICK_CTRL, AP1302_REG16,
		ap1302_flicker_values[val]);
	return 0;
}

static int ap1302_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ap1302_device *dev = container_of(
		ctrl->handler, struct ap1302_device, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_RUN_MODE:
		dev->cur_context = ap1302_cntx_mapping[ctrl->val];
		break;
	case V4L2_CID_EXPOSURE:
		ap1302_set_exposure_off(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ap1302_set_wb_mode(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_ZOOM_ABSOLUTE:
		ap1302_set_zoom(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_COLORFX:
		ap1302_set_special_effect(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_SCENE_MODE:
		ap1302_set_scene_mode(&dev->sd, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		ap1302_set_flicker_freq(&dev->sd, ctrl->val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ap1302_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	int ret;
	u32 reg_val;

	if (reg->size != AP1302_REG16 &&
	    reg->size != AP1302_REG32)
		return -EINVAL;

	mutex_lock(&dev->input_lock);
	if (dev->power_on)
		ret = ap1302_i2c_read_reg(sd, reg->reg, reg->size, &reg_val);
	else
		ret = -EIO;
	mutex_unlock(&dev->input_lock);
	if (ret)
		return ret;

	reg->val = reg_val;

	return 0;
}

static int ap1302_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	struct ap1302_device *dev = to_ap1302_device(sd);
	int ret;

	if (reg->size != AP1302_REG16 &&
	    reg->size != AP1302_REG32)
		return -EINVAL;

	mutex_lock(&dev->input_lock);
	if (dev->power_on)
		ret = ap1302_i2c_write_reg(sd, reg->reg, reg->size, reg->val);
	else
		ret = -EIO;
	mutex_unlock(&dev->input_lock);
	return ret;
}

static long ap1302_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	switch (cmd) {
	case VIDIOC_DBG_G_REGISTER:
		ret = ap1302_g_register(sd, arg);
		break;
	case VIDIOC_DBG_S_REGISTER:
		ret = ap1302_s_register(sd, arg);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = ap1302_s_ctrl,
};

static const char * const ctrl_run_mode_menu[] = {
	NULL,
	"Video",
	"Still capture",
	"Continuous capture",
	"Preview",
};

static const struct v4l2_ctrl_config ctrls[] = {
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_RUN_MODE,
		.name = "Run Mode",
		.type = V4L2_CTRL_TYPE_MENU,
		.min = 1,
		.def = 4,
		.max = 4,
		.qmenu = ctrl_run_mode_menu,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_EXPOSURE,
		.name = "Exposure",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = AP1302_MIN_EV,
		.def = 0,
		.max = AP1302_MAX_EV,
		.step = 1,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
		.name = "White Balance",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 0,
		.max = 9,
		.step = 1,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_ZOOM_ABSOLUTE,
		.name = "Zoom Absolute",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 0,
		.max = 1024,
		.step = 1,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_COLORFX,
		.name = "Color Special Effect",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 0,
		.max = 15,
		.step = 1,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_SCENE_MODE,
		.name = "Scene Mode",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 0,
		.max = 13,
		.step = 1,
	},
	{
		.ops = &ctrl_ops,
		.id = V4L2_CID_POWER_LINE_FREQUENCY,
		.name = "Light frequency filter",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = 0,
		.def = 3,
		.max = 3,
		.step = 1,
	},
};

static struct v4l2_subdev_sensor_ops ap1302_sensor_ops = {
	.g_skip_frames	= ap1302_g_skip_frames,
};

static const struct v4l2_subdev_video_ops ap1302_video_ops = {
	.s_stream = ap1302_s_stream,
	.g_frame_interval = ap1302_g_frame_interval,
};

static const struct v4l2_subdev_core_ops ap1302_core_ops = {
	.s_power = ap1302_s_power,
	.ioctl = ap1302_ioctl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ap1302_g_register,
	.s_register = ap1302_s_register,
#endif
};

static const struct v4l2_subdev_pad_ops ap1302_pad_ops = {
	.enum_mbus_code = ap1302_enum_mbus_code,
	.enum_frame_size = ap1302_enum_frame_size,
	.get_fmt = ap1302_get_fmt,
	.set_fmt = ap1302_set_fmt,
};

static const struct v4l2_subdev_ops ap1302_ops = {
	.core = &ap1302_core_ops,
	.pad = &ap1302_pad_ops,
	.video = &ap1302_video_ops,
	.sensor = &ap1302_sensor_ops
};

static int ap1302_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ap1302_device *dev = to_ap1302_device(sd);

	if (dev->platform_data->platform_deinit)
		dev->platform_data->platform_deinit();

	release_firmware(dev->fw);

	media_entity_cleanup(&dev->sd.entity);
	dev->platform_data->csi_cfg(sd, 0);
	v4l2_device_unregister_subdev(sd);

	return 0;
}

static int ap1302_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ap1302_device *dev;
	int ret;
	unsigned int i;

	dev_info(&client->dev, "ap1302 probe called.\n");

	/* allocate device & init sub device */
	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&client->dev, "%s: out of memory\n", __func__);
		return -ENOMEM;
	}

	mutex_init(&dev->input_lock);

	v4l2_i2c_subdev_init(&(dev->sd), client, &ap1302_ops);

	ret = ap1302_request_firmware(&(dev->sd));
	if (ret) {
		dev_err(&client->dev, "Cannot request ap1302 firmware.\n");
		goto out_free;
	}

	dev->regmap16 = devm_regmap_init_i2c(client, &ap1302_reg16_config);
	if (IS_ERR(dev->regmap16)) {
		ret = PTR_ERR(dev->regmap16);
		dev_err(&client->dev,
			"Failed to allocate 16bit register map: %d\n", ret);
		return ret;
	}

	dev->regmap32 = devm_regmap_init_i2c(client, &ap1302_reg32_config);
	if (IS_ERR(dev->regmap32)) {
		ret = PTR_ERR(dev->regmap32);
		dev_err(&client->dev,
			"Failed to allocate 32bit register map: %d\n", ret);
		return ret;
	}

	if (client->dev.platform_data) {
		ret = ap1302_s_config(&dev->sd, client->dev.platform_data);
		if (ret)
			goto out_free;
	}

	dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	dev->pad.flags = MEDIA_PAD_FL_SOURCE;
	dev->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	dev->cntx_res[CONTEXT_PREVIEW].res_num = ARRAY_SIZE(ap1302_preview_res);
	dev->cntx_res[CONTEXT_PREVIEW].res_table = ap1302_preview_res;
	dev->cntx_res[CONTEXT_SNAPSHOT].res_num =
		ARRAY_SIZE(ap1302_snapshot_res);
	dev->cntx_res[CONTEXT_SNAPSHOT].res_table = ap1302_snapshot_res;
	dev->cntx_res[CONTEXT_VIDEO].res_num = ARRAY_SIZE(ap1302_video_res);
	dev->cntx_res[CONTEXT_VIDEO].res_table = ap1302_video_res;

	ret = v4l2_ctrl_handler_init(&dev->ctrl_handler, ARRAY_SIZE(ctrls));
	if (ret) {
		ap1302_remove(client);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(ctrls); i++)
		v4l2_ctrl_new_custom(&dev->ctrl_handler, &ctrls[i], NULL);

	if (dev->ctrl_handler.error) {
		ap1302_remove(client);
		return dev->ctrl_handler.error;
	}

	/* Use same lock for controls as for everything else. */
	dev->ctrl_handler.lock = &dev->input_lock;
	dev->sd.ctrl_handler = &dev->ctrl_handler;
	v4l2_ctrl_handler_setup(&dev->ctrl_handler);

	dev->run_mode = v4l2_ctrl_find(&dev->ctrl_handler, V4L2_CID_RUN_MODE);
	v4l2_ctrl_s_ctrl(dev->run_mode, ATOMISP_RUN_MODE_PREVIEW);

	ret = media_entity_pads_init(&dev->sd.entity, 1, &dev->pad);
	if (ret)
		ap1302_remove(client);
	return ret;
out_free:
	v4l2_device_unregister_subdev(&dev->sd);
	return ret;
}

static const struct i2c_device_id ap1302_id[] = {
	{AP1302_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ap1302_id);

static struct i2c_driver ap1302_driver = {
	.driver = {
		.name = AP1302_NAME,
	},
	.probe = ap1302_probe,
	.remove = ap1302_remove,
	.id_table = ap1302_id,
};

module_i2c_driver(ap1302_driver);

MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_DESCRIPTION("AP1302 Driver");
MODULE_LICENSE("GPL");
