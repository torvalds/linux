// SPDX-License-Identifier: GPL-2.0-only
/* ffb.c: Creator/Elite3D frame buffer driver
 *
 * Copyright (C) 2003, 2006 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997,1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * Driver layout based loosely on tgafb.c, see that file for credits.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/upa.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int ffb_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int ffb_blank(int, struct fb_info *);

static void ffb_imageblit(struct fb_info *, const struct fb_image *);
static void ffb_fillrect(struct fb_info *, const struct fb_fillrect *);
static void ffb_copyarea(struct fb_info *, const struct fb_copyarea *);
static int ffb_sync(struct fb_info *);
static int ffb_pan_display(struct fb_var_screeninfo *, struct fb_info *);

static int ffb_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
static int ffb_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);

/*
 *  Frame buffer operations
 */

static const struct fb_ops ffb_ops = {
	.owner			= THIS_MODULE,
	__FB_DEFAULT_SBUS_OPS_RDWR(ffb),
	.fb_setcolreg		= ffb_setcolreg,
	.fb_blank		= ffb_blank,
	.fb_pan_display		= ffb_pan_display,
	.fb_fillrect		= ffb_fillrect,
	.fb_copyarea		= ffb_copyarea,
	.fb_imageblit		= ffb_imageblit,
	.fb_sync		= ffb_sync,
	__FB_DEFAULT_SBUS_OPS_IOCTL(ffb),
	__FB_DEFAULT_SBUS_OPS_MMAP(ffb),
};

/* Register layout and definitions */
#define	FFB_SFB8R_VOFF		0x00000000
#define	FFB_SFB8G_VOFF		0x00400000
#define	FFB_SFB8B_VOFF		0x00800000
#define	FFB_SFB8X_VOFF		0x00c00000
#define	FFB_SFB32_VOFF		0x01000000
#define	FFB_SFB64_VOFF		0x02000000
#define	FFB_FBC_REGS_VOFF	0x04000000
#define	FFB_BM_FBC_REGS_VOFF	0x04002000
#define	FFB_DFB8R_VOFF		0x04004000
#define	FFB_DFB8G_VOFF		0x04404000
#define	FFB_DFB8B_VOFF		0x04804000
#define	FFB_DFB8X_VOFF		0x04c04000
#define	FFB_DFB24_VOFF		0x05004000
#define	FFB_DFB32_VOFF		0x06004000
#define	FFB_DFB422A_VOFF	0x07004000	/* DFB 422 mode write to A */
#define	FFB_DFB422AD_VOFF	0x07804000	/* DFB 422 mode with line doubling */
#define	FFB_DFB24B_VOFF		0x08004000	/* DFB 24bit mode write to B */
#define	FFB_DFB422B_VOFF	0x09004000	/* DFB 422 mode write to B */
#define	FFB_DFB422BD_VOFF	0x09804000	/* DFB 422 mode with line doubling */
#define	FFB_SFB16Z_VOFF		0x0a004000	/* 16bit mode Z planes */
#define	FFB_SFB8Z_VOFF		0x0a404000	/* 8bit mode Z planes */
#define	FFB_SFB422_VOFF		0x0ac04000	/* SFB 422 mode write to A/B */
#define	FFB_SFB422D_VOFF	0x0b404000	/* SFB 422 mode with line doubling */
#define	FFB_FBC_KREGS_VOFF	0x0bc04000
#define	FFB_DAC_VOFF		0x0bc06000
#define	FFB_PROM_VOFF		0x0bc08000
#define	FFB_EXP_VOFF		0x0bc18000

