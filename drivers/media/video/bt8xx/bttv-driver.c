/*

    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97,98 Ralph  Metzler <rjkm@thp.uni-koeln.de>
			   & Marcus Metzler <mocm@thp.uni-koeln.de>
    (c) 1999-2002 Gerd Knorr <kraxel@bytesex.org>

    some v4l2 code lines are taken from Justin's bttv2 driver which is
    (c) 2000 Justin Schoeman <justin@suntiger.ee.up.ac.za>

    Cropping and overscan support
    Copyright (C) 2005, 2006 Michael H. Schimek <mschimek@gmx.at>
    Sponsored by OPQ Systems AB

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include "bttvp.h"
#include <media/v4l2-common.h>
#include <media/tvaudio.h>
#include <media/msp3400.h>

#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/byteorder.h>

#include <media/rds.h>


unsigned int bttv_num;			/* number of Bt848s in use */
struct bttv bttvs[BTTV_MAX];

unsigned int bttv_debug;
unsigned int bttv_verbose = 1;
unsigned int bttv_gpio;

/* config variables */
#ifdef __BIG_ENDIAN
static unsigned int bigendian=1;
#else
static unsigned int bigendian;
#endif
static unsigned int radio[BTTV_MAX];
static unsigned int irq_debug;
static unsigned int gbuffers = 8;
static unsigned int gbufsize = 0x208000;
static unsigned int reset_crop = 1;

static int video_nr = -1;
static int radio_nr = -1;
static int vbi_nr = -1;
static int debug_latency;

static unsigned int fdsr;

/* options */
static unsigned int combfilter;
static unsigned int lumafilter;
static unsigned int automute    = 1;
static unsigned int chroma_agc;
static unsigned int adc_crush   = 1;
static unsigned int whitecrush_upper = 0xCF;
static unsigned int whitecrush_lower = 0x7F;
static unsigned int vcr_hack;
static unsigned int irq_iswitch;
static unsigned int uv_ratio    = 50;
static unsigned int full_luma_range;
static unsigned int coring;
extern int no_overlay;

/* API features (turn on/off stuff for testing) */
static unsigned int v4l2        = 1;

/* insmod args */
module_param(bttv_verbose,      int, 0644);
module_param(bttv_gpio,         int, 0644);
module_param(bttv_debug,        int, 0644);
module_param(irq_debug,         int, 0644);
module_param(debug_latency,     int, 0644);

module_param(fdsr,              int, 0444);
module_param(video_nr,          int, 0444);
module_param(radio_nr,          int, 0444);
module_param(vbi_nr,            int, 0444);
module_param(gbuffers,          int, 0444);
module_param(gbufsize,          int, 0444);
module_param(reset_crop,        int, 0444);

module_param(v4l2,              int, 0644);
module_param(bigendian,         int, 0644);
module_param(irq_iswitch,       int, 0644);
module_param(combfilter,        int, 0444);
module_param(lumafilter,        int, 0444);
module_param(automute,          int, 0444);
module_param(chroma_agc,        int, 0444);
module_param(adc_crush,         int, 0444);
module_param(whitecrush_upper,  int, 0444);
module_param(whitecrush_lower,  int, 0444);
module_param(vcr_hack,          int, 0444);
module_param(uv_ratio,          int, 0444);
module_param(full_luma_range,   int, 0444);
module_param(coring,            int, 0444);

module_param_array(radio, int, NULL, 0444);

MODULE_PARM_DESC(radio,"The TV card supports radio, default is 0 (no)");
MODULE_PARM_DESC(bigendian,"byte order of the framebuffer, default is native endian");
MODULE_PARM_DESC(bttv_verbose,"verbose startup messages, default is 1 (yes)");
MODULE_PARM_DESC(bttv_gpio,"log gpio changes, default is 0 (no)");
MODULE_PARM_DESC(bttv_debug,"debug messages, default is 0 (no)");
MODULE_PARM_DESC(irq_debug,"irq handler debug messages, default is 0 (no)");
MODULE_PARM_DESC(gbuffers,"number of capture buffers. range 2-32, default 8");
MODULE_PARM_DESC(gbufsize,"size of the capture buffers, default is 0x208000");
MODULE_PARM_DESC(reset_crop,"reset cropping parameters at open(), default "
		 "is 1 (yes) for compatibility with older applications");
MODULE_PARM_DESC(automute,"mute audio on bad/missing video signal, default is 1 (yes)");
MODULE_PARM_DESC(chroma_agc,"enables the AGC of chroma signal, default is 0 (no)");
MODULE_PARM_DESC(adc_crush,"enables the luminance ADC crush, default is 1 (yes)");
MODULE_PARM_DESC(whitecrush_upper,"sets the white crush upper value, default is 207");
MODULE_PARM_DESC(whitecrush_lower,"sets the white crush lower value, default is 127");
MODULE_PARM_DESC(vcr_hack,"enables the VCR hack (improves synch on poor VCR tapes), default is 0 (no)");
MODULE_PARM_DESC(irq_iswitch,"switch inputs in irq handler");
MODULE_PARM_DESC(uv_ratio,"ratio between u and v gains, default is 50");
MODULE_PARM_DESC(full_luma_range,"use the full luma range, default is 0 (no)");
MODULE_PARM_DESC(coring,"set the luma coring level, default is 0 (no)");

MODULE_DESCRIPTION("bttv - v4l/v4l2 driver module for bt848/878 based cards");
MODULE_AUTHOR("Ralph Metzler & Marcus Metzler & Gerd Knorr");
MODULE_LICENSE("GPL");

/* ----------------------------------------------------------------------- */
/* sysfs                                                                   */

static ssize_t show_card(struct class_device *cd, char *buf)
{
	struct video_device *vfd = to_video_device(cd);
	struct bttv *btv = dev_get_drvdata(vfd->dev);
	return sprintf(buf, "%d\n", btv ? btv->c.type : UNSET);
}
static CLASS_DEVICE_ATTR(card, S_IRUGO, show_card, NULL);

/* ----------------------------------------------------------------------- */
/* static data                                                             */

/* special timing tables from conexant... */
static u8 SRAM_Table[][60] =
{
	/* PAL digital input over GPIO[7:0] */
	{
		45, // 45 bytes following
		0x36,0x11,0x01,0x00,0x90,0x02,0x05,0x10,0x04,0x16,
		0x12,0x05,0x11,0x00,0x04,0x12,0xC0,0x00,0x31,0x00,
		0x06,0x51,0x08,0x03,0x89,0x08,0x07,0xC0,0x44,0x00,
		0x81,0x01,0x01,0xA9,0x0D,0x02,0x02,0x50,0x03,0x37,
		0x37,0x00,0xAF,0x21,0x00
	},
	/* NTSC digital input over GPIO[7:0] */
	{
		51, // 51 bytes following
		0x0C,0xC0,0x00,0x00,0x90,0x02,0x03,0x10,0x03,0x06,
		0x10,0x04,0x12,0x12,0x05,0x02,0x13,0x04,0x19,0x00,
		0x04,0x39,0x00,0x06,0x59,0x08,0x03,0x83,0x08,0x07,
		0x03,0x50,0x00,0xC0,0x40,0x00,0x86,0x01,0x01,0xA6,
		0x0D,0x02,0x03,0x11,0x01,0x05,0x37,0x00,0xAC,0x21,
		0x00,
	},
	// TGB_NTSC392 // quartzsight
	// This table has been modified to be used for Fusion Rev D
	{
		0x2A, // size of table = 42
		0x06, 0x08, 0x04, 0x0a, 0xc0, 0x00, 0x18, 0x08, 0x03, 0x24,
		0x08, 0x07, 0x02, 0x90, 0x02, 0x08, 0x10, 0x04, 0x0c, 0x10,
		0x05, 0x2c, 0x11, 0x04, 0x55, 0x48, 0x00, 0x05, 0x50, 0x00,
		0xbf, 0x0c, 0x02, 0x2f, 0x3d, 0x00, 0x2f, 0x3f, 0x00, 0xc3,
		0x20, 0x00
	}
};

/* minhdelayx1	first video pixel we can capture on a line and
   hdelayx1	start of active video, both relative to rising edge of
		/HRESET pulse (0H) in 1 / fCLKx1.
   swidth	width of active video and
   totalwidth	total line width, both in 1 / fCLKx1.
   sqwidth	total line width in square pixels.
   vdelay	start of active video in 2 * field lines relative to
		trailing edge of /VRESET pulse (VDELAY register).
   sheight	height of active video in 2 * field lines.
   videostart0	ITU-R frame line number of the line corresponding
		to vdelay in the first field. */
#define CROPCAP(minhdelayx1, hdelayx1, swidth, totalwidth, sqwidth,	 \
		vdelay, sheight, videostart0)				 \
	.cropcap.bounds.left = minhdelayx1,				 \
	/* * 2 because vertically we count field lines times two, */	 \
	/* e.g. 23 * 2 to 23 * 2 + 576 in PAL-BGHI defrect. */		 \
	.cropcap.bounds.top = (videostart0) * 2 - (vdelay) + MIN_VDELAY, \
	/* 4 is a safety margin at the end of the line. */		 \
	.cropcap.bounds.width = (totalwidth) - (minhdelayx1) - 4,	 \
	.cropcap.bounds.height = (sheight) + (vdelay) - MIN_VDELAY,	 \
	.cropcap.defrect.left = hdelayx1,				 \
	.cropcap.defrect.top = (videostart0) * 2,			 \
	.cropcap.defrect.width = swidth,				 \
	.cropcap.defrect.height = sheight,				 \
	.cropcap.pixelaspect.numerator = totalwidth,			 \
	.cropcap.pixelaspect.denominator = sqwidth,

const struct bttv_tvnorm bttv_tvnorms[] = {
	/* PAL-BDGHI */
	/* max. active video is actually 922, but 924 is divisible by 4 and 3! */
	/* actually, max active PAL with HSCALE=0 is 948, NTSC is 768 - nil */
	{
		.v4l2_id        = V4L2_STD_PAL,
		.name           = "PAL",
		.Fsc            = 35468950,
		.swidth         = 924,
		.sheight        = 576,
		.totalwidth     = 1135,
		.adelay         = 0x7f,
		.bdelay         = 0x72,
		.iform          = (BT848_IFORM_PAL_BDGHI|BT848_IFORM_XT1),
		.scaledtwidth   = 1135,
		.hdelayx1       = 186,
		.hactivex1      = 924,
		.vdelay         = 0x20,
		.vbipack        = 255, /* min (2048 / 4, 0x1ff) & 0xff */
		.sram           = 0,
		/* ITU-R frame line number of the first VBI line
		   we can capture, of the first and second field.
		   The last line is determined by cropcap.bounds. */
		.vbistart       = { 7, 320 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 186,
			/* Should be (768 * 1135 + 944 / 2) / 944.
			   cropcap.defrect is used for image width
			   checks, so we keep the old value 924. */
			/* swidth */ 924,
			/* totalwidth */ 1135,
			/* sqwidth */ 944,
			/* vdelay */ 0x20,
			/* sheight */ 576,
			/* videostart0 */ 23)
		/* bt878 (and bt848?) can capture another
		   line below active video. */
		.cropcap.bounds.height = (576 + 2) + 0x20 - 2,
	},{
		.v4l2_id        = V4L2_STD_NTSC_M | V4L2_STD_NTSC_M_KR,
		.name           = "NTSC",
		.Fsc            = 28636363,
		.swidth         = 768,
		.sheight        = 480,
		.totalwidth     = 910,
		.adelay         = 0x68,
		.bdelay         = 0x5d,
		.iform          = (BT848_IFORM_NTSC|BT848_IFORM_XT0),
		.scaledtwidth   = 910,
		.hdelayx1       = 128,
		.hactivex1      = 910,
		.vdelay         = 0x1a,
		.vbipack        = 144, /* min (1600 / 4, 0x1ff) & 0xff */
		.sram           = 1,
		.vbistart	= { 10, 273 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 128,
			/* Should be (640 * 910 + 780 / 2) / 780? */
			/* swidth */ 768,
			/* totalwidth */ 910,
			/* sqwidth */ 780,
			/* vdelay */ 0x1a,
			/* sheight */ 480,
			/* videostart0 */ 23)
	},{
		.v4l2_id        = V4L2_STD_SECAM,
		.name           = "SECAM",
		.Fsc            = 35468950,
		.swidth         = 924,
		.sheight        = 576,
		.totalwidth     = 1135,
		.adelay         = 0x7f,
		.bdelay         = 0xb0,
		.iform          = (BT848_IFORM_SECAM|BT848_IFORM_XT1),
		.scaledtwidth   = 1135,
		.hdelayx1       = 186,
		.hactivex1      = 922,
		.vdelay         = 0x20,
		.vbipack        = 255,
		.sram           = 0, /* like PAL, correct? */
		.vbistart	= { 7, 320 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 186,
			/* swidth */ 924,
			/* totalwidth */ 1135,
			/* sqwidth */ 944,
			/* vdelay */ 0x20,
			/* sheight */ 576,
			/* videostart0 */ 23)
	},{
		.v4l2_id        = V4L2_STD_PAL_Nc,
		.name           = "PAL-Nc",
		.Fsc            = 28636363,
		.swidth         = 640,
		.sheight        = 576,
		.totalwidth     = 910,
		.adelay         = 0x68,
		.bdelay         = 0x5d,
		.iform          = (BT848_IFORM_PAL_NC|BT848_IFORM_XT0),
		.scaledtwidth   = 780,
		.hdelayx1       = 130,
		.hactivex1      = 734,
		.vdelay         = 0x1a,
		.vbipack        = 144,
		.sram           = -1,
		.vbistart	= { 7, 320 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 130,
			/* swidth */ (640 * 910 + 780 / 2) / 780,
			/* totalwidth */ 910,
			/* sqwidth */ 780,
			/* vdelay */ 0x1a,
			/* sheight */ 576,
			/* videostart0 */ 23)
	},{
		.v4l2_id        = V4L2_STD_PAL_M,
		.name           = "PAL-M",
		.Fsc            = 28636363,
		.swidth         = 640,
		.sheight        = 480,
		.totalwidth     = 910,
		.adelay         = 0x68,
		.bdelay         = 0x5d,
		.iform          = (BT848_IFORM_PAL_M|BT848_IFORM_XT0),
		.scaledtwidth   = 780,
		.hdelayx1       = 135,
		.hactivex1      = 754,
		.vdelay         = 0x1a,
		.vbipack        = 144,
		.sram           = -1,
		.vbistart	= { 10, 273 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 135,
			/* swidth */ (640 * 910 + 780 / 2) / 780,
			/* totalwidth */ 910,
			/* sqwidth */ 780,
			/* vdelay */ 0x1a,
			/* sheight */ 480,
			/* videostart0 */ 23)
	},{
		.v4l2_id        = V4L2_STD_PAL_N,
		.name           = "PAL-N",
		.Fsc            = 35468950,
		.swidth         = 768,
		.sheight        = 576,
		.totalwidth     = 1135,
		.adelay         = 0x7f,
		.bdelay         = 0x72,
		.iform          = (BT848_IFORM_PAL_N|BT848_IFORM_XT1),
		.scaledtwidth   = 944,
		.hdelayx1       = 186,
		.hactivex1      = 922,
		.vdelay         = 0x20,
		.vbipack        = 144,
		.sram           = -1,
		.vbistart       = { 7, 320 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 186,
			/* swidth */ (768 * 1135 + 944 / 2) / 944,
			/* totalwidth */ 1135,
			/* sqwidth */ 944,
			/* vdelay */ 0x20,
			/* sheight */ 576,
			/* videostart0 */ 23)
	},{
		.v4l2_id        = V4L2_STD_NTSC_M_JP,
		.name           = "NTSC-JP",
		.Fsc            = 28636363,
		.swidth         = 640,
		.sheight        = 480,
		.totalwidth     = 910,
		.adelay         = 0x68,
		.bdelay         = 0x5d,
		.iform          = (BT848_IFORM_NTSC_J|BT848_IFORM_XT0),
		.scaledtwidth   = 780,
		.hdelayx1       = 135,
		.hactivex1      = 754,
		.vdelay         = 0x16,
		.vbipack        = 144,
		.sram           = -1,
		.vbistart       = { 10, 273 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 135,
			/* swidth */ (640 * 910 + 780 / 2) / 780,
			/* totalwidth */ 910,
			/* sqwidth */ 780,
			/* vdelay */ 0x16,
			/* sheight */ 480,
			/* videostart0 */ 23)
	},{
		/* that one hopefully works with the strange timing
		 * which video recorders produce when playing a NTSC
		 * tape on a PAL TV ... */
		.v4l2_id        = V4L2_STD_PAL_60,
		.name           = "PAL-60",
		.Fsc            = 35468950,
		.swidth         = 924,
		.sheight        = 480,
		.totalwidth     = 1135,
		.adelay         = 0x7f,
		.bdelay         = 0x72,
		.iform          = (BT848_IFORM_PAL_BDGHI|BT848_IFORM_XT1),
		.scaledtwidth   = 1135,
		.hdelayx1       = 186,
		.hactivex1      = 924,
		.vdelay         = 0x1a,
		.vbipack        = 255,
		.vtotal         = 524,
		.sram           = -1,
		.vbistart	= { 10, 273 },
		CROPCAP(/* minhdelayx1 */ 68,
			/* hdelayx1 */ 186,
			/* swidth */ 924,
			/* totalwidth */ 1135,
			/* sqwidth */ 944,
			/* vdelay */ 0x1a,
			/* sheight */ 480,
			/* videostart0 */ 23)
	}
};
static const unsigned int BTTV_TVNORMS = ARRAY_SIZE(bttv_tvnorms);

/* ----------------------------------------------------------------------- */
/* bttv format list
   packed pixel formats must come first */
