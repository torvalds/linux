/*
 *
 * device driver for philips saa7134 based TV cards
 * video4linux video interface
 *
 * (c) 2001-03 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sort.h>

#include "saa7134-reg.h"
#include "saa7134.h"
#include <media/v4l2-common.h>
#include <media/saa6588.h>

/* ------------------------------------------------------------------ */

unsigned int video_debug;
static unsigned int gbuffers      = 8;
static unsigned int noninterlaced; /* 0 */
static unsigned int gbufsize      = 720*576*4;
static unsigned int gbufsize_max  = 720*576*4;
static char secam[] = "--";
module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug,"enable debug messages [video]");
module_param(gbuffers, int, 0444);
MODULE_PARM_DESC(gbuffers,"number of capture buffers, range 2-32");
module_param(noninterlaced, int, 0644);
MODULE_PARM_DESC(noninterlaced,"capture non interlaced video");
module_param_string(secam, secam, sizeof(secam), 0644);
MODULE_PARM_DESC(secam, "force SECAM variant, either DK,L or Lc");


#define dprintk(fmt, arg...)	if (video_debug&0x04) \
	printk(KERN_DEBUG "%s/video: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */
/* Defines for Video Output Port Register at address 0x191            */

/* Bit 0: VIP code T bit polarity */

#define VP_T_CODE_P_NON_INVERTED	0x00
#define VP_T_CODE_P_INVERTED		0x01

/* ------------------------------------------------------------------ */
/* Defines for Video Output Port Register at address 0x195            */

/* Bit 2: Video output clock delay control */

#define VP_CLK_CTRL2_NOT_DELAYED	0x00
#define VP_CLK_CTRL2_DELAYED		0x04

/* Bit 1: Video output clock invert control */

#define VP_CLK_CTRL1_NON_INVERTED	0x00
#define VP_CLK_CTRL1_INVERTED		0x02

/* ------------------------------------------------------------------ */
/* Defines for Video Output Port Register at address 0x196            */

/* Bits 2 to 0: VSYNC pin video vertical sync type */

#define VP_VS_TYPE_MASK			0x07

#define VP_VS_TYPE_OFF			0x00
#define VP_VS_TYPE_V123			0x01
#define VP_VS_TYPE_V_ITU		0x02
#define VP_VS_TYPE_VGATE_L		0x03
#define VP_VS_TYPE_RESERVED1		0x04
#define VP_VS_TYPE_RESERVED2		0x05
#define VP_VS_TYPE_F_ITU		0x06
#define VP_VS_TYPE_SC_FID		0x07

/* ------------------------------------------------------------------ */
/* data structs for video                                             */

static int video_out[][9] = {
	[CCIR656] = { 0x00, 0xb1, 0x00, 0xa1, 0x00, 0x04, 0x06, 0x00, 0x00 },
};

static struct saa7134_format formats[] = {
	{
		.name     = "8 bpp gray",
		.fourcc   = V4L2_PIX_FMT_GREY,
		.depth    = 8,
		.pm       = 0x06,
	},{
		.name     = "15 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.depth    = 16,
		.pm       = 0x13 | 0x80,
	},{
		.name     = "15 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.depth    = 16,
		.pm       = 0x13 | 0x80,
		.bswap    = 1,
	},{
		.name     = "16 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.depth    = 16,
		.pm       = 0x10 | 0x80,
	},{
		.name     = "16 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.depth    = 16,
		.pm       = 0x10 | 0x80,
		.bswap    = 1,
	},{
		.name     = "24 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.depth    = 24,
		.pm       = 0x11,
	},{
		.name     = "24 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB24,
		.depth    = 24,
		.pm       = 0x11,
		.bswap    = 1,
	},{
		.name     = "32 bpp RGB, le",
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.depth    = 32,
		.pm       = 0x12,
	},{
		.name     = "32 bpp RGB, be",
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.depth    = 32,
		.pm       = 0x12,
		.bswap    = 1,
		.wswap    = 1,
	},{
		.name     = "4:2:2 packed, YUYV",
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.depth    = 16,
		.pm       = 0x00,
		.bswap    = 1,
		.yuv      = 1,
	},{
		.name     = "4:2:2 packed, UYVY",
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.depth    = 16,
		.pm       = 0x00,
		.yuv      = 1,
	},{
		.name     = "4:2:2 planar, Y-Cb-Cr",
		.fourcc   = V4L2_PIX_FMT_YUV422P,
		.depth    = 16,
		.pm       = 0x09,
		.yuv      = 1,
		.planar   = 1,
		.hshift   = 1,
		.vshift   = 0,
	},{
		.name     = "4:2:0 planar, Y-Cb-Cr",
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.depth    = 12,
		.pm       = 0x0a,
		.yuv      = 1,
		.planar   = 1,
		.hshift   = 1,
		.vshift   = 1,
	},{
		.name     = "4:2:0 planar, Y-Cb-Cr",
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.depth    = 12,
		.pm       = 0x0a,
		.yuv      = 1,
		.planar   = 1,
		.uvswap   = 1,
		.hshift   = 1,
		.vshift   = 1,
	}
};
#define FORMATS ARRAY_SIZE(formats)

#define NORM_625_50			\
		.h_start       = 0,	\
		.h_stop        = 719,	\
		.video_v_start = 24,	\
		.video_v_stop  = 311,	\
		.vbi_v_start_0 = 7,	\
		.vbi_v_stop_0  = 22,	\
		.vbi_v_start_1 = 319,   \
		.src_timing    = 4

#define NORM_525_60			\
		.h_start       = 0,	\
		.h_stop        = 719,	\
		.video_v_start = 23,	\
		.video_v_stop  = 262,	\
		.vbi_v_start_0 = 10,	\
		.vbi_v_stop_0  = 21,	\
		.vbi_v_start_1 = 273,	\
		.src_timing    = 7

static struct saa7134_tvnorm tvnorms[] = {
	{
		.name          = "PAL", /* autodetect */
		.id            = V4L2_STD_PAL,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-BG",
		.id            = V4L2_STD_PAL_BG,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-I",
		.id            = V4L2_STD_PAL_I,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-DK",
		.id            = V4L2_STD_PAL_DK,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "NTSC",
		.id            = V4L2_STD_NTSC,
		NORM_525_60,

		.sync_control  = 0x59,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x89,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x0e,
		.vgate_misc    = 0x18,

	},{
		.name          = "SECAM",
		.id            = V4L2_STD_SECAM,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-DK",
		.id            = V4L2_STD_SECAM_DK,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-L",
		.id            = V4L2_STD_SECAM_L,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "SECAM-Lc",
		.id            = V4L2_STD_SECAM_LC,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x1b,
		.chroma_ctrl1  = 0xd1,
		.chroma_gain   = 0x80,
		.chroma_ctrl2  = 0x00,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-M",
		.id            = V4L2_STD_PAL_M,
		NORM_525_60,

		.sync_control  = 0x59,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0xb9,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x0e,
		.vgate_misc    = 0x18,

	},{
		.name          = "PAL-Nc",
		.id            = V4L2_STD_PAL_Nc,
		NORM_625_50,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0xa1,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,

	},{
		.name          = "PAL-60",
		.id            = V4L2_STD_PAL_60,

		.h_start       = 0,
		.h_stop        = 719,
		.video_v_start = 23,
		.video_v_stop  = 262,
		.vbi_v_start_0 = 10,
		.vbi_v_stop_0  = 21,
		.vbi_v_start_1 = 273,
		.src_timing    = 7,

		.sync_control  = 0x18,
		.luma_control  = 0x40,
		.chroma_ctrl1  = 0x81,
		.chroma_gain   = 0x2a,
		.chroma_ctrl2  = 0x06,
		.vgate_misc    = 0x1c,
	}
};
#define TVNORMS ARRAY_SIZE(tvnorms)

#define V4L2_CID_PRIVATE_INVERT      (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_Y_ODD       (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PRIVATE_Y_EVEN      (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PRIVATE_AUTOMUTE    (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 4)

static const struct v4l2_queryctrl no_ctrl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};
static const struct v4l2_queryctrl video_ctrls[] = {
	/* --- video --- */
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 128,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_CONTRAST,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 127,
		.step          = 1,
		.default_value = 68,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_SATURATION,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 127,
		.step          = 1,
		.default_value = 64,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_HUE,
		.name          = "Hue",
		.minimum       = -128,
		.maximum       = 127,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_HFLIP,
		.name          = "Mirror",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},
	/* --- audio --- */
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = -15,
		.maximum       = 15,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
	/* --- private --- */
	{
		.id            = V4L2_CID_PRIVATE_INVERT,
		.name          = "Invert",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_Y_ODD,
		.name          = "y offset odd field",
		.minimum       = 0,
		.maximum       = 128,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_Y_EVEN,
		.name          = "y offset even field",
		.minimum       = 0,
		.maximum       = 128,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_AUTOMUTE,
		.name          = "automute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};
static const unsigned int CTRLS = ARRAY_SIZE(video_ctrls);

static const struct v4l2_queryctrl* ctrl_by_id(unsigned int id)
{
	unsigned int i;

	for (i = 0; i < CTRLS; i++)
		if (video_ctrls[i].id == id)
			return video_ctrls+i;
	return NULL;
}

static struct saa7134_format* format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < FORMATS; i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ----------------------------------------------------------------------- */
/* resource management                                                     */

static int res_get(struct saa7134_dev *dev, struct saa7134_fh *fh, unsigned int bit)
{
	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	mutex_lock(&dev->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		mutex_unlock(&dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk("res: get %d\n",bit);
	mutex_unlock(&dev->lock);
	return 1;
}

static int res_check(struct saa7134_fh *fh, unsigned int bit)
{
	return (fh->resources & bit);
}

static int res_locked(struct saa7134_dev *dev, unsigned int bit)
{
	return (dev->resources & bit);
}

static
void res_free(struct saa7134_dev *dev, struct saa7134_fh *fh, unsigned int bits)
{
	BUG_ON((fh->resources & bits) != bits);

	mutex_lock(&dev->lock);
	fh->resources  &= ~bits;
	dev->resources &= ~bits;
	dprintk("res: put %d\n",bits);
	mutex_unlock(&dev->lock);
}

/* ------------------------------------------------------------------ */

static void set_tvnorm(struct saa7134_dev *dev, struct saa7134_tvnorm *norm)
{
	dprintk("set tv norm = %s\n",norm->name);
	dev->tvnorm = norm;

	/* setup cropping */
	dev->crop_bounds.left    = norm->h_start;
	dev->crop_defrect.left   = norm->h_start;
	dev->crop_bounds.width   = norm->h_stop - norm->h_start +1;
	dev->crop_defrect.width  = norm->h_stop - norm->h_start +1;

	dev->crop_bounds.top     = (norm->vbi_v_stop_0+1)*2;
	dev->crop_defrect.top    = norm->video_v_start*2;
	dev->crop_bounds.height  = ((norm->id & V4L2_STD_525_60) ? 524 : 624)
		- dev->crop_bounds.top;
	dev->crop_defrect.height = (norm->video_v_stop - norm->video_v_start +1)*2;

	dev->crop_current = dev->crop_defrect;

	saa7134_set_tvnorm_hw(dev);
}

static void video_mux(struct saa7134_dev *dev, int input)
{
	dprintk("video input = %d [%s]\n", input, card_in(dev, input).name);
	dev->ctl_input = input;
	set_tvnorm(dev, dev->tvnorm);
	saa7134_tvaudio_setinput(dev, &card_in(dev, input));
}


static void saa7134_set_decoder(struct saa7134_dev *dev)
{
	int luma_control, sync_control, mux;

	struct saa7134_tvnorm *norm = dev->tvnorm;
	mux = card_in(dev, dev->ctl_input).vmux;

	luma_control = norm->luma_control;
	sync_control = norm->sync_control;

	if (mux > 5)
		luma_control |= 0x80; /* svideo */
	if (noninterlaced || dev->nosignal)
		sync_control |= 0x20;

	/* setup video decoder */
	saa_writeb(SAA7134_INCR_DELAY,            0x08);
	saa_writeb(SAA7134_ANALOG_IN_CTRL1,       0xc0 | mux);
	saa_writeb(SAA7134_ANALOG_IN_CTRL2,       0x00);

	saa_writeb(SAA7134_ANALOG_IN_CTRL3,       0x90);
	saa_writeb(SAA7134_ANALOG_IN_CTRL4,       0x90);
	saa_writeb(SAA7134_HSYNC_START,           0xeb);
	saa_writeb(SAA7134_HSYNC_STOP,            0xe0);
	saa_writeb(SAA7134_SOURCE_TIMING1,        norm->src_timing);

	saa_writeb(SAA7134_SYNC_CTRL,             sync_control);
	saa_writeb(SAA7134_LUMA_CTRL,             luma_control);
	saa_writeb(SAA7134_DEC_LUMA_BRIGHT,       dev->ctl_bright);

	saa_writeb(SAA7134_DEC_LUMA_CONTRAST,
		dev->ctl_invert ? -dev->ctl_contrast : dev->ctl_contrast);

	saa_writeb(SAA7134_DEC_CHROMA_SATURATION,
		dev->ctl_invert ? -dev->ctl_saturation : dev->ctl_saturation);

	saa_writeb(SAA7134_DEC_CHROMA_HUE,        dev->ctl_hue);
	saa_writeb(SAA7134_CHROMA_CTRL1,          norm->chroma_ctrl1);
	saa_writeb(SAA7134_CHROMA_GAIN,           norm->chroma_gain);

	saa_writeb(SAA7134_CHROMA_CTRL2,          norm->chroma_ctrl2);
	saa_writeb(SAA7134_MODE_DELAY_CTRL,       0x00);

	saa_writeb(SAA7134_ANALOG_ADC,            0x01);
	saa_writeb(SAA7134_VGATE_START,           0x11);
	saa_writeb(SAA7134_VGATE_STOP,            0xfe);
	saa_writeb(SAA7134_MISC_VGATE_MSB,        norm->vgate_misc);
	saa_writeb(SAA7134_RAW_DATA_GAIN,         0x40);
	saa_writeb(SAA7134_RAW_DATA_OFFSET,       0x80);
}

void saa7134_set_tvnorm_hw(struct saa7134_dev *dev)
{
	saa7134_set_decoder(dev);

	if (card_in(dev, dev->ctl_input).tv)
		saa_call_all(dev, core, s_std, dev->tvnorm->id);
	/* Set the correct norm for the saa6752hs. This function
	   does nothing if there is no saa6752hs. */
	saa_call_empress(dev, core, s_std, dev->tvnorm->id);
}

static void set_h_prescale(struct saa7134_dev *dev, int task, int prescale)
{
	static const struct {
		int xpsc;
		int xacl;
		int xc2_1;
		int xdcg;
		int vpfy;
	} vals[] = {
		/* XPSC XACL XC2_1 XDCG VPFY */
		{    1,   0,    0,    0,   0 },
		{    2,   2,    1,    2,   2 },
		{    3,   4,    1,    3,   2 },
		{    4,   8,    1,    4,   2 },
		{    5,   8,    1,    4,   2 },
		{    6,   8,    1,    4,   3 },
		{    7,   8,    1,    4,   3 },
		{    8,  15,    0,    4,   3 },
		{    9,  15,    0,    4,   3 },
		{   10,  16,    1,    5,   3 },
	};
	static const int count = ARRAY_SIZE(vals);
	int i;

	for (i = 0; i < count; i++)
		if (vals[i].xpsc == prescale)
			break;
	if (i == count)
		return;

	saa_writeb(SAA7134_H_PRESCALE(task), vals[i].xpsc);
	saa_writeb(SAA7134_ACC_LENGTH(task), vals[i].xacl);
	saa_writeb(SAA7134_LEVEL_CTRL(task),
		   (vals[i].xc2_1 << 3) | (vals[i].xdcg));
	saa_andorb(SAA7134_FIR_PREFILTER_CTRL(task), 0x0f,
		   (vals[i].vpfy << 2) | vals[i].vpfy);
}

static void set_v_scale(struct saa7134_dev *dev, int task, int yscale)
{
	int val,mirror;

	saa_writeb(SAA7134_V_SCALE_RATIO1(task), yscale &  0xff);
	saa_writeb(SAA7134_V_SCALE_RATIO2(task), yscale >> 8);

	mirror = (dev->ctl_mirror) ? 0x02 : 0x00;
	if (yscale < 2048) {
		/* LPI */
		dprintk("yscale LPI yscale=%d\n",yscale);
		saa_writeb(SAA7134_V_FILTER(task), 0x00 | mirror);
		saa_writeb(SAA7134_LUMA_CONTRAST(task), 0x40);
		saa_writeb(SAA7134_CHROMA_SATURATION(task), 0x40);
	} else {
		/* ACM */
		val = 0x40 * 1024 / yscale;
		dprintk("yscale ACM yscale=%d val=0x%x\n",yscale,val);
		saa_writeb(SAA7134_V_FILTER(task), 0x01 | mirror);
		saa_writeb(SAA7134_LUMA_CONTRAST(task), val);
		saa_writeb(SAA7134_CHROMA_SATURATION(task), val);
	}
	saa_writeb(SAA7134_LUMA_BRIGHT(task),       0x80);
}

static void set_size(struct saa7134_dev *dev, int task,
		     int width, int height, int interlace)
{
	int prescale,xscale,yscale,y_even,y_odd;
	int h_start, h_stop, v_start, v_stop;
	int div = interlace ? 2 : 1;

	/* setup video scaler */
	h_start = dev->crop_current.left;
	v_start = dev->crop_current.top/2;
	h_stop  = (dev->crop_current.left + dev->crop_current.width -1);
	v_stop  = (dev->crop_current.top + dev->crop_current.height -1)/2;

	saa_writeb(SAA7134_VIDEO_H_START1(task), h_start &  0xff);
	saa_writeb(SAA7134_VIDEO_H_START2(task), h_start >> 8);
	saa_writeb(SAA7134_VIDEO_H_STOP1(task),  h_stop  &  0xff);
	saa_writeb(SAA7134_VIDEO_H_STOP2(task),  h_stop  >> 8);
	saa_writeb(SAA7134_VIDEO_V_START1(task), v_start &  0xff);
	saa_writeb(SAA7134_VIDEO_V_START2(task), v_start >> 8);
	saa_writeb(SAA7134_VIDEO_V_STOP1(task),  v_stop  &  0xff);
	saa_writeb(SAA7134_VIDEO_V_STOP2(task),  v_stop  >> 8);

	prescale = dev->crop_current.width / width;
	if (0 == prescale)
		prescale = 1;
	xscale = 1024 * dev->crop_current.width / prescale / width;
	yscale = 512 * div * dev->crop_current.height / height;
	dprintk("prescale=%d xscale=%d yscale=%d\n",prescale,xscale,yscale);
	set_h_prescale(dev,task,prescale);
	saa_writeb(SAA7134_H_SCALE_INC1(task),      xscale &  0xff);
	saa_writeb(SAA7134_H_SCALE_INC2(task),      xscale >> 8);
	set_v_scale(dev,task,yscale);

	saa_writeb(SAA7134_VIDEO_PIXELS1(task),     width  & 0xff);
	saa_writeb(SAA7134_VIDEO_PIXELS2(task),     width  >> 8);
	saa_writeb(SAA7134_VIDEO_LINES1(task),      height/div & 0xff);
	saa_writeb(SAA7134_VIDEO_LINES2(task),      height/div >> 8);

	/* deinterlace y offsets */
	y_odd  = dev->ctl_y_odd;
	y_even = dev->ctl_y_even;
	saa_writeb(SAA7134_V_PHASE_OFFSET0(task), y_odd);
	saa_writeb(SAA7134_V_PHASE_OFFSET1(task), y_even);
	saa_writeb(SAA7134_V_PHASE_OFFSET2(task), y_odd);
	saa_writeb(SAA7134_V_PHASE_OFFSET3(task), y_even);
}

/* ------------------------------------------------------------------ */

struct cliplist {
	__u16 position;
	__u8  enable;
	__u8  disable;
};

static void set_cliplist(struct saa7134_dev *dev, int reg,
			struct cliplist *cl, int entries, char *name)
{
	__u8 winbits = 0;
	int i;

	for (i = 0; i < entries; i++) {
		winbits |= cl[i].enable;
		winbits &= ~cl[i].disable;
		if (i < 15 && cl[i].position == cl[i+1].position)
			continue;
		saa_writeb(reg + 0, winbits);
		saa_writeb(reg + 2, cl[i].position & 0xff);
		saa_writeb(reg + 3, cl[i].position >> 8);
		dprintk("clip: %s winbits=%02x pos=%d\n",
			name,winbits,cl[i].position);
		reg += 8;
	}
	for (; reg < 0x400; reg += 8) {
		saa_writeb(reg+ 0, 0);
		saa_writeb(reg + 1, 0);
		saa_writeb(reg + 2, 0);
		saa_writeb(reg + 3, 0);
	}
}

static int clip_range(int val)
{
	if (val < 0)
		val = 0;
	return val;
}

/* Sort into smallest position first order */
static int cliplist_cmp(const void *a, const void *b)
{
	const struct cliplist *cla = a;
	const struct cliplist *clb = b;
	if (cla->position < clb->position)
		return -1;
	if (cla->position > clb->position)
		return 1;
	return 0;
}

static int setup_clipping(struct saa7134_dev *dev, struct v4l2_clip *clips,
			  int nclips, int interlace)
{
	struct cliplist col[16], row[16];
	int cols = 0, rows = 0, i;
	int div = interlace ? 2 : 1;

	memset(col, 0, sizeof(col));
	memset(row, 0, sizeof(row));
	for (i = 0; i < nclips && i < 8; i++) {
		col[cols].position = clip_range(clips[i].c.left);
		col[cols].enable   = (1 << i);
		cols++;
		col[cols].position = clip_range(clips[i].c.left+clips[i].c.width);
		col[cols].disable  = (1 << i);
		cols++;
		row[rows].position = clip_range(clips[i].c.top / div);
		row[rows].enable   = (1 << i);
		rows++;
		row[rows].position = clip_range((clips[i].c.top + clips[i].c.height)
						/ div);
		row[rows].disable  = (1 << i);
		rows++;
	}
	sort(col, cols, sizeof col[0], cliplist_cmp, NULL);
	sort(row, rows, sizeof row[0], cliplist_cmp, NULL);
	set_cliplist(dev,0x380,col,cols,"cols");
	set_cliplist(dev,0x384,row,rows,"rows");
	return 0;
}

static int verify_preview(struct saa7134_dev *dev, struct v4l2_window *win)
{
	enum v4l2_field field;
	int maxw, maxh;

	if (NULL == dev->ovbuf.base)
		return -EINVAL;
	if (NULL == dev->ovfmt)
		return -EINVAL;
	if (win->w.width < 48 || win->w.height <  32)
		return -EINVAL;
	if (win->clipcount > 2048)
		return -EINVAL;

	field = win->field;
	maxw  = dev->crop_current.width;
	maxh  = dev->crop_current.height;

	if (V4L2_FIELD_ANY == field) {
		field = (win->w.height > maxh/2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_TOP;
	}
	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		maxh = maxh / 2;
		break;
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		return -EINVAL;
	}

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;
	return 0;
}

static int start_preview(struct saa7134_dev *dev, struct saa7134_fh *fh)
{
	unsigned long base,control,bpl;
	int err;

	err = verify_preview(dev,&fh->win);
	if (0 != err)
		return err;

	dev->ovfield = fh->win.field;
	dprintk("start_preview %dx%d+%d+%d %s field=%s\n",
		fh->win.w.width,fh->win.w.height,
		fh->win.w.left,fh->win.w.top,
		dev->ovfmt->name,v4l2_field_names[dev->ovfield]);

	/* setup window + clipping */
	set_size(dev,TASK_B,fh->win.w.width,fh->win.w.height,
		 V4L2_FIELD_HAS_BOTH(dev->ovfield));
	setup_clipping(dev,fh->clips,fh->nclips,
		       V4L2_FIELD_HAS_BOTH(dev->ovfield));
	if (dev->ovfmt->yuv)
		saa_andorb(SAA7134_DATA_PATH(TASK_B), 0x3f, 0x03);
	else
		saa_andorb(SAA7134_DATA_PATH(TASK_B), 0x3f, 0x01);
	saa_writeb(SAA7134_OFMT_VIDEO_B, dev->ovfmt->pm | 0x20);

	/* dma: setup channel 1 (= Video Task B) */
	base  = (unsigned long)dev->ovbuf.base;
	base += dev->ovbuf.fmt.bytesperline * fh->win.w.top;
	base += dev->ovfmt->depth/8         * fh->win.w.left;
	bpl   = dev->ovbuf.fmt.bytesperline;
	control = SAA7134_RS_CONTROL_BURST_16;
	if (dev->ovfmt->bswap)
		control |= SAA7134_RS_CONTROL_BSWAP;
	if (dev->ovfmt->wswap)
		control |= SAA7134_RS_CONTROL_WSWAP;
	if (V4L2_FIELD_HAS_BOTH(dev->ovfield)) {
		saa_writel(SAA7134_RS_BA1(1),base);
		saa_writel(SAA7134_RS_BA2(1),base+bpl);
		saa_writel(SAA7134_RS_PITCH(1),bpl*2);
		saa_writel(SAA7134_RS_CONTROL(1),control);
	} else {
		saa_writel(SAA7134_RS_BA1(1),base);
		saa_writel(SAA7134_RS_BA2(1),base);
		saa_writel(SAA7134_RS_PITCH(1),bpl);
		saa_writel(SAA7134_RS_CONTROL(1),control);
	}

	/* start dma */
	dev->ovenable = 1;
	saa7134_set_dmabits(dev);

	return 0;
}

static int stop_preview(struct saa7134_dev *dev, struct saa7134_fh *fh)
{
	dev->ovenable = 0;
	saa7134_set_dmabits(dev);
	return 0;
}

/* ------------------------------------------------------------------ */

static int buffer_activate(struct saa7134_dev *dev,
			   struct saa7134_buf *buf,
			   struct saa7134_buf *next)
{
	unsigned long base,control,bpl;
	unsigned long bpl_uv,lines_uv,base2,base3,tmp; /* planar */

	dprintk("buffer_activate buf=%p\n",buf);
	buf->vb.state = VIDEOBUF_ACTIVE;
	buf->top_seen = 0;

	set_size(dev,TASK_A,buf->vb.width,buf->vb.height,
		 V4L2_FIELD_HAS_BOTH(buf->vb.field));
	if (buf->fmt->yuv)
		saa_andorb(SAA7134_DATA_PATH(TASK_A), 0x3f, 0x03);
	else
		saa_andorb(SAA7134_DATA_PATH(TASK_A), 0x3f, 0x01);
	saa_writeb(SAA7134_OFMT_VIDEO_A, buf->fmt->pm);

	/* DMA: setup channel 0 (= Video Task A0) */
	base  = saa7134_buffer_base(buf);
	if (buf->fmt->planar)
		bpl = buf->vb.width;
	else
		bpl = (buf->vb.width * buf->fmt->depth) / 8;
	control = SAA7134_RS_CONTROL_BURST_16 |
		SAA7134_RS_CONTROL_ME |
		(buf->pt->dma >> 12);
	if (buf->fmt->bswap)
		control |= SAA7134_RS_CONTROL_BSWAP;
	if (buf->fmt->wswap)
		control |= SAA7134_RS_CONTROL_WSWAP;
	if (V4L2_FIELD_HAS_BOTH(buf->vb.field)) {
		/* interlaced */
		saa_writel(SAA7134_RS_BA1(0),base);
		saa_writel(SAA7134_RS_BA2(0),base+bpl);
		saa_writel(SAA7134_RS_PITCH(0),bpl*2);
	} else {
		/* non-interlaced */
		saa_writel(SAA7134_RS_BA1(0),base);
		saa_writel(SAA7134_RS_BA2(0),base);
		saa_writel(SAA7134_RS_PITCH(0),bpl);
	}
	saa_writel(SAA7134_RS_CONTROL(0),control);

	if (buf->fmt->planar) {
		/* DMA: setup channel 4+5 (= planar task A) */
		bpl_uv   = bpl >> buf->fmt->hshift;
		lines_uv = buf->vb.height >> buf->fmt->vshift;
		base2    = base + bpl * buf->vb.height;
		base3    = base2 + bpl_uv * lines_uv;
		if (buf->fmt->uvswap)
			tmp = base2, base2 = base3, base3 = tmp;
		dprintk("uv: bpl=%ld lines=%ld base2/3=%ld/%ld\n",
			bpl_uv,lines_uv,base2,base3);
		if (V4L2_FIELD_HAS_BOTH(buf->vb.field)) {
			/* interlaced */
			saa_writel(SAA7134_RS_BA1(4),base2);
			saa_writel(SAA7134_RS_BA2(4),base2+bpl_uv);
			saa_writel(SAA7134_RS_PITCH(4),bpl_uv*2);
			saa_writel(SAA7134_RS_BA1(5),base3);
			saa_writel(SAA7134_RS_BA2(5),base3+bpl_uv);
			saa_writel(SAA7134_RS_PITCH(5),bpl_uv*2);
		} else {
			/* non-interlaced */
			saa_writel(SAA7134_RS_BA1(4),base2);
			saa_writel(SAA7134_RS_BA2(4),base2);
			saa_writel(SAA7134_RS_PITCH(4),bpl_uv);
			saa_writel(SAA7134_RS_BA1(5),base3);
			saa_writel(SAA7134_RS_BA2(5),base3);
			saa_writel(SAA7134_RS_PITCH(5),bpl_uv);
		}
		saa_writel(SAA7134_RS_CONTROL(4),control);
		saa_writel(SAA7134_RS_CONTROL(5),control);
	}

	/* start DMA */
	saa7134_set_dmabits(dev);
	mod_timer(&dev->video_q.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

static int buffer_prepare(struct videobuf_queue *q,
			  struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	struct saa7134_fh *fh = q->priv_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);
	unsigned int size;
	int err;

	/* sanity checks */
	if (NULL == fh->fmt)
		return -EINVAL;
	if (fh->width    < 48 ||
	    fh->height   < 32 ||
	    fh->width/4  > dev->crop_current.width  ||
	    fh->height/4 > dev->crop_current.height ||
	    fh->width    > dev->crop_bounds.width  ||
	    fh->height   > dev->crop_bounds.height)
		return -EINVAL;
	size = (fh->width * fh->height * fh->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	dprintk("buffer_prepare [%d,size=%dx%d,bytes=%d,fields=%s,%s]\n",
		vb->i,fh->width,fh->height,size,v4l2_field_names[field],
		fh->fmt->name);
	if (buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.size   != size       ||
	    buf->vb.field  != field      ||
	    buf->fmt       != fh->fmt) {
		saa7134_dma_free(q,buf);
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);

		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.size   = size;
		buf->vb.field  = field;
		buf->fmt       = fh->fmt;
		buf->pt        = &fh->pt_cap;
		dev->video_q.curr = NULL;

		err = videobuf_iolock(q,&buf->vb,&dev->ovbuf);
		if (err)
			goto oops;
		err = saa7134_pgtable_build(dev->pci,buf->pt,
					    dma->sglist,
					    dma->sglen,
					    saa7134_buffer_startpage(buf));
		if (err)
			goto oops;
	}
	buf->vb.state = VIDEOBUF_PREPARED;
	buf->activate = buffer_activate;
	return 0;

 oops:
	saa7134_dma_free(q,buf);
	return err;
}

static int
buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct saa7134_fh *fh = q->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;
	if (0 == *count)
		*count = gbuffers;
	*count = saa7134_buffer_count(*size,*count);
	return 0;
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct saa7134_fh *fh = q->priv_data;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);

	saa7134_buffer_queue(fh->dev,&fh->dev->video_q,buf);
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);

	saa7134_dma_free(q,buf);
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

int saa7134_g_ctrl_internal(struct saa7134_dev *dev, struct saa7134_fh *fh, struct v4l2_control *c)
{
	const struct v4l2_queryctrl* ctrl;

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		return -EINVAL;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = dev->ctl_bright;
		break;
	case V4L2_CID_HUE:
		c->value = dev->ctl_hue;
		break;
	case V4L2_CID_CONTRAST:
		c->value = dev->ctl_contrast;
		break;
	case V4L2_CID_SATURATION:
		c->value = dev->ctl_saturation;
		break;
	case V4L2_CID_AUDIO_MUTE:
		c->value = dev->ctl_mute;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		c->value = dev->ctl_volume;
		break;
	case V4L2_CID_PRIVATE_INVERT:
		c->value = dev->ctl_invert;
		break;
	case V4L2_CID_HFLIP:
		c->value = dev->ctl_mirror;
		break;
	case V4L2_CID_PRIVATE_Y_EVEN:
		c->value = dev->ctl_y_even;
		break;
	case V4L2_CID_PRIVATE_Y_ODD:
		c->value = dev->ctl_y_odd;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		c->value = dev->ctl_automute;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_g_ctrl_internal);

static int saa7134_g_ctrl(struct file *file, void *priv, struct v4l2_control *c)
{
	struct saa7134_fh *fh = priv;

	return saa7134_g_ctrl_internal(fh->dev, fh, c);
}

int saa7134_s_ctrl_internal(struct saa7134_dev *dev,  struct saa7134_fh *fh, struct v4l2_control *c)
{
	const struct v4l2_queryctrl* ctrl;
	unsigned long flags;
	int restart_overlay = 0;
	int err;

	err = -EINVAL;

	mutex_lock(&dev->lock);

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		goto error;

	dprintk("set_control name=%s val=%d\n",ctrl->name,c->value);
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER:
		if (c->value < ctrl->minimum)
			c->value = ctrl->minimum;
		if (c->value > ctrl->maximum)
			c->value = ctrl->maximum;
		break;
	default:
		/* nothing */;
	}
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		dev->ctl_bright = c->value;
		saa_writeb(SAA7134_DEC_LUMA_BRIGHT, dev->ctl_bright);
		break;
	case V4L2_CID_HUE:
		dev->ctl_hue = c->value;
		saa_writeb(SAA7134_DEC_CHROMA_HUE, dev->ctl_hue);
		break;
	case V4L2_CID_CONTRAST:
		dev->ctl_contrast = c->value;
		saa_writeb(SAA7134_DEC_LUMA_CONTRAST,
			   dev->ctl_invert ? -dev->ctl_contrast : dev->ctl_contrast);
		break;
	case V4L2_CID_SATURATION:
		dev->ctl_saturation = c->value;
		saa_writeb(SAA7134_DEC_CHROMA_SATURATION,
			   dev->ctl_invert ? -dev->ctl_saturation : dev->ctl_saturation);
		break;
	case V4L2_CID_AUDIO_MUTE:
		dev->ctl_mute = c->value;
		saa7134_tvaudio_setmute(dev);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		dev->ctl_volume = c->value;
		saa7134_tvaudio_setvolume(dev,dev->ctl_volume);
		break;
	case V4L2_CID_PRIVATE_INVERT:
		dev->ctl_invert = c->value;
		saa_writeb(SAA7134_DEC_LUMA_CONTRAST,
			   dev->ctl_invert ? -dev->ctl_contrast : dev->ctl_contrast);
		saa_writeb(SAA7134_DEC_CHROMA_SATURATION,
			   dev->ctl_invert ? -dev->ctl_saturation : dev->ctl_saturation);
		break;
	case V4L2_CID_HFLIP:
		dev->ctl_mirror = c->value;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_Y_EVEN:
		dev->ctl_y_even = c->value;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_Y_ODD:
		dev->ctl_y_odd = c->value;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
	{
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &dev->tda9887_conf;

		dev->ctl_automute = c->value;
		if (dev->tda9887_conf) {
			if (dev->ctl_automute)
				dev->tda9887_conf |= TDA9887_AUTOMUTE;
			else
				dev->tda9887_conf &= ~TDA9887_AUTOMUTE;

			saa_call_all(dev, tuner, s_config, &tda9887_cfg);
		}
		break;
	}
	default:
		goto error;
	}
	if (restart_overlay && fh && res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock,flags);
		stop_preview(dev,fh);
		start_preview(dev,fh);
		spin_unlock_irqrestore(&dev->slock,flags);
	}
	err = 0;