#define	FFB_SFB8R_POFF		0x04000000UL
#define	FFB_SFB8G_POFF		0x04400000UL
#define	FFB_SFB8B_POFF		0x04800000UL
#define	FFB_SFB8X_POFF		0x04c00000UL
#define	FFB_SFB32_POFF		0x05000000UL
#define	FFB_SFB64_POFF		0x06000000UL
#define	FFB_FBC_REGS_POFF	0x00600000UL
#define	FFB_BM_FBC_REGS_POFF	0x00600000UL
#define	FFB_DFB8R_POFF		0x01000000UL
#define	FFB_DFB8G_POFF		0x01400000UL
#define	FFB_DFB8B_POFF		0x01800000UL
#define	FFB_DFB8X_POFF		0x01c00000UL
#define	FFB_DFB24_POFF		0x02000000UL
#define	FFB_DFB32_POFF		0x03000000UL
#define	FFB_FBC_KREGS_POFF	0x00610000UL
#define	FFB_DAC_POFF		0x00400000UL
#define	FFB_PROM_POFF		0x00000000UL
#define	FFB_EXP_POFF		0x00200000UL
#define FFB_DFB422A_POFF	0x09000000UL
#define FFB_DFB422AD_POFF	0x09800000UL
#define FFB_DFB24B_POFF		0x0a000000UL
#define FFB_DFB422B_POFF	0x0b000000UL
#define FFB_DFB422BD_POFF	0x0b800000UL
#define FFB_SFB16Z_POFF		0x0c800000UL
#define FFB_SFB8Z_POFF		0x0c000000UL
#define FFB_SFB422_POFF		0x0d000000UL
#define FFB_SFB422D_POFF	0x0d800000UL

/* Draw operations */
#define FFB_DRAWOP_DOT		0x00
#define FFB_DRAWOP_AADOT	0x01
#define FFB_DRAWOP_BRLINECAP	0x02
#define FFB_DRAWOP_BRLINEOPEN	0x03
#define FFB_DRAWOP_DDLINE	0x04
#define FFB_DRAWOP_AALINE	0x05
#define FFB_DRAWOP_TRIANGLE	0x06
#define FFB_DRAWOP_POLYGON	0x07
#define FFB_DRAWOP_RECTANGLE	0x08
#define FFB_DRAWOP_FASTFILL	0x09
#define FFB_DRAWOP_BCOPY	0x0a
#define FFB_DRAWOP_VSCROLL	0x0b

/* Pixel processor control */
/* Force WID */
#define FFB_PPC_FW_DISABLE	0x800000
#define FFB_PPC_FW_ENABLE	0xc00000
/* Auxiliary clip */
#define FFB_PPC_ACE_DISABLE	0x040000
#define FFB_PPC_ACE_AUX_SUB	0x080000
#define FFB_PPC_ACE_AUX_ADD	0x0c0000
/* Depth cue */
#define FFB_PPC_DCE_DISABLE	0x020000
#define FFB_PPC_DCE_ENABLE	0x030000
/* Alpha blend */
#define FFB_PPC_ABE_DISABLE	0x008000
#define FFB_PPC_ABE_ENABLE	0x00c000
/* View clip */
#define FFB_PPC_VCE_DISABLE	0x001000
#define FFB_PPC_VCE_2D		0x002000
#define FFB_PPC_VCE_3D		0x003000
/* Area pattern */
#define FFB_PPC_APE_DISABLE	0x000800
#define FFB_PPC_APE_ENABLE	0x000c00
/* Transparent background */
#define FFB_PPC_TBE_OPAQUE	0x000200
#define FFB_PPC_TBE_TRANSPARENT	0x000300
/* Z source */
#define FFB_PPC_ZS_VAR		0x000080
#define FFB_PPC_ZS_CONST	0x0000c0
/* Y source */
#define FFB_PPC_YS_VAR		0x000020
#define FFB_PPC_YS_CONST	0x000030
/* X source */
#define FFB_PPC_XS_WID		0x000004
#define FFB_PPC_XS_VAR		0x000008
#define FFB_PPC_XS_CONST	0x00000c
/* Color (BGR) source */
#define FFB_PPC_CS_VAR		0x000002
#define FFB_PPC_CS_CONST	0x000003

#define FFB_ROP_NEW		0x83
#define FFB_ROP_OLD		0x85
#define FFB_ROP_NEW_XOR_OLD	0x86

#define FFB_UCSR_FIFO_MASK	0x00000fff
#define FFB_UCSR_FB_BUSY	0x01000000
#define FFB_UCSR_RP_BUSY	0x02000000
#define FFB_UCSR_ALL_BUSY	(FFB_UCSR_RP_BUSY|FFB_UCSR_FB_BUSY)
#define FFB_UCSR_READ_ERR	0x40000000
#define FFB_UCSR_FIFO_OVFL	0x80000000
#define FFB_UCSR_ALL_ERRORS	(FFB_UCSR_READ_ERR|FFB_UCSR_FIFO_OVFL)