static const struct bttv_format bttv_formats[] = {
	{
		.name     = "8 bpp, gray",
		.palette  = VIDEO_PALETTE_GREY,
		.fourcc   = V4L2_PIX_FMT_GREY,
		.btformat = BT848_COLOR_FMT_Y8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "8 bpp, dithered color",
		.palette  = VIDEO_PALETTE_HI240,
		.fourcc   = V4L2_PIX_FMT_HI240,
		.btformat = BT848_COLOR_FMT_RGB8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED | FORMAT_FLAGS_DITHER,
	},{
		.name     = "15 bpp RGB, le",
		.palette  = VIDEO_PALETTE_RGB555,
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.btformat = BT848_COLOR_FMT_RGB15,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "15 bpp RGB, be",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.btformat = BT848_COLOR_FMT_RGB15,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, le",
		.palette  = VIDEO_PALETTE_RGB565,
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.btformat = BT848_COLOR_FMT_RGB16,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "16 bpp RGB, be",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.btformat = BT848_COLOR_FMT_RGB16,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "24 bpp RGB, le",
		.palette  = VIDEO_PALETTE_RGB24,
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.btformat = BT848_COLOR_FMT_RGB24,
		.depth    = 24,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, le",
		.palette  = VIDEO_PALETTE_RGB32,
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.btformat = BT848_COLOR_FMT_RGB32,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "32 bpp RGB, be",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.btformat = BT848_COLOR_FMT_RGB32,
		.btswap   = 0x0f, /* byte+word swap */
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, YUYV",
		.palette  = VIDEO_PALETTE_YUV422,
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.btformat = BT848_COLOR_FMT_YUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, YUYV",
		.palette  = VIDEO_PALETTE_YUYV,
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.btformat = BT848_COLOR_FMT_YUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, packed, UYVY",
		.palette  = VIDEO_PALETTE_UYVY,
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.btformat = BT848_COLOR_FMT_YUY2,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.name     = "4:2:2, planar, Y-Cb-Cr",
		.palette  = VIDEO_PALETTE_YUV422P,
		.fourcc   = V4L2_PIX_FMT_YUV422P,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 1,
		.vshift   = 0,
	},{
		.name     = "4:2:0, planar, Y-Cb-Cr",
		.palette  = VIDEO_PALETTE_YUV420P,
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 1,
		.vshift   = 1,
	},{
		.name     = "4:2:0, planar, Y-Cr-Cb",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		.hshift   = 1,
		.vshift   = 1,
	},{
		.name     = "4:1:1, planar, Y-Cb-Cr",
		.palette  = VIDEO_PALETTE_YUV411P,
		.fourcc   = V4L2_PIX_FMT_YUV411P,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 2,
		.vshift   = 0,
	},{
		.name     = "4:1:0, planar, Y-Cb-Cr",
		.palette  = VIDEO_PALETTE_YUV410P,
		.fourcc   = V4L2_PIX_FMT_YUV410,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 2,
		.vshift   = 2,
	},{
		.name     = "4:1:0, planar, Y-Cr-Cb",
		.palette  = -1,
		.fourcc   = V4L2_PIX_FMT_YVU410,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		.hshift   = 2,
		.vshift   = 2,
	},{
		.name     = "raw scanlines",
		.palette  = VIDEO_PALETTE_RAW,
		.fourcc   = -1,
		.btformat = BT848_COLOR_FMT_RAW,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_RAW,
	}
};
static const unsigned int BTTV_FORMATS = ARRAY_SIZE(bttv_formats);

/* ----------------------------------------------------------------------- */

#define V4L2_CID_PRIVATE_CHROMA_AGC  (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_PRIVATE_COMBFILTER  (V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PRIVATE_AUTOMUTE    (V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_PRIVATE_LUMAFILTER  (V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_PRIVATE_AGC_CRUSH   (V4L2_CID_PRIVATE_BASE + 4)
#define V4L2_CID_PRIVATE_VCR_HACK    (V4L2_CID_PRIVATE_BASE + 5)
#define V4L2_CID_PRIVATE_WHITECRUSH_UPPER   (V4L2_CID_PRIVATE_BASE + 6)
#define V4L2_CID_PRIVATE_WHITECRUSH_LOWER   (V4L2_CID_PRIVATE_BASE + 7)
#define V4L2_CID_PRIVATE_UV_RATIO    (V4L2_CID_PRIVATE_BASE + 8)
#define V4L2_CID_PRIVATE_FULL_LUMA_RANGE    (V4L2_CID_PRIVATE_BASE + 9)
#define V4L2_CID_PRIVATE_CORING      (V4L2_CID_PRIVATE_BASE + 10)
#define V4L2_CID_PRIVATE_LASTP1      (V4L2_CID_PRIVATE_BASE + 11)

static const struct v4l2_queryctrl no_ctl = {
	.name  = "42",
	.flags = V4L2_CTRL_FLAG_DISABLED,
};
static const struct v4l2_queryctrl bttv_ctls[] = {
	/* --- video --- */
	{
		.id            = V4L2_CID_BRIGHTNESS,
		.name          = "Brightness",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 256,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_CONTRAST,
		.name          = "Contrast",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 128,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_SATURATION,
		.name          = "Saturation",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 128,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_HUE,
		.name          = "Hue",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 256,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
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
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 65535,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_BALANCE,
		.name          = "Balance",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_BASS,
		.name          = "Bass",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_TREBLE,
		.name          = "Treble",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
	/* --- private --- */
	{
		.id            = V4L2_CID_PRIVATE_CHROMA_AGC,
		.name          = "chroma agc",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_COMBFILTER,
		.name          = "combfilter",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_AUTOMUTE,
		.name          = "automute",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_LUMAFILTER,
		.name          = "luma decimation filter",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_AGC_CRUSH,
		.name          = "agc crush",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_VCR_HACK,
		.name          = "vcr hack",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_WHITECRUSH_UPPER,
		.name          = "whitecrush upper",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 0xCF,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_WHITECRUSH_LOWER,
		.name          = "whitecrush lower",
		.minimum       = 0,
		.maximum       = 255,
		.step          = 1,
		.default_value = 0x7F,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_UV_RATIO,
		.name          = "uv ratio",
		.minimum       = 0,
		.maximum       = 100,
		.step          = 1,
		.default_value = 50,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_PRIVATE_FULL_LUMA_RANGE,
		.name          = "full luma range",
		.minimum       = 0,
		.maximum       = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_PRIVATE_CORING,
		.name          = "coring",
		.minimum       = 0,
		.maximum       = 3,
		.step          = 1,
		.default_value = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	}



};
static const int BTTV_CTLS = ARRAY_SIZE(bttv_ctls);

/* ----------------------------------------------------------------------- */
/* resource management                                                     */

/*
   RESOURCE_    allocated by                freed by

   VIDEO_READ   bttv_read 1)                bttv_read 2)

   VIDEO_STREAM VIDIOC_STREAMON             VIDIOC_STREAMOFF
		 VIDIOC_QBUF 1)              bttv_release
		 VIDIOCMCAPTURE 1)

   OVERLAY	 VIDIOCCAPTURE on            VIDIOCCAPTURE off
		 VIDIOC_OVERLAY on           VIDIOC_OVERLAY off
		 3)                          bttv_release

   VBI		 VIDIOC_STREAMON             VIDIOC_STREAMOFF
		 VIDIOC_QBUF 1)              bttv_release
		 bttv_read, bttv_poll 1) 4)

   1) The resource must be allocated when we enter buffer prepare functions
      and remain allocated while buffers are in the DMA queue.
   2) This is a single frame read.
   3) VIDIOC_S_FBUF and VIDIOC_S_FMT (OVERLAY) still work when
      RESOURCE_OVERLAY is allocated.
   4) This is a continuous read, implies VIDIOC_STREAMON.

   Note this driver permits video input and standard changes regardless if
   resources are allocated.
*/

#define VBI_RESOURCES (RESOURCE_VBI)
#define VIDEO_RESOURCES (RESOURCE_VIDEO_READ | \
			 RESOURCE_VIDEO_STREAM | \
			 RESOURCE_OVERLAY)

static
int check_alloc_btres(struct bttv *btv, struct bttv_fh *fh, int bit)
{
	int xbits; /* mutual exclusive resources */

	if (fh->resources & bit)
		/* have it already allocated */
		return 1;

	xbits = bit;
	if (bit & (RESOURCE_VIDEO_READ | RESOURCE_VIDEO_STREAM))
		xbits |= RESOURCE_VIDEO_READ | RESOURCE_VIDEO_STREAM;

	/* is it free? */
	mutex_lock(&btv->lock);
	if (btv->resources & xbits) {
		/* no, someone else uses it */
		goto fail;
	}

	if ((bit & VIDEO_RESOURCES)
	    && 0 == (btv->resources & VIDEO_RESOURCES)) {
		/* Do crop - use current, don't - use default parameters. */
		__s32 top = btv->crop[!!fh->do_crop].rect.top;

		if (btv->vbi_end > top)
			goto fail;

		/* We cannot capture the same line as video and VBI data.
		   Claim scan lines crop[].rect.top to bottom. */
		btv->crop_start = top;
	} else if (bit & VBI_RESOURCES) {
		__s32 end = fh->vbi_fmt.end;

		if (end > btv->crop_start)
			goto fail;

		/* Claim scan lines above fh->vbi_fmt.end. */
		btv->vbi_end = end;
	}

	/* it's free, grab it */
	fh->resources  |= bit;
	btv->resources |= bit;
	mutex_unlock(&btv->lock);
	return 1;

 fail:
	mutex_unlock(&btv->lock);
	return 0;
}

static
int check_btres(struct bttv_fh *fh, int bit)
{
	return (fh->resources & bit);
}

static
int locked_btres(struct bttv *btv, int bit)
{
	return (btv->resources & bit);
}

/* Call with btv->lock down. */
static void
disclaim_vbi_lines(struct bttv *btv)
{
	btv->vbi_end = 0;
}

/* Call with btv->lock down. */
static void
disclaim_video_lines(struct bttv *btv)
{
	const struct bttv_tvnorm *tvnorm;
	u8 crop;

	tvnorm = &bttv_tvnorms[btv->tvnorm];
	btv->crop_start = tvnorm->cropcap.bounds.top
		+ tvnorm->cropcap.bounds.height;

	/* VBI capturing ends at VDELAY, start of video capturing, no
	   matter how many lines the VBI RISC program expects. When video
	   capturing is off, it shall no longer "preempt" VBI capturing,
	   so we set VDELAY to maximum. */
	crop = btread(BT848_E_CROP) | 0xc0;
	btwrite(crop, BT848_E_CROP);
	btwrite(0xfe, BT848_E_VDELAY_LO);
	btwrite(crop, BT848_O_CROP);
	btwrite(0xfe, BT848_O_VDELAY_LO);
}

static
void free_btres(struct bttv *btv, struct bttv_fh *fh, int bits)
{
	if ((fh->resources & bits) != bits) {
		/* trying to free ressources not allocated by us ... */
		printk("bttv: BUG! (btres)\n");
	}
	mutex_lock(&btv->lock);
	fh->resources  &= ~bits;
	btv->resources &= ~bits;

	bits = btv->resources;

	if (0 == (bits & VIDEO_RESOURCES))
		disclaim_video_lines(btv);

	if (0 == (bits & VBI_RESOURCES))
		disclaim_vbi_lines(btv);

	mutex_unlock(&btv->lock);
}

/* ----------------------------------------------------------------------- */
/* If Bt848a or Bt849, use PLL for PAL/SECAM and crystal for NTSC          */

/* Frequency = (F_input / PLL_X) * PLL_I.PLL_F/PLL_C
   PLL_X = Reference pre-divider (0=1, 1=2)
   PLL_C = Post divider (0=6, 1=4)
   PLL_I = Integer input
   PLL_F = Fractional input

   F_input = 28.636363 MHz:
   PAL (CLKx2 = 35.46895 MHz): PLL_X = 1, PLL_I = 0x0E, PLL_F = 0xDCF9, PLL_C = 0
*/

static void set_pll_freq(struct bttv *btv, unsigned int fin, unsigned int fout)
{
	unsigned char fl, fh, fi;

	/* prevent overflows */
	fin/=4;
	fout/=4;

	fout*=12;
	fi=fout/fin;

	fout=(fout%fin)*256;
	fh=fout/fin;

	fout=(fout%fin)*256;
	fl=fout/fin;

	btwrite(fl, BT848_PLL_F_LO);
	btwrite(fh, BT848_PLL_F_HI);
	btwrite(fi|BT848_PLL_X, BT848_PLL_XCI);
}

static void set_pll(struct bttv *btv)
{
	int i;

	if (!btv->pll.pll_crystal)
		return;

	if (btv->pll.pll_ofreq == btv->pll.pll_current) {
		dprintk("bttv%d: PLL: no change required\n",btv->c.nr);
		return;
	}

	if (btv->pll.pll_ifreq == btv->pll.pll_ofreq) {
		/* no PLL needed */
		if (btv->pll.pll_current == 0)
			return;
		bttv_printk(KERN_INFO "bttv%d: PLL can sleep, using XTAL (%d).\n",
			btv->c.nr,btv->pll.pll_ifreq);
		btwrite(0x00,BT848_TGCTRL);
		btwrite(0x00,BT848_PLL_XCI);
		btv->pll.pll_current = 0;
		return;
	}

	bttv_printk(KERN_INFO "bttv%d: PLL: %d => %d ",btv->c.nr,
		btv->pll.pll_ifreq, btv->pll.pll_ofreq);
	set_pll_freq(btv, btv->pll.pll_ifreq, btv->pll.pll_ofreq);

	for (i=0; i<10; i++) {
		/*  Let other people run while the PLL stabilizes */
		bttv_printk(".");
		msleep(10);

		if (btread(BT848_DSTATUS) & BT848_DSTATUS_PLOCK) {
			btwrite(0,BT848_DSTATUS);
		} else {
			btwrite(0x08,BT848_TGCTRL);
			btv->pll.pll_current = btv->pll.pll_ofreq;
			bttv_printk(" ok\n");
			return;
		}
	}
	btv->pll.pll_current = -1;
	bttv_printk("failed\n");
	return;
}

/* used to switch between the bt848's analog/digital video capture modes */
static void bt848A_set_timing(struct bttv *btv)
{
	int i, len;
	int table_idx = bttv_tvnorms[btv->tvnorm].sram;
	int fsc       = bttv_tvnorms[btv->tvnorm].Fsc;

	if (UNSET == bttv_tvcards[btv->c.type].muxsel[btv->input]) {
		dprintk("bttv%d: load digital timing table (table_idx=%d)\n",
			btv->c.nr,table_idx);

		/* timing change...reset timing generator address */
		btwrite(0x00, BT848_TGCTRL);
		btwrite(0x02, BT848_TGCTRL);
		btwrite(0x00, BT848_TGCTRL);

		len=SRAM_Table[table_idx][0];
		for(i = 1; i <= len; i++)
			btwrite(SRAM_Table[table_idx][i],BT848_TGLB);
		btv->pll.pll_ofreq = 27000000;

		set_pll(btv);
		btwrite(0x11, BT848_TGCTRL);
		btwrite(0x41, BT848_DVSIF);
	} else {
		btv->pll.pll_ofreq = fsc;
		set_pll(btv);
		btwrite(0x0, BT848_DVSIF);
	}
}

/* ----------------------------------------------------------------------- */

static void bt848_bright(struct bttv *btv, int bright)
{
	int value;

	// printk("bttv: set bright: %d\n",bright); // DEBUG
	btv->bright = bright;

	/* We want -128 to 127 we get 0-65535 */
	value = (bright >> 8) - 128;
	btwrite(value & 0xff, BT848_BRIGHT);
}

static void bt848_hue(struct bttv *btv, int hue)
{
	int value;

	btv->hue = hue;

	/* -128 to 127 */
	value = (hue >> 8) - 128;
	btwrite(value & 0xff, BT848_HUE);
}

static void bt848_contrast(struct bttv *btv, int cont)
{
	int value,hibit;

	btv->contrast = cont;

	/* 0-511 */
	value = (cont  >> 7);
	hibit = (value >> 6) & 4;
	btwrite(value & 0xff, BT848_CONTRAST_LO);
	btaor(hibit, ~4, BT848_E_CONTROL);
	btaor(hibit, ~4, BT848_O_CONTROL);
}

static void bt848_sat(struct bttv *btv, int color)
{
	int val_u,val_v,hibits;

	btv->saturation = color;

	/* 0-511 for the color */
	val_u   = ((color * btv->opt_uv_ratio) / 50) >> 7;
	val_v   = (((color * (100 - btv->opt_uv_ratio) / 50) >>7)*180L)/254;
	hibits  = (val_u >> 7) & 2;
	hibits |= (val_v >> 8) & 1;
	btwrite(val_u & 0xff, BT848_SAT_U_LO);
	btwrite(val_v & 0xff, BT848_SAT_V_LO);
	btaor(hibits, ~3, BT848_E_CONTROL);
	btaor(hibits, ~3, BT848_O_CONTROL);
}

/* ----------------------------------------------------------------------- */

