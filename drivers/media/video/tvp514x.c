/*
 * drivers/media/video/tvp514x.c
 *
 * TI TVP5146/47 decoder driver
 *
 * Copyright (C) 2008 Texas Instruments Inc
 * Author: Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * Contributors:
 *     Sivaraj R <sivaraj@ti.com>
 *     Brijesh R Jadav <brijesh.j@ti.com>
 *     Hardik Shah <hardik.shah@ti.com>
 *     Manjunath Hadli <mrh@ti.com>
 *     Karicheri Muralidharan <m-karicheri2@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-int-device.h>
#include <media/tvp514x.h>

#include "tvp514x_regs.h"

/* Module Name */
#define TVP514X_MODULE_NAME		"tvp514x"

/* Private macros for TVP */
#define I2C_RETRY_COUNT                 (5)
#define LOCK_RETRY_COUNT                (5)
#define LOCK_RETRY_DELAY                (200)

/* Debug functions */
static int debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define dump_reg(client, reg, val)				\
	do {							\
		val = tvp514x_read_reg(client, reg);		\
		v4l_info(client, "Reg(0x%.2X): 0x%.2X\n", reg, val); \
	} while (0)

/**
 * enum tvp514x_std - enum for supported standards
 */
enum tvp514x_std {
	STD_NTSC_MJ = 0,
	STD_PAL_BDGHIN,
	STD_INVALID
};

/**
 * enum tvp514x_state - enum for different decoder states
 */
enum tvp514x_state {
	STATE_NOT_DETECTED,
	STATE_DETECTED
};

/**
 * struct tvp514x_std_info - Structure to store standard informations
 * @width: Line width in pixels
 * @height:Number of active lines
 * @video_std: Value to write in REG_VIDEO_STD register
 * @standard: v4l2 standard structure information
 */
struct tvp514x_std_info {
	unsigned long width;
	unsigned long height;
	u8 video_std;
	struct v4l2_standard standard;
};

static struct tvp514x_reg tvp514x_reg_list_default[0x40];
/**
 * struct tvp514x_decoder - TVP5146/47 decoder object
 * @v4l2_int_device: Slave handle
 * @tvp514x_slave: Slave pointer which is used by @v4l2_int_device
 * @tvp514x_regs: copy of hw's regs with preset values.
 * @pdata: Board specific
 * @client: I2C client data
 * @id: Entry from I2C table
 * @ver: Chip version
 * @state: TVP5146/47 decoder state - detected or not-detected
 * @pix: Current pixel format
 * @num_fmts: Number of formats
 * @fmt_list: Format list
 * @current_std: Current standard
 * @num_stds: Number of standards
 * @std_list: Standards list
 * @route: input and output routing at chip level
 */
struct tvp514x_decoder {
	struct v4l2_int_device v4l2_int_device;
	struct v4l2_int_slave tvp514x_slave;
	struct tvp514x_reg tvp514x_regs[ARRAY_SIZE(tvp514x_reg_list_default)];
	const struct tvp514x_platform_data *pdata;
	struct i2c_client *client;

	struct i2c_device_id *id;

	int ver;
	enum tvp514x_state state;

	struct v4l2_pix_format pix;
	int num_fmts;
	const struct v4l2_fmtdesc *fmt_list;

	enum tvp514x_std current_std;
	int num_stds;
	struct tvp514x_std_info *std_list;

	struct v4l2_routing route;
};

/* TVP514x default register values */
static struct tvp514x_reg tvp514x_reg_list_default[] = {
	{TOK_WRITE, REG_INPUT_SEL, 0x05},	/* Composite selected */
	{TOK_WRITE, REG_AFE_GAIN_CTRL, 0x0F},
	{TOK_WRITE, REG_VIDEO_STD, 0x00},	/* Auto mode */
	{TOK_WRITE, REG_OPERATION_MODE, 0x00},
	{TOK_SKIP, REG_AUTOSWITCH_MASK, 0x3F},
	{TOK_WRITE, REG_COLOR_KILLER, 0x10},
	{TOK_WRITE, REG_LUMA_CONTROL1, 0x00},
	{TOK_WRITE, REG_LUMA_CONTROL2, 0x00},
	{TOK_WRITE, REG_LUMA_CONTROL3, 0x02},
	{TOK_WRITE, REG_BRIGHTNESS, 0x80},
	{TOK_WRITE, REG_CONTRAST, 0x80},
	{TOK_WRITE, REG_SATURATION, 0x80},
	{TOK_WRITE, REG_HUE, 0x00},
	{TOK_WRITE, REG_CHROMA_CONTROL1, 0x00},
	{TOK_WRITE, REG_CHROMA_CONTROL2, 0x0E},
	{TOK_SKIP, 0x0F, 0x00},	/* Reserved */
	{TOK_WRITE, REG_COMP_PR_SATURATION, 0x80},
	{TOK_WRITE, REG_COMP_Y_CONTRAST, 0x80},
	{TOK_WRITE, REG_COMP_PB_SATURATION, 0x80},
	{TOK_SKIP, 0x13, 0x00},	/* Reserved */
	{TOK_WRITE, REG_COMP_Y_BRIGHTNESS, 0x80},
	{TOK_SKIP, 0x15, 0x00},	/* Reserved */
	{TOK_SKIP, REG_AVID_START_PIXEL_LSB, 0x55},	/* NTSC timing */
	{TOK_SKIP, REG_AVID_START_PIXEL_MSB, 0x00},
	{TOK_SKIP, REG_AVID_STOP_PIXEL_LSB, 0x25},
	{TOK_SKIP, REG_AVID_STOP_PIXEL_MSB, 0x03},
	{TOK_SKIP, REG_HSYNC_START_PIXEL_LSB, 0x00},	/* NTSC timing */
	{TOK_SKIP, REG_HSYNC_START_PIXEL_MSB, 0x00},
	{TOK_SKIP, REG_HSYNC_STOP_PIXEL_LSB, 0x40},
	{TOK_SKIP, REG_HSYNC_STOP_PIXEL_MSB, 0x00},
	{TOK_SKIP, REG_VSYNC_START_LINE_LSB, 0x04},	/* NTSC timing */
	{TOK_SKIP, REG_VSYNC_START_LINE_MSB, 0x00},
	{TOK_SKIP, REG_VSYNC_STOP_LINE_LSB, 0x07},
	{TOK_SKIP, REG_VSYNC_STOP_LINE_MSB, 0x00},
	{TOK_SKIP, REG_VBLK_START_LINE_LSB, 0x01},	/* NTSC timing */
	{TOK_SKIP, REG_VBLK_START_LINE_MSB, 0x00},
	{TOK_SKIP, REG_VBLK_STOP_LINE_LSB, 0x15},
	{TOK_SKIP, REG_VBLK_STOP_LINE_MSB, 0x00},
	{TOK_SKIP, 0x26, 0x00},	/* Reserved */
	{TOK_SKIP, 0x27, 0x00},	/* Reserved */
	{TOK_SKIP, REG_FAST_SWTICH_CONTROL, 0xCC},
	{TOK_SKIP, 0x29, 0x00},	/* Reserved */
	{TOK_SKIP, REG_FAST_SWTICH_SCART_DELAY, 0x00},
	{TOK_SKIP, 0x2B, 0x00},	/* Reserved */
	{TOK_SKIP, REG_SCART_DELAY, 0x00},
	{TOK_SKIP, REG_CTI_DELAY, 0x00},
	{TOK_SKIP, REG_CTI_CONTROL, 0x00},
	{TOK_SKIP, 0x2F, 0x00},	/* Reserved */
	{TOK_SKIP, 0x30, 0x00},	/* Reserved */
	{TOK_SKIP, 0x31, 0x00},	/* Reserved */
	{TOK_WRITE, REG_SYNC_CONTROL, 0x00},	/* HS, VS active high */
	{TOK_WRITE, REG_OUTPUT_FORMATTER1, 0x00},	/* 10-bit BT.656 */
	{TOK_WRITE, REG_OUTPUT_FORMATTER2, 0x11},	/* Enable clk & data */
	{TOK_WRITE, REG_OUTPUT_FORMATTER3, 0xEE},	/* Enable AVID & FLD */
	{TOK_WRITE, REG_OUTPUT_FORMATTER4, 0xAF},	/* Enable VS & HS */
	{TOK_WRITE, REG_OUTPUT_FORMATTER5, 0xFF},
	{TOK_WRITE, REG_OUTPUT_FORMATTER6, 0xFF},
	{TOK_WRITE, REG_CLEAR_LOST_LOCK, 0x01},	/* Clear status */
	{TOK_TERM, 0, 0},
};

