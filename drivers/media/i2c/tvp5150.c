// SPDX-License-Identifier: GPL-2.0
//
// tvp5150 - Texas Instruments TVP5150A/AM1 and TVP5151 video decoder driver
//
// Copyright (c) 2005,2006 Mauro Carvalho Chehab <mchehab@kernel.org>

#include <dt-bindings/media/tvp5150.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-rect.h>

#include "tvp5150_reg.h"

#define TVP5150_H_MAX		720U
#define TVP5150_V_MAX_525_60	480U
#define TVP5150_V_MAX_OTHERS	576U
#define TVP5150_MAX_CROP_LEFT	511
#define TVP5150_MAX_CROP_TOP	127
#define TVP5150_CROP_SHIFT	2
#define TVP5150_MBUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8
#define TVP5150_FIELD		V4L2_FIELD_ALTERNATE
#define TVP5150_COLORSPACE	V4L2_COLORSPACE_SMPTE170M
#define TVP5150_STD_MASK	(V4L2_STD_NTSC     | \
				 V4L2_STD_NTSC_443 | \
				 V4L2_STD_PAL      | \
				 V4L2_STD_PAL_M    | \
				 V4L2_STD_PAL_N    | \
				 V4L2_STD_PAL_Nc   | \
				 V4L2_STD_SECAM)

#define TVP5150_MAX_CONNECTORS	3 /* Check dt-bindings for more information */

MODULE_DESCRIPTION("Texas Instruments TVP5150A/TVP5150AM1/TVP5151 video decoder driver");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL v2");


static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

#define dprintk0(__dev, __arg...) dev_dbg_lvl(__dev, 0, 0, __arg)

enum tvp5150_pads {
	TVP5150_PAD_AIP1A,
	TVP5150_PAD_AIP1B,
	TVP5150_PAD_VID_OUT,
	TVP5150_NUM_PADS
};

struct tvp5150_connector {
	struct v4l2_fwnode_connector base;
	struct media_entity ent;
	struct media_pad pad;
};

struct tvp5150 {
	struct v4l2_subdev sd;

	struct media_pad pads[TVP5150_NUM_PADS];
	struct tvp5150_connector connectors[TVP5150_MAX_CONNECTORS];
	struct tvp5150_connector *cur_connector;
	unsigned int connectors_num;

	struct v4l2_ctrl_handler hdl;
	struct v4l2_rect rect;
	struct regmap *regmap;
	int irq;

	v4l2_std_id norm;	/* Current set standard */
	v4l2_std_id detected_norm;
	u32 input;
	u32 output;
	u32 oe;
	int enable;
	bool lock;

	u16 dev_id;
	u16 rom_ver;

	enum v4l2_mbus_type mbus_type;
};

static inline struct tvp5150 *to_tvp5150(struct v4l2_subdev *sd)
{
	return container_of(sd, struct tvp5150, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct tvp5150, hdl)->sd;
}

static int tvp5150_read(struct v4l2_subdev *sd, unsigned char addr)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	int ret, val;

	ret = regmap_read(decoder->regmap, addr, &val);
	if (ret < 0)
		return ret;

	return val;
}

static void dump_reg_range(struct v4l2_subdev *sd, char *s, u8 init,
				const u8 end, int max_line)
{
	u8 buf[16];
	int i = 0, j, len;

	if (max_line > 16) {
		dprintk0(sd->dev, "too much data to dump\n");
		return;
	}

	for (i = init; i < end; i += max_line) {
		len = (end - i > max_line) ? max_line : end - i;

		for (j = 0; j < len; j++)
			buf[j] = tvp5150_read(sd, i + j);

		dprintk0(sd->dev, "%s reg %02x = %*ph\n", s, i, len, buf);
	}
}

