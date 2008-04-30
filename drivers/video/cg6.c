/* cg6.c: CGSIX (GX, GXplus, TGX) frame buffer driver
 *
 * Copyright (C) 2003, 2006 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1998 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
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

#include <asm/io.h>
#include <asm/of_device.h>
#include <asm/fbio.h>

#include "sbuslib.h"

/*
 * Local functions.
 */

static int cg6_setcolreg(unsigned, unsigned, unsigned, unsigned,
			 unsigned, struct fb_info *);
static int cg6_blank(int, struct fb_info *);

static void cg6_imageblit(struct fb_info *, const struct fb_image *);
static void cg6_fillrect(struct fb_info *, const struct fb_fillrect *);
static int cg6_sync(struct fb_info *);
static int cg6_mmap(struct fb_info *, struct vm_area_struct *);
static int cg6_ioctl(struct fb_info *, unsigned int, unsigned long);
static void cg6_copyarea(struct fb_info *info, const struct fb_copyarea *area);

/*
 *  Frame buffer operations
 */

static struct fb_ops cg6_ops = {
	.owner			= THIS_MODULE,
	.fb_setcolreg		= cg6_setcolreg,
	.fb_blank		= cg6_blank,
	.fb_fillrect		= cg6_fillrect,
	.fb_copyarea		= cg6_copyarea,
	.fb_imageblit		= cg6_imageblit,
	.fb_sync		= cg6_sync,
	.fb_mmap		= cg6_mmap,
	.fb_ioctl		= cg6_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= sbusfb_compat_ioctl,
#endif
};

/* Offset of interesting structures in the OBIO space */
/*
 * Brooktree is the video dac and is funny to program on the cg6.
 * (it's even funnier on the cg3)
 * The FBC could be the frame buffer control
 * The FHC could is the frame buffer hardware control.
 */
#define CG6_ROM_OFFSET			0x0UL
#define CG6_BROOKTREE_OFFSET		0x200000UL
#define CG6_DHC_OFFSET			0x240000UL
#define CG6_ALT_OFFSET			0x280000UL
#define CG6_FHC_OFFSET			0x300000UL
#define CG6_THC_OFFSET			0x301000UL
#define CG6_FBC_OFFSET			0x700000UL
#define CG6_TEC_OFFSET			0x701000UL
#define CG6_RAM_OFFSET			0x800000UL

/* FHC definitions */
#define CG6_FHC_FBID_SHIFT		24
#define CG6_FHC_FBID_MASK		255
#define CG6_FHC_REV_SHIFT		20
#define CG6_FHC_REV_MASK		15
#define CG6_FHC_FROP_DISABLE		(1 << 19)
#define CG6_FHC_ROW_DISABLE		(1 << 18)
#define CG6_FHC_SRC_DISABLE		(1 << 17)
#define CG6_FHC_DST_DISABLE		(1 << 16)
#define CG6_FHC_RESET			(1 << 15)
#define CG6_FHC_LITTLE_ENDIAN		(1 << 13)
#define CG6_FHC_RES_MASK		(3 << 11)
#define CG6_FHC_1024			(0 << 11)
#define CG6_FHC_1152			(1 << 11)
#define CG6_FHC_1280			(2 << 11)
#define CG6_FHC_1600			(3 << 11)
#define CG6_FHC_CPU_MASK		(3 << 9)
#define CG6_FHC_CPU_SPARC		(0 << 9)
#define CG6_FHC_CPU_68020		(1 << 9)
#define CG6_FHC_CPU_386			(2 << 9)
#define CG6_FHC_TEST			(1 << 8)
#define CG6_FHC_TEST_X_SHIFT		4
#define CG6_FHC_TEST_X_MASK		15
#define CG6_FHC_TEST_Y_SHIFT		0
#define CG6_FHC_TEST_Y_MASK		15

