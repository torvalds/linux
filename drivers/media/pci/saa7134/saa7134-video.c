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

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/saa6588.h>

#include "saa7134-reg.h"
#include "saa7134.h"

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

static int verify_preview(struct saa7134_dev *dev, struct v4l2_window *win, bool try)
{
	enum v4l2_field field;
	int maxw, maxh;

	if (!try && (dev->ovbuf.base == NULL || dev->ovfmt == NULL))
		return -EINVAL;
	if (win->w.width < 48)
		win->w.width = 48;
	if (win->w.height < 32)
		win->w.height = 32;
	if (win->clipcount > 8)
		win->clipcount = 8;

	win->chromakey = 0;
	win->global_alpha = 0;
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
	default:
		field = V4L2_FIELD_INTERLACED;
		break;
	}

	win->field = field;
	if (win->w.width > maxw)
		win->w.width = maxw;
	if (win->w.height > maxh)
		win->w.height = maxh;
	return 0;
}

static int start_preview(struct saa7134_dev *dev)
{
	unsigned long base,control,bpl;
	int err;

	err = verify_preview(dev, &dev->win, false);
	if (0 != err)
		return err;

	dev->ovfield = dev->win.field;
	dprintk("start_preview %dx%d+%d+%d %s field=%s\n",
		dev->win.w.width, dev->win.w.height,
		dev->win.w.left, dev->win.w.top,
		dev->ovfmt->name, v4l2_field_names[dev->ovfield]);

	/* setup window + clipping */
	set_size(dev, TASK_B, dev->win.w.width, dev->win.w.height,
		 V4L2_FIELD_HAS_BOTH(dev->ovfield));
	setup_clipping(dev, dev->clips, dev->nclips,
		       V4L2_FIELD_HAS_BOTH(dev->ovfield));
	if (dev->ovfmt->yuv)
		saa_andorb(SAA7134_DATA_PATH(TASK_B), 0x3f, 0x03);
	else
		saa_andorb(SAA7134_DATA_PATH(TASK_B), 0x3f, 0x01);
	saa_writeb(SAA7134_OFMT_VIDEO_B, dev->ovfmt->pm | 0x20);

	/* dma: setup channel 1 (= Video Task B) */
	base  = (unsigned long)dev->ovbuf.base;
	base += dev->ovbuf.fmt.bytesperline * dev->win.w.top;
	base += dev->ovfmt->depth/8         * dev->win.w.left;
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

static int stop_preview(struct saa7134_dev *dev)
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
	struct saa7134_dev *dev = q->priv_data;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);
	unsigned int size;
	int err;

	/* sanity checks */
	if (NULL == dev->fmt)
		return -EINVAL;
	if (dev->width    < 48 ||
	    dev->height   < 32 ||
	    dev->width/4  > dev->crop_current.width  ||
	    dev->height/4 > dev->crop_current.height ||
	    dev->width    > dev->crop_bounds.width  ||
	    dev->height   > dev->crop_bounds.height)
		return -EINVAL;
	size = (dev->width * dev->height * dev->fmt->depth) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < size)
		return -EINVAL;

	dprintk("buffer_prepare [%d,size=%dx%d,bytes=%d,fields=%s,%s]\n",
		vb->i, dev->width, dev->height, size, v4l2_field_names[field],
		dev->fmt->name);
	if (buf->vb.width  != dev->width  ||
	    buf->vb.height != dev->height ||
	    buf->vb.size   != size       ||
	    buf->vb.field  != field      ||
	    buf->fmt       != dev->fmt) {
		saa7134_dma_free(q,buf);
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);

		buf->vb.width  = dev->width;
		buf->vb.height = dev->height;
		buf->vb.size   = size;
		buf->vb.field  = field;
		buf->fmt       = dev->fmt;
		buf->pt        = &dev->pt_cap;
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
	struct saa7134_dev *dev = q->priv_data;

	*size = dev->fmt->depth * dev->width * dev->height >> 3;
	if (0 == *count)
		*count = gbuffers;
	*count = saa7134_buffer_count(*size,*count);
	return 0;
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct saa7134_dev *dev = q->priv_data;
	struct saa7134_buf *buf = container_of(vb,struct saa7134_buf,vb);

	saa7134_buffer_queue(dev, &dev->video_q, buf);
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