/* List of image formats supported by TVP5146/47 decoder
 * Currently we are using 8 bit mode only, but can be
 * extended to 10/20 bit mode.
 */
static const struct v4l2_fmtdesc tvp514x_fmt_list[] = {
	{
	 .index = 0,
	 .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	 .flags = 0,
	 .description = "8-bit UYVY 4:2:2 Format",
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	},
};

/*
 * Supported standards -
 *
 * Currently supports two standards only, need to add support for rest of the
 * modes, like SECAM, etc...
 */
static struct tvp514x_std_info tvp514x_std_list[] = {
	/* Standard: STD_NTSC_MJ */
	[STD_NTSC_MJ] = {
	 .width = NTSC_NUM_ACTIVE_PIXELS,
	 .height = NTSC_NUM_ACTIVE_LINES,
	 .video_std = VIDEO_STD_NTSC_MJ_BIT,
	 .standard = {
		      .index = 0,
		      .id = V4L2_STD_NTSC,
		      .name = "NTSC",
		      .frameperiod = {1001, 30000},
		      .framelines = 525
		     },
	/* Standard: STD_PAL_BDGHIN */
	},
	[STD_PAL_BDGHIN] = {
	 .width = PAL_NUM_ACTIVE_PIXELS,
	 .height = PAL_NUM_ACTIVE_LINES,
	 .video_std = VIDEO_STD_PAL_BDGHIN_BIT,
	 .standard = {
		      .index = 1,
		      .id = V4L2_STD_PAL,
		      .name = "PAL",
		      .frameperiod = {1, 25},
		      .framelines = 625
		     },
	},
	/* Standard: need to add for additional standard */
};
/*
 * Control structure for Auto Gain
 *     This is temporary data, will get replaced once
 *     v4l2_ctrl_query_fill supports it.
 */
static const struct v4l2_queryctrl tvp514x_autogain_ctrl = {
	.id = V4L2_CID_AUTOGAIN,
	.name = "Gain, Automatic",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.minimum = 0,
	.maximum = 1,
	.step = 1,
	.default_value = 1,
};

/*
 * Read a value from a register in an TVP5146/47 decoder device.
 * Returns value read if successful, or non-zero (-1) otherwise.
 */
static int tvp514x_read_reg(struct i2c_client *client, u8 reg)
{
	int err;
	int retry = 0;
read_again:

	err = i2c_smbus_read_byte_data(client, reg);
	if (err == -1) {
		if (retry <= I2C_RETRY_COUNT) {
			v4l_warn(client, "Read: retry ... %d\n", retry);
			retry++;
			msleep_interruptible(10);
			goto read_again;
		}
	}

	return err;
}

/*
 * Write a value to a register in an TVP5146/47 decoder device.
 * Returns zero if successful, or non-zero otherwise.
 */
static int tvp514x_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int err;
	int retry = 0;
write_again:

	err = i2c_smbus_write_byte_data(client, reg, val);
	if (err) {
		if (retry <= I2C_RETRY_COUNT) {
			v4l_warn(client, "Write: retry ... %d\n", retry);
			retry++;
			msleep_interruptible(10);
			goto write_again;
		}
	}

	return err;
}

/*
 * tvp514x_write_regs : Initializes a list of TVP5146/47 registers
 *		if token is TOK_TERM, then entire write operation terminates
 *		if token is TOK_DELAY, then a delay of 'val' msec is introduced
 *		if token is TOK_SKIP, then the register write is skipped
 *		if token is TOK_WRITE, then the register write is performed
 *
 * reglist - list of registers to be written
 * Returns zero if successful, or non-zero otherwise.
 */
static int tvp514x_write_regs(struct i2c_client *client,
			      const struct tvp514x_reg reglist[])
{
	int err;
	const struct tvp514x_reg *next = reglist;

	for (; next->token != TOK_TERM; next++) {
		if (next->token == TOK_DELAY) {
			msleep(next->val);
			continue;
		}

		if (next->token == TOK_SKIP)
			continue;

		err = tvp514x_write_reg(client, next->reg, (u8) next->val);
		if (err) {
			v4l_err(client, "Write failed. Err[%d]\n", err);
			return err;
		}
	}
	return 0;
}

