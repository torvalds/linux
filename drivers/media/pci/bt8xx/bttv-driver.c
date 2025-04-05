// SPDX-License-Identifier: GPL-2.0-or-later
/*

    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97,98 Ralph  Metzler <rjkm@thp.uni-koeln.de>
			   & Marcus Metzler <mocm@thp.uni-koeln.de>
    (c) 1999-2002 Gerd Knorr <kraxel@bytesex.org>

    some v4l2 code lines are taken from Justin's bttv2 driver which is
    (c) 2000 Justin Schoeman <justin@suntiger.ee.up.ac.za>

    V4L1 removal from:
    (c) 2005-2006 Nickolay V. Shmyrev <nshmyrev@yandex.ru>

    Fixes to be fully V4L2 compliant by
    (c) 2006 Mauro Carvalho Chehab <mchehab@kernel.org>

    Cropping and overscan support
    Copyright (C) 2005, 2006 Michael H. Schimek <mschimek@gmx.at>
    Sponsored by OPQ Systems AB

*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include "bttvp.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/i2c/tvaudio.h>
#include <media/drv-intf/msp3400.h>

#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/byteorder.h>

#include <media/i2c/saa6588.h>

#define BTTV_VERSION "0.9.19"

unsigned int bttv_num;			/* number of Bt848s in use */
struct bttv *bttvs[BTTV_MAX];

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

static int video_nr[BTTV_MAX] = { [0 ... (BTTV_MAX-1)] = -1 };
static int radio_nr[BTTV_MAX] = { [0 ... (BTTV_MAX-1)] = -1 };
static int vbi_nr[BTTV_MAX] = { [0 ... (BTTV_MAX-1)] = -1 };
static int debug_latency;
static int disable_ir;

static unsigned int fdsr;

/* options */
static unsigned int combfilter;
static unsigned int lumafilter;
static unsigned int automute    = 1;
static unsigned int chroma_agc;
static unsigned int agc_crush   = 1;
static unsigned int whitecrush_upper = 0xCF;
static unsigned int whitecrush_lower = 0x7F;
static unsigned int vcr_hack;
static unsigned int irq_iswitch;
static unsigned int uv_ratio    = 50;
static unsigned int full_luma_range;
static unsigned int coring;

/* API features (turn on/off stuff for testing) */
static unsigned int v4l2        = 1;

/* insmod args */
module_param(bttv_verbose,      int, 0644);
module_param(bttv_gpio,         int, 0644);
module_param(bttv_debug,        int, 0644);
module_param(irq_debug,         int, 0644);
module_param(debug_latency,     int, 0644);
module_param(disable_ir,        int, 0444);

module_param(fdsr,              int, 0444);
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
module_param(agc_crush,         int, 0444);
module_param(whitecrush_upper,  int, 0444);
module_param(whitecrush_lower,  int, 0444);
module_param(vcr_hack,          int, 0444);
module_param(uv_ratio,          int, 0444);
module_param(full_luma_range,   int, 0444);
module_param(coring,            int, 0444);

module_param_array(radio,       int, NULL, 0444);
module_param_array(video_nr,    int, NULL, 0444);
module_param_array(radio_nr,    int, NULL, 0444);
module_param_array(vbi_nr,      int, NULL, 0444);

MODULE_PARM_DESC(radio, "The TV card supports radio, default is 0 (no)");
MODULE_PARM_DESC(bigendian, "byte order of the framebuffer, default is native endian");
MODULE_PARM_DESC(bttv_verbose, "verbose startup messages, default is 1 (yes)");
MODULE_PARM_DESC(bttv_gpio, "log gpio changes, default is 0 (no)");
MODULE_PARM_DESC(bttv_debug, "debug messages, default is 0 (no)");
MODULE_PARM_DESC(irq_debug, "irq handler debug messages, default is 0 (no)");
MODULE_PARM_DESC(disable_ir, "disable infrared remote support");
MODULE_PARM_DESC(gbuffers, "number of capture buffers. range 2-32, default 8");
MODULE_PARM_DESC(gbufsize, "size of the capture buffers, default is 0x208000");
MODULE_PARM_DESC(reset_crop, "reset cropping parameters at open(), default is 1 (yes) for compatibility with older applications");
MODULE_PARM_DESC(automute, "mute audio on bad/missing video signal, default is 1 (yes)");
MODULE_PARM_DESC(chroma_agc, "enables the AGC of chroma signal, default is 0 (no)");
MODULE_PARM_DESC(agc_crush, "enables the luminance AGC crush, default is 1 (yes)");
MODULE_PARM_DESC(whitecrush_upper, "sets the white crush upper value, default is 207");
MODULE_PARM_DESC(whitecrush_lower, "sets the white crush lower value, default is 127");
MODULE_PARM_DESC(vcr_hack, "enables the VCR hack (improves synch on poor VCR tapes), default is 0 (no)");
MODULE_PARM_DESC(irq_iswitch, "switch inputs in irq handler");
MODULE_PARM_DESC(uv_ratio, "ratio between u and v gains, default is 50");
MODULE_PARM_DESC(full_luma_range, "use the full luma range, default is 0 (no)");
MODULE_PARM_DESC(coring, "set the luma coring level, default is 0 (no)");
MODULE_PARM_DESC(video_nr, "video device numbers");
MODULE_PARM_DESC(vbi_nr, "vbi device numbers");
MODULE_PARM_DESC(radio_nr, "radio device numbers");

MODULE_DESCRIPTION("bttv - v4l/v4l2 driver module for bt848/878 based cards");
MODULE_AUTHOR("Ralph Metzler & Marcus Metzler & Gerd Knorr");
MODULE_LICENSE("GPL");
MODULE_VERSION(BTTV_VERSION);

#define V4L2_CID_PRIVATE_COMBFILTER		(V4L2_CID_USER_BTTV_BASE + 0)
#define V4L2_CID_PRIVATE_AUTOMUTE		(V4L2_CID_USER_BTTV_BASE + 1)
#define V4L2_CID_PRIVATE_LUMAFILTER		(V4L2_CID_USER_BTTV_BASE + 2)
#define V4L2_CID_PRIVATE_AGC_CRUSH		(V4L2_CID_USER_BTTV_BASE + 3)
#define V4L2_CID_PRIVATE_VCR_HACK		(V4L2_CID_USER_BTTV_BASE + 4)
#define V4L2_CID_PRIVATE_WHITECRUSH_LOWER	(V4L2_CID_USER_BTTV_BASE + 5)
#define V4L2_CID_PRIVATE_WHITECRUSH_UPPER	(V4L2_CID_USER_BTTV_BASE + 6)
#define V4L2_CID_PRIVATE_UV_RATIO		(V4L2_CID_USER_BTTV_BASE + 7)
#define V4L2_CID_PRIVATE_FULL_LUMA_RANGE	(V4L2_CID_USER_BTTV_BASE + 8)
#define V4L2_CID_PRIVATE_CORING			(V4L2_CID_USER_BTTV_BASE + 9)

/* ----------------------------------------------------------------------- */
/* sysfs                                                                   */

static ssize_t card_show(struct device *cd,
			 struct device_attribute *attr, char *buf)
{
	struct video_device *vfd = to_video_device(cd);
	struct bttv *btv = video_get_drvdata(vfd);
	return sprintf(buf, "%d\n", btv ? btv->c.type : UNSET);
}
static DEVICE_ATTR_RO(card);

/* ----------------------------------------------------------------------- */
/* dvb auto-load setup                                                     */
#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	request_module("dvb-bt8xx");
}

static void request_modules(struct bttv *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct bttv *dev)
{
	flush_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev) do {} while(0)
#endif /* CONFIG_MODULES */


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
   extraheight	Added to sheight for cropcap.bounds.height only
   videostart0	ITU-R frame line number of the line corresponding
		to vdelay in the first field. */
#define CROPCAP(minhdelayx1, hdelayx1, swidth, totalwidth, sqwidth,	 \
		vdelay, sheight, extraheight, videostart0)		 \
	.cropcap.bounds.left = minhdelayx1,				 \
	/* * 2 because vertically we count field lines times two, */	 \
	/* e.g. 23 * 2 to 23 * 2 + 576 in PAL-BGHI defrect. */		 \
	.cropcap.bounds.top = (videostart0) * 2 - (vdelay) + MIN_VDELAY, \
	/* 4 is a safety margin at the end of the line. */		 \
	.cropcap.bounds.width = (totalwidth) - (minhdelayx1) - 4,	 \
	.cropcap.bounds.height = (sheight) + (extraheight) + (vdelay) -	 \
				 MIN_VDELAY,				 \
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
			/* bt878 (and bt848?) can capture another
			   line below active video. */
			/* extraheight */ 2,
			/* videostart0 */ 23)
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
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
			/* extraheight */ 0,
			/* videostart0 */ 23)
	}
};
static const unsigned int BTTV_TVNORMS = ARRAY_SIZE(bttv_tvnorms);

/* ----------------------------------------------------------------------- */
/* bttv format list
   packed pixel formats must come first */
static const struct bttv_format formats[] = {
	{
		.fourcc   = V4L2_PIX_FMT_GREY,
		.btformat = BT848_COLOR_FMT_Y8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_HI240,
		.btformat = BT848_COLOR_FMT_RGB8,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_PACKED | FORMAT_FLAGS_DITHER,
	},{
		.fourcc   = V4L2_PIX_FMT_RGB555,
		.btformat = BT848_COLOR_FMT_RGB15,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_RGB555X,
		.btformat = BT848_COLOR_FMT_RGB15,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_RGB565,
		.btformat = BT848_COLOR_FMT_RGB16,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_RGB565X,
		.btformat = BT848_COLOR_FMT_RGB16,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_BGR24,
		.btformat = BT848_COLOR_FMT_RGB24,
		.depth    = 24,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_BGR32,
		.btformat = BT848_COLOR_FMT_RGB32,
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_RGB32,
		.btformat = BT848_COLOR_FMT_RGB32,
		.btswap   = 0x0f, /* byte+word swap */
		.depth    = 32,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.btformat = BT848_COLOR_FMT_YUY2,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.btformat = BT848_COLOR_FMT_YUY2,
		.btswap   = 0x03, /* byteswap */
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PACKED,
	},{
		.fourcc   = V4L2_PIX_FMT_YUV422P,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 16,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 1,
		.vshift   = 0,
	},{
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 1,
		.vshift   = 1,
	},{
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.btformat = BT848_COLOR_FMT_YCrCb422,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		.hshift   = 1,
		.vshift   = 1,
	},{
		.fourcc   = V4L2_PIX_FMT_YUV411P,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 12,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 2,
		.vshift   = 0,
	},{
		.fourcc   = V4L2_PIX_FMT_YUV410,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR,
		.hshift   = 2,
		.vshift   = 2,
	},{
		.fourcc   = V4L2_PIX_FMT_YVU410,
		.btformat = BT848_COLOR_FMT_YCrCb411,
		.depth    = 9,
		.flags    = FORMAT_FLAGS_PLANAR | FORMAT_FLAGS_CrCb,
		.hshift   = 2,
		.vshift   = 2,
	},{
		.fourcc   = -1,
		.btformat = BT848_COLOR_FMT_RAW,
		.depth    = 8,
		.flags    = FORMAT_FLAGS_RAW,
	}
};
static const unsigned int FORMATS = ARRAY_SIZE(formats);

