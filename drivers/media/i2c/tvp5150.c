/*
 * tvp5150 - Texas Instruments TVP5150A/AM1 and TVP5151 video decoder driver
 *
 * Copyright (c) 2005,2006 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <dt-bindings/media/tvp5150.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-of.h>
#include <media/v4l2-mc.h>

#include "tvp5150_reg.h"

#define TVP5150_H_MAX		720U
#define TVP5150_V_MAX_525_60	480U
#define TVP5150_V_MAX_OTHERS	576U
#define TVP5150_MAX_CROP_LEFT	511
#define TVP5150_MAX_CROP_TOP	127
#define TVP5150_CROP_SHIFT	2

MODULE_DESCRIPTION("Texas Instruments TVP5150A/TVP5150AM1/TVP5151 video decoder driver");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");


static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

struct tvp5150 {
	struct v4l2_subdev sd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_pad pads[DEMOD_NUM_PADS];
	struct media_entity input_ent[TVP5150_INPUT_NUM];
	struct media_pad input_pad[TVP5150_INPUT_NUM];
#endif
	struct v4l2_ctrl_handler hdl;
	struct v4l2_rect rect;

	v4l2_std_id norm;	/* Current set standard */
	u32 input;
	u32 output;
	int enable;

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
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int rc;

	rc = i2c_smbus_read_byte_data(c, addr);
	if (rc < 0) {
		v4l2_err(sd, "i2c i/o error: rc == %d\n", rc);
		return rc;
	}

	v4l2_dbg(2, debug, sd, "tvp5150: read 0x%02x = 0x%02x\n", addr, rc);

	return rc;
}

static int tvp5150_write(struct v4l2_subdev *sd, unsigned char addr,
				 unsigned char value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int rc;

	v4l2_dbg(2, debug, sd, "tvp5150: writing 0x%02x 0x%02x\n", addr, value);
	rc = i2c_smbus_write_byte_data(c, addr, value);
	if (rc < 0)
		v4l2_err(sd, "i2c i/o error: rc == %d\n", rc);

	return rc;
}

static void dump_reg_range(struct v4l2_subdev *sd, char *s, u8 init,
				const u8 end, int max_line)
{
	int i = 0;

	while (init != (u8)(end + 1)) {
		if ((i % max_line) == 0) {
			if (i > 0)
				printk("\n");
			printk("tvp5150: %s reg 0x%02x = ", s, init);
		}
		printk("%02x ", tvp5150_read(sd, init));

		init++;
		i++;
	}
	printk("\n");
}

