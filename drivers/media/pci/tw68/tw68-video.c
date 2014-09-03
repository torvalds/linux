/*
 *  tw68 functions to handle video data
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <media/v4l2-common.h>
#include <linux/sort.h>

#include "tw68.h"
#include "tw68-reg.h"

unsigned int video_debug;

static unsigned int gbuffers	= 8;
static unsigned int noninterlaced; /* 0 */
static unsigned int gbufsz	= 768*576*4;
static unsigned int gbufsz_max	= 768*576*4;
static char secam[]		= "--";

module_param(video_debug, int, 0644);
MODULE_PARM_DESC(video_debug, "enable debug messages [video]");
module_param(gbuffers, int, 0444);
MODULE_PARM_DESC(gbuffers, "number of capture buffers, range 2-32");
module_param(noninterlaced, int, 0644);
MODULE_PARM_DESC(noninterlaced, "capture non interlaced video");
module_param_string(secam, secam, sizeof(secam), 0644);
MODULE_PARM_DESC(secam, "force SECAM variant, either DK,L or Lc");

#define dprintk(level, fmt, arg...)     if (video_debug & (level)) \
	printk(KERN_DEBUG "%s/0: " fmt, dev->name , ## arg)

/* ------------------------------------------------------------------ */
/* data structs for video                                             */
/*
 * FIXME -
 * Note that the saa7134 has formats, e.g. YUV420, which are classified
 * as "planar".  These affect overlay mode, and are flagged with a field
 * ".planar" in the format.  Do we need to implement this in this driver?
 */
static struct tw68_format formats[] = {
	{
		.name		= "15 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_RGB555,
		.depth		= 16,
		.twformat	= ColorFormatRGB15,
	}, {
		.name		= "15 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB555X,
		.depth		= 16,
		.twformat	= ColorFormatRGB15 | ColorFormatBSWAP,
	}, {
		.name		= "16 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.twformat	= ColorFormatRGB16,
	}, {
		.name		= "16 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= 16,
		.twformat	= ColorFormatRGB16 | ColorFormatBSWAP,
	}, {
		.name		= "24 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.depth		= 24,
		.twformat	= ColorFormatRGB24,
	}, {
		.name		= "24 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB24,
		.depth		= 24,
		.twformat	= ColorFormatRGB24 | ColorFormatBSWAP,
	}, {
		.name		= "32 bpp RGB, le",
		.fourcc		= V4L2_PIX_FMT_BGR32,
		.depth		= 32,
		.twformat	= ColorFormatRGB32,
	}, {
		.name		= "32 bpp RGB, be",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= 32,
		.twformat	= ColorFormatRGB32 | ColorFormatBSWAP |
				  ColorFormatWSWAP,
	}, {
		.name		= "4:2:2 packed, YUYV",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.twformat	= ColorFormatYUY2,
	}, {
		.name		= "4:2:2 packed, UYVY",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.depth		= 16,
		.twformat	= ColorFormatYUY2 | ColorFormatBSWAP,
	}
};
#define FORMATS ARRAY_SIZE(formats)

#define NORM_625_50			\
		.h_delay	= 3,	\
		.h_delay0	= 133,	\
		.h_start	= 0,	\
		.h_stop		= 719,	\
		.v_delay	= 24,	\
		.vbi_v_start_0	= 7,	\
		.vbi_v_stop_0	= 22,	\
		.video_v_start	= 24,	\
		.video_v_stop	= 311,	\
		.vbi_v_start_1	= 319

#define NORM_525_60			\
		.h_delay	= 8,	\
		.h_delay0	= 138,	\
		.h_start	= 0,	\
		.h_stop		= 719,	\
		.v_delay	= 22,	\
		.vbi_v_start_0	= 10,	\
		.vbi_v_stop_0	= 21,	\
		.video_v_start	= 22,	\
		.video_v_stop	= 262,	\
		.vbi_v_start_1	= 273

/*
 * The following table is searched by tw68_s_std, first for a specific
 * match, then for an entry which contains the desired id.  The table
 * entries should therefore be ordered in ascending order of specificity.
 */
static struct tw68_tvnorm tvnorms[]		= {
	{
		.name		= "PAL-BG",
		.id		= V4L2_STD_PAL_BG,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,

	}, {
		.name		= "PAL-I",
		.id		= V4L2_STD_PAL_I,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,

	}, {
		.name		= "PAL-DK",
		.id		= V4L2_STD_PAL_DK,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,

	}, {
		.name		= "PAL", /* autodetect */
		.id		= V4L2_STD_PAL,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,

	}, {
		.name		= "NTSC",
		.id		= V4L2_STD_NTSC,
		NORM_525_60,

		.sync_control	= 0x59,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x89,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x0e,
		.vgate_misc	= 0x18,
		.format		= VideoFormatNTSC,

	}, {
		.name		= "SECAM-DK",
		.id		= V4L2_STD_SECAM_DK,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x1b,
		.chroma_ctrl1	= 0xd1,
		.chroma_gain	= 0x80,
		.chroma_ctrl2	= 0x00,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatSECAM,

	}, {
		.name		= "SECAM-L",
		.id		= V4L2_STD_SECAM_L,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x1b,
		.chroma_ctrl1	= 0xd1,
		.chroma_gain	= 0x80,
		.chroma_ctrl2	= 0x00,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatSECAM,

	}, {
		.name		= "SECAM-LC",
		.id		= V4L2_STD_SECAM_LC,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x1b,
		.chroma_ctrl1	= 0xd1,
		.chroma_gain	= 0x80,
		.chroma_ctrl2	= 0x00,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatSECAM,

	}, {
		.name		= "SECAM",
		.id		= V4L2_STD_SECAM,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x1b,
		.chroma_ctrl1	= 0xd1,
		.chroma_gain	= 0x80,
		.chroma_ctrl2	= 0x00,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatSECAM,

	}, {
		.name		= "PAL-M",
		.id		= V4L2_STD_PAL_M,
		NORM_525_60,

		.sync_control	= 0x59,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0xb9,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x0e,
		.vgate_misc	= 0x18,
		.format		= VideoFormatPALM,

	}, {
		.name		= "PAL-Nc",
		.id		= V4L2_STD_PAL_Nc,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0xa1,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALNC,

	}, {
		.name		= "PAL-60",
		.id		= V4L2_STD_PAL_60,
		.h_delay	= 186,
		.h_start	= 0,
		.h_stop		= 719,
		.v_delay	= 26,
		.video_v_start	= 23,
		.video_v_stop	= 262,
		.vbi_v_start_0	= 10,
		.vbi_v_stop_0	= 21,
		.vbi_v_start_1	= 273,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPAL60,

	}, {
/*
 * 	FIXME:  The following are meant to be "catch-all", and need
 *		to be further thought out!
 */
		.name		= "STD-525-60",
		.id		= V4L2_STD_525_60,
		NORM_525_60,

		.sync_control	= 0x59,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x89,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x0e,
		.vgate_misc	= 0x18,
		.format		= VideoFormatNTSC,

	}, {
		.name		= "STD-625-50",
		.id		= V4L2_STD_625_50,
		NORM_625_50,

		.sync_control	= 0x18,
		.luma_control	= 0x40,
		.chroma_ctrl1	= 0x81,
		.chroma_gain	= 0x2a,
		.chroma_ctrl2	= 0x06,
		.vgate_misc	= 0x1c,
		.format		= VideoFormatPALBDGHI,
	}
};
#define TVNORMS ARRAY_SIZE(tvnorms)

static const struct v4l2_queryctrl no_ctrl		= {
	.name		= "42",
	.flags		= V4L2_CTRL_FLAG_DISABLED,
};
static const struct v4l2_queryctrl video_ctrls[]		= {
	/* --- video --- */
	{
		.id		= V4L2_CID_BRIGHTNESS,
		.name		= "Brightness",
		.minimum	= -128,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 20,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_CONTRAST,
		.name		= "Contrast",
		.minimum	= 0,
		.maximum	= 255,
		.step		= 1,
		.default_value	= 100,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_SATURATION,
		.name		= "Saturation",
		.minimum	= 0,
		.maximum	= 255,
		.step		= 1,
		.default_value	= 128,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_HUE,
		.name		= "Hue",
		.minimum	= -128,
		.maximum	= 127,
		.step		= 1,
		.default_value	= 0,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_COLOR_KILLER,
		.name		= "Color Killer",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 1,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	}, {
		.id		= V4L2_CID_CHROMA_AGC,
		.name		= "Chroma AGC",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 1,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	},
	/* --- audio --- */
	{
		.id		= V4L2_CID_AUDIO_MUTE,
		.name		= "Mute",
		.minimum	= 0,
		.maximum	= 1,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	}, {
		.id		= V4L2_CID_AUDIO_VOLUME,
		.name		= "Volume",
		.minimum	= -15,
		.maximum	= 15,
		.step		= 1,
		.default_value	= 0,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}
};
static const unsigned int CTRLS = ARRAY_SIZE(video_ctrls);

/*
 * Routine to lookup a control by its ID, and return a pointer
 * to the entry in the video_ctrls array for that control.
 */
static const struct v4l2_queryctrl *ctrl_by_id(unsigned int id)
{
	unsigned int i;

	for (i = 0; i < CTRLS; i++)
		if (video_ctrls[i].id == id)
			return video_ctrls+i;
	return NULL;
}

static struct tw68_format *format_by_fourcc(unsigned int fourcc)
{
	unsigned int i;

	for (i = 0; i < FORMATS; i++)
		if (formats[i].fourcc == fourcc)
			return formats+i;
	return NULL;
}

/* ----------------------------------------------------------------------- */
/* resource management                                                     */

static int res_get(struct tw68_fh *fh, unsigned int bit)
{
	struct tw68_dev *dev = fh->dev;

	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	/* is it free? */
	mutex_lock(&dev->lock);
	if (dev->resources & bit) {
		/* no, someone else uses it */
		mutex_unlock(&fh->dev->lock);
		return 0;
	}
	/* it's free, grab it */
	fh->resources  |= bit;
	dev->resources |= bit;
	dprintk(DBG_FLOW, "%s: %d\n", __func__, bit);
	mutex_unlock(&dev->lock);
	return 1;
}

static int res_check(struct tw68_fh *fh, unsigned int bit)
{
	return fh->resources & bit;
}

static int res_locked(struct tw68_dev *dev, unsigned int bit)
{
	return dev->resources & bit;
}

static void res_free(struct tw68_fh *fh,
		     unsigned int bits)
{
	struct tw68_dev *dev = fh->dev;

	BUG_ON((fh->resources & bits) != bits);

	mutex_lock(&fh->dev->lock);
	fh->resources  &= ~bits;
	fh->dev->resources &= ~bits;
	dprintk(DBG_FLOW, "%s: %d\n", __func__, bits);
	mutex_unlock(&fh->dev->lock);
}

/* ------------------------------------------------------------------ */
/*
 * Note that the cropping rectangles are described in terms of a single
 * frame, i.e. line positions are only 1/2 the interlaced equivalent
 */
static void set_tvnorm(struct tw68_dev *dev, struct tw68_tvnorm *norm)
{
	dprintk(DBG_FLOW, "%s: %s\n", __func__, norm->name);
	dev->tvnorm = norm;

	/* setup cropping */
	dev->crop_bounds.left    = norm->h_start;
	dev->crop_defrect.left   = norm->h_start;
	dev->crop_bounds.width   = norm->h_stop - norm->h_start + 1;
	dev->crop_defrect.width  = norm->h_stop - norm->h_start + 1;

	dev->crop_bounds.top     = norm->video_v_start;
	dev->crop_defrect.top    = norm->video_v_start;
	dev->crop_bounds.height  = (((norm->id & V4L2_STD_525_60) ?
				    524 : 624)) / 2 - dev->crop_bounds.top;
	dev->crop_defrect.height = (norm->video_v_stop -
				    norm->video_v_start + 1);

	dev->crop_current = dev->crop_defrect;

	if (norm != dev->tvnorm) {
		dev->tvnorm = norm;
		tw68_set_tvnorm_hw(dev);
	}
}

static void video_mux(struct tw68_dev *dev, int input)
{
	dprintk(DBG_FLOW, "%s: input = %d [%s]\n", __func__, input,
		card_in(dev, input).name);
	/*
	 * dev->input shows current application request,
	 * dev->hw_input shows current hardware setting
	 */
	dev->input = &card_in(dev, input);
	tw68_tvaudio_setinput(dev, &card_in(dev, input));
}

/*
 * tw68_set_scale
 *
 * Scaling and Cropping for video decoding
 *
 * We are working with 3 values for horizontal and vertical - scale,
 * delay and active.
 *
 * HACTIVE represent the actual number of pixels in the "usable" image,
 * before scaling.  HDELAY represents the number of pixels skipped
 * between the start of the horizontal sync and the start of the image.
 * HSCALE is calculated using the formula
 * 	HSCALE = (HACTIVE / (#pixels desired)) * 256
 *
 * The vertical registers are similar, except based upon the total number
 * of lines in the image, and the first line of the image (i.e. ignoring
 * vertical sync and VBI).
 *
 * Note that the number of bytes reaching the FIFO (and hence needing
 * to be processed by the DMAP program) is completely dependent upon
 * these values, especially HSCALE.
 *
 * Parameters:
 * 	@dev		pointer to the device structure, needed for
 * 			getting current norm (as well as debug print)
 * 	@width		actual image width (from user buffer)
 * 	@height		actual image height
 * 	@field		indicates Top, Bottom or Interlaced
 */
static int tw68_set_scale(struct tw68_dev *dev, unsigned int width,
			  unsigned int height, enum v4l2_field field)
{

	/* set individually for debugging clarity */
	int hactive, hdelay, hscale;
	int vactive, vdelay, vscale;
	int comb;

	if (V4L2_FIELD_HAS_BOTH(field))	/* if field is interlaced */
		height /= 2;		/* we must set for 1-frame */

	dprintk(DBG_FLOW, "%s: width=%d, height=%d, both=%d\n  Crop rect: "
		    "top=%d, left=%d, width=%d height=%d\n"
		    "  tvnorm h_delay=%d, h_start=%d, h_stop=%d, "
		    "v_delay=%d, v_start=%d, v_stop=%d\n" , __func__,
		width, height, V4L2_FIELD_HAS_BOTH(field),
		dev->crop_bounds.top, dev->crop_bounds.left,
		dev->crop_bounds.width, dev->crop_bounds.height,
		dev->tvnorm->h_delay, dev->tvnorm->h_start, dev->tvnorm->h_stop,
		dev->tvnorm->v_delay, dev->tvnorm->video_v_start,
		dev->tvnorm->video_v_stop);

	switch (dev->vdecoder) {
	case TW6800:
		hdelay = dev->tvnorm->h_delay0;
		break;
	default:
		hdelay = dev->tvnorm->h_delay;
		break;
	}
	hdelay += dev->crop_bounds.left;
	hactive = dev->crop_bounds.width;

	hscale = (hactive * 256) / (width);

	vdelay = dev->tvnorm->v_delay + dev->crop_bounds.top -
		 dev->crop_defrect.top;
	vactive = dev->crop_bounds.height;
	vscale = (vactive * 256) / height;

	dprintk(DBG_FLOW, "%s: %dx%d [%s%s,%s]\n", __func__,
		width, height,
		V4L2_FIELD_HAS_TOP(field)    ? "T" : "",
		V4L2_FIELD_HAS_BOTTOM(field) ? "B" : "",
		v4l2_norm_to_name(dev->tvnorm->id));
	dprintk(DBG_FLOW, "%s: hactive=%d, hdelay=%d, hscale=%d; "
		"vactive=%d, vdelay=%d, vscale=%d\n", __func__,
		hactive, hdelay, hscale, vactive, vdelay, vscale);

	comb =	((vdelay & 0x300)  >> 2) |
		((vactive & 0x300) >> 4) |
		((hdelay & 0x300)  >> 6) |
		((hactive & 0x300) >> 8);
	dprintk(DBG_FLOW, "%s: setting CROP_HI=%02x, VDELAY_LO=%02x, "
		"VACTIVE_LO=%02x, HDELAY_LO=%02x, HACTIVE_LO=%02x\n",
		__func__, comb, vdelay, vactive, hdelay, hactive);
	tw_writeb(TW68_CROP_HI, comb);
	tw_writeb(TW68_VDELAY_LO, vdelay & 0xff);
	tw_writeb(TW68_VACTIVE_LO, vactive & 0xff);
	tw_writeb(TW68_HDELAY_LO, hdelay & 0xff);
	tw_writeb(TW68_HACTIVE_LO, hactive & 0xff);

	comb = ((vscale & 0xf00) >> 4) | ((hscale & 0xf00) >> 8);
	dprintk(DBG_FLOW, "%s: setting SCALE_HI=%02x, VSCALE_LO=%02x, "
		"HSCALE_LO=%02x\n", __func__, comb, vscale, hscale);
	tw_writeb(TW68_SCALE_HI, comb);
	tw_writeb(TW68_VSCALE_LO, vscale);
	tw_writeb(TW68_HSCALE_LO, hscale);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tw68_video_start_dma(struct tw68_dev *dev, struct tw68_dmaqueue *q,
				struct tw68_buf *buf) {

	dprintk(DBG_FLOW, "%s: Starting risc program\n", __func__);
	/* Assure correct input */
	if (dev->hw_input != dev->input) {
		dev->hw_input = dev->input;
		tw_andorb(TW68_INFORM, 0x03 << 2, dev->input->vmux << 2);
	}
	/* Set cropping and scaling */
	tw68_set_scale(dev, buf->vb.width, buf->vb.height, buf->vb.field);
	/*
	 *  Set start address for RISC program.  Note that if the DMAP
	 *  processor is currently running, it must be stopped before
	 *  a new address can be set.
	 */
	tw_clearl(TW68_DMAC, TW68_DMAP_EN);
	tw_writel(TW68_DMAP_SA, cpu_to_le32(buf->risc.dma));
	/* Clear any pending interrupts */
	tw_writel(TW68_INTSTAT, dev->board_virqmask);
	/* Enable the risc engine and the fifo */
	tw_andorl(TW68_DMAC, 0xff, buf->fmt->twformat |
		ColorFormatGamma | TW68_DMAP_EN | TW68_FIFO_EN);
	dev->pci_irqmask |= dev->board_virqmask;
	tw_setl(TW68_INTMASK, dev->pci_irqmask);
	return 0;
}

/* ------------------------------------------------------------------ */
/* videobuf queue operations                                          */

/*
 * check_buf_fmt
 *
 * callback from tw68-core buffer_queue to determine whether the
 * current buffer and the previous one are "compatible" (i.e. the
 * risc programs can be chained without requiring a format change)
 */
static int tw68_check_video_fmt(struct tw68_buf *prev, struct tw68_buf *buf)
{
	return (prev->vb.width  == buf->vb.width  &&
		prev->vb.height == buf->vb.height &&
		prev->fmt       == buf->fmt);
}

/*
 * buffer_setup
 *
 * Calculate required size of buffer and maximum number allowed
 */
static int
buffer_setup(struct videobuf_queue *q, unsigned int *count,
	     unsigned int *size)
{
	struct tw68_fh *fh = q->priv_data;

	*size = fh->fmt->depth * fh->width * fh->height >> 3;
	if (0 == *count)
		*count = gbuffers;
	*count = tw68_buffer_count(*size, *count);
	return 0;
}

static int buffer_activate(struct tw68_dev *dev, struct tw68_buf *buf,
			   struct tw68_buf *next)
{
	dprintk(DBG_BUFF, "%s: dev=%p, buf=%p, next=%p\n",
		__func__, dev, buf, next);
	if (dev->hw_input != dev->input) {
		dev->hw_input = dev->input;
		tw_andorb(TW68_INFORM, 0x03 << 2,
			  dev->hw_input->vmux << 2);
	}
	buf->vb.state = VIDEOBUF_ACTIVE;
	/* TODO - need to assure scaling/cropping are set correctly */
	mod_timer(&dev->video_q.timeout, jiffies+BUFFER_TIMEOUT);
	return 0;
}

/*
* buffer_prepare
*
* Set the ancilliary information into the buffer structure.  This
* includes generating the necessary risc program if it hasn't already
* been done for the current buffer format.
* The structure fh contains the details of the format requested by the
* user - type, width, height and #fields.  This is compared with the
* last format set for the current buffer.  If they differ, the risc
* code (which controls the filling of the buffer) is (re-)generated.
*/
static int
buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct tw68_fh   *fh  = q->priv_data;
	struct tw68_dev  *dev = fh->dev;
	struct tw68_buf *buf = container_of(vb, struct tw68_buf, vb);
	struct videobuf_dmabuf *dma = videobuf_to_dma(&buf->vb);
	int rc, init_buffer = 0;
	unsigned int maxw, maxh;

	BUG_ON(NULL == fh->fmt);
	maxw = dev->tvnorm->h_stop - dev->tvnorm->h_start + 1;
	maxh = 2*(dev->tvnorm->video_v_stop - dev->tvnorm->video_v_start + 1);
	if (fh->width  < 48 || fh->width  > maxw || fh->height > maxh
		|| fh->height < 16) {
		dprintk(DBG_UNEXPECTED, "%s: invalid dimensions - "
			"fh->width=%d, fh->height=%d, maxw=%d, maxh=%d\n",
			__func__, fh->width, fh->height, maxw, maxh);
		return -EINVAL;
	}
	buf->vb.size = (fh->width * fh->height * (fh->fmt->depth)) >> 3;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (buf->fmt       != fh->fmt    ||
	    buf->vb.width  != fh->width  ||
	    buf->vb.height != fh->height ||
	    buf->vb.field  != field) {
		dprintk(DBG_BUFF, "%s: buf - fmt=%p, width=%3d, height=%3d, "
			"field=%d\n%s: fh  - fmt=%p, width=%3d, height=%3d, "
			"field=%d\n", __func__, buf->fmt, buf->vb.width,
			buf->vb.height, buf->vb.field, __func__, fh->fmt,
			fh->width, fh->height, field);
		buf->fmt       = fh->fmt;
		buf->vb.width  = fh->width;
		buf->vb.height = fh->height;
		buf->vb.field  = field;
		init_buffer = 1;	/* force risc code re-generation */
	}
	buf->input = dev->input;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(q, &buf->vb, NULL);
		if (0 != rc)
			goto fail;
		init_buffer = 1;	/* force risc code re-generation */
	}
	dprintk(DBG_BUFF, "%s: q=%p, vb=%p, init_buffer=%d\n",
		__func__, q, vb, init_buffer);

	if (init_buffer) {
		buf->bpl = buf->vb.width * (buf->fmt->depth) >> 3;
		dprintk(DBG_TESTING, "%s: Generating new risc code "
			"[%dx%dx%d](%d)\n", __func__, buf->vb.width,
			buf->vb.height, buf->fmt->depth, buf->bpl);
		switch (buf->vb.field) {
		case V4L2_FIELD_TOP:
			tw68_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 0, UNSET,
					 buf->bpl, 0,
					 buf->vb.height);
			break;
		case V4L2_FIELD_BOTTOM:
			tw68_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 UNSET, 0,
					 buf->bpl, 0,
					 buf->vb.height);
			break;
		case V4L2_FIELD_INTERLACED:
			tw68_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 0, buf->bpl,
					 buf->bpl, buf->bpl,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_TB:
			tw68_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 0, buf->bpl * (buf->vb.height >> 1),
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		case V4L2_FIELD_SEQ_BT:
			tw68_risc_buffer(dev->pci, &buf->risc,
					 dma->sglist,
					 buf->bpl * (buf->vb.height >> 1), 0,
					 buf->bpl, 0,
					 buf->vb.height >> 1);
			break;
		default:
			BUG();
		}
	}
	dprintk(DBG_BUFF, "%s: [%p/%d] - %dx%d %dbpp \"%s\" - dma=0x%08lx\n",
		__func__, buf, buf->vb.i, fh->width, fh->height,
		fh->fmt->depth, fh->fmt->name, (unsigned long)buf->risc.dma);

	buf->vb.state = VIDEOBUF_PREPARED;
	buf->activate = buffer_activate;
	return 0;

 fail:
	tw68_dma_free(q, buf);
	return rc;
}