error:
	mutex_unlock(&dev->lock);
	return err;
}
EXPORT_SYMBOL_GPL(saa7134_s_ctrl_internal);

static int saa7134_s_ctrl(struct file *file, void *f, struct v4l2_control *c)
{
	struct saa7134_fh *fh = f;

	return saa7134_s_ctrl_internal(fh->dev, fh, c);
}

/* ------------------------------------------------------------------ */

static struct videobuf_queue* saa7134_queue(struct saa7134_fh *fh)
{
	struct videobuf_queue* q = NULL;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		q = &fh->cap;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		q = &fh->vbi;
		break;
	default:
		BUG();
	}
	return q;
}

static int saa7134_resource(struct saa7134_fh *fh)
{
	if (fh->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return RESOURCE_VIDEO;

	if (fh->type == V4L2_BUF_TYPE_VBI_CAPTURE)
		return RESOURCE_VBI;

	BUG();
	return 0;
}

static int video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh;
	enum v4l2_buf_type type = 0;
	int radio = 0;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	case VFL_TYPE_VBI:
		type = V4L2_BUF_TYPE_VBI_CAPTURE;
		break;
	case VFL_TYPE_RADIO:
		radio = 1;
		break;
	}

	dprintk("open dev=%s radio=%d type=%s\n", video_device_node_name(vdev),
		radio, v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	fh->type     = type;
	fh->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);
	fh->width    = 720;
	fh->height   = 576;

	videobuf_queue_sg_init(&fh->cap, &video_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct saa7134_buf),
			    fh, NULL);
	videobuf_queue_sg_init(&fh->vbi, &saa7134_vbi_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct saa7134_buf),
			    fh, NULL);
	saa7134_pgtable_alloc(dev->pci,&fh->pt_cap);
	saa7134_pgtable_alloc(dev->pci,&fh->pt_vbi);

	if (fh->radio) {
		/* switch to radio mode */
		saa7134_tvaudio_setinput(dev,&card(dev).radio);
		saa_call_all(dev, tuner, s_radio);
	} else {
		/* switch to video/vbi mode */
		video_mux(dev,dev->ctl_input);
	}
	v4l2_fh_add(&fh->fh);

	return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct saa7134_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev,RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(saa7134_queue(fh),
					 data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (!res_get(fh->dev,fh,RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(saa7134_queue(fh),
					    data, count, ppos, 1,
					    file->f_flags & O_NONBLOCK);
		break;
	default:
		BUG();
		return 0;
	}
}

static unsigned int
video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct saa7134_fh *fh = file->private_data;
	struct videobuf_buffer *buf = NULL;
	unsigned int rc = 0;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type)
		return videobuf_poll_stream(file, &fh->vbi, wait);

	if (res_check(fh,RESOURCE_VIDEO)) {
		mutex_lock(&fh->cap.vb_lock);
		if (!list_empty(&fh->cap.stream))
			buf = list_entry(fh->cap.stream.next, struct videobuf_buffer, stream);
	} else {
		mutex_lock(&fh->cap.vb_lock);
		if (UNSET == fh->cap.read_off) {
			/* need to capture a new frame */
			if (res_locked(fh->dev,RESOURCE_VIDEO))
				goto err;
			if (0 != fh->cap.ops->buf_prepare(&fh->cap,fh->cap.read_buf,fh->cap.field))
				goto err;
			fh->cap.ops->buf_queue(&fh->cap,fh->cap.read_buf);
			fh->cap.read_off = 0;
		}
		buf = fh->cap.read_buf;
	}

	if (!buf)
		goto err;

	poll_wait(file, &buf->done, wait);
	if (buf->state == VIDEOBUF_DONE ||
	    buf->state == VIDEOBUF_ERROR)
		rc = POLLIN|POLLRDNORM;
	mutex_unlock(&fh->cap.vb_lock);
	return rc;

