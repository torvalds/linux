/*
 * drivers/video/cirrusfb.c - driver for Cirrus Logic chipsets
 *
 * Copyright 1999-2001 Jeff Garzik <jgarzik@pobox.com>
 *
 * Contributors (thanks, all!)
 *
 *	David Eger:
 *	Overhaul for Linux 2.6
 *
 *      Jeff Rugen:
 *      Major contributions;  Motorola PowerStack (PPC and PCI) support,
 *      GD54xx, 1280x1024 mode support, change MCLK based on VCLK.
 *
 *	Geert Uytterhoeven:
 *	Excellent code review.
 *
 *	Lars Hecking:
 *	Amiga updates and testing.
 *
 * Original cirrusfb author:  Frank Neumann
 *
 * Based on retz3fb.c and cirrusfb.c:
 *      Copyright (C) 1997 Jes Sorensen
 *      Copyright (C) 1996 Frank Neumann
 *
 ***************************************************************
 *
 * Format this code with GNU indent '-kr -i8 -pcs' options.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/pgtable.h>

#ifdef CONFIG_ZORRO
#include <linux/zorro.h>
#endif
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif

#include <video/vga.h>
#include <video/cirrus.h>

/*****************************************************************
 *
 * debugging and utility macros
 *
 */

/* disable runtime assertions? */
/* #define CIRRUSFB_NDEBUG */

