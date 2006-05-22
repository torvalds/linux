/*
 * saa7127 - Philips SAA7127/SAA7129 video encoder driver
 *
 * Copyright (C) 2003 Roy Bulter <rbulter@hetnet.nl>
 *
 * Based on SAA7126 video encoder driver by Gillem & Andreas Oberritter
 *
 * Copyright (C) 2000-2001 Gillem <htoa@gmx.net>
 * Copyright (C) 2002 Andreas Oberritter <obi@saftware.de>
 *
 * Based on Stadis 4:2:2 MPEG-2 Decoder Driver by Nathan Laredo
 *
 * Copyright (C) 1999 Nathan Laredo <laredo@gnu.org>
 *
 * This driver is designed for the Hauppauge 250/350 Linux driver
 * from the ivtv Project
 *
 * Copyright (C) 2003 Kevin Thayer <nufan_wfk@yahoo.com>
 *
 * Dual output support:
 * Copyright (C) 2004 Eric Varsanyi
 *
 * NTSC Tuning and 7.5 IRE Setup
 * Copyright (C) 2004  Chris Kennedy <c@groovy.org>
 *
 * VBI additions & cleanup:
 * Copyright (C) 2004, 2005 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * Note: the saa7126 is identical to the saa7127, and the saa7128 is
 * identical to the saa7129, except that the saa7126 and saa7128 have
 * macrovision anti-taping support. This driver will almost certainly
 * work find for those chips, except of course for the missing anti-taping
 * support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/saa7127.h>

static int debug = 0;
static int test_image = 0;

MODULE_DESCRIPTION("Philips SAA7127/9 video encoder driver");
MODULE_AUTHOR("Kevin Thayer, Chris Kennedy, Hans Verkuil");
MODULE_LICENSE("GPL");
module_param(debug, int, 0644);
module_param(test_image, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");
MODULE_PARM_DESC(test_image, "test_image (0-1)");

static unsigned short normal_i2c[] = { 0x88 >> 1, I2C_CLIENT_END };


I2C_CLIENT_INSMOD;

/*
 * SAA7127 registers
 */

#define SAA7127_REG_STATUS                           0x00
#define SAA7127_REG_WIDESCREEN_CONFIG                0x26
#define SAA7127_REG_WIDESCREEN_ENABLE                0x27
#define SAA7127_REG_BURST_START                      0x28
#define SAA7127_REG_BURST_END                        0x29
#define SAA7127_REG_COPYGEN_0                        0x2a
#define SAA7127_REG_COPYGEN_1                        0x2b
#define SAA7127_REG_COPYGEN_2                        0x2c
#define SAA7127_REG_OUTPUT_PORT_CONTROL              0x2d
#define SAA7127_REG_GAIN_LUMINANCE_RGB               0x38
#define SAA7127_REG_GAIN_COLORDIFF_RGB               0x39
#define SAA7127_REG_INPUT_PORT_CONTROL_1             0x3a
#define SAA7129_REG_FADE_KEY_COL2		     0x4f
#define SAA7127_REG_CHROMA_PHASE                     0x5a
#define SAA7127_REG_GAINU                            0x5b
#define SAA7127_REG_GAINV                            0x5c
#define SAA7127_REG_BLACK_LEVEL                      0x5d
#define SAA7127_REG_BLANKING_LEVEL                   0x5e
#define SAA7127_REG_VBI_BLANKING                     0x5f
#define SAA7127_REG_DAC_CONTROL                      0x61
#define SAA7127_REG_BURST_AMP                        0x62
#define SAA7127_REG_SUBC3                            0x63
#define SAA7127_REG_SUBC2                            0x64
#define SAA7127_REG_SUBC1                            0x65
#define SAA7127_REG_SUBC0                            0x66
#define SAA7127_REG_LINE_21_ODD_0                    0x67
#define SAA7127_REG_LINE_21_ODD_1                    0x68
#define SAA7127_REG_LINE_21_EVEN_0                   0x69
#define SAA7127_REG_LINE_21_EVEN_1                   0x6a
#define SAA7127_REG_RCV_PORT_CONTROL                 0x6b
#define SAA7127_REG_VTRIG                            0x6c
#define SAA7127_REG_HTRIG_HI                         0x6d
#define SAA7127_REG_MULTI                            0x6e
#define SAA7127_REG_CLOSED_CAPTION                   0x6f
#define SAA7127_REG_RCV2_OUTPUT_START                0x70
#define SAA7127_REG_RCV2_OUTPUT_END                  0x71
#define SAA7127_REG_RCV2_OUTPUT_MSBS                 0x72
#define SAA7127_REG_TTX_REQUEST_H_START              0x73
#define SAA7127_REG_TTX_REQUEST_H_DELAY_LENGTH       0x74
#define SAA7127_REG_CSYNC_ADVANCE_VSYNC_SHIFT        0x75
#define SAA7127_REG_TTX_ODD_REQ_VERT_START           0x76
#define SAA7127_REG_TTX_ODD_REQ_VERT_END             0x77
#define SAA7127_REG_TTX_EVEN_REQ_VERT_START          0x78
#define SAA7127_REG_TTX_EVEN_REQ_VERT_END            0x79
#define SAA7127_REG_FIRST_ACTIVE                     0x7a
#define SAA7127_REG_LAST_ACTIVE                      0x7b
#define SAA7127_REG_MSB_VERTICAL                     0x7c
#define SAA7127_REG_DISABLE_TTX_LINE_LO_0            0x7e
#define SAA7127_REG_DISABLE_TTX_LINE_LO_1            0x7f