static int tvp5150_log_status(struct v4l2_subdev *sd)
{
	dprintk0(sd->dev, "tvp5150: Video input source selection #1 = 0x%02x\n",
		tvp5150_read(sd, TVP5150_VD_IN_SRC_SEL_1));
	dprintk0(sd->dev, "tvp5150: Analog channel controls = 0x%02x\n",
		tvp5150_read(sd, TVP5150_ANAL_CHL_CTL));
	dprintk0(sd->dev, "tvp5150: Operation mode controls = 0x%02x\n",
		tvp5150_read(sd, TVP5150_OP_MODE_CTL));
	dprintk0(sd->dev, "tvp5150: Miscellaneous controls = 0x%02x\n",
		tvp5150_read(sd, TVP5150_MISC_CTL));
	dprintk0(sd->dev, "tvp5150: Autoswitch mask= 0x%02x\n",
		tvp5150_read(sd, TVP5150_AUTOSW_MSK));
	dprintk0(sd->dev, "tvp5150: Color killer threshold control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_COLOR_KIL_THSH_CTL));
	dprintk0(sd->dev, "tvp5150: Luminance processing controls #1 #2 and #3 = %02x %02x %02x\n",
		tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_1),
		tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_2),
		tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_3));
	dprintk0(sd->dev, "tvp5150: Brightness control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_BRIGHT_CTL));
	dprintk0(sd->dev, "tvp5150: Color saturation control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_SATURATION_CTL));
	dprintk0(sd->dev, "tvp5150: Hue control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_HUE_CTL));
	dprintk0(sd->dev, "tvp5150: Contrast control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_CONTRAST_CTL));
	dprintk0(sd->dev, "tvp5150: Outputs and data rates select = 0x%02x\n",
		tvp5150_read(sd, TVP5150_DATA_RATE_SEL));
	dprintk0(sd->dev, "tvp5150: Configuration shared pins = 0x%02x\n",
		tvp5150_read(sd, TVP5150_CONF_SHARED_PIN));
	dprintk0(sd->dev, "tvp5150: Active video cropping start = 0x%02x%02x\n",
		tvp5150_read(sd, TVP5150_ACT_VD_CROP_ST_MSB),
		tvp5150_read(sd, TVP5150_ACT_VD_CROP_ST_LSB));
	dprintk0(sd->dev, "tvp5150: Active video cropping stop  = 0x%02x%02x\n",
		tvp5150_read(sd, TVP5150_ACT_VD_CROP_STP_MSB),
		tvp5150_read(sd, TVP5150_ACT_VD_CROP_STP_LSB));
	dprintk0(sd->dev, "tvp5150: Genlock/RTC = 0x%02x\n",
		tvp5150_read(sd, TVP5150_GENLOCK));
	dprintk0(sd->dev, "tvp5150: Horizontal sync start = 0x%02x\n",
		tvp5150_read(sd, TVP5150_HORIZ_SYNC_START));
	dprintk0(sd->dev, "tvp5150: Vertical blanking start = 0x%02x\n",
		tvp5150_read(sd, TVP5150_VERT_BLANKING_START));
	dprintk0(sd->dev, "tvp5150: Vertical blanking stop = 0x%02x\n",
		tvp5150_read(sd, TVP5150_VERT_BLANKING_STOP));
	dprintk0(sd->dev, "tvp5150: Chrominance processing control #1 and #2 = %02x %02x\n",
		tvp5150_read(sd, TVP5150_CHROMA_PROC_CTL_1),
		tvp5150_read(sd, TVP5150_CHROMA_PROC_CTL_2));
	dprintk0(sd->dev, "tvp5150: Interrupt reset register B = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_RESET_REG_B));
	dprintk0(sd->dev, "tvp5150: Interrupt enable register B = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_ENABLE_REG_B));
	dprintk0(sd->dev, "tvp5150: Interrupt configuration register B = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INTT_CONFIG_REG_B));
	dprintk0(sd->dev, "tvp5150: Video standard = 0x%02x\n",
		tvp5150_read(sd, TVP5150_VIDEO_STD));
	dprintk0(sd->dev, "tvp5150: Chroma gain factor: Cb=0x%02x Cr=0x%02x\n",
		tvp5150_read(sd, TVP5150_CB_GAIN_FACT),
		tvp5150_read(sd, TVP5150_CR_GAIN_FACTOR));
	dprintk0(sd->dev, "tvp5150: Macrovision on counter = 0x%02x\n",
		tvp5150_read(sd, TVP5150_MACROVISION_ON_CTR));
	dprintk0(sd->dev, "tvp5150: Macrovision off counter = 0x%02x\n",
		tvp5150_read(sd, TVP5150_MACROVISION_OFF_CTR));
	dprintk0(sd->dev, "tvp5150: ITU-R BT.656.%d timing(TVP5150AM1 only)\n",
		(tvp5150_read(sd, TVP5150_REV_SELECT) & 1) ? 3 : 4);
	dprintk0(sd->dev, "tvp5150: Device ID = %02x%02x\n",
		tvp5150_read(sd, TVP5150_MSB_DEV_ID),
		tvp5150_read(sd, TVP5150_LSB_DEV_ID));
	dprintk0(sd->dev, "tvp5150: ROM version = (hex) %02x.%02x\n",
		tvp5150_read(sd, TVP5150_ROM_MAJOR_VER),
		tvp5150_read(sd, TVP5150_ROM_MINOR_VER));
	dprintk0(sd->dev, "tvp5150: Vertical line count = 0x%02x%02x\n",
		tvp5150_read(sd, TVP5150_VERT_LN_COUNT_MSB),
		tvp5150_read(sd, TVP5150_VERT_LN_COUNT_LSB));
	dprintk0(sd->dev, "tvp5150: Interrupt status register B = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_STATUS_REG_B));
	dprintk0(sd->dev, "tvp5150: Interrupt active register B = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_ACTIVE_REG_B));
	dprintk0(sd->dev, "tvp5150: Status regs #1 to #5 = %02x %02x %02x %02x %02x\n",
		tvp5150_read(sd, TVP5150_STATUS_REG_1),
		tvp5150_read(sd, TVP5150_STATUS_REG_2),
		tvp5150_read(sd, TVP5150_STATUS_REG_3),
		tvp5150_read(sd, TVP5150_STATUS_REG_4),
		tvp5150_read(sd, TVP5150_STATUS_REG_5));

	dump_reg_range(sd, "Teletext filter 1",   TVP5150_TELETEXT_FIL1_INI,
			TVP5150_TELETEXT_FIL1_END, 8);
	dump_reg_range(sd, "Teletext filter 2",   TVP5150_TELETEXT_FIL2_INI,
			TVP5150_TELETEXT_FIL2_END, 8);

	dprintk0(sd->dev, "tvp5150: Teletext filter enable = 0x%02x\n",
		tvp5150_read(sd, TVP5150_TELETEXT_FIL_ENA));
	dprintk0(sd->dev, "tvp5150: Interrupt status register A = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_STATUS_REG_A));
	dprintk0(sd->dev, "tvp5150: Interrupt enable register A = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_ENABLE_REG_A));
	dprintk0(sd->dev, "tvp5150: Interrupt configuration = 0x%02x\n",
		tvp5150_read(sd, TVP5150_INT_CONF));
	dprintk0(sd->dev, "tvp5150: VDP status register = 0x%02x\n",
		tvp5150_read(sd, TVP5150_VDP_STATUS_REG));
	dprintk0(sd->dev, "tvp5150: FIFO word count = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FIFO_WORD_COUNT));
	dprintk0(sd->dev, "tvp5150: FIFO interrupt threshold = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FIFO_INT_THRESHOLD));
	dprintk0(sd->dev, "tvp5150: FIFO reset = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FIFO_RESET));
	dprintk0(sd->dev, "tvp5150: Line number interrupt = 0x%02x\n",
		tvp5150_read(sd, TVP5150_LINE_NUMBER_INT));
	dprintk0(sd->dev, "tvp5150: Pixel alignment register = 0x%02x%02x\n",
		tvp5150_read(sd, TVP5150_PIX_ALIGN_REG_HIGH),
		tvp5150_read(sd, TVP5150_PIX_ALIGN_REG_LOW));
	dprintk0(sd->dev, "tvp5150: FIFO output control = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FIFO_OUT_CTRL));
	dprintk0(sd->dev, "tvp5150: Full field enable = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FULL_FIELD_ENA));
	dprintk0(sd->dev, "tvp5150: Full field mode register = 0x%02x\n",
		tvp5150_read(sd, TVP5150_FULL_FIELD_MODE_REG));

	dump_reg_range(sd, "CC   data",   TVP5150_CC_DATA_INI,
			TVP5150_CC_DATA_END, 8);

	dump_reg_range(sd, "WSS  data",   TVP5150_WSS_DATA_INI,
			TVP5150_WSS_DATA_END, 8);

	dump_reg_range(sd, "VPS  data",   TVP5150_VPS_DATA_INI,
			TVP5150_VPS_DATA_END, 8);

	dump_reg_range(sd, "VITC data",   TVP5150_VITC_DATA_INI,
			TVP5150_VITC_DATA_END, 10);

	dump_reg_range(sd, "Line mode",   TVP5150_LINE_MODE_INI,
			TVP5150_LINE_MODE_END, 8);
	return 0;
}

/****************************************************************************
			Basic functions
 ****************************************************************************/

static void tvp5150_selmux(struct v4l2_subdev *sd)
{
	int opmode = 0;
	struct tvp5150 *decoder = to_tvp5150(sd);
	unsigned int mask, val;
	int input = 0;

	/* Only tvp5150am1 and tvp5151 have signal generator support */
	if ((decoder->dev_id == 0x5150 && decoder->rom_ver == 0x0400) ||
	    (decoder->dev_id == 0x5151 && decoder->rom_ver == 0x0100)) {
		if (!decoder->enable)
			input = 8;
	}

	switch (decoder->input) {
	case TVP5150_COMPOSITE1:
		input |= 2;
		fallthrough;
	case TVP5150_COMPOSITE0:
		break;
	case TVP5150_SVIDEO:
	default:
		input |= 1;
		break;
	}

	dev_dbg_lvl(sd->dev, 1, debug,
		    "Selecting video route: route input=%s, output=%s => tvp5150 input=0x%02x, opmode=0x%02x\n",
		    decoder->input == 0 ? "aip1a" :
		    decoder->input == 2 ? "aip1b" : "svideo",
		    decoder->output == 0 ? "normal" : "black-frame-gen",
		    input, opmode);

	regmap_write(decoder->regmap, TVP5150_OP_MODE_CTL, opmode);
	regmap_write(decoder->regmap, TVP5150_VD_IN_SRC_SEL_1, input);

	/*
	 * Setup the FID/GLCO/VLK/HVLK and INTREQ/GPCL/VBLK output signals. For
	 * S-Video we output the vertical lock (VLK) signal on FID/GLCO/VLK/HVLK
	 * and set INTREQ/GPCL/VBLK to logic 0. For composite we output the
	 * field indicator (FID) signal on FID/GLCO/VLK/HVLK and set
	 * INTREQ/GPCL/VBLK to logic 1.
	 */
	mask = TVP5150_MISC_CTL_GPCL | TVP5150_MISC_CTL_HVLK;
	if (decoder->input == TVP5150_SVIDEO)
		val = TVP5150_MISC_CTL_HVLK;
	else
		val = TVP5150_MISC_CTL_GPCL;
	regmap_update_bits(decoder->regmap, TVP5150_MISC_CTL, mask, val);
};

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_default[] = {
	{ /* 0x00 */
		TVP5150_VD_IN_SRC_SEL_1, 0x00
	},
	{ /* 0x01 */
		TVP5150_ANAL_CHL_CTL, 0x15
	},
	{ /* 0x02 */
		TVP5150_OP_MODE_CTL, 0x00
	},
	{ /* 0x03 */
		TVP5150_MISC_CTL, 0x01
	},
	{ /* 0x06 */
		TVP5150_COLOR_KIL_THSH_CTL, 0x10
	},
	{ /* 0x07 */
		TVP5150_LUMA_PROC_CTL_1, 0x60
	},
	{ /* 0x08 */
		TVP5150_LUMA_PROC_CTL_2, 0x00
	},
	{ /* 0x09 */
		TVP5150_BRIGHT_CTL, 0x80
	},
	{ /* 0x0a */
		TVP5150_SATURATION_CTL, 0x80
	},
	{ /* 0x0b */
		TVP5150_HUE_CTL, 0x00
	},
	{ /* 0x0c */
		TVP5150_CONTRAST_CTL, 0x80
	},
	{ /* 0x0d */
		TVP5150_DATA_RATE_SEL, 0x47
	},
	{ /* 0x0e */
		TVP5150_LUMA_PROC_CTL_3, 0x00
	},
	{ /* 0x0f */
		TVP5150_CONF_SHARED_PIN, 0x08
	},
	{ /* 0x11 */
		TVP5150_ACT_VD_CROP_ST_MSB, 0x00
	},
	{ /* 0x12 */
		TVP5150_ACT_VD_CROP_ST_LSB, 0x00
	},
	{ /* 0x13 */
		TVP5150_ACT_VD_CROP_STP_MSB, 0x00
	},
	{ /* 0x14 */
		TVP5150_ACT_VD_CROP_STP_LSB, 0x00
	},
	{ /* 0x15 */
		TVP5150_GENLOCK, 0x01
	},
	{ /* 0x16 */
		TVP5150_HORIZ_SYNC_START, 0x80
	},
	{ /* 0x18 */
		TVP5150_VERT_BLANKING_START, 0x00
	},
	{ /* 0x19 */
		TVP5150_VERT_BLANKING_STOP, 0x00
	},
	{ /* 0x1a */
		TVP5150_CHROMA_PROC_CTL_1, 0x0c
	},
	{ /* 0x1b */
		TVP5150_CHROMA_PROC_CTL_2, 0x14
	},
	{ /* 0x1c */
		TVP5150_INT_RESET_REG_B, 0x00
	},
	{ /* 0x1d */
		TVP5150_INT_ENABLE_REG_B, 0x00
	},
	{ /* 0x1e */
		TVP5150_INTT_CONFIG_REG_B, 0x00
	},
	{ /* 0x28 */
		TVP5150_VIDEO_STD, 0x00
	},
	{ /* 0x2e */
		TVP5150_MACROVISION_ON_CTR, 0x0f
	},
	{ /* 0x2f */
		TVP5150_MACROVISION_OFF_CTR, 0x01
	},
	{ /* 0xbb */
		TVP5150_TELETEXT_FIL_ENA, 0x00
	},
	{ /* 0xc0 */
		TVP5150_INT_STATUS_REG_A, 0x00
	},
	{ /* 0xc1 */
		TVP5150_INT_ENABLE_REG_A, 0x00
	},
	{ /* 0xc2 */
		TVP5150_INT_CONF, 0x04
	},
	{ /* 0xc8 */
		TVP5150_FIFO_INT_THRESHOLD, 0x80
	},
	{ /* 0xc9 */
		TVP5150_FIFO_RESET, 0x00
	},
	{ /* 0xca */
		TVP5150_LINE_NUMBER_INT, 0x00
	},
	{ /* 0xcb */
		TVP5150_PIX_ALIGN_REG_LOW, 0x4e
	},
	{ /* 0xcc */
		TVP5150_PIX_ALIGN_REG_HIGH, 0x00
	},
	{ /* 0xcd */
		TVP5150_FIFO_OUT_CTRL, 0x01
	},
	{ /* 0xcf */
		TVP5150_FULL_FIELD_ENA, 0x00
	},
	{ /* 0xd0 */
		TVP5150_LINE_MODE_INI, 0x00
	},
	{ /* 0xfc */
		TVP5150_FULL_FIELD_MODE_REG, 0x7f
	},
	{ /* end of data */
		0xff, 0xff
	}
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_enable[] = {
	{	/* Automatic offset and AGC enabled */
		TVP5150_ANAL_CHL_CTL, 0x15
	}, {	/* Activate YCrCb output 0x9 or 0xd ? */
		TVP5150_MISC_CTL, TVP5150_MISC_CTL_GPCL |
				  TVP5150_MISC_CTL_INTREQ_OE |
				  TVP5150_MISC_CTL_YCBCR_OE |
				  TVP5150_MISC_CTL_SYNC_OE |
				  TVP5150_MISC_CTL_VBLANK |
				  TVP5150_MISC_CTL_CLOCK_OE,
	}, {	/* Activates video std autodetection for all standards */
		TVP5150_AUTOSW_MSK, 0x0
	}, {	/* Default format: 0x47. For 4:2:2: 0x40 */
		TVP5150_DATA_RATE_SEL, 0x47
	}, {
		TVP5150_CHROMA_PROC_CTL_1, 0x0c
	}, {
		TVP5150_CHROMA_PROC_CTL_2, 0x54
	}, {	/* Non documented, but initialized on WinTV USB2 */
		0x27, 0x20
	}, {
		0xff, 0xff
	}
};

struct tvp5150_vbi_type {
	unsigned int vbi_type;
	unsigned int ini_line;
	unsigned int end_line;
	unsigned int by_field :1;
};

struct i2c_vbi_ram_value {
	u16 reg;
	struct tvp5150_vbi_type type;
	unsigned char values[16];
};

/* This struct have the values for each supported VBI Standard
 * by
 tvp5150_vbi_types should follow the same order as vbi_ram_default
 * value 0 means rom position 0x10, value 1 means rom position 0x30
 * and so on. There are 16 possible locations from 0 to 15.
 */

static struct i2c_vbi_ram_value vbi_ram_default[] = {

	/*
	 * FIXME: Current api doesn't handle all VBI types, those not
	 * yet supported are placed under #if 0
	 */
#if 0
	[0] = {0x010, /* Teletext, SECAM, WST System A */
		{V4L2_SLICED_TELETEXT_SECAM, 6, 23, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x26,
		  0xe6, 0xb4, 0x0e, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
#endif
	[1] = {0x030, /* Teletext, PAL, WST System B */
		{V4L2_SLICED_TELETEXT_B, 6, 22, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0x27, 0x2e, 0x20, 0x2b,
		  0xa6, 0x72, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
#if 0
	[2] = {0x050, /* Teletext, PAL, WST System C */
		{V4L2_SLICED_TELETEXT_PAL_C, 6, 22, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x22,
		  0xa6, 0x98, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	[3] = {0x070, /* Teletext, NTSC, WST System B */
		{V4L2_SLICED_TELETEXT_NTSC_B, 10, 21, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0x27, 0x2e, 0x20, 0x23,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	[4] = {0x090, /* Tetetext, NTSC NABTS System C */
		{V4L2_SLICED_TELETEXT_NTSC_C, 10, 21, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x22,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x15, 0x00 }
	},
	[5] = {0x0b0, /* Teletext, NTSC-J, NABTS System D */
		{V4L2_SLICED_TELETEXT_NTSC_D, 10, 21, 1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xa7, 0x2e, 0x20, 0x23,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	[6] = {0x0d0, /* Closed Caption, PAL/SECAM */
		{V4L2_SLICED_CAPTION_625, 22, 22, 1},
		{ 0xaa, 0x2a, 0xff, 0x3f, 0x04, 0x51, 0x6e, 0x02,
		  0xa6, 0x7b, 0x09, 0x00, 0x00, 0x00, 0x27, 0x00 }
	},
#endif
	[7] = {0x0f0, /* Closed Caption, NTSC */
		{V4L2_SLICED_CAPTION_525, 21, 21, 1},
		{ 0xaa, 0x2a, 0xff, 0x3f, 0x04, 0x51, 0x6e, 0x02,
		  0x69, 0x8c, 0x09, 0x00, 0x00, 0x00, 0x27, 0x00 }
	},
	[8] = {0x110, /* Wide Screen Signal, PAL/SECAM */
		{V4L2_SLICED_WSS_625, 23, 23, 1},
		{ 0x5b, 0x55, 0xc5, 0xff, 0x00, 0x71, 0x6e, 0x42,
		  0xa6, 0xcd, 0x0f, 0x00, 0x00, 0x00, 0x3a, 0x00 }
	},
#if 0
	[9] = {0x130, /* Wide Screen Signal, NTSC C */
		{V4L2_SLICED_WSS_525, 20, 20, 1},
		{ 0x38, 0x00, 0x3f, 0x00, 0x00, 0x71, 0x6e, 0x43,
		  0x69, 0x7c, 0x08, 0x00, 0x00, 0x00, 0x39, 0x00 }
	},
	[10] = {0x150, /* Vertical Interval Timecode (VITC), PAL/SECAM */
		{V4l2_SLICED_VITC_625, 6, 22, 0},
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x6d, 0x49,
		  0xa6, 0x85, 0x08, 0x00, 0x00, 0x00, 0x4c, 0x00 }
	},
	[11] = {0x170, /* Vertical Interval Timecode (VITC), NTSC */
		{V4l2_SLICED_VITC_525, 10, 20, 0},
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x6d, 0x49,
		  0x69, 0x94, 0x08, 0x00, 0x00, 0x00, 0x4c, 0x00 }
	},
#endif
	[12] = {0x190, /* Video Program System (VPS), PAL */
		{V4L2_SLICED_VPS, 16, 16, 0},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xba, 0xce, 0x2b, 0x0d,
		  0xa6, 0xda, 0x0b, 0x00, 0x00, 0x00, 0x60, 0x00 }
	},
	/* 0x1d0 User programmable */
};

static int tvp5150_write_inittab(struct v4l2_subdev *sd,
				const struct i2c_reg_value *regs)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	while (regs->reg != 0xff) {
		regmap_write(decoder->regmap, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

static int tvp5150_vdp_init(struct v4l2_subdev *sd)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct regmap *map = decoder->regmap;
	unsigned int i;
	int j;

	/* Disable Full Field */
	regmap_write(map, TVP5150_FULL_FIELD_ENA, 0);

	/* Before programming, Line mode should be at 0xff */
	for (i = TVP5150_LINE_MODE_INI; i <= TVP5150_LINE_MODE_END; i++)
		regmap_write(map, i, 0xff);

	/* Load Ram Table */
	for (j = 0; j < ARRAY_SIZE(vbi_ram_default); j++) {
		const struct i2c_vbi_ram_value *regs = &vbi_ram_default[j];

		if (!regs->type.vbi_type)
			continue;

		regmap_write(map, TVP5150_CONF_RAM_ADDR_HIGH, regs->reg >> 8);
		regmap_write(map, TVP5150_CONF_RAM_ADDR_LOW, regs->reg);

		for (i = 0; i < 16; i++)
			regmap_write(map, TVP5150_VDP_CONF_RAM_DATA,
				     regs->values[i]);
	}
	return 0;
}

/* Fills VBI capabilities based on i2c_vbi_ram_value struct */
static int tvp5150_g_sliced_vbi_cap(struct v4l2_subdev *sd,
				struct v4l2_sliced_vbi_cap *cap)
{
	int line, i;

	dev_dbg_lvl(sd->dev, 1, debug, "g_sliced_vbi_cap\n");
	memset(cap, 0, sizeof(*cap));

	for (i = 0; i < ARRAY_SIZE(vbi_ram_default); i++) {
		const struct i2c_vbi_ram_value *regs = &vbi_ram_default[i];

		if (!regs->type.vbi_type)
			continue;

		for (line = regs->type.ini_line;
		     line <= regs->type.end_line;
		     line++) {
			cap->service_lines[0][line] |= regs->type.vbi_type;
		}
		cap->service_set |= regs->type.vbi_type;
	}
	return 0;
}

/* Set vbi processing
 * type - one of tvp5150_vbi_types
 * line - line to gather data
 * fields: bit 0 field1, bit 1, field2
 * flags (default=0xf0) is a bitmask, were set means:
 *	bit 7: enable filtering null bytes on CC
 *	bit 6: send data also to FIFO
 *	bit 5: don't allow data with errors on FIFO
 *	bit 4: enable ECC when possible
 * pix_align = pix alignment:
 *	LSB = field1
 *	MSB = field2
 */
static int tvp5150_set_vbi(struct v4l2_subdev *sd,
			unsigned int type, u8 flags, int line,
			const int fields)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std = decoder->norm;
	u8 reg;
	int i, pos = 0;

	if (std == V4L2_STD_ALL) {
		dev_err(sd->dev, "VBI can't be configured without knowing number of lines\n");
		return 0;
	} else if (std & V4L2_STD_625_50) {
		/* Don't follow NTSC Line number convension */
		line += 3;
	}

	if (line < 6 || line > 27)
		return 0;

	for (i = 0; i < ARRAY_SIZE(vbi_ram_default); i++) {
		const struct i2c_vbi_ram_value *regs =  &vbi_ram_default[i];

		if (!regs->type.vbi_type)
			continue;

		if ((type & regs->type.vbi_type) &&
		    (line >= regs->type.ini_line) &&
		    (line <= regs->type.end_line))
			break;
		pos++;
	}

	type = pos | (flags & 0xf0);
	reg = ((line - 6) << 1) + TVP5150_LINE_MODE_INI;

	if (fields & 1)
		regmap_write(decoder->regmap, reg, type);

	if (fields & 2)
		regmap_write(decoder->regmap, reg + 1, type);

	return type;
}

static int tvp5150_get_vbi(struct v4l2_subdev *sd, int line)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std = decoder->norm;
	u8 reg;
	int pos, type = 0;
	int i, ret = 0;

	if (std == V4L2_STD_ALL) {
		dev_err(sd->dev, "VBI can't be configured without knowing number of lines\n");
		return 0;
	} else if (std & V4L2_STD_625_50) {
		/* Don't follow NTSC Line number convension */
		line += 3;
	}

	if (line < 6 || line > 27)
		return 0;

	reg = ((line - 6) << 1) + TVP5150_LINE_MODE_INI;

	for (i = 0; i <= 1; i++) {
		ret = tvp5150_read(sd, reg + i);
		if (ret < 0) {
			dev_err(sd->dev, "%s: failed with error = %d\n",
				 __func__, ret);
			return 0;
		}
		pos = ret & 0x0f;
		if (pos < ARRAY_SIZE(vbi_ram_default))
			type |= vbi_ram_default[pos].type.vbi_type;
	}

	return type;
}

static int tvp5150_set_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	int fmt = 0;

	/* First tests should be against specific std */

	if (std == V4L2_STD_NTSC_443) {
		fmt = VIDEO_STD_NTSC_4_43_BIT;
	} else if (std == V4L2_STD_PAL_M) {
		fmt = VIDEO_STD_PAL_M_BIT;
	} else if (std == V4L2_STD_PAL_N || std == V4L2_STD_PAL_Nc) {
		fmt = VIDEO_STD_PAL_COMBINATION_N_BIT;
	} else {
		/* Then, test against generic ones */
		if (std & V4L2_STD_NTSC)
			fmt = VIDEO_STD_NTSC_MJ_BIT;
		else if (std & V4L2_STD_PAL)
			fmt = VIDEO_STD_PAL_BDGHIN_BIT;
		else if (std & V4L2_STD_SECAM)
			fmt = VIDEO_STD_SECAM_BIT;
	}

	dev_dbg_lvl(sd->dev, 1, debug, "Set video std register to %d.\n", fmt);
	regmap_write(decoder->regmap, TVP5150_VIDEO_STD, fmt);
	return 0;
}

static int tvp5150_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	*std = decoder->norm;

	return 0;
}

static int tvp5150_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct tvp5150_connector *cur_con = decoder->cur_connector;
	v4l2_std_id supported_stds;

	if (decoder->norm == std)
		return 0;

	/* In case of no of-connectors are available no limitations are made */
	if (!decoder->connectors_num)
		supported_stds = V4L2_STD_ALL;
	else
		supported_stds = cur_con->base.connector.analog.sdtv_stds;

	/*
	 * Check if requested std or group of std's is/are supported by the
	 * connector.
	 */
	if ((supported_stds & std) == 0)
		return -EINVAL;

	/* Change cropping height limits */
	if (std & V4L2_STD_525_60)
		decoder->rect.height = TVP5150_V_MAX_525_60;
	else
		decoder->rect.height = TVP5150_V_MAX_OTHERS;

	/* Set only the specific supported std in case of group of std's. */
	decoder->norm = supported_stds & std;

	return tvp5150_set_std(sd, std);
}

static v4l2_std_id tvp5150_read_std(struct v4l2_subdev *sd)
{
	int val = tvp5150_read(sd, TVP5150_STATUS_REG_5);

	switch (val & 0x0F) {
	case 0x01:
		return V4L2_STD_NTSC;
	case 0x03:
		return V4L2_STD_PAL;
	case 0x05:
		return V4L2_STD_PAL_M;
	case 0x07:
		return V4L2_STD_PAL_N | V4L2_STD_PAL_Nc;
	case 0x09:
		return V4L2_STD_NTSC_443;
	case 0xb:
		return V4L2_STD_SECAM;
	default:
		return V4L2_STD_UNKNOWN;
	}
}

static int query_lock(struct v4l2_subdev *sd)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	int status;

	if (decoder->irq)
		return decoder->lock;

	regmap_read(decoder->regmap, TVP5150_STATUS_REG_1, &status);

	/* For standard detection, we need the 3 locks */
	return (status & 0x0e) == 0x0e;
}

static int tvp5150_querystd(struct v4l2_subdev *sd, v4l2_std_id *std_id)
{
	*std_id = query_lock(sd) ? tvp5150_read_std(sd) : V4L2_STD_UNKNOWN;

	return 0;
}

static const struct v4l2_event tvp5150_ev_fmt = {
	.type = V4L2_EVENT_SOURCE_CHANGE,
	.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
};

static irqreturn_t tvp5150_isr(int irq, void *dev_id)
{
	struct tvp5150 *decoder = dev_id;
	struct regmap *map = decoder->regmap;
	unsigned int mask, active = 0, status = 0;

	mask = TVP5150_MISC_CTL_YCBCR_OE | TVP5150_MISC_CTL_SYNC_OE |
	       TVP5150_MISC_CTL_CLOCK_OE;

	regmap_read(map, TVP5150_INT_STATUS_REG_A, &status);
	if (status) {
		regmap_write(map, TVP5150_INT_STATUS_REG_A, status);

		if (status & TVP5150_INT_A_LOCK) {
			decoder->lock = !!(status & TVP5150_INT_A_LOCK_STATUS);
			dev_dbg_lvl(decoder->sd.dev, 1, debug,
				    "sync lo%s signal\n",
				    decoder->lock ? "ck" : "ss");
			v4l2_subdev_notify_event(&decoder->sd, &tvp5150_ev_fmt);
			regmap_update_bits(map, TVP5150_MISC_CTL, mask,
					   decoder->lock ? decoder->oe : 0);
		}

		return IRQ_HANDLED;
	}

	regmap_read(map, TVP5150_INT_ACTIVE_REG_B, &active);
	if (active) {
		status = 0;
		regmap_read(map, TVP5150_INT_STATUS_REG_B, &status);
		if (status)
			regmap_write(map, TVP5150_INT_RESET_REG_B, status);
	}

	return IRQ_HANDLED;
}

static int tvp5150_reset(struct v4l2_subdev *sd, u32 val)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct regmap *map = decoder->regmap;

	/* Initializes TVP5150 to its default values */
	tvp5150_write_inittab(sd, tvp5150_init_default);

	if (decoder->irq) {
		/* Configure pins: FID, VSYNC, INTREQ, SCLK */
		regmap_write(map, TVP5150_CONF_SHARED_PIN, 0x0);
		/* Set interrupt polarity to active high */
		regmap_write(map, TVP5150_INT_CONF, TVP5150_VDPOE | 0x1);
		regmap_write(map, TVP5150_INTT_CONFIG_REG_B, 0x1);
	} else {
		/* Configure pins: FID, VSYNC, GPCL/VBLK, SCLK */
		regmap_write(map, TVP5150_CONF_SHARED_PIN, 0x2);
		/* Keep interrupt polarity active low */
		regmap_write(map, TVP5150_INT_CONF, TVP5150_VDPOE);
		regmap_write(map, TVP5150_INTT_CONFIG_REG_B, 0x0);
	}

	/* Initializes VDP registers */
	tvp5150_vdp_init(sd);

	/* Selects decoder input */
	tvp5150_selmux(sd);

	/* Initialize image preferences */
	v4l2_ctrl_handler_setup(&decoder->hdl);

	return 0;
}

static int tvp5150_enable(struct v4l2_subdev *sd)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std;

	/* Initializes TVP5150 to stream enabled values */
	tvp5150_write_inittab(sd, tvp5150_init_enable);

	if (decoder->norm == V4L2_STD_ALL)
		std = tvp5150_read_std(sd);
	else
		std = decoder->norm;

	/* Disable autoswitch mode */
	tvp5150_set_std(sd, std);

	/*
	 * Enable the YCbCr and clock outputs. In discrete sync mode
	 * (non-BT.656) additionally enable the sync outputs.
	 */
	switch (decoder->mbus_type) {
	case V4L2_MBUS_PARALLEL:
		/* 8-bit 4:2:2 YUV with discrete sync output */
		regmap_update_bits(decoder->regmap, TVP5150_DATA_RATE_SEL,
				   0x7, 0x0);
		decoder->oe = TVP5150_MISC_CTL_YCBCR_OE |
			      TVP5150_MISC_CTL_CLOCK_OE |
			      TVP5150_MISC_CTL_SYNC_OE;
		break;
	case V4L2_MBUS_BT656:
		decoder->oe = TVP5150_MISC_CTL_YCBCR_OE |
			      TVP5150_MISC_CTL_CLOCK_OE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
};

static int tvp5150_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tvp5150 *decoder = to_tvp5150(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		regmap_write(decoder->regmap, TVP5150_BRIGHT_CTL, ctrl->val);
		return 0;
	case V4L2_CID_CONTRAST:
		regmap_write(decoder->regmap, TVP5150_CONTRAST_CTL, ctrl->val);
		return 0;
	case V4L2_CID_SATURATION:
		regmap_write(decoder->regmap, TVP5150_SATURATION_CTL,
			     ctrl->val);
		return 0;
	case V4L2_CID_HUE:
		regmap_write(decoder->regmap, TVP5150_HUE_CTL, ctrl->val);
		return 0;
	case V4L2_CID_TEST_PATTERN:
		decoder->enable = ctrl->val ? false : true;
		tvp5150_selmux(sd);
		return 0;
	}
	return -EINVAL;
}

static void tvp5150_set_default(v4l2_std_id std, struct v4l2_rect *crop)
{
	/* Default is no cropping */
	crop->top = 0;
	crop->left = 0;
	crop->width = TVP5150_H_MAX;
	if (std & V4L2_STD_525_60)
		crop->height = TVP5150_V_MAX_525_60;
	else
		crop->height = TVP5150_V_MAX_OTHERS;
}

static struct v4l2_rect *
tvp5150_get_pad_crop(struct tvp5150 *decoder,
		     struct v4l2_subdev_state *sd_state, unsigned int pad,
		     enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &decoder->rect;
	case V4L2_SUBDEV_FORMAT_TRY:
#if defined(CONFIG_VIDEO_V4L2_SUBDEV_API)
		return v4l2_subdev_get_try_crop(&decoder->sd, sd_state, pad);
#else
		return ERR_PTR(-EINVAL);
#endif
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int tvp5150_fill_fmt(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *f;
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (!format || (format->pad != TVP5150_PAD_VID_OUT))
		return -EINVAL;

	f = &format->format;

	f->width = decoder->rect.width;
	f->height = decoder->rect.height / 2;

	f->code = TVP5150_MBUS_FMT;
	f->field = TVP5150_FIELD;
	f->colorspace = TVP5150_COLORSPACE;

	dev_dbg_lvl(sd->dev, 1, debug, "width = %d, height = %d\n", f->width,
		    f->height);
	return 0;
}

static unsigned int tvp5150_get_hmax(struct v4l2_subdev *sd)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std;

	/* Calculate height based on current standard */
	if (decoder->norm == V4L2_STD_ALL)
		std = tvp5150_read_std(sd);
	else
		std = decoder->norm;

	return (std & V4L2_STD_525_60) ?
		TVP5150_V_MAX_525_60 : TVP5150_V_MAX_OTHERS;
}

static void tvp5150_set_hw_selection(struct v4l2_subdev *sd,
				     struct v4l2_rect *rect)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	unsigned int hmax = tvp5150_get_hmax(sd);

	regmap_write(decoder->regmap, TVP5150_VERT_BLANKING_START, rect->top);
	regmap_write(decoder->regmap, TVP5150_VERT_BLANKING_STOP,
		     rect->top + rect->height - hmax);
	regmap_write(decoder->regmap, TVP5150_ACT_VD_CROP_ST_MSB,
		     rect->left >> TVP5150_CROP_SHIFT);
	regmap_write(decoder->regmap, TVP5150_ACT_VD_CROP_ST_LSB,
		     rect->left | (1 << TVP5150_CROP_SHIFT));
	regmap_write(decoder->regmap, TVP5150_ACT_VD_CROP_STP_MSB,
		     (rect->left + rect->width - TVP5150_MAX_CROP_LEFT) >>
		     TVP5150_CROP_SHIFT);
	regmap_write(decoder->regmap, TVP5150_ACT_VD_CROP_STP_LSB,
		     rect->left + rect->width - TVP5150_MAX_CROP_LEFT);
}

static int tvp5150_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct v4l2_rect *rect = &sel->r;
	struct v4l2_rect *crop;
	unsigned int hmax;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg_lvl(sd->dev, 1, debug, "%s left=%d, top=%d, width=%d, height=%d\n",
		__func__, rect->left, rect->top, rect->width, rect->height);

	/* tvp5150 has some special limits */
	rect->left = clamp(rect->left, 0, TVP5150_MAX_CROP_LEFT);
	rect->top = clamp(rect->top, 0, TVP5150_MAX_CROP_TOP);
	hmax = tvp5150_get_hmax(sd);

	/*
	 * alignments:
	 *  - width = 2 due to UYVY colorspace
	 *  - height, image = no special alignment
	 */
	v4l_bound_align_image(&rect->width,
			      TVP5150_H_MAX - TVP5150_MAX_CROP_LEFT - rect->left,
			      TVP5150_H_MAX - rect->left, 1, &rect->height,
			      hmax - TVP5150_MAX_CROP_TOP - rect->top,
			      hmax - rect->top, 0, 0);

	if (!IS_ENABLED(CONFIG_VIDEO_V4L2_SUBDEV_API) &&
	    sel->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	crop = tvp5150_get_pad_crop(decoder, sd_state, sel->pad, sel->which);
	if (IS_ERR(crop))
		return PTR_ERR(crop);

	/*
	 * Update output image size if the selection (crop) rectangle size or
	 * position has been modified.
	 */
	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    !v4l2_rect_equal(rect, crop))
		tvp5150_set_hw_selection(sd, rect);

	*crop = *rect;

	return 0;
}

static int tvp5150_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct tvp5150 *decoder = container_of(sd, struct tvp5150, sd);
	struct v4l2_rect *crop;
	v4l2_std_id std;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = TVP5150_H_MAX;

		/* Calculate height based on current standard */
		if (decoder->norm == V4L2_STD_ALL)
			std = tvp5150_read_std(sd);
		else
			std = decoder->norm;
		if (std & V4L2_STD_525_60)
			sel->r.height = TVP5150_V_MAX_525_60;
		else
			sel->r.height = TVP5150_V_MAX_OTHERS;
		return 0;
	case V4L2_SEL_TGT_CROP:
		crop = tvp5150_get_pad_crop(decoder, sd_state, sel->pad,
					    sel->which);
		if (IS_ERR(crop))
			return PTR_ERR(crop);
		sel->r = *crop;
		return 0;
	default:
		return -EINVAL;
	}
}

static int tvp5150_get_mbus_config(struct v4l2_subdev *sd,
				   unsigned int pad,
				   struct v4l2_mbus_config *cfg)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	cfg->type = decoder->mbus_type;
	cfg->bus.parallel.flags = V4L2_MBUS_MASTER
				| V4L2_MBUS_PCLK_SAMPLE_RISING
				| V4L2_MBUS_FIELD_EVEN_LOW
				| V4L2_MBUS_DATA_ACTIVE_HIGH;

	return 0;
}

/****************************************************************************
			V4L2 subdev pad ops
 ****************************************************************************/
static int tvp5150_init_cfg(struct v4l2_subdev *sd,
			    struct v4l2_subdev_state *sd_state)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std;

	/*
	 * Reset selection to maximum on subdev_open() if autodetection is on
	 * and a standard change is detected.
	 */
	if (decoder->norm == V4L2_STD_ALL) {
		std = tvp5150_read_std(sd);
		if (std != decoder->detected_norm) {
			decoder->detected_norm = std;
			tvp5150_set_default(std, &decoder->rect);
		}
	}

	return 0;
}

static int tvp5150_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;

	code->code = TVP5150_MBUS_FMT;
	return 0;
}

static int tvp5150_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (fse->index >= 8 || fse->code != TVP5150_MBUS_FMT)
		return -EINVAL;

	fse->code = TVP5150_MBUS_FMT;
	fse->min_width = decoder->rect.width;
	fse->max_width = decoder->rect.width;
	fse->min_height = decoder->rect.height / 2;
	fse->max_height = decoder->rect.height / 2;

	return 0;
}

/****************************************************************************
 *			Media entity ops
 ****************************************************************************/
#if defined(CONFIG_MEDIA_CONTROLLER)
static int tvp5150_set_link(struct media_pad *connector_pad,
			    struct media_pad *tvp5150_pad, u32 flags)
{
	struct media_link *link;

	link = media_entity_find_link(connector_pad, tvp5150_pad);
	if (!link)
		return -EINVAL;

	link->flags = flags;
	link->reverse->flags = link->flags;

	return 0;
}

static int tvp5150_disable_all_input_links(struct tvp5150 *decoder)
{
	struct media_pad *connector_pad;
	unsigned int i;
	int err;

	for (i = 0; i < TVP5150_NUM_PADS - 1; i++) {
		connector_pad = media_pad_remote_pad_first(&decoder->pads[i]);
		if (!connector_pad)
			continue;

		err = tvp5150_set_link(connector_pad, &decoder->pads[i], 0);
		if (err)
			return err;
	}

	return 0;
}

static int tvp5150_s_routing(struct v4l2_subdev *sd, u32 input, u32 output,
			     u32 config);

static int tvp5150_link_setup(struct media_entity *entity,
			      const struct media_pad *tvp5150_pad,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct media_pad *other_tvp5150_pad =
		&decoder->pads[tvp5150_pad->index ^ 1];
	struct v4l2_fwnode_connector *v4l2c;
	bool is_svideo = false;
	unsigned int i;
	int err;

	/*
	 * The TVP5150 state is determined by the enabled sink pad link(s).
	 * Enabling or disabling the source pad link has no effect.
	 */
	if (tvp5150_pad->flags & MEDIA_PAD_FL_SOURCE)
		return 0;

	/* Check if the svideo connector should be enabled */
	for (i = 0; i < decoder->connectors_num; i++) {
		if (remote->entity == &decoder->connectors[i].ent) {
			v4l2c = &decoder->connectors[i].base;
			is_svideo = v4l2c->type == V4L2_CONN_SVIDEO;
			break;
		}
	}

	dev_dbg_lvl(sd->dev, 1, debug, "link setup '%s':%d->'%s':%d[%d]",
		    remote->entity->name, remote->index,
		    tvp5150_pad->entity->name, tvp5150_pad->index,
		    flags & MEDIA_LNK_FL_ENABLED);
	if (is_svideo)
		dev_dbg_lvl(sd->dev, 1, debug,
			    "link setup '%s':%d->'%s':%d[%d]",
			    remote->entity->name, remote->index,
			    other_tvp5150_pad->entity->name,
			    other_tvp5150_pad->index,
			    flags & MEDIA_LNK_FL_ENABLED);

	/*
	 * The TVP5150 has an internal mux which allows the following setup:
	 *
	 * comp-connector1  --\
	 *		       |---> AIP1A
	 *		      /
	 * svideo-connector -|
	 *		      \
	 *		       |---> AIP1B
	 * comp-connector2  --/
	 *
	 * We can't rely on user space that the current connector gets disabled
	 * first before enabling the new connector. Disable all active
	 * connector links to be on the safe side.
	 */
	err = tvp5150_disable_all_input_links(decoder);
	if (err)
		return err;

	tvp5150_s_routing(sd, is_svideo ? TVP5150_SVIDEO : tvp5150_pad->index,
			  flags & MEDIA_LNK_FL_ENABLED ? TVP5150_NORMAL :
			  TVP5150_BLACK_SCREEN, 0);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		struct v4l2_fwnode_connector_analog *v4l2ca;
		u32 new_norm;

		/*
		 * S-Video connector is conneted to both ports AIP1A and AIP1B.
		 * Both links must be enabled in one-shot regardless which link
		 * the user requests.
		 */
		if (is_svideo) {
			err = tvp5150_set_link((struct media_pad *)remote,
					       other_tvp5150_pad, flags);
			if (err)
				return err;
		}

		if (!decoder->connectors_num)
			return 0;

		/* Update the current connector */
		decoder->cur_connector =
			container_of(remote, struct tvp5150_connector, pad);

		/*
		 * Do nothing if the new connector supports the same tv-norms as
		 * the old one.
		 */
		v4l2ca = &decoder->cur_connector->base.connector.analog;
		new_norm = decoder->norm & v4l2ca->sdtv_stds;
		if (decoder->norm == new_norm)
			return 0;

		/*
		 * Fallback to the new connector tv-norms if we can't find any
		 * common between the current tv-norm and the new one.
		 */
		tvp5150_s_std(sd, new_norm ? new_norm : v4l2ca->sdtv_stds);
	}

	return 0;
}

static const struct media_entity_operations tvp5150_sd_media_ops = {
	.link_setup = tvp5150_link_setup,
};
#endif
/****************************************************************************
			I2C Command
 ****************************************************************************/
static int __maybe_unused tvp5150_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (decoder->irq)
		/* Disable lock interrupt */
		return regmap_update_bits(decoder->regmap,
					  TVP5150_INT_ENABLE_REG_A,
					  TVP5150_INT_A_LOCK, 0);
	return 0;
}

static int __maybe_unused tvp5150_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (decoder->irq)
		/* Enable lock interrupt */
		return regmap_update_bits(decoder->regmap,
					  TVP5150_INT_ENABLE_REG_A,
					  TVP5150_INT_A_LOCK,
					  TVP5150_INT_A_LOCK);
	return 0;
}

static int tvp5150_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	unsigned int mask, val = 0;
	int ret;

	mask = TVP5150_MISC_CTL_YCBCR_OE | TVP5150_MISC_CTL_SYNC_OE |
	       TVP5150_MISC_CTL_CLOCK_OE;

	if (enable) {
		ret = pm_runtime_resume_and_get(sd->dev);
		if (ret < 0)
			return ret;

		tvp5150_enable(sd);

		/* Enable outputs if decoder is locked */
		if (decoder->irq)
			val = decoder->lock ? decoder->oe : 0;
		else
			val = decoder->oe;

		v4l2_subdev_notify_event(&decoder->sd, &tvp5150_ev_fmt);
	} else {
		pm_runtime_put(sd->dev);
	}

	regmap_update_bits(decoder->regmap, TVP5150_MISC_CTL, mask, val);

	return 0;
}

static int tvp5150_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	decoder->input = input;
	decoder->output = output;

	if (output == TVP5150_BLACK_SCREEN)
		decoder->enable = false;
	else
		decoder->enable = true;

	tvp5150_selmux(sd);
	return 0;
}

static int tvp5150_s_raw_fmt(struct v4l2_subdev *sd, struct v4l2_vbi_format *fmt)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	/*
	 * this is for capturing 36 raw vbi lines
	 * if there's a way to cut off the beginning 2 vbi lines
	 * with the tvp5150 then the vbi line count could be lowered
	 * to 17 lines/field again, although I couldn't find a register
	 * which could do that cropping
	 */

	if (fmt->sample_format == V4L2_PIX_FMT_GREY)
		regmap_write(decoder->regmap, TVP5150_LUMA_PROC_CTL_1, 0x70);
	if (fmt->count[0] == 18 && fmt->count[1] == 18) {
		regmap_write(decoder->regmap, TVP5150_VERT_BLANKING_START,
			     0x00);
		regmap_write(decoder->regmap, TVP5150_VERT_BLANKING_STOP, 0x01);
	}
	return 0;
}

static int tvp5150_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	int i;

	if (svbi->service_set != 0) {
		for (i = 0; i <= 23; i++) {
			svbi->service_lines[1][i] = 0;
			svbi->service_lines[0][i] =
				tvp5150_set_vbi(sd, svbi->service_lines[0][i],
						0xf0, i, 3);
		}
		/* Enables FIFO */
		regmap_write(decoder->regmap, TVP5150_FIFO_OUT_CTRL, 1);
	} else {
		/* Disables FIFO*/
		regmap_write(decoder->regmap, TVP5150_FIFO_OUT_CTRL, 0);

		/* Disable Full Field */
		regmap_write(decoder->regmap, TVP5150_FULL_FIELD_ENA, 0);

		/* Disable Line modes */
		for (i = TVP5150_LINE_MODE_INI; i <= TVP5150_LINE_MODE_END; i++)
			regmap_write(decoder->regmap, i, 0xff);
	}
	return 0;
}

static int tvp5150_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	int i, mask = 0;

	memset(svbi->service_lines, 0, sizeof(svbi->service_lines));

	for (i = 0; i <= 23; i++) {
		svbi->service_lines[0][i] =
			tvp5150_get_vbi(sd, i);
		mask |= svbi->service_lines[0][i];
	}
	svbi->service_set = mask;
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int tvp5150_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	int res;

	res = tvp5150_read(sd, reg->reg & 0xff);
	if (res < 0) {
		dev_err(sd->dev, "%s: failed with error = %d\n", __func__, res);
		return res;
	}

	reg->val = res;
	reg->size = 1;
	return 0;
}