/* ----------------------------------------------------------------------- */
/* resource management                                                     */

/*
   RESOURCE_    allocated by                freed by

   VIDEO_READ   bttv_read 1)                bttv_read 2)

   VIDEO_STREAM VIDIOC_STREAMON             VIDIOC_STREAMOFF
		 VIDIOC_QBUF 1)              bttv_release
		 VIDIOCMCAPTURE 1)

   VBI		 VIDIOC_STREAMON             VIDIOC_STREAMOFF
		 VIDIOC_QBUF 1)              bttv_release
		 bttv_read, bttv_poll 1) 3)

   1) The resource must be allocated when we enter buffer prepare functions
      and remain allocated while buffers are in the DMA queue.
   2) This is a single frame read.
   3) This is a continuous read, implies VIDIOC_STREAMON.

   Note this driver permits video input and standard changes regardless if
   resources are allocated.
*/

#define VBI_RESOURCES (RESOURCE_VBI)
#define VIDEO_RESOURCES (RESOURCE_VIDEO_READ | \
			 RESOURCE_VIDEO_STREAM)

int check_alloc_btres_lock(struct bttv *btv, int bit)
{
	int xbits; /* mutual exclusive resources */

	xbits = bit;
	if (bit & (RESOURCE_VIDEO_READ | RESOURCE_VIDEO_STREAM))
		xbits |= RESOURCE_VIDEO_READ | RESOURCE_VIDEO_STREAM;

	/* is it free? */
	if (btv->resources & xbits) {
		/* no, someone else uses it */
		goto fail;
	}

	if ((bit & VIDEO_RESOURCES)
	    && 0 == (btv->resources & VIDEO_RESOURCES)) {
		/* Do crop - use current, don't - use default parameters. */
		__s32 top = btv->crop[!!btv->do_crop].rect.top;

		if (btv->vbi_end > top)
			goto fail;

		/* We cannot capture the same line as video and VBI data.
		   Claim scan lines crop[].rect.top to bottom. */
		btv->crop_start = top;
	} else if (bit & VBI_RESOURCES) {
		__s32 end = btv->vbi_fmt.end;

		if (end > btv->crop_start)
			goto fail;

		/* Claim scan lines above btv->vbi_fmt.end. */
		btv->vbi_end = end;
	}

	/* it's free, grab it */
	btv->resources |= bit;
	return 1;

 fail:
	return 0;
}

static
int check_btres(struct bttv *btv, int bit)
{
	return (btv->resources & bit);
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

void free_btres_lock(struct bttv *btv, int bits)
{
	if ((btv->resources & bits) != bits) {
		/* trying to free resources not allocated by us ... */
		pr_err("BUG! (btres)\n");
	}
	btv->resources &= ~bits;

	bits = btv->resources;

	if (0 == (bits & VIDEO_RESOURCES))
		disclaim_video_lines(btv);

	if (0 == (bits & VBI_RESOURCES))
		disclaim_vbi_lines(btv);
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
		dprintk("%d: PLL: no change required\n", btv->c.nr);
		return;
	}

	if (btv->pll.pll_ifreq == btv->pll.pll_ofreq) {
		/* no PLL needed */
		if (btv->pll.pll_current == 0)
			return;
		if (bttv_verbose)
			pr_info("%d: PLL can sleep, using XTAL (%d)\n",
				btv->c.nr, btv->pll.pll_ifreq);
		btwrite(0x00,BT848_TGCTRL);
		btwrite(0x00,BT848_PLL_XCI);
		btv->pll.pll_current = 0;
		return;
	}

	if (bttv_verbose)
		pr_info("%d: Setting PLL: %d => %d (needs up to 100ms)\n",
			btv->c.nr,
			btv->pll.pll_ifreq, btv->pll.pll_ofreq);
	set_pll_freq(btv, btv->pll.pll_ifreq, btv->pll.pll_ofreq);

	for (i=0; i<10; i++) {
		/*  Let other people run while the PLL stabilizes */
		msleep(10);

		if (btread(BT848_DSTATUS) & BT848_DSTATUS_PLOCK) {
			btwrite(0,BT848_DSTATUS);
		} else {
			btwrite(0x08,BT848_TGCTRL);
			btv->pll.pll_current = btv->pll.pll_ofreq;
			if (bttv_verbose)
				pr_info("PLL set ok\n");
			return;
		}
	}
	btv->pll.pll_current = -1;
	if (bttv_verbose)
		pr_info("Setting PLL failed\n");
	return;
}

/* used to switch between the bt848's analog/digital video capture modes */
static void bt848A_set_timing(struct bttv *btv)
{
	int i, len;
	int table_idx = bttv_tvnorms[btv->tvnorm].sram;
	int fsc       = bttv_tvnorms[btv->tvnorm].Fsc;

	if (btv->input == btv->dig) {
		dprintk("%d: load digital timing table (table_idx=%d)\n",
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

	// printk("set bright: %d\n", bright); // DEBUG
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
	mux = bttv_muxsel(btv, input);
	btaor(mux<<5, ~(3<<5), BT848_IFORM);
	dprintk("%d: video mux: input=%d mux=%d\n", btv->c.nr, input, mux);

	/* card specific hook */
	if(bttv_tvcards[btv->c.type].muxsel_hook)
		bttv_tvcards[btv->c.type].muxsel_hook (btv, input);
	return 0;
}

static char *audio_modes[] = {
	"audio: tuner", "audio: radio", "audio: extern",
	"audio: intern", "audio: mute"
};

static void
audio_mux_gpio(struct bttv *btv, int input, int mute)
{
	int gpio_val, signal, mute_gpio;

	gpio_inout(bttv_tvcards[btv->c.type].gpiomask,
		   bttv_tvcards[btv->c.type].gpiomask);
	signal = btread(BT848_DSTATUS) & BT848_DSTATUS_HLOC;

	/* automute */
	mute_gpio = mute || (btv->opt_automute && (!signal || !btv->users)
				&& !btv->has_radio_tuner);

	if (mute_gpio)
		gpio_val = bttv_tvcards[btv->c.type].gpiomute;
	else
		gpio_val = bttv_tvcards[btv->c.type].gpiomux[input];

	switch (btv->c.type) {
	case BTTV_BOARD_VOODOOTV_FM:
	case BTTV_BOARD_VOODOOTV_200:
		gpio_val = bttv_tda9880_setnorm(btv, gpio_val);
		break;

	default:
		gpio_bits(bttv_tvcards[btv->c.type].gpiomask, gpio_val);
	}

	if (bttv_gpio)
		bttv_gpio_tracking(btv, audio_modes[mute_gpio ? 4 : input]);
}

static int
audio_mute(struct bttv *btv, int mute)
{
	struct v4l2_ctrl *ctrl;

	audio_mux_gpio(btv, btv->audio_input, mute);

	if (btv->sd_msp34xx) {
		ctrl = v4l2_ctrl_find(btv->sd_msp34xx->ctrl_handler, V4L2_CID_AUDIO_MUTE);
		if (ctrl)
			v4l2_ctrl_s_ctrl(ctrl, mute);
	}
	if (btv->sd_tvaudio) {
		ctrl = v4l2_ctrl_find(btv->sd_tvaudio->ctrl_handler, V4L2_CID_AUDIO_MUTE);
		if (ctrl)
			v4l2_ctrl_s_ctrl(ctrl, mute);
	}
	if (btv->sd_tda7432) {
		ctrl = v4l2_ctrl_find(btv->sd_tda7432->ctrl_handler, V4L2_CID_AUDIO_MUTE);
		if (ctrl)
			v4l2_ctrl_s_ctrl(ctrl, mute);
	}
	return 0;
}

static int
audio_input(struct bttv *btv, int input)
{
	audio_mux_gpio(btv, input, btv->mute);

	if (btv->sd_msp34xx) {
		u32 in;

		/* Note: the inputs tuner/radio/extern/intern are translated
		   to msp routings. This assumes common behavior for all msp3400
		   based TV cards. When this assumption fails, then the
		   specific MSP routing must be added to the card table.
		   For now this is sufficient. */
		switch (input) {
		case TVAUDIO_INPUT_RADIO:
			/* Some boards need the msp do to the radio demod */
			if (btv->radio_uses_msp_demodulator) {
				in = MSP_INPUT_DEFAULT;
				break;
			}
			in = MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_EXTERN:
			in = MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_INTERN:
			/* Yes, this is the same input as for RADIO. I doubt
			   if this is ever used. The only board with an INTERN
			   input is the BTTV_BOARD_AVERMEDIA98. I wonder how
			   that was tested. My guess is that the whole INTERN
			   input does not work. */
			in = MSP_INPUT(MSP_IN_SCART2, MSP_IN_TUNER1,
				    MSP_DSP_IN_SCART, MSP_DSP_IN_SCART);
			break;
		case TVAUDIO_INPUT_TUNER:
		default:
			/* This is the only card that uses TUNER2, and afaik,
			   is the only difference between the VOODOOTV_FM
			   and VOODOOTV_200 */
			if (btv->c.type == BTTV_BOARD_VOODOOTV_200)
				in = MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER2, \
					MSP_DSP_IN_TUNER, MSP_DSP_IN_TUNER);
			else
				in = MSP_INPUT_DEFAULT;
			break;
		}
		v4l2_subdev_call(btv->sd_msp34xx, audio, s_routing,
			       in, MSP_OUTPUT_DEFAULT, 0);
	}
	if (btv->sd_tvaudio) {
		v4l2_subdev_call(btv->sd_tvaudio, audio, s_routing,
				 input, 0, 0);
	}
	return 0;
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
			(max_t(unsigned int, 48, c->rect.width >> 4) + 3) & ~3;
		c->min_scaled_height =
			max_t(unsigned int, 32, c->rect.height >> 4);
	}

	c->max_scaled_width  = c->rect.width & ~3;
	c->max_scaled_height = c->rect.height;
}

static void
bttv_crop_reset(struct bttv_crop *c, unsigned int norm)
{
	c->rect = bttv_tvnorms[norm].cropcap.defrect;
	bttv_crop_calc_limits(c);
}