err:
	mutex_unlock(&fh->cap.vb_lock);
	return POLLERR;
}

static int video_release(struct file *file)
{
	struct saa7134_fh  *fh  = file->private_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa6588_command cmd;
	unsigned long flags;

	saa7134_tvaudio_close(dev);

	/* turn off overlay */
	if (res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock,flags);
		stop_preview(dev,fh);
		spin_unlock_irqrestore(&dev->slock,flags);
		res_free(dev,fh,RESOURCE_OVERLAY);
	}

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		videobuf_streamoff(&fh->cap);
		res_free(dev,fh,RESOURCE_VIDEO);
	}
	if (fh->cap.read_buf) {
		buffer_release(&fh->cap,fh->cap.read_buf);
		kfree(fh->cap.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		videobuf_stop(&fh->vbi);
		res_free(dev,fh,RESOURCE_VBI);
	}

	/* ts-capture will not work in planar mode, so turn it off Hac: 04.05*/
	saa_andorb(SAA7134_OFMT_VIDEO_A, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_VIDEO_B, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_DATA_A, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_DATA_B, 0x1f, 0);

	saa_call_all(dev, core, s_power, 0);
	if (fh->radio)
		saa_call_all(dev, core, ioctl, SAA6588_CMD_CLOSE, &cmd);

	/* free stuff */
	videobuf_mmap_free(&fh->cap);
	videobuf_mmap_free(&fh->vbi);
	saa7134_pgtable_free(dev->pci,&fh->pt_cap);
	saa7134_pgtable_free(dev->pci,&fh->pt_vbi);

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct saa7134_fh *fh = file->private_data;

	return videobuf_mmap_mapper(saa7134_queue(fh), vma);
}

