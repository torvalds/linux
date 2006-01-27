/* ffb.c: Creator/Elite3D frame buffer driver
 *
 * Copyright (C) 2003 David S. Miller (davem@redhat.com)
 * Copyright (C) 1997,1998,1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * Driver layout based loosely on tgafb.c, see that file for credits.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/timer.h>

#include <asm/io.h>
#include <asm/upa.h>
#include <asm/oplib.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int ffb_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int ffb_blank(int, struct fb_info *);
static void ffb_init_fix(struct fb_info *);

static void ffb_imageblit(struct fb_info *, const struct fb_image *);
static void ffb_fillrect(struct fb_info *, const struct fb_fillrect *);
static void ffb_copyarea(struct fb_info *, const struct fb_copyarea *);
static int ffb_sync(struct fb_info *);
static int ffb_mmap(struct fb_info *, struct vm_area_struct *);
static int ffb_ioctl(struct fb_info *, unsigned int, unsigned long);
static int ffb_pan_display(struct fb_var_screeninfo *, struct fb_info *);

/*
 *  Frame buffer operations
 */

static struct fb_ops ffb_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= ffb_setcolreg,
	.fb_blank		= ffb_blank,
	.fb_pan_display		= ffb_pan_display,
	.fb_fillrect		= ffb_fillrect,
	.fb_copyarea		= ffb_copyarea,
	.fb_imageblit		= ffb_imageblit,
	.fb_sync		= ffb_sync,
	.fb_mmap		= ffb_mmap,
	.fb_ioctl		= ffb_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
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

#define FFB_ROP_NEW                  0x83
#define FFB_ROP_OLD                  0x85
#define FFB_ROP_NEW_XOR_OLD          0x86

#define FFB_UCSR_FIFO_MASK     0x00000fff
#define FFB_UCSR_FB_BUSY       0x01000000
#define FFB_UCSR_RP_BUSY       0x02000000
#define FFB_UCSR_ALL_BUSY      (FFB_UCSR_RP_BUSY|FFB_UCSR_FB_BUSY)
#define FFB_UCSR_READ_ERR      0x40000000
#define FFB_UCSR_FIFO_OVFL     0x80000000
#define FFB_UCSR_ALL_ERRORS    (FFB_UCSR_READ_ERR|FFB_UCSR_FIFO_OVFL)

struct ffb_fbc {
	/* Next vertex registers */
	u32		xxx1[3];
	volatile u32	alpha;
	volatile u32	red;
	volatile u32	green;
	volatile u32	blue;
	volatile u32	depth;
	volatile u32	y;
	volatile u32	x;
	u32		xxx2[2];
	volatile u32	ryf;
	volatile u32	rxf;
	u32		xxx3[2];
	
	volatile u32	dmyf;
	volatile u32	dmxf;
	u32		xxx4[2];
	volatile u32	ebyi;
	volatile u32	ebxi;
	u32		xxx5[2];
	volatile u32	by;
	volatile u32	bx;
	u32		dy;
	u32		dx;
	volatile u32	bh;
	volatile u32	bw;
	u32		xxx6[2];
	
	u32		xxx7[32];
	
	/* Setup unit vertex state register */
	volatile u32	suvtx;
	u32		xxx8[63];
	
	/* Control registers */
	volatile u32	ppc;
	volatile u32	wid;
	volatile u32	fg;
	volatile u32	bg;
	volatile u32	consty;
	volatile u32	constz;
	volatile u32	xclip;
	volatile u32	dcss;
	volatile u32	vclipmin;
	volatile u32	vclipmax;
	volatile u32	vclipzmin;
	volatile u32	vclipzmax;
	volatile u32	dcsf;
	volatile u32	dcsb;
	volatile u32	dczf;
	volatile u32	dczb;
	
	u32		xxx9;
	volatile u32	blendc;
	volatile u32	blendc1;
	volatile u32	blendc2;
	volatile u32	fbramitc;
	volatile u32	fbc;
	volatile u32	rop;
	volatile u32	cmp;
	volatile u32	matchab;
	volatile u32	matchc;
	volatile u32	magnab;
	volatile u32	magnc;
	volatile u32	fbcfg0;
	volatile u32	fbcfg1;
	volatile u32	fbcfg2;
	volatile u32	fbcfg3;
	