/* Call with btv->lock down. */
static int
set_tvnorm(struct bttv *btv, unsigned int norm)
{
	const struct bttv_tvnorm *tvnorm;
	v4l2_std_id id;

	WARN_ON(norm >= BTTV_TVNORMS);
	WARN_ON(btv->tvnorm >= BTTV_TVNORMS);

	tvnorm = &bttv_tvnorms[norm];

	if (memcmp(&bttv_tvnorms[btv->tvnorm].cropcap, &tvnorm->cropcap,
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
	case BTTV_BOARD_VOODOOTV_200:
		bttv_tda9880_setnorm(btv, gpio_read());
		break;
	}
	id = tvnorm->v4l2_id;
	bttv_call_all(btv, video, s_std, id);

	return 0;
}

/* Call with btv->lock down. */
static void
set_input(struct bttv *btv, unsigned int input, unsigned int norm)
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
	btv->audio_input = (btv->tuner_type != TUNER_ABSENT && input == 0) ?
				TVAUDIO_INPUT_TUNER : TVAUDIO_INPUT_EXTERN;
	audio_input(btv, btv->audio_input);
	set_tvnorm(btv, norm);
}

void init_irqreg(struct bttv *btv)
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
			BT848_INT_RISCI | BT848_INT_OCERR |
			BT848_INT_FMTCHG|BT848_INT_HLOCK|
			BT848_INT_I2CDONE,
			BT848_INT_MASK);
	}
}

static void init_bt848(struct bttv *btv)
{
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

	btwrite(0x20, BT848_E_VSCALE_HI);
	btwrite(0x20, BT848_O_VSCALE_HI);

	v4l2_ctrl_handler_setup(&btv->ctrl_handler);

	/* interrupt */
	init_irqreg(btv);
}

static void bttv_reinit_bt848(struct bttv *btv)
{
	unsigned long flags;

	if (bttv_verbose)
		pr_info("%d: reset, reinitialize\n", btv->c.nr);
	spin_lock_irqsave(&btv->s_lock,flags);
	btv->errors=0;
	bttv_set_dma(btv,0);
	spin_unlock_irqrestore(&btv->s_lock,flags);

	init_bt848(btv);
	btv->pll.pll_current = -1;
	set_input(btv, btv->input, btv->tvnorm);
}

static int bttv_s_ctrl(struct v4l2_ctrl *c)
{
	struct bttv *btv = container_of(c->handler, struct bttv, ctrl_handler);
	int val;

	switch (c->id) {
	case V4L2_CID_BRIGHTNESS:
		bt848_bright(btv, c->val);
		break;
	case V4L2_CID_HUE:
		bt848_hue(btv, c->val);
		break;
	case V4L2_CID_CONTRAST:
		bt848_contrast(btv, c->val);
		break;
	case V4L2_CID_SATURATION:
		bt848_sat(btv, c->val);
		break;
	case V4L2_CID_COLOR_KILLER:
		if (c->val) {
			btor(BT848_SCLOOP_CKILL, BT848_E_SCLOOP);
			btor(BT848_SCLOOP_CKILL, BT848_O_SCLOOP);
		} else {
			btand(~BT848_SCLOOP_CKILL, BT848_E_SCLOOP);
			btand(~BT848_SCLOOP_CKILL, BT848_O_SCLOOP);
		}
		break;
	case V4L2_CID_AUDIO_MUTE:
		audio_mute(btv, c->val);
		btv->mute = c->val;
		break;
	case V4L2_CID_AUDIO_VOLUME:
		btv->volume_gpio(btv, c->val);
		break;

	case V4L2_CID_CHROMA_AGC:
		val = c->val ? BT848_SCLOOP_CAGC : 0;
		btwrite(val, BT848_E_SCLOOP);
		btwrite(val, BT848_O_SCLOOP);
		break;
	case V4L2_CID_PRIVATE_COMBFILTER:
		btv->opt_combfilter = c->val;
		break;
	case V4L2_CID_PRIVATE_LUMAFILTER:
		if (c->val) {
			btand(~BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btand(~BT848_CONTROL_LDEC, BT848_O_CONTROL);
		} else {
			btor(BT848_CONTROL_LDEC, BT848_E_CONTROL);
			btor(BT848_CONTROL_LDEC, BT848_O_CONTROL);
		}
		break;
	case V4L2_CID_PRIVATE_AUTOMUTE:
		btv->opt_automute = c->val;
		break;
	case V4L2_CID_PRIVATE_AGC_CRUSH:
		btwrite(BT848_ADC_RESERVED |
				(c->val ? BT848_ADC_CRUSH : 0),
				BT848_ADC);
		break;
	case V4L2_CID_PRIVATE_VCR_HACK:
		btv->opt_vcr_hack = c->val;
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_UPPER:
		btwrite(c->val, BT848_WC_UP);
		break;
	case V4L2_CID_PRIVATE_WHITECRUSH_LOWER:
		btwrite(c->val, BT848_WC_DOWN);
		break;
	case V4L2_CID_PRIVATE_UV_RATIO:
		btv->opt_uv_ratio = c->val;
		bt848_sat(btv, btv->saturation);
		break;
	case V4L2_CID_PRIVATE_FULL_LUMA_RANGE:
		btaor((c->val << 7), ~BT848_OFORM_RANGE, BT848_OFORM);
		break;
	case V4L2_CID_PRIVATE_CORING:
		btaor((c->val << 5), ~BT848_OFORM_CORE32, BT848_OFORM);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops bttv_ctrl_ops = {
	.s_ctrl = bttv_s_ctrl,
};

static struct v4l2_ctrl_config bttv_ctrl_combfilter = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_COMBFILTER,
	.name = "Comb Filter",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_automute = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_AUTOMUTE,
	.name = "Auto Mute",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_lumafilter = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_LUMAFILTER,
	.name = "Luma Decimation Filter",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_agc_crush = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_AGC_CRUSH,
	.name = "AGC Crush",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_vcr_hack = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_VCR_HACK,
	.name = "VCR Hack",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_whitecrush_lower = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_WHITECRUSH_LOWER,
	.name = "Whitecrush Lower",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 255,
	.step = 1,
	.def = 0x7f,
};

static struct v4l2_ctrl_config bttv_ctrl_whitecrush_upper = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_WHITECRUSH_UPPER,
	.name = "Whitecrush Upper",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 255,
	.step = 1,
	.def = 0xcf,
};

static struct v4l2_ctrl_config bttv_ctrl_uv_ratio = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_UV_RATIO,
	.name = "UV Ratio",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 100,
	.step = 1,
	.def = 50,
};

static struct v4l2_ctrl_config bttv_ctrl_full_luma = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_FULL_LUMA_RANGE,
	.name = "Full Luma Range",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
};

static struct v4l2_ctrl_config bttv_ctrl_coring = {
	.ops = &bttv_ctrl_ops,
	.id = V4L2_CID_PRIVATE_CORING,
	.name = "Coring",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 3,
	.step = 1,
};


/* ----------------------------------------------------------------------- */

void bttv_gpio_tracking(struct bttv *btv, char *comment)
{
	unsigned int outbits, data;
	outbits = btread(BT848_GPIO_OUT_EN);
	data    = btread(BT848_GPIO_DATA);
	pr_debug("%d: gpio: en=%08x, out=%08x in=%08x [%s]\n",
		 btv->c.nr, outbits, data & outbits, data & ~outbits, comment);
}

static const struct bttv_format*
format_by_fourcc(int fourcc)
{
	unsigned int i;

	for (i = 0; i < FORMATS; i++) {
		if (-1 == formats[i].fourcc)
			continue;
		if (formats[i].fourcc == fourcc)
			return formats+i;
	}
	return NULL;
}

/* ----------------------------------------------------------------------- */
/* video4linux (1) interface                                               */

static int queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned int sizes[],
		       struct device *alloc_devs[])
{
	struct bttv *btv = vb2_get_drv_priv(q);
	unsigned int size = btv->fmt->depth * btv->width * btv->height >> 3;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;
	*num_planes = 1;
	sizes[0] = size;

	return 0;
}

static void buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct bttv *btv = vb2_get_drv_priv(vq);
	struct bttv_buffer *buf = container_of(vbuf, struct bttv_buffer, vbuf);
	unsigned long flags;

	spin_lock_irqsave(&btv->s_lock, flags);
	if (list_empty(&btv->capture)) {
		btv->loop_irq = BT848_RISC_VIDEO;
		if (vb2_is_streaming(&btv->vbiq))
			btv->loop_irq |= BT848_RISC_VBI;
		bttv_set_dma(btv, BT848_CAP_CTL_CAPTURE_ODD |
			     BT848_CAP_CTL_CAPTURE_EVEN);
	}
	list_add_tail(&buf->list, &btv->capture);
	spin_unlock_irqrestore(&btv->s_lock, flags);
}

static int buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct bttv *btv = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct bttv_buffer *buf = container_of(vbuf, struct bttv_buffer, vbuf);
	unsigned int size = (btv->fmt->depth * btv->width * btv->height) >> 3;

	if (vb2_plane_size(vb, 0) < size)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, size);

	if (btv->field != V4L2_FIELD_ALTERNATE) {
		buf->vbuf.field = btv->field;
	} else if (btv->field_last == V4L2_FIELD_TOP) {
		buf->vbuf.field = V4L2_FIELD_BOTTOM;
		btv->field_last = V4L2_FIELD_BOTTOM;
	} else {
		buf->vbuf.field = V4L2_FIELD_TOP;
		btv->field_last = V4L2_FIELD_TOP;
	}

	/* Allocate memory for risc struct and create the risc program. */
	return bttv_buffer_risc(btv, buf);
}

static void buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct bttv *btv = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct bttv_buffer *buf = container_of(vbuf, struct bttv_buffer, vbuf);

	btcx_riscmem_free(btv->c.pci, &buf->top);
	btcx_riscmem_free(btv->c.pci, &buf->bottom);
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	int seqnr = 0;
	struct bttv_buffer *buf;
	struct bttv *btv = vb2_get_drv_priv(q);

	if (!check_alloc_btres_lock(btv, RESOURCE_VIDEO_STREAM)) {
		if (btv->field_count)
			seqnr++;
		while (!list_empty(&btv->capture)) {
			buf = list_entry(btv->capture.next,
					 struct bttv_buffer, list);
			list_del(&buf->list);
			buf->vbuf.sequence = (btv->field_count >> 1) + seqnr++;
			vb2_buffer_done(&buf->vbuf.vb2_buf,
					VB2_BUF_STATE_QUEUED);
		}
		return -EBUSY;
	}
	if (!vb2_is_streaming(&btv->vbiq)) {
		init_irqreg(btv);
		btv->field_count = 0;
	}
	btv->framedrop = 0;

	return 0;
}

static void stop_streaming(struct vb2_queue *q)
{
	unsigned long flags;
	struct bttv *btv = vb2_get_drv_priv(q);

	vb2_wait_for_all_buffers(q);
	spin_lock_irqsave(&btv->s_lock, flags);
	free_btres_lock(btv, RESOURCE_VIDEO_STREAM);
	if (!vb2_is_streaming(&btv->vbiq)) {
		/* stop field counter */
		btand(~BT848_INT_VSYNC, BT848_INT_MASK);
	}
	spin_unlock_irqrestore(&btv->s_lock, flags);
}