static ssize_t radio_read(struct file *file, char __user *data,
			 size_t count, loff_t *ppos)
{
	struct saa7134_fh *fh = file->private_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa6588_command cmd;

	cmd.block_count = count/3;
	cmd.buffer = data;
	cmd.instance = file;
	cmd.result = -ENODEV;

	saa_call_all(dev, core, ioctl, SAA6588_CMD_READ, &cmd);

	return cmd.result;
}

static unsigned int radio_poll(struct file *file, poll_table *wait)
{
	struct saa7134_fh *fh = file->private_data;
	struct saa7134_dev *dev = fh->dev;
	struct saa6588_command cmd;

	cmd.instance = file;
	cmd.event_list = wait;
	cmd.result = -ENODEV;
	saa_call_all(dev, core, ioctl, SAA6588_CMD_POLL, &cmd);

	return cmd.result;
}

/* ------------------------------------------------------------------ */

static int saa7134_try_get_set_fmt_vbi_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_tvnorm *norm = dev->tvnorm;

	memset(&f->fmt.vbi.reserved, 0, sizeof(f->fmt.vbi.reserved));
	f->fmt.vbi.sampling_rate = 6750000 * 4;
	f->fmt.vbi.samples_per_line = 2048 /* VBI_LINE_LENGTH */;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 64 * 4;
	f->fmt.vbi.start[0] = norm->vbi_v_start_0;
	f->fmt.vbi.count[0] = norm->vbi_v_stop_0 - norm->vbi_v_start_0 +1;
	f->fmt.vbi.start[1] = norm->vbi_v_start_1;
	f->fmt.vbi.count[1] = f->fmt.vbi.count[0];
	f->fmt.vbi.flags = 0; /* VBI_UNSYNC VBI_INTERLACED */

	return 0;
}