struct ffb_fbc {
	/* Next vertex registers */
	u32	xxx1[3];
	u32	alpha;
	u32	red;
	u32	green;
	u32	blue;
	u32	depth;
	u32	y;
	u32	x;
	u32	xxx2[2];
	u32	ryf;
	u32	rxf;
	u32	xxx3[2];

	u32	dmyf;
	u32	dmxf;
	u32	xxx4[2];
	u32	ebyi;
	u32	ebxi;
	u32	xxx5[2];
	u32	by;
	u32	bx;
	u32	dy;
	u32	dx;
	u32	bh;
	u32	bw;
	u32	xxx6[2];

	u32	xxx7[32];

	/* Setup unit vertex state register */
	u32	suvtx;
	u32	xxx8[63];

	/* Control registers */
	u32	ppc;
	u32	wid;
	u32	fg;
	u32	bg;
	u32	consty;
	u32	constz;
	u32	xclip;
	u32	dcss;
	u32	vclipmin;
	u32	vclipmax;
	u32	vclipzmin;
	u32	vclipzmax;
	u32	dcsf;
	u32	dcsb;
	u32	dczf;
	u32	dczb;

	u32	xxx9;
	u32	blendc;
	u32	blendc1;
	u32	blendc2;
	u32	fbramitc;
	u32	fbc;
	u32	rop;
	u32	cmp;
	u32	matchab;
	u32	matchc;
	u32	magnab;
	u32	magnc;
	u32	fbcfg0;
	u32	fbcfg1;
	u32	fbcfg2;
	u32	fbcfg3;

	u32	ppcfg;
	u32	pick;
	u32	fillmode;
	u32	fbramwac;
	u32	pmask;
	u32	xpmask;
	u32	ypmask;
	u32	zpmask;
	u32	clip0min;
	u32	clip0max;
	u32	clip1min;
	u32	clip1max;
	u32	clip2min;
	u32	clip2max;
	u32	clip3min;
	u32	clip3max;

	/* New 3dRAM III support regs */
	u32	rawblend2;
	u32	rawpreblend;
	u32	rawstencil;
	u32	rawstencilctl;
	u32	threedram1;
	u32	threedram2;
	u32	passin;
	u32	rawclrdepth;
	u32	rawpmask;
	u32	rawcsrc;
	u32	rawmatch;
	u32	rawmagn;
	u32	rawropblend;
	u32	rawcmp;
	u32	rawwac;
	u32	fbramid;

	u32	drawop;
	u32	xxx10[2];
	u32	fontlpat;
	u32	xxx11;
	u32	fontxy;
	u32	fontw;
	u32	fontinc;
	u32	font;
	u32	xxx12[3];
	u32	blend2;
	u32	preblend;
	u32	stencil;
	u32	stencilctl;

	u32	xxx13[4];
	u32	dcss1;
	u32	dcss2;
	u32	dcss3;
	u32	widpmask;
	u32	dcs2;
	u32	dcs3;
	u32	dcs4;
	u32	xxx14;
	u32	dcd2;
	u32	dcd3;
	u32	dcd4;
	u32	xxx15;

	u32	pattern[32];

	u32	xxx16[256];

	u32	devid;
	u32	xxx17[63];

	u32	ucsr;
	u32	xxx18[31];

	u32	mer;
};

struct ffb_dac {
	u32	type;
	u32	value;
	u32	type2;
	u32	value2;
};

#define FFB_DAC_UCTRL		0x1001 /* User Control */
#define FFB_DAC_UCTRL_MANREV	0x00000f00 /* 4-bit Manufacturing Revision */
#define FFB_DAC_UCTRL_MANREV_SHIFT 8
#define FFB_DAC_TGEN		0x6000 /* Timing Generator */
#define FFB_DAC_TGEN_VIDE	0x00000001 /* Video Enable */
#define FFB_DAC_DID		0x8000 /* Device Identification */
#define FFB_DAC_DID_PNUM	0x0ffff000 /* Device Part Number */
#define FFB_DAC_DID_PNUM_SHIFT	12
#define FFB_DAC_DID_REV		0xf0000000 /* Device Revision */
#define FFB_DAC_DID_REV_SHIFT	28