static int tvp5150_log_status(struct v4l2_subdev *sd)
{
	printk("tvp5150: Video input source selection #1 = 0x%02x\n",
			tvp5150_read(sd, TVP5150_VD_IN_SRC_SEL_1));
	printk("tvp5150: Analog channel controls = 0x%02x\n",
			tvp5150_read(sd, TVP5150_ANAL_CHL_CTL));
	printk("tvp5150: Operation mode controls = 0x%02x\n",
			tvp5150_read(sd, TVP5150_OP_MODE_CTL));
	printk("tvp5150: Miscellaneous controls = 0x%02x\n",
			tvp5150_read(sd, TVP5150_MISC_CTL));
	printk("tvp5150: Autoswitch mask= 0x%02x\n",
			tvp5150_read(sd, TVP5150_AUTOSW_MSK));
	printk("tvp5150: Color killer threshold control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_COLOR_KIL_THSH_CTL));
	printk("tvp5150: Luminance processing controls #1 #2 and #3 = %02x %02x %02x\n",
			tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_1),
			tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_2),
			tvp5150_read(sd, TVP5150_LUMA_PROC_CTL_3));
	printk("tvp5150: Brightness control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_BRIGHT_CTL));
	printk("tvp5150: Color saturation control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_SATURATION_CTL));
	printk("tvp5150: Hue control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_HUE_CTL));
	printk("tvp5150: Contrast control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_CONTRAST_CTL));
	printk("tvp5150: Outputs and data rates select = 0x%02x\n",
			tvp5150_read(sd, TVP5150_DATA_RATE_SEL));
	printk("tvp5150: Configuration shared pins = 0x%02x\n",
			tvp5150_read(sd, TVP5150_CONF_SHARED_PIN));
	printk("tvp5150: Active video cropping start = 0x%02x%02x\n",
			tvp5150_read(sd, TVP5150_ACT_VD_CROP_ST_MSB),
			tvp5150_read(sd, TVP5150_ACT_VD_CROP_ST_LSB));
	printk("tvp5150: Active video cropping stop  = 0x%02x%02x\n",
			tvp5150_read(sd, TVP5150_ACT_VD_CROP_STP_MSB),
			tvp5150_read(sd, TVP5150_ACT_VD_CROP_STP_LSB));
	printk("tvp5150: Genlock/RTC = 0x%02x\n",
			tvp5150_read(sd, TVP5150_GENLOCK));
	printk("tvp5150: Horizontal sync start = 0x%02x\n",
			tvp5150_read(sd, TVP5150_HORIZ_SYNC_START));
	printk("tvp5150: Vertical blanking start = 0x%02x\n",
			tvp5150_read(sd, TVP5150_VERT_BLANKING_START));
	printk("tvp5150: Vertical blanking stop = 0x%02x\n",
			tvp5150_read(sd, TVP5150_VERT_BLANKING_STOP));
	printk("tvp5150: Chrominance processing control #1 and #2 = %02x %02x\n",
			tvp5150_read(sd, TVP5150_CHROMA_PROC_CTL_1),
			tvp5150_read(sd, TVP5150_CHROMA_PROC_CTL_2));
	printk("tvp5150: Interrupt reset register B = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_RESET_REG_B));
	printk("tvp5150: Interrupt enable register B = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_ENABLE_REG_B));
	printk("tvp5150: Interrupt configuration register B = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INTT_CONFIG_REG_B));
	printk("tvp5150: Video standard = 0x%02x\n",
			tvp5150_read(sd, TVP5150_VIDEO_STD));
	printk("tvp5150: Chroma gain factor: Cb=0x%02x Cr=0x%02x\n",
			tvp5150_read(sd, TVP5150_CB_GAIN_FACT),
			tvp5150_read(sd, TVP5150_CR_GAIN_FACTOR));
	printk("tvp5150: Macrovision on counter = 0x%02x\n",
			tvp5150_read(sd, TVP5150_MACROVISION_ON_CTR));
	printk("tvp5150: Macrovision off counter = 0x%02x\n",
			tvp5150_read(sd, TVP5150_MACROVISION_OFF_CTR));
	printk("tvp5150: ITU-R BT.656.%d timing(TVP5150AM1 only)\n",
			(tvp5150_read(sd, TVP5150_REV_SELECT) & 1) ? 3 : 4);
	printk("tvp5150: Device ID = %02x%02x\n",
			tvp5150_read(sd, TVP5150_MSB_DEV_ID),
			tvp5150_read(sd, TVP5150_LSB_DEV_ID));
	printk("tvp5150: ROM version = (hex) %02x.%02x\n",
			tvp5150_read(sd, TVP5150_ROM_MAJOR_VER),
			tvp5150_read(sd, TVP5150_ROM_MINOR_VER));
	printk("tvp5150: Vertical line count = 0x%02x%02x\n",
			tvp5150_read(sd, TVP5150_VERT_LN_COUNT_MSB),
			tvp5150_read(sd, TVP5150_VERT_LN_COUNT_LSB));
	printk("tvp5150: Interrupt status register B = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_STATUS_REG_B));
	printk("tvp5150: Interrupt active register B = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_ACTIVE_REG_B));
	printk("tvp5150: Status regs #1 to #5 = %02x %02x %02x %02x %02x\n",
			tvp5150_read(sd, TVP5150_STATUS_REG_1),
			tvp5150_read(sd, TVP5150_STATUS_REG_2),
			tvp5150_read(sd, TVP5150_STATUS_REG_3),
			tvp5150_read(sd, TVP5150_STATUS_REG_4),
			tvp5150_read(sd, TVP5150_STATUS_REG_5));

	dump_reg_range(sd, "Teletext filter 1",   TVP5150_TELETEXT_FIL1_INI,
			TVP5150_TELETEXT_FIL1_END, 8);
	dump_reg_range(sd, "Teletext filter 2",   TVP5150_TELETEXT_FIL2_INI,
			TVP5150_TELETEXT_FIL2_END, 8);

	printk("tvp5150: Teletext filter enable = 0x%02x\n",
			tvp5150_read(sd, TVP5150_TELETEXT_FIL_ENA));
	printk("tvp5150: Interrupt status register A = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_STATUS_REG_A));
	printk("tvp5150: Interrupt enable register A = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_ENABLE_REG_A));
	printk("tvp5150: Interrupt configuration = 0x%02x\n",
			tvp5150_read(sd, TVP5150_INT_CONF));
	printk("tvp5150: VDP status register = 0x%02x\n",
			tvp5150_read(sd, TVP5150_VDP_STATUS_REG));
	printk("tvp5150: FIFO word count = 0x%02x\n",
			tvp5150_read(sd, TVP5150_FIFO_WORD_COUNT));
	printk("tvp5150: FIFO interrupt threshold = 0x%02x\n",
			tvp5150_read(sd, TVP5150_FIFO_INT_THRESHOLD));
	printk("tvp5150: FIFO reset = 0x%02x\n",
			tvp5150_read(sd, TVP5150_FIFO_RESET));
	printk("tvp5150: Line number interrupt = 0x%02x\n",
			tvp5150_read(sd, TVP5150_LINE_NUMBER_INT));
	printk("tvp5150: Pixel alignment register = 0x%02x%02x\n",
			tvp5150_read(sd, TVP5150_PIX_ALIGN_REG_HIGH),
			tvp5150_read(sd, TVP5150_PIX_ALIGN_REG_LOW));
	printk("tvp5150: FIFO output control = 0x%02x\n",
			tvp5150_read(sd, TVP5150_FIFO_OUT_CTRL));
	printk("tvp5150: Full field enable = 0x%02x\n",
			tvp5150_read(sd, TVP5150_FULL_FIELD_ENA));
	printk("tvp5150: Full field mode register = 0x%02x\n",
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

static inline void tvp5150_selmux(struct v4l2_subdev *sd)
{
	int opmode = 0;
	struct tvp5150 *decoder = to_tvp5150(sd);
	int input = 0;
	int val;

	/* Only tvp5150am1 and tvp5151 have signal generator support */
	if ((decoder->dev_id == 0x5150 && decoder->rom_ver == 0x0400) ||
	    (decoder->dev_id == 0x5151 && decoder->rom_ver == 0x0100)) {
		if (!decoder->enable)
			input = 8;
	}

	switch (decoder->input) {
	case TVP5150_COMPOSITE1:
		input |= 2;
		/* fall through */
	case TVP5150_COMPOSITE0:
		break;
	case TVP5150_SVIDEO:
	default:
		input |= 1;
		break;
	}

	v4l2_dbg(1, debug, sd, "Selecting video route: route input=%i, output=%i "
			"=> tvp5150 input=%i, opmode=%i\n",
			decoder->input, decoder->output,
			input, opmode);

	tvp5150_write(sd, TVP5150_OP_MODE_CTL, opmode);
	tvp5150_write(sd, TVP5150_VD_IN_SRC_SEL_1, input);

	/* Svideo should enable YCrCb output and disable GPCL output
	 * For Composite and TV, it should be the reverse
	 */
	val = tvp5150_read(sd, TVP5150_MISC_CTL);
	if (val < 0) {
		v4l2_err(sd, "%s: failed with error = %d\n", __func__, val);
		return;
	}

	if (decoder->input == TVP5150_SVIDEO)
		val = (val & ~0x40) | 0x10;
	else
		val = (val & ~0x10) | 0x40;
	tvp5150_write(sd, TVP5150_MISC_CTL, val);
};

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_default[] = {
	{ /* 0x00 */
		TVP5150_VD_IN_SRC_SEL_1,0x00
	},
	{ /* 0x01 */
		TVP5150_ANAL_CHL_CTL,0x15
	},
	{ /* 0x02 */
		TVP5150_OP_MODE_CTL,0x00
	},
	{ /* 0x03 */
		TVP5150_MISC_CTL,0x01
	},
	{ /* 0x06 */
		TVP5150_COLOR_KIL_THSH_CTL,0x10
	},
	{ /* 0x07 */
		TVP5150_LUMA_PROC_CTL_1,0x60
	},
	{ /* 0x08 */
		TVP5150_LUMA_PROC_CTL_2,0x00
	},
	{ /* 0x09 */
		TVP5150_BRIGHT_CTL,0x80
	},
	{ /* 0x0a */
		TVP5150_SATURATION_CTL,0x80
	},
	{ /* 0x0b */
		TVP5150_HUE_CTL,0x00
	},
	{ /* 0x0c */
		TVP5150_CONTRAST_CTL,0x80
	},
	{ /* 0x0d */
		TVP5150_DATA_RATE_SEL,0x47
	},
	{ /* 0x0e */
		TVP5150_LUMA_PROC_CTL_3,0x00
	},
	{ /* 0x0f */
		TVP5150_CONF_SHARED_PIN,0x08
	},
	{ /* 0x11 */
		TVP5150_ACT_VD_CROP_ST_MSB,0x00
	},
	{ /* 0x12 */
		TVP5150_ACT_VD_CROP_ST_LSB,0x00
	},
	{ /* 0x13 */
		TVP5150_ACT_VD_CROP_STP_MSB,0x00
	},
	{ /* 0x14 */
		TVP5150_ACT_VD_CROP_STP_LSB,0x00
	},
	{ /* 0x15 */
		TVP5150_GENLOCK,0x01
	},
	{ /* 0x16 */
		TVP5150_HORIZ_SYNC_START,0x80
	},
	{ /* 0x18 */
		TVP5150_VERT_BLANKING_START,0x00
	},
	{ /* 0x19 */
		TVP5150_VERT_BLANKING_STOP,0x00
	},
	{ /* 0x1a */
		TVP5150_CHROMA_PROC_CTL_1,0x0c
	},
	{ /* 0x1b */
		TVP5150_CHROMA_PROC_CTL_2,0x14
	},
	{ /* 0x1c */
		TVP5150_INT_RESET_REG_B,0x00
	},
	{ /* 0x1d */
		TVP5150_INT_ENABLE_REG_B,0x00
	},
	{ /* 0x1e */
		TVP5150_INTT_CONFIG_REG_B,0x00
	},
	{ /* 0x28 */
		TVP5150_VIDEO_STD,0x00
	},
	{ /* 0x2e */
		TVP5150_MACROVISION_ON_CTR,0x0f
	},
	{ /* 0x2f */
		TVP5150_MACROVISION_OFF_CTR,0x01
	},
	{ /* 0xbb */
		TVP5150_TELETEXT_FIL_ENA,0x00
	},
	{ /* 0xc0 */
		TVP5150_INT_STATUS_REG_A,0x00
	},
	{ /* 0xc1 */
		TVP5150_INT_ENABLE_REG_A,0x00
	},
	{ /* 0xc2 */
		TVP5150_INT_CONF,0x04
	},
	{ /* 0xc8 */
		TVP5150_FIFO_INT_THRESHOLD,0x80
	},
	{ /* 0xc9 */
		TVP5150_FIFO_RESET,0x00
	},
	{ /* 0xca */
		TVP5150_LINE_NUMBER_INT,0x00
	},
	{ /* 0xcb */
		TVP5150_PIX_ALIGN_REG_LOW,0x4e
	},
	{ /* 0xcc */
		TVP5150_PIX_ALIGN_REG_HIGH,0x00
	},
	{ /* 0xcd */
		TVP5150_FIFO_OUT_CTRL,0x01
	},
	{ /* 0xcf */
		TVP5150_FULL_FIELD_ENA,0x00
	},
	{ /* 0xd0 */
		TVP5150_LINE_MODE_INI,0x00
	},
	{ /* 0xfc */
		TVP5150_FULL_FIELD_MODE_REG,0x7f
	},
	{ /* end of data */
		0xff,0xff
	}
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_enable[] = {
	{
		TVP5150_CONF_SHARED_PIN, 2
	},{	/* Automatic offset and AGC enabled */
		TVP5150_ANAL_CHL_CTL, 0x15
	},{	/* Activate YCrCb output 0x9 or 0xd ? */
		TVP5150_MISC_CTL, 0x6f
	},{	/* Activates video std autodetection for all standards */
		TVP5150_AUTOSW_MSK, 0x0
	},{	/* Default format: 0x47. For 4:2:2: 0x40 */
		TVP5150_DATA_RATE_SEL, 0x47
	},{
		TVP5150_CHROMA_PROC_CTL_1, 0x0c
	},{
		TVP5150_CHROMA_PROC_CTL_2, 0x54
	},{	/* Non documented, but initialized on WinTV USB2 */
		0x27, 0x20
	},{
		0xff,0xff
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

static struct i2c_vbi_ram_value vbi_ram_default[] =
{
	/* FIXME: Current api doesn't handle all VBI types, those not
	   yet supported are placed under #if 0 */
#if 0
	{0x010, /* Teletext, SECAM, WST System A */
		{V4L2_SLICED_TELETEXT_SECAM,6,23,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x26,
		  0xe6, 0xb4, 0x0e, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
#endif
	{0x030, /* Teletext, PAL, WST System B */
		{V4L2_SLICED_TELETEXT_B,6,22,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0x27, 0x2e, 0x20, 0x2b,
		  0xa6, 0x72, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
#if 0
	{0x050, /* Teletext, PAL, WST System C */
		{V4L2_SLICED_TELETEXT_PAL_C,6,22,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x22,
		  0xa6, 0x98, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	{0x070, /* Teletext, NTSC, WST System B */
		{V4L2_SLICED_TELETEXT_NTSC_B,10,21,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0x27, 0x2e, 0x20, 0x23,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	{0x090, /* Tetetext, NTSC NABTS System C */
		{V4L2_SLICED_TELETEXT_NTSC_C,10,21,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xe7, 0x2e, 0x20, 0x22,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x15, 0x00 }
	},
	{0x0b0, /* Teletext, NTSC-J, NABTS System D */
		{V4L2_SLICED_TELETEXT_NTSC_D,10,21,1},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xa7, 0x2e, 0x20, 0x23,
		  0x69, 0x93, 0x0d, 0x00, 0x00, 0x00, 0x10, 0x00 }
	},
	{0x0d0, /* Closed Caption, PAL/SECAM */
		{V4L2_SLICED_CAPTION_625,22,22,1},
		{ 0xaa, 0x2a, 0xff, 0x3f, 0x04, 0x51, 0x6e, 0x02,
		  0xa6, 0x7b, 0x09, 0x00, 0x00, 0x00, 0x27, 0x00 }
	},
#endif
	{0x0f0, /* Closed Caption, NTSC */
		{V4L2_SLICED_CAPTION_525,21,21,1},
		{ 0xaa, 0x2a, 0xff, 0x3f, 0x04, 0x51, 0x6e, 0x02,
		  0x69, 0x8c, 0x09, 0x00, 0x00, 0x00, 0x27, 0x00 }
	},
	{0x110, /* Wide Screen Signal, PAL/SECAM */
		{V4L2_SLICED_WSS_625,23,23,1},
		{ 0x5b, 0x55, 0xc5, 0xff, 0x00, 0x71, 0x6e, 0x42,
		  0xa6, 0xcd, 0x0f, 0x00, 0x00, 0x00, 0x3a, 0x00 }
	},
#if 0
	{0x130, /* Wide Screen Signal, NTSC C */
		{V4L2_SLICED_WSS_525,20,20,1},
		{ 0x38, 0x00, 0x3f, 0x00, 0x00, 0x71, 0x6e, 0x43,
		  0x69, 0x7c, 0x08, 0x00, 0x00, 0x00, 0x39, 0x00 }
	},
	{0x150, /* Vertical Interval Timecode (VITC), PAL/SECAM */
		{V4l2_SLICED_VITC_625,6,22,0},
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x6d, 0x49,
		  0xa6, 0x85, 0x08, 0x00, 0x00, 0x00, 0x4c, 0x00 }
	},
	{0x170, /* Vertical Interval Timecode (VITC), NTSC */
		{V4l2_SLICED_VITC_525,10,20,0},
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0x6d, 0x49,
		  0x69, 0x94, 0x08, 0x00, 0x00, 0x00, 0x4c, 0x00 }
	},
#endif
	{0x190, /* Video Program System (VPS), PAL */
		{V4L2_SLICED_VPS,16,16,0},
		{ 0xaa, 0xaa, 0xff, 0xff, 0xba, 0xce, 0x2b, 0x0d,
		  0xa6, 0xda, 0x0b, 0x00, 0x00, 0x00, 0x60, 0x00 }
	},
	/* 0x1d0 User programmable */

	/* End of struct */
	{ (u16)-1 }
};

static int tvp5150_write_inittab(struct v4l2_subdev *sd,
				const struct i2c_reg_value *regs)
{
	while (regs->reg != 0xff) {
		tvp5150_write(sd, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

static int tvp5150_vdp_init(struct v4l2_subdev *sd,
				const struct i2c_vbi_ram_value *regs)
{
	unsigned int i;

	/* Disable Full Field */
	tvp5150_write(sd, TVP5150_FULL_FIELD_ENA, 0);

	/* Before programming, Line mode should be at 0xff */
	for (i = TVP5150_LINE_MODE_INI; i <= TVP5150_LINE_MODE_END; i++)
		tvp5150_write(sd, i, 0xff);

	/* Load Ram Table */
	while (regs->reg != (u16)-1) {
		tvp5150_write(sd, TVP5150_CONF_RAM_ADDR_HIGH, regs->reg >> 8);
		tvp5150_write(sd, TVP5150_CONF_RAM_ADDR_LOW, regs->reg);

		for (i = 0; i < 16; i++)
			tvp5150_write(sd, TVP5150_VDP_CONF_RAM_DATA, regs->values[i]);

		regs++;
	}
	return 0;
}

/* Fills VBI capabilities based on i2c_vbi_ram_value struct */
static int tvp5150_g_sliced_vbi_cap(struct v4l2_subdev *sd,
				struct v4l2_sliced_vbi_cap *cap)
{
	const struct i2c_vbi_ram_value *regs = vbi_ram_default;
	int line;

	v4l2_dbg(1, debug, sd, "g_sliced_vbi_cap\n");
	memset(cap, 0, sizeof *cap);

	while (regs->reg != (u16)-1 ) {
		for (line=regs->type.ini_line;line<=regs->type.end_line;line++) {
			cap->service_lines[0][line] |= regs->type.vbi_type;
		}
		cap->service_set |= regs->type.vbi_type;

		regs++;
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
			const struct i2c_vbi_ram_value *regs,
			unsigned int type,u8 flags, int line,
			const int fields)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std = decoder->norm;
	u8 reg;
	int pos=0;

	if (std == V4L2_STD_ALL) {
		v4l2_err(sd, "VBI can't be configured without knowing number of lines\n");
		return 0;
	} else if (std & V4L2_STD_625_50) {
		/* Don't follow NTSC Line number convension */
		line += 3;
	}

	if (line<6||line>27)
		return 0;

	while (regs->reg != (u16)-1 ) {
		if ((type & regs->type.vbi_type) &&
		    (line>=regs->type.ini_line) &&
		    (line<=regs->type.end_line)) {
			type=regs->type.vbi_type;
			break;
		}

		regs++;
		pos++;
	}
	if (regs->reg == (u16)-1)
		return 0;

	type=pos | (flags & 0xf0);
	reg=((line-6)<<1)+TVP5150_LINE_MODE_INI;

	if (fields&1) {
		tvp5150_write(sd, reg, type);
	}

	if (fields&2) {
		tvp5150_write(sd, reg+1, type);
	}

	return type;
}

static int tvp5150_get_vbi(struct v4l2_subdev *sd,
			const struct i2c_vbi_ram_value *regs, int line)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	v4l2_std_id std = decoder->norm;
	u8 reg;
	int pos, type = 0;
	int i, ret = 0;

	if (std == V4L2_STD_ALL) {
		v4l2_err(sd, "VBI can't be configured without knowing number of lines\n");
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
			v4l2_err(sd, "%s: failed with error = %d\n",
				 __func__, ret);
			return 0;
		}
		pos = ret & 0x0f;
		if (pos < 0x0f)
			type |= regs[pos].type.vbi_type;
	}

	return type;
}

static int tvp5150_set_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	int fmt = 0;

	decoder->norm = std;

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

	v4l2_dbg(1, debug, sd, "Set video std register to %d.\n", fmt);
	tvp5150_write(sd, TVP5150_VIDEO_STD, fmt);
	return 0;
}

static int tvp5150_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (decoder->norm == std)
		return 0;

	/* Change cropping height limits */
	if (std & V4L2_STD_525_60)
		decoder->rect.height = TVP5150_V_MAX_525_60;
	else
		decoder->rect.height = TVP5150_V_MAX_OTHERS;


	return tvp5150_set_std(sd, std);
}

static int tvp5150_reset(struct v4l2_subdev *sd, u32 val)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	/* Initializes TVP5150 to its default values */
	tvp5150_write_inittab(sd, tvp5150_init_default);

	/* Initializes VDP registers */
	tvp5150_vdp_init(sd, vbi_ram_default);

	/* Selects decoder input */
	tvp5150_selmux(sd);

	/* Initializes TVP5150 to stream enabled values */
	tvp5150_write_inittab(sd, tvp5150_init_enable);

	/* Initialize image preferences */
	v4l2_ctrl_handler_setup(&decoder->hdl);

	tvp5150_set_std(sd, decoder->norm);

	if (decoder->mbus_type == V4L2_MBUS_PARALLEL)
		tvp5150_write(sd, TVP5150_DATA_RATE_SEL, 0x40);

	return 0;
};

static int tvp5150_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct tvp5150 *decoder = to_tvp5150(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tvp5150_write(sd, TVP5150_BRIGHT_CTL, ctrl->val);
		return 0;
	case V4L2_CID_CONTRAST:
		tvp5150_write(sd, TVP5150_CONTRAST_CTL, ctrl->val);
		return 0;
	case V4L2_CID_SATURATION:
		tvp5150_write(sd, TVP5150_SATURATION_CTL, ctrl->val);
		return 0;
	case V4L2_CID_HUE:
		tvp5150_write(sd, TVP5150_HUE_CTL, ctrl->val);
	case V4L2_CID_TEST_PATTERN:
		decoder->enable = ctrl->val ? false : true;
		tvp5150_selmux(sd);
		return 0;
	}
	return -EINVAL;
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

static int tvp5150_fill_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *f;
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (!format || format->pad)
		return -EINVAL;

	f = &format->format;

	tvp5150_reset(sd, 0);

	f->width = decoder->rect.width;
	f->height = decoder->rect.height / 2;

	f->code = MEDIA_BUS_FMT_UYVY8_2X8;
	f->field = V4L2_FIELD_ALTERNATE;
	f->colorspace = V4L2_COLORSPACE_SMPTE170M;

	v4l2_dbg(1, debug, sd, "width = %d, height = %d\n", f->width,
			f->height);
	return 0;
}

static int tvp5150_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	struct v4l2_rect rect = sel->r;
	v4l2_std_id std;
	int hmax;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	v4l2_dbg(1, debug, sd, "%s left=%d, top=%d, width=%d, height=%d\n",
		__func__, rect.left, rect.top, rect.width, rect.height);

	/* tvp5150 has some special limits */
	rect.left = clamp(rect.left, 0, TVP5150_MAX_CROP_LEFT);
	rect.width = clamp_t(unsigned int, rect.width,
			     TVP5150_H_MAX - TVP5150_MAX_CROP_LEFT - rect.left,
			     TVP5150_H_MAX - rect.left);
	rect.top = clamp(rect.top, 0, TVP5150_MAX_CROP_TOP);

	/* Calculate height based on current standard */
	if (decoder->norm == V4L2_STD_ALL)
		std = tvp5150_read_std(sd);
	else
		std = decoder->norm;

	if (std & V4L2_STD_525_60)
		hmax = TVP5150_V_MAX_525_60;
	else
		hmax = TVP5150_V_MAX_OTHERS;

	rect.height = clamp_t(unsigned int, rect.height,
			      hmax - TVP5150_MAX_CROP_TOP - rect.top,
			      hmax - rect.top);

	tvp5150_write(sd, TVP5150_VERT_BLANKING_START, rect.top);
	tvp5150_write(sd, TVP5150_VERT_BLANKING_STOP,
		      rect.top + rect.height - hmax);
	tvp5150_write(sd, TVP5150_ACT_VD_CROP_ST_MSB,
		      rect.left >> TVP5150_CROP_SHIFT);
	tvp5150_write(sd, TVP5150_ACT_VD_CROP_ST_LSB,
		      rect.left | (1 << TVP5150_CROP_SHIFT));
	tvp5150_write(sd, TVP5150_ACT_VD_CROP_STP_MSB,
		      (rect.left + rect.width - TVP5150_MAX_CROP_LEFT) >>
		      TVP5150_CROP_SHIFT);
	tvp5150_write(sd, TVP5150_ACT_VD_CROP_STP_LSB,
		      rect.left + rect.width - TVP5150_MAX_CROP_LEFT);

	decoder->rect = rect;

	return 0;
}

static int tvp5150_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct tvp5150 *decoder = container_of(sd, struct tvp5150, sd);
	v4l2_std_id std;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
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
		sel->r = decoder->rect;
		return 0;
	default:
		return -EINVAL;
	}
}

static int tvp5150_g_mbus_config(struct v4l2_subdev *sd,
				 struct v4l2_mbus_config *cfg)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	cfg->type = decoder->mbus_type;
	cfg->flags = V4L2_MBUS_MASTER | V4L2_MBUS_PCLK_SAMPLE_RISING
		   | V4L2_MBUS_FIELD_EVEN_LOW | V4L2_MBUS_DATA_ACTIVE_HIGH;

	return 0;
}

/****************************************************************************
			V4L2 subdev pad ops
 ****************************************************************************/
static int tvp5150_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;
	return 0;
}

static int tvp5150_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct tvp5150 *decoder = to_tvp5150(sd);

	if (fse->index >= 8 || fse->code != MEDIA_BUS_FMT_UYVY8_2X8)
		return -EINVAL;

	fse->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fse->min_width = decoder->rect.width;
	fse->max_width = decoder->rect.width;
	fse->min_height = decoder->rect.height / 2;
	fse->max_height = decoder->rect.height / 2;

	return 0;
}

/****************************************************************************
			Media entity ops
 ****************************************************************************/

static int tvp5150_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct tvp5150 *decoder = to_tvp5150(sd);
	int i;

	for (i = 0; i < TVP5150_INPUT_NUM; i++) {
		if (remote->entity == &decoder->input_ent[i])
			break;
	}

	/* Do nothing for entities that are not input connectors */
	if (i == TVP5150_INPUT_NUM)
		return 0;

	decoder->input = i;

	tvp5150_selmux(sd);
#endif

	return 0;
}

static const struct media_entity_operations tvp5150_sd_media_ops = {
	.link_setup = tvp5150_link_setup,
};

/****************************************************************************
			I2C Command
 ****************************************************************************/

static int tvp5150_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct tvp5150 *decoder = to_tvp5150(sd);
	/* Output format: 8-bit ITU-R BT.656 with embedded syncs */
	int val = 0x09;

	/* Output format: 8-bit 4:2:2 YUV with discrete sync */
	if (decoder->mbus_type == V4L2_MBUS_PARALLEL)
		val = 0x0d;

	/* Initializes TVP5150 to its default values */
	/* # set PCLK (27MHz) */
	tvp5150_write(sd, TVP5150_CONF_SHARED_PIN, 0x00);

	if (enable)
		tvp5150_write(sd, TVP5150_MISC_CTL, val);
	else
		tvp5150_write(sd, TVP5150_MISC_CTL, 0x00);

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
	/* this is for capturing 36 raw vbi lines
	   if there's a way to cut off the beginning 2 vbi lines
	   with the tvp5150 then the vbi line count could be lowered
	   to 17 lines/field again, although I couldn't find a register
	   which could do that cropping */
	if (fmt->sample_format == V4L2_PIX_FMT_GREY)
		tvp5150_write(sd, TVP5150_LUMA_PROC_CTL_1, 0x70);
	if (fmt->count[0] == 18 && fmt->count[1] == 18) {
		tvp5150_write(sd, TVP5150_VERT_BLANKING_START, 0x00);
		tvp5150_write(sd, TVP5150_VERT_BLANKING_STOP, 0x01);
	}
	return 0;
}

static int tvp5150_s_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	int i;

	if (svbi->service_set != 0) {
		for (i = 0; i <= 23; i++) {
			svbi->service_lines[1][i] = 0;
			svbi->service_lines[0][i] =
				tvp5150_set_vbi(sd, vbi_ram_default,
				       svbi->service_lines[0][i], 0xf0, i, 3);
		}
		/* Enables FIFO */
		tvp5150_write(sd, TVP5150_FIFO_OUT_CTRL, 1);
	} else {
		/* Disables FIFO*/
		tvp5150_write(sd, TVP5150_FIFO_OUT_CTRL, 0);

		/* Disable Full Field */
		tvp5150_write(sd, TVP5150_FULL_FIELD_ENA, 0);

		/* Disable Line modes */
		for (i = TVP5150_LINE_MODE_INI; i <= TVP5150_LINE_MODE_END; i++)
			tvp5150_write(sd, i, 0xff);
	}
	return 0;
}

static int tvp5150_g_sliced_fmt(struct v4l2_subdev *sd, struct v4l2_sliced_vbi_format *svbi)
{
	int i, mask = 0;

	memset(svbi->service_lines, 0, sizeof(svbi->service_lines));

	for (i = 0; i <= 23; i++) {
		svbi->service_lines[0][i] =
			tvp5150_get_vbi(sd, vbi_ram_default, i);
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
		v4l2_err(sd, "%s: failed with error = %d\n", __func__, res);
		return res;
	}

	reg->val = res;
	reg->size = 1;
	return 0;
}

static int tvp5150_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	return tvp5150_write(sd, reg->reg & 0xff, reg->val & 0xff);
}
#endif

static int tvp5150_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	int status = tvp5150_read(sd, 0x88);

	vt->signal = ((status & 0x04) && (status & 0x02)) ? 0xffff : 0x0;
	return 0;
}

static int tvp5150_registered(struct v4l2_subdev *sd)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct tvp5150 *decoder = to_tvp5150(sd);
	int ret = 0;
	int i;

	for (i = 0; i < TVP5150_INPUT_NUM; i++) {
		struct media_entity *input = &decoder->input_ent[i];
		struct media_pad *pad = &decoder->input_pad[i];

		if (!input->name)
			continue;

		decoder->input_pad[i].flags = MEDIA_PAD_FL_SOURCE;

		ret = media_entity_pads_init(input, 1, pad);
		if (ret < 0)
			return ret;

		ret = media_device_register_entity(sd->v4l2_dev->mdev, input);
		if (ret < 0)
			return ret;

		ret = media_create_pad_link(input, 0, &sd->entity,
					    DEMOD_PAD_IF_INPUT, 0);
		if (ret < 0) {
			media_device_unregister_entity(input);
			return ret;
		}
	}
#endif

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
};

static const struct v4l2_subdev_tuner_ops tvp5150_tuner_ops = {
	.g_tuner = tvp5150_g_tuner,
};

static const struct v4l2_subdev_video_ops tvp5150_video_ops = {
	.s_std = tvp5150_s_std,
	.s_stream = tvp5150_s_stream,
	.s_routing = tvp5150_s_routing,
	.g_mbus_config = tvp5150_g_mbus_config,
};

static const struct v4l2_subdev_vbi_ops tvp5150_vbi_ops = {
	.g_sliced_vbi_cap = tvp5150_g_sliced_vbi_cap,
	.g_sliced_fmt = tvp5150_g_sliced_fmt,
	.s_sliced_fmt = tvp5150_s_sliced_fmt,
	.s_raw_fmt = tvp5150_s_raw_fmt,
};

static const struct v4l2_subdev_pad_ops tvp5150_pad_ops = {
	.enum_mbus_code = tvp5150_enum_mbus_code,
	.enum_frame_size = tvp5150_enum_frame_size,
	.set_fmt = tvp5150_fill_fmt,
	.get_fmt = tvp5150_fill_fmt,
	.get_selection = tvp5150_get_selection,
	.set_selection = tvp5150_set_selection,
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
};


/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/

static int tvp5150_detect_version(struct tvp5150 *core)
{
	struct v4l2_subdev *sd = &core->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	unsigned int i;
	u8 regs[4];
	int res;

	/*
	 * Read consequent registers - TVP5150_MSB_DEV_ID, TVP5150_LSB_DEV_ID,
	 * TVP5150_ROM_MAJOR_VER, TVP5150_ROM_MINOR_VER
	 */
	for (i = 0; i < 4; i++) {
		res = tvp5150_read(sd, TVP5150_MSB_DEV_ID + i);
		if (res < 0)
			return res;
		regs[i] = res;
	}

	core->dev_id = (regs[0] << 8) | regs[1];
	core->rom_ver = (regs[2] << 8) | regs[3];

	v4l2_info(sd, "tvp%04x (%u.%u) chip found @ 0x%02x (%s)\n",
		  core->dev_id, regs[2], regs[3], c->addr << 1,
		  c->adapter->name);

	if (core->dev_id == 0x5150 && core->rom_ver == 0x0321) {
		v4l2_info(sd, "tvp5150a detected.\n");
	} else if (core->dev_id == 0x5150 && core->rom_ver == 0x0400) {
		v4l2_info(sd, "tvp5150am1 detected.\n");

		/* ITU-T BT.656.4 timing */
		tvp5150_write(sd, TVP5150_REV_SELECT, 0);
	} else if (core->dev_id == 0x5151 && core->rom_ver == 0x0100) {
		v4l2_info(sd, "tvp5151 detected.\n");
	} else {
		v4l2_info(sd, "*** unknown tvp%04x chip detected.\n",
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

static int tvp5150_parse_dt(struct tvp5150 *decoder, struct device_node *np)
{
	struct v4l2_of_endpoint bus_cfg;
	struct device_node *ep;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct device_node *connectors, *child;
	struct media_entity *input;
	const char *name;
	u32 input_type;
#endif
	unsigned int flags;
	int ret = 0;

	ep = of_graph_get_next_endpoint(np, NULL);
	if (!ep)
		return -EINVAL;

	ret = v4l2_of_parse_endpoint(ep, &bus_cfg);
	if (ret)
		goto err;

	flags = bus_cfg.bus.parallel.flags;

	if (bus_cfg.bus_type == V4L2_MBUS_PARALLEL &&
	    !(flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH &&
	      flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH &&
	      flags & V4L2_MBUS_FIELD_EVEN_LOW)) {
		ret = -EINVAL;
		goto err;
	}

	decoder->mbus_type = bus_cfg.bus_type;

#ifdef CONFIG_MEDIA_CONTROLLER
	connectors = of_get_child_by_name(np, "connectors");

	if (!connectors)
		goto err;

	for_each_available_child_of_node(connectors, child) {
		ret = of_property_read_u32(child, "input", &input_type);
		if (ret) {
			v4l2_err(&decoder->sd,
				 "missing type property in node %s\n",
				 child->name);
			goto err_connector;
		}

		if (input_type >= TVP5150_INPUT_NUM) {
			ret = -EINVAL;
			goto err_connector;
		}

		input = &decoder->input_ent[input_type];

		/* Each input connector can only be defined once */
		if (input->name) {
			v4l2_err(&decoder->sd,
				 "input %s with same type already exists\n",
				 input->name);
			ret = -EINVAL;
			goto err_connector;
		}

		switch (input_type) {
		case TVP5150_COMPOSITE0:
		case TVP5150_COMPOSITE1:
			input->function = MEDIA_ENT_F_CONN_COMPOSITE;
			break;
		case TVP5150_SVIDEO:
			input->function = MEDIA_ENT_F_CONN_SVIDEO;
			break;
		}

		input->flags = MEDIA_ENT_FL_CONNECTOR;

		ret = of_property_read_string(child, "label", &name);
		if (ret < 0) {
			v4l2_err(&decoder->sd,
				 "missing label property in node %s\n",
				 child->name);
			goto err_connector;
		}

		input->name = name;
	}

err_connector:
	of_node_put(connectors);
#endif
err:
	of_node_put(ep);
	return ret;
}

static const char * const tvp5150_test_patterns[2] = {
	"Disabled",
	"Black screen"
};

static int tvp5150_probe(struct i2c_client *c,
			 const struct i2c_device_id *id)
{
	struct tvp5150 *core;
	struct v4l2_subdev *sd;
	struct device_node *np = c->dev.of_node;
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

	sd = &core->sd;

	if (IS_ENABLED(CONFIG_OF) && np) {
		res = tvp5150_parse_dt(core, np);
		if (res) {
			v4l2_err(sd, "DT parsing error: %d\n", res);
			return res;
		}
	} else {
		/* Default to BT.656 embedded sync */
		core->mbus_type = V4L2_MBUS_BT656;
	}

	v4l2_i2c_subdev_init(sd, c, &tvp5150_ops);
	sd->internal_ops = &tvp5150_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

#if defined(CONFIG_MEDIA_CONTROLLER)
	core->pads[DEMOD_PAD_IF_INPUT].flags = MEDIA_PAD_FL_SINK;
	core->pads[DEMOD_PAD_VID_OUT].flags = MEDIA_PAD_FL_SOURCE;
	core->pads[DEMOD_PAD_VBI_OUT].flags = MEDIA_PAD_FL_SOURCE;

	sd->entity.function = MEDIA_ENT_F_ATV_DECODER;

	res = media_entity_pads_init(&sd->entity, DEMOD_NUM_PADS, core->pads);
	if (res < 0)
		return res;

	sd->entity.ops = &tvp5150_sd_media_ops;
#endif

	res = tvp5150_detect_version(core);
	if (res < 0)
		return res;

	core->norm = V4L2_STD_ALL;	/* Default is autodetect */
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
				     ARRAY_SIZE(tvp5150_test_patterns),
				     0, 0, tvp5150_test_patterns);
	sd->ctrl_handler = &core->hdl;
	if (core->hdl.error) {
		res = core->hdl.error;
		goto err;
	}
	v4l2_ctrl_handler_setup(&core->hdl);

	/* Default is no cropping */
	core->rect.top = 0;
	if (tvp5150_read_std(sd) & V4L2_STD_525_60)
		core->rect.height = TVP5150_V_MAX_525_60;
	else
		core->rect.height = TVP5150_V_MAX_OTHERS;
	core->rect.left = 0;
	core->rect.width = TVP5150_H_MAX;

	res = v4l2_async_register_subdev(sd);
	if (res < 0)
		goto err;

	if (debug > 1)
		tvp5150_log_status(sd);
	return 0;

err:
	v4l2_ctrl_handler_free(&core->hdl);
	return res;
}

static int tvp5150_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);
	struct tvp5150 *decoder = to_tvp5150(sd);

	v4l2_dbg(1, debug, sd,
		"tvp5150.c: removing tvp5150 adapter on address 0x%x\n",
		c->addr << 1);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&decoder->hdl);
	return 0;
}

/* ----------------------------------------------------------------------- */

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
	},
	.probe		= tvp5150_probe,
	.remove		= tvp5150_remove,
	.id_table	= tvp5150_id,
};

module_i2c_driver(tvp5150_driver);