/*
 * buffer_queue
 *
 * Callback whenever a buffer has been requested (by read() or QBUF)
 */
static void
buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct tw68_fh	*fh = q->priv_data;
	struct tw68_buf	*buf = container_of(vb, struct tw68_buf, vb);

	tw68_buffer_queue(fh->dev, &fh->dev->video_q, buf);
}

/*
 * buffer_release
 *
 * Free a buffer previously allocated.
 */
static void buffer_release(struct videobuf_queue *q,
			   struct videobuf_buffer *vb)
{
	struct tw68_buf *buf = container_of(vb, struct tw68_buf, vb);

	tw68_dma_free(q, buf);
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

static int tw68_g_ctrl_internal(struct tw68_dev *dev, struct tw68_fh *fh,
				struct v4l2_control *c)
{
	const struct v4l2_queryctrl *ctrl;

	dprintk(DBG_FLOW, "%s\n", __func__);
	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		return -EINVAL;
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = (char)tw_readb(TW68_BRIGHT);
		break;
	case V4L2_CID_HUE:
		c->value = (char)tw_readb(TW68_HUE);
		break;
	case V4L2_CID_CONTRAST:
		c->value = tw_readb(TW68_CONTRAST);
		break;
	case V4L2_CID_SATURATION:
		c->value = tw_readb(TW68_SAT_U);
		break;
	case V4L2_CID_COLOR_KILLER:
		c->value = 0 != (tw_readb(TW68_MISC2) & 0xe0);
		break;
	case V4L2_CID_CHROMA_AGC:
		c->value = 0 != (tw_readb(TW68_LOOP) & 0x30);
		break;
	case V4L2_CID_AUDIO_MUTE:
		/*hack to suppresss tvtime complaint */
		c->value = 0;
		break;
#if 0
	case V4L2_CID_AUDIO_VOLUME:
		c->value = dev->ctl_volume;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static int tw68_g_ctrl(struct file *file, void *priv, struct v4l2_control *c)
{
	struct tw68_fh *fh = priv;

	return tw68_g_ctrl_internal(fh->dev, fh, c);
}

static int tw68_s_ctrl_value(struct tw68_dev *dev, __u32 id, int val)
{
	int err = 0;

	dprintk(DBG_FLOW, "%s\n", __func__);
	switch (id) {
	case V4L2_CID_BRIGHTNESS:
		tw_writeb(TW68_BRIGHT, val);
		break;
	case V4L2_CID_HUE:
		tw_writeb(TW68_HUE, val);
		break;
	case V4L2_CID_CONTRAST:
		tw_writeb(TW68_CONTRAST, val);
		break;
	case V4L2_CID_SATURATION:
		tw_writeb(TW68_SAT_U, val);
		tw_writeb(TW68_SAT_V, val);
		break;
	case V4L2_CID_COLOR_KILLER:
		if (val)
			tw_andorb(TW68_MISC2, 0xe0, 0xe0);
		else
			tw_andorb(TW68_MISC2, 0xe0, 0x00);
		break;
	case V4L2_CID_CHROMA_AGC:
		if (val)
			tw_andorb(TW68_LOOP, 0x30, 0x20);
		else
			tw_andorb(TW68_LOOP, 0x30, 0x00);
		break;
	case V4L2_CID_AUDIO_MUTE:
		/* hack to suppress tvtime complaint */
		break;
#if 0
	case V4L2_CID_AUDIO_VOLUME:
		dev->ctl_volume = val;
		tw68_tvaudio_setvolume(dev, dev->ctl_volume);
		break;
	case V4L2_CID_HFLIP:
		dev->ctl_mirror = val;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
	{
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &dev->tda9887_conf;

		dev->ctl_automute = val;
		if (dev->tda9887_conf) {
			if (dev->ctl_automute)
				dev->tda9887_conf |= TDA9887_AUTOMUTE;
			else
				dev->tda9887_conf &= ~TDA9887_AUTOMUTE;

			tw_call_all(dev, tuner, s_config, &tda9887_cfg);
		}
		break;
	}
#endif
	default:
		err = -EINVAL;
	}
	return err;
}

static int tw68_s_ctrl_internal(struct tw68_dev *dev,  struct tw68_fh *fh,
			 struct v4l2_control *c)
{
	const struct v4l2_queryctrl *ctrl;
	int err;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (fh) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
		err = v4l2_prio_check(&dev->prio, &fh->prio);
#else
		err = v4l2_prio_check(&dev->prio, fh->prio);
#endif
		if (0 != err)
			return err;
	}

