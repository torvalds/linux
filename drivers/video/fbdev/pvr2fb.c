/*
 * drivers/video/pvr2fb.c
 *
 * Frame buffer and fbcon support for the NEC PowerVR2 found within the Sega
 * Dreamcast.
 *
 * Copyright (c) 2001 M. R. Brown <mrbrown@0xd6.org>
 * Copyright (c) 2001 - 2008  Paul Mundt <lethal@linux-sh.org>
 *
 * This driver is mostly based on the excellent amifb and vfb sources.  It uses
 * an odd scheme for converting hardware values to/from framebuffer values,
 * here are some hacked-up formulas:
 *
 *  The Dreamcast has screen offsets from each side of its four borders and
 *  the start offsets of the display window.  I used these values to calculate
 *  'pseudo' values (think of them as placeholders) for the fb video mode, so
 *  that when it came time to convert these values back into their hardware
 *  values, I could just add mode- specific offsets to get the correct mode
 *  settings:
 *
 *      left_margin = diwstart_h - borderstart_h;
 *      right_margin = borderstop_h - (diwstart_h + xres);
 *      upper_margin = diwstart_v - borderstart_v;
 *      lower_margin = borderstop_v - (diwstart_h + yres);
 *
 *      hsync_len = borderstart_h + (hsync_total - borderstop_h);
 *      vsync_len = borderstart_v + (vsync_total - borderstop_v);
 *
 *  Then, when it's time to convert back to hardware settings, the only
 *  constants are the borderstart_* offsets, all other values are derived from
 *  the fb video mode:
 *
 *      // PAL
 *      borderstart_h = 116;
 *      borderstart_v = 44;
 *      ...
 *      borderstop_h = borderstart_h + hsync_total - hsync_len;
 *      ...
 *      diwstart_v = borderstart_v - upper_margin;
 *
 *  However, in the current implementation, the borderstart values haven't had
 *  the benefit of being fully researched, so some modes may be broken.
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

#ifdef CONFIG_SH_DREAMCAST
#include <asm/machvec.h>
#include <mach-dreamcast/mach/sysasic.h>
#endif

#ifdef CONFIG_PVR2_DMA
#include <linux/pagemap.h>
#include <mach/dma.h>
#include <asm/dma.h>
#endif

#ifdef CONFIG_SH_STORE_QUEUES
#include <linux/uaccess.h>
#include <cpu/sq.h>
#endif

#ifndef PCI_DEVICE_ID_NEC_NEON250
#  define PCI_DEVICE_ID_NEC_NEON250	0x0067
#endif

/* 2D video registers */
#define DISP_BASE	par->mmio_base
#define DISP_BRDRCOLR (DISP_BASE + 0x40)
#define DISP_DIWMODE (DISP_BASE + 0x44)
#define DISP_DIWADDRL (DISP_BASE + 0x50)
#define DISP_DIWADDRS (DISP_BASE + 0x54)
#define DISP_DIWSIZE (DISP_BASE + 0x5c)
#define DISP_SYNCCONF (DISP_BASE + 0xd0)
#define DISP_BRDRHORZ (DISP_BASE + 0xd4)
#define DISP_SYNCSIZE (DISP_BASE + 0xd8)
#define DISP_BRDRVERT (DISP_BASE + 0xdc)
#define DISP_DIWCONF (DISP_BASE + 0xe8)
#define DISP_DIWHSTRT (DISP_BASE + 0xec)
#define DISP_DIWVSTRT (DISP_BASE + 0xf0)
#define DISP_PIXDEPTH (DISP_BASE + 0x108)

/* Pixel clocks, one for TV output, doubled for VGA output */
#define TV_CLK 74239
#define VGA_CLK 37119

/* This is for 60Hz - the VTOTAL is doubled for interlaced modes */
#define PAL_HTOTAL 863
#define PAL_VTOTAL 312
#define NTSC_HTOTAL 857
#define NTSC_VTOTAL 262

/* Supported cable types */
enum { CT_VGA, CT_NONE, CT_RGB, CT_COMPOSITE };

/* Supported video output types */
enum { VO_PAL, VO_NTSC, VO_VGA };

/* Supported palette types */
enum { PAL_ARGB1555, PAL_RGB565, PAL_ARGB4444, PAL_ARGB8888 };

struct pvr2_params { unsigned int val; char *name; };
static struct pvr2_params cables[] = {
	{ CT_VGA, "VGA" }, { CT_RGB, "RGB" }, { CT_COMPOSITE, "COMPOSITE" },
};

static struct pvr2_params outputs[] = {
	{ VO_PAL, "PAL" }, { VO_NTSC, "NTSC" }, { VO_VGA, "VGA" },
};