static int
video_mux(struct bttv *btv, unsigned int input)
{
	int mux,mask2;

	if (input >= bttv_tvcards[btv->c.type].video_inputs)
		return -EINVAL;

	/* needed by RemoteVideo MX */
	mask2 = bttv_tvcards[btv->c.type].gpiomask2;
	if (mask2)
		gpio_inout(mask2,mask2);

	if (input == btv->svhs)  {
		btor(BT848_CONTROL_COMP, BT848_E_CONTROL);
		btor(BT848_CONTROL_COMP, BT848_O_CONTROL);
	} else {
		btand(~BT848_CONTROL_COMP, BT848_E_CONTROL);
		btand(~BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	mux = bttv_tvcards[btv->c.type].muxsel[input] & 3;
	btaor(mux<<5, ~(3<<5), BT848_IFORM);
	dprintk(KERN_DEBUG "bttv%d: video mux: input=%d mux=%d\n",
		btv->c.nr,input,mux);

	/* card specific hook */
	if(bttv_tvcards[btv->c.type].muxsel_hook)
		bttv_tvcards[btv->c.type].muxsel_hook (btv, input);
	return 0;
}

static char *audio_modes[] = {
	"audio: tuner", "audio: radio", "audio: extern",
	"audio: intern", "audio: mute"
};

static int
audio_mux(struct bttv *btv, int input, int mute)
{
	int gpio_val, signal;
	struct v4l2_control ctrl;
	struct i2c_client *c;

	gpio_inout(bttv_tvcards[btv->c.type].gpiomask,
		   bttv_tvcards[btv->c.type].gpiomask);
	signal = btread(BT848_DSTATUS) & BT848_DSTATUS_HLOC;

	btv->mute = mute;
	btv->audio = input;

	/* automute */
	mute = mute || (btv->opt_automute && !signal && !btv->radio_user);

	if (mute)
		gpio_val = bttv_tvcards[btv->c.type].gpiomute;
	else
		gpio_val = bttv_tvcards[btv->c.type].gpiomux[input];

	gpio_bits(bttv_tvcards[btv->c.type].gpiomask, gpio_val);
	if (bttv_gpio)
		bttv_gpio_tracking(btv, audio_modes[mute ? 4 : input]);
	if (in_interrupt())
		return 0;

	ctrl.id = V4L2_CID_AUDIO_MUTE;
	ctrl.value = btv->mute;
	bttv_call_i2c_clients(btv, VIDIOC_S_CTRL, &ctrl);
	c = btv->i2c_msp34xx_client;
	if (c) {
		struct v4l2_routing route;

		/* Note: the inputs tuner/radio/extern/intern are translated
		   to msp routings. This assumes common behavior for all msp3400
		   based TV cards. When this assumption fails, then the
		   specific MSP routing must be added to the card table.
		   For now this is sufficient. */
		switch (input) {
		case TVAUDIO_INPUT_RADIO:
			route.input = MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_EXTERN:
			route.input = MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_INTERN:
			/* Yes, this is the same input as for RADIO. I doubt
			   if this is ever used. The only board with an INTERN
			   input is the BTTV_BOARD_AVERMEDIA98. I wonder how
			   that was tested. My guess is that the whole INTERN
			   input does not work. */
			route.input = MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_TUNER:
		default:
			route.input = MSP_INPUT_DEFAULT;
			break;
		}
		route.output = MSP_OUTPUT_DEFAULT;
		c->driver->command(c, VIDIOC_INT_S_AUDIO_ROUTING, &route);
	}
	c = btv->i2c_tvaudio_client;
	if (c) {
		struct v4l2_routing route;

		route.input = input;
		route.output = 0;
		c->driver->command(c, VIDIOC_INT_S_AUDIO_ROUTING, &route);
	}
	return 0;
}

static inline int
audio_mute(struct bttv *btv, int mute)
{
	return audio_mux(btv, btv->audio, mute);
}

static inline int
audio_input(struct bttv *btv, int input)
{
	return audio_mux(btv, input, btv->mute);
}

static void
i2c_vidiocschan(struct bttv *btv)
{
	v4l2_std_id std = bttv_tvnorms[btv->tvnorm].v4l2_id;

	bttv_call_i2c_clients(btv, VIDIOC_S_STD, &std);
	if (btv->c.type == BTTV_BOARD_VOODOOTV_FM)
		bttv_tda9880_setnorm(btv,btv->tvnorm);
}

static void
bttv_crop_calc_limits(struct bttv_crop *c)
{
	/* Scale factor min. 1:1, max. 16:1. Min. image size
	   48 x 32. Scaled width must be a multiple of 4. */

	if (1) {
		/* For bug compatibility with VIDIOCGCAP and image
		   size checks in earlier driver versions. */
		c->min_scaled_width = 48;
		c->min_scaled_height = 32;
	} else {
		c->min_scaled_width =
			(max(48, c->rect.width >> 4) + 3) & ~3;
		c->min_scaled_height =
			max(32, c->rect.height >> 4);
	}

	c->max_scaled_width  = c->rect.width & ~3;
	c->max_scaled_height = c->rect.height;
}

static void
bttv_crop_reset(struct bttv_crop *c, int norm)
{
	c->rect = bttv_tvnorms[norm].cropcap.defrect;
	bttv_crop_calc_limits(c);
}

/* Call with btv->lock down. */
static int
set_tvnorm(struct bttv *btv, unsigned int norm)
{
	const struct bttv_tvnorm *tvnorm;

	if (norm < 0 || norm >= BTTV_TVNORMS)
		return -EINVAL;

	tvnorm = &bttv_tvnorms[norm];

	if (btv->tvnorm < 0 ||
	    btv->tvnorm >= BTTV_TVNORMS ||
	    0 != memcmp(&bttv_tvnorms[btv->tvnorm].cropcap,
			&tvnorm->cropcap,
			sizeof (tvnorm->cropcap))) {
		bttv_crop_reset(&btv->crop[0], norm);
		btv->crop[1] = btv->crop[0]; /* current = default */

		if (0 == (btv->resources & VIDEO_RESOURCES)) {
			btv->crop_start = tvnorm->cropcap.bounds.top
				+ tvnorm->cropcap.bounds.height;
		}
	}

	btv->tvnorm = norm;

	btwrite(tvnorm->adelay, BT848_ADELAY);
	btwrite(tvnorm->bdelay, BT848_BDELAY);
	btaor(tvnorm->iform,~(BT848_IFORM_NORM|BT848_IFORM_XTBOTH),
	      BT848_IFORM);
	btwrite(tvnorm->vbipack, BT848_VBI_PACK_SIZE);
	btwrite(1, BT848_VBI_PACK_DEL);
	bt848A_set_timing(btv);

	switch (btv->c.type) {
	case BTTV_BOARD_VOODOOTV_FM:
		bttv_tda9880_setnorm(btv,norm);
		break;
	}
	return 0;
}

/* Call with btv->lock down. */
static void
set_input(struct bttv *btv, unsigned int input)
{
	unsigned long flags;

	btv->input = input;
	if (irq_iswitch) {
		spin_lock_irqsave(&btv->s_lock,flags);
		if (btv->curr.frame_irq) {
			/* active capture -> delayed input switch */
			btv->new_input = input;
		} else {
			video_mux(btv,input);
		}
		spin_unlock_irqrestore(&btv->s_lock,flags);
	} else {
		video_mux(btv,input);
	}
	audio_input(btv,(input == bttv_tvcards[btv->c.type].tuner ?
		       TVAUDIO_INPUT_TUNER : TVAUDIO_INPUT_EXTERN));
	set_tvnorm(btv,btv->tvnorm);
	i2c_vidiocschan(btv);
}

static void init_irqreg(struct bttv *btv)
{
	/* clear status */
	btwrite(0xfffffUL, BT848_INT_STAT);

	if (bttv_tvcards[btv->c.type].no_video) {
		/* i2c only */
		btwrite(BT848_INT_I2CDONE,
			BT848_INT_MASK);
	} else {
		/* full video */
		btwrite((btv->triton1)  |
			(btv->gpioirq ? BT848_INT_GPINT : 0) |
			BT848_INT_SCERR |
			(fdsr ? BT848_INT_FDSR : 0) |
			BT848_INT_RISCI|BT848_INT_OCERR|BT848_INT_VPRES|
			BT848_INT_FMTCHG|BT848_INT_HLOCK|
			BT848_INT_I2CDONE,
			BT848_INT_MASK);
	}
}

static void init_bt848(struct bttv *btv)
{
	int val;

	if (bttv_tvcards[btv->c.type].no_video) {
		/* very basic init only */
		init_irqreg(btv);
		return;
	}

	btwrite(0x00, BT848_CAP_CTL);
	btwrite(BT848_COLOR_CTL_GAMMA, BT848_COLOR_CTL);
	btwrite(BT848_IFORM_XTAUTO | BT848_IFORM_AUTO, BT848_IFORM);

	/* set planar and packed mode trigger points and         */
	/* set rising edge of inverted GPINTR pin as irq trigger */
	btwrite(BT848_GPIO_DMA_CTL_PKTP_32|
		BT848_GPIO_DMA_CTL_PLTP1_16|
		BT848_GPIO_DMA_CTL_PLTP23_16|
		BT848_GPIO_DMA_CTL_GPINTC|
		BT848_GPIO_DMA_CTL_GPINTI,
		BT848_GPIO_DMA_CTL);

	val = btv->opt_chroma_agc ? BT848_SCLOOP_CAGC : 0;
	btwrite(val, BT848_E_SCLOOP);
	btwrite(val, BT848_O_SCLOOP);

	btwrite(0x20, BT848_E_VSCALE_HI);
	btwrite(0x20, BT848_O_VSCALE_HI);
	btwrite(BT848_ADC_RESERVED | (btv->opt_adc_crush ? BT848_ADC_CRUSH : 0),
		BT848_ADC);

	btwrite(whitecrush_upper, BT848_WC_UP);
	btwrite(whitecrush_lower, BT848_WC_DOWN);

	if (btv->opt_lumafilter) {
		btwrite(0, BT848_E_CONTROL);
		btwrite(0, BT848_O_CONTROL);
	} else {
		btwrite(BT848_CONTROL_LDEC, BT848_E_CONTROL);
		btwrite(BT848_CONTROL_LDEC, BT848_O_CONTROL);
	}

	bt848_bright(btv,   btv->bright);
	bt848_hue(btv,      btv->hue);
	bt848_contrast(btv, btv->contrast);
	bt848_sat(btv,      btv->saturation);

	/* interrupt */
	init_irqreg(btv);
}

static void bttv_reinit_bt848(struct bttv *btv)
{
	unsigned long flags;

	if (bttv_verbose)
		printk(KERN_INFO "bttv%d: reset, reinitialize\n",btv->c.nr);
	spin_lock_irqsave(&btv->s_lock,flags);
	btv->errors=0;
	bttv_set_dma(btv,0);
	spin_unlock_irqrestore(&btv->s_lock,flags);

	init_bt848(btv);
	btv->pll.pll_current = -1;
	set_input(btv,btv->input);
}

static int get_control(struct bttv *btv, struct v4l2_control *c)
{
	struct video_audio va;
	int i;

	for (i = 0; i < BTTV_CTLS; i++)
		if (bttv_ctls[i].id == c->id)
			break;
	if (i == BTTV_CTLS)
		return -EINVAL;
	if (btv->audio_hook && i >= 4 && i <= 8) {
		memset(&va,0,sizeof(va));
		btv->audio_hook(btv,&va,0);
		switch (c->id) {
		case V4L2_CID_AUDIO_MUTE:
			c->value = (VIDEO_AUDIO_MUTE & va.flags) ? 1 : 0;
			break;
		case V4L2_CID_AUDIO_VOLUME:
			c->value = va.volume;
			break;
		case V4L2_CID_AUDIO_BALANCE:
			c->value = va.balance;
			break;
		case V4L2_CID_AUDIO_BASS:
			c->value = va.bass;
			break;
		case V4L2_CID_AUDIO_TREBLE:
			c->value = va.treble;
			break;
		}
		return 0;
	}
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		c->value = btv->bright;
		break;
	case V4L2_CID_HUE:
		c->value = btv->hue;
		break;
	case V4L2_CID_CONTRAST:
		c->value = btv->contrast;
		break;
	case V4L2_CID_SATURATION:
		c->value = btv->saturation;
		break;

	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
		bttv_call_i2c_clients(btv,VIDIOC_G_CTRL,c);
		break;

	case V4L2_CID_PRIVATE_CHROMA_AGC:
		c->value = btv->opt_chroma_agc;
		break;
	case V4L2_CID_PRIVATE_COMBFILTER:
		c->value = btv->opt_combfilter;
		break;
	case V4L2_CID_PRIVATE_LUMAFILTER:
		c->value = btv->opt_lumafilter;
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		c->value = btv->opt_automute;
		break;
	case V4L2_CID_PRIVATE_AGC_CRUSH:
		c->value = btv->opt_adc_crush;
		break;
	case V4L2_CID_PRIVATE_VCR_HACK:
		c->value = btv->opt_vcr_hack;
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_UPPER:
		c->value = btv->opt_whitecrush_upper;
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_LOWER:
		c->value = btv->opt_whitecrush_lower;
		break;
	case V4L2_CID_PRIVATE_UV_RATIO:
		c->value = btv->opt_uv_ratio;
		break;
	case V4L2_CID_PRIVATE_FULL_LUMA_RANGE:
		c->value = btv->opt_full_luma_range;
		break;
	case V4L2_CID_PRIVATE_CORING:
		c->value = btv->opt_coring;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int set_control(struct bttv *btv, struct v4l2_control *c)
{
	struct video_audio va;
	int i,val;

	for (i = 0; i < BTTV_CTLS; i++)
		if (bttv_ctls[i].id == c->id)
			break;
	if (i == BTTV_CTLS)
		return -EINVAL;
	if (btv->audio_hook && i >= 4 && i <= 8) {
		memset(&va,0,sizeof(va));
		btv->audio_hook(btv,&va,0);
		switch (c->id) {
		case V4L2_CID_AUDIO_MUTE:
			if (c->value) {
				va.flags |= VIDEO_AUDIO_MUTE;
				audio_mute(btv, 1);
			} else {
				va.flags &= ~VIDEO_AUDIO_MUTE;
				audio_mute(btv, 0);
			}
			break;

		case V4L2_CID_AUDIO_VOLUME:
			va.volume = c->value;
			break;
		case V4L2_CID_AUDIO_BALANCE:
			va.balance = c->value;
			break;
		case V4L2_CID_AUDIO_BASS:
			va.bass = c->value;
			break;
		case V4L2_CID_AUDIO_TREBLE:
			va.treble = c->value;
			break;
		}
		btv->audio_hook(btv,&va,1);
		return 0;
	}
	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		bt848_bright(btv,c->value);
		break;
	case V4L2_CID_HUE:
		bt848_hue(btv,c->value);
		break;
	case V4L2_CID_CONTRAST:
		bt848_contrast(btv,c->value);
		break;
	case V4L2_CID_SATURATION:
		bt848_sat(btv,c->value);
		break;
	case V4L2_CID_AUDIO_MUTE:
		audio_mute(btv, c->value);
		/* fall through */
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
		bttv_call_i2c_clients(btv,VIDIOC_S_CTRL,c);
		break;

	case V4L2_CID_PRIVATE_CHROMA_AGC:
		btv->opt_chroma_agc = c->value;
		val = btv->opt_chroma_agc ? BT848_SCLOOP_CAGC : 0;
		btwrite(val, BT848_E_SCLOOP);
		btwrite(val, BT848_O_SCLOOP);
		break;
	case V4L2_CID_PRIVATE_COMBFILTER:
		btv->opt_combfilter = c->value;
		break;
	case V4L2_CID_PRIVATE_LUMAFILTER:
		btv->opt_lumafilter = c->value;
		if (btv->opt_lumafilter) {
			btand(~BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btand(~BT848_CONTROL_LDEC, BT848_O_CONTROL);
		} else {
			btor(BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btor(BT848_CONTROL_LDEC, BT848_O_CONTROL);
		}
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		btv->opt_automute = c->value;
		break;
	case V4L2_CID_PRIVATE_AGC_CRUSH:
		btv->opt_adc_crush = c->value;
		btwrite(BT848_ADC_RESERVED | (btv->opt_adc_crush ? BT848_ADC_CRUSH : 0),
			BT848_ADC);
		break;
	case V4L2_CID_PRIVATE_VCR_HACK:
		btv->opt_vcr_hack = c->value;
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_UPPER:
		btv->opt_whitecrush_upper = c->value;
		btwrite(c->value, BT848_WC_UP);
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_LOWER:
		btv->opt_whitecrush_lower = c->value;
		btwrite(c->value, BT848_WC_DOWN);
		break;
	case V4L2_CID_PRIVATE_UV_RATIO:
		btv->opt_uv_ratio = c->value;
		bt848_sat(btv, btv->saturation);
		break;
	case V4L2_CID_PRIVATE_FULL_LUMA_RANGE:
		btv->opt_full_luma_range = c->value;
		btaor((c->value<<7), ~BT848_OFORM_RANGE, BT848_OFORM);
		break;
	case V4L2_CID_PRIVATE_CORING:
		btv->opt_coring = c->value;
		btaor((c->value<<5), ~BT848_OFORM_CORE32, BT848_OFORM);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

void bttv_gpio_tracking(struct bttv *btv, char *comment)
{
	unsigned int outbits, data;
	outbits = btread(BT848_GPIO_OUT_EN);
	data    = btread(BT848_GPIO_DATA);
	printk(KERN_DEBUG "bttv%d: gpio: en=%08x, out=%08x in=%08x [%s]\n",
	       btv->c.nr,outbits,data & outbits, data & ~outbits, comment);
}

static void bttv_field_count(struct bttv *btv)
{
	int need_count = 0;

	if (btv->users)
		need_count++;

	if (need_count) {
		/* start field counter */
		btor(BT848_INT_VSYNC,BT848_INT_MASK);
	} else {
		/* stop field counter */
		btand(~BT848_INT_VSYNC,BT848_INT_MASK);
		btv->field_count = 0;
	}
}

static const struct bttv_format*
format_by_palette(int palette)
{
	unsigned int i;

	for (i = 0; i < BTTV_FORMATS; i++) {
		if (-1 == bttv_formats[i].palette)
			continue;
		if (bttv_formats[i].palette == palette)
			return bttv_formats+i;
	}
	return NULL;
}

static const struct bttv_format*
format_by_fourcc(int fourcc)
{
	unsigned int i;

	for (i = 0; i < BTTV_FORMATS; i++) {
		if (-1 == bttv_formats[i].fourcc)
			continue;
		if (bttv_formats[i].fourcc == fourcc)
			return bttv_formats+i;
	}
	return NULL;
}

/* ----------------------------------------------------------------------- */
/* misc helpers                                                            */

static int
bttv_switch_overlay(struct bttv *btv, struct bttv_fh *fh,
		    struct bttv_buffer *new)
{
	struct bttv_buffer *old;
	unsigned long flags;
	int retval = 0;

	dprintk("switch_overlay: enter [new=%p]\n",new);
	if (new)
		new->vb.state = STATE_DONE;
	spin_lock_irqsave(&btv->s_lock,flags);
	old = btv->screen;
	btv->screen = new;
	btv->loop_irq |= 1;
	bttv_set_dma(btv, 0x03);
	spin_unlock_irqrestore(&btv->s_lock,flags);
	if (NULL != old) {
		dprintk("switch_overlay: old=%p state is %d\n",old,old->vb.state);
		bttv_dma_free(&fh->cap,btv, old);
		kfree(old);
	}
	if (NULL == new)
		free_btres(btv,fh,RESOURCE_OVERLAY);
	dprintk("switch_overlay: done\n");
	return retval;
}

/* ----------------------------------------------------------------------- */
/* video4linux (1) interface                                               */

static int bttv_prepare_buffer(struct videobuf_queue *q,struct bttv *btv,
			       struct bttv_buffer *buf,
			       const struct bttv_format *fmt,
			       unsigned int width, unsigned int height,
			       enum v4l2_field field)
{
	struct bttv_fh *fh = q->priv_data;
	int redo_dma_risc = 0;
	struct bttv_crop c;
	int norm;
	int rc;

	/* check settings */
	if (NULL == fmt)
		return -EINVAL;
	if (fmt->btformat == BT848_COLOR_FMT_RAW) {
		width  = RAW_BPL;
		height = RAW_LINES*2;
		if (width*height > buf->vb.bsize)
			return -EINVAL;
		buf->vb.size = buf->vb.bsize;

		/* Make sure tvnorm and vbi_end remain consistent
		   until we're done. */
		mutex_lock(&btv->lock);

		norm = btv->tvnorm;

		/* In this mode capturing always starts at defrect.top
		   (default VDELAY), ignoring cropping parameters. */
		if (btv->vbi_end > bttv_tvnorms[norm].cropcap.defrect.top) {
			mutex_unlock(&btv->lock);
			return -EINVAL;
		}

		mutex_unlock(&btv->lock);

		c.rect = bttv_tvnorms[norm].cropcap.defrect;
	} else {
		mutex_lock(&btv->lock);

		norm = btv->tvnorm;
		c = btv->crop[!!fh->do_crop];

		mutex_unlock(&btv->lock);

		if (width < c.min_scaled_width ||
		    width > c.max_scaled_width ||
		    height < c.min_scaled_height)
			return -EINVAL;

		switch (field) {
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
		case V4L2_FIELD_ALTERNATE:
			/* btv->crop counts frame lines. Max. scale
			   factor is 16:1 for frames, 8:1 for fields. */
			if (height * 2 > c.max_scaled_height)
				return -EINVAL;
			break;

		default:
			if (height > c.max_scaled_height)
				return -EINVAL;
			break;
		}

		buf->vb.size = (width * height * fmt->depth) >> 3;
		if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
			return -EINVAL;
	}

	/* alloc + fill struct bttv_buffer (if changed) */
	if (buf->vb.width != width || buf->vb.height != height ||
	    buf->vb.field != field ||
	    buf->tvnorm != norm || buf->fmt != fmt ||
	    buf->crop.top != c.rect.top ||
	    buf->crop.left != c.rect.left ||
	    buf->crop.width != c.rect.width ||
	    buf->crop.height != c.rect.height) {
		buf->vb.width  = width;
		buf->vb.height = height;
		buf->vb.field  = field;
		buf->tvnorm    = norm;
		buf->fmt       = fmt;
		buf->crop      = c.rect;
		redo_dma_risc = 1;
	}

	/* alloc risc memory */
	if (STATE_NEEDS_INIT == buf->vb.state) {
		redo_dma_risc = 1;
		if (0 != (rc = videobuf_iolock(q,&buf->vb,&btv->fbuf)))
			goto fail;
	}

	if (redo_dma_risc)
		if (0 != (rc = bttv_buffer_risc(btv,buf)))
			goto fail;

	buf->vb.state = STATE_PREPARED;
	return 0;

 fail:
	bttv_dma_free(q,btv,buf);
	return rc;
}

static int
buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct bttv_fh *fh = q->priv_data;

	*size = fh->fmt->depth*fh->width*fh->height >> 3;
	if (0 == *count)
		*count = gbuffers;
	while (*size * *count > gbuffers * gbufsize)
		(*count)--;
	return 0;
}

static int
buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);
	struct bttv_fh *fh = q->priv_data;

	return bttv_prepare_buffer(q,fh->btv, buf, fh->fmt,
				   fh->width, fh->height, field);
}

static void
buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);
	struct bttv_fh *fh = q->priv_data;
	struct bttv    *btv = fh->btv;

	buf->vb.state = STATE_QUEUED;
	list_add_tail(&buf->vb.queue,&btv->capture);
	if (!btv->curr.frame_irq) {
		btv->loop_irq |= 1;
		bttv_set_dma(btv, 0x03);
	}
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);
	struct bttv_fh *fh = q->priv_data;

	bttv_dma_free(q,fh->btv,buf);
}