	mutex_lock(&dev->lock);

	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl) {
		err = -EINVAL;
		goto error;
	}

	dprintk(DBG_BUFF, "%s: name=%s val=%d\n", __func__,
		ctrl->name, c->value);
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
	};
	err = tw68_s_ctrl_value(dev, c->id, c->value);

error:
	mutex_unlock(&dev->lock);
	return err;
}

static int tw68_s_ctrl(struct file *file, void *f, struct v4l2_control *c)
{
	struct tw68_fh *fh = f;

	return tw68_s_ctrl_internal(fh->dev, fh, c);
}

/* ------------------------------------------------------------------ */

/*
 * Returns a pointer to the currently used queue (e.g. video, vbi, etc.)
 */
static struct videobuf_queue *tw68_queue(struct tw68_fh *fh)
{
	struct videobuf_queue *q = NULL;

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

static int tw68_resource(struct tw68_fh *fh)
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
	int minor = video_devdata(file)->minor;
	struct tw68_dev *dev;
	struct tw68_fh *fh;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int radio = 0;

	mutex_lock(&tw68_devlist_lock);
	list_for_each_entry(dev, &tw68_devlist, devlist) {
		if (dev->video_dev && (dev->video_dev->minor == minor))
			goto found;
		if (dev->radio_dev && (dev->radio_dev->minor == minor)) {
			radio = 1;
			goto found;
		}
		if (dev->vbi_dev && (dev->vbi_dev->minor == minor)) {
			type = V4L2_BUF_TYPE_VBI_CAPTURE;
			goto found;
		}
	}
	mutex_unlock(&tw68_devlist_lock);
	return -ENODEV;

found:
	mutex_unlock(&tw68_devlist_lock);