/*
 * tvp514x_get_current_std:
 * Returns the current standard detected by TVP5146/47
 */
static enum tvp514x_std tvp514x_get_current_std(struct tvp514x_decoder
						*decoder)
{
	u8 std, std_status;

	std = tvp514x_read_reg(decoder->client, REG_VIDEO_STD);
	if ((std & VIDEO_STD_MASK) == VIDEO_STD_AUTO_SWITCH_BIT) {
		/* use the standard status register */
		std_status = tvp514x_read_reg(decoder->client,
				REG_VIDEO_STD_STATUS);
	} else
		std_status = std;	/* use the standard register itself */

	switch (std_status & VIDEO_STD_MASK) {
	case VIDEO_STD_NTSC_MJ_BIT:
		return STD_NTSC_MJ;

	case VIDEO_STD_PAL_BDGHIN_BIT:
		return STD_PAL_BDGHIN;

	default:
		return STD_INVALID;
	}

	return STD_INVALID;
}

/*
 * TVP5146/47 register dump function
 */
static void tvp514x_reg_dump(struct tvp514x_decoder *decoder)
{
	u8 value;

	dump_reg(decoder->client, REG_INPUT_SEL, value);
	dump_reg(decoder->client, REG_AFE_GAIN_CTRL, value);
	dump_reg(decoder->client, REG_VIDEO_STD, value);
	dump_reg(decoder->client, REG_OPERATION_MODE, value);
	dump_reg(decoder->client, REG_COLOR_KILLER, value);
	dump_reg(decoder->client, REG_LUMA_CONTROL1, value);
	dump_reg(decoder->client, REG_LUMA_CONTROL2, value);
	dump_reg(decoder->client, REG_LUMA_CONTROL3, value);
	dump_reg(decoder->client, REG_BRIGHTNESS, value);
	dump_reg(decoder->client, REG_CONTRAST, value);
	dump_reg(decoder->client, REG_SATURATION, value);
	dump_reg(decoder->client, REG_HUE, value);
	dump_reg(decoder->client, REG_CHROMA_CONTROL1, value);
	dump_reg(decoder->client, REG_CHROMA_CONTROL2, value);
	dump_reg(decoder->client, REG_COMP_PR_SATURATION, value);
	dump_reg(decoder->client, REG_COMP_Y_CONTRAST, value);
	dump_reg(decoder->client, REG_COMP_PB_SATURATION, value);
	dump_reg(decoder->client, REG_COMP_Y_BRIGHTNESS, value);
	dump_reg(decoder->client, REG_AVID_START_PIXEL_LSB, value);
	dump_reg(decoder->client, REG_AVID_START_PIXEL_MSB, value);
	dump_reg(decoder->client, REG_AVID_STOP_PIXEL_LSB, value);
	dump_reg(decoder->client, REG_AVID_STOP_PIXEL_MSB, value);
	dump_reg(decoder->client, REG_HSYNC_START_PIXEL_LSB, value);
	dump_reg(decoder->client, REG_HSYNC_START_PIXEL_MSB, value);
	dump_reg(decoder->client, REG_HSYNC_STOP_PIXEL_LSB, value);
	dump_reg(decoder->client, REG_HSYNC_STOP_PIXEL_MSB, value);
	dump_reg(decoder->client, REG_VSYNC_START_LINE_LSB, value);
	dump_reg(decoder->client, REG_VSYNC_START_LINE_MSB, value);
	dump_reg(decoder->client, REG_VSYNC_STOP_LINE_LSB, value);
	dump_reg(decoder->client, REG_VSYNC_STOP_LINE_MSB, value);
	dump_reg(decoder->client, REG_VBLK_START_LINE_LSB, value);
	dump_reg(decoder->client, REG_VBLK_START_LINE_MSB, value);
	dump_reg(decoder->client, REG_VBLK_STOP_LINE_LSB, value);
	dump_reg(decoder->client, REG_VBLK_STOP_LINE_MSB, value);
	dump_reg(decoder->client, REG_SYNC_CONTROL, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER1, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER2, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER3, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER4, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER5, value);
	dump_reg(decoder->client, REG_OUTPUT_FORMATTER6, value);
	dump_reg(decoder->client, REG_CLEAR_LOST_LOCK, value);
}

/*
 * Configure the TVP5146/47 with the current register settings
 * Returns zero if successful, or non-zero otherwise.
 */
static int tvp514x_configure(struct tvp514x_decoder *decoder)
{
	int err;

	/* common register initialization */
	err =
	    tvp514x_write_regs(decoder->client, decoder->tvp514x_regs);
	if (err)
		return err;

	if (debug)
		tvp514x_reg_dump(decoder);

	return 0;
}

/*
 * Detect if an tvp514x is present, and if so which revision.
 * A device is considered to be detected if the chip ID (LSB and MSB)
 * registers match the expected values.
 * Any value of the rom version register is accepted.
 * Returns ENODEV error number if no device is detected, or zero
 * if a device is detected.
 */
static int tvp514x_detect(struct tvp514x_decoder *decoder)
{
	u8 chip_id_msb, chip_id_lsb, rom_ver;

	chip_id_msb = tvp514x_read_reg(decoder->client, REG_CHIP_ID_MSB);
	chip_id_lsb = tvp514x_read_reg(decoder->client, REG_CHIP_ID_LSB);
	rom_ver = tvp514x_read_reg(decoder->client, REG_ROM_VERSION);

	v4l_dbg(1, debug, decoder->client,
		 "chip id detected msb:0x%x lsb:0x%x rom version:0x%x\n",
		 chip_id_msb, chip_id_lsb, rom_ver);
	if ((chip_id_msb != TVP514X_CHIP_ID_MSB)
		|| ((chip_id_lsb != TVP5146_CHIP_ID_LSB)
		&& (chip_id_lsb != TVP5147_CHIP_ID_LSB))) {
		/* We didn't read the values we expected, so this must not be
		 * an TVP5146/47.
		 */
		v4l_err(decoder->client,
			"chip id mismatch msb:0x%x lsb:0x%x\n",
			chip_id_msb, chip_id_lsb);
		return -ENODEV;
	}

	decoder->ver = rom_ver;
	decoder->state = STATE_DETECTED;

	v4l_info(decoder->client,
			"%s found at 0x%x (%s)\n", decoder->client->name,
			decoder->client->addr << 1,
			decoder->client->adapter->name);
	return 0;
}