/*
 * This describes the current video mode
 */

static struct pvr2fb_par {
	unsigned int hsync_total;	/* Clocks/line */
	unsigned int vsync_total;	/* Lines/field */
	unsigned int borderstart_h;
	unsigned int borderstop_h;
	unsigned int borderstart_v;
	unsigned int borderstop_v;
	unsigned int diwstart_h;	/* Horizontal offset of the display field */
	unsigned int diwstart_v;	/* Vertical offset of the display field, for
				   interlaced modes, this is the long field */
	unsigned long disp_start;	/* Address of image within VRAM */
	unsigned char is_interlaced;	/* Is the display interlaced? */
	unsigned char is_doublescan;	/* Are scanlines output twice? (doublescan) */
	unsigned char is_lowres;	/* Is horizontal pixel-doubling enabled? */

	unsigned long mmio_base;	/* MMIO base */
	u32 palette[16];
} *currentpar;

static struct fb_info *fb_info;

static struct fb_fix_screeninfo pvr2_fix = {
	.id =		"NEC PowerVR2",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.ypanstep =	1,
	.ywrapstep =	1,
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo pvr2_var = {
	.xres =		640,
	.yres =		480,
	.xres_virtual =	640,
	.yres_virtual = 480,
	.bits_per_pixel	=16,
	.red =		{ 11, 5, 0 },
	.green =	{  5, 6, 0 },
	.blue =		{  0, 5, 0 },
	.activate =	FB_ACTIVATE_NOW,
	.height =	-1,
	.width =	-1,
	.vmode =	FB_VMODE_NONINTERLACED,
};

static int cable_type = CT_VGA;
static int video_output = VO_VGA;

static int nopan = 0;
static int nowrap = 1;

/*
 * We do all updating, blanking, etc. during the vertical retrace period
 */
static unsigned int do_vmode_full = 0;	/* Change the video mode */
static unsigned int do_vmode_pan = 0;	/* Update the video mode */
static short do_blank = 0;		/* (Un)Blank the screen */

static unsigned int is_blanked = 0;		/* Is the screen blanked? */

#ifdef CONFIG_SH_STORE_QUEUES
static unsigned long pvr2fb_map;
#endif

#ifdef CONFIG_PVR2_DMA
static unsigned int shdma = PVR2_CASCADE_CHAN;
static unsigned int pvr2dma = ONCHIP_NR_DMA_CHANNELS;
#endif

static int pvr2fb_setcolreg(unsigned int regno, unsigned int red, unsigned int green, unsigned int blue,
                            unsigned int transp, struct fb_info *info);
static int pvr2fb_blank(int blank, struct fb_info *info);
static unsigned long get_line_length(int xres_virtual, int bpp);
static void set_color_bitfields(struct fb_var_screeninfo *var);
static int pvr2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int pvr2fb_set_par(struct fb_info *info);
static void pvr2_update_display(struct fb_info *info);
static void pvr2_init_display(struct fb_info *info);
static void pvr2_do_blank(void);
static irqreturn_t pvr2fb_interrupt(int irq, void *dev_id);
static int pvr2_init_cable(void);
static int pvr2_get_param(const struct pvr2_params *p, const char *s,
                            int val, int size);
#ifdef CONFIG_PVR2_DMA
static ssize_t pvr2fb_write(struct fb_info *info, const char *buf,
			    size_t count, loff_t *ppos);
#endif

static struct fb_ops pvr2fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= pvr2fb_setcolreg,
	.fb_blank	= pvr2fb_blank,
	.fb_check_var	= pvr2fb_check_var,
	.fb_set_par	= pvr2fb_set_par,
#ifdef CONFIG_PVR2_DMA
	.fb_write	= pvr2fb_write,
#endif
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static struct fb_videomode pvr2_modedb[] = {
    /*
     * Broadcast video modes (PAL and NTSC).  I'm unfamiliar with
     * PAL-M and PAL-N, but from what I've read both modes parallel PAL and
     * NTSC, so it shouldn't be a problem (I hope).
     */

    {
	/* 640x480 @ 60Hz interlaced (NTSC) */
	"ntsc_640x480i", 60, 640, 480, TV_CLK, 38, 33, 0, 18, 146, 26,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED | FB_VMODE_YWRAP
    }, {
	/* 640x240 @ 60Hz (NTSC) */
	/* XXX: Broken! Don't use... */
	"ntsc_640x240", 60, 640, 240, TV_CLK, 38, 33, 0, 0, 146, 22,
	FB_SYNC_BROADCAST, FB_VMODE_YWRAP
    }, {
	/* 640x480 @ 60hz (VGA) */
	"vga_640x480", 60, 640, 480, VGA_CLK, 38, 33, 0, 18, 146, 26,
	0, FB_VMODE_YWRAP
    },
};

#define NUM_TOTAL_MODES  ARRAY_SIZE(pvr2_modedb)

#define DEFMODE_NTSC	0
#define DEFMODE_PAL	0
#define DEFMODE_VGA	2

static int defmode = DEFMODE_NTSC;
static char *mode_option = NULL;

static inline void pvr2fb_set_pal_type(unsigned int type)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *)fb_info->par;

	fb_writel(type, par->mmio_base + 0x108);
}