static const struct vb2_ops bttv_video_qops = {
	.queue_setup    = queue_setup,
	.buf_queue      = buf_queue,
	.buf_prepare    = buf_prepare,
	.buf_cleanup    = buf_cleanup,
	.start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
};

static void radio_enable(struct bttv *btv)
{
	/* Switch to the radio tuner */
	if (!btv->has_radio_tuner) {
		btv->has_radio_tuner = 1;
		bttv_call_all(btv, tuner, s_radio);
		btv->audio_input = TVAUDIO_INPUT_RADIO;
		audio_input(btv, btv->audio_input);
	}
}

static int bttv_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct bttv *btv = video_drvdata(file);
	unsigned int i;

	for (i = 0; i < BTTV_TVNORMS; i++)
		if (id & bttv_tvnorms[i].v4l2_id)
			break;
	if (i == BTTV_TVNORMS)
		return -EINVAL;
	btv->std = id;
	set_tvnorm(btv, i);
	return 0;
}

static int bttv_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct bttv *btv = video_drvdata(file);

	*id = btv->std;
	return 0;
}

static int bttv_querystd(struct file *file, void *f, v4l2_std_id *id)
{
	struct bttv *btv = video_drvdata(file);

	if (btread(BT848_DSTATUS) & BT848_DSTATUS_NUML)
		*id &= V4L2_STD_625_50;
	else
		*id &= V4L2_STD_525_60;
	return 0;
}

static int bttv_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	struct bttv *btv = video_drvdata(file);

	if (i->index >= bttv_tvcards[btv->c.type].video_inputs)
		return -EINVAL;

	i->type     = V4L2_INPUT_TYPE_CAMERA;
	i->audioset = 0;

	if (btv->tuner_type != TUNER_ABSENT && i->index == 0) {
		sprintf(i->name, "Television");
		i->type  = V4L2_INPUT_TYPE_TUNER;
		i->tuner = 0;
	} else if (i->index == btv->svhs) {
		sprintf(i->name, "S-Video");
	} else {
		sprintf(i->name, "Composite%d", i->index);
	}

	if (i->index == btv->input) {
		__u32 dstatus = btread(BT848_DSTATUS);
		if (0 == (dstatus & BT848_DSTATUS_PRES))
			i->status |= V4L2_IN_ST_NO_SIGNAL;
		if (0 == (dstatus & BT848_DSTATUS_HLOC))
			i->status |= V4L2_IN_ST_NO_H_LOCK;
	}

	i->std = BTTV_NORMS;
	return 0;
}

static int bttv_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct bttv *btv = video_drvdata(file);

	*i = btv->input;

	return 0;
}

static int bttv_s_input(struct file *file, void *priv, unsigned int i)
{
	struct bttv *btv = video_drvdata(file);

	if (i >= bttv_tvcards[btv->c.type].video_inputs)
		return -EINVAL;

	set_input(btv, i, btv->tvnorm);
	return 0;
}

static int bttv_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct bttv *btv = video_drvdata(file);

	if (t->index)
		return -EINVAL;

	bttv_call_all(btv, tuner, s_tuner, t);

	if (btv->audio_mode_gpio) {
		struct v4l2_tuner copy = *t;

		btv->audio_mode_gpio(btv, &copy, 1);
	}
	return 0;
}

static int bttv_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct bttv *btv = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;

	if (f->type == V4L2_TUNER_RADIO)
		radio_enable(btv);
	f->frequency = f->type == V4L2_TUNER_RADIO ?
				btv->radio_freq : btv->tv_freq;

	return 0;
}

static void bttv_set_frequency(struct bttv *btv, const struct v4l2_frequency *f)
{
	struct v4l2_frequency new_freq = *f;

	bttv_call_all(btv, tuner, s_frequency, f);
	/* s_frequency may clamp the frequency, so get the actual
	   frequency before assigning radio/tv_freq. */
	bttv_call_all(btv, tuner, g_frequency, &new_freq);
	if (new_freq.type == V4L2_TUNER_RADIO) {
		radio_enable(btv);
		btv->radio_freq = new_freq.frequency;
		if (btv->has_tea575x) {
			btv->tea.freq = btv->radio_freq;
			snd_tea575x_set_freq(&btv->tea);
		}
	} else {
		btv->tv_freq = new_freq.frequency;
	}
}

static int bttv_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *f)
{
	struct bttv *btv = video_drvdata(file);

	if (f->tuner)
		return -EINVAL;

	bttv_set_frequency(btv, f);
	return 0;
}