static int tvp5150_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	return regmap_write(decoder->regmap, reg->reg & 0xff, reg->val & 0xff);
}
#endif

static int tvp5150_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static int tvp5150_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	int status = tvp5150_read(sd, 0x88);

	vt->signal = ((status & 0x04) && (status & 0x02)) ? 0xffff : 0x0;
	return 0;
}

static int tvp5150_registered(struct v4l2_subdev *sd)
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct tvp5150 *decoder = to_tvp5150(sd);
	unsigned int i;
	int ret;

	/*
	 * Setup connector pads and links. Enable the link to the first
	 * available connector per default.
	 */
	for (i = 0; i < decoder->connectors_num; i++) {
		struct media_entity *con = &decoder->connectors[i].ent;
		struct media_pad *pad = &decoder->connectors[i].pad;
		struct v4l2_fwnode_connector *v4l2c =
			&decoder->connectors[i].base;
		struct v4l2_connector_link *link =
			v4l2_connector_first_link(v4l2c);
		unsigned int port = link->fwnode_link.remote_port;
		unsigned int flags = i ? 0 : MEDIA_LNK_FL_ENABLED;
		bool is_svideo = v4l2c->type == V4L2_CONN_SVIDEO;

		pad->flags = MEDIA_PAD_FL_SOURCE;
		ret = media_entity_pads_init(con, 1, pad);
		if (ret < 0)
			goto err;

		ret = media_device_register_entity(sd->v4l2_dev->mdev, con);
		if (ret < 0)
			goto err;

		ret = media_create_pad_link(con, 0, &sd->entity, port, flags);
		if (ret < 0)
			goto err;

		if (is_svideo) {
			/*
			 * Check tvp5150_link_setup() comments for more
			 * information.
			 */
			link = v4l2_connector_last_link(v4l2c);
			port = link->fwnode_link.remote_port;
			ret = media_create_pad_link(con, 0, &sd->entity, port,
						    flags);
			if (ret < 0)
				goto err;
		}

		/* Enable default input. */
		if (flags == MEDIA_LNK_FL_ENABLED) {
			decoder->input =
				is_svideo ? TVP5150_SVIDEO :
				port == 0 ? TVP5150_COMPOSITE0 :
				TVP5150_COMPOSITE1;

			tvp5150_selmux(sd);
			decoder->cur_connector = &decoder->connectors[i];
			tvp5150_s_std(sd, v4l2c->connector.analog.sdtv_stds);
		}
	}

	return 0;