/* FBC mode definitions */
#define CG6_FBC_BLIT_IGNORE		0x00000000
#define CG6_FBC_BLIT_NOSRC		0x00100000
#define CG6_FBC_BLIT_SRC		0x00200000
#define CG6_FBC_BLIT_ILLEGAL		0x00300000
#define CG6_FBC_BLIT_MASK		0x00300000

#define CG6_FBC_VBLANK			0x00080000

#define CG6_FBC_MODE_IGNORE		0x00000000
#define CG6_FBC_MODE_COLOR8		0x00020000
#define CG6_FBC_MODE_COLOR1		0x00040000
#define CG6_FBC_MODE_HRMONO		0x00060000
#define CG6_FBC_MODE_MASK		0x00060000

#define CG6_FBC_DRAW_IGNORE		0x00000000
#define CG6_FBC_DRAW_RENDER		0x00008000
#define CG6_FBC_DRAW_PICK		0x00010000
#define CG6_FBC_DRAW_ILLEGAL		0x00018000
#define CG6_FBC_DRAW_MASK		0x00018000

#define CG6_FBC_BWRITE0_IGNORE		0x00000000
#define CG6_FBC_BWRITE0_ENABLE		0x00002000
#define CG6_FBC_BWRITE0_DISABLE		0x00004000
#define CG6_FBC_BWRITE0_ILLEGAL		0x00006000
#define CG6_FBC_BWRITE0_MASK		0x00006000

#define CG6_FBC_BWRITE1_IGNORE		0x00000000
#define CG6_FBC_BWRITE1_ENABLE		0x00000800
#define CG6_FBC_BWRITE1_DISABLE		0x00001000
#define CG6_FBC_BWRITE1_ILLEGAL		0x00001800
#define CG6_FBC_BWRITE1_MASK		0x00001800

#define CG6_FBC_BREAD_IGNORE		0x00000000
#define CG6_FBC_BREAD_0			0x00000200
#define CG6_FBC_BREAD_1			0x00000400
#define CG6_FBC_BREAD_ILLEGAL		0x00000600
#define CG6_FBC_BREAD_MASK		0x00000600

#define CG6_FBC_BDISP_IGNORE		0x00000000
#define CG6_FBC_BDISP_0			0x00000080
#define CG6_FBC_BDISP_1			0x00000100
#define CG6_FBC_BDISP_ILLEGAL		0x00000180
#define CG6_FBC_BDISP_MASK		0x00000180

#define CG6_FBC_INDEX_MOD		0x00000040
#define CG6_FBC_INDEX_MASK		0x00000030

/* THC definitions */
#define CG6_THC_MISC_REV_SHIFT		16
#define CG6_THC_MISC_REV_MASK		15
#define CG6_THC_MISC_RESET		(1 << 12)
#define CG6_THC_MISC_VIDEO		(1 << 10)
#define CG6_THC_MISC_SYNC		(1 << 9)
#define CG6_THC_MISC_VSYNC		(1 << 8)
#define CG6_THC_MISC_SYNC_ENAB		(1 << 7)
#define CG6_THC_MISC_CURS_RES		(1 << 6)
#define CG6_THC_MISC_INT_ENAB		(1 << 5)
#define CG6_THC_MISC_INT		(1 << 4)
#define CG6_THC_MISC_INIT		0x9f

/* The contents are unknown */
struct cg6_tec {
	int tec_matrix;
	int tec_clip;
	int tec_vdc;
};

struct cg6_thc {
	u32	thc_pad0[512];
	u32	thc_hs;		/* hsync timing */
	u32	thc_hsdvs;
	u32	thc_hd;
	u32	thc_vs;		/* vsync timing */
	u32	thc_vd;
	u32	thc_refresh;
	u32	thc_misc;
	u32	thc_pad1[56];
	u32	thc_cursxy;	/* cursor x,y position (16 bits each) */
	u32	thc_cursmask[32];	/* cursor mask bits */
	u32	thc_cursbits[32];	/* what to show where mask enabled */
};