static struct videobuf_queue_ops bttv_video_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

static int bttv_common_ioctls(struct bttv *btv, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case BTTV_VERSION:
		return BTTV_VERSION_CODE;

	/* ***  v4l1  *** ************************************************ */
	case VIDIOCGFREQ:
	{
		unsigned long *freq = arg;
		*freq = btv->freq;
		return 0;
	}
	case VIDIOCSFREQ:
	{
		struct v4l2_frequency freq;

		memset(&freq, 0, sizeof(freq));
		freq.frequency = *(unsigned long *)arg;
		mutex_lock(&btv->lock);
		freq.type = btv->radio_user ? V4L2_TUNER_RADIO : V4L2_TUNER_ANALOG_TV;
		btv->freq = *(unsigned long *)arg;
		bttv_call_i2c_clients(btv,VIDIOC_S_FREQUENCY,&freq);
		if (btv->has_matchbox && btv->radio_user)
			tea5757_set_freq(btv,*(unsigned long *)arg);
		mutex_unlock(&btv->lock);
		return 0;
	}

	case VIDIOCGTUNER:
	{
		struct video_tuner *v = arg;

		if (UNSET == bttv_tvcards[btv->c.type].tuner)
			return -EINVAL;
		if (v->tuner) /* Only tuner 0 */
			return -EINVAL;
		strcpy(v->name, "Television");
		v->rangelow  = 0;
		v->rangehigh = 0x7FFFFFFF;
		v->flags     = VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
		v->mode      = btv->tvnorm;
		v->signal    = (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC) ? 0xFFFF : 0;
		bttv_call_i2c_clients(btv,cmd,v);
		return 0;
	}
	case VIDIOCSTUNER:
	{
		struct video_tuner *v = arg;

		if (v->tuner) /* Only tuner 0 */
			return -EINVAL;
		if (v->mode >= BTTV_TVNORMS)
			return -EINVAL;

		mutex_lock(&btv->lock);
		set_tvnorm(btv,v->mode);
		bttv_call_i2c_clients(btv,cmd,v);
		mutex_unlock(&btv->lock);
		return 0;
	}

	case VIDIOCGCHAN:
	{
		struct video_channel *v = arg;
		unsigned int channel = v->channel;

		if (channel >= bttv_tvcards[btv->c.type].video_inputs)
			return -EINVAL;
		v->tuners=0;
		v->flags = VIDEO_VC_AUDIO;
		v->type = VIDEO_TYPE_CAMERA;
		v->norm = btv->tvnorm;
		if (channel == bttv_tvcards[btv->c.type].tuner)  {
			strcpy(v->name,"Television");
			v->flags|=VIDEO_VC_TUNER;
			v->type=VIDEO_TYPE_TV;
			v->tuners=1;
		} else if (channel == btv->svhs) {
			strcpy(v->name,"S-Video");
		} else {
			sprintf(v->name,"Composite%d",channel);
		}
		return 0;
	}
	case VIDIOCSCHAN:
	{
		struct video_channel *v = arg;
		unsigned int channel = v->channel;

		if (channel >= bttv_tvcards[btv->c.type].video_inputs)
			return -EINVAL;
		if (v->norm >= BTTV_TVNORMS)
			return -EINVAL;

		mutex_lock(&btv->lock);
		if (channel == btv->input &&
		    v->norm == btv->tvnorm) {
			/* nothing to do */
			mutex_unlock(&btv->lock);
			return 0;
		}

		btv->tvnorm = v->norm;
		set_input(btv,v->channel);
		mutex_unlock(&btv->lock);
		return 0;
	}

	case VIDIOCGAUDIO:
	{
		struct video_audio *v = arg;

		memset(v,0,sizeof(*v));
		strcpy(v->name,"Television");
		v->flags |= VIDEO_AUDIO_MUTABLE;
		v->mode  = VIDEO_SOUND_MONO;

		mutex_lock(&btv->lock);
		bttv_call_i2c_clients(btv,cmd,v);

		/* card specific hooks */
		if (btv->audio_hook)
			btv->audio_hook(btv,v,0);

		mutex_unlock(&btv->lock);
		return 0;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *v = arg;
		unsigned int audio = v->audio;

		if (audio >= bttv_tvcards[btv->c.type].audio_inputs)
			return -EINVAL;

		mutex_lock(&btv->lock);
		audio_mute(btv, (v->flags&VIDEO_AUDIO_MUTE) ? 1 : 0);
		bttv_call_i2c_clients(btv,cmd,v);

		/* card specific hooks */
		if (btv->audio_hook)
			btv->audio_hook(btv,v,1);

		mutex_unlock(&btv->lock);
		return 0;
	}

	/* ***  v4l2  *** ************************************************ */
	case VIDIOC_ENUMSTD:
	{
		struct v4l2_standard *e = arg;
		unsigned int index = e->index;

		if (index >= BTTV_TVNORMS)
			return -EINVAL;
		v4l2_video_std_construct(e, bttv_tvnorms[e->index].v4l2_id,
					 bttv_tvnorms[e->index].name);
		e->index = index;
		return 0;
	}
	case VIDIOC_G_STD:
	{
		v4l2_std_id *id = arg;
		*id = bttv_tvnorms[btv->tvnorm].v4l2_id;
		return 0;
	}
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;
		unsigned int i;

		for (i = 0; i < BTTV_TVNORMS; i++)
			if (*id & bttv_tvnorms[i].v4l2_id)
				break;
		if (i == BTTV_TVNORMS)
			return -EINVAL;

		mutex_lock(&btv->lock);
		set_tvnorm(btv,i);
		i2c_vidiocschan(btv);
		mutex_unlock(&btv->lock);
		return 0;
	}
	case VIDIOC_QUERYSTD:
	{
		v4l2_std_id *id = arg;

		if (btread(BT848_DSTATUS) & BT848_DSTATUS_NUML)
			*id = V4L2_STD_625_50;
		else
			*id = V4L2_STD_525_60;
		return 0;
	}

	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;
		unsigned int n;

		n = i->index;
		if (n >= bttv_tvcards[btv->c.type].video_inputs)
			return -EINVAL;
		memset(i,0,sizeof(*i));
		i->index    = n;
		i->type     = V4L2_INPUT_TYPE_CAMERA;
		i->audioset = 1;
		if (i->index == bttv_tvcards[btv->c.type].tuner) {
			sprintf(i->name, "Television");
			i->type  = V4L2_INPUT_TYPE_TUNER;
			i->tuner = 0;
		} else if (i->index == btv->svhs) {
			sprintf(i->name, "S-Video");
		} else {
			sprintf(i->name,"Composite%d",i->index);
		}
		if (i->index == btv->input) {
			__u32 dstatus = btread(BT848_DSTATUS);
			if (0 == (dstatus & BT848_DSTATUS_PRES))
				i->status |= V4L2_IN_ST_NO_SIGNAL;
			if (0 == (dstatus & BT848_DSTATUS_HLOC))
				i->status |= V4L2_IN_ST_NO_H_LOCK;
		}
		for (n = 0; n < BTTV_TVNORMS; n++)
			i->std |= bttv_tvnorms[n].v4l2_id;
		return 0;
	}
	case VIDIOC_G_INPUT:
	{
		int *i = arg;
		*i = btv->input;
		return 0;
	}
	case VIDIOC_S_INPUT:
	{
		unsigned int *i = arg;

		if (*i > bttv_tvcards[btv->c.type].video_inputs)
			return -EINVAL;
		mutex_lock(&btv->lock);
		set_input(btv,*i);
		mutex_unlock(&btv->lock);
		return 0;
	}

	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (UNSET == bttv_tvcards[btv->c.type].tuner)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;
		mutex_lock(&btv->lock);
		memset(t,0,sizeof(*t));
		t->rxsubchans = V4L2_TUNER_SUB_MONO;
		bttv_call_i2c_clients(btv, VIDIOC_G_TUNER, t);
		strcpy(t->name, "Television");
		t->capability = V4L2_TUNER_CAP_NORM;
		t->type       = V4L2_TUNER_ANALOG_TV;
		if (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC)
			t->signal = 0xffff;

		if (btv->audio_hook) {
			/* Hmmm ... */
			struct video_audio va;
			memset(&va, 0, sizeof(struct video_audio));
			btv->audio_hook(btv,&va,0);
			t->audmode    = V4L2_TUNER_MODE_MONO;
			t->rxsubchans = V4L2_TUNER_SUB_MONO;
			if(va.mode & VIDEO_SOUND_STEREO) {
				t->audmode    = V4L2_TUNER_MODE_STEREO;
				t->rxsubchans = V4L2_TUNER_SUB_STEREO;
			}
			if(va.mode & VIDEO_SOUND_LANG2) {
				t->audmode    = V4L2_TUNER_MODE_LANG1;
				t->rxsubchans = V4L2_TUNER_SUB_LANG1
					| V4L2_TUNER_SUB_LANG2;
			}
		}
		/* FIXME: fill capability+audmode */
		mutex_unlock(&btv->lock);
		return 0;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *t = arg;

		if (UNSET == bttv_tvcards[btv->c.type].tuner)
			return -EINVAL;
		if (0 != t->index)
			return -EINVAL;
		mutex_lock(&btv->lock);
		bttv_call_i2c_clients(btv, VIDIOC_S_TUNER, t);
		if (btv->audio_hook) {
			struct video_audio va;
			memset(&va, 0, sizeof(struct video_audio));
			if (t->audmode == V4L2_TUNER_MODE_MONO)
				va.mode = VIDEO_SOUND_MONO;
			else if (t->audmode == V4L2_TUNER_MODE_STEREO ||
				 t->audmode == V4L2_TUNER_MODE_LANG1_LANG2)
				va.mode = VIDEO_SOUND_STEREO;
			else if (t->audmode == V4L2_TUNER_MODE_LANG1)
				va.mode = VIDEO_SOUND_LANG1;
			else if (t->audmode == V4L2_TUNER_MODE_LANG2)
				va.mode = VIDEO_SOUND_LANG2;
			btv->audio_hook(btv,&va,1);
		}
		mutex_unlock(&btv->lock);
		return 0;
	}

	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		memset(f,0,sizeof(*f));
		f->type = V4L2_TUNER_ANALOG_TV;
		f->frequency = btv->freq;
		return 0;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		if (unlikely(f->tuner != 0))
			return -EINVAL;
		if (unlikely (f->type != V4L2_TUNER_ANALOG_TV))
			return -EINVAL;
		mutex_lock(&btv->lock);
		btv->freq = f->frequency;
		bttv_call_i2c_clients(btv,VIDIOC_S_FREQUENCY,f);
		if (btv->has_matchbox && btv->radio_user)
			tea5757_set_freq(btv,btv->freq);
		mutex_unlock(&btv->lock);
		return 0;
	}
	case VIDIOC_LOG_STATUS:
	{
		printk(KERN_INFO "bttv%d: =================  START STATUS CARD #%d  =================\n", btv->c.nr, btv->c.nr);
		bttv_call_i2c_clients(btv, VIDIOC_LOG_STATUS, NULL);
		printk(KERN_INFO "bttv%d: ==================  END STATUS CARD #%d  ==================\n", btv->c.nr, btv->c.nr);
		return 0;
	}

	default:
		return -ENOIOCTLCMD;

	}
	return 0;
}

/* Given cropping boundaries b and the scaled width and height of a
   single field or frame, which must not exceed hardware limits, this
   function adjusts the cropping parameters c. */
static void
bttv_crop_adjust	(struct bttv_crop *             c,
			 const struct v4l2_rect *	b,
			 __s32                          width,
			 __s32                          height,
			 enum v4l2_field                field)
{
	__s32 frame_height = height << !V4L2_FIELD_HAS_BOTH(field);
	__s32 max_left;
	__s32 max_top;

	if (width < c->min_scaled_width) {
		/* Max. hor. scale factor 16:1. */
		c->rect.width = width * 16;
	} else if (width > c->max_scaled_width) {
		/* Min. hor. scale factor 1:1. */
		c->rect.width = width;

		max_left = b->left + b->width - width;
		max_left = min(max_left, (__s32) MAX_HDELAY);
		if (c->rect.left > max_left)
			c->rect.left = max_left;
	}

	if (height < c->min_scaled_height) {
		/* Max. vert. scale factor 16:1, single fields 8:1. */
		c->rect.height = height * 16;
	} else if (frame_height > c->max_scaled_height) {
		/* Min. vert. scale factor 1:1.
		   Top and height count field lines times two. */
		c->rect.height = (frame_height + 1) & ~1;

		max_top = b->top + b->height - c->rect.height;
		if (c->rect.top > max_top)
			c->rect.top = max_top;
	}

	bttv_crop_calc_limits(c);
}

/* Returns an error if scaling to a frame or single field with the given
   width and height is not possible with the current cropping parameters
   and width aligned according to width_mask. If adjust_size is TRUE the
   function may adjust the width and/or height instead, rounding width
   to (width + width_bias) & width_mask. If adjust_crop is TRUE it may
   also adjust the current cropping parameters to get closer to the
   desired image size. */
static int
limit_scaled_size       (struct bttv_fh *               fh,
			 __s32 *                        width,
			 __s32 *                        height,
			 enum v4l2_field                field,
			 unsigned int			width_mask,
			 unsigned int			width_bias,
			 int                            adjust_size,
			 int                            adjust_crop)
{
	struct bttv *btv = fh->btv;
	const struct v4l2_rect *b;
	struct bttv_crop *c;
	__s32 min_width;
	__s32 min_height;
	__s32 max_width;
	__s32 max_height;
	int rc;

	BUG_ON((int) width_mask >= 0 ||
	       width_bias >= (unsigned int) -width_mask);

	/* Make sure tvnorm, vbi_end and the current cropping parameters
	   remain consistent until we're done. */
	mutex_lock(&btv->lock);

	b = &bttv_tvnorms[btv->tvnorm].cropcap.bounds;

	/* Do crop - use current, don't - use default parameters. */
	c = &btv->crop[!!fh->do_crop];

	if (fh->do_crop
	    && adjust_size
	    && adjust_crop
	    && !locked_btres(btv, VIDEO_RESOURCES)) {
		min_width = 48;
		min_height = 32;

		/* We cannot scale up. When the scaled image is larger
		   than crop.rect we adjust the crop.rect as required
		   by the V4L2 spec, hence cropcap.bounds are our limit. */
		max_width = min(b->width, (__s32) MAX_HACTIVE);
		max_height = b->height;

		/* We cannot capture the same line as video and VBI data.
		   Note btv->vbi_end is really a minimum, see
		   bttv_vbi_try_fmt(). */
		if (btv->vbi_end > b->top) {
			max_height -= btv->vbi_end - b->top;
			rc = -EBUSY;
			if (min_height > max_height)
				goto fail;
		}
	} else {
		rc = -EBUSY;
		if (btv->vbi_end > c->rect.top)
			goto fail;

		min_width  = c->min_scaled_width;
		min_height = c->min_scaled_height;
		max_width  = c->max_scaled_width;
		max_height = c->max_scaled_height;

		adjust_crop = 0;
	}

	min_width = (min_width - width_mask - 1) & width_mask;
	max_width = max_width & width_mask;

	/* Max. scale factor is 16:1 for frames, 8:1 for fields. */
	min_height = min_height;
	/* Min. scale factor is 1:1. */
	max_height >>= !V4L2_FIELD_HAS_BOTH(field);

	if (adjust_size) {
		*width = clamp(*width, min_width, max_width);
		*height = clamp(*height, min_height, max_height);

		/* Round after clamping to avoid overflow. */
		*width = (*width + width_bias) & width_mask;

		if (adjust_crop) {
			bttv_crop_adjust(c, b, *width, *height, field);

			if (btv->vbi_end > c->rect.top) {
				/* Move the crop window out of the way. */
				c->rect.top = btv->vbi_end;
			}
		}
	} else {
		rc = -EINVAL;
		if (*width  < min_width ||
		    *height < min_height ||
		    *width  > max_width ||
		    *height > max_height ||
		    0 != (*width & ~width_mask))
			goto fail;
	}

	rc = 0; /* success */

 fail:
	mutex_unlock(&btv->lock);

	return rc;
}

/* Returns an error if the given overlay window dimensions are not
   possible with the current cropping parameters. If adjust_size is
   TRUE the function may adjust the window width and/or height
   instead, however it always rounds the horizontal position and
   width as btcx_align() does. If adjust_crop is TRUE the function
   may also adjust the current cropping parameters to get closer
   to the desired window size. */
static int
verify_window		(struct bttv_fh *               fh,
			 struct v4l2_window *           win,
			 int                            adjust_size,
			 int                            adjust_crop)
{
	enum v4l2_field field;
	unsigned int width_mask;
	int rc;

	if (win->w.width  < 48 || win->w.height < 32)
		return -EINVAL;
	if (win->clipcount > 2048)
		return -EINVAL;

	field = win->field;

	if (V4L2_FIELD_ANY == field) {
		__s32 height2;

		height2 = fh->btv->crop[!!fh->do_crop].rect.height >> 1;
		field = (win->w.height > height2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_TOP;
	}
	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_INTERLACED:
		break;
	default:
		return -EINVAL;
	}

	/* 4-byte alignment. */
	if (NULL == fh->ovfmt)
		return -EINVAL;
	width_mask = ~0;
	switch (fh->ovfmt->depth) {
	case 8:
	case 24:
		width_mask = ~3;
		break;
	case 16:
		width_mask = ~1;
		break;
	case 32:
		break;
	default:
		BUG();
	}

	win->w.width -= win->w.left & ~width_mask;
	win->w.left = (win->w.left - width_mask - 1) & width_mask;

	rc = limit_scaled_size(fh, &win->w.width, &win->w.height,
			       field, width_mask,
			       /* width_bias: round down */ 0,
			       adjust_size, adjust_crop);
	if (0 != rc)
		return rc;

	win->field = field;
	return 0;
}

static int setup_window(struct bttv_fh *fh, struct bttv *btv,
			struct v4l2_window *win, int fixup)
{
	struct v4l2_clip *clips = NULL;
	int n,size,retval = 0;

	if (NULL == fh->ovfmt)
		return -EINVAL;
	if (!(fh->ovfmt->flags & FORMAT_FLAGS_PACKED))
		return -EINVAL;
	retval = verify_window(fh, win,
			       /* adjust_size */ fixup,
			       /* adjust_crop */ fixup);
	if (0 != retval)
		return retval;

	/* copy clips  --  luckily v4l1 + v4l2 are binary
	   compatible here ...*/
	n = win->clipcount;
	size = sizeof(*clips)*(n+4);
	clips = kmalloc(size,GFP_KERNEL);
	if (NULL == clips)
		return -ENOMEM;
	if (n > 0) {
		if (copy_from_user(clips,win->clips,sizeof(struct v4l2_clip)*n)) {
			kfree(clips);
			return -EFAULT;
		}
	}
	/* clip against screen */
	if (NULL != btv->fbuf.base)
		n = btcx_screen_clips(btv->fbuf.fmt.width, btv->fbuf.fmt.height,
				      &win->w, clips, n);
	btcx_sort_clips(clips,n);