	dprintk(DBG_FLOW, "%s: minor=%d radio=%d type=%s\n", __func__, minor,
		radio, v4l2_type_names[type]);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	file->private_data = fh;
	fh->dev      = dev;
	fh->radio    = radio;
	fh->type     = type;
	fh->fmt      = format_by_fourcc(V4L2_PIX_FMT_BGR24);
	fh->width    = 720;
	fh->height   = 576;
	v4l2_prio_open(&dev->prio, &fh->prio);

	videobuf_queue_sg_init(&fh->cap, &video_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct tw68_buf),
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
			    fh
#else
			    fh, &dev->lock
#endif
             );
	videobuf_queue_sg_init(&fh->vbi, &tw68_vbi_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct tw68_buf),
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,37)
			    fh
#else
			    fh, &dev->lock
#endif
             );
	if (fh->radio) {
		/* switch to radio mode */
		tw68_tvaudio_setinput(dev, &card(dev).radio);
		tw_call_all(dev, tuner, s_radio);
	} else {
		/* switch to video/vbi mode */
		tw68_tvaudio_setinput(dev, dev->input);
	}
	return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct tw68_fh *fh = file->private_data;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (res_locked(fh->dev, RESOURCE_VIDEO))
			return -EBUSY;
		return videobuf_read_one(tw68_queue(fh),
					 data, count, ppos,
					 file->f_flags & O_NONBLOCK);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (!res_get(fh, RESOURCE_VBI))
			return -EBUSY;
		return videobuf_read_stream(tw68_queue(fh),
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
	struct tw68_fh *fh = file->private_data;
	struct videobuf_buffer *buf = NULL;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type)
		return videobuf_poll_stream(file, &fh->vbi, wait);

	if (res_check(fh, RESOURCE_VIDEO)) {
		if (!list_empty(&fh->cap.stream))
			buf = list_entry(fh->cap.stream.next,
				struct videobuf_buffer, stream);
	} else {
		mutex_lock(&fh->cap.vb_lock);
		if (UNSET == fh->cap.read_off) {
			/* need to capture a new frame */
			if (res_locked(fh->dev, RESOURCE_VIDEO))
				goto err;
			if (0 != fh->cap.ops->buf_prepare(&fh->cap,
					fh->cap.read_buf, fh->cap.field))
				goto err;
			fh->cap.ops->buf_queue(&fh->cap, fh->cap.read_buf);
			fh->cap.read_off = 0;
		}
		mutex_unlock(&fh->cap.vb_lock);
		buf = fh->cap.read_buf;
	}

	if (!buf)
		return POLLERR;

	poll_wait(file, &buf->done, wait);
	if (buf->state == VIDEOBUF_DONE ||
	    buf->state == VIDEOBUF_ERROR)
		return POLLIN | POLLRDNORM;
	return 0;