struct cg6_fbc {
	u32	xxx0[1];
	u32	mode;
	u32	clip;
	u32	xxx1[1];
	u32	s;
	u32	draw;
	u32	blit;
	u32	font;
	u32	xxx2[24];
	u32	x0, y0, z0, color0;
	u32	x1, y1, z1, color1;
	u32	x2, y2, z2, color2;
	u32	x3, y3, z3, color3;
	u32	offx, offy;
	u32	xxx3[2];
	u32	incx, incy;
	u32	xxx4[2];
	u32	clipminx, clipminy;
	u32	xxx5[2];
	u32	clipmaxx, clipmaxy;
	u32	xxx6[2];
	u32	fg;
	u32	bg;
	u32	alu;
	u32	pm;
	u32	pixelm;
	u32	xxx7[2];
	u32	patalign;
	u32	pattern[8];
	u32	xxx8[432];
	u32	apointx, apointy, apointz;
	u32	xxx9[1];
	u32	rpointx, rpointy, rpointz;
	u32	xxx10[5];
	u32	pointr, pointg, pointb, pointa;
	u32	alinex, aliney, alinez;
	u32	xxx11[1];
	u32	rlinex, rliney, rlinez;
	u32	xxx12[5];
	u32	liner, lineg, lineb, linea;
	u32	atrix, atriy, atriz;
	u32	xxx13[1];
	u32	rtrix, rtriy, rtriz;
	u32	xxx14[5];
	u32	trir, trig, trib, tria;
	u32	aquadx, aquady, aquadz;
	u32	xxx15[1];
	u32	rquadx, rquady, rquadz;
	u32	xxx16[5];
	u32	quadr, quadg, quadb, quada;
	u32	arectx, arecty, arectz;
	u32	xxx17[1];
	u32	rrectx, rrecty, rrectz;
	u32	xxx18[5];
	u32	rectr, rectg, rectb, recta;
};

struct bt_regs {
	u32	addr;
	u32	color_map;
	u32	control;
	u32	cursor;
};

struct cg6_par {
	spinlock_t		lock;
	struct bt_regs		__iomem *bt;
	struct cg6_fbc		__iomem *fbc;
	struct cg6_thc		__iomem *thc;
	struct cg6_tec		__iomem *tec;
	u32			__iomem *fhc;

	u32			flags;
#define CG6_FLAG_BLANKED	0x00000001

	unsigned long		physbase;
	unsigned long		which_io;
	unsigned long		fbsize;
};

static int cg6_sync(struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	int limit = 10000;

	do {
		if (!(sbus_readl(&fbc->s) & 0x10000000))
			break;
		udelay(10);
	} while (--limit > 0);

	return 0;
}

/**
 *	cg6_fillrect -	Draws a rectangle on the screen.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@rect: structure defining the rectagle and operation.
 */
static void cg6_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	unsigned long flags;
	s32 val;

	/* CG6 doesn't handle ROP_XOR */

	spin_lock_irqsave(&par->lock, flags);

	cg6_sync(info);

	sbus_writel(rect->color, &fbc->fg);
	sbus_writel(~(u32)0, &fbc->pixelm);
	sbus_writel(0xea80ff00, &fbc->alu);
	sbus_writel(0, &fbc->s);
	sbus_writel(0, &fbc->clip);
	sbus_writel(~(u32)0, &fbc->pm);
	sbus_writel(rect->dy, &fbc->arecty);
	sbus_writel(rect->dx, &fbc->arectx);
	sbus_writel(rect->dy + rect->height, &fbc->arecty);
	sbus_writel(rect->dx + rect->width, &fbc->arectx);
	do {
		val = sbus_readl(&fbc->draw);
	} while (val < 0 && (val & 0x20000000));
	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *	cg6_copyarea - Copies one area of the screen to another area.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@area: Structure providing the data to copy the framebuffer contents
 *		from one region to another.
 *
 *	This drawing operation copies a rectangular area from one area of the
 *	screen to another area.
 */