	/* 4-byte alignments */
	switch (fh->ovfmt->depth) {
	case 8:
	case 24:
		btcx_align(&win->w, clips, n, 3);
		break;
	case 16:
		btcx_align(&win->w, clips, n, 1);
		break;
	case 32:
		/* no alignment fixups needed */
		break;
	default:
		BUG();
	}

	mutex_lock(&fh->cap.lock);
		kfree(fh->ov.clips);
	fh->ov.clips    = clips;
	fh->ov.nclips   = n;

	fh->ov.w        = win->w;
	fh->ov.field    = win->field;
	fh->ov.setup_ok = 1;
	btv->init.ov.w.width   = win->w.width;
	btv->init.ov.w.height  = win->w.height;
	btv->init.ov.field     = win->field;

	/* update overlay if needed */
	retval = 0;
	if (check_btres(fh, RESOURCE_OVERLAY)) {
		struct bttv_buffer *new;

		new = videobuf_alloc(sizeof(*new));
		new->crop = btv->crop[!!fh->do_crop].rect;
		bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		retval = bttv_switch_overlay(btv,fh,new);
	}
	mutex_unlock(&fh->cap.lock);
	return retval;
}

/* ----------------------------------------------------------------------- */

static struct videobuf_queue* bttv_queue(struct bttv_fh *fh)
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

static int bttv_resource(struct bttv_fh *fh)
{
	int res = 0;

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		res = RESOURCE_VIDEO_STREAM;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		res = RESOURCE_VBI;
		break;
	default:
		BUG();
	}
	return res;
}

static int bttv_switch_type(struct bttv_fh *fh, enum v4l2_buf_type type)
{
	struct videobuf_queue *q = bttv_queue(fh);
	int res = bttv_resource(fh);

	if (check_btres(fh,res))
		return -EBUSY;
	if (videobuf_queue_is_busy(q))
		return -EBUSY;
	fh->type = type;
	return 0;
}

static void
pix_format_set_size     (struct v4l2_pix_format *       f,
			 const struct bttv_format *     fmt,
			 unsigned int                   width,
			 unsigned int                   height)
{
	f->width = width;
	f->height = height;

	if (fmt->flags & FORMAT_FLAGS_PLANAR) {
		f->bytesperline = width; /* Y plane */
		f->sizeimage = (width * height * fmt->depth) >> 3;
	} else {
		f->bytesperline = (width * fmt->depth) >> 3;
		f->sizeimage = height * f->bytesperline;
	}
}

static int bttv_g_fmt(struct bttv_fh *fh, struct v4l2_format *f)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(&f->fmt.pix,0,sizeof(struct v4l2_pix_format));
		pix_format_set_size (&f->fmt.pix, fh->fmt,
				     fh->width, fh->height);
		f->fmt.pix.field        = fh->cap.field;
		f->fmt.pix.pixelformat  = fh->fmt->fourcc;
		return 0;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		memset(&f->fmt.win,0,sizeof(struct v4l2_window));
		f->fmt.win.w     = fh->ov.w;
		f->fmt.win.field = fh->ov.field;
		return 0;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		bttv_vbi_get_fmt(fh, &f->fmt.vbi);
		return 0;
	default:
		return -EINVAL;
	}
}

static int bttv_try_fmt(struct bttv_fh *fh, struct bttv *btv,
			struct v4l2_format *f, int adjust_crop)
{
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		const struct bttv_format *fmt;
		enum v4l2_field field;
		__s32 width, height;
		int rc;

		fmt = format_by_fourcc(f->fmt.pix.pixelformat);
		if (NULL == fmt)
			return -EINVAL;

		field = f->fmt.pix.field;
		if (V4L2_FIELD_ANY == field) {
			__s32 height2;

			height2 = btv->crop[!!fh->do_crop].rect.height >> 1;
			field = (f->fmt.pix.height > height2)
				? V4L2_FIELD_INTERLACED
				: V4L2_FIELD_BOTTOM;
		}
		if (V4L2_FIELD_SEQ_BT == field)
			field = V4L2_FIELD_SEQ_TB;
		switch (field) {
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
		case V4L2_FIELD_ALTERNATE:
		case V4L2_FIELD_INTERLACED:
			break;
		case V4L2_FIELD_SEQ_TB:
			if (fmt->flags & FORMAT_FLAGS_PLANAR)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}

		width = f->fmt.pix.width;
		height = f->fmt.pix.height;

		rc = limit_scaled_size(fh, &width, &height, field,
				       /* width_mask: 4 pixels */ ~3,
				       /* width_bias: nearest */ 2,
				       /* adjust_size */ 1,
				       adjust_crop);
		if (0 != rc)
			return rc;

		/* update data for the application */
		f->fmt.pix.field = field;
		pix_format_set_size(&f->fmt.pix, fmt, width, height);

		return 0;
	}
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		return verify_window(fh, &f->fmt.win,
				     /* adjust_size */ 1,
				     /* adjust_crop */ 0);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		return bttv_vbi_try_fmt(fh, &f->fmt.vbi);
	default:
		return -EINVAL;
	}
}

static int bttv_s_fmt(struct bttv_fh *fh, struct bttv *btv,
		      struct v4l2_format *f)
{
	int retval;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	{
		const struct bttv_format *fmt;

		retval = bttv_switch_type(fh,f->type);
		if (0 != retval)
			return retval;
		retval = bttv_try_fmt(fh,btv,f, /* adjust_crop */ 1);
		if (0 != retval)
			return retval;
		fmt = format_by_fourcc(f->fmt.pix.pixelformat);

		/* update our state informations */
		mutex_lock(&fh->cap.lock);
		fh->fmt              = fmt;
		fh->cap.field        = f->fmt.pix.field;
		fh->cap.last         = V4L2_FIELD_NONE;
		fh->width            = f->fmt.pix.width;
		fh->height           = f->fmt.pix.height;
		btv->init.fmt        = fmt;
		btv->init.width      = f->fmt.pix.width;
		btv->init.height     = f->fmt.pix.height;
		mutex_unlock(&fh->cap.lock);

		return 0;
	}
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		if (no_overlay > 0) {
			printk ("V4L2_BUF_TYPE_VIDEO_OVERLAY: no_overlay\n");
			return -EINVAL;
		}
		return setup_window(fh, btv, &f->fmt.win, 1);
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		retval = bttv_switch_type(fh,f->type);
		if (0 != retval)
			return retval;
		return bttv_vbi_set_fmt(fh, &f->fmt.vbi);
	default:
		return -EINVAL;
	}
}

static int bttv_do_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, void *arg)
{
	struct bttv_fh *fh  = file->private_data;
	struct bttv    *btv = fh->btv;
	unsigned long flags;
	int retval = 0;

	if (bttv_debug > 1)
		v4l_print_ioctl(btv->c.name, cmd);

	if (btv->errors)
		bttv_reinit_bt848(btv);

	switch (cmd) {
	case VIDIOCSFREQ:
	case VIDIOCSTUNER:
	case VIDIOCSCHAN:
	case VIDIOC_S_CTRL:
	case VIDIOC_S_STD:
	case VIDIOC_S_INPUT:
	case VIDIOC_S_TUNER:
	case VIDIOC_S_FREQUENCY:
		retval = v4l2_prio_check(&btv->prio,&fh->prio);
		if (0 != retval)
			return retval;
	};

	switch (cmd) {

	/* ***  v4l1  *** ************************************************ */
	case VIDIOCGCAP:
	{
		struct video_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
		strcpy(cap->name,btv->video_dev->name);
		if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type) {
			/* vbi */
			cap->type = VID_TYPE_TUNER|VID_TYPE_TELETEXT;
		} else {
			/* others */
			cap->type = VID_TYPE_CAPTURE|
				VID_TYPE_TUNER|
				VID_TYPE_CLIPPING|
				VID_TYPE_SCALES;
			if (no_overlay <= 0)
				cap->type |= VID_TYPE_OVERLAY;

			cap->maxwidth  = bttv_tvnorms[btv->tvnorm].swidth;
			cap->maxheight = bttv_tvnorms[btv->tvnorm].sheight;
			cap->minwidth  = 48;
			cap->minheight = 32;
		}
		cap->channels  = bttv_tvcards[btv->c.type].video_inputs;
		cap->audios    = bttv_tvcards[btv->c.type].audio_inputs;
		return 0;
	}

	case VIDIOCGPICT:
	{
		struct video_picture *pic = arg;

		memset(pic,0,sizeof(*pic));
		pic->brightness = btv->bright;
		pic->contrast   = btv->contrast;
		pic->hue        = btv->hue;
		pic->colour     = btv->saturation;
		if (fh->fmt) {
			pic->depth   = fh->fmt->depth;
			pic->palette = fh->fmt->palette;
		}
		return 0;
	}
	case VIDIOCSPICT:
	{
		struct video_picture *pic = arg;
		const struct bttv_format *fmt;

		fmt = format_by_palette(pic->palette);
		if (NULL == fmt)
			return -EINVAL;
		mutex_lock(&fh->cap.lock);
		if (fmt->depth != pic->depth) {
			retval = -EINVAL;
			goto fh_unlock_and_return;
		}
		if (fmt->flags & FORMAT_FLAGS_RAW) {
			/* VIDIOCMCAPTURE uses gbufsize, not RAW_BPL *
			   RAW_LINES * 2. F1 is stored at offset 0, F2
			   at buffer size / 2. */
			fh->width = RAW_BPL;
			fh->height = gbufsize / RAW_BPL;
			btv->init.width  = RAW_BPL;
			btv->init.height = gbufsize / RAW_BPL;
		}
		fh->ovfmt   = fmt;
		fh->fmt     = fmt;
		btv->init.ovfmt   = fmt;
		btv->init.fmt     = fmt;
		if (bigendian) {
			/* dirty hack time:  swap bytes for overlay if the
			   display adaptor is big endian (insmod option) */
			if (fmt->palette == VIDEO_PALETTE_RGB555 ||
			    fmt->palette == VIDEO_PALETTE_RGB565 ||
			    fmt->palette == VIDEO_PALETTE_RGB32) {
				fh->ovfmt = fmt+1;
			}
		}
		bt848_bright(btv,pic->brightness);
		bt848_contrast(btv,pic->contrast);
		bt848_hue(btv,pic->hue);
		bt848_sat(btv,pic->colour);
		mutex_unlock(&fh->cap.lock);
		return 0;
	}

	case VIDIOCGWIN:
	{
		struct video_window *win = arg;

		memset(win,0,sizeof(*win));
		win->x      = fh->ov.w.left;
		win->y      = fh->ov.w.top;
		win->width  = fh->ov.w.width;
		win->height = fh->ov.w.height;
		return 0;
	}
	case VIDIOCSWIN:
	{
		struct video_window *win = arg;
		struct v4l2_window w2;

		if (no_overlay > 0) {
			printk ("VIDIOCSWIN: no_overlay\n");
			return -EINVAL;
		}

		w2.field = V4L2_FIELD_ANY;
		w2.w.left    = win->x;
		w2.w.top     = win->y;
		w2.w.width   = win->width;
		w2.w.height  = win->height;
		w2.clipcount = win->clipcount;
		w2.clips     = (struct v4l2_clip __user *)win->clips;
		retval = setup_window(fh, btv, &w2, 0);
		if (0 == retval) {
			/* on v4l1 this ioctl affects the read() size too */
			fh->width  = fh->ov.w.width;
			fh->height = fh->ov.w.height;
			btv->init.width  = fh->ov.w.width;
			btv->init.height = fh->ov.w.height;
		}
		return retval;
	}

	case VIDIOCGFBUF:
	{
		struct video_buffer *fbuf = arg;

		fbuf->base          = btv->fbuf.base;
		fbuf->width         = btv->fbuf.fmt.width;
		fbuf->height        = btv->fbuf.fmt.height;
		fbuf->bytesperline  = btv->fbuf.fmt.bytesperline;
		if (fh->ovfmt)
			fbuf->depth = fh->ovfmt->depth;
		else {
			if (fbuf->width)
				fbuf->depth   = ((fbuf->bytesperline<<3)
						  + (fbuf->width-1) )
						  /fbuf->width;
			else
				fbuf->depth = 0;
		}
		return 0;
	}
	case VIDIOCSFBUF:
	{
		struct video_buffer *fbuf = arg;
		const struct bttv_format *fmt;
		unsigned long end;

		if(!capable(CAP_SYS_ADMIN) &&
		   !capable(CAP_SYS_RAWIO))
			return -EPERM;
		end = (unsigned long)fbuf->base +
			fbuf->height * fbuf->bytesperline;
		mutex_lock(&fh->cap.lock);
		retval = -EINVAL;

		switch (fbuf->depth) {
		case 8:
			fmt = format_by_palette(VIDEO_PALETTE_HI240);
			break;
		case 16:
			fmt = format_by_palette(VIDEO_PALETTE_RGB565);
			break;
		case 24:
			fmt = format_by_palette(VIDEO_PALETTE_RGB24);
			break;
		case 32:
			fmt = format_by_palette(VIDEO_PALETTE_RGB32);
			break;
		case 15:
			fbuf->depth = 16;
			fmt = format_by_palette(VIDEO_PALETTE_RGB555);
			break;
		default:
			fmt = NULL;
			break;
		}
		if (NULL == fmt)
			goto fh_unlock_and_return;

		fh->ovfmt = fmt;
		fh->fmt   = fmt;
		btv->init.ovfmt = fmt;
		btv->init.fmt   = fmt;
		btv->fbuf.base             = fbuf->base;
		btv->fbuf.fmt.width        = fbuf->width;
		btv->fbuf.fmt.height       = fbuf->height;
		if (fbuf->bytesperline)
			btv->fbuf.fmt.bytesperline = fbuf->bytesperline;
		else
			btv->fbuf.fmt.bytesperline = btv->fbuf.fmt.width*fbuf->depth/8;
		mutex_unlock(&fh->cap.lock);
		return 0;
	}

	case VIDIOCCAPTURE:
	case VIDIOC_OVERLAY:
	{
		struct bttv_buffer *new;
		int *on = arg;

		if (*on) {
			/* verify args */
			if (NULL == btv->fbuf.base)
				return -EINVAL;
			if (!fh->ov.setup_ok) {
				dprintk("bttv%d: overlay: !setup_ok\n",btv->c.nr);
				return -EINVAL;
			}
		}

		if (!check_alloc_btres(btv,fh,RESOURCE_OVERLAY))
			return -EBUSY;

		mutex_lock(&fh->cap.lock);
		if (*on) {
			fh->ov.tvnorm = btv->tvnorm;
			new = videobuf_alloc(sizeof(*new));
			new->crop = btv->crop[!!fh->do_crop].rect;
			bttv_overlay_risc(btv, &fh->ov, fh->ovfmt, new);
		} else {
			new = NULL;
		}

		/* switch over */
		retval = bttv_switch_overlay(btv,fh,new);
		mutex_unlock(&fh->cap.lock);
		return retval;
	}

	case VIDIOCGMBUF:
	{
		struct video_mbuf *mbuf = arg;
		unsigned int i;

		mutex_lock(&fh->cap.lock);
		retval = videobuf_mmap_setup(&fh->cap,gbuffers,gbufsize,
					     V4L2_MEMORY_MMAP);
		if (retval < 0)
			goto fh_unlock_and_return;
		memset(mbuf,0,sizeof(*mbuf));
		mbuf->frames = gbuffers;
		mbuf->size   = gbuffers * gbufsize;
		for (i = 0; i < gbuffers; i++)
			mbuf->offsets[i] = i * gbufsize;
		mutex_unlock(&fh->cap.lock);
		return 0;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap *vm = arg;
		struct bttv_buffer *buf;
		enum v4l2_field field;
		__s32 height2;
		int res;

		if (vm->frame >= VIDEO_MAX_FRAME)
			return -EINVAL;

		res = bttv_resource(fh);
		if (!check_alloc_btres(btv, fh, res))
			return -EBUSY;

		mutex_lock(&fh->cap.lock);
		retval = -EINVAL;
		buf = (struct bttv_buffer *)fh->cap.bufs[vm->frame];
		if (NULL == buf)
			goto fh_unlock_and_return;
		if (0 == buf->vb.baddr)
			goto fh_unlock_and_return;
		if (buf->vb.state == STATE_QUEUED ||
		    buf->vb.state == STATE_ACTIVE)
			goto fh_unlock_and_return;

		height2 = btv->crop[!!fh->do_crop].rect.height >> 1;
		field = (vm->height > height2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
		retval = bttv_prepare_buffer(&fh->cap,btv,buf,
					     format_by_palette(vm->format),
					     vm->width,vm->height,field);
		if (0 != retval)
			goto fh_unlock_and_return;
		spin_lock_irqsave(&btv->s_lock,flags);
		buffer_queue(&fh->cap,&buf->vb);
		spin_unlock_irqrestore(&btv->s_lock,flags);
		mutex_unlock(&fh->cap.lock);
		return 0;
	}
	case VIDIOCSYNC:
	{
		int *frame = arg;
		struct bttv_buffer *buf;

		if (*frame >= VIDEO_MAX_FRAME)
			return -EINVAL;

		mutex_lock(&fh->cap.lock);
		retval = -EINVAL;
		buf = (struct bttv_buffer *)fh->cap.bufs[*frame];
		if (NULL == buf)
			goto fh_unlock_and_return;
		retval = videobuf_waiton(&buf->vb,0,1);
		if (0 != retval)
			goto fh_unlock_and_return;
		switch (buf->vb.state) {
		case STATE_ERROR:
			retval = -EIO;
			/* fall through */
		case STATE_DONE:
			videobuf_dma_sync(&fh->cap,&buf->vb.dma);
			bttv_dma_free(&fh->cap,btv,buf);
			break;
		default:
			retval = -EINVAL;
			break;
		}
		mutex_unlock(&fh->cap.lock);
		return retval;
	}

	case VIDIOCGVBIFMT:
		if (fh->type != V4L2_BUF_TYPE_VBI_CAPTURE) {
			retval = bttv_switch_type(fh,V4L2_BUF_TYPE_VBI_CAPTURE);
			if (0 != retval)
				return retval;
		}

		/* fall through */

	case VIDIOCSVBIFMT:
		return v4l_compat_translate_ioctl(inode, file, cmd,
						  arg, bttv_do_ioctl);

	case BTTV_VERSION:
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	case VIDIOCGCHAN:
	case VIDIOCSCHAN:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		return bttv_common_ioctls(btv,cmd,arg);

	/* ***  v4l2  *** ************************************************ */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		if (0 == v4l2)
			return -EINVAL;
		memset(cap, 0, sizeof (*cap));
		strlcpy(cap->driver, "bttv", sizeof (cap->driver));
		strlcpy(cap->card, btv->video_dev->name, sizeof (cap->card));
		snprintf(cap->bus_info, sizeof (cap->bus_info),
			 "PCI:%s", pci_name(btv->c.pci));
		cap->version = BTTV_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_VBI_CAPTURE |
			V4L2_CAP_READWRITE |
			V4L2_CAP_STREAMING;
		if (no_overlay <= 0)
			cap->capabilities |= V4L2_CAP_VIDEO_OVERLAY;

		if (bttv_tvcards[btv->c.type].tuner != UNSET &&
		    bttv_tvcards[btv->c.type].tuner != TUNER_ABSENT)
			cap->capabilities |= V4L2_CAP_TUNER;
		return 0;
	}

	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		enum v4l2_buf_type type;
		unsigned int i;
		int index;

		type  = f->type;
		if (V4L2_BUF_TYPE_VBI_CAPTURE == type) {
			/* vbi */
			index = f->index;
			if (0 != index)
				return -EINVAL;
			memset(f,0,sizeof(*f));
			f->index       = index;
			f->type        = type;
			f->pixelformat = V4L2_PIX_FMT_GREY;
			strcpy(f->description,"vbi data");
			return 0;
		}

		/* video capture + overlay */
		index = -1;
		for (i = 0; i < BTTV_FORMATS; i++) {
			if (bttv_formats[i].fourcc != -1)
				index++;
			if ((unsigned int)index == f->index)
				break;
		}
		if (BTTV_FORMATS == i)
			return -EINVAL;

		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
			break;
		case V4L2_BUF_TYPE_VIDEO_OVERLAY:
			if (!(bttv_formats[i].flags & FORMAT_FLAGS_PACKED))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		memset(f,0,sizeof(*f));
		f->index       = index;
		f->type        = type;
		f->pixelformat = bttv_formats[i].fourcc;
		strlcpy(f->description,bttv_formats[i].name,sizeof(f->description));
		return 0;
	}

	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;
		return bttv_try_fmt(fh,btv,f, /* adjust_crop */ 0);
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;
		return bttv_g_fmt(fh,f);
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;
		return bttv_s_fmt(fh,btv,f);
	}

	case VIDIOC_G_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;

		*fb = btv->fbuf;
		fb->capability = V4L2_FBUF_CAP_LIST_CLIPPING;
		if (fh->ovfmt)
			fb->fmt.pixelformat  = fh->ovfmt->fourcc;
		return 0;
	}
	case VIDIOC_S_FBUF:
	{
		struct v4l2_framebuffer *fb = arg;
		const struct bttv_format *fmt;

		if(!capable(CAP_SYS_ADMIN) &&
		   !capable(CAP_SYS_RAWIO))
			return -EPERM;

		/* check args */
		fmt = format_by_fourcc(fb->fmt.pixelformat);
		if (NULL == fmt)
			return -EINVAL;
		if (0 == (fmt->flags & FORMAT_FLAGS_PACKED))
			return -EINVAL;

		retval = -EINVAL;
		if (fb->flags & V4L2_FBUF_FLAG_OVERLAY) {
			__s32 width = fb->fmt.width;
			__s32 height = fb->fmt.height;

			retval = limit_scaled_size(fh, &width, &height,
						   V4L2_FIELD_INTERLACED,
						   /* width_mask */ ~3,
						   /* width_bias */ 2,
						   /* adjust_size */ 0,
						   /* adjust_crop */ 0);
			if (0 != retval)
				return retval;
		}

		/* ok, accept it */
		mutex_lock(&fh->cap.lock);
		btv->fbuf.base       = fb->base;
		btv->fbuf.fmt.width  = fb->fmt.width;
		btv->fbuf.fmt.height = fb->fmt.height;
		if (0 != fb->fmt.bytesperline)
			btv->fbuf.fmt.bytesperline = fb->fmt.bytesperline;
		else
			btv->fbuf.fmt.bytesperline = btv->fbuf.fmt.width*fmt->depth/8;

		retval = 0;
		fh->ovfmt = fmt;
		btv->init.ovfmt = fmt;
		if (fb->flags & V4L2_FBUF_FLAG_OVERLAY) {
			fh->ov.w.left   = 0;
			fh->ov.w.top    = 0;
			fh->ov.w.width  = fb->fmt.width;
			fh->ov.w.height = fb->fmt.height;
			btv->init.ov.w.width  = fb->fmt.width;
			btv->init.ov.w.height = fb->fmt.height;
				kfree(fh->ov.clips);
			fh->ov.clips = NULL;
			fh->ov.nclips = 0;

			if (check_btres(fh, RESOURCE_OVERLAY)) {
				struct bttv_buffer *new;

				new = videobuf_alloc(sizeof(*new));
				new->crop = btv->crop[!!fh->do_crop].rect;
				bttv_overlay_risc(btv,&fh->ov,fh->ovfmt,new);
				retval = bttv_switch_overlay(btv,fh,new);
			}
		}
		mutex_unlock(&fh->cap.lock);
		return retval;
	}

	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(bttv_queue(fh),arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(bttv_queue(fh),arg);

	case VIDIOC_QBUF:
	{
		int res = bttv_resource(fh);

		if (!check_alloc_btres(btv, fh, res))
			return -EBUSY;
		return videobuf_qbuf(bttv_queue(fh),arg);
	}

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(bttv_queue(fh),arg,
				      file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
	{
		int res = bttv_resource(fh);

		if (!check_alloc_btres(btv,fh,res))
			return -EBUSY;
		return videobuf_streamon(bttv_queue(fh));
	}
	case VIDIOC_STREAMOFF:
	{
		int res = bttv_resource(fh);

		retval = videobuf_streamoff(bttv_queue(fh));
		if (retval < 0)
			return retval;
		free_btres(btv,fh,res);
		return 0;
	}

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *c = arg;
		int i;

		if ((c->id <  V4L2_CID_BASE ||
		     c->id >= V4L2_CID_LASTP1) &&
		    (c->id <  V4L2_CID_PRIVATE_BASE ||
		     c->id >= V4L2_CID_PRIVATE_LASTP1))
			return -EINVAL;
		for (i = 0; i < BTTV_CTLS; i++)
			if (bttv_ctls[i].id == c->id)
				break;
		if (i == BTTV_CTLS) {
			*c = no_ctl;
			return 0;
		}
		*c = bttv_ctls[i];
		if (btv->audio_hook && i >= 4 && i <= 8) {
			struct video_audio va;
			memset(&va,0,sizeof(va));
			btv->audio_hook(btv,&va,0);
			switch (bttv_ctls[i].id) {
			case V4L2_CID_AUDIO_VOLUME:
				if (!(va.flags & VIDEO_AUDIO_VOLUME))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_BALANCE:
				if (!(va.flags & VIDEO_AUDIO_BALANCE))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_BASS:
				if (!(va.flags & VIDEO_AUDIO_BASS))
					*c = no_ctl;
				break;
			case V4L2_CID_AUDIO_TREBLE:
				if (!(va.flags & VIDEO_AUDIO_TREBLE))
					*c = no_ctl;
				break;
			}
		}
		return 0;
	}
	case VIDIOC_G_CTRL:
		return get_control(btv,arg);
	case VIDIOC_S_CTRL:
		return set_control(btv,arg);
	case VIDIOC_G_PARM:
	{
		struct v4l2_streamparm *parm = arg;
		struct v4l2_standard s;
		if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		memset(parm,0,sizeof(*parm));
		v4l2_video_std_construct(&s, bttv_tvnorms[btv->tvnorm].v4l2_id,
					 bttv_tvnorms[btv->tvnorm].name);
		parm->parm.capture.timeperframe = s.frameperiod;
		return 0;
	}

	case VIDIOC_G_PRIORITY:
	{
		enum v4l2_priority *p = arg;

		*p = v4l2_prio_max(&btv->prio);
		return 0;
	}
	case VIDIOC_S_PRIORITY:
	{
		enum v4l2_priority *prio = arg;

		return v4l2_prio_change(&btv->prio, &fh->prio, *prio);
	}

	case VIDIOC_CROPCAP:
	{
		struct v4l2_cropcap *cap = arg;
		enum v4l2_buf_type type;

		type = cap->type;

		if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
			return -EINVAL;

		*cap = bttv_tvnorms[btv->tvnorm].cropcap;
		cap->type = type;

		return 0;
	}
	case VIDIOC_G_CROP:
	{
		struct v4l2_crop * crop = arg;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
			return -EINVAL;

		/* No fh->do_crop = 1; because btv->crop[1] may be
		   inconsistent with fh->width or fh->height and apps
		   do not expect a change here. */

		crop->c = btv->crop[!!fh->do_crop].rect;

		return 0;
	}
	case VIDIOC_S_CROP:
	{
		struct v4l2_crop *crop = arg;
		const struct v4l2_rect *b;
		struct bttv_crop c;
		__s32 b_left;
		__s32 b_top;
		__s32 b_right;
		__s32 b_bottom;

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY)
			return -EINVAL;

		retval = v4l2_prio_check(&btv->prio,&fh->prio);
		if (0 != retval)
			return retval;

		/* Make sure tvnorm, vbi_end and the current cropping
		   parameters remain consistent until we're done. Note
		   read() may change vbi_end in check_alloc_btres(). */
		mutex_lock(&btv->lock);

		retval = -EBUSY;

		if (locked_btres(fh->btv, VIDEO_RESOURCES))
			goto btv_unlock_and_return;

		b = &bttv_tvnorms[btv->tvnorm].cropcap.bounds;

		b_left = b->left;
		b_right = b_left + b->width;
		b_bottom = b->top + b->height;

		b_top = max(b->top, btv->vbi_end);
		if (b_top + 32 >= b_bottom)
			goto btv_unlock_and_return;

		/* Min. scaled size 48 x 32. */
		c.rect.left = clamp(crop->c.left, b_left, b_right - 48);
		c.rect.left = min(c.rect.left, (__s32) MAX_HDELAY);

		c.rect.width = clamp(crop->c.width,
				     48, b_right - c.rect.left);

		c.rect.top = clamp(crop->c.top, b_top, b_bottom - 32);
		/* Top and height must be a multiple of two. */
		c.rect.top = (c.rect.top + 1) & ~1;

		c.rect.height = clamp(crop->c.height,
				      32, b_bottom - c.rect.top);
		c.rect.height = (c.rect.height + 1) & ~1;

		bttv_crop_calc_limits(&c);

		btv->crop[1] = c;

		mutex_unlock(&btv->lock);

		fh->do_crop = 1;

		mutex_lock(&fh->cap.lock);

		if (fh->width < c.min_scaled_width) {
			fh->width = c.min_scaled_width;
			btv->init.width = c.min_scaled_width;
		} else if (fh->width > c.max_scaled_width) {
			fh->width = c.max_scaled_width;
			btv->init.width = c.max_scaled_width;
		}

		if (fh->height < c.min_scaled_height) {
			fh->height = c.min_scaled_height;
			btv->init.height = c.min_scaled_height;
		} else if (fh->height > c.max_scaled_height) {
			fh->height = c.max_scaled_height;
			btv->init.height = c.max_scaled_height;
		}

		mutex_unlock(&fh->cap.lock);

		return 0;
	}

	case VIDIOC_ENUMSTD:
	case VIDIOC_G_STD:
	case VIDIOC_S_STD:
	case VIDIOC_ENUMINPUT:
	case VIDIOC_G_INPUT:
	case VIDIOC_S_INPUT:
	case VIDIOC_G_TUNER:
	case VIDIOC_S_TUNER:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
	case VIDIOC_LOG_STATUS:
		return bttv_common_ioctls(btv,cmd,arg);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;

 fh_unlock_and_return:
	mutex_unlock(&fh->cap.lock);
	return retval;

 btv_unlock_and_return:
	mutex_unlock(&btv->lock);
	return retval;
}