err:
	mutex_unlock(&fh->cap.vb_lock);
	return POLLERR;
}

static int video_release(struct file *file)
{
	struct tw68_fh  *fh  = file->private_data;
	struct tw68_dev *dev = fh->dev;

	/* stop video capture */
	if (res_check(fh, RESOURCE_VIDEO)) {
		videobuf_streamoff(&fh->cap);
		res_free(fh , RESOURCE_VIDEO);
	}
	if (fh->cap.read_buf) {
		buffer_release(&fh->cap, fh->cap.read_buf);
		kfree(fh->cap.read_buf);
	}

	/* stop vbi capture */
	if (res_check(fh, RESOURCE_VBI)) {
		videobuf_stop(&fh->vbi);
		res_free(fh, RESOURCE_VBI);
	}

#if 0
	tw_call_all(dev, core, s_standby, 0);
#endif

	/* free stuff */
	videobuf_mmap_free(&fh->cap);
	videobuf_mmap_free(&fh->vbi);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
	v4l2_prio_close(&dev->prio, &fh->prio);
#else
	v4l2_prio_close(&dev->prio, fh->prio);
#endif
	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int video_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct tw68_fh *fh = file->private_data;

	return videobuf_mmap_mapper(tw68_queue(fh), vma);
}

/* ------------------------------------------------------------------ */

#if 0
static int tw68_try_get_set_fmt_vbi_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	struct tw68_tvnorm *norm = dev->tvnorm;

	f->fmt.vbi.sampling_rate = 6750000 * 4;
	f->fmt.vbi.samples_per_line = 2048 /* VBI_LINE_LENGTH */;
	f->fmt.vbi.sample_format = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset = 64 * 4;
	f->fmt.vbi.start[0] = norm->vbi_v_start_0;
	f->fmt.vbi.count[0] = norm->vbi_v_stop_0 - norm->vbi_v_start_0 + 1;
	f->fmt.vbi.start[1] = norm->vbi_v_start_1;
	f->fmt.vbi.count[1] = f->fmt.vbi.count[0];
	f->fmt.vbi.flags = 0; /* VBI_UNSYNC VBI_INTERLACED */

#if 0
	if (V4L2_STD_PAL == norm->id) {
		/* FIXME */
		f->fmt.vbi.start[0] += 3;
		f->fmt.vbi.start[1] += 3*2;
	}
#endif
	return 0;
}
#endif

/*
 * Note that this routine returns what is stored in the fh structure, and
 * does not interrogate any of the device registers.
 */
static int tw68_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	f->fmt.pix.width        = fh->width;
	f->fmt.pix.height       = fh->height;
	f->fmt.pix.field        = fh->cap.field;
	f->fmt.pix.pixelformat  = fh->fmt->fourcc;
	f->fmt.pix.bytesperline =
		(f->fmt.pix.width * (fh->fmt->depth)) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.colorspace	= V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int tw68_try_fmt_vid_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	struct tw68_format *fmt;
	enum v4l2_field field;
	unsigned int maxw, maxh;

	dprintk(DBG_FLOW, "%s\n", __func__);
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
		break;
	case V4L2_FIELD_INTERLACED:
		maxh = maxh * 2;
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
		(f->fmt.pix.width * (fmt->depth)) >> 3;
	f->fmt.pix.sizeimage =
		f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

/*
 * Note that tw68_s_fmt_vid_cap sets the information into the fh structure,
 * and it will be used for all future new buffers.  However, there could be
 * some number of buffers on the "active" chain which will be filled before
 * the change takes place.
 */
static int tw68_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int err;

	dprintk(DBG_FLOW, "%s\n", __func__);
	err = tw68_try_fmt_vid_cap(file, priv, f);
	if (0 != err)
		return err;

	fh->fmt       = format_by_fourcc(f->fmt.pix.pixelformat);
	fh->width     = f->fmt.pix.width;
	fh->height    = f->fmt.pix.height;
	fh->cap.field = f->fmt.pix.field;
	/*
	 * The following lines are to make v4l2-test program happy.
	 * The docs should be checked to assure they make sense.
	 */
	f->fmt.pix.colorspace	= V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;
	return 0;
}