static int saa7134_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa7134_dev *dev = container_of(ctrl->handler, struct saa7134_dev, ctrl_handler);
	unsigned long flags;
	int restart_overlay = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		dev->ctl_bright = ctrl->val;
		saa_writeb(SAA7134_DEC_LUMA_BRIGHT, ctrl->val);
		break;
	case V4L2_CID_HUE:
		dev->ctl_hue = ctrl->val;
		saa_writeb(SAA7134_DEC_CHROMA_HUE, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		dev->ctl_contrast = ctrl->val;
		saa_writeb(SAA7134_DEC_LUMA_CONTRAST,
			   dev->ctl_invert ? -dev->ctl_contrast : dev->ctl_contrast);
		break;
	case V4L2_CID_SATURATION:
		dev->ctl_saturation = ctrl->val;
		saa_writeb(SAA7134_DEC_CHROMA_SATURATION,
			   dev->ctl_invert ? -dev->ctl_saturation : dev->ctl_saturation);
		break;
	case V4L2_CID_AUDIO_MUTE:
		dev->ctl_mute = ctrl->val;
		saa7134_tvaudio_setmute(dev);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		dev->ctl_volume = ctrl->val;
		saa7134_tvaudio_setvolume(dev,dev->ctl_volume);
		break;
	case V4L2_CID_PRIVATE_INVERT:
		dev->ctl_invert = ctrl->val;
		saa_writeb(SAA7134_DEC_LUMA_CONTRAST,
			   dev->ctl_invert ? -dev->ctl_contrast : dev->ctl_contrast);
		saa_writeb(SAA7134_DEC_CHROMA_SATURATION,
			   dev->ctl_invert ? -dev->ctl_saturation : dev->ctl_saturation);
		break;
	case V4L2_CID_HFLIP:
		dev->ctl_mirror = ctrl->val;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_Y_EVEN:
		dev->ctl_y_even = ctrl->val;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_Y_ODD:
		dev->ctl_y_odd = ctrl->val;
		restart_overlay = 1;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
	{
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &dev->tda9887_conf;

		dev->ctl_automute = ctrl->val;
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
		return -EINVAL;
	}
	if (restart_overlay && res_locked(dev, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev);
		start_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);
	}
	return 0;
}

/* ------------------------------------------------------------------ */

static struct videobuf_queue *saa7134_queue(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;
	struct videobuf_queue *q = NULL;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		q = fh->is_empress ? &dev->empress_tsq : &dev->cap;
		break;
	case VFL_TYPE_VBI:
		q = &dev->vbi;
		break;
	default:
		BUG();
	}
	return q;
}

static int saa7134_resource(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_fh *fh = file->private_data;

	if (vdev->vfl_type == VFL_TYPE_GRABBER)
		return fh->is_empress ? RESOURCE_EMPRESS : RESOURCE_VIDEO;

	if (vdev->vfl_type == VFL_TYPE_VBI)
		return RESOURCE_VBI;

	BUG();
	return 0;
}