/*
 **********************************************************************
 *
 *  Arrays with configuration parameters for the SAA7127
 *
 **********************************************************************
 */

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

static const struct i2c_reg_value saa7129_init_config_extra[] = {
	{ SAA7127_REG_OUTPUT_PORT_CONTROL, 		0x38 },
	{ SAA7127_REG_VTRIG, 				0xfa },
	{ 0, 0 }
};

static const struct i2c_reg_value saa7127_init_config_common[] = {
	{ SAA7127_REG_WIDESCREEN_CONFIG, 		0x0d },
	{ SAA7127_REG_WIDESCREEN_ENABLE, 		0x00 },
	{ SAA7127_REG_COPYGEN_0, 			0x77 },
	{ SAA7127_REG_COPYGEN_1, 			0x41 },
	{ SAA7127_REG_COPYGEN_2, 			0x00 },	/* Macrovision enable/disable */
	{ SAA7127_REG_OUTPUT_PORT_CONTROL, 		0x9e },
	{ SAA7127_REG_GAIN_LUMINANCE_RGB, 		0x00 },
	{ SAA7127_REG_GAIN_COLORDIFF_RGB, 		0x00 },
	{ SAA7127_REG_INPUT_PORT_CONTROL_1, 		0x80 },	/* for color bars */
	{ SAA7127_REG_LINE_21_ODD_0, 			0x77 },
	{ SAA7127_REG_LINE_21_ODD_1, 			0x41 },
	{ SAA7127_REG_LINE_21_EVEN_0, 			0x88 },
	{ SAA7127_REG_LINE_21_EVEN_1, 			0x41 },
	{ SAA7127_REG_RCV_PORT_CONTROL, 		0x12 },
	{ SAA7127_REG_VTRIG, 				0xf9 },
	{ SAA7127_REG_HTRIG_HI, 			0x00 },
	{ SAA7127_REG_RCV2_OUTPUT_START, 		0x41 },
	{ SAA7127_REG_RCV2_OUTPUT_END, 			0xc3 },
	{ SAA7127_REG_RCV2_OUTPUT_MSBS, 		0x00 },
	{ SAA7127_REG_TTX_REQUEST_H_START, 		0x3e },
	{ SAA7127_REG_TTX_REQUEST_H_DELAY_LENGTH, 	0xb8 },
	{ SAA7127_REG_CSYNC_ADVANCE_VSYNC_SHIFT,  	0x03 },
	{ SAA7127_REG_TTX_ODD_REQ_VERT_START, 		0x15 },
	{ SAA7127_REG_TTX_ODD_REQ_VERT_END, 		0x16 },
	{ SAA7127_REG_TTX_EVEN_REQ_VERT_START, 		0x15 },
	{ SAA7127_REG_TTX_EVEN_REQ_VERT_END, 		0x16 },
	{ SAA7127_REG_FIRST_ACTIVE, 			0x1a },
	{ SAA7127_REG_LAST_ACTIVE, 			0x01 },
	{ SAA7127_REG_MSB_VERTICAL, 			0xc0 },
	{ SAA7127_REG_DISABLE_TTX_LINE_LO_0, 		0x00 },
	{ SAA7127_REG_DISABLE_TTX_LINE_LO_1, 		0x00 },
	{ 0, 0 }
};