static void cg6_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&par->lock, flags);

	cg6_sync(info);

	sbus_writel(0xff, &fbc->fg);
	sbus_writel(0x00, &fbc->bg);
	sbus_writel(~0, &fbc->pixelm);
	sbus_writel(0xe880cccc, &fbc->alu);
	sbus_writel(0, &fbc->s);
	sbus_writel(0, &fbc->clip);

	sbus_writel(area->sy, &fbc->y0);
	sbus_writel(area->sx, &fbc->x0);
	sbus_writel(area->sy + area->height - 1, &fbc->y1);
	sbus_writel(area->sx + area->width - 1, &fbc->x1);
	sbus_writel(area->dy, &fbc->y2);
	sbus_writel(area->dx, &fbc->x2);
	sbus_writel(area->dy + area->height - 1, &fbc->y3);
	sbus_writel(area->dx + area->width - 1, &fbc->x3);
	do {
		i = sbus_readl(&fbc->blit);
	} while (i < 0 && (i & 0x20000000));
	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *	cg6_imageblit -	Copies a image from system memory to the screen.
 *
 *	@info: frame buffer structure that represents a single frame buffer
 *	@image: structure defining the image.
 */
static void cg6_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_fbc __iomem *fbc = par->fbc;
	const u8 *data = image->data;
	unsigned long flags;
	u32 x, y;
	int i, width;

	if (image->depth > 1) {
		cfb_imageblit(info, image);
		return;
	}

	spin_lock_irqsave(&par->lock, flags);

	cg6_sync(info);

	sbus_writel(image->fg_color, &fbc->fg);
	sbus_writel(image->bg_color, &fbc->bg);
	sbus_writel(0x140000, &fbc->mode);
	sbus_writel(0xe880fc30, &fbc->alu);
	sbus_writel(~(u32)0, &fbc->pixelm);
	sbus_writel(0, &fbc->s);
	sbus_writel(0, &fbc->clip);
	sbus_writel(0xff, &fbc->pm);
	sbus_writel(32, &fbc->incx);
	sbus_writel(0, &fbc->incy);

	x = image->dx;
	y = image->dy;
	for (i = 0; i < image->height; i++) {
		width = image->width;

		while (width >= 32) {
			u32 val;

			sbus_writel(y, &fbc->y0);
			sbus_writel(x, &fbc->x0);
			sbus_writel(x + 32 - 1, &fbc->x1);

			val = ((u32)data[0] << 24) |
			      ((u32)data[1] << 16) |
			      ((u32)data[2] <<  8) |
			      ((u32)data[3] <<  0);
			sbus_writel(val, &fbc->font);

			data += 4;
			x += 32;
			width -= 32;
		}
		if (width) {
			u32 val;

			sbus_writel(y, &fbc->y0);
			sbus_writel(x, &fbc->x0);
			sbus_writel(x + width - 1, &fbc->x1);
			if (width <= 8) {
				val = (u32) data[0] << 24;
				data += 1;
			} else if (width <= 16) {
				val = ((u32) data[0] << 24) |
				      ((u32) data[1] << 16);
				data += 2;
			} else {
				val = ((u32) data[0] << 24) |
				      ((u32) data[1] << 16) |
				      ((u32) data[2] <<  8);
				data += 3;
			}
			sbus_writel(val, &fbc->font);
		}

		y += 1;
		x = image->dx;
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 *	cg6_setcolreg - Sets a color register.
 *
 *	@regno: boolean, 0 copy local, 1 get_user() function
 *	@red: frame buffer colormap structure
 *	@green: The green value which can be up to 16 bits wide
 *	@blue:  The blue value which can be up to 16 bits wide.
 *	@transp: If supported the alpha value which can be up to 16 bits wide.
 *	@info: frame buffer info structure
 */
static int cg6_setcolreg(unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp, struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct bt_regs __iomem *bt = par->bt;
	unsigned long flags;

	if (regno >= 256)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	spin_lock_irqsave(&par->lock, flags);

	sbus_writel((u32)regno << 24, &bt->addr);
	sbus_writel((u32)red << 24, &bt->color_map);
	sbus_writel((u32)green << 24, &bt->color_map);
	sbus_writel((u32)blue << 24, &bt->color_map);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 *	cg6_blank - Blanks the display.
 *
 *	@blank_mode: the blank mode we want.
 *	@info: frame buffer structure that represents a single frame buffer
 */
static int cg6_blank(int blank, struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_thc __iomem *thc = par->thc;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&par->lock, flags);
	val = sbus_readl(&thc->thc_misc);

	switch (blank) {
	case FB_BLANK_UNBLANK: /* Unblanking */
		val |= CG6_THC_MISC_VIDEO;
		par->flags &= ~CG6_FLAG_BLANKED;
		break;

	case FB_BLANK_NORMAL: /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND: /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND: /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN: /* Poweroff */
		val &= ~CG6_THC_MISC_VIDEO;
		par->flags |= CG6_FLAG_BLANKED;
		break;
	}

	sbus_writel(val, &thc->thc_misc);
	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static struct sbus_mmap_map cg6_mmap_map[] = {
	{
		.voff	= CG6_FBC,
		.poff	= CG6_FBC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_TEC,
		.poff	= CG6_TEC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_BTREGS,
		.poff	= CG6_BROOKTREE_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_FHC,
		.poff	= CG6_FHC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_THC,
		.poff	= CG6_THC_OFFSET,
		.size	= PAGE_SIZE
	},
	{
		.voff	= CG6_ROM,
		.poff	= CG6_ROM_OFFSET,
		.size	= 0x10000
	},
	{
		.voff	= CG6_RAM,
		.poff	= CG6_RAM_OFFSET,
		.size	= SBUS_MMAP_FBSIZE(1)
	},
	{
		.voff	= CG6_DHC,
		.poff	= CG6_DHC_OFFSET,
		.size	= 0x40000
	},
	{ .size	= 0 }
};

static int cg6_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct cg6_par *par = (struct cg6_par *)info->par;

	return sbusfb_mmap_helper(cg6_mmap_map,
				  par->physbase, par->fbsize,
				  par->which_io, vma);
}

static int cg6_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct cg6_par *par = (struct cg6_par *)info->par;

	return sbusfb_ioctl_helper(cmd, arg, info,
				   FBTYPE_SUNFAST_COLOR, 8, par->fbsize);
}