#define FFB_DAC_CUR_CTRL	0x100
#define FFB_DAC_CUR_CTRL_P0	0x00000001
#define FFB_DAC_CUR_CTRL_P1	0x00000002

struct ffb_par {
	spinlock_t		lock;
	struct ffb_fbc __iomem	*fbc;
	struct ffb_dac __iomem	*dac;

	u32			flags;
#define FFB_FLAG_AFB		0x00000001 /* AFB m3 or m6 */
#define FFB_FLAG_BLANKED	0x00000002 /* screen is blanked */
#define FFB_FLAG_INVCURSOR	0x00000004 /* DAC has inverted cursor logic */

	u32			fg_cache __attribute__((aligned (8)));
	u32			bg_cache;
	u32			rop_cache;

	int			fifo_cache;

	unsigned long		physbase;
	unsigned long		fbsize;

	int			board_type;

	u32			pseudo_palette[16];
};

static void FFBFifo(struct ffb_par *par, int n)
{
	struct ffb_fbc __iomem *fbc;
	int cache = par->fifo_cache;

	if (cache - n < 0) {
		fbc = par->fbc;
		do {
			cache = (upa_readl(&fbc->ucsr) & FFB_UCSR_FIFO_MASK);
			cache -= 8;
		} while (cache - n < 0);
	}
	par->fifo_cache = cache - n;
}

static void FFBWait(struct ffb_par *par)
{
	struct ffb_fbc __iomem *fbc;
	int limit = 10000;

	fbc = par->fbc;
	do {
		if ((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_BUSY) == 0)
			break;
		if ((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_ERRORS) != 0) {
			upa_writel(FFB_UCSR_ALL_ERRORS, &fbc->ucsr);
		}
		udelay(10);
	} while (--limit > 0);
}

static int ffb_sync(struct fb_info *p)
{
	struct ffb_par *par = (struct ffb_par *)p->par;

	FFBWait(par);
	return 0;
}

static __inline__ void ffb_rop(struct ffb_par *par, u32 rop)
{
	if (par->rop_cache != rop) {
		FFBFifo(par, 1);
		upa_writel(rop, &par->fbc->rop);
		par->rop_cache = rop;
	}
}

static void ffb_switch_from_graph(struct ffb_par *par)
{
	struct ffb_fbc __iomem *fbc = par->fbc;
	struct ffb_dac __iomem *dac = par->dac;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);
	FFBWait(par);
	par->fifo_cache = 0;
	FFBFifo(par, 7);
	upa_writel(FFB_PPC_VCE_DISABLE | FFB_PPC_TBE_OPAQUE |
		   FFB_PPC_APE_DISABLE | FFB_PPC_CS_CONST,
		   &fbc->ppc);
	upa_writel(0x2000707f, &fbc->fbc);
	upa_writel(par->rop_cache, &fbc->rop);
	upa_writel(0xffffffff, &fbc->pmask);
	upa_writel((1 << 16) | (0 << 0), &fbc->fontinc);
	upa_writel(par->fg_cache, &fbc->fg);
	upa_writel(par->bg_cache, &fbc->bg);
	FFBWait(par);

	/* Disable cursor.  */
	upa_writel(FFB_DAC_CUR_CTRL, &dac->type2);
	if (par->flags & FFB_FLAG_INVCURSOR)
		upa_writel(0, &dac->value2);
	else
		upa_writel((FFB_DAC_CUR_CTRL_P0 |
			    FFB_DAC_CUR_CTRL_P1), &dac->value2);

	spin_unlock_irqrestore(&par->lock, flags);
}

static int ffb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ffb_par *par = (struct ffb_par *)info->par;

	/* We just use this to catch switches out of
	 * graphics mode.
	 */
	ffb_switch_from_graph(par);

	if (var->xoffset || var->yoffset || var->vmode)
		return -EINVAL;
	return 0;
}