static inline void pvr2fb_set_pal_entry(struct pvr2fb_par *par,
					unsigned int regno,
					unsigned int val)
{
	fb_writel(val, par->mmio_base + 0x1000 + (4 * regno));
}

static int pvr2fb_blank(int blank, struct fb_info *info)
{
	do_blank = blank ? blank : -1;
	return 0;
}

static inline unsigned long get_line_length(int xres_virtual, int bpp)
{
	return (unsigned long)((((xres_virtual*bpp)+31)&~31) >> 3);
}

static void set_color_bitfields(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	    case 16:        /* RGB 565 */
		pvr2fb_set_pal_type(PAL_RGB565);
		var->red.offset = 11;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	    case 24:        /* RGB 888 */
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	    case 32:        /* ARGB 8888 */
		pvr2fb_set_pal_type(PAL_ARGB8888);
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		break;
	}
}

static int pvr2fb_setcolreg(unsigned int regno, unsigned int red,
			    unsigned int green, unsigned int blue,
                            unsigned int transp, struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *)info->par;
	unsigned int tmp;

	if (regno > info->cmap.len)
		return 1;

	/*
	 * We only support the hardware palette for 16 and 32bpp. It's also
	 * expected that the palette format has been set by the time we get
	 * here, so we don't waste time setting it again.
	 */
	switch (info->var.bits_per_pixel) {
	    case 16: /* RGB 565 */
		tmp =  (red   & 0xf800)       |
		      ((green & 0xfc00) >> 5) |
		      ((blue  & 0xf800) >> 11);

		pvr2fb_set_pal_entry(par, regno, tmp);
		break;
	    case 24: /* RGB 888 */
		red >>= 8; green >>= 8; blue >>= 8;
		tmp = (red << 16) | (green << 8) | blue;
		break;
	    case 32: /* ARGB 8888 */
		red >>= 8; green >>= 8; blue >>= 8;
		tmp = (transp << 24) | (red << 16) | (green << 8) | blue;

		pvr2fb_set_pal_entry(par, regno, tmp);
		break;
	    default:
		pr_debug("Invalid bit depth %d?!?\n", info->var.bits_per_pixel);
		return 1;
	}

	if (regno < 16)
		((u32*)(info->pseudo_palette))[regno] = tmp;

	return 0;
}

static int pvr2fb_set_par(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *)info->par;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long line_length;
	unsigned int vtotal;

	/*
	 * XXX: It's possible that a user could use a VGA box, change the cable
	 * type in hardware (i.e. switch from VGA<->composite), then change
	 * modes (i.e. switching to another VT).  If that happens we should
	 * automagically change the output format to cope, but currently I
	 * don't have a VGA box to make sure this works properly.
	 */
	cable_type = pvr2_init_cable();
	if (cable_type == CT_VGA && video_output != VO_VGA)
		video_output = VO_VGA;

	var->vmode &= FB_VMODE_MASK;
	if (var->vmode & FB_VMODE_INTERLACED && video_output != VO_VGA)
		par->is_interlaced = 1;
	/*
	 * XXX: Need to be more creative with this (i.e. allow doublecan for
	 * PAL/NTSC output).
	 */
	if (var->vmode & FB_VMODE_DOUBLE && video_output == VO_VGA)
		par->is_doublescan = 1;

	par->hsync_total = var->left_margin + var->xres + var->right_margin +
	                   var->hsync_len;
	par->vsync_total = var->upper_margin + var->yres + var->lower_margin +
	                   var->vsync_len;

	if (var->sync & FB_SYNC_BROADCAST) {
		vtotal = par->vsync_total;
		if (par->is_interlaced)
			vtotal /= 2;
		if (vtotal > (PAL_VTOTAL + NTSC_VTOTAL)/2) {
			/* XXX: Check for start values here... */
			/* XXX: Check hardware for PAL-compatibility */
			par->borderstart_h = 116;
			par->borderstart_v = 44;
		} else {
			/* NTSC video output */
			par->borderstart_h = 126;
			par->borderstart_v = 18;
		}
	} else {
		/* VGA mode */
		/* XXX: What else needs to be checked? */
		/*
		 * XXX: We have a little freedom in VGA modes, what ranges
		 * should be here (i.e. hsync/vsync totals, etc.)?
		 */
		par->borderstart_h = 126;
		par->borderstart_v = 40;
	}

	/* Calculate the remainding offsets */
	par->diwstart_h = par->borderstart_h + var->left_margin;
	par->diwstart_v = par->borderstart_v + var->upper_margin;
	par->borderstop_h = par->diwstart_h + var->xres +
			    var->right_margin;
	par->borderstop_v = par->diwstart_v + var->yres +
			    var->lower_margin;

	if (!par->is_interlaced)
		par->borderstop_v /= 2;
	if (info->var.xres < 640)
		par->is_lowres = 1;

	line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
	par->disp_start = info->fix.smem_start + (line_length * var->yoffset) * line_length;
	info->fix.line_length = line_length;
	return 0;
}