static int tw68_queryctrl(struct file *file, void *priv,
			  struct v4l2_queryctrl *c)
{
	const struct v4l2_queryctrl *ctrl;
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if ((c->id <  V4L2_CID_BASE || c->id >= V4L2_CID_LASTP1)
#if 0
	     && (c->id <  V4L2_CID_PRIVATE_BASE ||
	     c->id >= V4L2_CID_PRIVATE_LASTP1)
#endif
	)
		return -EINVAL;
	ctrl = ctrl_by_id(c->id);
	if (NULL == ctrl)
		return -EINVAL;
	*c = *ctrl;
	return 0;
}

static int tw68_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	unsigned int n;

	n = i->index;
	dprintk(DBG_FLOW, "%s: index is %d\n", __func__, n);
	if (n >= TW68_INPUT_MAX) {
		dprintk(DBG_FLOW, "%s: INPUT_MAX reached\n", __func__);
		return -EINVAL;
	}
	if (NULL == card_in(dev, n).name) {
		dprintk(DBG_FLOW, "%s: End of list\n", __func__);
		return -EINVAL;
	}
	memset(i, 0, sizeof(*i));
	i->index = n;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, card_in(dev, n).name);
	if (card_in(dev, n).tv)
		i->type = V4L2_INPUT_TYPE_TUNER;
	i->audioset = 1;
	/* If the query is for the current input, get live data */
	if (n == dev->hw_input->vmux) {
		int v1 = tw_readb(TW68_STATUS1);
		int v2 = tw_readb(TW68_MVSN);

		if (0 != (v1 & (1 << 7)))
			i->status |= V4L2_IN_ST_NO_SYNC;
		if (0 != (v1 & (1 << 6)))
			i->status |= V4L2_IN_ST_NO_H_LOCK;
		if (0 != (v1 & (1 << 2)))
			i->status |= V4L2_IN_ST_NO_SIGNAL;
		if (0 != (v1 & 1 << 1))
			i->status |= V4L2_IN_ST_NO_COLOR;
		if (0 != (v2 & (1 << 2)))
			i->status |= V4L2_IN_ST_MACROVISION;
	}
	i->std = TW68_NORMS;
	return 0;
}

static int tw68_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	*i = dev->input->vmux;
	return 0;
}

static int tw68_s_input(struct file *file, void *priv, unsigned int i)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int err;

	dprintk(DBG_FLOW, "%s\n", __func__);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
		err = v4l2_prio_check(&dev->prio, &fh->prio);
#else
		err = v4l2_prio_check(&dev->prio, fh->prio);
#endif
		if (0 != err)
	if (0 != err)
		return err;

	if (i < 0  ||  i >= TW68_INPUT_MAX)
		return -EINVAL;
	if (NULL == card_in(dev, i).name)
		return -EINVAL;
	mutex_lock(&dev->lock);
	video_mux(dev, i);
	mutex_unlock(&dev->lock);
	return 0;
}

static int tw68_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	unsigned int tuner_type = dev->tuner_type;

	dprintk(DBG_FLOW, "%s\n", __func__);
	strcpy(cap->driver, "tw68");
	strlcpy(cap->card, tw68_boards[dev->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));
	cap->version = TW68_VERSION_CODE;
	cap->capabilities =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_VBI_CAPTURE |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING |
		V4L2_CAP_TUNER;

	if ((tuner_type == TUNER_ABSENT) || (tuner_type == UNSET))
		cap->capabilities &= ~V4L2_CAP_TUNER;
	return 0;
}

static int tw68_s_std_internal(struct tw68_dev *dev, struct tw68_fh *fh,
			v4l2_std_id *id)
{
/*	unsigned long flags; */
	unsigned int i;
	v4l2_std_id fixup;
	int err;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (fh) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
		err = v4l2_prio_check(&dev->prio, &fh->prio);
#else
		err = v4l2_prio_check(&dev->prio, fh->prio);
#endif
		if (0 != err)
		if (0 != err)
			return err;
	}

	/* Look for match on complete norm id (may have mult bits) */
	for (i = 0; i < TVNORMS; i++) {
		if (*id == tvnorms[i].id)
			break;
	}

	/* If no exact match, look for norm which contains this one */
	if (i == TVNORMS)
		for (i = 0; i < TVNORMS; i++) {
			if (*id & tvnorms[i].id)
				break;
		}
	/* If still not matched, give up */
	if (i == TVNORMS)
		return -EINVAL;

	/* TODO - verify this additional work with SECAM applies to TW */
	if ((*id & V4L2_STD_SECAM) && (secam[0] != '-')) {
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
		for (i = 0; i < TVNORMS; i++)
			if (fixup == tvnorms[i].id)
				break;
	}

	*id = tvnorms[i].id;
	mutex_lock(&dev->lock);
	set_tvnorm(dev, &tvnorms[i]);	/* do the actual setting */
	tw68_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}

static int tw68_s_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	return tw68_s_std_internal(fh->dev, fh, id);
}

static int tw68_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	*id = dev->tvnorm->id;
	return 0;
}

static int tw68_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int n;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;
	memset(t, 0, sizeof(*t));
	for (n = 0; n < TW68_INPUT_MAX; n++)
		if (card_in(dev, n).tv)
			break;
	if (n == TW68_INPUT_MAX)
		return -EINVAL;
#if 0
	if (NULL != card_in(dev, n).name) {
		strcpy(t->name, "Television");
		t->type = V4L2_TUNER_ANALOG_TV;
		t->capability = V4L2_TUNER_CAP_NORM |
			V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 |
			V4L2_TUNER_CAP_LANG2;
		t->rangehigh = 0xffffffffUL;
		t->rxsubchans = tw68_tvaudio_getstereo(dev);
		t->audmode = tw68_tvaudio_rx2mode(t->rxsubchans);
	}
	if (0 != (saa_readb(TW68_STATUS_VIDEO1) & 0x03))
		t->signal = 0xffff;
#endif
	return 0;
}

static int tw68_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *t)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int err;
#if 0
	int rx, mode
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
		err = v4l2_prio_check(&dev->prio, &fh->prio);
#else
		err = v4l2_prio_check(&dev->prio, fh->prio);
#endif
		if (0 != err)
	if (0 != err)
		return err;

#if 0
	mode = dev->thread.mode;
	if (UNSET == mode) {
		rx   = tw68_tvaudio_getstereo(dev);
		mode = tw68_tvaudio_rx2mode(t->rxsubchans);
	}
	if (mode != t->audmode)
		dev->thread.mode = t->audmode;
#endif
	return 0;
}

static int tw68_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	if (unlikely(dev->tuner_type))
		return -EINVAL;
	f->type = fh->radio ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
/*	f->frequency = dev->ctl_freq; */

	return 0;
}

static int tw68_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int err;

	if (unlikely(UNSET == dev->tuner_type))
		return -EINVAL;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,34)
		err = v4l2_prio_check(&dev->prio, &fh->prio);
#else
		err = v4l2_prio_check(&dev->prio, fh->prio);
#endif
		if (0 != err)
	if (0 != err)
		return err;

	if (0 != f->tuner)
		return -EINVAL;
	if (0 == fh->radio && V4L2_TUNER_ANALOG_TV != f->type)
		return -EINVAL;
	if (1 == fh->radio && V4L2_TUNER_RADIO != f->type)
		return -EINVAL;
	mutex_lock(&dev->lock);
/*	dev->ctl_freq = f->frequency; */

	tw_call_all(dev, tuner, s_frequency, f);

	tw68_tvaudio_do_scan(dev);
	mutex_unlock(&dev->lock);
	return 0;
}