/*
 *  Initialisation
 */

static void __devinit cg6_init_fix(struct fb_info *info, int linebytes)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	const char *cg6_cpu_name, *cg6_card_name;
	u32 conf;

	conf = sbus_readl(par->fhc);
	switch (conf & CG6_FHC_CPU_MASK) {
	case CG6_FHC_CPU_SPARC:
		cg6_cpu_name = "sparc";
		break;
	case CG6_FHC_CPU_68020:
		cg6_cpu_name = "68020";
		break;
	default:
		cg6_cpu_name = "i386";
		break;
	};
	if (((conf >> CG6_FHC_REV_SHIFT) & CG6_FHC_REV_MASK) >= 11) {
		if (par->fbsize <= 0x100000)
			cg6_card_name = "TGX";
		else
			cg6_card_name = "TGX+";
	} else {
		if (par->fbsize <= 0x100000)
			cg6_card_name = "GX";
		else
			cg6_card_name = "GX+";
	}

	sprintf(info->fix.id, "%s %s", cg6_card_name, cg6_cpu_name);
	info->fix.id[sizeof(info->fix.id) - 1] = 0;

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_PSEUDOCOLOR;

	info->fix.line_length = linebytes;

	info->fix.accel = FB_ACCEL_SUN_CGSIX;
}

/* Initialize Brooktree DAC */
static void __devinit cg6_bt_init(struct cg6_par *par)
{
	struct bt_regs __iomem *bt = par->bt;

	sbus_writel(0x04 << 24, &bt->addr);	 /* color planes */
	sbus_writel(0xff << 24, &bt->control);
	sbus_writel(0x05 << 24, &bt->addr);
	sbus_writel(0x00 << 24, &bt->control);
	sbus_writel(0x06 << 24, &bt->addr);	 /* overlay plane */
	sbus_writel(0x73 << 24, &bt->control);
	sbus_writel(0x07 << 24, &bt->addr);
	sbus_writel(0x00 << 24, &bt->control);
}