static int pvr2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *)info->par;
	unsigned int vtotal, hsync_total;
	unsigned long line_length;

	if (var->pixclock != TV_CLK && var->pixclock != VGA_CLK) {
		pr_debug("Invalid pixclock value %d\n", var->pixclock);
		return -EINVAL;
	}

	if (var->xres < 320)
		var->xres = 320;
	if (var->yres < 240)
		var->yres = 240;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 24)
		var->bits_per_pixel = 24;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;

	set_color_bitfields(var);

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->xoffset || var->yoffset < 0 ||
		    var->yoffset >= var->yres_virtual) {
			var->xoffset = var->yoffset = 0;
		} else {
			if (var->xoffset > var->xres_virtual - var->xres ||
			    var->yoffset > var->yres_virtual - var->yres ||
			    var->xoffset < 0 || var->yoffset < 0)
				var->xoffset = var->yoffset = 0;
		}
	} else {
		var->xoffset = var->yoffset = 0;
	}

	/*
	 * XXX: Need to be more creative with this (i.e. allow doublecan for
	 * PAL/NTSC output).
	 */
	if (var->yres < 480 && video_output == VO_VGA)
		var->vmode |= FB_VMODE_DOUBLE;

	if (video_output != VO_VGA) {
		var->sync |= FB_SYNC_BROADCAST;
		var->vmode |= FB_VMODE_INTERLACED;
	} else {
		var->sync &= ~FB_SYNC_BROADCAST;
		var->vmode &= ~FB_VMODE_INTERLACED;
		var->vmode |= FB_VMODE_NONINTERLACED;
	}

	if ((var->activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_TEST) {
		var->right_margin = par->borderstop_h -
				   (par->diwstart_h + var->xres);
		var->left_margin  = par->diwstart_h - par->borderstart_h;
		var->hsync_len    = par->borderstart_h +
		                   (par->hsync_total - par->borderstop_h);

		var->upper_margin = par->diwstart_v - par->borderstart_v;
		var->lower_margin = par->borderstop_v -
				   (par->diwstart_v + var->yres);
		var->vsync_len    = par->borderstop_v +
				   (par->vsync_total - par->borderstop_v);
	}

	hsync_total = var->left_margin + var->xres + var->right_margin +
		      var->hsync_len;
	vtotal = var->upper_margin + var->yres + var->lower_margin +
		 var->vsync_len;

	if (var->sync & FB_SYNC_BROADCAST) {
		if (var->vmode & FB_VMODE_INTERLACED)
			vtotal /= 2;
		if (vtotal > (PAL_VTOTAL + NTSC_VTOTAL)/2) {
			/* PAL video output */
			/* XXX: Should be using a range here ... ? */
			if (hsync_total != PAL_HTOTAL) {
				pr_debug("invalid hsync total for PAL\n");
				return -EINVAL;
			}
		} else {
			/* NTSC video output */
			if (hsync_total != NTSC_HTOTAL) {
				pr_debug("invalid hsync total for NTSC\n");
				return -EINVAL;
			}
		}
	}

	/* Check memory sizes */
	line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (line_length * var->yres_virtual > info->fix.smem_len)
		return -ENOMEM;

	return 0;
}

static void pvr2_update_display(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *) info->par;
	struct fb_var_screeninfo *var = &info->var;

	/* Update the start address of the display image */
	fb_writel(par->disp_start, DISP_DIWADDRL);
	fb_writel(par->disp_start +
		  get_line_length(var->xoffset+var->xres, var->bits_per_pixel),
	          DISP_DIWADDRS);
}

/*
 * Initialize the video mode.  Currently, the 16bpp and 24bpp modes aren't
 * very stable.  It's probably due to the fact that a lot of the 2D video
 * registers are still undocumented.
 */