static int bttv_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg)
{
	struct bttv_fh *fh  = file->private_data;

	switch (cmd) {
	case BTTV_VBISIZE:
	{
		const struct bttv_tvnorm *tvnorm;

		tvnorm = fh->vbi_fmt.tvnorm;

		if (fh->vbi_fmt.fmt.start[0] != tvnorm->vbistart[0] ||
		    fh->vbi_fmt.fmt.start[1] != tvnorm->vbistart[1] ||
		    fh->vbi_fmt.fmt.count[0] != fh->vbi_fmt.fmt.count[1]) {
			/* BTTV_VBISIZE cannot express these parameters,
			   however open() resets the paramters to defaults
			   and apps shouldn't call BTTV_VBISIZE after
			   VIDIOC_S_FMT. */
			return -EINVAL;
		}

		bttv_switch_type(fh,V4L2_BUF_TYPE_VBI_CAPTURE);
		return (fh->vbi_fmt.fmt.count[0] * 2
			* fh->vbi_fmt.fmt.samples_per_line);
	}

	default:
		return video_usercopy(inode, file, cmd, arg, bttv_do_ioctl);
	}
}

static ssize_t bttv_read(struct file *file, char __user *data,
			 size_t count, loff_t *ppos)
{
	struct bttv_fh *fh = file->private_data;
	int retval = 0;

	if (fh->btv->errors)
		bttv_reinit_bt848(fh->btv);
	dprintk("bttv%d: read count=%d type=%s\n",
		fh->btv->c.nr,(int)count,v4l2_type_names[fh->type]);

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (!check_alloc_btres(fh->btv, fh, RESOURCE_VIDEO_READ)) {
			/* VIDEO_READ in use by another fh,
			   or VIDEO_STREAM by any fh. */
			return -EBUSY;
		}
		retval = videobuf_read_one(&fh->cap, data, count, ppos,
					   file->f_flags & O_NONBLOCK);
		free_btres(fh->btv, fh, RESOURCE_VIDEO_READ);
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		if (!check_alloc_btres(fh->btv,fh,RESOURCE_VBI))
			return -EBUSY;
		retval = videobuf_read_stream(&fh->vbi, data, count, ppos, 1,
					      file->f_flags & O_NONBLOCK);
		break;
	default:
		BUG();
	}
	return retval;
}

static unsigned int bttv_poll(struct file *file, poll_table *wait)
{
	struct bttv_fh *fh = file->private_data;
	struct bttv_buffer *buf;
	enum v4l2_field field;

	if (V4L2_BUF_TYPE_VBI_CAPTURE == fh->type) {
		if (!check_alloc_btres(fh->btv,fh,RESOURCE_VBI))
			return POLLERR;
		return videobuf_poll_stream(file, &fh->vbi, wait);
	}

	if (check_btres(fh,RESOURCE_VIDEO_STREAM)) {
		/* streaming capture */
		if (list_empty(&fh->cap.stream))
			return POLLERR;
		buf = list_entry(fh->cap.stream.next,struct bttv_buffer,vb.stream);
	} else {
		/* read() capture */
		mutex_lock(&fh->cap.lock);
		if (NULL == fh->cap.read_buf) {
			/* need to capture a new frame */
			if (locked_btres(fh->btv,RESOURCE_VIDEO_STREAM)) {
				mutex_unlock(&fh->cap.lock);
				return POLLERR;
			}
			fh->cap.read_buf = videobuf_alloc(fh->cap.msize);
			if (NULL == fh->cap.read_buf) {
				mutex_unlock(&fh->cap.lock);
				return POLLERR;
			}
			fh->cap.read_buf->memory = V4L2_MEMORY_USERPTR;
			field = videobuf_next_field(&fh->cap);
			if (0 != fh->cap.ops->buf_prepare(&fh->cap,fh->cap.read_buf,field)) {
				kfree (fh->cap.read_buf);
				fh->cap.read_buf = NULL;
				mutex_unlock(&fh->cap.lock);
				return POLLERR;
			}
			fh->cap.ops->buf_queue(&fh->cap,fh->cap.read_buf);
			fh->cap.read_off = 0;
		}
		mutex_unlock(&fh->cap.lock);
		buf = (struct bttv_buffer*)fh->cap.read_buf;
	}

	poll_wait(file, &buf->vb.done, wait);
	if (buf->vb.state == STATE_DONE ||
	    buf->vb.state == STATE_ERROR)
		return POLLIN|POLLRDNORM;
	return 0;
}

static int bttv_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct bttv *btv = NULL;
	struct bttv_fh *fh;
	enum v4l2_buf_type type = 0;
	unsigned int i;

	dprintk(KERN_DEBUG "bttv: open minor=%d\n",minor);

	for (i = 0; i < bttv_num; i++) {
		if (bttvs[i].video_dev &&
		    bttvs[i].video_dev->minor == minor) {
			btv = &bttvs[i];
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			break;
		}
		if (bttvs[i].vbi_dev &&
		    bttvs[i].vbi_dev->minor == minor) {
			btv = &bttvs[i];
			type = V4L2_BUF_TYPE_VBI_CAPTURE;
			break;
		}
	}
	if (NULL == btv)
		return -ENODEV;

	dprintk(KERN_DEBUG "bttv%d: open called (type=%s)\n",
		btv->c.nr,v4l2_type_names[type]);

	/* allocate per filehandle data */
	fh = kmalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	file->private_data = fh;
	*fh = btv->init;
	fh->type = type;
	fh->ov.setup_ok = 0;
	v4l2_prio_open(&btv->prio,&fh->prio);

	videobuf_queue_init(&fh->cap, &bttv_video_qops,
			    btv->c.pci, &btv->s_lock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct bttv_buffer),
			    fh);
	videobuf_queue_init(&fh->vbi, &bttv_vbi_qops,
			    btv->c.pci, &btv->s_lock,
			    V4L2_BUF_TYPE_VBI_CAPTURE,
			    V4L2_FIELD_SEQ_TB,
			    sizeof(struct bttv_buffer),
			    fh);
	i2c_vidiocschan(btv);

	btv->users++;

	/* The V4L2 spec requires one global set of cropping parameters
	   which only change on request. These are stored in btv->crop[1].
	   However for compatibility with V4L apps and cropping unaware
	   V4L2 apps we now reset the cropping parameters as seen through
	   this fh, which is to say VIDIOC_G_CROP and scaling limit checks
	   will use btv->crop[0], the default cropping parameters for the
	   current video standard, and VIDIOC_S_FMT will not implicitely
	   change the cropping parameters until VIDIOC_S_CROP has been
	   called. */
	fh->do_crop = !reset_crop; /* module parameter */

	/* Likewise there should be one global set of VBI capture
	   parameters, but for compatibility with V4L apps and earlier
	   driver versions each fh has its own parameters. */
	bttv_vbi_fmt_reset(&fh->vbi_fmt, btv->tvnorm);

	bttv_field_count(btv);
	return 0;
}

static int bttv_release(struct inode *inode, struct file *file)
{
	struct bttv_fh *fh = file->private_data;
	struct bttv *btv = fh->btv;

	/* turn off overlay */
	if (check_btres(fh, RESOURCE_OVERLAY))
		bttv_switch_overlay(btv,fh,NULL);

	/* stop video capture */
	if (check_btres(fh, RESOURCE_VIDEO_STREAM)) {
		videobuf_streamoff(&fh->cap);
		free_btres(btv,fh,RESOURCE_VIDEO_STREAM);
	}
	if (fh->cap.read_buf) {
		buffer_release(&fh->cap,fh->cap.read_buf);
		kfree(fh->cap.read_buf);
	}
	if (check_btres(fh, RESOURCE_VIDEO_READ)) {
		free_btres(btv, fh, RESOURCE_VIDEO_READ);
	}

	/* stop vbi capture */
	if (check_btres(fh, RESOURCE_VBI)) {
		if (fh->vbi.streaming)
			videobuf_streamoff(&fh->vbi);
		if (fh->vbi.reading)
			videobuf_read_stop(&fh->vbi);
		free_btres(btv,fh,RESOURCE_VBI);
	}

	/* free stuff */
	videobuf_mmap_free(&fh->cap);
	videobuf_mmap_free(&fh->vbi);
	v4l2_prio_close(&btv->prio,&fh->prio);
	file->private_data = NULL;
	kfree(fh);

	btv->users--;
	bttv_field_count(btv);
	return 0;
}