/* debugging assertions */
#ifndef CIRRUSFB_NDEBUG
#define assert(expr) \
	if (!(expr)) { \
		printk("Assertion failed! %s,%s,%s,line=%d\n", \
		#expr, __FILE__, __func__, __LINE__); \
	}
#else
#define assert(expr)
#endif

#define MB_ (1024 * 1024)

/*****************************************************************
 *
 * chipset information
 *
 */

/* board types */
enum cirrus_board {
	BT_NONE = 0,
	BT_SD64,	/* GD5434 */
	BT_PICCOLO,	/* GD5426 */
	BT_PICASSO,	/* GD5426 or GD5428 */
	BT_SPECTRUM,	/* GD5426 or GD5428 */
	BT_PICASSO4,	/* GD5446 */
	BT_ALPINE,	/* GD543x/4x */
	BT_GD5480,
	BT_LAGUNA,	/* GD5462/64 */
	BT_LAGUNAB,	/* GD5465 */
};

/*
 * per-board-type information, used for enumerating and abstracting
 * chip-specific information
 * NOTE: MUST be in the same order as enum cirrus_board in order to
 * use direct indexing on this array
 * NOTE: '__initdata' cannot be used as some of this info
 * is required at runtime.  Maybe separate into an init-only and
 * a run-time table?
 */
static const struct cirrusfb_board_info_rec {
	char *name;		/* ASCII name of chipset */
	long maxclock[5];		/* maximum video clock */
	/* for  1/4bpp, 8bpp 15/16bpp, 24bpp, 32bpp - numbers from xorg code */
	bool init_sr07 : 1; /* init SR07 during init_vgachip() */
	bool init_sr1f : 1; /* write SR1F during init_vgachip() */
	/* construct bit 19 of screen start address */
	bool scrn_start_bit19 : 1;

	/* initial SR07 value, then for each mode */
	unsigned char sr07;
	unsigned char sr07_1bpp;
	unsigned char sr07_1bpp_mux;
	unsigned char sr07_8bpp;
	unsigned char sr07_8bpp_mux;

	unsigned char sr1f;	/* SR1F VGA initial register value */
} cirrusfb_board_info[] = {
	[BT_SD64] = {
		.name			= "CL SD64",
		.maxclock		= {
			/* guess */
			/* the SD64/P4 have a higher max. videoclock */
			135100, 135100, 85500, 85500, 0
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= true,
		.sr07			= 0xF0,
		.sr07_1bpp		= 0xF0,
		.sr07_1bpp_mux		= 0xF6,
		.sr07_8bpp		= 0xF1,
		.sr07_8bpp_mux		= 0xF7,
		.sr1f			= 0x1E
	},
	[BT_PICCOLO] = {
		.name			= "CL Piccolo",
		.maxclock		= {
			/* guess */
			90000, 90000, 90000, 90000, 90000
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= false,
		.sr07			= 0x80,
		.sr07_1bpp		= 0x80,
		.sr07_8bpp		= 0x81,
		.sr1f			= 0x22
	},
	[BT_PICASSO] = {
		.name			= "CL Picasso",
		.maxclock		= {
			/* guess */
			90000, 90000, 90000, 90000, 90000
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= false,
		.sr07			= 0x20,
		.sr07_1bpp		= 0x20,
		.sr07_8bpp		= 0x21,
		.sr1f			= 0x22
	},
	[BT_SPECTRUM] = {
		.name			= "CL Spectrum",
		.maxclock		= {
			/* guess */
			90000, 90000, 90000, 90000, 90000
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= false,
		.sr07			= 0x80,
		.sr07_1bpp		= 0x80,
		.sr07_8bpp		= 0x81,
		.sr1f			= 0x22
	},
	[BT_PICASSO4] = {
		.name			= "CL Picasso4",
		.maxclock		= {
			135100, 135100, 85500, 85500, 0
		},
		.init_sr07		= true,
		.init_sr1f		= false,
		.scrn_start_bit19	= true,
		.sr07			= 0xA0,
		.sr07_1bpp		= 0xA0,
		.sr07_1bpp_mux		= 0xA6,
		.sr07_8bpp		= 0xA1,
		.sr07_8bpp_mux		= 0xA7,
		.sr1f			= 0
	},
	[BT_ALPINE] = {
		.name			= "CL Alpine",
		.maxclock		= {
			/* for the GD5430.  GD5446 can do more... */
			85500, 85500, 50000, 28500, 0
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= true,
		.sr07			= 0xA0,
		.sr07_1bpp		= 0xA0,
		.sr07_1bpp_mux		= 0xA6,
		.sr07_8bpp		= 0xA1,
		.sr07_8bpp_mux		= 0xA7,
		.sr1f			= 0x1C
	},
	[BT_GD5480] = {
		.name			= "CL GD5480",
		.maxclock		= {
			135100, 200000, 200000, 135100, 135100
		},
		.init_sr07		= true,
		.init_sr1f		= true,
		.scrn_start_bit19	= true,
		.sr07			= 0x10,
		.sr07_1bpp		= 0x11,
		.sr07_8bpp		= 0x11,
		.sr1f			= 0x1C
	},
	[BT_LAGUNA] = {
		.name			= "CL Laguna",
		.maxclock		= {
			/* taken from X11 code */
			170000, 170000, 170000, 170000, 135100,
		},
		.init_sr07		= false,
		.init_sr1f		= false,
		.scrn_start_bit19	= true,
	},
	[BT_LAGUNAB] = {
		.name			= "CL Laguna AGP",
		.maxclock		= {
			/* taken from X11 code */
			170000, 250000, 170000, 170000, 135100,
		},
		.init_sr07		= false,
		.init_sr1f		= false,
		.scrn_start_bit19	= true,
	}
};

#ifdef CONFIG_PCI
#define CHIP(id, btype) \
	{ PCI_VENDOR_ID_CIRRUS, id, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (btype) }

static struct pci_device_id cirrusfb_pci_table[] = {
	CHIP(PCI_DEVICE_ID_CIRRUS_5436, BT_ALPINE),
	CHIP(PCI_DEVICE_ID_CIRRUS_5434_8, BT_SD64),
	CHIP(PCI_DEVICE_ID_CIRRUS_5434_4, BT_SD64),
	CHIP(PCI_DEVICE_ID_CIRRUS_5430, BT_ALPINE), /* GD-5440 is same id */
	CHIP(PCI_DEVICE_ID_CIRRUS_7543, BT_ALPINE),
	CHIP(PCI_DEVICE_ID_CIRRUS_7548, BT_ALPINE),
	CHIP(PCI_DEVICE_ID_CIRRUS_5480, BT_GD5480), /* MacPicasso likely */
	CHIP(PCI_DEVICE_ID_CIRRUS_5446, BT_PICASSO4), /* Picasso 4 is 5446 */
	CHIP(PCI_DEVICE_ID_CIRRUS_5462, BT_LAGUNA), /* CL Laguna */
	CHIP(PCI_DEVICE_ID_CIRRUS_5464, BT_LAGUNA), /* CL Laguna 3D */
	CHIP(PCI_DEVICE_ID_CIRRUS_5465, BT_LAGUNAB), /* CL Laguna 3DA*/
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, cirrusfb_pci_table);
#undef CHIP
#endif /* CONFIG_PCI */

#ifdef CONFIG_ZORRO
struct zorrocl {
	enum cirrus_board type;	/* Board type */
	u32 regoffset;		/* Offset of registers in first Zorro device */
	u32 ramsize;		/* Size of video RAM in first Zorro device */
				/* If zero, use autoprobe on RAM device */
	u32 ramoffset;		/* Offset of video RAM in first Zorro device */
	zorro_id ramid;		/* Zorro ID of RAM device */
	zorro_id ramid2;	/* Zorro ID of optional second RAM device */
};

static const struct zorrocl zcl_sd64 = {
	.type		= BT_SD64,
	.ramid		= ZORRO_PROD_HELFRICH_SD64_RAM,
};

static const struct zorrocl zcl_piccolo = {
	.type		= BT_PICCOLO,
	.ramid		= ZORRO_PROD_HELFRICH_PICCOLO_RAM,
};

static const struct zorrocl zcl_picasso = {
	.type		= BT_PICASSO,
	.ramid		= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_RAM,
};

static const struct zorrocl zcl_spectrum = {
	.type		= BT_SPECTRUM,
	.ramid		= ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_RAM,
};

static const struct zorrocl zcl_picasso4_z3 = {
	.type		= BT_PICASSO4,
	.regoffset	= 0x00600000,
	.ramsize	= 4 * MB_,
	.ramoffset	= 0x01000000,	/* 0x02000000 for 64 MiB boards */
};

static const struct zorrocl zcl_picasso4_z2 = {
	.type		= BT_PICASSO4,
	.regoffset	= 0x10000,
	.ramid		= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z2_RAM1,
	.ramid2		= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z2_RAM2,
};


static const struct zorro_device_id cirrusfb_zorro_table[] = {
	{
		.id		= ZORRO_PROD_HELFRICH_SD64_REG,
		.driver_data	= (unsigned long)&zcl_sd64,
	}, {
		.id		= ZORRO_PROD_HELFRICH_PICCOLO_REG,
		.driver_data	= (unsigned long)&zcl_piccolo,
	}, {
		.id	= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_II_II_PLUS_REG,
		.driver_data	= (unsigned long)&zcl_picasso,
	}, {
		.id		= ZORRO_PROD_GVP_EGS_28_24_SPECTRUM_REG,
		.driver_data	= (unsigned long)&zcl_spectrum,
	}, {
		.id		= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z3,
		.driver_data	= (unsigned long)&zcl_picasso4_z3,
	}, {
		.id		= ZORRO_PROD_VILLAGE_TRONIC_PICASSO_IV_Z2_REG,
		.driver_data	= (unsigned long)&zcl_picasso4_z2,
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, cirrusfb_zorro_table);
#endif /* CONFIG_ZORRO */

#ifdef CIRRUSFB_DEBUG
enum cirrusfb_dbg_reg_class {
	CRT,
	SEQ
};
#endif		/* CIRRUSFB_DEBUG */

/* info about board */
struct cirrusfb_info {
	u8 __iomem *regbase;
	u8 __iomem *laguna_mmio;
	enum cirrus_board btype;
	unsigned char SFR;	/* Shadow of special function register */

	int multiplexing;
	int doubleVCLK;
	int blank_mode;
	u32 pseudo_palette[16];

	void (*unmap)(struct fb_info *info);
};

static bool noaccel;
static char *mode_option = "640x480@60";

/****************************************************************************/
/**** BEGIN PROTOTYPES ******************************************************/

/*--- Interface used by the world ------------------------------------------*/
static int cirrusfb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info);

/*--- Internal routines ----------------------------------------------------*/
static void init_vgachip(struct fb_info *info);
static void switch_monitor(struct cirrusfb_info *cinfo, int on);
static void WGen(const struct cirrusfb_info *cinfo,
		 int regnum, unsigned char val);
static unsigned char RGen(const struct cirrusfb_info *cinfo, int regnum);
static void AttrOn(const struct cirrusfb_info *cinfo);
static void WHDR(const struct cirrusfb_info *cinfo, unsigned char val);
static void WSFR(struct cirrusfb_info *cinfo, unsigned char val);
static void WSFR2(struct cirrusfb_info *cinfo, unsigned char val);
static void WClut(struct cirrusfb_info *cinfo, unsigned char regnum,
		  unsigned char red, unsigned char green, unsigned char blue);
#if 0
static void RClut(struct cirrusfb_info *cinfo, unsigned char regnum,
		  unsigned char *red, unsigned char *green,
		  unsigned char *blue);
#endif
static void cirrusfb_WaitBLT(u8 __iomem *regbase);
static void cirrusfb_BitBLT(u8 __iomem *regbase, int bits_per_pixel,
			    u_short curx, u_short cury,
			    u_short destx, u_short desty,
			    u_short width, u_short height,
			    u_short line_length);
static void cirrusfb_RectFill(u8 __iomem *regbase, int bits_per_pixel,
			      u_short x, u_short y,
			      u_short width, u_short height,
			      u32 fg_color, u32 bg_color,
			      u_short line_length, u_char blitmode);

static void bestclock(long freq, int *nom, int *den, int *div);

#ifdef CIRRUSFB_DEBUG
static void cirrusfb_dbg_reg_dump(struct fb_info *info, caddr_t regbase);
static void cirrusfb_dbg_print_regs(struct fb_info *info,
				    caddr_t regbase,
				    enum cirrusfb_dbg_reg_class reg_class, ...);
#endif /* CIRRUSFB_DEBUG */

/*** END   PROTOTYPES ********************************************************/
/*****************************************************************************/
/*** BEGIN Interface Used by the World ***************************************/

static inline int is_laguna(const struct cirrusfb_info *cinfo)
{
	return cinfo->btype == BT_LAGUNA || cinfo->btype == BT_LAGUNAB;
}

static int opencount;

/*--- Open /dev/fbx ---------------------------------------------------------*/
static int cirrusfb_open(struct fb_info *info, int user)
{
	if (opencount++ == 0)
		switch_monitor(info->par, 1);
	return 0;
}

/*--- Close /dev/fbx --------------------------------------------------------*/
static int cirrusfb_release(struct fb_info *info, int user)
{
	if (--opencount == 0)
		switch_monitor(info->par, 0);
	return 0;
}

/**** END   Interface used by the World *************************************/
/****************************************************************************/
/**** BEGIN Hardware specific Routines **************************************/

/* Check if the MCLK is not a better clock source */
static int cirrusfb_check_mclk(struct fb_info *info, long freq)
{
	struct cirrusfb_info *cinfo = info->par;
	long mclk = vga_rseq(cinfo->regbase, CL_SEQR1F) & 0x3f;

	/* Read MCLK value */
	mclk = (14318 * mclk) >> 3;
	dev_dbg(info->device, "Read MCLK of %ld kHz\n", mclk);

	/* Determine if we should use MCLK instead of VCLK, and if so, what we
	 * should divide it by to get VCLK
	 */

	if (abs(freq - mclk) < 250) {
		dev_dbg(info->device, "Using VCLK = MCLK\n");
		return 1;
	} else if (abs(freq - (mclk / 2)) < 250) {
		dev_dbg(info->device, "Using VCLK = MCLK/2\n");
		return 2;
	}

	return 0;
}

static int cirrusfb_check_pixclock(const struct fb_var_screeninfo *var,
				   struct fb_info *info)
{
	long freq;
	long maxclock;
	struct cirrusfb_info *cinfo = info->par;
	unsigned maxclockidx = var->bits_per_pixel >> 3;

	/* convert from ps to kHz */
	freq = PICOS2KHZ(var->pixclock);

	dev_dbg(info->device, "desired pixclock: %ld kHz\n", freq);

	maxclock = cirrusfb_board_info[cinfo->btype].maxclock[maxclockidx];
	cinfo->multiplexing = 0;

	/* If the frequency is greater than we can support, we might be able
	 * to use multiplexing for the video mode */
	if (freq > maxclock) {
		dev_err(info->device,
			"Frequency greater than maxclock (%ld kHz)\n",
			maxclock);
		return -EINVAL;
	}
	/*
	 * Additional constraint: 8bpp uses DAC clock doubling to allow maximum
	 * pixel clock
	 */
	if (var->bits_per_pixel == 8) {
		switch (cinfo->btype) {
		case BT_ALPINE:
		case BT_SD64:
		case BT_PICASSO4:
			if (freq > 85500)
				cinfo->multiplexing = 1;
			break;
		case BT_GD5480:
			if (freq > 135100)
				cinfo->multiplexing = 1;
			break;

		default:
			break;
		}
	}

	/* If we have a 1MB 5434, we need to put ourselves in a mode where
	 * the VCLK is double the pixel clock. */
	cinfo->doubleVCLK = 0;
	if (cinfo->btype == BT_SD64 && info->fix.smem_len <= MB_ &&
	    var->bits_per_pixel == 16) {
		cinfo->doubleVCLK = 1;
	}

	return 0;
}

static int cirrusfb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	int yres;
	/* memory size in pixels */
	unsigned pixels = info->screen_size * 8 / var->bits_per_pixel;
	struct cirrusfb_info *cinfo = info->par;

	switch (var->bits_per_pixel) {
	case 1:
		var->red.offset = 0;
		var->red.length = 1;
		var->green = var->red;
		var->blue = var->red;
		break;

	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green = var->red;
		var->blue = var->red;
		break;

	case 16:
		var->red.offset = 11;
		var->green.offset = 5;
		var->blue.offset = 0;
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;

	case 24:
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;

	default:
		dev_dbg(info->device,
			"Unsupported bpp size: %d\n", var->bits_per_pixel);
		return -EINVAL;
	}

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	/* use highest possible virtual resolution */
	if (var->yres_virtual == -1) {
		var->yres_virtual = pixels / var->xres_virtual;

		dev_info(info->device,
			 "virtual resolution set to maximum of %dx%d\n",
			 var->xres_virtual, var->yres_virtual);
	}
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->xres_virtual * var->yres_virtual > pixels) {
		dev_err(info->device, "mode %dx%dx%d rejected... "
		      "virtual resolution too high to fit into video memory!\n",
			var->xres_virtual, var->yres_virtual,
			var->bits_per_pixel);
		return -EINVAL;
	}

	/* truncate xoffset and yoffset to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;
	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;

	var->red.msb_right =
	    var->green.msb_right =
	    var->blue.msb_right =
	    var->transp.offset =
	    var->transp.length =
	    var->transp.msb_right = 0;

	yres = var->yres;
	if (var->vmode & FB_VMODE_DOUBLE)
		yres *= 2;
	else if (var->vmode & FB_VMODE_INTERLACED)
		yres = (yres + 1) / 2;

	if (yres >= 1280) {
		dev_err(info->device, "ERROR: VerticalTotal >= 1280; "
			"special treatment required! (TODO)\n");
		return -EINVAL;
	}

	if (cirrusfb_check_pixclock(var, info))
		return -EINVAL;

	if (!is_laguna(cinfo))
		var->accel_flags = FB_ACCELF_TEXT;

	return 0;
}

static void cirrusfb_set_mclk_as_source(const struct fb_info *info, int div)
{
	struct cirrusfb_info *cinfo = info->par;
	unsigned char old1f, old1e;

	assert(cinfo != NULL);
	old1f = vga_rseq(cinfo->regbase, CL_SEQR1F) & ~0x40;

	if (div) {
		dev_dbg(info->device, "Set %s as pixclock source.\n",
			(div == 2) ? "MCLK/2" : "MCLK");
		old1f |= 0x40;
		old1e = vga_rseq(cinfo->regbase, CL_SEQR1E) & ~0x1;
		if (div == 2)
			old1e |= 1;

		vga_wseq(cinfo->regbase, CL_SEQR1E, old1e);
	}
	vga_wseq(cinfo->regbase, CL_SEQR1F, old1f);
}

/*************************************************************************
	cirrusfb_set_par_foo()

	actually writes the values for a new video mode into the hardware,
**************************************************************************/
static int cirrusfb_set_par_foo(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;
	struct fb_var_screeninfo *var = &info->var;
	u8 __iomem *regbase = cinfo->regbase;
	unsigned char tmp;
	int pitch;
	const struct cirrusfb_board_info_rec *bi;
	int hdispend, hsyncstart, hsyncend, htotal;
	int yres, vdispend, vsyncstart, vsyncend, vtotal;
	long freq;
	int nom, den, div;
	unsigned int control = 0, format = 0, threshold = 0;

	dev_dbg(info->device, "Requested mode: %dx%dx%d\n",
	       var->xres, var->yres, var->bits_per_pixel);

	switch (var->bits_per_pixel) {
	case 1:
		info->fix.line_length = var->xres_virtual / 8;
		info->fix.visual = FB_VISUAL_MONO10;
		break;

	case 8:
		info->fix.line_length = var->xres_virtual;
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;

	case 16:
	case 24:
		info->fix.line_length = var->xres_virtual *
					var->bits_per_pixel >> 3;
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	}
	info->fix.type = FB_TYPE_PACKED_PIXELS;

	init_vgachip(info);

	bi = &cirrusfb_board_info[cinfo->btype];

	hsyncstart = var->xres + var->right_margin;
	hsyncend = hsyncstart + var->hsync_len;
	htotal = (hsyncend + var->left_margin) / 8;
	hdispend = var->xres / 8;
	hsyncstart = hsyncstart / 8;
	hsyncend = hsyncend / 8;

	vdispend = var->yres;
	vsyncstart = vdispend + var->lower_margin;
	vsyncend = vsyncstart + var->vsync_len;
	vtotal = vsyncend + var->upper_margin;

	if (var->vmode & FB_VMODE_DOUBLE) {
		vdispend *= 2;
		vsyncstart *= 2;
		vsyncend *= 2;
		vtotal *= 2;
	} else if (var->vmode & FB_VMODE_INTERLACED) {
		vdispend = (vdispend + 1) / 2;
		vsyncstart = (vsyncstart + 1) / 2;
		vsyncend = (vsyncend + 1) / 2;
		vtotal = (vtotal + 1) / 2;
	}
	yres = vdispend;
	if (yres >= 1024) {
		vtotal /= 2;
		vsyncstart /= 2;
		vsyncend /= 2;
		vdispend /= 2;
	}

	vdispend -= 1;
	vsyncstart -= 1;
	vsyncend -= 1;
	vtotal -= 2;

	if (cinfo->multiplexing) {
		htotal /= 2;
		hsyncstart /= 2;
		hsyncend /= 2;
		hdispend /= 2;
	}

	htotal -= 5;
	hdispend -= 1;
	hsyncstart += 1;
	hsyncend += 1;

	/* unlock register VGA_CRTC_H_TOTAL..CRT7 */
	vga_wcrt(regbase, VGA_CRTC_V_SYNC_END, 0x20);	/* previously: 0x00) */

	/* if debugging is enabled, all parameters get output before writing */
	dev_dbg(info->device, "CRT0: %d\n", htotal);
	vga_wcrt(regbase, VGA_CRTC_H_TOTAL, htotal);

	dev_dbg(info->device, "CRT1: %d\n", hdispend);
	vga_wcrt(regbase, VGA_CRTC_H_DISP, hdispend);

	dev_dbg(info->device, "CRT2: %d\n", var->xres / 8);
	vga_wcrt(regbase, VGA_CRTC_H_BLANK_START, var->xres / 8);

	/*  + 128: Compatible read */
	dev_dbg(info->device, "CRT3: 128+%d\n", (htotal + 5) % 32);
	vga_wcrt(regbase, VGA_CRTC_H_BLANK_END,
		 128 + ((htotal + 5) % 32));

	dev_dbg(info->device, "CRT4: %d\n", hsyncstart);
	vga_wcrt(regbase, VGA_CRTC_H_SYNC_START, hsyncstart);

	tmp = hsyncend % 32;
	if ((htotal + 5) & 32)
		tmp += 128;
	dev_dbg(info->device, "CRT5: %d\n", tmp);
	vga_wcrt(regbase, VGA_CRTC_H_SYNC_END, tmp);

	dev_dbg(info->device, "CRT6: %d\n", vtotal & 0xff);
	vga_wcrt(regbase, VGA_CRTC_V_TOTAL, vtotal & 0xff);

	tmp = 16;		/* LineCompare bit #9 */
	if (vtotal & 256)
		tmp |= 1;
	if (vdispend & 256)
		tmp |= 2;
	if (vsyncstart & 256)
		tmp |= 4;
	if ((vdispend + 1) & 256)
		tmp |= 8;
	if (vtotal & 512)
		tmp |= 32;
	if (vdispend & 512)
		tmp |= 64;
	if (vsyncstart & 512)
		tmp |= 128;
	dev_dbg(info->device, "CRT7: %d\n", tmp);
	vga_wcrt(regbase, VGA_CRTC_OVERFLOW, tmp);

	tmp = 0x40;		/* LineCompare bit #8 */
	if ((vdispend + 1) & 512)
		tmp |= 0x20;
	if (var->vmode & FB_VMODE_DOUBLE)
		tmp |= 0x80;
	dev_dbg(info->device, "CRT9: %d\n", tmp);
	vga_wcrt(regbase, VGA_CRTC_MAX_SCAN, tmp);

	dev_dbg(info->device, "CRT10: %d\n", vsyncstart & 0xff);
	vga_wcrt(regbase, VGA_CRTC_V_SYNC_START, vsyncstart & 0xff);

	dev_dbg(info->device, "CRT11: 64+32+%d\n", vsyncend % 16);
	vga_wcrt(regbase, VGA_CRTC_V_SYNC_END, vsyncend % 16 + 64 + 32);

	dev_dbg(info->device, "CRT12: %d\n", vdispend & 0xff);
	vga_wcrt(regbase, VGA_CRTC_V_DISP_END, vdispend & 0xff);

	dev_dbg(info->device, "CRT15: %d\n", (vdispend + 1) & 0xff);
	vga_wcrt(regbase, VGA_CRTC_V_BLANK_START, (vdispend + 1) & 0xff);

	dev_dbg(info->device, "CRT16: %d\n", vtotal & 0xff);
	vga_wcrt(regbase, VGA_CRTC_V_BLANK_END, vtotal & 0xff);

	dev_dbg(info->device, "CRT18: 0xff\n");
	vga_wcrt(regbase, VGA_CRTC_LINE_COMPARE, 0xff);

	tmp = 0;
	if (var->vmode & FB_VMODE_INTERLACED)
		tmp |= 1;
	if ((htotal + 5) & 64)
		tmp |= 16;
	if ((htotal + 5) & 128)
		tmp |= 32;
	if (vtotal & 256)
		tmp |= 64;
	if (vtotal & 512)
		tmp |= 128;

	dev_dbg(info->device, "CRT1a: %d\n", tmp);
	vga_wcrt(regbase, CL_CRT1A, tmp);

	freq = PICOS2KHZ(var->pixclock);
	if (var->bits_per_pixel == 24)
		if (cinfo->btype == BT_ALPINE || cinfo->btype == BT_SD64)
			freq *= 3;
	if (cinfo->multiplexing)
		freq /= 2;
	if (cinfo->doubleVCLK)
		freq *= 2;

	bestclock(freq, &nom, &den, &div);

	dev_dbg(info->device, "VCLK freq: %ld kHz  nom: %d  den: %d  div: %d\n",
		freq, nom, den, div);

	/* set VCLK0 */
	/* hardware RefClock: 14.31818 MHz */
	/* formula: VClk = (OSC * N) / (D * (1+P)) */
	/* Example: VClk = (14.31818 * 91) / (23 * (1+1)) = 28.325 MHz */

	if (cinfo->btype == BT_ALPINE || cinfo->btype == BT_PICASSO4 ||
	    cinfo->btype == BT_SD64) {
		/* if freq is close to mclk or mclk/2 select mclk
		 * as clock source
		 */
		int divMCLK = cirrusfb_check_mclk(info, freq);
		if (divMCLK)
			nom = 0;
		cirrusfb_set_mclk_as_source(info, divMCLK);
	}
	if (is_laguna(cinfo)) {
		long pcifc = fb_readl(cinfo->laguna_mmio + 0x3fc);
		unsigned char tile = fb_readb(cinfo->laguna_mmio + 0x407);
		unsigned short tile_control;

		if (cinfo->btype == BT_LAGUNAB) {
			tile_control = fb_readw(cinfo->laguna_mmio + 0x2c4);
			tile_control &= ~0x80;
			fb_writew(tile_control, cinfo->laguna_mmio + 0x2c4);
		}

		fb_writel(pcifc | 0x10000000l, cinfo->laguna_mmio + 0x3fc);
		fb_writeb(tile & 0x3f, cinfo->laguna_mmio + 0x407);
		control = fb_readw(cinfo->laguna_mmio + 0x402);
		threshold = fb_readw(cinfo->laguna_mmio + 0xea);
		control &= ~0x6800;
		format = 0;
		threshold &= 0xffc0 & 0x3fbf;
	}
	if (nom) {
		tmp = den << 1;
		if (div != 0)
			tmp |= 1;
		/* 6 bit denom; ONLY 5434!!! (bugged me 10 days) */
		if ((cinfo->btype == BT_SD64) ||
		    (cinfo->btype == BT_ALPINE) ||
		    (cinfo->btype == BT_GD5480))
			tmp |= 0x80;

		/* Laguna chipset has reversed clock registers */
		if (is_laguna(cinfo)) {
			vga_wseq(regbase, CL_SEQRE, tmp);
			vga_wseq(regbase, CL_SEQR1E, nom);
		} else {
			vga_wseq(regbase, CL_SEQRE, nom);
			vga_wseq(regbase, CL_SEQR1E, tmp);
		}
	}

	if (yres >= 1024)
		/* 1280x1024 */
		vga_wcrt(regbase, VGA_CRTC_MODE, 0xc7);
	else
		/* mode control: VGA_CRTC_START_HI enable, ROTATE(?), 16bit
		 * address wrap, no compat. */
		vga_wcrt(regbase, VGA_CRTC_MODE, 0xc3);

	/* don't know if it would hurt to also program this if no interlaced */
	/* mode is used, but I feel better this way.. :-) */
	if (var->vmode & FB_VMODE_INTERLACED)
		vga_wcrt(regbase, VGA_CRTC_REGS, htotal / 2);
	else
		vga_wcrt(regbase, VGA_CRTC_REGS, 0x00);	/* interlace control */

	/* adjust horizontal/vertical sync type (low/high), use VCLK3 */
	/* enable display memory & CRTC I/O address for color mode */
	tmp = 0x03 | 0xc;
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		tmp |= 0x40;
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		tmp |= 0x80;
	WGen(cinfo, VGA_MIS_W, tmp);

	/* text cursor on and start line */
	vga_wcrt(regbase, VGA_CRTC_CURSOR_START, 0);
	/* text cursor end line */
	vga_wcrt(regbase, VGA_CRTC_CURSOR_END, 31);

	/******************************************************
	 *
	 * 1 bpp
	 *
	 */

	/* programming for different color depths */
	if (var->bits_per_pixel == 1) {
		dev_dbg(info->device, "preparing for 1 bit deep display\n");
		vga_wgfx(regbase, VGA_GFX_MODE, 0);	/* mode register */

		/* SR07 */
		switch (cinfo->btype) {
		case BT_SD64:
		case BT_PICCOLO:
		case BT_PICASSO:
		case BT_SPECTRUM:
		case BT_PICASSO4:
		case BT_ALPINE:
		case BT_GD5480:
			vga_wseq(regbase, CL_SEQR7,
				 cinfo->multiplexing ?
					bi->sr07_1bpp_mux : bi->sr07_1bpp);
			break;

		case BT_LAGUNA:
		case BT_LAGUNAB:
			vga_wseq(regbase, CL_SEQR7,
				vga_rseq(regbase, CL_SEQR7) & ~0x01);
			break;

		default:
			dev_warn(info->device, "unknown Board\n");
			break;
		}

		/* Extended Sequencer Mode */
		switch (cinfo->btype) {

		case BT_PICCOLO:
		case BT_SPECTRUM:
			/* evtl d0 bei 1 bit? avoid FIFO underruns..? */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_PICASSO:
			/* ## vorher d0 avoid FIFO underruns..? */
			vga_wseq(regbase, CL_SEQRF, 0xd0);
			break;

		case BT_SD64:
		case BT_PICASSO4:
		case BT_ALPINE:
		case BT_GD5480:
		case BT_LAGUNA:
		case BT_LAGUNAB:
			/* do nothing */
			break;

		default:
			dev_warn(info->device, "unknown Board\n");
			break;
		}

		/* pixel mask: pass-through for first plane */
		WGen(cinfo, VGA_PEL_MSK, 0x01);
		if (cinfo->multiplexing)
			/* hidden dac reg: 1280x1024 */
			WHDR(cinfo, 0x4a);
		else
			/* hidden dac: nothing */
			WHDR(cinfo, 0);
		/* memory mode: odd/even, ext. memory */
		vga_wseq(regbase, VGA_SEQ_MEMORY_MODE, 0x06);
		/* plane mask: only write to first plane */
		vga_wseq(regbase, VGA_SEQ_PLANE_WRITE, 0x01);
	}

	/******************************************************
	 *
	 * 8 bpp
	 *
	 */

	else if (var->bits_per_pixel == 8) {
		dev_dbg(info->device, "preparing for 8 bit deep display\n");
		switch (cinfo->btype) {
		case BT_SD64:
		case BT_PICCOLO:
		case BT_PICASSO:
		case BT_SPECTRUM:
		case BT_PICASSO4:
		case BT_ALPINE:
		case BT_GD5480:
			vga_wseq(regbase, CL_SEQR7,
				  cinfo->multiplexing ?
					bi->sr07_8bpp_mux : bi->sr07_8bpp);
			break;

		case BT_LAGUNA:
		case BT_LAGUNAB:
			vga_wseq(regbase, CL_SEQR7,
				vga_rseq(regbase, CL_SEQR7) | 0x01);
			threshold |= 0x10;
			break;

		default:
			dev_warn(info->device, "unknown Board\n");
			break;
		}

		switch (cinfo->btype) {
		case BT_PICCOLO:
		case BT_PICASSO:
		case BT_SPECTRUM:
			/* Fast Page-Mode writes */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_PICASSO4:
#ifdef CONFIG_ZORRO
			/* ### INCOMPLETE!! */
			vga_wseq(regbase, CL_SEQRF, 0xb8);
#endif
		case BT_ALPINE:
		case BT_SD64:
		case BT_GD5480:
		case BT_LAGUNA:
		case BT_LAGUNAB:
			/* do nothing */
			break;

		default:
			dev_warn(info->device, "unknown board\n");
			break;
		}

		/* mode register: 256 color mode */
		vga_wgfx(regbase, VGA_GFX_MODE, 64);
		if (cinfo->multiplexing)
			/* hidden dac reg: 1280x1024 */
			WHDR(cinfo, 0x4a);
		else
			/* hidden dac: nothing */
			WHDR(cinfo, 0);
	}

	/******************************************************
	 *
	 * 16 bpp
	 *
	 */

	else if (var->bits_per_pixel == 16) {
		dev_dbg(info->device, "preparing for 16 bit deep display\n");
		switch (cinfo->btype) {
		case BT_PICCOLO:
		case BT_SPECTRUM:
			vga_wseq(regbase, CL_SEQR7, 0x87);
			/* Fast Page-Mode writes */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_PICASSO:
			vga_wseq(regbase, CL_SEQR7, 0x27);
			/* Fast Page-Mode writes */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_SD64:
		case BT_PICASSO4:
		case BT_ALPINE:
			/* Extended Sequencer Mode: 256c col. mode */
			vga_wseq(regbase, CL_SEQR7,
					cinfo->doubleVCLK ? 0xa3 : 0xa7);
			break;

		case BT_GD5480:
			vga_wseq(regbase, CL_SEQR7, 0x17);
			/* We already set SRF and SR1F */
			break;

		case BT_LAGUNA:
		case BT_LAGUNAB:
			vga_wseq(regbase, CL_SEQR7,
				vga_rseq(regbase, CL_SEQR7) & ~0x01);
			control |= 0x2000;
			format |= 0x1400;
			threshold |= 0x10;
			break;

		default:
			dev_warn(info->device, "unknown Board\n");
			break;
		}

		/* mode register: 256 color mode */
		vga_wgfx(regbase, VGA_GFX_MODE, 64);
#ifdef CONFIG_PCI
		WHDR(cinfo, cinfo->doubleVCLK ? 0xe1 : 0xc1);
#elif defined(CONFIG_ZORRO)
		/* FIXME: CONFIG_PCI and CONFIG_ZORRO may be defined both */
		WHDR(cinfo, 0xa0);	/* hidden dac reg: nothing special */
#endif
	}

	/******************************************************
	 *
	 * 24 bpp
	 *
	 */

	else if (var->bits_per_pixel == 24) {
		dev_dbg(info->device, "preparing for 24 bit deep display\n");
		switch (cinfo->btype) {
		case BT_PICCOLO:
		case BT_SPECTRUM:
			vga_wseq(regbase, CL_SEQR7, 0x85);
			/* Fast Page-Mode writes */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_PICASSO:
			vga_wseq(regbase, CL_SEQR7, 0x25);
			/* Fast Page-Mode writes */
			vga_wseq(regbase, CL_SEQRF, 0xb0);
			break;

		case BT_SD64:
		case BT_PICASSO4:
		case BT_ALPINE:
			/* Extended Sequencer Mode: 256c col. mode */
			vga_wseq(regbase, CL_SEQR7, 0xa5);
			break;

		case BT_GD5480:
			vga_wseq(regbase, CL_SEQR7, 0x15);
			/* We already set SRF and SR1F */
			break;

		case BT_LAGUNA:
		case BT_LAGUNAB:
			vga_wseq(regbase, CL_SEQR7,
				vga_rseq(regbase, CL_SEQR7) & ~0x01);
			control |= 0x4000;
			format |= 0x2400;
			threshold |= 0x20;
			break;

		default:
			dev_warn(info->device, "unknown Board\n");
			break;
		}

		/* mode register: 256 color mode */
		vga_wgfx(regbase, VGA_GFX_MODE, 64);
		/* hidden dac reg: 8-8-8 mode (24 or 32) */
		WHDR(cinfo, 0xc5);
	}

	/******************************************************
	 *
	 * unknown/unsupported bpp
	 *
	 */

	else
		dev_err(info->device,
			"What's this? requested color depth == %d.\n",
			var->bits_per_pixel);

	pitch = info->fix.line_length >> 3;
	vga_wcrt(regbase, VGA_CRTC_OFFSET, pitch & 0xff);
	tmp = 0x22;
	if (pitch & 0x100)
		tmp |= 0x10;	/* offset overflow bit */

	/* screen start addr #16-18, fastpagemode cycles */
	vga_wcrt(regbase, CL_CRT1B, tmp);

	/* screen start address bit 19 */
	if (cirrusfb_board_info[cinfo->btype].scrn_start_bit19)
		vga_wcrt(regbase, CL_CRT1D, (pitch >> 9) & 1);

	if (is_laguna(cinfo)) {
		tmp = 0;
		if ((htotal + 5) & 256)
			tmp |= 128;
		if (hdispend & 256)
			tmp |= 64;
		if (hsyncstart & 256)
			tmp |= 48;
		if (vtotal & 1024)
			tmp |= 8;
		if (vdispend & 1024)
			tmp |= 4;
		if (vsyncstart & 1024)
			tmp |= 3;

		vga_wcrt(regbase, CL_CRT1E, tmp);
		dev_dbg(info->device, "CRT1e: %d\n", tmp);
	}

	/* pixel panning */
	vga_wattr(regbase, CL_AR33, 0);

	/* [ EGS: SetOffset(); ] */
	/* From SetOffset(): Turn on VideoEnable bit in Attribute controller */
	AttrOn(cinfo);

	if (is_laguna(cinfo)) {
		/* no tiles */
		fb_writew(control | 0x1000, cinfo->laguna_mmio + 0x402);
		fb_writew(format, cinfo->laguna_mmio + 0xc0);
		fb_writew(threshold, cinfo->laguna_mmio + 0xea);
	}
	/* finally, turn on everything - turn off "FullBandwidth" bit */
	/* also, set "DotClock%2" bit where requested */
	tmp = 0x01;

/*** FB_VMODE_CLOCK_HALVE in linux/fb.h not defined anymore ?
    if (var->vmode & FB_VMODE_CLOCK_HALVE)
	tmp |= 0x08;
*/

	vga_wseq(regbase, VGA_SEQ_CLOCK_MODE, tmp);
	dev_dbg(info->device, "CL_SEQR1: %d\n", tmp);

#ifdef CIRRUSFB_DEBUG
	cirrusfb_dbg_reg_dump(info, NULL);
#endif

	return 0;
}

/* for some reason incomprehensible to me, cirrusfb requires that you write
 * the registers twice for the settings to take..grr. -dte */
static int cirrusfb_set_par(struct fb_info *info)
{
	cirrusfb_set_par_foo(info);
	return cirrusfb_set_par_foo(info);
}

static int cirrusfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			      unsigned blue, unsigned transp,
			      struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;

	if (regno > 255)
		return -EINVAL;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
		u32 v;
		red >>= (16 - info->var.red.length);
		green >>= (16 - info->var.green.length);
		blue >>= (16 - info->var.blue.length);

		if (regno >= 16)
			return 1;
		v = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset);

		cinfo->pseudo_palette[regno] = v;
		return 0;
	}

	if (info->var.bits_per_pixel == 8)
		WClut(cinfo, regno, red >> 10, green >> 10, blue >> 10);

	return 0;

}

/*************************************************************************
	cirrusfb_pan_display()

	performs display panning - provided hardware permits this
**************************************************************************/
static int cirrusfb_pan_display(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	int xoffset;
	unsigned long base;
	unsigned char tmp, xpix;
	struct cirrusfb_info *cinfo = info->par;

	/* no range checks for xoffset and yoffset,   */
	/* as fb_pan_display has already done this */
	if (var->vmode & FB_VMODE_YWRAP)
		return -EINVAL;

	xoffset = var->xoffset * info->var.bits_per_pixel / 8;

	base = var->yoffset * info->fix.line_length + xoffset;

	if (info->var.bits_per_pixel == 1) {
		/* base is already correct */
		xpix = (unsigned char) (var->xoffset % 8);
	} else {
		base /= 4;
		xpix = (unsigned char) ((xoffset % 4) * 2);
	}

	if (!is_laguna(cinfo))
		cirrusfb_WaitBLT(cinfo->regbase);

	/* lower 8 + 8 bits of screen start address */
	vga_wcrt(cinfo->regbase, VGA_CRTC_START_LO, base & 0xff);
	vga_wcrt(cinfo->regbase, VGA_CRTC_START_HI, (base >> 8) & 0xff);

	/* 0xf2 is %11110010, exclude tmp bits */
	tmp = vga_rcrt(cinfo->regbase, CL_CRT1B) & 0xf2;
	/* construct bits 16, 17 and 18 of screen start address */
	if (base & 0x10000)
		tmp |= 0x01;
	if (base & 0x20000)
		tmp |= 0x04;
	if (base & 0x40000)
		tmp |= 0x08;

	vga_wcrt(cinfo->regbase, CL_CRT1B, tmp);

	/* construct bit 19 of screen start address */
	if (cirrusfb_board_info[cinfo->btype].scrn_start_bit19) {
		tmp = vga_rcrt(cinfo->regbase, CL_CRT1D);
		if (is_laguna(cinfo))
			tmp = (tmp & ~0x18) | ((base >> 16) & 0x18);
		else
			tmp = (tmp & ~0x80) | ((base >> 12) & 0x80);
		vga_wcrt(cinfo->regbase, CL_CRT1D, tmp);
	}

	/* write pixel panning value to AR33; this does not quite work in 8bpp
	 *
	 * ### Piccolo..? Will this work?
	 */
	if (info->var.bits_per_pixel == 1)
		vga_wattr(cinfo->regbase, CL_AR33, xpix);

	return 0;
}

static int cirrusfb_blank(int blank_mode, struct fb_info *info)
{
	/*
	 * Blank the screen if blank_mode != 0, else unblank. If blank == NULL
	 * then the caller blanks by setting the CLUT (Color Look Up Table)
	 * to all black. Return 0 if blanking succeeded, != 0 if un-/blanking
	 * failed due to e.g. a video mode which doesn't support it.
	 * Implements VESA suspend and powerdown modes on hardware that
	 * supports disabling hsync/vsync:
	 *   blank_mode == 2: suspend vsync
	 *   blank_mode == 3: suspend hsync
	 *   blank_mode == 4: powerdown
	 */
	unsigned char val;
	struct cirrusfb_info *cinfo = info->par;
	int current_mode = cinfo->blank_mode;

	dev_dbg(info->device, "ENTER, blank mode = %d\n", blank_mode);

	if (info->state != FBINFO_STATE_RUNNING ||
	    current_mode == blank_mode) {
		dev_dbg(info->device, "EXIT, returning 0\n");
		return 0;
	}

	/* Undo current */
	if (current_mode == FB_BLANK_NORMAL ||
	    current_mode == FB_BLANK_UNBLANK)
		/* clear "FullBandwidth" bit */
		val = 0;
	else
		/* set "FullBandwidth" bit */
		val = 0x20;

	val |= vga_rseq(cinfo->regbase, VGA_SEQ_CLOCK_MODE) & 0xdf;
	vga_wseq(cinfo->regbase, VGA_SEQ_CLOCK_MODE, val);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		val = 0x00;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		val = 0x04;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		val = 0x02;
		break;
	case FB_BLANK_POWERDOWN:
		val = 0x06;
		break;
	default:
		dev_dbg(info->device, "EXIT, returning 1\n");
		return 1;
	}

	vga_wgfx(cinfo->regbase, CL_GRE, val);

	cinfo->blank_mode = blank_mode;
	dev_dbg(info->device, "EXIT, returning 0\n");

	/* Let fbcon do a soft blank for us */
	return (blank_mode == FB_BLANK_NORMAL) ? 1 : 0;
}

/**** END   Hardware specific Routines **************************************/
/****************************************************************************/
/**** BEGIN Internal Routines ***********************************************/

static void init_vgachip(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;
	const struct cirrusfb_board_info_rec *bi;

	assert(cinfo != NULL);

	bi = &cirrusfb_board_info[cinfo->btype];

	/* reset board globally */
	switch (cinfo->btype) {
	case BT_PICCOLO:
		WSFR(cinfo, 0x01);
		udelay(500);
		WSFR(cinfo, 0x51);
		udelay(500);
		break;
	case BT_PICASSO:
		WSFR2(cinfo, 0xff);
		udelay(500);
		break;
	case BT_SD64:
	case BT_SPECTRUM:
		WSFR(cinfo, 0x1f);
		udelay(500);
		WSFR(cinfo, 0x4f);
		udelay(500);
		break;
	case BT_PICASSO4:
		/* disable flickerfixer */
		vga_wcrt(cinfo->regbase, CL_CRT51, 0x00);
		mdelay(100);
		/* mode */
		vga_wgfx(cinfo->regbase, CL_GR31, 0x00);
		/* fall through */
	case BT_GD5480:
		/* from Klaus' NetBSD driver: */
		vga_wgfx(cinfo->regbase, CL_GR2F, 0x00);
		/* fall through */
	case BT_ALPINE:
		/* put blitter into 542x compat */
		vga_wgfx(cinfo->regbase, CL_GR33, 0x00);
		break;

	case BT_LAGUNA:
	case BT_LAGUNAB:
		/* Nothing to do to reset the board. */
		break;

	default:
		dev_err(info->device, "Warning: Unknown board type\n");
		break;
	}

	/* make sure RAM size set by this point */
	assert(info->screen_size > 0);

	/* the P4 is not fully initialized here; I rely on it having been */
	/* inited under AmigaOS already, which seems to work just fine    */
	/* (Klaus advised to do it this way)			      */

	if (cinfo->btype != BT_PICASSO4) {
		WGen(cinfo, CL_VSSM, 0x10);	/* EGS: 0x16 */
		WGen(cinfo, CL_POS102, 0x01);
		WGen(cinfo, CL_VSSM, 0x08);	/* EGS: 0x0e */

		if (cinfo->btype != BT_SD64)
			WGen(cinfo, CL_VSSM2, 0x01);

		/* reset sequencer logic */
		vga_wseq(cinfo->regbase, VGA_SEQ_RESET, 0x03);

		/* FullBandwidth (video off) and 8/9 dot clock */
		vga_wseq(cinfo->regbase, VGA_SEQ_CLOCK_MODE, 0x21);

		/* "magic cookie" - doesn't make any sense to me.. */
/*      vga_wgfx(cinfo->regbase, CL_GRA, 0xce);   */
		/* unlock all extension registers */
		vga_wseq(cinfo->regbase, CL_SEQR6, 0x12);

		switch (cinfo->btype) {
		case BT_GD5480:
			vga_wseq(cinfo->regbase, CL_SEQRF, 0x98);
			break;
		case BT_ALPINE:
		case BT_LAGUNA:
		case BT_LAGUNAB:
			break;
		case BT_SD64:
#ifdef CONFIG_ZORRO
			vga_wseq(cinfo->regbase, CL_SEQRF, 0xb8);
#endif
			break;
		default:
			vga_wseq(cinfo->regbase, CL_SEQR16, 0x0f);
			vga_wseq(cinfo->regbase, CL_SEQRF, 0xb0);
			break;
		}
	}
	/* plane mask: nothing */
	vga_wseq(cinfo->regbase, VGA_SEQ_PLANE_WRITE, 0xff);
	/* character map select: doesn't even matter in gx mode */
	vga_wseq(cinfo->regbase, VGA_SEQ_CHARACTER_MAP, 0x00);
	/* memory mode: chain4, ext. memory */
	vga_wseq(cinfo->regbase, VGA_SEQ_MEMORY_MODE, 0x0a);

	/* controller-internal base address of video memory */
	if (bi->init_sr07)
		vga_wseq(cinfo->regbase, CL_SEQR7, bi->sr07);

	/*  vga_wseq(cinfo->regbase, CL_SEQR8, 0x00); */
	/* EEPROM control: shouldn't be necessary to write to this at all.. */

	/* graphics cursor X position (incomplete; position gives rem. 3 bits */
	vga_wseq(cinfo->regbase, CL_SEQR10, 0x00);
	/* graphics cursor Y position (..."... ) */
	vga_wseq(cinfo->regbase, CL_SEQR11, 0x00);
	/* graphics cursor attributes */
	vga_wseq(cinfo->regbase, CL_SEQR12, 0x00);
	/* graphics cursor pattern address */
	vga_wseq(cinfo->regbase, CL_SEQR13, 0x00);

	/* writing these on a P4 might give problems..  */
	if (cinfo->btype != BT_PICASSO4) {
		/* configuration readback and ext. color */
		vga_wseq(cinfo->regbase, CL_SEQR17, 0x00);
		/* signature generator */
		vga_wseq(cinfo->regbase, CL_SEQR18, 0x02);
	}

	/* Screen A preset row scan: none */
	vga_wcrt(cinfo->regbase, VGA_CRTC_PRESET_ROW, 0x00);
	/* Text cursor start: disable text cursor */
	vga_wcrt(cinfo->regbase, VGA_CRTC_CURSOR_START, 0x20);
	/* Text cursor end: - */
	vga_wcrt(cinfo->regbase, VGA_CRTC_CURSOR_END, 0x00);
	/* text cursor location high: 0 */
	vga_wcrt(cinfo->regbase, VGA_CRTC_CURSOR_HI, 0x00);
	/* text cursor location low: 0 */
	vga_wcrt(cinfo->regbase, VGA_CRTC_CURSOR_LO, 0x00);

	/* Underline Row scanline: - */
	vga_wcrt(cinfo->regbase, VGA_CRTC_UNDERLINE, 0x00);
	/* ### add 0x40 for text modes with > 30 MHz pixclock */
	/* ext. display controls: ext.adr. wrap */
	vga_wcrt(cinfo->regbase, CL_CRT1B, 0x02);

	/* Set/Reset registers: - */
	vga_wgfx(cinfo->regbase, VGA_GFX_SR_VALUE, 0x00);
	/* Set/Reset enable: - */
	vga_wgfx(cinfo->regbase, VGA_GFX_SR_ENABLE, 0x00);
	/* Color Compare: - */
	vga_wgfx(cinfo->regbase, VGA_GFX_COMPARE_VALUE, 0x00);
	/* Data Rotate: - */
	vga_wgfx(cinfo->regbase, VGA_GFX_DATA_ROTATE, 0x00);
	/* Read Map Select: - */
	vga_wgfx(cinfo->regbase, VGA_GFX_PLANE_READ, 0x00);
	/* Mode: conf. for 16/4/2 color mode, no odd/even, read/write mode 0 */
	vga_wgfx(cinfo->regbase, VGA_GFX_MODE, 0x00);
	/* Miscellaneous: memory map base address, graphics mode */
	vga_wgfx(cinfo->regbase, VGA_GFX_MISC, 0x01);
	/* Color Don't care: involve all planes */
	vga_wgfx(cinfo->regbase, VGA_GFX_COMPARE_MASK, 0x0f);
	/* Bit Mask: no mask at all */
	vga_wgfx(cinfo->regbase, VGA_GFX_BIT_MASK, 0xff);

	if (cinfo->btype == BT_ALPINE || cinfo->btype == BT_SD64 ||
	    is_laguna(cinfo))
		/* (5434 can't have bit 3 set for bitblt) */
		vga_wgfx(cinfo->regbase, CL_GRB, 0x20);
	else
	/* Graphics controller mode extensions: finer granularity,
	 * 8byte data latches
	 */
		vga_wgfx(cinfo->regbase, CL_GRB, 0x28);

	vga_wgfx(cinfo->regbase, CL_GRC, 0xff);	/* Color Key compare: - */
	vga_wgfx(cinfo->regbase, CL_GRD, 0x00);	/* Color Key compare mask: - */
	vga_wgfx(cinfo->regbase, CL_GRE, 0x00);	/* Miscellaneous control: - */
	/* Background color byte 1: - */
	/*  vga_wgfx (cinfo->regbase, CL_GR10, 0x00); */
	/*  vga_wgfx (cinfo->regbase, CL_GR11, 0x00); */

	/* Attribute Controller palette registers: "identity mapping" */
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE0, 0x00);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE1, 0x01);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE2, 0x02);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE3, 0x03);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE4, 0x04);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE5, 0x05);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE6, 0x06);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE7, 0x07);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE8, 0x08);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTE9, 0x09);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTEA, 0x0a);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTEB, 0x0b);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTEC, 0x0c);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTED, 0x0d);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTEE, 0x0e);
	vga_wattr(cinfo->regbase, VGA_ATC_PALETTEF, 0x0f);

	/* Attribute Controller mode: graphics mode */
	vga_wattr(cinfo->regbase, VGA_ATC_MODE, 0x01);
	/* Overscan color reg.: reg. 0 */
	vga_wattr(cinfo->regbase, VGA_ATC_OVERSCAN, 0x00);
	/* Color Plane enable: Enable all 4 planes */
	vga_wattr(cinfo->regbase, VGA_ATC_PLANE_ENABLE, 0x0f);
	/* Color Select: - */
	vga_wattr(cinfo->regbase, VGA_ATC_COLOR_PAGE, 0x00);

	WGen(cinfo, VGA_PEL_MSK, 0xff);	/* Pixel mask: no mask */

	/* BLT Start/status: Blitter reset */
	vga_wgfx(cinfo->regbase, CL_GR31, 0x04);
	/* - " -	   : "end-of-reset" */
	vga_wgfx(cinfo->regbase, CL_GR31, 0x00);

	/* misc... */
	WHDR(cinfo, 0);	/* Hidden DAC register: - */
	return;
}