static void pvr2_init_display(struct fb_info *info)
{
	struct pvr2fb_par *par = (struct pvr2fb_par *) info->par;
	struct fb_var_screeninfo *var = &info->var;
	unsigned int diw_height, diw_width, diw_modulo = 1;
	unsigned int bytesperpixel = var->bits_per_pixel >> 3;

	/* hsync and vsync totals */
	fb_writel((par->vsync_total << 16) | par->hsync_total, DISP_SYNCSIZE);

	/* column height, modulo, row width */
	/* since we're "panning" within vram, we need to offset things based
	 * on the offset from the virtual x start to our real gfx. */
	if (video_output != VO_VGA && par->is_interlaced)
		diw_modulo += info->fix.line_length / 4;
	diw_height = (par->is_interlaced ? var->yres / 2 : var->yres);
	diw_width = get_line_length(var->xres, var->bits_per_pixel) / 4;
	fb_writel((diw_modulo << 20) | (--diw_height << 10) | --diw_width,
	          DISP_DIWSIZE);

	/* display address, long and short fields */
	fb_writel(par->disp_start, DISP_DIWADDRL);
	fb_writel(par->disp_start +
	          get_line_length(var->xoffset+var->xres, var->bits_per_pixel),
	          DISP_DIWADDRS);

	/* border horizontal, border vertical, border color */
	fb_writel((par->borderstart_h << 16) | par->borderstop_h, DISP_BRDRHORZ);
	fb_writel((par->borderstart_v << 16) | par->borderstop_v, DISP_BRDRVERT);
	fb_writel(0, DISP_BRDRCOLR);

	/* display window start position */
	fb_writel(par->diwstart_h, DISP_DIWHSTRT);
	fb_writel((par->diwstart_v << 16) | par->diwstart_v, DISP_DIWVSTRT);

	/* misc. settings */
	fb_writel((0x16 << 16) | par->is_lowres, DISP_DIWCONF);

	/* clock doubler (for VGA), scan doubler, display enable */
	fb_writel(((video_output == VO_VGA) << 23) |
	          (par->is_doublescan << 1) | 1, DISP_DIWMODE);

	/* bits per pixel */
	fb_writel(fb_readl(DISP_DIWMODE) | (--bytesperpixel << 2), DISP_DIWMODE);
	fb_writel(bytesperpixel << 2, DISP_PIXDEPTH);

	/* video enable, color sync, interlace,
	 * hsync and vsync polarity (currently unused) */
	fb_writel(0x100 | ((par->is_interlaced /*|4*/) << 4), DISP_SYNCCONF);
}

/* Simulate blanking by making the border cover the entire screen */

#define BLANK_BIT (1<<3)

static void pvr2_do_blank(void)
{
	struct pvr2fb_par *par = currentpar;
	unsigned long diwconf;

	diwconf = fb_readl(DISP_DIWCONF);
	if (do_blank > 0)
		fb_writel(diwconf | BLANK_BIT, DISP_DIWCONF);
	else
		fb_writel(diwconf & ~BLANK_BIT, DISP_DIWCONF);

	is_blanked = do_blank > 0 ? do_blank : 0;
}

static irqreturn_t pvr2fb_interrupt(int irq, void *dev_id)
{
	struct fb_info *info = dev_id;

	if (do_vmode_pan || do_vmode_full)
		pvr2_update_display(info);
	if (do_vmode_full)
		pvr2_init_display(info);
	if (do_vmode_pan)
		do_vmode_pan = 0;
	if (do_vmode_full)
		do_vmode_full = 0;
	if (do_blank) {
		pvr2_do_blank();
		do_blank = 0;
	}
	return IRQ_HANDLED;
}

/*
 * Determine the cable type and initialize the cable output format.  Don't do
 * anything if the cable type has been overidden (via "cable:XX").
 */

#define PCTRA 0xff80002c
#define PDTRA 0xff800030
#define VOUTC 0xa0702c00

static int pvr2_init_cable(void)
{
	if (cable_type < 0) {
		fb_writel((fb_readl(PCTRA) & 0xfff0ffff) | 0x000a0000,
	                  PCTRA);
		cable_type = (fb_readw(PDTRA) >> 8) & 3;
	}

	/* Now select the output format (either composite or other) */
	/* XXX: Save the previous val first, as this reg is also AICA
	  related */
	if (cable_type == CT_COMPOSITE)
		fb_writel(3 << 8, VOUTC);
	else if (cable_type == CT_RGB)
		fb_writel(1 << 9, VOUTC);
	else
		fb_writel(0, VOUTC);

	return cable_type;
}