static void __devinit cg6_chip_init(struct fb_info *info)
{
	struct cg6_par *par = (struct cg6_par *)info->par;
	struct cg6_tec __iomem *tec = par->tec;
	struct cg6_fbc __iomem *fbc = par->fbc;
	u32 rev, conf, mode;
	int i;

	/* Turn off stuff in the Transform Engine. */
	sbus_writel(0, &tec->tec_matrix);
	sbus_writel(0, &tec->tec_clip);
	sbus_writel(0, &tec->tec_vdc);

	/* Take care of bugs in old revisions. */
	rev = (sbus_readl(par->fhc) >> CG6_FHC_REV_SHIFT) & CG6_FHC_REV_MASK;
	if (rev < 5) {
		conf = (sbus_readl(par->fhc) & CG6_FHC_RES_MASK) |
			CG6_FHC_CPU_68020 | CG6_FHC_TEST |
			(11 << CG6_FHC_TEST_X_SHIFT) |
			(11 << CG6_FHC_TEST_Y_SHIFT);
		if (rev < 2)
			conf |= CG6_FHC_DST_DISABLE;
		sbus_writel(conf, par->fhc);
	}

	/* Set things in the FBC. Bad things appear to happen if we do
	 * back to back store/loads on the mode register, so copy it
	 * out instead. */
	mode = sbus_readl(&fbc->mode);
	do {
		i = sbus_readl(&fbc->s);
	} while (i & 0x10000000);
	mode &= ~(CG6_FBC_BLIT_MASK | CG6_FBC_MODE_MASK |
		  CG6_FBC_DRAW_MASK | CG6_FBC_BWRITE0_MASK |
		  CG6_FBC_BWRITE1_MASK | CG6_FBC_BREAD_MASK |
		  CG6_FBC_BDISP_MASK);
	mode |= (CG6_FBC_BLIT_SRC | CG6_FBC_MODE_COLOR8 |
		 CG6_FBC_DRAW_RENDER | CG6_FBC_BWRITE0_ENABLE |
		 CG6_FBC_BWRITE1_DISABLE | CG6_FBC_BREAD_0 |
		 CG6_FBC_BDISP_0);
	sbus_writel(mode, &fbc->mode);

	sbus_writel(0, &fbc->clip);
	sbus_writel(0, &fbc->offx);
	sbus_writel(0, &fbc->offy);
	sbus_writel(0, &fbc->clipminx);
	sbus_writel(0, &fbc->clipminy);
	sbus_writel(info->var.xres - 1, &fbc->clipmaxx);
	sbus_writel(info->var.yres - 1, &fbc->clipmaxy);
}

static void cg6_unmap_regs(struct of_device *op, struct fb_info *info,
			   struct cg6_par *par)
{
	if (par->fbc)
		of_iounmap(&op->resource[0], par->fbc, 4096);
	if (par->tec)
		of_iounmap(&op->resource[0], par->tec, sizeof(struct cg6_tec));
	if (par->thc)
		of_iounmap(&op->resource[0], par->thc, sizeof(struct cg6_thc));
	if (par->bt)
		of_iounmap(&op->resource[0], par->bt, sizeof(struct bt_regs));
	if (par->fhc)
		of_iounmap(&op->resource[0], par->fhc, sizeof(u32));

	if (info->screen_base)
		of_iounmap(&op->resource[0], info->screen_base, par->fbsize);
}