static int bttv_log_status(struct file *file, void *f)
{
	struct video_device *vdev = video_devdata(file);
	struct bttv *btv = video_drvdata(file);

	v4l2_ctrl_handler_log_status(vdev->ctrl_handler, btv->c.v4l2_dev.name);
	bttv_call_all(btv, core, log_status);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int bttv_g_register(struct file *file, void *f,
					struct v4l2_dbg_register *reg)
{
	struct bttv *btv = video_drvdata(file);

	/* bt848 has a 12-bit register space */
	reg->reg &= 0xfff;
	reg->val = btread(reg->reg);
	reg->size = 1;

	return 0;
}

static int bttv_s_register(struct file *file, void *f,
					const struct v4l2_dbg_register *reg)
{
	struct bttv *btv = video_drvdata(file);

	/* bt848 has a 12-bit register space */
	btwrite(reg->val, reg->reg & 0xfff);

	return 0;
}
#endif

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
limit_scaled_size_lock(struct bttv *btv, __s32 *width, __s32 *height,
		       enum v4l2_field field, unsigned int width_mask,
		       unsigned int width_bias, int adjust_size,
		       int adjust_crop)
{
	const struct v4l2_rect *b;
	struct bttv_crop *c;
	__s32 min_width;
	__s32 min_height;
	__s32 max_width;
	__s32 max_height;
	int rc;

	WARN_ON((int)width_mask >= 0 ||
		width_bias >= (unsigned int)(-width_mask));

	/* Make sure tvnorm, vbi_end and the current cropping parameters
	   remain consistent until we're done. */

	b = &bttv_tvnorms[btv->tvnorm].cropcap.bounds;

	/* Do crop - use current, don't - use default parameters. */
	c = &btv->crop[!!btv->do_crop];

	if (btv->do_crop
	    && adjust_size
	    && adjust_crop
	    && !locked_btres(btv, VIDEO_RESOURCES)) {
		min_width = 48;
		min_height = 32;

		/* We cannot scale up. When the scaled image is larger
		   than crop.rect we adjust the crop.rect as required
		   by the V4L2 spec, hence cropcap.bounds are our limit. */
		max_width = min_t(unsigned int, b->width, MAX_HACTIVE);
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

	return rc;
}

static int bttv_switch_type(struct bttv *btv, enum v4l2_buf_type type)
{
	int res;
	struct vb2_queue *q;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		q = &btv->capq;
		res = RESOURCE_VIDEO_STREAM;
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		q = &btv->vbiq;
		res = RESOURCE_VBI;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	if (check_btres(btv, res))
		return -EBUSY;
	if (vb2_is_busy(q))
		return -EBUSY;
	btv->type = type;

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

static int bttv_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct bttv *btv = video_drvdata(file);

	pix_format_set_size(&f->fmt.pix, btv->fmt, btv->width, btv->height);
	f->fmt.pix.field = btv->field;
	f->fmt.pix.pixelformat  = btv->fmt->fourcc;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static void bttv_get_width_mask_vid_cap(const struct bttv_format *fmt,
					unsigned int *width_mask,
					unsigned int *width_bias)
{
	if (fmt->flags & FORMAT_FLAGS_PLANAR) {
		*width_mask = ~15; /* width must be a multiple of 16 pixels */
		*width_bias = 8;   /* nearest */
	} else {
		*width_mask = ~3; /* width must be a multiple of 4 pixels */
		*width_bias = 2;  /* nearest */
	}
}

static int bttv_try_fmt_vid_cap(struct file *file, void *priv,
						struct v4l2_format *f)
{
	const struct bttv_format *fmt;
	struct bttv *btv = video_drvdata(file);
	enum v4l2_field field;
	__s32 width, height;
	__s32 height2;
	unsigned int width_mask, width_bias;
	int rc;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	if (NULL == fmt)
		return -EINVAL;

	field = f->fmt.pix.field;

	switch (field) {
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
	case V4L2_FIELD_ALTERNATE:
	case V4L2_FIELD_INTERLACED:
		break;
	case V4L2_FIELD_SEQ_BT:
	case V4L2_FIELD_SEQ_TB:
		if (!(fmt->flags & FORMAT_FLAGS_PLANAR)) {
			field = V4L2_FIELD_SEQ_TB;
			break;
		}
		fallthrough;
	default: /* FIELD_ANY case */
		height2 = btv->crop[!!btv->do_crop].rect.height >> 1;
		field = (f->fmt.pix.height > height2)
			? V4L2_FIELD_INTERLACED
			: V4L2_FIELD_BOTTOM;
		break;
	}

	width = f->fmt.pix.width;
	height = f->fmt.pix.height;

	bttv_get_width_mask_vid_cap(fmt, &width_mask, &width_bias);
	rc = limit_scaled_size_lock(btv, &width, &height, field, width_mask,
				    width_bias, 1, 0);
	if (0 != rc)
		return rc;

	/* update data for the application */
	f->fmt.pix.field = field;
	pix_format_set_size(&f->fmt.pix, fmt, width, height);
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int bttv_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	int retval;
	const struct bttv_format *fmt;
	struct bttv *btv = video_drvdata(file);
	__s32 width, height;
	unsigned int width_mask, width_bias;
	enum v4l2_field field;

	retval = bttv_switch_type(btv, f->type);
	if (0 != retval)
		return retval;

	retval = bttv_try_fmt_vid_cap(file, priv, f);
	if (0 != retval)
		return retval;

	width = f->fmt.pix.width;
	height = f->fmt.pix.height;
	field = f->fmt.pix.field;

	fmt = format_by_fourcc(f->fmt.pix.pixelformat);
	bttv_get_width_mask_vid_cap(fmt, &width_mask, &width_bias);
	retval = limit_scaled_size_lock(btv, &width, &height, f->fmt.pix.field,
					width_mask, width_bias, 1, 1);
	if (0 != retval)
		return retval;

	f->fmt.pix.field = field;

	/* update our state information */
	btv->fmt = fmt;
	btv->width = f->fmt.pix.width;
	btv->height = f->fmt.pix.height;
	btv->field = f->fmt.pix.field;
	/*
	 * When field is V4L2_FIELD_ALTERNATE, buffers will be either
	 * V4L2_FIELD_TOP or V4L2_FIELD_BOTTOM depending on the value of
	 * field_last. Initialize field_last to V4L2_FIELD_BOTTOM so that
	 * streaming starts with a V4L2_FIELD_TOP buffer.
	 */
	btv->field_last = V4L2_FIELD_BOTTOM;

	return 0;
}

static int bttv_querycap(struct file *file, void  *priv,
				struct v4l2_capability *cap)
{
	struct bttv *btv = video_drvdata(file);

	if (0 == v4l2)
		return -EINVAL;

	strscpy(cap->driver, "bttv", sizeof(cap->driver));
	strscpy(cap->card, btv->video_dev.name, sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			    V4L2_CAP_STREAMING | V4L2_CAP_DEVICE_CAPS;
	if (video_is_registered(&btv->vbi_dev))
		cap->capabilities |= V4L2_CAP_VBI_CAPTURE;
	if (video_is_registered(&btv->radio_dev)) {
		cap->capabilities |= V4L2_CAP_RADIO;
		if (btv->has_tea575x)
			cap->capabilities |= V4L2_CAP_HW_FREQ_SEEK;
	}

	/*
	 * No need to lock here: those vars are initialized during board
	 * probe and remains untouched during the rest of the driver lifecycle
	 */
	if (btv->has_saa6588)
		cap->capabilities |= V4L2_CAP_RDS_CAPTURE;
	if (btv->tuner_type != TUNER_ABSENT)
		cap->capabilities |= V4L2_CAP_TUNER;
	return 0;
}

static int bttv_enum_fmt_vid_cap(struct file *file, void  *priv,
				 struct v4l2_fmtdesc *f)
{
	int index = -1, i;

	for (i = 0; i < FORMATS; i++) {
		if (formats[i].fourcc != -1)
			index++;
		if ((unsigned int)index == f->index)
			break;
	}
	if (FORMATS == i)
		return -EINVAL;

	f->pixelformat = formats[i].fourcc;

	return 0;
}

static int bttv_g_parm(struct file *file, void *f,
				struct v4l2_streamparm *parm)
{
	struct bttv *btv = video_drvdata(file);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parm->parm.capture.readbuffers = gbuffers;
	v4l2_video_std_frame_period(bttv_tvnorms[btv->tvnorm].v4l2_id,
				    &parm->parm.capture.timeperframe);

	return 0;
}

static int bttv_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct bttv *btv = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;

	t->rxsubchans = V4L2_TUNER_SUB_MONO;
	t->capability = V4L2_TUNER_CAP_NORM;
	bttv_call_all(btv, tuner, g_tuner, t);
	strscpy(t->name, "Television", sizeof(t->name));
	t->type       = V4L2_TUNER_ANALOG_TV;
	if (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC)
		t->signal = 0xffff;

	if (btv->audio_mode_gpio)
		btv->audio_mode_gpio(btv, t, 0);

	return 0;
}

static int bttv_g_pixelaspect(struct file *file, void *priv,
			      int type, struct v4l2_fract *f)
{
	struct bttv *btv = video_drvdata(file);

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* defrect and bounds are set via g_selection */
	*f = bttv_tvnorms[btv->tvnorm].cropcap.pixelaspect;
	return 0;
}

static int bttv_g_selection(struct file *file, void *f, struct v4l2_selection *sel)
{
	struct bttv *btv = video_drvdata(file);

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = btv->crop[!!btv->do_crop].rect;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r = bttv_tvnorms[btv->tvnorm].cropcap.defrect;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = bttv_tvnorms[btv->tvnorm].cropcap.bounds;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bttv_s_selection(struct file *file, void *f, struct v4l2_selection *sel)
{
	struct bttv *btv = video_drvdata(file);
	const struct v4l2_rect *b;
	int retval;
	struct bttv_crop c;
	__s32 b_left;
	__s32 b_top;
	__s32 b_right;
	__s32 b_bottom;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	/* Make sure tvnorm, vbi_end and the current cropping
	   parameters remain consistent until we're done. Note
	   read() may change vbi_end in check_alloc_btres_lock(). */
	retval = -EBUSY;

	if (locked_btres(btv, VIDEO_RESOURCES))
		return retval;

	b = &bttv_tvnorms[btv->tvnorm].cropcap.bounds;

	b_left = b->left;
	b_right = b_left + b->width;
	b_bottom = b->top + b->height;

	b_top = max(b->top, btv->vbi_end);
	if (b_top + 32 >= b_bottom) {
		return retval;
	}

	/* Min. scaled size 48 x 32. */
	c.rect.left = clamp_t(s32, sel->r.left, b_left, b_right - 48);
	c.rect.left = min(c.rect.left, (__s32) MAX_HDELAY);

	c.rect.width = clamp_t(s32, sel->r.width,
			     48, b_right - c.rect.left);

	c.rect.top = clamp_t(s32, sel->r.top, b_top, b_bottom - 32);
	/* Top and height must be a multiple of two. */
	c.rect.top = (c.rect.top + 1) & ~1;

	c.rect.height = clamp_t(s32, sel->r.height,
			      32, b_bottom - c.rect.top);
	c.rect.height = (c.rect.height + 1) & ~1;

	bttv_crop_calc_limits(&c);

	sel->r = c.rect;

	btv->crop[1] = c;

	btv->do_crop = 1;

	if (btv->width < c.min_scaled_width)
		btv->width = c.min_scaled_width;
	else if (btv->width > c.max_scaled_width)
		btv->width = c.max_scaled_width;

	if (btv->height < c.min_scaled_height)
		btv->height = c.min_scaled_height;
	else if (btv->height > c.max_scaled_height)
		btv->height = c.max_scaled_height;

	return 0;
}

static const struct v4l2_file_operations bttv_fops =
{
	.owner		  = THIS_MODULE,
	.open		  = v4l2_fh_open,
	.release	  = vb2_fop_release,
	.unlocked_ioctl	  = video_ioctl2,
	.read		  = vb2_fop_read,
	.mmap		  = vb2_fop_mmap,
	.poll		  = vb2_fop_poll,
};

static const struct v4l2_ioctl_ops bttv_ioctl_ops = {
	.vidioc_querycap                = bttv_querycap,
	.vidioc_enum_fmt_vid_cap        = bttv_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap           = bttv_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap         = bttv_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap           = bttv_s_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap           = bttv_g_fmt_vbi_cap,
	.vidioc_try_fmt_vbi_cap         = bttv_try_fmt_vbi_cap,
	.vidioc_s_fmt_vbi_cap           = bttv_s_fmt_vbi_cap,
	.vidioc_g_pixelaspect           = bttv_g_pixelaspect,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
	.vidioc_s_std                   = bttv_s_std,
	.vidioc_g_std                   = bttv_g_std,
	.vidioc_enum_input              = bttv_enum_input,
	.vidioc_g_input                 = bttv_g_input,
	.vidioc_s_input                 = bttv_s_input,
	.vidioc_g_tuner                 = bttv_g_tuner,
	.vidioc_s_tuner                 = bttv_s_tuner,
	.vidioc_g_selection             = bttv_g_selection,
	.vidioc_s_selection             = bttv_s_selection,
	.vidioc_g_parm                  = bttv_g_parm,
	.vidioc_g_frequency             = bttv_g_frequency,
	.vidioc_s_frequency             = bttv_s_frequency,
	.vidioc_log_status		= bttv_log_status,
	.vidioc_querystd		= bttv_querystd,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		= bttv_g_register,
	.vidioc_s_register		= bttv_s_register,
#endif
};

static struct video_device bttv_video_template = {
	.fops         = &bttv_fops,
	.ioctl_ops    = &bttv_ioctl_ops,
	.tvnorms      = BTTV_NORMS,
};

/* ----------------------------------------------------------------------- */
/* radio interface                                                         */

static int radio_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct bttv *btv = video_drvdata(file);
	int ret = v4l2_fh_open(file);

	if (ret)
		return ret;

	dprintk("open dev=%s\n", video_device_node_name(vdev));
	dprintk("%d: open called (radio)\n", btv->c.nr);

	btv->radio_user++;
	audio_mute(btv, btv->mute);

	return 0;
}

static int radio_release(struct file *file)
{
	struct bttv *btv = video_drvdata(file);
	struct saa6588_command cmd;

	btv->radio_user--;

	bttv_call_all(btv, core, command, SAA6588_CMD_CLOSE, &cmd);

	if (btv->radio_user == 0)
		btv->has_radio_tuner = 0;

	v4l2_fh_release(file);

	return 0;
}

static int radio_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct bttv *btv = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;
	strscpy(t->name, "Radio", sizeof(t->name));
	t->type = V4L2_TUNER_RADIO;
	radio_enable(btv);

	bttv_call_all(btv, tuner, g_tuner, t);

	if (btv->audio_mode_gpio)
		btv->audio_mode_gpio(btv, t, 0);

	if (btv->has_tea575x)
		return snd_tea575x_g_tuner(&btv->tea, t);

	return 0;
}

static int radio_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *t)
{
	struct bttv *btv = video_drvdata(file);

	if (0 != t->index)
		return -EINVAL;

	radio_enable(btv);
	bttv_call_all(btv, tuner, s_tuner, t);
	return 0;
}

static int radio_s_hw_freq_seek(struct file *file, void *priv,
					const struct v4l2_hw_freq_seek *a)
{
	struct bttv *btv = video_drvdata(file);

	if (btv->has_tea575x)
		return snd_tea575x_s_hw_freq_seek(file, &btv->tea, a);

	return -ENOTTY;
}

static int radio_enum_freq_bands(struct file *file, void *priv,
					 struct v4l2_frequency_band *band)
{
	struct bttv *btv = video_drvdata(file);

	if (btv->has_tea575x)
		return snd_tea575x_enum_freq_bands(&btv->tea, band);

	return -ENOTTY;
}

static ssize_t radio_read(struct file *file, char __user *data,
			 size_t count, loff_t *ppos)
{
	struct bttv *btv = video_drvdata(file);
	struct saa6588_command cmd;

	cmd.block_count = count / 3;
	cmd.nonblocking = file->f_flags & O_NONBLOCK;
	cmd.buffer = data;
	cmd.instance = file;
	cmd.result = -ENODEV;
	radio_enable(btv);

	bttv_call_all(btv, core, command, SAA6588_CMD_READ, &cmd);

	return cmd.result;
}

static __poll_t radio_poll(struct file *file, poll_table *wait)
{
	struct bttv *btv = video_drvdata(file);
	struct saa6588_command cmd;
	__poll_t rc = v4l2_ctrl_poll(file, wait);

	radio_enable(btv);
	cmd.instance = file;
	cmd.event_list = wait;
	cmd.poll_mask = 0;
	bttv_call_all(btv, core, command, SAA6588_CMD_POLL, &cmd);

	return rc | cmd.poll_mask;
}

static const struct v4l2_file_operations radio_fops =
{
	.owner	  = THIS_MODULE,
	.open	  = radio_open,
	.read     = radio_read,
	.release  = radio_release,
	.unlocked_ioctl = video_ioctl2,
	.poll     = radio_poll,
};

static const struct v4l2_ioctl_ops radio_ioctl_ops = {
	.vidioc_querycap        = bttv_querycap,
	.vidioc_log_status	= bttv_log_status,
	.vidioc_g_tuner         = radio_g_tuner,
	.vidioc_s_tuner         = radio_s_tuner,
	.vidioc_g_frequency     = bttv_g_frequency,
	.vidioc_s_frequency     = bttv_s_frequency,
	.vidioc_s_hw_freq_seek	= radio_s_hw_freq_seek,
	.vidioc_enum_freq_bands	= radio_enum_freq_bands,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device radio_template = {
	.fops      = &radio_fops,
	.ioctl_ops = &radio_ioctl_ops,
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

	pr_cont("0x%08x [ %s", risc,
	       instr[risc >> 28] ? instr[risc >> 28] : "INVALID");
	for (i = ARRAY_SIZE(bits)-1; i >= 0; i--)
		if (risc & (1 << (i + 12)))
			pr_cont(" %s", bits[i]);
	pr_cont(" count=%d ]\n", risc & 0xfff);
	return incr[risc >> 28] ? incr[risc >> 28] : 1;
}

static void bttv_risc_disasm(struct bttv *btv,
			     struct btcx_riscmem *risc)
{
	unsigned int i,j,n;

	pr_info("%s: risc disasm: %p [dma=0x%08lx]\n",
		btv->c.v4l2_dev.name, risc->cpu, (unsigned long)risc->dma);
	for (i = 0; i < (risc->size >> 2); i += n) {
		pr_info("%s:   0x%lx: ",
			btv->c.v4l2_dev.name,
			(unsigned long)(risc->dma + (i<<2)));
		n = bttv_risc_decode(le32_to_cpu(risc->cpu[i]));
		for (j = 1; j < n; j++)
			pr_info("%s:   0x%lx: 0x%08x [ arg #%d ]\n",
				btv->c.v4l2_dev.name,
				(unsigned long)(risc->dma + ((i+j)<<2)),
				risc->cpu[i+j], j);
		if (0 == risc->cpu[i])
			break;
	}
}

static void bttv_print_riscaddr(struct bttv *btv)
{
	pr_info("  main: %08llx\n", (unsigned long long)btv->main.dma);
	pr_info("  vbi : o=%08llx e=%08llx\n",
		btv->cvbi ? (unsigned long long)btv->cvbi->top.dma : 0,
		btv->cvbi ? (unsigned long long)btv->cvbi->bottom.dma : 0);
	pr_info("  cap : o=%08llx e=%08llx\n",
		btv->curr.top
		? (unsigned long long)btv->curr.top->top.dma : 0,
		btv->curr.bottom
		? (unsigned long long)btv->curr.bottom->bottom.dma : 0);
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

	pr_cont("bits:");
	for (i = 0; i < ARRAY_SIZE(irq_name); i++) {
		if (print & (1 << i))
			pr_cont(" %s", irq_name[i]);
		if (mark & (1 << i))
			pr_cont("*");
	}
}

static void bttv_irq_debug_low_latency(struct bttv *btv, u32 rc)
{
	pr_warn("%d: irq: skipped frame [main=%lx,o_vbi=%lx,o_field=%lx,rc=%lx]\n",
		btv->c.nr,
		(unsigned long)btv->main.dma,
		(unsigned long)le32_to_cpu(btv->main.cpu[RISC_SLOT_O_VBI+1]),
		(unsigned long)le32_to_cpu(btv->main.cpu[RISC_SLOT_O_FIELD+1]),
		(unsigned long)rc);

	if (0 == (btread(BT848_DSTATUS) & BT848_DSTATUS_HLOC)) {
		pr_notice("%d: Oh, there (temporarily?) is no input signal. Ok, then this is harmless, don't worry ;)\n",
			  btv->c.nr);
		return;
	}
	pr_notice("%d: Uhm. Looks like we have unusual high IRQ latencies\n",
		  btv->c.nr);
	pr_notice("%d: Lets try to catch the culprit red-handed ...\n",
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
		set->frame_irq = BT848_RISC_VIDEO;
		item = list_entry(btv->capture.next, struct bttv_buffer, list);

		if (V4L2_FIELD_HAS_TOP(item->vbuf.field))
			set->top    = item;
		if (V4L2_FIELD_HAS_BOTTOM(item->vbuf.field))
			set->bottom = item;

		/* capture request for other field ? */
		if (!V4L2_FIELD_HAS_BOTH(item->vbuf.field) &&
		    item->list.next != &btv->capture) {
			item = list_entry(item->list.next,
					  struct bttv_buffer, list);
			/* Mike Isely <isely@pobox.com> - Only check
			 * and set up the bottom field in the logic
			 * below.  Don't ever do the top field.  This
			 * of course means that if we set up the
			 * bottom field in the above code that we'll
			 * actually skip a field.  But that's OK.
			 * Having processed only a single buffer this
			 * time, then the next time around the first
			 * available buffer should be for a top field.
			 * That will then cause us here to set up a
			 * top then a bottom field in the normal way.
			 * The alternative to this understanding is
			 * that we set up the second available buffer
			 * as a top field, but that's out of order
			 * since this driver always processes the top
			 * field first - the effect will be the two
			 * buffers being returned in the wrong order,
			 * with the second buffer also being delayed
			 * by one field time (owing to the fifo nature
			 * of videobuf).  Worse still, we'll be stuck
			 * doing fields out of order now every time
			 * until something else causes a field to be
			 * dropped.  By effectively forcing a field to
			 * drop this way then we always get back into
			 * sync within a single frame time.  (Out of
			 * order fields can screw up deinterlacing
			 * algorithms.) */
			if (!V4L2_FIELD_HAS_BOTH(item->vbuf.field)) {
				if (!set->bottom &&
				    item->vbuf.field == V4L2_FIELD_BOTTOM)
					set->bottom = item;
				if (set->top && set->bottom) {
					/*
					 * The buffer set has a top buffer and
					 * a bottom buffer and they are not
					 * copies of each other.
					 */
					set->top_irq = BT848_RISC_TOP;
				}
			}
		}
	}

	dprintk("%d: next set: top=%p bottom=%p [irq=%d,%d]\n",
		btv->c.nr, set->top, set->bottom,
		set->frame_irq, set->top_irq);
	return 0;
}

static void
bttv_irq_wakeup_video(struct bttv *btv, struct bttv_buffer_set *wakeup,
		      struct bttv_buffer_set *curr, unsigned int state)
{
	u64 ts = ktime_get_ns();

	if (wakeup->top == wakeup->bottom) {
		if (NULL != wakeup->top && curr->top != wakeup->top) {
			if (irq_debug > 1)
				pr_debug("%d: wakeup: both=%p\n",
					 btv->c.nr, wakeup->top);
			wakeup->top->vbuf.vb2_buf.timestamp = ts;
			wakeup->top->vbuf.sequence = btv->field_count >> 1;
			vb2_buffer_done(&wakeup->top->vbuf.vb2_buf, state);
			if (btv->field_count == 0)
				btor(BT848_INT_VSYNC, BT848_INT_MASK);
		}
	} else {
		if (NULL != wakeup->top && curr->top != wakeup->top) {
			if (irq_debug > 1)
				pr_debug("%d: wakeup: top=%p\n",
					 btv->c.nr, wakeup->top);
			wakeup->top->vbuf.vb2_buf.timestamp = ts;
			wakeup->top->vbuf.sequence = btv->field_count >> 1;
			vb2_buffer_done(&wakeup->top->vbuf.vb2_buf, state);
			if (btv->field_count == 0)
				btor(BT848_INT_VSYNC, BT848_INT_MASK);
		}
		if (NULL != wakeup->bottom && curr->bottom != wakeup->bottom) {
			if (irq_debug > 1)
				pr_debug("%d: wakeup: bottom=%p\n",
					 btv->c.nr, wakeup->bottom);
			wakeup->bottom->vbuf.vb2_buf.timestamp = ts;
			wakeup->bottom->vbuf.sequence = btv->field_count >> 1;
			vb2_buffer_done(&wakeup->bottom->vbuf.vb2_buf, state);
			if (btv->field_count == 0)
				btor(BT848_INT_VSYNC, BT848_INT_MASK);
		}
	}
}

static void
bttv_irq_wakeup_vbi(struct bttv *btv, struct bttv_buffer *wakeup,
				unsigned int state)
{
	if (NULL == wakeup)
		return;
	wakeup->vbuf.vb2_buf.timestamp = ktime_get_ns();
	wakeup->vbuf.sequence = btv->field_count >> 1;

	/*
	 * Ugly hack for backwards compatibility.
	 * Some applications expect that the last 4 bytes of
	 * the VBI data contains the sequence number.
	 *
	 * This makes it possible to associate the VBI data
	 * with the video frame if you use read() to get the
	 * VBI data.
	 */
	if (vb2_fileio_is_active(wakeup->vbuf.vb2_buf.vb2_queue)) {
		u32 *vaddr = vb2_plane_vaddr(&wakeup->vbuf.vb2_buf, 0);
		unsigned long size =
			vb2_get_plane_payload(&wakeup->vbuf.vb2_buf, 0) / 4;

		if (vaddr && size) {
			vaddr += size - 1;
			*vaddr = wakeup->vbuf.sequence;
		}
	}

	vb2_buffer_done(&wakeup->vbuf.vb2_buf, state);
	if (btv->field_count == 0)
		btor(BT848_INT_VSYNC, BT848_INT_MASK);
}

static void bttv_irq_timeout(struct timer_list *t)
{
	struct bttv *btv = from_timer(btv, t, timeout);
	struct bttv_buffer_set old,new;
	struct bttv_buffer *ovbi;
	struct bttv_buffer *item;
	unsigned long flags;
	int seqnr = 0;

	if (bttv_verbose) {
		pr_info("%d: timeout: drop=%d irq=%d/%d, risc=%08x, ",
			btv->c.nr, btv->framedrop, btv->irq_me, btv->irq_total,
			btread(BT848_RISC_COUNT));
		bttv_print_irqbits(btread(BT848_INT_STAT),0);
		pr_cont("\n");
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
	bttv_irq_wakeup_video(btv, &old, &new, VB2_BUF_STATE_DONE);
	bttv_irq_wakeup_vbi(btv, ovbi, VB2_BUF_STATE_DONE);

	/* cancel all outstanding capture / vbi requests */
	if (btv->field_count)
		seqnr++;
	while (!list_empty(&btv->capture)) {
		item = list_entry(btv->capture.next, struct bttv_buffer, list);
		list_del(&item->list);
		item->vbuf.vb2_buf.timestamp = ktime_get_ns();
		item->vbuf.sequence = (btv->field_count >> 1) + seqnr++;
		vb2_buffer_done(&item->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	while (!list_empty(&btv->vcapture)) {
		item = list_entry(btv->vcapture.next, struct bttv_buffer, list);
		list_del(&item->list);
		item->vbuf.vb2_buf.timestamp = ktime_get_ns();
		item->vbuf.sequence = (btv->field_count >> 1) + seqnr++;
		vb2_buffer_done(&item->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
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
	wakeup->vbuf.vb2_buf.timestamp = ktime_get_ns();
	wakeup->vbuf.sequence = btv->field_count >> 1;
	vb2_buffer_done(&wakeup->vbuf.vb2_buf, VB2_BUF_STATE_DONE);
	if (btv->field_count == 0)
		btor(BT848_INT_VSYNC, BT848_INT_MASK);
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
	btv->loop_irq &= ~BT848_RISC_VIDEO;
	bttv_buffer_activate_video(btv, &new);
	bttv_set_dma(btv, 0);

	/* switch input */
	if (UNSET != btv->new_input) {
		video_mux(btv,btv->new_input);
		btv->new_input = UNSET;
	}

	/* wake up finished buffers */
	bttv_irq_wakeup_video(btv, &old, &new, VB2_BUF_STATE_DONE);
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
		new = list_entry(btv->vcapture.next, struct bttv_buffer, list);
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
	btv->loop_irq &= ~BT848_RISC_VBI;
	bttv_buffer_activate_vbi(btv, new);
	bttv_set_dma(btv, 0);

	bttv_irq_wakeup_vbi(btv, old, VB2_BUF_STATE_DONE);
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
			pr_debug("%d: irq loop=%d fc=%d riscs=%x, riscc=%08x, ",
				 btv->c.nr, count, btv->field_count,
				 stat>>28, btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			if (stat & BT848_INT_HLOCK)
				pr_cont("   HLOC => %s",
					dstat & BT848_DSTATUS_HLOC
					? "yes" : "no");
			if (stat & BT848_INT_VPRES)
				pr_cont("   PRES => %s",
					dstat & BT848_DSTATUS_PRES
					? "yes" : "no");
			if (stat & BT848_INT_FMTCHG)
				pr_cont("   NUML => %s",
					dstat & BT848_DSTATUS_NUML
					? "625" : "525");
			pr_cont("\n");
		}

		if (astat&BT848_INT_VSYNC)
			btv->field_count++;

		if ((astat & BT848_INT_GPINT) && btv->remote) {
			bttv_input_irq(btv);
		}

		if (astat & BT848_INT_I2CDONE) {
			btv->i2c_done = stat;
			wake_up(&btv->i2c_queue);
		}

		if ((astat & BT848_INT_RISCI) && (stat & BT848_INT_RISCS_VBI))
			bttv_irq_switch_vbi(btv);

		if ((astat & BT848_INT_RISCI) && (stat & BT848_INT_RISCS_TOP))
			bttv_irq_wakeup_top(btv);

		if ((astat & BT848_INT_RISCI) && (stat & BT848_INT_RISCS_VIDEO))
			bttv_irq_switch_video(btv);

		if ((astat & BT848_INT_HLOCK)  &&  btv->opt_automute)
			/* trigger automute */
			audio_mux_gpio(btv, btv->audio_input, btv->mute);

		if (astat & (BT848_INT_SCERR|BT848_INT_OCERR)) {
			pr_info("%d: %s%s @ %08x,",
				btv->c.nr,
				(astat & BT848_INT_SCERR) ? "SCERR" : "",
				(astat & BT848_INT_OCERR) ? "OCERR" : "",
				btread(BT848_RISC_COUNT));
			bttv_print_irqbits(stat,astat);
			pr_cont("\n");
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}
		if (fdsr && astat & BT848_INT_FDSR) {
			pr_info("%d: FDSR @ %08x\n",
				btv->c.nr, btread(BT848_RISC_COUNT));
			if (bttv_debug)
				bttv_print_riscaddr(btv);
		}

		count++;
		if (count > 4) {

			if (count > 8 || !(astat & BT848_INT_GPINT)) {
				btwrite(0, BT848_INT_MASK);

				pr_err("%d: IRQ lockup, cleared int mask [",
				       btv->c.nr);
			} else {
				pr_err("%d: IRQ lockup, clearing GPINT from int mask [",
				       btv->c.nr);

				btwrite(btread(BT848_INT_MASK) & (-1 ^ BT848_INT_GPINT),
						BT848_INT_MASK);
			}

			bttv_print_irqbits(stat,astat);

			pr_cont("]\n");
		}
	}
	btv->irq_total++;
	if (handled)
		btv->irq_me++;
	return IRQ_RETVAL(handled);
}


/* ----------------------------------------------------------------------- */
/* initialization                                                          */

static int vdev_init(struct bttv *btv, struct video_device *vfd,
		     const struct video_device *template,
		     const char *type_name)
{
	int err;
	struct vb2_queue *q;
	*vfd = *template;
	vfd->v4l2_dev = &btv->c.v4l2_dev;
	vfd->release = video_device_release_empty;
	video_set_drvdata(vfd, btv);
	snprintf(vfd->name, sizeof(vfd->name), "BT%d%s %s (%s)",
		 btv->id, (btv->id==848 && btv->revision==0x12) ? "A" : "",
		 type_name, bttv_tvcards[btv->c.type].name);
	if (btv->tuner_type == TUNER_ABSENT) {
		v4l2_disable_ioctl(vfd, VIDIOC_G_FREQUENCY);
		v4l2_disable_ioctl(vfd, VIDIOC_S_FREQUENCY);
		v4l2_disable_ioctl(vfd, VIDIOC_G_TUNER);
		v4l2_disable_ioctl(vfd, VIDIOC_S_TUNER);
	}

	if (strcmp(type_name, "radio") == 0)
		return 0;

	if (strcmp(type_name, "video") == 0) {
		q = &btv->capq;
		q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		q->ops = &bttv_video_qops;
	} else if (strcmp(type_name, "vbi") == 0) {
		q = &btv->vbiq;
		q->type = V4L2_BUF_TYPE_VBI_CAPTURE;
		q->ops = &bttv_vbi_qops;
	} else {
		return -EINVAL;
	}
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ | VB2_DMABUF;
	q->mem_ops = &vb2_dma_sg_memops;
	q->drv_priv = btv;
	q->gfp_flags = __GFP_DMA32;
	q->buf_struct_size = sizeof(struct bttv_buffer);
	q->lock = &btv->lock;
	q->min_queued_buffers = 2;
	q->dev = &btv->c.pci->dev;
	err = vb2_queue_init(q);
	if (err)
		return err;
	vfd->queue = q;

	return 0;
}

static void bttv_unregister_video(struct bttv *btv)
{
	video_unregister_device(&btv->video_dev);
	video_unregister_device(&btv->vbi_dev);
	video_unregister_device(&btv->radio_dev);
}

/* register video4linux devices */
static int bttv_register_video(struct bttv *btv)
{
	/* video */
	vdev_init(btv, &btv->video_dev, &bttv_video_template, "video");
	btv->video_dev.device_caps = V4L2_CAP_VIDEO_CAPTURE |
				     V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	if (btv->tuner_type != TUNER_ABSENT)
		btv->video_dev.device_caps |= V4L2_CAP_TUNER;

	if (video_register_device(&btv->video_dev, VFL_TYPE_VIDEO,
				  video_nr[btv->c.nr]) < 0)
		goto err;
	pr_info("%d: registered device %s\n",
		btv->c.nr, video_device_node_name(&btv->video_dev));
	if (device_create_file(&btv->video_dev.dev,
				     &dev_attr_card)<0) {
		pr_err("%d: device_create_file 'card' failed\n", btv->c.nr);
		goto err;
	}

	/* vbi */
	vdev_init(btv, &btv->vbi_dev, &bttv_video_template, "vbi");
	btv->vbi_dev.device_caps = V4L2_CAP_VBI_CAPTURE | V4L2_CAP_READWRITE |
				   V4L2_CAP_STREAMING;
	if (btv->tuner_type != TUNER_ABSENT)
		btv->vbi_dev.device_caps |= V4L2_CAP_TUNER;

	if (video_register_device(&btv->vbi_dev, VFL_TYPE_VBI,
				  vbi_nr[btv->c.nr]) < 0)
		goto err;
	pr_info("%d: registered device %s\n",
		btv->c.nr, video_device_node_name(&btv->vbi_dev));

	if (!btv->has_radio)
		return 0;
	/* radio */
	vdev_init(btv, &btv->radio_dev, &radio_template, "radio");
	btv->radio_dev.device_caps = V4L2_CAP_RADIO | V4L2_CAP_TUNER;
	if (btv->has_saa6588)
		btv->radio_dev.device_caps |= V4L2_CAP_READWRITE |
					      V4L2_CAP_RDS_CAPTURE;
	if (btv->has_tea575x)
		btv->radio_dev.device_caps |= V4L2_CAP_HW_FREQ_SEEK;
	btv->radio_dev.ctrl_handler = &btv->radio_ctrl_handler;
	if (video_register_device(&btv->radio_dev, VFL_TYPE_RADIO,
				  radio_nr[btv->c.nr]) < 0)
		goto err;
	pr_info("%d: registered device %s\n",
		btv->c.nr, video_device_node_name(&btv->radio_dev));

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

static int bttv_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
	struct v4l2_frequency init_freq = {
		.tuner = 0,
		.type = V4L2_TUNER_ANALOG_TV,
		.frequency = 980,
	};
	int result;
	unsigned char lat;
	struct bttv *btv;
	struct v4l2_ctrl_handler *hdl;

	if (bttv_num == BTTV_MAX)
		return -ENOMEM;
	pr_info("Bt8xx card found (%d)\n", bttv_num);
	bttvs[bttv_num] = btv = kzalloc(sizeof(*btv), GFP_KERNEL);
	if (btv == NULL) {
		pr_err("out of memory\n");
		return -ENOMEM;
	}
	btv->c.nr  = bttv_num;
	snprintf(btv->c.v4l2_dev.name, sizeof(btv->c.v4l2_dev.name),
			"bttv%d", btv->c.nr);

	/* initialize structs / fill in defaults */
	mutex_init(&btv->lock);
	spin_lock_init(&btv->s_lock);
	spin_lock_init(&btv->gpio_lock);
	init_waitqueue_head(&btv->i2c_queue);
	INIT_LIST_HEAD(&btv->c.subs);
	INIT_LIST_HEAD(&btv->capture);
	INIT_LIST_HEAD(&btv->vcapture);

	timer_setup(&btv->timeout, bttv_irq_timeout, 0);

	btv->i2c_rc = -1;
	btv->tuner_type  = UNSET;
	btv->new_input   = UNSET;
	btv->has_radio=radio[btv->c.nr];

	/* pci stuff (init, get irq/mmio, ... */
	btv->c.pci = dev;
	btv->id  = dev->device;
	if (pci_enable_device(dev)) {
		pr_warn("%d: Can't enable device\n", btv->c.nr);
		result = -EIO;
		goto free_mem;
	}
	if (dma_set_mask(&dev->dev, DMA_BIT_MASK(32))) {
		pr_warn("%d: No suitable DMA available\n", btv->c.nr);
		result = -EIO;
		goto free_mem;
	}
	if (!request_mem_region(pci_resource_start(dev,0),
				pci_resource_len(dev,0),
				btv->c.v4l2_dev.name)) {
		pr_warn("%d: can't request iomem (0x%llx)\n",
			btv->c.nr,
			(unsigned long long)pci_resource_start(dev, 0));
		result = -EBUSY;
		goto free_mem;
	}
	pci_set_master(dev);
	pci_set_command(dev);

	result = v4l2_device_register(&dev->dev, &btv->c.v4l2_dev);
	if (result < 0) {
		pr_warn("%d: v4l2_device_register() failed\n", btv->c.nr);
		goto fail0;
	}
	hdl = &btv->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 20);
	btv->c.v4l2_dev.ctrl_handler = hdl;
	v4l2_ctrl_handler_init(&btv->radio_ctrl_handler, 6);

	btv->revision = dev->revision;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	pr_info("%d: Bt%d (rev %d) at %s, irq: %d, latency: %d, mmio: 0x%llx\n",
		bttv_num, btv->id, btv->revision, pci_name(dev),
		btv->c.pci->irq, lat,
		(unsigned long long)pci_resource_start(dev, 0));
	schedule();

	btv->bt848_mmio = ioremap(pci_resource_start(dev, 0), 0x1000);
	if (NULL == btv->bt848_mmio) {
		pr_err("%d: ioremap() failed\n", btv->c.nr);
		result = -EIO;
		goto fail1;
	}

	/* identify card */
	bttv_idcard(btv);

	/* disable irqs, register irq handler */
	btwrite(0, BT848_INT_MASK);
	result = request_irq(btv->c.pci->irq, bttv_irq,
	    IRQF_SHARED, btv->c.v4l2_dev.name, (void *)btv);
	if (result < 0) {
		pr_err("%d: can't get IRQ %d\n",
		       bttv_num, btv->c.pci->irq);
		goto fail1;
	}

	if (0 != bttv_handle_chipset(btv)) {
		result = -EIO;
		goto fail2;
	}

	/* init options from insmod args */
	btv->opt_combfilter = combfilter;
	bttv_ctrl_combfilter.def = combfilter;
	bttv_ctrl_lumafilter.def = lumafilter;
	btv->opt_automute   = automute;
	bttv_ctrl_automute.def = automute;
	bttv_ctrl_agc_crush.def = agc_crush;
	btv->opt_vcr_hack   = vcr_hack;
	bttv_ctrl_vcr_hack.def = vcr_hack;
	bttv_ctrl_whitecrush_upper.def = whitecrush_upper;
	bttv_ctrl_whitecrush_lower.def = whitecrush_lower;
	btv->opt_uv_ratio   = uv_ratio;
	bttv_ctrl_uv_ratio.def = uv_ratio;
	bttv_ctrl_full_luma.def = full_luma_range;
	bttv_ctrl_coring.def = coring;

	/* fill struct bttv with some useful defaults */
	btv->fmt = format_by_fourcc(V4L2_PIX_FMT_BGR24);
	btv->width = 320;
	btv->height = 240;
	btv->field = V4L2_FIELD_INTERLACED;
	btv->input = 0;
	btv->tvnorm = 0; /* Index into bttv_tvnorms[] i.e. PAL. */
	bttv_vbi_fmt_reset(&btv->vbi_fmt, btv->tvnorm);
	btv->vbi_count[0] = VBI_DEFLINES;
	btv->vbi_count[1] = VBI_DEFLINES;
	btv->do_crop = 0;

	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 0xff00, 0x100, 32768);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 0xff80, 0x80, 0x6c00);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_SATURATION, 0, 0xff80, 0x80, 32768);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_COLOR_KILLER, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_HUE, 0, 0xff00, 0x100, 32768);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_CHROMA_AGC, 0, 1, 1, !!chroma_agc);
	v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
		V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	if (btv->volume_gpio)
		v4l2_ctrl_new_std(hdl, &bttv_ctrl_ops,
			V4L2_CID_AUDIO_VOLUME, 0, 0xff00, 0x100, 0xff00);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_combfilter, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_automute, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_lumafilter, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_agc_crush, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_vcr_hack, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_whitecrush_lower, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_whitecrush_upper, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_uv_ratio, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_full_luma, NULL);
	v4l2_ctrl_new_custom(hdl, &bttv_ctrl_coring, NULL);

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
	bttv_init_tuner(btv);
	if (btv->tuner_type != TUNER_ABSENT) {
		bttv_set_frequency(btv, &init_freq);
		btv->radio_freq = 90500 * 16; /* 90.5Mhz default */
	}
	btv->std = V4L2_STD_PAL;
	init_irqreg(btv);
	if (!bttv_tvcards[btv->c.type].no_video)
		v4l2_ctrl_handler_setup(hdl);
	if (hdl->error) {
		result = hdl->error;
		goto fail2;
	}
	/* mute device */
	audio_mute(btv, 1);

	/* register video4linux + input */
	if (!bttv_tvcards[btv->c.type].no_video) {
		v4l2_ctrl_add_handler(&btv->radio_ctrl_handler, hdl,
				v4l2_ctrl_radio_filter, false);
		if (btv->radio_ctrl_handler.error) {
			result = btv->radio_ctrl_handler.error;
			goto fail2;
		}
		set_input(btv, btv->input, btv->tvnorm);
		bttv_crop_reset(&btv->crop[0], btv->tvnorm);
		btv->crop[1] = btv->crop[0]; /* current = default */
		disclaim_vbi_lines(btv);
		disclaim_video_lines(btv);
		bttv_register_video(btv);
	}

	/* add subdevices and autoload dvb-bt8xx if needed */
	if (bttv_tvcards[btv->c.type].has_dvb) {
		bttv_sub_add_device(&btv->c, "dvb");
		request_modules(btv);
	}

	if (!disable_ir) {
		init_bttv_i2c_ir(btv);
		bttv_input_init(btv);
	}

	/* everything is fine */
	bttv_num++;
	return 0;