	u32		ppcfg;
	volatile u32	pick;
	volatile u32	fillmode;
	volatile u32	fbramwac;
	volatile u32	pmask;
	volatile u32	xpmask;
	volatile u32	ypmask;
	volatile u32	zpmask;
	volatile u32	clip0min;
	volatile u32	clip0max;
	volatile u32	clip1min;
	volatile u32	clip1max;
	volatile u32	clip2min;
	volatile u32	clip2max;
	volatile u32	clip3min;
	volatile u32	clip3max;
	
	/* New 3dRAM III support regs */
	volatile u32	rawblend2;
	volatile u32	rawpreblend;
	volatile u32	rawstencil;
	volatile u32	rawstencilctl;
	volatile u32	threedram1;
	volatile u32	threedram2;
	volatile u32	passin;
	volatile u32	rawclrdepth;
	volatile u32	rawpmask;
	volatile u32	rawcsrc;
	volatile u32	rawmatch;
	volatile u32	rawmagn;
	volatile u32	rawropblend;
	volatile u32	rawcmp;
	volatile u32	rawwac;
	volatile u32	fbramid;
	
	volatile u32	drawop;
	u32		xxx10[2];
	volatile u32	fontlpat;
	u32		xxx11;
	volatile u32	fontxy;
	volatile u32	fontw;
	volatile u32	fontinc;
	volatile u32	font;
	u32		xxx12[3];
	volatile u32	blend2;
	volatile u32	preblend;
	volatile u32	stencil;
	volatile u32	stencilctl;

	u32		xxx13[4];	
	volatile u32	dcss1;
	volatile u32	dcss2;
	volatile u32	dcss3;
	volatile u32	widpmask;
	volatile u32	dcs2;
	volatile u32	dcs3;
	volatile u32	dcs4;
	u32		xxx14;
	volatile u32	dcd2;
	volatile u32	dcd3;
	volatile u32	dcd4;
	u32		xxx15;
	
	volatile u32	pattern[32];
	
	u32		xxx16[256];
	
	volatile u32	devid;
	u32		xxx17[63];
	
	volatile u32	ucsr;
	u32		xxx18[31];
	
	volatile u32	mer;
};

struct ffb_dac {
	volatile u32	type;
	volatile u32	value;
	volatile u32	type2;
	volatile u32	value2;
};

struct ffb_par {
	spinlock_t		lock;
	struct ffb_fbc		*fbc;
	struct ffb_dac		*dac;

	u32			flags;
#define FFB_FLAG_AFB		0x00000001
#define FFB_FLAG_BLANKED	0x00000002

	u32			fg_cache __attribute__((aligned (8)));
	u32			bg_cache;
	u32			rop_cache;

	int			fifo_cache;

	unsigned long		physbase;
	unsigned long		fbsize;

	char			name[64];
	int			prom_node;
	int			prom_parent_node;
	int			dac_rev;
	int			board_type;
};

static void FFBFifo(struct ffb_par *par, int n)
{
	struct ffb_fbc *fbc;
	int cache = par->fifo_cache;

	if (cache - n < 0) {
		fbc = par->fbc;
		do {	cache = (upa_readl(&fbc->ucsr) & FFB_UCSR_FIFO_MASK) - 8;
		} while (cache - n < 0);
	}
	par->fifo_cache = cache - n;
}

static void FFBWait(struct ffb_par *par)
{
	struct ffb_fbc *fbc;
	int limit = 10000;

	fbc = par->fbc;
	do {
		if ((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_BUSY) == 0)
			break;
		if ((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_ERRORS) != 0) {
			upa_writel(FFB_UCSR_ALL_ERRORS, &fbc->ucsr);
		}
		udelay(10);
	} while(--limit > 0);
}