/*
 * Following are decoder interface functions implemented by
 * TVP5146/47 decoder driver.
 */

/**
 * ioctl_querystd - V4L2 decoder interface handler for VIDIOC_QUERYSTD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 std_id ioctl enum
 *
 * Returns the current standard detected by TVP5146/47. If no active input is
 * detected, returns -EINVAL
 */
static int ioctl_querystd(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct tvp514x_decoder *decoder = s->priv;
	enum tvp514x_std current_std;
	enum tvp514x_input input_sel;
	u8 sync_lock_status, lock_mask;

	if (std_id == NULL)
		return -EINVAL;

	/* get the current standard */
	current_std = tvp514x_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	input_sel = decoder->route.input;

	switch (input_sel) {
	case INPUT_CVBS_VI1A:
	case INPUT_CVBS_VI1B:
	case INPUT_CVBS_VI1C:
	case INPUT_CVBS_VI2A:
	case INPUT_CVBS_VI2B:
	case INPUT_CVBS_VI2C:
	case INPUT_CVBS_VI3A:
	case INPUT_CVBS_VI3B:
	case INPUT_CVBS_VI3C:
	case INPUT_CVBS_VI4A:
		lock_mask = STATUS_CLR_SUBCAR_LOCK_BIT |
			STATUS_HORZ_SYNC_LOCK_BIT |
			STATUS_VIRT_SYNC_LOCK_BIT;
		break;

	case INPUT_SVIDEO_VI2A_VI1A:
	case INPUT_SVIDEO_VI2B_VI1B:
	case INPUT_SVIDEO_VI2C_VI1C:
	case INPUT_SVIDEO_VI2A_VI3A:
	case INPUT_SVIDEO_VI2B_VI3B:
	case INPUT_SVIDEO_VI2C_VI3C:
	case INPUT_SVIDEO_VI4A_VI1A:
	case INPUT_SVIDEO_VI4A_VI1B:
	case INPUT_SVIDEO_VI4A_VI1C:
	case INPUT_SVIDEO_VI4A_VI3A:
	case INPUT_SVIDEO_VI4A_VI3B:
	case INPUT_SVIDEO_VI4A_VI3C:
		lock_mask = STATUS_HORZ_SYNC_LOCK_BIT |
			STATUS_VIRT_SYNC_LOCK_BIT;
		break;
		/*Need to add other interfaces*/
	default:
		return -EINVAL;
	}
	/* check whether signal is locked */
	sync_lock_status = tvp514x_read_reg(decoder->client, REG_STATUS1);
	if (lock_mask != (sync_lock_status & lock_mask))
		return -EINVAL;	/* No input detected */

	decoder->current_std = current_std;
	*std_id = decoder->std_list[current_std].standard.id;

	v4l_dbg(1, debug, decoder->client, "Current STD: %s",
			decoder->std_list[current_std].standard.name);
	return 0;
}

/**
 * ioctl_s_std - V4L2 decoder interface handler for VIDIOC_S_STD ioctl
 * @s: pointer to standard V4L2 device structure
 * @std_id: standard V4L2 v4l2_std_id ioctl enum
 *
 * If std_id is supported, sets the requested standard. Otherwise, returns
 * -EINVAL
 */
static int ioctl_s_std(struct v4l2_int_device *s, v4l2_std_id *std_id)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err, i;

	if (std_id == NULL)
		return -EINVAL;

	for (i = 0; i < decoder->num_stds; i++)
		if (*std_id & decoder->std_list[i].standard.id)
			break;

	if ((i == decoder->num_stds) || (i == STD_INVALID))
		return -EINVAL;

	err = tvp514x_write_reg(decoder->client, REG_VIDEO_STD,
				decoder->std_list[i].video_std);
	if (err)
		return err;

	decoder->current_std = i;
	decoder->tvp514x_regs[REG_VIDEO_STD].val =
		decoder->std_list[i].video_std;

	v4l_dbg(1, debug, decoder->client, "Standard set to: %s",
			decoder->std_list[i].standard.name);
	return 0;
}

/**
 * ioctl_s_routing - V4L2 decoder interface handler for VIDIOC_S_INPUT ioctl
 * @s: pointer to standard V4L2 device structure
 * @index: number of the input
 *
 * If index is valid, selects the requested input. Otherwise, returns -EINVAL if
 * the input is not supported or there is no active signal present in the
 * selected input.
 */
static int ioctl_s_routing(struct v4l2_int_device *s,
				struct v4l2_routing *route)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err;
	enum tvp514x_input input_sel;
	enum tvp514x_output output_sel;
	enum tvp514x_std current_std = STD_INVALID;
	u8 sync_lock_status, lock_mask;
	int try_count = LOCK_RETRY_COUNT;

	if ((!route) || (route->input >= INPUT_INVALID) ||
			(route->output >= OUTPUT_INVALID))
		return -EINVAL;	/* Index out of bound */

	input_sel = route->input;
	output_sel = route->output;

	err = tvp514x_write_reg(decoder->client, REG_INPUT_SEL, input_sel);
	if (err)
		return err;

	output_sel |= tvp514x_read_reg(decoder->client,
			REG_OUTPUT_FORMATTER1) & 0x7;
	err = tvp514x_write_reg(decoder->client, REG_OUTPUT_FORMATTER1,
			output_sel);
	if (err)
		return err;

	decoder->tvp514x_regs[REG_INPUT_SEL].val = input_sel;
	decoder->tvp514x_regs[REG_OUTPUT_FORMATTER1].val = output_sel;

	/* Clear status */
	msleep(LOCK_RETRY_DELAY);
	err =
	    tvp514x_write_reg(decoder->client, REG_CLEAR_LOST_LOCK, 0x01);
	if (err)
		return err;

	switch (input_sel) {
	case INPUT_CVBS_VI1A:
	case INPUT_CVBS_VI1B:
	case INPUT_CVBS_VI1C:
	case INPUT_CVBS_VI2A:
	case INPUT_CVBS_VI2B:
	case INPUT_CVBS_VI2C:
	case INPUT_CVBS_VI3A:
	case INPUT_CVBS_VI3B:
	case INPUT_CVBS_VI3C:
	case INPUT_CVBS_VI4A:
		lock_mask = STATUS_CLR_SUBCAR_LOCK_BIT |
			STATUS_HORZ_SYNC_LOCK_BIT |
			STATUS_VIRT_SYNC_LOCK_BIT;
		break;

	case INPUT_SVIDEO_VI2A_VI1A:
	case INPUT_SVIDEO_VI2B_VI1B:
	case INPUT_SVIDEO_VI2C_VI1C:
	case INPUT_SVIDEO_VI2A_VI3A:
	case INPUT_SVIDEO_VI2B_VI3B:
	case INPUT_SVIDEO_VI2C_VI3C:
	case INPUT_SVIDEO_VI4A_VI1A:
	case INPUT_SVIDEO_VI4A_VI1B:
	case INPUT_SVIDEO_VI4A_VI1C:
	case INPUT_SVIDEO_VI4A_VI3A:
	case INPUT_SVIDEO_VI4A_VI3B:
	case INPUT_SVIDEO_VI4A_VI3C:
		lock_mask = STATUS_HORZ_SYNC_LOCK_BIT |
			STATUS_VIRT_SYNC_LOCK_BIT;
		break;
	/*Need to add other interfaces*/
	default:
		return -EINVAL;
	}

	while (try_count-- > 0) {
		/* Allow decoder to sync up with new input */
		msleep(LOCK_RETRY_DELAY);

		/* get the current standard for future reference */
		current_std = tvp514x_get_current_std(decoder);
		if (current_std == STD_INVALID)
			continue;

		sync_lock_status = tvp514x_read_reg(decoder->client,
				REG_STATUS1);
		if (lock_mask == (sync_lock_status & lock_mask))
			break;	/* Input detected */
	}

	if ((current_std == STD_INVALID) || (try_count < 0))
		return -EINVAL;

	decoder->current_std = current_std;
	decoder->route.input = route->input;
	decoder->route.output = route->output;

	v4l_dbg(1, debug, decoder->client,
			"Input set to: %d, std : %d",
			input_sel, current_std);

	return 0;
}