fail2:
	free_irq(btv->c.pci->irq,btv);

fail1:
	v4l2_ctrl_handler_free(&btv->ctrl_handler);
	v4l2_ctrl_handler_free(&btv->radio_ctrl_handler);
	v4l2_device_unregister(&btv->c.v4l2_dev);

fail0:
	if (btv->bt848_mmio)
		iounmap(btv->bt848_mmio);
	release_mem_region(pci_resource_start(btv->c.pci,0),
			   pci_resource_len(btv->c.pci,0));
	pci_disable_device(btv->c.pci);

free_mem:
	bttvs[btv->c.nr] = NULL;
	kfree(btv);
	return result;
}

static void bttv_remove(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct bttv *btv = to_bttv(v4l2_dev);

	if (bttv_verbose)
		pr_info("%d: unloading\n", btv->c.nr);

	if (bttv_tvcards[btv->c.type].has_dvb)
		flush_request_modules(btv);

	/* shutdown everything (DMA+IRQs) */
	btand(~15, BT848_GPIO_DMA_CTL);
	btwrite(0, BT848_INT_MASK);
	btwrite(~0x0, BT848_INT_STAT);
	btwrite(0x0, BT848_GPIO_OUT_EN);
	if (bttv_gpio)
		bttv_gpio_tracking(btv,"cleanup");

	/* tell gpio modules we are leaving ... */
	btv->shutdown=1;
	bttv_input_fini(btv);
	bttv_sub_del_devices(&btv->c);

	/* unregister i2c_bus + input */
	fini_bttv_i2c(btv);

	/* unregister video4linux */
	bttv_unregister_video(btv);

	/* free allocated memory */
	v4l2_ctrl_handler_free(&btv->ctrl_handler);
	v4l2_ctrl_handler_free(&btv->radio_ctrl_handler);
	btcx_riscmem_free(btv->c.pci,&btv->main);

	/* free resources */
	free_irq(btv->c.pci->irq,btv);
	timer_delete_sync(&btv->timeout);
	iounmap(btv->bt848_mmio);
	release_mem_region(pci_resource_start(btv->c.pci,0),
			   pci_resource_len(btv->c.pci,0));
	pci_disable_device(btv->c.pci);

	v4l2_device_unregister(&btv->c.v4l2_dev);
	bttvs[btv->c.nr] = NULL;
	kfree(btv);

	return;
}