err:
	for (i = 0; i < decoder->connectors_num; i++) {
		media_device_unregister_entity(&decoder->connectors[i].ent);
		media_entity_cleanup(&decoder->connectors[i].ent);
	}
	return ret;
#endif

	return 0;
}

static int tvp5150_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return pm_runtime_resume_and_get(sd->dev);
}

static int tvp5150_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	pm_runtime_put(sd->dev);

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops tvp5150_ctrl_ops = {
	.s_ctrl = tvp5150_s_ctrl,
};

static const struct v4l2_subdev_core_ops tvp5150_core_ops = {
	.log_status = tvp5150_log_status,
	.reset = tvp5150_reset,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = tvp5150_g_register,
	.s_register = tvp5150_s_register,
#endif
	.subscribe_event = tvp5150_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_tuner_ops tvp5150_tuner_ops = {
	.g_tuner = tvp5150_g_tuner,
};

static const struct v4l2_subdev_video_ops tvp5150_video_ops = {
	.s_std = tvp5150_s_std,
	.g_std = tvp5150_g_std,
	.querystd = tvp5150_querystd,
	.s_stream = tvp5150_s_stream,
	.s_routing = tvp5150_s_routing,
};

static const struct v4l2_subdev_vbi_ops tvp5150_vbi_ops = {
	.g_sliced_vbi_cap = tvp5150_g_sliced_vbi_cap,
	.g_sliced_fmt = tvp5150_g_sliced_fmt,
	.s_sliced_fmt = tvp5150_s_sliced_fmt,
	.s_raw_fmt = tvp5150_s_raw_fmt,
};

static const struct v4l2_subdev_pad_ops tvp5150_pad_ops = {
	.init_cfg = tvp5150_init_cfg,
	.enum_mbus_code = tvp5150_enum_mbus_code,
	.enum_frame_size = tvp5150_enum_frame_size,
	.set_fmt = tvp5150_fill_fmt,
	.get_fmt = tvp5150_fill_fmt,
	.get_selection = tvp5150_get_selection,
	.set_selection = tvp5150_set_selection,
	.get_mbus_config = tvp5150_get_mbus_config,
};

static const struct v4l2_subdev_ops tvp5150_ops = {
	.core = &tvp5150_core_ops,
	.tuner = &tvp5150_tuner_ops,
	.video = &tvp5150_video_ops,
	.vbi = &tvp5150_vbi_ops,
	.pad = &tvp5150_pad_ops,
};

static const struct v4l2_subdev_internal_ops tvp5150_internal_ops = {
	.registered = tvp5150_registered,
	.open = tvp5150_open,
	.close = tvp5150_close,
};

/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/

static const struct regmap_range tvp5150_readable_ranges[] = {
	{
		.range_min = TVP5150_VD_IN_SRC_SEL_1,
		.range_max = TVP5150_AUTOSW_MSK,
	}, {
		.range_min = TVP5150_COLOR_KIL_THSH_CTL,
		.range_max = TVP5150_CONF_SHARED_PIN,
	}, {
		.range_min = TVP5150_ACT_VD_CROP_ST_MSB,
		.range_max = TVP5150_HORIZ_SYNC_START,
	}, {
		.range_min = TVP5150_VERT_BLANKING_START,
		.range_max = TVP5150_INTT_CONFIG_REG_B,
	}, {
		.range_min = TVP5150_VIDEO_STD,
		.range_max = TVP5150_VIDEO_STD,
	}, {
		.range_min = TVP5150_CB_GAIN_FACT,
		.range_max = TVP5150_REV_SELECT,
	}, {
		.range_min = TVP5150_MSB_DEV_ID,
		.range_max = TVP5150_STATUS_REG_5,
	}, {
		.range_min = TVP5150_CC_DATA_INI,
		.range_max = TVP5150_TELETEXT_FIL_ENA,
	}, {
		.range_min = TVP5150_INT_STATUS_REG_A,
		.range_max = TVP5150_FIFO_OUT_CTRL,
	}, {
		.range_min = TVP5150_FULL_FIELD_ENA,
		.range_max = TVP5150_FULL_FIELD_MODE_REG,
	},
};

static bool tvp5150_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TVP5150_VERT_LN_COUNT_MSB:
	case TVP5150_VERT_LN_COUNT_LSB:
	case TVP5150_INT_STATUS_REG_A:
	case TVP5150_INT_STATUS_REG_B:
	case TVP5150_INT_ACTIVE_REG_B:
	case TVP5150_STATUS_REG_1:
	case TVP5150_STATUS_REG_2:
	case TVP5150_STATUS_REG_3:
	case TVP5150_STATUS_REG_4:
	case TVP5150_STATUS_REG_5:
	/* CC, WSS, VPS, VITC data? */
	case TVP5150_VBI_FIFO_READ_DATA:
	case TVP5150_VDP_STATUS_REG:
	case TVP5150_FIFO_WORD_COUNT:
		return true;
	default:
		return false;
	}
}