#ifdef CONFIG_PVR2_DMA
static ssize_t pvr2fb_write(struct fb_info *info, const char *buf,
			    size_t count, loff_t *ppos)
{
	unsigned long dst, start, end, len;
	unsigned int nr_pages;
	struct page **pages;
	int ret, i;

	nr_pages = (count + PAGE_SIZE - 1) >> PAGE_SHIFT;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = get_user_pages_unlocked((unsigned long)buf, nr_pages, WRITE,
			0, pages);

	if (ret < nr_pages) {
		nr_pages = ret;
		ret = -EINVAL;
		goto out_unmap;
	}

	dma_configure_channel(shdma, 0x12c1);

	dst   = (unsigned long)fb_info->screen_base + *ppos;
	start = (unsigned long)page_address(pages[0]);
	end   = (unsigned long)page_address(pages[nr_pages]);
	len   = nr_pages << PAGE_SHIFT;

	/* Half-assed contig check */
	if (start + len == end) {
		/* As we do this in one shot, it's either all or nothing.. */
		if ((*ppos + len) > fb_info->fix.smem_len) {
			ret = -ENOSPC;
			goto out_unmap;
		}

		dma_write(shdma, start, 0, len);
		dma_write(pvr2dma, 0, dst, len);
		dma_wait_for_completion(pvr2dma);

		goto out;
	}

	/* Not contiguous, writeout per-page instead.. */
	for (i = 0; i < nr_pages; i++, dst += PAGE_SIZE) {
		if ((*ppos + (i << PAGE_SHIFT)) > fb_info->fix.smem_len) {
			ret = -ENOSPC;
			goto out_unmap;
		}

		dma_write_page(shdma, (unsigned long)page_address(pages[i]), 0);
		dma_write_page(pvr2dma, 0, dst);
		dma_wait_for_completion(pvr2dma);
	}

out:
	*ppos += count;
	ret = count;

out_unmap:
	for (i = 0; i < nr_pages; i++)
		page_cache_release(pages[i]);

	kfree(pages);

	return ret;
}
#endif /* CONFIG_PVR2_DMA */

/**
 * pvr2fb_common_init
 *
 * Common init code for the PVR2 chips.
 *
 * This mostly takes care of the common aspects of the fb setup and
 * registration. It's expected that the board-specific init code has
 * already setup pvr2_fix with something meaningful at this point.
 *
 * Device info reporting is also done here, as well as picking a sane
 * default from the modedb. For board-specific modelines, simply define
 * a per-board modedb.
 *
 * Also worth noting is that the cable and video output types are likely
 * always going to be VGA for the PCI-based PVR2 boards, but we leave this
 * in for flexibility anyways. Who knows, maybe someone has tv-out on a
 * PCI-based version of these things ;-)
 */
static int pvr2fb_common_init(void)
{
	struct pvr2fb_par *par = currentpar;
	unsigned long modememused, rev;

	fb_info->screen_base = ioremap_nocache(pvr2_fix.smem_start,
					       pvr2_fix.smem_len);

	if (!fb_info->screen_base) {
		printk(KERN_ERR "pvr2fb: Failed to remap smem space\n");
		goto out_err;
	}

	par->mmio_base = (unsigned long)ioremap_nocache(pvr2_fix.mmio_start,
							pvr2_fix.mmio_len);
	if (!par->mmio_base) {
		printk(KERN_ERR "pvr2fb: Failed to remap mmio space\n");
		goto out_err;
	}

	fb_memset(fb_info->screen_base, 0, pvr2_fix.smem_len);

	pvr2_fix.ypanstep	= nopan  ? 0 : 1;
	pvr2_fix.ywrapstep	= nowrap ? 0 : 1;

	fb_info->fbops		= &pvr2fb_ops;
	fb_info->fix		= pvr2_fix;
	fb_info->par		= currentpar;
	fb_info->pseudo_palette	= currentpar->palette;
	fb_info->flags		= FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	if (video_output == VO_VGA)
		defmode = DEFMODE_VGA;

	if (!mode_option)
		mode_option = "640x480@60";

	if (!fb_find_mode(&fb_info->var, fb_info, mode_option, pvr2_modedb,
	                  NUM_TOTAL_MODES, &pvr2_modedb[defmode], 16))
		fb_info->var = pvr2_var;

	fb_alloc_cmap(&fb_info->cmap, 256, 0);

	if (register_framebuffer(fb_info) < 0)
		goto out_err;
	/*Must write PIXDEPTH to register before anything is displayed - so force init */
	pvr2_init_display(fb_info);

	modememused = get_line_length(fb_info->var.xres_virtual,
				      fb_info->var.bits_per_pixel);
	modememused *= fb_info->var.yres_virtual;

	rev = fb_readl(par->mmio_base + 0x04);

	fb_info(fb_info, "%s (rev %ld.%ld) frame buffer device, using %ldk/%ldk of video memory\n",
		fb_info->fix.id, (rev >> 4) & 0x0f, rev & 0x0f,
		modememused >> 10,
		(unsigned long)(fb_info->fix.smem_len >> 10));
	fb_info(fb_info, "Mode %dx%d-%d pitch = %ld cable: %s video output: %s\n",
		fb_info->var.xres, fb_info->var.yres,
		fb_info->var.bits_per_pixel,
		get_line_length(fb_info->var.xres, fb_info->var.bits_per_pixel),
		(char *)pvr2_get_param(cables, NULL, cable_type, 3),
		(char *)pvr2_get_param(outputs, NULL, video_output, 3));

#ifdef CONFIG_SH_STORE_QUEUES
	fb_notice(fb_info, "registering with SQ API\n");

	pvr2fb_map = sq_remap(fb_info->fix.smem_start, fb_info->fix.smem_len,
			      fb_info->fix.id, PAGE_SHARED);

	fb_notice(fb_info, "Mapped video memory to SQ addr 0x%lx\n",
		  pvr2fb_map);
#endif

	return 0;

out_err:
	if (fb_info->screen_base)
		iounmap(fb_info->screen_base);
	if (par->mmio_base)
		iounmap((void *)par->mmio_base);

	return -ENXIO;
}