static int saa7134_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;

	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->cap.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int saa7134_g_fmt_vid_overlay(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}
	f->fmt.win = fh->win;

	return 0;
}

static int saa7134_try_fmt_vid_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_format *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;
	maxw  = min(dev->crop_current.width*4,  dev->crop_bounds.width);
	maxh  = min(dev->crop_current.height*4, dev->crop_bounds.height);

	if (V4L2_FIELD_ANY == field) {
		field = (f->fmt.pix.height > maxh/2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
	}
	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		maxh = maxh / 2;
		break;
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		return -EINVAL;
	}

	f->fmt.pix.field = field;
	if (f->fmt.pix.width  < 48)
		f->fmt.pix.width  = 48;
	if (f->fmt.pix.height < 32)
		f->fmt.pix.height = 32;
	if (f->fmt.pix.width > maxw)
		f->fmt.pix.width = maxw;
	if (f->fmt.pix.height > maxh)
		f->fmt.pix.height = maxh;
	f->fmt.pix.width &= ~0x03;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int saa7134_try_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}

	return verify_preview(dev, &f->fmt.win);
}

static int saa7134_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;
	int err;

	err = saa7134_try_fmt_vid_cap(file, priv, f);
	if (0 != err)
		return err;

	fh->fmt       = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->width     = f->fmt.pix.width;
	fh->height    = f->fmt.pix.height;
	fh->cap.field = f->fmt.pix.field;
	return 0;
}