/**
 *	ffb_fillrect - Draws a rectangle on the screen.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@rect: structure defining the rectagle and operation.
 */
static void ffb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct ffb_par *par = (struct ffb_par *)info->par;
	struct ffb_fbc __iomem *fbc = par->fbc;
	unsigned long flags;
	u32 fg;

	BUG_ON(rect->rop != ROP_COPY && rect->rop != ROP_XOR);

	fg = ((u32 *)info->pseudo_palette)[rect->color];

	spin_lock_irqsave(&par->lock, flags);

	if (fg != par->fg_cache) {
		FFBFifo(par, 1);
		upa_writel(fg, &fbc->fg);
		par->fg_cache = fg;
	}

	ffb_rop(par, rect->rop == ROP_COPY ?
		     FFB_ROP_NEW :
		     FFB_ROP_NEW_XOR_OLD);

	FFBFifo(par, 5);
	upa_writel(FFB_DRAWOP_RECTANGLE, &fbc->drawop);
	upa_writel(rect->dy, &fbc->by);
	upa_writel(rect->dx, &fbc->bx);
	upa_writel(rect->height, &fbc->bh);
	upa_writel(rect->width, &fbc->bw);

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *	ffb_copyarea - Copies on area of the screen to another area.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@area: structure defining the source and destination.
 */

static void ffb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct ffb_par *par = (struct ffb_par *)info->par;
	struct ffb_fbc __iomem *fbc = par->fbc;
	unsigned long flags;

	if (area->dx != area->sx ||
	    area->dy == area->sy) {
		cfb_copyarea(info, area);
		return;
	}

	spin_lock_irqsave(&par->lock, flags);

	ffb_rop(par, FFB_ROP_OLD);

	FFBFifo(par, 7);
	upa_writel(FFB_DRAWOP_VSCROLL, &fbc->drawop);
	upa_writel(area->sy, &fbc->by);
	upa_writel(area->sx, &fbc->bx);
	upa_writel(area->dy, &fbc->dy);
	upa_writel(area->dx, &fbc->dx);
	upa_writel(area->height, &fbc->bh);
	upa_writel(area->width, &fbc->bw);

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *	ffb_imageblit - Copies a image from system memory to the screen.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@image: structure defining the image.
 */
static void ffb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct ffb_par *par = (struct ffb_par *)info->par;
	struct ffb_fbc __iomem *fbc = par->fbc;
	const u8 *data = image->data;
	unsigned long flags;
	u32 fg, bg, xy;
	u64 fgbg;
	int i, width, stride;

	if (image->depth > 1) {
		cfb_imageblit(info, image);
		return;
	}

	fg = ((u32 *)info->pseudo_palette)[image->fg_color];
	bg = ((u32 *)info->pseudo_palette)[image->bg_color];
	fgbg = ((u64) fg << 32) | (u64) bg;
	xy = (image->dy << 16) | image->dx;
	width = image->width;
	stride = ((width + 7) >> 3);

	spin_lock_irqsave(&par->lock, flags);

	if (fgbg != *(u64 *)&par->fg_cache) {
		FFBFifo(par, 2);
		upa_writeq(fgbg, &fbc->fg);
		*(u64 *)&par->fg_cache = fgbg;
	}

	if (width >= 32) {
		FFBFifo(par, 1);
		upa_writel(32, &fbc->fontw);
	}

	while (width >= 32) {
		const u8 *next_data = data + 4;

		FFBFifo(par, 1);
		upa_writel(xy, &fbc->fontxy);
		xy += (32 << 0);

		for (i = 0; i < image->height; i++) {
			u32 val = (((u32)data[0] << 24) |
				   ((u32)data[1] << 16) |
				   ((u32)data[2] <<  8) |
				   ((u32)data[3] <<  0));
			FFBFifo(par, 1);
			upa_writel(val, &fbc->font);

			data += stride;
		}

		data = next_data;
		width -= 32;
	}

	if (width) {
		FFBFifo(par, 2);
		upa_writel(width, &fbc->fontw);
		upa_writel(xy, &fbc->fontxy);

		for (i = 0; i < image->height; i++) {
			u32 val = (((u32)data[0] << 24) |
				   ((u32)data[1] << 16) |
				   ((u32)data[2] <<  8) |
				   ((u32)data[3] <<  0));
			FFBFifo(par, 1);
			upa_writel(val, &fbc->font);

			data += stride;
		}
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

static void ffb_fixup_var_rgb(struct fb_var_screeninfo *var)
{
	var->red.offset = 0;
	var->red.length = 8;
	var->green.offset = 8;
	var->green.length = 8;
	var->blue.offset = 16;
	var->blue.length = 8;
	var->transp.offset = 0;
	var->transp.length = 0;
}

/**
 *	ffb_setcolreg - Sets a color register.
 *
 *	@regno: boolean, 0 copy local, 1 get_user() function
 *	@red: frame buffer colormap structure
 *	@green: The green value which can be up to 16 bits wide
 *	@blue:  The blue value which can be up to 16 bits wide.
 *	@transp: If supported the alpha value which can be up to 16 bits wide.
 *	@info: frame buffer info structure
 */
static int ffb_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	u32 value;

	if (regno >= 16)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	value = (blue << 16) | (green << 8) | red;
	((u32 *)info->pseudo_palette)[regno] = value;

	return 0;
}

