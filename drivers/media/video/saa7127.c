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
 * work fine for those chips, except of course for the missing anti-taping
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
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>
#include <media/saa7127.h>

static int debug;
static int test_image;

MODULE_DESCRIPTION("Philips SAA7127/9 video encoder driver");
MODULE_AUTHOR("Kevin Thayer, Chris Kennedy, Hans Verkuil");
MODULE_LICENSE("GPL");
module_param(debug, int, 0644);
module_param(test_image, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");
MODULE_PARM_DESC(test_image, "test_image (0-1)");


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
	struct v4l2_subdev sd;
	v4l2_std_id std;
	u32 ident;
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

static inline struct saa7127_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa7127_state, sd);
}

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
	"16:9 full format anamorphic",
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

static int saa7127_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

/* ----------------------------------------------------------------------- */

static int saa7127_write(struct v4l2_subdev *sd, u8 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i;

	for (i = 0; i < 3; i++) {
		if (i2c_smbus_write_byte_data(client, reg, val) == 0)
			return 0;
	}
	v4l2_err(sd, "I2C Write Problem\n");
	return -1;
}

/* ----------------------------------------------------------------------- */

static int saa7127_write_inittab(struct v4l2_subdev *sd,
				 const struct i2c_reg_value *regs)
{
	while (regs->reg != 0) {
		saa7127_write(sd, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_vps(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = to_state(sd);
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 16))
		return -EINVAL;
	if (state->vps_enable != enable) {
		v4l2_dbg(1, debug, sd, "Turn VPS Signal %s\n", enable ? "on" : "off");
		saa7127_write(sd, 0x54, enable << 7);
		state->vps_enable = enable;
	}
	if (!enable)
		return 0;

	state->vps_data[0] = data->data[2];
	state->vps_data[1] = data->data[8];
	state->vps_data[2] = data->data[9];
	state->vps_data[3] = data->data[10];
	state->vps_data[4] = data->data[11];
	v4l2_dbg(1, debug, sd, "Set VPS data %02x %02x %02x %02x %02x\n",
		state->vps_data[0], state->vps_data[1],
		state->vps_data[2], state->vps_data[3],
		state->vps_data[4]);
	saa7127_write(sd, 0x55, state->vps_data[0]);
	saa7127_write(sd, 0x56, state->vps_data[1]);
	saa7127_write(sd, 0x57, state->vps_data[2]);
	saa7127_write(sd, 0x58, state->vps_data[3]);
	saa7127_write(sd, 0x59, state->vps_data[4]);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_cc(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = to_state(sd);
	u16 cc = data->data[1] << 8 | data->data[0];
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 21))
		return -EINVAL;
	if (state->cc_enable != enable) {
		v4l2_dbg(1, debug, sd,
			"Turn CC %s\n", enable ? "on" : "off");
		saa7127_write(sd, SAA7127_REG_CLOSED_CAPTION,
			(state->xds_enable << 7) | (enable << 6) | 0x11);
		state->cc_enable = enable;
	}
	if (!enable)
		return 0;

	v4l2_dbg(2, debug, sd, "CC data: %04x\n", cc);
	saa7127_write(sd, SAA7127_REG_LINE_21_ODD_0, cc & 0xff);
	saa7127_write(sd, SAA7127_REG_LINE_21_ODD_1, cc >> 8);
	state->cc_data = cc;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_xds(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = to_state(sd);
	u16 xds = data->data[1] << 8 | data->data[0];
	int enable = (data->line != 0);

	if (enable && (data->field != 1 || data->line != 21))
		return -EINVAL;
	if (state->xds_enable != enable) {
		v4l2_dbg(1, debug, sd, "Turn XDS %s\n", enable ? "on" : "off");
		saa7127_write(sd, SAA7127_REG_CLOSED_CAPTION,
				(enable << 7) | (state->cc_enable << 6) | 0x11);
		state->xds_enable = enable;
	}
	if (!enable)
		return 0;

	v4l2_dbg(2, debug, sd, "XDS data: %04x\n", xds);
	saa7127_write(sd, SAA7127_REG_LINE_21_EVEN_0, xds & 0xff);
	saa7127_write(sd, SAA7127_REG_LINE_21_EVEN_1, xds >> 8);
	state->xds_data = xds;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_wss(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *data)
{
	struct saa7127_state *state = to_state(sd);
	int enable = (data->line != 0);

	if (enable && (data->field != 0 || data->line != 23))
		return -EINVAL;
	if (state->wss_enable != enable) {
		v4l2_dbg(1, debug, sd, "Turn WSS %s\n", enable ? "on" : "off");
		saa7127_write(sd, 0x27, enable << 7);
		state->wss_enable = enable;
	}
	if (!enable)
		return 0;

	saa7127_write(sd, 0x26, data->data[0]);
	saa7127_write(sd, 0x27, 0x80 | (data->data[1] & 0x3f));
	v4l2_dbg(1, debug, sd,
		"WSS mode: %s\n", wss_strs[data->data[0] & 0xf]);
	state->wss_mode = (data->data[1] & 0x3f) << 8 | data->data[0];
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_video_enable(struct v4l2_subdev *sd, int enable)
{
	struct saa7127_state *state = to_state(sd);

	if (enable) {
		v4l2_dbg(1, debug, sd, "Enable Video Output\n");
		saa7127_write(sd, 0x2d, state->reg_2d);
		saa7127_write(sd, 0x61, state->reg_61);
	} else {
		v4l2_dbg(1, debug, sd, "Disable Video Output\n");
		saa7127_write(sd, 0x2d, (state->reg_2d & 0xf0));
		saa7127_write(sd, 0x61, (state->reg_61 | 0xc0));
	}
	state->video_enable = enable;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa7127_state *state = to_state(sd);
	const struct i2c_reg_value *inittab;

	if (std & V4L2_STD_525_60) {
		v4l2_dbg(1, debug, sd, "Selecting 60 Hz video Standard\n");
		inittab = saa7127_init_config_60hz;
		state->reg_61 = SAA7127_60HZ_DAC_CONTROL;
	} else {
		v4l2_dbg(1, debug, sd, "Selecting 50 Hz video Standard\n");
		inittab = saa7127_init_config_50hz;
		state->reg_61 = SAA7127_50HZ_DAC_CONTROL;
	}

	/* Write Table */
	saa7127_write_inittab(sd, inittab);
	state->std = std;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_output_type(struct v4l2_subdev *sd, int output)
{
	struct saa7127_state *state = to_state(sd);

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
	v4l2_dbg(1, debug, sd,
		"Selecting %s output type\n", output_strs[output]);

	/* Configure Encoder */
	saa7127_write(sd, 0x2d, state->reg_2d);
	saa7127_write(sd, 0x3a, state->reg_3a | state->reg_3a_cb);
	state->output_type = output;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_set_input_type(struct v4l2_subdev *sd, int input)
{
	struct saa7127_state *state = to_state(sd);

	switch (input) {
	case SAA7127_INPUT_TYPE_NORMAL:	/* avia */
		v4l2_dbg(1, debug, sd, "Selecting Normal Encoder Input\n");
		state->reg_3a_cb = 0;
		break;

	case SAA7127_INPUT_TYPE_TEST_IMAGE:	/* color bar */
		v4l2_dbg(1, debug, sd, "Selecting Color Bar generator\n");
		state->reg_3a_cb = 0x80;
		break;

	default:
		return -EINVAL;
	}
	saa7127_write(sd, 0x3a, state->reg_3a | state->reg_3a_cb);
	state->input_type = input;
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_s_std_output(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa7127_state *state = to_state(sd);

	if (state->std == std)
		return 0;
	return saa7127_set_std(sd, std);
}

static int saa7127_s_routing(struct v4l2_subdev *sd, const struct v4l2_routing *route)
{
	struct saa7127_state *state = to_state(sd);
	int rc = 0;

	if (state->input_type != route->input)
		rc = saa7127_set_input_type(sd, route->input);
	if (rc == 0 && state->output_type != route->output)
		rc = saa7127_set_output_type(sd, route->output);
	return rc;
}

static int saa7127_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct saa7127_state *state = to_state(sd);

	if (state->video_enable == enable)
		return 0;
	return saa7127_set_video_enable(sd, enable);
}

static int saa7127_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	struct saa7127_state *state = to_state(sd);

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
	return 0;
}

static int saa7127_s_vbi_data(struct v4l2_subdev *sd, const struct v4l2_sliced_vbi_data *data)
{
	switch (data->id) {
	case V4L2_SLICED_WSS_625:
		return saa7127_set_wss(sd, data);
	case V4L2_SLICED_VPS:
		return saa7127_set_vps(sd, data);
	case V4L2_SLICED_CAPTION_525:
		if (data->field == 0)
			return saa7127_set_cc(sd, data);
		return saa7127_set_xds(sd, data);
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int saa7127_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->val = saa7127_read(sd, reg->reg & 0xff);
	reg->size = 1;
	return 0;
}

static int saa7127_s_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!v4l2_chip_match_i2c_client(client, &reg->match))
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	saa7127_write(sd, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

static int saa7127_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct saa7127_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, state->ident, 0);
}

static int saa7127_log_status(struct v4l2_subdev *sd)
{
	struct saa7127_state *state = to_state(sd);

	v4l2_info(sd, "Standard: %s\n", (state->std & V4L2_STD_525_60) ? "60 Hz" : "50 Hz");
	v4l2_info(sd, "Input:    %s\n", state->input_type ?  "color bars" : "normal");
	v4l2_info(sd, "Output:   %s\n", state->video_enable ?
			output_strs[state->output_type] : "disabled");
	v4l2_info(sd, "WSS:      %s\n", state->wss_enable ?
			wss_strs[state->wss_mode] : "disabled");
	v4l2_info(sd, "VPS:      %s\n", state->vps_enable ? "enabled" : "disabled");
	v4l2_info(sd, "CC:       %s\n", state->cc_enable ? "enabled" : "disabled");
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa7127_core_ops = {
	.log_status = saa7127_log_status,
	.g_chip_ident = saa7127_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = saa7127_g_register,
	.s_register = saa7127_s_register,
#endif
};

static const struct v4l2_subdev_video_ops saa7127_video_ops = {
	.s_vbi_data = saa7127_s_vbi_data,
	.g_fmt = saa7127_g_fmt,
	.s_std_output = saa7127_s_std_output,
	.s_routing = saa7127_s_routing,
	.s_stream = saa7127_s_stream,
};

static const struct v4l2_subdev_ops saa7127_ops = {
	.core = &saa7127_core_ops,
	.video = &saa7127_video_ops,
};

/* ----------------------------------------------------------------------- */

static int saa7127_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct saa7127_state *state;
	struct v4l2_subdev *sd;
	struct v4l2_sliced_vbi_data vbi = { 0, 0, 0, 0 };  /* set to disabled */

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	v4l_dbg(1, debug, client, "detecting saa7127 client on address 0x%x\n",
			client->addr << 1);

	state = kzalloc(sizeof(struct saa7127_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &saa7127_ops);

	/* First test register 0: Bits 5-7 are a version ID (should be 0),
	   and bit 2 should also be 0.
	   This is rather general, so the second test is more specific and
	   looks at the 'ending point of burst in clock cycles' which is
	   0x1d after a reset and not expected to ever change. */
	if ((saa7127_read(sd, 0) & 0xe4) != 0 ||
			(saa7127_read(sd, 0x29) & 0x3f) != 0x1d) {
		v4l2_dbg(1, debug, sd, "saa7127 not found\n");
		kfree(state);
		return -ENODEV;
	}

	/* Configure Encoder */

	v4l2_dbg(1, debug, sd, "Configuring encoder\n");
	saa7127_write_inittab(sd, saa7127_init_config_common);
	saa7127_set_std(sd, V4L2_STD_NTSC);
	saa7127_set_output_type(sd, SAA7127_OUTPUT_TYPE_BOTH);
	saa7127_set_vps(sd, &vbi);
	saa7127_set_wss(sd, &vbi);
	saa7127_set_cc(sd, &vbi);
	saa7127_set_xds(sd, &vbi);
	if (test_image == 1)
		/* The Encoder has an internal Colorbar generator */
		/* This can be used for debugging */
		saa7127_set_input_type(sd, SAA7127_INPUT_TYPE_TEST_IMAGE);
	else
		saa7127_set_input_type(sd, SAA7127_INPUT_TYPE_NORMAL);
	saa7127_set_video_enable(sd, 1);

	if (id->driver_data) {	/* Chip type is already known */
		state->ident = id->driver_data;
	} else {		/* Needs detection */
		int read_result;

		/* Detect if it's an saa7129 */
		read_result = saa7127_read(sd, SAA7129_REG_FADE_KEY_COL2);
		saa7127_write(sd, SAA7129_REG_FADE_KEY_COL2, 0xaa);
		if (saa7127_read(sd, SAA7129_REG_FADE_KEY_COL2) == 0xaa) {
			saa7127_write(sd, SAA7129_REG_FADE_KEY_COL2,
					read_result);
			state->ident = V4L2_IDENT_SAA7129;
			strlcpy(client->name, "saa7129", I2C_NAME_SIZE);
		} else {
			state->ident = V4L2_IDENT_SAA7127;
			strlcpy(client->name, "saa7127", I2C_NAME_SIZE);
		}
	}

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);
	if (state->ident == V4L2_IDENT_SAA7129)
		saa7127_write_inittab(sd, saa7129_init_config_extra);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int saa7127_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	/* Turn off TV output */
	saa7127_set_video_enable(sd, 0);
	kfree(to_state(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_device_id saa7127_id[] = {
	{ "saa7127_auto", 0 },	/* auto-detection */
	{ "saa7126", V4L2_IDENT_SAA7127 },
	{ "saa7127", V4L2_IDENT_SAA7127 },
	{ "saa7128", V4L2_IDENT_SAA7129 },
	{ "saa7129", V4L2_IDENT_SAA7129 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa7127_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "saa7127",
	.driverid = I2C_DRIVERID_SAA7127,
	.probe = saa7127_probe,
	.remove = saa7127_remove,
	.id_table = saa7127_id,
};