static void switch_monitor(struct cirrusfb_info *cinfo, int on)
{
#ifdef CONFIG_ZORRO /* only works on Zorro boards */
	static int IsOn = 0;	/* XXX not ok for multiple boards */

	if (cinfo->btype == BT_PICASSO4)
		return;		/* nothing to switch */
	if (cinfo->btype == BT_ALPINE)
		return;		/* nothing to switch */
	if (cinfo->btype == BT_GD5480)
		return;		/* nothing to switch */
	if (cinfo->btype == BT_PICASSO) {
		if ((on && !IsOn) || (!on && IsOn))
			WSFR(cinfo, 0xff);
		return;
	}
	if (on) {
		switch (cinfo->btype) {
		case BT_SD64:
			WSFR(cinfo, cinfo->SFR | 0x21);
			break;
		case BT_PICCOLO:
			WSFR(cinfo, cinfo->SFR | 0x28);
			break;
		case BT_SPECTRUM:
			WSFR(cinfo, 0x6f);
			break;
		default: /* do nothing */ break;
		}
	} else {
		switch (cinfo->btype) {
		case BT_SD64:
			WSFR(cinfo, cinfo->SFR & 0xde);
			break;
		case BT_PICCOLO:
			WSFR(cinfo, cinfo->SFR & 0xd7);
			break;
		case BT_SPECTRUM:
			WSFR(cinfo, 0x4f);
			break;
		default: /* do nothing */
			break;
		}
	}
#endif /* CONFIG_ZORRO */
}