static int __maybe_unused bttv_suspend(struct device *dev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);
	struct bttv *btv = to_bttv(v4l2_dev);
	struct bttv_buffer_set idle;
	unsigned long flags;

	dprintk("%d: suspend\n", btv->c.nr);

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

	btv->state.disabled = 1;
	return 0;
}

static int __maybe_unused bttv_resume(struct device *dev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(dev);
	struct bttv *btv = to_bttv(v4l2_dev);
	unsigned long flags;

	dprintk("%d: resume\n", btv->c.nr);

	btv->state.disabled = 0;

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

static const struct pci_device_id bttv_pci_tbl[] = {
	{PCI_VDEVICE(BROOKTREE, PCI_DEVICE_ID_BT848), 0},
	{PCI_VDEVICE(BROOKTREE, PCI_DEVICE_ID_BT849), 0},
	{PCI_VDEVICE(BROOKTREE, PCI_DEVICE_ID_BT878), 0},
	{PCI_VDEVICE(BROOKTREE, PCI_DEVICE_ID_BT879), 0},
	{PCI_VDEVICE(BROOKTREE, PCI_DEVICE_ID_FUSION879), 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, bttv_pci_tbl);

static SIMPLE_DEV_PM_OPS(bttv_pm_ops,
			 bttv_suspend,
			 bttv_resume);

static struct pci_driver bttv_pci_driver = {
	.name      = "bttv",
	.id_table  = bttv_pci_tbl,
	.probe     = bttv_probe,
	.remove    = bttv_remove,
	.driver.pm = &bttv_pm_ops,
};

static int __init bttv_init_module(void)
{
	int ret;

	bttv_num = 0;

	pr_info("driver version %s loaded\n", BTTV_VERSION);
	if (gbuffers < 2 || gbuffers > VIDEO_MAX_FRAME)
		gbuffers = 2;
	if (gbufsize > BTTV_MAX_FBUF)
		gbufsize = BTTV_MAX_FBUF;
	gbufsize = (gbufsize + PAGE_SIZE - 1) & PAGE_MASK;
	if (bttv_verbose)
		pr_info("using %d buffers with %dk (%d pages) each for capture\n",
			gbuffers, gbufsize >> 10, gbufsize >> PAGE_SHIFT);

	bttv_check_chipset();

	ret = bus_register(&bttv_sub_bus_type);
	if (ret < 0) {
		pr_warn("bus_register error: %d\n", ret);
		return ret;
	}
	ret = pci_register_driver(&bttv_pci_driver);
	if (ret < 0)
		bus_unregister(&bttv_sub_bus_type);

	return ret;
}

static void __exit bttv_cleanup_module(void)
{
	pci_unregister_driver(&bttv_pci_driver);
	bus_unregister(&bttv_sub_bus_type);
}

module_init(bttv_init_module);
module_exit(bttv_cleanup_module);