static const struct regmap_access_table tvp5150_readable_table = {
	.yes_ranges = tvp5150_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(tvp5150_readable_ranges),
};

static struct regmap_config tvp5150_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,

	.cache_type = REGCACHE_RBTREE,

	.rd_table = &tvp5150_readable_table,
	.volatile_reg = tvp5150_volatile_reg,
};

static int tvp5150_detect_version(struct tvp5150 *core)
{
	struct v4l2_subdev *sd = &core->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	u8 regs[4];
	int res;

	/*
	 * Read consequent registers - TVP5150_MSB_DEV_ID, TVP5150_LSB_DEV_ID,
	 * TVP5150_ROM_MAJOR_VER, TVP5150_ROM_MINOR_VER
	 */
	res = regmap_bulk_read(core->regmap, TVP5150_MSB_DEV_ID, regs, 4);
	if (res < 0) {
		dev_err(&c->dev, "reading ID registers failed: %d\n", res);
		return res;
	}

	core->dev_id = (regs[0] << 8) | regs[1];
	core->rom_ver = (regs[2] << 8) | regs[3];

	dev_info(sd->dev, "tvp%04x (%u.%u) chip found @ 0x%02x (%s)\n",
		  core->dev_id, regs[2], regs[3], c->addr << 1,
		  c->adapter->name);

	if (core->dev_id == 0x5150 && core->rom_ver == 0x0321) {
		dev_info(sd->dev, "tvp5150a detected.\n");
	} else if (core->dev_id == 0x5150 && core->rom_ver == 0x0400) {
		dev_info(sd->dev, "tvp5150am1 detected.\n");

		/* ITU-T BT.656.4 timing */
		regmap_write(core->regmap, TVP5150_REV_SELECT, 0);
	} else if (core->dev_id == 0x5151 && core->rom_ver == 0x0100) {
		dev_info(sd->dev, "tvp5151 detected.\n");
	} else {
		dev_info(sd->dev, "*** unknown tvp%04x chip detected.\n",
			  core->dev_id);
	}

	return 0;
}