/******************************************/
/* Linux 2.6-style  accelerated functions */
/******************************************/

static int cirrusfb_sync(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;

	if (!is_laguna(cinfo)) {
		while (vga_rgfx(cinfo->regbase, CL_GR31) & 0x03)
			cpu_relax();
	}
	return 0;
}

static void cirrusfb_fillrect(struct fb_info *info,
			      const struct fb_fillrect *region)
{
	struct fb_fillrect modded;
	int vxres, vyres;
	struct cirrusfb_info *cinfo = info->par;
	int m = info->var.bits_per_pixel;
	u32 color = (info->fix.visual == FB_VISUAL_TRUECOLOR) ?
		cinfo->pseudo_palette[region->color] : region->color;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_fillrect(info, region);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;

	memcpy(&modded, region, sizeof(struct fb_fillrect));

	if (!modded.width || !modded.height ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;

	if (modded.dx + modded.width  > vxres)
		modded.width  = vxres - modded.dx;
	if (modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	cirrusfb_RectFill(cinfo->regbase,
			  info->var.bits_per_pixel,
			  (region->dx * m) / 8, region->dy,
			  (region->width * m) / 8, region->height,
			  color, color,
			  info->fix.line_length, 0x40);
}

static void cirrusfb_copyarea(struct fb_info *info,
			      const struct fb_copyarea *area)
{
	struct fb_copyarea modded;
	u32 vxres, vyres;
	struct cirrusfb_info *cinfo = info->par;
	int m = info->var.bits_per_pixel;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (info->flags & FBINFO_HWACCEL_DISABLED) {
		cfb_copyarea(info, area);
		return;
	}

	vxres = info->var.xres_virtual;
	vyres = info->var.yres_virtual;
	memcpy(&modded, area, sizeof(struct fb_copyarea));

	if (!modded.width || !modded.height ||
	   modded.sx >= vxres || modded.sy >= vyres ||
	   modded.dx >= vxres || modded.dy >= vyres)
		return;

	if (modded.sx + modded.width > vxres)
		modded.width = vxres - modded.sx;
	if (modded.dx + modded.width > vxres)
		modded.width = vxres - modded.dx;
	if (modded.sy + modded.height > vyres)
		modded.height = vyres - modded.sy;
	if (modded.dy + modded.height > vyres)
		modded.height = vyres - modded.dy;

	cirrusfb_BitBLT(cinfo->regbase, info->var.bits_per_pixel,
			(area->sx * m) / 8, area->sy,
			(area->dx * m) / 8, area->dy,
			(area->width * m) / 8, area->height,
			info->fix.line_length);

}

static void cirrusfb_imageblit(struct fb_info *info,
			       const struct fb_image *image)
{
	struct cirrusfb_info *cinfo = info->par;
	unsigned char op = (info->var.bits_per_pixel == 24) ? 0xc : 0x4;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	/* Alpine/SD64 does not work at 24bpp ??? */
	if (info->flags & FBINFO_HWACCEL_DISABLED || image->depth != 1)
		cfb_imageblit(info, image);
	else if ((cinfo->btype == BT_ALPINE || cinfo->btype == BT_SD64) &&
		  op == 0xc)
		cfb_imageblit(info, image);
	else {
		unsigned size = ((image->width + 7) >> 3) * image->height;
		int m = info->var.bits_per_pixel;
		u32 fg, bg;

		if (info->var.bits_per_pixel == 8) {
			fg = image->fg_color;
			bg = image->bg_color;
		} else {
			fg = ((u32 *)(info->pseudo_palette))[image->fg_color];
			bg = ((u32 *)(info->pseudo_palette))[image->bg_color];
		}
		if (info->var.bits_per_pixel == 24) {
			/* clear background first */
			cirrusfb_RectFill(cinfo->regbase,
					  info->var.bits_per_pixel,
					  (image->dx * m) / 8, image->dy,
					  (image->width * m) / 8,
					  image->height,
					  bg, bg,
					  info->fix.line_length, 0x40);
		}
		cirrusfb_RectFill(cinfo->regbase,
				  info->var.bits_per_pixel,
				  (image->dx * m) / 8, image->dy,
				  (image->width * m) / 8, image->height,
				  fg, bg,
				  info->fix.line_length, op);
		memcpy(info->screen_base, image->data, size);
	}
}

#ifdef CONFIG_PCI
static int release_io_ports;

/* Pulled the logic from XFree86 Cirrus driver to get the memory size,
 * based on the DRAM bandwidth bit and DRAM bank switching bit.  This
 * works with 1MB, 2MB and 4MB configurations (which the Motorola boards
 * seem to have. */
static unsigned int cirrusfb_get_memsize(struct fb_info *info,
					 u8 __iomem *regbase)
{
	unsigned long mem;
	struct cirrusfb_info *cinfo = info->par;

	if (is_laguna(cinfo)) {
		unsigned char SR14 = vga_rseq(regbase, CL_SEQR14);

		mem = ((SR14 & 7) + 1) << 20;
	} else {
		unsigned char SRF = vga_rseq(regbase, CL_SEQRF);
		switch ((SRF & 0x18)) {
		case 0x08:
			mem = 512 * 1024;
			break;
		case 0x10:
			mem = 1024 * 1024;
			break;
		/* 64-bit DRAM data bus width; assume 2MB.
		 * Also indicates 2MB memory on the 5430.
		 */
		case 0x18:
			mem = 2048 * 1024;
			break;
		default:
			dev_warn(info->device, "Unknown memory size!\n");
			mem = 1024 * 1024;
		}
		/* If DRAM bank switching is enabled, there must be
		 * twice as much memory installed. (4MB on the 5434)
		 */
		if (cinfo->btype != BT_ALPINE && (SRF & 0x80) != 0)
			mem *= 2;
	}

	/* TODO: Handling of GD5446/5480 (see XF86 sources ...) */
	return mem;
}

static void get_pci_addrs(const struct pci_dev *pdev,
			  unsigned long *display, unsigned long *registers)
{
	assert(pdev != NULL);
	assert(display != NULL);
	assert(registers != NULL);

	*display = 0;
	*registers = 0;

	/* This is a best-guess for now */

	if (pci_resource_flags(pdev, 0) & IORESOURCE_IO) {
		*display = pci_resource_start(pdev, 1);
		*registers = pci_resource_start(pdev, 0);
	} else {
		*display = pci_resource_start(pdev, 0);
		*registers = pci_resource_start(pdev, 1);
	}

	assert(*display != 0);
}

static void cirrusfb_pci_unmap(struct fb_info *info)
{
	struct pci_dev *pdev = to_pci_dev(info->device);
	struct cirrusfb_info *cinfo = info->par;

	if (cinfo->laguna_mmio == NULL)
		iounmap(cinfo->laguna_mmio);
	iounmap(info->screen_base);
#if 0 /* if system didn't claim this region, we would... */
	release_mem_region(0xA0000, 65535);
#endif
	if (release_io_ports)
		release_region(0x3C0, 32);
	pci_release_regions(pdev);
}
#endif /* CONFIG_PCI */

#ifdef CONFIG_ZORRO
static void cirrusfb_zorro_unmap(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;
	struct zorro_dev *zdev = to_zorro_dev(info->device);

	if (info->fix.smem_start > 16 * MB_)
		iounmap(info->screen_base);
	if (info->fix.mmio_start > 16 * MB_)
		iounmap(cinfo->regbase);

	zorro_release_device(zdev);
}
#endif /* CONFIG_ZORRO */

/* function table of the above functions */
static struct fb_ops cirrusfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= cirrusfb_open,
	.fb_release	= cirrusfb_release,
	.fb_setcolreg	= cirrusfb_setcolreg,
	.fb_check_var	= cirrusfb_check_var,
	.fb_set_par	= cirrusfb_set_par,
	.fb_pan_display = cirrusfb_pan_display,
	.fb_blank	= cirrusfb_blank,
	.fb_fillrect	= cirrusfb_fillrect,
	.fb_copyarea	= cirrusfb_copyarea,
	.fb_sync	= cirrusfb_sync,
	.fb_imageblit	= cirrusfb_imageblit,
};

static int cirrusfb_set_fbinfo(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;
	struct fb_var_screeninfo *var = &info->var;

	info->pseudo_palette = cinfo->pseudo_palette;
	info->flags = FBINFO_DEFAULT
		    | FBINFO_HWACCEL_XPAN
		    | FBINFO_HWACCEL_YPAN
		    | FBINFO_HWACCEL_FILLRECT
		    | FBINFO_HWACCEL_IMAGEBLIT
		    | FBINFO_HWACCEL_COPYAREA;
	if (noaccel || is_laguna(cinfo)) {
		info->flags |= FBINFO_HWACCEL_DISABLED;
		info->fix.accel = FB_ACCEL_NONE;
	} else
		info->fix.accel = FB_ACCEL_CIRRUS_ALPINE;

	info->fbops = &cirrusfb_ops;

	if (cinfo->btype == BT_GD5480) {
		if (var->bits_per_pixel == 16)
			info->screen_base += 1 * MB_;
		if (var->bits_per_pixel == 32)
			info->screen_base += 2 * MB_;
	}

	/* Fill fix common fields */
	strlcpy(info->fix.id, cirrusfb_board_info[cinfo->btype].name,
		sizeof(info->fix.id));

	/* monochrome: only 1 memory plane */
	/* 8 bit and above: Use whole memory area */
	info->fix.smem_len   = info->screen_size;
	if (var->bits_per_pixel == 1)
		info->fix.smem_len /= 4;
	info->fix.type_aux   = 0;
	info->fix.xpanstep   = 1;
	info->fix.ypanstep   = 1;
	info->fix.ywrapstep  = 0;

	/* FIXME: map region at 0xB8000 if available, fill in here */
	info->fix.mmio_len   = 0;

	fb_alloc_cmap(&info->cmap, 256, 0);

	return 0;
}

static int cirrusfb_register(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;
	int err;

	/* sanity checks */
	assert(cinfo->btype != BT_NONE);

	/* set all the vital stuff */
	cirrusfb_set_fbinfo(info);

	dev_dbg(info->device, "(RAM start set to: 0x%p)\n", info->screen_base);

	err = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 8);
	if (!err) {
		dev_dbg(info->device, "wrong initial video mode\n");
		err = -EINVAL;
		goto err_dealloc_cmap;
	}

	info->var.activate = FB_ACTIVATE_NOW;

	err = cirrusfb_check_var(&info->var, info);
	if (err < 0) {
		/* should never happen */
		dev_dbg(info->device,
			"choking on default var... umm, no good.\n");
		goto err_dealloc_cmap;
	}

	err = register_framebuffer(info);
	if (err < 0) {
		dev_err(info->device,
			"could not register fb device; err = %d!\n", err);
		goto err_dealloc_cmap;
	}

	return 0;

err_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);
	return err;
}