static int __devinit cg6_probe(struct of_device *op,
				const struct of_device_id *match)
{
	struct device_node *dp = op->node;
	struct fb_info *info;
	struct cg6_par *par;
	int linebytes, err;
	int dblbuf;

	info = framebuffer_alloc(sizeof(struct cg6_par), &op->dev);

	err = -ENOMEM;
	if (!info)
		goto out_err;
	par = info->par;

	spin_lock_init(&par->lock);

	par->physbase = op->resource[0].start;
	par->which_io = op->resource[0].flags & IORESOURCE_BITS;

	sbusfb_fill_var(&info->var, dp->node, 8);
	info->var.red.length = 8;
	info->var.green.length = 8;
	info->var.blue.length = 8;

	linebytes = of_getintprop_default(dp, "linebytes",
					  info->var.xres);
	par->fbsize = PAGE_ALIGN(linebytes * info->var.yres);

	dblbuf = of_getintprop_default(dp, "dblbuf", 0);
	if (dblbuf)
		par->fbsize *= 4;

	par->fbc = of_ioremap(&op->resource[0], CG6_FBC_OFFSET,
				4096, "cgsix fbc");
	par->tec = of_ioremap(&op->resource[0], CG6_TEC_OFFSET,
				sizeof(struct cg6_tec), "cgsix tec");
	par->thc = of_ioremap(&op->resource[0], CG6_THC_OFFSET,
				sizeof(struct cg6_thc), "cgsix thc");
	par->bt = of_ioremap(&op->resource[0], CG6_BROOKTREE_OFFSET,
				sizeof(struct bt_regs), "cgsix dac");
	par->fhc = of_ioremap(&op->resource[0], CG6_FHC_OFFSET,
				sizeof(u32), "cgsix fhc");

	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_IMAGEBLIT |
			FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
			FBINFO_READS_FAST;
	info->fbops = &cg6_ops;

	info->screen_base = of_ioremap(&op->resource[0], CG6_RAM_OFFSET,
					par->fbsize, "cgsix ram");
	if (!par->fbc || !par->tec || !par->thc ||
	    !par->bt || !par->fhc || !info->screen_base)
		goto out_unmap_regs;

	info->var.accel_flags = FB_ACCELF_TEXT;

	cg6_bt_init(par);
	cg6_chip_init(info);
	cg6_blank(0, info);

	if (fb_alloc_cmap(&info->cmap, 256, 0))
		goto out_unmap_regs;

	fb_set_cmap(&info->cmap, info);
	cg6_init_fix(info, linebytes);

	err = register_framebuffer(info);
	if (err < 0)
		goto out_dealloc_cmap;

	dev_set_drvdata(&op->dev, info);

	printk(KERN_INFO "%s: CGsix [%s] at %lx:%lx\n",
	       dp->full_name, info->fix.id,
	       par->which_io, par->physbase);

	return 0;

out_dealloc_cmap:
	fb_dealloc_cmap(&info->cmap);

out_unmap_regs:
	cg6_unmap_regs(op, info, par);

out_err:
	return err;
}

static int __devexit cg6_remove(struct of_device *op)
{
	struct fb_info *info = dev_get_drvdata(&op->dev);
	struct cg6_par *par = info->par;

	unregister_framebuffer(info);
	fb_dealloc_cmap(&info->cmap);

	cg6_unmap_regs(op, info, par);

	framebuffer_release(info);

	dev_set_drvdata(&op->dev, NULL);

	return 0;
}

static struct of_device_id cg6_match[] = {
	{
		.name = "cgsix",
	},
	{
		.name = "cgthree+",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cg6_match);

static struct of_platform_driver cg6_driver = {
	.name		= "cg6",
	.match_table	= cg6_match,
	.probe		= cg6_probe,
	.remove		= __devexit_p(cg6_remove),
};

static int __init cg6_init(void)
{
	if (fb_get_options("cg6fb", NULL))
		return -ENODEV;

	return of_register_driver(&cg6_driver, &of_bus_type);
}

static void __exit cg6_exit(void)
{
	of_unregister_driver(&cg6_driver);
}

module_init(cg6_init);
module_exit(cg6_exit);

MODULE_DESCRIPTION("framebuffer driver for CGsix chipsets");
MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