#ifdef CONFIG_SH_DREAMCAST
static int __init pvr2fb_dc_init(void)
{
	if (!mach_is_dreamcast())
		return -ENXIO;

	/* Make a guess at the monitor based on the attached cable */
	if (pvr2_init_cable() == CT_VGA) {
		fb_info->monspecs.hfmin = 30000;
		fb_info->monspecs.hfmax = 70000;
		fb_info->monspecs.vfmin = 60;
		fb_info->monspecs.vfmax = 60;
	} else {
		/* Not VGA, using a TV (taken from acornfb) */
		fb_info->monspecs.hfmin = 15469;
		fb_info->monspecs.hfmax = 15781;
		fb_info->monspecs.vfmin = 49;
		fb_info->monspecs.vfmax = 51;
	}

	/*
	 * XXX: This needs to pull default video output via BIOS or other means
	 */
	if (video_output < 0) {
		if (cable_type == CT_VGA) {
			video_output = VO_VGA;
		} else {
			video_output = VO_NTSC;
		}
	}

	/*
	 * Nothing exciting about the DC PVR2 .. only a measly 8MiB.
	 */
	pvr2_fix.smem_start	= 0xa5000000;	/* RAM starts here */
	pvr2_fix.smem_len	= 8 << 20;

	pvr2_fix.mmio_start	= 0xa05f8000;	/* registers start here */
	pvr2_fix.mmio_len	= 0x2000;

	if (request_irq(HW_EVENT_VSYNC, pvr2fb_interrupt, IRQF_SHARED,
	                "pvr2 VBL handler", fb_info)) {
		return -EBUSY;
	}

#ifdef CONFIG_PVR2_DMA
	if (request_dma(pvr2dma, "pvr2") != 0) {
		free_irq(HW_EVENT_VSYNC, fb_info);
		return -EBUSY;
	}
#endif

	return pvr2fb_common_init();
}

static void __exit pvr2fb_dc_exit(void)
{
	if (fb_info->screen_base) {
		iounmap(fb_info->screen_base);
		fb_info->screen_base = NULL;
	}
	if (currentpar->mmio_base) {
		iounmap((void *)currentpar->mmio_base);
		currentpar->mmio_base = 0;
	}

	free_irq(HW_EVENT_VSYNC, fb_info);
#ifdef CONFIG_PVR2_DMA
	free_dma(pvr2dma);
#endif
}
#endif /* CONFIG_SH_DREAMCAST */

#ifdef CONFIG_PCI
static int pvr2fb_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		printk(KERN_ERR "pvr2fb: PCI enable failed\n");
		return ret;
	}

	ret = pci_request_regions(pdev, "pvr2fb");
	if (ret) {
		printk(KERN_ERR "pvr2fb: PCI request regions failed\n");
		return ret;
	}

	/*
	 * Slightly more exciting than the DC PVR2 .. 16MiB!
	 */
	pvr2_fix.smem_start	= pci_resource_start(pdev, 0);
	pvr2_fix.smem_len	= pci_resource_len(pdev, 0);

	pvr2_fix.mmio_start	= pci_resource_start(pdev, 1);
	pvr2_fix.mmio_len	= pci_resource_len(pdev, 1);

	fb_info->device		= &pdev->dev;

	return pvr2fb_common_init();
}