static void cirrusfb_cleanup(struct fb_info *info)
{
	struct cirrusfb_info *cinfo = info->par;

	switch_monitor(cinfo, 0);
	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);
	dev_dbg(info->device, "Framebuffer unregistered\n");
	cinfo->unmap(info);
	framebuffer_release(info);
}

#ifdef CONFIG_PCI
static int cirrusfb_pci_register(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct cirrusfb_info *cinfo;
	struct fb_info *info;
	unsigned long board_addr, board_size;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		printk(KERN_ERR "cirrusfb: Cannot enable PCI device\n");
		goto err_out;
	}

	info = framebuffer_alloc(sizeof(struct cirrusfb_info), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err_out;
	}

	cinfo = info->par;
	cinfo->btype = (enum cirrus_board) ent->driver_data;

	dev_dbg(info->device,
		" Found PCI device, base address 0 is 0x%Lx, btype set to %d\n",
		(unsigned long long)pdev->resource[0].start,  cinfo->btype);
	dev_dbg(info->device, " base address 1 is 0x%Lx\n",
		(unsigned long long)pdev->resource[1].start);

	dev_dbg(info->device,
		"Attempt to get PCI info for Cirrus Graphics Card\n");
	get_pci_addrs(pdev, &board_addr, &info->fix.mmio_start);
	/* FIXME: this forces VGA.  alternatives? */
	cinfo->regbase = NULL;
	cinfo->laguna_mmio = ioremap(info->fix.mmio_start, 0x1000);

	dev_dbg(info->device, "Board address: 0x%lx, register address: 0x%lx\n",
		board_addr, info->fix.mmio_start);

	board_size = (cinfo->btype == BT_GD5480) ?
		32 * MB_ : cirrusfb_get_memsize(info, cinfo->regbase);

	ret = pci_request_regions(pdev, "cirrusfb");
	if (ret < 0) {
		dev_err(info->device, "cannot reserve region 0x%lx, abort\n",
			board_addr);
		goto err_release_fb;
	}