static int ffb_sync(struct fb_info *p)
{
	struct ffb_par *par = (struct ffb_par *) p->par;

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
	struct ffb_fbc *fbc = par->fbc;
	struct ffb_dac *dac = par->dac;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);
	FFBWait(par);
	par->fifo_cache = 0;
	FFBFifo(par, 7);
	upa_writel(FFB_PPC_VCE_DISABLE|FFB_PPC_TBE_OPAQUE|
		   FFB_PPC_APE_DISABLE|FFB_PPC_CS_CONST,
		   &fbc->ppc);
	upa_writel(0x2000707f, &fbc->fbc);
	upa_writel(par->rop_cache, &fbc->rop);
	upa_writel(0xffffffff, &fbc->pmask);
	upa_writel((1 << 16) | (0 << 0), &fbc->fontinc);
	upa_writel(par->fg_cache, &fbc->fg);
	upa_writel(par->bg_cache, &fbc->bg);
	FFBWait(par);

	/* Disable cursor.  */
	upa_writel(0x100, &dac->type2);
	if (par->dac_rev <= 2)
		upa_writel(0, &dac->value2);
	else
		upa_writel(3, &dac->value2);

	spin_unlock_irqrestore(&par->lock, flags);
}

static int ffb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ffb_par *par = (struct ffb_par *) info->par;

	/* We just use this to catch switches out of
	 * graphics mode.
	 */
	ffb_switch_from_graph(par);

	if (var->xoffset || var->yoffset || var->vmode)
		return -EINVAL;
	return 0;
}

/**
 *      ffb_fillrect - REQUIRED function. Can use generic routines if 
 *                     non acclerated hardware and packed pixel based.
 *                     Draws a rectangle on the screen.               
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @rect: structure defining the rectagle and operation.
 */
static void ffb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct ffb_par *par = (struct ffb_par *) info->par;
	struct ffb_fbc *fbc = par->fbc;
	unsigned long flags;
	u32 fg;

	if (rect->rop != ROP_COPY && rect->rop != ROP_XOR)
		BUG();

	fg = ((u32 *)info->pseudo_palette)[rect->color];

	spin_lock_irqsave(&par->lock, flags);

	if (fg != par->fg_cache) {
		FFBFifo(par, 1);
		upa_writel(fg, &fbc->fg);
		par->fg_cache = fg;
	}

	ffb_rop(par, (rect->rop == ROP_COPY ?
		      FFB_ROP_NEW :
		      FFB_ROP_NEW_XOR_OLD));

	FFBFifo(par, 5);
	upa_writel(FFB_DRAWOP_RECTANGLE, &fbc->drawop);
	upa_writel(rect->dy, &fbc->by);
	upa_writel(rect->dx, &fbc->bx);
	upa_writel(rect->height, &fbc->bh);
	upa_writel(rect->width, &fbc->bw);

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *      ffb_copyarea - REQUIRED function. Can use generic routines if
 *                     non acclerated hardware and packed pixel based.
 *                     Copies on area of the screen to another area.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @area: structure defining the source and destination.
 */

static void
ffb_copyarea(struct fb_info *info, const struct fb_copyarea *area) 
{
	struct ffb_par *par = (struct ffb_par *) info->par;
	struct ffb_fbc *fbc = par->fbc;
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
 *      ffb_imageblit - REQUIRED function. Can use generic routines if
 *                      non acclerated hardware and packed pixel based.
 *                      Copies a image from system memory to the screen. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @image: structure defining the image.
 */
static void ffb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct ffb_par *par = (struct ffb_par *) info->par;
	struct ffb_fbc *fbc = par->fbc;
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
 *      ffb_setcolreg - Optional function. Sets a color register.
 *      @regno: boolean, 0 copy local, 1 get_user() function
 *      @red: frame buffer colormap structure
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 */
static int ffb_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	u32 value;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	value = (blue << 16) | (green << 8) | red;
	((u32 *)info->pseudo_palette)[regno] = value;

	return 0;
}

/**
 *      ffb_blank - Optional function.  Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 */