static int
bttv_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct bttv_fh *fh = file->private_data;

	dprintk("bttv%d: mmap type=%s 0x%lx+%ld\n",
		fh->btv->c.nr, v4l2_type_names[fh->type],
		vma->vm_start, vma->vm_end - vma->vm_start);
	return videobuf_mmap_mapper(bttv_queue(fh),vma);
}

static const struct file_operations bttv_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = bttv_open,
	.release  = bttv_release,
	.ioctl	  = bttv_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek	  = no_llseek,
	.read	  = bttv_read,
	.mmap	  = bttv_mmap,
	.poll     = bttv_poll,
};

static struct video_device bttv_video_template =
{
	.name     = "UNSET",
	.type     = VID_TYPE_CAPTURE|VID_TYPE_TUNER|
		    VID_TYPE_CLIPPING|VID_TYPE_SCALES,
	.hardware = VID_HARDWARE_BT848,
	.fops     = &bttv_fops,
	.minor    = -1,
};

static struct video_device bttv_vbi_template =
{
	.name     = "bt848/878 vbi",
	.type     = VID_TYPE_TUNER|VID_TYPE_TELETEXT,
	.hardware = VID_HARDWARE_BT848,
	.fops     = &bttv_fops,
	.minor    = -1,
};

/* ----------------------------------------------------------------------- */
/* radio interface                                                         */

static int radio_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct bttv *btv = NULL;
	unsigned int i;

	dprintk("bttv: open minor=%d\n",minor);

	for (i = 0; i < bttv_num; i++) {
		if (bttvs[i].radio_dev->minor == minor) {
			btv = &bttvs[i];
			break;
		}
	}
	if (NULL == btv)
		return -ENODEV;

	dprintk("bttv%d: open called (radio)\n",btv->c.nr);
	mutex_lock(&btv->lock);

	btv->radio_user++;

	file->private_data = btv;

	bttv_call_i2c_clients(btv,AUDC_SET_RADIO,NULL);
	audio_input(btv,TVAUDIO_INPUT_RADIO);

	mutex_unlock(&btv->lock);
	return 0;
}

static int radio_release(struct inode *inode, struct file *file)
{
	struct bttv        *btv = file->private_data;
	struct rds_command cmd;

	btv->radio_user--;

	bttv_call_i2c_clients(btv, RDS_CMD_CLOSE, &cmd);

	return 0;
}

static int radio_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *arg)
{
	struct bttv    *btv = file->private_data;

	switch (cmd) {
	case VIDIOCGCAP:
	{
		struct video_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
		strcpy(cap->name,btv->radio_dev->name);
		cap->type = VID_TYPE_TUNER;
		cap->channels = 1;
		cap->audios = 1;
		return 0;
	}

	case VIDIOCGTUNER:
	{
		struct video_tuner *v = arg;

		if(v->tuner)
			return -EINVAL;
		memset(v,0,sizeof(*v));
		strcpy(v->name, "Radio");
		bttv_call_i2c_clients(btv,cmd,v);
		return 0;
	}
	case VIDIOCSTUNER:
		/* nothing to do */
		return 0;

	case BTTV_VERSION:
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
	case VIDIOC_LOG_STATUS:
		return bttv_common_ioctls(btv,cmd,arg);

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int radio_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, radio_do_ioctl);
}

static ssize_t radio_read(struct file *file, char __user *data,
			 size_t count, loff_t *ppos)
{
	struct bttv    *btv = file->private_data;
	struct rds_command cmd;
	cmd.block_count = count/3;
	cmd.buffer = data;
	cmd.instance = file;
	cmd.result = -ENODEV;

	bttv_call_i2c_clients(btv, RDS_CMD_READ, &cmd);

	return cmd.result;
}

static unsigned int radio_poll(struct file *file, poll_table *wait)
{
	struct bttv    *btv = file->private_data;
	struct rds_command cmd;
	cmd.instance = file;
	cmd.event_list = wait;
	cmd.result = -ENODEV;
	bttv_call_i2c_clients(btv, RDS_CMD_POLL, &cmd);

	return cmd.result;
}

static const struct file_operations radio_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = radio_open,
	.read     = radio_read,
	.release  = radio_release,
	.ioctl	  = radio_ioctl,
	.llseek	  = no_llseek,
	.poll     = radio_poll,
};

static struct video_device radio_template =
{
	.name     = "bt848/878 radio",
	.type     = VID_TYPE_TUNER,
	.hardware = VID_HARDWARE_BT848,
	.fops     = &radio_fops,
	.minor    = -1,
};

/* ----------------------------------------------------------------------- */
/* some debug code                                                         */

static int bttv_risc_decode(u32 risc)
{
	static char *instr[16] = {
		[ BT848_RISC_WRITE     >> 28 ] = "write",
		[ BT848_RISC_SKIP      >> 28 ] = "skip",
		[ BT848_RISC_WRITEC    >> 28 ] = "writec",
		[ BT848_RISC_JUMP      >> 28 ] = "jump",
		[ BT848_RISC_SYNC      >> 28 ] = "sync",
		[ BT848_RISC_WRITE123  >> 28 ] = "write123",
		[ BT848_RISC_SKIP123   >> 28 ] = "skip123",
		[ BT848_RISC_WRITE1S23 >> 28 ] = "write1s23",
	};
	static int incr[16] = {
		[ BT848_RISC_WRITE     >> 28 ] = 2,
		[ BT848_RISC_JUMP      >> 28 ] = 2,
		[ BT848_RISC_SYNC      >> 28 ] = 2,
		[ BT848_RISC_WRITE123  >> 28 ] = 5,
		[ BT848_RISC_SKIP123   >> 28 ] = 2,
		[ BT848_RISC_WRITE1S23 >> 28 ] = 3,
	};
	static char *bits[] = {
		"be0",  "be1",  "be2",  "be3/resync",
		"set0", "set1", "set2", "set3",
		"clr0", "clr1", "clr2", "clr3",
		"irq",  "res",  "eol",  "sol",
	};
	int i;

	printk("0x%08x [ %s", risc,
	       instr[risc >> 28] ? instr[risc >> 28] : "INVALID");
	for (i = ARRAY_SIZE(bits)-1; i >= 0; i--)
		if (risc & (1 << (i + 12)))
			printk(" %s",bits[i]);
	printk(" count=%d ]\n", risc & 0xfff);
	return incr[risc >> 28] ? incr[risc >> 28] : 1;
}

static void bttv_risc_disasm(struct bttv *btv,
			     struct btcx_riscmem *risc)
{
	unsigned int i,j,n;

	printk("%s: risc disasm: %p [dma=0x%08lx]\n",
	       btv->c.name, risc->cpu, (unsigned long)risc->dma);
	for (i = 0; i < (risc->size >> 2); i += n) {
		printk("%s:   0x%lx: ", btv->c.name,
		       (unsigned long)(risc->dma + (i<<2)));
		n = bttv_risc_decode(risc->cpu[i]);
		for (j = 1; j < n; j++)
			printk("%s:   0x%lx: 0x%08x [ arg #%d ]\n",
			       btv->c.name, (unsigned long)(risc->dma + ((i+j)<<2)),
			       risc->cpu[i+j], j);
		if (0 == risc->cpu[i])
			break;
	}
}

static void bttv_print_riscaddr(struct bttv *btv)
{
	printk("  main: %08Lx\n",
	       (unsigned long long)btv->main.dma);
	printk("  vbi : o=%08Lx e=%08Lx\n",
	       btv->cvbi ? (unsigned long long)btv->cvbi->top.dma : 0,
	       btv->cvbi ? (unsigned long long)btv->cvbi->bottom.dma : 0);
	printk("  cap : o=%08Lx e=%08Lx\n",
	       btv->curr.top    ? (unsigned long long)btv->curr.top->top.dma : 0,
	       btv->curr.bottom ? (unsigned long long)btv->curr.bottom->bottom.dma : 0);
	printk("  scr : o=%08Lx e=%08Lx\n",
	       btv->screen ? (unsigned long long)btv->screen->top.dma : 0,
	       btv->screen ? (unsigned long long)btv->screen->bottom.dma : 0);
	bttv_risc_disasm(btv, &btv->main);
}

/* ----------------------------------------------------------------------- */
/* irq handler                                                             */

static char *irq_name[] = {
	"FMTCHG",  // format change detected (525 vs. 625)
	"VSYNC",   // vertical sync (new field)
	"HSYNC",   // horizontal sync
	"OFLOW",   // chroma/luma AGC overflow
	"HLOCK",   // horizontal lock changed
	"VPRES",   // video presence changed
	"6", "7",
	"I2CDONE", // hw irc operation finished
	"GPINT",   // gpio port triggered irq
	"10",
	"RISCI",   // risc instruction triggered irq
	"FBUS",    // pixel data fifo dropped data (high pci bus latencies)
	"FTRGT",   // pixel data fifo overrun
	"FDSR",    // fifo data stream resyncronisation
	"PPERR",   // parity error (data transfer)
	"RIPERR",  // parity error (read risc instructions)
	"PABORT",  // pci abort
	"OCERR",   // risc instruction error
	"SCERR",   // syncronisation error
};

static void bttv_print_irqbits(u32 print, u32 mark)
{
	unsigned int i;

	printk("bits:");
	for (i = 0; i < ARRAY_SIZE(irq_name); i++) {
		if (print & (1 << i))
			printk(" %s",irq_name[i]);
		if (mark & (1 << i))
			printk("*");
	}
}

static void bttv_irq_debug_low_latency(struct bttv *btv, u32 rc)
{
	printk("bttv%d: irq: skipped frame [main=%lx,o_vbi=%lx,o_field=%lx,rc=%lx]\n",
	       btv->c.nr,
	       (unsigned long)btv->main.dma,
	       (unsigned long)btv->main.cpu[RISC_SLOT_O_VBI+1],
	       (unsigned long)btv->main.cpu[RISC_SLOT_O_FIELD+1],
	       (unsigned long)rc);

	if (0 == (btread(BT848_DSTATUS) & BT848_DSTATUS_HLOC)) {
		printk("bttv%d: Oh, there (temporarely?) is no input signal. "
		       "Ok, then this is harmless, don't worry ;)\n",
		       btv->c.nr);
		return;
	}
	printk("bttv%d: Uhm. Looks like we have unusual high IRQ latencies.\n",
	       btv->c.nr);
	printk("bttv%d: Lets try to catch the culpit red-handed ...\n",
	       btv->c.nr);
	dump_stack();
}

static int
bttv_irq_next_video(struct bttv *btv, struct bttv_buffer_set *set)
{
	struct bttv_buffer *item;

	memset(set,0,sizeof(*set));

	/* capture request ? */
	if (!list_empty(&btv->capture)) {
		set->frame_irq = 1;
		item = list_entry(btv->capture.next, struct bttv_buffer, vb.queue);
		if (V4L2_FIELD_HAS_TOP(item->vb.field))
			set->top    = item;
		if (V4L2_FIELD_HAS_BOTTOM(item->vb.field))
			set->bottom = item;

		/* capture request for other field ? */
		if (!V4L2_FIELD_HAS_BOTH(item->vb.field) &&
		    (item->vb.queue.next != &btv->capture)) {
			item = list_entry(item->vb.queue.next, struct bttv_buffer, vb.queue);
			if (!V4L2_FIELD_HAS_BOTH(item->vb.field)) {
				if (NULL == set->top &&
				    V4L2_FIELD_TOP == item->vb.field) {
					set->top = item;
				}
				if (NULL == set->bottom &&
				    V4L2_FIELD_BOTTOM == item->vb.field) {
					set->bottom = item;
				}
				if (NULL != set->top  &&  NULL != set->bottom)
					set->top_irq = 2;
			}
		}
	}

	/* screen overlay ? */
	if (NULL != btv->screen) {
		if (V4L2_FIELD_HAS_BOTH(btv->screen->vb.field)) {
			if (NULL == set->top && NULL == set->bottom) {
				set->top    = btv->screen;
				set->bottom = btv->screen;
			}
		} else {
			if (V4L2_FIELD_TOP == btv->screen->vb.field &&
			    NULL == set->top) {
				set->top = btv->screen;
			}
			if (V4L2_FIELD_BOTTOM == btv->screen->vb.field &&
			    NULL == set->bottom) {
				set->bottom = btv->screen;
			}
		}
	}

	dprintk("bttv%d: next set: top=%p bottom=%p [screen=%p,irq=%d,%d]\n",
		btv->c.nr,set->top, set->bottom,
		btv->screen,set->frame_irq,set->top_irq);
	return 0;
}

static void
bttv_irq_wakeup_video(struct bttv *btv, struct bttv_buffer_set *wakeup,
		      struct bttv_buffer_set *curr, unsigned int state)
{
	struct timeval ts;

	do_gettimeofday(&ts);

	if (wakeup->top == wakeup->bottom) {
		if (NULL != wakeup->top && curr->top != wakeup->top) {
			if (irq_debug > 1)
				printk("bttv%d: wakeup: both=%p\n",btv->c.nr,wakeup->top);
			wakeup->top->vb.ts = ts;
			wakeup->top->vb.field_count = btv->field_count;
			wakeup->top->vb.state = state;
			wake_up(&wakeup->top->vb.done);
		}
	} else {
		if (NULL != wakeup->top && curr->top != wakeup->top) {
			if (irq_debug > 1)
				printk("bttv%d: wakeup: top=%p\n",btv->c.nr,wakeup->top);
			wakeup->top->vb.ts = ts;
			wakeup->top->vb.field_count = btv->field_count;
			wakeup->top->vb.state = state;
			wake_up(&wakeup->top->vb.done);
		}
		if (NULL != wakeup->bottom && curr->bottom != wakeup->bottom) {
			if (irq_debug > 1)
				printk("bttv%d: wakeup: bottom=%p\n",btv->c.nr,wakeup->bottom);
			wakeup->bottom->vb.ts = ts;
			wakeup->bottom->vb.field_count = btv->field_count;
			wakeup->bottom->vb.state = state;
			wake_up(&wakeup->bottom->vb.done);
		}
	}
}

static void
bttv_irq_wakeup_vbi(struct bttv *btv, struct bttv_buffer *wakeup,
		    unsigned int state)
{
	struct timeval ts;

	if (NULL == wakeup)
		return;

	do_gettimeofday(&ts);
	wakeup->vb.ts = ts;
	wakeup->vb.field_count = btv->field_count;
	wakeup->vb.state = state;
	wake_up(&wakeup->vb.done);
}

static void bttv_irq_timeout(unsigned long data)
{
	struct bttv *btv = (struct bttv *)data;
	struct bttv_buffer_set old,new;
	struct bttv_buffer *ovbi;
	struct bttv_buffer *item;
	unsigned long flags;

	if (bttv_verbose) {
		printk(KERN_INFO "bttv%d: timeout: drop=%d irq=%d/%d, risc=%08x, ",
		       btv->c.nr, btv->framedrop, btv->irq_me, btv->irq_total,
		       btread(BT848_RISC_COUNT));
		bttv_print_irqbits(btread(BT848_INT_STAT),0);
		printk("\n");
	}

	spin_lock_irqsave(&btv->s_lock,flags);

	/* deactivate stuff */
	memset(&new,0,sizeof(new));
	old  = btv->curr;
	ovbi = btv->cvbi;
	btv->curr = new;
	btv->cvbi = NULL;
	btv->loop_irq = 0;
	bttv_buffer_activate_video(btv, &new);
	bttv_buffer_activate_vbi(btv,   NULL);
	bttv_set_dma(btv, 0);

	/* wake up */
	bttv_irq_wakeup_video(btv, &old, &new, STATE_ERROR);
	bttv_irq_wakeup_vbi(btv, ovbi, STATE_ERROR);

	/* cancel all outstanding capture / vbi requests */
	while (!list_empty(&btv->capture)) {
		item = list_entry(btv->capture.next, struct bttv_buffer, vb.queue);
		list_del(&item->vb.queue);
		item->vb.state = STATE_ERROR;
		wake_up(&item->vb.done);
	}
	while (!list_empty(&btv->vcapture)) {
		item = list_entry(btv->vcapture.next, struct bttv_buffer, vb.queue);
		list_del(&item->vb.queue);
		item->vb.state = STATE_ERROR;
		wake_up(&item->vb.done);
	}

	btv->errors++;
	spin_unlock_irqrestore(&btv->s_lock,flags);
}

static void
bttv_irq_wakeup_top(struct bttv *btv)
{
	struct bttv_buffer *wakeup = btv->curr.top;

	if (NULL == wakeup)
		return;

	spin_lock(&btv->s_lock);
	btv->curr.top_irq = 0;
	btv->curr.top = NULL;
	bttv_risc_hook(btv, RISC_SLOT_O_FIELD, NULL, 0);

	do_gettimeofday(&wakeup->vb.ts);
	wakeup->vb.field_count = btv->field_count;
	wakeup->vb.state = STATE_DONE;
	wake_up(&wakeup->vb.done);
	spin_unlock(&btv->s_lock);
}

static inline int is_active(struct btcx_riscmem *risc, u32 rc)
{
	if (rc < risc->dma)
		return 0;
	if (rc > risc->dma + risc->size)
		return 0;
	return 1;
}

static void
bttv_irq_switch_video(struct bttv *btv)
{
	struct bttv_buffer_set new;
	struct bttv_buffer_set old;
	dma_addr_t rc;

	spin_lock(&btv->s_lock);

	/* new buffer set */
	bttv_irq_next_video(btv, &new);
	rc = btread(BT848_RISC_COUNT);
	if ((btv->curr.top    && is_active(&btv->curr.top->top,       rc)) ||
	    (btv->curr.bottom && is_active(&btv->curr.bottom->bottom, rc))) {
		btv->framedrop++;
		if (debug_latency)
			bttv_irq_debug_low_latency(btv, rc);
		spin_unlock(&btv->s_lock);
		return;
	}

	/* switch over */
	old = btv->curr;
	btv->curr = new;
	btv->loop_irq &= ~1;
	bttv_buffer_activate_video(btv, &new);
	bttv_set_dma(btv, 0);

	/* switch input */
	if (UNSET != btv->new_input) {
		video_mux(btv,btv->new_input);
		btv->new_input = UNSET;
	}

	/* wake up finished buffers */
	bttv_irq_wakeup_video(btv, &old, &new, STATE_DONE);
	spin_unlock(&btv->s_lock);
}

static void
bttv_irq_switch_vbi(struct bttv *btv)
{
	struct bttv_buffer *new = NULL;
	struct bttv_buffer *old;
	u32 rc;

	spin_lock(&btv->s_lock);

	if (!list_empty(&btv->vcapture))
		new = list_entry(btv->vcapture.next, struct bttv_buffer, vb.queue);
	old = btv->cvbi;

	rc = btread(BT848_RISC_COUNT);
	if (NULL != old && (is_active(&old->top,    rc) ||
			    is_active(&old->bottom, rc))) {
		btv->framedrop++;
		if (debug_latency)
			bttv_irq_debug_low_latency(btv, rc);
		spin_unlock(&btv->s_lock);
		return;
	}

	/* switch */
	btv->cvbi = new;
	btv->loop_irq &= ~4;
	bttv_buffer_activate_vbi(btv, new);
	bttv_set_dma(btv, 0);

	bttv_irq_wakeup_vbi(btv, old, STATE_DONE);
	spin_unlock(&btv->s_lock);
}