static int saa7134_s_fmt_vid_overlay(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	int err;
	unsigned long flags;

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}
	err = verify_preview(dev, &f->fmt.win);
	if (0 != err)
		return err;

	mutex_lock(&dev->lock);

	fh->win    = f->fmt.win;
	fh->nclips = f->fmt.win.clipcount;

	if (fh->nclips > 8)
		fh->nclips = 8;

	if (copy_from_user(fh->clips, f->fmt.win.clips,
			   sizeof(struct v4l2_clip)*fh->nclips)) {
		mutex_unlock(&dev->lock);
		return -EFAULT;
	}

	if (res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev, fh);
		start_preview(dev, fh);
		spin_unlock_irqrestore(&dev->slock, flags);
	}

	mutex_unlock(&dev->lock);
	return 0;
}

int saa7134_queryctrl(struct file *file, void *priv, struct v4l2_queryctrl *c)
{
	const struct v4l2_queryctrl *ctrl;

	if ((c->id <  V4L2_CID_BASE ||
	     c->id >= V4L2_CID_LASTP1) &&
	    (c->id <  V4L2_CID_PRIVATE_BASE ||
	     c->id >= V4L2_CID_PRIVATE_LASTP1))
		return -EINVAL;
	ctrl = ctrl_by_id(c->id);
	*c = (NULL != ctrl) ? *ctrl : no_ctrl;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_queryctrl);

static int saa7134_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	unsigned int n;

	n = i->index;
	if (n >= SAA7134_INPUT_MAX)
		return -EINVAL;
	if (NULL == card_in(dev, i->index).name)
		return -EINVAL;
	i->index = n;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, card_in(dev, n).name);
	if (card_in(dev, n).tv)
		i->type = V4L2_INPUT_TYPE_TUNER;
	if (n == dev->ctl_input) {
		int v1 = saa_readb(SAA7134_STATUS_VIDEO1);
		int v2 = saa_readb(SAA7134_STATUS_VIDEO2);

		if (0 != (v1 & 0x40))
			i->status |= V4L2_IN_ST_NO_H_LOCK;
		if (0 != (v2 & 0x40))
			i->status |= V4L2_IN_ST_NO_SYNC;
		if (0 != (v2 & 0x0e))
			i->status |= V4L2_IN_ST_MACROVISION;
	}
	i->std = SAA7134_NORMS;
	return 0;
}

static int saa7134_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	*i = dev->ctl_input;
	return 0;
}

static int saa7134_s_input(struct file *file, void *priv, unsigned int i)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	if (i >= SAA7134_INPUT_MAX)
		return -EINVAL;
	if (NULL == card_in(dev, i).name)
		return -EINVAL;
	mutex_lock(&dev->lock);
	video_mux(dev, i);
	mutex_unlock(&dev->lock);
	return 0;
}

static int saa7134_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	struct video_device *vdev = video_devdata(file);
	u32 radio_caps, video_caps, vbi_caps;

	unsigned int tuner_type = dev->tuner_type;

	strcpy(cap->driver, "saa7134");
	strlcpy(cap->card, saa7134_boards[dev->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));

	cap->device_caps = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	if ((tuner_type != TUNER_ABSENT) && (tuner_type != UNSET))
		cap->device_caps |= V4L2_CAP_TUNER;

	radio_caps = V4L2_CAP_RADIO;
	if (dev->has_rds)
		radio_caps |= V4L2_CAP_RDS_CAPTURE;

	video_caps = V4L2_CAP_VIDEO_CAPTURE;
	if (saa7134_no_overlay <= 0)
		video_caps |= V4L2_CAP_VIDEO_OVERLAY;

	vbi_caps = V4L2_CAP_VBI_CAPTURE;

	switch (vdev->vfl_type) {
	case VFL_TYPE_RADIO:
		cap->device_caps |= radio_caps;
		break;
	case VFL_TYPE_GRABBER:
		cap->device_caps |= video_caps;
		break;
	case VFL_TYPE_VBI:
		cap->device_caps |= vbi_caps;
		break;
	}
	cap->capabilities = radio_caps | video_caps | vbi_caps |
		cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	if (vdev->vfl_type == VFL_TYPE_RADIO) {
		cap->device_caps &= ~V4L2_CAP_STREAMING;
		if (!dev->has_rds)
			cap->device_caps &= ~V4L2_CAP_READWRITE;
	}

	return 0;
}

int saa7134_s_std_internal(struct saa7134_dev *dev, struct saa7134_fh *fh, v4l2_std_id id)
{
	unsigned long flags;
	unsigned int i;
	v4l2_std_id fixup;

	if (!fh && res_locked(dev, RESOURCE_OVERLAY)) {
		/* Don't change the std from the mpeg device
		   if overlay is active. */
		return -EBUSY;
	}

	for (i = 0; i < TVNORMS; i++)
		if (id == tvnorms[i].id)
			break;

	if (i == TVNORMS)
		for (i = 0; i < TVNORMS; i++)
			if (id & tvnorms[i].id)
				break;
	if (i == TVNORMS)
		return -EINVAL;

	if ((id & V4L2_STD_SECAM) && (secam[0] != '-')) {
		if (secam[0] == 'L' || secam[0] == 'l') {
			if (secam[1] == 'C' || secam[1] == 'c')
				fixup = V4L2_STD_SECAM_LC;
			else
				fixup = V4L2_STD_SECAM_L;
		} else {
			if (secam[0] == 'D' || secam[0] == 'd')
				fixup = V4L2_STD_SECAM_DK;
			else
				fixup = V4L2_STD_SECAM;
		}
		for (i = 0; i < TVNORMS; i++) {
			if (fixup == tvnorms[i].id)
				break;
		}
		if (i == TVNORMS)
			return -EINVAL;
	}

	id = tvnorms[i].id;

	mutex_lock(&dev->lock);
	if (fh && res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev, fh);
		spin_unlock_irqrestore(&dev->slock, flags);

		set_tvnorm(dev, &tvnorms[i]);

		spin_lock_irqsave(&dev->slock, flags);
		start_preview(dev, fh);
		spin_unlock_irqrestore(&dev->slock, flags);
	} else
		set_tvnorm(dev, &tvnorms[i]);

	saa7134_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_s_std_internal);

static int saa7134_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct saa7134_fh *fh = priv;

	return saa7134_s_std_internal(fh->dev, fh, id);
}

static int saa7134_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	*id = dev->tvnorm->id;
	return 0;
}

static int saa7134_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cap)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	if (cap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    cap->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;
	cap->bounds  = dev->crop_bounds;
	cap->defrect = dev->crop_defrect;
	cap->pixelaspect.numerator   = 1;
	cap->pixelaspect.denominator = 1;
	if (dev->tvnorm->id & V4L2_STD_525_60) {
		cap->pixelaspect.numerator   = 11;
		cap->pixelaspect.denominator = 10;
	}
	if (dev->tvnorm->id & V4L2_STD_625_50) {
		cap->pixelaspect.numerator   = 54;
		cap->pixelaspect.denominator = 59;
	}
	return 0;
}

static int saa7134_g_crop(struct file *file, void *f, struct v4l2_crop *crop)
{
	struct saa7134_fh *fh = f;
	struct saa7134_dev *dev = fh->dev;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;
	crop->c = dev->crop_current;
	return 0;
}

static int saa7134_s_crop(struct file *file, void *f, const struct v4l2_crop *crop)
{
	struct saa7134_fh *fh = f;
	struct saa7134_dev *dev = fh->dev;
	struct v4l2_rect *b = &dev->crop_bounds;
	struct v4l2_rect *c = &dev->crop_current;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;
	if (crop->c.height < 0)
		return -EINVAL;
	if (crop->c.width < 0)
		return -EINVAL;

	if (res_locked(fh->dev, RESOURCE_OVERLAY))
		return -EBUSY;
	if (res_locked(fh->dev, RESOURCE_VIDEO))
		return -EBUSY;

	*c = crop->c;
	if (c->top < b->top)
		c->top = b->top;
	if (c->top > b->top + b->height)
		c->top = b->top + b->height;
	if (c->height > b->top - c->top + b->height)
		c->height = b->top - c->top + b->height;

	if (c->left < b->left)
		c->left = b->left;
	if (c->left > b->left + b->width)
		c->left = b->left + b->width;
	if (c->width > b->left - c->left + b->width)
		c->width = b->left - c->left + b->width;
	return 0;
}