/**
 * ioctl_queryctrl - V4L2 decoder interface handler for VIDIOC_QUERYCTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @qctrl: standard V4L2 v4l2_queryctrl structure
 *
 * If the requested control is supported, returns the control information.
 * Otherwise, returns -EINVAL if the control is not supported.
 */
static int
ioctl_queryctrl(struct v4l2_int_device *s, struct v4l2_queryctrl *qctrl)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err = -EINVAL;

	if (qctrl == NULL)
		return err;

	switch (qctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		/* Brightness supported is (0-255),
		 */
		err = v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 128);
		break;
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
		/* Saturation and Contrast supported is -
		 *	Contrast: 0 - 255 (Default - 128)
		 *	Saturation: 0 - 255 (Default - 128)
		 */
		err = v4l2_ctrl_query_fill(qctrl, 0, 255, 1, 128);
		break;
	case V4L2_CID_HUE:
		/* Hue Supported is -
		 *	Hue - -180 - +180 (Default - 0, Step - +180)
		 */
		err = v4l2_ctrl_query_fill(qctrl, -180, 180, 180, 0);
		break;
	case V4L2_CID_AUTOGAIN:
		/* Autogain is either 0 or 1*/
		memcpy(qctrl, &tvp514x_autogain_ctrl,
				sizeof(struct v4l2_queryctrl));
		err = 0;
		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", qctrl->id);
		return err;
	}

	v4l_dbg(1, debug, decoder->client,
			"Query Control: %s : Min - %d, Max - %d, Def - %d",
			qctrl->name,
			qctrl->minimum,
			qctrl->maximum,
			qctrl->default_value);

	return err;
}

/**
 * ioctl_g_ctrl - V4L2 decoder interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @ctrl: pointer to v4l2_control structure
 *
 * If the requested control is supported, returns the control's current
 * value from the decoder. Otherwise, returns -EINVAL if the control is not
 * supported.
 */
static int
ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *ctrl)
{
	struct tvp514x_decoder *decoder = s->priv;

	if (ctrl == NULL)
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = decoder->tvp514x_regs[REG_BRIGHTNESS].val;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = decoder->tvp514x_regs[REG_CONTRAST].val;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = decoder->tvp514x_regs[REG_SATURATION].val;
		break;
	case V4L2_CID_HUE:
		ctrl->value = decoder->tvp514x_regs[REG_HUE].val;
		if (ctrl->value == 0x7F)
			ctrl->value = 180;
		else if (ctrl->value == 0x80)
			ctrl->value = -180;
		else
			ctrl->value = 0;

		break;
	case V4L2_CID_AUTOGAIN:
		ctrl->value = decoder->tvp514x_regs[REG_AFE_GAIN_CTRL].val;
		if ((ctrl->value & 0x3) == 3)
			ctrl->value = 1;
		else
			ctrl->value = 0;

		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", ctrl->id);
		return -EINVAL;
	}

	v4l_dbg(1, debug, decoder->client,
			"Get Control: ID - %d - %d",
			ctrl->id, ctrl->value);
	return 0;
}

/**
 * ioctl_s_ctrl - V4L2 decoder interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @ctrl: pointer to v4l2_control structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW. Otherwise, returns -EINVAL if the control is not supported.
 */
static int
ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *ctrl)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err = -EINVAL, value;

	if (ctrl == NULL)
		return err;

	value = (__s32) ctrl->value;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid brightness setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = tvp514x_write_reg(decoder->client, REG_BRIGHTNESS,
				value);
		if (err)
			return err;
		decoder->tvp514x_regs[REG_BRIGHTNESS].val = value;
		break;
	case V4L2_CID_CONTRAST:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid contrast setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = tvp514x_write_reg(decoder->client, REG_CONTRAST,
				value);
		if (err)
			return err;
		decoder->tvp514x_regs[REG_CONTRAST].val = value;
		break;
	case V4L2_CID_SATURATION:
		if (ctrl->value < 0 || ctrl->value > 255) {
			v4l_err(decoder->client,
					"invalid saturation setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = tvp514x_write_reg(decoder->client, REG_SATURATION,
				value);
		if (err)
			return err;
		decoder->tvp514x_regs[REG_SATURATION].val = value;
		break;
	case V4L2_CID_HUE:
		if (value == 180)
			value = 0x7F;
		else if (value == -180)
			value = 0x80;
		else if (value == 0)
			value = 0;
		else {
			v4l_err(decoder->client,
					"invalid hue setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = tvp514x_write_reg(decoder->client, REG_HUE,
				value);
		if (err)
			return err;
		decoder->tvp514x_regs[REG_HUE].val = value;
		break;
	case V4L2_CID_AUTOGAIN:
		if (value == 1)
			value = 0x0F;
		else if (value == 0)
			value = 0x0C;
		else {
			v4l_err(decoder->client,
					"invalid auto gain setting %d\n",
					ctrl->value);
			return -ERANGE;
		}
		err = tvp514x_write_reg(decoder->client, REG_AFE_GAIN_CTRL,
				value);
		if (err)
			return err;
		decoder->tvp514x_regs[REG_AFE_GAIN_CTRL].val = value;
		break;
	default:
		v4l_err(decoder->client,
			"invalid control id %d\n", ctrl->id);
		return err;
	}

	v4l_dbg(1, debug, decoder->client,
			"Set Control: ID - %d - %d",
			ctrl->id, ctrl->value);

	return err;
}