static int video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh;

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	v4l2_fh_init(&fh->fh, vdev);
	file->private_data = fh;

	if (vdev->vfl_type == VFL_TYPE_RADIO) {
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
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		if (res_locked(dev, RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(saa7134_queue(file),
					 data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case VFL_TYPE_VBI:
		if (!res_get(dev, fh, RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(saa7134_queue(file),
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
	unsigned long req_events = poll_requested_events(wait);
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;
	struct videobuf_buffer *buf = NULL;
	unsigned int rc = 0;

	if (v4l2_event_pending(&fh->fh))
		rc = POLLPRI;
	else if (req_events & POLLPRI)
		poll_wait(file, &fh->fh.wait, wait);

	if (vdev->vfl_type == VFL_TYPE_VBI)
		return rc | videobuf_poll_stream(file, &dev->vbi, wait);

	if (res_check(fh, RESOURCE_VIDEO)) {
		mutex_lock(&dev->cap.vb_lock);
		if (!list_empty(&dev->cap.stream))
			buf = list_entry(dev->cap.stream.next, struct videobuf_buffer, stream);
	} else {
		mutex_lock(&dev->cap.vb_lock);
		if (UNSET == dev->cap.read_off) {
			/* need to capture a new frame */
			if (res_locked(dev, RESOURCE_VIDEO))
				goto err;
			if (0 != dev->cap.ops->buf_prepare(&dev->cap,
					dev->cap.read_buf, dev->cap.field))
				goto err;
			dev->cap.ops->buf_queue(&dev->cap, dev->cap.read_buf);
			dev->cap.read_off = 0;
		}
		buf = dev->cap.read_buf;
	}

	if (!buf)
		goto err;

	poll_wait(file, &buf->done, wait);
	if (buf->state == VIDEOBUF_DONE || buf->state == VIDEOBUF_ERROR)
		rc |= POLLIN | POLLRDNORM;
	mutex_unlock(&dev->cap.vb_lock);
	return rc;

err:
	mutex_unlock(&dev->cap.vb_lock);
	return rc | POLLERR;
}

static int video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = file->private_data;
	struct saa6588_command cmd;
	unsigned long flags;

	saa7134_tvaudio_close(dev);

	/* turn off overlay */
	if (res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock,flags);
		stop_preview(dev);
		spin_unlock_irqrestore(&dev->slock,flags);
		res_free(dev, fh, RESOURCE_OVERLAY);
	}

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		pm_qos_remove_request(&dev->qos_request);
		videobuf_streamoff(&dev->cap);
		res_free(dev, fh, RESOURCE_VIDEO);
		videobuf_mmap_free(&dev->cap);
	}
	if (dev->cap.read_buf) {
		buffer_release(&dev->cap, dev->cap.read_buf);
		kfree(dev->cap.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		videobuf_stop(&dev->vbi);
		res_free(dev, fh, RESOURCE_VBI);
		videobuf_mmap_free(&dev->vbi);
	}

	/* ts-capture will not work in planar mode, so turn it off Hac: 04.05*/
	saa_andorb(SAA7134_OFMT_VIDEO_A, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_VIDEO_B, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_DATA_A, 0x1f, 0);
	saa_andorb(SAA7134_OFMT_DATA_B, 0x1f, 0);

	saa_call_all(dev, core, s_power, 0);
	if (vdev->vfl_type == VFL_TYPE_RADIO)
		saa_call_all(dev, core, ioctl, SAA6588_CMD_CLOSE, &cmd);

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int video_mmap(struct file *file, struct vm_area_struct * vma)
{
	return videobuf_mmap_mapper(saa7134_queue(file), vma);
}

static ssize_t radio_read(struct file *file, char __user *data,
			 size_t count, loff_t *ppos)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa6588_command cmd;

	cmd.block_count = count/3;
	cmd.nonblocking = file->f_flags & O_NONBLOCK;
	cmd.buffer = data;
	cmd.instance = file;
	cmd.result = -ENODEV;

	saa_call_all(dev, core, ioctl, SAA6588_CMD_READ, &cmd);

	return cmd.result;
}

static unsigned int radio_poll(struct file *file, poll_table *wait)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa6588_command cmd;
	unsigned int rc = v4l2_ctrl_poll(file, wait);

	cmd.instance = file;
	cmd.event_list = wait;
	cmd.result = 0;
	saa_call_all(dev, core, ioctl, SAA6588_CMD_POLL, &cmd);

	return rc | cmd.result;
}

/* ------------------------------------------------------------------ */

static int saa7134_try_get_set_fmt_vbi_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
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
	struct saa7134_dev *dev = video_drvdata(file);

	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = dev->cap.field;
	f->fmt.pix.pixelformat  = dev->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * dev->fmt->depth) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;
	return 0;
}

static int saa7134_g_fmt_vid_overlay(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct v4l2_clip __user *clips = f->fmt.win.clips;
	u32 clipcount = f->fmt.win.clipcount;
	int err = 0;
	int i;

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}
	mutex_lock(&dev->lock);
	f->fmt.win = dev->win;
	f->fmt.win.clips = clips;
	if (clips == NULL)
		clipcount = 0;
	if (dev->nclips < clipcount)
		clipcount = dev->nclips;
	f->fmt.win.clipcount = clipcount;

	for (i = 0; !err && i < clipcount; i++) {
		if (copy_to_user(&f->fmt.win.clips[i].c, &dev->clips[i].c,
					sizeof(struct v4l2_rect)))
			err = -EFAULT;
	}
	mutex_unlock(&dev->lock);

	return err;
}

static int saa7134_try_fmt_vid_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
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
	default:
		field = V4L2_FIELD_INTERLACED;
		break;
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
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;

	return 0;
}

static int saa7134_try_fmt_vid_overlay(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}

	if (f->fmt.win.clips == NULL)
		f->fmt.win.clipcount = 0;
	return verify_preview(dev, &f->fmt.win, true);
}