static irqreturn_t bttv_irq(int irq, void *dev_id)
{
	u32 stat,astat;
	u32 dstat;
	int count;
	struct bttv *btv;
	int handled = 0;

	btv=(struct bttv *)dev_id;

	if (btv->custom_irq)
		handled = btv->custom_irq(btv);

	count=0;
	while (1) {
		/* get/clear interrupt status bits */
		stat=btread(BT848_INT_STAT);
		astat=stat&btread(BT848_INT_MASK);
		if (!astat)
			break;
		handled = 1;
		btwrite(stat,BT848_INT_STAT);

		/* get device status bits */
		dstat=btread(BT848_DSTATUS);

		if (irq_debug) {
			printk(KERN_DEBUG "bttv%d: irq loop=%d fc=%d "
			       "riscs=%x, riscc=%08x, ",
			       btv->c.nr, count, btv->field_count,
			       stat>>28, btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			if (stat & BT848_INT_HLOCK)
				printk("   HLOC => %s", (dstat & BT848_DSTATUS_HLOC)
				       ? "yes" : "no");
			if (stat & BT848_INT_VPRES)
				printk("   PRES => %s", (dstat & BT848_DSTATUS_PRES)
				       ? "yes" : "no");
			if (stat & BT848_INT_FMTCHG)
				printk("   NUML => %s", (dstat & BT848_DSTATUS_NUML)
				       ? "625" : "525");
			printk("\n");
		}

		if (astat&BT848_INT_VSYNC)
			btv->field_count++;

		if ((astat & BT848_INT_GPINT) && btv->remote) {
			wake_up(&btv->gpioq);
			bttv_input_irq(btv);
		}

		if (astat & BT848_INT_I2CDONE) {
			btv->i2c_done = stat;
			wake_up(&btv->i2c_queue);
		}

		if ((astat & BT848_INT_RISCI)  &&  (stat & (4<<28)))
			bttv_irq_switch_vbi(btv);

		if ((astat & BT848_INT_RISCI)  &&  (stat & (2<<28)))
			bttv_irq_wakeup_top(btv);

		if ((astat & BT848_INT_RISCI)  &&  (stat & (1<<28)))
			bttv_irq_switch_video(btv);

		if ((astat & BT848_INT_HLOCK)  &&  btv->opt_automute)
			audio_mute(btv, btv->mute);  /* trigger automute */

		if (astat & (BT848_INT_SCERR|BT848_INT_OCERR)) {
			printk(KERN_INFO "bttv%d: %s%s @ %08x,",btv->c.nr,
			       (astat & BT848_INT_SCERR) ? "SCERR" : "",
			       (astat & BT848_INT_OCERR) ? "OCERR" : "",
			       btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			printk("\n");
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}
		if (fdsr && astat & BT848_INT_FDSR) {
			printk(KERN_INFO "bttv%d: FDSR @ %08x\n",
			       btv->c.nr,btread(BT848_RISC_COUNT));
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}

		count++;
		if (count > 4) {

			if (count > 8 || !(astat & BT848_INT_GPINT)) {
				btwrite(0, BT848_INT_MASK);

				printk(KERN_ERR
					   "bttv%d: IRQ lockup, cleared int mask [", btv->c.nr);
			} else {
				printk(KERN_ERR
					   "bttv%d: IRQ lockup, clearing GPINT from int mask [", btv->c.nr);

				btwrite(btread(BT848_INT_MASK) & (-1 ^ BT848_INT_GPINT),
						BT848_INT_MASK);
			};

			bttv_print_irqbits(stat,astat);

			printk("]\n");
		}
	}
	btv->irq_total++;
	if (handled)
		btv->irq_me++;
	return IRQ_RETVAL(handled);
}


/* ----------------------------------------------------------------------- */
/* initialitation                                                          */

static struct video_device *vdev_init(struct bttv *btv,
				      struct video_device *template,
				      char *type)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;
	*vfd = *template;
	vfd->minor   = -1;
	vfd->dev     = &btv->c.pci->dev;
	vfd->release = video_device_release;
	snprintf(vfd->name, sizeof(vfd->name), "BT%d%s %s (%s)",
		 btv->id, (btv->id==848 && btv->revision==0x12) ? "A" : "",
		 type, bttv_tvcards[btv->c.type].name);
	return vfd;
}

static void bttv_unregister_video(struct bttv *btv)
{
	if (btv->video_dev) {
		if (-1 != btv->video_dev->minor)
			video_unregister_device(btv->video_dev);
		else
			video_device_release(btv->video_dev);
		btv->video_dev = NULL;
	}
	if (btv->vbi_dev) {
		if (-1 != btv->vbi_dev->minor)
			video_unregister_device(btv->vbi_dev);
		else
			video_device_release(btv->vbi_dev);
		btv->vbi_dev = NULL;
	}
	if (btv->radio_dev) {
		if (-1 != btv->radio_dev->minor)
			video_unregister_device(btv->radio_dev);
		else
			video_device_release(btv->radio_dev);
		btv->radio_dev = NULL;
	}
}

/* register video4linux devices */
static int __devinit bttv_register_video(struct bttv *btv)
{
	if (no_overlay <= 0) {
		bttv_video_template.type |= VID_TYPE_OVERLAY;
	} else {
		printk("bttv: Overlay support disabled.\n");
	}

	/* video */
	btv->video_dev = vdev_init(btv, &bttv_video_template, "video");
	if (NULL == btv->video_dev)
		goto err;
	if (video_register_device(btv->video_dev,VFL_TYPE_GRABBER,video_nr)<0)
		goto err;
	printk(KERN_INFO "bttv%d: registered device video%d\n",
	       btv->c.nr,btv->video_dev->minor & 0x1f);
	if (class_device_create_file(&btv->video_dev->class_dev,
				     &class_device_attr_card)<0) {
		printk(KERN_ERR "bttv%d: class_device_create_file 'card' "
		       "failed\n", btv->c.nr);
		goto err;
	}

	/* vbi */
	btv->vbi_dev = vdev_init(btv, &bttv_vbi_template, "vbi");
	if (NULL == btv->vbi_dev)
		goto err;
	if (video_register_device(btv->vbi_dev,VFL_TYPE_VBI,vbi_nr)<0)
		goto err;
	printk(KERN_INFO "bttv%d: registered device vbi%d\n",
	       btv->c.nr,btv->vbi_dev->minor & 0x1f);

	if (!btv->has_radio)
		return 0;
	/* radio */
	btv->radio_dev = vdev_init(btv, &radio_template, "radio");
	if (NULL == btv->radio_dev)
		goto err;
	if (video_register_device(btv->radio_dev, VFL_TYPE_RADIO,radio_nr)<0)
		goto err;
	printk(KERN_INFO "bttv%d: registered device radio%d\n",
	       btv->c.nr,btv->radio_dev->minor & 0x1f);

	/* all done */
	return 0;

 err:
	bttv_unregister_video(btv);
	return -1;
}


/* on OpenFirmware machines (PowerMac at least), PCI memory cycle */
/* response on cards with no firmware is not enabled by OF */
static void pci_set_command(struct pci_dev *dev)
{
#if defined(__powerpc__)
	unsigned int cmd;

	pci_read_config_dword(dev, PCI_COMMAND, &cmd);
	cmd = (cmd | PCI_COMMAND_MEMORY );
	pci_write_config_dword(dev, PCI_COMMAND, cmd);
#endif
}

static int __devinit bttv_probe(struct pci_dev *dev,
				const struct pci_device_id *pci_id)
{
	int result;
	unsigned char lat;
	struct bttv *btv;

	if (bttv_num == BTTV_MAX)
		return -ENOMEM;
	printk(KERN_INFO "bttv: Bt8xx card found (%d).\n", bttv_num);
	btv=&bttvs[bttv_num];
	memset(btv,0,sizeof(*btv));
	btv->c.nr  = bttv_num;
	sprintf(btv->c.name,"bttv%d",btv->c.nr);

	/* initialize structs / fill in defaults */
	mutex_init(&btv->lock);
	spin_lock_init(&btv->s_lock);
	spin_lock_init(&btv->gpio_lock);
	init_waitqueue_head(&btv->gpioq);
	init_waitqueue_head(&btv->i2c_queue);
	INIT_LIST_HEAD(&btv->c.subs);
	INIT_LIST_HEAD(&btv->capture);
	INIT_LIST_HEAD(&btv->vcapture);
	v4l2_prio_init(&btv->prio);

	init_timer(&btv->timeout);
	btv->timeout.function = bttv_irq_timeout;
	btv->timeout.data     = (unsigned long)btv;

	btv->i2c_rc = -1;
	btv->tuner_type  = UNSET;
	btv->new_input   = UNSET;
	btv->has_radio=radio[btv->c.nr];

	/* pci stuff (init, get irq/mmio, ... */
	btv->c.pci = dev;
	btv->id  = dev->device;
	if (pci_enable_device(dev)) {
		printk(KERN_WARNING "bttv%d: Can't enable device.\n",
		       btv->c.nr);
		return -EIO;
	}
	if (pci_set_dma_mask(dev, DMA_32BIT_MASK)) {
		printk(KERN_WARNING "bttv%d: No suitable DMA available.\n",
		       btv->c.nr);
		return -EIO;
	}
	if (!request_mem_region(pci_resource_start(dev,0),
				pci_resource_len(dev,0),
				btv->c.name)) {
		printk(KERN_WARNING "bttv%d: can't request iomem (0x%llx).\n",
		       btv->c.nr,
		       (unsigned long long)pci_resource_start(dev,0));
		return -EBUSY;
	}
	pci_set_master(dev);
	pci_set_command(dev);
	pci_set_drvdata(dev,btv);

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &btv->revision);
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	printk(KERN_INFO "bttv%d: Bt%d (rev %d) at %s, ",
	       bttv_num,btv->id, btv->revision, pci_name(dev));
	printk("irq: %d, latency: %d, mmio: 0x%llx\n",
	       btv->c.pci->irq, lat,
	       (unsigned long long)pci_resource_start(dev,0));
	schedule();

	btv->bt848_mmio = ioremap(pci_resource_start(dev, 0), 0x1000);
	if (NULL == btv->bt848_mmio) {
		printk("bttv%d: ioremap() failed\n", btv->c.nr);
		result = -EIO;
		goto fail1;
	}

	/* identify card */
	bttv_idcard(btv);

	/* disable irqs, register irq handler */
	btwrite(0, BT848_INT_MASK);
	result = request_irq(btv->c.pci->irq, bttv_irq,
			     IRQF_SHARED | IRQF_DISABLED,btv->c.name,(void *)btv);
	if (result < 0) {
		printk(KERN_ERR "bttv%d: can't get IRQ %d\n",
		       bttv_num,btv->c.pci->irq);
		goto fail1;
	}

	if (0 != bttv_handle_chipset(btv)) {
		result = -EIO;
		goto fail2;
	}

	/* init options from insmod args */
	btv->opt_combfilter = combfilter;
	btv->opt_lumafilter = lumafilter;
	btv->opt_automute   = automute;
	btv->opt_chroma_agc = chroma_agc;
	btv->opt_adc_crush  = adc_crush;
	btv->opt_vcr_hack   = vcr_hack;
	btv->opt_whitecrush_upper  = whitecrush_upper;
	btv->opt_whitecrush_lower  = whitecrush_lower;
	btv->opt_uv_ratio   = uv_ratio;
	btv->opt_full_luma_range   = full_luma_range;
	btv->opt_coring     = coring;

	/* fill struct bttv with some useful defaults */
	btv->init.btv         = btv;
	btv->init.ov.w.width  = 320;
	btv->init.ov.w.height = 240;
	btv->init.fmt         = format_by_palette(VIDEO_PALETTE_RGB24);
	btv->init.width       = 320;
	btv->init.height      = 240;
	btv->input = 0;

	/* initialize hardware */
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"pre-init");

	bttv_risc_init_main(btv);
	init_bt848(btv);

	/* gpio */
	btwrite(0x00, BT848_GPIO_REG_INP);
	btwrite(0x00, BT848_GPIO_OUT_EN);
	if (bttv_verbose)
		bttv_gpio_tracking(btv,"init");

	/* needs to be done before i2c is registered */
	bttv_init_card1(btv);

	/* register i2c + gpio */
	init_bttv_i2c(btv);

	/* some card-specific stuff (needs working i2c) */
	bttv_init_card2(btv);
	init_irqreg(btv);

	/* register video4linux + input */
	if (!bttv_tvcards[btv->c.type].no_video) {
		bttv_register_video(btv);
		bt848_bright(btv,32768);
		bt848_contrast(btv,32768);
		bt848_hue(btv,32768);
		bt848_sat(btv,32768);
		audio_mute(btv, 1);
		set_input(btv,0);
		bttv_crop_reset(&btv->crop[0], btv->tvnorm);
		btv->crop[1] = btv->crop[0]; /* current = default */
		disclaim_vbi_lines(btv);
		disclaim_video_lines(btv);
	}

	/* add subdevices */
	if (bttv_tvcards[btv->c.type].has_dvb)
		bttv_sub_add_device(&btv->c, "dvb");

	bttv_input_init(btv);

	/* everything is fine */
	bttv_num++;
	return 0;

 fail2:
	free_irq(btv->c.pci->irq,btv);

 fail1:
	if (btv->bt848_mmio)
		iounmap(btv->bt848_mmio);
	release_mem_region(pci_resource_start(btv->c.pci,0),
			   pci_resource_len(btv->c.pci,0));
	pci_set_drvdata(dev,NULL);
	return result;
}

static void __devexit bttv_remove(struct pci_dev *pci_dev)
{
	struct bttv *btv = pci_get_drvdata(pci_dev);

	if (bttv_verbose)
		printk("bttv%d: unloading\n",btv->c.nr);

	/* shutdown everything (DMA+IRQs) */
	btand(~15, BT848_GPIO_DMA_CTL);
	btwrite(0, BT848_INT_MASK);
	btwrite(~0x0, BT848_INT_STAT);
	btwrite(0x0, BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"cleanup");

	/* tell gpio modules we are leaving ... */
	btv->shutdown=1;
	wake_up(&btv->gpioq);
	bttv_input_fini(btv);
	bttv_sub_del_devices(&btv->c);

	/* unregister i2c_bus + input */
	fini_bttv_i2c(btv);

	/* unregister video4linux */
	bttv_unregister_video(btv);

	/* free allocated memory */
	btcx_riscmem_free(btv->c.pci,&btv->main);

	/* free ressources */
	free_irq(btv->c.pci->irq,btv);
	iounmap(btv->bt848_mmio);
	release_mem_region(pci_resource_start(btv->c.pci,0),
			   pci_resource_len(btv->c.pci,0));

	pci_set_drvdata(pci_dev, NULL);
	return;
}

#ifdef CONFIG_PM
static int bttv_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct bttv *btv = pci_get_drvdata(pci_dev);
	struct bttv_buffer_set idle;
	unsigned long flags;

	dprintk("bttv%d: suspend %d\n", btv->c.nr, state.event);

	/* stop dma + irqs */
	spin_lock_irqsave(&btv->s_lock,flags);
	memset(&idle, 0, sizeof(idle));
	btv->state.video = btv->curr;
	btv->state.vbi   = btv->cvbi;
	btv->state.loop_irq = btv->loop_irq;
	btv->curr = idle;
	btv->loop_irq = 0;
	bttv_buffer_activate_video(btv, &idle);
	bttv_buffer_activate_vbi(btv, NULL);
	bttv_set_dma(btv, 0);
	btwrite(0, BT848_INT_MASK);
	spin_unlock_irqrestore(&btv->s_lock,flags);

	/* save bt878 state */
	btv->state.gpio_enable = btread(BT848_GPIO_OUT_EN);
	btv->state.gpio_data   = gpio_read();

	/* save pci state */
	pci_save_state(pci_dev);
	if (0 != pci_set_power_state(pci_dev, pci_choose_state(pci_dev, state))) {
		pci_disable_device(pci_dev);
		btv->state.disabled = 1;
	}
	return 0;
}

static int bttv_resume(struct pci_dev *pci_dev)
{
	struct bttv *btv = pci_get_drvdata(pci_dev);
	unsigned long flags;
	int err;

	dprintk("bttv%d: resume\n", btv->c.nr);

	/* restore pci state */
	if (btv->state.disabled) {
		err=pci_enable_device(pci_dev);
		if (err) {
			printk(KERN_WARNING "bttv%d: Can't enable device.\n",
								btv->c.nr);
			return err;
		}
		btv->state.disabled = 0;
	}
	err=pci_set_power_state(pci_dev, PCI_D0);
	if (err) {
		pci_disable_device(pci_dev);
		printk(KERN_WARNING "bttv%d: Can't enable device.\n",
							btv->c.nr);
		btv->state.disabled = 1;
		return err;
	}

	pci_restore_state(pci_dev);

	/* restore bt878 state */
	bttv_reinit_bt848(btv);
	gpio_inout(0xffffff, btv->state.gpio_enable);
	gpio_write(btv->state.gpio_data);

	/* restart dma */
	spin_lock_irqsave(&btv->s_lock,flags);
	btv->curr = btv->state.video;
	btv->cvbi = btv->state.vbi;
	btv->loop_irq = btv->state.loop_irq;
	bttv_buffer_activate_video(btv, &btv->curr);
	bttv_buffer_activate_vbi(btv, btv->cvbi);
	bttv_set_dma(btv, 0);
	spin_unlock_irqrestore(&btv->s_lock,flags);
	return 0;
}
#endif

static struct pci_device_id bttv_pci_tbl[] = {
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT848,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT849,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT878,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT879,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, bttv_pci_tbl);

static struct pci_driver bttv_pci_driver = {
	.name     = "bttv",
	.id_table = bttv_pci_tbl,
	.probe    = bttv_probe,
	.remove   = __devexit_p(bttv_remove),
#ifdef CONFIG_PM
	.suspend  = bttv_suspend,
	.resume   = bttv_resume,
#endif
};

static int bttv_init_module(void)
{
	int ret;

	bttv_num = 0;

	printk(KERN_INFO "bttv: driver version %d.%d.%d loaded\n",
	       (BTTV_VERSION_CODE >> 16) & 0xff,
	       (BTTV_VERSION_CODE >> 8) & 0xff,
	       BTTV_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "bttv: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsize < 0 || gbufsize > BTTV_MAX_FBUF)
		gbufsize = BTTV_MAX_FBUF;
	gbufsize = (gbufsize + PAGE_SIZE - 1) & PAGE_MASK;
	if (bttv_verbose)
		printk(KERN_INFO "bttv: using %d buffers with %dk (%d pages) each for capture\n",
		       gbuffers, gbufsize >> 10, gbufsize >> PAGE_SHIFT);

	bttv_check_chipset();

	ret = bus_register(&bttv_sub_bus_type);
	if (ret < 0) {
		printk(KERN_WARNING "bttv: bus_register error: %d\n", ret);
		return ret;
	}
	return pci_register_driver(&bttv_pci_driver);
}

static void bttv_cleanup_module(void)
{
	pci_unregister_driver(&bttv_pci_driver);
	bus_unregister(&bttv_sub_bus_type);
	return;
}

module_init(bttv_init_module);
module_exit(bttv_cleanup_module);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