/**
 * ioctl_enum_fmt_cap - Implement the CAPTURE buffer VIDIOC_ENUM_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @fmt: standard V4L2 VIDIOC_ENUM_FMT ioctl structure
 *
 * Implement the VIDIOC_ENUM_FMT ioctl to enumerate supported formats
 */
static int
ioctl_enum_fmt_cap(struct v4l2_int_device *s, struct v4l2_fmtdesc *fmt)
{
	struct tvp514x_decoder *decoder = s->priv;
	int index;

	if (fmt == NULL)
		return -EINVAL;

	index = fmt->index;
	if ((index >= decoder->num_fmts) || (index < 0))
		return -EINVAL;	/* Index out of bound */

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memcpy(fmt, &decoder->fmt_list[index],
		sizeof(struct v4l2_fmtdesc));

	v4l_dbg(1, debug, decoder->client,
			"Current FMT: index - %d (%s)",
			decoder->fmt_list[index].index,
			decoder->fmt_list[index].description);
	return 0;
}

/**
 * ioctl_try_fmt_cap - Implement the CAPTURE buffer VIDIOC_TRY_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_TRY_FMT ioctl structure
 *
 * Implement the VIDIOC_TRY_FMT ioctl for the CAPTURE buffer type. This
 * ioctl is used to negotiate the image capture size and pixel format
 * without actually making it take effect.
 */
static int
ioctl_try_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp514x_decoder *decoder = s->priv;
	int ifmt;
	struct v4l2_pix_format *pix;
	enum tvp514x_std current_std;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	pix = &f->fmt.pix;

	/* Calculate height and width based on current standard */
	current_std = tvp514x_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;
	pix->width = decoder->std_list[current_std].width;
	pix->height = decoder->std_list[current_std].height;

	for (ifmt = 0; ifmt < decoder->num_fmts; ifmt++) {
		if (pix->pixelformat ==
			decoder->fmt_list[ifmt].pixelformat)
			break;
	}
	if (ifmt == decoder->num_fmts)
		ifmt = 0;	/* None of the format matched, select default */
	pix->pixelformat = decoder->fmt_list[ifmt].pixelformat;

	pix->field = V4L2_FIELD_INTERLACED;
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->priv = 0;

	v4l_dbg(1, debug, decoder->client,
			"Try FMT: pixelformat - %s, bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->fmt_list[ifmt].description, pix->bytesperline,
			pix->width, pix->height);
	return 0;
}

/**
 * ioctl_s_fmt_cap - V4L2 decoder interface handler for VIDIOC_S_FMT ioctl
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 VIDIOC_S_FMT ioctl structure
 *
 * If the requested format is supported, configures the HW to use that
 * format, returns error code if format not supported or HW can't be
 * correctly configured.
 */
static int
ioctl_s_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp514x_decoder *decoder = s->priv;
	struct v4l2_pix_format *pix;
	int rval;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	pix = &f->fmt.pix;
	rval = ioctl_try_fmt_cap(s, f);
	if (rval)
		return rval;

		decoder->pix = *pix;

	return rval;
}

/**
 * ioctl_g_fmt_cap - V4L2 decoder interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the decoder's current pixel format in the v4l2_format
 * parameter.
 */
static int
ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tvp514x_decoder *decoder = s->priv;

	if (f == NULL)
		return -EINVAL;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	f->fmt.pix = decoder->pix;

	v4l_dbg(1, debug, decoder->client,
			"Current FMT: bytesperline - %d"
			"Width - %d, Height - %d",
			decoder->pix.bytesperline,
			decoder->pix.width, decoder->pix.height);
	return 0;
}

/**
 * ioctl_g_parm - V4L2 decoder interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the decoder's video CAPTURE parameters.
 */
static int
ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tvp514x_decoder *decoder = s->priv;
	struct v4l2_captureparm *cparm;
	enum tvp514x_std current_std;

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* get the current standard */
	current_std = tvp514x_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	cparm = &a->parm.capture;
	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe =
		decoder->std_list[current_std].standard.frameperiod;

	return 0;
}

/**
 * ioctl_s_parm - V4L2 decoder interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the decoder to use the input parameters, if possible. If
 * not possible, returns the appropriate error code.
 */
static int
ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tvp514x_decoder *decoder = s->priv;
	struct v4l2_fract *timeperframe;
	enum tvp514x_std current_std;

	if (a == NULL)
		return -EINVAL;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;	/* only capture is supported */

	timeperframe = &a->parm.capture.timeperframe;

	/* get the current standard */
	current_std = tvp514x_get_current_std(decoder);
	if (current_std == STD_INVALID)
		return -EINVAL;

	decoder->current_std = current_std;

	*timeperframe =
	    decoder->std_list[current_std].standard.frameperiod;

	return 0;
}

/**
 * ioctl_g_ifparm - V4L2 decoder interface handler for vidioc_int_g_ifparm_num
 * @s: pointer to standard V4L2 device structure
 * @p: pointer to standard V4L2 vidioc_int_g_ifparm_num ioctl structure
 *
 * Gets slave interface parameters.
 * Calculates the required xclk value to support the requested
 * clock parameters in p. This value is returned in the p
 * parameter.
 */
static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct tvp514x_decoder *decoder = s->priv;
	int rval;

	if (p == NULL)
		return -EINVAL;

	if (NULL == decoder->pdata->ifparm)
		return -EINVAL;

	rval = decoder->pdata->ifparm(p);
	if (rval) {
		v4l_err(decoder->client, "g_ifparm.Err[%d]\n", rval);
		return rval;
	}

	p->u.bt656.clock_curr = TVP514X_XCLK_BT656;

	return 0;
}

