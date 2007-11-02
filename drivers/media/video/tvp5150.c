/*
 * tvp5150 - Texas Instruments TVP5150A/AM1 video decoder driver
 *
 * Copyright (c) 2005,2006 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>
#include <media/tvp5150.h>

#include "tvp5150_reg.h"

MODULE_DESCRIPTION("Texas Instruments TVP5150A video decoder driver");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");

/* standard i2c insmod options */
static unsigned short normal_i2c[] = {
	0xb8 >> 1,
	0xba >> 1,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define tvp5150_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, c->driver->driver.name, \
	       i2c_adapter_id(c->adapter), c->addr , ## arg); } while (0)
#define tvp5150_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, c->driver->driver.name, \
	       i2c_adapter_id(c->adapter), c->addr , ## arg); } while (0)
#define tvp5150_dbg(num, fmt, arg...) \
	do { \
		if (debug >= num) \
			printk(KERN_DEBUG "%s debug %d-%04x: " fmt,\
				c->driver->driver.name, \
				i2c_adapter_id(c->adapter), \
				c->addr , ## arg); } while (0)

/* supported controls */
static struct v4l2_queryctrl tvp5150_qctrl[] = {
	{
		.id = V4L2_CID_BRIGHTNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 128,
		.flags = 0,
	}, {
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step = 0x1,
		.default_value = 128,
		.flags = 0,
	}, {
		 .id = V4L2_CID_SATURATION,
		 .type = V4L2_CTRL_TYPE_INTEGER,
		 .name = "Saturation",
		 .minimum = 0,
		 .maximum = 255,
		 .step = 0x1,
		 .default_value = 128,
		 .flags = 0,
	}, {
		.id = V4L2_CID_HUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Hue",
		.minimum = -128,
		.maximum = 127,
		.step = 0x1,
		.default_value = 0,
		.flags = 0,
	}
};

struct tvp5150 {
	struct i2c_client *client;

	v4l2_std_id norm;	/* Current set standard */
	struct v4l2_routing route;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static int tvp5150_read(struct i2c_client *c, unsigned char addr)
{
	unsigned char buffer[1];
	int rc;

	buffer[0] = addr;
	if (1 != (rc = i2c_master_send(c, buffer, 1)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	msleep(10);

	if (1 != (rc = i2c_master_recv(c, buffer, 1)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	tvp5150_dbg(2, "tvp5150: read 0x%02x = 0x%02x\n", addr, buffer[0]);

	return (buffer[0]);
}

static inline void tvp5150_write(struct i2c_client *c, unsigned char addr,
				 unsigned char value)
{
	unsigned char buffer[2];
	int rc;

	buffer[0] = addr;
	buffer[1] = value;
	tvp5150_dbg(2, "tvp5150: writing 0x%02x 0x%02x\n", buffer[0], buffer[1]);
	if (2 != (rc = i2c_master_send(c, buffer, 2)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 2)\n", rc);
}

static void dump_reg_range(struct i2c_client *c, char *s, u8 init, const u8 end,int max_line)
{
	int i=0;

	while (init!=(u8)(end+1)) {
		if ((i%max_line) == 0) {
			if (i>0)
				printk("\n");
			printk("tvp5150: %s reg 0x%02x = ",s,init);
		}
		printk("%02x ",tvp5150_read(c, init));

		init++;
		i++;
	}
	printk("\n");
}

static void dump_reg(struct i2c_client *c)
{
	printk("tvp5150: Video input source selection #1 = 0x%02x\n",
					tvp5150_read(c, TVP5150_VD_IN_SRC_SEL_1));
	printk("tvp5150: Analog channel controls = 0x%02x\n",
					tvp5150_read(c, TVP5150_ANAL_CHL_CTL));
	printk("tvp5150: Operation mode controls = 0x%02x\n",
					tvp5150_read(c, TVP5150_OP_MODE_CTL));
	printk("tvp5150: Miscellaneous controls = 0x%02x\n",
					tvp5150_read(c, TVP5150_MISC_CTL));
	printk("tvp5150: Autoswitch mask= 0x%02x\n",
					tvp5150_read(c, TVP5150_AUTOSW_MSK));
	printk("tvp5150: Color killer threshold control = 0x%02x\n",
					tvp5150_read(c, TVP5150_COLOR_KIL_THSH_CTL));
	printk("tvp5150: Luminance processing controls #1 #2 and #3 = %02x %02x %02x\n",
					tvp5150_read(c, TVP5150_LUMA_PROC_CTL_1),
					tvp5150_read(c, TVP5150_LUMA_PROC_CTL_2),
					tvp5150_read(c, TVP5150_LUMA_PROC_CTL_3));
	printk("tvp5150: Brightness control = 0x%02x\n",
					tvp5150_read(c, TVP5150_BRIGHT_CTL));
	printk("tvp5150: Color saturation control = 0x%02x\n",
					tvp5150_read(c, TVP5150_SATURATION_CTL));
	printk("tvp5150: Hue control = 0x%02x\n",
					tvp5150_read(c, TVP5150_HUE_CTL));
	printk("tvp5150: Contrast control = 0x%02x\n",
					tvp5150_read(c, TVP5150_CONTRAST_CTL));
	printk("tvp5150: Outputs and data rates select = 0x%02x\n",
					tvp5150_read(c, TVP5150_DATA_RATE_SEL));
	printk("tvp5150: Configuration shared pins = 0x%02x\n",
					tvp5150_read(c, TVP5150_CONF_SHARED_PIN));
	printk("tvp5150: Active video cropping start = 0x%02x%02x\n",
					tvp5150_read(c, TVP5150_ACT_VD_CROP_ST_MSB),
					tvp5150_read(c, TVP5150_ACT_VD_CROP_ST_LSB));
	printk("tvp5150: Active video cropping stop  = 0x%02x%02x\n",
					tvp5150_read(c, TVP5150_ACT_VD_CROP_STP_MSB),
					tvp5150_read(c, TVP5150_ACT_VD_CROP_STP_LSB));
	printk("tvp5150: Genlock/RTC = 0x%02x\n",
					tvp5150_read(c, TVP5150_GENLOCK));
	printk("tvp5150: Horizontal sync start = 0x%02x\n",
					tvp5150_read(c, TVP5150_HORIZ_SYNC_START));
	printk("tvp5150: Vertical blanking start = 0x%02x\n",
					tvp5150_read(c, TVP5150_VERT_BLANKING_START));
	printk("tvp5150: Vertical blanking stop = 0x%02x\n",
					tvp5150_read(c, TVP5150_VERT_BLANKING_STOP));
	printk("tvp5150: Chrominance processing control #1 and #2 = %02x %02x\n",
					tvp5150_read(c, TVP5150_CHROMA_PROC_CTL_1),
					tvp5150_read(c, TVP5150_CHROMA_PROC_CTL_2));
	printk("tvp5150: Interrupt reset register B = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_RESET_REG_B));
	printk("tvp5150: Interrupt enable register B = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_ENABLE_REG_B));
	printk("tvp5150: Interrupt configuration register B = 0x%02x\n",
					tvp5150_read(c, TVP5150_INTT_CONFIG_REG_B));
	printk("tvp5150: Video standard = 0x%02x\n",
					tvp5150_read(c, TVP5150_VIDEO_STD));
	printk("tvp5150: Chroma gain factor: Cb=0x%02x Cr=0x%02x\n",
					tvp5150_read(c, TVP5150_CB_GAIN_FACT),
					tvp5150_read(c, TVP5150_CR_GAIN_FACTOR));
	printk("tvp5150: Macrovision on counter = 0x%02x\n",
					tvp5150_read(c, TVP5150_MACROVISION_ON_CTR));
	printk("tvp5150: Macrovision off counter = 0x%02x\n",
					tvp5150_read(c, TVP5150_MACROVISION_OFF_CTR));
	printk("tvp5150: ITU-R BT.656.%d timing(TVP5150AM1 only)\n",
					(tvp5150_read(c, TVP5150_REV_SELECT)&1)?3:4);
	printk("tvp5150: Device ID = %02x%02x\n",
					tvp5150_read(c, TVP5150_MSB_DEV_ID),
					tvp5150_read(c, TVP5150_LSB_DEV_ID));
	printk("tvp5150: ROM version = (hex) %02x.%02x\n",
					tvp5150_read(c, TVP5150_ROM_MAJOR_VER),
					tvp5150_read(c, TVP5150_ROM_MINOR_VER));
	printk("tvp5150: Vertical line count = 0x%02x%02x\n",
					tvp5150_read(c, TVP5150_VERT_LN_COUNT_MSB),
					tvp5150_read(c, TVP5150_VERT_LN_COUNT_LSB));
	printk("tvp5150: Interrupt status register B = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_STATUS_REG_B));
	printk("tvp5150: Interrupt active register B = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_ACTIVE_REG_B));
	printk("tvp5150: Status regs #1 to #5 = %02x %02x %02x %02x %02x\n",
					tvp5150_read(c, TVP5150_STATUS_REG_1),
					tvp5150_read(c, TVP5150_STATUS_REG_2),
					tvp5150_read(c, TVP5150_STATUS_REG_3),
					tvp5150_read(c, TVP5150_STATUS_REG_4),
					tvp5150_read(c, TVP5150_STATUS_REG_5));

	dump_reg_range(c,"Teletext filter 1",   TVP5150_TELETEXT_FIL1_INI,
						TVP5150_TELETEXT_FIL1_END,8);
	dump_reg_range(c,"Teletext filter 2",   TVP5150_TELETEXT_FIL2_INI,
						TVP5150_TELETEXT_FIL2_END,8);

	printk("tvp5150: Teletext filter enable = 0x%02x\n",
					tvp5150_read(c, TVP5150_TELETEXT_FIL_ENA));
	printk("tvp5150: Interrupt status register A = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_STATUS_REG_A));
	printk("tvp5150: Interrupt enable register A = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_ENABLE_REG_A));
	printk("tvp5150: Interrupt configuration = 0x%02x\n",
					tvp5150_read(c, TVP5150_INT_CONF));
	printk("tvp5150: VDP status register = 0x%02x\n",
					tvp5150_read(c, TVP5150_VDP_STATUS_REG));
	printk("tvp5150: FIFO word count = 0x%02x\n",
					tvp5150_read(c, TVP5150_FIFO_WORD_COUNT));
	printk("tvp5150: FIFO interrupt threshold = 0x%02x\n",
					tvp5150_read(c, TVP5150_FIFO_INT_THRESHOLD));
	printk("tvp5150: FIFO reset = 0x%02x\n",
					tvp5150_read(c, TVP5150_FIFO_RESET));
	printk("tvp5150: Line number interrupt = 0x%02x\n",
					tvp5150_read(c, TVP5150_LINE_NUMBER_INT));
	printk("tvp5150: Pixel alignment register = 0x%02x%02x\n",
					tvp5150_read(c, TVP5150_PIX_ALIGN_REG_HIGH),
					tvp5150_read(c, TVP5150_PIX_ALIGN_REG_LOW));
	printk("tvp5150: FIFO output control = 0x%02x\n",
					tvp5150_read(c, TVP5150_FIFO_OUT_CTRL));
	printk("tvp5150: Full field enable = 0x%02x\n",
					tvp5150_read(c, TVP5150_FULL_FIELD_ENA));
	printk("tvp5150: Full field mode register = 0x%02x\n",
					tvp5150_read(c, TVP5150_FULL_FIELD_MODE_REG));

	dump_reg_range(c,"CC   data",   TVP5150_CC_DATA_INI,
					TVP5150_CC_DATA_END,8);

	dump_reg_range(c,"WSS  data",   TVP5150_WSS_DATA_INI,
					TVP5150_WSS_DATA_END,8);

	dump_reg_range(c,"VPS  data",   TVP5150_VPS_DATA_INI,
					TVP5150_VPS_DATA_END,8);

	dump_reg_range(c,"VITC data",   TVP5150_VITC_DATA_INI,
					TVP5150_VITC_DATA_END,10);

	dump_reg_range(c,"Line mode",   TVP5150_LINE_MODE_INI,
					TVP5150_LINE_MODE_END,8);
}

/****************************************************************************
			Basic functions
 ****************************************************************************/

static inline void tvp5150_selmux(struct i2c_client *c)
{
	int opmode=0;
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	int input = 0;

	if ((decoder->route.output & TVP5150_BLACK_SCREEN) || !decoder->enable)
		input = 8;

	switch (decoder->route.input) {
	case TVP5150_COMPOSITE1:
		input |= 2;
		/* fall through */
	case TVP5150_COMPOSITE0:
		opmode=0x30;		/* TV Mode */
		break;
	case TVP5150_SVIDEO:
	default:
		input |= 1;
		opmode=0;		/* Auto Mode */
		break;
	}

	tvp5150_dbg( 1, "Selecting video route: route input=%i, output=%i "
			"=> tvp5150 input=%i, opmode=%i\n",
			decoder->route.input,decoder->route.output,
			input, opmode );

	tvp5150_write(c, TVP5150_OP_MODE_CTL, opmode);
	tvp5150_write(c, TVP5150_VD_IN_SRC_SEL_1, input);
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

static int tvp5150_write_inittab(struct i2c_client *c,
				const struct i2c_reg_value *regs)
{
	while (regs->reg != 0xff) {
		tvp5150_write(c, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

static int tvp5150_vdp_init(struct i2c_client *c,
				const struct i2c_vbi_ram_value *regs)
{
	unsigned int i;

	/* Disable Full Field */
	tvp5150_write(c, TVP5150_FULL_FIELD_ENA, 0);

	/* Before programming, Line mode should be at 0xff */
	for (i=TVP5150_LINE_MODE_INI; i<=TVP5150_LINE_MODE_END; i++)
		tvp5150_write(c, i, 0xff);

	/* Load Ram Table */
	while (regs->reg != (u16)-1 ) {
		tvp5150_write(c, TVP5150_CONF_RAM_ADDR_HIGH,regs->reg>>8);
		tvp5150_write(c, TVP5150_CONF_RAM_ADDR_LOW,regs->reg);

		for (i=0;i<16;i++)
			tvp5150_write(c, TVP5150_VDP_CONF_RAM_DATA,regs->values[i]);

		regs++;
	}
	return 0;
}

/* Fills VBI capabilities based on i2c_vbi_ram_value struct */
static void tvp5150_vbi_get_cap(const struct i2c_vbi_ram_value *regs,
				struct v4l2_sliced_vbi_cap *cap)
{
	int line;

	memset(cap, 0, sizeof *cap);

	while (regs->reg != (u16)-1 ) {
		for (line=regs->type.ini_line;line<=regs->type.end_line;line++) {
			cap->service_lines[0][line] |= regs->type.vbi_type;
		}
		cap->service_set |= regs->type.vbi_type;

		regs++;
	}
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
static int tvp5150_set_vbi(struct i2c_client *c,
			const struct i2c_vbi_ram_value *regs,
			unsigned int type,u8 flags, int line,
			const int fields)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	v4l2_std_id std=decoder->norm;
	u8 reg;
	int pos=0;

	if (std == V4L2_STD_ALL) {
		tvp5150_err("VBI can't be configured without knowing number of lines\n");
		return 0;
	} else if (std && V4L2_STD_625_50) {
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
		tvp5150_write(c, reg, type);
	}

	if (fields&2) {
		tvp5150_write(c, reg+1, type);
	}

	return type;
}

static int tvp5150_get_vbi(struct i2c_client *c,
			const struct i2c_vbi_ram_value *regs, int line)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	v4l2_std_id std=decoder->norm;
	u8 reg;
	int pos, type=0;

	if (std == V4L2_STD_ALL) {
		tvp5150_err("VBI can't be configured without knowing number of lines\n");
		return 0;
	} else if (std && V4L2_STD_625_50) {
		/* Don't follow NTSC Line number convension */
		line += 3;
	}

	if (line<6||line>27)
		return 0;

	reg=((line-6)<<1)+TVP5150_LINE_MODE_INI;

	pos=tvp5150_read(c, reg)&0x0f;
	if (pos<0x0f)
		type=regs[pos].type.vbi_type;

	pos=tvp5150_read(c, reg+1)&0x0f;
	if (pos<0x0f)
		type|=regs[pos].type.vbi_type;

	return type;
}
static int tvp5150_set_std(struct i2c_client *c, v4l2_std_id std)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	int fmt=0;

	decoder->norm=std;

	/* First tests should be against specific std */

	if (std == V4L2_STD_ALL) {
		fmt=0;	/* Autodetect mode */
	} else if (std & V4L2_STD_NTSC_443) {
		fmt=0xa;
	} else if (std & V4L2_STD_PAL_M) {
		fmt=0x6;
	} else if (std & (V4L2_STD_PAL_N| V4L2_STD_PAL_Nc)) {
		fmt=0x8;
	} else {
		/* Then, test against generic ones */
		if (std & V4L2_STD_NTSC) {
			fmt=0x2;
		} else if (std & V4L2_STD_PAL) {
			fmt=0x4;
		} else if (std & V4L2_STD_SECAM) {
			fmt=0xc;
		}
	}

	tvp5150_dbg(1,"Set video std register to %d.\n",fmt);
	tvp5150_write(c, TVP5150_VIDEO_STD, fmt);

	return 0;
}

static inline void tvp5150_reset(struct i2c_client *c)
{
	u8 msb_id, lsb_id, msb_rom, lsb_rom;
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	msb_id=tvp5150_read(c,TVP5150_MSB_DEV_ID);
	lsb_id=tvp5150_read(c,TVP5150_LSB_DEV_ID);
	msb_rom=tvp5150_read(c,TVP5150_ROM_MAJOR_VER);
	lsb_rom=tvp5150_read(c,TVP5150_ROM_MINOR_VER);

	if ((msb_rom==4)&&(lsb_rom==0)) { /* Is TVP5150AM1 */
		tvp5150_info("tvp%02x%02xam1 detected.\n",msb_id, lsb_id);

		/* ITU-T BT.656.4 timing */
		tvp5150_write(c,TVP5150_REV_SELECT,0);
	} else {
		if ((msb_rom==3)||(lsb_rom==0x21)) { /* Is TVP5150A */
			tvp5150_info("tvp%02x%02xa detected.\n",msb_id, lsb_id);
		} else {
			tvp5150_info("*** unknown tvp%02x%02x chip detected.\n",msb_id,lsb_id);
			tvp5150_info("*** Rom ver is %d.%d\n",msb_rom,lsb_rom);
		}
	}

	/* Initializes TVP5150 to its default values */
	tvp5150_write_inittab(c, tvp5150_init_default);

	/* Initializes VDP registers */
	tvp5150_vdp_init(c, vbi_ram_default);

	/* Selects decoder input */
	tvp5150_selmux(c);

	/* Initializes TVP5150 to stream enabled values */
	tvp5150_write_inittab(c, tvp5150_init_enable);

	/* Initialize image preferences */
	tvp5150_write(c, TVP5150_BRIGHT_CTL, decoder->bright);
	tvp5150_write(c, TVP5150_CONTRAST_CTL, decoder->contrast);
	tvp5150_write(c, TVP5150_SATURATION_CTL, decoder->contrast);
	tvp5150_write(c, TVP5150_HUE_CTL, decoder->hue);

	tvp5150_set_std(c, decoder->norm);
};

static int tvp5150_get_ctrl(struct i2c_client *c, struct v4l2_control *ctrl)
{
/*	struct tvp5150 *decoder = i2c_get_clientdata(c); */

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = tvp5150_read(c, TVP5150_BRIGHT_CTL);
		return 0;
	case V4L2_CID_CONTRAST:
		ctrl->value = tvp5150_read(c, TVP5150_CONTRAST_CTL);
		return 0;
	case V4L2_CID_SATURATION:
		ctrl->value = tvp5150_read(c, TVP5150_SATURATION_CTL);
		return 0;
	case V4L2_CID_HUE:
		ctrl->value = tvp5150_read(c, TVP5150_HUE_CTL);
		return 0;
	}
	return -EINVAL;
}

static int tvp5150_set_ctrl(struct i2c_client *c, struct v4l2_control *ctrl)
{
/*	struct tvp5150 *decoder = i2c_get_clientdata(c); */

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tvp5150_write(c, TVP5150_BRIGHT_CTL, ctrl->value);
		return 0;
	case V4L2_CID_CONTRAST:
		tvp5150_write(c, TVP5150_CONTRAST_CTL, ctrl->value);
		return 0;
	case V4L2_CID_SATURATION:
		tvp5150_write(c, TVP5150_SATURATION_CTL, ctrl->value);
		return 0;
	case V4L2_CID_HUE:
		tvp5150_write(c, TVP5150_HUE_CTL, ctrl->value);
		return 0;
	}
	return -EINVAL;
}

/****************************************************************************
			I2C Command
 ****************************************************************************/
static int tvp5150_command(struct i2c_client *c,
			   unsigned int cmd, void *arg)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	switch (cmd) {

	case 0:
	case VIDIOC_INT_RESET:
		tvp5150_reset(c);
		break;
	case VIDIOC_INT_G_VIDEO_ROUTING:
	{
		struct v4l2_routing *route = arg;

		*route = decoder->route;
		break;
	}
	case VIDIOC_INT_S_VIDEO_ROUTING:
	{
		struct v4l2_routing *route = arg;

		decoder->route = *route;
		tvp5150_selmux(c);
		break;
	}
	case VIDIOC_S_STD:
		if (decoder->norm == *(v4l2_std_id *)arg)
			break;
		return tvp5150_set_std(c, *(v4l2_std_id *)arg);
	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = decoder->norm;
		break;

	case VIDIOC_G_SLICED_VBI_CAP:
	{
		struct v4l2_sliced_vbi_cap *cap = arg;
		tvp5150_dbg(1, "VIDIOC_G_SLICED_VBI_CAP\n");

		tvp5150_vbi_get_cap(vbi_ram_default, cap);
		break;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *fmt;
		struct v4l2_sliced_vbi_format *svbi;
		int i;

		fmt = arg;
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
		svbi = &fmt->fmt.sliced;
		if (svbi->service_set != 0) {
			for (i = 0; i <= 23; i++) {
				svbi->service_lines[1][i] = 0;

				svbi->service_lines[0][i]=tvp5150_set_vbi(c,
					 vbi_ram_default,
					 svbi->service_lines[0][i],0xf0,i,3);
			}
			/* Enables FIFO */
			tvp5150_write(c, TVP5150_FIFO_OUT_CTRL,1);
		} else {
			/* Disables FIFO*/
			tvp5150_write(c, TVP5150_FIFO_OUT_CTRL,0);

			/* Disable Full Field */
			tvp5150_write(c, TVP5150_FULL_FIELD_ENA, 0);

			/* Disable Line modes */
			for (i=TVP5150_LINE_MODE_INI; i<=TVP5150_LINE_MODE_END; i++)
				tvp5150_write(c, i, 0xff);
		}
		break;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *fmt;
		struct v4l2_sliced_vbi_format *svbi;

		int i, mask=0;

		fmt = arg;
		if (fmt->type != V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			return -EINVAL;
		svbi = &fmt->fmt.sliced;
		memset(svbi, 0, sizeof(*svbi));

		for (i = 0; i <= 23; i++) {
			svbi->service_lines[0][i]=tvp5150_get_vbi(c,
				vbi_ram_default,i);
			mask|=svbi->service_lines[0][i];
		}
		svbi->service_set=mask;
		break;
	}

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_DBG_G_REGISTER:
	case VIDIOC_DBG_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (!v4l2_chip_match_i2c_client(c, reg->match_type, reg->match_chip))
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (cmd == VIDIOC_DBG_G_REGISTER)
			reg->val = tvp5150_read(c, reg->reg & 0xff);
		else
			tvp5150_write(c, reg->reg & 0xff, reg->val & 0xff);
		break;
	}
#endif

	case VIDIOC_LOG_STATUS:
		dump_reg(c);
		break;

	case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *vt = arg;
			int status = tvp5150_read(c, 0x88);

			vt->signal = ((status & 0x04) && (status & 0x02)) ? 0xffff : 0x0;
			break;
		}
	case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			tvp5150_dbg(1, "VIDIOC_QUERYCTRL called\n");

			for (i = 0; i < ARRAY_SIZE(tvp5150_qctrl); i++)
				if (qc->id && qc->id == tvp5150_qctrl[i].id) {
					memcpy(qc, &(tvp5150_qctrl[i]),
					       sizeof(*qc));
					return 0;
				}

			return -EINVAL;
		}
	case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			tvp5150_dbg(1, "VIDIOC_G_CTRL called\n");

			return tvp5150_get_ctrl(c, ctrl);
		}
	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			u8 i, n;
			n = ARRAY_SIZE(tvp5150_qctrl);
			for (i = 0; i < n; i++)
				if (ctrl->id == tvp5150_qctrl[i].id) {
					if (ctrl->value <
					    tvp5150_qctrl[i].minimum
					    || ctrl->value >
					    tvp5150_qctrl[i].maximum)
						return -ERANGE;
					tvp5150_dbg(1,
						"VIDIOC_S_CTRL: id=%d, value=%d\n",
						ctrl->id, ctrl->value);
					return tvp5150_set_ctrl(c, ctrl);
				}
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}

	return 0;
}

/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/
static struct i2c_driver driver;

static struct i2c_client client_template = {
	.name = "(unset)",
	.driver = &driver,
};

static int tvp5150_detect_client(struct i2c_adapter *adapter,
				 int address, int kind)
{
	struct i2c_client *c;
	struct tvp5150 *core;
	int rv;

	if (debug)
		printk( KERN_INFO
		"tvp5150.c: detecting tvp5150 client on address 0x%x\n",
		address << 1);

	client_template.adapter = adapter;
	client_template.addr = address;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality
	    (adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return 0;

	c = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (c == 0)
		return -ENOMEM;
	memcpy(c, &client_template, sizeof(struct i2c_client));

	core = kzalloc(sizeof(struct tvp5150), GFP_KERNEL);
	if (core == 0) {
		kfree(c);
		return -ENOMEM;
	}
	i2c_set_clientdata(c, core);

	rv = i2c_attach_client(c);

	core->norm = V4L2_STD_ALL;	/* Default is autodetect */
	core->route.input = TVP5150_COMPOSITE1;
	core->enable = 1;
	core->bright = 128;
	core->contrast = 128;
	core->hue = 0;
	core->sat = 128;

	if (rv) {
		kfree(c);
		kfree(core);
		return rv;
	}

	if (debug > 1)
		dump_reg(c);
	return 0;
}

static int tvp5150_attach_adapter(struct i2c_adapter *adapter)
{
	if (debug)
		printk( KERN_INFO
		"tvp5150.c: starting probe for adapter %s (0x%x)\n",
		adapter->name, adapter->id);
	return i2c_probe(adapter, &addr_data, &tvp5150_detect_client);
}

static int tvp5150_detach_client(struct i2c_client *c)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	int err;

	tvp5150_dbg(1,
		"tvp5150.c: removing tvp5150 adapter on address 0x%x\n",
		c->addr << 1);

	err = i2c_detach_client(c);
	if (err) {
		return err;
	}

	kfree(decoder);
	kfree(c);

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.driver = {
		.name = "tvp5150",
	},
	.id = I2C_DRIVERID_TVP5150,

	.attach_adapter = tvp5150_attach_adapter,
	.detach_client = tvp5150_detach_client,

	.command = tvp5150_command,
};

static int __init tvp5150_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit tvp5150_exit(void)
{
	i2c_del_driver(&driver);
}

module_init(tvp5150_init);
module_exit(tvp5150_exit);