static int saa7134_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	int n;

	if (0 != t->index)
		return -EINVAL;
	memset(t, 0, sizeof(*t));
	for (n = 0; n < SAA7134_INPUT_MAX; n++) {
		if (card_in(dev, n).tv)
			break;
	}
	if (n == SAA7134_INPUT_MAX)
		return -EINVAL;
	if (NULL != card_in(dev, n).name) {
		strcpy(t->name, "Television");
		t->type = V4L2_TUNER_ANALOG_TV;
		saa_call_all(dev, tuner, g_tuner, t);
		t->capability = V4L2_TUNER_CAP_NORM |
			V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 |
			V4L2_TUNER_CAP_LANG2;
		t->rxsubchans = saa7134_tvaudio_getstereo(dev);
		t->audmode = saa7134_tvaudio_rx2mode(t->rxsubchans);
	}
	if (0 != (saa_readb(SAA7134_STATUS_VIDEO1) & 0x03))
		t->signal = 0xffff;
	return 0;
}

static int saa7134_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	int rx, mode;

	if (0 != t->index)
		return -EINVAL;

	mode = dev->thread.mode;
	if (UNSET == mode) {
		rx   = saa7134_tvaudio_getstereo(dev);
		mode = saa7134_tvaudio_rx2mode(rx);
	}
	if (mode != t->audmode)
		dev->thread.mode = t->audmode;

	return 0;
}

static int saa7134_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	if (0 != f->tuner)
		return -EINVAL;

	f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
	saa_call_all(dev, tuner, g_frequency, f);

	return 0;
}

static int saa7134_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *f)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	if (0 != f->tuner)
		return -EINVAL;
	if (0 == fh->radio && V4L2_TUNER_ANALOG_TV != f->type)
		return -EINVAL;
	if (1 == fh->radio && V4L2_TUNER_RADIO != f->type)
		return -EINVAL;
	mutex_lock(&dev->lock);

	saa_call_all(dev, tuner, s_frequency, f);

	saa7134_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}

static int saa7134_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index >= FORMATS)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name,
		sizeof(f->description));

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int saa7134_enum_fmt_vid_overlay(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}

	if ((f->index >= FORMATS) || formats[f->index].planar)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name,
		sizeof(f->description));

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int saa7134_g_fbuf(struct file *file, void *f,
				struct v4l2_framebuffer *fb)
{
	struct saa7134_fh *fh = f;
	struct saa7134_dev *dev = fh->dev;

	*fb = dev->ovbuf;
	fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

	return 0;
}

static int saa7134_s_fbuf(struct file *file, void *f,
					const struct v4l2_framebuffer *fb)
{
	struct saa7134_fh *fh = f;
	struct saa7134_dev *dev = fh->dev;
	struct saa7134_format *fmt;

	if (!capable(CAP_SYS_ADMIN) &&
	   !capable(CAP_SYS_RAWIO))
		return -EPERM;

	/* check args */
	fmt = format_by_fourcc(fb->fmt.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	/* ok, accept it */
	dev->ovbuf = *fb;
	dev->ovfmt = fmt;
	if (0 == dev->ovbuf.fmt.bytesperline)
		dev->ovbuf.fmt.bytesperline =
			dev->ovbuf.fmt.width*fmt->depth/8;
	return 0;
}

static int saa7134_overlay(struct file *file, void *f, unsigned int on)
{
	struct saa7134_fh *fh = f;
	struct saa7134_dev *dev = fh->dev;
	unsigned long flags;

	if (on) {
		if (saa7134_no_overlay > 0) {
			dprintk("no_overlay\n");
			return -EINVAL;
		}

		if (!res_get(dev, fh, RESOURCE_OVERLAY))
			return -EBUSY;
		spin_lock_irqsave(&dev->slock, flags);
		start_preview(dev, fh);
		spin_unlock_irqrestore(&dev->slock, flags);
	}
	if (!on) {
		if (!res_check(fh, RESOURCE_OVERLAY))
			return -EINVAL;
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev, fh);
		spin_unlock_irqrestore(&dev->slock, flags);
		res_free(dev, fh, RESOURCE_OVERLAY);
	}
	return 0;
}

static int saa7134_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *p)
{
	struct saa7134_fh *fh = priv;
	return videobuf_reqbufs(saa7134_queue(fh), p);
}

static int saa7134_querybuf(struct file *file, void *priv,
					struct v4l2_buffer *b)
{
	struct saa7134_fh *fh = priv;
	return videobuf_querybuf(saa7134_queue(fh), b);
}

static int saa7134_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct saa7134_fh *fh = priv;
	return videobuf_qbuf(saa7134_queue(fh), b);
}

static int saa7134_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct saa7134_fh *fh = priv;
	return videobuf_dqbuf(saa7134_queue(fh), b,
				file->f_flags & O_NONBLOCK);
}

static int saa7134_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	int res = saa7134_resource(fh);

	if (!res_get(dev, fh, res))
		return -EBUSY;

	/* The SAA7134 has a 1K FIFO; the datasheet suggests that when
	 * configured conservatively, there's 22 usec of buffering for video.
	 * We therefore request a DMA latency of 20 usec, giving us 2 usec of
	 * margin in case the FIFO is configured differently to the datasheet.
	 * Unfortunately, I lack register-level documentation to check the
	 * Linux FIFO setup and confirm the perfect value.
	 */
	pm_qos_add_request(&fh->qos_request,
			   PM_QOS_CPU_DMA_LATENCY,
			   20);

	return videobuf_streamon(saa7134_queue(fh));
}

static int saa7134_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	int err;
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;
	int res = saa7134_resource(fh);

	pm_qos_remove_request(&fh->qos_request);

	err = videobuf_streamoff(saa7134_queue(fh));
	if (err < 0)
		return err;
	res_free(dev, fh, res);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register (struct file *file, void *priv,
			      struct v4l2_dbg_register *reg)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	reg->val = saa_readb(reg->reg & 0xffffff);
	reg->size = 1;
	return 0;
}

static int vidioc_s_register (struct file *file, void *priv,
				const struct v4l2_dbg_register *reg)
{
	struct saa7134_fh *fh = priv;
	struct saa7134_dev *dev = fh->dev;

	saa_writeb(reg->reg & 0xffffff, reg->val);
	return 0;
}
#endif

static int radio_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct saa7134_fh *fh = file->private_data;
	struct saa7134_dev *dev = fh->dev;

	if (0 != t->index)
		return -EINVAL;

	memset(t, 0, sizeof(*t));
	strcpy(t->name, "Radio");
	t->type = V4L2_TUNER_RADIO;

	saa_call_all(dev, tuner, g_tuner, t);
	t->audmode &= V4L2_TUNER_MODE_MONO | V4L2_TUNER_MODE_STEREO;
	if (dev->input->amux == TV) {
		t->signal = 0xf800 - ((saa_readb(0x581) & 0x1f) << 11);
		t->rxsubchans = (saa_readb(0x529) & 0x08) ?
				V4L2_TUNER_SUB_STEREO : V4L2_TUNER_SUB_MONO;
	}
	return 0;
}
static int radio_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct saa7134_fh *fh = file->private_data;
	struct saa7134_dev *dev = fh->dev;

	if (0 != t->index)
		return -EINVAL;

	saa_call_all(dev, tuner, s_tuner, t);
	return 0;
}

static int radio_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	if (i->index != 0)
		return -EINVAL;

	strcpy(i->name, "Radio");
	i->type = V4L2_INPUT_TYPE_TUNER;

	return 0;
}

static int radio_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int radio_s_input(struct file *filp, void *priv, unsigned int i)
{
	return 0;
}

static int radio_s_std(struct file *file, void *fh, v4l2_std_id norm)
{
	return 0;
}

static int radio_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *c)
{
	const struct v4l2_queryctrl *ctrl;

	if (c->id <  V4L2_CID_BASE ||
	    c->id >= V4L2_CID_LASTP1)
		return -EINVAL;
	if (c->id == V4L2_CID_AUDIO_MUTE) {
		ctrl = ctrl_by_id(c->id);
		*c = *ctrl;
	} else
		*c = no_ctrl;
	return 0;
}

static const struct v4l2_file_operations video_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = video_open,
	.release  = video_release,
	.read	  = video_read,
	.poll     = video_poll,
	.mmap	  = video_mmap,
	.ioctl	  = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap		= saa7134_querycap,
	.vidioc_enum_fmt_vid_cap	= saa7134_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= saa7134_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= saa7134_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= saa7134_s_fmt_vid_cap,
	.vidioc_enum_fmt_vid_overlay	= saa7134_enum_fmt_vid_overlay,
	.vidioc_g_fmt_vid_overlay	= saa7134_g_fmt_vid_overlay,
	.vidioc_try_fmt_vid_overlay	= saa7134_try_fmt_vid_overlay,
	.vidioc_s_fmt_vid_overlay	= saa7134_s_fmt_vid_overlay,
	.vidioc_g_fmt_vbi_cap		= saa7134_try_get_set_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap		= saa7134_try_get_set_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap		= saa7134_try_get_set_fmt_vbi_cap,
	.vidioc_cropcap			= saa7134_cropcap,
	.vidioc_reqbufs			= saa7134_reqbufs,
	.vidioc_querybuf		= saa7134_querybuf,
	.vidioc_qbuf			= saa7134_qbuf,
	.vidioc_dqbuf			= saa7134_dqbuf,
	.vidioc_s_std			= saa7134_s_std,
	.vidioc_g_std			= saa7134_g_std,
	.vidioc_enum_input		= saa7134_enum_input,
	.vidioc_g_input			= saa7134_g_input,
	.vidioc_s_input			= saa7134_s_input,
	.vidioc_queryctrl		= saa7134_queryctrl,
	.vidioc_g_ctrl			= saa7134_g_ctrl,
	.vidioc_s_ctrl			= saa7134_s_ctrl,
	.vidioc_streamon		= saa7134_streamon,
	.vidioc_streamoff		= saa7134_streamoff,
	.vidioc_g_tuner			= saa7134_g_tuner,
	.vidioc_s_tuner			= saa7134_s_tuner,
	.vidioc_g_crop			= saa7134_g_crop,
	.vidioc_s_crop			= saa7134_s_crop,
	.vidioc_g_fbuf			= saa7134_g_fbuf,
	.vidioc_s_fbuf			= saa7134_s_fbuf,
	.vidioc_overlay			= saa7134_overlay,
	.vidioc_g_frequency		= saa7134_g_frequency,
	.vidioc_s_frequency		= saa7134_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register              = vidioc_g_register,
	.vidioc_s_register              = vidioc_s_register,