/**
 * ioctl_g_priv - V4L2 decoder interface handler for vidioc_int_g_priv_num
 * @s: pointer to standard V4L2 device structure
 * @p: void pointer to hold decoder's private data address
 *
 * Returns device's (decoder's) private data area address in p parameter
 */
static int ioctl_g_priv(struct v4l2_int_device *s, void *p)
{
	struct tvp514x_decoder *decoder = s->priv;

	if (NULL == decoder->pdata->priv_data_set)
		return -EINVAL;

	return decoder->pdata->priv_data_set(p);
}

/**
 * ioctl_s_power - V4L2 decoder interface handler for vidioc_int_s_power_num
 * @s: pointer to standard V4L2 device structure
 * @on: power state to which device is to be set
 *
 * Sets devices power state to requrested state, if possible.
 */
static int ioctl_s_power(struct v4l2_int_device *s, enum v4l2_power on)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err = 0;

	switch (on) {
	case V4L2_POWER_OFF:
		/* Power Down Sequence */
		err =
		    tvp514x_write_reg(decoder->client, REG_OPERATION_MODE,
					0x01);
		/* Disable mux for TVP5146/47 decoder data path */
		if (decoder->pdata->power_set)
			err |= decoder->pdata->power_set(on);
		decoder->state = STATE_NOT_DETECTED;
		break;

	case V4L2_POWER_STANDBY:
		if (decoder->pdata->power_set)
			err = decoder->pdata->power_set(on);
		break;

	case V4L2_POWER_ON:
		/* Enable mux for TVP5146/47 decoder data path */
		if ((decoder->pdata->power_set) &&
				(decoder->state == STATE_NOT_DETECTED)) {
			int i;
			struct tvp514x_init_seq *int_seq =
				(struct tvp514x_init_seq *)
				decoder->id->driver_data;

			err = decoder->pdata->power_set(on);

			/* Power Up Sequence */
			for (i = 0; i < int_seq->no_regs; i++) {
				err |= tvp514x_write_reg(decoder->client,
						int_seq->init_reg_seq[i].reg,
						int_seq->init_reg_seq[i].val);
			}
			/* Detect the sensor is not already detected */
			err |= tvp514x_detect(decoder);
			if (err) {
				v4l_err(decoder->client,
						"Unable to detect decoder\n");
				return err;
			}
		}
		err |= tvp514x_configure(decoder);
		break;

	default:
		err = -ENODEV;
		break;
	}

	return err;
}

/**
 * ioctl_init - V4L2 decoder interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 *
 * Initialize the decoder device (calls tvp514x_configure())
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	struct tvp514x_decoder *decoder = s->priv;

	/* Set default standard to auto */
	decoder->tvp514x_regs[REG_VIDEO_STD].val =
	    VIDEO_STD_AUTO_SWITCH_BIT;

	return tvp514x_configure(decoder);
}

/**
 * ioctl_dev_exit - V4L2 decoder interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the dev. at slave detach. The complement of ioctl_dev_init.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	return 0;
}

/**
 * ioctl_dev_init - V4L2 decoder interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master. Returns 0 if
 * TVP5146/47 device could be found, otherwise returns appropriate error.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct tvp514x_decoder *decoder = s->priv;
	int err;

	err = tvp514x_detect(decoder);
	if (err < 0) {
		v4l_err(decoder->client,
			"Unable to detect decoder\n");
		return err;
	}

	v4l_info(decoder->client,
		 "chip version 0x%.2x detected\n", decoder->ver);

	return 0;
}

static struct v4l2_int_ioctl_desc tvp514x_ioctl_desc[] = {
	{vidioc_int_dev_init_num, (v4l2_int_ioctl_func*) ioctl_dev_init},
	{vidioc_int_dev_exit_num, (v4l2_int_ioctl_func*) ioctl_dev_exit},
	{vidioc_int_s_power_num, (v4l2_int_ioctl_func*) ioctl_s_power},
	{vidioc_int_g_priv_num, (v4l2_int_ioctl_func*) ioctl_g_priv},
	{vidioc_int_g_ifparm_num, (v4l2_int_ioctl_func*) ioctl_g_ifparm},
	{vidioc_int_init_num, (v4l2_int_ioctl_func*) ioctl_init},
	{vidioc_int_enum_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_enum_fmt_cap},
	{vidioc_int_try_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_try_fmt_cap},
	{vidioc_int_g_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_g_fmt_cap},
	{vidioc_int_s_fmt_cap_num,
	 (v4l2_int_ioctl_func *) ioctl_s_fmt_cap},
	{vidioc_int_g_parm_num, (v4l2_int_ioctl_func *) ioctl_g_parm},
	{vidioc_int_s_parm_num, (v4l2_int_ioctl_func *) ioctl_s_parm},
	{vidioc_int_queryctrl_num,
	 (v4l2_int_ioctl_func *) ioctl_queryctrl},
	{vidioc_int_g_ctrl_num, (v4l2_int_ioctl_func *) ioctl_g_ctrl},
	{vidioc_int_s_ctrl_num, (v4l2_int_ioctl_func *) ioctl_s_ctrl},
	{vidioc_int_querystd_num, (v4l2_int_ioctl_func *) ioctl_querystd},
	{vidioc_int_s_std_num, (v4l2_int_ioctl_func *) ioctl_s_std},
	{vidioc_int_s_video_routing_num,
		(v4l2_int_ioctl_func *) ioctl_s_routing},
};

static struct tvp514x_decoder tvp514x_dev = {
	.state = STATE_NOT_DETECTED,

	.fmt_list = tvp514x_fmt_list,
	.num_fmts = ARRAY_SIZE(tvp514x_fmt_list),

	.pix = {		/* Default to NTSC 8-bit YUV 422 */
		.width = NTSC_NUM_ACTIVE_PIXELS,
		.height = NTSC_NUM_ACTIVE_LINES,
		.pixelformat = V4L2_PIX_FMT_UYVY,
		.field = V4L2_FIELD_INTERLACED,
		.bytesperline = NTSC_NUM_ACTIVE_PIXELS * 2,
		.sizeimage =
		NTSC_NUM_ACTIVE_PIXELS * 2 * NTSC_NUM_ACTIVE_LINES,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		},

	.current_std = STD_NTSC_MJ,
	.std_list = tvp514x_std_list,
	.num_stds = ARRAY_SIZE(tvp514x_std_list),
	.v4l2_int_device = {
		.module = THIS_MODULE,
		.name = TVP514X_MODULE_NAME,
		.type = v4l2_int_type_slave,
	},
	.tvp514x_slave = {
		.ioctls = tvp514x_ioctl_desc,
		.num_ioctls = ARRAY_SIZE(tvp514x_ioctl_desc),
	},
};