static void pvr2fb_pci_remove(struct pci_dev *pdev)
{
	if (fb_info->screen_base) {
		iounmap(fb_info->screen_base);
		fb_info->screen_base = NULL;
	}
	if (currentpar->mmio_base) {
		iounmap((void *)currentpar->mmio_base);
		currentpar->mmio_base = 0;
	}

	pci_release_regions(pdev);
}

static struct pci_device_id pvr2fb_pci_tbl[] = {
	{ PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_NEON250,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, pvr2fb_pci_tbl);

static struct pci_driver pvr2fb_pci_driver = {
	.name		= "pvr2fb",
	.id_table	= pvr2fb_pci_tbl,
	.probe		= pvr2fb_pci_probe,
	.remove		= pvr2fb_pci_remove,
};

static int __init pvr2fb_pci_init(void)
{
	return pci_register_driver(&pvr2fb_pci_driver);
}

static void __exit pvr2fb_pci_exit(void)
{
	pci_unregister_driver(&pvr2fb_pci_driver);
}
#endif /* CONFIG_PCI */

static int pvr2_get_param(const struct pvr2_params *p, const char *s, int val,
			  int size)
{
	int i;

	for (i = 0 ; i < size ; i++ ) {
		if (s != NULL) {
			if (!strncasecmp(p[i].name, s, strlen(s)))
				return p[i].val;
		} else {
			if (p[i].val == val)
				return (int)p[i].name;
		}
	}
	return -1;
}

/*
 * Parse command arguments.  Supported arguments are:
 *    inverse                             Use inverse color maps
 *    cable:composite|rgb|vga             Override the video cable type
 *    output:NTSC|PAL|VGA                 Override the video output format
 *
 *    <xres>x<yres>[-<bpp>][@<refresh>]   or,
 *    <name>[-<bpp>][@<refresh>]          Startup using this video mode
 */

#ifndef MODULE
static int __init pvr2fb_setup(char *options)
{
	char *this_opt;
	char cable_arg[80];
	char output_arg[80];

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ","))) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "cable:", 6)) {
			strcpy(cable_arg, this_opt + 6);
		} else if (!strncmp(this_opt, "output:", 7)) {
			strcpy(output_arg, this_opt + 7);
		} else if (!strncmp(this_opt, "nopan", 5)) {
			nopan = 1;
		} else if (!strncmp(this_opt, "nowrap", 6)) {
			nowrap = 1;
		} else {
			mode_option = this_opt;
		}
	}

	if (*cable_arg)
		cable_type = pvr2_get_param(cables, cable_arg, 0, 3);
	if (*output_arg)
		video_output = pvr2_get_param(outputs, output_arg, 0, 3);

	return 0;
}
#endif

static struct pvr2_board {
	int (*init)(void);
	void (*exit)(void);
	char name[16];
} board_driver[] __refdata = {
#ifdef CONFIG_SH_DREAMCAST
	{ pvr2fb_dc_init, pvr2fb_dc_exit, "Sega DC PVR2" },
#endif
#ifdef CONFIG_PCI
	{ pvr2fb_pci_init, pvr2fb_pci_exit, "PCI PVR2" },
#endif
	{ 0, },
};

static int __init pvr2fb_init(void)
{
	int i, ret = -ENODEV;
	int size;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("pvr2fb", &option))
		return -ENODEV;
	pvr2fb_setup(option);
#endif
	size = sizeof(struct fb_info) + sizeof(struct pvr2fb_par) + 16 * sizeof(u32);

	fb_info = framebuffer_alloc(sizeof(struct pvr2fb_par), NULL);

	if (!fb_info) {
		printk(KERN_ERR "Failed to allocate memory for fb_info\n");
		return -ENOMEM;
	}


	currentpar = fb_info->par;

	for (i = 0; i < ARRAY_SIZE(board_driver); i++) {
		struct pvr2_board *pvr_board = board_driver + i;

		if (!pvr_board->init)
			continue;

		ret = pvr_board->init();

		if (ret != 0) {
			printk(KERN_ERR "pvr2fb: Failed init of %s device\n",
				pvr_board->name);
			framebuffer_release(fb_info);
			break;
		}
	}

	return ret;
}

static void __exit pvr2fb_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(board_driver); i++) {
		struct pvr2_board *pvr_board = board_driver + i;

		if (pvr_board->exit)
			pvr_board->exit();
	}

#ifdef CONFIG_SH_STORE_QUEUES
	sq_unmap(pvr2fb_map);
#endif

	unregister_framebuffer(fb_info);
	framebuffer_release(fb_info);
}

module_init(pvr2fb_init);
module_exit(pvr2fb_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>, M. R. Brown <mrbrown@0xd6.org>");
MODULE_DESCRIPTION("Framebuffer driver for NEC PowerVR 2 based graphics boards");
MODULE_LICENSE("GPL");