static int tw68_g_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	strcpy(a->name, "audio");
	return 0;
}

static int tw68_s_audio(struct file *file, void *priv, struct v4l2_audio *a)
{
	return 0;
}

static int tw68_g_priority(struct file *file, void *f, enum v4l2_priority *p)
{
	struct tw68_fh *fh = f;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	*p = v4l2_prio_max(&dev->prio);
	return 0;
}

static int tw68_s_priority(struct file *file, void *f,
					enum v4l2_priority prio)
{
	struct tw68_fh *fh = f;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	return v4l2_prio_change(&dev->prio, &fh->prio, prio);
}

static int tw68_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (f->index >= FORMATS)
		return -EINVAL;

	strlcpy(f->description, formats[f->index].name,
		sizeof(f->description));

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int tw68_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cap)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (cap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
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

static int tw68_g_crop(struct file *file, void *f, struct v4l2_crop *crop)
{
	struct tw68_fh *fh = f;
	struct tw68_dev *dev = fh->dev;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	crop->c = dev->crop_current;
	return 0;
}

static int tw68_s_crop(struct file *file, void *f, struct v4l2_crop *crop)
{
	struct tw68_fh *fh = f;
	struct tw68_dev *dev = fh->dev;
	struct v4l2_rect *b = &dev->crop_bounds;

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (res_locked(fh->dev, RESOURCE_VIDEO))
		return -EBUSY;

	if ((crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
	    (crop->c.height < 0) || (crop->c.width < 0)) {
		dprintk(DBG_UNEXPECTED, "%s: invalid request\n", __func__);
		return -EINVAL;
	}

	if (crop->c.top < b->top)
		crop->c.top = b->top;
	if (crop->c.top > b->top + b->height)
		crop->c.top = b->top + b->height;
	if (crop->c.height > b->top - crop->c.top + b->height)
		crop->c.height = b->top - crop->c.top + b->height;

	if (crop->c.left < b->left)
		crop->c.left = b->left;
	if (crop->c.left > b->left + b->width)
		crop->c.left = b->left + b->width;
	if (crop->c.width > b->left - crop->c.left + b->width)
		crop->c.width = b->left - crop->c.left + b->width;

	dprintk(DBG_FLOW, "%s: setting cropping rectangle: top=%d, left=%d, "
		    "width=%d, height=%d\n", __func__, crop->c.top,
		    crop->c.left, crop->c.width, crop->c.height);
	dev->crop_current = crop->c;
	return 0;
}

/*
 * Wrappers for the v4l2_ioctl_ops functions
 */
#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
	struct tw68_fh *fh = file->private_data;
	return videobuf_cgmbuf(tw68_queue(fh), mbuf, 8);
}
#endif

static int tw68_reqbufs(struct file *file, void *priv,
					struct v4l2_requestbuffers *p)
{
	struct tw68_fh *fh = priv;
	return videobuf_reqbufs(tw68_queue(fh), p);
}

static int tw68_querybuf(struct file *file, void *priv,
					struct v4l2_buffer *b)
{
	struct tw68_fh *fh = priv;
	return videobuf_querybuf(tw68_queue(fh), b);
}

static int tw68_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct tw68_fh *fh = priv;
	return videobuf_qbuf(tw68_queue(fh), b);
}

static int tw68_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct tw68_fh *fh = priv;
	return videobuf_dqbuf(tw68_queue(fh), b,
				file->f_flags & O_NONBLOCK);
}

static int tw68_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int res = tw68_resource(fh);

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (!res_get(fh, res))
		return -EBUSY;

	tw68_buffer_requeue(dev, &dev->video_q);
	return videobuf_streamon(tw68_queue(fh));
}

static int tw68_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	int err;
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;
	int res = tw68_resource(fh);

	dprintk(DBG_FLOW, "%s\n", __func__);
	err = videobuf_streamoff(tw68_queue(fh));
	if (err < 0)
		return err;
	res_free(fh, res);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
/*
 * Used strictly for internal development and debugging, this routine
 * prints out the current register contents for the tw68xx device.
 */
static void tw68_dump_regs(struct tw68_dev *dev)
{
	unsigned char line[80];
	int i, j, k;
	unsigned char *cptr;

	printk(KERN_DEBUG "Full dump of TW68 registers:\n");
	/* First we do the PCI regs, 8 4-byte regs per line */
	for (i = 0; i < 0x100; i += 32) {
		cptr = line;
		cptr += sprintf(cptr, "%03x  ", i);
		/* j steps through the next 4 words */
		for (j = i; j < i + 16; j += 4)
			cptr += sprintf(cptr, "%08x ", tw_readl(j));
		*cptr++ = ' ';
		for (; j < i + 32; j += 4)
			cptr += sprintf(cptr, "%08x ", tw_readl(j));
		*cptr++ = '\n';
		*cptr = 0;
		printk(KERN_DEBUG "%s", line);
	}
	/* Next the control regs, which are single-byte, address mod 4 */
	while (i < 0x400) {
		cptr = line;
		cptr += sprintf(cptr, "%03x ", i);
		/* Print out 4 groups of 4 bytes */
		for (j = 0; j < 4; j++) {
			for (k = 0; k < 4; k++) {
				cptr += sprintf(cptr, "%02x ",
					tw_readb(i));
				i += 4;
			}
			*cptr++ = ' ';
		}
		*cptr++ = '\n';
		*cptr = 0;
		printk(KERN_DEBUG "%s", line);
	}
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;

	tw68_dump_regs(dev);
	return 0;
}

static int vidioc_g_register(struct file *file, void *priv,
			      struct v4l2_dbg_register *reg)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;	/* needed for tw_readb */

	dprintk(DBG_FLOW, "%s\n", __func__);
	if (!v4l2_chip_match_host(&reg->match))
		dprintk(DBG_UNEXPECTED, "%s: match failed\n", __func__);
		return -EINVAL;
	if (reg->size == 1)
		reg->val = tw_readb(reg->reg);
	else
		reg->val = tw_readl(reg->reg);
	return 0;
}

static int vidioc_s_register(struct file *file, void *priv,
				struct v4l2_dbg_register *reg)
{
	struct tw68_fh *fh = priv;
	struct tw68_dev *dev = fh->dev;	/* needed for tw_writeb */

	dprintk(DBG_FLOW, "%s: request to set reg 0x%04x to 0x%02x\n",
		__func__, (unsigned int)reg->reg, (unsigned int)reg->val);
	if (!v4l2_chip_match_host(&reg->match)) {
		dprintk(DBG_UNEXPECTED, "%s: match failed\n", __func__);
		return -EINVAL;
	}
	if (reg->size == 1)
		tw_writeb(reg->reg, reg->val);
	else
		tw_writel(reg->reg & 0xffff, reg->val);
	return 0;
}
#endif