static int tvp5150_init(struct i2c_client *c)
{
	struct gpio_desc *pdn_gpio;
	struct gpio_desc *reset_gpio;

	pdn_gpio = devm_gpiod_get_optional(&c->dev, "pdn", GPIOD_OUT_HIGH);
	if (IS_ERR(pdn_gpio))
		return PTR_ERR(pdn_gpio);

	if (pdn_gpio) {
		gpiod_set_value_cansleep(pdn_gpio, 0);
		/* Delay time between power supplies active and reset */
		msleep(20);
	}

	reset_gpio = devm_gpiod_get_optional(&c->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	if (reset_gpio) {
		/* RESETB pulse duration */
		ndelay(500);
		gpiod_set_value_cansleep(reset_gpio, 0);
		/* Delay time between end of reset to I2C active */
		usleep_range(200, 250);
	}

	return 0;
}

#if defined(CONFIG_MEDIA_CONTROLLER)
static int tvp5150_mc_init(struct tvp5150 *decoder)
{
	struct v4l2_subdev *sd = &decoder->sd;
	unsigned int i;

	sd->entity.ops = &tvp5150_sd_media_ops;
	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;

	for (i = 0; i < TVP5150_NUM_PADS - 1; i++) {
		decoder->pads[i].flags = MEDIA_PAD_FL_SINK;
		decoder->pads[i].sig_type = PAD_SIGNAL_ANALOG;
	}

	decoder->pads[i].flags = MEDIA_PAD_FL_SOURCE;
	decoder->pads[i].sig_type = PAD_SIGNAL_DV;

	return media_entity_pads_init(&sd->entity, TVP5150_NUM_PADS,
				      decoder->pads);
}

#else /* !defined(CONFIG_MEDIA_CONTROLLER) */

static inline int tvp5150_mc_init(struct tvp5150 *decoder)
{
	return 0;
}
#endif /* defined(CONFIG_MEDIA_CONTROLLER) */

static int tvp5150_validate_connectors(struct tvp5150 *decoder)
{
	struct device *dev = decoder->sd.dev;
	struct tvp5150_connector *tvpc;
	struct v4l2_fwnode_connector *v4l2c;
	unsigned int i;

	if (!decoder->connectors_num) {
		dev_err(dev, "No valid connector found\n");
		return -ENODEV;
	}

	for (i = 0; i < decoder->connectors_num; i++) {
		struct v4l2_connector_link *link0 = NULL;
		struct v4l2_connector_link *link1;

		tvpc = &decoder->connectors[i];
		v4l2c = &tvpc->base;

		if (v4l2c->type == V4L2_CONN_COMPOSITE) {
			if (v4l2c->nr_of_links != 1) {
				dev_err(dev, "Composite: connector needs 1 link\n");
				return -EINVAL;
			}
			link0 = v4l2_connector_first_link(v4l2c);
			if (!link0) {
				dev_err(dev, "Composite: invalid first link\n");
				return -EINVAL;
			}
			if (link0->fwnode_link.remote_id == 1) {
				dev_err(dev, "Composite: invalid endpoint id\n");
				return -EINVAL;
			}
		}

		if (v4l2c->type == V4L2_CONN_SVIDEO) {
			if (v4l2c->nr_of_links != 2) {
				dev_err(dev, "SVideo: connector needs 2 links\n");
				return -EINVAL;
			}
			link0 = v4l2_connector_first_link(v4l2c);
			if (!link0) {
				dev_err(dev, "SVideo: invalid first link\n");
				return -EINVAL;
			}
			link1 = v4l2_connector_last_link(v4l2c);
			if (link0->fwnode_link.remote_port ==
			    link1->fwnode_link.remote_port) {
				dev_err(dev, "SVideo: invalid link setup\n");
				return -EINVAL;
			}
		}

		if (!(v4l2c->connector.analog.sdtv_stds & TVP5150_STD_MASK)) {
			dev_err(dev, "Unsupported tv-norm on connector %s\n",
				v4l2c->name);
			return -EINVAL;
		}
	}

	return 0;
}

static int tvp5150_parse_dt(struct tvp5150 *decoder, struct device_node *np)
{
	struct device *dev = decoder->sd.dev;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_UNKNOWN
	};
	struct device_node *ep_np;
	struct tvp5150_connector *tvpc;
	struct v4l2_fwnode_connector *v4l2c;
	unsigned int flags, ep_num;
	unsigned int i;
	int ret;

	/* At least 1 output and 1 input */
	ep_num = of_graph_get_endpoint_count(np);
	if (ep_num < 2 || ep_num > 5) {
		dev_err(dev, "At least 1 input and 1 output must be connected to the device.\n");
		return -EINVAL;
	}

	/* Layout if all connectors are used:
	 *
	 * tvp-5150 port@0 (AIP1A)
	 *	endpoint@0 -----------> Comp0-Con  port
	 *	endpoint@1 --------+--> Svideo-Con port
	 * tvp-5150 port@1 (AIP1B) |
	 *	endpoint@1 --------+
	 *	endpoint@0 -----------> Comp1-Con  port
	 * tvp-5150 port@2
	 *	endpoint (video bitstream output at YOUT[0-7] parallel bus)
	 */
	for_each_endpoint_of_node(np, ep_np) {
		struct fwnode_handle *ep_fwnode = of_fwnode_handle(ep_np);
		unsigned int next_connector = decoder->connectors_num;
		struct of_endpoint ep;

		of_graph_parse_endpoint(ep_np, &ep);
		if (ep.port > 1 || ep.id > 1) {
			dev_dbg(dev, "Ignore connector on port@%u/ep@%u\n",
				ep.port, ep.id);
			continue;
		}

		tvpc = &decoder->connectors[next_connector];
		v4l2c = &tvpc->base;

		if (ep.port == 0 || (ep.port == 1 && ep.id == 0)) {
			ret = v4l2_fwnode_connector_parse(ep_fwnode, v4l2c);
			if (ret)
				goto err_put;
			ret = v4l2_fwnode_connector_add_link(ep_fwnode, v4l2c);
			if (ret)
				goto err_put;
			decoder->connectors_num++;
		} else {
			/* Adding the 2nd svideo link */
			for (i = 0; i < TVP5150_MAX_CONNECTORS; i++) {
				tvpc = &decoder->connectors[i];
				v4l2c = &tvpc->base;
				if (v4l2c->type == V4L2_CONN_SVIDEO)
					break;
			}

			ret = v4l2_fwnode_connector_add_link(ep_fwnode, v4l2c);
			if (ret)
				goto err_put;
		}
	}

	ret = tvp5150_validate_connectors(decoder);
	if (ret)
		goto err_free;

	for (i = 0; i < decoder->connectors_num; i++) {
		tvpc = &decoder->connectors[i];
		v4l2c = &tvpc->base;
		tvpc->ent.flags = MEDIA_ENT_FL_CONNECTOR;
		tvpc->ent.function = v4l2c->type == V4L2_CONN_SVIDEO ?
			MEDIA_ENT_F_CONN_SVIDEO : MEDIA_ENT_F_CONN_COMPOSITE;
		tvpc->ent.name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
						v4l2c->name, v4l2c->label ?
						v4l2c->label : "");
	}

	ep_np = of_graph_get_endpoint_by_regs(np, TVP5150_PAD_VID_OUT, 0);
	if (!ep_np) {
		ret = -EINVAL;
		dev_err(dev, "Error no output endpoint available\n");
		goto err_free;
	}
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep_np), &bus_cfg);
	of_node_put(ep_np);
	if (ret)
		goto err_free;

	flags = bus_cfg.bus.parallel.flags;
	if (bus_cfg.bus_type == V4L2_MBUS_PARALLEL &&
	    !(flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH &&
	      flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH &&
	      flags & V4L2_MBUS_FIELD_EVEN_LOW)) {
		ret = -EINVAL;
		goto err_free;
	}

	decoder->mbus_type = bus_cfg.bus_type;

	return 0;