#if 0 /* if the system didn't claim this region, we would... */
	if (!request_mem_region(0xA0000, 65535, "cirrusfb")) {
		dev_err(info->device, "cannot reserve region 0x%lx, abort\n",
			0xA0000L);
		ret = -EBUSY;
		goto err_release_regions;
	}
#endif
	if (request_region(0x3C0, 32, "cirrusfb"))
		release_io_ports = 1;

	info->screen_base = ioremap(board_addr, board_size);
	if (!info->screen_base) {
		ret = -EIO;
		goto err_release_legacy;
	}

	info->fix.smem_start = board_addr;
	info->screen_size = board_size;
	cinfo->unmap = cirrusfb_pci_unmap;

	dev_info(info->device,
		 "Cirrus Logic chipset on PCI bus, RAM (%lu kB) at 0x%lx\n",
		 info->screen_size >> 10, board_addr);
	pci_set_drvdata(pdev, info);

	ret = cirrusfb_register(info);
	if (!ret)
		return 0;

	iounmap(info->screen_base);
err_release_legacy:
	if (release_io_ports)
		release_region(0x3C0, 32);
#if 0
	release_mem_region(0xA0000, 65535);
err_release_regions:
#endif
	pci_release_regions(pdev);
err_release_fb:
	if (cinfo->laguna_mmio != NULL)
		iounmap(cinfo->laguna_mmio);
	framebuffer_release(info);
err_out:
	return ret;
}

static void cirrusfb_pci_unregister(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);

	cirrusfb_cleanup(info);
}

static struct pci_driver cirrusfb_pci_driver = {
	.name		= "cirrusfb",
	.id_table	= cirrusfb_pci_table,
	.probe		= cirrusfb_pci_register,
	.remove		= cirrusfb_pci_unregister,
#ifdef CONFIG_PM
#if 0
	.suspend	= cirrusfb_pci_suspend,
	.resume		= cirrusfb_pci_resume,
#endif
#endif
};
#endif /* CONFIG_PCI */