#endif
};

static const struct v4l2_file_operations radio_fops = {
	.owner	  = THIS_MODULE,
	.open	  = video_open,
	.read     = radio_read,
	.release  = video_release,
	.ioctl	  = video_ioctl2,
	.poll     = radio_poll,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap	= saa7134_querycap,
	.vidioc_g_tuner		= radio_g_tuner,
	.vidioc_enum_input	= radio_enum_input,
	.vidioc_s_tuner		= radio_s_tuner,
	.vidioc_s_input		= radio_s_input,
	.vidioc_s_std		= radio_s_std,
	.vidioc_queryctrl	= radio_queryctrl,
	.vidioc_g_input		= radio_g_input,
	.vidioc_g_ctrl		= saa7134_g_ctrl,
	.vidioc_s_ctrl		= saa7134_s_ctrl,
	.vidioc_g_frequency	= saa7134_g_frequency,
	.vidioc_s_frequency	= saa7134_s_frequency,
};

/* ----------------------------------------------------------- */
/* exported stuff                                              */

struct video_device saa7134_video_template = {
	.name				= "saa7134-video",
	.fops				= &video_fops,
	.ioctl_ops 			= &video_ioctl_ops,
	.tvnorms			= SAA7134_NORMS,
};

struct video_device saa7134_radio_template = {
	.name			= "saa7134-radio",
	.fops			= &radio_fops,
	.ioctl_ops 		= &radio_ioctl_ops,
};

int saa7134_video_init1(struct saa7134_dev *dev)
{
	/* sanitycheck insmod options */
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsize > gbufsize_max)
		gbufsize = gbufsize_max;
	gbufsize = (gbufsize + PAGE_SIZE - 1) & PAGE_MASK;

	/* put some sensible defaults into the data structures ... */
	dev->ctl_bright     = ctrl_by_id(V4L2_CID_BRIGHTNESS)->default_value;
	dev->ctl_contrast   = ctrl_by_id(V4L2_CID_CONTRAST)->default_value;
	dev->ctl_hue        = ctrl_by_id(V4L2_CID_HUE)->default_value;
	dev->ctl_saturation = ctrl_by_id(V4L2_CID_SATURATION)->default_value;
	dev->ctl_volume     = ctrl_by_id(V4L2_CID_AUDIO_VOLUME)->default_value;
	dev->ctl_mute       = 1; // ctrl_by_id(V4L2_CID_AUDIO_MUTE)->default_value;
	dev->ctl_invert     = ctrl_by_id(V4L2_CID_PRIVATE_INVERT)->default_value;
	dev->ctl_automute   = ctrl_by_id(V4L2_CID_PRIVATE_AUTOMUTE)->default_value;

	if (dev->tda9887_conf && dev->ctl_automute)
		dev->tda9887_conf |= TDA9887_AUTOMUTE;
	dev->automute       = 0;

	INIT_LIST_HEAD(&dev->video_q.queue);
	init_timer(&dev->video_q.timeout);
	dev->video_q.timeout.function = saa7134_buffer_timeout;
	dev->video_q.timeout.data     = (unsigned long)(&dev->video_q);
	dev->video_q.dev              = dev;

	if (saa7134_boards[dev->board].video_out)
		saa7134_videoport_init(dev);

	return 0;
}

int saa7134_videoport_init(struct saa7134_dev *dev)
{
	/* enable video output */
	int vo = saa7134_boards[dev->board].video_out;
	int video_reg;
	unsigned int vid_port_opts = saa7134_boards[dev->board].vid_port_opts;

	/* Configure videoport */
	saa_writeb(SAA7134_VIDEO_PORT_CTRL0, video_out[vo][0]);
	video_reg = video_out[vo][1];
	if (vid_port_opts & SET_T_CODE_POLARITY_NON_INVERTED)
		video_reg &= ~VP_T_CODE_P_INVERTED;
	saa_writeb(SAA7134_VIDEO_PORT_CTRL1, video_reg);
	saa_writeb(SAA7134_VIDEO_PORT_CTRL2, video_out[vo][2]);
	saa_writeb(SAA7134_VIDEO_PORT_CTRL4, video_out[vo][4]);
	video_reg = video_out[vo][5];
	if (vid_port_opts & SET_CLOCK_NOT_DELAYED)
		video_reg &= ~VP_CLK_CTRL2_DELAYED;
	if (vid_port_opts & SET_CLOCK_INVERTED)
		video_reg |= VP_CLK_CTRL1_INVERTED;
	saa_writeb(SAA7134_VIDEO_PORT_CTRL5, video_reg);
	video_reg = video_out[vo][6];
	if (vid_port_opts & SET_VSYNC_OFF) {
		video_reg &= ~VP_VS_TYPE_MASK;
		video_reg |= VP_VS_TYPE_OFF;
	}
	saa_writeb(SAA7134_VIDEO_PORT_CTRL6, video_reg);
	saa_writeb(SAA7134_VIDEO_PORT_CTRL7, video_out[vo][7]);
	saa_writeb(SAA7134_VIDEO_PORT_CTRL8, video_out[vo][8]);

	/* Start videoport */
	saa_writeb(SAA7134_VIDEO_PORT_CTRL3, video_out[vo][3]);

	return 0;
}

int saa7134_video_init2(struct saa7134_dev *dev)
{
	/* init video hw */
	set_tvnorm(dev,&tvnorms[0]);
	video_mux(dev,0);
	saa7134_tvaudio_setmute(dev);
	saa7134_tvaudio_setvolume(dev,dev->ctl_volume);
	return 0;
}

void saa7134_irq_video_signalchange(struct saa7134_dev *dev)
{
	static const char *st[] = {
		"(no signal)", "NTSC", "PAL", "SECAM" };
	u32 st1,st2;

	st1 = saa_readb(SAA7134_STATUS_VIDEO1);
	st2 = saa_readb(SAA7134_STATUS_VIDEO2);
	dprintk("DCSDT: pll: %s, sync: %s, norm: %s\n",
		(st1 & 0x40) ? "not locked" : "locked",
		(st2 & 0x40) ? "no"         : "yes",
		st[st1 & 0x03]);
	dev->nosignal = (st1 & 0x40) || (st2 & 0x40)  || !(st2 & 0x1);

	if (dev->nosignal) {
		/* no video signal -> mute audio */
		if (dev->ctl_automute)
			dev->automute = 1;
		saa7134_tvaudio_setmute(dev);
	} else {
		/* wake up tvaudio audio carrier scan thread */
		saa7134_tvaudio_do_scan(dev);
	}

	if ((st2 & 0x80) && !noninterlaced && !dev->nosignal)
		saa_clearb(SAA7134_SYNC_CTRL, 0x20);
	else
		saa_setb(SAA7134_SYNC_CTRL, 0x20);

	if (dev->mops && dev->mops->signal_change)
		dev->mops->signal_change(dev);
}


void saa7134_irq_video_done(struct saa7134_dev *dev, unsigned long status)
{
	enum v4l2_field field;

	spin_lock(&dev->slock);
	if (dev->video_q.curr) {
		dev->video_fieldcount++;
		field = dev->video_q.curr->vb.field;
		if (V4L2_FIELD_HAS_BOTH(field)) {
			/* make sure we have seen both fields */
			if ((status & 0x10) == 0x00) {
				dev->video_q.curr->top_seen = 1;
				goto done;
			}
			if (!dev->video_q.curr->top_seen)
				goto done;
		} else if (field == V4L2_FIELD_TOP) {
			if ((status & 0x10) != 0x10)
				goto done;
		} else if (field == V4L2_FIELD_BOTTOM) {
			if ((status & 0x10) != 0x00)
				goto done;
		}
		dev->video_q.curr->vb.field_count = dev->video_fieldcount;
		saa7134_buffer_finish(dev,&dev->video_q,VIDEOBUF_DONE);
	}
	saa7134_buffer_next(dev,&dev->video_q);

 done:
	spin_unlock(&dev->slock);
}

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