static int saa7134_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	int err;

	err = saa7134_try_fmt_vid_cap(file, priv, f);
	if (0 != err)
		return err;

	dev->fmt       = format_by_fourcc(f->fmt.pix.pixelformat);
	dev->width     = f->fmt.pix.width;
	dev->height    = f->fmt.pix.height;
	dev->cap.field = f->fmt.pix.field;
	return 0;
}

static int saa7134_s_fmt_vid_overlay(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct saa7134_dev *dev = video_drvdata(file);
	int err;
	unsigned long flags;

	if (saa7134_no_overlay > 0) {
		printk(KERN_ERR "V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
		return -EINVAL;
	}
	if (f->fmt.win.clips == NULL)
		f->fmt.win.clipcount = 0;
	err = verify_preview(dev, &f->fmt.win, true);
	if (0 != err)
		return err;

	mutex_lock(&dev->lock);

	dev->win    = f->fmt.win;
	dev->nclips = f->fmt.win.clipcount;

	if (copy_from_user(dev->clips, f->fmt.win.clips,
			   sizeof(struct v4l2_clip) * dev->nclips)) {
		mutex_unlock(&dev->lock);
		return -EFAULT;
	}

	if (res_check(priv, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev);
		start_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);
	}

	mutex_unlock(&dev->lock);
	return 0;
}

int saa7134_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct saa7134_dev *dev = video_drvdata(file);
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
			i->status |= V4L2_IN_ST_NO_SIGNAL;
		if (0 != (v2 & 0x0e))
			i->status |= V4L2_IN_ST_MACROVISION;
	}
	i->std = SAA7134_NORMS;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_enum_input);

int saa7134_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct saa7134_dev *dev = video_drvdata(file);

	*i = dev->ctl_input;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_g_input);

int saa7134_s_input(struct file *file, void *priv, unsigned int i)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (i >= SAA7134_INPUT_MAX)
		return -EINVAL;
	if (NULL == card_in(dev, i).name)
		return -EINVAL;
	mutex_lock(&dev->lock);
	video_mux(dev, i);
	mutex_unlock(&dev->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_s_input);

int saa7134_querycap(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct saa7134_fh *fh = priv;
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
	if (saa7134_no_overlay <= 0 && !fh->is_empress)
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
EXPORT_SYMBOL_GPL(saa7134_querycap);

int saa7134_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct saa7134_fh *fh = priv;
	unsigned long flags;
	unsigned int i;
	v4l2_std_id fixup;

	if (fh->is_empress && res_locked(dev, RESOURCE_OVERLAY)) {
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
	if (!fh->is_empress && res_check(fh, RESOURCE_OVERLAY)) {
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);

		set_tvnorm(dev, &tvnorms[i]);

		spin_lock_irqsave(&dev->slock, flags);
		start_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);
	} else
		set_tvnorm(dev, &tvnorms[i]);

	saa7134_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_s_std);

int saa7134_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct saa7134_dev *dev = video_drvdata(file);

	*id = dev->tvnorm->id;
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_g_std);

static int saa7134_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cap)
{
	struct saa7134_dev *dev = video_drvdata(file);

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
	struct saa7134_dev *dev = video_drvdata(file);

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;
	crop->c = dev->crop_current;
	return 0;
}

static int saa7134_s_crop(struct file *file, void *f, const struct v4l2_crop *crop)
{
	struct saa7134_dev *dev = video_drvdata(file);
	struct v4l2_rect *b = &dev->crop_bounds;
	struct v4l2_rect *c = &dev->crop_current;

	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
		return -EINVAL;

	if (res_locked(dev, RESOURCE_OVERLAY))
		return -EBUSY;
	if (res_locked(dev, RESOURCE_VIDEO))
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

int saa7134_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct saa7134_dev *dev = video_drvdata(file);
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
EXPORT_SYMBOL_GPL(saa7134_g_tuner);

int saa7134_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct saa7134_dev *dev = video_drvdata(file);
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
EXPORT_SYMBOL_GPL(saa7134_s_tuner);

int saa7134_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (0 != f->tuner)
		return -EINVAL;

	saa_call_all(dev, tuner, g_frequency, f);

	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_g_frequency);