#ifdef CONFIG_ZORRO
static int cirrusfb_zorro_register(struct zorro_dev *z,
				   const struct zorro_device_id *ent)
{
	struct fb_info *info;
	int error;
	const struct zorrocl *zcl;
	enum cirrus_board btype;
	unsigned long regbase, ramsize, rambase;
	struct cirrusfb_info *cinfo;

	info = framebuffer_alloc(sizeof(struct cirrusfb_info), &z->dev);
	if (!info)
		return -ENOMEM;

	zcl = (const struct zorrocl *)ent->driver_data;
	btype = zcl->type;
	regbase = zorro_resource_start(z) + zcl->regoffset;
	ramsize = zcl->ramsize;
	if (ramsize) {
		rambase = zorro_resource_start(z) + zcl->ramoffset;
		if (zorro_resource_len(z) == 64 * MB_) {
			/* Quirk for 64 MiB Picasso IV */
			rambase += zcl->ramoffset;
		}
	} else {
		struct zorro_dev *ram = zorro_find_device(zcl->ramid, NULL);
		if (!ram || !zorro_resource_len(ram)) {
			dev_err(info->device, "No video RAM found\n");
			error = -ENODEV;
			goto err_release_fb;
		}
		rambase = zorro_resource_start(ram);
		ramsize = zorro_resource_len(ram);
		if (zcl->ramid2 &&
		    (ram = zorro_find_device(zcl->ramid2, NULL))) {
			if (zorro_resource_start(ram) != rambase + ramsize) {
				dev_warn(info->device,
					 "Skipping non-contiguous RAM at %pR\n",
					 &ram->resource);
			} else {
				ramsize += zorro_resource_len(ram);
			}
		}
	}

	dev_info(info->device,
		 "%s board detected, REG at 0x%lx, %lu MiB RAM at 0x%lx\n",
		 cirrusfb_board_info[btype].name, regbase, ramsize / MB_,
		 rambase);

	if (!zorro_request_device(z, "cirrusfb")) {
		dev_err(info->device, "Cannot reserve %pR\n", &z->resource);
		error = -EBUSY;
		goto err_release_fb;
	}

	cinfo = info->par;
	cinfo->btype = btype;

	info->fix.mmio_start = regbase;
	cinfo->regbase = regbase > 16 * MB_ ? ioremap(regbase, 64 * 1024)
					    : ZTWO_VADDR(regbase);
	if (!cinfo->regbase) {
		dev_err(info->device, "Cannot map registers\n");
		error = -EIO;
		goto err_release_dev;
	}

	info->fix.smem_start = rambase;
	info->screen_size = ramsize;
	info->screen_base = rambase > 16 * MB_ ? ioremap(rambase, ramsize)
					       : ZTWO_VADDR(rambase);
	if (!info->screen_base) {
		dev_err(info->device, "Cannot map video RAM\n");
		error = -EIO;
		goto err_unmap_reg;
	}

	cinfo->unmap = cirrusfb_zorro_unmap;

	dev_info(info->device,
		 "Cirrus Logic chipset on Zorro bus, RAM (%lu MiB) at 0x%lx\n",
		 ramsize / MB_, rambase);

	/* MCLK select etc. */
	if (cirrusfb_board_info[btype].init_sr1f)
		vga_wseq(cinfo->regbase, CL_SEQR1F,
			 cirrusfb_board_info[btype].sr1f);

	error = cirrusfb_register(info);
	if (error) {
		dev_err(info->device, "Failed to register device, error %d\n",
			error);
		goto err_unmap_ram;
	}

	zorro_set_drvdata(z, info);
	return 0;

err_unmap_ram:
	if (rambase > 16 * MB_)
		iounmap(info->screen_base);

err_unmap_reg:
	if (regbase > 16 * MB_)
		iounmap(cinfo->regbase);
err_release_dev:
	zorro_release_device(z);
err_release_fb:
	framebuffer_release(info);
	return error;
}

void cirrusfb_zorro_unregister(struct zorro_dev *z)
{
	struct fb_info *info = zorro_get_drvdata(z);

	cirrusfb_cleanup(info);
	zorro_set_drvdata(z, NULL);
}

static struct zorro_driver cirrusfb_zorro_driver = {
	.name		= "cirrusfb",
	.id_table	= cirrusfb_zorro_table,
	.probe		= cirrusfb_zorro_register,
	.remove		= cirrusfb_zorro_unregister,
};
#endif /* CONFIG_ZORRO */

#ifndef MODULE
static int __init cirrusfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strcmp(this_opt, "noaccel"))
			noaccel = 1;
		else if (!strncmp(this_opt, "mode:", 5))
			mode_option = this_opt + 5;
		else
			mode_option = this_opt;
	}
	return 0;
}
#endif

    /*
     *  Modularization
     */

MODULE_AUTHOR("Copyright 1999,2000 Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION("Accelerated FBDev driver for Cirrus Logic chips");
MODULE_LICENSE("GPL");

static int __init cirrusfb_init(void)
{
	int error = 0;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("cirrusfb", &option))
		return -ENODEV;
	cirrusfb_setup(option);
#endif

#ifdef CONFIG_ZORRO
	error |= zorro_register_driver(&cirrusfb_zorro_driver);
#endif
#ifdef CONFIG_PCI
	error |= pci_register_driver(&cirrusfb_pci_driver);
#endif
	return error;
}

static void __exit cirrusfb_exit(void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver(&cirrusfb_pci_driver);
#endif
#ifdef CONFIG_ZORRO
	zorro_unregister_driver(&cirrusfb_zorro_driver);
#endif
}

module_init(cirrusfb_init);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Initial video mode e.g. '648x480-8@60'");
module_param(noaccel, bool, 0);
MODULE_PARM_DESC(noaccel, "Disable acceleration");

#ifdef MODULE
module_exit(cirrusfb_exit);
#endif

/**********************************************************************/
/* about the following functions - I have used the same names for the */
/* functions as Markus Wild did in his Retina driver for NetBSD as    */
/* they just made sense for this purpose. Apart from that, I wrote    */
/* these functions myself.					    */
/**********************************************************************/

/*** WGen() - write into one of the external/general registers ***/
static void WGen(const struct cirrusfb_info *cinfo,
		  int regnum, unsigned char val)
{
	unsigned long regofs = 0;

	if (cinfo->btype == BT_PICASSO) {
		/* Picasso II specific hack */
/*	      if (regnum == VGA_PEL_IR || regnum == VGA_PEL_D ||
		  regnum == CL_VSSM2) */
		if (regnum == VGA_PEL_IR || regnum == VGA_PEL_D)
			regofs = 0xfff;
	}

	vga_w(cinfo->regbase, regofs + regnum, val);
}

/*** RGen() - read out one of the external/general registers ***/
static unsigned char RGen(const struct cirrusfb_info *cinfo, int regnum)
{
	unsigned long regofs = 0;

	if (cinfo->btype == BT_PICASSO) {
		/* Picasso II specific hack */
/*	      if (regnum == VGA_PEL_IR || regnum == VGA_PEL_D ||
		  regnum == CL_VSSM2) */
		if (regnum == VGA_PEL_IR || regnum == VGA_PEL_D)
			regofs = 0xfff;
	}

	return vga_r(cinfo->regbase, regofs + regnum);
}

/*** AttrOn() - turn on VideoEnable for Attribute controller ***/
static void AttrOn(const struct cirrusfb_info *cinfo)
{
	assert(cinfo != NULL);

	if (vga_rcrt(cinfo->regbase, CL_CRT24) & 0x80) {
		/* if we're just in "write value" mode, write back the */
		/* same value as before to not modify anything */
		vga_w(cinfo->regbase, VGA_ATT_IW,
		      vga_r(cinfo->regbase, VGA_ATT_R));
	}
	/* turn on video bit */
/*      vga_w(cinfo->regbase, VGA_ATT_IW, 0x20); */
	vga_w(cinfo->regbase, VGA_ATT_IW, 0x33);

	/* dummy write on Reg0 to be on "write index" mode next time */
	vga_w(cinfo->regbase, VGA_ATT_IW, 0x00);
}

/*** WHDR() - write into the Hidden DAC register ***/
/* as the HDR is the only extension register that requires special treatment
 * (the other extension registers are accessible just like the "ordinary"
 * registers of their functional group) here is a specialized routine for
 * accessing the HDR
 */
static void WHDR(const struct cirrusfb_info *cinfo, unsigned char val)
{
	unsigned char dummy;

	if (is_laguna(cinfo))
		return;
	if (cinfo->btype == BT_PICASSO) {
		/* Klaus' hint for correct access to HDR on some boards */
		/* first write 0 to pixel mask (3c6) */
		WGen(cinfo, VGA_PEL_MSK, 0x00);
		udelay(200);
		/* next read dummy from pixel address (3c8) */
		dummy = RGen(cinfo, VGA_PEL_IW);
		udelay(200);
	}
	/* now do the usual stuff to access the HDR */

	dummy = RGen(cinfo, VGA_PEL_MSK);
	udelay(200);
	dummy = RGen(cinfo, VGA_PEL_MSK);
	udelay(200);
	dummy = RGen(cinfo, VGA_PEL_MSK);
	udelay(200);
	dummy = RGen(cinfo, VGA_PEL_MSK);
	udelay(200);

	WGen(cinfo, VGA_PEL_MSK, val);
	udelay(200);

	if (cinfo->btype == BT_PICASSO) {
		/* now first reset HDR access counter */
		dummy = RGen(cinfo, VGA_PEL_IW);
		udelay(200);

		/* and at the end, restore the mask value */
		/* ## is this mask always 0xff? */
		WGen(cinfo, VGA_PEL_MSK, 0xff);
		udelay(200);
	}
}

/*** WSFR() - write to the "special function register" (SFR) ***/
static void WSFR(struct cirrusfb_info *cinfo, unsigned char val)
{
#ifdef CONFIG_ZORRO
	assert(cinfo->regbase != NULL);
	cinfo->SFR = val;
	z_writeb(val, cinfo->regbase + 0x8000);
#endif
}

/* The Picasso has a second register for switching the monitor bit */
static void WSFR2(struct cirrusfb_info *cinfo, unsigned char val)
{
#ifdef CONFIG_ZORRO
	/* writing an arbitrary value to this one causes the monitor switcher */
	/* to flip to Amiga display */
	assert(cinfo->regbase != NULL);
	cinfo->SFR = val;
	z_writeb(val, cinfo->regbase + 0x9000);
#endif
}

/*** WClut - set CLUT entry (range: 0..63) ***/
static void WClut(struct cirrusfb_info *cinfo, unsigned char regnum, unsigned char red,
	    unsigned char green, unsigned char blue)
{
	unsigned int data = VGA_PEL_D;

	/* address write mode register is not translated.. */
	vga_w(cinfo->regbase, VGA_PEL_IW, regnum);

	if (cinfo->btype == BT_PICASSO || cinfo->btype == BT_PICASSO4 ||
	    cinfo->btype == BT_ALPINE || cinfo->btype == BT_GD5480 ||
	    cinfo->btype == BT_SD64 || is_laguna(cinfo)) {
		/* but DAC data register IS, at least for Picasso II */
		if (cinfo->btype == BT_PICASSO)
			data += 0xfff;
		vga_w(cinfo->regbase, data, red);
		vga_w(cinfo->regbase, data, green);
		vga_w(cinfo->regbase, data, blue);
	} else {
		vga_w(cinfo->regbase, data, blue);
		vga_w(cinfo->regbase, data, green);
		vga_w(cinfo->regbase, data, red);
	}
}