/**
 *	ffb_blank - Optional function.  Blanks the display.
 *	@blank: the blank mode we want.
 *	@info: frame buffer structure that represents a single frame buffer
 */
static int ffb_blank(int blank, struct fb_info *info)
{
	struct ffb_par *par = (struct ffb_par *)info->par;
	struct ffb_dac __iomem *dac = par->dac;
	unsigned long flags;
	u32 val;
	int i;

	spin_lock_irqsave(&par->lock, flags);

	FFBWait(par);

	upa_writel(FFB_DAC_TGEN, &dac->type);
	val = upa_readl(&dac->value);
	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val |= FFB_DAC_TGEN_VIDE;
		par->flags &= ~FFB_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val &= ~FFB_DAC_TGEN_VIDE;
		par->flags |= FFB_FLAG_BLANKED;
		break;
	}
	upa_writel(FFB_DAC_TGEN, &dac->type);
	upa_writel(val, &dac->value);
	for (i = 0; i < 10; i++) {
		upa_writel(FFB_DAC_TGEN, &dac->type);
		upa_readl(&dac->value);
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static const struct sbus_mmap_map ffb_mmap_map[] = {
	{
		.voff	= FFB_SFB8R_VOFF,
		.poff	= FFB_SFB8R_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_SFB8G_VOFF,
		.poff	= FFB_SFB8G_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_SFB8B_VOFF,
		.poff	= FFB_SFB8B_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_SFB8X_VOFF,
		.poff	= FFB_SFB8X_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_SFB32_VOFF,
		.poff	= FFB_SFB32_POFF,
		.size	= 0x1000000
	},
	{
		.voff	= FFB_SFB64_VOFF,
		.poff	= FFB_SFB64_POFF,
		.size	= 0x2000000
	},
	{
		.voff	= FFB_FBC_REGS_VOFF,
		.poff	= FFB_FBC_REGS_POFF,
		.size	= 0x0002000
	},
	{
		.voff	= FFB_BM_FBC_REGS_VOFF,
		.poff	= FFB_BM_FBC_REGS_POFF,
		.size	= 0x0002000
	},
	{
		.voff	= FFB_DFB8R_VOFF,
		.poff	= FFB_DFB8R_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_DFB8G_VOFF,
		.poff	= FFB_DFB8G_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_DFB8B_VOFF,
		.poff	= FFB_DFB8B_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_DFB8X_VOFF,
		.poff	= FFB_DFB8X_POFF,
		.size	= 0x0400000
	},
	{
		.voff	= FFB_DFB24_VOFF,
		.poff	= FFB_DFB24_POFF,
		.size	= 0x1000000
	},
	{
		.voff	= FFB_DFB32_VOFF,
		.poff	= FFB_DFB32_POFF,
		.size	= 0x1000000
	},
	{
		.voff	= FFB_FBC_KREGS_VOFF,
		.poff	= FFB_FBC_KREGS_POFF,
		.size	= 0x0002000
	},
	{
		.voff	= FFB_DAC_VOFF,
		.poff	= FFB_DAC_POFF,
		.size	= 0x0002000
	},
	{
		.voff	= FFB_PROM_VOFF,
		.poff	= FFB_PROM_POFF,
		.size	= 0x0010000
	},
	{
		.voff	= FFB_EXP_VOFF,
		.poff	= FFB_EXP_POFF,
		.size	= 0x0002000
	},
	{
		.voff	= FFB_DFB422A_VOFF,
		.poff	= FFB_DFB422A_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_DFB422AD_VOFF,
		.poff	= FFB_DFB422AD_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_DFB24B_VOFF,
		.poff	= FFB_DFB24B_POFF,
		.size	= 0x1000000
	},
	{
		.voff	= FFB_DFB422B_VOFF,
		.poff	= FFB_DFB422B_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_DFB422BD_VOFF,
		.poff	= FFB_DFB422BD_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_SFB16Z_VOFF,
		.poff	= FFB_SFB16Z_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_SFB8Z_VOFF,
		.poff	= FFB_SFB8Z_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_SFB422_VOFF,
		.poff	= FFB_SFB422_POFF,
		.size	= 0x0800000
	},
	{
		.voff	= FFB_SFB422D_VOFF,
		.poff	= FFB_SFB422D_POFF,
		.size	= 0x0800000
	},
	{ .size = 0 }
};

static int ffb_sbusfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct ffb_par *par = (struct ffb_par *)info->par;

	return sbusfb_mmap_helper(ffb_mmap_map,
				  par->physbase, par->fbsize,
				  0, vma);
}