#define SAA7127_60HZ_DAC_CONTROL 0x15
static const struct i2c_reg_value saa7127_init_config_60hz[] = {
	{ SAA7127_REG_BURST_START, 			0x19 },
	/* BURST_END is also used as a chip ID in saa7127_detect_client */
	{ SAA7127_REG_BURST_END, 			0x1d },
	{ SAA7127_REG_CHROMA_PHASE, 			0xa3 },
	{ SAA7127_REG_GAINU, 				0x98 },
	{ SAA7127_REG_GAINV, 				0xd3 },
	{ SAA7127_REG_BLACK_LEVEL, 			0x39 },
	{ SAA7127_REG_BLANKING_LEVEL, 			0x2e },
	{ SAA7127_REG_VBI_BLANKING, 			0x2e },
	{ SAA7127_REG_DAC_CONTROL, 			0x15 },
	{ SAA7127_REG_BURST_AMP, 			0x4d },
	{ SAA7127_REG_SUBC3, 				0x1f },
	{ SAA7127_REG_SUBC2, 				0x7c },
	{ SAA7127_REG_SUBC1, 				0xf0 },
	{ SAA7127_REG_SUBC0, 				0x21 },
	{ SAA7127_REG_MULTI, 				0x90 },
	{ SAA7127_REG_CLOSED_CAPTION, 			0x11 },
	{ 0, 0 }
};

#define SAA7127_50HZ_DAC_CONTROL 0x02
static struct i2c_reg_value saa7127_init_config_50hz[] = {
	{ SAA7127_REG_BURST_START, 			0x21 },
	/* BURST_END is also used as a chip ID in saa7127_detect_client */
	{ SAA7127_REG_BURST_END, 			0x1d },
	{ SAA7127_REG_CHROMA_PHASE, 			0x3f },
	{ SAA7127_REG_GAINU, 				0x7d },
	{ SAA7127_REG_GAINV, 				0xaf },
	{ SAA7127_REG_BLACK_LEVEL, 			0x33 },
	{ SAA7127_REG_BLANKING_LEVEL, 			0x35 },
	{ SAA7127_REG_VBI_BLANKING, 			0x35 },
	{ SAA7127_REG_DAC_CONTROL, 			0x02 },
	{ SAA7127_REG_BURST_AMP, 			0x2f },
	{ SAA7127_REG_SUBC3, 				0xcb },
	{ SAA7127_REG_SUBC2, 				0x8a },
	{ SAA7127_REG_SUBC1, 				0x09 },
	{ SAA7127_REG_SUBC0, 				0x2a },
	{ SAA7127_REG_MULTI, 				0xa0 },
	{ SAA7127_REG_CLOSED_CAPTION, 			0x00 },
	{ 0, 0 }
};

/*
 **********************************************************************
 *
 *  Encoder Struct, holds the configuration state of the encoder
 *
 **********************************************************************
 */

struct saa7127_state {
	v4l2_std_id std;
	enum v4l2_chip_ident ident;
	enum saa7127_input_type input_type;
	enum saa7127_output_type output_type;
	int video_enable;
	int wss_enable;
	u16 wss_mode;
	int cc_enable;
	u16 cc_data;
	int xds_enable;
	u16 xds_data;
	int vps_enable;
	u8 vps_data[5];
	u8 reg_2d;
	u8 reg_3a;
	u8 reg_3a_cb;   /* colorbar bit */
	u8 reg_61;
};