#if 0
/*** RClut - read CLUT entry (range 0..63) ***/
static void RClut(struct cirrusfb_info *cinfo, unsigned char regnum, unsigned char *red,
	    unsigned char *green, unsigned char *blue)
{
	unsigned int data = VGA_PEL_D;

	vga_w(cinfo->regbase, VGA_PEL_IR, regnum);

	if (cinfo->btype == BT_PICASSO || cinfo->btype == BT_PICASSO4 ||
	    cinfo->btype == BT_ALPINE || cinfo->btype == BT_GD5480) {
		if (cinfo->btype == BT_PICASSO)
			data += 0xfff;
		*red = vga_r(cinfo->regbase, data);
		*green = vga_r(cinfo->regbase, data);
		*blue = vga_r(cinfo->regbase, data);
	} else {
		*blue = vga_r(cinfo->regbase, data);
		*green = vga_r(cinfo->regbase, data);
		*red = vga_r(cinfo->regbase, data);
	}
}
#endif

/*******************************************************************
	cirrusfb_WaitBLT()

	Wait for the BitBLT engine to complete a possible earlier job
*********************************************************************/

/* FIXME: use interrupts instead */
static void cirrusfb_WaitBLT(u8 __iomem *regbase)
{
	while (vga_rgfx(regbase, CL_GR31) & 0x08)
		cpu_relax();
}

/*******************************************************************
	cirrusfb_BitBLT()

	perform accelerated "scrolling"
********************************************************************/

static void cirrusfb_set_blitter(u8 __iomem *regbase,
			    u_short nwidth, u_short nheight,
			    u_long nsrc, u_long ndest,
			    u_short bltmode, u_short line_length)

{
	/* pitch: set to line_length */
	/* dest pitch low */
	vga_wgfx(regbase, CL_GR24, line_length & 0xff);
	/* dest pitch hi */
	vga_wgfx(regbase, CL_GR25, line_length >> 8);
	/* source pitch low */
	vga_wgfx(regbase, CL_GR26, line_length & 0xff);
	/* source pitch hi */
	vga_wgfx(regbase, CL_GR27, line_length >> 8);

	/* BLT width: actual number of pixels - 1 */
	/* BLT width low */
	vga_wgfx(regbase, CL_GR20, nwidth & 0xff);
	/* BLT width hi */
	vga_wgfx(regbase, CL_GR21, nwidth >> 8);

	/* BLT height: actual number of lines -1 */
	/* BLT height low */
	vga_wgfx(regbase, CL_GR22, nheight & 0xff);
	/* BLT width hi */
	vga_wgfx(regbase, CL_GR23, nheight >> 8);

	/* BLT destination */
	/* BLT dest low */
	vga_wgfx(regbase, CL_GR28, (u_char) (ndest & 0xff));
	/* BLT dest mid */
	vga_wgfx(regbase, CL_GR29, (u_char) (ndest >> 8));
	/* BLT dest hi */
	vga_wgfx(regbase, CL_GR2A, (u_char) (ndest >> 16));

	/* BLT source */
	/* BLT src low */
	vga_wgfx(regbase, CL_GR2C, (u_char) (nsrc & 0xff));
	/* BLT src mid */
	vga_wgfx(regbase, CL_GR2D, (u_char) (nsrc >> 8));
	/* BLT src hi */
	vga_wgfx(regbase, CL_GR2E, (u_char) (nsrc >> 16));

	/* BLT mode */
	vga_wgfx(regbase, CL_GR30, bltmode);	/* BLT mode */

	/* BLT ROP: SrcCopy */
	vga_wgfx(regbase, CL_GR32, 0x0d);	/* BLT ROP */

	/* and finally: GO! */
	vga_wgfx(regbase, CL_GR31, 0x02);	/* BLT Start/status */
}

/*******************************************************************
	cirrusfb_BitBLT()

	perform accelerated "scrolling"
********************************************************************/

static void cirrusfb_BitBLT(u8 __iomem *regbase, int bits_per_pixel,
			    u_short curx, u_short cury,
			    u_short destx, u_short desty,
			    u_short width, u_short height,
			    u_short line_length)
{
	u_short nwidth = width - 1;
	u_short nheight = height - 1;
	u_long nsrc, ndest;
	u_char bltmode;

	bltmode = 0x00;
	/* if source adr < dest addr, do the Blt backwards */
	if (cury <= desty) {
		if (cury == desty) {
			/* if src and dest are on the same line, check x */
			if (curx < destx)
				bltmode |= 0x01;
		} else
			bltmode |= 0x01;
	}
	/* standard case: forward blitting */
	nsrc = (cury * line_length) + curx;
	ndest = (desty * line_length) + destx;
	if (bltmode) {
		/* this means start addresses are at the end,
		 * counting backwards
		 */
		nsrc += nheight * line_length + nwidth;
		ndest += nheight * line_length + nwidth;
	}

	cirrusfb_WaitBLT(regbase);

	cirrusfb_set_blitter(regbase, nwidth, nheight,
			    nsrc, ndest, bltmode, line_length);
}

/*******************************************************************
	cirrusfb_RectFill()

	perform accelerated rectangle fill
********************************************************************/

static void cirrusfb_RectFill(u8 __iomem *regbase, int bits_per_pixel,
		     u_short x, u_short y, u_short width, u_short height,
		     u32 fg_color, u32 bg_color, u_short line_length,
		     u_char blitmode)
{
	u_long ndest = (y * line_length) + x;
	u_char op;

	cirrusfb_WaitBLT(regbase);

	/* This is a ColorExpand Blt, using the */
	/* same color for foreground and background */
	vga_wgfx(regbase, VGA_GFX_SR_VALUE, bg_color);
	vga_wgfx(regbase, VGA_GFX_SR_ENABLE, fg_color);

	op = 0x80;
	if (bits_per_pixel >= 16) {
		vga_wgfx(regbase, CL_GR10, bg_color >> 8);
		vga_wgfx(regbase, CL_GR11, fg_color >> 8);
		op = 0x90;
	}
	if (bits_per_pixel >= 24) {
		vga_wgfx(regbase, CL_GR12, bg_color >> 16);
		vga_wgfx(regbase, CL_GR13, fg_color >> 16);
		op = 0xa0;
	}
	if (bits_per_pixel == 32) {
		vga_wgfx(regbase, CL_GR14, bg_color >> 24);
		vga_wgfx(regbase, CL_GR15, fg_color >> 24);
		op = 0xb0;
	}
	cirrusfb_set_blitter(regbase, width - 1, height - 1,
			    0, ndest, op | blitmode, line_length);
}

/**************************************************************************
 * bestclock() - determine closest possible clock lower(?) than the
 * desired pixel clock
 **************************************************************************/
static void bestclock(long freq, int *nom, int *den, int *div)
{
	int n, d;
	long h, diff;

	assert(nom != NULL);
	assert(den != NULL);
	assert(div != NULL);

	*nom = 0;
	*den = 0;
	*div = 0;

	if (freq < 8000)
		freq = 8000;

	diff = freq;

	for (n = 32; n < 128; n++) {
		int s = 0;

		d = (14318 * n) / freq;
		if ((d >= 7) && (d <= 63)) {
			int temp = d;

			if (temp > 31) {
				s = 1;
				temp >>= 1;
			}
			h = ((14318 * n) / temp) >> s;
			h = h > freq ? h - freq : freq - h;
			if (h < diff) {
				diff = h;
				*nom = n;
				*den = temp;
				*div = s;
			}
		}
		d++;
		if ((d >= 7) && (d <= 63)) {
			if (d > 31) {
				s = 1;
				d >>= 1;
			}
			h = ((14318 * n) / d) >> s;
			h = h > freq ? h - freq : freq - h;
			if (h < diff) {
				diff = h;
				*nom = n;
				*den = d;
				*div = s;
			}
		}
	}
}

/* -------------------------------------------------------------------------
 *
 * debugging functions
 *
 * -------------------------------------------------------------------------
 */

#ifdef CIRRUSFB_DEBUG

/**
 * cirrusfb_dbg_print_regs
 * @base: If using newmmio, the newmmio base address, otherwise %NULL
 * @reg_class: type of registers to read: %CRT, or %SEQ
 *
 * DESCRIPTION:
 * Dumps the given list of VGA CRTC registers.  If @base is %NULL,
 * old-style I/O ports are queried for information, otherwise MMIO is
 * used at the given @base address to query the information.
 */

static void cirrusfb_dbg_print_regs(struct fb_info *info,
				    caddr_t regbase,
				    enum cirrusfb_dbg_reg_class reg_class, ...)
{
	va_list list;
	unsigned char val = 0;
	unsigned reg;
	char *name;

	va_start(list, reg_class);

	name = va_arg(list, char *);
	while (name != NULL) {
		reg = va_arg(list, int);

		switch (reg_class) {
		case CRT:
			val = vga_rcrt(regbase, (unsigned char) reg);
			break;
		case SEQ:
			val = vga_rseq(regbase, (unsigned char) reg);
			break;
		default:
			/* should never occur */
			assert(false);
			break;
		}

		dev_dbg(info->device, "%8s = 0x%02X\n", name, val);

		name = va_arg(list, char *);
	}

	va_end(list);
}

/**
 * cirrusfb_dbg_reg_dump
 * @base: If using newmmio, the newmmio base address, otherwise %NULL
 *
 * DESCRIPTION:
 * Dumps a list of interesting VGA and CIRRUSFB registers.  If @base is %NULL,
 * old-style I/O ports are queried for information, otherwise MMIO is
 * used at the given @base address to query the information.
 */

static void cirrusfb_dbg_reg_dump(struct fb_info *info, caddr_t regbase)
{
	dev_dbg(info->device, "VGA CRTC register dump:\n");

	cirrusfb_dbg_print_regs(info, regbase, CRT,
			   "CR00", 0x00,
			   "CR01", 0x01,
			   "CR02", 0x02,
			   "CR03", 0x03,
			   "CR04", 0x04,
			   "CR05", 0x05,
			   "CR06", 0x06,
			   "CR07", 0x07,
			   "CR08", 0x08,
			   "CR09", 0x09,
			   "CR0A", 0x0A,
			   "CR0B", 0x0B,
			   "CR0C", 0x0C,
			   "CR0D", 0x0D,
			   "CR0E", 0x0E,
			   "CR0F", 0x0F,
			   "CR10", 0x10,
			   "CR11", 0x11,
			   "CR12", 0x12,
			   "CR13", 0x13,
			   "CR14", 0x14,
			   "CR15", 0x15,
			   "CR16", 0x16,
			   "CR17", 0x17,
			   "CR18", 0x18,
			   "CR22", 0x22,
			   "CR24", 0x24,
			   "CR26", 0x26,
			   "CR2D", 0x2D,
			   "CR2E", 0x2E,
			   "CR2F", 0x2F,
			   "CR30", 0x30,
			   "CR31", 0x31,
			   "CR32", 0x32,
			   "CR33", 0x33,
			   "CR34", 0x34,
			   "CR35", 0x35,
			   "CR36", 0x36,
			   "CR37", 0x37,
			   "CR38", 0x38,
			   "CR39", 0x39,
			   "CR3A", 0x3A,
			   "CR3B", 0x3B,
			   "CR3C", 0x3C,
			   "CR3D", 0x3D,
			   "CR3E", 0x3E,
			   "CR3F", 0x3F,
			   NULL);

	dev_dbg(info->device, "\n");

	dev_dbg(info->device, "VGA SEQ register dump:\n");

	cirrusfb_dbg_print_regs(info, regbase, SEQ,
			   "SR00", 0x00,
			   "SR01", 0x01,
			   "SR02", 0x02,
			   "SR03", 0x03,
			   "SR04", 0x04,
			   "SR08", 0x08,
			   "SR09", 0x09,
			   "SR0A", 0x0A,
			   "SR0B", 0x0B,
			   "SR0D", 0x0D,
			   "SR10", 0x10,
			   "SR11", 0x11,
			   "SR12", 0x12,
			   "SR13", 0x13,
			   "SR14", 0x14,
			   "SR15", 0x15,
			   "SR16", 0x16,
			   "SR17", 0x17,
			   "SR18", 0x18,
			   "SR19", 0x19,
			   "SR1A", 0x1A,
			   "SR1B", 0x1B,
			   "SR1C", 0x1C,
			   "SR1D", 0x1D,
			   "SR1E", 0x1E,
			   "SR1F", 0x1F,
			   NULL);

	dev_dbg(info->device, "\n");
}

#endif				/* CIRRUSFB_DEBUG */