err_put:
	of_node_put(ep_np);
err_free:
	for (i = 0; i < TVP5150_MAX_CONNECTORS; i++)
		v4l2_fwnode_connector_free(&decoder->connectors[i].base);

	return ret;
}

static const char * const tvp5150_test_patterns[2] = {
	"Disabled",
	"Black screen"
};

static int tvp5150_probe(struct i2c_client *c)
{
	struct tvp5150 *core;
	struct v4l2_subdev *sd;
	struct device_node *np = c->dev.of_node;
	struct regmap *map;
	unsigned int i;
	int res;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(c->adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	res = tvp5150_init(c);
	if (res)
		return res;

	core = devm_kzalloc(&c->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	map = devm_regmap_init_i2c(c, &tvp5150_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	core->regmap = map;
	sd = &core->sd;
	v4l2_i2c_subdev_init(sd, c, &tvp5150_ops);
	sd->internal_ops = &tvp5150_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	if (IS_ENABLED(CONFIG_OF) && np) {
		res = tvp5150_parse_dt(core, np);
		if (res) {
			dev_err(sd->dev, "DT parsing error: %d\n", res);
			return res;
		}
	} else {
		/* Default to BT.656 embedded sync */
		core->mbus_type = V4L2_MBUS_BT656;
	}

	res = tvp5150_mc_init(core);
	if (res)
		return res;

	res = tvp5150_detect_version(core);
	if (res < 0)
		return res;

	/*
	 * Iterate over all available connectors in case they are supported and
	 * successfully parsed. Fallback to default autodetect in case they
	 * aren't supported.
	 */
	for (i = 0; i < core->connectors_num; i++) {
		struct v4l2_fwnode_connector *v4l2c;

		v4l2c = &core->connectors[i].base;
		core->norm |= v4l2c->connector.analog.sdtv_stds;
	}

	if (!core->connectors_num)
		core->norm = V4L2_STD_ALL;

	core->detected_norm = V4L2_STD_UNKNOWN;
	core->input = TVP5150_COMPOSITE1;
	core->enable = true;

	v4l2_ctrl_handler_init(&core->hdl, 5);
	v4l2_ctrl_new_std(&core->hdl, &tvp5150_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&core->hdl, &tvp5150_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&core->hdl, &tvp5150_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&core->hdl, &tvp5150_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	v4l2_ctrl_new_std(&core->hdl, &tvp5150_ctrl_ops,
			V4L2_CID_PIXEL_RATE, 27000000,
			27000000, 1, 27000000);
	v4l2_ctrl_new_std_menu_items(&core->hdl, &tvp5150_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(tvp5150_test_patterns) - 1,
				     0, 0, tvp5150_test_patterns);
	sd->ctrl_handler = &core->hdl;
	if (core->hdl.error) {
		res = core->hdl.error;
		goto err;
	}

	tvp5150_set_default(tvp5150_read_std(sd), &core->rect);

	core->irq = c->irq;
	tvp5150_reset(sd, 0);	/* Calls v4l2_ctrl_handler_setup() */
	if (c->irq) {
		res = devm_request_threaded_irq(&c->dev, c->irq, NULL,
						tvp5150_isr, IRQF_TRIGGER_HIGH |
						IRQF_ONESHOT, "tvp5150", core);
		if (res)
			goto err;
	}

	res = v4l2_async_register_subdev(sd);
	if (res < 0)
		goto err;

	if (debug > 1)
		tvp5150_log_status(sd);

	pm_runtime_set_active(&c->dev);
	pm_runtime_enable(&c->dev);
	pm_runtime_idle(&c->dev);

	return 0;

err:
	v4l2_ctrl_handler_free(&core->hdl);
	return res;
}

static void tvp5150_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);
	struct tvp5150 *decoder = to_tvp5150(sd);
	unsigned int i;

	dev_dbg_lvl(sd->dev, 1, debug,
		"tvp5150.c: removing tvp5150 adapter on address 0x%x\n",
		c->addr << 1);

	for (i = 0; i < decoder->connectors_num; i++)
		v4l2_fwnode_connector_free(&decoder->connectors[i].base);
	for (i = 0; i < decoder->connectors_num; i++) {
		media_device_unregister_entity(&decoder->connectors[i].ent);
		media_entity_cleanup(&decoder->connectors[i].ent);
	}
	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&decoder->hdl);
	pm_runtime_disable(&c->dev);
	pm_runtime_set_suspended(&c->dev);
}

/* ----------------------------------------------------------------------- */

static const struct dev_pm_ops tvp5150_pm_ops = {
	SET_RUNTIME_PM_OPS(tvp5150_runtime_suspend,
			   tvp5150_runtime_resume,
			   NULL)
};

static const struct i2c_device_id tvp5150_id[] = {
	{ "tvp5150", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tvp5150_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tvp5150_of_match[] = {
	{ .compatible = "ti,tvp5150", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tvp5150_of_match);
#endif

static struct i2c_driver tvp5150_driver = {
	.driver = {
		.of_match_table = of_match_ptr(tvp5150_of_match),
		.name	= "tvp5150",
		.pm	= &tvp5150_pm_ops,
	},
	.probe_new	= tvp5150_probe,
	.remove		= tvp5150_remove,
	.id_table	= tvp5150_id,
};

module_i2c_driver(tvp5150_driver);