static int ffb_sbusfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct ffb_par *par = (struct ffb_par *)info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_CREATOR, 24, par->fbsize);
}

/*
 *  Initialisation
 */

static void ffb_init_fix(struct fb_info *info)
{
	struct ffb_par *par = (struct ffb_par *)info->par;
	const char *ffb_type_name;

	if (!(par->flags & FFB_FLAG_AFB)) {
		if ((par->board_type & 0x7) == 0x3)
			ffb_type_name = "Creator 3D";
		else
			ffb_type_name = "Creator";
	} else
		ffb_type_name = "Elite 3D";

	strscpy(info->fix.id, ffb_type_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;

	/* Framebuffer length is the same regardless of resolution. */
	info->fix.line_length = 8192;

	info->fix.accel = FB_ACCEL_SUN_CREATOR;
}

static int ffb_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	struct ffb_fbc __iomem *fbc;
	struct ffb_dac __iomem *dac;
	struct fb_info *info;
	struct ffb_par *par;
	u32 dac_pnum, dac_rev, dac_mrev;
	int err;

	info = framebuffer_alloc(sizeof(struct ffb_par), &op->dev);

	err = -ENOMEM;
	if (!info)
		goto out_err;

	par = info->par;

	spin_lock_init(&par->lock);
	par->fbc = of_ioremap(&op->resource[2], 0,
			      sizeof(struct ffb_fbc), "ffb fbc");
	if (!par->fbc)
		goto out_release_fb;

	par->dac = of_ioremap(&op->resource[1], 0,
			      sizeof(struct ffb_dac), "ffb dac");
	if (!par->dac)
		goto out_unmap_fbc;

	par->rop_cache = FFB_ROP_NEW;
	par->physbase = op->resource[0].start;

	/* Don't mention copyarea, so SCROLL_REDRAW is always
	 * used.  It is the fastest on this chip.
	 */
	info->flags = (/* FBINFO_HWACCEL_COPYAREA | */
		       FBINFO_HWACCEL_FILLRECT |
		       FBINFO_HWACCEL_IMAGEBLIT);

	info->fbops = &ffb_ops;

	info->screen_base = (char *) par->physbase + FFB_DFB24_POFF;
	info->pseudo_palette = par->pseudo_palette;

	sbusfb_fill_var(&info->var, dp, 32);
	par->fbsize = PAGE_ALIGN(info->var.xres * info->var.yres * 4);
	ffb_fixup_var_rgb(&info->var);

	info->var.accel_flags = FB_ACCELF_TEXT;

	if (of_node_name_eq(dp, "SUNW,afb"))
		par->flags |= FFB_FLAG_AFB;

	par->board_type = of_getintprop_default(dp, "board_type", 0);

	fbc = par->fbc;
	if ((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_ERRORS) != 0)
		upa_writel(FFB_UCSR_ALL_ERRORS, &fbc->ucsr);

	dac = par->dac;
	upa_writel(FFB_DAC_DID, &dac->type);
	dac_pnum = upa_readl(&dac->value);
	dac_rev = (dac_pnum & FFB_DAC_DID_REV) >> FFB_DAC_DID_REV_SHIFT;
	dac_pnum = (dac_pnum & FFB_DAC_DID_PNUM) >> FFB_DAC_DID_PNUM_SHIFT;

	upa_writel(FFB_DAC_UCTRL, &dac->type);
	dac_mrev = upa_readl(&dac->value);
	dac_mrev = (dac_mrev & FFB_DAC_UCTRL_MANREV) >>
		FFB_DAC_UCTRL_MANREV_SHIFT;

	/* Elite3D has different DAC revision numbering, and no DAC revisions
	 * have the reversed meaning of cursor enable.  Otherwise, Pacifica 1
	 * ramdacs with manufacturing revision less than 3 have inverted
	 * cursor logic.  We identify Pacifica 1 as not Pacifica 2, the
	 * latter having a part number value of 0x236e.
	 */
	if ((par->flags & FFB_FLAG_AFB) || dac_pnum == 0x236e) {
		par->flags &= ~FFB_FLAG_INVCURSOR;
	} else {
		if (dac_mrev < 3)
			par->flags |= FFB_FLAG_INVCURSOR;
	}

	ffb_switch_from_graph(par);

	/* Unblank it just to be sure.  When there are multiple
	 * FFB/AFB cards in the system, or it is not the OBP
	 * chosen console, it will have video outputs off in
	 * the DAC.
	 */
	ffb_blank(FB_BLANK_UNBLANK, info);

	if (fb_alloc_cmap(&info->cmap, 256, 0))
		goto out_unmap_dac;

	ffb_init_fix(info);

	err = register_framebuffer(info);
	if (err < 0)
		goto out_dealloc_cmap;

	dev_set_drvdata(&op->dev, info);

	printk(KERN_INFO "%pOF: %s at %016lx, type %d, "
	       "DAC pnum[%x] rev[%d] manuf_rev[%d]\n",
	       dp,
	       ((par->flags & FFB_FLAG_AFB) ? "AFB" : "FFB"),
	       par->physbase, par->board_type,
	       dac_pnum, dac_rev, dac_mrev);

	return 0;

out_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);