int saa7134_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *f)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (0 != f->tuner)
		return -EINVAL;
	mutex_lock(&dev->lock);

	saa_call_all(dev, tuner, s_frequency, f);

	saa7134_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_s_frequency);

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
	struct saa7134_dev *dev = video_drvdata(file);

	*fb = dev->ovbuf;
	fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;

	return 0;
}

static int saa7134_s_fbuf(struct file *file, void *f,
					const struct v4l2_framebuffer *fb)
{
	struct saa7134_dev *dev = video_drvdata(file);
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

static int saa7134_overlay(struct file *file, void *priv, unsigned int on)
{
	struct saa7134_dev *dev = video_drvdata(file);
	unsigned long flags;

	if (on) {
		if (saa7134_no_overlay > 0) {
			dprintk("no_overlay\n");
			return -EINVAL;
		}

		if (!res_get(dev, priv, RESOURCE_OVERLAY))
			return -EBUSY;
		spin_lock_irqsave(&dev->slock, flags);
		start_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);
	}
	if (!on) {
		if (!res_check(priv, RESOURCE_OVERLAY))
			return -EINVAL;
		spin_lock_irqsave(&dev->slock, flags);
		stop_preview(dev);
		spin_unlock_irqrestore(&dev->slock, flags);
		res_free(dev, priv, RESOURCE_OVERLAY);
	}
	return 0;
}

int saa7134_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *p)
{
	return videobuf_reqbufs(saa7134_queue(file), p);
}
EXPORT_SYMBOL_GPL(saa7134_reqbufs);

int saa7134_querybuf(struct file *file, void *priv,
					struct v4l2_buffer *b)
{
	return videobuf_querybuf(saa7134_queue(file), b);
}
EXPORT_SYMBOL_GPL(saa7134_querybuf);

int saa7134_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	return videobuf_qbuf(saa7134_queue(file), b);
}
EXPORT_SYMBOL_GPL(saa7134_qbuf);

int saa7134_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	return videobuf_dqbuf(saa7134_queue(file), b,
				file->f_flags & O_NONBLOCK);
}
EXPORT_SYMBOL_GPL(saa7134_dqbuf);

int saa7134_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct saa7134_dev *dev = video_drvdata(file);
	int res = saa7134_resource(file);

	if (!res_get(dev, priv, res))
		return -EBUSY;

	/* The SAA7134 has a 1K FIFO; the datasheet suggests that when
	 * configured conservatively, there's 22 usec of buffering for video.
	 * We therefore request a DMA latency of 20 usec, giving us 2 usec of
	 * margin in case the FIFO is configured differently to the datasheet.
	 * Unfortunately, I lack register-level documentation to check the
	 * Linux FIFO setup and confirm the perfect value.
	 */
	if (res != RESOURCE_EMPRESS)
		pm_qos_add_request(&dev->qos_request,
			   PM_QOS_CPU_DMA_LATENCY, 20);

	return videobuf_streamon(saa7134_queue(file));
}
EXPORT_SYMBOL_GPL(saa7134_streamon);

int saa7134_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct saa7134_dev *dev = video_drvdata(file);
	int err;
	int res = saa7134_resource(file);

	if (res != RESOURCE_EMPRESS)
		pm_qos_remove_request(&dev->qos_request);

	err = videobuf_streamoff(saa7134_queue(file));
	if (err < 0)
		return err;
	res_free(dev, priv, res);
	return 0;
}
EXPORT_SYMBOL_GPL(saa7134_streamoff);

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vidioc_g_register (struct file *file, void *priv,
			      struct v4l2_dbg_register *reg)
{
	struct saa7134_dev *dev = video_drvdata(file);

	reg->val = saa_readb(reg->reg & 0xffffff);
	reg->size = 1;
	return 0;
}

static int vidioc_s_register (struct file *file, void *priv,
				const struct v4l2_dbg_register *reg)
{
	struct saa7134_dev *dev = video_drvdata(file);

	saa_writeb(reg->reg & 0xffffff, reg->val);
	return 0;
}
#endif

static int radio_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct saa7134_dev *dev = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Radio");

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
	struct saa7134_dev *dev = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;

	saa_call_all(dev, tuner, s_tuner, t);
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
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
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
	.vidioc_s_tuner		= radio_s_tuner,
	.vidioc_g_frequency	= saa7134_g_frequency,
	.vidioc_s_frequency	= saa7134_s_frequency,
	.vidioc_subscribe_event	= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
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