static const struct v4l2_file_operations video_fops = {
	.owner			= THIS_MODULE,
	.open			= video_open,
	.release		= video_release,
	.read			= video_read,
	.poll			= video_poll,
	.mmap			= video_mmap,
	.ioctl			= video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap		= tw68_querycap,
	.vidioc_enum_fmt_vid_cap	= tw68_enum_fmt_vid_cap,
	.vidioc_reqbufs			= tw68_reqbufs,
	.vidioc_querybuf		= tw68_querybuf,
	.vidioc_qbuf			= tw68_qbuf,
	.vidioc_dqbuf			= tw68_dqbuf,
	.vidioc_s_std			= tw68_s_std,
	.vidioc_g_std			= tw68_g_std,
	.vidioc_enum_input		= tw68_enum_input,
	.vidioc_g_input			= tw68_g_input,
	.vidioc_s_input			= tw68_s_input,
	.vidioc_queryctrl		= tw68_queryctrl,
	.vidioc_g_ctrl			= tw68_g_ctrl,
	.vidioc_s_ctrl			= tw68_s_ctrl,
	.vidioc_streamon		= tw68_streamon,
	.vidioc_streamoff		= tw68_streamoff,
	.vidioc_g_priority		= tw68_g_priority,
	.vidioc_s_priority		= tw68_s_priority,
	.vidioc_g_fmt_vid_cap		= tw68_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= tw68_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= tw68_s_fmt_vid_cap,
	.vidioc_cropcap			= tw68_cropcap,
	.vidioc_g_crop			= tw68_g_crop,
	.vidioc_s_crop			= tw68_s_crop,
/*
 * Functions not yet implemented / not yet passing tests.
 */

#if 0
	.vidioc_g_fmt_vbi_cap		= tw68_try_get_set_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap		= tw68_try_get_set_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap		= tw68_try_get_set_fmt_vbi_cap,
#endif
	.vidioc_g_audio			= tw68_g_audio,
	.vidioc_s_audio			= tw68_s_audio,
	.vidioc_g_tuner			= tw68_g_tuner,
	.vidioc_s_tuner			= tw68_s_tuner,
	.vidioc_g_frequency		= tw68_g_frequency,
	.vidioc_s_frequency		= tw68_s_frequency,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf			= vidiocgmbuf,
#endif
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_log_status		= vidioc_log_status,
	.vidioc_g_register              = vidioc_g_register,
	.vidioc_s_register              = vidioc_s_register,
#endif
};

/* ------------------------------------------------------------------ */
/* exported stuff                                                     */
struct video_device tw68_video_template = {
	.name			= "tw68_video",
	.fops			= &video_fops,
	.ioctl_ops		= &video_ioctl_ops,
	.minor			= -1,
	.tvnorms		= TW68_NORMS,
	.current_norm		= V4L2_STD_PAL,
};

struct video_device tw68_radio_template = {
	.name			= "tw68_radio",
};

int tw68_videoport_init(struct tw68_dev *dev)
{
	return 0;
}

void tw68_set_tvnorm_hw(struct tw68_dev *dev)
{
	tw_andorb(TW68_SDT, 0x07, dev->tvnorm->format);
	return;
}

int tw68_video_init1(struct tw68_dev *dev)
{
	int i;

	dprintk(DBG_FLOW, "%s\n", __func__);
	/* sanitycheck insmod options */
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsz < 0 || gbufsz > gbufsz_max)
		gbufsz = gbufsz_max;
	gbufsz = (gbufsz + PAGE_SIZE - 1) & PAGE_MASK;

	/* put some sensible defaults into the data structures ... */
	for (i = 0; i < CTRLS; i++)
		tw68_s_ctrl_value(dev, video_ctrls[i].id,
				  video_ctrls[i].default_value);
#if 0
	if (dev->tda9887_conf && dev->ctl_automute)
		dev->tda9887_conf |= TDA9887_AUTOMUTE;
	dev->automute       = 0;
#endif
	INIT_LIST_HEAD(&dev->video_q.queued);
	INIT_LIST_HEAD(&dev->video_q.active);
	init_timer(&dev->video_q.timeout);
	dev->video_q.timeout.function	= tw68_buffer_timeout;
	dev->video_q.timeout.data	= (unsigned long)(&dev->video_q);
	dev->video_q.dev		= dev;
	dev->video_q.buf_compat		= tw68_check_video_fmt;
	dev->video_q.start_dma		= tw68_video_start_dma;
	tw68_risc_stopper(dev->pci, &dev->video_q.stopper);

	if (tw68_boards[dev->board].video_out)
		tw68_videoport_init(dev);

	return 0;
}

int tw68_video_init2(struct tw68_dev *dev)
{
	dprintk(DBG_FLOW, "%s\n", __func__);
	set_tvnorm(dev, &tvnorms[0]);
	video_mux(dev, 0);
/*
	tw68_tvaudio_setmut(dev);
	tw68_tvaudio_setvolume(dev, dev->ctl_volume);
*/
	return 0;
}

/*
 * tw68_irq_video_signalchange
 *
 * TODO:
 * Check for presence of video signal.  If not present, mute audio.
 * If present, log type of signal present.
 */
void tw68_irq_video_signalchange(struct tw68_dev *dev)
{
	return;
}

/*
 * tw68_irq_video_done
 */
void tw68_irq_video_done(struct tw68_dev *dev, unsigned long status)
{
	__u32 reg;

	/* reset interrupts handled by this routine */
	tw_writel(TW68_INTSTAT, status);
	/*
	 * Check most likely first
	 *
	 * DMAPI shows we have reached the end of the risc code
	 * for the current buffer.
	 */
	if (status & TW68_DMAPI) {
		struct tw68_dmaqueue *q = &dev->video_q;
		dprintk(DBG_FLOW | DBG_TESTING, "DMAPI interrupt\n");
		spin_lock(&dev->slock);
		/*
		 * tw68_wakeup will take care of the buffer handling,
		 * plus any non-video requirements.
		 */
		tw68_wakeup(q, &dev->video_fieldcount);
		spin_unlock(&dev->slock);
		/* Check whether we have gotten into 'stopper' code */
		reg = tw_readl(TW68_DMAP_PP);
		if ((reg >= q->stopper.dma) &&
		    (reg < q->stopper.dma + q->stopper.size)) {
			/* Yes - log the information */
			dprintk(DBG_FLOW | DBG_TESTING,
				"%s: stopper risc code entered\n", __func__);
		}
		status &= ~(TW68_DMAPI);
		if (0 == status)
			return;
	}
	if (status & (TW68_VLOCK | TW68_HLOCK)) { /* lost sync */
		dprintk(DBG_UNUSUAL, "Lost sync\n");
	}
	if (status & TW68_PABORT) {	/* TODO - what should we do? */
		dprintk(DBG_UNEXPECTED, "PABORT interrupt\n");
	}
	if (status & TW68_DMAPERR) {
		dprintk(DBG_UNEXPECTED, "DMAPERR interrupt\n");
#if 0
		/* Stop risc & fifo */
		tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
		tw_clearl(TW68_INTMASK, dev->board_virqmask);
		dev->pci_irqmask &= ~dev->board_virqmask;
#endif
	}
	/*
	 * On TW6800, FDMIS is apparently generated if video input is switched
	 * during operation.  Therefore, it is not enabled for that chip.
	 */
	if (status & TW68_FDMIS) {	/* logic error somewhere */
		dprintk(DBG_UNEXPECTED, "FDMIS interrupt\n");
		/* Stop risc & fifo */
//		tw_clearl(TW68_DMAC, TW68_DMAP_EN | TW68_FIFO_EN);
//		tw_clearl(TW68_INTMASK, dev->board_virqmask);
//		dev->pci_irqmask &= ~dev->board_virqmask;
	}
	if (status & TW68_FFOF) {	/* probably a logic error */
		reg = tw_readl(TW68_DMAC) & TW68_FIFO_EN;
		tw_clearl(TW68_DMAC, TW68_FIFO_EN);
		dprintk(DBG_UNUSUAL, "FFOF interrupt\n");
		tw_setl(TW68_DMAC, reg);
	}
	if (status & TW68_FFERR)
		dprintk(DBG_UNEXPECTED, "FFERR interrupt\n");
	return;
}