static int
ffb_blank(int blank, struct fb_info *info)
{
	struct ffb_par *par = (struct ffb_par *) info->par;
	struct ffb_dac *dac = par->dac;
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&par->lock, flags);

	FFBWait(par);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		upa_writel(0x6000, &dac->type);
		tmp = (upa_readl(&dac->value) | 0x1);
		upa_writel(0x6000, &dac->type);
		upa_writel(tmp, &dac->value);
		par->flags &= ~FFB_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		upa_writel(0x6000, &dac->type);
		tmp = (upa_readl(&dac->value) & ~0x1);
		upa_writel(0x6000, &dac->type);
		upa_writel(tmp, &dac->value);
		par->flags |= FFB_FLAG_BLANKED;
		break;
	}

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map ffb_mmap_map[] = {
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

static int ffb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct ffb_par *par = (struct ffb_par *)info->par;

	return sbusfb_mmap_helper(ffb_mmap_map,
				  par->physbase, par->fbsize,
				  0, vma);
}

static int ffb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct ffb_par *par = (struct ffb_par *) info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_CREATOR, 24, par->fbsize);
}

/*
 *  Initialisation
 */

static void
ffb_init_fix(struct fb_info *info)
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

	strlcpy(info->fix.id, ffb_type_name, sizeof(info->fix.id));

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;

	/* Framebuffer length is the same regardless of resolution. */
	info->fix.line_length = 8192;

	info->fix.accel = FB_ACCEL_SUN_CREATOR;
}

static int ffb_apply_upa_parent_ranges(int parent,
				       struct linux_prom64_registers *regs)
{
	struct linux_prom64_ranges ranges[PROMREG_MAX];
	char name[128];
	int len, i;

	prom_getproperty(parent, "name", name, sizeof(name));
	if (strcmp(name, "upa") != 0)
		return 0;

	len = prom_getproperty(parent, "ranges", (void *) ranges, sizeof(ranges));
	if (len <= 0)
		return 1;

	len /= sizeof(struct linux_prom64_ranges);
	for (i = 0; i < len; i++) {
		struct linux_prom64_ranges *rng = &ranges[i];
		u64 phys_addr = regs->phys_addr;

		if (phys_addr >= rng->ot_child_base &&
		    phys_addr < (rng->ot_child_base + rng->or_size)) {
			regs->phys_addr -= rng->ot_child_base;
			regs->phys_addr += rng->ot_parent_base;
			return 0;
		}
	}

	return 1;
}

struct all_info {
	struct fb_info info;
	struct ffb_par par;
	u32 pseudo_palette[256];
	struct list_head list;
};
static LIST_HEAD(ffb_list);