out_unmap_dac:
	of_iounmap(&op->resource[1], par->dac, sizeof(struct ffb_dac));

out_unmap_fbc:
	of_iounmap(&op->resource[2], par->fbc, sizeof(struct ffb_fbc));

out_release_fb:
	framebuffer_release(info);

out_err:
	return err;
}

static void ffb_remove(struct platform_device *op)
{
	struct fb_info *info = dev_get_drvdata(&op->dev);
	struct ffb_par *par = info->par;

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);

	of_iounmap(&op->resource[2], par->fbc, sizeof(struct ffb_fbc));
	of_iounmap(&op->resource[1], par->dac, sizeof(struct ffb_dac));

	framebuffer_release(info);
}

static const struct of_device_id ffb_match[] = {
	{
		.name = "SUNW,ffb",
	},
	{
		.name = "SUNW,afb",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ffb_match);

static struct platform_driver ffb_driver = {
	.driver = {
		.name = "ffb",
		.of_match_table = ffb_match,
	},
	.probe		= ffb_probe,
	.remove		= ffb_remove,
};

static int __init ffb_init(void)
{
	if (fb_get_options("ffb", NULL))
		return -ENODEV;

	return platform_driver_register(&ffb_driver);
}

static void __exit ffb_exit(void)
{
	platform_driver_unregister(&ffb_driver);
}

module_init(ffb_init);
module_exit(ffb_exit);

MODULE_DESCRIPTION("framebuffer driver for Creator/Elite3D chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