static const char * const output_strs[] =
{
	"S-Video + Composite",
	"Composite",
	"S-Video",
	"RGB",
	"YUV C",
	"YUV V"
};

static const char * const wss_strs[] = {
	"invalid",
	"letterbox 14:9 center",
	"letterbox 14:9 top",
	"invalid",
	"letterbox 16:9 top",
	"invalid",
	"invalid",
	"16:9 full format anamorphic"
	"4:3 full format",
	"invalid",
	"invalid",
	"letterbox 16:9 center",
	"invalid",
	"letterbox >16:9 center",
	"14:9 full format center",
	"invalid",
};

/* ----------------------------------------------------------------------- */

static int saa7127_read(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

static int saa7127_write(struct i2c_client *client, u8 reg, u8 val)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (i2c_smbus_write_byte_data(client, reg, val) == 0)
			return 0;
	}
	v4l_err(client, "I2C Write Problem\n");
	return -1;
}

/* ----------------------------------------------------------------------- */

static int saa7127_write_inittab(struct i2c_client *client,
				 const struct i2c_reg_value *regs)
{
	while (regs->reg != 0) {
		saa7127_write(client, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_vps(struct i2c_client *client, struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 16))
		return -EINVAL;
	if (state->vps_enable != enable) {
		v4l_dbg(1, debug, client, "Turn VPS Signal %s\n", enable ? "on" : "off");
		saa7127_write(client, 0x54, enable << 7);
		state->vps_enable = enable;
	}
	if (!enable)
		return 0;

	state->vps_data[0] = data->data[4];
	state->vps_data[1] = data->data[10];
	state->vps_data[2] = data->data[11];
	state->vps_data[3] = data->data[12];
	state->vps_data[4] = data->data[13];
	v4l_dbg(1, debug, client, "Set VPS data %02x %02x %02x %02x %02x\n",
		state->vps_data[0], state->vps_data[1],
		state->vps_data[2], state->vps_data[3],
		state->vps_data[4]);
	saa7127_write(client, 0x55, state->vps_data[0]);
	saa7127_write(client, 0x56, state->vps_data[1]);
	saa7127_write(client, 0x57, state->vps_data[2]);
	saa7127_write(client, 0x58, state->vps_data[3]);
	saa7127_write(client, 0x59, state->vps_data[4]);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_cc(struct i2c_client *client, struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	u16 cc = data->data[1] << 8 | data->data[0];
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 21))
		return -EINVAL;
	if (state->cc_enable != enable) {
		v4l_dbg(1, debug, client, "Turn CC %s\n", enable ? "on" : "off");
		saa7127_write(client, SAA7127_REG_CLOSED_CAPTION,
				(state->xds_enable << 7) | (enable << 6) | 0x11);
		state->cc_enable = enable;
	}
	if (!enable)
		return 0;

	v4l_dbg(2, debug, client, "CC data: %04x\n", cc);
	saa7127_write(client, SAA7127_REG_LINE_21_ODD_0, cc & 0xff);
	saa7127_write(client, SAA7127_REG_LINE_21_ODD_1, cc >> 8);
	state->cc_data = cc;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_xds(struct i2c_client *client, struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	u16 xds = data->data[1] << 8 | data->data[0];
	int enable = (data->line != 0);

	if (enable && (data->field != 1 || data->line != 21))
		return -EINVAL;
	if (state->xds_enable != enable) {
		v4l_dbg(1, debug, client, "Turn XDS %s\n", enable ? "on" : "off");
		saa7127_write(client, SAA7127_REG_CLOSED_CAPTION,
				(enable << 7) | (state->cc_enable << 6) | 0x11);
		state->xds_enable = enable;
	}
	if (!enable)
		return 0;

	v4l_dbg(2, debug, client, "XDS data: %04x\n", xds);
	saa7127_write(client, SAA7127_REG_LINE_21_EVEN_0, xds & 0xff);
	saa7127_write(client, SAA7127_REG_LINE_21_EVEN_1, xds >> 8);
	state->xds_data = xds;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_wss(struct i2c_client *client, struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 23))
		return -EINVAL;
	if (state->wss_enable != enable) {
		v4l_dbg(1, debug, client, "Turn WSS %s\n", enable ? "on" : "off");
		saa7127_write(client, 0x27, enable << 7);
		state->wss_enable = enable;
	}
	if (!enable)
		return 0;

	saa7127_write(client, 0x26, data->data[0]);
	saa7127_write(client, 0x27, 0x80 | (data->data[1] & 0x3f));
	v4l_dbg(1, debug, client, "WSS mode: %s\n", wss_strs[data->data[0] & 0xf]);
	state->wss_mode = (data->data[1] & 0x3f) << 8 | data->data[0];
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_video_enable(struct i2c_client *client, int enable)
{
	struct saa7127_state *state = i2c_get_clientdata(client);

	if (enable) {
		v4l_dbg(1, debug, client, "Enable Video Output\n");
		saa7127_write(client, 0x2d, state->reg_2d);
		saa7127_write(client, 0x61, state->reg_61);
	} else {
		v4l_dbg(1, debug, client, "Disable Video Output\n");
		saa7127_write(client, 0x2d, (state->reg_2d & 0xf0));
		saa7127_write(client, 0x61, (state->reg_61 | 0xc0));
	}
	state->video_enable = enable;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_std(struct i2c_client *client, v4l2_std_id std)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	const struct i2c_reg_value *inittab;

	if (std & V4L2_STD_525_60) {
		v4l_dbg(1, debug, client, "Selecting 60 Hz video Standard\n");
		inittab = saa7127_init_config_60hz;
		state->reg_61 = SAA7127_60HZ_DAC_CONTROL;
	} else {
		v4l_dbg(1, debug, client, "Selecting 50 Hz video Standard\n");
		inittab = saa7127_init_config_50hz;
		state->reg_61 = SAA7127_50HZ_DAC_CONTROL;
	}

	/* Write Table */
	saa7127_write_inittab(client, inittab);
	state->std = std;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_output_type(struct i2c_client *client, int output)
{
	struct saa7127_state *state = i2c_get_clientdata(client);

	switch (output) {
	case SAA7127_OUTPUT_TYPE_RGB:
		state->reg_2d = 0x0f;	/* RGB + CVBS (for sync) */
		state->reg_3a = 0x13;	/* by default switch YUV to RGB-matrix on */
		break;

	case SAA7127_OUTPUT_TYPE_COMPOSITE:
		state->reg_2d = 0x08;	/* 00001000 CVBS only, RGB DAC's off (high impedance mode) */
		state->reg_3a = 0x13;	/* by default switch YUV to RGB-matrix on */
		break;

	case SAA7127_OUTPUT_TYPE_SVIDEO:
		state->reg_2d = 0xff;	/* 11111111  croma -> R, luma -> CVBS + G + B */
		state->reg_3a = 0x13;	/* by default switch YUV to RGB-matrix on */
		break;

	case SAA7127_OUTPUT_TYPE_YUV_V:
		state->reg_2d = 0x4f;	/* reg 2D = 01001111, all DAC's on, RGB + VBS */
		state->reg_3a = 0x0b;	/* reg 3A = 00001011, bypass RGB-matrix */
		break;

	case SAA7127_OUTPUT_TYPE_YUV_C:
		state->reg_2d = 0x0f;	/* reg 2D = 00001111, all DAC's on, RGB + CVBS */
		state->reg_3a = 0x0b;	/* reg 3A = 00001011, bypass RGB-matrix */
		break;

	case SAA7127_OUTPUT_TYPE_BOTH:
		state->reg_2d = 0xbf;
		state->reg_3a = 0x13;	/* by default switch YUV to RGB-matrix on */
		break;

	default:
		return -EINVAL;
	}
	v4l_dbg(1, debug, client, "Selecting %s output type\n", output_strs[output]);

	/* Configure Encoder */
	saa7127_write(client, 0x2d, state->reg_2d);
	saa7127_write(client, 0x3a, state->reg_3a | state->reg_3a_cb);
	state->output_type = output;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_input_type(struct i2c_client *client, int input)
{
	struct saa7127_state *state = i2c_get_clientdata(client);

	switch (input) {
	case SAA7127_INPUT_TYPE_NORMAL:	/* avia */
		v4l_dbg(1, debug, client, "Selecting Normal Encoder Input\n");
		state->reg_3a_cb = 0;
		break;

	case SAA7127_INPUT_TYPE_TEST_IMAGE:	/* color bar */
		v4l_dbg(1, debug, client, "Selecting Color Bar generator\n");
		state->reg_3a_cb = 0x80;
		break;

	default:
		return -EINVAL;
	}
	saa7127_write(client, 0x3a, state->reg_3a | state->reg_3a_cb);
	state->input_type = input;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	struct v4l2_format *fmt = arg;
	struct v4l2_routing *route = arg;

	switch (cmd) {
	case VIDIOC_S_STD:
		if (state->std == *(v4l2_std_id *)arg)
			break;
		return saa7127_set_std(client, *(v4l2_std_id *)arg);

	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = state->std;
		break;

	case VIDIOC_INT_G_VIDEO_ROUTING:
		route->input = state->input_type;
		route->output = state->output_type;
		break;

	case VIDIOC_INT_S_VIDEO_ROUTING:
	{
		int rc = 0;

		if (state->input_type != route->input) {
			rc = saa7127_set_input_type(client, route->input);
		}
		if (rc == 0 && state->output_type != route->output) {
			rc = saa7127_set_output_type(client, route->output);
		}
		return rc;
	}

	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
		if (state->video_enable == (cmd == VIDIOC_STREAMON))
			break;
		return saa7127_set_video_enable(client, cmd == VIDIOC_STREAMON);

	case VIDIOC_G_FMT:
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;

		memset(&fmt->fmt.sliced, 0, sizeof(fmt->fmt.sliced));
		if (state->vps_enable)
			fmt->fmt.sliced.service_lines[0][16] = V4L2_SLICED_VPS;
		if (state->wss_enable)
			fmt->fmt.sliced.service_lines[0][23] = V4L2_SLICED_WSS_625;
		if (state->cc_enable) {
			fmt->fmt.sliced.service_lines[0][21] = V4L2_SLICED_CAPTION_525;
			fmt->fmt.sliced.service_lines[1][21] = V4L2_SLICED_CAPTION_525;
		}
		fmt->fmt.sliced.service_set =
			(state->vps_enable ? V4L2_SLICED_VPS : 0) |
			(state->wss_enable ? V4L2_SLICED_WSS_625 : 0) |
			(state->cc_enable ? V4L2_SLICED_CAPTION_525 : 0);
		break;

	case VIDIOC_LOG_STATUS:
		v4l_info(client, "Standard: %s\n", (state->std & V4L2_STD_525_60) ? "60 Hz" : "50 Hz");
		v4l_info(client, "Input:    %s\n", state->input_type ?  "color bars" : "normal");
		v4l_info(client, "Output:   %s\n", state->video_enable ?
			output_strs[state->output_type] : "disabled");
		v4l_info(client, "WSS:      %s\n", state->wss_enable ?
			wss_strs[state->wss_mode] : "disabled");
		v4l_info(client, "VPS:      %s\n", state->vps_enable ? "enabled" : "disabled");
		v4l_info(client, "CC:       %s\n", state->cc_enable ? "enabled" : "disabled");
		break;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_INT_G_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_SAA7127)
			return -EINVAL;
		reg->val = saa7127_read(client, reg->reg & 0xff);
		break;
	}

	case VIDIOC_INT_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_SAA7127)
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		saa7127_write(client, reg->reg & 0xff, reg->val & 0xff);
		break;
	}
#endif

	case VIDIOC_INT_S_VBI_DATA:
	{
		struct v4l2_sliced_vbi_data *data = arg;

		switch (data->id) {
			case V4L2_SLICED_WSS_625:
				return saa7127_set_wss(client, data);
			case V4L2_SLICED_VPS:
				return saa7127_set_vps(client, data);
			case V4L2_SLICED_CAPTION_525:
				if (data->field == 0)
					return saa7127_set_cc(client, data);
				return saa7127_set_xds(client, data);
			default:
				return -EINVAL;
		}
		break;
	}

	case VIDIOC_INT_G_CHIP_IDENT:
		*(enum v4l2_chip_ident *)arg = state->ident;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_saa7127;

/* ----------------------------------------------------------------------- */

static int saa7127_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct saa7127_state *state;
	struct v4l2_sliced_vbi_data vbi = { 0, 0, 0, 0 };  /* set to disabled */
	int read_result = 0;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;

	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver_saa7127;
	snprintf(client->name, sizeof(client->name) - 1, "saa7127");

	v4l_dbg(1, debug, client, "detecting saa7127 client on address 0x%x\n", address << 1);

	/* First test register 0: Bits 5-7 are a version ID (should be 0),
	   and bit 2 should also be 0.
	   This is rather general, so the second test is more specific and
	   looks at the 'ending point of burst in clock cycles' which is
	   0x1d after a reset and not expected to ever change. */
	if ((saa7127_read(client, 0) & 0xe4) != 0 ||
			(saa7127_read(client, 0x29) & 0x3f) != 0x1d) {
		v4l_dbg(1, debug, client, "saa7127 not found\n");
		kfree(client);
		return 0;
	}
	state = kzalloc(sizeof(struct saa7127_state), GFP_KERNEL);

	if (state == NULL) {
		kfree(client);
		return (-ENOMEM);
	}

	i2c_set_clientdata(client, state);

	/* Configure Encoder */

	v4l_dbg(1, debug, client, "Configuring encoder\n");
	saa7127_write_inittab(client, saa7127_init_config_common);
	saa7127_set_std(client, V4L2_STD_NTSC);
	saa7127_set_output_type(client, SAA7127_OUTPUT_TYPE_BOTH);
	saa7127_set_vps(client, &vbi);
	saa7127_set_wss(client, &vbi);
	saa7127_set_cc(client, &vbi);
	saa7127_set_xds(client, &vbi);
	if (test_image == 1) {
		/* The Encoder has an internal Colorbar generator */
		/* This can be used for debugging */
		saa7127_set_input_type(client, SAA7127_INPUT_TYPE_TEST_IMAGE);
	} else {
		saa7127_set_input_type(client, SAA7127_INPUT_TYPE_NORMAL);
	}
	saa7127_set_video_enable(client, 1);

	/* Detect if it's an saa7129 */
	read_result = saa7127_read(client, SAA7129_REG_FADE_KEY_COL2);
	saa7127_write(client, SAA7129_REG_FADE_KEY_COL2, 0xaa);
	if (saa7127_read(client, SAA7129_REG_FADE_KEY_COL2) == 0xaa) {
		v4l_info(client, "saa7129 found @ 0x%x (%s)\n", address << 1, adapter->name);
		saa7127_write(client, SAA7129_REG_FADE_KEY_COL2, read_result);
		saa7127_write_inittab(client, saa7129_init_config_extra);
		state->ident = V4L2_IDENT_SAA7129;
	} else {
		v4l_info(client, "saa7127 found @ 0x%x (%s)\n", address << 1, adapter->name);
		state->ident = V4L2_IDENT_SAA7127;
	}

	i2c_attach_client(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, saa7127_attach);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_detach(struct i2c_client *client)
{
	struct saa7127_state *state = i2c_get_clientdata(client);
	int err;

	/* Turn off TV output */
	saa7127_set_video_enable(client, 0);

	err = i2c_detach_client(client);

	if (err) {
		return err;
	}

	kfree(state);
	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver_saa7127 = {
	.driver = {
		.name = "saa7127",
	},
	.id = I2C_DRIVERID_SAA7127,
	.attach_adapter = saa7127_probe,
	.detach_client = saa7127_detach,
	.command = saa7127_command,
};


/* ----------------------------------------------------------------------- */

static int __init saa7127_init_module(void)
{
	return i2c_add_driver(&i2c_driver_saa7127);
}

/* ----------------------------------------------------------------------- */

static void __exit saa7127_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver_saa7127);
}

/* ----------------------------------------------------------------------- */

module_init(saa7127_init_module);
module_exit(saa7127_cleanup_module);