static void ffb_init_one(int node, int parent)
{
	struct linux_prom64_registers regs[2*PROMREG_MAX];
	struct ffb_fbc *fbc;
	struct ffb_dac *dac;
	struct all_info *all;

	if (prom_getproperty(node, "reg", (void *) regs, sizeof(regs)) <= 0) {
		printk("ffb: Cannot get reg device node property.\n");
		return;
	}

	if (ffb_apply_upa_parent_ranges(parent, &regs[0])) {
		printk("ffb: Cannot apply parent ranges to regs.\n");
		return;
	}

	all = kmalloc(sizeof(*all), GFP_KERNEL);
	if (!all) {
		printk(KERN_ERR "ffb: Cannot allocate memory.\n");
		return;
	}
	memset(all, 0, sizeof(*all));

	INIT_LIST_HEAD(&all->list);	

	spin_lock_init(&all->par.lock);
	all->par.fbc = (struct ffb_fbc *)(regs[0].phys_addr + FFB_FBC_REGS_POFF);
	all->par.dac = (struct ffb_dac *)(regs[0].phys_addr + FFB_DAC_POFF);
	all->par.rop_cache = FFB_ROP_NEW;
	all->par.physbase = regs[0].phys_addr;
	all->par.prom_node = node;
	all->par.prom_parent_node = parent;

	/* Don't mention copyarea, so SCROLL_REDRAW is always
	 * used.  It is the fastest on this chip.
	 */
	all->info.flags = (FBINFO_DEFAULT |
			   /* FBINFO_HWACCEL_COPYAREA | */
			   FBINFO_HWACCEL_FILLRECT |
			   FBINFO_HWACCEL_IMAGEBLIT);
	all->info.fbops = &ffb_ops;
	all->info.screen_base = (char *) all->par.physbase + FFB_DFB24_POFF;
	all->info.par = &all->par;
	all->info.pseudo_palette = all->pseudo_palette;

	sbusfb_fill_var(&all->info.var, all->par.prom_node, 32);
	all->par.fbsize = PAGE_ALIGN(all->info.var.xres *
				     all->info.var.yres *
				     4);
	ffb_fixup_var_rgb(&all->info.var);

	all->info.var.accel_flags = FB_ACCELF_TEXT;

	prom_getstring(node, "name", all->par.name, sizeof(all->par.name));
	if (!strcmp(all->par.name, "SUNW,afb"))
		all->par.flags |= FFB_FLAG_AFB;

	all->par.board_type = prom_getintdefault(node, "board_type", 0);

	fbc = all->par.fbc;
	if((upa_readl(&fbc->ucsr) & FFB_UCSR_ALL_ERRORS) != 0)
		upa_writel(FFB_UCSR_ALL_ERRORS, &fbc->ucsr);

	ffb_switch_from_graph(&all->par);

	dac = all->par.dac;
	upa_writel(0x8000, &dac->type);
	all->par.dac_rev = upa_readl(&dac->value) >> 0x1c;

	/* Elite3D has different DAC revision numbering, and no DAC revisions
	 * have the reversed meaning of cursor enable.
	 */
	if (all->par.flags & FFB_FLAG_AFB)
		all->par.dac_rev = 10;

	/* Unblank it just to be sure.  When there are multiple
	 * FFB/AFB cards in the system, or it is not the OBP
	 * chosen console, it will have video outputs off in
	 * the DAC.
	 */
	ffb_blank(0, &all->info);

	if (fb_alloc_cmap(&all->info.cmap, 256, 0)) {
		printk(KERN_ERR "ffb: Could not allocate color map.\n");
		kfree(all);
		return;
	}

	ffb_init_fix(&all->info);

	if (register_framebuffer(&all->info) < 0) {
		printk(KERN_ERR "ffb: Could not register framebuffer.\n");
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
		return;
	}

	list_add(&all->list, &ffb_list);

	printk("ffb: %s at %016lx type %d DAC %d\n",
	       ((all->par.flags & FFB_FLAG_AFB) ? "AFB" : "FFB"),
	       regs[0].phys_addr, all->par.board_type, all->par.dac_rev);
}

static void ffb_scan_siblings(int root)
{
	int node, child;

	child = prom_getchild(root);
	for (node = prom_searchsiblings(child, "SUNW,ffb"); node;
	     node = prom_searchsiblings(prom_getsibling(node), "SUNW,ffb"))
		ffb_init_one(node, root);
	for (node = prom_searchsiblings(child, "SUNW,afb"); node;
	     node = prom_searchsiblings(prom_getsibling(node), "SUNW,afb"))
		ffb_init_one(node, root);
}

int __init ffb_init(void)
{
	int root;

	if (fb_get_options("ffb", NULL))
		return -ENODEV;

	ffb_scan_siblings(prom_root_node);

	root = prom_getchild(prom_root_node);
	for (root = prom_searchsiblings(root, "upa"); root;
	     root = prom_searchsiblings(prom_getsibling(root), "upa"))
		ffb_scan_siblings(root);

	return 0;
}

void __exit ffb_exit(void)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &ffb_list) {
		struct all_info *all = list_entry(pos, typeof(*all), list);

		unregister_framebuffer(&all->info);
		fb_dealloc_cmap(&all->info.cmap);
		kfree(all);
	}
}

int __init
ffb_setup(char *arg)
{
	/* No cmdline options yet... */
	return 0;
}

module_init(ffb_init);

#ifdef MODULE
module_exit(ffb_exit);
#endif

MODULE_DESCRIPTION("framebuffer driver for Creator/Elite3D chipsets");
MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_LICENSE("GPL");