static const struct v4l2_ctrl_ops saa7134_ctrl_ops = {
	.s_ctrl = saa7134_s_ctrl,
};

static const struct v4l2_ctrl_config saa7134_ctrl_invert = {
	.ops = &saa7134_ctrl_ops,
	.id = V4L2_CID_PRIVATE_INVERT,
	.name = "Invert",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config saa7134_ctrl_y_odd = {
	.ops = &saa7134_ctrl_ops,
	.id = V4L2_CID_PRIVATE_Y_ODD,
	.name = "Y Offset Odd Field",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 128,
	.step = 1,
};

static const struct v4l2_ctrl_config saa7134_ctrl_y_even = {
	.ops = &saa7134_ctrl_ops,
	.id = V4L2_CID_PRIVATE_Y_EVEN,
	.name = "Y Offset Even Field",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 128,
	.step = 1,
};

static const struct v4l2_ctrl_config saa7134_ctrl_automute = {
	.ops = &saa7134_ctrl_ops,
	.id = V4L2_CID_PRIVATE_AUTOMUTE,
	.name = "Automute",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

int saa7134_video_init1(struct saa7134_dev *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->ctrl_handler;

	/* sanitycheck insmod options */
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsize > gbufsize_max)
		gbufsize = gbufsize_max;
	gbufsize = (gbufsize + PAGE_SIZE - 1) & PAGE_MASK;

	v4l2_ctrl_handler_init(hdl, 11);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 68);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_SATURATION, 0, 127, 1, 64);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_HUE, -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &saa7134_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, -15, 15, 1, 0);
	v4l2_ctrl_new_custom(hdl, &saa7134_ctrl_invert, NULL);
	v4l2_ctrl_new_custom(hdl, &saa7134_ctrl_y_odd, NULL);
	v4l2_ctrl_new_custom(hdl, &saa7134_ctrl_y_even, NULL);
	v4l2_ctrl_new_custom(hdl, &saa7134_ctrl_automute, NULL);
	if (hdl->error)
		return hdl->error;
	if (card_has_radio(dev)) {
		hdl = &dev->radio_ctrl_handler;
		v4l2_ctrl_handler_init(hdl, 2);
		v4l2_ctrl_add_handler(hdl, &dev->ctrl_handler,
				v4l2_ctrl_radio_filter);
		if (hdl->error)
			return hdl->error;
	}
	dev->ctl_mute       = 1;

	if (dev->tda9887_conf && saa7134_ctrl_automute.def)
		dev->tda9887_conf |= TDA9887_AUTOMUTE;
	dev->automute       = 0;

	INIT_LIST_HEAD(&dev->video_q.queue);
	init_timer(&dev->video_q.timeout);
	dev->video_q.timeout.function = saa7134_buffer_timeout;
	dev->video_q.timeout.data     = (unsigned long)(&dev->video_q);
	dev->video_q.dev              = dev;
	dev->fmt = format_by_fourcc(V4L2_PIX_FMT_BGR24);
	dev->width    = 720;
	dev->height   = 576;
	dev->win.w.width = dev->width;
	dev->win.w.height = dev->height;
	dev->win.field = V4L2_FIELD_INTERLACED;
	dev->ovbuf.fmt.width = dev->width;
	dev->ovbuf.fmt.height = dev->height;
	dev->ovbuf.fmt.pixelformat = dev->fmt->fourcc;
	dev->ovbuf.fmt.colorspace = V4L2_COLORSPACE_SMPTE170M;

	if (saa7134_boards[dev->board].video_out)
		saa7134_videoport_init(dev);

	videobuf_queue_sg_init(&dev->cap, &video_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct saa7134_buf),
			    dev, NULL);
	videobuf_queue_sg_init(&dev->vbi, &saa7134_vbi_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct saa7134_buf),
			    dev, NULL);
	saa7134_pgtable_alloc(dev->pci, &dev->pt_cap);
	saa7134_pgtable_alloc(dev->pci, &dev->pt_vbi);

	return 0;
}

void saa7134_video_fini(struct saa7134_dev *dev)
{
	/* free stuff */
	saa7134_pgtable_free(dev->pci, &dev->pt_cap);
	saa7134_pgtable_free(dev->pci, &dev->pt_vbi);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	if (card_has_radio(dev))
		v4l2_ctrl_handler_free(&dev->radio_ctrl_handler);
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
	v4l2_ctrl_handler_setup(&dev->ctrl_handler);
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