/**
 * tvp514x_probe - decoder driver i2c probe handler
 * @client: i2c driver client device structure
 *
 * Register decoder as an i2c client device and V4L2
 * device.
 */
static int
tvp514x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct tvp514x_decoder *decoder;
	int err;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	decoder = kzalloc(sizeof(*decoder), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;

	if (!client->dev.platform_data) {
		v4l_err(client, "No platform data!!\n");
		err = -ENODEV;
		goto out_free;
	}

	*decoder = tvp514x_dev;
	decoder->v4l2_int_device.priv = decoder;
	decoder->pdata = client->dev.platform_data;
	decoder->v4l2_int_device.u.slave = &decoder->tvp514x_slave;
	memcpy(decoder->tvp514x_regs, tvp514x_reg_list_default,
			sizeof(tvp514x_reg_list_default));
	/*
	 * Fetch platform specific data, and configure the
	 * tvp514x_reg_list[] accordingly. Since this is one
	 * time configuration, no need to preserve.
	 */
	decoder->tvp514x_regs[REG_OUTPUT_FORMATTER2].val |=
			(decoder->pdata->clk_polarity << 1);
	decoder->tvp514x_regs[REG_SYNC_CONTROL].val |=
			((decoder->pdata->hs_polarity << 2) |
			(decoder->pdata->vs_polarity << 3));
	/*
	 * Save the id data, required for power up sequence
	 */
	decoder->id = (struct i2c_device_id *)id;
	/* Attach to Master */
	strcpy(decoder->v4l2_int_device.u.slave->attach_to,
			decoder->pdata->master);
	decoder->client = client;
	i2c_set_clientdata(client, decoder);

	/* Register with V4L2 layer as slave device */
	err = v4l2_int_device_register(&decoder->v4l2_int_device);
	if (err) {
		i2c_set_clientdata(client, NULL);
		v4l_err(client,
			"Unable to register to v4l2. Err[%d]\n", err);
		goto out_free;

	} else
		v4l_info(client, "Registered to v4l2 master %s!!\n",
				decoder->pdata->master);
	return 0;

out_free:
	kfree(decoder);
	return err;
}

/**
 * tvp514x_remove - decoder driver i2c remove handler
 * @client: i2c driver client device structure
 *
 * Unregister decoder as an i2c client device and V4L2
 * device. Complement of tvp514x_probe().
 */
static int __exit tvp514x_remove(struct i2c_client *client)
{
	struct tvp514x_decoder *decoder = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(&decoder->v4l2_int_device);
	i2c_set_clientdata(client, NULL);
	kfree(decoder);
	return 0;
}
/*
 * TVP5146 Init/Power on Sequence
 */
static const struct tvp514x_reg tvp5146_init_reg_seq[] = {
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x02},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0x80},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x01},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x60},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0xB0},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x01},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x00},
	{TOK_WRITE, REG_OPERATION_MODE, 0x01},
	{TOK_WRITE, REG_OPERATION_MODE, 0x00},
};
static const struct tvp514x_init_seq tvp5146_init = {
	.no_regs = ARRAY_SIZE(tvp5146_init_reg_seq),
	.init_reg_seq = tvp5146_init_reg_seq,
};
/*
 * TVP5147 Init/Power on Sequence
 */
static const struct tvp514x_reg tvp5147_init_reg_seq[] =	{
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x02},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0x80},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x01},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x60},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0xB0},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x01},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x16},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0xA0},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x16},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS1, 0x60},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS2, 0x00},
	{TOK_WRITE, REG_VBUS_ADDRESS_ACCESS3, 0xB0},
	{TOK_WRITE, REG_VBUS_DATA_ACCESS_NO_VBUS_ADDR_INCR, 0x00},
	{TOK_WRITE, REG_OPERATION_MODE, 0x01},
	{TOK_WRITE, REG_OPERATION_MODE, 0x00},
};
static const struct tvp514x_init_seq tvp5147_init = {
	.no_regs = ARRAY_SIZE(tvp5147_init_reg_seq),
	.init_reg_seq = tvp5147_init_reg_seq,
};
/*
 * TVP5146M2/TVP5147M1 Init/Power on Sequence
 */
static const struct tvp514x_reg tvp514xm_init_reg_seq[] = {
	{TOK_WRITE, REG_OPERATION_MODE, 0x01},
	{TOK_WRITE, REG_OPERATION_MODE, 0x00},
};
static const struct tvp514x_init_seq tvp514xm_init = {
	.no_regs = ARRAY_SIZE(tvp514xm_init_reg_seq),
	.init_reg_seq = tvp514xm_init_reg_seq,
};
/*
 * I2C Device Table -
 *
 * name - Name of the actual device/chip.
 * driver_data - Driver data
 */
static const struct i2c_device_id tvp514x_id[] = {
	{"tvp5146", (unsigned long)&tvp5146_init},
	{"tvp5146m2", (unsigned long)&tvp514xm_init},
	{"tvp5147", (unsigned long)&tvp5147_init},
	{"tvp5147m1", (unsigned long)&tvp514xm_init},
	{},
};

MODULE_DEVICE_TABLE(i2c, tvp514x_id);

static struct i2c_driver tvp514x_i2c_driver = {
	.driver = {
		   .name = TVP514X_MODULE_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = tvp514x_probe,
	.remove = __exit_p(tvp514x_remove),
	.id_table = tvp514x_id,
};

/**
 * tvp514x_init
 *
 * Module init function
 */
static int __init tvp514x_init(void)
{
	return i2c_add_driver(&tvp514x_i2c_driver);
}

/**
 * tvp514x_cleanup
 *
 * Module exit function
 */
static void __exit tvp514x_cleanup(void)
{
	i2c_del_driver(&tvp514x_i2c_driver);
}

module_init(tvp514x_init);
module_exit(tvp514x_cleanup);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("TVP514X linux decoder driver");
MODULE_LICENSE("GPL");
